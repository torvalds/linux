/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
 */

#if !defined(_DPU_TRACE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _DPU_TRACE_H_

#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/tracepoint.h>

#include <drm/drm_rect.h>
#include "dpu_crtc.h"
#include "dpu_encoder_phys.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_vbif.h"
#include "dpu_plane.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dpu
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE dpu_trace

TRACE_EVENT(dpu_perf_set_qos_luts,
	TP_PROTO(u32 pnum, u32 fmt, bool rt, u32 fl,
		u32 lut, u32 lut_usage),
	TP_ARGS(pnum, fmt, rt, fl, lut, lut_usage),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, fmt)
			__field(bool, rt)
			__field(u32, fl)
			__field(u64, lut)
			__field(u32, lut_usage)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->fmt = fmt;
			__entry->rt = rt;
			__entry->fl = fl;
			__entry->lut = lut;
			__entry->lut_usage = lut_usage;
	),
	TP_printk("pnum=%d fmt=%x rt=%d fl=%d lut=0x%llx lut_usage=%d",
			__entry->pnum, __entry->fmt,
			__entry->rt, __entry->fl,
			__entry->lut, __entry->lut_usage)
);

TRACE_EVENT(dpu_perf_set_danger_luts,
	TP_PROTO(u32 pnum, u32 fmt, u32 mode, u32 danger_lut,
		u32 safe_lut),
	TP_ARGS(pnum, fmt, mode, danger_lut, safe_lut),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, fmt)
			__field(u32, mode)
			__field(u32, danger_lut)
			__field(u32, safe_lut)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->fmt = fmt;
			__entry->mode = mode;
			__entry->danger_lut = danger_lut;
			__entry->safe_lut = safe_lut;
	),
	TP_printk("pnum=%d fmt=%x mode=%d luts[0x%x, 0x%x]",
			__entry->pnum, __entry->fmt,
			__entry->mode, __entry->danger_lut,
			__entry->safe_lut)
);

TRACE_EVENT(dpu_perf_set_ot,
	TP_PROTO(u32 pnum, u32 xin_id, u32 rd_lim, u32 vbif_idx),
	TP_ARGS(pnum, xin_id, rd_lim, vbif_idx),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, xin_id)
			__field(u32, rd_lim)
			__field(u32, vbif_idx)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->xin_id = xin_id;
			__entry->rd_lim = rd_lim;
			__entry->vbif_idx = vbif_idx;
	),
	TP_printk("pnum:%d xin_id:%d ot:%d vbif:%d",
			__entry->pnum, __entry->xin_id, __entry->rd_lim,
			__entry->vbif_idx)
)

TRACE_EVENT(dpu_cmd_release_bw,
	TP_PROTO(u32 crtc_id),
	TP_ARGS(crtc_id),
	TP_STRUCT__entry(
			__field(u32, crtc_id)
	),
	TP_fast_assign(
			__entry->crtc_id = crtc_id;
	),
	TP_printk("crtc:%d", __entry->crtc_id)
);

TRACE_EVENT(tracing_mark_write,
	TP_PROTO(int pid, const char *name, bool trace_begin),
	TP_ARGS(pid, name, trace_begin),
	TP_STRUCT__entry(
			__field(int, pid)
			__string(trace_name, name)
			__field(bool, trace_begin)
	),
	TP_fast_assign(
			__entry->pid = pid;
			__assign_str(trace_name, name);
			__entry->trace_begin = trace_begin;
	),
	TP_printk("%s|%d|%s", __entry->trace_begin ? "B" : "E",
		__entry->pid, __get_str(trace_name))
)

TRACE_EVENT(dpu_trace_counter,
	TP_PROTO(int pid, char *name, int value),
	TP_ARGS(pid, name, value),
	TP_STRUCT__entry(
			__field(int, pid)
			__string(counter_name, name)
			__field(int, value)
	),
	TP_fast_assign(
			__entry->pid = current->tgid;
			__assign_str(counter_name, name);
			__entry->value = value;
	),
	TP_printk("%d|%s|%d", __entry->pid,
			__get_str(counter_name), __entry->value)
)

TRACE_EVENT(dpu_perf_crtc_update,
	TP_PROTO(u32 crtc, u64 bw_ctl, u32 core_clk_rate,
			bool stop_req, bool update_bus, bool update_clk),
	TP_ARGS(crtc, bw_ctl, core_clk_rate, stop_req, update_bus, update_clk),
	TP_STRUCT__entry(
			__field(u32, crtc)
			__field(u64, bw_ctl)
			__field(u32, core_clk_rate)
			__field(bool, stop_req)
			__field(u32, update_bus)
			__field(u32, update_clk)
	),
	TP_fast_assign(
			__entry->crtc = crtc;
			__entry->bw_ctl = bw_ctl;
			__entry->core_clk_rate = core_clk_rate;
			__entry->stop_req = stop_req;
			__entry->update_bus = update_bus;
			__entry->update_clk = update_clk;
	),
	 TP_printk(
		"crtc=%d bw_ctl=%llu clk_rate=%u stop_req=%d u_bus=%d u_clk=%d",
			__entry->crtc,
			__entry->bw_ctl,
			__entry->core_clk_rate,
			__entry->stop_req,
			__entry->update_bus,
			__entry->update_clk)
);

DECLARE_EVENT_CLASS(dpu_irq_template,
	TP_PROTO(unsigned int irq_reg, unsigned int irq_bit),
	TP_ARGS(irq_reg, irq_bit),
	TP_STRUCT__entry(
		__field(	unsigned int,		irq_reg		)
		__field(	unsigned int,		irq_bit		)
	),
	TP_fast_assign(
		__entry->irq_reg = irq_reg;
		__entry->irq_bit = irq_bit;
	),
	TP_printk("IRQ=[%d, %d]", __entry->irq_reg, __entry->irq_bit)
);
DEFINE_EVENT(dpu_irq_template, dpu_irq_register_success,
	TP_PROTO(unsigned int irq_reg, unsigned int irq_bit),
	TP_ARGS(irq_reg, irq_bit)
);
DEFINE_EVENT(dpu_irq_template, dpu_irq_unregister_success,
	TP_PROTO(unsigned int irq_reg, unsigned int irq_bit),
	TP_ARGS(irq_reg, irq_bit)
);

TRACE_EVENT(dpu_enc_irq_wait_success,
	TP_PROTO(uint32_t drm_id, void *func,
		 unsigned int irq_reg, unsigned int irq_bit, enum dpu_pingpong pp_idx, int atomic_cnt),
	TP_ARGS(drm_id, func, irq_reg, irq_bit, pp_idx, atomic_cnt),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	void *,			func		)
		__field(	unsigned int,		irq_reg		)
		__field(	unsigned int,		irq_bit		)
		__field(	enum dpu_pingpong,	pp_idx		)
		__field(	int,			atomic_cnt	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->func = func;
		__entry->irq_reg = irq_reg;
		__entry->irq_bit = irq_bit;
		__entry->pp_idx = pp_idx;
		__entry->atomic_cnt = atomic_cnt;
	),
	TP_printk("id=%u, callback=%ps, IRQ=[%d, %d], pp=%d, atomic_cnt=%d",
		  __entry->drm_id, __entry->func,
		  __entry->irq_reg, __entry->irq_bit, __entry->pp_idx, __entry->atomic_cnt)
);

DECLARE_EVENT_CLASS(dpu_drm_obj_template,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
	),
	TP_printk("id=%u", __entry->drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_atomic_check,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_mode_set,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_disable,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_kickoff,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_prepare_kickoff,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_prepare_kickoff_reset,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_crtc_complete_flip,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_crtc_vblank_cb,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_crtc_complete_commit,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_kms_commit,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_kms_wait_for_commit_done,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_crtc_runtime_resume,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_rc_enable,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);
DEFINE_EVENT(dpu_drm_obj_template, dpu_enc_rc_disable,
	TP_PROTO(uint32_t drm_id),
	TP_ARGS(drm_id)
);

TRACE_EVENT(dpu_enc_enable,
	TP_PROTO(uint32_t drm_id, int hdisplay, int vdisplay),
	TP_ARGS(drm_id, hdisplay, vdisplay),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	int,			hdisplay	)
		__field(	int,			vdisplay	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->hdisplay = hdisplay;
		__entry->vdisplay = vdisplay;
	),
	TP_printk("id=%u, mode=%dx%d",
		  __entry->drm_id, __entry->hdisplay, __entry->vdisplay)
);

DECLARE_EVENT_CLASS(dpu_enc_keyval_template,
	TP_PROTO(uint32_t drm_id, int val),
	TP_ARGS(drm_id, val),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id	)
		__field(	int,		val	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->val = val;
	),
	TP_printk("id=%u, val=%d", __entry->drm_id, __entry->val)
);
DEFINE_EVENT(dpu_enc_keyval_template, dpu_enc_underrun_cb,
	TP_PROTO(uint32_t drm_id, int count),
	TP_ARGS(drm_id, count)
);
DEFINE_EVENT(dpu_enc_keyval_template, dpu_enc_trigger_start,
	TP_PROTO(uint32_t drm_id, int ctl_idx),
	TP_ARGS(drm_id, ctl_idx)
);

TRACE_EVENT(dpu_enc_atomic_check_flags,
	TP_PROTO(uint32_t drm_id, unsigned int flags),
	TP_ARGS(drm_id, flags),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	unsigned int,		flags		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->flags = flags;
	),
	TP_printk("id=%u, flags=%u",
		  __entry->drm_id, __entry->flags)
);

DECLARE_EVENT_CLASS(dpu_enc_id_enable_template,
	TP_PROTO(uint32_t drm_id, bool enable),
	TP_ARGS(drm_id, enable),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	bool,			enable		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->enable = enable;
	),
	TP_printk("id=%u, enable=%s",
		  __entry->drm_id, __entry->enable ? "true" : "false")
);
DEFINE_EVENT(dpu_enc_id_enable_template, dpu_enc_vblank_cb,
	TP_PROTO(uint32_t drm_id, bool enable),
	TP_ARGS(drm_id, enable)
);
DEFINE_EVENT(dpu_enc_id_enable_template, dpu_enc_frame_event_cb,
	TP_PROTO(uint32_t drm_id, bool enable),
	TP_ARGS(drm_id, enable)
);
DEFINE_EVENT(dpu_enc_id_enable_template, dpu_enc_phys_cmd_connect_te,
	TP_PROTO(uint32_t drm_id, bool enable),
	TP_ARGS(drm_id, enable)
);

TRACE_EVENT(dpu_enc_rc,
	TP_PROTO(uint32_t drm_id, u32 sw_event, bool idle_pc_supported,
		 int rc_state, const char *stage),
	TP_ARGS(drm_id, sw_event, idle_pc_supported, rc_state, stage),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id			)
		__field(	u32,		sw_event		)
		__field(	bool,		idle_pc_supported	)
		__field(	int,		rc_state		)
		__string(	stage_str,	stage			)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->sw_event = sw_event;
		__entry->idle_pc_supported = idle_pc_supported;
		__entry->rc_state = rc_state;
		__assign_str(stage_str, stage);
	),
	TP_printk("%s: id:%u, sw_event:%d, idle_pc_supported:%s, rc_state:%d",
		  __get_str(stage_str), __entry->drm_id, __entry->sw_event,
		  __entry->idle_pc_supported ? "true" : "false",
		  __entry->rc_state)
);

TRACE_EVENT(dpu_enc_frame_done_cb_not_busy,
	TP_PROTO(uint32_t drm_id, u32 event, char *intf_mode, enum dpu_intf intf_idx,
			enum dpu_wb wb_idx),
	TP_ARGS(drm_id, event, intf_mode, intf_idx, wb_idx),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id		)
		__field(	u32,		event		)
		__string(	intf_mode_str,	intf_mode	)
		__field(	enum dpu_intf,	intf_idx	)
		__field(	enum dpu_wb,	wb_idx		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->event = event;
		__assign_str(intf_mode_str, intf_mode);
		__entry->intf_idx = intf_idx;
		__entry->wb_idx = wb_idx;
	),
	TP_printk("id=%u, event=%u, intf_mode=%s intf=%d wb=%d", __entry->drm_id,
			__entry->event, __get_str(intf_mode_str),
			__entry->intf_idx, __entry->wb_idx)
);

TRACE_EVENT(dpu_enc_frame_done_cb,
	TP_PROTO(uint32_t drm_id, unsigned int idx,
		 unsigned long frame_busy_mask),
	TP_ARGS(drm_id, idx, frame_busy_mask),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	unsigned int,		idx		)
		__field(	unsigned long,		frame_busy_mask	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->idx = idx;
		__entry->frame_busy_mask = frame_busy_mask;
	),
	TP_printk("id=%u, idx=%u, frame_busy_mask=%lx", __entry->drm_id,
		  __entry->idx, __entry->frame_busy_mask)
);

TRACE_EVENT(dpu_enc_trigger_flush,
	TP_PROTO(uint32_t drm_id, char *intf_mode, enum dpu_intf intf_idx, enum dpu_wb wb_idx,
		 int pending_kickoff_cnt, int ctl_idx, u32 extra_flush_bits,
		 u32 pending_flush_ret),
	TP_ARGS(drm_id, intf_mode, intf_idx, wb_idx, pending_kickoff_cnt, ctl_idx,
		extra_flush_bits, pending_flush_ret),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id			)
		__string(	intf_mode_str,	intf_mode		)
		__field(	enum dpu_intf,	intf_idx		)
		__field(	enum dpu_wb,	wb_idx			)
		__field(	int,		pending_kickoff_cnt	)
		__field(	int,		ctl_idx			)
		__field(	u32,		extra_flush_bits	)
		__field(	u32,		pending_flush_ret	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__assign_str(intf_mode_str, intf_mode);
		__entry->intf_idx = intf_idx;
		__entry->wb_idx = wb_idx;
		__entry->pending_kickoff_cnt = pending_kickoff_cnt;
		__entry->ctl_idx = ctl_idx;
		__entry->extra_flush_bits = extra_flush_bits;
		__entry->pending_flush_ret = pending_flush_ret;
	),
	TP_printk("id=%u, intf_mode=%s, intf_idx=%d, wb_idx=%d, pending_kickoff_cnt=%d ctl_idx=%d "
		  "extra_flush_bits=0x%x pending_flush_ret=0x%x",
		  __entry->drm_id, __get_str(intf_mode_str), __entry->intf_idx, __entry->wb_idx,
		  __entry->pending_kickoff_cnt, __entry->ctl_idx,
		  __entry->extra_flush_bits, __entry->pending_flush_ret)
);

DECLARE_EVENT_CLASS(dpu_id_event_template,
	TP_PROTO(uint32_t drm_id, u32 event),
	TP_ARGS(drm_id, event),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id	)
		__field(	u32,		event	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->event = event;
	),
	TP_printk("id=%u, event=%u", __entry->drm_id, __entry->event)
);
DEFINE_EVENT(dpu_id_event_template, dpu_enc_frame_done_timeout,
	TP_PROTO(uint32_t drm_id, u32 event),
	TP_ARGS(drm_id, event)
);
DEFINE_EVENT(dpu_id_event_template, dpu_crtc_frame_event_cb,
	TP_PROTO(uint32_t drm_id, u32 event),
	TP_ARGS(drm_id, event)
);
DEFINE_EVENT(dpu_id_event_template, dpu_crtc_frame_event_done,
	TP_PROTO(uint32_t drm_id, u32 event),
	TP_ARGS(drm_id, event)
);
DEFINE_EVENT(dpu_id_event_template, dpu_crtc_frame_event_more_pending,
	TP_PROTO(uint32_t drm_id, u32 event),
	TP_ARGS(drm_id, event)
);

TRACE_EVENT(dpu_enc_wait_event_timeout,
	TP_PROTO(uint32_t drm_id, unsigned int irq_reg, unsigned int irq_bit, int rc, s64 time,
		 s64 expected_time, int atomic_cnt),
	TP_ARGS(drm_id, irq_reg, irq_bit, rc, time, expected_time, atomic_cnt),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id		)
		__field(	unsigned int,	irq_reg		)
		__field(	unsigned int,	irq_bit		)
		__field(	int,		rc		)
		__field(	s64,		time		)
		__field(	s64,		expected_time	)
		__field(	int,		atomic_cnt	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->irq_reg = irq_reg;
		__entry->irq_bit = irq_bit;
		__entry->rc = rc;
		__entry->time = time;
		__entry->expected_time = expected_time;
		__entry->atomic_cnt = atomic_cnt;
	),
	TP_printk("id=%u, IRQ=[%d, %d], rc=%d, time=%lld, expected=%lld cnt=%d",
		  __entry->drm_id, __entry->irq_reg, __entry->irq_bit, __entry->rc, __entry->time,
		  __entry->expected_time, __entry->atomic_cnt)
);

TRACE_EVENT(dpu_enc_phys_cmd_irq_enable,
	TP_PROTO(uint32_t drm_id, enum dpu_pingpong pp,
		 int refcnt),
	TP_ARGS(drm_id, pp, refcnt),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id	)
		__field(	enum dpu_pingpong,	pp	)
		__field(	int,			refcnt	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->pp = pp;
		__entry->refcnt = refcnt;
	),
	TP_printk("id=%u, pp=%d, refcnt=%d", __entry->drm_id,
		  __entry->pp,
		  __entry->refcnt)
);

TRACE_EVENT(dpu_enc_phys_cmd_irq_disable,
	TP_PROTO(uint32_t drm_id, enum dpu_pingpong pp,
		 int refcnt),
	TP_ARGS(drm_id, pp, refcnt),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id	)
		__field(	enum dpu_pingpong,	pp	)
		__field(	int,			refcnt	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->pp = pp;
		__entry->refcnt = refcnt;
	),
	TP_printk("id=%u, pp=%d, refcnt=%d", __entry->drm_id,
		  __entry->pp,
		  __entry->refcnt)
);

TRACE_EVENT(dpu_enc_phys_cmd_pp_tx_done,
	TP_PROTO(uint32_t drm_id, enum dpu_pingpong pp, int new_count,
		 u32 event),
	TP_ARGS(drm_id, pp, new_count, event),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	enum dpu_pingpong,	pp		)
		__field(	int,			new_count	)
		__field(	u32,			event		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->pp = pp;
		__entry->new_count = new_count;
		__entry->event = event;
	),
	TP_printk("id=%u, pp=%d, new_count=%d, event=%u", __entry->drm_id,
		  __entry->pp, __entry->new_count, __entry->event)
);

TRACE_EVENT(dpu_enc_phys_cmd_pdone_timeout,
	TP_PROTO(uint32_t drm_id, enum dpu_pingpong pp, int timeout_count,
		 int kickoff_count, u32 event),
	TP_ARGS(drm_id, pp, timeout_count, kickoff_count, event),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	enum dpu_pingpong,	pp		)
		__field(	int,			timeout_count	)
		__field(	int,			kickoff_count	)
		__field(	u32,			event		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->pp = pp;
		__entry->timeout_count = timeout_count;
		__entry->kickoff_count = kickoff_count;
		__entry->event = event;
	),
	TP_printk("id=%u, pp=%d, timeout_count=%d, kickoff_count=%d, event=%u",
		  __entry->drm_id, __entry->pp, __entry->timeout_count,
		  __entry->kickoff_count, __entry->event)
);

TRACE_EVENT(dpu_enc_phys_vid_post_kickoff,
	TP_PROTO(uint32_t drm_id, enum dpu_intf intf_idx),
	TP_ARGS(drm_id, intf_idx),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id			)
		__field(	enum dpu_intf,	intf_idx		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->intf_idx = intf_idx;
	),
	TP_printk("id=%u, intf_idx=%d", __entry->drm_id, __entry->intf_idx)
);

TRACE_EVENT(dpu_enc_phys_vid_irq_enable,
	TP_PROTO(uint32_t drm_id, enum dpu_intf intf_idx,
		 int refcnt),
	TP_ARGS(drm_id, intf_idx, refcnt),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id		)
		__field(	enum dpu_intf,	intf_idx	)
		__field(	int,		refcnt		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->intf_idx = intf_idx;
		__entry->refcnt = refcnt;
	),
	TP_printk("id=%u, intf_idx=%d refcnt=%d", __entry->drm_id,
		  __entry->intf_idx,
		  __entry->drm_id)
);

TRACE_EVENT(dpu_enc_phys_vid_irq_disable,
	TP_PROTO(uint32_t drm_id, enum dpu_intf intf_idx,
		 int refcnt),
	TP_ARGS(drm_id, intf_idx, refcnt),
	TP_STRUCT__entry(
		__field(	uint32_t,	drm_id		)
		__field(	enum dpu_intf,	intf_idx	)
		__field(	int,		refcnt		)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->intf_idx = intf_idx;
		__entry->refcnt = refcnt;
	),
	TP_printk("id=%u, intf_idx=%d refcnt=%d", __entry->drm_id,
		  __entry->intf_idx,
		  __entry->drm_id)
);

TRACE_EVENT(dpu_crtc_setup_mixer,
	TP_PROTO(uint32_t crtc_id, uint32_t plane_id,
		 struct drm_plane_state *state, struct dpu_plane_state *pstate,
		 uint32_t stage_idx, uint32_t pixel_format,
		 uint64_t modifier),
	TP_ARGS(crtc_id, plane_id, state, pstate, stage_idx,
		pixel_format, modifier),
	TP_STRUCT__entry(
		__field(	uint32_t,		crtc_id		)
		__field(	uint32_t,		plane_id	)
		__field(	uint32_t,		fb_id		)
		__field_struct(	struct drm_rect,	src_rect	)
		__field_struct(	struct drm_rect,	dst_rect	)
		__field(	uint32_t,		stage_idx	)
		__field(	enum dpu_stage,		stage		)
		__field(	enum dpu_sspp,		sspp		)
		__field(	uint32_t,		multirect_idx	)
		__field(	uint32_t,		multirect_mode	)
		__field(	uint32_t,		pixel_format	)
		__field(	uint64_t,		modifier	)
	),
	TP_fast_assign(
		__entry->crtc_id = crtc_id;
		__entry->plane_id = plane_id;
		__entry->fb_id = state ? state->fb->base.id : 0;
		__entry->src_rect = drm_plane_state_src(state);
		__entry->dst_rect = drm_plane_state_dest(state);
		__entry->stage_idx = stage_idx;
		__entry->stage = pstate->stage;
		__entry->sspp = pstate->pipe.sspp->idx;
		__entry->multirect_idx = pstate->pipe.multirect_index;
		__entry->multirect_mode = pstate->pipe.multirect_mode;
		__entry->pixel_format = pixel_format;
		__entry->modifier = modifier;
	),
	TP_printk("crtc_id:%u plane_id:%u fb_id:%u src:" DRM_RECT_FP_FMT
		  " dst:" DRM_RECT_FMT " stage_idx:%u stage:%d, sspp:%d "
		  "multirect_index:%d multirect_mode:%u pix_format:%u "
		  "modifier:%llu",
		  __entry->crtc_id, __entry->plane_id, __entry->fb_id,
		  DRM_RECT_FP_ARG(&__entry->src_rect),
		  DRM_RECT_ARG(&__entry->dst_rect),
		  __entry->stage_idx, __entry->stage, __entry->sspp,
		  __entry->multirect_idx, __entry->multirect_mode,
		  __entry->pixel_format, __entry->modifier)
);

TRACE_EVENT(dpu_crtc_setup_lm_bounds,
	TP_PROTO(uint32_t drm_id, int mixer, struct drm_rect *bounds),
	TP_ARGS(drm_id, mixer, bounds),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id	)
		__field(	int,			mixer	)
		__field_struct(	struct drm_rect,	bounds	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->mixer = mixer;
		__entry->bounds = *bounds;
	),
	TP_printk("id:%u mixer:%d bounds:" DRM_RECT_FMT, __entry->drm_id,
		  __entry->mixer, DRM_RECT_ARG(&__entry->bounds))
);

TRACE_EVENT(dpu_crtc_vblank_enable,
	TP_PROTO(uint32_t drm_id, uint32_t enc_id, bool enable,
		 struct dpu_crtc *crtc),
	TP_ARGS(drm_id, enc_id, enable, crtc),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id	)
		__field(	uint32_t,		enc_id	)
		__field(	bool,			enable	)
		__field(	bool,			enabled )
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->enc_id = enc_id;
		__entry->enable = enable;
		__entry->enabled = crtc->enabled;
	),
	TP_printk("id:%u encoder:%u enable:%s state{enabled:%s}",
		  __entry->drm_id, __entry->enc_id,
		  __entry->enable ? "true" : "false",
		  __entry->enabled ? "true" : "false")
);

DECLARE_EVENT_CLASS(dpu_crtc_enable_template,
	TP_PROTO(uint32_t drm_id, bool enable, struct dpu_crtc *crtc),
	TP_ARGS(drm_id, enable, crtc),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id	)
		__field(	bool,			enable	)
		__field(	bool,			enabled )
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->enable = enable;
		__entry->enabled = crtc->enabled;
	),
	TP_printk("id:%u enable:%s state{enabled:%s}",
		  __entry->drm_id, __entry->enable ? "true" : "false",
		  __entry->enabled ? "true" : "false")
);
DEFINE_EVENT(dpu_crtc_enable_template, dpu_crtc_enable,
	TP_PROTO(uint32_t drm_id, bool enable, struct dpu_crtc *crtc),
	TP_ARGS(drm_id, enable, crtc)
);
DEFINE_EVENT(dpu_crtc_enable_template, dpu_crtc_disable,
	TP_PROTO(uint32_t drm_id, bool enable, struct dpu_crtc *crtc),
	TP_ARGS(drm_id, enable, crtc)
);
DEFINE_EVENT(dpu_crtc_enable_template, dpu_crtc_vblank,
	TP_PROTO(uint32_t drm_id, bool enable, struct dpu_crtc *crtc),
	TP_ARGS(drm_id, enable, crtc)
);

TRACE_EVENT(dpu_crtc_disable_frame_pending,
	TP_PROTO(uint32_t drm_id, int frame_pending),
	TP_ARGS(drm_id, frame_pending),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	int,			frame_pending	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->frame_pending = frame_pending;
	),
	TP_printk("id:%u frame_pending:%d", __entry->drm_id,
		  __entry->frame_pending)
);

TRACE_EVENT(dpu_plane_set_scanout,
	TP_PROTO(struct dpu_sw_pipe *pipe, struct dpu_hw_fmt_layout *layout),
	TP_ARGS(pipe, layout),
	TP_STRUCT__entry(
		__field(	enum dpu_sspp,			index	)
		__field_struct(	struct dpu_hw_fmt_layout,	layout	)
		__field(	enum dpu_sspp_multirect_index,	multirect_index)
	),
	TP_fast_assign(
		__entry->index = pipe->sspp->idx;
		__entry->layout = *layout;
		__entry->multirect_index = pipe->multirect_index;
	),
	TP_printk("index:%d layout:{%ux%u @ [%u/%u, %u/%u, %u/%u, %u/%u]} "
		  "multirect_index:%d", __entry->index, __entry->layout.width,
		  __entry->layout.height, __entry->layout.plane_addr[0],
		  __entry->layout.plane_size[0],
		  __entry->layout.plane_addr[1],
		  __entry->layout.plane_size[1],
		  __entry->layout.plane_addr[2],
		  __entry->layout.plane_size[2],
		  __entry->layout.plane_addr[3],
		  __entry->layout.plane_size[3], __entry->multirect_index)
);

TRACE_EVENT(dpu_plane_disable,
	TP_PROTO(uint32_t drm_id, bool is_virtual, uint32_t multirect_mode),
	TP_ARGS(drm_id, is_virtual, multirect_mode),
	TP_STRUCT__entry(
		__field(	uint32_t,		drm_id		)
		__field(	bool,			is_virtual	)
		__field(	uint32_t,		multirect_mode	)
	),
	TP_fast_assign(
		__entry->drm_id = drm_id;
		__entry->is_virtual = is_virtual;
		__entry->multirect_mode = multirect_mode;
	),
	TP_printk("id:%u is_virtual:%s multirect_mode:%u", __entry->drm_id,
		  __entry->is_virtual ? "true" : "false",
		  __entry->multirect_mode)
);

DECLARE_EVENT_CLASS(dpu_rm_iter_template,
	TP_PROTO(uint32_t id, uint32_t enc_id),
	TP_ARGS(id, enc_id),
	TP_STRUCT__entry(
		__field(	uint32_t,		id	)
		__field(	uint32_t,		enc_id	)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->enc_id = enc_id;
	),
	TP_printk("id:%d enc_id:%u", __entry->id, __entry->enc_id)
);
DEFINE_EVENT(dpu_rm_iter_template, dpu_rm_reserve_intf,
	TP_PROTO(uint32_t id, uint32_t enc_id),
	TP_ARGS(id, enc_id)
);
DEFINE_EVENT(dpu_rm_iter_template, dpu_rm_reserve_ctls,
	TP_PROTO(uint32_t id, uint32_t enc_id),
	TP_ARGS(id, enc_id)
);

TRACE_EVENT(dpu_rm_reserve_lms,
	TP_PROTO(uint32_t id, uint32_t enc_id, uint32_t pp_id),
	TP_ARGS(id, enc_id, pp_id),
	TP_STRUCT__entry(
		__field(	uint32_t,		id	)
		__field(	uint32_t,		enc_id	)
		__field(	uint32_t,		pp_id	)
	),
	TP_fast_assign(
		__entry->id = id;
		__entry->enc_id = enc_id;
		__entry->pp_id = pp_id;
	),
	TP_printk("id:%d enc_id:%u pp_id:%u", __entry->id,
		  __entry->enc_id, __entry->pp_id)
);

TRACE_EVENT(dpu_vbif_wait_xin_halt_fail,
	TP_PROTO(enum dpu_vbif index, u32 xin_id),
	TP_ARGS(index, xin_id),
	TP_STRUCT__entry(
		__field(	enum dpu_vbif,	index	)
		__field(	u32,		xin_id	)
	),
	TP_fast_assign(
		__entry->index = index;
		__entry->xin_id = xin_id;
	),
	TP_printk("index:%d xin_id:%u", __entry->index, __entry->xin_id)
);

TRACE_EVENT(dpu_pp_connect_ext_te,
	TP_PROTO(enum dpu_pingpong pp, u32 cfg),
	TP_ARGS(pp, cfg),
	TP_STRUCT__entry(
		__field(	enum dpu_pingpong,	pp	)
		__field(	u32,			cfg	)
	),
	TP_fast_assign(
		__entry->pp = pp;
		__entry->cfg = cfg;
	),
	TP_printk("pp:%d cfg:%u", __entry->pp, __entry->cfg)
);

TRACE_EVENT(dpu_intf_connect_ext_te,
	TP_PROTO(enum dpu_intf intf, u32 cfg),
	TP_ARGS(intf, cfg),
	TP_STRUCT__entry(
		__field(	enum dpu_intf,	intf	)
		__field(	u32,			cfg	)
	),
	TP_fast_assign(
		__entry->intf = intf;
		__entry->cfg = cfg;
	),
	TP_printk("intf:%d cfg:%u", __entry->intf, __entry->cfg)
);

TRACE_EVENT(dpu_core_irq_register_callback,
	TP_PROTO(unsigned int irq_reg, unsigned int irq_bit, void *callback),
	TP_ARGS(irq_reg, irq_bit, callback),
	TP_STRUCT__entry(
		__field(	unsigned int,			irq_reg	)
		__field(	unsigned int,			irq_bit	)
		__field(	void *,				callback)
	),
	TP_fast_assign(
		__entry->irq_reg = irq_reg;
		__entry->irq_bit = irq_bit;
		__entry->callback = callback;
	),
	TP_printk("IRQ=[%d, %d] callback:%ps", __entry->irq_reg, __entry->irq_bit,
		  __entry->callback)
);

TRACE_EVENT(dpu_core_irq_unregister_callback,
	TP_PROTO(unsigned int irq_reg, unsigned int irq_bit),
	TP_ARGS(irq_reg, irq_bit),
	TP_STRUCT__entry(
		__field(	unsigned int,			irq_reg	)
		__field(	unsigned int,			irq_bit	)
	),
	TP_fast_assign(
		__entry->irq_reg = irq_reg;
		__entry->irq_bit = irq_bit;
	),
	TP_printk("IRQ=[%d, %d]", __entry->irq_reg, __entry->irq_bit)
);

TRACE_EVENT(dpu_core_perf_update_clk,
	TP_PROTO(struct drm_device *dev, bool stop_req, u64 clk_rate),
	TP_ARGS(dev, stop_req, clk_rate),
	TP_STRUCT__entry(
		__string(	dev_name,		dev->unique	)
		__field(	bool,			stop_req	)
		__field(	u64,			clk_rate	)
	),
	TP_fast_assign(
		__assign_str(dev_name, dev->unique);
		__entry->stop_req = stop_req;
		__entry->clk_rate = clk_rate;
	),
	TP_printk("dev:%s stop_req:%s clk_rate:%llu", __get_str(dev_name),
		  __entry->stop_req ? "true" : "false", __entry->clk_rate)
);

TRACE_EVENT(dpu_hw_ctl_update_pending_flush,
	TP_PROTO(u32 new_bits, u32 pending_mask),
	TP_ARGS(new_bits, pending_mask),
	TP_STRUCT__entry(
		__field(	u32,			new_bits	)
		__field(	u32,			pending_mask	)
	),
	TP_fast_assign(
		__entry->new_bits = new_bits;
		__entry->pending_mask = pending_mask;
	),
	TP_printk("new=%x existing=%x", __entry->new_bits,
		  __entry->pending_mask)
);

DECLARE_EVENT_CLASS(dpu_hw_ctl_pending_flush_template,
	TP_PROTO(u32 pending_mask, u32 ctl_flush),
	TP_ARGS(pending_mask, ctl_flush),
	TP_STRUCT__entry(
		__field(	u32,			pending_mask	)
		__field(	u32,			ctl_flush	)
	),
	TP_fast_assign(
		__entry->pending_mask = pending_mask;
		__entry->ctl_flush = ctl_flush;
	),
	TP_printk("pending_mask=%x CTL_FLUSH=%x", __entry->pending_mask,
		  __entry->ctl_flush)
);
DEFINE_EVENT(dpu_hw_ctl_pending_flush_template, dpu_hw_ctl_clear_pending_flush,
	TP_PROTO(u32 pending_mask, u32 ctl_flush),
	TP_ARGS(pending_mask, ctl_flush)
);
DEFINE_EVENT(dpu_hw_ctl_pending_flush_template,
	     dpu_hw_ctl_trigger_pending_flush,
	TP_PROTO(u32 pending_mask, u32 ctl_flush),
	TP_ARGS(pending_mask, ctl_flush)
);
DEFINE_EVENT(dpu_hw_ctl_pending_flush_template, dpu_hw_ctl_trigger_prepare,
	TP_PROTO(u32 pending_mask, u32 ctl_flush),
	TP_ARGS(pending_mask, ctl_flush)
);
DEFINE_EVENT(dpu_hw_ctl_pending_flush_template, dpu_hw_ctl_trigger_start,
	TP_PROTO(u32 pending_mask, u32 ctl_flush),
	TP_ARGS(pending_mask, ctl_flush)
);

#define DPU_ATRACE_END(name) trace_tracing_mark_write(current->tgid, name, 0)
#define DPU_ATRACE_BEGIN(name) trace_tracing_mark_write(current->tgid, name, 1)
#define DPU_ATRACE_FUNC() DPU_ATRACE_BEGIN(__func__)

#define DPU_ATRACE_INT(name, value) \
	trace_dpu_trace_counter(current->tgid, name, value)

#endif /* _DPU_TRACE_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#include <trace/define_trace.h>
