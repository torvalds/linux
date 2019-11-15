/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#ifndef _AMDGPU_RAS_H
#define _AMDGPU_RAS_H

#include <linux/debugfs.h>
#include <linux/list.h>
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "ta_ras_if.h"
#include "amdgpu_ras_eeprom.h"

enum amdgpu_ras_block {
	AMDGPU_RAS_BLOCK__UMC = 0,
	AMDGPU_RAS_BLOCK__SDMA,
	AMDGPU_RAS_BLOCK__GFX,
	AMDGPU_RAS_BLOCK__MMHUB,
	AMDGPU_RAS_BLOCK__ATHUB,
	AMDGPU_RAS_BLOCK__PCIE_BIF,
	AMDGPU_RAS_BLOCK__HDP,
	AMDGPU_RAS_BLOCK__XGMI_WAFL,
	AMDGPU_RAS_BLOCK__DF,
	AMDGPU_RAS_BLOCK__SMN,
	AMDGPU_RAS_BLOCK__SEM,
	AMDGPU_RAS_BLOCK__MP0,
	AMDGPU_RAS_BLOCK__MP1,
	AMDGPU_RAS_BLOCK__FUSE,

	AMDGPU_RAS_BLOCK__LAST
};

#define AMDGPU_RAS_BLOCK_COUNT	AMDGPU_RAS_BLOCK__LAST
#define AMDGPU_RAS_BLOCK_MASK	((1ULL << AMDGPU_RAS_BLOCK_COUNT) - 1)

enum amdgpu_ras_gfx_subblock {
	/* CPC */
	AMDGPU_RAS_BLOCK__GFX_CPC_INDEX_START = 0,
	AMDGPU_RAS_BLOCK__GFX_CPC_SCRATCH =
		AMDGPU_RAS_BLOCK__GFX_CPC_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_CPC_UCODE,
	AMDGPU_RAS_BLOCK__GFX_DC_STATE_ME1,
	AMDGPU_RAS_BLOCK__GFX_DC_CSINVOC_ME1,
	AMDGPU_RAS_BLOCK__GFX_DC_RESTORE_ME1,
	AMDGPU_RAS_BLOCK__GFX_DC_STATE_ME2,
	AMDGPU_RAS_BLOCK__GFX_DC_CSINVOC_ME2,
	AMDGPU_RAS_BLOCK__GFX_DC_RESTORE_ME2,
	AMDGPU_RAS_BLOCK__GFX_CPC_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_DC_RESTORE_ME2,
	/* CPF */
	AMDGPU_RAS_BLOCK__GFX_CPF_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_CPF_ROQ_ME2 =
		AMDGPU_RAS_BLOCK__GFX_CPF_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_CPF_ROQ_ME1,
	AMDGPU_RAS_BLOCK__GFX_CPF_TAG,
	AMDGPU_RAS_BLOCK__GFX_CPF_INDEX_END = AMDGPU_RAS_BLOCK__GFX_CPF_TAG,
	/* CPG */
	AMDGPU_RAS_BLOCK__GFX_CPG_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_CPG_DMA_ROQ =
		AMDGPU_RAS_BLOCK__GFX_CPG_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_CPG_DMA_TAG,
	AMDGPU_RAS_BLOCK__GFX_CPG_TAG,
	AMDGPU_RAS_BLOCK__GFX_CPG_INDEX_END = AMDGPU_RAS_BLOCK__GFX_CPG_TAG,
	/* GDS */
	AMDGPU_RAS_BLOCK__GFX_GDS_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_GDS_MEM = AMDGPU_RAS_BLOCK__GFX_GDS_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_GDS_INPUT_QUEUE,
	AMDGPU_RAS_BLOCK__GFX_GDS_OA_PHY_CMD_RAM_MEM,
	AMDGPU_RAS_BLOCK__GFX_GDS_OA_PHY_DATA_RAM_MEM,
	AMDGPU_RAS_BLOCK__GFX_GDS_OA_PIPE_MEM,
	AMDGPU_RAS_BLOCK__GFX_GDS_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_GDS_OA_PIPE_MEM,
	/* SPI */
	AMDGPU_RAS_BLOCK__GFX_SPI_SR_MEM,
	/* SQ */
	AMDGPU_RAS_BLOCK__GFX_SQ_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_SQ_SGPR = AMDGPU_RAS_BLOCK__GFX_SQ_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_SQ_LDS_D,
	AMDGPU_RAS_BLOCK__GFX_SQ_LDS_I,
	AMDGPU_RAS_BLOCK__GFX_SQ_VGPR,
	AMDGPU_RAS_BLOCK__GFX_SQ_INDEX_END = AMDGPU_RAS_BLOCK__GFX_SQ_VGPR,
	/* SQC (3 ranges) */
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX_START,
	/* SQC range 0 */
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX0_START =
		AMDGPU_RAS_BLOCK__GFX_SQC_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_UTCL1_LFIFO =
		AMDGPU_RAS_BLOCK__GFX_SQC_INDEX0_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU0_WRITE_DATA_BUF,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU0_UTCL1_LFIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU1_WRITE_DATA_BUF,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU1_UTCL1_LFIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU2_WRITE_DATA_BUF,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU2_UTCL1_LFIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX0_END =
		AMDGPU_RAS_BLOCK__GFX_SQC_DATA_CU2_UTCL1_LFIFO,
	/* SQC range 1 */
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKA_TAG_RAM =
		AMDGPU_RAS_BLOCK__GFX_SQC_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKA_UTCL1_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKA_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKA_BANK_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_TAG_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_HIT_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_DIRTY_BIT_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_BANK_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX1_END =
		AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKA_BANK_RAM,
	/* SQC range 2 */
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKB_TAG_RAM =
		AMDGPU_RAS_BLOCK__GFX_SQC_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKB_UTCL1_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKB_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_INST_BANKB_BANK_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_TAG_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_HIT_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_MISS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_DIRTY_BIT_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_BANK_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX2_END =
		AMDGPU_RAS_BLOCK__GFX_SQC_DATA_BANKB_BANK_RAM,
	AMDGPU_RAS_BLOCK__GFX_SQC_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_SQC_INDEX2_END,
	/* TA */
	AMDGPU_RAS_BLOCK__GFX_TA_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TA_FS_DFIFO =
		AMDGPU_RAS_BLOCK__GFX_TA_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TA_FS_AFIFO,
	AMDGPU_RAS_BLOCK__GFX_TA_FL_LFIFO,
	AMDGPU_RAS_BLOCK__GFX_TA_FX_LFIFO,
	AMDGPU_RAS_BLOCK__GFX_TA_FS_CFIFO,
	AMDGPU_RAS_BLOCK__GFX_TA_INDEX_END = AMDGPU_RAS_BLOCK__GFX_TA_FS_CFIFO,
	/* TCA */
	AMDGPU_RAS_BLOCK__GFX_TCA_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TCA_HOLE_FIFO =
		AMDGPU_RAS_BLOCK__GFX_TCA_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TCA_REQ_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCA_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_TCA_REQ_FIFO,
	/* TCC (5 sub-ranges) */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX_START,
	/* TCC range 0 */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX0_START =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DATA =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX0_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DATA_BANK_0_1,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DATA_BANK_1_0,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DATA_BANK_1_1,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DIRTY_BANK_0,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_DIRTY_BANK_1,
	AMDGPU_RAS_BLOCK__GFX_TCC_HIGH_RATE_TAG,
	AMDGPU_RAS_BLOCK__GFX_TCC_LOW_RATE_TAG,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX0_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_LOW_RATE_TAG,
	/* TCC range 1 */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_IN_USE_DEC =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_IN_USE_TRANSFER,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX1_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_IN_USE_TRANSFER,
	/* TCC range 2 */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_RETURN_DATA =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_RETURN_CONTROL,
	AMDGPU_RAS_BLOCK__GFX_TCC_UC_ATOMIC_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCC_WRITE_RETURN,
	AMDGPU_RAS_BLOCK__GFX_TCC_WRITE_CACHE_READ,
	AMDGPU_RAS_BLOCK__GFX_TCC_SRC_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCC_SRC_FIFO_NEXT_RAM,
	AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_TAG_PROBE_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX2_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_CACHE_TAG_PROBE_FIFO,
	/* TCC range 3 */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX3_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_LATENCY_FIFO =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX3_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_LATENCY_FIFO_NEXT_RAM,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX3_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_LATENCY_FIFO_NEXT_RAM,
	/* TCC range 4 */
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX4_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_WRRET_TAG_WRITE_RETURN =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX4_START,
	AMDGPU_RAS_BLOCK__GFX_TCC_ATOMIC_RETURN_BUFFER,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX4_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_ATOMIC_RETURN_BUFFER,
	AMDGPU_RAS_BLOCK__GFX_TCC_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_TCC_INDEX4_END,
	/* TCI */
	AMDGPU_RAS_BLOCK__GFX_TCI_WRITE_RAM,
	/* TCP */
	AMDGPU_RAS_BLOCK__GFX_TCP_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TCP_CACHE_RAM =
		AMDGPU_RAS_BLOCK__GFX_TCP_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TCP_LFIFO_RAM,
	AMDGPU_RAS_BLOCK__GFX_TCP_CMD_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCP_VM_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TCP_DB_RAM,
	AMDGPU_RAS_BLOCK__GFX_TCP_UTCL1_LFIFO0,
	AMDGPU_RAS_BLOCK__GFX_TCP_UTCL1_LFIFO1,
	AMDGPU_RAS_BLOCK__GFX_TCP_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_TCP_UTCL1_LFIFO1,
	/* TD */
	AMDGPU_RAS_BLOCK__GFX_TD_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TD_SS_FIFO_LO =
		AMDGPU_RAS_BLOCK__GFX_TD_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_TD_SS_FIFO_HI,
	AMDGPU_RAS_BLOCK__GFX_TD_CS_FIFO,
	AMDGPU_RAS_BLOCK__GFX_TD_INDEX_END = AMDGPU_RAS_BLOCK__GFX_TD_CS_FIFO,
	/* EA (3 sub-ranges) */
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX_START,
	/* EA range 0 */
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX0_START =
		AMDGPU_RAS_BLOCK__GFX_EA_INDEX_START,
	AMDGPU_RAS_BLOCK__GFX_EA_DRAMRD_CMDMEM =
		AMDGPU_RAS_BLOCK__GFX_EA_INDEX0_START,
	AMDGPU_RAS_BLOCK__GFX_EA_DRAMWR_CMDMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_DRAMWR_DATAMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_RRET_TAGMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_WRET_TAGMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_GMIRD_CMDMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_GMIWR_CMDMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_GMIWR_DATAMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX0_END =
		AMDGPU_RAS_BLOCK__GFX_EA_GMIWR_DATAMEM,
	/* EA range 1 */
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_EA_DRAMRD_PAGEMEM =
		AMDGPU_RAS_BLOCK__GFX_EA_INDEX1_START,
	AMDGPU_RAS_BLOCK__GFX_EA_DRAMWR_PAGEMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_IORD_CMDMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_IOWR_CMDMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_IOWR_DATAMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_GMIRD_PAGEMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_GMIWR_PAGEMEM,
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX1_END =
		AMDGPU_RAS_BLOCK__GFX_EA_GMIWR_PAGEMEM,
	/* EA range 2 */
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_EA_MAM_D0MEM =
		AMDGPU_RAS_BLOCK__GFX_EA_INDEX2_START,
	AMDGPU_RAS_BLOCK__GFX_EA_MAM_D1MEM,
	AMDGPU_RAS_BLOCK__GFX_EA_MAM_D2MEM,
	AMDGPU_RAS_BLOCK__GFX_EA_MAM_D3MEM,
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX2_END =
		AMDGPU_RAS_BLOCK__GFX_EA_MAM_D3MEM,
	AMDGPU_RAS_BLOCK__GFX_EA_INDEX_END =
		AMDGPU_RAS_BLOCK__GFX_EA_INDEX2_END,
	/* UTC VM L2 bank */
	AMDGPU_RAS_BLOCK__UTC_VML2_BANK_CACHE,
	/* UTC VM walker */
	AMDGPU_RAS_BLOCK__UTC_VML2_WALKER,
	/* UTC ATC L2 2MB cache */
	AMDGPU_RAS_BLOCK__UTC_ATCL2_CACHE_2M_BANK,
	/* UTC ATC L2 4KB cache */
	AMDGPU_RAS_BLOCK__UTC_ATCL2_CACHE_4K_BANK,
	AMDGPU_RAS_BLOCK__GFX_MAX
};

enum amdgpu_ras_error_type {
	AMDGPU_RAS_ERROR__NONE							= 0,
	AMDGPU_RAS_ERROR__PARITY						= 1,
	AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE					= 2,
	AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE					= 4,
	AMDGPU_RAS_ERROR__POISON						= 8,
};

enum amdgpu_ras_ret {
	AMDGPU_RAS_SUCCESS = 0,
	AMDGPU_RAS_FAIL,
	AMDGPU_RAS_UE,
	AMDGPU_RAS_CE,
	AMDGPU_RAS_PT,
};

struct ras_common_if {
	enum amdgpu_ras_block block;
	enum amdgpu_ras_error_type type;
	uint32_t sub_block_index;
	/* block name */
	char name[32];
};

struct amdgpu_ras {
	/* ras infrastructure */
	/* for ras itself. */
	uint32_t hw_supported;
	/* for IP to check its ras ability. */
	uint32_t supported;
	uint32_t features;
	struct list_head head;
	/* debugfs */
	struct dentry *dir;
	/* sysfs */
	struct device_attribute features_attr;
	struct bin_attribute badpages_attr;
	/* block array */
	struct ras_manager *objs;

	/* gpu recovery */
	struct work_struct recovery_work;
	atomic_t in_recovery;
	struct amdgpu_device *adev;
	/* error handler data */
	struct ras_err_handler_data *eh_data;
	struct mutex recovery_lock;

	uint32_t flags;
	bool reboot;
	struct amdgpu_ras_eeprom_control eeprom_control;
};

struct ras_fs_data {
	char sysfs_name[32];
	char debugfs_name[32];
};

struct ras_err_data {
	unsigned long ue_count;
	unsigned long ce_count;
	unsigned long err_addr_cnt;
	struct eeprom_table_record *err_addr;
};

struct ras_err_handler_data {
	/* point to bad page records array */
	struct eeprom_table_record *bps;
	/* point to reserved bo array */
	struct amdgpu_bo **bps_bo;
	/* the count of entries */
	int count;
	/* the space can place new entries */
	int space_left;
	/* last reserved entry's index + 1 */
	int last_reserved;
};

typedef int (*ras_ih_cb)(struct amdgpu_device *adev,
		void *err_data,
		struct amdgpu_iv_entry *entry);

struct ras_ih_data {
	/* interrupt bottom half */
	struct work_struct ih_work;
	int inuse;
	/* IP callback */
	ras_ih_cb cb;
	/* full of entries */
	unsigned char *ring;
	unsigned int ring_size;
	unsigned int element_size;
	unsigned int aligned_element_size;
	unsigned int rptr;
	unsigned int wptr;
};

struct ras_manager {
	struct ras_common_if head;
	/* reference count */
	int use;
	/* ras block link */
	struct list_head node;
	/* the device */
	struct amdgpu_device *adev;
	/* debugfs */
	struct dentry *ent;
	/* sysfs */
	struct device_attribute sysfs_attr;
	int attr_inuse;

	/* fs node name */
	struct ras_fs_data fs_data;

	/* IH data */
	struct ras_ih_data ih_data;

	struct ras_err_data err_data;
};

struct ras_badpage {
	unsigned int bp;
	unsigned int size;
	unsigned int flags;
};

/* interfaces for IP */
struct ras_fs_if {
	struct ras_common_if head;
	char sysfs_name[32];
	char debugfs_name[32];
};

struct ras_query_if {
	struct ras_common_if head;
	unsigned long ue_count;
	unsigned long ce_count;
};

struct ras_inject_if {
	struct ras_common_if head;
	uint64_t address;
	uint64_t value;
};

struct ras_cure_if {
	struct ras_common_if head;
	uint64_t address;
};

struct ras_ih_if {
	struct ras_common_if head;
	ras_ih_cb cb;
};

struct ras_dispatch_if {
	struct ras_common_if head;
	struct amdgpu_iv_entry *entry;
};

struct ras_debug_if {
	union {
		struct ras_common_if head;
		struct ras_inject_if inject;
	};
	int op;
};
/* work flow
 * vbios
 * 1: ras feature enable (enabled by default)
 * psp
 * 2: ras framework init (in ip_init)
 * IP
 * 3: IH add
 * 4: debugfs/sysfs create
 * 5: query/inject
 * 6: debugfs/sysfs remove
 * 7: IH remove
 * 8: feature disable
 */

#define amdgpu_ras_get_context(adev)		((adev)->psp.ras.ras)
#define amdgpu_ras_set_context(adev, ras_con)	((adev)->psp.ras.ras = (ras_con))

/* check if ras is supported on block, say, sdma, gfx */
static inline int amdgpu_ras_is_supported(struct amdgpu_device *adev,
		unsigned int block)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (block >= AMDGPU_RAS_BLOCK_COUNT)
		return 0;
	return ras && (ras->supported & (1 << block));
}

int amdgpu_ras_recovery_init(struct amdgpu_device *adev);
int amdgpu_ras_request_reset_on_boot(struct amdgpu_device *adev,
		unsigned int block);

void amdgpu_ras_resume(struct amdgpu_device *adev);
void amdgpu_ras_suspend(struct amdgpu_device *adev);

unsigned long amdgpu_ras_query_error_count(struct amdgpu_device *adev,
		bool is_ce);

/* error handling functions */
int amdgpu_ras_add_bad_pages(struct amdgpu_device *adev,
		struct eeprom_table_record *bps, int pages);

int amdgpu_ras_reserve_bad_pages(struct amdgpu_device *adev);

static inline int amdgpu_ras_reset_gpu(struct amdgpu_device *adev,
		bool is_baco)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	/* save bad page to eeprom before gpu reset,
	 * i2c may be unstable in gpu reset
	 */
	if (in_task())
		amdgpu_ras_reserve_bad_pages(adev);

	if (atomic_cmpxchg(&ras->in_recovery, 0, 1) == 0)
		schedule_work(&ras->recovery_work);
	return 0;
}

static inline enum ta_ras_block
amdgpu_ras_block_to_ta(enum amdgpu_ras_block block) {
	switch (block) {
	case AMDGPU_RAS_BLOCK__UMC:
		return TA_RAS_BLOCK__UMC;
	case AMDGPU_RAS_BLOCK__SDMA:
		return TA_RAS_BLOCK__SDMA;
	case AMDGPU_RAS_BLOCK__GFX:
		return TA_RAS_BLOCK__GFX;
	case AMDGPU_RAS_BLOCK__MMHUB:
		return TA_RAS_BLOCK__MMHUB;
	case AMDGPU_RAS_BLOCK__ATHUB:
		return TA_RAS_BLOCK__ATHUB;
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		return TA_RAS_BLOCK__PCIE_BIF;
	case AMDGPU_RAS_BLOCK__HDP:
		return TA_RAS_BLOCK__HDP;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		return TA_RAS_BLOCK__XGMI_WAFL;
	case AMDGPU_RAS_BLOCK__DF:
		return TA_RAS_BLOCK__DF;
	case AMDGPU_RAS_BLOCK__SMN:
		return TA_RAS_BLOCK__SMN;
	case AMDGPU_RAS_BLOCK__SEM:
		return TA_RAS_BLOCK__SEM;
	case AMDGPU_RAS_BLOCK__MP0:
		return TA_RAS_BLOCK__MP0;
	case AMDGPU_RAS_BLOCK__MP1:
		return TA_RAS_BLOCK__MP1;
	case AMDGPU_RAS_BLOCK__FUSE:
		return TA_RAS_BLOCK__FUSE;
	default:
		WARN_ONCE(1, "RAS ERROR: unexpected block id %d\n", block);
		return TA_RAS_BLOCK__UMC;
	}
}

static inline enum ta_ras_error_type
amdgpu_ras_error_to_ta(enum amdgpu_ras_error_type error) {
	switch (error) {
	case AMDGPU_RAS_ERROR__NONE:
		return TA_RAS_ERROR__NONE;
	case AMDGPU_RAS_ERROR__PARITY:
		return TA_RAS_ERROR__PARITY;
	case AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE:
		return TA_RAS_ERROR__SINGLE_CORRECTABLE;
	case AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE:
		return TA_RAS_ERROR__MULTI_UNCORRECTABLE;
	case AMDGPU_RAS_ERROR__POISON:
		return TA_RAS_ERROR__POISON;
	default:
		WARN_ONCE(1, "RAS ERROR: unexpected error type %d\n", error);
		return TA_RAS_ERROR__NONE;
	}
}

/* called in ip_init and ip_fini */
int amdgpu_ras_init(struct amdgpu_device *adev);
int amdgpu_ras_fini(struct amdgpu_device *adev);
int amdgpu_ras_pre_fini(struct amdgpu_device *adev);
int amdgpu_ras_late_init(struct amdgpu_device *adev,
			 struct ras_common_if *ras_block,
			 struct ras_fs_if *fs_info,
			 struct ras_ih_if *ih_info);
void amdgpu_ras_late_fini(struct amdgpu_device *adev,
			  struct ras_common_if *ras_block,
			  struct ras_ih_if *ih_info);

int amdgpu_ras_feature_enable(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable);

int amdgpu_ras_feature_enable_on_boot(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable);

int amdgpu_ras_sysfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head);

int amdgpu_ras_sysfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head);

void amdgpu_ras_debugfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head);

void amdgpu_ras_debugfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_error_query(struct amdgpu_device *adev,
		struct ras_query_if *info);

int amdgpu_ras_error_inject(struct amdgpu_device *adev,
		struct ras_inject_if *info);

int amdgpu_ras_interrupt_add_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info);

int amdgpu_ras_interrupt_remove_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info);

int amdgpu_ras_interrupt_dispatch(struct amdgpu_device *adev,
		struct ras_dispatch_if *info);

extern atomic_t amdgpu_ras_in_intr;

static inline bool amdgpu_ras_intr_triggered(void)
{
	return !!atomic_read(&amdgpu_ras_in_intr);
}

void amdgpu_ras_global_ras_isr(struct amdgpu_device *adev);

#endif
