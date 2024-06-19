/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

	TP_printk("PTI:%s:%u:%llu", __entry->pchan_name,
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

	TP_printk("PTO:%s:%u:%llu", __entry->pchan_name,
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

	TP_printk("VTI:%s:%u:%llu", __entry->pchan_name,
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

	TP_printk("VTO:%s:%u:%llu", __entry->pchan_name,
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

	TP_printk("PRI:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

TRACE_EVENT(hab_pchan_recv_wakeup,

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

	TP_printk("PRW:%s:%u:%llu", __entry->pchan_name,
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

	TP_printk("VRO:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

/* Virtio HAB reclaim in-buf path */
TRACE_EVENT(hab_recv_txq_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RECV_TXQ_ST:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_recv_txq_end,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RECV_TXQ_END:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

/* Virtio HAB recv path */
TRACE_EVENT(hab_recv_rxq_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_rx = pchan->sequence_rx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RECV_RXQ_ST:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

TRACE_EVENT(hab_recv_rxq_end,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_rx = pchan->sequence_rx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RECV_RXQ_END:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

/* Vhost HAB send path */
TRACE_EVENT(hab_rxworker_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx + 1;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RXWST:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_rxworker_end,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RXWEND:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

TRACE_EVENT(hab_rxworker_send_one,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_tx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_tx = pchan->sequence_tx;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("RXWSO:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_tx, __entry->mpm_tv)
);

/* Vhost HAB recv path */
TRACE_EVENT(hab_txworker_start,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_rx = pchan->sequence_rx + 1;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("TXWST:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

TRACE_EVENT(hab_txworker_end,

	TP_PROTO(struct physical_channel *pchan),

	TP_ARGS(pchan),

	TP_STRUCT__entry(
		__array(char, pchan_name, MAX_VMID_NAME_SIZE)
		__field(uint32_t, seq_rx)
		__field(unsigned long long, mpm_tv)
	),

	TP_fast_assign(
		memcpy(__entry->pchan_name, pchan->name,
			MAX_VMID_NAME_SIZE);
		__entry->seq_rx = pchan->sequence_rx + 1;
		__entry->mpm_tv = msm_timer_get_sclk_ticks();
	),

	TP_printk("TXWEND:%s:%u:%llu", __entry->pchan_name,
			__entry->seq_rx, __entry->mpm_tv)
);

#endif /* _TRACE_HAB_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE hab_trace_os

#include <trace/define_trace.h>
