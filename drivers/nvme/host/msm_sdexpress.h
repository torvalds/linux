/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __MSM_SDEXPRESS_H
#define __MSM_SDEXPRESS_H

#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/errno.h>
#include <linux/msm_pcie.h>
#include <linux/kobject.h>

#define DRIVER_NAME "msm_sdexpress"
#define MAX_PROP_SIZE 32
#define SDEXPRESS_VREG_VDD1_DEFAULT_UV 2960000 /* uV */
#define SDEXPRESS_VREG_VDD2_DEFAULT_UV 1800000 /* uV */
#define SDEXPRESS_VREG_DEFAULT_MIN_LOAD_UA 0 /* uA */
#define SDEXPRESS_VREG_DEFAULT_MAX_LODA_UA 600000 /* uA */
#define SDEXPRESS_PROBE_DELAYED_PERIOD 50000 /* msec */
#define PCIE_ENUMERATE_RETRY 3 /* retries upon msm_pcie_enumerate() failure */

/*
 * This structure keeps information per regulator
 */
struct msm_sdexpress_reg_data {
	/* voltage regulator handle */
	struct regulator *reg;
	/* voltage level to be set */
	u32 low_vol_level;
	u32 high_vol_level;
	/* Load values for low power and high power mode */
	u32 lpm_uA;
	u32 hpm_uA;

	/* regulator name */
	const char *name;
	/* is this regulator enabled? */
	bool is_enabled;
	/* is this regulator needs to be always on? */
	bool is_always_on;
	/* is low power mode setting required for this regulator? */
	bool lpm_sup;
	bool set_voltage_sup;
};

/*
 * This structure keeps information for all the
 * regulators required for a SDX slot.
 */
struct msm_sdexpress_vreg_data {
	/* keeps VDD1 regulator info */
	struct msm_sdexpress_reg_data *vdd1_data;
	/* keeps VDD2 regulator info */
	struct msm_sdexpress_reg_data *vdd2_data;
};

/*
 * Structure to hold sdexpress gpio information.
 */
struct msm_sdexpress_gpio {
	/* gpio_desc */
	struct gpio_desc *gpio;
	/* gpio irq handler*/
	irqreturn_t (*cd_gpio_isr)(int irq, void *dev_id);
	/* debounce time value in ms */
	u16 cd_debounce_delay_ms;
	/* gpio label/name */
	char *label;
};

/*
 * This structure that defines sdexpress private data.
 */
struct msm_sdexpress_info {
	/* to know card enumeration status */
	bool card_enumerated;
	/* sdexpress card detect irq */
	int cd_irq;
	/* nvme endpoint instance id from pcie */
	int pci_nvme_instance;
	/* to check the card trigger event */
	atomic_t trigger_card_event;
	/* struct device from platform device */
	struct device *dev;
	/* workqueue struct for sdexpress work */
	struct workqueue_struct *sdexpress_wq;
	/* structure to hold sdexpress voltage reg information */
	struct msm_sdexpress_vreg_data *vreg_data;
	/* structure to hold sdexpress card detect gpio information */
	struct msm_sdexpress_gpio *sdexpress_gpio;
	/* structure to hold pcie clkreq gpio information */
	struct msm_sdexpress_gpio *sdexpress_clkreq_gpio;
	/* sdexpress work item */
	struct delayed_work sdex_work;
	/* struct kobject for uevents */
	struct kobject kobj;
	/*
	 * This lock must be acquired before doing the actual
	 * card enumerate/deenumerate functionality.
	 *
	 * This is required to ensure that there is no race during
	 * aggressive PIPO of sdexpress card on bootup and other
	 * use-cases.
	 */
	struct mutex detect_lock;
};

#endif /* MSM_SDEXPRESS_H */
