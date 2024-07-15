// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2023 Google LLC.
 */

#include <linux/device.h>
#include <linux/spmi.h>

static void devm_spmi_controller_release(struct device *parent, void *res)
{
	spmi_controller_put(*(struct spmi_controller **)res);
}

struct spmi_controller *devm_spmi_controller_alloc(struct device *parent, size_t size)
{
	struct spmi_controller **ptr, *ctrl;

	ptr = devres_alloc(devm_spmi_controller_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	ctrl = spmi_controller_alloc(parent, size);
	if (IS_ERR(ctrl)) {
		devres_free(ptr);
		return ctrl;
	}

	*ptr = ctrl;
	devres_add(parent, ptr);

	return ctrl;
}
EXPORT_SYMBOL_GPL(devm_spmi_controller_alloc);

static void devm_spmi_controller_remove(struct device *parent, void *res)
{
	spmi_controller_remove(*(struct spmi_controller **)res);
}

int devm_spmi_controller_add(struct device *parent, struct spmi_controller *ctrl)
{
	struct spmi_controller **ptr;
	int ret;

	ptr = devres_alloc(devm_spmi_controller_remove, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = spmi_controller_add(ctrl);
	if (ret) {
		devres_free(ptr);
		return ret;
	}

	*ptr = ctrl;
	devres_add(parent, ptr);

	return 0;

}
EXPORT_SYMBOL_GPL(devm_spmi_controller_add);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPMI devres helpers");
