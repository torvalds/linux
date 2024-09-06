// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "qcom-bwmon: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/log2.h>
#include <linux/sizes.h>
#include <linux/suspend.h>
#include <soc/qcom/dcvs.h>
#include <trace/hooks/sched.h>
#include "bwmon.h"
#include "trace-dcvs.h"

static LIST_HEAD(hwmon_list);
static DEFINE_SPINLOCK(list_lock);
static DEFINE_SPINLOCK(sample_irq_lock);
static DEFINE_SPINLOCK(mon_irq_lock);
static DEFINE_MUTEX(bwmon_lock);
static struct workqueue_struct *bwmon_wq;
static u32 get_dst_from_map(struct bw_hwmon *hw, u32 src_vote);

struct qcom_bwmon_attr {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count);
};

#define to_bwmon_attr(_attr) \
	container_of(_attr, struct qcom_bwmon_attr, attr)
#define to_hwmon_node(k)	container_of(k, struct hwmon_node, kobj)
#define to_bwmon(ptr)		container_of(ptr, struct bwmon, hw)

#define BWMON_ATTR_RW(_name)						\
struct qcom_bwmon_attr _name =						\
__ATTR(_name, 0644, show_##_name, store_##_name)			\

#define BWMON_ATTR_RO(_name)						\
struct qcom_bwmon_attr _name =						\
__ATTR(_name, 0444, show_##_name, NULL)					\

#define show_attr(name)							\
static ssize_t show_##name(struct kobject *kobj,			\
			struct attribute *attr, char *buf)		\
{									\
	struct hwmon_node *node = to_hwmon_node(kobj);			\
	return scnprintf(buf, PAGE_SIZE, "%u\n", node->name);		\
}									\

#define store_attr(name, _min, _max) \
static ssize_t store_##name(struct kobject *kobj,			\
			struct attribute *attr, const char *buf,	\
			size_t count)					\
{									\
	int ret;							\
	unsigned int val;						\
	struct hwmon_node *node = to_hwmon_node(kobj);			\
	ret = kstrtouint(buf, 10, &val);				\
	if (ret < 0)							\
		return ret;						\
	val = max(val, _min);						\
	val = min(val, _max);						\
	node->name = val;						\
	return count;							\
}									\

#define show_list_attr(name, n)						\
static ssize_t show_##name(struct kobject *kobj,			\
			struct attribute *attr, char *buf)		\
{									\
	struct hwmon_node *node = to_hwmon_node(kobj);			\
	unsigned int i, cnt = 0;					\
									\
	for (i = 0; i < n && node->name[i]; i++)			\
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "%u ",	\
							node->name[i]);	\
	cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "\n");		\
	return cnt;							\
}									\

#define store_list_attr(name, n, _min, _max)				\
static ssize_t store_##name(struct kobject *kobj,			\
			struct attribute *attr, const char *buf,	\
			size_t count)					\
{									\
	struct hwmon_node *node = to_hwmon_node(kobj);			\
	int ret, numvals;						\
	unsigned int i = 0, val;					\
	char **strlist;							\
									\
	strlist = argv_split(GFP_KERNEL, buf, &numvals);		\
	if (!strlist)							\
		return -ENOMEM;						\
	numvals = min(numvals, n - 1);					\
	for (i = 0; i < numvals; i++) {					\
		ret = kstrtouint(strlist[i], 10, &val);			\
		if (ret < 0)						\
			goto out;					\
		val = max(val, _min);					\
		val = min(val, _max);					\
		node->name[i] = val;					\
	}								\
	ret = count;							\
out:									\
	argv_free(strlist);						\
	node->name[i] = 0;						\
	return ret;							\
}									\

static ssize_t store_min_freq(struct kobject *kobj,
			struct attribute *attr, const char *buf,
			size_t count)
{
	int ret;
	unsigned int freq;
	struct hwmon_node *node = to_hwmon_node(kobj);

	ret = kstrtouint(buf, 10, &freq);
	if (ret < 0)
		return ret;
	freq = max(freq, node->hw_min_freq);
	freq = min(freq, node->max_freq);

	node->min_freq = freq;

	return count;
}

static ssize_t store_max_freq(struct kobject *kobj,
			struct attribute *attr, const char *buf,
			size_t count)
{
	int ret;
	unsigned int freq;
	struct hwmon_node *node = to_hwmon_node(kobj);

	ret = kstrtouint(buf, 10, &freq);
	if (ret < 0)
		return ret;
	freq = max(freq, node->min_freq);
	freq = min(freq, node->hw_max_freq);

	node->max_freq = freq;

	return count;
}

static ssize_t store_throttle_adj(struct kobject *kobj,
			struct attribute *attr, const char *buf,
			size_t count)
{
	struct hwmon_node *node = to_hwmon_node(kobj);
	int ret;
	unsigned int val;

	if (!node->hw->set_throttle_adj)
		return -EPERM;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = node->hw->set_throttle_adj(node->hw, val);

	if (!ret)
		return count;
	else
		return ret;
}

static ssize_t show_throttle_adj(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	struct hwmon_node *node = to_hwmon_node(kobj);
	unsigned int val;

	if (!node->hw->get_throttle_adj)
		val = 0;
	else
		val = node->hw->get_throttle_adj(node->hw);

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

#define SAMPLE_MIN_MS	1U
#define SAMPLE_MAX_MS	50U
static ssize_t store_sample_ms(struct kobject *kobj,
			struct attribute *attr, const char *buf,
			size_t count)
{
	struct hwmon_node *node = to_hwmon_node(kobj);

	int ret;
	unsigned int val;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	val = max(val, SAMPLE_MIN_MS);
	val = min(val, SAMPLE_MAX_MS);
	if (val > node->window_ms)
		return -EINVAL;

	node->sample_ms = val;
	return count;
}

static ssize_t show_cur_freq(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	struct hwmon_node *node = to_hwmon_node(kobj);

	return scnprintf(buf, PAGE_SIZE, "%u\n", node->cur_freqs[0].ib);
}

static ssize_t store_second_vote_limit(struct kobject *kobj,
			struct attribute *attr, const char *buf,
			size_t count)
{
	struct hwmon_node *node = to_hwmon_node(kobj);
	struct bw_hwmon *hw = node->hw;
	int ret;
	unsigned int val;

	if (!hw->second_vote_supported)
		return -ENODEV;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val == hw->second_vote_limit)
		return count;

	mutex_lock(&node->update_lock);
	if (val >= node->cur_freqs[1].ib)
		goto unlock_out;
	node->cur_freqs[1].ib = val;
	ret = qcom_dcvs_update_votes(dev_name(hw->dev), node->cur_freqs, 0x3,
							hw->dcvs_path);
	if (ret < 0)
		dev_err(hw->dev, "second vote update failed: %d\n", ret);
unlock_out:
	hw->second_vote_limit = val;
	mutex_unlock(&node->update_lock);

	return count;
}

static ssize_t show_second_vote_limit(struct kobject *kobj,
			struct attribute *attr, char *buf)
{
	struct hwmon_node *node = to_hwmon_node(kobj);
	struct bw_hwmon *hw = node->hw;

	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->second_vote_limit);
}
show_attr(min_freq);
static BWMON_ATTR_RW(min_freq);
show_attr(max_freq);
static BWMON_ATTR_RW(max_freq);
static BWMON_ATTR_RW(throttle_adj);
show_attr(sample_ms);
static BWMON_ATTR_RW(sample_ms);
static BWMON_ATTR_RO(cur_freq);
static BWMON_ATTR_RW(second_vote_limit);

show_attr(window_ms);
store_attr(window_ms, 8U, 1000U);
static BWMON_ATTR_RW(window_ms);
show_attr(guard_band_mbps);
store_attr(guard_band_mbps, 0U, 2000U);
static BWMON_ATTR_RW(guard_band_mbps);
show_attr(decay_rate);
store_attr(decay_rate, 0U, 100U);
static BWMON_ATTR_RW(decay_rate);
show_attr(io_percent);
store_attr(io_percent, 1U, 400U);
static BWMON_ATTR_RW(io_percent);
show_attr(bw_step);
store_attr(bw_step, 50U, 1000U);
static BWMON_ATTR_RW(bw_step);
show_attr(up_scale);
store_attr(up_scale, 0U, 500U);
static BWMON_ATTR_RW(up_scale);
show_attr(up_thres);
store_attr(up_thres, 1U, 100U);
static BWMON_ATTR_RW(up_thres);
show_attr(down_thres);
store_attr(down_thres, 0U, 90U);
static BWMON_ATTR_RW(down_thres);
show_attr(down_count);
store_attr(down_count, 0U, 90U);
static BWMON_ATTR_RW(down_count);
show_attr(hist_memory);
store_attr(hist_memory, 0U, 90U);
static BWMON_ATTR_RW(hist_memory);
show_attr(hyst_trigger_count);
store_attr(hyst_trigger_count, 0U, 90U);
static BWMON_ATTR_RW(hyst_trigger_count);
show_attr(hyst_length);
store_attr(hyst_length, 0U, 90U);
static BWMON_ATTR_RW(hyst_length);
show_attr(idle_length);
store_attr(idle_length, 0U, 90U);
static BWMON_ATTR_RW(idle_length);
show_attr(idle_mbps);
store_attr(idle_mbps, 0U, 2000U);
static BWMON_ATTR_RW(idle_mbps);
show_attr(ab_scale);
store_attr(ab_scale, 0U, 100U);
static BWMON_ATTR_RW(ab_scale);
show_list_attr(mbps_zones, NUM_MBPS_ZONES);
store_list_attr(mbps_zones, NUM_MBPS_ZONES, 0U, UINT_MAX);
static BWMON_ATTR_RW(mbps_zones);

static struct attribute *bwmon_attrs[] = {
	&min_freq.attr,
	&max_freq.attr,
	&cur_freq.attr,
	&window_ms.attr,
	&guard_band_mbps.attr,
	&decay_rate.attr,
	&io_percent.attr,
	&bw_step.attr,
	&sample_ms.attr,
	&up_scale.attr,
	&up_thres.attr,
	&down_thres.attr,
	&down_count.attr,
	&hist_memory.attr,
	&hyst_trigger_count.attr,
	&hyst_length.attr,
	&idle_length.attr,
	&idle_mbps.attr,
	&ab_scale.attr,
	&mbps_zones.attr,
	&throttle_adj.attr,
	&second_vote_limit.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bwmon);

static ssize_t attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct qcom_bwmon_attr *bwmon_attr = to_bwmon_attr(attr);
	ssize_t ret = -EIO;

	if (bwmon_attr->show)
		ret = bwmon_attr->show(kobj, attr, buf);

	return ret;
}

static ssize_t attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct qcom_bwmon_attr *bwmon_attr = to_bwmon_attr(attr);
	ssize_t ret = -EIO;

	if (bwmon_attr->store)
		ret = bwmon_attr->store(kobj, attr, buf, count);

	return ret;
}

static const struct sysfs_ops bwmon_sysfs_ops = {
	.show	= attr_show,
	.store	= attr_store,
};

static struct kobj_type bwmon_ktype = {
	.sysfs_ops	= &bwmon_sysfs_ops,
	.default_groups	= bwmon_groups,

};

/* Returns MBps of read/writes for the sampling window. */
static unsigned long bytes_to_mbps(unsigned long long bytes, unsigned int us)
{
	bytes *= USEC_PER_SEC;
	do_div(bytes, us);
	bytes = DIV_ROUND_UP_ULL(bytes, SZ_1M);
	return bytes;
}

static unsigned int mbps_to_bytes(unsigned long mbps, unsigned int ms)
{
	mbps *= ms;
	mbps = DIV_ROUND_UP(mbps, MSEC_PER_SEC);
	mbps *= SZ_1M;
	return mbps;
}

static int __bw_hwmon_sw_sample_end(struct bw_hwmon *hwmon)
{
	struct hwmon_node *node = hwmon->node;
	ktime_t ts;
	unsigned long bytes, mbps;
	unsigned int us;
	int wake = 0;

	ts = ktime_get();
	us = ktime_to_us(ktime_sub(ts, node->prev_ts));

	bytes = hwmon->get_bytes_and_clear(hwmon);
	bytes += node->bytes;
	node->bytes = 0;

	mbps = bytes_to_mbps(bytes, us);
	node->max_mbps = max(node->max_mbps, mbps);

	/*
	 * If the measured bandwidth in a micro sample is greater than the
	 * wake up threshold, it indicates an increase in load that's non
	 * trivial. So, have the governor ignore historical idle time or low
	 * bandwidth usage and do the bandwidth calculation based on just
	 * this micro sample.
	 */
	if (mbps > node->hw->up_wake_mbps) {
		wake = UP_WAKE;
	} else if (mbps < node->hw->down_wake_mbps) {
		if (node->down_cnt)
			node->down_cnt--;
		if (node->down_cnt <= 0)
			wake = DOWN_WAKE;
	}

	node->prev_ts = ts;
	node->wake = wake;
	node->sampled = true;

	trace_bw_hwmon_meas(dev_name(hwmon->dev),
				mbps,
				us,
				wake);

	return wake;
}

static int __bw_hwmon_hw_sample_end(struct bw_hwmon *hwmon)
{
	struct hwmon_node *node = hwmon->node;
	unsigned long bytes, mbps;
	int wake = 0;

	/*
	 * If this read is in response to an IRQ, the HW monitor should
	 * return the measurement in the micro sample that triggered the IRQ.
	 * Otherwise, it should return the maximum measured value in any
	 * micro sample since the last time we called get_bytes_and_clear()
	 */
	bytes = hwmon->get_bytes_and_clear(hwmon);
	mbps = bytes_to_mbps(bytes, node->sample_ms * USEC_PER_MSEC);
	node->max_mbps = mbps;

	if (mbps > node->hw->up_wake_mbps)
		wake = UP_WAKE;
	else if (mbps < node->hw->down_wake_mbps)
		wake = DOWN_WAKE;

	node->wake = wake;
	node->sampled = true;

	trace_bw_hwmon_meas(dev_name(hwmon->dev),
				mbps,
				node->sample_ms * USEC_PER_MSEC,
				wake);

	return 1;
}

static int __bw_hwmon_sample_end(struct bw_hwmon *hwmon)
{
	if (hwmon->set_hw_events)
		return __bw_hwmon_hw_sample_end(hwmon);
	else
		return __bw_hwmon_sw_sample_end(hwmon);
}

static int bw_hwmon_sample_end(struct bw_hwmon *hwmon)
{
	unsigned long flags;
	int wake;

	spin_lock_irqsave(&sample_irq_lock, flags);
	wake = __bw_hwmon_sample_end(hwmon);
	spin_unlock_irqrestore(&sample_irq_lock, flags);

	return wake;
}

static unsigned long to_mbps_zone(struct hwmon_node *node, unsigned long mbps)
{
	int i;

	for (i = 0; i < NUM_MBPS_ZONES && node->mbps_zones[i]; i++)
		if (node->mbps_zones[i] >= mbps)
			return node->mbps_zones[i];

	return KHZ_TO_MBPS(node->max_freq, node->hw->dcvs_width);
}

#define MIN_MBPS	500UL
#define HIST_PEAK_TOL	75
static unsigned long get_bw_and_set_irq(struct hwmon_node *node,
					struct dcvs_freq *freq_mbps)
{
	unsigned long meas_mbps, thres, flags, req_mbps, adj_mbps;
	unsigned long meas_mbps_zone;
	unsigned long hist_lo_tol, hyst_lo_tol;
	struct bw_hwmon *hw = node->hw;
	unsigned int new_bw, io_percent = node->io_percent;
	ktime_t ts;
	unsigned int ms = 0;

	spin_lock_irqsave(&sample_irq_lock, flags);

	if (!hw->set_hw_events) {
		ts = ktime_get();
		ms = ktime_to_ms(ktime_sub(ts, node->prev_ts));
	}
	if (!node->sampled || ms >= node->sample_ms)
		__bw_hwmon_sample_end(node->hw);
	node->sampled = false;

	req_mbps = meas_mbps = node->max_mbps;
	node->max_mbps = 0;

	hist_lo_tol = (node->hist_max_mbps * HIST_PEAK_TOL) / 100;
	/* Remember historic peak in the past hist_mem decision windows. */
	if (meas_mbps > node->hist_max_mbps || !node->hist_mem) {
		/* If new max or no history */
		node->hist_max_mbps = meas_mbps;
		node->hist_mem = node->hist_memory;
	} else if (meas_mbps >= hist_lo_tol) {
		/*
		 * If subsequent peaks come close (within tolerance) to but
		 * less than the historic peak, then reset the history start,
		 * but not the peak value.
		 */
		node->hist_mem = node->hist_memory;
	} else {
		/* Count down history expiration. */
		if (node->hist_mem)
			node->hist_mem--;
	}

	/*
	 * The AB value that corresponds to the lowest mbps zone greater than
	 * or equal to the "frequency" the current measurement will pick.
	 * This upper limit is useful for balancing out any prediction
	 * mechanisms to be power friendly.
	 */
	meas_mbps_zone = (meas_mbps * 100) / io_percent;
	meas_mbps_zone = to_mbps_zone(node, meas_mbps_zone);
	meas_mbps_zone = (meas_mbps_zone * io_percent) / 100;
	meas_mbps_zone = max(meas_mbps, meas_mbps_zone);

	/*
	 * If this is a wake up due to BW increase, vote much higher BW than
	 * what we measure to stay ahead of increasing traffic and then set
	 * it up to vote for measured BW if we see down_count short sample
	 * windows of low traffic.
	 */
	if (node->wake == UP_WAKE) {
		req_mbps += ((meas_mbps - node->prev_req)
				* node->up_scale) / 100;
		/*
		 * However if the measured load is less than the historic
		 * peak, but the over request is higher than the historic
		 * peak, then we could limit the over requesting to the
		 * historic peak.
		 */
		if (req_mbps > node->hist_max_mbps
		    && meas_mbps < node->hist_max_mbps)
			req_mbps = node->hist_max_mbps;

		req_mbps = min(req_mbps, meas_mbps_zone);
	}

	hyst_lo_tol = (node->hyst_mbps * HIST_PEAK_TOL) / 100;
	if (meas_mbps > node->hyst_mbps && meas_mbps > MIN_MBPS) {
		hyst_lo_tol = (meas_mbps * HIST_PEAK_TOL) / 100;
		node->hyst_peak = 0;
		node->hyst_trig_win = node->hyst_length;
		node->hyst_mbps = meas_mbps;
		if (node->hyst_en)
			node->hyst_en = node->hyst_length;
	}

	/*
	 * Check node->max_mbps to avoid double counting peaks that cause
	 * early termination of a window.
	 */
	if (meas_mbps >= hyst_lo_tol && meas_mbps > MIN_MBPS
	    && !node->max_mbps) {
		node->hyst_peak++;
		if (node->hyst_peak >= node->hyst_trigger_count) {
			node->hyst_peak = 0;
			node->hyst_en = node->hyst_length;
		}
	}

	if (node->hyst_trig_win)
		node->hyst_trig_win--;
	if (node->hyst_en)
		node->hyst_en--;

	if (!node->hyst_trig_win && !node->hyst_en) {
		node->hyst_peak = 0;
		node->hyst_mbps = 0;
	}

	if (node->hyst_en) {
		if (meas_mbps > node->idle_mbps) {
			req_mbps = max(req_mbps, node->hyst_mbps);
			node->idle_en = node->idle_length;
		} else if (node->idle_en) {
			req_mbps = max(req_mbps, node->hyst_mbps);
			node->idle_en--;
		}
	}

	/* Stretch the short sample window size, if the traffic is too low */
	if (meas_mbps < MIN_MBPS) {
		hw->up_wake_mbps = (max(MIN_MBPS, req_mbps)
					* (100 + node->up_thres)) / 100;
		hw->down_wake_mbps = 0;
		thres = mbps_to_bytes(max(MIN_MBPS, req_mbps / 2),
					node->sample_ms);
	} else {
		/*
		 * Up wake vs down wake are intentionally a percentage of
		 * req_mbps vs meas_mbps to make sure the over requesting
		 * phase is handled properly. We only want to wake up and
		 * reduce the vote based on the measured mbps being less than
		 * the previous measurement that caused the "over request".
		 */
		hw->up_wake_mbps = (req_mbps * (100 + node->up_thres)) / 100;
		hw->down_wake_mbps = (meas_mbps * node->down_thres) / 100;
		thres = mbps_to_bytes(meas_mbps, node->sample_ms);
	}

	if (hw->set_hw_events) {
		hw->down_cnt = node->down_count;
		hw->set_hw_events(hw, node->sample_ms);
	} else {
		node->down_cnt = node->down_count;
		node->bytes = hw->set_thres(hw, thres);
	}

	node->wake = 0;
	node->prev_req = req_mbps;

	spin_unlock_irqrestore(&sample_irq_lock, flags);

	adj_mbps = req_mbps + node->guard_band_mbps;

	if (adj_mbps > node->prev_ab) {
		new_bw = adj_mbps;
	} else {
		new_bw = adj_mbps * node->decay_rate
			+ node->prev_ab * (100 - node->decay_rate);
		new_bw /= 100;
	}

	node->prev_ab = new_bw;
	freq_mbps->ib = (new_bw * 100) / io_percent;
	if (node->ab_scale < 100)
		new_bw = mult_frac(new_bw, node->ab_scale, 100);
	freq_mbps->ab = roundup(new_bw, node->bw_step);
	trace_bw_hwmon_update(dev_name(node->hw->dev),
				freq_mbps->ab,
				freq_mbps->ib,
				hw->up_wake_mbps,
				hw->down_wake_mbps);

	trace_bw_hwmon_debug(dev_name(node->hw->dev),
				req_mbps,
				meas_mbps_zone,
				node->hist_max_mbps,
				node->hist_mem,
				node->hyst_mbps,
				node->hyst_en);
	return req_mbps;
}

static u32 get_dst_from_map(struct bw_hwmon *hw, u32 src_vote)
{
	struct bwmon_second_map *map = hw->second_map;
	u32 dst_vote = 0;

	if (!map)
		goto out;

	while (map->src_freq && map->src_freq < src_vote)
		map++;
	if (!map->src_freq)
		map--;
	dst_vote = map->dst_freq;

out:
	return dst_vote;
}

/*
 * Governor function that computes new target frequency
 * based on bw measurement (mbps) and updates cur_freq (khz).
 * Returns true if cur_freq was changed
 * Note: must hold node->update_lock before calling
 */
static bool bwmon_update_cur_freq(struct hwmon_node *node)
{
	struct bw_hwmon *hw = node->hw;
	struct dcvs_freq new_freq;
	u32 primary_mbps;

	get_bw_and_set_irq(node, &new_freq);

	/* first convert freq from mbps to khz */
	new_freq.ab = MBPS_TO_KHZ(new_freq.ab, hw->dcvs_width);
	new_freq.ib = MBPS_TO_KHZ(new_freq.ib, hw->dcvs_width);
	new_freq.ib = max(new_freq.ib, node->min_freq);
	new_freq.ib = min(new_freq.ib, node->max_freq);
	primary_mbps = KHZ_TO_MBPS(new_freq.ib, hw->dcvs_width);

	if (new_freq.ib != node->cur_freqs[0].ib ||
			new_freq.ab != node->cur_freqs[0].ab) {
		node->cur_freqs[0].ib = new_freq.ib;
		node->cur_freqs[0].ab = new_freq.ab;
		if (hw->second_vote_supported) {
			if (hw->second_map)
				node->cur_freqs[1].ib = get_dst_from_map(hw,
								new_freq.ib);
			else if (hw->second_dcvs_width)
				node->cur_freqs[1].ib = MBPS_TO_KHZ(primary_mbps,
							hw->second_dcvs_width);
			else
				node->cur_freqs[1].ib = 0;
			node->cur_freqs[1].ib = min(node->cur_freqs[1].ib,
							hw->second_vote_limit);
		}
		return true;
	}

	return false;
}

static const u64 HALF_TICK_NS = (NSEC_PER_SEC / HZ) >> 1;
static void bwmon_jiffies_update_cb(void *unused, void *extra)
{
	struct bw_hwmon *hw;
	struct hwmon_node *node;
	unsigned long flags;
	ktime_t now = ktime_get();
	s64 delta_ns;

	spin_lock_irqsave(&list_lock, flags);
	list_for_each_entry(node, &hwmon_list, list) {
		hw = node->hw;
		if (!hw->is_active)
			continue;
		delta_ns = now - hw->last_update_ts + HALF_TICK_NS;
		if (delta_ns > ms_to_ktime(hw->node->window_ms)) {
			queue_work(bwmon_wq, &hw->work);
			hw->last_update_ts = now;
		}
	}
	spin_unlock_irqrestore(&list_lock, flags);
}

static void bwmon_monitor_work(struct work_struct *work)
{
	int err = 0;
	struct bw_hwmon *hw = container_of(work, struct bw_hwmon, work);
	struct hwmon_node *node = hw->node;

	/* governor update and commit */
	mutex_lock(&node->update_lock);
	if (bwmon_update_cur_freq(node))
		err = qcom_dcvs_update_votes(dev_name(hw->dev),
					node->cur_freqs,
					1 + (hw->second_vote_supported << 1),
					hw->dcvs_path);
	if (err < 0)
		dev_err(hw->dev, "bwmon monitor update failed: %d\n", err);
	mutex_unlock(&node->update_lock);
}

static inline void bwmon_monitor_start(struct bw_hwmon *hw)
{
	hw->last_update_ts = ktime_get();
	hw->is_active = true;
}

static inline void bwmon_monitor_stop(struct bw_hwmon *hw)
{
	hw->is_active = false;
	cancel_work_sync(&hw->work);
}

static int update_bw_hwmon(struct bw_hwmon *hw)
{
	struct hwmon_node *node = hw->node;
	int ret = 0;

	mutex_lock(&node->mon_lock);
	if (!node->mon_started) {
		mutex_unlock(&node->mon_lock);
		return -EBUSY;
	}
	dev_dbg(hw->dev, "Got update request\n");
	bwmon_monitor_stop(hw);

	/* governor update and commit */
	mutex_lock(&node->update_lock);
	if (bwmon_update_cur_freq(node))
		ret = qcom_dcvs_update_votes(dev_name(hw->dev),
					node->cur_freqs,
					1 + (hw->second_vote_supported << 1),
					hw->dcvs_path);
	if (ret < 0)
		dev_err(hw->dev, "bwmon irq update failed: %d\n", ret);
	mutex_unlock(&node->update_lock);

	bwmon_monitor_start(hw);
	mutex_unlock(&node->mon_lock);

	return 0;
}

static int start_monitor(struct bw_hwmon *hwmon)
{
	struct hwmon_node *node = hwmon->node;
	unsigned long mbps;
	int ret;

	node->prev_ts = ktime_get();
	node->prev_ab = 0;
	mbps = KHZ_TO_MBPS(node->cur_freqs[0].ib, hwmon->dcvs_width) *
					node->io_percent / 100;
	hwmon->up_wake_mbps = mbps;
	hwmon->down_wake_mbps = MIN_MBPS;
	ret = hwmon->start_hwmon(hwmon, mbps);
	if (ret < 0) {
		dev_err(hwmon->dev, "Unable to start HW monitor! (%d)\n", ret);
		return ret;
	}

	node->mon_started = true;

	return 0;
}

static void stop_monitor(struct bw_hwmon *hwmon)
{
	struct hwmon_node *node = hwmon->node;

	mutex_lock(&node->mon_lock);
	node->mon_started = false;
	mutex_unlock(&node->mon_lock);

	hwmon->stop_hwmon(hwmon);

}

static int configure_hwmon_node(struct bw_hwmon *hwmon)
{
	struct hwmon_node *node;
	unsigned long flags;

	node = devm_kzalloc(hwmon->dev, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	hwmon->node = node;

	node->guard_band_mbps = 100;
	node->decay_rate = 90;
	node->io_percent = 16;
	node->bw_step = 190;
	node->sample_ms = 50;
	node->window_ms = 50;
	node->up_scale = 0;
	node->up_thres = 10;
	node->down_thres = 0;
	node->down_count = 3;
	node->hist_memory = 0;
	node->hyst_trigger_count = 3;
	node->hyst_length = 0;
	node->idle_length = 0;
	node->idle_mbps = 400;
	node->ab_scale = 100;
	node->mbps_zones[0] = 0;
	node->hw = hwmon;

	mutex_init(&node->mon_lock);
	mutex_init(&node->update_lock);
	spin_lock_irqsave(&list_lock, flags);
	list_add_tail(&node->list, &hwmon_list);
	spin_unlock_irqrestore(&list_lock, flags);

	return 0;
}

#define SECOND_MAP_TBL	"qcom,secondary-map"
#define NUM_COLS	2
static struct bwmon_second_map *init_second_map(struct device *dev,
						struct device_node *of_node)
{
	int len, nf, i, j;
	u32 data;
	struct bwmon_second_map *tbl;
	int ret;

	if (!of_find_property(of_node, SECOND_MAP_TBL, &len))
		return NULL;
	len /= sizeof(data);

	if (len % NUM_COLS || len == 0)
		return NULL;
	nf = len / NUM_COLS;

	tbl = devm_kzalloc(dev, (nf + 1) * sizeof(struct bwmon_second_map),
			GFP_KERNEL);
	if (!tbl)
		return NULL;

	for (i = 0, j = 0; i < nf; i++, j += 2) {
		ret = of_property_read_u32_index(of_node, SECOND_MAP_TBL,
							j, &data);
		if (ret < 0)
			return NULL;
		tbl[i].src_freq = data;

		ret = of_property_read_u32_index(of_node, SECOND_MAP_TBL,
							j + 1, &data);
		if (ret < 0)
			return NULL;
		tbl[i].dst_freq = data;
		pr_debug("Entry%d src:%u, dst:%u\n", i, tbl[i].src_freq,
				tbl[i].dst_freq);
	}
	tbl[i].src_freq = 0;

	return tbl;

}

static int bwmon_pm_notifier(struct notifier_block *nb, unsigned long action,
				void *unused)
{
	struct bw_hwmon *hw = container_of(nb, struct bw_hwmon, pm_nb);

	if (action == PM_HIBERNATION_PREPARE)
		stop_monitor(hw);
	else if (action == PM_POST_HIBERNATION)
		start_monitor(hw);

	return NOTIFY_OK;
}

#define ENABLE_MASK BIT(0)
static __always_inline void mon_enable(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON_EN(m));
		break;
	case MON2:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON2_EN(m));
		break;
	case MON3:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON3_EN(m));
		break;
	}
}

static __always_inline void mon_disable(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(m->throttle_adj, MON_EN(m));
		break;
	case MON2:
		writel_relaxed(m->throttle_adj, MON2_EN(m));
		break;
	case MON3:
		writel_relaxed(m->throttle_adj, MON3_EN(m));
		break;
	}
	/*
	 * mon_disable() and mon_irq_clear(),
	 * If latter goes first and count happen to trigger irq, we would
	 * have the irq line high but no one handling it.
	 */
	mb();
}

#define MON_CLEAR_BIT	0x1
#define MON_CLEAR_ALL_BIT	0x2
static __always_inline
void mon_clear(struct bwmon *m, bool clear_all, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(MON_CLEAR_BIT, MON_CLEAR(m));
		break;
	case MON2:
		if (clear_all)
			writel_relaxed(MON_CLEAR_ALL_BIT, MON2_CLEAR(m));
		else
			writel_relaxed(MON_CLEAR_BIT, MON2_CLEAR(m));
		break;
	case MON3:
		if (clear_all)
			writel_relaxed(MON_CLEAR_ALL_BIT, MON3_CLEAR(m));
		else
			writel_relaxed(MON_CLEAR_BIT, MON3_CLEAR(m));
		/*
		 * In some hardware versions since MON3_CLEAR(m) register does
		 * not have self-clearing capability it needs to be cleared
		 * explicitly. But we also need to ensure the writes to it
		 * are successful before clearing it.
		 */
		wmb();
		writel_relaxed(0, MON3_CLEAR(m));
		break;
	}
	/*
	 * The counter clear and IRQ clear bits are not in the same 4KB
	 * region. So, we need to make sure the counter clear is completed
	 * before we try to clear the IRQ or do any other counter operations.
	 */
	mb();
}

#define	SAMPLE_WIN_LIM	0xFFFFFF
static __always_inline
void mon_set_hw_sampling_window(struct bwmon *m, unsigned int sample_ms,
				enum mon_reg_type type)
{
	u32 rate;

	if (unlikely(sample_ms != m->sample_size_ms)) {
		rate = mult_frac(sample_ms, m->hw_timer_hz, MSEC_PER_SEC);
		m->sample_size_ms = sample_ms;
		if (unlikely(rate > SAMPLE_WIN_LIM)) {
			rate = SAMPLE_WIN_LIM;
			pr_warn("Sample window %u larger than hw limit: %u\n",
					rate, SAMPLE_WIN_LIM);
		}
		switch (type) {
		case MON1:
			WARN(1, "Invalid\n");
			return;
		case MON2:
			writel_relaxed(rate, MON2_SW(m));
			break;
		case MON3:
			writel_relaxed(rate, MON3_SW(m));
			break;
		}
	}
}

static void mon_glb_irq_enable(struct bwmon *m)
{
	u32 val;

	val = readl_relaxed(GLB_INT_EN(m));
	val |= 1 << m->mport;
	writel_relaxed(val, GLB_INT_EN(m));
}

static __always_inline
void mon_irq_enable(struct bwmon *m, enum mon_reg_type type)
{
	u32 val;

	spin_lock(&mon_irq_lock);
	switch (type) {
	case MON1:
		mon_glb_irq_enable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val |= MON_INT_ENABLE;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON2:
		mon_glb_irq_enable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val |= MON2_INT_STATUS_MASK;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON3:
		val = readl_relaxed(MON3_INT_EN(m));
		val |= MON3_INT_STATUS_MASK;
		writel_relaxed(val, MON3_INT_EN(m));
		break;
	}
	spin_unlock(&mon_irq_lock);
	/*
	 * make sure irq enable complete for local and global
	 * to avoid race with other monitor calls
	 */
	mb();
}

static void mon_glb_irq_disable(struct bwmon *m)
{
	u32 val;

	val = readl_relaxed(GLB_INT_EN(m));
	val &= ~(1 << m->mport);
	writel_relaxed(val, GLB_INT_EN(m));
}

static __always_inline
void mon_irq_disable(struct bwmon *m, enum mon_reg_type type)
{
	u32 val;

	spin_lock(&mon_irq_lock);

	switch (type) {
	case MON1:
		mon_glb_irq_disable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val &= ~MON_INT_ENABLE;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON2:
		mon_glb_irq_disable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val &= ~MON2_INT_STATUS_MASK;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON3:
		val = readl_relaxed(MON3_INT_EN(m));
		val &= ~MON3_INT_STATUS_MASK;
		writel_relaxed(val, MON3_INT_EN(m));
		break;
	}
	spin_unlock(&mon_irq_lock);
	/*
	 * make sure irq disable complete for local and global
	 * to avoid race with other monitor calls
	 */
	mb();
}

static __always_inline
unsigned int mon_irq_status(struct bwmon *m, enum mon_reg_type type)
{
	u32 mval;

	switch (type) {
	case MON1:
		mval = readl_relaxed(MON_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x, g:%x\n", mval,
				readl_relaxed(GLB_INT_STATUS(m)));
		mval &= MON_INT_STATUS_MASK;
		break;
	case MON2:
		mval = readl_relaxed(MON_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x, g:%x\n", mval,
				readl_relaxed(GLB_INT_STATUS(m)));
		mval &= MON2_INT_STATUS_MASK;
		mval >>= MON2_INT_STATUS_SHIFT;
		break;
	case MON3:
		mval = readl_relaxed(MON3_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x\n", mval);
		mval &= MON3_INT_STATUS_MASK;
		break;
	}

	return mval;
}

static void mon_glb_irq_clear(struct bwmon *m)
{
	/*
	 * Synchronize the local interrupt clear in mon_irq_clear()
	 * with the global interrupt clear here. Otherwise, the CPU
	 * may reorder the two writes and clear the global interrupt
	 * before the local interrupt, causing the global interrupt
	 * to be retriggered by the local interrupt still being high.
	 */
	mb();
	writel_relaxed(1 << m->mport, GLB_INT_CLR(m));
	/*
	 * Similarly, because the global registers are in a different
	 * region than the local registers, we need to ensure any register
	 * writes to enable the monitor after this call are ordered with the
	 * clearing here so that local writes don't happen before the
	 * interrupt is cleared.
	 */
	mb();
}

static __always_inline
void mon_irq_clear(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(MON_INT_STATUS_MASK, MON_INT_CLR(m));
		mon_glb_irq_clear(m);
		break;
	case MON2:
		writel_relaxed(MON2_INT_STATUS_MASK, MON_INT_CLR(m));
		mon_glb_irq_clear(m);
		break;
	case MON3:
		writel_relaxed(MON3_INT_STATUS_MASK, MON3_INT_CLR(m));
		/*
		 * In some hardware versions since MON3_INT_CLEAR(m) register
		 * does not have self-clearing capability it needs to be
		 * cleared explicitly. But we also need to ensure the writes
		 * to it are successful before clearing it.
		 */
		wmb();
		writel_relaxed(0, MON3_INT_CLR(m));
		break;
	}
}

#define THROTTLE_MASK 0x1F
#define THROTTLE_SHIFT 16
static int mon_set_throttle_adj(struct bw_hwmon *hw, uint adj)
{
	struct bwmon *m = to_bwmon(hw);

	if (adj > THROTTLE_MASK)
		return -EINVAL;

	adj = (adj & THROTTLE_MASK) << THROTTLE_SHIFT;
	m->throttle_adj = adj;

	return 0;
}

static u32 mon_get_throttle_adj(struct bw_hwmon *hw)
{
	struct bwmon *m = to_bwmon(hw);

	return m->throttle_adj >> THROTTLE_SHIFT;
}

#define ZONE1_SHIFT	8
#define ZONE2_SHIFT	16
#define ZONE3_SHIFT	24
#define ZONE0_ACTION	0x01	/* Increment zone 0 count */
#define ZONE1_ACTION	0x09	/* Increment zone 1 & clear lower zones */
#define ZONE2_ACTION	0x25	/* Increment zone 2 & clear lower zones */
#define ZONE3_ACTION	0x95	/* Increment zone 3 & clear lower zones */
static u32 calc_zone_actions(void)
{
	u32 zone_actions;

	zone_actions = ZONE0_ACTION;
	zone_actions |= ZONE1_ACTION << ZONE1_SHIFT;
	zone_actions |= ZONE2_ACTION << ZONE2_SHIFT;
	zone_actions |= ZONE3_ACTION << ZONE3_SHIFT;

	return zone_actions;
}

#define ZONE_CNT_LIM	0xFFU
#define UP_CNT_1	1
static u32 calc_zone_counts(struct bw_hwmon *hw)
{
	u32 zone_counts;

	zone_counts = ZONE_CNT_LIM;
	zone_counts |= min(hw->down_cnt, ZONE_CNT_LIM) << ZONE1_SHIFT;
	zone_counts |= ZONE_CNT_LIM << ZONE2_SHIFT;
	zone_counts |= UP_CNT_1 << ZONE3_SHIFT;

	return zone_counts;
}

#define MB_SHIFT	20

static u32 mbps_to_count(unsigned long mbps, unsigned int ms, u8 shift)
{
	mbps *= ms;

	if (shift > MB_SHIFT)
		mbps >>= shift - MB_SHIFT;
	else
		mbps <<= MB_SHIFT - shift;

	return DIV_ROUND_UP(mbps, MSEC_PER_SEC);
}

/*
 * Define the 4 zones using HI, MED & LO thresholds:
 * Zone 0: byte count < THRES_LO
 * Zone 1: THRES_LO < byte count < THRES_MED
 * Zone 2: THRES_MED < byte count < THRES_HI
 * Zone 3: THRES_LIM > byte count > THRES_HI
 */
#define	THRES_LIM(shift)	(0xFFFFFFFF >> shift)

static __always_inline
void set_zone_thres(struct bwmon *m, unsigned int sample_ms,
		    enum mon_reg_type type)
{
	struct bw_hwmon *hw = &m->hw;
	u32 hi, med, lo;
	u32 zone_cnt_thres = calc_zone_counts(hw);

	hi = mbps_to_count(hw->up_wake_mbps, sample_ms, m->count_shift);
	med = mbps_to_count(hw->down_wake_mbps, sample_ms, m->count_shift);
	lo = 0;

	if (unlikely((hi > m->thres_lim) || (med > hi) || (lo > med))) {
		pr_warn("Zone thres larger than hw limit: hi:%u med:%u lo:%u\n",
				hi, med, lo);
		hi = min(hi, m->thres_lim);
		med = min(med, hi - 1);
		lo = min(lo, med-1);
	}

	switch (type) {
	case MON1:
		WARN(1, "Invalid\n");
		return;
	case MON2:
		writel_relaxed(hi, MON2_THRES_HI(m));
		writel_relaxed(med, MON2_THRES_MED(m));
		writel_relaxed(lo, MON2_THRES_LO(m));
		/* Set the zone count thresholds for interrupts */
		writel_relaxed(zone_cnt_thres, MON2_ZONE_CNT_THRES(m));
		break;
	case MON3:
		writel_relaxed(hi, MON3_THRES_HI(m));
		writel_relaxed(med, MON3_THRES_MED(m));
		writel_relaxed(lo, MON3_THRES_LO(m));
		/* Set the zone count thresholds for interrupts */
		writel_relaxed(zone_cnt_thres, MON3_ZONE_CNT_THRES(m));
		break;
	}

	dev_dbg(m->dev, "Thres: hi:%u med:%u lo:%u\n", hi, med, lo);
	dev_dbg(m->dev, "Zone Count Thres: %0x\n", zone_cnt_thres);
}

static __always_inline
void mon_set_zones(struct bwmon *m, unsigned int sample_ms,
		   enum mon_reg_type type)
{
	mon_set_hw_sampling_window(m, sample_ms, type);
	set_zone_thres(m, sample_ms, type);
}

static void mon_set_limit(struct bwmon *m, u32 count)
{
	writel_relaxed(count, MON_THRES(m));
	dev_dbg(m->dev, "Thres: %08x\n", count);
}

static u32 mon_get_limit(struct bwmon *m)
{
	return readl_relaxed(MON_THRES(m));
}

#define THRES_HIT(status)	(status & BIT(0))
#define OVERFLOW(status)	(status & BIT(1))
static unsigned long mon_get_count1(struct bwmon *m)
{
	unsigned long count, status;

	count = readl_relaxed(MON_CNT(m));
	status = mon_irq_status(m, MON1);

	dev_dbg(m->dev, "Counter: %08lx\n", count);

	if (OVERFLOW(status) && m->spec->overflow)
		count += 0xFFFFFFFF;
	if (THRES_HIT(status) && m->spec->wrap_on_thres)
		count += mon_get_limit(m);

	dev_dbg(m->dev, "Actual Count: %08lx\n", count);

	return count;
}

static __always_inline
unsigned int get_zone(struct bwmon *m, enum mon_reg_type type)
{
	u32 zone_counts;
	u32 zone;

	zone = get_bitmask_order(m->intr_status);
	if (zone) {
		zone--;
	} else {
		switch (type) {
		case MON1:
			WARN(1, "Invalid\n");
			return 0;
		case MON2:
			zone_counts = readl_relaxed(MON2_ZONE_CNT(m));
			break;
		case MON3:
			zone_counts = readl_relaxed(MON3_ZONE_CNT(m));
			break;
		}

		if (zone_counts) {
			zone = get_bitmask_order(zone_counts) - 1;
			zone /= 8;
		}
	}

	m->intr_status = 0;
	return zone;
}

static __always_inline
unsigned long get_zone_count(struct bwmon *m, unsigned int zone,
			     enum mon_reg_type type)
{
	unsigned long count;

	switch (type) {
	case MON1:
		WARN(1, "Invalid\n");
		return 0;
	case MON2:
		count = readl_relaxed(MON2_ZONE_MAX(m, zone));
		break;
	case MON3:
		count = readl_relaxed(MON3_ZONE_MAX(m, zone));
		break;
	}

	if (count)
		count++;

	return count;
}

static __always_inline
unsigned long mon_get_zone_stats(struct bwmon *m, enum mon_reg_type type)
{
	unsigned int zone;
	unsigned long count = 0;

	zone = get_zone(m, type);
	count = get_zone_count(m, zone, type);
	count <<= m->count_shift;

	dev_dbg(m->dev, "Zone%d Max byte count: %08lx\n", zone, count);

	return count;
}

static __always_inline
unsigned long mon_get_count(struct bwmon *m, enum mon_reg_type type)
{
	unsigned long count;

	switch (type) {
	case MON1:
		count = mon_get_count1(m);
		break;
	case MON2:
	case MON3:
		count = mon_get_zone_stats(m, type);
		break;
	}

	return count;
}

/* ********** CPUBW specific code  ********** */

static __always_inline
unsigned long __get_bytes_and_clear(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);
	unsigned long count;

	mon_disable(m, type);
	count = mon_get_count(m, type);
	mon_clear(m, false, type);
	mon_irq_clear(m, type);
	mon_enable(m, type);

	return count;
}

static unsigned long get_bytes_and_clear(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON1);
}

static unsigned long get_bytes_and_clear2(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON2);
}

static unsigned long get_bytes_and_clear3(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON3);
}

static unsigned long set_thres(struct bw_hwmon *hw, unsigned long bytes)
{
	unsigned long count;
	u32 limit;
	struct bwmon *m = to_bwmon(hw);

	mon_disable(m, MON1);
	count = mon_get_count1(m);
	mon_clear(m, false, MON1);
	mon_irq_clear(m, MON1);

	if (likely(!m->spec->wrap_on_thres))
		limit = bytes;
	else
		limit = max(bytes, 500000UL);

	mon_set_limit(m, limit);
	mon_enable(m, MON1);

	return count;
}

static unsigned long
__set_hw_events(struct bw_hwmon *hw, unsigned int sample_ms,
		enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);

	mon_disable(m, type);
	mon_clear(m, false, type);
	mon_irq_clear(m, type);

	mon_set_zones(m, sample_ms, type);
	mon_enable(m, type);

	return 0;
}

static unsigned long set_hw_events(struct bw_hwmon *hw, unsigned int sample_ms)
{
	return __set_hw_events(hw, sample_ms, MON2);
}

static unsigned long
set_hw_events3(struct bw_hwmon *hw, unsigned int sample_ms)
{
	return __set_hw_events(hw, sample_ms, MON3);
}

static irqreturn_t
__bwmon_intr_handler(int irq, void *dev, enum mon_reg_type type)
{
	struct bwmon *m = dev;

	m->intr_status = mon_irq_status(m, type);
	if (!m->intr_status)
		return IRQ_NONE;

	if (bw_hwmon_sample_end(&m->hw) > 0)
		return IRQ_WAKE_THREAD;

	return IRQ_HANDLED;
}

static irqreturn_t bwmon_intr_handler(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON1);
}

static irqreturn_t bwmon_intr_handler2(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON2);
}

static irqreturn_t bwmon_intr_handler3(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON3);
}

static irqreturn_t bwmon_intr_thread(int irq, void *dev)
{
	struct bwmon *m = dev;

	update_bw_hwmon(&m->hw);
	return IRQ_HANDLED;
}

static __always_inline
void mon_set_byte_count_filter(struct bwmon *m, enum mon_reg_type type)
{
	if (!m->byte_mask)
		return;

	switch (type) {
	case MON1:
	case MON2:
		writel_relaxed(m->byte_mask, MON_MASK(m));
		writel_relaxed(m->byte_match, MON_MATCH(m));
		break;
	case MON3:
		writel_relaxed(m->byte_mask, MON3_MASK(m));
		writel_relaxed(m->byte_match, MON3_MATCH(m));
		break;
	}
}

static __always_inline int __start_bw_hwmon(struct bw_hwmon *hw,
		unsigned long mbps, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);
	u32 limit, zone_actions;
	int ret;
	irq_handler_t handler;

	switch (type) {
	case MON1:
		handler = bwmon_intr_handler;
		limit = mbps_to_bytes(mbps, hw->node->window_ms);
		break;
	case MON2:
		zone_actions = calc_zone_actions();
		handler = bwmon_intr_handler2;
		break;
	case MON3:
		zone_actions = calc_zone_actions();
		handler = bwmon_intr_handler3;
		break;
	}

	ret = request_threaded_irq(m->irq, handler, bwmon_intr_thread,
				  IRQF_ONESHOT | IRQF_SHARED,
				  dev_name(m->dev), m);
	if (ret < 0) {
		dev_err(m->dev, "Unable to register interrupt handler! (%d)\n",
			ret);
		return ret;
	}

	mon_disable(m, type);
	mon_clear(m, false, type);

	switch (type) {
	case MON1:
		mon_set_limit(m, limit);
		break;
	case MON2:
		mon_set_zones(m, hw->node->window_ms, type);
		/* Set the zone actions to increment appropriate counters */
		writel_relaxed(zone_actions, MON2_ZONE_ACTIONS(m));
		break;
	case MON3:
		mon_set_zones(m, hw->node->window_ms, type);
		/* Set the zone actions to increment appropriate counters */
		writel_relaxed(zone_actions, MON3_ZONE_ACTIONS(m));
	}

	mon_set_byte_count_filter(m, type);
	mon_irq_clear(m, type);
	mon_irq_enable(m, type);
	mon_enable(m, type);
	bwmon_monitor_start(hw);

	return 0;
}

static int start_bw_hwmon(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON1);
}

static int start_bw_hwmon2(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON2);
}

static int start_bw_hwmon3(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON3);
}

static __always_inline
void __stop_bw_hwmon(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);

	bwmon_monitor_stop(hw);
	mon_irq_disable(m, type);
	synchronize_irq(m->irq);
	free_irq(m->irq, m);
	mon_disable(m, type);
	mon_clear(m, true, type);
	mon_irq_clear(m, type);
}

static void stop_bw_hwmon(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON1);
}

static void stop_bw_hwmon2(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON2);
}

static void stop_bw_hwmon3(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON3);
}

/*************************************************************************/

static const struct bwmon_spec spec[] = {
	[0] = {
		.wrap_on_thres = true,
		.overflow = false,
		.throt_adj = false,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[1] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = false,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[2] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = true,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[3] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = true,
		.hw_sampling = true,
		.has_global_base = true,
		.reg_type = MON2,
	},
	[4] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = false,
		.hw_sampling = true,
		.reg_type = MON3,
	},
};

static const struct of_device_id qcom_bwmon_match_table[] = {
	{ .compatible = "qcom,bwmon", .data = &spec[0] },
	{ .compatible = "qcom,bwmon2", .data = &spec[1] },
	{ .compatible = "qcom,bwmon3", .data = &spec[2] },
	{ .compatible = "qcom,bwmon4", .data = &spec[3] },
	{ .compatible = "qcom,bwmon5", .data = &spec[4] },
	{}
};

static int qcom_bwmon_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct bwmon *m;
	struct hwmon_node *node;
	int ret;
	u32 data, count_unit;
	u32 dcvs_hw = NUM_DCVS_PATHS, second_hw = NUM_DCVS_PATHS;
	struct kobject *dcvs_kobj;
	struct device_node *of_node, *tmp_of_node;
	unsigned long flags;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = dev;
	m->hw.dev = dev;

	m->spec = of_device_get_match_data(dev);
	if (!m->spec) {
		dev_err(dev, "Unknown device type!\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "base");
	if (!res) {
		dev_err(dev, "base not found!\n");
		return -EINVAL;
	}
	m->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!m->base) {
		dev_err(dev, "Unable map base!\n");
		return -ENOMEM;
	}

	if (m->spec->has_global_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "global_base");
		if (!res) {
			dev_err(dev, "global_base not found!\n");
			return -EINVAL;
		}
		m->global_base = devm_ioremap(dev, res->start,
					      resource_size(res));
		if (!m->global_base) {
			dev_err(dev, "Unable map global_base!\n");
			return -ENOMEM;
		}

		ret = of_property_read_u32(dev->of_node, "qcom,mport", &data);
		if (ret < 0) {
			dev_err(dev, "mport not found! (%d)\n", ret);
			return ret;
		}
		m->mport = data;
	}

	m->irq = platform_get_irq(pdev, 0);
	if (m->irq < 0) {
		dev_err(dev, "Unable to get IRQ number\n");
		return m->irq;
	}

	if (m->spec->hw_sampling) {
		ret = of_property_read_u32(dev->of_node, "qcom,hw-timer-hz",
					   &m->hw_timer_hz);
		if (ret < 0) {
			dev_err(dev, "HW sampling rate not specified!\n");
			return ret;
		}
	}

	if (of_property_read_u32(dev->of_node, "qcom,count-unit", &count_unit))
		count_unit = SZ_1M;
	m->count_shift = order_base_2(count_unit);
	m->thres_lim = THRES_LIM(m->count_shift);

	switch (m->spec->reg_type) {
	case MON3:
		m->hw.start_hwmon = start_bw_hwmon3;
		m->hw.stop_hwmon = stop_bw_hwmon3;
		m->hw.get_bytes_and_clear = get_bytes_and_clear3;
		m->hw.set_hw_events = set_hw_events3;
		break;
	case MON2:
		m->hw.start_hwmon = start_bw_hwmon2;
		m->hw.stop_hwmon = stop_bw_hwmon2;
		m->hw.get_bytes_and_clear = get_bytes_and_clear2;
		m->hw.set_hw_events = set_hw_events;
		break;
	case MON1:
		m->hw.start_hwmon = start_bw_hwmon;
		m->hw.stop_hwmon = stop_bw_hwmon;
		m->hw.get_bytes_and_clear = get_bytes_and_clear;
		m->hw.set_thres = set_thres;
		break;
	}

	of_property_read_u32(dev->of_node, "qcom,byte-mid-match",
			     &m->byte_match);
	of_property_read_u32(dev->of_node, "qcom,byte-mid-mask",
			     &m->byte_mask);

	if (m->spec->throt_adj) {
		m->hw.set_throttle_adj = mon_set_throttle_adj;
		m->hw.get_throttle_adj = mon_get_throttle_adj;
	}

	of_node = of_parse_phandle(dev->of_node, "qcom,target-dev", 0);
	if (!of_node) {
		dev_err(dev, "Unable to find target-dev for bwmon device\n");
		return -EINVAL;
	}
	ret = of_property_read_u32(of_node, "qcom,dcvs-hw-type", &dcvs_hw);
	if (ret < 0 || dcvs_hw >= NUM_DCVS_HW_TYPES) {
		dev_err(dev, "invalid dcvs_hw=%d, ret=%d\n", dcvs_hw, ret);
		return -EINVAL;
	}
	m->hw.dcvs_hw = dcvs_hw;
	ret = of_property_read_u32(of_node, "qcom,bus-width",
							&m->hw.dcvs_width);
	if (ret < 0 || !m->hw.dcvs_width) {
		dev_err(dev, "invalid hw width=%d, ret=%d\n",
							m->hw.dcvs_width, ret);
		return -EINVAL;
	}
	m->hw.dcvs_path = DCVS_SLOW_PATH;

	of_node = of_parse_phandle(dev->of_node, "qcom,second-vote", 0);
	if (of_node) {
		tmp_of_node = of_parse_phandle(of_node, "qcom,target-dev", 0);
		if (!tmp_of_node) {
			dev_err(dev, "Unable to find target-dev for second vote\n");
			return -EINVAL;
		}
		ret = of_property_read_u32(tmp_of_node, "qcom,dcvs-hw-type",
						&second_hw);
		if (ret < 0 || second_hw >= NUM_DCVS_HW_TYPES) {
			dev_err(dev, "invalid sec dcvs_hw=%d, ret=%d\n",
							second_hw, ret);
			return -EINVAL;
		}
		m->hw.second_dcvs_hw = second_hw;
		if (of_find_property(of_node, "qcom,secondary-map", &ret)) {
			m->hw.second_map = init_second_map(dev, of_node);
			if (!m->hw.second_map) {
				dev_err(dev, "error importing second map!\n");
				return -EINVAL;
			}
		}
		if (!m->hw.second_map) {
			ret = of_property_read_u32(tmp_of_node, "qcom,bus-width",
						&m->hw.second_dcvs_width);
			if (ret < 0 || !m->hw.second_dcvs_width) {
				dev_err(dev, "invalid sec hw width=%d, ret=%d\n",
						m->hw.second_dcvs_width, ret);
				return -EINVAL;
			}
		}
		m->hw.second_vote_supported = true;
	}

	ret = qcom_dcvs_register_voter(dev_name(dev), dcvs_hw, m->hw.dcvs_path);
	if (ret < 0) {
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "qcom dcvs registration error: %d\n", ret);
		return ret;
	}

	if (m->hw.second_vote_supported) {
		ret = qcom_dcvs_register_voter(dev_name(dev), second_hw,
							DCVS_SLOW_PATH);
		if (ret < 0) {
			dev_err(dev, "second hw qcom dcvs reg err: %d\n", ret);
			return ret;
		}
	}

	ret = configure_hwmon_node(&m->hw);
	if (ret < 0) {
		dev_err(dev, "bwmon node configuration failed: %d\n", ret);
		return ret;
	}
	node = m->hw.node;

	ret = qcom_dcvs_hw_minmax_get(dcvs_hw, &node->hw_min_freq,
						&node->hw_max_freq);
	if (ret < 0) {
		dev_err(dev, "error getting minmax from qcom dcvs: %d\n", ret);
		return ret;
	}
	node->min_freq = node->hw_min_freq;
	node->max_freq = node->hw_max_freq;
	node->cur_freqs[0].ib = node->min_freq;
	node->cur_freqs[0].ab = 0;
	node->cur_freqs[0].hw_type = dcvs_hw;
	node->cur_freqs[1].hw_type = second_hw;
	/* second vote only enabled by default if secondary map is present */
	if (m->hw.second_map)
		m->hw.second_vote_limit = get_dst_from_map(&m->hw, U32_MAX);


	m->hw.is_active = false;
	mutex_lock(&bwmon_lock);
	if (!bwmon_wq) {
		bwmon_wq = create_freezable_workqueue("bwmon_wq");
		if (!bwmon_wq) {
			dev_err(dev, "Couldn't create bwmon workqueue.\n");
			mutex_unlock(&bwmon_lock);
			return -ENOMEM;
		}
		register_trace_android_vh_jiffies_update(
						bwmon_jiffies_update_cb, NULL);
	}
	mutex_unlock(&bwmon_lock);

	INIT_WORK(&m->hw.work, &bwmon_monitor_work);
	m->hw.pm_nb.notifier_call = bwmon_pm_notifier;
	register_pm_notifier(&m->hw.pm_nb);
	ret = start_monitor(&m->hw);
	if (ret < 0) {
		dev_err(dev, "Error starting BWMON monitor: %d\n", ret);
		goto err_sysfs;
	}

	dcvs_kobj = qcom_dcvs_kobject_get(dcvs_hw);
	if (IS_ERR(dcvs_kobj)) {
		ret = PTR_ERR(dcvs_kobj);
		dev_err(dev, "error getting kobj from qcom_dcvs: %d\n", ret);
		goto err_sysfs;
	}
	ret = kobject_init_and_add(&node->kobj, &bwmon_ktype, dcvs_kobj,
					dev_name(dev));
	if (ret < 0) {
		dev_err(dev, "failed to init bwmon kobj: %d\n", ret);
		kobject_put(&node->kobj);
		goto err_sysfs;
	}

	return 0;

err_sysfs:
	stop_monitor(&m->hw);
	spin_lock_irqsave(&list_lock, flags);
	list_del(&node->list);
	spin_unlock_irqrestore(&list_lock, flags);
	return ret;
}

static struct platform_driver qcom_bwmon_driver = {
	.probe = qcom_bwmon_driver_probe,
	.driver = {
		.name = "qcom-bwmon",
		.of_match_table = qcom_bwmon_match_table,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(qcom_bwmon_driver);

MODULE_DESCRIPTION("QCOM BWMON driver");
MODULE_LICENSE("GPL");
