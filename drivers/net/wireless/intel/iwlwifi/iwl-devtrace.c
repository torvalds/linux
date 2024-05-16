// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright (C) 2018, 2023 Intel Corporation
 *****************************************************************************/

#include <linux/module.h>

/* sparse doesn't like tracepoint macros */
#ifndef __CHECKER__
#include "iwl-trans.h"

#define CREATE_TRACE_POINTS
#ifdef CONFIG_CC_IS_GCC
#pragma GCC diagnostic ignored "-Wsuggest-attribute=format"
#endif
#include "iwl-devtrace.h"

EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_cont_event);
EXPORT_TRACEPOINT_SYMBOL(iwlwifi_dev_ucode_wrap_event);
#else
#include "iwl-devtrace.h"
#endif /* __CHECKER__ */

void __trace_iwlwifi_dev_rx(struct iwl_trans *trans, void *pkt, size_t len)
{
	size_t hdr_offset = 0, trace_len;

	trace_len = iwl_rx_trace_len(trans, pkt, len, &hdr_offset);
	trace_iwlwifi_dev_rx(trans->dev, pkt, len, trace_len, hdr_offset);

	if (trace_len < len)
		trace_iwlwifi_dev_rx_data(trans->dev, pkt, len, trace_len);
}
