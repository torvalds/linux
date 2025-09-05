// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/component.h>
#include <linux/delay.h>
#include <linux/firmware.h>

#include <drm/drm_managed.h>
#include <drm/intel/i915_component.h>
#include <drm/intel/intel_lb_mei_interface.h>
#include <drm/drm_print.h>

#include "xe_device.h"
#include "xe_late_bind_fw.h"
#include "xe_pcode.h"
#include "xe_pcode_api.h"

static const u32 fw_id_to_type[] = {
		[XE_LB_FW_FAN_CONTROL] = INTEL_LB_TYPE_FAN_CONTROL,
	};

static const char * const fw_id_to_name[] = {
		[XE_LB_FW_FAN_CONTROL] = "fan_control",
	};

static struct xe_device *
late_bind_to_xe(struct xe_late_bind *late_bind)
{
	return container_of(late_bind, struct xe_device, late_bind);
}

static int xe_late_bind_fw_num_fans(struct xe_late_bind *late_bind)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);
	u32 uval;

	if (!xe_pcode_read(root_tile,
			   PCODE_MBOX(FAN_SPEED_CONTROL, FSC_READ_NUM_FANS, 0), &uval, NULL))
		return uval;
	else
		return 0;
}

static int __xe_late_bind_fw_init(struct xe_late_bind *late_bind, u32 fw_id)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct xe_late_bind_fw *lb_fw;
	const struct firmware *fw;
	u32 num_fans;
	int ret;

	if (fw_id >= XE_LB_FW_MAX_ID)
		return -EINVAL;

	lb_fw = &late_bind->late_bind_fw[fw_id];

	lb_fw->id = fw_id;
	lb_fw->type = fw_id_to_type[lb_fw->id];
	lb_fw->flags &= ~INTEL_LB_FLAG_IS_PERSISTENT;

	if (lb_fw->type == INTEL_LB_TYPE_FAN_CONTROL) {
		num_fans = xe_late_bind_fw_num_fans(late_bind);
		drm_dbg(&xe->drm, "Number of Fans: %d\n", num_fans);
		if (!num_fans)
			return 0;
	}

	snprintf(lb_fw->blob_path, sizeof(lb_fw->blob_path), "xe/%s_8086_%04x_%04x_%04x.bin",
		 fw_id_to_name[lb_fw->id], pdev->device,
		 pdev->subsystem_vendor, pdev->subsystem_device);

	drm_dbg(&xe->drm, "Request late binding firmware %s\n", lb_fw->blob_path);
	ret = firmware_request_nowarn(&fw, lb_fw->blob_path, xe->drm.dev);
	if (ret) {
		drm_dbg(&xe->drm, "%s late binding fw not available for current device",
			fw_id_to_name[lb_fw->id]);
		return 0;
	}

	if (fw->size > XE_LB_MAX_PAYLOAD_SIZE) {
		drm_err(&xe->drm, "Firmware %s size %zu is larger than max pay load size %u\n",
			lb_fw->blob_path, fw->size, XE_LB_MAX_PAYLOAD_SIZE);
		release_firmware(fw);
		return -ENODATA;
	}

	lb_fw->payload_size = fw->size;
	lb_fw->payload = drmm_kzalloc(&xe->drm, lb_fw->payload_size, GFP_KERNEL);
	if (!lb_fw->payload) {
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy((void *)lb_fw->payload, fw->data, lb_fw->payload_size);
	release_firmware(fw);

	return 0;
}

static int xe_late_bind_fw_init(struct xe_late_bind *late_bind)
{
	int ret;
	int fw_id;

	for (fw_id = 0; fw_id < XE_LB_FW_MAX_ID; fw_id++) {
		ret = __xe_late_bind_fw_init(late_bind, fw_id);
		if (ret)
			return ret;
	}
	return 0;
}

static int xe_late_bind_component_bind(struct device *xe_kdev,
				       struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe_device(xe_kdev);
	struct xe_late_bind *late_bind = &xe->late_bind;

	late_bind->component.ops = data;
	late_bind->component.mei_dev = mei_kdev;

	return 0;
}

static void xe_late_bind_component_unbind(struct device *xe_kdev,
					  struct device *mei_kdev, void *data)
{
	struct xe_device *xe = kdev_to_xe_device(xe_kdev);
	struct xe_late_bind *late_bind = &xe->late_bind;

	late_bind->component.ops = NULL;
}

static const struct component_ops xe_late_bind_component_ops = {
	.bind   = xe_late_bind_component_bind,
	.unbind = xe_late_bind_component_unbind,
};

static void xe_late_bind_remove(void *arg)
{
	struct xe_late_bind *late_bind = arg;
	struct xe_device *xe = late_bind_to_xe(late_bind);

	component_del(xe->drm.dev, &xe_late_bind_component_ops);
}

/**
 * xe_late_bind_init() - add xe mei late binding component
 * @late_bind: pointer to late bind structure.
 *
 * Return: 0 if the initialization was successful, a negative errno otherwise.
 */
int xe_late_bind_init(struct xe_late_bind *late_bind)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	int err;

	if (!xe->info.has_late_bind)
		return 0;

	if (!IS_ENABLED(CONFIG_INTEL_MEI_LB) || !IS_ENABLED(CONFIG_INTEL_MEI_GSC)) {
		drm_info(&xe->drm, "Can't init xe mei late bind missing mei component\n");
		return 0;
	}

	err = component_add_typed(xe->drm.dev, &xe_late_bind_component_ops,
				  INTEL_COMPONENT_LB);
	if (err < 0) {
		drm_err(&xe->drm, "Failed to add mei late bind component (%pe)\n", ERR_PTR(err));
		return err;
	}

	err = devm_add_action_or_reset(xe->drm.dev, xe_late_bind_remove, late_bind);
	if (err)
		return err;

	return xe_late_bind_fw_init(late_bind);
}
