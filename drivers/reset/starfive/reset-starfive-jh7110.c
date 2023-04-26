// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 * Author: Samin Guo <samin.guo@starfivetech.com>
 */

#include <linux/bitmap.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/spinlock.h>

#include <dt-bindings/reset/starfive-jh7110.h>

/* register offsets */
#define AONCRG_RESET_ASSERT	0x38
#define ISPCRG_RESET_ASSERT	0x38
#define VOUTCRG_RESET_ASSERT	0x48
#define STGCRG_RESET_ASSERT	0x74
#define AONCRG_RESET_STATUS	0x3C
#define ISPCRG_RESET_STATUS	0x3C
#define VOUTCRG_RESET_STATUS	0x4C
#define STGCRG_RESET_STATUS	0x78

#define SYSCRG_RESET_ASSERT0	0x2F8
#define SYSCRG_RESET_ASSERT1	0x2FC
#define SYSCRG_RESET_ASSERT2	0x300
#define SYSCRG_RESET_ASSERT3	0x304
#define SYSCRG_RESET_STATUS0	0x308
#define SYSCRG_RESET_STATUS1	0x30C
#define SYSCRG_RESET_STATUS2	0x310
#define SYSCRG_RESET_STATUS3	0x314

struct reset_assert_t {
	void *__iomem reg_assert;
	void *__iomem reg_status;
};

enum JH7110_RESET_CRG_GROUP {
	SYSCRG_0 = 0,
	SYSCRG_1,
	SYSCRG_2,
	SYSCRG_3,
	STGCRG,
	AONCRG,
	ISPCRG,
	VOUTCRG,
};

struct jh7110_reset {
	struct reset_controller_dev rcdev;
	/* protect registers against concurrent read-modify-write */
	spinlock_t lock;
	void __iomem *syscrg;
	void __iomem *stgcrg;
	void __iomem *aoncrg;
	void __iomem *ispcrg;
	void __iomem *voutcrg;
};

static inline struct jh7110_reset *
jh7110_reset_from(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct jh7110_reset, rcdev);
}

static int jh7110_get_reset(struct jh7110_reset *data,
				struct reset_assert_t *reset,
				unsigned long group)
{
	switch (group) {
	case SYSCRG_0:
		reset->reg_assert = data->syscrg + SYSCRG_RESET_ASSERT0;
		reset->reg_status = data->syscrg + SYSCRG_RESET_STATUS0;
		break;
	case SYSCRG_1:
		reset->reg_assert = data->syscrg + SYSCRG_RESET_ASSERT1;
		reset->reg_status = data->syscrg + SYSCRG_RESET_STATUS1;
		break;
	case SYSCRG_2:
		reset->reg_assert = data->syscrg + SYSCRG_RESET_ASSERT2;
		reset->reg_status = data->syscrg + SYSCRG_RESET_STATUS2;
		break;
	case SYSCRG_3:
		reset->reg_assert = data->syscrg + SYSCRG_RESET_ASSERT3;
		reset->reg_status = data->syscrg + SYSCRG_RESET_STATUS3;
		break;
	case STGCRG:
		reset->reg_assert = data->stgcrg + STGCRG_RESET_ASSERT;
		reset->reg_status = data->stgcrg + STGCRG_RESET_STATUS;
		break;
	case AONCRG:
		reset->reg_assert = data->aoncrg + AONCRG_RESET_ASSERT;
		reset->reg_status = data->aoncrg + AONCRG_RESET_STATUS;
		break;
	case ISPCRG:
		reset->reg_assert = data->ispcrg + ISPCRG_RESET_ASSERT;
		reset->reg_status = data->ispcrg + ISPCRG_RESET_STATUS;
		break;
	case VOUTCRG:
		reset->reg_assert = data->voutcrg + VOUTCRG_RESET_ASSERT;
		reset->reg_status = data->voutcrg + VOUTCRG_RESET_STATUS;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int jh7110_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct jh7110_reset *data = jh7110_reset_from(rcdev);
	struct reset_assert_t reset;
	void __iomem *reg_assert, *reg_status;
	unsigned long group, flags;
	u32 mask, value, done = 0;
	int ret;

	group = id / 32;
	mask =  BIT(id % 32);
	jh7110_get_reset(data, &reset, group);
	reg_assert = reset.reg_assert;
	reg_status = reset.reg_status;

	if (!assert)
		done ^= mask;

	spin_lock_irqsave(&data->lock, flags);

	value = readl(reg_assert);
	if (assert)
		value |= mask;
	else
		value &= ~mask;
	writel(value, reg_assert);

	/* if the associated clock is gated, deasserting might otherwise hang forever */
	ret = readl_poll_timeout_atomic(reg_status, value, (value & mask) == done, 0, 1000);
	if (ret)
		dev_warn(rcdev->dev, "id:%ld group:%ld, mask:%#x assert:%#llx status:%#llx ret:%d\n",
				id, group, mask, (u64)reg_assert, (u64)reg_status, ret);

	spin_unlock_irqrestore(&data->lock, flags);
	return ret;
}

static int jh7110_reset_assert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	return jh7110_reset_update(rcdev, id, true);
}

static int jh7110_reset_deassert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return jh7110_reset_update(rcdev, id, false);
}

static int jh7110_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	int ret;

	ret = jh7110_reset_assert(rcdev, id);
	if (ret)
		return ret;

	return jh7110_reset_deassert(rcdev, id);
}

static int jh7110_reset_status(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct jh7110_reset *data = jh7110_reset_from(rcdev);
	struct reset_assert_t reset;
	unsigned long group;
	u32 mask, val;

	group = id / 32;
	mask =  BIT(id % 32);
	jh7110_get_reset(data, &reset, group);
	val = readl(reset.reg_status);

	return !(val & mask);
}

static const struct reset_control_ops jh7110_reset_ops = {
	.assert		= jh7110_reset_assert,
	.deassert	= jh7110_reset_deassert,
	.reset		= jh7110_reset_reset,
	.status		= jh7110_reset_status,
};

static void __iomem *platform_ioremap_iomem_byname(struct platform_device *pdev,
						const char *name)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	if (!res) {
		dev_err(&pdev->dev, "get %s io base fail.\n", name);
		return NULL;
	}

	return  ioremap(res->start, resource_size(res));
}

int __init reset_starfive_jh7110_generic_probe(struct platform_device *pdev,
						unsigned int nr_resets)
{
	struct jh7110_reset *data;
	struct device *dev = &pdev->dev;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev->driver_data = data;

	data->syscrg =  platform_ioremap_iomem_byname(pdev, "syscrg");
	if (IS_ERR(data->syscrg))
		return PTR_ERR(data->syscrg);

	data->stgcrg =  platform_ioremap_iomem_byname(pdev, "stgcrg");
	if (IS_ERR(data->stgcrg))
		return PTR_ERR(data->stgcrg);

	data->aoncrg =  platform_ioremap_iomem_byname(pdev, "aoncrg");
	if (IS_ERR(data->aoncrg))
		return PTR_ERR(data->aoncrg);

	data->ispcrg =  platform_ioremap_iomem_byname(pdev, "ispcrg");
	if (IS_ERR(data->ispcrg))
		return PTR_ERR(data->ispcrg);

	data->voutcrg =  platform_ioremap_iomem_byname(pdev, "voutcrg");
	if (IS_ERR(data->voutcrg))
		return PTR_ERR(data->voutcrg);

	data->rcdev.ops = &jh7110_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = nr_resets;
	data->rcdev.dev = &pdev->dev;
	data->rcdev.of_node = pdev->dev.of_node;
	spin_lock_init(&data->lock);

	return devm_reset_controller_register(dev, &data->rcdev);
}
EXPORT_SYMBOL_GPL(reset_starfive_jh7110_generic_probe);

static int __init jh7110_reset_probe(struct platform_device *pdev)
{
	return reset_starfive_jh7110_generic_probe(pdev, RSTN_JH7110_RESET_END);
}

static const struct of_device_id jh7110_reset_dt_ids[] = {
	{ .compatible = "starfive,jh7110-reset" },
	{ /* sentinel */ }
};

static struct platform_driver jh7110_reset_driver = {
	.driver = {
		.name = "jh7110-reset",
		.of_match_table = jh7110_reset_dt_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(jh7110_reset_driver, jh7110_reset_probe);
