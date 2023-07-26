/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_BWMON_H
#define _QCOM_BWMON_H

#include <linux/kernel.h>
#include <soc/qcom/dcvs.h>

#define NUM_MBPS_ZONES		10
#define UP_WAKE			1
#define DOWN_WAKE		2
#define MBYTE			(1ULL << 20)
#define MBPS_TO_KHZ(mbps, w)	(mult_frac(mbps, MBYTE, w * 1000ULL))
#define KHZ_TO_MBPS(khz, w)	(mult_frac(w * 1000ULL, khz, MBYTE))
#define to_bwmon(ptr)		container_of(ptr, struct bwmon, hw)

enum mon_reg_type {
	MON1,
	MON2,
	MON3,
};

struct bwmon_spec {
	bool wrap_on_thres;
	bool overflow;
	bool throt_adj;
	bool hw_sampling;
	bool has_global_base;
	enum mon_reg_type reg_type;
};

struct bwmon_second_map {
	u32 src_freq;
	u32 dst_freq;
};

/**
 * struct bw_hwmon - dev BW HW monitor info
 * @start_hwmon:		Start the HW monitoring of the dev BW
 * @stop_hwmon:			Stop the HW monitoring of dev BW
 * @set_thres:			Set the count threshold to generate an IRQ
 * @set_hw_events:		Set hw settings for up/down wake events
 * @get_bytes_and_clear:	Get the bytes transferred since the last call
 *				and reset the counter to start over.
 * @set_throttle_adj:		Set throttle adjust field to the given value
 * @get_throttle_adj:		Get the value written to throttle adjust field
 * @dev:			Pointer to device tied to this HW monitor
 * @dcvs_hw:			DCVS HW type that this HW is monitoring for
 * @dcvs_path:			DCVS Path type that this monitor votes on
 * @node:			Pointer to hwmon node that contains tunables
 * @last_update_ts:		Time that the last bwmon work was queued
 * @work:			bwmon monitor work
 * @is_active:			Toggled when HW monitor is started/stopped
 * @up_wake_mbps:		Setting for HW monitor to send IRQ for up wake
 * @down_wake_mbps:		Setting for HW monitor to send IRQ fow down wake
 * @down_cnt:			Setting for down sample count needed for wake
 */
struct bw_hwmon {
	int			(*start_hwmon)(struct bw_hwmon *hw,
					unsigned long mbps);
	void			(*stop_hwmon)(struct bw_hwmon *hw);
	unsigned long		(*set_thres)(struct bw_hwmon *hw,
					unsigned long bytes);
	unsigned long		(*set_hw_events)(struct bw_hwmon *hw,
					unsigned int sample_ms);
	unsigned long		(*get_bytes_and_clear)(struct bw_hwmon *hw);
	int			(*set_throttle_adj)(struct bw_hwmon *hw,
					uint adj);
	u32			(*get_throttle_adj)(struct bw_hwmon *hw);
	struct device		*dev;
	enum dcvs_hw_type	dcvs_hw;
	enum dcvs_path_type	dcvs_path;
	u32			dcvs_width;
	enum dcvs_hw_type	second_dcvs_hw;
	u32			second_dcvs_width;
	bool			second_vote_supported;
	u32			second_vote_limit;
	struct bwmon_second_map	*second_map;
	struct hwmon_node	*node;
	ktime_t			last_update_ts;
	struct work_struct	work;
	bool			is_active;
	unsigned long		up_wake_mbps;
	unsigned long		down_wake_mbps;
	unsigned int		down_cnt;
};

struct bwmon {
	void __iomem		*base;
	void __iomem		*global_base;
	unsigned int		mport;
	int			irq;
	const struct bwmon_spec	*spec;
	struct device		*dev;
	struct bw_hwmon		hw;
	u32			hw_timer_hz;
	u32			throttle_adj;
	u32			sample_size_ms;
	u32			intr_status;
	u8			count_shift;
	u32			thres_lim;
	u32			byte_mask;
	u32			byte_match;
};

struct hwmon_node {
	u32			hw_min_freq;
	u32			hw_max_freq;
	u32			min_freq;
	u32			max_freq;
	struct dcvs_freq	cur_freqs[2];
	u32			window_ms;
	unsigned int		guard_band_mbps;
	unsigned int		decay_rate;
	unsigned int		io_percent;
	unsigned int		bw_step;
	unsigned int		sample_ms;
	unsigned int		up_scale;
	unsigned int		up_thres;
	unsigned int		down_thres;
	unsigned int		down_count;
	unsigned int		hist_memory;
	unsigned int		hyst_trigger_count;
	unsigned int		hyst_length;
	unsigned int		idle_length;
	unsigned int		idle_mbps;
	unsigned int		ab_scale;
	unsigned int		mbps_zones[NUM_MBPS_ZONES];
	unsigned long		prev_ab;
	unsigned long		bytes;
	unsigned long		max_mbps;
	unsigned long		hist_max_mbps;
	unsigned long		hist_mem;
	unsigned long		hyst_peak;
	unsigned long		hyst_mbps;
	unsigned long		hyst_trig_win;
	unsigned long		hyst_en;
	unsigned long		idle_en;
	unsigned long		prev_req;
	unsigned int		wake;
	unsigned int		down_cnt;
	ktime_t			prev_ts;
	ktime_t			hist_max_ts;
	bool			sampled;
	bool			mon_started;
	struct list_head	list;
	struct bw_hwmon		*hw;
	struct kobject		kobj;
	struct mutex		mon_lock;
	struct mutex		update_lock;
};

/* BWMON register offsets */
#define GLB_INT_STATUS(m)	((m)->global_base + 0x100)
#define GLB_INT_CLR(m)		((m)->global_base + 0x108)
#define	GLB_INT_EN(m)		((m)->global_base + 0x10C)
#define MON_INT_STATUS(m)	((m)->base + 0x100)
#define MON_INT_STATUS_MASK	0x03
#define MON2_INT_STATUS_MASK	0xF0
#define MON2_INT_STATUS_SHIFT	4
#define MON_INT_CLR(m)		((m)->base + 0x108)
#define	MON_INT_EN(m)		((m)->base + 0x10C)
#define MON_INT_ENABLE		0x1
#define	MON_EN(m)		((m)->base + 0x280)
#define MON_CLEAR(m)		((m)->base + 0x284)
#define MON_CNT(m)		((m)->base + 0x288)
#define MON_THRES(m)		((m)->base + 0x290)
#define MON_MASK(m)		((m)->base + 0x298)
#define MON_MATCH(m)		((m)->base + 0x29C)

#define MON2_EN(m)		((m)->base + 0x2A0)
#define MON2_CLEAR(m)		((m)->base + 0x2A4)
#define MON2_SW(m)		((m)->base + 0x2A8)
#define MON2_THRES_HI(m)	((m)->base + 0x2AC)
#define MON2_THRES_MED(m)	((m)->base + 0x2B0)
#define MON2_THRES_LO(m)	((m)->base + 0x2B4)
#define MON2_ZONE_ACTIONS(m)	((m)->base + 0x2B8)
#define MON2_ZONE_CNT_THRES(m)	((m)->base + 0x2BC)
#define MON2_BYTE_CNT(m)	((m)->base + 0x2D0)
#define MON2_WIN_TIMER(m)	((m)->base + 0x2D4)
#define MON2_ZONE_CNT(m)	((m)->base + 0x2D8)
#define MON2_ZONE_MAX(m, zone)	((m)->base + 0x2E0 + 0x4 * zone)

#define MON3_INT_STATUS(m)	((m)->base + 0x00)
#define MON3_INT_CLR(m)		((m)->base + 0x08)
#define MON3_INT_EN(m)		((m)->base + 0x0C)
#define MON3_INT_STATUS_MASK	0x0F
#define MON3_EN(m)		((m)->base + 0x10)
#define MON3_CLEAR(m)		((m)->base + 0x14)
#define MON3_MASK(m)		((m)->base + 0x18)
#define MON3_MATCH(m)		((m)->base + 0x1C)
#define MON3_SW(m)		((m)->base + 0x20)
#define MON3_THRES_HI(m)	((m)->base + 0x24)
#define MON3_THRES_MED(m)	((m)->base + 0x28)
#define MON3_THRES_LO(m)	((m)->base + 0x2C)
#define MON3_ZONE_ACTIONS(m)	((m)->base + 0x30)
#define MON3_ZONE_CNT_THRES(m)	((m)->base + 0x34)
#define MON3_BYTE_CNT(m)	((m)->base + 0x38)
#define MON3_WIN_TIMER(m)	((m)->base + 0x3C)
#define MON3_ZONE_CNT(m)	((m)->base + 0x40)
#define MON3_ZONE_MAX(m, zone)	((m)->base + 0x44 + 0x4 * zone)

#endif /* _QCOM_BWMON_H */
