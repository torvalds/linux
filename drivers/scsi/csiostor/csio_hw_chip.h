/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2013 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CSIO_HW_CHIP_H__
#define __CSIO_HW_CHIP_H__

#include "csio_defs.h"

/* Define MACRO values */
#define CSIO_HW_T4				0x4000
#define CSIO_T4_FCOE_ASIC			0x4600
#define CSIO_HW_T5				0x5000
#define CSIO_T5_FCOE_ASIC			0x5600
#define CSIO_HW_CHIP_MASK			0xF000

#define T4_REGMAP_SIZE				(160 * 1024)
#define T5_REGMAP_SIZE				(332 * 1024)
#define FW_FNAME_T4				"cxgb4/t4fw.bin"
#define FW_FNAME_T5				"cxgb4/t5fw.bin"
#define FW_CFG_NAME_T4				"cxgb4/t4-config.txt"
#define FW_CFG_NAME_T5				"cxgb4/t5-config.txt"

/* Define static functions */
static inline int csio_is_t4(uint16_t chip)
{
	return (chip == CSIO_HW_T4);
}

static inline int csio_is_t5(uint16_t chip)
{
	return (chip == CSIO_HW_T5);
}

/* Define MACRO DEFINITIONS */
#define CSIO_DEVICE(devid, idx)						\
	{ PCI_VENDOR_ID_CHELSIO, (devid), PCI_ANY_ID, PCI_ANY_ID, 0, 0, (idx) }

#define CSIO_HW_PIDX(hw, index)						\
	(csio_is_t4(hw->chip_id) ? (PIDX(index)) :			\
					(PIDX_T5(index) | DBTYPE(1U)))

#define CSIO_HW_LP_INT_THRESH(hw, val)					\
	(csio_is_t4(hw->chip_id) ? (LP_INT_THRESH(val)) :		\
					(V_LP_INT_THRESH_T5(val)))

#define CSIO_HW_M_LP_INT_THRESH(hw)					\
	(csio_is_t4(hw->chip_id) ? (LP_INT_THRESH_MASK) : (M_LP_INT_THRESH_T5))

#define CSIO_MAC_INT_CAUSE_REG(hw, port)				\
	(csio_is_t4(hw->chip_id) ? (PORT_REG(port, XGMAC_PORT_INT_CAUSE)) : \
				(T5_PORT_REG(port, MAC_PORT_INT_CAUSE)))

#define FW_VERSION_MAJOR(hw) (csio_is_t4(hw->chip_id) ? 1 : 0)
#define FW_VERSION_MINOR(hw) (csio_is_t4(hw->chip_id) ? 2 : 0)
#define FW_VERSION_MICRO(hw) (csio_is_t4(hw->chip_id) ? 8 : 0)

#define CSIO_FW_FNAME(hw)						\
	(csio_is_t4(hw->chip_id) ? FW_FNAME_T4 : FW_FNAME_T5)

#define CSIO_CF_FNAME(hw)						\
	(csio_is_t4(hw->chip_id) ? FW_CFG_NAME_T4 : FW_CFG_NAME_T5)

/* Declare ENUMS */
enum { MEM_EDC0, MEM_EDC1, MEM_MC, MEM_MC0 = MEM_MC, MEM_MC1 };

enum {
	MEMWIN_APERTURE = 2048,
	MEMWIN_BASE     = 0x1b800,
	MEMWIN_CSIOSTOR = 6,		/* PCI-e Memory Window access */
};

/* Slow path handlers */
struct intr_info {
	unsigned int mask;       /* bits to check in interrupt status */
	const char *msg;         /* message to print or NULL */
	short stat_idx;          /* stat counter to increment or -1 */
	unsigned short fatal;    /* whether the condition reported is fatal */
};

/* T4/T5 Chip specific ops */
struct csio_hw;
struct csio_hw_chip_ops {
	int (*chip_set_mem_win)(struct csio_hw *, uint32_t);
	void (*chip_pcie_intr_handler)(struct csio_hw *);
	uint32_t (*chip_flash_cfg_addr)(struct csio_hw *);
	int (*chip_mc_read)(struct csio_hw *, int, uint32_t,
					__be32 *, uint64_t *);
	int (*chip_edc_read)(struct csio_hw *, int, uint32_t,
					__be32 *, uint64_t *);
	int (*chip_memory_rw)(struct csio_hw *, u32, int, u32,
					u32, uint32_t *, int);
	void (*chip_dfs_create_ext_mem)(struct csio_hw *);
};

extern struct csio_hw_chip_ops t4_ops;
extern struct csio_hw_chip_ops t5_ops;

#endif /* #ifndef __CSIO_HW_CHIP_H__ */
