/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    mm_addr.h
*
*  @brief   Memory Map address definitions
*
*  @note
*     None
*/
/****************************************************************************/

#ifndef _MM_ADDR_H
#define _MM_ADDR_H

/* ---- Include Files ---------------------------------------------------- */

#if !defined(CSP_SIMULATION)
#include <cfg_global.h>
#endif

/* ---- Public Constants and Types --------------------------------------- */

/*  Memory Map address definitions */

#define MM_ADDR_DDR                0x00000000

#define MM_ADDR_IO_VPM_EXTMEM_RSVD 0x0F000000	/* 16 MB - Reserved external memory for VPM use */

#define MM_ADDR_IO_FLASHC          0x20000000
#define MM_ADDR_IO_BROM            0x30000000
#define MM_ADDR_IO_ARAM            0x30100000	/* 64 KB - extra cycle latency - WS switch */
#define MM_ADDR_IO_DMA0            0x30200000
#define MM_ADDR_IO_DMA1            0x30300000
#define MM_ADDR_IO_ESW             0x30400000
#define MM_ADDR_IO_CLCD            0x30500000
#define MM_ADDR_IO_PIF             0x30580000
#define MM_ADDR_IO_APM             0x30600000
#define MM_ADDR_IO_SPUM            0x30700000
#define MM_ADDR_IO_VPM_PROG        0x30800000
#define MM_ADDR_IO_VPM_DATA        0x30A00000
#define MM_ADDR_IO_VRAM            0x40000000	/* 64 KB  - security block in front of it */
#define MM_ADDR_IO_CHIPC           0x80000000
#define MM_ADDR_IO_UMI             0x80001000
#define MM_ADDR_IO_NAND            0x80001800
#define MM_ADDR_IO_LEDM            0x80002000
#define MM_ADDR_IO_PWM             0x80002040
#define MM_ADDR_IO_VINTC           0x80003000
#define MM_ADDR_IO_GPIO0           0x80004000
#define MM_ADDR_IO_GPIO1           0x80004800
#define MM_ADDR_IO_I2CS            0x80005000
#define MM_ADDR_IO_SPIS            0x80006000
#define MM_ADDR_IO_HPM             0x80007400
#define MM_ADDR_IO_HPM_REMAP       0x80007800
#define MM_ADDR_IO_TZPC            0x80008000
#define MM_ADDR_IO_MPU             0x80009000
#define MM_ADDR_IO_SPUMP           0x8000a000
#define MM_ADDR_IO_PKA             0x8000b000
#define MM_ADDR_IO_RNG             0x8000c000
#define MM_ADDR_IO_KEYC            0x8000d000
#define MM_ADDR_IO_BBL             0x8000e000
#define MM_ADDR_IO_OTP             0x8000f000
#define MM_ADDR_IO_I2S0            0x80010000
#define MM_ADDR_IO_I2S1            0x80011000
#define MM_ADDR_IO_UARTA           0x80012000
#define MM_ADDR_IO_UARTB           0x80013000
#define MM_ADDR_IO_I2CH            0x80014020
#define MM_ADDR_IO_SPIH            0x80015000
#define MM_ADDR_IO_TSC             0x80016000
#define MM_ADDR_IO_TMR             0x80017000
#define MM_ADDR_IO_WATCHDOG        0x80017800
#define MM_ADDR_IO_ETM             0x80018000
#define MM_ADDR_IO_DDRC            0x80019000
#define MM_ADDR_IO_SINTC           0x80100000
#define MM_ADDR_IO_INTC0           0x80200000
#define MM_ADDR_IO_INTC1           0x80201000
#define MM_ADDR_IO_GE              0x80300000
#define MM_ADDR_IO_USB_CTLR0       0x80400000
#define MM_ADDR_IO_USB_CTLR1       0x80410000
#define MM_ADDR_IO_USB_PHY         0x80420000
#define MM_ADDR_IO_SDIOH0          0x80500000
#define MM_ADDR_IO_SDIOH1          0x80600000
#define MM_ADDR_IO_VDEC            0x80700000

/* ---- Public Variable Externs ------------------------------------------ */
/* ---- Public Function Prototypes --------------------------------------- */

#endif /* _MM_ADDR_H */
