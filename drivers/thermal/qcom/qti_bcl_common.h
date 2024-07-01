/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/sched/clock.h>
#include <linux/thermal.h>

#define BPM_CLR_OFFSET 0xA1
#define MAX_BCL_LVL_COUNT 3
#define BCL_HISTORY_COUNT     10
#define BCL_STATS_NAME_LENGTH 30

enum bcl_dev_type {
	BCL_IBAT_LVL0,
	BCL_IBAT_LVL1,
	BCL_VBAT_LVL0,
	BCL_VBAT_LVL1,
	BCL_VBAT_LVL2,
	BCL_LVL0,
	BCL_LVL1,
	BCL_LVL2,
	BCL_2S_IBAT_LVL0,
	BCL_2S_IBAT_LVL1,
	BCL_TYPE_MAX,
};

struct bcl_device;

struct bcl_data_history {
	uint32_t vbat;
	uint32_t ibat;
	unsigned long long trigger_ts;
	unsigned long long clear_ts;
};

struct bcl_bpm {
	int lvl0_cnt;
	int lvl1_cnt;
	int lvl2_cnt;
	int max_ibat;
	int sync_vbat;
	int min_vbat;
	int sync_ibat;
};

struct bcl_lvl_stats {
	uint32_t counter;
	uint32_t self_cleared_counter;
	bool trigger_state;
	unsigned long long max_mitig_ts;
	unsigned long long max_mitig_latency;
	unsigned long long max_duration;
	unsigned long long total_duration;
	struct mutex		stats_lock;
	struct bcl_device	*bcl_dev;
	struct bcl_data_history bcl_history[BCL_HISTORY_COUNT];
};

struct bcl_peripheral_data {
	int			irq_num;
	int			status_bit_idx;
	long			trip_thresh;
	int			last_val;
	struct mutex		state_trans_lock;
	bool			irq_enabled;
	enum bcl_dev_type	type;
	struct thermal_zone_device_ops ops;
	struct thermal_zone_device *tz_dev;
	struct bcl_device	*dev;
};

struct bcl_device {
	struct device			*dev;
	struct regmap			*regmap;
	uint16_t			fg_bcl_addr;
	uint8_t				dig_major;
	uint8_t				dig_minor;
	uint8_t				ana_major;
	uint8_t				bcl_param_1;
	uint8_t				bcl_type;
	uint8_t				enable_bpm;
	void				*ipc_log;
	bool				ibat_ccm_enabled;
	bool				ibat_use_qg_adc;
	bool				no_bit_shift;
	uint32_t			ibat_ext_range_factor;
	struct mutex			stats_lock;
	struct bcl_peripheral_data	param[BCL_TYPE_MAX];
	struct bcl_lvl_stats		stats[MAX_BCL_LVL_COUNT];
};

void bcl_stats_init(char *bcl_name, struct bcl_device *bcl_perph,
		     uint32_t stats_len);

void bcl_update_clear_stats(struct bcl_lvl_stats *bcl_stat);

void bcl_update_trigger_stats(struct bcl_lvl_stats *bcl_stat,
			int ibat, int vbat, unsigned long long trigger_ts);
int get_bpm_stats(struct bcl_device *bcl_dev, struct bcl_bpm *bpm_stats);
