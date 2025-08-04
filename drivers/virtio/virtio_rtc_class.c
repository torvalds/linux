// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * virtio_rtc RTC class driver
 *
 * Copyright (C) 2023 OpenSynergy GmbH
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/math64.h>
#include <linux/overflow.h>
#include <linux/rtc.h>
#include <linux/time64.h>

#include <uapi/linux/virtio_rtc.h>

#include "virtio_rtc_internal.h"

/**
 * struct viortc_class - RTC class wrapper
 * @viortc: virtio_rtc device data
 * @rtc: RTC device
 * @vio_clk_id: virtio_rtc clock id
 * @stopped: Whether RTC ops are disallowed. Access protected by rtc_lock().
 */
struct viortc_class {
	struct viortc_dev *viortc;
	struct rtc_device *rtc;
	u16 vio_clk_id;
	bool stopped;
};

/**
 * viortc_class_get_locked() - get RTC class wrapper, if ops allowed
 * @dev: virtio device
 *
 * Gets the RTC class wrapper from the virtio device, if it is available and
 * ops are allowed.
 *
 * Context: Caller must hold rtc_lock().
 * Return: RTC class wrapper if available and ops allowed, ERR_PTR otherwise.
 */
static struct viortc_class *viortc_class_get_locked(struct device *dev)
{
	struct viortc_class *viortc_class;

	viortc_class = viortc_class_from_dev(dev);
	if (IS_ERR(viortc_class))
		return viortc_class;

	if (viortc_class->stopped)
		return ERR_PTR(-EBUSY);

	return viortc_class;
}

/**
 * viortc_class_read_time() - RTC class op read_time
 * @dev: virtio device
 * @tm: read time
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_class_read_time(struct device *dev, struct rtc_time *tm)
{
	struct viortc_class *viortc_class;
	time64_t sec;
	int ret;
	u64 ns;

	viortc_class = viortc_class_get_locked(dev);
	if (IS_ERR(viortc_class))
		return PTR_ERR(viortc_class);

	ret = viortc_read(viortc_class->viortc, viortc_class->vio_clk_id, &ns);
	if (ret)
		return ret;

	sec = div_u64(ns, NSEC_PER_SEC);

	rtc_time64_to_tm(sec, tm);

	return 0;
}

/**
 * viortc_class_read_alarm() - RTC class op read_alarm
 * @dev: virtio device
 * @alrm: alarm read out
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_class_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct viortc_class *viortc_class;
	time64_t alarm_time_sec;
	u64 alarm_time_ns;
	bool enabled;
	int ret;

	viortc_class = viortc_class_get_locked(dev);
	if (IS_ERR(viortc_class))
		return PTR_ERR(viortc_class);

	ret = viortc_read_alarm(viortc_class->viortc, viortc_class->vio_clk_id,
				&alarm_time_ns, &enabled);
	if (ret)
		return ret;

	alarm_time_sec = div_u64(alarm_time_ns, NSEC_PER_SEC);
	rtc_time64_to_tm(alarm_time_sec, &alrm->time);

	alrm->enabled = enabled;

	return 0;
}

/**
 * viortc_class_set_alarm() - RTC class op set_alarm
 * @dev: virtio device
 * @alrm: alarm to set
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_class_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct viortc_class *viortc_class;
	time64_t alarm_time_sec;
	u64 alarm_time_ns;

	viortc_class = viortc_class_get_locked(dev);
	if (IS_ERR(viortc_class))
		return PTR_ERR(viortc_class);

	alarm_time_sec = rtc_tm_to_time64(&alrm->time);

	if (alarm_time_sec < 0)
		return -EINVAL;

	if (check_mul_overflow((u64)alarm_time_sec, (u64)NSEC_PER_SEC,
			       &alarm_time_ns))
		return -EINVAL;

	return viortc_set_alarm(viortc_class->viortc, viortc_class->vio_clk_id,
				alarm_time_ns, alrm->enabled);
}

/**
 * viortc_class_alarm_irq_enable() - RTC class op alarm_irq_enable
 * @dev: virtio device
 * @enabled: enable or disable alarm IRQ
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
static int viortc_class_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct viortc_class *viortc_class;

	viortc_class = viortc_class_get_locked(dev);
	if (IS_ERR(viortc_class))
		return PTR_ERR(viortc_class);

	return viortc_set_alarm_enabled(viortc_class->viortc,
					viortc_class->vio_clk_id, enabled);
}

static const struct rtc_class_ops viortc_class_ops = {
	.read_time = viortc_class_read_time,
	.read_alarm = viortc_class_read_alarm,
	.set_alarm = viortc_class_set_alarm,
	.alarm_irq_enable = viortc_class_alarm_irq_enable,
};

/**
 * viortc_class_alarm() - propagate alarm notification as alarm interrupt
 * @viortc_class: RTC class wrapper
 * @vio_clk_id: virtio_rtc clock id
 *
 * Context: Any context.
 */
void viortc_class_alarm(struct viortc_class *viortc_class, u16 vio_clk_id)
{
	if (vio_clk_id != viortc_class->vio_clk_id) {
		dev_warn_ratelimited(&viortc_class->rtc->dev,
				     "ignoring alarm for clock id %d, expected id %d\n",
				     vio_clk_id, viortc_class->vio_clk_id);
		return;
	}

	rtc_update_irq(viortc_class->rtc, 1, RTC_AF | RTC_IRQF);
}

/**
 * viortc_class_stop() - disallow RTC class ops
 * @viortc_class: RTC class wrapper
 *
 * Context: Process context. Caller must NOT hold rtc_lock().
 */
void viortc_class_stop(struct viortc_class *viortc_class)
{
	rtc_lock(viortc_class->rtc);

	viortc_class->stopped = true;

	rtc_unlock(viortc_class->rtc);
}

/**
 * viortc_class_register() - register RTC class device
 * @viortc_class: RTC class wrapper
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
int viortc_class_register(struct viortc_class *viortc_class)
{
	return devm_rtc_register_device(viortc_class->rtc);
}

/**
 * viortc_class_init() - init RTC class wrapper and device
 * @viortc: device data
 * @vio_clk_id: virtio_rtc clock id
 * @have_alarm: have alarm feature
 * @parent_dev: virtio device
 *
 * Context: Process context.
 * Return: RTC class wrapper on success, ERR_PTR otherwise.
 */
struct viortc_class *viortc_class_init(struct viortc_dev *viortc,
				       u16 vio_clk_id, bool have_alarm,
				       struct device *parent_dev)
{
	struct viortc_class *viortc_class;
	struct rtc_device *rtc;

	viortc_class =
		devm_kzalloc(parent_dev, sizeof(*viortc_class), GFP_KERNEL);
	if (!viortc_class)
		return ERR_PTR(-ENOMEM);

	rtc = devm_rtc_allocate_device(parent_dev);
	if (IS_ERR(rtc))
		return ERR_CAST(rtc);

	viortc_class->viortc = viortc;
	viortc_class->rtc = rtc;
	viortc_class->vio_clk_id = vio_clk_id;

	if (!have_alarm)
		clear_bit(RTC_FEATURE_ALARM, rtc->features);
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->features);

	rtc->ops = &viortc_class_ops;
	rtc->range_max = div_u64(U64_MAX, NSEC_PER_SEC);

	return viortc_class;
}
