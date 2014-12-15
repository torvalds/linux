/******************************************************************************/
//!file     si_drv_hawb.h
//!brief    SiI5293 HAWB Control Driver.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2007-2013, Silicon Image, Inc.  All rights reserved.
/******************************************************************************/


#ifndef __SI_DRV_HAWB_H__
#define __SI_DRV_HAWB_H__

#define REG_HAWB_RCV_TIMEOUT            (CBUS_PAGE | 0x84)
#define REG_HAWB_XMIT_TIMEOUT           (CBUS_PAGE | 0x85)
#define REG_HAWB_CTRL                   (CBUS_PAGE | 0x86)
#define BIT_HAWB_RCV_EN                     0x80
#define BIT_HAWB_WRITE_BURST_DISABLE	    0x04
#define REG_HAWB_XMIT_CTRL              (CBUS_PAGE | 0x88)
#define BIT_HAWB_XMIT_EN                    0x80
#define REG_HAWB_XFIFO_DATA             (CBUS_PAGE | 0x89)
#define REG_HAWB_XFIFO_STATUS           (CBUS_PAGE | 0x8B)
#define REG_HAWB_INTR                   (CBUS_PAGE | 0x8C)
#define BIT_HAWB_FW_TAKEOVER                0x04
#define BIT_HAWB_XFIFO_EMPTY                0x08
#define REG_HAWB_INTR_MASK              (CBUS_PAGE | 0x8D)
#define REG_HAWB_ERROR_INTR             (CBUS_PAGE | 0x8E)
#define BIT_HAWB_RTIMEOUT                   0x01        // Receive time-out status
#define BIT_HAWB_RSM_RCVD_ABORTPKT          0x02        // Receive state machine received abort packet status
#define BIT_HAWB_RSM_ERROR                  0x04        // Receive state machine error status
#define BIT_HAWB_XTIMEOUT                   0x20        // Transmit time-out status
#define BIT_HAWB_XSM_RCVD_ABORTPKT          0x40        // Transmit state machine received abort packet status
#define BIT_HAWB_XSM_ERROR                  0x80        // Transmit state machine error status
#define REG_HAWB_ERROR_INTR_MASK        (CBUS_PAGE | 0x8F)


void SiiDrvHawbInit(void);
bool_t SiiDrvHawbEnable( bool_t enableTx);
void SiiDrvHawbProcessInterrupts(void);

#endif
