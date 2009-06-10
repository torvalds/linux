/*
 * amd8131_edac.h, EDAC defs for AMD8131 hypertransport chip
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

#ifndef _AMD8131_EDAC_H_
#define _AMD8131_EDAC_H_

#define DEVFN_PCIX_BRIDGE_NORTH_A	8
#define DEVFN_PCIX_BRIDGE_NORTH_B	16
#define DEVFN_PCIX_BRIDGE_SOUTH_A	24
#define DEVFN_PCIX_BRIDGE_SOUTH_B	32

/************************************************************
 *	PCI-X Bridge Status and Command Register, DevA:0x04
 ************************************************************/
#define REG_STS_CMD	0x04
enum sts_cmd_bits {
	STS_CMD_SSE	= BIT(30),
	STS_CMD_SERREN	= BIT(8)
};

/************************************************************
 *	PCI-X Bridge Interrupt and Bridge Control Register,
 ************************************************************/
#define REG_INT_CTLR	0x3c
enum int_ctlr_bits {
	INT_CTLR_DTSE	= BIT(27),
	INT_CTLR_DTS	= BIT(26),
	INT_CTLR_SERR	= BIT(17),
	INT_CTLR_PERR	= BIT(16)
};

/************************************************************
 *	PCI-X Bridge Memory Base-Limit Register, DevA:0x1C
 ************************************************************/
#define REG_MEM_LIM	0x1c
enum mem_limit_bits {
	MEM_LIMIT_DPE 	= BIT(31),
	MEM_LIMIT_RSE 	= BIT(30),
	MEM_LIMIT_RMA 	= BIT(29),
	MEM_LIMIT_RTA 	= BIT(28),
	MEM_LIMIT_STA	= BIT(27),
	MEM_LIMIT_MDPE	= BIT(24),
	MEM_LIMIT_MASK	= MEM_LIMIT_DPE|MEM_LIMIT_RSE|MEM_LIMIT_RMA|
				MEM_LIMIT_RTA|MEM_LIMIT_STA|MEM_LIMIT_MDPE
};

/************************************************************
 *	Link Configuration And Control Register, side A
 ************************************************************/
#define REG_LNK_CTRL_A	0xc4

/************************************************************
 *	Link Configuration And Control Register, side B
 ************************************************************/
#define REG_LNK_CTRL_B  0xc8

enum lnk_ctrl_bits {
	LNK_CTRL_CRCERR_A	= BIT(9),
	LNK_CTRL_CRCERR_B	= BIT(8),
	LNK_CTRL_CRCFEN		= BIT(1)
};

enum pcix_bridge_inst {
	NORTH_A = 0,
	NORTH_B = 1,
	SOUTH_A = 2,
	SOUTH_B = 3,
	NO_BRIDGE = 4
};

struct amd8131_dev_info {
	int devfn;
	enum pcix_bridge_inst inst;
	struct pci_dev *dev;
	int edac_idx;	/* pci device index */
	char *ctl_name;
	struct edac_pci_ctl_info *edac_dev;
};

/*
 * AMD8131 chipset has two pairs of PCIX Bridge and related IOAPIC
 * Controler, and ATCA-6101 has two AMD8131 chipsets, so there are
 * four PCIX Bridges on ATCA-6101 altogether.
 *
 * These PCIX Bridges share the same PCI Device ID and are all of
 * Function Zero, they could be discrimated by their pci_dev->devfn.
 * They share the same set of init/check/exit methods, and their
 * private structures are collected in the devices[] array.
 */
struct amd8131_info {
	u16 err_dev;	/* PCI Device ID for AMD8131 APIC*/
	struct amd8131_dev_info *devices;
	void (*init)(struct amd8131_dev_info *dev_info);
	void (*exit)(struct amd8131_dev_info *dev_info);
	void (*check)(struct edac_pci_ctl_info *edac_dev);
};

#endif /* _AMD8131_EDAC_H_ */

