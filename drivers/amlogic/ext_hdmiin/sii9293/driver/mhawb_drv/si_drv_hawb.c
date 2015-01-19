/******************************************************************************/
//!file     si_drv_hawb.c
//!brief    SiI5293 HAWB Control  Driver.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2007-2013, Silicon Image, Inc.  All rights reserved.
/******************************************************************************/

#include "si_drv_cbus.h"
#include "si_cbus_enums.h"
#include "si_drv_board.h"
#include "si_drv_hawb.h"
#include "si_edid_3d_internal.h"
#include "si_drv_mhawb.h"


/*****************************************************************************/
/**
 *  @brief		Initialize HAWB mask registers
 *              Note: HAWB masks may be cleared after XFIFO write.
 *                    Need forced recovery as SWWA.
 *
 *****************************************************************************/
void SiiDrvHawbInitMask(void)
{
    SiiRegWrite( REG_HAWB_INTR_MASK, 0x0C );
    SiiRegWrite( REG_HAWB_ERROR_INTR_MASK, 0xE7 );
}

/*****************************************************************************/
/**
 *  @brief		Initialize HAWB
 *
 *****************************************************************************/
void SiiDrvHawbInit(void)
{
    //xyu: this function is not used in current code
    // SiiRegWrite( REG_HAWB_INTR, 0xFF );            // [0x8C]; Clear the interrupt bits
    SiiRegWrite( REG_HAWB_RCV_TIMEOUT, 0x3F );     // RSM Timeout // 0x02
    SiiRegWrite( REG_HAWB_XMIT_TIMEOUT, 0x3F );    // XSM Timeout // 0x02
    SiiRegWrite( REG_HAWB_XMIT_CTRL, 0x00 );
}

/*****************************************************************************/
/**
 *  @brief		Send out the 3D Write Burst infomation using HAWB
 *
 *  @param[in]         enableRx		true - enable rx, false - disable rx
 *
 *  @return[out]        bool_t             true - success, false - current state is takeover, cannot change now
 *
 *****************************************************************************/
bool_t SiiDrvHawbEnable(bool_t enableTx )
{  
    //since there's no work to do; don't waste time with I2C
    if (enableTx == false)
    {
         if (g_mhawb.state == MHAWB_STATE_DISABLED || g_mhawb.state == MHAWB_STATE_UNINITIALIZED)
         {
            return true;
         }
         else if (g_mhawb.state == MHAWB_STATE_TAKEOVER)
         {
            return false;
         }
    }
    else if (enableTx == true)
    {
        if (g_mhawb.state == MHAWB_STATE_TAKEOVER)
        {
            return false;
        }
        else if (g_mhawb.state != MHAWB_STATE_DISABLED)
        {
            return true;
        }
    }

    DEBUG_PRINT( MSG_ALWAYS, "[HAWB] %s TX\n", enableTx ? "Enable" : "Disable" );

    //SiiRegWrite( REG_HAWB_INTR, BIT_HAWB_FW_TAKEOVER);  //Clear Fake TAKEOVER interrupt
    SiiRegBitsSet( REG_HAWB_XMIT_CTRL, BIT_HAWB_XMIT_EN, enableTx );    // [0x88][7] = 1 to enable MDT TX

    if (enableTx == false)
    {
        SiiRegWrite( REG_HAWB_INTR_MASK, BIT_HAWB_FW_TAKEOVER);
        SiiRegWrite( REG_HAWB_ERROR_INTR_MASK, 0x00);
        g_mhawb.state = MHAWB_STATE_TAKEOVER;
        return false;
    }else
    {
        g_mhawb.state = MHAWB_STATE_INIT;
        mhawb_do_work();
        return true;
    }
}


/*****************************************************************************/
/**
 *  @brief		Process Hardware Assistant Write Burst interrupts
 *
 *****************************************************************************/
void SiiDrvHawbProcessInterrupts(void)
{
    uint8_t intStatus, intStatusError;

    intStatus = SiiRegRead( REG_HAWB_INTR );
    SiiRegWrite( REG_HAWB_INTR, intStatus );

    intStatusError = SiiRegRead( REG_HAWB_ERROR_INTR );
    SiiRegWrite( REG_HAWB_ERROR_INTR, intStatusError );

    if ( intStatus & BIT_HAWB_XFIFO_EMPTY )
    {
#ifndef __KERNEL__
        hawb3DXfifoEmptyFlag = true;
#endif
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] Got INTR: XFIFO Empty!\n" );
    }

    if ( intStatus & BIT_HAWB_FW_TAKEOVER )
    {
        if (g_mhawb.state == MHAWB_STATE_TAKEOVER)
        {
            DEBUG_PRINT( MSG_ALWAYS, "[HAWB] Got INTR: FW TakeOver!\n" );
            g_mhawb.state = MHAWB_STATE_DISABLED;
        }
    }

    // Error notifications for application
    if ( intStatusError & BIT_HAWB_RTIMEOUT )
    {
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] ERROR: RX TimeOut!\n" );
    }

    if ( intStatusError & BIT_HAWB_RSM_ERROR )
    {
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] ERROR: RX state machine error!\n" );
    }

    if ( intStatusError & BIT_HAWB_XTIMEOUT )
    {
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] ERROR: TX TimeOut!\n" );
    }

    if ( intStatusError & (BIT_HAWB_XSM_RCVD_ABORTPKT | BIT_HAWB_RSM_RCVD_ABORTPKT) )
    {
        SiiCbusAbortTimerStart();
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] ERROR: RX/TX state machine received abort packet!\n" );
    }

    if ( intStatusError & BIT_HAWB_XSM_ERROR )
    {
        DEBUG_PRINT( MSG_ALWAYS, "[HAWB] ERROR: TX state machine error!\n" );
    }
}

