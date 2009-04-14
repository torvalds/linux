/*
 * amd8111_edac.h, EDAC defs for AMD8111 hypertransport chip
 *
 * Copyright (c) 2008 Wind River Systems, Inc.
 *
 * Authors:	Cao Qingtao <qingtao.cao@windriver.com>
 * 		Benjamin Walsh <benjamin.walsh@windriver.com>
 * 		Hu Yongqi <yongqi.hu@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _AMD8111_EDAC_H_
#define _AMD8111_EDAC_H_

/************************************************************
 *	PCI Bridge Status and Command Register, DevA:0x04
 ************************************************************/
#define REG_PCI_STSCMD	0x04
enum pci_stscmd_bits {
	PCI_STSCMD_SSE		= BIT(30),
	PCI_STSCMD_RMA		= BIT(29),
	PCI_STSCMD_RTA		= BIT(28),
	PCI_STSCMD_SERREN	= BIT(8),
	PCI_STSCMD_CLEAR_MASK	= (PCI_STSCMD_SSE |
				   PCI_STSCMD_RMA |
				   PCI_STSCMD_RTA)
};

/************************************************************
 *	PCI Bridge Memory Base-Limit Register, DevA:0x1c
 ************************************************************/
#define REG_MEM_LIM     0x1c
enum mem_limit_bits {
	MEM_LIMIT_DPE   = BIT(31),
	MEM_LIMIT_RSE   = BIT(30),
	MEM_LIMIT_RMA   = BIT(29),
	MEM_LIMIT_RTA   = BIT(28),
	MEM_LIMIT_STA   = BIT(27),
	MEM_LIMIT_MDPE  = BIT(24),
	MEM_LIMIT_CLEAR_MASK  = (MEM_LIMIT_DPE |
				 MEM_LIMIT_RSE |
				 MEM_LIMIT_RMA |
				 MEM_LIMIT_RTA |
				 MEM_LIMIT_STA |
				 MEM_LIMIT_MDPE)
};

/************************************************************
 *	HyperTransport Link Control Register, DevA:0xc4
 ************************************************************/
#define REG_HT_LINK	0xc4
enum ht_link_bits {
	HT_LINK_LKFAIL	= BIT(4),
	HT_LINK_CRCFEN	= BIT(1),
	HT_LINK_CLEAR_MASK = (HT_LINK_LKFAIL)
};

/************************************************************
 *	PCI Bridge Interrupt and Bridge Control, DevA:0x3c
 ************************************************************/
#define REG_PCI_INTBRG_CTRL	0x3c
enum pci_intbrg_ctrl_bits {
	PCI_INTBRG_CTRL_DTSERREN	= BIT(27),
	PCI_INTBRG_CTRL_DTSTAT		= BIT(26),
	PCI_INTBRG_CTRL_MARSP		= BIT(21),
	PCI_INTBRG_CTRL_SERREN		= BIT(17),
	PCI_INTBRG_CTRL_PEREN		= BIT(16),
	PCI_INTBRG_CTRL_CLEAR_MASK	= (PCI_INTBRG_CTRL_DTSTAT),
	PCI_INTBRG_CTRL_POLL_MASK	= (PCI_INTBRG_CTRL_DTSERREN |
					   PCI_INTBRG_CTRL_MARSP |
					   PCI_INTBRG_CTRL_SERREN)
};

/************************************************************
 *		I/O Control 1 Register, DevB:0x40
 ************************************************************/
#define REG_IO_CTRL_1 0x40
enum io_ctrl_1_bits {
	IO_CTRL_1_NMIONERR	= BIT(7),
	IO_CTRL_1_LPC_ERR	= BIT(6),
	IO_CTRL_1_PW2LPC	= BIT(1),
	IO_CTRL_1_CLEAR_MASK	= (IO_CTRL_1_LPC_ERR | IO_CTRL_1_PW2LPC)
};

/************************************************************
 *		Legacy I/O Space Registers
 ************************************************************/
#define REG_AT_COMPAT 0x61
enum at_compat_bits {
	AT_COMPAT_SERR		= BIT(7),
	AT_COMPAT_IOCHK		= BIT(6),
	AT_COMPAT_CLRIOCHK	= BIT(3),
	AT_COMPAT_CLRSERR	= BIT(2),
};

struct amd8111_dev_info {
	u16 err_dev;	/* PCI Device ID */
	struct pci_dev *dev;
	int edac_idx;	/* device index */
	char *ctl_name;
	struct edac_device_ctl_info *edac_dev;
	void (*init)(struct amd8111_dev_info *dev_info);
	void (*exit)(struct amd8111_dev_info *dev_info);
	void (*check)(struct edac_device_ctl_info *edac_dev);
};

struct amd8111_pci_info {
	u16 err_dev;	/* PCI Device ID */
	struct pci_dev *dev;
	int edac_idx;	/* pci index */
	const char *ctl_name;
	struct edac_pci_ctl_info *edac_dev;
	void (*init)(struct amd8111_pci_info *dev_info);
	void (*exit)(struct amd8111_pci_info *dev_info);
	void (*check)(struct edac_pci_ctl_info *edac_dev);
};

#endif /* _AMD8111_EDAC_H_ */
