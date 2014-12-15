/*
 * SiI8332/8336 MHL Transmitter driver
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the
 * GNU General Public License for more details.
*/

/*
 *****************************************************************************
 * @file  si_platform.h
 *
 * @brief Implementation of the Foo API.
 *
 *****************************************************************************
*/


#ifndef __SI_PLATFORM_H__
#define __SI_PLATFORM_H__
#include <linux/types.h>
#include "osal/include/osal.h"
/* C99 defined data types.  */

#define PLACE_IN_DATA_SEG
#define PLACE_IN_CODE_SEG

#define ROM     /* Nothing */
#define XDATA   /* Nothing */


typedef char BOOL;

#define LOW                     0
#define HIGH                    1

//------------------------------------------------------------------------------
// Configuration defines used by hal_config.h
//------------------------------------------------------------------------------

#ifndef BIT0
#define BIT0                    0x01
#define BIT1                    0x02
#define BIT2                    0x04
#define BIT3                    0x08
#define BIT4                    0x10
#define BIT5                    0x20
#define BIT6                    0x40
#define BIT7                    0x80
#endif

// Why two sets of BIT defines?  Because one type is used in SII code and the other
// kind is stadard for Linux.  We need to cover both type or change to the Linux
// standard everywhere.
#ifndef BIT_0
#define BIT_0                    0x01
#define BIT_1                    0x02
#define BIT_2                    0x04
#define BIT_3                    0x08
#define BIT_4                    0x10
#define BIT_5                    0x20
#define BIT_6                    0x40
#define BIT_7                    0x80
#endif

#define SET_BITS    0xFF
#define CLEAR_BITS  0x00

#endif  // __SI_PLATFORM_H__

