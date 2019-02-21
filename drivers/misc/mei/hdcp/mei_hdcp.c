// SPDX-License-Identifier: (GPL-2.0)
/*
 * Copyright © 2019 Intel Corporation
 *
 * Mei_hdcp.c: HDCP client driver for mei bus
 *
 * Author:
 * Ramalingam C <ramalingam.c@intel.com>
 */

/**
 * DOC: MEI_HDCP Client Driver
 *
 * This is a client driver to the mei_bus to make the HDCP2.2 services of
 * ME FW available for the interested consumers like I915.
 *
 * This module will act as a translation layer between HDCP protocol
 * implementor(I915) and ME FW by translating HDCP2.2 authentication
 * messages to ME FW command payloads and vice versa.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/mei_cl_bus.h>
#include <drm/drm_connector.h>
#include <drm/i915_component.h>
#include <drm/i915_mei_hdcp_interface.h>

#include "mei_hdcp.h"

static inline u8 mei_get_ddi_index(enum port port)
{
	switch (port) {
	case PORT_A:
		return MEI_DDI_A;
	case PORT_B ... PORT_F:
		return (u8)port;
	default:
		return MEI_DDI_INVALID_PORT;
	}
}

/**
 * mei_hdcp_initiate_session() - Initiate a Wired HDCP2.2 Tx Session in ME FW
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @ake_data: AKE_Init msg output.
 *
 * Return:  0 on Success, <0 on Failure.
 */
static int
mei_hdcp_initiate_session(struct device *dev, struct hdcp_port_data *data,
			  struct hdcp2_ake_init *ake_data)
{
	struct wired_cmd_initiate_hdcp2_session_in session_init_in = { { 0 } };
	struct wired_cmd_initiate_hdcp2_session_out
						session_init_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !ake_data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	session_init_in.header.api_version = HDCP_API_VERSION;
	session_init_in.header.command_id = WIRED_INITIATE_HDCP2_SESSION;
	session_init_in.header.status = ME_HDCP_STATUS_SUCCESS;
	session_init_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_INITIATE_HDCP2_SESSION_IN;

	session_init_in.port.integrated_port_type = data->port_type;
	session_init_in.port.physical_port = mei_get_ddi_index(data->port);
	session_init_in.protocol = data->protocol;

	byte = mei_cldev_send(cldev, (u8 *)&session_init_in,
			      sizeof(session_init_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&session_init_out,
			      sizeof(session_init_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (session_init_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X Failed. Status: 0x%X\n",
			WIRED_INITIATE_HDCP2_SESSION,
			session_init_out.header.status);
		return -EIO;
	}

	ake_data->msg_id = HDCP_2_2_AKE_INIT;
	ake_data->tx_caps = session_init_out.tx_caps;
	memcpy(ake_data->r_tx, session_init_out.r_tx, HDCP_2_2_RTX_LEN);

	return 0;
}

static const __attribute__((unused))
struct i915_hdcp_component_ops mei_hdcp_ops = {
	.owner = THIS_MODULE,
	.initiate_hdcp2_session = mei_hdcp_initiate_session,
	.verify_receiver_cert_prepare_km = NULL,
	.verify_hprime = NULL,
	.store_pairing_info = NULL,
	.initiate_locality_check = NULL,
	.verify_lprime = NULL,
	.get_session_key = NULL,
	.repeater_check_flow_prepare_ack = NULL,
	.verify_mprime = NULL,
	.enable_hdcp_authentication = NULL,
	.close_hdcp_session = NULL,
};

static int mei_hdcp_probe(struct mei_cl_device *cldev,
			  const struct mei_cl_device_id *id)
{
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret < 0)
		dev_err(&cldev->dev, "mei_cldev_enable Failed. %d\n", ret);

	return ret;
}

static int mei_hdcp_remove(struct mei_cl_device *cldev)
{
	return mei_cldev_disable(cldev);
}

#define MEI_UUID_HDCP GUID_INIT(0xB638AB7E, 0x94E2, 0x4EA2, 0xA5, \
				0x52, 0xD1, 0xC5, 0x4B, 0x62, 0x7F, 0x04)

static struct mei_cl_device_id mei_hdcp_tbl[] = {
	{ .uuid = MEI_UUID_HDCP, .version = MEI_CL_VERSION_ANY },
	{ }
};
MODULE_DEVICE_TABLE(mei, mei_hdcp_tbl);

static struct mei_cl_driver mei_hdcp_driver = {
	.id_table = mei_hdcp_tbl,
	.name = KBUILD_MODNAME,
	.probe = mei_hdcp_probe,
	.remove	= mei_hdcp_remove,
};

module_mei_cl_driver(mei_hdcp_driver);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MEI HDCP");
