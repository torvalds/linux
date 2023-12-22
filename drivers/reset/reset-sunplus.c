// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * SP7021 reset driver
 *
 * Copyright (C) Sunplus Technology Co., Ltd.
 *       All rights reserved.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/reboot.h>

/* HIWORD_MASK_REG BITS */
#define BITS_PER_HWM_REG	16

/* resets HW info: reg_index_shift */
static const u32 sp_resets[] = {
/* SP7021: mo_reset0 ~ mo_reset9 */
	0x00,
	0x02,
	0x03,
	0x04,
	0x05,
	0x06,
	0x07,
	0x08,
	0x09,
	0x0a,
	0x0b,
	0x0d,
	0x0e,
	0x0f,
	0x10,
	0x12,
	0x14,
	0x15,
	0x16,
	0x17,
	0x18,
	0x19,
	0x1a,
	0x1b,
	0x1c,
	0x1d,
	0x1e,
	0x1f,
	0x20,
	0x21,
	0x22,
	0x23,
	0x24,
	0x25,
	0x26,
	0x2a,
	0x2b,
	0x2d,
	0x2e,
	0x30,
	0x31,
	0x32,
	0x33,
	0x3d,
	0x3e,
	0x3f,
	0x42,
	0x44,
	0x4b,
	0x4c,
	0x4d,
	0x4e,
	0x4f,
	0x50,
	0x55,
	0x60,
	0x61,
	0x6a,
	0x6f,
	0x70,
	0x73,
	0x74,
	0x86,
	0x8a,
	0x8b,
	0x8d,
	0x8e,
	0x8f,
	0x90,
	0x92,
	0x93,
	0x94,
	0x95,
	0x96,
	0x97,
	0x98,
	0x99,
};

struct sp_reset {
	struct reset_controller_dev rcdev;
	struct notifier_block notifier;
	void __iomem *base;
};

static inline struct sp_reset *to_sp_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct sp_reset, rcdev);
}

static int sp_reset_update(struct reset_controller_dev *rcdev,
			   unsigned long id, bool assert)
{
	struct sp_reset *reset = to_sp_reset(rcdev);
	int index = sp_resets[id] / BITS_PER_HWM_REG;
	int shift = sp_resets[id] % BITS_PER_HWM_REG;
	u32 val;

	val = (1 << (16 + shift)) | (assert << shift);
	writel(val, reset->base + (index * 4));

	return 0;
}

static int sp_reset_assert(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	return sp_reset_update(rcdev, id, true);
}

static int sp_reset_deassert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	return sp_reset_update(rcdev, id, false);
}

static int sp_reset_status(struct reset_controller_dev *rcdev,
			   unsigned long id)
{
	struct sp_reset *reset = to_sp_reset(rcdev);
	int index = sp_resets[id] / BITS_PER_HWM_REG;
	int shift = sp_resets[id] % BITS_PER_HWM_REG;
	u32 reg;

	reg = readl(reset->base + (index * 4));

	return !!(reg & BIT(shift));
}

static const struct reset_control_ops sp_reset_ops = {
	.assert   = sp_reset_assert,
	.deassert = sp_reset_deassert,
	.status   = sp_reset_status,
};

static int sp_restart(struct notifier_block *nb, unsigned long mode,
		      void *cmd)
{
	struct sp_reset *reset = container_of(nb, struct sp_reset, notifier);

	sp_reset_assert(&reset->rcdev, 0);
	sp_reset_deassert(&reset->rcdev, 0);

	return NOTIFY_DONE;
}

static int sp_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sp_reset *reset;
	struct resource *res;
	int ret;

	reset = devm_kzalloc(dev, sizeof(*reset), GFP_KERNEL);
	if (!reset)
		return -ENOMEM;

	reset->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(reset->base))
		return PTR_ERR(reset->base);

	reset->rcdev.ops = &sp_reset_ops;
	reset->rcdev.owner = THIS_MODULE;
	reset->rcdev.of_node = dev->of_node;
	reset->rcdev.nr_resets = resource_size(res) / 4 * BITS_PER_HWM_REG;

	ret = devm_reset_controller_register(dev, &reset->rcdev);
	if (ret)
		return ret;

	reset->notifier.notifier_call = sp_restart;
	reset->notifier.priority = 192;

	return register_restart_handler(&reset->notifier);
}

static const struct of_device_id sp_reset_dt_ids[] = {
	{.compatible = "sunplus,sp7021-reset",},
	{ /* sentinel */ },
};

static struct platform_driver sp_reset_driver = {
	.probe = sp_reset_probe,
	.driver = {
		.name			= "sunplus-reset",
		.of_match_table		= sp_reset_dt_ids,
		.suppress_bind_attrs	= true,
	},
};
builtin_platform_driver(sp_reset_driver);
