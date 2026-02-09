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
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
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

#define REG_SUBBLK_RESET_CR	0x88u

struct mpfs_reset {
	struct regmap *regmap;
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

	return regmap_set_bits(rst->regmap, REG_SUBBLK_RESET_CR, BIT(id));

}

static int mpfs_deassert(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct mpfs_reset *rst = to_mpfs_reset(rcdev);

	return regmap_clear_bits(rst->regmap, REG_SUBBLK_RESET_CR, BIT(id));

}

static int mpfs_status(struct reset_controller_dev *rcdev, unsigned long id)
{
	struct mpfs_reset *rst = to_mpfs_reset(rcdev);
	u32 reg;

	regmap_read(rst->regmap, REG_SUBBLK_RESET_CR, &reg);

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

static int mpfs_reset_mfd_probe(struct platform_device *pdev)
{
	struct reset_controller_dev *rcdev;
	struct device *dev = &pdev->dev;
	struct mpfs_reset *rst;

	rst = devm_kzalloc(dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rcdev = &rst->rcdev;
	rcdev->dev = dev;
	rcdev->ops = &mpfs_reset_ops;

	rcdev->of_node = pdev->dev.parent->of_node;
	rcdev->of_reset_n_cells = 1;
	rcdev->of_xlate = mpfs_reset_xlate;
	rcdev->nr_resets = MPFS_NUM_RESETS;

	rst->regmap = device_node_to_regmap(pdev->dev.parent->of_node);
	if (IS_ERR(rst->regmap))
		return dev_err_probe(dev, PTR_ERR(rst->regmap),
				     "Failed to find syscon regmap\n");

	return devm_reset_controller_register(dev, rcdev);
}

static struct platform_driver mpfs_reset_mfd_driver = {
	.probe = mpfs_reset_mfd_probe,
	.driver = {
		.name = "mpfs-reset",
	},
};
module_platform_driver(mpfs_reset_mfd_driver);

static int mpfs_reset_adev_probe(struct auxiliary_device *adev,
				 const struct auxiliary_device_id *id)
{
	struct reset_controller_dev *rcdev;
	struct device *dev = &adev->dev;
	struct mpfs_reset *rst;

	rst = devm_kzalloc(dev, sizeof(*rst), GFP_KERNEL);
	if (!rst)
		return -ENOMEM;

	rst->regmap = (struct regmap *)adev->dev.platform_data;

	rcdev = &rst->rcdev;
	rcdev->dev = dev;
	rcdev->ops = &mpfs_reset_ops;

	rcdev->of_node = dev->parent->of_node;
	rcdev->of_reset_n_cells = 1;
	rcdev->of_xlate = mpfs_reset_xlate;
	rcdev->nr_resets = MPFS_NUM_RESETS;

	return devm_reset_controller_register(dev, rcdev);
}

int mpfs_reset_controller_register(struct device *clk_dev, struct regmap *map)
{
	struct auxiliary_device *adev;

	adev = devm_auxiliary_device_create(clk_dev, "reset-mpfs", (void *)map);
	if (!adev)
		return -ENODEV;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(mpfs_reset_controller_register, "MCHP_CLK_MPFS");

static const struct auxiliary_device_id mpfs_reset_ids[] = {
	{
		.name = "reset_mpfs.reset-mpfs",
	},
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, mpfs_reset_ids);

static struct auxiliary_driver mpfs_reset_aux_driver = {
	.probe		= mpfs_reset_adev_probe,
	.id_table	= mpfs_reset_ids,
};

module_auxiliary_driver(mpfs_reset_aux_driver);

MODULE_DESCRIPTION("Microchip PolarFire SoC Reset Driver");
MODULE_AUTHOR("Conor Dooley <conor.dooley@microchip.com>");
MODULE_IMPORT_NS("MCHP_CLK_MPFS");
