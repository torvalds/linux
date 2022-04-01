// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef __IWLMEI_INTERNAL_H_
#define __IWLMEI_INTERNAL_H_

#include <uapi/linux/if_ether.h>
#include <linux/netdevice.h>

#include "sap.h"

rx_handler_result_t iwl_mei_rx_filter(struct sk_buff *skb,
				      const struct iwl_sap_oob_filters *filters,
				      bool *pass_to_csme);

void iwl_mei_add_data_to_ring(struct sk_buff *skb, bool cb_tx);

#endif /* __IWLMEI_INTERNAL_H_ */
