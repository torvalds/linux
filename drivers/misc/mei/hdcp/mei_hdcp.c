// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Intel Corporation
 *
 * mei_hdcp.c: HDCP client driver for mei bus
 *
 * Author:
 * Ramalingam C <ramalingam.c@intel.com>
 */

/**
 * DOC: MEI_HDCP Client Driver
 *
 * The mei_hdcp driver acts as a translation layer between HDCP 2.2
 * protocol  implementer (I915) and ME FW by translating HDCP2.2
 * negotiation messages to ME FW command payloads and vice versa.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/mei_cl_bus.h>
#include <linux/component.h>
#include <drm/drm_connector.h>
#include <drm/i915_component.h>
#include <drm/i915_mei_hdcp_interface.h>

#include "mei_hdcp.h"

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
	session_init_in.port.physical_port = (u8)data->fw_ddi;
	session_init_in.port.attached_transcoder = (u8)data->fw_tc;
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
	verify_rxcert_in.port.physical_port = (u8)data->fw_ddi;
	verify_rxcert_in.port.attached_transcoder = (u8)data->fw_tc;

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
	send_hprime_in.port.physical_port = (u8)data->fw_ddi;
	send_hprime_in.port.attached_transcoder = (u8)data->fw_tc;

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
	pairing_info_in.port.physical_port = (u8)data->fw_ddi;
	pairing_info_in.port.attached_transcoder = (u8)data->fw_tc;

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
	lc_init_in.port.physical_port = (u8)data->fw_ddi;
	lc_init_in.port.attached_transcoder = (u8)data->fw_tc;

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

/**
 * mei_hdcp_verify_lprime() - Verify lprime.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @rx_lprime: LC_Send_L_prime msg for ME FW verification
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_verify_lprime(struct device *dev, struct hdcp_port_data *data,
		       struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct wired_cmd_validate_locality_in verify_lprime_in = { { 0 } };
	struct wired_cmd_validate_locality_out verify_lprime_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !rx_lprime)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	verify_lprime_in.header.api_version = HDCP_API_VERSION;
	verify_lprime_in.header.command_id = WIRED_VALIDATE_LOCALITY;
	verify_lprime_in.header.status = ME_HDCP_STATUS_SUCCESS;
	verify_lprime_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_VALIDATE_LOCALITY_IN;

	verify_lprime_in.port.integrated_port_type = data->port_type;
	verify_lprime_in.port.physical_port = (u8)data->fw_ddi;
	verify_lprime_in.port.attached_transcoder = (u8)data->fw_tc;

	memcpy(verify_lprime_in.l_prime, rx_lprime->l_prime,
	       HDCP_2_2_L_PRIME_LEN);

	byte = mei_cldev_send(cldev, (u8 *)&verify_lprime_in,
			      sizeof(verify_lprime_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&verify_lprime_out,
			      sizeof(verify_lprime_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (verify_lprime_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. status: 0x%X\n",
			WIRED_VALIDATE_LOCALITY,
			verify_lprime_out.header.status);
		return -EIO;
	}

	return 0;
}

/**
 * mei_hdcp_get_session_key() - Prepare SKE_Send_Eks.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @ske_data: SKE_Send_Eks msg output from ME FW.
 *
 * Return: 0 on Success, <0 on Failure
 */
static int mei_hdcp_get_session_key(struct device *dev,
				    struct hdcp_port_data *data,
				    struct hdcp2_ske_send_eks *ske_data)
{
	struct wired_cmd_get_session_key_in get_skey_in = { { 0 } };
	struct wired_cmd_get_session_key_out get_skey_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data || !ske_data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	get_skey_in.header.api_version = HDCP_API_VERSION;
	get_skey_in.header.command_id = WIRED_GET_SESSION_KEY;
	get_skey_in.header.status = ME_HDCP_STATUS_SUCCESS;
	get_skey_in.header.buffer_len = WIRED_CMD_BUF_LEN_GET_SESSION_KEY_IN;

	get_skey_in.port.integrated_port_type = data->port_type;
	get_skey_in.port.physical_port = (u8)data->fw_ddi;
	get_skey_in.port.attached_transcoder = (u8)data->fw_tc;

	byte = mei_cldev_send(cldev, (u8 *)&get_skey_in, sizeof(get_skey_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&get_skey_out, sizeof(get_skey_out));

	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (get_skey_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. status: 0x%X\n",
			WIRED_GET_SESSION_KEY, get_skey_out.header.status);
		return -EIO;
	}

	ske_data->msg_id = HDCP_2_2_SKE_SEND_EKS;
	memcpy(ske_data->e_dkey_ks, get_skey_out.e_dkey_ks,
	       HDCP_2_2_E_DKEY_KS_LEN);
	memcpy(ske_data->riv, get_skey_out.r_iv, HDCP_2_2_RIV_LEN);

	return 0;
}

/**
 * mei_hdcp_repeater_check_flow_prepare_ack() - Validate the Downstream topology
 * and prepare rep_ack.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @rep_topology: Receiver ID List to be validated
 * @rep_send_ack : repeater ack from ME FW.
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_repeater_check_flow_prepare_ack(struct device *dev,
					 struct hdcp_port_data *data,
					 struct hdcp2_rep_send_receiverid_list
							*rep_topology,
					 struct hdcp2_rep_send_ack
							*rep_send_ack)
{
	struct wired_cmd_verify_repeater_in verify_repeater_in = { { 0 } };
	struct wired_cmd_verify_repeater_out verify_repeater_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !rep_topology || !rep_send_ack || !data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	verify_repeater_in.header.api_version = HDCP_API_VERSION;
	verify_repeater_in.header.command_id = WIRED_VERIFY_REPEATER;
	verify_repeater_in.header.status = ME_HDCP_STATUS_SUCCESS;
	verify_repeater_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_VERIFY_REPEATER_IN;

	verify_repeater_in.port.integrated_port_type = data->port_type;
	verify_repeater_in.port.physical_port = (u8)data->fw_ddi;
	verify_repeater_in.port.attached_transcoder = (u8)data->fw_tc;

	memcpy(verify_repeater_in.rx_info, rep_topology->rx_info,
	       HDCP_2_2_RXINFO_LEN);
	memcpy(verify_repeater_in.seq_num_v, rep_topology->seq_num_v,
	       HDCP_2_2_SEQ_NUM_LEN);
	memcpy(verify_repeater_in.v_prime, rep_topology->v_prime,
	       HDCP_2_2_V_PRIME_HALF_LEN);
	memcpy(verify_repeater_in.receiver_ids, rep_topology->receiver_ids,
	       HDCP_2_2_RECEIVER_IDS_MAX_LEN);

	byte = mei_cldev_send(cldev, (u8 *)&verify_repeater_in,
			      sizeof(verify_repeater_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&verify_repeater_out,
			      sizeof(verify_repeater_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (verify_repeater_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. status: 0x%X\n",
			WIRED_VERIFY_REPEATER,
			verify_repeater_out.header.status);
		return -EIO;
	}

	memcpy(rep_send_ack->v, verify_repeater_out.v,
	       HDCP_2_2_V_PRIME_HALF_LEN);
	rep_send_ack->msg_id = HDCP_2_2_REP_SEND_ACK;

	return 0;
}

/**
 * mei_hdcp_verify_mprime() - Verify mprime.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 * @stream_ready: RepeaterAuth_Stream_Ready msg for ME FW verification.
 *
 * Return: 0 on Success, <0 on Failure
 */
static int mei_hdcp_verify_mprime(struct device *dev,
				  struct hdcp_port_data *data,
				  struct hdcp2_rep_stream_ready *stream_ready)
{
	struct wired_cmd_repeater_auth_stream_req_in *verify_mprime_in;
	struct wired_cmd_repeater_auth_stream_req_out
					verify_mprime_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;
	size_t cmd_size;

	if (!dev || !stream_ready || !data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	cmd_size = struct_size(verify_mprime_in, streams, data->k);
	if (cmd_size == SIZE_MAX)
		return -EINVAL;

	verify_mprime_in = kzalloc(cmd_size, GFP_KERNEL);
	if (!verify_mprime_in)
		return -ENOMEM;

	verify_mprime_in->header.api_version = HDCP_API_VERSION;
	verify_mprime_in->header.command_id = WIRED_REPEATER_AUTH_STREAM_REQ;
	verify_mprime_in->header.status = ME_HDCP_STATUS_SUCCESS;
	verify_mprime_in->header.buffer_len = cmd_size  - sizeof(verify_mprime_in->header);

	verify_mprime_in->port.integrated_port_type = data->port_type;
	verify_mprime_in->port.physical_port = (u8)data->fw_ddi;
	verify_mprime_in->port.attached_transcoder = (u8)data->fw_tc;

	memcpy(verify_mprime_in->m_prime, stream_ready->m_prime, HDCP_2_2_MPRIME_LEN);
	drm_hdcp_cpu_to_be24(verify_mprime_in->seq_num_m, data->seq_num_m);

	memcpy(verify_mprime_in->streams, data->streams,
	       array_size(data->k, sizeof(*data->streams)));

	verify_mprime_in->k = cpu_to_be16(data->k);

	byte = mei_cldev_send(cldev, (u8 *)verify_mprime_in, cmd_size);
	kfree(verify_mprime_in);
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&verify_mprime_out,
			      sizeof(verify_mprime_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (verify_mprime_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. status: 0x%X\n",
			WIRED_REPEATER_AUTH_STREAM_REQ,
			verify_mprime_out.header.status);
		return -EIO;
	}

	return 0;
}

/**
 * mei_hdcp_enable_authentication() - Mark a port as authenticated
 * through ME FW
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 *
 * Return: 0 on Success, <0 on Failure
 */
static int mei_hdcp_enable_authentication(struct device *dev,
					  struct hdcp_port_data *data)
{
	struct wired_cmd_enable_auth_in enable_auth_in = { { 0 } };
	struct wired_cmd_enable_auth_out enable_auth_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	enable_auth_in.header.api_version = HDCP_API_VERSION;
	enable_auth_in.header.command_id = WIRED_ENABLE_AUTH;
	enable_auth_in.header.status = ME_HDCP_STATUS_SUCCESS;
	enable_auth_in.header.buffer_len = WIRED_CMD_BUF_LEN_ENABLE_AUTH_IN;

	enable_auth_in.port.integrated_port_type = data->port_type;
	enable_auth_in.port.physical_port = (u8)data->fw_ddi;
	enable_auth_in.port.attached_transcoder = (u8)data->fw_tc;
	enable_auth_in.stream_type = data->streams[0].stream_type;

	byte = mei_cldev_send(cldev, (u8 *)&enable_auth_in,
			      sizeof(enable_auth_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&enable_auth_out,
			      sizeof(enable_auth_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (enable_auth_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "ME cmd 0x%08X failed. status: 0x%X\n",
			WIRED_ENABLE_AUTH, enable_auth_out.header.status);
		return -EIO;
	}

	return 0;
}

/**
 * mei_hdcp_close_session() - Close the Wired HDCP Tx session of ME FW per port.
 * This also disables the authenticated state of the port.
 * @dev: device corresponding to the mei_cl_device
 * @data: Intel HW specific hdcp data
 *
 * Return: 0 on Success, <0 on Failure
 */
static int
mei_hdcp_close_session(struct device *dev, struct hdcp_port_data *data)
{
	struct wired_cmd_close_session_in session_close_in = { { 0 } };
	struct wired_cmd_close_session_out session_close_out = { { 0 } };
	struct mei_cl_device *cldev;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	cldev = to_mei_cl_device(dev);

	session_close_in.header.api_version = HDCP_API_VERSION;
	session_close_in.header.command_id = WIRED_CLOSE_SESSION;
	session_close_in.header.status = ME_HDCP_STATUS_SUCCESS;
	session_close_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_CLOSE_SESSION_IN;

	session_close_in.port.integrated_port_type = data->port_type;
	session_close_in.port.physical_port = (u8)data->fw_ddi;
	session_close_in.port.attached_transcoder = (u8)data->fw_tc;

	byte = mei_cldev_send(cldev, (u8 *)&session_close_in,
			      sizeof(session_close_in));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_send failed. %zd\n", byte);
		return byte;
	}

	byte = mei_cldev_recv(cldev, (u8 *)&session_close_out,
			      sizeof(session_close_out));
	if (byte < 0) {
		dev_dbg(dev, "mei_cldev_recv failed. %zd\n", byte);
		return byte;
	}

	if (session_close_out.header.status != ME_HDCP_STATUS_SUCCESS) {
		dev_dbg(dev, "Session Close Failed. status: 0x%X\n",
			session_close_out.header.status);
		return -EIO;
	}

	return 0;
}

static const struct i915_hdcp_component_ops mei_hdcp_ops = {
	.owner = THIS_MODULE,
	.initiate_hdcp2_session = mei_hdcp_initiate_session,
	.verify_receiver_cert_prepare_km =
				mei_hdcp_verify_receiver_cert_prepare_km,
	.verify_hprime = mei_hdcp_verify_hprime,
	.store_pairing_info = mei_hdcp_store_pairing_info,
	.initiate_locality_check = mei_hdcp_initiate_locality_check,
	.verify_lprime = mei_hdcp_verify_lprime,
	.get_session_key = mei_hdcp_get_session_key,
	.repeater_check_flow_prepare_ack =
				mei_hdcp_repeater_check_flow_prepare_ack,
	.verify_mprime = mei_hdcp_verify_mprime,
	.enable_hdcp_authentication = mei_hdcp_enable_authentication,
	.close_hdcp_session = mei_hdcp_close_session,
};

static int mei_component_master_bind(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct i915_hdcp_comp_master *comp_master =
						mei_cldev_get_drvdata(cldev);
	int ret;

	dev_dbg(dev, "%s\n", __func__);
	comp_master->ops = &mei_hdcp_ops;
	comp_master->mei_dev = dev;
	ret = component_bind_all(dev, comp_master);
	if (ret < 0)
		return ret;

	return 0;
}

static void mei_component_master_unbind(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct i915_hdcp_comp_master *comp_master =
						mei_cldev_get_drvdata(cldev);

	dev_dbg(dev, "%s\n", __func__);
	component_unbind_all(dev, comp_master);
}

static const struct component_master_ops mei_component_master_ops = {
	.bind = mei_component_master_bind,
	.unbind = mei_component_master_unbind,
};

/**
 * mei_hdcp_component_match - compare function for matching mei hdcp.
 *
 *    The function checks if the driver is i915, the subcomponent is HDCP
 *    and the grand parent of hdcp and the parent of i915 are the same
 *    PCH device.
 *
 * @dev: master device
 * @subcomponent: subcomponent to match (I915_COMPONENT_HDCP)
 * @data: compare data (mei hdcp device)
 *
 * Return:
 * * 1 - if components match
 * * 0 - otherwise
 */
static int mei_hdcp_component_match(struct device *dev, int subcomponent,
				    void *data)
{
	struct device *base = data;

	if (strcmp(dev->driver->name, "i915") ||
	    subcomponent != I915_COMPONENT_HDCP)
		return 0;

	base = base->parent;
	if (!base)
		return 0;

	base = base->parent;
	dev = dev->parent;

	return (base && dev && dev == base);
}

static int mei_hdcp_probe(struct mei_cl_device *cldev,
			  const struct mei_cl_device_id *id)
{
	struct i915_hdcp_comp_master *comp_master;
	struct component_match *master_match;
	int ret;

	ret = mei_cldev_enable(cldev);
	if (ret < 0) {
		dev_err(&cldev->dev, "mei_cldev_enable Failed. %d\n", ret);
		goto enable_err_exit;
	}

	comp_master = kzalloc(sizeof(*comp_master), GFP_KERNEL);
	if (!comp_master) {
		ret = -ENOMEM;
		goto err_exit;
	}

	master_match = NULL;
	component_match_add_typed(&cldev->dev, &master_match,
				  mei_hdcp_component_match, &cldev->dev);
	if (IS_ERR_OR_NULL(master_match)) {
		ret = -ENOMEM;
		goto err_exit;
	}

	mei_cldev_set_drvdata(cldev, comp_master);
	ret = component_master_add_with_match(&cldev->dev,
					      &mei_component_master_ops,
					      master_match);
	if (ret < 0) {
		dev_err(&cldev->dev, "Master comp add failed %d\n", ret);
		goto err_exit;
	}

	return 0;

err_exit:
	mei_cldev_set_drvdata(cldev, NULL);
	kfree(comp_master);
	mei_cldev_disable(cldev);
enable_err_exit:
	return ret;
}

static int mei_hdcp_remove(struct mei_cl_device *cldev)
{
	struct i915_hdcp_comp_master *comp_master =
						mei_cldev_get_drvdata(cldev);

	component_master_del(&cldev->dev, &mei_component_master_ops);
	kfree(comp_master);
	mei_cldev_set_drvdata(cldev, NULL);

	return mei_cldev_disable(cldev);
}

#define MEI_UUID_HDCP GUID_INIT(0xB638AB7E, 0x94E2, 0x4EA2, 0xA5, \
				0x52, 0xD1, 0xC5, 0x4B, 0x62, 0x7F, 0x04)

static const struct mei_cl_device_id mei_hdcp_tbl[] = {
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
