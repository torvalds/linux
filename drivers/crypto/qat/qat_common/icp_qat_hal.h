/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef __ICP_QAT_HAL_H
#define __ICP_QAT_HAL_H
#include "icp_qat_fw_loader_handle.h"

enum hal_global_csr {
	MISC_CONTROL = 0xA04,
	ICP_RESET = 0xA0c,
	ICP_GLOBAL_CLK_ENABLE = 0xA50
};

enum {
	MISC_CONTROL_C4XXX = 0xAA0,
	ICP_RESET_CPP0 = 0x938,
	ICP_RESET_CPP1 = 0x93c,
	ICP_GLOBAL_CLK_ENABLE_CPP0 = 0x964,
	ICP_GLOBAL_CLK_ENABLE_CPP1 = 0x968
};

enum hal_ae_csr {
	USTORE_ADDRESS = 0x000,
	USTORE_DATA_LOWER = 0x004,
	USTORE_DATA_UPPER = 0x008,
	ALU_OUT = 0x010,
	CTX_ARB_CNTL = 0x014,
	CTX_ENABLES = 0x018,
	CC_ENABLE = 0x01c,
	CSR_CTX_POINTER = 0x020,
	CTX_STS_INDIRECT = 0x040,
	ACTIVE_CTX_STATUS = 0x044,
	CTX_SIG_EVENTS_INDIRECT = 0x048,
	CTX_SIG_EVENTS_ACTIVE = 0x04c,
	CTX_WAKEUP_EVENTS_INDIRECT = 0x050,
	LM_ADDR_0_INDIRECT = 0x060,
	LM_ADDR_1_INDIRECT = 0x068,
	LM_ADDR_2_INDIRECT = 0x0cc,
	LM_ADDR_3_INDIRECT = 0x0d4,
	INDIRECT_LM_ADDR_0_BYTE_INDEX = 0x0e0,
	INDIRECT_LM_ADDR_1_BYTE_INDEX = 0x0e8,
	INDIRECT_LM_ADDR_2_BYTE_INDEX = 0x10c,
	INDIRECT_LM_ADDR_3_BYTE_INDEX = 0x114,
	INDIRECT_T_INDEX = 0x0f8,
	INDIRECT_T_INDEX_BYTE_INDEX = 0x0fc,
	FUTURE_COUNT_SIGNAL_INDIRECT = 0x078,
	TIMESTAMP_LOW = 0x0c0,
	TIMESTAMP_HIGH = 0x0c4,
	PROFILE_COUNT = 0x144,
	SIGNATURE_ENABLE = 0x150,
	AE_MISC_CONTROL = 0x160,
	LOCAL_CSR_STATUS = 0x180,
};

enum fcu_csr {
	FCU_CONTROL           = 0x8c0,
	FCU_STATUS            = 0x8c4,
	FCU_STATUS1           = 0x8c8,
	FCU_DRAM_ADDR_LO      = 0x8cc,
	FCU_DRAM_ADDR_HI      = 0x8d0,
	FCU_RAMBASE_ADDR_HI   = 0x8d4,
	FCU_RAMBASE_ADDR_LO   = 0x8d8
};

enum fcu_csr_4xxx {
	FCU_CONTROL_4XXX           = 0x1000,
	FCU_STATUS_4XXX            = 0x1004,
	FCU_ME_BROADCAST_MASK_TYPE = 0x1008,
	FCU_AE_LOADED_4XXX         = 0x1010,
	FCU_DRAM_ADDR_LO_4XXX      = 0x1014,
	FCU_DRAM_ADDR_HI_4XXX      = 0x1018,
};

enum fcu_cmd {
	FCU_CTRL_CMD_NOOP  = 0,
	FCU_CTRL_CMD_AUTH  = 1,
	FCU_CTRL_CMD_LOAD  = 2,
	FCU_CTRL_CMD_START = 3
};

enum fcu_sts {
	FCU_STS_NO_STS    = 0,
	FCU_STS_VERI_DONE = 1,
	FCU_STS_LOAD_DONE = 2,
	FCU_STS_VERI_FAIL = 3,
	FCU_STS_LOAD_FAIL = 4,
	FCU_STS_BUSY      = 5
};

#define ALL_AE_MASK                 0xFFFFFFFF
#define UA_ECS                      (0x1 << 31)
#define ACS_ABO_BITPOS              31
#define ACS_ACNO                    0x7
#define CE_ENABLE_BITPOS            0x8
#define CE_LMADDR_0_GLOBAL_BITPOS   16
#define CE_LMADDR_1_GLOBAL_BITPOS   17
#define CE_LMADDR_2_GLOBAL_BITPOS   22
#define CE_LMADDR_3_GLOBAL_BITPOS   23
#define CE_T_INDEX_GLOBAL_BITPOS    21
#define CE_NN_MODE_BITPOS           20
#define CE_REG_PAR_ERR_BITPOS       25
#define CE_BREAKPOINT_BITPOS        27
#define CE_CNTL_STORE_PARITY_ERROR_BITPOS 29
#define CE_INUSE_CONTEXTS_BITPOS    31
#define CE_NN_MODE                  (0x1 << CE_NN_MODE_BITPOS)
#define CE_INUSE_CONTEXTS           (0x1 << CE_INUSE_CONTEXTS_BITPOS)
#define XCWE_VOLUNTARY              (0x1)
#define LCS_STATUS          (0x1)
#define MMC_SHARE_CS_BITPOS         2
#define WAKEUP_EVENT 0x10000
#define FCU_CTRL_BROADCAST_POS   0x4
#define FCU_CTRL_AE_POS     0x8
#define FCU_AUTH_STS_MASK   0x7
#define FCU_STS_DONE_POS    0x9
#define FCU_STS_AUTHFWLD_POS 0X8
#define FCU_LOADED_AE_POS   0x16
#define FW_AUTH_WAIT_PERIOD 10
#define FW_AUTH_MAX_RETRY   300
#define ICP_QAT_AE_OFFSET 0x20000
#define ICP_QAT_CAP_OFFSET (ICP_QAT_AE_OFFSET + 0x10000)
#define LOCAL_TO_XFER_REG_OFFSET 0x800
#define ICP_QAT_EP_OFFSET 0x3a000
#define ICP_QAT_EP_OFFSET_4XXX   0x200000 /* HI MMIO CSRs */
#define ICP_QAT_AE_OFFSET_4XXX   0x600000
#define ICP_QAT_CAP_OFFSET_4XXX  0x640000
#define SET_CAP_CSR(handle, csr, val) \
	ADF_CSR_WR((handle)->hal_cap_g_ctl_csr_addr_v, csr, val)
#define GET_CAP_CSR(handle, csr) \
	ADF_CSR_RD((handle)->hal_cap_g_ctl_csr_addr_v, csr)
#define AE_CSR(handle, ae) \
	((char __iomem *)(handle)->hal_cap_ae_local_csr_addr_v + ((ae) << 12))
#define AE_CSR_ADDR(handle, ae, csr) (AE_CSR(handle, ae) + (0x3ff & (csr)))
#define SET_AE_CSR(handle, ae, csr, val) \
	ADF_CSR_WR(AE_CSR_ADDR(handle, ae, csr), 0, val)
#define GET_AE_CSR(handle, ae, csr) ADF_CSR_RD(AE_CSR_ADDR(handle, ae, csr), 0)
#define AE_XFER(handle, ae) \
	((char __iomem *)(handle)->hal_cap_ae_xfer_csr_addr_v + ((ae) << 12))
#define AE_XFER_ADDR(handle, ae, reg) (AE_XFER(handle, ae) + \
	(((reg) & 0xff) << 2))
#define SET_AE_XFER(handle, ae, reg, val) \
	ADF_CSR_WR(AE_XFER_ADDR(handle, ae, reg), 0, val)
#define SRAM_WRITE(handle, addr, val) \
	ADF_CSR_WR((handle)->hal_sram_addr_v, addr, val)
#endif
