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
#include "ta_ras_if.h"
#include "amdgpu_ras_eeprom.h"
#include "amdgpu_smuio.h"

struct amdgpu_iv_entry;

#define AMDGPU_RAS_FLAG_INIT_BY_VBIOS		(0x1 << 0)
/* position of instance value in sub_block_index of
 * ta_ras_trigger_error_input, the sub block uses lower 12 bits
 */
#define AMDGPU_RAS_INST_MASK 0xfffff000
#define AMDGPU_RAS_INST_SHIFT 0xc

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
	AMDGPU_RAS_BLOCK__MCA,
	AMDGPU_RAS_BLOCK__VCN,
	AMDGPU_RAS_BLOCK__JPEG,

	AMDGPU_RAS_BLOCK__LAST
};

enum amdgpu_ras_mca_block {
	AMDGPU_RAS_MCA_BLOCK__MP0 = 0,
	AMDGPU_RAS_MCA_BLOCK__MP1,
	AMDGPU_RAS_MCA_BLOCK__MPIO,
	AMDGPU_RAS_MCA_BLOCK__IOHC,

	AMDGPU_RAS_MCA_BLOCK__LAST
};

#define AMDGPU_RAS_BLOCK_COUNT	AMDGPU_RAS_BLOCK__LAST
#define AMDGPU_RAS_MCA_BLOCK_COUNT	AMDGPU_RAS_MCA_BLOCK__LAST
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

enum amdgpu_ras_error_query_mode {
	AMDGPU_RAS_INVALID_ERROR_QUERY		= 0,
	AMDGPU_RAS_DIRECT_ERROR_QUERY		= 1,
	AMDGPU_RAS_FIRMWARE_ERROR_QUERY		= 2,
};

/* ras error status reisger fields */
#define ERR_STATUS_LO__ERR_STATUS_VALID_FLAG__SHIFT	0x0
#define ERR_STATUS_LO__ERR_STATUS_VALID_FLAG_MASK	0x00000001L
#define ERR_STATUS_LO__MEMORY_ID__SHIFT			0x18
#define ERR_STATUS_LO__MEMORY_ID_MASK			0xFF000000L
#define ERR_STATUS_HI__ERR_INFO_VALID_FLAG__SHIFT	0x2
#define ERR_STATUS_HI__ERR_INFO_VALID_FLAG_MASK		0x00000004L
#define ERR_STATUS__ERR_CNT__SHIFT			0x17
#define ERR_STATUS__ERR_CNT_MASK			0x03800000L

#define AMDGPU_RAS_REG_ENTRY(ip, inst, reg_lo, reg_hi) \
	ip##_HWIP, inst, reg_lo##_BASE_IDX, reg_lo, reg_hi##_BASE_IDX, reg_hi

#define AMDGPU_RAS_REG_ENTRY_OFFSET(hwip, ip_inst, segment, reg) \
	(adev->reg_offset[hwip][ip_inst][segment] + (reg))

#define AMDGPU_RAS_ERR_INFO_VALID	(1 << 0)
#define AMDGPU_RAS_ERR_STATUS_VALID	(1 << 1)
#define AMDGPU_RAS_ERR_ADDRESS_VALID	(1 << 2)

#define AMDGPU_RAS_GPU_RESET_MODE2_RESET  (0x1 << 0)
#define AMDGPU_RAS_GPU_RESET_MODE1_RESET  (0x1 << 1)

struct amdgpu_ras_err_status_reg_entry {
	uint32_t hwip;
	uint32_t ip_inst;
	uint32_t seg_lo;
	uint32_t reg_lo;
	uint32_t seg_hi;
	uint32_t reg_hi;
	uint32_t reg_inst;
	uint32_t flags;
	const char *block_name;
};

struct amdgpu_ras_memory_id_entry {
	uint32_t memory_id;
	const char *name;
};

struct ras_common_if {
	enum amdgpu_ras_block block;
	enum amdgpu_ras_error_type type;
	uint32_t sub_block_index;
	char name[32];
};

#define MAX_UMC_CHANNEL_NUM 32

struct ecc_info_per_ch {
	uint16_t ce_count_lo_chip;
	uint16_t ce_count_hi_chip;
	uint64_t mca_umc_status;
	uint64_t mca_umc_addr;
	uint64_t mca_ceumc_addr;
};

struct umc_ecc_info {
	struct ecc_info_per_ch ecc[MAX_UMC_CHANNEL_NUM];

	/* Determine smu ecctable whether support
	 * record correctable error address
	 */
	int record_ce_addr_supported;
};

struct amdgpu_ras {
	/* ras infrastructure */
	/* for ras itself. */
	uint32_t features;
	uint32_t schema;
	struct list_head head;
	/* sysfs */
	struct device_attribute features_attr;
	struct device_attribute version_attr;
	struct device_attribute schema_attr;
	struct bin_attribute badpages_attr;
	struct dentry *de_ras_eeprom_table;
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

	bool error_query_ready;

	/* bad page count threshold */
	uint32_t bad_page_cnt_threshold;

	/* disable ras error count harvest in recovery */
	bool disable_ras_err_cnt_harvest;

	/* is poison mode supported */
	bool poison_supported;

	/* RAS count errors delayed work */
	struct delayed_work ras_counte_delay_work;
	atomic_t ras_ue_count;
	atomic_t ras_ce_count;

	/* record umc error info queried from smu */
	struct umc_ecc_info umc_ecc;

	/* Indicates smu whether need update bad channel info */
	bool update_channel_flag;
	/* Record status of smu mca debug mode */
	bool is_mca_debug_mode;

	/* Record special requirements of gpu reset caller */
	uint32_t  gpu_reset_flags;
};

struct ras_fs_data {
	char sysfs_name[48];
	char debugfs_name[32];
};

struct ras_err_info {
	struct amdgpu_smuio_mcm_config_info mcm_info;
	u64 ce_count;
	u64 ue_count;
};

struct ras_err_node {
	struct list_head node;
	struct ras_err_info err_info;
};

struct ras_err_data {
	unsigned long ue_count;
	unsigned long ce_count;
	unsigned long err_addr_cnt;
	struct eeprom_table_record *err_addr;
	u32 err_list_count;
	struct list_head err_node_list;
};

#define for_each_ras_error(err_node, err_data) \
	list_for_each_entry(err_node, &(err_data)->err_node_list, node)

struct ras_err_handler_data {
	/* point to bad page records array */
	struct eeprom_table_record *bps;
	/* the count of entries */
	int count;
	/* the space can place new entries */
	int space_left;
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
	const char* sysfs_name;
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
	uint32_t instance_mask;
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

struct amdgpu_ras_block_object {
	struct ras_common_if  ras_comm;

	int (*ras_block_match)(struct amdgpu_ras_block_object *block_obj,
				enum amdgpu_ras_block block, uint32_t sub_block_index);
	int (*ras_late_init)(struct amdgpu_device *adev, struct ras_common_if *ras_block);
	void (*ras_fini)(struct amdgpu_device *adev, struct ras_common_if *ras_block);
	ras_ih_cb ras_cb;
	const struct amdgpu_ras_block_hw_ops *hw_ops;
};

struct amdgpu_ras_block_hw_ops {
	int  (*ras_error_inject)(struct amdgpu_device *adev,
			void *inject_if, uint32_t instance_mask);
	void (*query_ras_error_count)(struct amdgpu_device *adev, void *ras_error_status);
	void (*query_ras_error_status)(struct amdgpu_device *adev);
	void (*query_ras_error_address)(struct amdgpu_device *adev, void *ras_error_status);
	void (*reset_ras_error_count)(struct amdgpu_device *adev);
	void (*reset_ras_error_status)(struct amdgpu_device *adev);
	bool (*query_poison_status)(struct amdgpu_device *adev);
	bool (*handle_poison_consumption)(struct amdgpu_device *adev);
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


int amdgpu_ras_recovery_init(struct amdgpu_device *adev);

void amdgpu_ras_resume(struct amdgpu_device *adev);
void amdgpu_ras_suspend(struct amdgpu_device *adev);

int amdgpu_ras_query_error_count(struct amdgpu_device *adev,
				 unsigned long *ce_count,
				 unsigned long *ue_count,
				 struct ras_query_if *query_info);

/* error handling functions */
int amdgpu_ras_add_bad_pages(struct amdgpu_device *adev,
		struct eeprom_table_record *bps, int pages);

int amdgpu_ras_save_bad_pages(struct amdgpu_device *adev,
		unsigned long *new_cnt);

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
	case AMDGPU_RAS_BLOCK__MCA:
		return TA_RAS_BLOCK__MCA;
	case AMDGPU_RAS_BLOCK__VCN:
		return TA_RAS_BLOCK__VCN;
	case AMDGPU_RAS_BLOCK__JPEG:
		return TA_RAS_BLOCK__JPEG;
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
int amdgpu_ras_late_init(struct amdgpu_device *adev);
int amdgpu_ras_fini(struct amdgpu_device *adev);
int amdgpu_ras_pre_fini(struct amdgpu_device *adev);

int amdgpu_ras_block_late_init(struct amdgpu_device *adev,
			struct ras_common_if *ras_block);

void amdgpu_ras_block_late_fini(struct amdgpu_device *adev,
			  struct ras_common_if *ras_block);

int amdgpu_ras_feature_enable(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable);

int amdgpu_ras_feature_enable_on_boot(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable);

int amdgpu_ras_sysfs_create(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_sysfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head);

void amdgpu_ras_debugfs_create_all(struct amdgpu_device *adev);

int amdgpu_ras_query_error_status(struct amdgpu_device *adev,
		struct ras_query_if *info);

int amdgpu_ras_reset_error_count(struct amdgpu_device *adev,
		enum amdgpu_ras_block block);
int amdgpu_ras_reset_error_status(struct amdgpu_device *adev,
		enum amdgpu_ras_block block);

int amdgpu_ras_error_inject(struct amdgpu_device *adev,
		struct ras_inject_if *info);

int amdgpu_ras_interrupt_add_handler(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_interrupt_remove_handler(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_interrupt_dispatch(struct amdgpu_device *adev,
		struct ras_dispatch_if *info);

struct ras_manager *amdgpu_ras_find_obj(struct amdgpu_device *adev,
		struct ras_common_if *head);

extern atomic_t amdgpu_ras_in_intr;

static inline bool amdgpu_ras_intr_triggered(void)
{
	return !!atomic_read(&amdgpu_ras_in_intr);
}

static inline void amdgpu_ras_intr_cleared(void)
{
	atomic_set(&amdgpu_ras_in_intr, 0);
}

void amdgpu_ras_global_ras_isr(struct amdgpu_device *adev);

void amdgpu_ras_set_error_query_ready(struct amdgpu_device *adev, bool ready);

bool amdgpu_ras_need_emergency_restart(struct amdgpu_device *adev);

void amdgpu_release_ras_context(struct amdgpu_device *adev);

int amdgpu_persistent_edc_harvesting_supported(struct amdgpu_device *adev);

const char *get_ras_block_str(struct ras_common_if *ras_block);

bool amdgpu_ras_is_poison_mode_supported(struct amdgpu_device *adev);

int amdgpu_ras_is_supported(struct amdgpu_device *adev, unsigned int block);

int amdgpu_ras_reset_gpu(struct amdgpu_device *adev);

struct amdgpu_ras* amdgpu_ras_get_context(struct amdgpu_device *adev);

int amdgpu_ras_set_context(struct amdgpu_device *adev, struct amdgpu_ras *ras_con);

int amdgpu_ras_set_mca_debug_mode(struct amdgpu_device *adev, bool enable);
bool amdgpu_ras_get_mca_debug_mode(struct amdgpu_device *adev);
bool amdgpu_ras_get_error_query_mode(struct amdgpu_device *adev,
				     unsigned int *mode);

int amdgpu_ras_register_ras_block(struct amdgpu_device *adev,
				struct amdgpu_ras_block_object *ras_block_obj);
void amdgpu_ras_interrupt_fatal_error_handler(struct amdgpu_device *adev);
void amdgpu_ras_get_error_type_name(uint32_t err_type, char *err_type_name);
bool amdgpu_ras_inst_get_memory_id_field(struct amdgpu_device *adev,
					 const struct amdgpu_ras_err_status_reg_entry *reg_entry,
					 uint32_t instance,
					 uint32_t *memory_id);
bool amdgpu_ras_inst_get_err_cnt_field(struct amdgpu_device *adev,
				       const struct amdgpu_ras_err_status_reg_entry *reg_entry,
				       uint32_t instance,
				       unsigned long *err_cnt);
void amdgpu_ras_inst_query_ras_error_count(struct amdgpu_device *adev,
					   const struct amdgpu_ras_err_status_reg_entry *reg_list,
					   uint32_t reg_list_size,
					   const struct amdgpu_ras_memory_id_entry *mem_list,
					   uint32_t mem_list_size,
					   uint32_t instance,
					   uint32_t err_type,
					   unsigned long *err_count);
void amdgpu_ras_inst_reset_ras_error_count(struct amdgpu_device *adev,
					   const struct amdgpu_ras_err_status_reg_entry *reg_list,
					   uint32_t reg_list_size,
					   uint32_t instance);

int amdgpu_ras_error_data_init(struct ras_err_data *err_data);
void amdgpu_ras_error_data_fini(struct ras_err_data *err_data);
int amdgpu_ras_error_statistic_ce_count(struct ras_err_data *err_data,
					struct amdgpu_smuio_mcm_config_info *mcm_info, u64 count);
int amdgpu_ras_error_statistic_ue_count(struct ras_err_data *err_data,
					struct amdgpu_smuio_mcm_config_info *mcm_info, u64 count);

#endif
