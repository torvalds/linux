// SPDX-License-Identifier: GPL-2.0-only

/* SpacemiT reset controller driver - common implementation */

#include <linux/container_of.h>
#include <linux/device.h>
#include <linux/module.h>

#include <soc/spacemit/ccu.h>

#include "reset-spacemit-common.h"

static int spacemit_reset_update(struct reset_controller_dev *rcdev,
				 unsigned long id, bool assert)
{
	struct ccu_reset_controller *controller;
	const struct ccu_reset_data *data;
	u32 mask;
	u32 val;

	controller = container_of(rcdev, struct ccu_reset_controller, rcdev);
	data = &controller->data->reset_data[id];
	mask = data->assert_mask | data->deassert_mask;
	val = assert ? data->assert_mask : data->deassert_mask;

	return regmap_update_bits(controller->regmap, data->offset, mask, val);
}

static int spacemit_reset_assert(struct reset_controller_dev *rcdev,
				 unsigned long id)
{
	return spacemit_reset_update(rcdev, id, true);
}

static int spacemit_reset_deassert(struct reset_controller_dev *rcdev,
				   unsigned long id)
{
	return spacemit_reset_update(rcdev, id, false);
}

static const struct reset_control_ops spacemit_reset_control_ops = {
	.assert		= spacemit_reset_assert,
	.deassert	= spacemit_reset_deassert,
};

static int spacemit_reset_controller_register(struct device *dev,
					      struct ccu_reset_controller *controller)
{
	struct reset_controller_dev *rcdev = &controller->rcdev;

	rcdev->ops = &spacemit_reset_control_ops;
	rcdev->owner = dev->driver->owner;
	rcdev->of_node = dev->of_node;
	rcdev->nr_resets = controller->data->count;

	return devm_reset_controller_register(dev, &controller->rcdev);
}

int spacemit_reset_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct spacemit_ccu_adev *rdev = to_spacemit_ccu_adev(adev);
	struct ccu_reset_controller *controller;
	struct device *dev = &adev->dev;

	controller = devm_kzalloc(dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;
	controller->data = (const struct ccu_reset_controller_data *)id->driver_data;
	controller->regmap = rdev->regmap;

	return spacemit_reset_controller_register(dev, controller);
}
EXPORT_SYMBOL_NS_GPL(spacemit_reset_probe, "RESET_SPACEMIT");

MODULE_DESCRIPTION("SpacemiT reset controller driver - common code");
MODULE_LICENSE("GPL");
