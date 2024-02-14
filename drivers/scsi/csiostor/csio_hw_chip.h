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
#define CSIO_HW_T5				0x5000
#define CSIO_T5_FCOE_ASIC			0x5600
#define CSIO_HW_T6				0x6000
#define CSIO_T6_FCOE_ASIC			0x6600
#define CSIO_HW_CHIP_MASK			0xF000

#define T5_REGMAP_SIZE				(332 * 1024)
#define FW_FNAME_T5				"cxgb4/t5fw.bin"
#define FW_CFG_NAME_T5				"cxgb4/t5-config.txt"
#define FW_FNAME_T6				"cxgb4/t6fw.bin"
#define FW_CFG_NAME_T6				"cxgb4/t6-config.txt"

#define CHELSIO_CHIP_CODE(version, revision) (((version) << 4) | (revision))
#define CHELSIO_CHIP_FPGA          0x100
#define CHELSIO_CHIP_VERSION(code) (((code) >> 12) & 0xf)
#define CHELSIO_CHIP_RELEASE(code) ((code) & 0xf)

#define CHELSIO_T5		0x5
#define CHELSIO_T6		0x6

enum chip_type {
	T5_A0 = CHELSIO_CHIP_CODE(CHELSIO_T5, 0),
	T5_A1 = CHELSIO_CHIP_CODE(CHELSIO_T5, 1),
	T5_FIRST_REV	= T5_A0,
	T5_LAST_REV	= T5_A1,

	T6_A0 = CHELSIO_CHIP_CODE(CHELSIO_T6, 0),
	T6_FIRST_REV    = T6_A0,
	T6_LAST_REV     = T6_A0,
};

static inline int csio_is_t5(uint16_t chip)
{
	return (chip == CSIO_HW_T5);
}

static inline int csio_is_t6(uint16_t chip)
{
	return (chip == CSIO_HW_T6);
}

/* Define MACRO DEFINITIONS */
#define CSIO_DEVICE(devid, idx)						\
	{ PCI_VENDOR_ID_CHELSIO, (devid), PCI_ANY_ID, PCI_ANY_ID, 0, 0, (idx) }

#include "t4fw_api.h"
#include "t4fw_version.h"

#define FW_VERSION(chip) ( \
		FW_HDR_FW_VER_MAJOR_G(chip##FW_VERSION_MAJOR) | \
		FW_HDR_FW_VER_MINOR_G(chip##FW_VERSION_MINOR) | \
		FW_HDR_FW_VER_MICRO_G(chip##FW_VERSION_MICRO) | \
		FW_HDR_FW_VER_BUILD_G(chip##FW_VERSION_BUILD))
#define FW_INTFVER(chip, intf) (FW_HDR_INTFVER_##intf)

struct fw_info {
	u8 chip;
	char *fs_name;
	char *fw_mod_name;
	struct fw_hdr fw_hdr;
};

/* Declare ENUMS */
enum { MEM_EDC0, MEM_EDC1, MEM_MC, MEM_MC0 = MEM_MC, MEM_MC1 };

enum {
	MEMWIN_APERTURE = 2048,
	MEMWIN_BASE     = 0x1b800,
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

extern struct csio_hw_chip_ops t5_ops;

#endif /* #ifndef __CSIO_HW_CHIP_H__ */
