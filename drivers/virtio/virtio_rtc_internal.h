/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * virtio_rtc internal interfaces
 *
 * Copyright (C) 2022-2023 OpenSynergy GmbH
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _VIRTIO_RTC_INTERNAL_H_
#define _VIRTIO_RTC_INTERNAL_H_

#include <linux/device.h>
#include <linux/err.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/types.h>

/* driver core IFs */

struct viortc_dev;

int viortc_read(struct viortc_dev *viortc, u16 vio_clk_id, u64 *reading);
int viortc_read_cross(struct viortc_dev *viortc, u16 vio_clk_id, u8 hw_counter,
		      u64 *reading, u64 *cycles);
int viortc_cross_cap(struct viortc_dev *viortc, u16 vio_clk_id, u8 hw_counter,
		     bool *supported);
int viortc_read_alarm(struct viortc_dev *viortc, u16 vio_clk_id,
		      u64 *alarm_time, bool *enabled);
int viortc_set_alarm(struct viortc_dev *viortc, u16 vio_clk_id, u64 alarm_time,
		     bool alarm_enable);
int viortc_set_alarm_enabled(struct viortc_dev *viortc, u16 vio_clk_id,
			     bool alarm_enable);

struct viortc_class;

struct viortc_class *viortc_class_from_dev(struct device *dev);

/* PTP IFs */

struct viortc_ptp_clock;

#if IS_ENABLED(CONFIG_VIRTIO_RTC_PTP)

struct viortc_ptp_clock *viortc_ptp_register(struct viortc_dev *viortc,
					     struct device *parent_dev,
					     u16 vio_clk_id,
					     const char *ptp_clock_name);
int viortc_ptp_unregister(struct viortc_ptp_clock *vio_ptp,
			  struct device *parent_dev);

#else

static inline struct viortc_ptp_clock *
viortc_ptp_register(struct viortc_dev *viortc, struct device *parent_dev,
		    u16 vio_clk_id, const char *ptp_clock_name)
{
	return NULL;
}

static inline int viortc_ptp_unregister(struct viortc_ptp_clock *vio_ptp,
					struct device *parent_dev)
{
	return -ENODEV;
}

#endif

/* HW counter IFs */

/**
 * viortc_hw_xtstamp_params() - get HW-specific xtstamp params
 * @hw_counter: virtio_rtc HW counter type
 * @cs_id: clocksource id corresponding to hw_counter
 *
 * Gets the HW-specific xtstamp params. Returns an error if the driver cannot
 * support xtstamp.
 *
 * Context: Process context.
 * Return: Zero on success, negative error code otherwise.
 */
int viortc_hw_xtstamp_params(u8 *hw_counter, enum clocksource_ids *cs_id);

/* RTC class IFs */

#if IS_ENABLED(CONFIG_VIRTIO_RTC_CLASS)

void viortc_class_alarm(struct viortc_class *viortc_class, u16 vio_clk_id);

void viortc_class_stop(struct viortc_class *viortc_class);

int viortc_class_register(struct viortc_class *viortc_class);

struct viortc_class *viortc_class_init(struct viortc_dev *viortc,
				       u16 vio_clk_id, bool have_alarm,
				       struct device *parent_dev);

#else /* CONFIG_VIRTIO_RTC_CLASS */

static inline void viortc_class_alarm(struct viortc_class *viortc_class,
				      u16 vio_clk_id)
{
}

static inline void viortc_class_stop(struct viortc_class *viortc_class)
{
}

static inline int viortc_class_register(struct viortc_class *viortc_class)
{
	return -ENODEV;
}

static inline struct viortc_class *viortc_class_init(struct viortc_dev *viortc,
						     u16 vio_clk_id,
						     bool have_alarm,
						     struct device *parent_dev)
{
	return ERR_PTR(-ENODEV);
}

#endif /* CONFIG_VIRTIO_RTC_CLASS */

#endif /* _VIRTIO_RTC_INTERNAL_H_ */
