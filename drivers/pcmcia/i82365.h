/*
 * i82365.h 1.15 1999/10/25 20:03:34
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

#ifndef _LINUX_I82365_H
#define _LINUX_I82365_H

/* register definitions for the Intel 82365SL PCMCIA controller */

/* Offsets for PCIC registers */
#define I365_IDENT	0x00	/* Identification and revision */
#define I365_STATUS	0x01	/* Interface status */
#define I365_POWER	0x02	/* Power and RESETDRV control */
#define I365_INTCTL	0x03	/* Interrupt and general control */
#define I365_CSC	0x04	/* Card status change */
#define I365_CSCINT	0x05	/* Card status change interrupt control */
#define I365_ADDRWIN	0x06	/* Address window enable */
#define I365_IOCTL	0x07	/* I/O control */
#define I365_GENCTL	0x16	/* Card detect and general control */
#define I365_GBLCTL	0x1E	/* Global control register */

/* Offsets for I/O and memory window registers */
#define I365_IO(map)	(0x08+((map)<<2))
#define I365_MEM(map)	(0x10+((map)<<3))
#define I365_W_START	0
#define I365_W_STOP	2
#define I365_W_OFF	4

/* Flags for I365_STATUS */
#define I365_CS_BVD1	0x01
#define I365_CS_STSCHG	0x01
#define I365_CS_BVD2	0x02
#define I365_CS_SPKR	0x02
#define I365_CS_DETECT	0x0C
#define I365_CS_WRPROT	0x10
#define I365_CS_READY	0x20	/* Inverted */
#define I365_CS_POWERON	0x40
#define I365_CS_GPI	0x80

/* Flags for I365_POWER */
#define I365_PWR_OFF	0x00	/* Turn off the socket */
#define I365_PWR_OUT	0x80	/* Output enable */
#define I365_PWR_NORESET 0x40	/* Disable RESETDRV on resume */
#define I365_PWR_AUTO	0x20	/* Auto pwr switch enable */
#define I365_VCC_MASK	0x18	/* Mask for turning off Vcc */
/* There are different layouts for B-step and DF-step chips: the B
   step has independent Vpp1/Vpp2 control, and the DF step has only
   Vpp1 control, plus 3V control */
#define I365_VCC_5V	0x10	/* Vcc = 5.0v */
#define I365_VCC_3V	0x18	/* Vcc = 3.3v */
#define I365_VPP2_MASK	0x0c	/* Mask for turning off Vpp2 */
#define I365_VPP2_5V	0x04	/* Vpp2 = 5.0v */
#define I365_VPP2_12V	0x08	/* Vpp2 = 12.0v */
#define I365_VPP1_MASK	0x03	/* Mask for turning off Vpp1 */
#define I365_VPP1_5V	0x01	/* Vpp2 = 5.0v */
#define I365_VPP1_12V	0x02	/* Vpp2 = 12.0v */

/* Flags for I365_INTCTL */
#define I365_RING_ENA	0x80
#define I365_PC_RESET	0x40
#define I365_PC_IOCARD	0x20
#define I365_INTR_ENA	0x10
#define I365_IRQ_MASK	0x0F

/* Flags for I365_CSC and I365_CSCINT*/
#define I365_CSC_BVD1	0x01
#define I365_CSC_STSCHG	0x01
#define I365_CSC_BVD2	0x02
#define I365_CSC_READY	0x04
#define I365_CSC_DETECT	0x08
#define I365_CSC_ANY	0x0F
#define I365_CSC_GPI	0x10

/* Flags for I365_ADDRWIN */
#define I365_ENA_IO(map)	(0x40 << (map))
#define I365_ENA_MEM(map)	(0x01 << (map))

/* Flags for I365_IOCTL */
#define I365_IOCTL_MASK(map)	(0x0F << (map<<2))
#define I365_IOCTL_WAIT(map)	(0x08 << (map<<2))
#define I365_IOCTL_0WS(map)	(0x04 << (map<<2))
#define I365_IOCTL_IOCS16(map)	(0x02 << (map<<2))
#define I365_IOCTL_16BIT(map)	(0x01 << (map<<2))

/* Flags for I365_GENCTL */
#define I365_CTL_16DELAY	0x01
#define I365_CTL_RESET		0x02
#define I365_CTL_GPI_ENA	0x04
#define I365_CTL_GPI_CTL	0x08
#define I365_CTL_RESUME		0x10
#define I365_CTL_SW_IRQ		0x20

/* Flags for I365_GBLCTL */
#define I365_GBL_PWRDOWN	0x01
#define I365_GBL_CSC_LEV	0x02
#define I365_GBL_WRBACK		0x04
#define I365_GBL_IRQ_0_LEV	0x08
#define I365_GBL_IRQ_1_LEV	0x10

/* Flags for memory window registers */
#define I365_MEM_16BIT	0x8000	/* In memory start high byte */
#define I365_MEM_0WS	0x4000
#define I365_MEM_WS1	0x8000	/* In memory stop high byte */
#define I365_MEM_WS0	0x4000
#define I365_MEM_WRPROT	0x8000	/* In offset high byte */
#define I365_MEM_REG	0x4000

#define I365_REG(slot, reg)	(((slot) << 6) + reg)

#endif /* _LINUX_I82365_H */
