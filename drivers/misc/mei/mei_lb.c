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

/* Late Binding version 1 */

#define INTEL_LB_CMD	0x12
#define INTEL_LB_RSP	(INTEL_LB_CMD | 0x80)

#define INTEL_LB_SEND_TIMEOUT_MSEC 3000
#define INTEL_LB_RECV_TIMEOUT_MSEC 3000

#define MEI_GUID_MKHI UUID_LE(0xe2c2afa2, 0x3817, 0x4d19, \
			      0x9d, 0x95, 0x6, 0xb1, 0x6b, 0x58, 0x8a, 0x5d)

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

/* Late Binding version 2 */

#define MEI_LB2_CMD 0x01

#define MEI_LB2_HDR_FLAG_RSP 0x01

#define MEI_GUID_LB UUID_LE(0x4ed87243, 0x3980, 0x4d8e, \
			    0xb1, 0xf9, 0x6f, 0xb7, 0xc0, 0x14, 0x8c, 0x4d)

/**
 * struct mei_lb2_header - Late Binding2 header
 * @command_id:
 * @flags: Flags for transport layer (e.g. MEI_LB2_HDR_FLAG_RSP)
 * @reserved: Reserved for future use by authentication firmware, must be set to 0
 */
struct mei_lb2_header {
	__le32 command_id;
	u8 flags;
	u8 reserved[3];
};

/**
 * struct mei_lb2_rsp_header - Late Binding2 response header
 * @header: Common command header
 * @status: Status returned by authentication firmware (see &enum intel_lb_status)
 */
struct mei_lb2_rsp_header {
	struct mei_lb2_header header;
	__le32 status;
};

#define MEI_LB2_FLAG_FST_CHUNK 0x02
#define MEI_LB2_FLAG_LST_CHUNK 0x04

/**
 * struct mei_lb2_req - Late Binding2 request
 * @header: Common command header
 * @type: Type of the Late Binding payload (see &enum intel_lb_type)
 * @flags: Flags to be passed to the authentication firmware (MEI_LB2_FLAG_*)
 * @reserved: Reserved for future use by authentication firmware, must be set to 0
 * @total_payload_size: Size of whole Late Binding package in bytes
 * @payload_size: Size of the payload chunk in bytes
 * @payload: Data chunk to be sent to the authentication firmware
 */
struct mei_lb2_req {
	struct mei_lb2_header header;
	__le32 type;
	__le32 flags;
	__le32 reserved;
	__le32 total_payload_size;
	__le32 payload_size;
	u8 payload[] __counted_by(payload_size);
};

/**
 * struct mei_lb2_rsp - Late Binding2 response
 * @rheader: Common response header
 * @type: Type of the Late Binding payload (see &enum intel_lb_type)
 * @reserved: Reserved for future use by authentication firmware, must be set to 0
 */
struct mei_lb2_rsp {
	struct mei_lb2_rsp_header rheader;
	__le32 type;
	__le32 reserved[2];
};

static bool mei_lb_check_response_v1(const struct device *dev, ssize_t bytes,
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

static int mei_lb_push_payload_v1(struct device *dev, struct mei_cl_device *cldev,
				  u32 type, u32 flags, const void *payload, size_t payload_size)
{
	struct mei_lb_req *req = NULL;
	struct mei_lb_rsp rsp;
	size_t req_size;
	ssize_t bytes;
	int ret;

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
	if (!mei_lb_check_response_v1(dev, bytes, &rsp)) {
		dev_err(dev, "Bad response from the firmware. header: %02x %02x %02x %02x\n",
			rsp.header.group_id, rsp.header.command,
			rsp.header.reserved, rsp.header.result);
		ret = -EPROTO;
		goto end;
	}

	dev_dbg(dev, "status = %u\n", le32_to_cpu(rsp.status));
	ret = (int)le32_to_cpu(rsp.status);
end:
	kfree(req);
	return ret;
}

static int mei_lb_check_response_v2(const struct device *dev, ssize_t bytes,
				    struct mei_lb2_rsp *rsp)
{
	/*
	 * Received message size may be smaller than the full message size when
	 * reply contains only header with status field set to the error code.
	 * Check the header size and content first to output exact error, if needed,
	 * and then process to the whole message.
	 */
	if (bytes < sizeof(rsp->rheader)) {
		dev_err(dev, "Received less than header size from the firmware: %zd < %zu\n",
			bytes, sizeof(rsp->rheader));
		return -ENOMSG;
	}
	if (rsp->rheader.header.command_id != cpu_to_le32(MEI_LB2_CMD)) {
		dev_err(dev, "Mismatch command: 0x%x instead of 0x%x\n",
			rsp->rheader.header.command_id, MEI_LB2_CMD);
		return -EPROTO;
	}
	if (!(rsp->rheader.header.flags & MEI_LB2_HDR_FLAG_RSP)) {
		dev_err(dev, "Not a response: 0x%x\n", rsp->rheader.header.flags);
		return -EBADMSG;
	}
	if (rsp->rheader.status) {
		dev_err(dev, "Error in result: 0x%x\n", rsp->rheader.status);
		return (int)le32_to_cpu(rsp->rheader.status);
	}
	if (bytes < sizeof(*rsp)) {
		dev_err(dev, "Received less than message size from the firmware: %zd < %zu\n",
			bytes, sizeof(*rsp));
		return -ENODATA;
	}

	return 0;
}

static int mei_lb_push_payload_v2(struct device *dev, struct mei_cl_device *cldev,
				  u32 type, u32 flags, const void *payload, size_t payload_size)
{
	u32 first_chunk, last_chunk;
	struct mei_lb2_rsp rsp;
	size_t sent_data = 0;
	size_t chunk_size;
	size_t req_size;
	ssize_t bytes;
	int ret;

	struct mei_lb2_req *req __free(kfree) = kzalloc(mei_cldev_mtu(cldev), GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	first_chunk = MEI_LB2_FLAG_FST_CHUNK;
	last_chunk = 0;
	do {
		chunk_size = min(payload_size - sent_data, mei_cldev_mtu(cldev) - sizeof(*req));

		req_size = struct_size(req, payload, chunk_size);
		if (sent_data + chunk_size == payload_size)
			last_chunk = MEI_LB2_FLAG_LST_CHUNK;

		req->header.command_id = cpu_to_le32(MEI_LB2_CMD);
		req->type = cpu_to_le32(type);
		req->flags = cpu_to_le32(flags | first_chunk | last_chunk);
		req->reserved = 0;
		req->total_payload_size = cpu_to_le32(payload_size);
		req->payload_size = cpu_to_le32(chunk_size);
		memcpy(req->payload, payload + sent_data, chunk_size);

		dev_dbg(dev, "Sending %zu bytes from offset %zu of %zu%s%s\n",
			chunk_size, sent_data, payload_size,
			first_chunk ? " first" : "", last_chunk ? " last" : "");

		bytes = mei_cldev_send_timeout(cldev, (u8 *)req, req_size,
					       INTEL_LB_SEND_TIMEOUT_MSEC);
		if (bytes < 0) {
			dev_err(dev, "Failed to send late binding request to firmware. %zd\n",
				bytes);
			return bytes;
		}

		bytes = mei_cldev_recv_timeout(cldev, (u8 *)&rsp, sizeof(rsp),
					       INTEL_LB_RECV_TIMEOUT_MSEC);
		if (bytes < 0) {
			dev_err(dev, "Failed to receive late binding reply from firmware. %zd\n",
				bytes);
			return bytes;
		}
		ret = mei_lb_check_response_v2(dev, bytes, &rsp);
		if (ret)
			return ret;

		/* prepare for the next chunk */
		sent_data += chunk_size;
		first_chunk = 0;
	} while (!last_chunk);

	return 0;
}

static int mei_lb_push_payload(struct device *dev, u32 type, u32 flags,
			       const void *payload, size_t payload_size)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret) {
		dev_dbg(dev, "Failed to enable firmware client. %d\n", ret);
		return ret;
	}

	if (memcmp(&MEI_GUID_LB, mei_cldev_uuid(cldev), sizeof(uuid_le)) == 0)
		ret = mei_lb_push_payload_v2(dev, cldev, type, flags, payload, payload_size);
	else
		ret = mei_lb_push_payload_v1(dev, cldev, type, flags, payload, payload_size);

	mei_cldev_disable(cldev);
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
	 * This function checks if requester is Intel vendor,
	 * determines if MEI is standalone PCI device or the auxiliary one
	 * and checks the following:
	 * 0) PCI parent: (e.g. /sys/class/mei/mei0/device -> ../../../0000:15:00.0)
	 *  the requester and MEI device has the same grand parent
	 * 1) Auxiliary parent: (e.g. /sys/class/mei/mei1/device -> ../../../xe.mei-gscfi.768)
	 *  the requester is the parent of MEI device
	 */
	struct device *base = data;
	struct device *basep = dev;
	struct pci_dev *pdev;

	if (!dev)
		return 0;

	if (!dev_is_pci(dev))
		return 0;

	pdev = to_pci_dev(dev);

	if (pdev->vendor != PCI_VENDOR_ID_INTEL)
		return 0;

	if (subcomponent != INTEL_COMPONENT_LB)
		return 0;

	base = base->parent;
	if (!base) /* MEI device */
		return 0;

	if (dev_is_pci(base)) {
		/* case 0) PCI parent */
		base = base->parent; /* bridge 1 */
		if (!base)
			return 0;
		base = base->parent; /* bridge 2 */

		basep = basep->parent; /* bridge 1 */
		if (!basep)
			return 0;
		basep = basep->parent; /* bridge 2 */
	} else {
		/* case 1) Auxiliary parent */
		base = base->parent; /* PCI device */
	}

	return !!base && !!basep && base == basep;
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

static const struct mei_cl_device_id mei_lb_tbl[] = {
	{ .uuid = MEI_GUID_MKHI, .version = 1 },
	{ .uuid = MEI_GUID_LB, .version = MEI_CL_VERSION_ANY },
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
