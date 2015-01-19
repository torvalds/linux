//***************************************************************************
//!file     sk_app_cbus.c
//!brief    Wraps board and device functions for the CBUS component
//          and the application
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2009-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/
#include "si_common.h"

#include "si_cbus_component.h"
#include "si_drv_cbus.h"
#include "si_drv_rx.h"


/*****************************************************************************/
/**
 *  @brief		Process the passed RCP message.
 *
 *  @param[in]		rcpData		rcp data
 *
 *  @return	RCPK status code
 *
 *****************************************************************************/
static uint8_t SkAppCbusProcessRcpMessage ( uint8_t rcpData )
{
    uint8_t rcpkStatus = MHL_MSC_MSG_RCP_NO_ERROR;

    switch ( rcpData & 0x7F )
    {
		case MHL_RCP_CMD_MUTE:
		case MHL_RCP_CMD_MUTE_FUNC:
			DEBUG_PRINT( MSG_ALWAYS, "MUTE received\n", (int)rcpData );
			break;
		case MHL_RCP_CMD_UN_MUTE_FUNC:
			DEBUG_PRINT( MSG_ALWAYS, "UN-MUTE received\n", (int)rcpData );
			break;
		case MHL_RCP_CMD_VOL_UP:
			DEBUG_PRINT( MSG_ALWAYS, "VOL UP received\n", (int)rcpData );
			break;
		case MHL_RCP_CMD_VOL_DOWN:
			DEBUG_PRINT( MSG_ALWAYS, "VOL DOWN received\n", (int)rcpData );
			break;
		default:
			rcpkStatus = MHL_MSC_MSG_INEFFECTIVE_KEY_CODE;
			break;
	}

	if ( rcpkStatus == MHL_MSC_MSG_INEFFECTIVE_KEY_CODE )
	{
		DEBUG_PRINT( MSG_DBG, ("KeyCode not effective!!\n" ));
	}

	return( rcpkStatus );
}

/*****************************************************************************/
/**
 *  @brief		Process the passed RAP message.
 *
 *  @param[in]		rapData		rap data
 *
 *  @return	RAPK status code
 *
 *****************************************************************************/
static uint8_t SkAppCbusProcessRapMessage ( uint8_t rapData )
{
    uint8_t rapkStatus = MHL_MSC_MSG_RAP_NO_ERROR;

    switch ( rapData )
    {
	case MHL_RAP_CMD_POLL:
		DEBUG_PRINT( MSG_DBG, "POLL received\n", (int)rapData );
		break;
	case MHL_RAP_CONTENT_ON:
		DEBUG_PRINT( MSG_DBG, "Change TO CONTENT_ON STATE received\n", (int)rapData );
    	SiiDrvRxMuteVideo(OFF);   // switch on
    	RxAudio_ReStart();
		break;
	case MHL_RAP_CONTENT_OFF:
		DEBUG_PRINT( MSG_DBG, "Change TO CONTENT_OFF STATE received\n", (int)rapData );
    	SiiDrvRxMuteVideo(ON);   // switch off
    	RxAudio_Stop();
		break;
	default:
		rapkStatus = MHL_MSC_MSG_RAP_UNRECOGNIZED_ACT_CODE;
        DEBUG_PRINT( MSG_DBG, ("Action Code not recognized !! \n" ));
		break;
    }

    return( rapkStatus );
}

/*****************************************************************************/
/**
 *  @brief		Process the passed UCP message.
 *
 *  @param[in]		ucpData		ucp data
 *
 *  @return	UCPK status code
 *
 *****************************************************************************/
static uint8_t SkAppCbusProcessUcpMessage ( uint8_t ucpData )
{
    uint8_t ucpkStatus = MHL_MSC_MSG_UCP_NO_ERROR;
	
    ucpData = ucpData;

    return( ucpkStatus );
}

/*****************************************************************************/
/**
 *  @brief		This is a notification API for Cbus connection change, prototype
 *			is defined in si_cbus_component.h
 *
 *****************************************************************************/
void SiiMhlRxConnNtfy(bool_t connected)
{
#if defined(__KERNEL__)
    sysfs_notify(&devinfo->mhl->device->kobj, NULL, "connection_state");
    send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT, connected ? MHL_CONNECTED_EVENT : MHL_DISCONNECTED_EVENT, NULL);
    SiiConnectionStateNotify(connected);
#else
    connected =connected;   // suppress warning
#endif
}

/*****************************************************************************/
/**
 *  @brief		This is a notification API for scratchpad bein written by peer
 *
 *****************************************************************************/
void SiiMhlRxScratchpadWrittenNtfy()
{
	DEBUG_PRINT( MSG_DBG, "\nNotification to Application:: Scratchpad written!!\n" );
}

/*****************************************************************************/
/**
 *  @brief		RCP/RAP Received Notification
 *
 *  @param[in]		cmd			command
 *  @param[in]		rcvdCode		received code
 *
 *****************************************************************************/
void SiiMhlRxRcpRapRcvdNtfy( uint8_t cmd, uint8_t rcvdCode )
{
    uint8_t     status;
#if defined(__KERNEL__)
#define MAX_MHL_CHAR_REPORT_DATA_STRING_SIZE 20
    extern int input_dev_rap;
    extern int input_dev_rcp;
    extern int input_dev_ucp;
    char char_report_data_str[MAX_MHL_CHAR_REPORT_DATA_STRING_SIZE];
    scnprintf(char_report_data_str,	MAX_MHL_CHAR_REPORT_DATA_STRING_SIZE, "0x%02X", rcvdCode);
#endif
    //DEBUG_PRINT( MSG_DBG, "\nApplication layer:: SiiMhlRxRcpRapRcvdNtfy() called !!\n" );
    switch ( cmd )
    {
        case MHL_MSC_MSG_RCP:
            DEBUG_PRINT( MSG_DBG, "RCP Key Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            gDriverContext.rcp_in_keycode = rcvdCode;
            sysfs_notify(&devinfo->mhl->device->kobj, "rcp", "in");
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
				    MHL_RCP_RECEIVED_EVENT, char_report_data_str);
// if rcp is setting for process by sysfs interface, break it. 
            if (!input_dev_rcp)
                break;
#endif
            status = SkAppCbusProcessRcpMessage( rcvdCode );
            if ( status != MHL_MSC_MSG_RCP_NO_ERROR)
            {
                SiiMhlRxSendRcpe( status );
            }
            SiiMhlRxSendRcpk( rcvdCode );
            break;

        case MHL_MSC_MSG_RAP:
            DEBUG_PRINT( MSG_DBG, "RAP Key Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            gDriverContext.rap_in_keycode = rcvdCode;
            sysfs_notify(&devinfo->mhl->device->kobj, "rap", "in");
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
				    MHL_RAP_RECEIVED_EVENT, char_report_data_str);
// if rap is setting for process by sysfs interface, break it. 
            if (!input_dev_rap)
                break;
#endif
            status = SkAppCbusProcessRapMessage( rcvdCode );
            SiiMhlRxSendRapk( status );
            break;
        case MHL_MSC_MSG_UCP:
            DEBUG_PRINT( MSG_DBG, "UCP Code Received is: 0x%02X\n", (int)rcvdCode );

#if defined(__KERNEL__)
            gDriverContext.ucp_in_keycode = rcvdCode;
            sysfs_notify(&devinfo->mhl->device->kobj, "ucp", "in");
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
				    MHL_UCP_RECEIVED_EVENT, char_report_data_str);
// if ucp is setting for process by sysfs interface, break it. 
            if (!input_dev_ucp)
                break;
#endif
            status = SkAppCbusProcessUcpMessage( rcvdCode );
            if ( status != MHL_MSC_MSG_UCP_NO_ERROR)
            {
                SiiMhlRxSendUcpe( status );
            }
            SiiMhlRxSendUcpk( rcvdCode );
            break;

        case MHL_MSC_MSG_RCPK:
            DEBUG_PRINT( MSG_DBG, "RCPK Key Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            if (gDriverContext.rcp_out_keycode == rcvdCode)
            {
                sysfs_notify(&devinfo->mhl->device->kobj, "rcp", "out_status");
                send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
				        MHL_RCP_ACKED_EVENT, char_report_data_str);
            }
#endif
            break;

        case MHL_MSC_MSG_RCPE:
            DEBUG_PRINT( MSG_DBG, "RCPE State Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            gDriverContext.rcp_out_statecode = rcvdCode;
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
			        MHL_RCP_ERROR_EVENT, char_report_data_str);
#endif
            break;
        case MHL_MSC_MSG_RAPK:
        	DEBUG_PRINT( MSG_DBG, "RAPK Error Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            gDriverContext.rap_out_statecode = rcvdCode;
            sysfs_notify(&devinfo->mhl->device->kobj, "rap", "out_status");
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
			        MHL_RAP_ACKED_EVENT, char_report_data_str);
#endif
            break;

        case MHL_MSC_MSG_UCPK:
            DEBUG_PRINT( MSG_DBG, "UCPK Key Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            if (gDriverContext.ucp_out_keycode == rcvdCode)
            {
                sysfs_notify(&devinfo->mhl->device->kobj, "ucp", "out_status");
                send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
    			        MHL_UCP_ACKED_EVENT, char_report_data_str);
            }
#endif
            break;
        case MHL_MSC_MSG_UCPE:
            DEBUG_PRINT( MSG_DBG, "UCPE State Code: 0x%02X\n", (int)rcvdCode );
#if defined(__KERNEL__)
            gDriverContext.ucp_out_statecode = rcvdCode;
            send_sii5293_uevent(devinfo->mhl->device, MHL_EVENT,
			        MHL_UCP_ERROR_EVENT, char_report_data_str);
#endif
            break;
        default:
        	DEBUG_PRINT( MSG_DBG, "\nApplication layer:: MSC_MSG sub-command not recognized!! Sending back MSGE code !!\n" );
        	SiiMhlRxSendMsge(MHL_MSC_INVALID_SUBCMD);
        	break;
    }
}

