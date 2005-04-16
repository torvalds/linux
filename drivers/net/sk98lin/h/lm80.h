/******************************************************************************
 *
 * Name:	lm80.h	
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.6 $
 * Date:	$Date: 2003/05/13 17:26:52 $
 * Purpose:	Contains all defines for the LM80 Chip
 *		(National Semiconductor).
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef __INC_LM80_H
#define __INC_LM80_H

#ifdef __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* defines ********************************************************************/

/*
 * LM80 register definition
 *
 * All registers are 8 bit wide
 */
#define LM80_CFG			0x00	/* Configuration Register */
#define LM80_ISRC_1			0x01	/* Interrupt Status Register 1 */
#define LM80_ISRC_2			0x02	/* Interrupt Status Register 2 */
#define LM80_IMSK_1			0x03	/* Interrupt Mask Register 1 */
#define LM80_IMSK_2			0x04	/* Interrupt Mask Register 2 */
#define LM80_FAN_CTRL		0x05	/* Fan Devisor/RST#/OS# Register */
#define LM80_TEMP_CTRL		0x06	/* OS# Config, Temp Res. Reg */
	/* 0x07 - 0x1f reserved	*/
	/* current values */
#define LM80_VT0_IN			0x20	/* current Voltage 0 value */
#define LM80_VT1_IN			0x21	/* current Voltage 1 value */
#define LM80_VT2_IN			0x22	/* current Voltage 2 value */
#define LM80_VT3_IN			0x23	/* current Voltage 3 value */
#define LM80_VT4_IN			0x24	/* current Voltage 4 value */
#define LM80_VT5_IN			0x25	/* current Voltage 5 value */
#define LM80_VT6_IN			0x26	/* current Voltage 6 value */
#define LM80_TEMP_IN		0x27	/* current Temperature value */
#define LM80_FAN1_IN		0x28	/* current Fan 1 count */
#define LM80_FAN2_IN		0x29	/* current Fan 2 count */
	/* limit values */
#define LM80_VT0_HIGH_LIM	0x2a	/* high limit val for Voltage 0 */
#define LM80_VT0_LOW_LIM	0x2b	/* low limit val for Voltage 0 */
#define LM80_VT1_HIGH_LIM	0x2c	/* high limit val for Voltage 1 */
#define LM80_VT1_LOW_LIM	0x2d	/* low limit val for Voltage 1 */
#define LM80_VT2_HIGH_LIM	0x2e	/* high limit val for Voltage 2 */
#define LM80_VT2_LOW_LIM	0x2f	/* low limit val for Voltage 2 */
#define LM80_VT3_HIGH_LIM	0x30	/* high limit val for Voltage 3 */
#define LM80_VT3_LOW_LIM	0x31	/* low limit val for Voltage 3 */
#define LM80_VT4_HIGH_LIM	0x32	/* high limit val for Voltage 4 */
#define LM80_VT4_LOW_LIM	0x33	/* low limit val for Voltage 4 */
#define LM80_VT5_HIGH_LIM	0x34	/* high limit val for Voltage 5 */
#define LM80_VT5_LOW_LIM	0x35	/* low limit val for Voltage 5 */
#define LM80_VT6_HIGH_LIM	0x36	/* high limit val for Voltage 6 */
#define LM80_VT6_LOW_LIM	0x37	/* low limit val for Voltage 6 */
#define LM80_THOT_LIM_UP	0x38	/* hot temperature limit (high) */
#define LM80_THOT_LIM_LO	0x39	/* hot temperature limit (low) */
#define LM80_TOS_LIM_UP		0x3a	/* OS temperature limit (high) */
#define LM80_TOS_LIM_LO		0x3b	/* OS temperature limit (low) */
#define LM80_FAN1_COUNT_LIM	0x3c	/* Fan 1 count limit (high) */
#define LM80_FAN2_COUNT_LIM	0x3d	/* Fan 2 count limit (low) */
	/* 0x3e - 0x3f reserved	*/

/*
 * LM80 bit definitions
 */

/*	LM80_CFG		Configuration Register */
#define LM80_CFG_START		(1<<0)	/* start monitoring operation */
#define LM80_CFG_INT_ENA	(1<<1)	/* enables the INT# Interrupt output */
#define LM80_CFG_INT_POL	(1<<2)	/* INT# pol: 0 act low, 1 act high */
#define LM80_CFG_INT_CLR	(1<<3)	/* disables INT#/RST_OUT#/OS# outputs */
#define LM80_CFG_RESET		(1<<4)	/* signals a reset */
#define LM80_CFG_CHASS_CLR	(1<<5)	/* clears Chassis Intrusion (CI) pin */
#define LM80_CFG_GPO		(1<<6)	/* drives the GPO# pin */
#define LM80_CFG_INIT		(1<<7)	/* restore power on defaults */

/*	LM80_ISRC_1		Interrupt Status Register 1 */
/*	LM80_IMSK_1		Interrupt Mask Register 1 */
#define LM80_IS_VT0			(1<<0)	/* limit exceeded for Voltage 0 */
#define LM80_IS_VT1			(1<<1)	/* limit exceeded for Voltage 1 */
#define LM80_IS_VT2			(1<<2)	/* limit exceeded for Voltage 2 */
#define LM80_IS_VT3			(1<<3)	/* limit exceeded for Voltage 3 */
#define LM80_IS_VT4			(1<<4)	/* limit exceeded for Voltage 4 */
#define LM80_IS_VT5			(1<<5)	/* limit exceeded for Voltage 5 */
#define LM80_IS_VT6			(1<<6)	/* limit exceeded for Voltage 6 */
#define LM80_IS_INT_IN		(1<<7)	/* state of INT_IN# */

/*	LM80_ISRC_2		Interrupt Status Register 2 */
/*	LM80_IMSK_2		Interrupt Mask Register 2 */
#define LM80_IS_TEMP		(1<<0)	/* HOT temperature limit exceeded */
#define LM80_IS_BTI			(1<<1)	/* state of BTI# pin */
#define LM80_IS_FAN1		(1<<2)	/* count limit exceeded for Fan 1 */
#define LM80_IS_FAN2		(1<<3)	/* count limit exceeded for Fan 2 */
#define LM80_IS_CI			(1<<4)	/* Chassis Intrusion occured */
#define LM80_IS_OS			(1<<5)	/* OS temperature limit exceeded */
	/* bit 6 and 7 are reserved in LM80_ISRC_2 */
#define LM80_IS_HT_IRQ_MD	(1<<6)	/* Hot temperature interrupt mode */
#define LM80_IS_OT_IRQ_MD	(1<<7)	/* OS temperature interrupt mode */

/*	LM80_FAN_CTRL		Fan Devisor/RST#/OS# Register */
#define LM80_FAN1_MD_SEL	(1<<0)	/* Fan 1 mode select */
#define LM80_FAN2_MD_SEL	(1<<1)	/* Fan 2 mode select */
#define LM80_FAN1_PRM_CTL	(3<<2)	/* Fan 1 speed control */
#define LM80_FAN2_PRM_CTL	(3<<4)	/* Fan 2 speed control */
#define LM80_FAN_OS_ENA		(1<<6)	/* enable OS mode on RST_OUT#/OS# pins*/
#define LM80_FAN_RST_ENA	(1<<7)	/* sets RST_OUT#/OS# pins in RST mode */

/*	LM80_TEMP_CTRL		OS# Config, Temp Res. Reg */
#define LM80_TEMP_OS_STAT	(1<<0)	/* mirrors the state of RST_OUT#/OS# */
#define LM80_TEMP_OS_POL	(1<<1)	/* select OS# polarity */
#define LM80_TEMP_OS_MODE	(1<<2)	/* selects Interrupt mode */
#define LM80_TEMP_RES		(1<<3)	/* selects 9 or 11 bit temp resulution*/
#define LM80_TEMP_LSB		(0xf<<4)/* 4 LSBs of 11 bit temp data */
#define LM80_TEMP_LSB_9		(1<<7)	/* LSB of 9 bit temperature data */

	/* 0x07 - 0x1f reserved	*/
/*	LM80_VT0_IN		current Voltage 0 value */
/*	LM80_VT1_IN		current Voltage 1 value */
/*	LM80_VT2_IN		current Voltage 2 value */
/*	LM80_VT3_IN		current Voltage 3 value */
/*	LM80_VT4_IN		current Voltage 4 value */
/*	LM80_VT5_IN		current Voltage 5 value */
/*	LM80_VT6_IN		current Voltage 6 value */
/*	LM80_TEMP_IN		current temperature value */
/*	LM80_FAN1_IN		current Fan 1 count */
/*	LM80_FAN2_IN		current Fan 2 count */
/*	LM80_VT0_HIGH_LIM	high limit val for Voltage 0 */
/*	LM80_VT0_LOW_LIM	low limit val for Voltage 0 */
/*	LM80_VT1_HIGH_LIM	high limit val for Voltage 1 */
/*	LM80_VT1_LOW_LIM	low limit val for Voltage 1 */
/*	LM80_VT2_HIGH_LIM	high limit val for Voltage 2 */
/*	LM80_VT2_LOW_LIM	low limit val for Voltage 2 */
/*	LM80_VT3_HIGH_LIM	high limit val for Voltage 3 */
/*	LM80_VT3_LOW_LIM	low limit val for Voltage 3 */
/*	LM80_VT4_HIGH_LIM	high limit val for Voltage 4 */
/*	LM80_VT4_LOW_LIM	low limit val for Voltage 4 */
/*	LM80_VT5_HIGH_LIM	high limit val for Voltage 5 */
/*	LM80_VT5_LOW_LIM	low limit val for Voltage 5 */
/*	LM80_VT6_HIGH_LIM	high limit val for Voltage 6 */
/*	LM80_VT6_LOW_LIM	low limit val for Voltage 6 */
/*	LM80_THOT_LIM_UP	hot temperature limit (high) */
/*	LM80_THOT_LIM_LO	hot temperature limit (low) */
/*	LM80_TOS_LIM_UP		OS temperature limit (high) */
/*	LM80_TOS_LIM_LO		OS temperature limit (low) */
/*	LM80_FAN1_COUNT_LIM	Fan 1 count limit (high) */
/*	LM80_FAN2_COUNT_LIM	Fan 2 count limit (low) */
	/* 0x3e - 0x3f reserved	*/

#define LM80_ADDR		0x28	/* LM80 default addr */

/* typedefs *******************************************************************/


/* function prototypes ********************************************************/

#ifdef __cplusplus
}
#endif	/* __cplusplus */

#endif	/* __INC_LM80_H */
