/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USB4 port sideband registers found on routers and retimers
 *
 * Copyright (C) 2020, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Rajmohan Mani <rajmohan.mani@intel.com>
 */

#ifndef _SB_REGS
#define _SB_REGS

#define USB4_SB_VENDOR_ID			0x00
#define USB4_SB_PRODUCT_ID			0x01
#define USB4_SB_OPCODE				0x08

enum usb4_sb_opcode {
	USB4_SB_OPCODE_ERR = 0x20525245,			/* "ERR " */
	USB4_SB_OPCODE_ONS = 0x444d4321,			/* "!CMD" */
	USB4_SB_OPCODE_ENUMERATE_RETIMERS = 0x4d554e45,		/* "ENUM" */
	USB4_SB_OPCODE_QUERY_LAST_RETIMER = 0x5453414c,		/* "LAST" */
	USB4_SB_OPCODE_GET_NVM_SECTOR_SIZE = 0x53534e47,	/* "GNSS" */
	USB4_SB_OPCODE_NVM_SET_OFFSET = 0x53504f42,		/* "BOPS" */
	USB4_SB_OPCODE_NVM_BLOCK_WRITE = 0x574b4c42,		/* "BLKW" */
	USB4_SB_OPCODE_NVM_AUTH_WRITE = 0x48545541,		/* "AUTH" */
	USB4_SB_OPCODE_NVM_READ = 0x52524641,			/* "AFRR" */
};

#define USB4_SB_METADATA			0x09
#define USB4_SB_METADATA_NVM_AUTH_WRITE_MASK	GENMASK(5, 0)
#define USB4_SB_DATA				0x12

#endif
