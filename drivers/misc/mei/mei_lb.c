// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Intel Corporation
 */

#include <linux/component.h>
#include <linux/mei_cl_bus.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uuid.h>

#include <drm/intel/i915_component.h>
#include <drm/intel/intel_lb_mei_interface.h>

#include "mkhi.h"

/**
 * DOC: Late Binding Firmware Update/Upload
 *
 * Late Binding is a firmware update/upload mechanism that allows configuration
 * payloads to be securely delivered and applied at runtime, rather than
 * being embedded in the system firmware image (e.g., IFWI or SPI flash).
 *
 * This mechanism is used to update device-level configuration such as:
 * - Fan controller
 * - Voltage regulator (VR)
 *
 * Key Characteristics:
 * ---------------------
 * - Runtime Delivery:
 *   Firmware blobs are loaded by the host driver (e.g., Xe KMD)
 *   after the GPU or SoC has booted.
 *
 * - Secure and Authenticated:
 *   All payloads are signed and verified by the authentication firmware.
 *
 * - No Firmware Flashing Required:
 *   Updates are applied in volatile memory and do not require SPI flash
 *   modification or system reboot.
 *
 * - Re-entrant:
 *   Multiple updates of the same or different types can be applied
 *   sequentially within a single boot session.
 *
 * - Version Controlled:
 *   Each payload includes version and security version number (SVN)
 *   metadata to support anti-rollback enforcement.
 *
 * Upload Flow:
 * ------------
 * 1. Host driver (KMD or user-space tool) loads the late binding firmware.
 * 2. Firmware is passed to the MEI interface and forwarded to
 *    authentication firmware.
 * 3. Authentication firmware authenticates the payload and extracts
 *    command and data arrays.
 * 4. Authentication firmware delivers the configuration to PUnit/PCODE.
 * 5. Status is returned back to the host via MEI.
 */

#define INTEL_LB_CMD	0x12
#define INTEL_LB_RSP	(INTEL_LB_CMD | 0x80)

#define INTEL_LB_SEND_TIMEOUT_MSEC 3000
#define INTEL_LB_RECV_TIMEOUT_MSEC 3000

/**
 * struct mei_lb_req - Late Binding request structure
 * @header: MKHI message header (see struct mkhi_msg_hdr)
 * @type: Type of the Late Binding payload
 * @flags: Flags to be passed to the authentication firmware (e.g. %INTEL_LB_FLAGS_IS_PERSISTENT)
 * @reserved: Reserved for future use by authentication firmware, must be set to 0
 * @payload_size: Size of the payload data in bytes
 * @payload: Payload data to be sent to the authentication firmware
 */
struct mei_lb_req {
	struct mkhi_msg_hdr header;
	__le32 type;
	__le32 flags;
	__le32 reserved[2];
	__le32 payload_size;
	u8 payload[] __counted_by(payload_size);
} __packed;

/**
 * struct mei_lb_rsp - Late Binding response structure
 * @header: MKHI message header (see struct mkhi_msg_hdr)
 * @type: Type of the Late Binding payload
 * @reserved: Reserved for future use by authentication firmware, must be set to 0
 * @status: Status returned by authentication firmware (see &enum intel_lb_status)
 */
struct mei_lb_rsp {
	struct mkhi_msg_hdr header;
	__le32 type;
	__le32 reserved[2];
	__le32 status;
} __packed;

static bool mei_lb_check_response(const struct device *dev, ssize_t bytes,
				  struct mei_lb_rsp *rsp)
{
	/*
	 * Received message size may be smaller than the full message size when
	 * reply contains only MKHI header with result field set to the error code.
	 * Check the header size and content first to output exact error, if needed,
	 * and then process to the whole message.
	 */
	if (bytes < sizeof(rsp->header)) {
		dev_err(dev, "Received less than header size from the firmware: %zd < %zu\n",
			bytes, sizeof(rsp->header));
		return false;
	}
	if (rsp->header.group_id != MKHI_GROUP_ID_GFX) {
		dev_err(dev, "Mismatch group id: 0x%x instead of 0x%x\n",
			rsp->header.group_id, MKHI_GROUP_ID_GFX);
		return false;
	}
	if (rsp->header.command != INTEL_LB_RSP) {
		dev_err(dev, "Mismatch command: 0x%x instead of 0x%x\n",
			rsp->header.command, INTEL_LB_RSP);
		return false;
	}
	if (rsp->header.result) {
		dev_err(dev, "Error in result: 0x%x\n", rsp->header.result);
		return false;
	}
	if (bytes < sizeof(*rsp)) {
		dev_err(dev, "Received less than message size from the firmware: %zd < %zu\n",
			bytes, sizeof(*rsp));
		return false;
	}

	return true;
}

static int mei_lb_push_payload(struct device *dev, u32 type, u32 flags,
			       const void *payload, size_t payload_size)
{
	struct mei_cl_device *cldev;
	struct mei_lb_req *req = NULL;
	struct mei_lb_rsp rsp;
	size_t req_size;
	ssize_t bytes;
	int ret;

	cldev = to_mei_cl_device(dev);

	ret = mei_cldev_enable(cldev);
	if (ret) {
		dev_dbg(dev, "Failed to enable firmware client. %d\n", ret);
		return ret;
	}

	req_size = struct_size(req, payload, payload_size);
	if (req_size > mei_cldev_mtu(cldev)) {
		dev_err(dev, "Payload is too big: %zu\n", payload_size);
		ret = -EMSGSIZE;
		goto end;
	}

	req = kmalloc(req_size, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto end;
	}

	req->header.group_id = MKHI_GROUP_ID_GFX;
	req->header.command = INTEL_LB_CMD;
	req->type = cpu_to_le32(type);
	req->flags = cpu_to_le32(flags);
	req->reserved[0] = 0;
	req->reserved[1] = 0;
	req->payload_size = cpu_to_le32(payload_size);
	memcpy(req->payload, payload, payload_size);

	bytes = mei_cldev_send_timeout(cldev, (u8 *)req, req_size,
				       INTEL_LB_SEND_TIMEOUT_MSEC);
	if (bytes < 0) {
		dev_err(dev, "Failed to send late binding request to firmware. %zd\n", bytes);
		ret = bytes;
		goto end;
	}

	bytes = mei_cldev_recv_timeout(cldev, (u8 *)&rsp, sizeof(rsp),
				       INTEL_LB_RECV_TIMEOUT_MSEC);
	if (bytes < 0) {
		dev_err(dev, "Failed to receive late binding reply from MEI firmware. %zd\n",
			bytes);
		ret = bytes;
		goto end;
	}
	if (!mei_lb_check_response(dev, bytes, &rsp)) {
		dev_err(dev, "Bad response from the firmware. header: %02x %02x %02x %02x\n",
			rsp.header.group_id, rsp.header.command,
			rsp.header.reserved, rsp.header.result);
		ret = -EPROTO;
		goto end;
	}

	dev_dbg(dev, "status = %u\n", le32_to_cpu(rsp.status));
	ret = (int)le32_to_cpu(rsp.status);
end:
	mei_cldev_disable(cldev);
	kfree(req);
	return ret;
}

static const struct intel_lb_component_ops mei_lb_ops = {
	.push_payload = mei_lb_push_payload,
};

static int mei_lb_component_master_bind(struct device *dev)
{
	return component_bind_all(dev, (void *)&mei_lb_ops);
}

static void mei_lb_component_master_unbind(struct device *dev)
{
	component_unbind_all(dev, (void *)&mei_lb_ops);
}

static const struct component_master_ops mei_lb_component_master_ops = {
	.bind = mei_lb_component_master_bind,
	.unbind = mei_lb_component_master_unbind,
};

static int mei_lb_component_match(struct device *dev, int subcomponent,
				  void *data)
{
	/*
	 * This function checks if requester is Intel %PCI_CLASS_DISPLAY_VGA or
	 * %PCI_CLASS_DISPLAY_OTHER device, and checks if the requester is the
	 * grand parent of mei_if i.e. late bind MEI device
	 */
	struct device *base = data;
	struct pci_dev *pdev;

	if (!dev)
		return 0;

	if (!dev_is_pci(dev))
		return 0;

	pdev = to_pci_dev(dev);

	if (pdev->vendor != PCI_VENDOR_ID_INTEL)
		return 0;

	if (pdev->class != (PCI_CLASS_DISPLAY_VGA << 8) &&
	    pdev->class != (PCI_CLASS_DISPLAY_OTHER << 8))
		return 0;

	if (subcomponent != INTEL_COMPONENT_LB)
		return 0;

	base = base->parent;
	if (!base) /* mei device */
		return 0;

	base = base->parent; /* pci device */

	return !!base && dev == base;
}

static int mei_lb_probe(struct mei_cl_device *cldev,
			const struct mei_cl_device_id *id)
{
	struct component_match *master_match = NULL;
	int ret;

	component_match_add_typed(&cldev->dev, &master_match,
				  mei_lb_component_match, &cldev->dev);
	if (IS_ERR_OR_NULL(master_match))
		return -ENOMEM;

	ret = component_master_add_with_match(&cldev->dev,
					      &mei_lb_component_master_ops,
					      master_match);
	if (ret < 0)
		dev_err(&cldev->dev, "Failed to add late binding master component. %d\n", ret);

	return ret;
}

static void mei_lb_remove(struct mei_cl_device *cldev)
{
	component_master_del(&cldev->dev, &mei_lb_component_master_ops);
}

#define MEI_GUID_MKHI UUID_LE(0xe2c2afa2, 0x3817, 0x4d19, \
			      0x9d, 0x95, 0x6, 0xb1, 0x6b, 0x58, 0x8a, 0x5d)

static const struct mei_cl_device_id mei_lb_tbl[] = {
	{ .uuid = MEI_GUID_MKHI, .version = MEI_CL_VERSION_ANY },
	{ }
};
MODULE_DEVICE_TABLE(mei, mei_lb_tbl);

static struct mei_cl_driver mei_lb_driver = {
	.id_table = mei_lb_tbl,
	.name = "mei_lb",
	.probe = mei_lb_probe,
	.remove	= mei_lb_remove,
};

module_mei_cl_driver(mei_lb_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MEI Late Binding Firmware Update/Upload");
