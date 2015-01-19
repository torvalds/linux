//***************************************************************************
//!file     si_common.h
//!brief    Silicon Image common definitions header.
//
// No part of this work may be reproduced, modified, distributed, 
// transmitted, transcribed, or translated into any language or computer 
// format, in any form or by any means without written permission of 
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2008-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_COMMON_H__
#define __SI_COMMON_H__


#if defined(__KERNEL__)
#include <linux/types.h>
#include <linux/string.h>
#include "sii_hal.h"
#include "si_osdebug.h"
#include "mhl_linuxdrv.h"
#else
#include "string.h"
#include "si_datatypes.h"
#include "si_platform.h"
#include "si_hal.h"
#include "si_i2c.h"
#include "si_eeprom.h"
#include "si_debugger_hdmigear.h"
#include "si_dbgprint.h"
#include "si_osal_timer.h"
#endif
#include "si_rx_info.h"
#include "si_sii5293_registers.h"
#include "si_drv_rx_cfg.h"
#include "si_drv_cbus.h"
#include "si_cra.h"
#include "si_drv_device.h"
#include "si_video_tables.h"

typedef enum
{
    SiiSYSTEM_NONE      = 0,
    SiiSYSTEM_SINK,
    SiiSYSTEM_SWITCH,
    SiiSYSTEM_SOURCE,
    SiiSYSTEM_REPEATER,
} SiiSystemTypes_t;

#define YES                         1
#define NO                          0

//------------------------------------------------------------------------------
//  Basic system functions
//------------------------------------------------------------------------------

#define ELAPSED_TIMER               0xFF
#define ELAPSED_TIMER1              0xFE
#define TIMER_0                     0   // DO NOT USE - reserved for TimerWait()
#define TIMER_1                     1
#define TIMER_2                     2
#define TIMER_3                     3
#define TIMER_COUNT                 4
#if 0
uint8_t SiiTimerExpired( uint8_t timer );
long    SiiTimerElapsed( uint8_t index );
long    SiiTimerTotalElapsed ( void );
void    SiiTimerWait( uint16_t m_sec );
void    SiiTimerSet( uint8_t index, uint16_t m_sec );
void    SiiTimerInit( void );
#endif
//------------------------------------------------------------------------------
//  Miscellaneous Stuffs
//--------------------------------------------------

#define FPGA_BUILD_NEW		0	// 1 == FPGA, 0 == Silicon

//#define SK_TX_EVITA				// enable this when there is Evita as TX on SK

#endif  // __SI_COMMON_H__
