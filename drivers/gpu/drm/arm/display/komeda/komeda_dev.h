/* SPDX-License-Identifier: GPL-2.0 */
/*
 * (C) COPYRIGHT 2018 ARM Limited. All rights reserved.
 * Author: James.Qian.Wang <james.qian.wang@arm.com>
 *
 */
#ifndef _KOMEDA_DEV_H_
#define _KOMEDA_DEV_H_

#include <linux/device.h>
#include <linux/clk.h>
#include "komeda_pipeline.h"
#include "malidp_product.h"
#include "komeda_format_caps.h"

/* malidp device id */
enum {
	MALI_D71 = 0,
};

/* pipeline DT ports */
enum {
	KOMEDA_OF_PORT_OUTPUT		= 0,
	KOMEDA_OF_PORT_COPROC		= 1,
};

struct komeda_chip_info {
	u32 arch_id;
	u32 core_id;
	u32 core_info;
	u32 bus_width;
};

struct komeda_product_data {
	u32 product_id;
	struct komeda_dev_funcs *(*identify)(u32 __iomem *reg,
					     struct komeda_chip_info *info);
};

struct komeda_dev;

/**
 * struct komeda_dev_funcs
 *
 * Supplied by chip level and returned by the chip entry function xxx_identify,
 */
struct komeda_dev_funcs {
	/**
	 * @init_format_table:
	 *
	 * initialize &komeda_dev->format_table, this function should be called
	 * before the &enum_resource
	 */
	void (*init_format_table)(struct komeda_dev *mdev);
	/**
	 * @enum_resources:
	 *
	 * for CHIP to report or add pipeline and component resources to CORE
	 */
	int (*enum_resources)(struct komeda_dev *mdev);
	/** @cleanup: call to chip to cleanup komeda_dev->chip data */
	void (*cleanup)(struct komeda_dev *mdev);
};

/**
 * struct komeda_dev
 *
 * Pipeline and component are used to describe how to handle the pixel data.
 * komeda_device is for describing the whole view of the device, and the
 * control-abilites of device.
 */
struct komeda_dev {
	struct device *dev;
	u32 __iomem   *reg_base;

	struct komeda_chip_info chip;
	/** @fmt_tbl: initialized by &komeda_dev_funcs->init_format_table */
	struct komeda_format_caps_table fmt_tbl;
	/** @pclk: APB clock for register access */
	struct clk *pclk;
	/** @mck: HW main engine clk */
	struct clk *mclk;

	int n_pipelines;
	struct komeda_pipeline *pipelines[KOMEDA_MAX_PIPELINES];

	/** @funcs: chip funcs to access to HW */
	struct komeda_dev_funcs *funcs;
	/**
	 * @chip_data:
	 *
	 * chip data will be added by &komeda_dev_funcs.enum_resources() and
	 * destroyed by &komeda_dev_funcs.cleanup()
	 */
	void *chip_data;
};

static inline bool
komeda_product_match(struct komeda_dev *mdev, u32 target)
{
	return MALIDP_CORE_ID_PRODUCT_ID(mdev->chip.core_id) == target;
}

struct komeda_dev_funcs *
d71_identify(u32 __iomem *reg, struct komeda_chip_info *chip);

struct komeda_dev *komeda_dev_create(struct device *dev);
void komeda_dev_destroy(struct komeda_dev *mdev);

#endif /*_KOMEDA_DEV_H_*/
