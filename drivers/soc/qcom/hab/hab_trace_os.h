/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hab

#if !defined(_TRACE_HAB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HAB_H
#include <linux/tracepoint.h>
#include "hab.h"

/* send path */
TRACE_EVENT(hab_pchan_send_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name, MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx + 1;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("PTI:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_pchan_send_done,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name, MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("PTO:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_vchan_send_start,

	TP_PROTO(struct virtual_channel *vchan),

	TP_ARGS(vchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, vchan->pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = vchan->pchan->sequence_tx + 1;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("VTI:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_vchan_send_done,

	TP_PROTO(struct virtual_channel *vchan),

	TP_ARGS(vchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, vchan->pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = vchan->pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("VTO:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

/* receive path */
TRACE_EVENT(hab_pchan_recv_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name, MAX_VMID_NAME_SIZE);
		__entry->seq_rx = pchan->sequence_rx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("PRI:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

TRACE_EVENT(hab_vchan_recv_done,

	TP_PROTO(struct virtual_channel *vchan,
		struct hab_message *msg),

	TP_ARGS(vchan, msg),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, vchan->pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_rx = msg->sequence_rx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("VRO:%s:%u:%llu\n", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

#endif /* _TRACE_HAB_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hab_trace_os

#include <trace/define_trace.h>
