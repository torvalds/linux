//***************************************************************************
//!file     si_cbus_component.h
//!brief    Silicon Image CBUS Component.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2002-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_CBUS_COMPONENT_H__
#define __SI_CBUS_COMPONENT_H__

#include "si_common.h"
#include "si_cbus_enums.h"

//------------------------------------------------------------------------------
//  Manifest Constants
//------------------------------------------------------------------------------

#define     MHL_DEVCAP_SIZE                 16
#define     MHL_INTERRUPT_SIZE              4
#define     MHL_STATUS_SIZE                 4
#define     MHL_SCRATCHPAD_SIZE             16
#define     MHL_MAX_BUFFER_SIZE             MHL_SCRATCHPAD_SIZE // manually define highest number

#define CBUS_MAX_COMMAND_QUEUE      6
#define CBUS_MAX_BURST_QUEUE        6

#define CBUS_BURST_WAIT_TIMER       1200        //This timer will be useless after enable HAWB
#define CBUS_ABORT_TIMER			2000
#define CBUS_DCAP_READY_TIMER       1000
//------------------------------------------------------------------------------
//  CBUS Component typedefs
//------------------------------------------------------------------------------

//
// structure to hold command details from upper layer to CBUS module
//
typedef struct
{
    uint8_t retry;        		            // retry times
    uint8_t reqStatus;                      // CBUS_IDLE, CBUS_PENDING
    uint8_t command;                        // VS_CMD or RCP opcode
    uint8_t offsetData;                     // Offset of register on CBUS or RCP data
    uint8_t length;                         // Only applicable to write burst. ignored otherwise.
    uint8_t msgData[MHL_MAX_BUFFER_SIZE];   // Pointer to message data area.
    uint8_t retData[2];                     // Pointer to read back message data area.
	uint8_t	*pdatabytes;					// pointer for write burst or read many bytes
} cbus_req_t;

typedef struct
{
    bool_t arrived;                         // CBUS message is arrived
    uint8_t command;                        // VS_CMD or RCP opcode
    uint8_t offsetData;                     // Offset of register on CBUS or RCP data
} cbus_rev_t;

typedef struct
{
    uint8_t retry;        		            // retry times
    uint8_t burstStatus;
    uint8_t offset;
    uint8_t length;
    uint8_t burstData[MHL_MAX_BUFFER_SIZE];
} cbus_burst_t;

typedef struct
{
    bool_t connected;      		// True if a connected MHL port
    bool_t dcap_ready;          // device capability ready
    bool_t dcap_ongoing;        // True if read dcap is on going
    uint8_t remote_dcap[MHL_DEVCAP_SIZE];      // cached remote dcap registers
    uint8_t state;          		// State of command execution for this channel
    uint8_t activeIndex;    		// Active queue entry for req.
    uint8_t activeBurst;
#if defined(__KERNEL__)
    SiiOsTimer_t abortTimer;
    SiiOsTimer_t burstTimer;
    SiiOsTimer_t dcapTimer;
#else
	clock_time_t	abortTimer;
	clock_time_t	burstTimer;
	clock_time_t	dcapTimer;
#endif
    bool_t abortState;
    bool_t burstWaitState;
    cbus_burst_t    burst[ CBUS_MAX_BURST_QUEUE ];
    cbus_req_t      request[ CBUS_MAX_COMMAND_QUEUE ];
    cbus_rev_t      receive;
} cbusChannelState_t;



typedef struct
{
    cbusChannelState_t  chState;
} CbusInstanceData_t;


//------------------------------------------------------------------------------
//  Standard component functions
//-------------------------------------------------------------
bool_t      SiiMhlRxInitialize( void );

//------------------------------------------------------------------------------
//  Component Specific functions
//------------------------------------------------------------------------------

uint8_t     SiiMhlRxIntrHandler( void );

uint8_t     SiiCbusRequestStatus( void );
void        SiiCbusRequestSetIdle( uint8_t newState );

bool_t      SiiMhlRxIsQueueFull ( void );
bool_t      SiiMhlRxIsQueueEmpty ( void );

uint8_t     SiiCbusChannelStatus(void);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxCbusConnected
// Description: Return the CBUS channel connected status for this channel.
// Returns:     true if connected.
//              false if disconnected.
//------------------------------------------------------------------------------

bool_t SiiMhlRxCbusConnected(void);

//------------------------------------------------------------------------------
// Function:    SiiCbusRequestDataGet
// Description: Return a copy of the currently active request structure
// Parameters:  pCmdRequest
// Returns:     none
//------------------------------------------------------------------------------

void        SiiCbusRequestDataGet( cbus_req_t *pCmdRequest );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRAPCmd
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- RAP action code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRAPCmd ( uint8_t actCode );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRCPCmd
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- RCP key code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRCPCmd ( uint8_t keyCode );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUCPCmd
// Description: Send MSC_MSG (UCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- UCP key code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendUCPCmd ( uint8_t keyCode );

//------------------------------------------------------------------------------
// Function:    SiiCbusMscMsgSubCmdSend
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  vsCommand   - MSC_MSG cmd (RCP, RCPK or RCPE)
//              cmdData     - MSC_MSG data
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiCbusSendMscMsgCmd ( uint8_t subCmd, uint8_t mscData );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRcpk
// Description: Send RCPK (ack) message
//
// Parameters:  keyCode
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendRcpk ( uint8_t keyCode);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRcpe
// Description: Send RCPE (error) message
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRcpe ( uint8_t cmdStatus );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRapk
// Description: Send RAPK (acknowledge) message to the specified CBUS channel
//              and set the request status to idle.
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendRapk ( uint8_t cmdStatus );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUcpk
// Description: Send UCPK (acknowledge) message to the specified CBUS channel
//              and set the request status to idle.
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendUcpk ( uint8_t cmdStatus );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUcpe
// Description: Send UCPE (error) message
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendUcpe ( uint8_t cmdStatus );

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendMsge
// Description: Send MSGE msg back if the MSC command received is not recognized
//
// Returns:     true        - successful
//              false       - failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendMsge (uint8_t opcode);
bool_t SiiCbusWriteCommand( cbus_req_t *pReq );
bool_t SiiCbusWriteStatus ( uint8_t regOffset, uint8_t value );
bool_t SiiCbusSetInt ( uint8_t regOffset, uint8_t regBit );
bool_t SiiCbusWriteBurst(void);

//------------------------------------------------------------------------------
// Function:    SiMhlRxSendEdidChange
//------------------------------------------------------------------------------
bool_t SiMhlRxSendEdidChange (void);

//------------------------------------------------------------------------------
// Function:    SiiGrtWrt
//------------------------------------------------------------------------------
bool_t SiiCbusGrtWrt (void);

//------------------------------------------------------------------------------
// Function:    SiiReqWrt
//------------------------------------------------------------------------------
bool_t SiiCbusReqWrt (void);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendDcapChange
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendDcapChange (void);

//------------------------------------------------------------------------------
// Function:    SiiDscrChange
//------------------------------------------------------------------------------
bool_t SiiCbusSendDscrChange (void);

//------------------------------------------------------------------------------
// Function:    SiiSendDcapRdy
//------------------------------------------------------------------------------
bool_t SiiCbusSendDcapRdy (void);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxPathEnable
// Description: Check if the channel is an active channel
//------------------------------------------------------------------------------
bool_t SiiMhlRxPathEnable  (bool_t enable);

//------------------------------------------------------------------------------
// Function:    SiiCbusSendMscCommand
// Description: sends general MSC commands
//------------------------------------------------------------------------------
bool_t SiiCbusSendMscCommand(uint8_t cmd);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxWrtPeersScratchpad
// Description: sends MHL write burst cmd
//------------------------------------------------------------------------------
bool_t SiiCbusWritePeersScratchpad(uint8_t startOffset, uint8_t length, uint8_t* pMsgData);

//------------------------------------------------------------------------------
// Function:    SiiReadDevCapReg
// Description: Read device capability register
//------------------------------------------------------------------------------
bool_t SiiMhlRxReadDevCapReg(uint8_t regOffset);

//------------------------------------------------------------------------------
// Function:    SiMhlRxHpdSet
// Description: Send MHL_SET_HPD to source
// parameters:	setHpd - true/false
//------------------------------------------------------------------------------
bool_t SiMhlRxHpdSet (bool_t setHpd);

//------------------------------------------------------------------------------
// Function:    SiMhlRxMscCmdRetDataNtfy
// Description: Response data received from peer in response to an MSC command
//------------------------------------------------------------------------------
void SiMhlRxMscCmdRetDataNtfy (uint8_t mscData);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxConnNtfy
// Description: This is a notification API for Cbus connection change, prototype
//				is defined in si_cbus_component.h
//------------------------------------------------------------------------------
void SiiMhlRxConnNtfy(bool_t connected);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxScratchpadWrittenNtfy
// Description: This is a notification API for scratchpad bein written by peer
//------------------------------------------------------------------------------
void SiiMhlRxScratchpadWrittenNtfy(void);

//------------------------------------------------------------------------------
// Function:    SiiMhlRxRcpRapRcvdNtfy
// Description: process RCP/RAP msg
//------------------------------------------------------------------------------
void SiiMhlRxRcpRapRcvdNtfy( uint8_t cmd, uint8_t rcvdCode);

#if !defined(__KERNEL__)
void SiiCbuschkTimers (void);
#else
//------------------------------------------------------------------------------
// Function:    SiiCbusAbortTimerStart
//------------------------------------------------------------------------------
void SiiCbusAbortTimerStart (void);
#endif

//------------------------------------------------------------------------------
// Function:    SiiCbusAbortStateSet
//------------------------------------------------------------------------------
void SiiCbusAbortStateSet (bool_t value);

//------------------------------------------------------------------------------
// Function:    SiiCbusAbortStateGet
//------------------------------------------------------------------------------
bool_t SiiCbusAbortStateGet (void);

uint8_t SiiCbusRemoteDcapGet(uint8_t offset);

#endif // __SI_CBUS_COMPONENT_H__
