/*
 * DHD debugability support
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dhd_mschdbg.c 639872 2016-05-25 05:39:30Z $
 */
#ifdef SHOW_LOGTRACE
#include <typedefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <bcmendian.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_debug.h>
#include <dhd_mschdbg.h>

#include <event_log.h>
#include <event_trace.h>
#include <msgtrace.h>

static const char *head_log = "";
#define MSCH_EVENT_HEAD(space) \
	do { \
		MSCH_EVENT(("%s_E:  ", head_log)); \
		if (space > 0) { \
			int ii; \
			for (ii = 0; ii < space; ii += 4) MSCH_EVENT(("    ")); \
		} \
	} while (0)
#define MSCH_EVENT(args) do {if (dhd_msg_level & DHD_EVENT_VAL) printf args;} while (0)

static uint64 solt_start_time[4], req_start_time[4], profiler_start_time[4];
static uint32 solt_chanspec[4] = {0, }, req_start[4] = {0, };
static bool lastMessages = FALSE;

#define US_PRE_SEC		1000000
#define DATA_UNIT_FOR_LOG_CNT	4

static void dhd_mschdbg_us_to_sec(uint32 time_h, uint32 time_l, uint32 *sec, uint32 *remain)
{
	uint64 cur_time = ((uint64)(ntoh32(time_h)) << 32) | ntoh32(time_l);
	uint64 r, u = 0;

	r = cur_time;
	while (time_h != 0) {
		u += (uint64)((0xffffffff / US_PRE_SEC)) * time_h;
		r = cur_time - u * US_PRE_SEC;
		time_h = (uint32)(r >> 32);
	}

	*sec = (uint32)(u + ((uint32)(r) / US_PRE_SEC));
	*remain = (uint32)(r) % US_PRE_SEC;
}

static char *dhd_mschdbg_display_time(uint32 time_h, uint32 time_l)
{
	static char display_time[32];
	uint32 s, ss;

	if (time_h == 0xffffffff && time_l == 0xffffffff) {
		snprintf(display_time, 31, "-1");
	} else {
		dhd_mschdbg_us_to_sec(time_h, time_l, &s, &ss);
		snprintf(display_time, 31, "%d.%06d", s, ss);
	}
	return display_time;
}

static void
dhd_mschdbg_chanspec_list(int sp, char *data, uint16 ptr, uint16 chanspec_cnt)
{
	int i, cnt = (int)ntoh16(chanspec_cnt);
	uint16 *chanspec_list = (uint16 *)(data + ntoh16(ptr));
	char buf[CHANSPEC_STR_LEN];
	chanspec_t c;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<chanspec_list>:"));
	for (i = 0; i < cnt; i++) {
		c = (chanspec_t)ntoh16(chanspec_list[i]);
		MSCH_EVENT((" %s", wf_chspec_ntoa(c, buf)));
	}
	MSCH_EVENT(("\n"));
}

static void
dhd_mschdbg_elem_list(int sp, char *title, char *data, uint16 ptr, uint16 list_cnt)
{
	int i, cnt = (int)ntoh16(list_cnt);
	uint32 *list = (uint32 *)(data + ntoh16(ptr));

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("%s_list: ", title));
	for (i = 0; i < cnt; i++) {
		MSCH_EVENT(("0x%08x->", ntoh32(list[i])));
	}
	MSCH_EVENT(("null\n"));
}

static void
dhd_mschdbg_req_param_profiler_event_data(int sp, int ver, char *data, uint16 ptr)
{
	int sn = sp + 4;
	msch_req_param_profiler_event_data_t *p =
		(msch_req_param_profiler_event_data_t *)(data + ntoh16(ptr));
	uint32 type, flags;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<request parameters>\n"));
	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("req_type: "));

	type = p->req_type;
	if (type < 4) {
		char *req_type[] = {"fixed", "start-flexible", "duration-flexible",
			"both-flexible"};
		MSCH_EVENT(("%s", req_type[type]));
	}
	else
		MSCH_EVENT(("unknown(%d)", type));

	flags = ntoh16(p->flags);
	if (flags & WL_MSCH_REQ_FLAGS_CHAN_CONTIGUOUS)
		MSCH_EVENT((", CHAN_CONTIGUOUS"));
	if (flags & WL_MSCH_REQ_FLAGS_MERGE_CONT_SLOTS)
		MSCH_EVENT((", MERGE_CONT_SLOTS"));
	if (flags & WL_MSCH_REQ_FLAGS_PREMTABLE)
		MSCH_EVENT((", PREMTABLE"));
	if (flags & WL_MSCH_REQ_FLAGS_PREMT_CURTS)
		MSCH_EVENT((", PREMT_CURTS"));
	if (flags & WL_MSCH_REQ_FLAGS_PREMT_IMMEDIATE)
		MSCH_EVENT((", PREMT_IMMEDIATE"));
	MSCH_EVENT((", priority: %d\n", p->priority));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("start-time: %s, duration: %d(us), interval: %d(us)\n",
		dhd_mschdbg_display_time(p->start_time_h, p->start_time_l),
		ntoh32(p->duration), ntoh32(p->interval)));

	if (type == WL_MSCH_RT_DUR_FLEX) {
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("dur_flex: %d(us)\n", ntoh32(p->flex.dur_flex)));
	} else if (type == WL_MSCH_RT_BOTH_FLEX) {
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("min_dur: %d(us), max_away_dur: %d(us)\n",
			ntoh32(p->flex.bf.min_dur), ntoh32(p->flex.bf.max_away_dur)));

		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("hi_prio_time: %s, hi_prio_interval: %d(us)\n",
			dhd_mschdbg_display_time(p->flex.bf.hi_prio_time_h,
			p->flex.bf.hi_prio_time_l),
			ntoh32(p->flex.bf.hi_prio_interval)));
	}
}

static void
dhd_mschdbg_timeslot_profiler_event_data(int sp, int ver, char *title, char *data,
	uint16 ptr, bool empty)
{
	int s, sn = sp + 4;
	msch_timeslot_profiler_event_data_t *p =
		(msch_timeslot_profiler_event_data_t *)(data + ntoh16(ptr));
	char *state[] = {"NONE", "CHN_SW", "ONCHAN_FIRE", "OFF_CHN_PREP",
		"OFF_CHN_DONE", "TS_COMPLETE"};

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<%s timeslot>: ", title));
	if (empty) {
		MSCH_EVENT((" null\n"));
		return;
	}
	else
		MSCH_EVENT(("0x%08x\n", ntoh32(p->p_timeslot)));

	s = (int)(ntoh32(p->state));
	if (s > 5) s = 0;

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("id: %d, state[%d]: %s, chan_ctxt: [0x%08x]\n",
		ntoh32(p->timeslot_id), ntoh32(p->state), state[s], ntoh32(p->p_chan_ctxt)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("fire_time: %s",
		dhd_mschdbg_display_time(p->fire_time_h, p->fire_time_l)));

	MSCH_EVENT((", pre_start_time: %s",
		dhd_mschdbg_display_time(p->pre_start_time_h, p->pre_start_time_l)));

	MSCH_EVENT((", end_time: %s",
		dhd_mschdbg_display_time(p->end_time_h, p->end_time_l)));

	MSCH_EVENT((", sch_dur: %s\n",
		dhd_mschdbg_display_time(p->sch_dur_h, p->sch_dur_l)));
}

static void
dhd_mschdbg_req_timing_profiler_event_data(int sp, int ver, char *title, char *data,
	uint16 ptr, bool empty)
{
	int sn = sp + 4;
	msch_req_timing_profiler_event_data_t *p =
		(msch_req_timing_profiler_event_data_t *)(data + ntoh16(ptr));
	uint32 type;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<%s req_timing>: ", title));
	if (empty) {
		MSCH_EVENT((" null\n"));
		return;
	}
	else
		MSCH_EVENT(("0x%08x (prev 0x%08x, next 0x%08x)\n",
			ntoh32(p->p_req_timing), ntoh32(p->p_prev), ntoh32(p->p_next)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("flags:"));
	type = ntoh16(p->flags);
	if ((type & 0x7f) == 0)
		MSCH_EVENT((" NONE"));
	else {
		if (type & WL_MSCH_RC_FLAGS_ONCHAN_FIRE)
			MSCH_EVENT((" ONCHAN_FIRE"));
		if (type & WL_MSCH_RC_FLAGS_START_FIRE_DONE)
			MSCH_EVENT((" START_FIRE"));
		if (type & WL_MSCH_RC_FLAGS_END_FIRE_DONE)
			MSCH_EVENT((" END_FIRE"));
		if (type & WL_MSCH_RC_FLAGS_ONFIRE_DONE)
			MSCH_EVENT((" ONFIRE_DONE"));
		if (type & WL_MSCH_RC_FLAGS_SPLIT_SLOT_START)
			MSCH_EVENT((" SPLIT_SLOT_START"));
		if (type & WL_MSCH_RC_FLAGS_SPLIT_SLOT_END)
			MSCH_EVENT((" SPLIT_SLOT_END"));
		if (type & WL_MSCH_RC_FLAGS_PRE_ONFIRE_DONE)
			MSCH_EVENT((" PRE_ONFIRE_DONE"));
	}
	MSCH_EVENT(("\n"));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("pre_start_time: %s",
		dhd_mschdbg_display_time(p->pre_start_time_h, p->pre_start_time_l)));

	MSCH_EVENT((", start_time: %s",
		dhd_mschdbg_display_time(p->start_time_h, p->start_time_l)));

	MSCH_EVENT((", end_time: %s\n",
		dhd_mschdbg_display_time(p->end_time_h, p->end_time_l)));

	if (p->p_timeslot && (p->timeslot_ptr == 0)) {
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("<%s timeslot>: 0x%08x\n", title, ntoh32(p->p_timeslot)));
	} else
		dhd_mschdbg_timeslot_profiler_event_data(sn, ver, title, data, p->timeslot_ptr,
			(p->timeslot_ptr == 0));
}

static void
dhd_mschdbg_chan_ctxt_profiler_event_data(int sp, int ver, char *data, uint16 ptr, bool empty)
{
	int sn = sp + 4;
	msch_chan_ctxt_profiler_event_data_t *p =
		(msch_chan_ctxt_profiler_event_data_t *)(data + ntoh16(ptr));
	chanspec_t c;
	char buf[CHANSPEC_STR_LEN];

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<chan_ctxt>: "));
	if (empty) {
		MSCH_EVENT((" null\n"));
		return;
	}
	else
		MSCH_EVENT(("0x%08x (prev 0x%08x, next 0x%08x)\n",
			ntoh32(p->p_chan_ctxt), ntoh32(p->p_prev), ntoh32(p->p_next)));

	c = (chanspec_t)ntoh16(p->chanspec);

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("channel: %s, bf_sch_pending: %s, bf_skipped: %d\n",
		wf_chspec_ntoa(c, buf), p->bf_sch_pending? "TRUE" : "FALSE",
		ntoh32(p->bf_skipped_count)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("bf_link: prev 0x%08x, next 0x%08x\n",
		ntoh32(p->bf_link_prev), ntoh32(p->bf_link_next)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("onchan_time: %s",
		dhd_mschdbg_display_time(p->onchan_time_h, p->onchan_time_l)));
	MSCH_EVENT((", actual_onchan_dur: %s",
		dhd_mschdbg_display_time(p->actual_onchan_dur_h, p->actual_onchan_dur_l)));
	MSCH_EVENT((", pend_onchan_dur: %s\n",
		dhd_mschdbg_display_time(p->pend_onchan_dur_h, p->pend_onchan_dur_l)));

	dhd_mschdbg_elem_list(sn, "req_entity", data, p->req_entity_list_ptr,
		p->req_entity_list_cnt);
	dhd_mschdbg_elem_list(sn, "bf_entity", data, p->bf_entity_list_ptr,
		p->bf_entity_list_cnt);
}

static void
dhd_mschdbg_req_entity_profiler_event_data(int sp, int ver, char *data, uint16 ptr, bool empty)
{
	int sn = sp + 4;
	msch_req_entity_profiler_event_data_t *p =
		(msch_req_entity_profiler_event_data_t *)(data + ntoh16(ptr));
	char buf[CHANSPEC_STR_LEN];
	chanspec_t c;
	uint32 flags;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<req_entity>: "));
	if (empty) {
		MSCH_EVENT((" null\n"));
		return;
	}
	else
		MSCH_EVENT(("0x%08x (prev 0x%08x, next 0x%08x)\n",
			ntoh32(p->p_req_entity), ntoh32(p->req_hdl_link_prev),
			ntoh32(p->req_hdl_link_next)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("req_hdl: [0x%08x]\n", ntoh32(p->p_req_hdl)));

	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("chan_ctxt_link: prev 0x%08x, next 0x%08x\n",
		ntoh32(p->chan_ctxt_link_prev), ntoh32(p->chan_ctxt_link_next)));
	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("rt_specific_link: prev 0x%08x, next 0x%08x\n",
		ntoh32(p->rt_specific_link_prev), ntoh32(p->rt_specific_link_next)));
	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("start_fixed_link: prev 0x%08x, next 0x%08x\n",
		ntoh32(p->start_fixed_link_prev), ntoh32(p->start_fixed_link_next)));
	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("both_flex_list: prev 0x%08x, next 0x%08x\n",
		ntoh32(p->both_flex_list_prev), ntoh32(p->both_flex_list_next)));

	c = (chanspec_t)ntoh16(p->chanspec);
	MSCH_EVENT_HEAD(sn);
	if (ver >= 2) {
		MSCH_EVENT(("channel: %s, onchan Id %d, current chan Id %d, priority %d",
			wf_chspec_ntoa(c, buf), ntoh16(p->onchan_chn_idx), ntoh16(p->cur_chn_idx),
			ntoh16(p->priority)));
		flags = ntoh32(p->flags);
		if (flags & WL_MSCH_ENTITY_FLAG_MULTI_INSTANCE)
			MSCH_EVENT((" : MULTI_INSTANCE\n"));
		else
			MSCH_EVENT(("\n"));
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("actual_start_time: %s, ",
			dhd_mschdbg_display_time(p->actual_start_time_h, p->actual_start_time_l)));
		MSCH_EVENT(("curts_fire_time: %s, ",
			dhd_mschdbg_display_time(p->curts_fire_time_h, p->curts_fire_time_l)));
	} else {
		MSCH_EVENT(("channel: %s, priority %d, ", wf_chspec_ntoa(c, buf),
			ntoh16(p->priority)));
	}
	MSCH_EVENT(("bf_last_serv_time: %s\n",
		dhd_mschdbg_display_time(p->bf_last_serv_time_h, p->bf_last_serv_time_l)));

	dhd_mschdbg_req_timing_profiler_event_data(sn, ver, "current", data, p->cur_slot_ptr,
		(p->cur_slot_ptr == 0));
	dhd_mschdbg_req_timing_profiler_event_data(sn, ver, "pending", data, p->pend_slot_ptr,
		(p->pend_slot_ptr == 0));

	if (p->p_chan_ctxt && (p->chan_ctxt_ptr == 0)) {
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("<chan_ctxt>: 0x%08x\n", ntoh32(p->p_chan_ctxt)));
	}
	else
		dhd_mschdbg_chan_ctxt_profiler_event_data(sn, ver, data, p->chan_ctxt_ptr,
			(p->chan_ctxt_ptr == 0));
}

static void
dhd_mschdbg_req_handle_profiler_event_data(int sp, int ver, char *data, uint16 ptr, bool empty)
{
	int sn = sp + 4;
	msch_req_handle_profiler_event_data_t *p =
		(msch_req_handle_profiler_event_data_t *)(data + ntoh16(ptr));
	uint32 flags;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("<req_handle>: "));
	if (empty) {
		MSCH_EVENT((" null\n"));
		return;
	}
	else
		MSCH_EVENT(("0x%08x (prev 0x%08x, next 0x%08x)\n",
			ntoh32(p->p_req_handle), ntoh32(p->p_prev), ntoh32(p->p_next)));

	dhd_mschdbg_elem_list(sn, "req_entity", data, p->req_entity_list_ptr,
		p->req_entity_list_cnt);
	MSCH_EVENT_HEAD(sn);
	MSCH_EVENT(("cb_func: [0x%08x], cb_func: [0x%08x]",
		ntoh32(p->cb_func), ntoh32(p->cb_ctxt)));
	if (ver < 2) {
		MSCH_EVENT((", chan_cnt: %d", ntoh16(p->chan_cnt)));
	}
	flags = ntoh32(p->flags);
	if (flags & WL_MSCH_REQ_HDL_FLAGS_NEW_REQ)
		MSCH_EVENT((", NEW_REQ"));
	MSCH_EVENT(("\n"));

	dhd_mschdbg_req_param_profiler_event_data(sn, ver, data, p->req_param_ptr);

	if (ver >= 2) {
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("req_time: %s\n",
			dhd_mschdbg_display_time(p->req_time_h, p->req_time_l)));
		MSCH_EVENT_HEAD(sn);
		MSCH_EVENT(("chan_cnt: %d, chan idx %d, last chan idx %d\n",
			ntoh16(p->chan_cnt), ntoh16(p->chan_idx), ntoh16(p->last_chan_idx)));
		if (p->chanspec_list && p->chanspec_cnt) {
			dhd_mschdbg_chanspec_list(sn, data, p->chanspec_list, p->chanspec_cnt);
		}
	}
}

static void
dhd_mschdbg_profiler_profiler_event_data(int sp, int ver, char *data, uint16 ptr)
{
	msch_profiler_profiler_event_data_t *p =
		(msch_profiler_profiler_event_data_t *)(data + ntoh16(ptr));
	uint32 flags;

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("free list: req_hdl 0x%08x, req_entity 0x%08x,"
		" chan_ctxt 0x%08x, chanspec 0x%08x\n",
		ntoh32(p->free_req_hdl_list), ntoh32(p->free_req_entity_list),
		ntoh32(p->free_chan_ctxt_list), ntoh32(p->free_chanspec_list)));

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("alloc count: chanspec %d, req_entity %d, req_hdl %d, "
		"chan_ctxt %d, timeslot %d\n",
		ntoh16(p->msch_chanspec_alloc_cnt), ntoh16(p->msch_req_entity_alloc_cnt),
		ntoh16(p->msch_req_hdl_alloc_cnt), ntoh16(p->msch_chan_ctxt_alloc_cnt),
		ntoh16(p->msch_timeslot_alloc_cnt)));

	dhd_mschdbg_elem_list(sp, "req_hdl", data, p->msch_req_hdl_list_ptr,
		p->msch_req_hdl_list_cnt);
	dhd_mschdbg_elem_list(sp, "chan_ctxt", data, p->msch_chan_ctxt_list_ptr,
		p->msch_chan_ctxt_list_cnt);
	dhd_mschdbg_elem_list(sp, "req_timing", data, p->msch_req_timing_list_ptr,
		p->msch_req_timing_list_cnt);
	dhd_mschdbg_elem_list(sp, "start_fixed", data, p->msch_start_fixed_list_ptr,
		p->msch_start_fixed_list_cnt);
	dhd_mschdbg_elem_list(sp, "both_flex_req_entity", data,
		p->msch_both_flex_req_entity_list_ptr,
		p->msch_both_flex_req_entity_list_cnt);
	dhd_mschdbg_elem_list(sp, "start_flex", data, p->msch_start_flex_list_ptr,
		p->msch_start_flex_list_cnt);
	dhd_mschdbg_elem_list(sp, "both_flex", data, p->msch_both_flex_list_ptr,
		p->msch_both_flex_list_cnt);

	if (p->p_cur_msch_timeslot && (p->cur_msch_timeslot_ptr == 0)) {
		MSCH_EVENT_HEAD(sp);
		MSCH_EVENT(("<cur_msch timeslot>: 0x%08x\n",
			ntoh32(p->p_cur_msch_timeslot)));
	} else
		dhd_mschdbg_timeslot_profiler_event_data(sp, ver, "cur_msch", data,
			p->cur_msch_timeslot_ptr, (p->cur_msch_timeslot_ptr == 0));

	if (p->p_next_timeslot && (p->next_timeslot_ptr == 0)) {
		MSCH_EVENT_HEAD(sp);
		MSCH_EVENT(("<next timeslot>: 0x%08x\n",
			ntoh32(p->p_next_timeslot)));
	} else
		dhd_mschdbg_timeslot_profiler_event_data(sp, ver, "next", data,
			p->next_timeslot_ptr, (p->next_timeslot_ptr == 0));

	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("ts_id: %d, ", ntoh32(p->ts_id)));
	flags = ntoh32(p->flags);
	if (flags & WL_MSCH_STATE_IN_TIEMR_CTXT)
		MSCH_EVENT(("IN_TIEMR_CTXT, "));
	if (flags & WL_MSCH_STATE_SCHD_PENDING)
		MSCH_EVENT(("SCHD_PENDING, "));
	MSCH_EVENT(("slotskip_flags: %d, cur_armed_timeslot: 0x%08x\n",
		(ver >= 2)? ntoh32(p->slotskip_flag) : 0, ntoh32(p->cur_armed_timeslot)));
	MSCH_EVENT_HEAD(sp);
	MSCH_EVENT(("flex_list_cnt: %d, service_interval: %d, "
		"max_lo_prio_interval: %d\n",
		ntoh16(p->flex_list_cnt), ntoh32(p->service_interval),
		ntoh32(p->max_lo_prio_interval)));
}

static void dhd_mschdbg_dump_data(dhd_pub_t *dhdp, void *raw_event_ptr, int type,
	char *data, int len)
{
	uint64 t = 0, tt = 0;
	uint32 s = 0, ss = 0;
	int wlc_index, ver;

	ver = (type & WL_MSCH_PROFILER_VER_MASK) >> WL_MSCH_PROFILER_VER_SHIFT;
	wlc_index = (type & WL_MSCH_PROFILER_WLINDEX_MASK) >> WL_MSCH_PROFILER_WLINDEX_SHIFT;
	if (wlc_index >= 4)
		return;

	type &= WL_MSCH_PROFILER_TYPE_MASK;
	if (type <= WL_MSCH_PROFILER_PROFILE_END) {
		msch_profiler_event_data_t *pevent = (msch_profiler_event_data_t *)data;
		tt = ((uint64)(ntoh32(pevent->time_hi)) << 32) | ntoh32(pevent->time_lo);
		dhd_mschdbg_us_to_sec(pevent->time_hi, pevent->time_lo, &s, &ss);
	}

	if (lastMessages && (type != WL_MSCH_PROFILER_MESSAGE) &&
		(type != WL_MSCH_PROFILER_EVENT_LOG)) {
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("\n"));
		lastMessages = FALSE;
	}

	switch (type) {
	case WL_MSCH_PROFILER_START:
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d START\n", s, ss));
		break;

	case WL_MSCH_PROFILER_EXIT:
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d EXIT\n", s, ss));
		break;

	case WL_MSCH_PROFILER_REQ:
	{
		msch_req_profiler_event_data_t *p = (msch_req_profiler_event_data_t *)data;
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("\n"));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("===============================\n"));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d] REGISTER:\n", s, ss, wlc_index));
		dhd_mschdbg_req_param_profiler_event_data(4, ver, data, p->req_param_ptr);
		dhd_mschdbg_chanspec_list(4, data, p->chanspec_ptr, p->chanspec_cnt);
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("===============================\n"));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("\n"));
	}
		break;

	case WL_MSCH_PROFILER_CALLBACK:
	{
		msch_callback_profiler_event_data_t *p =
			(msch_callback_profiler_event_data_t *)data;
		char buf[CHANSPEC_STR_LEN];
		chanspec_t chanspec;
		uint16 cbtype;

		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d] CALLBACK: ", s, ss, wlc_index));
		chanspec = (chanspec_t)ntoh16(p->chanspec);
		MSCH_EVENT(("req_hdl[0x%08x], channel %s --",
			ntoh32(p->p_req_hdl), wf_chspec_ntoa(chanspec, buf)));

		cbtype = ntoh16(p->type);
		if (cbtype & WL_MSCH_CT_ON_CHAN)
			MSCH_EVENT((" ON_CHAN"));
		if (cbtype & WL_MSCH_CT_OFF_CHAN)
			MSCH_EVENT((" OFF_CHAN"));
		if (cbtype & WL_MSCH_CT_REQ_START)
			MSCH_EVENT((" REQ_START"));
		if (cbtype & WL_MSCH_CT_REQ_END)
			MSCH_EVENT((" REQ_END"));
		if (cbtype & WL_MSCH_CT_SLOT_START)
			MSCH_EVENT((" SLOT_START"));
		if (cbtype & WL_MSCH_CT_SLOT_SKIP)
			MSCH_EVENT((" SLOT_SKIP"));
		if (cbtype & WL_MSCH_CT_SLOT_END)
			MSCH_EVENT((" SLOT_END"));
		if (cbtype & WL_MSCH_CT_OFF_CHAN_DONE)
			MSCH_EVENT((" OFF_CHAN_DONE"));
		if (cbtype & WL_MSCH_CT_PARTIAL)
			MSCH_EVENT((" PARTIAL"));
		if (cbtype & WL_MSCH_CT_PRE_ONCHAN)
			MSCH_EVENT((" PRE_ONCHAN"));
		if (cbtype & WL_MSCH_CT_PRE_REQ_START)
			MSCH_EVENT((" PRE_REQ_START"));

		if (cbtype & WL_MSCH_CT_REQ_START) {
			req_start[wlc_index] = 1;
			req_start_time[wlc_index] = tt;
		} else if (cbtype & WL_MSCH_CT_REQ_END) {
			if (req_start[wlc_index]) {
				MSCH_EVENT((" : REQ duration %d",
					(uint32)(tt - req_start_time[wlc_index])));
				req_start[wlc_index] = 0;
			}
		}

		if (cbtype & WL_MSCH_CT_SLOT_START) {
			solt_chanspec[wlc_index] = p->chanspec;
			solt_start_time[wlc_index] = tt;
		} else if (cbtype & WL_MSCH_CT_SLOT_END) {
			if (p->chanspec == solt_chanspec[wlc_index]) {
				MSCH_EVENT((" : SLOT duration %d",
					(uint32)(tt - solt_start_time[wlc_index])));
				solt_chanspec[wlc_index] = 0;
			}
		}
		MSCH_EVENT(("\n"));

		if (cbtype & (WL_MSCH_CT_ON_CHAN | WL_MSCH_CT_SLOT_SKIP)) {
			MSCH_EVENT_HEAD(4);
			if (cbtype & WL_MSCH_CT_ON_CHAN) {
				MSCH_EVENT(("ID %d onchan idx %d cur_chan_seq_start %s ",
					ntoh32(p->timeslot_id), ntoh32(p->onchan_idx),
					dhd_mschdbg_display_time(p->cur_chan_seq_start_time_h,
					p->cur_chan_seq_start_time_l)));
			}
			t = ((uint64)(ntoh32(p->start_time_h)) << 32) |
				ntoh32(p->start_time_l);
			MSCH_EVENT(("start %s ",
				dhd_mschdbg_display_time(p->start_time_h,
				p->start_time_l)));
			tt = ((uint64)(ntoh32(p->end_time_h)) << 32) | ntoh32(p->end_time_l);
			MSCH_EVENT(("end %s duration %d\n",
				dhd_mschdbg_display_time(p->end_time_h, p->end_time_l),
				(p->end_time_h == 0xffffffff && p->end_time_l == 0xffffffff)?
				-1 : (int)(tt - t)));
		}

	}
		break;

	case WL_MSCH_PROFILER_EVENT_LOG:
	{
		while (len >= (int)WL_MSCH_EVENT_LOG_HEAD_SIZE) {
			msch_event_log_profiler_event_data_t *p =
				(msch_event_log_profiler_event_data_t *)data;
			/* TODO: How to parse MSCH if extended event tag is present ??? */
			prcd_event_log_hdr_t hdr;
			int size = WL_MSCH_EVENT_LOG_HEAD_SIZE + p->hdr.count * sizeof(uint32);
			if (len < size || size > sizeof(msch_event_log_profiler_event_data_t)) {
				break;
			}
			data += size;
			len -= size;
			dhd_mschdbg_us_to_sec(p->time_hi, p->time_lo, &s, &ss);
			MSCH_EVENT_HEAD(0);
			MSCH_EVENT(("%06d.%06d [wl%d]: ", s, ss, p->hdr.tag));
			bzero(&hdr, sizeof(hdr));
			hdr.tag = EVENT_LOG_TAG_MSCHPROFILE;
			hdr.count = p->hdr.count + 1;
			/* exclude LSB 2 bits which indicate binary/non-binary data */
			hdr.fmt_num = ntoh16(p->hdr.fmt_num) >> 2;
			hdr.fmt_num_raw = ntoh16(p->hdr.fmt_num);
			if (ntoh16(p->hdr.fmt_num) == DHD_OW_BI_RAW_EVENT_LOG_FMT) {
				hdr.binary_payload = TRUE;
			}
			dhd_dbg_verboselog_printf(dhdp, &hdr, raw_event_ptr, p->data, 0, 0);
		}
		lastMessages = TRUE;
		break;
	}

	case WL_MSCH_PROFILER_MESSAGE:
	{
		msch_message_profiler_event_data_t *p = (msch_message_profiler_event_data_t *)data;
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d]: %s", s, ss, wlc_index, p->message));
		lastMessages = TRUE;
		break;
	}

	case WL_MSCH_PROFILER_PROFILE_START:
		profiler_start_time[wlc_index] = tt;
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("-------------------------------\n"));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d] PROFILE DATA:\n", s, ss, wlc_index));
		dhd_mschdbg_profiler_profiler_event_data(4, ver, data, 0);
		break;

	case WL_MSCH_PROFILER_PROFILE_END:
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d] PROFILE END: take time %d\n", s, ss,
			wlc_index, (uint32)(tt - profiler_start_time[wlc_index])));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("-------------------------------\n"));
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("\n"));
		break;

	case WL_MSCH_PROFILER_REQ_HANDLE:
		dhd_mschdbg_req_handle_profiler_event_data(4, ver, data, 0, FALSE);
		break;

	case WL_MSCH_PROFILER_REQ_ENTITY:
		dhd_mschdbg_req_entity_profiler_event_data(4, ver, data, 0, FALSE);
		break;

	case WL_MSCH_PROFILER_CHAN_CTXT:
		dhd_mschdbg_chan_ctxt_profiler_event_data(4, ver, data, 0, FALSE);
		break;

	case WL_MSCH_PROFILER_REQ_TIMING:
		dhd_mschdbg_req_timing_profiler_event_data(4, ver, "msch", data, 0, FALSE);
		break;

	default:
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("[wl%d] ERROR: unsupported EVENT reason code:%d; ",
			wlc_index, type));
		break;
	}
}

void
wl_mschdbg_event_handler(dhd_pub_t *dhdp, void *raw_event_ptr, int type, void *data, int len)
{
	head_log = "MSCH";
	dhd_mschdbg_dump_data(dhdp, raw_event_ptr, type, (char *)data, len);
}

void
wl_mschdbg_verboselog_handler(dhd_pub_t *dhdp, void *raw_event_ptr, prcd_event_log_hdr_t *plog_hdr,
	uint32 *log_ptr)
{
	uint32 log_pyld_len;
	head_log = "CONSOLE";

	if (plog_hdr->count == 0) {
		return;
	}
	log_pyld_len = (plog_hdr->count - 1) * DATA_UNIT_FOR_LOG_CNT;

	if (plog_hdr->tag == EVENT_LOG_TAG_MSCHPROFILE) {
		msch_event_log_profiler_event_data_t *p =
			(msch_event_log_profiler_event_data_t *)log_ptr;
		/* TODO: How to parse MSCH if extended event tag is present ??? */
		prcd_event_log_hdr_t hdr;
		uint32 s, ss;

		if (log_pyld_len < OFFSETOF(msch_event_log_profiler_event_data_t, data) ||
			log_pyld_len > sizeof(msch_event_log_profiler_event_data_t)) {
			return;
		}

		dhd_mschdbg_us_to_sec(p->time_hi, p->time_lo, &s, &ss);
		MSCH_EVENT_HEAD(0);
		MSCH_EVENT(("%06d.%06d [wl%d]: ", s, ss, p->hdr.tag));
		bzero(&hdr, sizeof(hdr));
		hdr.tag = EVENT_LOG_TAG_MSCHPROFILE;
		hdr.count = p->hdr.count + 1;
		/* exclude LSB 2 bits which indicate binary/non-binary data */
		hdr.fmt_num = ntoh16(p->hdr.fmt_num) >> 2;
		hdr.fmt_num_raw = ntoh16(p->hdr.fmt_num);
		if (ntoh16(p->hdr.fmt_num) == DHD_OW_BI_RAW_EVENT_LOG_FMT) {
			hdr.binary_payload = TRUE;
		}
		dhd_dbg_verboselog_printf(dhdp, &hdr, raw_event_ptr, p->data, 0, 0);
	} else {
		msch_collect_tlv_t *p = (msch_collect_tlv_t *)log_ptr;
		int type = ntoh16(p->type);
		int len = ntoh16(p->size);

		if (log_pyld_len < OFFSETOF(msch_collect_tlv_t, value) + len) {
			return;
		}

		dhd_mschdbg_dump_data(dhdp, raw_event_ptr, type, p->value, len);
	}
}
#endif /* SHOW_LOGTRACE */
