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
#ifndef __ICP_QAT_FW_LOADER_HANDLE_H__
#define __ICP_QAT_FW_LOADER_HANDLE_H__
#include "icp_qat_uclo.h"

struct icp_qat_fw_loader_ae_data {
	unsigned int state;
	unsigned int ustore_size;
	unsigned int free_addr;
	unsigned int free_size;
	unsigned int live_ctx_mask;
};

struct icp_qat_fw_loader_hal_handle {
	struct icp_qat_fw_loader_ae_data aes[ICP_QAT_UCLO_MAX_AE];
	unsigned int ae_mask;
	unsigned int slice_mask;
	unsigned int revision_id;
	unsigned int ae_max_num;
	unsigned int upc_mask;
	unsigned int max_ustore;
};

struct icp_qat_fw_loader_handle {
	struct icp_qat_fw_loader_hal_handle *hal_handle;
	void *obj_handle;
	void __iomem *hal_sram_addr_v;
	void __iomem *hal_cap_g_ctl_csr_addr_v;
	void __iomem *hal_cap_ae_xfer_csr_addr_v;
	void __iomem *hal_cap_ae_local_csr_addr_v;
	void __iomem *hal_ep_csr_addr_v;
};
#endif
