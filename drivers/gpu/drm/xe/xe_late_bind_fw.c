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
#include "xe_pm.h"

/*
 * The component should load quite quickly in most cases, but it could take
 * a bit. Using a very big timeout just to cover the worst case scenario
 */
#define LB_INIT_TIMEOUT_MS 20000

/*
 * Retry interval set to 6 seconds, in steps of 200 ms, to allow time for
 * other OS components to release the MEI CL handle
 */
#define LB_FW_LOAD_RETRY_MAXCOUNT 30
#define LB_FW_LOAD_RETRY_PAUSE_MS 200

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

static struct xe_device *
late_bind_fw_to_xe(struct xe_late_bind_fw *lb_fw)
{
	return container_of(lb_fw, struct xe_device, late_bind.late_bind_fw[lb_fw->id]);
}

/* Refer to the "Late Bind based Firmware Layout" documentation entry for details */
static int parse_cpd_header(struct xe_late_bind_fw *lb_fw,
			    const void *data, size_t size, const char *manifest_entry)
{
	struct xe_device *xe = late_bind_fw_to_xe(lb_fw);
	const struct gsc_cpd_header_v2 *header = data;
	const struct gsc_manifest_header *manifest;
	const struct gsc_cpd_entry *entry;
	size_t min_size = sizeof(*header);
	u32 offset = 0;
	int i;

	/* manifest_entry is mandatory */
	xe_assert(xe, manifest_entry);

	if (size < min_size || header->header_marker != GSC_CPD_HEADER_MARKER)
		return -ENOENT;

	if (header->header_length < sizeof(struct gsc_cpd_header_v2)) {
		drm_err(&xe->drm, "%s late binding fw: Invalid CPD header length %u!\n",
			fw_id_to_name[lb_fw->id], header->header_length);
		return -EINVAL;
	}

	min_size = header->header_length + sizeof(struct gsc_cpd_entry) * header->num_of_entries;
	if (size < min_size) {
		drm_err(&xe->drm, "%s late binding fw: too small! %zu < %zu\n",
			fw_id_to_name[lb_fw->id], size, min_size);
		return -ENODATA;
	}

	/* Look for the manifest first */
	entry = (void *)header + header->header_length;
	for (i = 0; i < header->num_of_entries; i++, entry++)
		if (strcmp(entry->name, manifest_entry) == 0)
			offset = entry->offset & GSC_CPD_ENTRY_OFFSET_MASK;

	if (!offset) {
		drm_err(&xe->drm, "%s late binding fw: Failed to find manifest_entry\n",
			fw_id_to_name[lb_fw->id]);
		return -ENODATA;
	}

	min_size = offset + sizeof(struct gsc_manifest_header);
	if (size < min_size) {
		drm_err(&xe->drm, "%s late binding fw: too small! %zu < %zu\n",
			fw_id_to_name[lb_fw->id], size, min_size);
		return -ENODATA;
	}

	manifest = data + offset;

	lb_fw->version = manifest->fw_version;

	return 0;
}

/* Refer to the "Late Bind based Firmware Layout" documentation entry for details */
static int parse_lb_layout(struct xe_late_bind_fw *lb_fw,
			   const void *data, size_t size, const char *fpt_entry)
{
	struct xe_device *xe = late_bind_fw_to_xe(lb_fw);
	const struct csc_fpt_header *header = data;
	const struct csc_fpt_entry *entry;
	size_t min_size = sizeof(*header);
	u32 offset = 0;
	int i;

	/* fpt_entry is mandatory */
	xe_assert(xe, fpt_entry);

	if (size < min_size || header->header_marker != CSC_FPT_HEADER_MARKER)
		return -ENOENT;

	if (header->header_length < sizeof(struct csc_fpt_header)) {
		drm_err(&xe->drm, "%s late binding fw: Invalid FPT header length %u!\n",
			fw_id_to_name[lb_fw->id], header->header_length);
		return -EINVAL;
	}

	min_size = header->header_length + sizeof(struct csc_fpt_entry) * header->num_of_entries;
	if (size < min_size) {
		drm_err(&xe->drm, "%s late binding fw: too small! %zu < %zu\n",
			fw_id_to_name[lb_fw->id], size, min_size);
		return -ENODATA;
	}

	/* Look for the cpd header first */
	entry = (void *)header + header->header_length;
	for (i = 0; i < header->num_of_entries; i++, entry++)
		if (strcmp(entry->name, fpt_entry) == 0)
			offset = entry->offset;

	if (!offset) {
		drm_err(&xe->drm, "%s late binding fw: Failed to find fpt_entry\n",
			fw_id_to_name[lb_fw->id]);
		return -ENODATA;
	}

	min_size = offset + sizeof(struct gsc_cpd_header_v2);
	if (size < min_size) {
		drm_err(&xe->drm, "%s late binding fw: too small! %zu < %zu\n",
			fw_id_to_name[lb_fw->id], size, min_size);
		return -ENODATA;
	}

	return parse_cpd_header(lb_fw, data + offset, size - offset, "LTES.man");
}

static const char *xe_late_bind_parse_status(uint32_t status)
{
	switch (status) {
	case INTEL_LB_STATUS_SUCCESS:
		return "success";
	case INTEL_LB_STATUS_4ID_MISMATCH:
		return "4Id Mismatch";
	case INTEL_LB_STATUS_ARB_FAILURE:
		return "ARB Failure";
	case INTEL_LB_STATUS_GENERAL_ERROR:
		return "General Error";
	case INTEL_LB_STATUS_INVALID_PARAMS:
		return "Invalid Params";
	case INTEL_LB_STATUS_INVALID_SIGNATURE:
		return "Invalid Signature";
	case INTEL_LB_STATUS_INVALID_PAYLOAD:
		return "Invalid Payload";
	case INTEL_LB_STATUS_TIMEOUT:
		return "Timeout";
	default:
		return "Unknown error";
	}
}

static int xe_late_bind_fw_num_fans(struct xe_late_bind *late_bind, u32 *num_fans)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	struct xe_tile *root_tile = xe_device_get_root_tile(xe);

	return xe_pcode_read(root_tile,
			     PCODE_MBOX(FAN_SPEED_CONTROL, FSC_READ_NUM_FANS, 0), num_fans, NULL);
}

void xe_late_bind_wait_for_worker_completion(struct xe_late_bind *late_bind)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	struct xe_late_bind_fw *lbfw;
	int fw_id;

	for (fw_id = 0; fw_id < XE_LB_FW_MAX_ID; fw_id++) {
		lbfw = &late_bind->late_bind_fw[fw_id];
		if (lbfw->payload && late_bind->wq) {
			drm_dbg(&xe->drm, "Flush work: load %s firmware\n",
				fw_id_to_name[lbfw->id]);
			flush_work(&lbfw->work);
		}
	}
}

static void xe_late_bind_work(struct work_struct *work)
{
	struct xe_late_bind_fw *lbfw = container_of(work, struct xe_late_bind_fw, work);
	struct xe_late_bind *late_bind = container_of(lbfw, struct xe_late_bind,
						      late_bind_fw[lbfw->id]);
	struct xe_device *xe = late_bind_to_xe(late_bind);
	int retry = LB_FW_LOAD_RETRY_MAXCOUNT;
	int ret;
	int slept;

	xe_device_assert_mem_access(xe);

	/* we can queue this before the component is bound */
	for (slept = 0; slept < LB_INIT_TIMEOUT_MS; slept += 100) {
		if (late_bind->component.ops)
			break;
		msleep(100);
	}

	if (!late_bind->component.ops) {
		drm_err(&xe->drm, "Late bind component not bound\n");
		/* Do not re-attempt fw load */
		drmm_kfree(&xe->drm, (void *)lbfw->payload);
		lbfw->payload = NULL;
		goto out;
	}

	drm_dbg(&xe->drm, "Load %s firmware\n", fw_id_to_name[lbfw->id]);

	do {
		ret = late_bind->component.ops->push_payload(late_bind->component.mei_dev,
							     lbfw->type,
							     lbfw->flags,
							     lbfw->payload,
							     lbfw->payload_size);
		if (!ret)
			break;
		msleep(LB_FW_LOAD_RETRY_PAUSE_MS);
	} while (--retry && ret == -EBUSY);

	if (!ret) {
		drm_dbg(&xe->drm, "Load %s firmware successful\n",
			fw_id_to_name[lbfw->id]);
		goto out;
	}

	if (ret > 0)
		drm_err(&xe->drm, "Load %s firmware failed with err %d, %s\n",
			fw_id_to_name[lbfw->id], ret, xe_late_bind_parse_status(ret));
	else
		drm_err(&xe->drm, "Load %s firmware failed with err %d",
			fw_id_to_name[lbfw->id], ret);
	/* Do not re-attempt fw load */
	drmm_kfree(&xe->drm, (void *)lbfw->payload);
	lbfw->payload = NULL;

out:
	xe_pm_runtime_put(xe);
}

int xe_late_bind_fw_load(struct xe_late_bind *late_bind)
{
	struct xe_device *xe = late_bind_to_xe(late_bind);
	struct xe_late_bind_fw *lbfw;
	int fw_id;

	if (!late_bind->component_added)
		return -ENODEV;

	if (late_bind->disable)
		return 0;

	for (fw_id = 0; fw_id < XE_LB_FW_MAX_ID; fw_id++) {
		lbfw = &late_bind->late_bind_fw[fw_id];
		if (lbfw->payload) {
			xe_pm_runtime_get_noresume(xe);
			queue_work(late_bind->wq, &lbfw->work);
		}
	}
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
		ret = xe_late_bind_fw_num_fans(late_bind, &num_fans);
		if (ret) {
			drm_dbg(&xe->drm, "Failed to read number of fans: %d\n", ret);
			return 0; /* Not a fatal error, continue without fan control */
		}
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

	ret = parse_lb_layout(lb_fw, fw->data, fw->size, "LTES");
	if (ret)
		return ret;

	lb_fw->payload_size = fw->size;
	lb_fw->payload = drmm_kzalloc(&xe->drm, lb_fw->payload_size, GFP_KERNEL);
	if (!lb_fw->payload) {
		release_firmware(fw);
		return -ENOMEM;
	}

	drm_info(&xe->drm, "Using %s firmware from %s version %u.%u.%u.%u\n",
		 fw_id_to_name[lb_fw->id], lb_fw->blob_path,
		 lb_fw->version.major, lb_fw->version.minor,
		 lb_fw->version.hotfix, lb_fw->version.build);

	memcpy((void *)lb_fw->payload, fw->data, lb_fw->payload_size);
	release_firmware(fw);
	INIT_WORK(&lb_fw->work, xe_late_bind_work);

	return 0;
}

static int xe_late_bind_fw_init(struct xe_late_bind *late_bind)
{
	int ret;
	int fw_id;

	late_bind->wq = alloc_ordered_workqueue("late-bind-ordered-wq", 0);
	if (!late_bind->wq)
		return -ENOMEM;

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

	xe_late_bind_wait_for_worker_completion(late_bind);

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

	xe_late_bind_wait_for_worker_completion(late_bind);

	late_bind->component_added = false;

	component_del(xe->drm.dev, &xe_late_bind_component_ops);
	if (late_bind->wq) {
		destroy_workqueue(late_bind->wq);
		late_bind->wq = NULL;
	}
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

	late_bind->component_added = true;

	err = devm_add_action_or_reset(xe->drm.dev, xe_late_bind_remove, late_bind);
	if (err)
		return err;

	err = xe_late_bind_fw_init(late_bind);
	if (err)
		return err;

	return xe_late_bind_fw_load(late_bind);
}
