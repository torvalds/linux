/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mhi_host

#if !defined(_TRACE_EVENT_MHI_HOST_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EVENT_MHI_HOST_H

#include <linux/byteorder/generic.h>
#include <linux/tracepoint.h>
#include <linux/trace_seq.h>
#include "../common.h"
#include "internal.h"

#undef mhi_state
#undef mhi_state_end

#define mhi_state(a, b)		TRACE_DEFINE_ENUM(MHI_STATE_##a);
#define mhi_state_end(a, b)	TRACE_DEFINE_ENUM(MHI_STATE_##a);

MHI_STATE_LIST

#undef mhi_state
#undef mhi_state_end

#define mhi_state(a, b)		{ MHI_STATE_##a, b },
#define mhi_state_end(a, b)	{ MHI_STATE_##a, b }

#undef mhi_pm_state
#undef mhi_pm_state_end

#define mhi_pm_state(a, b)		TRACE_DEFINE_ENUM(MHI_PM_STATE_##a);
#define mhi_pm_state_end(a, b)		TRACE_DEFINE_ENUM(MHI_PM_STATE_##a);

MHI_PM_STATE_LIST

#undef mhi_pm_state
#undef mhi_pm_state_end

#define mhi_pm_state(a, b)		{ MHI_PM_STATE_##a, b },
#define mhi_pm_state_end(a, b)		{ MHI_PM_STATE_##a, b }

#undef mhi_ee
#undef mhi_ee_end

#define mhi_ee(a, b)			TRACE_DEFINE_ENUM(MHI_EE_##a);
#define mhi_ee_end(a, b)		TRACE_DEFINE_ENUM(MHI_EE_##a);

MHI_EE_LIST

#undef mhi_ee
#undef mhi_ee_end

#define mhi_ee(a, b)			{ MHI_EE_##a, b },
#define mhi_ee_end(a, b)		{ MHI_EE_##a, b }

#undef ch_state_type
#undef ch_state_type_end

#define ch_state_type(a, b)		TRACE_DEFINE_ENUM(MHI_CH_STATE_TYPE_##a);
#define ch_state_type_end(a, b)		TRACE_DEFINE_ENUM(MHI_CH_STATE_TYPE_##a);

MHI_CH_STATE_TYPE_LIST

#undef ch_state_type
#undef ch_state_type_end

#define ch_state_type(a, b)		{ MHI_CH_STATE_TYPE_##a, b },
#define ch_state_type_end(a, b)		{ MHI_CH_STATE_TYPE_##a, b }

#undef dev_st_trans
#undef dev_st_trans_end

#define dev_st_trans(a, b)		TRACE_DEFINE_ENUM(DEV_ST_TRANSITION_##a);
#define dev_st_trans_end(a, b)		TRACE_DEFINE_ENUM(DEV_ST_TRANSITION_##a);

DEV_ST_TRANSITION_LIST

#undef dev_st_trans
#undef dev_st_trans_end

#define dev_st_trans(a, b)		{ DEV_ST_TRANSITION_##a, b },
#define dev_st_trans_end(a, b)		{ DEV_ST_TRANSITION_##a, b }

#define TPS(x)	tracepoint_string(x)

TRACE_EVENT(mhi_gen_tre,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan,
		 struct mhi_ring_element *mhi_tre),

	TP_ARGS(mhi_cntrl, mhi_chan, mhi_tre),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(int, ch_num)
		__field(void *, wp)
		__field(uint64_t, tre_ptr)
		__field(uint32_t, dword0)
		__field(uint32_t, dword1)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->ch_num = mhi_chan->chan;
		__entry->wp = mhi_tre;
		__entry->tre_ptr = le64_to_cpu(mhi_tre->ptr);
		__entry->dword0 = le32_to_cpu(mhi_tre->dword[0]);
		__entry->dword1 = le32_to_cpu(mhi_tre->dword[1]);
	),

	TP_printk("%s: Chan: %d TRE: 0x%p TRE buf: 0x%llx DWORD0: 0x%08x DWORD1: 0x%08x\n",
		  __get_str(name), __entry->ch_num, __entry->wp, __entry->tre_ptr,
		  __entry->dword0, __entry->dword1)
);

TRACE_EVENT(mhi_intvec_states,

	TP_PROTO(struct mhi_controller *mhi_cntrl, int dev_ee, int dev_state),

	TP_ARGS(mhi_cntrl, dev_ee, dev_state),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(int, local_ee)
		__field(int, state)
		__field(int, dev_ee)
		__field(int, dev_state)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->local_ee = mhi_cntrl->ee;
		__entry->state = mhi_cntrl->dev_state;
		__entry->dev_ee = dev_ee;
		__entry->dev_state = dev_state;
	),

	TP_printk("%s: Local EE: %s State: %s Device EE: %s Dev State: %s\n",
		  __get_str(name),
		  __print_symbolic(__entry->local_ee, MHI_EE_LIST),
		  __print_symbolic(__entry->state, MHI_STATE_LIST),
		  __print_symbolic(__entry->dev_ee, MHI_EE_LIST),
		  __print_symbolic(__entry->dev_state, MHI_STATE_LIST))
);

TRACE_EVENT(mhi_tryset_pm_state,

	TP_PROTO(struct mhi_controller *mhi_cntrl, int pm_state),

	TP_ARGS(mhi_cntrl, pm_state),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(int, pm_state)
	),

	TP_fast_assign(
		__assign_str(name);
		if (pm_state)
			pm_state = __fls(pm_state);
		__entry->pm_state = pm_state;
	),

	TP_printk("%s: PM state: %s\n", __get_str(name),
		  __print_symbolic(__entry->pm_state, MHI_PM_STATE_LIST))
);

DECLARE_EVENT_CLASS(mhi_process_event_ring,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_ring_element *rp),

	TP_ARGS(mhi_cntrl, rp),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(uint32_t, dword0)
		__field(uint32_t, dword1)
		__field(int, state)
		__field(uint64_t, ptr)
		__field(void *, rp)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->rp = rp;
		__entry->ptr = le64_to_cpu(rp->ptr);
		__entry->dword0 = le32_to_cpu(rp->dword[0]);
		__entry->dword1 = le32_to_cpu(rp->dword[1]);
		__entry->state = MHI_TRE_GET_EV_STATE(rp);
	),

	TP_printk("%s: TRE: 0x%p TRE buf: 0x%llx DWORD0: 0x%08x DWORD1: 0x%08x State: %s\n",
		  __get_str(name), __entry->rp, __entry->ptr, __entry->dword0,
		  __entry->dword1, __print_symbolic(__entry->state, MHI_STATE_LIST))
);

DEFINE_EVENT(mhi_process_event_ring, mhi_data_event,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_ring_element *rp),

	TP_ARGS(mhi_cntrl, rp)
);

DEFINE_EVENT(mhi_process_event_ring, mhi_ctrl_event,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_ring_element *rp),

	TP_ARGS(mhi_cntrl, rp)
);

DECLARE_EVENT_CLASS(mhi_update_channel_state,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan, int state,
		 const char *reason),

	TP_ARGS(mhi_cntrl, mhi_chan, state, reason),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(int, ch_num)
		__field(int, state)
		__field(const char *, reason)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->ch_num = mhi_chan->chan;
		__entry->state = state;
		__entry->reason = reason;
	),

	TP_printk("%s: chan%d: %s state to: %s\n",
		  __get_str(name),  __entry->ch_num, __entry->reason,
		  __print_symbolic(__entry->state, MHI_CH_STATE_TYPE_LIST))
);

DEFINE_EVENT(mhi_update_channel_state, mhi_channel_command_start,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan, int state,
		 const char *reason),

	TP_ARGS(mhi_cntrl, mhi_chan, state, reason)
);

DEFINE_EVENT(mhi_update_channel_state, mhi_channel_command_end,

	TP_PROTO(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan, int state,
		 const char *reason),

	TP_ARGS(mhi_cntrl, mhi_chan, state, reason)
);

TRACE_EVENT(mhi_pm_st_transition,

	TP_PROTO(struct mhi_controller *mhi_cntrl, int state),

	TP_ARGS(mhi_cntrl, state),

	TP_STRUCT__entry(
		__string(name, mhi_cntrl->mhi_dev->name)
		__field(int, state)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->state = state;
	),

	TP_printk("%s: Handling state transition: %s\n", __get_str(name),
		  __print_symbolic(__entry->state, DEV_ST_TRANSITION_LIST))
);

#endif
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/bus/mhi/host
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
