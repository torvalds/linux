// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reset driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 samin <samin.guo@starfivetech.com>
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
#define SYSCRG_RESET_ASSERT0	0x2F8
#define SYSCRG_RESET_ASSERT1	0x2FC
#define SYSCRG_RESET_ASSERT2	0x300
#define SYSCRG_RESET_ASSERT3	0x304
#define SYSCRG_RESET_STATUS0	0x308
#define SYSCRG_RESET_STATUS1	0x30C
#define SYSCRG_RESET_STATUS2	0x310
#define SYSCRG_RESET_STATUS3	0x314

#define STGCRG_RESET_ASSERT0	0x74
#define STGCRG_RESET_STATUS0	0x78

#define AONCRG_RESET_ASSERT0	0x38
#define AONCRG_RESET_STATUS0	0x3C

struct reset_assert_t {
	void *__iomem reg_assert;
	void *__iomem reg_status;
};

enum JH7110_RESET_CRG_GROUP {
	SYSCRG_0 = 0,
	SYSCRG_1,
	SYSCRG_2,
	SYSCRG_3,
	STGCRG_0,
	AONCRG_0,
};

struct jh7110_reset {
	struct reset_assert_t reset_assert[6];
	struct reset_controller_dev rcdev;
	/* protect registers against concurrent read-modify-write */
	spinlock_t lock;
	void __iomem *syscrg;
	void __iomem *stgcrg;
	void __iomem *aoncrg;
	const u32 *asserted;
};

/*
 * Writing a 1 to the n'th bit of the m'th ASSERT register asserts
 * line 32m + n, and writing a 0 deasserts the same line.
 * Most reset lines have their status inverted so a 0 bit in the STATUS
 * register means the line is asserted and a 1 means it's deasserted. A few
 * lines don't though, so store the expected value of the status registers when
 * all lines are asserted.
 */
static const u32 jh7110_reset_asserted[6] = {
	/* SYSCRG_STATUS0 */
	BIT(RSTN_U0_U7MC_RST_BUS % 32) |
	BIT(RSTN_U0_U7MC_CORE0 % 32) |
	BIT(RSTN_U0_U7MC_CORE1 % 32) |
	BIT(RSTN_U0_U7MC_CORE2 % 32) |
	BIT(RSTN_U0_U7MC_CORE3 % 32) |
	BIT(RSTN_U0_U7MC_CORE4 % 32),
	/* SYSCRG_STATUS1 */
	0,
	/* SYSCRG_STATUS2 */
	0,
	/* SYSCRG_STATUS3 */
	0,
	/* STGCRG */
	BIT(RSTN_U0_HIFI4_CORE % 32) |
	BIT(RSTN_U0_E24_CORE % 32),
	/* AONCRG */
	0,
};

static inline struct jh7110_reset *
jh7110_reset_from(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct jh7110_reset, rcdev);
}

static void jh7110_devm_reset_set(struct device *dev)
{
	struct jh7110_reset *data = dev->driver_data;

	data->reset_assert[SYSCRG_0].reg_assert = data->syscrg + SYSCRG_RESET_ASSERT0;
	data->reset_assert[SYSCRG_0].reg_status = data->syscrg + SYSCRG_RESET_STATUS0;

	data->reset_assert[SYSCRG_1].reg_assert = data->syscrg + SYSCRG_RESET_ASSERT1;
	data->reset_assert[SYSCRG_1].reg_status = data->syscrg + SYSCRG_RESET_STATUS1;

	data->reset_assert[SYSCRG_2].reg_assert = data->syscrg + SYSCRG_RESET_ASSERT2;
	data->reset_assert[SYSCRG_2].reg_status = data->syscrg + SYSCRG_RESET_STATUS2;

	data->reset_assert[SYSCRG_3].reg_assert = data->syscrg + SYSCRG_RESET_ASSERT3;
	data->reset_assert[SYSCRG_3].reg_status = data->syscrg + SYSCRG_RESET_STATUS3;

	data->reset_assert[STGCRG_0].reg_assert = data->stgcrg + STGCRG_RESET_ASSERT0;
	data->reset_assert[STGCRG_0].reg_status = data->stgcrg + STGCRG_RESET_STATUS0;

	data->reset_assert[AONCRG_0].reg_assert = data->aoncrg + AONCRG_RESET_ASSERT0;
	data->reset_assert[AONCRG_0].reg_status = data->aoncrg + AONCRG_RESET_STATUS0;
}

static int jh7110_reset_update(struct reset_controller_dev *rcdev,
			       unsigned long id, bool assert)
{
	struct jh7110_reset *data = jh7110_reset_from(rcdev);
	void __iomem *reg_assert, *reg_status;
	unsigned long group, flags;
	u32 mask, value, done;
	int ret;

	group = id / 32;
	mask =  BIT(id % 32);
	reg_assert = data->reset_assert[group].reg_assert;
	reg_status = data->reset_assert[group].reg_status;

	done = data->asserted[group] & mask;

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
	void __iomem *reg_status;
	unsigned long group;
	u32 mask, val;

	group = id / 32;
	mask =  BIT(id % 32);
	reg_status = data->reset_assert[group].reg_status;
	val = readl(reg_status);

	return !((val ^ data->asserted[group]) & mask);
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
		dev_err(&pdev->dev, "get %s io base fail.\n",name);
		return NULL;
	}

	return  ioremap(res->start, resource_size(res));
}

int __init reset_starfive_jh7110_generic_probe(struct platform_device *pdev,
					const u32 *asserted,
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

	jh7110_devm_reset_set(dev);

	data->rcdev.ops = &jh7110_reset_ops;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = nr_resets;
	data->rcdev.dev = &pdev->dev;
	data->rcdev.of_node = pdev->dev.of_node;
	spin_lock_init(&data->lock);

	data->asserted = asserted;

	return devm_reset_controller_register(dev, &data->rcdev);
}
EXPORT_SYMBOL_GPL(reset_starfive_jh7110_generic_probe);

static int __init jh7110_reset_probe(struct platform_device *pdev)
{
	return reset_starfive_jh7110_generic_probe(pdev, jh7110_reset_asserted,
							RSTN_JH7110_RESET_END);
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
