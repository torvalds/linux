/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2018, 2020-2026 Intel Corporation
 */
#ifndef __iwl_context_info_file_v2_h__
#define __iwl_context_info_file_v2_h__

#include "fw/api/ctxt-info-v2.h"

int iwl_pcie_ctxt_info_v2_alloc(struct iwl_trans *trans,
				const struct iwl_fw *fw,
				const struct fw_img *img);
void iwl_pcie_ctxt_info_v2_kick(struct iwl_trans *trans);
void iwl_pcie_ctxt_info_v2_free(struct iwl_trans *trans, bool alive);

int iwl_trans_pcie_ctx_info_v2_load_pnvm(struct iwl_trans *trans,
					 const struct iwl_pnvm_image *pnvm_payloads,
					 const struct iwl_ucode_capabilities *capa);
void iwl_trans_pcie_ctx_info_v2_set_pnvm(struct iwl_trans *trans,
					 const struct iwl_ucode_capabilities *capa);
int
iwl_trans_pcie_ctx_info_v2_load_reduce_power(struct iwl_trans *trans,
					     const struct iwl_pnvm_image *payloads,
					     const struct iwl_ucode_capabilities *capa);
void
iwl_trans_pcie_ctx_info_v2_set_reduce_power(struct iwl_trans *trans,
					    const struct iwl_ucode_capabilities *capa);
#endif /* __iwl_context_info_file_v2_h__ */
