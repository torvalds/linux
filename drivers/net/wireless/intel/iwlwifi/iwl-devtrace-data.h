/* SPDX-License-Identifier: GPL-2.0-only */
/******************************************************************************
 *
 * Copyright(c) 2009 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015        Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#if !defined(__IWLWIFI_DEVICE_TRACE_DATA) || defined(TRACE_HEADER_MULTI_READ)
#define __IWLWIFI_DEVICE_TRACE_DATA

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM iwlwifi_data

TRACE_EVENT(iwlwifi_dev_tx_tb,
	TP_PROTO(const struct device *dev, struct sk_buff *skb,
		 u8 *data_src, size_t data_len),
	TP_ARGS(dev, skb, data_src, data_len),
	TP_STRUCT__entry(
		DEV_ENTRY

		__dynamic_array(u8, data,
				iwl_trace_data(skb) ? data_len : 0)
	),
	TP_fast_assign(
		DEV_ASSIGN;
		if (iwl_trace_data(skb))
			memcpy(__get_dynamic_array(data), data_src, data_len);
	),
	TP_printk("[%s] TX frame data", __get_str(dev))
);

TRACE_EVENT(iwlwifi_dev_rx_data,
	TP_PROTO(const struct device *dev,
		 const struct iwl_trans *trans,
		 void *rxbuf, size_t len),
	TP_ARGS(dev, trans, rxbuf, len),
	TP_STRUCT__entry(
		DEV_ENTRY
		__dynamic_array(u8, data,
				len - iwl_rx_trace_len(trans, rxbuf, len, NULL))
	),
	TP_fast_assign(
		size_t offs = iwl_rx_trace_len(trans, rxbuf, len, NULL);
		DEV_ASSIGN;
		if (offs < len)
			memcpy(__get_dynamic_array(data),
			       ((u8 *)rxbuf) + offs, len - offs);
	),
	TP_printk("[%s] RX frame data", __get_str(dev))
);
#endif /* __IWLWIFI_DEVICE_TRACE_DATA */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE iwl-devtrace-data
#include <trace/define_trace.h>
