/*
 * pcicfg.h: PCI configuration constants and structures.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
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
 * $Id: pcicfg.h 275703 2011-08-04 20:20:27Z $
 */


#ifndef	_h_pcicfg_
#define	_h_pcicfg_


#define	PCI_CFG_VID		0
#define	PCI_CFG_CMD		4
#define	PCI_CFG_REV		8
#define	PCI_CFG_BAR0		0x10
#define	PCI_CFG_BAR1		0x14
#define	PCI_BAR0_WIN		0x80	
#define	PCI_INT_STATUS		0x90	
#define	PCI_INT_MASK		0x94	

#define PCIE_EXTCFG_OFFSET	0x100
#define	PCI_SPROM_CONTROL	0x88	
#define	PCI_BAR1_CONTROL	0x8c	
#define PCI_TO_SB_MB		0x98	
#define PCI_BACKPLANE_ADDR	0xa0	
#define PCI_BACKPLANE_DATA	0xa4	
#define	PCI_CLK_CTL_ST		0xa8	
#define	PCI_BAR0_WIN2		0xac	
#define	PCI_GPIO_IN		0xb0	
#define	PCI_GPIO_OUT		0xb4	
#define	PCI_GPIO_OUTEN		0xb8	

#define	PCI_BAR0_SHADOW_OFFSET	(2 * 1024)	
#define	PCI_BAR0_SPROM_OFFSET	(4 * 1024)	
#define	PCI_BAR0_PCIREGS_OFFSET	(6 * 1024)	
#define	PCI_BAR0_PCISBR_OFFSET	(4 * 1024)	

#define PCI_BAR0_WINSZ		(16 * 1024)	


#define	PCI_16KB0_PCIREGS_OFFSET (8 * 1024)	
#define	PCI_16KB0_CCREGS_OFFSET	(12 * 1024)	
#define PCI_16KBB0_WINSZ	(16 * 1024)	


#define	PCI_16KB0_WIN2_OFFSET	(4 * 1024)	



#define SPROM_SZ_MSK		0x02	
#define SPROM_LOCKED		0x08	
#define	SPROM_BLANK		0x04	
#define SPROM_WRITEEN		0x10	
#define SPROM_BOOTROM_WE	0x20	
#define SPROM_BACKPLANE_EN	0x40	
#define SPROM_OTPIN_USE		0x80	

#endif	
