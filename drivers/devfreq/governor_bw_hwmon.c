// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "bw-hwmon: " fmt

#include <linux/kernel.h>
#include <linux/sizes.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/time.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/devfreq.h>
#include <trace/events/power.h>
#include "governor.h"
#include "governor_bw_hwmon.h"

#define NUM_MBPS_ZONES		10
struct hwmon_node {
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
	unsigned int		idle_mbps;
	unsigned int		use_ab;
	unsigned int		mbps_zones[NUM_MBPS_ZONES];

	unsigned long		prev_ab;
	unsigned long		*dev_ab;
	unsigned long		resume_freq;
	unsigned long		resume_ab;
	unsigned long		bytes;
	unsigned long		max_mbps;
	unsigned long		hist_max_mbps;
	unsigned long		hist_mem;
	unsigned long		hyst_peak;
	unsigned long		hyst_mbps;
	unsigned long		hyst_trig_win;
	unsigned long		hyst_en;
	unsigned long		prev_req;
	unsigned int		wake;
	unsigned int		down_cnt;
	ktime_t			prev_ts;
	ktime_t			hist_max_ts;
	bool			sampled;
	bool			mon_started;
	struct list_head	list;
	void			*orig_data;
	struct bw_hwmon		*hw;
	struct devfreq_governor	*gov;
	struct attribute_group	*attr_grp;
	struct mutex		mon_lock;
};

#define UP_WAKE 1
#define DOWN_WAKE 2
static DEFINE_SPINLOCK(irq_lock);

static LIST_HEAD(hwmon_list);
static DEFINE_MUTEX(list_lock);

static int use_cnt;
static DEFINE_MUTEX(state_lock);

static DEFINE_MUTEX(event_handle_lock);

#define show_attr(name) \
static ssize_t name##_show(struct device *dev,				\
			struct device_attribute *attr, char *buf)	\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct hwmon_node *hw = df->data;				\
	return scnprintf(buf, PAGE_SIZE, "%u\n", hw->name);		\
}

#define store_attr(name, _min, _max) \
static ssize_t name##_store(struct device *dev,				\
			struct device_attribute *attr, const char *buf,	\
			size_t count)					\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct hwmon_node *hw = df->data;				\
	int ret;							\
	unsigned int val;						\
	ret = kstrtoint(buf, 10, &val);					\
	if (ret < 0)							\
		return ret;						\
	val = max(val, _min);						\
	val = min(val, _max);						\
	hw->name = val;							\
	return count;							\
}

#define show_list_attr(name, n) \
static ssize_t name##_show(struct device *dev,			\
			struct device_attribute *attr, char *buf)	\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct hwmon_node *hw = df->data;				\
	unsigned int i, cnt = 0;					\
									\
	for (i = 0; i < n && hw->name[i]; i++)				\
		cnt += scnprintf(buf + cnt, PAGE_SIZE, "%u ", hw->name[i]);\
	cnt += scnprintf(buf + cnt, PAGE_SIZE, "\n");			\
	return cnt;							\
}

#define store_list_attr(name, n, _min, _max) \
static ssize_t name##_store(struct device *dev,			\
			struct device_attribute *attr, const char *buf,	\
			size_t count)					\
{									\
	struct devfreq *df = to_devfreq(dev);				\
	struct hwmon_node *hw = df->data;				\
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
		hw->name[i] = val;					\
	}								\
	ret = count;							\
out:									\
	argv_free(strlist);						\
	hw->name[i] = 0;						\
	return ret;							\
}

#define MIN_MS	10U
#define MAX_MS	500U

#define SAMPLE_MIN_MS	1U
#define SAMPLE_MAX_MS	50U

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
	struct devfreq *df;
	struct hwmon_node *node;
	ktime_t ts;
	unsigned long bytes, mbps;
	unsigned int us;
	int wake = 0;

	df = hwmon->df;
	node = df->data;

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

	//trace_bw_hwmon_meas(dev_name(df->dev.parent),
	//			mbps,
	//			us,
	//			wake);

	return wake;
}

static int __bw_hwmon_hw_sample_end(struct bw_hwmon *hwmon)
{
	struct devfreq *df;
	struct hwmon_node *node;
	unsigned long bytes, mbps;
	int wake = 0;

	df = hwmon->df;
	node = df->data;

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

	//trace_bw_hwmon_meas(dev_name(df->dev.parent),
	//			mbps,
	//			node->sample_ms * USEC_PER_MSEC,
	//			wake);

	return 1;
}

static int __bw_hwmon_sample_end(struct bw_hwmon *hwmon)
{
	if (hwmon->set_hw_events)
		return __bw_hwmon_hw_sample_end(hwmon);
	else
		return __bw_hwmon_sw_sample_end(hwmon);
}

int bw_hwmon_sample_end(struct bw_hwmon *hwmon)
{
	unsigned long flags;
	int wake;

	spin_lock_irqsave(&irq_lock, flags);
	wake = __bw_hwmon_sample_end(hwmon);
	spin_unlock_irqrestore(&irq_lock, flags);

	return wake;
}
EXPORT_SYMBOL_GPL(bw_hwmon_sample_end);

static unsigned long to_mbps_zone(struct hwmon_node *node, unsigned long mbps)
{
	int i;

	for (i = 0; i < NUM_MBPS_ZONES && node->mbps_zones[i]; i++)
		if (node->mbps_zones[i] >= mbps)
			return node->mbps_zones[i];

	return node->hw->df->scaling_max_freq;
}

#define MIN_MBPS	500UL
#define HIST_PEAK_TOL	60
static unsigned long get_bw_and_set_irq(struct hwmon_node *node,
					unsigned long *freq, unsigned long *ab)
{
	unsigned long meas_mbps, thres, flags, req_mbps, adj_mbps;
	unsigned long meas_mbps_zone;
	unsigned long hist_lo_tol, hyst_lo_tol;
	struct bw_hwmon *hw = node->hw;
	unsigned int new_bw, io_percent = node->io_percent;
	ktime_t ts;
	unsigned int ms = 0;

	spin_lock_irqsave(&irq_lock, flags);

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
	}

	/*
	 * Check node->max_mbps to avoid double counting peaks that cause
	 * early termination of a window.
	 */
	if (meas_mbps >= hyst_lo_tol && meas_mbps > MIN_MBPS
	    && !node->max_mbps) {
		node->hyst_peak++;
		if (node->hyst_peak >= node->hyst_trigger_count
		    || node->hyst_en)
			node->hyst_en = node->hyst_length;
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
		if (meas_mbps > node->idle_mbps)
			req_mbps = max(req_mbps, node->hyst_mbps);
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

	spin_unlock_irqrestore(&irq_lock, flags);

	adj_mbps = req_mbps + node->guard_band_mbps;

	if (adj_mbps > node->prev_ab) {
		new_bw = adj_mbps;
	} else {
		new_bw = adj_mbps * node->decay_rate
			+ node->prev_ab * (100 - node->decay_rate);
		new_bw /= 100;
	}

	node->prev_ab = new_bw;
	if (ab && node->use_ab)
		*ab = roundup(new_bw, node->bw_step);
	else if (ab)
		*ab = 0;

	*freq = (new_bw * 100) / io_percent;
	//trace_bw_hwmon_update(dev_name(node->hw->df->dev.parent),
	//			new_bw,
	//			*freq,
	//			hw->up_wake_mbps,
	//			hw->down_wake_mbps);

	//trace_bw_hwmon_debug(dev_name(node->hw->df->dev.parent),
	//			req_mbps,
	//			meas_mbps_zone,
	//			node->hist_max_mbps,
	//			node->hist_mem,
	//			node->hyst_mbps,
	//			node->hyst_en);
	return req_mbps;
}

static struct hwmon_node *find_hwmon_node(struct devfreq *df)
{
	struct hwmon_node *node, *found = NULL;

	mutex_lock(&list_lock);
	list_for_each_entry(node, &hwmon_list, list)
		if (node->hw->dev == df->dev.parent ||
		    node->hw->of_node == df->dev.parent->of_node ||
		    (!node->hw->dev && !node->hw->of_node &&
		     node->gov == df->governor)) {
			found = node;
			break;
		}
	mutex_unlock(&list_lock);

	return found;
}

int update_bw_hwmon(struct bw_hwmon *hwmon)
{
	struct devfreq *df;
	struct hwmon_node *node;
	int ret;

	if (!hwmon)
		return -EINVAL;
	df = hwmon->df;
	if (!df)
		return -ENODEV;
	node = df->data;
	if (!node)
		return -ENODEV;

	mutex_lock(&node->mon_lock);
	if (!node->mon_started) {
		mutex_unlock(&node->mon_lock);
		return -EBUSY;
	}
	dev_dbg(df->dev.parent, "Got update request\n");
	devfreq_monitor_stop(df);

	mutex_lock(&df->lock);
	ret = update_devfreq(df);
	if (ret < 0)
		dev_err(df->dev.parent,
			"Unable to update freq on request! (%d)\n", ret);
	mutex_unlock(&df->lock);

	devfreq_monitor_start(df);
	mutex_unlock(&node->mon_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(update_bw_hwmon);

static int start_monitor(struct devfreq *df, bool init)
{
	struct hwmon_node *node = df->data;
	struct bw_hwmon *hw = node->hw;
	struct device *dev = df->dev.parent;
	unsigned long mbps;
	int ret;

	node->prev_ts = ktime_get();

	if (init) {
		node->prev_ab = 0;
		node->resume_freq = 0;
		node->resume_ab = 0;
		mbps = (df->previous_freq * node->io_percent) / 100;
		hw->up_wake_mbps = mbps;
		hw->down_wake_mbps = MIN_MBPS;
		ret = hw->start_hwmon(hw, mbps);
	} else {
		ret = hw->resume_hwmon(hw);
	}

	if (ret < 0) {
		dev_err(dev, "Unable to start HW monitor! (%d)\n", ret);
		return ret;
	}

	if (init)
		devfreq_monitor_start(df);
	else
		devfreq_monitor_resume(df);

	node->mon_started = true;

	return 0;
}

static void stop_monitor(struct devfreq *df, bool init)
{
	struct hwmon_node *node = df->data;
	struct bw_hwmon *hw = node->hw;

	mutex_lock(&node->mon_lock);
	node->mon_started = false;
	mutex_unlock(&node->mon_lock);

	if (init) {
		devfreq_monitor_stop(df);
		hw->stop_hwmon(hw);
	} else {
		devfreq_monitor_suspend(df);
		hw->suspend_hwmon(hw);
	}

}

static int gov_start(struct devfreq *df)
{
	int ret = 0;
	struct device *dev = df->dev.parent;
	struct hwmon_node *node;
	struct bw_hwmon *hw;
	struct devfreq_dev_status stat;

	node = find_hwmon_node(df);
	if (!node) {
		dev_err(dev, "Unable to find HW monitor!\n");
		return -ENODEV;
	}
	hw = node->hw;

	stat.private_data = NULL;
	if (df->profile->get_dev_status)
		ret = df->profile->get_dev_status(df->dev.parent, &stat);
	if (ret < 0 || !stat.private_data)
		dev_warn(dev, "Device doesn't take AB votes!\n");
	else
		node->dev_ab = stat.private_data;

	hw->df = df;
	node->orig_data = df->data;
	df->data = node;

	ret = start_monitor(df, true);
	if (ret < 0)
		goto err_start;

	ret = sysfs_create_group(&df->dev.kobj, node->attr_grp);
	if (ret < 0) {
		dev_err(dev, "Error creating sys entries: %d\n", ret);
		goto err_sysfs;
	}

	mutex_lock(&df->lock);
	df->scaling_min_freq = df->scaling_max_freq;
	update_devfreq(df);
	mutex_unlock(&df->lock);

	return 0;

err_sysfs:
	stop_monitor(df, true);
err_start:
	df->data = node->orig_data;
	node->orig_data = NULL;
	hw->df = NULL;
	node->dev_ab = NULL;
	return ret;
}

static void gov_stop(struct devfreq *df)
{
	struct hwmon_node *node = df->data;
	struct bw_hwmon *hw = node->hw;

	sysfs_remove_group(&df->dev.kobj, node->attr_grp);
	stop_monitor(df, true);
	df->data = node->orig_data;
	node->orig_data = NULL;
	hw->df = NULL;
	/*
	 * Not all governors know about this additional extended device
	 * configuration. To avoid leaving the extended configuration at a
	 * stale state, set it to 0 and let the next governor take it from
	 * there.
	 */
	if (node->dev_ab)
		*node->dev_ab = 0;
	node->dev_ab = NULL;
}

static int gov_suspend(struct devfreq *df)
{
	struct hwmon_node *node = df->data;
	unsigned long resume_freq = df->previous_freq;
	unsigned long resume_ab = *node->dev_ab;

	if (!node->hw->suspend_hwmon)
		return -EPERM;

	if (node->resume_freq) {
		dev_warn(df->dev.parent, "Governor already suspended!\n");
		return -EBUSY;
	}

	stop_monitor(df, false);

	mutex_lock(&df->lock);
	update_devfreq(df);
	mutex_unlock(&df->lock);

	node->resume_freq = resume_freq;
	node->resume_ab = resume_ab;

	return 0;
}

static int gov_resume(struct devfreq *df)
{
	struct hwmon_node *node = df->data;

	if (!node->hw->resume_hwmon)
		return -EPERM;

	mutex_lock(&df->lock);
	update_devfreq(df);
	mutex_unlock(&df->lock);

	node->resume_freq = 0;
	node->resume_ab = 0;

	return start_monitor(df, false);
}

static int devfreq_bw_hwmon_get_freq(struct devfreq *df,
					unsigned long *freq)
{
	struct hwmon_node *node = df->data;

	/* Suspend/resume sequence */
	if (!node->mon_started) {
		*freq = node->resume_freq;
		*node->dev_ab = node->resume_ab;
		return 0;
	}

	get_bw_and_set_irq(node, freq, node->dev_ab);

	return 0;
}

static ssize_t throttle_adj_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct hwmon_node *node = df->data;
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

static ssize_t throttle_adj_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct hwmon_node *node = df->data;
	unsigned int val;

	if (!node->hw->get_throttle_adj)
		val = 0;
	else
		val = node->hw->get_throttle_adj(node->hw);

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static DEVICE_ATTR_RW(throttle_adj);

static ssize_t sample_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct devfreq *df = to_devfreq(dev);
	struct hwmon_node *hw = df->data;
	int ret;
	unsigned int val;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	val = max(val, SAMPLE_MIN_MS);
	val = min(val, SAMPLE_MAX_MS);
	if (val > df->profile->polling_ms)
		return -EINVAL;

	hw->sample_ms = val;
	return count;
}

static ssize_t sample_ms_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct devfreq *df = to_devfreq(dev);
	struct hwmon_node *node = df->data;

	return scnprintf(buf, PAGE_SIZE, "%u\n", node->sample_ms);
}

static DEVICE_ATTR_RW(sample_ms);

show_attr(guard_band_mbps);
store_attr(guard_band_mbps, 0U, 2000U);
static DEVICE_ATTR_RW(guard_band_mbps);
show_attr(decay_rate);
store_attr(decay_rate, 0U, 100U);
static DEVICE_ATTR_RW(decay_rate);
show_attr(io_percent);
store_attr(io_percent, 1U, 400U);
static DEVICE_ATTR_RW(io_percent);
show_attr(bw_step);
store_attr(bw_step, 50U, 1000U);
static DEVICE_ATTR_RW(bw_step);
show_attr(up_scale);
store_attr(up_scale, 0U, 500U);
static DEVICE_ATTR_RW(up_scale);
show_attr(up_thres);
store_attr(up_thres, 1U, 100U);
static DEVICE_ATTR_RW(up_thres);
show_attr(down_thres);
store_attr(down_thres, 0U, 90U);
static DEVICE_ATTR_RW(down_thres);
show_attr(down_count);
store_attr(down_count, 0U, 90U);
static DEVICE_ATTR_RW(down_count);
show_attr(hist_memory);
store_attr(hist_memory, 0U, 90U);
static DEVICE_ATTR_RW(hist_memory);
show_attr(hyst_trigger_count);
store_attr(hyst_trigger_count, 0U, 90U);
static DEVICE_ATTR_RW(hyst_trigger_count);
show_attr(hyst_length);
store_attr(hyst_length, 0U, 90U);
static DEVICE_ATTR_RW(hyst_length);
show_attr(idle_mbps);
store_attr(idle_mbps, 0U, 2000U);
static DEVICE_ATTR_RW(idle_mbps);
show_attr(use_ab);
store_attr(use_ab, 0U, 1U);
static DEVICE_ATTR_RW(use_ab);
show_list_attr(mbps_zones, NUM_MBPS_ZONES);
store_list_attr(mbps_zones, NUM_MBPS_ZONES, 0U, UINT_MAX);
static DEVICE_ATTR_RW(mbps_zones);

static struct attribute *dev_attr[] = {
	&dev_attr_guard_band_mbps.attr,
	&dev_attr_decay_rate.attr,
	&dev_attr_io_percent.attr,
	&dev_attr_bw_step.attr,
	&dev_attr_sample_ms.attr,
	&dev_attr_up_scale.attr,
	&dev_attr_up_thres.attr,
	&dev_attr_down_thres.attr,
	&dev_attr_down_count.attr,
	&dev_attr_hist_memory.attr,
	&dev_attr_hyst_trigger_count.attr,
	&dev_attr_hyst_length.attr,
	&dev_attr_idle_mbps.attr,
	&dev_attr_use_ab.attr,
	&dev_attr_mbps_zones.attr,
	&dev_attr_throttle_adj.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.name = "bw_hwmon",
	.attrs = dev_attr,
};

static int devfreq_bw_hwmon_ev_handler(struct devfreq *df,
					unsigned int event, void *data)
{
	int ret = 0;
	unsigned int sample_ms;
	struct hwmon_node *node;
	struct bw_hwmon *hw;

	mutex_lock(&event_handle_lock);

	switch (event) {
	case DEVFREQ_GOV_START:
		sample_ms = df->profile->polling_ms;
		sample_ms = max(MIN_MS, sample_ms);
		sample_ms = min(MAX_MS, sample_ms);
		df->profile->polling_ms = sample_ms;

		ret = gov_start(df);
		if (ret < 0)
			goto out;

		dev_dbg(df->dev.parent,
			"Enabled dev BW HW monitor governor\n");
		break;
	case DEVFREQ_GOV_STOP:
		gov_stop(df);
		dev_dbg(df->dev.parent,
			"Disabled dev BW HW monitor governor\n");
		break;
	case DEVFREQ_GOV_UPDATE_INTERVAL:
		node = df->data;
		sample_ms = *(unsigned int *)data;
		if (sample_ms < node->sample_ms) {
			ret = -EINVAL;
			goto out;
		}

		sample_ms = max(MIN_MS, sample_ms);
		sample_ms = min(MAX_MS, sample_ms);
		/*
		 * Suspend/resume the HW monitor around the interval update
		 * to prevent the HW monitor IRQ from trying to change
		 * stop/start the delayed workqueue while the interval update
		 * is happening.
		 */
		hw = node->hw;
		hw->suspend_hwmon(hw);
		devfreq_update_interval(df, &sample_ms);
		ret = hw->resume_hwmon(hw);
		if (ret < 0) {
			dev_err(df->dev.parent,
				"Unable to resume HW monitor (%d)\n", ret);
			goto out;
		}
		break;

	case DEVFREQ_GOV_SUSPEND:
		ret = gov_suspend(df);
		if (ret < 0) {
			dev_err(df->dev.parent,
				"Unable to suspend BW HW mon governor (%d)\n",
				ret);
			goto out;
		}

		dev_dbg(df->dev.parent, "Suspended BW HW mon governor\n");
		break;

	case DEVFREQ_GOV_RESUME:
		ret = gov_resume(df);
		if (ret < 0) {
			dev_err(df->dev.parent,
				"Unable to resume BW HW mon governor (%d)\n",
				ret);
			goto out;
		}

		dev_dbg(df->dev.parent, "Resumed BW HW mon governor\n");
		break;
	}

out:
	mutex_unlock(&event_handle_lock);

	return ret;
}

static struct devfreq_governor devfreq_gov_bw_hwmon = {
	.name = "bw_hwmon",
	.flags = 1,
	.get_target_freq = devfreq_bw_hwmon_get_freq,
	.event_handler = devfreq_bw_hwmon_ev_handler,
};

int register_bw_hwmon(struct device *dev, struct bw_hwmon *hwmon)
{
	int ret = 0;
	struct hwmon_node *node;
	struct attribute_group *attr_grp;

	if (!hwmon->gov && !hwmon->dev && !hwmon->of_node)
		return -EINVAL;

	node = devm_kzalloc(dev, sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	if (hwmon->gov) {
		attr_grp = devm_kzalloc(dev, sizeof(*attr_grp), GFP_KERNEL);
		if (!attr_grp)
			return -ENOMEM;

		hwmon->gov->get_target_freq = devfreq_bw_hwmon_get_freq;
		hwmon->gov->event_handler = devfreq_bw_hwmon_ev_handler;
		attr_grp->name = hwmon->gov->name;
		attr_grp->attrs = dev_attr;

		node->gov = hwmon->gov;
		node->attr_grp = attr_grp;
	} else {
		node->gov = &devfreq_gov_bw_hwmon;
		node->attr_grp = &dev_attr_group;
	}

	node->guard_band_mbps = 100;
	node->decay_rate = 90;
	node->io_percent = 16;
	node->bw_step = 190;
	node->sample_ms = 50;
	node->up_scale = 0;
	node->up_thres = 10;
	node->down_thres = 0;
	node->down_count = 3;
	node->hist_memory = 0;
	node->hyst_trigger_count = 3;
	node->hyst_length = 0;
	node->idle_mbps = 400;
	node->use_ab = 1;
	node->mbps_zones[0] = 0;
	node->hw = hwmon;

	mutex_init(&node->mon_lock);
	mutex_lock(&list_lock);
	list_add_tail(&node->list, &hwmon_list);
	mutex_unlock(&list_lock);

	if (hwmon->gov) {
		ret = devfreq_add_governor(hwmon->gov);
		dev_err(dev, "BW HWmon governor name %s\n", hwmon->gov->name);
	} else {
		mutex_lock(&state_lock);
		if (!use_cnt)
			ret = devfreq_add_governor(&devfreq_gov_bw_hwmon);
		if (!ret)
			use_cnt++;
		mutex_unlock(&state_lock);
		dev_err(dev, "BW HWmon governor name %s\n", devfreq_gov_bw_hwmon.name);
	}

	if (!ret)
		dev_err(dev, "BW HWmon governor registered.\n");
	else
		dev_err(dev, "BW HWmon governor registration failed!\n");

	return ret;
}
EXPORT_SYMBOL_GPL(register_bw_hwmon);

MODULE_DESCRIPTION("HW monitor based dev DDR bandwidth voting driver");
MODULE_LICENSE("GPL");
