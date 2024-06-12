// SPDX-License-Identifier: GPL-2.0-only
/*
 * PolarFire SoC (MPFS) Peripheral Clock Reset Controller
 *
 * Author: Conor Dooley <conor.dooley@microchip.com>
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 */
#include <linux/auxiliary_bus.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/reset-controller.h>
#include <dt-bindings/clock/microchip,mpfs-clock.h>
#include <soc/microchip/mpfs.h>

/*
 * The ENVM reset is the lowest bit in the register & I am using the CLK_FOO
 * defines in the dt to make things easier to configure - so this is accounting
 * for the offset of 3 there.
 */
#define MPFS_PERIPH_OFFSET	CLK_ENVM
#define MPFS_NUM_RESETS		30u
#define MPFS_SLEEP_MIN_US	100
#define MPFS_SLEEP_MAX_US	200

/* block concurrent access to the soft reset register */
static DEFINE_SPINLOCK(mpfs_reset_lock);

struct mpfs_reset {
	void __iomem *base;
	struct reset_controller_dev rcdev;
};

static inline struct mpfs_reset *to_mpfs_reset(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct mpfs_reset, rcdev);
}

/*
 * Peripheral clock resets
 */
static int mpfs_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct mpfs_reset *rst = to_mpfs_reset(rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpfs_reset_lock, flags);

	reg = readl(rst->base);
	reg |= BIT(id);
	writel(reg, rst->base);

	spin_unlock_irqrestore(&mpfs_reset_lock, flags);

	return 0;
}

static int mpfs_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct mpfs_reset *rst = to_mpfs_reset(rcdev);
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpfs_reset_lock, flags);

	reg = readl(rst->base);
	reg &= ~BIT(id);
	writel(reg, rst->base);

	spin_unlock_irqrestore(&mpfs_reset_lock, flags);

	return 0;
}

static int mpfs_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct mpfs_reset *rst = to_mpfs_reset(rcdev);
	u32 reg = readl(rst->base);

	/*
	 * It is safe to return here as MPFS_NUM_RESETS makes sure the sign bit
	 * is never hit.
	 */
	return (reg & BIT(id));
}

static int mpfs_reset(struct reset_controller_dev *rcdev, unsigned long id)
{
	mpfs_assert(rcdev, id);

	usleep_range(MPFS_SLEEP_MIN_US, MPFS_SLEEP_MAX_US);

	mpfs_deassert(rcdev, id);

	return 0;
}

static const struct reset_control_ops mpfs_reset_ops = {
	.reset = mpfs_reset,
	.assert = mpfs_assert,
	.deassert = mpfs_deassert,
	.status = mpfs_status,
};

static int mpfs_reset_xlate(struct reset_controller_dev *rcdev,
			    const struct of_phandle_args *reset_spec)
{
	unsigned int index = reset_spec->args[0];

	/*
	 * CLK_RESERVED does not map to a clock, but it does map to a reset,
	 * so it has to be accounted for here. It is the reset for the fabric,
	 * so if this reset gets called - do not reset it.
	 */
	if (index == CLK_RESERVED) {
		dev_err(rcdev->dev, "Resetting the fabric is not supported\n");
		return -EINVAL;
	}

	if (index < MPFS_PERIPH_OFFSET || index >= (MPFS_PERIPH_OFFSET + rcdev->nr_resets)) {
		dev_err(rcdev->dev, "Invalid reset index %u\n", index);
		return -EINVAL;
	}

	return index - MPFS_PERIPH_OFFSET;
}

static int mpfs_reset_probe(struct auxiliary_device *adev,
			    const struct auxiliary_device_id *id)
{
	struct device *dev = &adev->dev;
	struct reset_controller_dev *rcdev;
	struct mpfs_reset *rst;

	rst = devm_kzalloc(dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->base = (void __iomem *)adev->dev.platform_data;

	rcdev = &rst->rcdev;
	rcdev->dev = dev;
	rcdev->dev->parent = dev->parent;
	rcdev->ops = &mpfs_reset_ops;
	rcdev->of_node = dev->parent->of_node;
	rcdev->of_reset_n_cells = 1;
	rcdev->of_xlate = mpfs_reset_xlate;
	rcdev->nr_resets = MPFS_NUM_RESETS;

	return devm_reset_controller_register(dev, rcdev);
}

static void mpfs_reset_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static void mpfs_reset_adev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	kfree(adev);
}

static struct auxiliary_device *mpfs_reset_adev_alloc(struct device *clk_dev)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	adev->name = "reset-mpfs";
	adev->dev.parent = clk_dev;
	adev->dev.release = mpfs_reset_adev_release;
	adev->id = 666u;

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(adev);
		return ERR_PTR(ret);
	}

	return adev;
}

int mpfs_reset_controller_register(struct device *clk_dev, void __iomem *base)
{
	struct auxiliary_device *adev;
	int ret;

	adev = mpfs_reset_adev_alloc(clk_dev);
	if (IS_ERR(adev))
		return PTR_ERR(adev);

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	adev->dev.platform_data = (__force void *)base;

	return devm_add_action_or_reset(clk_dev, mpfs_reset_unregister_adev, adev);
}
EXPORT_SYMBOL_NS_GPL(mpfs_reset_controller_register, MCHP_CLK_MPFS);

static const struct auxiliary_device_id mpfs_reset_ids[] = {
	{
		.name = "reset_mpfs.reset-mpfs",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, mpfs_reset_ids);

static struct auxiliary_driver mpfs_reset_driver = {
	.probe		= mpfs_reset_probe,
	.id_table	= mpfs_reset_ids,
};

module_auxiliary_driver(mpfs_reset_driver);

MODULE_DESCRIPTION("Microchip PolarFire SoC Reset Driver");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_IMPORT_NS(MCHP_CLK_MPFS);
