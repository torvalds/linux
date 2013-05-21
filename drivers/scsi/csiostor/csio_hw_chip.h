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

/* FCoE device IDs for T4 */
#define CSIO_DEVID_T440DBG_FCOE			0x4600
#define CSIO_DEVID_T420CR_FCOE			0x4601
#define CSIO_DEVID_T422CR_FCOE			0x4602
#define CSIO_DEVID_T440CR_FCOE			0x4603
#define CSIO_DEVID_T420BCH_FCOE			0x4604
#define CSIO_DEVID_T440BCH_FCOE			0x4605
#define CSIO_DEVID_T440CH_FCOE			0x4606
#define CSIO_DEVID_T420SO_FCOE			0x4607
#define CSIO_DEVID_T420CX_FCOE			0x4608
#define CSIO_DEVID_T420BT_FCOE			0x4609
#define CSIO_DEVID_T404BT_FCOE			0x460A
#define CSIO_DEVID_B420_FCOE			0x460B
#define CSIO_DEVID_B404_FCOE			0x460C
#define CSIO_DEVID_T480CR_FCOE			0x460D
#define CSIO_DEVID_T440LPCR_FCOE		0x460E
#define CSIO_DEVID_AMSTERDAM_T4_FCOE		0x460F
#define CSIO_DEVID_HUAWEI_T480_FCOE		0x4680
#define CSIO_DEVID_HUAWEI_T440_FCOE		0x4681
#define CSIO_DEVID_HUAWEI_STG310_FCOE		0x4682
#define CSIO_DEVID_ACROMAG_XMC_XAUI		0x4683
#define CSIO_DEVID_ACROMAG_XMC_SFP_FCOE		0x4684
#define CSIO_DEVID_QUANTA_MEZZ_SFP_FCOE		0x4685
#define CSIO_DEVID_HUAWEI_10GT_FCOE		0x4686
#define CSIO_DEVID_HUAWEI_T440_TOE_FCOE		0x4687

/* FCoE device IDs for T5 */
#define CSIO_DEVID_T580DBG_FCOE			0x5600
#define CSIO_DEVID_T520CR_FCOE			0x5601
#define CSIO_DEVID_T522CR_FCOE			0x5602
#define CSIO_DEVID_T540CR_FCOE			0x5603
#define CSIO_DEVID_T520BCH_FCOE			0x5604
#define CSIO_DEVID_T540BCH_FCOE			0x5605
#define CSIO_DEVID_T540CH_FCOE			0x5606
#define CSIO_DEVID_T520SO_FCOE			0x5607
#define CSIO_DEVID_T520CX_FCOE			0x5608
#define CSIO_DEVID_T520BT_FCOE			0x5609
#define CSIO_DEVID_T504BT_FCOE			0x560A
#define CSIO_DEVID_B520_FCOE			0x560B
#define CSIO_DEVID_B504_FCOE			0x560C
#define CSIO_DEVID_T580CR2_FCOE			0x560D
#define CSIO_DEVID_T540LPCR_FCOE		0x560E
#define CSIO_DEVID_AMSTERDAM_T5_FCOE		0x560F
#define CSIO_DEVID_T580LPCR_FCOE		0x5610
#define CSIO_DEVID_T520LLCR_FCOE		0x5611
#define CSIO_DEVID_T560CR_FCOE			0x5612
#define CSIO_DEVID_T580CR_FCOE			0x5613

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
