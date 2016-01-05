/*
 * pcicfg.h: PCI configuration constants and structures.
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: pcicfg.h 413666 2013-07-20 01:16:40Z $
 */

#ifndef	_h_pcicfg_
#define	_h_pcicfg_

/* A structure for the config registers is nice, but in most
 * systems the config space is not memory mapped, so we need
 * field offsetts. :-(
 */
#define	PCI_CFG_VID		0
#define	PCI_CFG_DID		2
#define	PCI_CFG_CMD		4
#define	PCI_CFG_STAT		6
#define	PCI_CFG_REV		8
#define	PCI_CFG_PROGIF		9
#define	PCI_CFG_SUBCL		0xa
#define	PCI_CFG_BASECL		0xb
#define	PCI_CFG_CLSZ		0xc
#define	PCI_CFG_LATTIM		0xd
#define	PCI_CFG_HDR		0xe
#define	PCI_CFG_BIST		0xf
#define	PCI_CFG_BAR0		0x10
#define	PCI_CFG_BAR1		0x14
#define	PCI_CFG_BAR2		0x18
#define	PCI_CFG_BAR3		0x1c
#define	PCI_CFG_BAR4		0x20
#define	PCI_CFG_BAR5		0x24
#define	PCI_CFG_CIS		0x28
#define	PCI_CFG_SVID		0x2c
#define	PCI_CFG_SSID		0x2e
#define	PCI_CFG_ROMBAR		0x30
#define PCI_CFG_CAPPTR		0x34
#define	PCI_CFG_INT		0x3c
#define	PCI_CFG_PIN		0x3d
#define	PCI_CFG_MINGNT		0x3e
#define	PCI_CFG_MAXLAT		0x3f
#define	PCI_CFG_DEVCTRL		0xd8
#define	PCI_BAR0_WIN		0x80	/* backplane addres space accessed by BAR0 */
#define	PCI_BAR1_WIN		0x84	/* backplane addres space accessed by BAR1 */
#define	PCI_SPROM_CONTROL	0x88	/* sprom property control */
#define	PCI_BAR1_CONTROL	0x8c	/* BAR1 region burst control */
#define	PCI_INT_STATUS		0x90	/* PCI and other cores interrupts */
#define	PCI_INT_MASK		0x94	/* mask of PCI and other cores interrupts */
#define PCI_TO_SB_MB		0x98	/* signal backplane interrupts */
#define PCI_BACKPLANE_ADDR	0xa0	/* address an arbitrary location on the system backplane */
#define PCI_BACKPLANE_DATA	0xa4	/* data at the location specified by above address */
#define	PCI_CLK_CTL_ST		0xa8	/* pci config space clock control/status (>=rev14) */
#define	PCI_BAR0_WIN2		0xac	/* backplane addres space accessed by second 4KB of BAR0 */
#define	PCI_GPIO_IN		0xb0	/* pci config space gpio input (>=rev3) */
#define	PCI_GPIO_OUT		0xb4	/* pci config space gpio output (>=rev3) */
#define	PCI_GPIO_OUTEN		0xb8	/* pci config space gpio output enable (>=rev3) */

#define	PCI_BAR0_SHADOW_OFFSET	(2 * 1024)	/* bar0 + 2K accesses sprom shadow (in pci core) */
#define	PCI_BAR0_SPROM_OFFSET	(4 * 1024)	/* bar0 + 4K accesses external sprom */
#define	PCI_BAR0_PCIREGS_OFFSET	(6 * 1024)	/* bar0 + 6K accesses pci core registers */
#define	PCI_BAR0_PCISBR_OFFSET	(4 * 1024)	/* pci core SB registers are at the end of the
						 * 8KB window, so their address is the "regular"
						 * address plus 4K
						 */
/*
 * PCIE GEN2 changed some of the above locations for
 * Bar0WrapperBase, SecondaryBAR0Window and SecondaryBAR0WrapperBase
 * BAR0 maps 32K of register space
*/
#define PCIE2_BAR0_WIN2		0x70 /* backplane addres space accessed by second 4KB of BAR0 */
#define PCIE2_BAR0_CORE2_WIN	0x74 /* backplane addres space accessed by second 4KB of BAR0 */
#define PCIE2_BAR0_CORE2_WIN2	0x78 /* backplane addres space accessed by second 4KB of BAR0 */

#define PCI_BAR0_WINSZ		(16 * 1024)	/* bar0 window size Match with corerev 13 */
/* On pci corerev >= 13 and all pcie, the bar0 is now 16KB and it maps: */
#define	PCI_16KB0_PCIREGS_OFFSET (8 * 1024)	/* bar0 + 8K accesses pci/pcie core registers */
#define	PCI_16KB0_CCREGS_OFFSET	(12 * 1024)	/* bar0 + 12K accesses chipc core registers */
#define PCI_16KBB0_WINSZ	(16 * 1024)	/* bar0 window size */


#define PCI_CONFIG_SPACE_SIZE	256
#endif	/* _h_pcicfg_ */
