/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#ifndef __ICP_QAT_HAL_H
#define __ICP_QAT_HAL_H
#include "icp_qat_fw_loader_handle.h"

enum hal_global_csr {
	MISC_CONTROL = 0x04,
	ICP_RESET = 0x0c,
	ICP_GLOBAL_CLK_ENABLE = 0x50
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
	INDIRECT_LM_ADDR_0_BYTE_INDEX = 0x0e0,
	INDIRECT_LM_ADDR_1_BYTE_INDEX = 0x0e8,
	FUTURE_COUNT_SIGNAL_INDIRECT = 0x078,
	TIMESTAMP_LOW = 0x0c0,
	TIMESTAMP_HIGH = 0x0c4,
	PROFILE_COUNT = 0x144,
	SIGNATURE_ENABLE = 0x150,
	AE_MISC_CONTROL = 0x160,
	LOCAL_CSR_STATUS = 0x180,
};

#define UA_ECS                      (0x1 << 31)
#define ACS_ABO_BITPOS              31
#define ACS_ACNO                    0x7
#define CE_ENABLE_BITPOS            0x8
#define CE_LMADDR_0_GLOBAL_BITPOS   16
#define CE_LMADDR_1_GLOBAL_BITPOS   17
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
#define GLOBAL_CSR                0xA00

#define SET_CAP_CSR(handle, csr, val) \
	ADF_CSR_WR(handle->hal_cap_g_ctl_csr_addr_v, csr, val)
#define GET_CAP_CSR(handle, csr) \
	ADF_CSR_RD(handle->hal_cap_g_ctl_csr_addr_v, csr)
#define SET_GLB_CSR(handle, csr, val) SET_CAP_CSR(handle, csr + GLOBAL_CSR, val)
#define GET_GLB_CSR(handle, csr) GET_CAP_CSR(handle, GLOBAL_CSR + csr)
#define AE_CSR(handle, ae) \
	(handle->hal_cap_ae_local_csr_addr_v + \
	((ae & handle->hal_handle->ae_mask) << 12))
#define AE_CSR_ADDR(handle, ae, csr) (AE_CSR(handle, ae) + (0x3ff & csr))
#define SET_AE_CSR(handle, ae, csr, val) \
	ADF_CSR_WR(AE_CSR_ADDR(handle, ae, csr), 0, val)
#define GET_AE_CSR(handle, ae, csr) ADF_CSR_RD(AE_CSR_ADDR(handle, ae, csr), 0)
#define AE_XFER(handle, ae) \
	(handle->hal_cap_ae_xfer_csr_addr_v + \
	((ae & handle->hal_handle->ae_mask) << 12))
#define AE_XFER_ADDR(handle, ae, reg) (AE_XFER(handle, ae) + \
	((reg & 0xff) << 2))
#define SET_AE_XFER(handle, ae, reg, val) \
	ADF_CSR_WR(AE_XFER_ADDR(handle, ae, reg), 0, val)
#define SRAM_WRITE(handle, addr, val) \
	ADF_CSR_WR(handle->hal_sram_addr_v, addr, val)
#define SRAM_READ(handle, addr) ADF_CSR_RD(handle->hal_sram_addr_v, addr)
#endif
