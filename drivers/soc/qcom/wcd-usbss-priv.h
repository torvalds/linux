/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef WCD_USBSS_PRIV_H
#define WCD_USBSS_PRIV_H

#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sched.h>
#include <linux/soc/qcom/wcd939x-i2c.h>
#include "wcd-usbss-registers.h"

#define WCD_USBSS_SUPPLY_MAX 4

struct wcd_usbss_ctxt {
	struct regmap *regmap;
	struct device *dev;
	struct i2c_client *client;
	struct notifier_block ucsi_nb;
	atomic_t usbc_mode;
	struct work_struct usbc_analog_work;
	struct blocking_notifier_head wcd_usbss_notifier;
	struct mutex notification_lock;
	struct regulator_bulk_data supplies[WCD_USBSS_SUPPLY_MAX];
	struct pinctrl *rst_pins;
	struct pinctrl_state *rst_pins_active;
	struct pinctrl_state *rst_pins_sleep;
	u8 prev_pg;
	bool prev_pg_valid;
	struct mutex io_lock;
	unsigned int cable_status;
	bool surge_enable;
	struct kobject *surge_kobject;
	struct task_struct *surge_thread;
	unsigned int surge_timer_period_ms;
	unsigned int cached_audio_pwr_mode;
	bool standby_enable;
	bool is_in_standby;
	struct mutex switch_update_lock;
	struct mutex runtime_env_counter_lock;
	unsigned int version;
	int wcd_standby_status;
	int runtime_env_counter;
	struct nvmem_cell *nvmem_cell;
	bool suspended;
	bool defer_writes;
	int req_state;
};

extern struct regmap *wcd_usbss_regmap_init(struct device *dev,
				   const struct regmap_config *config);
extern struct regmap_config wcd_usbss_regmap_config;
extern const u8 wcd_usbss_reg_access[WCD_USBSS_NUM_REGISTERS];
#endif /* WCD_USBSS_PRIV_H */
