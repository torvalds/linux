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

/**
 * mei_hdcp_verify_receiver_cert_prepare_km() - Verify the Receiver Certificate
 * AKE_Send_Cert and prepare AKE_Stored_Km/AKE_No_Stored_Km
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @rx_cert: AKE_Send_Cert for verification
 * @km_stored: Pairing status flag output
 * @ek_pub_km: AKE_Stored_Km/AKE_No_Stored_Km output msg
 * @msg_sz : size of AKE_XXXXX_Km output msg
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_verify_receiver_cert_prepare_km(struct device *dev,
					 struct hdcp_port_data *data,
					 struct hdcp2_ake_send_cert *rx_cert,
					 bool *km_stored,
					 struct hdcp2_ake_no_stored_km
								*ek_pub_km,
					 size_t *msg_sz)
{
	struct wired_cmd_verify_receiver_cert_in verify_rxcert_in = { { 0 } };
	struct wired_cmd_verify_receiver_cert_out verify_rxcert_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !rx_cert || !km_stored || !ek_pub_km || !msg_sz)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	verify_rxcert_in.header.api_version = HDCP_API_VERSION;
	verify_rxcert_in.header.command_id = WIRED_VERIFY_RECEIVER_CERT;
	verify_rxcert_in.header.status = ME_HDCP_STATUS_SUCCESS;
	verify_rxcert_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_VERIFY_RECEIVER_CERT_IN;

	verify_rxcert_in.port.integrated_port_type = data->port_type;
	verify_rxcert_in.port.physical_port = mei_get_ddi_index(data->port);

	verify_rxcert_in.cert_rx = rx_cert->cert_rx;
	memcpy(verify_rxcert_in.r_rx, &rx_cert->r_rx, HDCP_2_2_RRX_LEN);
	memcpy(verify_rxcert_in.rx_caps, rx_cert->rx_caps, HDCP_2_2_RXCAPS_LEN);

	byte = mei_cldev_send(cldev, (u8 *)&verify_rxcert_in,
			      sizeof(verify_rxcert_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed: %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&verify_rxcert_out,
			      sizeof(verify_rxcert_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed: %zd\n", byte);
		return byte;
	}

	if (verify_rxcert_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X Failed. Status: 0x%X\n",
			WIRED_VERIFY_RECEIVER_CERT,
			verify_rxcert_out.header.status);
		return -EIO;
	}

	*km_stored = !!verify_rxcert_out.km_stored;
	if (verify_rxcert_out.km_stored) {
		ek_pub_km->msg_id = HDCP_2_2_AKE_STORED_KM;
		*msg_sz = sizeof(struct hdcp2_ake_stored_km);
	} else {
		ek_pub_km->msg_id = HDCP_2_2_AKE_NO_STORED_KM;
		*msg_sz = sizeof(struct hdcp2_ake_no_stored_km);
	}

	memcpy(ek_pub_km->e_kpub_km, &verify_rxcert_out.ekm_buff,
	       sizeof(verify_rxcert_out.ekm_buff));

	return 0;
}

/**
 * mei_hdcp_verify_hprime() - Verify AKE_Send_H_prime at ME FW.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @rx_hprime: AKE_Send_H_prime msg for ME FW verification
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_verify_hprime(struct device *dev, struct hdcp_port_data *data,
		       struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct wired_cmd_ake_send_hprime_in send_hprime_in = { { 0 } };
	struct wired_cmd_ake_send_hprime_out send_hprime_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !rx_hprime)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	send_hprime_in.header.api_version = HDCP_API_VERSION;
	send_hprime_in.header.command_id = WIRED_AKE_SEND_HPRIME;
	send_hprime_in.header.status = ME_HDCP_STATUS_SUCCESS;
	send_hprime_in.header.buffer_len = WIRED_CMD_BUF_LEN_AKE_SEND_HPRIME_IN;

	send_hprime_in.port.integrated_port_type = data->port_type;
	send_hprime_in.port.physical_port = mei_get_ddi_index(data->port);

	memcpy(send_hprime_in.h_prime, rx_hprime->h_prime,
	       HDCP_2_2_H_PRIME_LEN);

	byte = mei_cldev_send(cldev, (u8 *)&send_hprime_in,
			      sizeof(send_hprime_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&send_hprime_out,
			      sizeof(send_hprime_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (send_hprime_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X Failed. Status: 0x%X\n",
			WIRED_AKE_SEND_HPRIME, send_hprime_out.header.status);
		return -EIO;
	}

	return 0;
}

/**
 * mei_hdcp_store_pairing_info() - Store pairing info received at ME FW
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @pairing_info: AKE_Send_Pairing_Info msg input to ME FW
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_store_pairing_info(struct device *dev, struct hdcp_port_data *data,
			    struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct wired_cmd_ake_send_pairing_info_in pairing_info_in = { { 0 } };
	struct wired_cmd_ake_send_pairing_info_out pairing_info_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !pairing_info)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	pairing_info_in.header.api_version = HDCP_API_VERSION;
	pairing_info_in.header.command_id = WIRED_AKE_SEND_PAIRING_INFO;
	pairing_info_in.header.status = ME_HDCP_STATUS_SUCCESS;
	pairing_info_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_SEND_PAIRING_INFO_IN;

	pairing_info_in.port.integrated_port_type = data->port_type;
	pairing_info_in.port.physical_port = mei_get_ddi_index(data->port);

	memcpy(pairing_info_in.e_kh_km, pairing_info->e_kh_km,
	       HDCP_2_2_E_KH_KM_LEN);

	byte = mei_cldev_send(cldev, (u8 *)&pairing_info_in,
			      sizeof(pairing_info_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&pairing_info_out,
			      sizeof(pairing_info_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (pairing_info_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. Status: 0x%X\n",
			WIRED_AKE_SEND_PAIRING_INFO,
			pairing_info_out.header.status);
		return -EIO;
	}

	return 0;
}

/**
 * mei_hdcp_initiate_locality_check() - Prepare LC_Init
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @lc_init_data: LC_Init msg output
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_initiate_locality_check(struct device *dev,
				 struct hdcp_port_data *data,
				 struct hdcp2_lc_init *lc_init_data)
{
	struct wired_cmd_init_locality_check_in lc_init_in = { { 0 } };
	struct wired_cmd_init_locality_check_out lc_init_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !lc_init_data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	lc_init_in.header.api_version = HDCP_API_VERSION;
	lc_init_in.header.command_id = WIRED_INIT_LOCALITY_CHECK;
	lc_init_in.header.status = ME_HDCP_STATUS_SUCCESS;
	lc_init_in.header.buffer_len = WIRED_CMD_BUF_LEN_INIT_LOCALITY_CHECK_IN;

	lc_init_in.port.integrated_port_type = data->port_type;
	lc_init_in.port.physical_port = mei_get_ddi_index(data->port);

	byte = mei_cldev_send(cldev, (u8 *)&lc_init_in, sizeof(lc_init_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&lc_init_out, sizeof(lc_init_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (lc_init_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X Failed. status: 0x%X\n",
			WIRED_INIT_LOCALITY_CHECK, lc_init_out.header.status);
		return -EIO;
	}

	lc_init_data->msg_id = HDCP_2_2_LC_INIT;
	memcpy(lc_init_data->r_n, lc_init_out.r_n, HDCP_2_2_RN_LEN);

	return 0;
}

static const __attribute__((unused))
struct i915_hdcp_component_ops mei_hdcp_ops = {
	.owner = THIS_MODULE,
	.initiate_hdcp2_session = mei_hdcp_initiate_session,
	.verify_receiver_cert_prepare_km =
				mei_hdcp_verify_receiver_cert_prepare_km,
	.verify_hprime = mei_hdcp_verify_hprime,
	.store_pairing_info = mei_hdcp_store_pairing_info,
	.initiate_locality_check = mei_hdcp_initiate_locality_check,
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
