/*
 * vg468.h 1.11 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_VG468_H
#define _LINUX_VG468_H

/* Special bit in I365_IDENT used for Vadem chip detection */
#define I365_IDENT_VADEM	0x08

/* Special definitions in I365_POWER */
#define VG468_VPP2_MASK		0x0c
#define VG468_VPP2_5V		0x04
#define VG468_VPP2_12V		0x08

/* Unique Vadem registers */
#define VG469_VSENSE		0x1f	/* Card voltage sense */
#define VG469_VSELECT		0x2f	/* Card voltage select */
#define VG468_CTL		0x38	/* Control register */
#define VG468_TIMER		0x39	/* Timer control */
#define VG468_MISC		0x3a	/* Miscellaneous */
#define VG468_GPIO_CFG		0x3b	/* GPIO configuration */
#define VG469_EXT_MODE		0x3c	/* Extended mode register */
#define VG468_SELECT		0x3d	/* Programmable chip select */
#define VG468_SELECT_CFG	0x3e	/* Chip select configuration */
#define VG468_ATA		0x3f	/* ATA control */

/* Flags for VG469_VSENSE */
#define VG469_VSENSE_A_VS1	0x01
#define VG469_VSENSE_A_VS2	0x02
#define VG469_VSENSE_B_VS1	0x04
#define VG469_VSENSE_B_VS2	0x08

/* Flags for VG469_VSELECT */
#define VG469_VSEL_VCC		0x03
#define VG469_VSEL_5V		0x00
#define VG469_VSEL_3V		0x03
#define VG469_VSEL_MAX		0x0c
#define VG469_VSEL_EXT_STAT	0x10
#define VG469_VSEL_EXT_BUS	0x20
#define VG469_VSEL_MIXED	0x40
#define VG469_VSEL_ISA		0x80

/* Flags for VG468_CTL */
#define VG468_CTL_SLOW		0x01	/* 600ns memory timing */
#define VG468_CTL_ASYNC		0x02	/* Asynchronous bus clocking */
#define VG468_CTL_TSSI		0x08	/* Tri-state some outputs */
#define VG468_CTL_DELAY		0x10	/* Card detect debounce */
#define VG468_CTL_INPACK	0x20	/* Obey INPACK signal? */
#define VG468_CTL_POLARITY	0x40	/* VCCEN polarity */
#define VG468_CTL_COMPAT	0x80	/* Compatibility stuff */

#define VG469_CTL_WS_COMPAT	0x04	/* Wait state compatibility */
#define VG469_CTL_STRETCH	0x10	/* LED stretch */

/* Flags for VG468_TIMER */
#define VG468_TIMER_ZEROPWR	0x10	/* Zero power control */
#define VG468_TIMER_SIGEN	0x20	/* Power up */
#define VG468_TIMER_STATUS	0x40	/* Activity timer status */
#define VG468_TIMER_RES		0x80	/* Timer resolution */
#define VG468_TIMER_MASK	0x0f	/* Activity timer timeout */

/* Flags for VG468_MISC */
#define VG468_MISC_GPIO		0x04	/* General-purpose IO */
#define VG468_MISC_DMAWSB	0x08	/* DMA wait state control */
#define VG469_MISC_LEDENA	0x10	/* LED enable */
#define VG468_MISC_VADEMREV	0x40	/* Vadem revision control */
#define VG468_MISC_UNLOCK	0x80	/* Unique register lock */

/* Flags for VG469_EXT_MODE_A */
#define VG469_MODE_VPPST	0x03	/* Vpp steering control */
#define VG469_MODE_INT_SENSE	0x04	/* Internal voltage sense */
#define VG469_MODE_CABLE	0x08
#define VG469_MODE_COMPAT	0x10	/* i82365sl B or DF step */
#define VG469_MODE_TEST		0x20
#define VG469_MODE_RIO		0x40	/* Steer RIO to INTR? */

/* Flags for VG469_EXT_MODE_B */
#define VG469_MODE_B_3V		0x01	/* 3.3v for socket B */

#endif /* _LINUX_VG468_H */
