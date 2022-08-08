/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/******************************************************************************
 *
 * Copyright(c) 2020-2021 Intel Corporation
 *
 *****************************************************************************/

#ifndef __IWL_PNVM_H__
#define __IWL_PNVM_H__

#include "fw/notif-wait.h"

#define MVM_UCODE_PNVM_TIMEOUT	(HZ / 4)

int iwl_pnvm_load(struct iwl_trans *trans,
		  struct iwl_notif_wait_data *notif_wait);

#endif /* __IWL_PNVM_H__ */
