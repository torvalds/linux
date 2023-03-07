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
#include <linux/module.h>
#include <linux/platform_device.h>
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

/*
 * Peripheral clock resets
 */

static int mpfs_assert(struct reset_controller_dev *rcdev, unsigned long id)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpfs_reset_lock, flags);

	reg = mpfs_reset_read(rcdev->dev);
	reg |= BIT(id);
	mpfs_reset_write(rcdev->dev, reg);

	spin_unlock_irqrestore(&mpfs_reset_lock, flags);

	return 0;
}

static int mpfs_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&mpfs_reset_lock, flags);

	reg = mpfs_reset_read(rcdev->dev);
	reg &= ~BIT(id);
	mpfs_reset_write(rcdev->dev, reg);

	spin_unlock_irqrestore(&mpfs_reset_lock, flags);

	return 0;
}

static int mpfs_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	u32 reg = mpfs_reset_read(rcdev->dev);

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

	rcdev = devm_kzalloc(dev, sizeof(*rcdev), GFP_KERNEL);
	if (!rcdev)
		return -ENOMEM;

	rcdev->dev = dev;
	rcdev->dev->parent = dev->parent;
	rcdev->ops = &mpfs_reset_ops;
	rcdev->of_node = dev->parent->of_node;
	rcdev->of_reset_n_cells = 1;
	rcdev->of_xlate = mpfs_reset_xlate;
	rcdev->nr_resets = MPFS_NUM_RESETS;

	return devm_reset_controller_register(dev, rcdev);
}

static const struct auxiliary_device_id mpfs_reset_ids[] = {
	{
		.name = "clk_mpfs.reset-mpfs",
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
