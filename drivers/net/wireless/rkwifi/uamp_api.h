/*
 *  Name:       uamp_api.h
 *
 *  Description: Universal AMP API
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: uamp_api.h,v 1.2.8.1 2011-02-05 00:16:14 $
 *
 */
#ifndef UAMP_API_H
#define UAMP_API_H


#include "typedefs.h"


/*****************************************************************************
**  Constant and Type Definitions
******************************************************************************
*/

#define BT_API

/* Types. */
typedef bool	BOOLEAN;
typedef uint8	UINT8;
typedef uint16	UINT16;


/* UAMP identifiers */
#define UAMP_ID_1   1
#define UAMP_ID_2   2
typedef UINT8 tUAMP_ID;

/* UAMP event ids (used by UAMP_CBACK) */
#define UAMP_EVT_RX_READY           0   /* Data from AMP controller is ready to be read */
#define UAMP_EVT_CTLR_REMOVED       1   /* Controller removed */
#define UAMP_EVT_CTLR_READY         2   /* Controller added/ready */
typedef UINT8 tUAMP_EVT;


/* UAMP Channels */
#define UAMP_CH_HCI_CMD            0   /* HCI Command channel */
#define UAMP_CH_HCI_EVT            1   /* HCI Event channel */
#define UAMP_CH_HCI_DATA           2   /* HCI ACL Data channel */
typedef UINT8 tUAMP_CH;

/* tUAMP_EVT_DATA: union for event-specific data, used by UAMP_CBACK */
typedef union {
    tUAMP_CH channel;       /* UAMP_EVT_RX_READY: channel for which rx occured */
} tUAMP_EVT_DATA;


/*****************************************************************************
**
** Function:    UAMP_CBACK
**
** Description: Callback for events. Register callback using UAMP_Init.
**
** Parameters   amp_id:         AMP device identifier that generated the event
**              amp_evt:        event id
**              p_amp_evt_data: pointer to event-specific data
**
******************************************************************************
*/
typedef void (*tUAMP_CBACK)(tUAMP_ID amp_id, tUAMP_EVT amp_evt, tUAMP_EVT_DATA *p_amp_evt_data);

/*****************************************************************************
**  external function declarations
******************************************************************************
*/
#ifdef __cplusplus
extern "C"
{
#endif

/*****************************************************************************
**
** Function:    UAMP_Init
**
** Description: Initialize UAMP driver
**
** Parameters   p_cback:    Callback function for UAMP event notification
**
******************************************************************************
*/
BT_API BOOLEAN UAMP_Init(tUAMP_CBACK p_cback);


/*****************************************************************************
**
** Function:    UAMP_Open
**
** Description: Open connection to local AMP device.
**
** Parameters   app_id: Application specific AMP identifer. This value
**                      will be included in AMP messages sent to the
**                      BTU task, to identify source of the message
**
******************************************************************************
*/
BT_API BOOLEAN UAMP_Open(tUAMP_ID amp_id);

/*****************************************************************************
**
** Function:    UAMP_Close
**
** Description: Close connection to local AMP device.
**
** Parameters   app_id: Application specific AMP identifer.
**
******************************************************************************
*/
BT_API void UAMP_Close(tUAMP_ID amp_id);


/*****************************************************************************
**
** Function:    UAMP_Write
**
** Description: Send buffer to AMP device. Frees GKI buffer when done.
**
**
** Parameters:  app_id:     AMP identifer.
**              p_buf:      pointer to buffer to write
**              num_bytes:  number of bytes to write
**              channel:    UAMP_CH_HCI_ACL, or UAMP_CH_HCI_CMD
**
** Returns:     number of bytes written
**
******************************************************************************
*/
BT_API UINT16 UAMP_Write(tUAMP_ID amp_id, UINT8 *p_buf, UINT16 num_bytes, tUAMP_CH channel);

/*****************************************************************************
**
** Function:    UAMP_Read
**
** Description: Read incoming data from AMP. Call after receiving a
**              UAMP_EVT_RX_READY callback event.
**
** Parameters:  app_id:     AMP identifer.
**              p_buf:      pointer to buffer for holding incoming AMP data
**              buf_size:   size of p_buf
**              channel:    UAMP_CH_HCI_ACL, or UAMP_CH_HCI_EVT
**
** Returns:     number of bytes read
**
******************************************************************************
*/
BT_API UINT16 UAMP_Read(tUAMP_ID amp_id, UINT8 *p_buf, UINT16 buf_size, tUAMP_CH channel);

#ifdef __cplusplus
}
#endif

#endif /* UAMP_API_H */
