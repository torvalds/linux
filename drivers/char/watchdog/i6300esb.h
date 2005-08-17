/*
 *	i6300esb:	Watchdog timer driver for Intel 6300ESB chipset
 *
 *	(c) Copyright 2000 kernel concepts <nils@kernelconcepts.de>, All Rights Reserved.
 *				http://www.kernelconcepts.de
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither kernel concepts nor Nils Faerber admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 2000	kernel concepts <nils@kernelconcepts.de>
 *				developed for
 *                              Jentro AG, Haar/Munich (Germany)
 *
 *	TCO timer driver for i8xx chipsets
 *	based on softdog.c by Alan Cox <alan@redhat.com>
 *
 *	For history and the complete list of supported I/O Controller Hub's
 *	see i8xx_tco.c
 */


/*
 * Some address definitions for the TCO
 */

/* PCI configuration registers */
#define ESB_CONFIG_REG  0x60            /* Config register                   */
#define ESB_LOCK_REG    0x68            /* WDT lock register                 */

/* Memory mapped registers */
#define ESB_TIMER1_REG  BASEADDR + 0x00 /* Timer1 value after each reset     */
#define ESB_TIMER2_REG  BASEADDR + 0x04 /* Timer2 value after each reset     */
#define ESB_GINTSR_REG  BASEADDR + 0x08 /* General Interrupt Status Register */
#define ESB_RELOAD_REG  BASEADDR + 0x0c /* Reload register                   */


/*
 * Some register bits
 */

/* Lock register bits */
#define ESB_WDT_FUNC    ( 0x01 << 2 )   /* Watchdog functionality            */
#define ESB_WDT_ENABLE  ( 0x01 << 1 )   /* Enable WDT                        */
#define ESB_WDT_LOCK    ( 0x01 << 0 )   /* Lock (nowayout)                   */

/* Config register bits */
#define ESB_WDT_REBOOT  ( 0x01 << 5 )   /* Enable reboot on timeout          */
#define ESB_WDT_FREQ    ( 0x01 << 2 )   /* Decrement frequency               */
#define ESB_WDT_INTTYPE ( 0x11 << 0 )   /* Interrupt type on timer1 timeout  */


/*
 * Some magic constants
 */
#define ESB_UNLOCK1     0x80            /* Step 1 to unlock reset registers  */
#define ESB_UNLOCK2     0x86            /* Step 2 to unlock reset registers  */
