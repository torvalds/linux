// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ixd.h"
#include "ixd_devlink.h"

#define IXD_DEVLINK_INFO_LEN	128

/**
 * ixd_fill_dsn - Get the serial number for the ixd device
 * @adapter: adapter to query
 * @buf: storage buffer for the info request
 */
static void ixd_fill_dsn(struct ixd_adapter *adapter, char *buf)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(adapter->cp_ctx.mmio_info.pdev), dsn);

	snprintf(buf, IXD_DEVLINK_INFO_LEN, "%8phD", dsn);
}

/**
 * ixd_fill_device_name - Get the name of the underlying hardware
 * @adapter: adapter to query
 * @buf: storage buffer for the info request
 * @buf_size: size of the storage buffer
 */
static void ixd_fill_device_name(struct ixd_adapter *adapter, char *buf,
				 size_t buf_size)
{
	if (adapter->caps.device_type == cpu_to_le32(VIRTCHNL2_MEV_DEVICE))
		snprintf(buf, buf_size, "%s", "MEV");
	else
		snprintf(buf, buf_size, "%s", "UNKNOWN");
}

/**
 * ixd_devlink_info_get - .info_get devlink handler
 * @devlink: devlink instance structure
 * @req: the devlink info request
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .info_get operation. Reports information about the
 * device.
 *
 * Return: zero on success or an error code on failure.
 */
static int ixd_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct ixd_adapter *adapter = devlink_priv(devlink);
	char buf[IXD_DEVLINK_INFO_LEN];
	int err;

	ixd_fill_dsn(adapter, buf);
	err = devlink_info_serial_number_put(req, buf);
	if (err)
		return err;

	ixd_fill_device_name(adapter, buf, IXD_DEVLINK_INFO_LEN);
	err = devlink_info_version_fixed_put(req, "device.type", buf);
	if (err)
		return err;

	snprintf(buf, sizeof(buf), "%u.%u",
		 adapter->vc_ver.major, adapter->vc_ver.minor);

	return devlink_info_version_running_put(req,
						DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
						buf);
}

static const struct devlink_ops ixd_devlink_ops = {
	.info_get = ixd_devlink_info_get,
};

/**
 * ixd_adapter_alloc - Allocate devlink and return adapter pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private area as
 * the adapter structure.
 *
 * Return: adapter structure on success, NULL on failure
 */
struct ixd_adapter *ixd_adapter_alloc(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&ixd_devlink_ops, sizeof(struct ixd_adapter),
				dev);
	if (!devlink)
		return NULL;

	return devlink_priv(devlink);
}
