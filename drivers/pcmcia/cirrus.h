/*
 * cirrus.h 1.4 1999/10/25 20:03:34
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

#ifndef _LINUX_CIRRUS_H
#define _LINUX_CIRRUS_H

#ifndef PCI_VENDOR_ID_CIRRUS
#define PCI_VENDOR_ID_CIRRUS		0x1013
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_6729
#define PCI_DEVICE_ID_CIRRUS_6729	0x1100
#endif
#ifndef PCI_DEVICE_ID_CIRRUS_6832
#define PCI_DEVICE_ID_CIRRUS_6832	0x1110
#endif

#define PD67_MISC_CTL_1		0x16	/* Misc control 1 */
#define PD67_FIFO_CTL		0x17	/* FIFO control */
#define PD67_MISC_CTL_2		0x1E	/* Misc control 2 */
#define PD67_CHIP_INFO		0x1f	/* Chip information */
#define PD67_ATA_CTL		0x026	/* 6730: ATA control */
#define PD67_EXT_INDEX		0x2e	/* Extension index */
#define PD67_EXT_DATA		0x2f	/* Extension data */

/* PD6722 extension registers -- indexed in PD67_EXT_INDEX */
#define PD67_DATA_MASK0		0x01	/* Data mask 0 */
#define PD67_DATA_MASK1		0x02	/* Data mask 1 */
#define PD67_DMA_CTL		0x03	/* DMA control */

/* PD6730 extension registers -- indexed in PD67_EXT_INDEX */
#define PD67_EXT_CTL_1		0x03	/* Extension control 1 */
#define PD67_MEM_PAGE(n)	((n)+5)	/* PCI window bits 31:24 */
#define PD67_EXTERN_DATA	0x0a
#define PD67_MISC_CTL_3		0x25
#define PD67_SMB_PWR_CTL	0x26

/* I/O window address offset */
#define PD67_IO_OFF(w)		(0x36+((w)<<1))

/* Timing register sets */
#define PD67_TIME_SETUP(n)	(0x3a + 3*(n))
#define PD67_TIME_CMD(n)	(0x3b + 3*(n))
#define PD67_TIME_RECOV(n)	(0x3c + 3*(n))

/* Flags for PD67_MISC_CTL_1 */
#define PD67_MC1_5V_DET		0x01	/* 5v detect */
#define PD67_MC1_MEDIA_ENA	0x01	/* 6730: Multimedia enable */
#define PD67_MC1_VCC_3V		0x02	/* 3.3v Vcc */
#define PD67_MC1_PULSE_MGMT	0x04
#define PD67_MC1_PULSE_IRQ	0x08
#define PD67_MC1_SPKR_ENA	0x10
#define PD67_MC1_INPACK_ENA	0x80

/* Flags for PD67_FIFO_CTL */
#define PD67_FIFO_EMPTY		0x80

/* Flags for PD67_MISC_CTL_2 */
#define PD67_MC2_FREQ_BYPASS	0x01
#define PD67_MC2_DYNAMIC_MODE	0x02
#define PD67_MC2_SUSPEND	0x04
#define PD67_MC2_5V_CORE	0x08
#define PD67_MC2_LED_ENA	0x10	/* IRQ 12 is LED enable */
#define PD67_MC2_FAST_PCI	0x10	/* 6729: PCI bus > 25 MHz */
#define PD67_MC2_3STATE_BIT7	0x20	/* Floppy change bit */
#define PD67_MC2_DMA_MODE	0x40
#define PD67_MC2_IRQ15_RI	0x80	/* IRQ 15 is ring enable */

/* Flags for PD67_CHIP_INFO */
#define PD67_INFO_SLOTS		0x20	/* 0 = 1 slot, 1 = 2 slots */
#define PD67_INFO_CHIP_ID	0xc0
#define PD67_INFO_REV		0x1c

/* Fields in PD67_TIME_* registers */
#define PD67_TIME_SCALE		0xc0
#define PD67_TIME_SCALE_1	0x00
#define PD67_TIME_SCALE_16	0x40
#define PD67_TIME_SCALE_256	0x80
#define PD67_TIME_SCALE_4096	0xc0
#define PD67_TIME_MULT		0x3f

/* Fields in PD67_DMA_CTL */
#define PD67_DMA_MODE		0xc0
#define PD67_DMA_OFF		0x00
#define PD67_DMA_DREQ_INPACK	0x40
#define PD67_DMA_DREQ_WP	0x80
#define PD67_DMA_DREQ_BVD2	0xc0
#define PD67_DMA_PULLUP		0x20	/* Disable socket pullups? */

/* Fields in PD67_EXT_CTL_1 */
#define PD67_EC1_VCC_PWR_LOCK	0x01
#define PD67_EC1_AUTO_PWR_CLEAR	0x02
#define PD67_EC1_LED_ENA	0x04
#define PD67_EC1_INV_CARD_IRQ	0x08
#define PD67_EC1_INV_MGMT_IRQ	0x10
#define PD67_EC1_PULLUP_CTL	0x20

/* Fields in PD67_MISC_CTL_3 */
#define PD67_MC3_IRQ_MASK	0x03
#define PD67_MC3_IRQ_PCPCI	0x00
#define PD67_MC3_IRQ_EXTERN	0x01
#define PD67_MC3_IRQ_PCIWAY	0x02
#define PD67_MC3_IRQ_PCI	0x03
#define PD67_MC3_PWR_MASK	0x0c
#define PD67_MC3_PWR_SERIAL	0x00
#define PD67_MC3_PWR_TI2202	0x08
#define PD67_MC3_PWR_SMB	0x0c

/* Register definitions for Cirrus PD6832 PCI-to-CardBus bridge */

/* PD6832 extension registers -- indexed in PD67_EXT_INDEX */
#define PD68_EXT_CTL_2			0x0b
#define PD68_PCI_SPACE			0x22
#define PD68_PCCARD_SPACE		0x23
#define PD68_WINDOW_TYPE		0x24
#define PD68_EXT_CSC			0x2e
#define PD68_MISC_CTL_4			0x2f
#define PD68_MISC_CTL_5			0x30
#define PD68_MISC_CTL_6			0x31

/* Extra flags in PD67_MISC_CTL_3 */
#define PD68_MC3_HW_SUSP		0x10
#define PD68_MC3_MM_EXPAND		0x40
#define PD68_MC3_MM_ARM			0x80

/* Bridge Control Register */
#define  PD6832_BCR_MGMT_IRQ_ENA	0x0800

/* Socket Number Register */
#define PD6832_SOCKET_NUMBER		0x004c	/* 8 bit */

#endif /* _LINUX_CIRRUS_H */
