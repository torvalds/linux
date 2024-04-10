/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#if !defined(_TRACE_DCVS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_DCVS_H

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dcvs

#include <linux/tracepoint.h>
#include <soc/qcom/dcvs.h>

TRACE_EVENT(qcom_dcvs_update,

	TP_PROTO(const char *name, int hw, int path, unsigned long ib,
		 unsigned long path_ib, unsigned long ab, unsigned long path_ab,
		 unsigned long boost_freq),

	TP_ARGS(name, hw, path, ib, path_ib, ab, path_ab, boost_freq),

	TP_STRUCT__entry(
		__string(name, name)
		__field(int, hw)
		__field(int, path)
		__field(unsigned long, ib)
		__field(unsigned long, path_ib)
		__field(unsigned long, ab)
		__field(unsigned long, path_ab)
		__field(unsigned long, boost_freq)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->hw = hw;
		__entry->path = path;
		__entry->ib = ib;
		__entry->path_ib = path_ib;
		__entry->ab = ab;
		__entry->path_ab = path_ab;
		__entry->boost_freq = boost_freq;
	),

	TP_printk("name=%s hw=%d path=%d ib=%lu path_ib=%lu ab=%lu path_ab=%lu boost=%lu",
		__get_str(name),
		__entry->hw,
		__entry->path,
		__entry->ib,
		__entry->path_ib,
		__entry->ab,
		__entry->path_ab,
		__entry->boost_freq)
);

TRACE_EVENT(qcom_dcvs_boost,

	TP_PROTO(int hw, int path, unsigned long boost, unsigned long path_ib,
		 unsigned long path_ab),

	TP_ARGS(hw, path, boost, path_ib, path_ab),

	TP_STRUCT__entry(
		__field(int, hw)
		__field(int, path)
		__field(unsigned long, boost)
		__field(unsigned long, path_ib)
		__field(unsigned long, path_ab)
	),

	TP_fast_assign(
		__entry->hw = hw;
		__entry->path = path;
		__entry->boost = boost;
		__entry->path_ib = path_ib;
		__entry->path_ab = path_ab;
	),

	TP_printk("hw=%d path=%d boost=%lu path_ib=%lu path_ab=%lu",
		__entry->hw,
		__entry->path,
		__entry->boost,
		__entry->path_ib,
		__entry->path_ab)
);

TRACE_EVENT(memlat_dev_meas,

	TP_PROTO(const char *name, unsigned int dev_id, unsigned long inst,
		 unsigned long mem, unsigned long freq, unsigned int stall,
		 unsigned int wb, unsigned int ratio, unsigned int fe_stall),

	TP_ARGS(name, dev_id, inst, mem, freq, stall, wb, ratio, fe_stall),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned int, dev_id)
		__field(unsigned long, inst)
		__field(unsigned long, mem)
		__field(unsigned long, freq)
		__field(unsigned int, stall)
		__field(unsigned int, wb)
		__field(unsigned int, ratio)
		__field(unsigned int, fe_stall)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->dev_id = dev_id;
		__entry->inst = inst;
		__entry->mem = mem;
		__entry->freq = freq;
		__entry->stall = stall;
		__entry->wb = wb;
		__entry->ratio = ratio;
		__entry->fe_stall = fe_stall;
	),

	TP_printk("dev: %s, id=%u, inst=%lu, mem=%lu, freq=%lu, stall=%u, wb=%u, ratio=%u, fe_stall=%u",
		__get_str(name),
		__entry->dev_id,
		__entry->inst,
		__entry->mem,
		__entry->freq,
		__entry->stall,
		__entry->wb,
		__entry->ratio,
		__entry->fe_stall)
);

TRACE_EVENT(memlat_dev_update,

	TP_PROTO(const char *name, unsigned int dev_id, unsigned long inst,
		 unsigned long mem, unsigned long freq, unsigned long vote),

	TP_ARGS(name, dev_id, inst, mem, freq, vote),

	TP_STRUCT__entry(
		__string(name, name)
		__field(unsigned int, dev_id)
		__field(unsigned long, inst)
		__field(unsigned long, mem)
		__field(unsigned long, freq)
		__field(unsigned long, vote)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->dev_id = dev_id;
		__entry->inst = inst;
		__entry->mem = mem;
		__entry->freq = freq;
		__entry->vote = vote;
	),

	TP_printk("dev: %s, id=%u, inst=%lu, mem=%lu, freq=%lu, vote=%lu",
		__get_str(name),
		__entry->dev_id,
		__entry->inst,
		__entry->mem,
		__entry->freq,
		__entry->vote)
);

TRACE_EVENT(bw_hwmon_meas,

	TP_PROTO(const char *name, unsigned long mbps,
		 unsigned long us, int wake),

	TP_ARGS(name, mbps, us, wake),

	TP_STRUCT__entry(
		__string(name,			name)
		__field(unsigned long,		mbps)
		__field(unsigned long,		us)
		__field(int,			wake)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->mbps = mbps;
		__entry->us = us;
		__entry->wake = wake;
	),

	TP_printk("dev: %s, mbps = %lu, us = %lu, wake = %d",
		__get_str(name),
		__entry->mbps,
		__entry->us,
		__entry->wake)
);

TRACE_EVENT(bw_hwmon_update,

	TP_PROTO(const char *name, unsigned long mbps, unsigned long freq,
		 unsigned long up_thres, unsigned long down_thres),

	TP_ARGS(name, mbps, freq, up_thres, down_thres),

	TP_STRUCT__entry(
		__string(name,			name)
		__field(unsigned long,		mbps)
		__field(unsigned long,		freq)
		__field(unsigned long,		up_thres)
		__field(unsigned long,		down_thres)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->mbps = mbps;
		__entry->freq = freq;
		__entry->up_thres = up_thres;
		__entry->down_thres = down_thres;
	),

	TP_printk("dev: %s, mbps = %lu, freq = %lu, up = %lu, down = %lu",
		__get_str(name),
		__entry->mbps,
		__entry->freq,
		__entry->up_thres,
		__entry->down_thres)
);

TRACE_EVENT(bw_hwmon_debug,

	TP_PROTO(const char *name, unsigned long mbps, unsigned long zone,
		 unsigned long hist_max, unsigned long hist_mem,
		 unsigned long hyst_mbps, unsigned long hyst_len),

	TP_ARGS(name, mbps, zone, hist_max, hist_mem, hyst_mbps, hyst_len),

	TP_STRUCT__entry(
		__string(name,			name)
		__field(unsigned long,		mbps)
		__field(unsigned long,		zone)
		__field(unsigned long,		hist_max)
		__field(unsigned long,		hist_mem)
		__field(unsigned long,		hyst_mbps)
		__field(unsigned long,		hyst_len)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__entry->mbps = mbps;
		__entry->zone = zone;
		__entry->hist_max = hist_max;
		__entry->hist_mem = hist_mem;
		__entry->hyst_mbps = hyst_mbps;
		__entry->hyst_len = hyst_len;
	),

	TP_printk("dev=%s mbps=%lu zone=%lu hist_max=%lu hist_mem=%lu hyst_mbps=%lu hyst_len=%lu",
		__get_str(name),
		__entry->mbps,
		__entry->zone,
		__entry->hist_max,
		__entry->hist_mem,
		__entry->hyst_mbps,
		__entry->hyst_len)
);

TRACE_EVENT(bwprof_last_sample,

	TP_PROTO(const char *name, const char *client, ktime_t ts, u32 meas_mbps,
			u32 max_mbps, u32 mem_freq),

	TP_ARGS(name, client, ts, meas_mbps, max_mbps, mem_freq),

	TP_STRUCT__entry(
		__string(name, name)
		__string(client, client)
		__field(ktime_t, ts)
		__field(u32, meas_mbps)
		__field(u32, max_mbps)
		__field(u32, mem_freq)
	),

	TP_fast_assign(
		__assign_str(name, name);
		__assign_str(client, client);
		__entry->ts = ts;
		__entry->meas_mbps = meas_mbps;
		__entry->max_mbps = max_mbps;
		__entry->mem_freq = mem_freq;
	),

	TP_printk("dev=%s client=%s ts=%llu meas_mbps=%u max_mbps=%u mem_freq=%u",
		__get_str(name),
		__get_str(client),
		__entry->ts,
		__entry->meas_mbps,
		__entry->max_mbps,
		__entry->mem_freq)
);


#endif /* _TRACE_DCVS_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace-dcvs

#include <trace/define_trace.h>
