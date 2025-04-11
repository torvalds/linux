/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_coex_h__
#define __iwl_mld_coex_h__

#include "mld.h"

int iwl_mld_send_bt_init_conf(struct iwl_mld *mld);

void iwl_mld_handle_bt_coex_notif(struct iwl_mld *mld,
				  struct iwl_rx_packet *pkt);

#endif /* __iwl_mld_coex_h__ */
