//***************************************************************************
//!file     si_cbus_component.c
//!brief    Silicon Image CBUS Component.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2011-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/


#include "si_common.h"
#include "si_cbus_component.h"
#include "si_drv_cbus.h"
#include "si_drv_cra_cfg.h"
#include "si_cra.h"
#include "si_drv_rx.h"
#ifdef MHAWB_SUPPORT
#include "si_drv_hawb.h"
#endif

//------------------------------------------------------------------------------
//  CBUS Component Instance Data
//------------------------------------------------------------------------------

#define CH_ACTIVE_INDEX     (pCbus->chState.activeIndex)
#define CH_ACTIVE_BURST     (pCbus->chState.activeBurst)

//------------------------------------------------------------------------------
//  CBUS Component Instance Data
//------------------------------------------------------------------------------

CbusInstanceData_t cbusInstance;
CbusInstanceData_t *pCbus = &cbusInstance;

//------------------------------------------------------------------------------
// Function:    SiiCbusRequestStatus
// Description: Return the status of the message currently in process, if any.
// Returns:     CBUS_REQ_IDLE, CBUS_REQ_PENDING, CBUS_REQ_SENT, or CBUS_REQ_RECEIVED
//------------------------------------------------------------------------------

uint8_t SiiCbusRequestStatus ()
{
    return( pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus );
}

//------------------------------------------------------------------------------
// Function:    SiiCbusChannelStatus
// Description: Return the status of the current channel.
// Parameters:  channel - CBUS channel to check
// Returns:     CBUS_IDLE,
//              CBUS_SENT,
//              CBUS_XFR_DONE,
//              CBUS_WAIT_RESPONSE,
//              CBUS_RECEIVED
//------------------------------------------------------------------------------

uint8_t SiiCbusChannelStatus ()
{
    return( pCbus->chState.state );
}

//------------------------------------------------------------------------------
// Function:    SiiCbusRequestSetIdle
// Description: Set the active request to the specified state
// Parameters:  newState - new CBus State
//------------------------------------------------------------------------------

void SiiCbusRequestSetIdle ( uint8_t newState )
{
    pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus = newState;
}

//------------------------------------------------------------------------------
// Function:    SiiCbusRequestDataGet
// Description: Return a copy of the currently active request structure
// Parameters:  pCmdRequest
// Returns:     none
//------------------------------------------------------------------------------

void SiiCbusRequestDataGet ( cbus_req_t *pCmdRequest )
{

    memcpy( pCmdRequest, &pCbus->chState.request[ CH_ACTIVE_INDEX ], sizeof( cbus_req_t ));
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxCbusConnected
// Description: Return the CBUS channel connected status for this channel.
// Returns:     true if connected.
//              false if disconnected.
//------------------------------------------------------------------------------

bool_t SiiMhlRxCbusConnected ()
{
    return( pCbus->chState.connected );
}

//------------------------------------------------------------------------------
// Function:    CBusSendNextInQueue
// Description: Starting at the current active index, send the next pending
//              entry, if any
//------------------------------------------------------------------------------

static int_t CBusSendNextInQueue (void)
{
    int_t   result = SUCCESS;    

    if (SiiCbusAbortStateGet())
    {
        return( SUCCESS );
    }

    if ( ( pCbus->chState.request[ CH_ACTIVE_INDEX].reqStatus != CBUS_REQ_PENDING ) )
    {
#ifdef MHAWB_SUPPORT
        if (pCbus->chState.connected && (!pCbus->chState.burstWaitState))
        {
            if (pCbus->chState.remote_dcap[0x0A] & MHL_FEATURE_SP_SUPPORT)
                SiiDrvHawbEnable(true); //There's no pending CBUS message need to send out, take HAWB ON
        }
#endif
	    return( SUCCESS );
    }

#ifdef MHAWB_SUPPORT
    if (SiiDrvHawbEnable(false))
#endif
    {
        // Found a pending message, send it out
        if ( SiiDrvInternalCBusWriteCommand( &pCbus->chState.request[ CH_ACTIVE_INDEX] ))
        {
            pCbus->chState.state = CBUS_SENT;
            pCbus->chState.request[ CH_ACTIVE_INDEX].reqStatus = CBUS_REQ_SENT;
        }
        else
        {
            result = ERROR_WRITE_FAILED;
        }
    }
    return( result );
}

//------------------------------------------------------------------------------
// Function:    CBusProcessSubCommand
// Description: Process a sub-command (RCP) or sub-command response (RCPK).
//              Modifies channel state as necessary.
// Returns:     SUCCESS or CBUS_SOFTWARE_ERRORS_t
//              If SUCCESS, command data is returned in pCbus->chState.msgData[i]
//------------------------------------------------------------------------------

static uint8_t CBusProcessSubCommand (uint8_t* pData)
{
	uint8_t vsCmdData[2];

	vsCmdData[0] = *pData++;
	vsCmdData[1] = *pData;

    // Save RCP message data in the channel receive structure to be returned
    // to the upper level.
    pCbus->chState.receive.arrived = true;
    pCbus->chState.receive.command    = vsCmdData[0];
    pCbus->chState.receive.offsetData = vsCmdData[1];

    DEBUG_PRINT( MSG_DBG, "CBUS:: MSG_MSC CMD:  0x%02X  MSG_MSC Data: 0x%02X\n", (int)vsCmdData[0], (int)vsCmdData[1] );

    return( SUCCESS );
}

//------------------------------------------------------------------------------
// Function:    CBusResetToIdle
// Description: Set the specified channel state to IDLE. Clears any messages that
//              are in progress or queued.  Usually used if a channel connection
//              changed or the channel heartbeat has been lost.
//------------------------------------------------------------------------------

static void CBusResetToIdle (void)
{
    SiiMhlRxInitialize();
}


bool_t SiiMhlRxIsQueueFull ( void )
{
    uint8_t   queueIndex;

    for ( queueIndex = 0; queueIndex < CBUS_MAX_COMMAND_QUEUE; queueIndex++ )
    {
        if ( pCbus->chState.request[ queueIndex].reqStatus == CBUS_REQ_IDLE )
        {
            return false;
        }
    }

    return true;
}

bool_t SiiMhlRxIsQueueEmpty ( void )
{
    uint8_t   queueIndex;

    for ( queueIndex = 0; queueIndex < CBUS_MAX_COMMAND_QUEUE; queueIndex++ )
    {
        if ( pCbus->chState.request[ queueIndex].reqStatus == CBUS_REQ_PENDING )
        {
            return false;
        }
    }

    return true;
}

void SiiMhlRxSetQueueEmpty( void )
{
    uint8_t   queueIndex;

    for ( queueIndex = 0; queueIndex < CBUS_MAX_COMMAND_QUEUE; queueIndex++ )
    {
        pCbus->chState.request[ queueIndex].reqStatus = CBUS_REQ_IDLE;
    }
}

//------------------------------------------------------------------------------
// Function:    SiiCbusWriteCommand
// Description: Place a command in the CBUS message queue.  If queue was empty,
//              send the new command immediately.
//
// Parameters:  pReq    - Pointer to a cbus_req_t structure containing the
//                        command to write
// Returns:     true    - successful queue/write
//              false   - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiCbusWriteCommand ( cbus_req_t *pReq  )
{
    uint8_t   queueIndex, i;
    bool_t  success = false;

    /* Copy the request to the queue.   */

    if( SiiMhlRxCbusConnected() )
    {
        //In Abort state, discard any cbus command need to transmit. 
        if (SiiCbusAbortStateGet() == false)
        {
    		//DEBUG_PRINT( MSG_DBG, "CBUS:: SiiCbusWriteCommand:: Channel State: %02X", (int)pCbus->chState[ channel].state );
    		for ( i = 0; i < CBUS_MAX_COMMAND_QUEUE; i++ )
    		{
                queueIndex = (CH_ACTIVE_INDEX + i) % CBUS_MAX_COMMAND_QUEUE;
    			if ( pCbus->chState.request[ queueIndex].reqStatus == CBUS_REQ_IDLE )
    			{
    				// Found an idle queue entry, copy the request and set to pending.

    				memcpy( &pCbus->chState.request[ queueIndex], pReq, sizeof( cbus_req_t ));
    				pCbus->chState.request[ queueIndex].reqStatus = CBUS_REQ_PENDING;
                    pCbus->chState.request[ queueIndex].retry = 1;
    				success = true;
    				break;
    			}
    		}
            if (i == CBUS_MAX_COMMAND_QUEUE)
            {
                DEBUG_PRINT(
    				MSG_ERR,
    				"CBUS:: Queue full - Request0: %02X Request1: %02X",
    				(int)pCbus->chState.request[ 0].reqStatus,
    				(int)pCbus->chState.request[ 1].reqStatus
    				);
            }
        }
#if 0
		/* If successful at putting the request into the queue, decide  */
		/* whether it can be sent now or later.                         */

		if ( success )
		{
			switch ( pCbus->chState.state )
			{
				case CBUS_IDLE:
				//case CBUS_RECEIVED:

				//DEBUG_PRINT( MSG_DBG, "CBUS:: SiiCbusWriteCommand:: calling CBusSendNextInQueue!!" );
				if (CBusSendNextInQueue() == SUCCESS )   // No command in progress, write new command immediately.
				{
					success = true;
				}
				break;

				//case CBUS_WAIT_RESPONSE:
				case CBUS_SENT:
				case CBUS_XFR_DONE:

					/* Another command is in progress, the Handler loop will    */
					/* send the new command when the bus is free.               */
					/*
					DEBUG_PRINT( MSG_ERR, "CBUS:: ERROR!!! This msg should never happen, Channel State: %02X",
							(int)pCbus->chState.state );
					*/
					break;

				default:

					/* Illegal values return to IDLE state.     */

					DEBUG_PRINT( MSG_ERR, "CBUS:: Channel State: %02X (illegal)", (int)pCbus->chState.state );
					pCbus->chState.state = CBUS_IDLE;
					pCbus->chState.request[ queueIndex].reqStatus = CBUS_REQ_IDLE;
					success = false;
					break;
			}
		}
#endif
    }
    else
    {
    	DEBUG_PRINT( MSG_DBG, "CBus is not connected yet! MHL command could not be sent!" );
    }

    return( success );
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRAPCmd
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- RAP action code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRAPCmd ( uint8_t actCode )
{
    return SiiCbusSendMscMsgCmd(MHL_MSC_MSG_RAP, actCode);
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRCPCmd
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- RCP key code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRCPCmd ( uint8_t keyCode )
{
    return SiiCbusSendMscMsgCmd(MHL_MSC_MSG_RCP, keyCode);
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUCPCmd
// Description: Send MSC_MSG (UCP) message to the specified CBUS channel (port)
//
// Parameters:  keyCode 	- UCP key code
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendUCPCmd ( uint8_t keyCode )
{
    return SiiCbusSendMscMsgCmd(MHL_MSC_MSG_UCP, keyCode);
}

//------------------------------------------------------------------------------
// Function:    SiiCbusMscMsgSubCmdSend
// Description: Send MSC_MSG (RCP) message to the specified CBUS channel (port)
//
// Parameters:  vsCommand   - MSC_MSG cmd (RCP, RCPK or RCPE)
//              cmdData     - MSC_MSG data
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiCbusSendMscMsgCmd ( uint8_t subCmd, uint8_t mscData )
{
    cbus_req_t req;

    // Send MSC_MSG command (Vendor Specific command)

    req.command = MHL_MSC_MSG;
    req.msgData[0] = subCmd;
    req.msgData[1] = mscData;
    if (!(SiiCbusWriteCommand(&req)))
    {
        DEBUG_PRINT( MSG_ERR, "Couldn't send MHL_MSC_MSG to peer");
        return false;
    }

    if (pCbus->chState.state == CBUS_IDLE)
    {
        CBusSendNextInQueue();     // No command in progress, write new command immediately.
    }
    return true;
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRcpk
// Description: Send RCPK (ack) message
//
// Parameters:  keyCode
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendRcpk ( uint8_t keyCode)
{

    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_RCPK, keyCode ));
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRcpe
// Description: Send RCPE (error) message
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendRcpe ( uint8_t cmdStatus )
    {
    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_RCPE, cmdStatus ));
    }

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendRapk
// Description: Send RAPK (acknowledge) message to the specified CBUS channel
//              and set the request status to idle.
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendRapk ( uint8_t cmdStatus )
{

    //SiiCbusRequestSetIdle( CBUS_REQ_IDLE );
    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_RAPK, cmdStatus ));
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUcpk
// Description: Send UCPK (ack) message
//
// Parameters:  keyCode
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendUcpk ( uint8_t keyCode)
{
    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_UCPK, keyCode ));
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendUcpe
// Description: Send UCPE (error) message
//
// Parameters:  cmdStatus
// Returns:     true        - successful queue/write
//              false       - write and/or queue failed
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendUcpe ( uint8_t cmdStatus )
{
    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_UCPE, cmdStatus ));
}


//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendMsge
// Description: Send MSGE msg back if the MSC command received is not recognized
//
// Returns:     true        - successful
//              false       - failed
//------------------------------------------------------------------------------

bool_t SiiMhlRxSendMsge (uint8_t opcode)
{
    //SiiCbusRequestSetIdle( CBUS_REQ_IDLE );
    return( SiiCbusSendMscMsgCmd( MHL_MSC_MSG_E, opcode ));
    }

//------------------------------------------------------------------------------
// Function:    SiMhlRxHpdSet
// Description: Send MHL_SET_HPD to source
// parameters:	setHpd - true/false
//------------------------------------------------------------------------------
bool_t SiMhlRxHpdSet (bool_t setHpd)
    {
	bool_t retValue = false;
	if( setHpd )
    {
        if (SiiCbusSendMscCommand( MHL_SET_HPD ))
        {
		    retValue = SiiMhlRxPathEnable(true);
        }
    }

	else
    {
        if (SiiMhlRxPathEnable(false))
        {
		    retValue = SiiCbusSendMscCommand( MHL_CLR_HPD );
        }
    }

	return retValue;
}

//------------------------------------------------------------------------------
// Function:    CBusCheckInterruptStatus
// Description: If any interrupts on the specified channel are set, process them.
// Returns:     SUCCESS or CBUS_SOFTWARE_ERRORS_t error code.
//------------------------------------------------------------------------------
static uint8_t CBusCheckInterruptStatus ( void )
{
    uint8_t result;
    uint8_t busStatus;
    uint8_t	temp;
    uint8_t cbusData[2];

    result = SUCCESS;

    if ( SiiDrvCbusIntrFlagGet() )
    {
        //CBUS connction change
        if ( SiiDrvCbusBusStatusGet( &busStatus ) )
        {
        	/* The connection change interrupt has been received.   */
			pCbus->chState.connected = busStatus ? true : false;
        	DEBUG_PRINT( MSG_DBG, "CBUS:: ----Connection Change---- %s \n", pCbus->chState.connected ? "Connected" : "Disconnected" );
			if( pCbus->chState.connected )
			{
				SiiDrvCbusTermCtrl( true );
                if ( SiiCbusSendDcapRdy())
                {
                    //set DCAP_CHG bit
                    if(SiiMhlRxSendDcapChange())
                    {
                        // send set_hpd and set path_en bit too
                        SiMhlRxHpdSet(true);
                    }
                }
#if defined(__KERNEL__)
                SiiOsTimerStart(pCbus->chState.dcapTimer, CBUS_DCAP_READY_TIMER);
#else
                pCbus->chState.dcapTimer = SiiTimerTotalElapsed();
#endif
			}
			else
			{
				SiiDrvCbusTermCtrl( false );       
				//set the cbus to idle
				CBusResetToIdle();
			}

			SiiMhlRxConnNtfy(pCbus->chState.connected);
        }

        // Receive CBUS messages
        if ( SiiDrvCbusVsDataGet( &cbusData[0] ) )
        {
            CBusProcessSubCommand( &cbusData[0] );
        }

        //Cbus message transfer done
        if ( SiiDrvCbusCmdRetDataGet( &pCbus->chState.request[ CH_ACTIVE_INDEX ].retData[0] ) )
        {
            if (pCbus->chState.state == CBUS_SENT)
                pCbus->chState.state = CBUS_XFR_DONE;
        }

        //Cbus Devcap ready
        if (SiiDrvCbusDevCapReadyGet ())
        {
            if (!pCbus->chState.dcap_ready)
            {
                DEBUG_PRINT( MSG_DBG, "CBUS:: DCap Ready received!\n");
#if defined(__KERNEL__)
                SiiOsTimerStop(pCbus->chState.dcapTimer);
#endif
                pCbus->chState.dcap_ready = true;
                // Read Dcap
                SiiMhlRxReadDevCapReg(0x00);
                pCbus->chState.dcap_ongoing = true;
            }
        }

        if (SiiDrvCbusDevCapChangedGet ())
        {
            DEBUG_PRINT( MSG_DBG, "CBUS:: DCap Change received!\n");
            if (pCbus->chState.dcap_ready)
            {
                // Read Dcap
                if (!pCbus->chState.dcap_ongoing)
                {
                    SiiMhlRxReadDevCapReg(0x00);
                    pCbus->chState.dcap_ongoing = true;
                }
            }
        }

    	// request received from peer to write into scratchpad
    	if ( SiiDrvCbusReqWrtGet() )
    	{
    		DEBUG_PRINT( MSG_DBG, "Grant peer's request to write scratchpad\n");
    		SiiCbusGrtWrt();
	    }

    	// scratchpad write notification received from peer
    	if ( SiiDrvCbusScratchpadWrtnGet() )
    	{
    		// send it to app layer
    		SiiMhlRxScratchpadWrittenNtfy();
    	}

    	// request to write into peer's scratchpad is granted
    	if ( SiiDrvCbusGrtWrtGet() && pCbus->chState.burstWaitState )
    	{
    		DEBUG_PRINT( MSG_DBG, "Peer sent grant write\n");
            SiiCbusWriteBurst();
            //Recieve grt, stop burst timer.
            pCbus->chState.burst [CH_ACTIVE_BURST].burstStatus = CBUS_BURST_IDLE;
            pCbus->chState.burstWaitState = false;
#if defined(__KERNEL__)
            SiiOsTimerStop(pCbus->chState.burstTimer);
#else
            pCbus->chState.burstTimer = 0;
#endif
        }


    	if ( SiiDrvCbus3DReqGet() )
    	{
            DEBUG_PRINT( MSG_DBG, "Peer request 3D infomation\n");
            SiiDrvCbusBuild3DData();
            SiiDrv3DWriteBurst();
        }

        if( SiiDrvCbusNackFromPeerGet() )
        {
            DEBUG_PRINT( MSG_ERR,( "NACK received from peer\n" ));
            result |= ERROR_NACK_FROM_PEER;
        }

        if( SiiDrvCbusDdcAbortReasonGet( &temp ) )
        {
            DEBUG_PRINT( MSG_DBG, "CBUS DDC ABORT happened, reason: %02X\n", (int)temp );
            if (temp & BIT_MSC_ABORT_BY_PEER)    //Abort by peer
            {
                result |= ERROR_CBUS_ABORT;
            }
            else if(temp)
            {
                result |= ERROR_CBUS_OTHER;
            }
        }


        if ( SiiDrvCbusMscAbortReasonGet( &temp ) )
        {
            DEBUG_PRINT( MSG_DBG, "MSC CMD aborted, reason: %02X\n", (int)temp );
            if (temp & BIT_MSC_ABORT_BY_PEER)    //Abort by peer
            {
                result |= ERROR_CBUS_ABORT;
            }
            if (temp & (BIT_MSC_MAX_RETRY|BIT_MSC_TIMEOUT))
            {
                result |= ERROR_CBUS_TIMEOUT;
            }
            if ((temp & ~(ERROR_CBUS_ABORT|BIT_MSC_MAX_RETRY|BIT_MSC_TIMEOUT)) != 0)
            {
                result |= ERROR_CBUS_ABORT_OTHER;
            }
        }

        if ( SiiDrvCbusMscAbortResReasonGet( &temp ) )
        {
            DEBUG_PRINT( MSG_DBG, "MSC CMD aborted as Responder , reason: %02X\n", (int)temp );
            if (temp & BIT_MSC_ABORT_BY_PEER)    //Abort by peer
            {
                result |= ERROR_CBUS_ABORT;;
            }
            else if(temp)
            {
                result |= ERROR_CBUS_OTHER;
            }
        }
    }

    SiiDrvCbusIntrFlagSet();
    return( result );
}

static void CBusBurstNextInQueue (void)
{
    uint8_t i;
    bool_t success = false;

    //previous burst not done
    if (pCbus->chState.burstWaitState)
    {
        return;
    }

    for (i = 0; i < CBUS_MAX_BURST_QUEUE; i++)
    {
        if (pCbus->chState.burst [i].burstStatus == CBUS_BURST_PENDING)
        {
            // found the pending burst, send it out.
            success = true;
            CH_ACTIVE_BURST = i;
        }
    }
    if (success)
    {
    	// send REQ_WRT interrupt to peer
    	if( !(SiiCbusReqWrt()) )
    	{
    		DEBUG_PRINT( MSG_ERR, "Couldn't send REQ_WRT to peer" );
    	}
        else
        {
            pCbus->chState.burstWaitState = true;
#if defined(__KERNEL__)
            SiiOsTimerStart(pCbus->chState.burstTimer, CBUS_BURST_WAIT_TIMER);
#else
            pCbus->chState.burstTimer = SiiTimerTotalElapsed();
#endif
        }
    }
}


//------------------------------------------------------------------------------
// Function:    SiiCbusWritePeersScratchpad
// Description: sends MHL write burst cmd
//------------------------------------------------------------------------------
bool_t SiiCbusWritePeersScratchpad(uint8_t startOffset, uint8_t length, uint8_t* pMsgData)
{
    uint8_t i;
    bool_t success = false;

    /* Copy the request to the queue.   */

    for ( i = 0; i < CBUS_MAX_BURST_QUEUE; i++ )
    {
        if ( pCbus->chState.burst[ i].burstStatus == CBUS_BURST_IDLE )
        {
            // Found an idle queue entry, copy the request and set to pending.
            memcpy( &pCbus->chState.burst[ i].burstData, pMsgData, length);
            pCbus->chState.burst[ i].offset = startOffset;
            pCbus->chState.burst[ i].length = length;
            pCbus->chState.burst[ i].burstStatus = CBUS_BURST_PENDING;
            pCbus->chState.burst[ i].retry = 1;
            success = true;
            break;
        }
    }

    /* If successful at putting the request into the queue, decide  */
    /* whether it can be sent now or later.                         */

    if ( success )
    {
        CBusBurstNextInQueue();
    }else
    {
        DEBUG_PRINT( MSG_ERR, "CBUS:: Burst Queue full");
    }
    return success;
}

bool_t SiiCbusWriteBurst()
{
    cbus_req_t req;
    bool_t success = false;

    req.command = MHL_WRITE_BURST;
    req.offsetData = pCbus->chState.burst [CH_ACTIVE_BURST].offset;
    req.length = pCbus->chState.burst [CH_ACTIVE_BURST].length;

    memcpy(req.msgData, pCbus->chState.burst [CH_ACTIVE_BURST].burstData, req.length);
    if( !(SiiCbusWriteCommand(&req)) )
    {
    	DEBUG_PRINT( MSG_ERR, "Couldn't send Write Burst to peer" );
    	return false;
    }

    // send DSCR_CHG interrupt to peer
    if( !(SiiCbusSendDscrChange()) )
    {
    	DEBUG_PRINT( MSG_ERR, "Couldn't send DSCR_CHG to peer" );
    	return false;
    }
    success = true;

    return success;
}


//------------------------------------------------------------------------------
// Function:    SiiMhlRxReadDevCapReg
// Description: Read device capability register
//------------------------------------------------------------------------------
bool_t SiiMhlRxReadDevCapReg(uint8_t regOffset)
{
	cbus_req_t 	req;
	bool_t		success;

    req.command = MHL_READ_DEVCAP;
	req.offsetData = regOffset;

	if( !(success = SiiCbusWriteCommand(&req)) )
	{
		DEBUG_PRINT( MSG_ERR, "Couldn't send MHL_READ_DEVCAP to peer" );
	}

	return success;
}


//------------------------------------------------------------------------------
// Function:    SiiCbusSendMscCommand
// Description: sends general MHL commands
//------------------------------------------------------------------------------
bool_t SiiCbusSendMscCommand(uint8_t cmd)
{
    cbus_req_t req;
    bool_t success = true;

    req.command = cmd;

    switch (cmd)
    {
        case MHL_GET_STATE:
        case MHL_GET_VENDOR_ID:
        case MHL_SET_HPD:
        case MHL_CLR_HPD:
        case MHL_GET_SC1_ERRORCODE:
        case MHL_GET_DDC_ERRORCODE:
        case MHL_GET_MSC_ERRORCODE:
        case MHL_GET_SC3_ERRORCODE:

            if (!(success = SiiCbusWriteCommand(&req)))
            {
                DEBUG_PRINT( MSG_ERR, "Couldn't send cmd: %02X to peer", (int)cmd );
                return false;
            }

            break;

        default:
            DEBUG_PRINT( MSG_ERR, "Invalid command %02X send request!!", (int)cmd );
            success = false;
    }
    return success;
}

//------------------------------------------------------------------------------
// Function:    SiiCbusSetInt
// Description: write peer's status registers
//				regOffset - peer's register offset
//				regBit - bit to be set
//------------------------------------------------------------------------------
bool_t SiiCbusSetInt ( uint8_t regOffset, uint8_t regBit )
{
	cbus_req_t req;

	req.command = MHL_SET_INT;
	req.offsetData = regOffset;
	req.msgData[0] = regBit;

	if( !(SiiCbusWriteCommand(&req)) )
	{
		DEBUG_PRINT( MSG_ERR, "Couldn't send MHL_SET_INT to peer" );
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
// Function:    SiMhlRxSendEdidChange
// Description: set edid_chg interrupt
//------------------------------------------------------------------------------
bool_t SiMhlRxSendEdidChange ()
{
	return ( SiiCbusSetInt(0x01, BIT1) );
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxSendDcapChange
//------------------------------------------------------------------------------
bool_t SiiMhlRxSendDcapChange  ()
{
	return ( SiiCbusSetInt(0x00, BIT0) );
}

//------------------------------------------------------------------------------
// Function:    SiiDscrChange
//------------------------------------------------------------------------------
bool_t SiiCbusSendDscrChange ()
{
	return ( SiiCbusSetInt(0x00, BIT1) );
}

//------------------------------------------------------------------------------
// Function:    SiiReqWrt
//------------------------------------------------------------------------------
bool_t SiiCbusReqWrt ()
{
	return ( SiiCbusSetInt(0x00, BIT2) );
}

//------------------------------------------------------------------------------
// Function:    SiiGrtWrt
// Description:
//------------------------------------------------------------------------------
bool_t SiiCbusGrtWrt ()
{
	return ( SiiCbusSetInt(0x00, BIT3) );
}

//------------------------------------------------------------------------------
// Function:    SiiCbusWriteStatus
// Description: write peer's status registers
// Parameters:  regOffset - peer's register offset
//				value - value to be written
//------------------------------------------------------------------------------
bool_t SiiCbusWriteStatus ( uint8_t regOffset, uint8_t value )
{
    cbus_req_t req;

    req.command = MHL_WRITE_STAT;
    req.offsetData = regOffset;
    req.msgData[0] = value;

    if (!(SiiCbusWriteCommand(&req)))
    {
        DEBUG_PRINT( MSG_ERR, "Couldn't send MHL_WRITE_STAT to peer" );
        return false;
    }

    return true;
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxPathEnable
//------------------------------------------------------------------------------
bool_t SiiMhlRxPathEnable  (bool_t enable)
{
    if ( enable )
    {
        // enable PATH_EN bit on peer's appropriate status register (offset 0x31)
        DEBUG_PRINT( MSG_STAT, "SiiMhlRxPathEnable:: Enable\n" );
        return ( SiiCbusWriteStatus(0x01, BIT3) );
    }
    else
    {
        // disable PATH_EN bit on peer's appropriate status register (offset 0x31)
        DEBUG_PRINT( MSG_STAT, "SiiMhlRxPathEnable:: Disable\n" );
        return ( SiiCbusWriteStatus(0x01, 0) );
    }
}

//------------------------------------------------------------------------------
// Function:    SiiSendDcapRdy
//------------------------------------------------------------------------------
bool_t SiiCbusSendDcapRdy ()
{
	return ( SiiCbusWriteStatus(0x00, BIT0) );

}


#if defined(__KERNEL__)
static void CbusAbort_Timer_Callback(void *pArg)
{
    DEBUG_PRINT(MSG_STAT, ("CBUS Abort timer expired.\n"));
    SiiCbusAbortStateSet(false);

    if (pCbus->chState.state == CBUS_IDLE)
    {
        CBusSendNextInQueue();     // No command in progress, write new command immediately.
    }
}

static void CbusBurst_Timer_Callback(void *pArg)
{
    DEBUG_PRINT(MSG_STAT, ("CBUS Burst timer expired.\n"));
    pCbus->chState.burstWaitState = false;
    if (pCbus->chState.burst [CH_ACTIVE_BURST].retry)
    {
        pCbus->chState.burst [CH_ACTIVE_BURST].burstStatus = CBUS_BURST_PENDING;
        pCbus->chState.burst [CH_ACTIVE_BURST].retry--;
    }
    else
    {
        pCbus->chState.burst [CH_ACTIVE_BURST].burstStatus = CBUS_BURST_IDLE;
    }
    CBusBurstNextInQueue();

    if (pCbus->chState.state == CBUS_IDLE)
    {
        CBusSendNextInQueue();     // No command in progress, write new command immediately.
    }
}

static void CbusDcap_Timer_Callback(void *pArg)
{
    if (pCbus->chState.connected && (!pCbus->chState.dcap_ready))
    {
        DEBUG_PRINT(MSG_ERR, ("CBUS Dcap ready timer expired.\n"));
        pCbus->chState.dcap_ready = true;
        // Read Dcap
        SiiMhlRxReadDevCapReg(0x00);
        pCbus->chState.dcap_ongoing = true;
        if (pCbus->chState.state == CBUS_IDLE)
        {
            CBusSendNextInQueue();     // No command in progress, write new command immediately.
        }
    }
}

//------------------------------------------------------------------------------
// Function:    SiiCbusAbortTimerStart
//------------------------------------------------------------------------------
void SiiCbusAbortTimerStart (void)
{
#ifdef MHAWB_SUPPORT
    SiiDrvHawbEnable(false);    //Disable HAWB when receive abort.
#endif
    SiiOsTimerStart(pCbus->chState.abortTimer, CBUS_ABORT_TIMER);
    SiiCbusAbortStateSet(true);
}
#else

/*****************************************************************************/
/**
 * @brief Check the cbus timers, if cbus need to be wait until timeout. 
 *
 * @note: 
 *
 *****************************************************************************/
void SiiCbuschkTimers (void)
{
    if ( pCbus->chState.abortState && (SiiTimerTotalElapsed() - pCbus->chState.abortTimer >  CBUS_ABORT_TIMER) )
    {
        DEBUG_PRINT(MSG_ALWAYS, ("CBUS Abort timer expired.\n"));
        pCbus->chState.abortState = false;
    }
    if ( pCbus->chState.burstWaitState && (SiiTimerTotalElapsed() - pCbus->chState.burstTimer >  CBUS_BURST_WAIT_TIMER) )
    {
        DEBUG_PRINT(MSG_ALWAYS, ("CBUS Burst timer expired.\n"));
        pCbus->chState.burstWaitState = false;
        if (pCbus->chState.burst [CH_ACTIVE_BURST].retry)
        {
            pCbus->chState.burst [CH_ACTIVE_BURST].burstStatus = CBUS_BURST_PENDING;
            pCbus->chState.burst [CH_ACTIVE_BURST].retry--;
        }
        else
        {
            pCbus->chState.burst [CH_ACTIVE_BURST].burstStatus = CBUS_BURST_IDLE;
        }
    }
    if (pCbus->chState.connected && (!pCbus->chState.dcap_ready) && (SiiTimerTotalElapsed() - pCbus->chState.dcapTimer >  CBUS_DCAP_READY_TIMER))
    {        
        DEBUG_PRINT(MSG_ERR, ("CBUS Dcap ready timer expired.\n"));
        pCbus->chState.dcap_ready = true;
        // Read Dcap
        SiiMhlRxReadDevCapReg(0x00);
        pCbus->chState.dcap_ongoing = true;
    }
    CBusBurstNextInQueue();

    if (pCbus->chState.state == CBUS_IDLE)
    {
        CBusSendNextInQueue();     // No command in progress, write new command immediately.
    }

}
#endif

//------------------------------------------------------------------------------
// Function:    SiiMhlRxIntrHandler
// Description: Check the state of any current CBUS message on specified channel.
//              Handle responses or failures and send any pending message if
//              channel is IDLE.
// Parameters:  channel - CBUS channel to check, must be in range, NOT 0xFF
// Returns:     SUCCESS or one of CBUS_SOFTWARE_ERRORS_t
//------------------------------------------------------------------------------

uint8_t SiiMhlRxIntrHandler ()
{
    uint8_t result = SUCCESS;
    bool_t retrycheck = false;
    /* Check the channel interrupt status to see if anybody is  */
    /* talking to us. If they are, talk back.                   */
    result = CBusCheckInterruptStatus();

    /* Don't bother with the rest if the heart is gone. */
    if ( result != SUCCESS )
    {
        if (result & ERROR_CBUS_ABORT)
        {
            //Set abort timer
#if defined(__KERNEL__)
            SiiCbusAbortTimerStart();
#else
            pCbus->chState.abortTimer = SiiTimerTotalElapsed();
#endif
            SiiCbusAbortStateSet(true);
            pCbus->chState.state = CBUS_IDLE;
            SiiMhlRxSetQueueEmpty();
        }
        else if (result & ERROR_CBUS_ABORT_OTHER)
        {
            //send MSC error cannot retry
            if (pCbus->chState.state == CBUS_SENT)
            {
                pCbus->chState.state = CBUS_IDLE;
                pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus = CBUS_REQ_IDLE;
            }
        }
        else if (result & (ERROR_CBUS_TIMEOUT | ERROR_NACK_FROM_PEER))
        {
            //send MSC error, retry if possible
            if (pCbus->chState.state == CBUS_SENT)
            {
                retrycheck = true;
            }
        }
    }

    // If there's some in the burst queue, send it out
    CBusBurstNextInQueue();

    // Check if there's any cbus message arrived
    if (pCbus->chState.receive.arrived)
    {
        SiiMhlRxRcpRapRcvdNtfy(pCbus->chState.receive.command, pCbus->chState.receive.offsetData);
        pCbus->chState.receive.arrived = false;
    }

    /* Update the channel state machine as necessary.   */
    if ( pCbus->chState.state == CBUS_XFR_DONE )
    {
        pCbus->chState.state = CBUS_IDLE;

        /* We may be waiting for a response message, but the    */
        /* request queue is idle.                               */
        if ( pCbus->chState.request[ CH_ACTIVE_INDEX ].command == MHL_READ_DEVCAP )
        {
            DEBUG_PRINT( MSG_DBG, "Response data Received, %02X\n", pCbus->chState.request[ CH_ACTIVE_INDEX ].retData[0] );
            if (pCbus->chState.request[ CH_ACTIVE_INDEX ].offsetData < 15)
                SiiMhlRxReadDevCapReg(pCbus->chState.request[ CH_ACTIVE_INDEX ].offsetData+1);
            else
                pCbus->chState.dcap_ongoing = false;
            pCbus->chState.remote_dcap[pCbus->chState.request[ CH_ACTIVE_INDEX ].offsetData] = pCbus->chState.request[ CH_ACTIVE_INDEX ].retData[0];
        }
        if ( (pCbus->chState.request[ CH_ACTIVE_INDEX ].command == MHL_MSC_MSG )
            && ((pCbus->chState.request[ CH_ACTIVE_INDEX ].msgData[0] == MHL_MSC_MSG_RCPE) ||
            (pCbus->chState.request[ CH_ACTIVE_INDEX ].msgData[0] == MHL_MSC_MSG_UCPE)))
        {
            //Add a little delay after sending RCPE/UCPE
            HalTimerWait(20);
        }
        pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus = CBUS_REQ_IDLE;
        CH_ACTIVE_INDEX = (CH_ACTIVE_INDEX + 1)% CBUS_MAX_COMMAND_QUEUE;
    }
#if 0
    if (pCbus->chState.state == CBUS_SENT)
    {
        //From spec need Wait for TCMD_RECEIVER_TIMEOUT, at least 320ms
        if (pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus == CBUS_REQ_SENT)
        {
            if (SiiTimerTotalElapsed() - pCbus->chState.request[ CH_ACTIVE_INDEX ].reqTimer > CBUS_CMD_TIMEOUT)
            {
                //should never be here, when this timeout occured, there should be abort interrupt firstly.
                retrycheck = true;
            }
        }
        else
        {
            //should never be here
            pCbus->chState.state = CBUS_IDLE;
            result = ERROR_INVALID;
            return result;
        }
    }
#endif

    if (retrycheck)
    {
        pCbus->chState.state = CBUS_IDLE;
        if (pCbus->chState.request[ CH_ACTIVE_INDEX ].retry)
        {
            pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus = CBUS_REQ_PENDING;
            pCbus->chState.request[ CH_ACTIVE_INDEX ].retry--;
        }
        else
        {
            // retry failed, skip the current index, and move on. 
            pCbus->chState.request[ CH_ACTIVE_INDEX ].reqStatus = CBUS_REQ_IDLE;
            CH_ACTIVE_INDEX = (CH_ACTIVE_INDEX + 1)% CBUS_MAX_COMMAND_QUEUE;
        }
    }

    if (pCbus->chState.state == CBUS_IDLE)
    {
        result = CBusSendNextInQueue();     // No command in progress, write new command immediately.
    }

    return( result );
}

//------------------------------------------------------------------------------
// Function:    SiiMhlRxInitialize
// Description: Attempts to initialize the CBUS. If register reads return 0xFF,
//              it declares error in initialization.
//              Initializes discovery enabling registers and anything needed in
//              config register, interrupt masks.
// Returns:     TRUE if no problem
//------------------------------------------------------------------------------

bool_t SiiMhlRxInitialize ( void )
{
    memset( pCbus, 0, sizeof( CbusInstanceData_t ));
#if defined(__KERNEL__)
    SiiOsTimerCreate("Abt Timer", CbusAbort_Timer_Callback, NULL, &pCbus->chState.abortTimer);
    SiiOsTimerCreate("Bst Timer", CbusBurst_Timer_Callback, NULL, &pCbus->chState.burstTimer);
    SiiOsTimerCreate("Dcap Timer", CbusDcap_Timer_Callback, NULL, &pCbus->chState.dcapTimer);
#endif
    return( SiiDrvCbusInitialize() );
}

//------------------------------------------------------------------------------
// Function:    SiiCbusAbortStateSet
//------------------------------------------------------------------------------
void SiiCbusAbortStateSet (bool_t value)
{
    pCbus->chState.abortState = value;
}

//------------------------------------------------------------------------------
// Function:    SiiCbusAbortStateGet
//------------------------------------------------------------------------------
bool_t SiiCbusAbortStateGet (void)
{
    return pCbus->chState.abortState;
}


uint8_t SiiCbusRemoteDcapGet(uint8_t offset)
{
    if (offset <= 0x10)
        return pCbus->chState.remote_dcap[offset];
    else return 0x00;
}
