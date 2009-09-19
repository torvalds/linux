/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
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
*  @file    tmrHw_reg.h
*
*  @brief   Definitions for low level Timer registers
*
*/
/****************************************************************************/
#ifndef _TMRHW_REG_H
#define _TMRHW_REG_H

#include <mach/csp/mm_io.h>
#include <mach/csp/hw_cfg.h>
/* Base address */
#define tmrHw_MODULE_BASE_ADDR          MM_IO_BASE_TMR

/*
This platform has four different timers running at different clock speed

Timer one   (Timer ID 0) runs at  25 MHz
Timer two   (Timer ID 1) runs at  25 MHz
Timer three (Timer ID 2) runs at 150 MHz
Timer four  (Timer ID 3) runs at 150 MHz
*/
#define tmrHw_LOW_FREQUENCY_MHZ         25	/* Always 25MHz from XTAL */
#define tmrHw_LOW_FREQUENCY_HZ          25000000

#if defined(CFG_GLOBAL_CHIP) && (CFG_GLOBAL_CHIP == FPGA11107)
#define tmrHw_HIGH_FREQUENCY_MHZ        150	/* Always 150MHz for FPGA */
#define tmrHw_HIGH_FREQUENCY_HZ         150000000
#else
#define tmrHw_HIGH_FREQUENCY_HZ         HW_CFG_BUS_CLK_HZ
#define tmrHw_HIGH_FREQUENCY_MHZ        (HW_CFG_BUS_CLK_HZ / 1000000)
#endif

#define tmrHw_LOW_RESOLUTION_CLOCK      tmrHw_LOW_FREQUENCY_HZ
#define tmrHw_HIGH_RESOLUTION_CLOCK     tmrHw_HIGH_FREQUENCY_HZ
#define tmrHw_MAX_COUNT                 (0xFFFFFFFF)	/* maximum number of count a timer can count */
#define tmrHw_TIMER_NUM_COUNT           (4)	/* Number of timer module supported */

typedef struct {
	uint32_t LoadValue;	/* Load value for timer */
	uint32_t CurrentValue;	/* Current value for timer */
	uint32_t Control;	/* Control register */
	uint32_t InterruptClear;	/* Interrupt clear register */
	uint32_t RawInterruptStatus;	/* Raw interrupt status */
	uint32_t InterruptStatus;	/* Masked interrupt status */
	uint32_t BackgroundLoad;	/* Background load value */
	uint32_t padding;	/* Padding register */
} tmrHw_REG_t;

/* Control bot masks */
#define tmrHw_CONTROL_TIMER_ENABLE            0x00000080
#define tmrHw_CONTROL_PERIODIC                0x00000040
#define tmrHw_CONTROL_INTERRUPT_ENABLE        0x00000020
#define tmrHw_CONTROL_PRESCALE_MASK           0x0000000C
#define tmrHw_CONTROL_PRESCALE_1              0x00000000
#define tmrHw_CONTROL_PRESCALE_16             0x00000004
#define tmrHw_CONTROL_PRESCALE_256            0x00000008
#define tmrHw_CONTROL_32BIT                   0x00000002
#define tmrHw_CONTROL_ONESHOT                 0x00000001
#define tmrHw_CONTROL_FREE_RUNNING            0x00000000

#define tmrHw_CONTROL_MODE_MASK               (tmrHw_CONTROL_PERIODIC | tmrHw_CONTROL_ONESHOT)

#define pTmrHw ((volatile tmrHw_REG_t *)tmrHw_MODULE_BASE_ADDR)

#endif /* _TMRHW_REG_H */
