/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020, 2022, 2024-2026 Intel Corporation
 */
#ifndef __iwl_context_info_file_h__
#define __iwl_context_info_file_h__

#include "fw/api/ctxt-info.h"

int iwl_pcie_ctxt_info_init(struct iwl_trans *trans, const struct fw_img *img);
void iwl_pcie_ctxt_info_free(struct iwl_trans *trans);
void iwl_pcie_ctxt_info_free_paging(struct iwl_trans *trans);
int iwl_pcie_init_fw_sec(struct iwl_trans *trans,
			 const struct fw_img *fw,
			 struct iwl_context_info_dram_nonfseq *ctxt_dram);
void *iwl_pcie_ctxt_info_dma_alloc_coherent(struct iwl_trans *trans,
					    size_t size,
					    dma_addr_t *phys);
int iwl_pcie_ctxt_info_alloc_dma(struct iwl_trans *trans,
				 const void *data, u32 len,
				 struct iwl_dram_data *dram);

#endif /* __iwl_context_info_file_h__ */
