/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright(c) 2020-2023 Intel Corporation
 */
#ifndef __IWL_PNVM_H__
#define __IWL_PNVM_H__

#include "iwl-drv.h"
#include "fw/notif-wait.h"

#define MVM_UCODE_PNVM_TIMEOUT	(HZ / 4)

#define MAX_PNVM_NAME  64

int iwl_pnvm_load(struct iwl_trans *trans,
		  struct iwl_notif_wait_data *notif_wait,
		  const struct iwl_ucode_capabilities *capa);

static inline
void iwl_pnvm_get_fs_name(struct iwl_trans *trans,
			  u8 *pnvm_name, size_t max_len)
{
	char _fw_name_pre[FW_NAME_PRE_BUFSIZE];
	const char *fw_name_pre;
	int pre_len;

	fw_name_pre = iwl_drv_get_fwname_pre(trans, _fw_name_pre);

	/*
	 * The prefix unfortunately includes a hyphen at the end, so
	 * don't add the dot here...
	 */
	snprintf(pnvm_name, max_len, "%spnvm", fw_name_pre);

	/* ...but replace the hyphen with the dot here. */
	pre_len = strlen(fw_name_pre);
	if (pre_len < max_len && pre_len > 0)
		pnvm_name[pre_len - 1] = '.';
}

#endif /* __IWL_PNVM_H__ */
