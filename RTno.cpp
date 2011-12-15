/*******************************************
 * RTno.cpp
 * @author Yuki Suga
 * @copyright Yuki Suga (ysuga.net) Nov, 10th, 2010.
 * @license LGPLv3
 *****************************************/
#include <Arduino.h>

#include "RTno.h"
#include "Packet.h"

#include "UARTTransport.h"
#include "RTnoProfile.h"

using namespace RTno;

// global variables
config_str conf;
exec_cxt_str exec_cxt;

// module private variables.
#define PRIVATE static

PRIVATE Transport *m_pTransport;
PRIVATE char m_Condition = CREATED;
PRIVATE char m_pPacketBuffer[PACKET_BUFFER_SIZE];
PRIVATE RTnoProfile m_Profile;

/*
 * Send Profile Data
 */
PRIVATE void _SendProfile();

/**
 * Packet Handler in Error State.
 */
PRIVATE void _PacketHandlerOnError();

/**
 * Packet Handler in Inactive State.
 */
PRIVATE void _PacketHandlerOnInactive();

/**
 * Packet Handler in Active State.
 */
PRIVATE void _PacketHandlerOnActive();


/**
 * Arduino Setup Routine.
 * This function is called when arduino device is turned on.
 */
void setup() {

  // This function must be called first.
  rtcconf();

  switch(conf._default.connection_type) {
  case ConnectionTypeSerial1:
    m_pTransport = new UARTTransport(1, conf._default.baudrate);
    break;
  case ConnectionTypeSerial2:
    m_pTransport = new UARTTransport(2, conf._default.baudrate);
    break;
  case ConnectionTypeSerial3:
    m_pTransport = new UARTTransport(3, conf._default.baudrate);
    break;
  default:
    return;
  }
  
  if(onInitialize() == RTC_OK) {
    m_Condition = INACTIVE;
  } else {
    m_Condition = NONE;
  }
}


/**
 * Arduino Loop routine.
 * This function is repeadedly called when arduino is turned on.
 */
void loop() {
  char ret;
  ret = m_pTransport->ReceivePacket((unsigned char*)m_pPacketBuffer);

  if(ret < 0) { // Timeout Error or Checksum Error
    m_pTransport->SendPacket(PACKET_ERROR, 1, &ret);
  } else if (ret > 0) { // Packet is successfully received
    if (m_pPacketBuffer[INTERFACE] == GET_PROFILE) {
      _SendProfile();
    } else if(m_pPacketBuffer[INTERFACE] == GET_STATUS) {
      m_pTransport->SendPacket(GET_STATUS, 1, &m_Condition);
    } else if(m_pPacketBuffer[INTERFACE] == GET_CONTEXT) {
      m_pTransport->SendPacket(GET_CONTEXT, 1, (char*)&(exec_cxt.periodic.type));
    } else {
      switch (m_Condition) {
      case ERROR:
	_PacketHandlerOnError();
	break;
      case INACTIVE:
	_PacketHandlerOnInactive();
	break;
      case ACTIVE:
	_PacketHandlerOnActive();
	break;
      case NONE:
	ret = RTNO_NONE;
	m_pTransport->SendPacket(m_pPacketBuffer[INTERFACE], 1, &ret);
	break;
      default: // if m_Condition is unknown...
	m_Condition = ERROR;
	break;
      }
    }
  }

  int numOutPort = m_Profile.getNumOutPort();
  for(int i = 0;i < numOutPort;i++) {
    OutPort* pOutPort = m_Profile.getOutPort(i);
    if(pOutPort->hasNext()) {
      char* name = pOutPort->GetName();
      unsigned char nameLen = strlen(name);
      

      unsigned char dataLen = pOutPort->getNextDataSize();


      m_pPacketBuffer[0] = nameLen;
      m_pPacketBuffer[1] = dataLen;
      memcpy(m_pPacketBuffer + 2, name, nameLen);
      pOutPort->pop(m_pPacketBuffer + 2 + nameLen, dataLen);
      m_pTransport->SendPacket(RECEIVE_DATA, 2 + nameLen + dataLen, m_pPacketBuffer);
    }

  }
  
}

/**
 * add InPort data to Profile.
 */
void addInPort(InPort& Port)
{
  m_Profile.addInPort(Port);
}

/**
 * add OutPort data to Profile
 */
void addOutPort(OutPort& Port)
{
  m_Profile.addOutPort(Port);
}


/**
 * Private Function Definitions
 *
 */


/**
 * Send Profile Data
 */
PRIVATE void _SendProfile() {
  char ret = RTNO_OK;
  for(int i = 0;i < m_Profile.getNumInPort();i++) {
    InPort* inPort = m_Profile.getInPort(i);
    int typeCode = inPort->GetTypeCode();
    int nameLen = strlen(inPort->GetName());
    m_pPacketBuffer[0] = typeCode;
    memcpy(&(m_pPacketBuffer[1]), inPort->GetName(), nameLen);
    m_pTransport->SendPacket(ADD_INPORT, 1+nameLen, m_pPacketBuffer);
  }

  for(int i = 0;i < m_Profile.getNumOutPort();i++) {
    OutPort* outPort = m_Profile.getOutPort(i);
    int typeCode = outPort->GetTypeCode();
    int nameLen = strlen(outPort->GetName());
    m_pPacketBuffer[0] = typeCode;
    memcpy(&(m_pPacketBuffer[1]), outPort->GetName(), nameLen);
    m_pTransport->SendPacket(ADD_OUTPORT, 1+nameLen, m_pPacketBuffer);
  }

  m_pTransport->SendPacket(GET_PROFILE, 1, &ret);
}



/**
 * Packet Handler in Error State
 */
PRIVATE void _PacketHandlerOnError() {
  char intface;
  char ret = RTNO_ERROR;
  if(m_pPacketBuffer[INTERFACE] == ONERROR) {
    if(exec_cxt.periodic.type == ProxySynchronousExecutionContext) {
      onError();
      intface = ONERROR;
      ret = RTNO_OK;
    }
  } else if(m_pPacketBuffer[INTERFACE] == RESET) {
    intface = RESET;
    if(onReset() == RTC_OK) {
      m_Condition = INACTIVE;
      ret = RTNO_OK;

    } else {
      m_Condition = ERROR;
    }

  }
  m_pTransport->SendPacket(intface, 1, &ret);
}


/** 
 * Packet Handler in Inactive State
 */
PRIVATE void _PacketHandlerOnInactive() {
  char ret = RTNO_ERROR;
  if(m_pPacketBuffer[INTERFACE] == ACTIVATE) {
    if(onActivated() == RTC_OK) {
      m_Condition = ACTIVE;
      ret = RTNO_OK;
    } else {
      m_Condition = ERROR;
    }
    m_pTransport->SendPacket(ACTIVATE, 1, &ret);
  }

}

/**
 * Packet Handler in Active State.
 */
PRIVATE void _PacketHandlerOnActive() {
  char ret = RTNO_ERROR;
  char intface;
  switch(m_pPacketBuffer[INTERFACE]) {
  case DEACTIVATE:
    intface = DEACTIVATE;
    onDeactivated();
    m_Condition = INACTIVE;
    ret = RTNO_OK;
    m_pTransport->SendPacket(intface, 1, &ret);
    break;
  case EXECUTE:
    intface = EXECUTE;
    if(exec_cxt.periodic.type == ProxySynchronousExecutionContext) {
      if(onExecute() == RTC_OK) {
	ret = RTNO_OK;
      } else {
	m_Condition = ERROR;
      }

    }
    m_pTransport->SendPacket(intface, 1, &ret);
    break;
  case SEND_DATA:
    intface = SEND_DATA;
    {
      InPort* pInPort = m_Profile.getInPort(
					  &(m_pPacketBuffer[DATA_START_ADDR+2]),
					  m_pPacketBuffer[DATA_START_ADDR]);
      if(pInPort == NULL) {
      } else {
	pInPort->push(&(m_pPacketBuffer[DATA_START_ADDR+2+m_pPacketBuffer[DATA_START_ADDR]]), m_pPacketBuffer[DATA_START_ADDR+1]);
	ret = RTNO_OK;
      }
    }
    break;
  default:
    break;
  }

}

