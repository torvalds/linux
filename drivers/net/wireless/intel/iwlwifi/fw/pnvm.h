/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2020-2023, 2025 Intel Corporation
 */
#ifndef __IWL_PNVM_H__
#define __IWL_PNVM_H__

#include "iwl-drv.h"
#include "fw/notif-wait.h"

#define MVM_UCODE_PNVM_TIMEOUT	(HZ / 4)

#define MAX_PNVM_NAME  64

int iwl_pnvm_load(struct iwl_trans *trans,
		  struct iwl_notif_wait_data *notif_wait,
		  const struct iwl_ucode_capabilities *capa,
		  __le32 sku_id[3]);

static inline
void iwl_pnvm_get_fs_name(struct iwl_trans *trans,
			  u8 *pnvm_name, size_t max_len)
{
	char _fw_name_pre[FW_NAME_PRE_BUFSIZE];

	snprintf(pnvm_name, max_len, "%s.pnvm",
		 iwl_drv_get_fwname_pre(trans, _fw_name_pre));
}

#endif /* __IWL_PNVM_H__ */
