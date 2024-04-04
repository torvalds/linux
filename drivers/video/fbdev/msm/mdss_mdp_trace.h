/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2018, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#if !defined(TRACE_MDSS_MDP_H) || defined(TRACE_HEADER_MULTI_READ)
#define TRACE_MDSS_MDP_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mdss
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mdss_mdp_trace

#include <linux/tracepoint.h>
#include "mdss_mdp.h"

DECLARE_EVENT_CLASS(mdp_sspp_template,
	TP_PROTO(struct mdss_mdp_pipe *pipe),
	TP_ARGS(pipe),
	TP_STRUCT__entry(
			__field(u32, num)
			__field(u32, play_cnt)
			__field(u32, mixer)
			__field(u32, stage)
			__field(u32, flags)
			__field(u32, format)
			__field(u16, img_w)
			__field(u16, img_h)
			__field(u16, src_x)
			__field(u16, src_y)
			__field(u16, src_w)
			__field(u16, src_h)
			__field(u16, dst_x)
			__field(u16, dst_y)
			__field(u16, dst_w)
			__field(u16, dst_h)
	),
	TP_fast_assign(
			__entry->num = pipe->num;
			__entry->play_cnt = pipe->play_cnt;
			__entry->mixer = pipe->mixer_left->num;
			__entry->stage = pipe->mixer_stage;
			__entry->flags = pipe->flags;
			__entry->format = pipe->src_fmt ?
					pipe->src_fmt->format : -1;
			__entry->img_w = pipe->img_width;
			__entry->img_h = pipe->img_height;
			__entry->src_x = pipe->src.x;
			__entry->src_y = pipe->src.y;
			__entry->src_w = pipe->src.w;
			__entry->src_h = pipe->src.h;
			__entry->dst_x = pipe->dst.x;
			__entry->dst_y = pipe->dst.y;
			__entry->dst_w = pipe->dst.w;
			__entry->dst_h = pipe->dst.h;
	),

	TP_printk("pnum=%d mixer=%d play_cnt=%d flags=0x%x stage=%d format=%d img=%dx%d src=[%d,%d,%d,%d] dst=[%d,%d,%d,%d]",
			__entry->num, __entry->mixer, __entry->play_cnt,
			__entry->flags, __entry->stage,
			__entry->format, __entry->img_w, __entry->img_h,
			__entry->src_x, __entry->src_y,
			__entry->src_w, __entry->src_h,
			__entry->dst_x, __entry->dst_y,
			__entry->dst_w, __entry->dst_h)
);

DEFINE_EVENT(mdp_sspp_template, mdp_sspp_set,
	TP_PROTO(struct mdss_mdp_pipe *pipe),
	TP_ARGS(pipe)
);

DEFINE_EVENT(mdp_sspp_template, mdp_sspp_change,
	TP_PROTO(struct mdss_mdp_pipe *pipe),
	TP_ARGS(pipe)
);

TRACE_EVENT(mdp_perf_set_qos_luts,
	TP_PROTO(u32 pnum, u32 fmt, u32 intf, u32 rot, u32 fl,
		u32 lut, bool linear),
	TP_ARGS(pnum, fmt, intf, rot, fl, lut, linear),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, fmt)
			__field(u32, intf)
			__field(u32, rot)
			__field(u32, fl)
			__field(u32, lut)
			__field(bool, linear)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->fmt = fmt;
			__entry->intf = intf;
			__entry->rot = rot;
			__entry->fl = fl;
			__entry->lut = lut;
			__entry->linear = linear;
	),
	TP_printk("pnum=%d fmt=%d intf=%d rot=%d fl:%d lut=0x%x lin:%d",
			__entry->pnum, __entry->fmt,
			__entry->intf, __entry->rot, __entry->fl,
			__entry->lut, __entry->linear)
);

TRACE_EVENT(mdp_perf_set_panic_luts,
	TP_PROTO(u32 pnum, u32 fmt, u32 mode, u32 panic_lut,
		u32 robust_lut),
	TP_ARGS(pnum, fmt, mode, panic_lut, robust_lut),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, fmt)
			__field(u32, mode)
			__field(u32, panic_lut)
			__field(u32, robust_lut)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->fmt = fmt;
			__entry->mode = mode;
			__entry->panic_lut = panic_lut;
			__entry->robust_lut = robust_lut;
	),
	TP_printk("pnum=%d fmt=%d mode=%d luts[0x%x, 0x%x]",
			__entry->pnum, __entry->fmt,
			__entry->mode, __entry->panic_lut,
			__entry->robust_lut)
);

TRACE_EVENT(mdp_perf_set_wm_levels,
	TP_PROTO(u32 pnum, u32 use_space, u32 priority_bytes, u32 wm0, u32 wm1,
		u32 wm2, u32 mb_cnt, u32 mb_size),
	TP_ARGS(pnum, use_space, priority_bytes, wm0, wm1, wm2, mb_cnt,
		mb_size),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, use_space)
			__field(u32, priority_bytes)
			__field(u32, wm0)
			__field(u32, wm1)
			__field(u32, wm2)
			__field(u32, mb_cnt)
			__field(u32, mb_size)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->use_space = use_space;
			__entry->priority_bytes = priority_bytes;
			__entry->wm0 = wm0;
			__entry->wm1 = wm1;
			__entry->wm2 = wm2;
			__entry->mb_cnt = mb_cnt;
			__entry->mb_size = mb_size;
	),
	TP_printk("pnum:%d useable_space:%d priority_bytes:%d watermark:[%d | %d | %d] nmb=%d mb_size=%d",
			__entry->pnum, __entry->use_space,
			__entry->priority_bytes, __entry->wm0, __entry->wm1,
			__entry->wm2, __entry->mb_cnt, __entry->mb_size)
);

TRACE_EVENT(mdp_perf_set_ot,
	TP_PROTO(u32 pnum, u32 xin_id, u32 rd_lim, u32 is_vbif_rt),
	TP_ARGS(pnum, xin_id, rd_lim, is_vbif_rt),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, xin_id)
			__field(u32, rd_lim)
			__field(u32, is_vbif_rt)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->xin_id = xin_id;
			__entry->rd_lim = rd_lim;
			__entry->is_vbif_rt = is_vbif_rt;
	),
	TP_printk("pnum:%d xin_id:%d ot:%d rt:%d",
			__entry->pnum, __entry->xin_id, __entry->rd_lim,
			__entry->is_vbif_rt)
);

TRACE_EVENT(mdp_perf_prefill_calc,
	TP_PROTO(u32 pnum, u32 latency_buf, u32 ot, u32 y_buf, u32 y_scaler,
		u32 pp_lines, u32 pp_bytes, u32 post_sc, u32 fbc_bytes,
		u32 prefill_bytes),
	TP_ARGS(pnum, latency_buf, ot, y_buf, y_scaler, pp_lines, pp_bytes,
		post_sc, fbc_bytes, prefill_bytes),
	TP_STRUCT__entry(
			__field(u32, pnum)
			__field(u32, latency_buf)
			__field(u32, ot)
			__field(u32, y_buf)
			__field(u32, y_scaler)
			__field(u32, pp_lines)
			__field(u32, pp_bytes)
			__field(u32, post_sc)
			__field(u32, fbc_bytes)
			__field(u32, prefill_bytes)
	),
	TP_fast_assign(
			__entry->pnum = pnum;
			__entry->latency_buf = latency_buf;
			__entry->ot = ot;
			__entry->y_buf = y_buf;
			__entry->y_scaler = y_scaler;
			__entry->pp_lines = pp_lines;
			__entry->pp_bytes = pp_bytes;
			__entry->post_sc = post_sc;
			__entry->fbc_bytes = fbc_bytes;
			__entry->prefill_bytes = prefill_bytes;
	),
	TP_printk("pnum:%d latency_buf:%d ot:%d y_buf:%d y_scaler:%d pp_lines:%d, pp_bytes=%d post_sc:%d fbc_bytes:%d prefill:%d",
			__entry->pnum, __entry->latency_buf, __entry->ot,
			__entry->y_buf, __entry->y_scaler, __entry->pp_lines,
			__entry->pp_bytes, __entry->post_sc,
			__entry->fbc_bytes, __entry->prefill_bytes)
);

TRACE_EVENT(mdp_mixer_update,
	TP_PROTO(u32 mixer_num),
	TP_ARGS(mixer_num),
	TP_STRUCT__entry(
			__field(u32, mixer_num)
	),
	TP_fast_assign(
			__entry->mixer_num = mixer_num;
	),
	TP_printk("mixer=%d",
			__entry->mixer_num)
);

TRACE_EVENT(mdp_commit,
	TP_PROTO(struct mdss_mdp_ctl *ctl),
	TP_ARGS(ctl),
	TP_STRUCT__entry(
			__field(u32, num)
			__field(u32, play_cnt)
			__field(u32, clk_rate)
			__field(u64, bandwidth)
	),
	TP_fast_assign(
			__entry->num = ctl->num;
			__entry->play_cnt = ctl->play_cnt;
			__entry->clk_rate = ctl->new_perf.mdp_clk_rate;
			__entry->bandwidth = ctl->new_perf.bw_ctl;
	),
	TP_printk("num=%d play_cnt=%d bandwidth=%llu clk_rate=%u",
			__entry->num,
			__entry->play_cnt,
			__entry->bandwidth,
			__entry->clk_rate)
);

TRACE_EVENT(mdp_video_underrun_done,
	TP_PROTO(u32 ctl_num, u32 underrun_cnt),
	TP_ARGS(ctl_num, underrun_cnt),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
			__field(u32, underrun_cnt)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl_num;
			__entry->underrun_cnt = underrun_cnt;
	),
	TP_printk("ctl=%d count=%d",
			__entry->ctl_num, __entry->underrun_cnt)
);

TRACE_EVENT(mdp_perf_update_bus,
	TP_PROTO(int client, unsigned long long ab_quota,
	unsigned long long ib_quota),
	TP_ARGS(client, ab_quota, ib_quota),
	TP_STRUCT__entry(
			__field(int, client)
			__field(u64, ab_quota)
			__field(u64, ib_quota)
	),
	TP_fast_assign(
			__entry->client = client;
			__entry->ab_quota = ab_quota;
			__entry->ib_quota = ib_quota;
	),
	TP_printk("Request client:%d ab=%llu ib=%llu",
			__entry->client,
			__entry->ab_quota,
			__entry->ib_quota)
);

TRACE_EVENT(mdp_compare_bw,
	TP_PROTO(unsigned long long new_ab, unsigned long long new_ib,
		unsigned long long new_wb,
		unsigned long long old_ab, unsigned long long old_ib,
		unsigned long long old_wb,
		u32 params_changed, bool update_bw),
	TP_ARGS(new_ab, new_ib, new_wb,
					old_ab, old_ib, old_wb,
					params_changed, update_bw),
	TP_STRUCT__entry(
			__field(u64, new_ab)
			__field(u64, new_ib)
			__field(u64, new_wb)
			__field(u64, old_ab)
			__field(u64, old_ib)
			__field(u64, old_wb)
			__field(u32, params_changed)
			__field(bool, update_bw)
	),
	TP_fast_assign(
			__entry->new_ab = new_ab;
			__entry->new_ib = new_ib;
			__entry->new_wb = new_wb;
			__entry->old_ab = old_ab;
			__entry->old_ib = old_ib;
			__entry->old_wb = old_wb;
			__entry->params_changed = params_changed;
			__entry->update_bw = update_bw;
	),
	TP_printk("[ab,ib,wb]new[%llu,%llu,%llu]old[%llu,%llu,%llu]chgd:%d %d",
		__entry->new_ab, __entry->new_ib, __entry->new_wb,
		__entry->old_ab, __entry->old_ib,
		__entry->old_wb, __entry->params_changed,
		__entry->update_bw)
);

TRACE_EVENT(mdp_misr_crc,
	TP_PROTO(u32 block_id, u32 vsync_cnt, u32 crc),
	TP_ARGS(block_id, vsync_cnt, crc),
	TP_STRUCT__entry(
			__field(u32, block_id)
			__field(u32, vsync_cnt)
			__field(u32, crc)
	),
	TP_fast_assign(
			__entry->block_id = block_id;
			__entry->vsync_cnt = vsync_cnt;
			__entry->crc = crc;
	),
	TP_printk("block_id:%d vsync_cnt:%d crc:0x%08x",
			__entry->block_id, __entry->vsync_cnt, __entry->crc)
);

TRACE_EVENT(mdp_cmd_pingpong_done,
	TP_PROTO(struct mdss_mdp_ctl *ctl, u32 pp_num, int koff_cnt),
	TP_ARGS(ctl, pp_num, koff_cnt),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
			__field(u32, intf_num)
			__field(u32, pp_num)
			__field(int, koff_cnt)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl->num;
			__entry->intf_num = ctl->intf_num;
			__entry->pp_num = pp_num;
			__entry->koff_cnt = koff_cnt;
	),
	TP_printk("ctl num:%d intf_num:%d ctx:%d kickoff:%d",
			__entry->ctl_num, __entry->intf_num, __entry->pp_num,
			__entry->koff_cnt)
);

TRACE_EVENT(mdp_cmd_readptr_done,
	TP_PROTO(u32 ctl_num, int koff_cnt),
	TP_ARGS(ctl_num, koff_cnt),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
			__field(int, koff_cnt)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl_num;
			__entry->koff_cnt = koff_cnt;
	),
	TP_printk("ctl num:%d kickoff:%d",
			__entry->ctl_num,
			__entry->koff_cnt)
);

TRACE_EVENT(mdp_cmd_release_bw,
	TP_PROTO(u32 ctl_num),
	TP_ARGS(ctl_num),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl_num;
	),
	TP_printk("ctl num:%d", __entry->ctl_num)
);

TRACE_EVENT(mdp_cmd_kickoff,
	TP_PROTO(u32 ctl_num, int kickoff_cnt),
	TP_ARGS(ctl_num, kickoff_cnt),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
			__field(int, kickoff_cnt)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl_num;
			__entry->kickoff_cnt = kickoff_cnt;
	),
	TP_printk("kickoff ctl=%d cnt=%d",
			__entry->ctl_num,
			__entry->kickoff_cnt)
);

TRACE_EVENT(mdp_cmd_wait_pingpong,
	TP_PROTO(u32 ctl_num, int kickoff_cnt),
	TP_ARGS(ctl_num, kickoff_cnt),
	TP_STRUCT__entry(
			__field(u32, ctl_num)
			__field(int, kickoff_cnt)
	),
	TP_fast_assign(
			__entry->ctl_num = ctl_num;
			__entry->kickoff_cnt = kickoff_cnt;
	),
	TP_printk("pingpong ctl=%d cnt=%d",
			__entry->ctl_num,
			__entry->kickoff_cnt)
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
);

TRACE_EVENT(mdp_trace_counter,
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
);

TRACE_EVENT(rotator_bw_ao_as_context,
	TP_PROTO(u32 state),
	TP_ARGS(state),
	TP_STRUCT__entry(
			__field(u32, state)
	),
	TP_fast_assign(
			__entry->state = state;
	),
	TP_printk("Rotator bw context %s",
			__entry->state ? "Active Only" : "Active+Sleep")

);

#endif /* if !defined(TRACE_MDSS_MDP_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
