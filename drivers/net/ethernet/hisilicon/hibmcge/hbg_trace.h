/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2025 Hisilicon Limited. */

/* This must be outside ifdef _HBG_TRACE_H */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hibmcge

#if !defined(_HBG_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _HBG_TRACE_H_

#include <linux/bitfield.h>
#include <linux/pci.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include "hbg_reg.h"

TRACE_EVENT(hbg_rx_desc,
	    TP_PROTO(struct hbg_priv *priv, u32 index,
		     struct hbg_rx_desc *rx_desc),
	    TP_ARGS(priv, index, rx_desc),

	    TP_STRUCT__entry(__field(u32, index)
			     __field(u8, port_num)
			     __field(u8, ip_offset)
			     __field(u8, parse_mode)
			     __field(u8, l4_error_code)
			     __field(u8, l3_error_code)
			     __field(u8, l2_error_code)
			     __field(u16, packet_len)
			     __field(u16, valid_size)
			     __field(u16, vlan)
			     __string(pciname, pci_name(priv->pdev))
			     __string(devname, priv->netdev->name)
	    ),

	    TP_fast_assign(__entry->index = index,
			   __entry->packet_len =
				FIELD_GET(HBG_RX_DESC_W2_PKT_LEN_M,
					  rx_desc->word2);
			   __entry->port_num =
				FIELD_GET(HBG_RX_DESC_W2_PORT_NUM_M,
					  rx_desc->word2);
			   __entry->ip_offset =
				FIELD_GET(HBG_RX_DESC_W3_IP_OFFSET_M,
					  rx_desc->word3);
			   __entry->vlan =
				FIELD_GET(HBG_RX_DESC_W3_VLAN_M,
					  rx_desc->word3);
			   __entry->parse_mode =
				FIELD_GET(HBG_RX_DESC_W4_PARSE_MODE_M,
					  rx_desc->word4);
			   __entry->l4_error_code =
				FIELD_GET(HBG_RX_DESC_W4_L4_ERR_CODE_M,
					  rx_desc->word4);
			   __entry->l3_error_code =
				FIELD_GET(HBG_RX_DESC_W4_L3_ERR_CODE_M,
					  rx_desc->word4);
			   __entry->l2_error_code =
				FIELD_GET(HBG_RX_DESC_W4_L2_ERR_B,
					  rx_desc->word4);
			   __entry->valid_size =
				FIELD_GET(HBG_RX_DESC_W5_VALID_SIZE_M,
					  rx_desc->word5);
			   __assign_str(pciname);
			   __assign_str(devname);
	    ),

	    TP_printk("%s %s index:%u, port num:%u, len:%u, valid size:%u, ip_offset:%u, vlan:0x%04x, parse mode:%u, l4_err:0x%x, l3_err:0x%x, l2_err:0x%x",
		      __get_str(pciname), __get_str(devname), __entry->index,
		      __entry->port_num, __entry->packet_len,
		      __entry->valid_size, __entry->ip_offset,  __entry->vlan,
		      __entry->parse_mode, __entry->l4_error_code,
		      __entry->l3_error_code, __entry->l2_error_code
	    )
);

#endif /* _HBG_TRACE_H_ */

/* This must be outside ifdef _HBG_TRACE_H */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hbg_trace
#include <trace/define_trace.h>
