// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Author: Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */
#include <linux/auxiliary_bus.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/of.h>

#include <drm/drm_bridge.h>
#include <drm/bridge/aux-bridge.h>

static DEFINE_IDA(drm_aux_bridge_ida);

static void drm_aux_bridge_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	of_node_put(dev->of_node);
	ida_free(&drm_aux_bridge_ida, adev->id);

	kfree(adev);
}

static void drm_aux_bridge_unregister_adev(void *_adev)
{
	struct auxiliary_device *adev = _adev;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

/**
 * drm_aux_bridge_register - Create a simple bridge device to link the chain
 * @parent: device instance providing this bridge
 *
 * Creates a simple DRM bridge that doesn't implement any drm_bridge
 * operations. Such bridges merely fill a place in the bridge chain linking
 * surrounding DRM bridges.
 *
 * Return: zero on success, negative error code on failure
 */
int drm_aux_bridge_register(struct device *parent)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	ret = ida_alloc(&drm_aux_bridge_ida, GFP_KERNEL);
	if (ret < 0) {
		kfree(adev);
		return ret;
	}

	adev->id = ret;
	adev->name = "aux_bridge";
	adev->dev.parent = parent;
	adev->dev.release = drm_aux_bridge_release;

	device_set_of_node_from_dev(&adev->dev, parent);

	ret = auxiliary_device_init(adev);
	if (ret) {
		of_node_put(adev->dev.of_node);
		ida_free(&drm_aux_bridge_ida, adev->id);
		kfree(adev);
		return ret;
	}

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(parent, drm_aux_bridge_unregister_adev, adev);
}
EXPORT_SYMBOL_GPL(drm_aux_bridge_register);

struct drm_aux_bridge_data {
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct device *dev;
};

static int drm_aux_bridge_attach(struct drm_bridge *bridge,
				 struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct drm_aux_bridge_data *data;

	if (!(flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR))
		return -EINVAL;

	data = container_of(bridge, struct drm_aux_bridge_data, bridge);

	return drm_bridge_attach(encoder, data->next_bridge, bridge,
				 DRM_BRIDGE_ATTACH_NO_CONNECTOR);
}

static const struct drm_bridge_funcs drm_aux_bridge_funcs = {
	.attach	= drm_aux_bridge_attach,
};

static int drm_aux_bridge_probe(struct auxiliary_device *auxdev,
				const struct auxiliary_device_id *id)
{
	struct drm_aux_bridge_data *data;

	data = devm_drm_bridge_alloc(&auxdev->dev, struct drm_aux_bridge_data,
				     bridge, &drm_aux_bridge_funcs);
	if (IS_ERR(data))
		return PTR_ERR(data);

	data->dev = &auxdev->dev;
	data->next_bridge = devm_drm_of_get_bridge(&auxdev->dev, auxdev->dev.of_node, 0, 0);
	if (IS_ERR(data->next_bridge))
		return dev_err_probe(&auxdev->dev, PTR_ERR(data->next_bridge),
				     "failed to acquire drm_bridge\n");

	data->bridge.of_node = data->dev->of_node;

	/* passthrough data, allow everything */
	data->bridge.interlace_allowed = true;
	data->bridge.ycbcr_420_allowed = true;

	return devm_drm_bridge_add(data->dev, &data->bridge);
}

static const struct auxiliary_device_id drm_aux_bridge_table[] = {
	{ .name = KBUILD_MODNAME ".aux_bridge" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, drm_aux_bridge_table);

static struct auxiliary_driver drm_aux_bridge_drv = {
	.name = "aux_bridge",
	.id_table = drm_aux_bridge_table,
	.probe = drm_aux_bridge_probe,
};
module_auxiliary_driver(drm_aux_bridge_drv);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_DESCRIPTION("DRM transparent bridge");
MODULE_LICENSE("GPL");
