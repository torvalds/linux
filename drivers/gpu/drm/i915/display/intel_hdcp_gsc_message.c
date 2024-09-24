// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <linux/err.h>
#include <drm/intel/i915_hdcp_interface.h>

#include "i915_drv.h"
#include "intel_display_types.h"
#include "intel_hdcp_gsc_message.h"

int
intel_hdcp_gsc_initiate_session(struct device *dev, struct hdcp_port_data *data,
				struct hdcp2_ake_init *ake_data)
{
	struct wired_cmd_initiate_hdcp2_session_in session_init_in = {};
	struct wired_cmd_initiate_hdcp2_session_out session_init_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !ake_data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	session_init_in.header.api_version = HDCP_API_VERSION;
	session_init_in.header.command_id = WIRED_INITIATE_HDCP2_SESSION;
	session_init_in.header.status = FW_HDCP_STATUS_SUCCESS;
	session_init_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_INITIATE_HDCP2_SESSION_IN;

	session_init_in.port.integrated_port_type = data->port_type;
	session_init_in.port.physical_port = (u8)data->hdcp_ddi;
	session_init_in.port.attached_transcoder = (u8)data->hdcp_transcoder;
	session_init_in.protocol = data->protocol;

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&session_init_in,
				       sizeof(session_init_in),
				       (u8 *)&session_init_out,
				       sizeof(session_init_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (session_init_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X Failed. Status: 0x%X\n",
			    WIRED_INITIATE_HDCP2_SESSION,
			    session_init_out.header.status);
		return -EIO;
	}

	ake_data->msg_id = HDCP_2_2_AKE_INIT;
	ake_data->tx_caps = session_init_out.tx_caps;
	memcpy(ake_data->r_tx, session_init_out.r_tx, HDCP_2_2_RTX_LEN);

	return 0;
}

int
intel_hdcp_gsc_verify_receiver_cert_prepare_km(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_ake_send_cert *rx_cert,
					       bool *km_stored,
					       struct hdcp2_ake_no_stored_km
					       *ek_pub_km,
					       size_t *msg_sz)
{
	struct wired_cmd_verify_receiver_cert_in verify_rxcert_in = {};
	struct wired_cmd_verify_receiver_cert_out verify_rxcert_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_cert || !km_stored || !ek_pub_km || !msg_sz)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	verify_rxcert_in.header.api_version = HDCP_API_VERSION;
	verify_rxcert_in.header.command_id = WIRED_VERIFY_RECEIVER_CERT;
	verify_rxcert_in.header.status = FW_HDCP_STATUS_SUCCESS;
	verify_rxcert_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_VERIFY_RECEIVER_CERT_IN;

	verify_rxcert_in.port.integrated_port_type = data->port_type;
	verify_rxcert_in.port.physical_port = (u8)data->hdcp_ddi;
	verify_rxcert_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	verify_rxcert_in.cert_rx = rx_cert->cert_rx;
	memcpy(verify_rxcert_in.r_rx, &rx_cert->r_rx, HDCP_2_2_RRX_LEN);
	memcpy(verify_rxcert_in.rx_caps, rx_cert->rx_caps, HDCP_2_2_RXCAPS_LEN);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&verify_rxcert_in,
				       sizeof(verify_rxcert_in),
				       (u8 *)&verify_rxcert_out,
				       sizeof(verify_rxcert_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed: %zd\n", byte);
		return byte;
	}

	if (verify_rxcert_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X Failed. Status: 0x%X\n",
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

int
intel_hdcp_gsc_verify_hprime(struct device *dev, struct hdcp_port_data *data,
			     struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct wired_cmd_ake_send_hprime_in send_hprime_in = {};
	struct wired_cmd_ake_send_hprime_out send_hprime_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_hprime)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	send_hprime_in.header.api_version = HDCP_API_VERSION;
	send_hprime_in.header.command_id = WIRED_AKE_SEND_HPRIME;
	send_hprime_in.header.status = FW_HDCP_STATUS_SUCCESS;
	send_hprime_in.header.buffer_len = WIRED_CMD_BUF_LEN_AKE_SEND_HPRIME_IN;

	send_hprime_in.port.integrated_port_type = data->port_type;
	send_hprime_in.port.physical_port = (u8)data->hdcp_ddi;
	send_hprime_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	memcpy(send_hprime_in.h_prime, rx_hprime->h_prime,
	       HDCP_2_2_H_PRIME_LEN);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&send_hprime_in,
				       sizeof(send_hprime_in),
				       (u8 *)&send_hprime_out,
				       sizeof(send_hprime_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (send_hprime_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X Failed. Status: 0x%X\n",
			    WIRED_AKE_SEND_HPRIME, send_hprime_out.header.status);
		return -EIO;
	}

	return 0;
}

int
intel_hdcp_gsc_store_pairing_info(struct device *dev, struct hdcp_port_data *data,
				  struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct wired_cmd_ake_send_pairing_info_in pairing_info_in = {};
	struct wired_cmd_ake_send_pairing_info_out pairing_info_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !pairing_info)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	pairing_info_in.header.api_version = HDCP_API_VERSION;
	pairing_info_in.header.command_id = WIRED_AKE_SEND_PAIRING_INFO;
	pairing_info_in.header.status = FW_HDCP_STATUS_SUCCESS;
	pairing_info_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_SEND_PAIRING_INFO_IN;

	pairing_info_in.port.integrated_port_type = data->port_type;
	pairing_info_in.port.physical_port = (u8)data->hdcp_ddi;
	pairing_info_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	memcpy(pairing_info_in.e_kh_km, pairing_info->e_kh_km,
	       HDCP_2_2_E_KH_KM_LEN);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&pairing_info_in,
				       sizeof(pairing_info_in),
				       (u8 *)&pairing_info_out,
				       sizeof(pairing_info_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (pairing_info_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. Status: 0x%X\n",
			    WIRED_AKE_SEND_PAIRING_INFO,
			    pairing_info_out.header.status);
		return -EIO;
	}

	return 0;
}

int
intel_hdcp_gsc_initiate_locality_check(struct device *dev,
				       struct hdcp_port_data *data,
				       struct hdcp2_lc_init *lc_init_data)
{
	struct wired_cmd_init_locality_check_in lc_init_in = {};
	struct wired_cmd_init_locality_check_out lc_init_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !lc_init_data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	lc_init_in.header.api_version = HDCP_API_VERSION;
	lc_init_in.header.command_id = WIRED_INIT_LOCALITY_CHECK;
	lc_init_in.header.status = FW_HDCP_STATUS_SUCCESS;
	lc_init_in.header.buffer_len = WIRED_CMD_BUF_LEN_INIT_LOCALITY_CHECK_IN;

	lc_init_in.port.integrated_port_type = data->port_type;
	lc_init_in.port.physical_port = (u8)data->hdcp_ddi;
	lc_init_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&lc_init_in, sizeof(lc_init_in),
				       (u8 *)&lc_init_out, sizeof(lc_init_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (lc_init_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X Failed. status: 0x%X\n",
			    WIRED_INIT_LOCALITY_CHECK, lc_init_out.header.status);
		return -EIO;
	}

	lc_init_data->msg_id = HDCP_2_2_LC_INIT;
	memcpy(lc_init_data->r_n, lc_init_out.r_n, HDCP_2_2_RN_LEN);

	return 0;
}

int
intel_hdcp_gsc_verify_lprime(struct device *dev, struct hdcp_port_data *data,
			     struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct wired_cmd_validate_locality_in verify_lprime_in = {};
	struct wired_cmd_validate_locality_out verify_lprime_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_lprime)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	verify_lprime_in.header.api_version = HDCP_API_VERSION;
	verify_lprime_in.header.command_id = WIRED_VALIDATE_LOCALITY;
	verify_lprime_in.header.status = FW_HDCP_STATUS_SUCCESS;
	verify_lprime_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_VALIDATE_LOCALITY_IN;

	verify_lprime_in.port.integrated_port_type = data->port_type;
	verify_lprime_in.port.physical_port = (u8)data->hdcp_ddi;
	verify_lprime_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	memcpy(verify_lprime_in.l_prime, rx_lprime->l_prime,
	       HDCP_2_2_L_PRIME_LEN);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&verify_lprime_in,
				       sizeof(verify_lprime_in),
				       (u8 *)&verify_lprime_out,
				       sizeof(verify_lprime_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (verify_lprime_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. status: 0x%X\n",
			    WIRED_VALIDATE_LOCALITY,
			    verify_lprime_out.header.status);
		return -EIO;
	}

	return 0;
}

int intel_hdcp_gsc_get_session_key(struct device *dev,
				   struct hdcp_port_data *data,
				   struct hdcp2_ske_send_eks *ske_data)
{
	struct wired_cmd_get_session_key_in get_skey_in = {};
	struct wired_cmd_get_session_key_out get_skey_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !ske_data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	get_skey_in.header.api_version = HDCP_API_VERSION;
	get_skey_in.header.command_id = WIRED_GET_SESSION_KEY;
	get_skey_in.header.status = FW_HDCP_STATUS_SUCCESS;
	get_skey_in.header.buffer_len = WIRED_CMD_BUF_LEN_GET_SESSION_KEY_IN;

	get_skey_in.port.integrated_port_type = data->port_type;
	get_skey_in.port.physical_port = (u8)data->hdcp_ddi;
	get_skey_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&get_skey_in, sizeof(get_skey_in),
				       (u8 *)&get_skey_out, sizeof(get_skey_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (get_skey_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. status: 0x%X\n",
			    WIRED_GET_SESSION_KEY, get_skey_out.header.status);
		return -EIO;
	}

	ske_data->msg_id = HDCP_2_2_SKE_SEND_EKS;
	memcpy(ske_data->e_dkey_ks, get_skey_out.e_dkey_ks,
	       HDCP_2_2_E_DKEY_KS_LEN);
	memcpy(ske_data->riv, get_skey_out.r_iv, HDCP_2_2_RIV_LEN);

	return 0;
}

int
intel_hdcp_gsc_repeater_check_flow_prepare_ack(struct device *dev,
					       struct hdcp_port_data *data,
					       struct hdcp2_rep_send_receiverid_list
					       *rep_topology,
					       struct hdcp2_rep_send_ack
					       *rep_send_ack)
{
	struct wired_cmd_verify_repeater_in verify_repeater_in = {};
	struct wired_cmd_verify_repeater_out verify_repeater_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !rep_topology || !rep_send_ack || !data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	verify_repeater_in.header.api_version = HDCP_API_VERSION;
	verify_repeater_in.header.command_id = WIRED_VERIFY_REPEATER;
	verify_repeater_in.header.status = FW_HDCP_STATUS_SUCCESS;
	verify_repeater_in.header.buffer_len =
					WIRED_CMD_BUF_LEN_VERIFY_REPEATER_IN;

	verify_repeater_in.port.integrated_port_type = data->port_type;
	verify_repeater_in.port.physical_port = (u8)data->hdcp_ddi;
	verify_repeater_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	memcpy(verify_repeater_in.rx_info, rep_topology->rx_info,
	       HDCP_2_2_RXINFO_LEN);
	memcpy(verify_repeater_in.seq_num_v, rep_topology->seq_num_v,
	       HDCP_2_2_SEQ_NUM_LEN);
	memcpy(verify_repeater_in.v_prime, rep_topology->v_prime,
	       HDCP_2_2_V_PRIME_HALF_LEN);
	memcpy(verify_repeater_in.receiver_ids, rep_topology->receiver_ids,
	       HDCP_2_2_RECEIVER_IDS_MAX_LEN);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&verify_repeater_in,
				       sizeof(verify_repeater_in),
				       (u8 *)&verify_repeater_out,
				       sizeof(verify_repeater_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (verify_repeater_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. status: 0x%X\n",
			    WIRED_VERIFY_REPEATER,
			    verify_repeater_out.header.status);
		return -EIO;
	}

	memcpy(rep_send_ack->v, verify_repeater_out.v,
	       HDCP_2_2_V_PRIME_HALF_LEN);
	rep_send_ack->msg_id = HDCP_2_2_REP_SEND_ACK;

	return 0;
}

int intel_hdcp_gsc_verify_mprime(struct device *dev,
				 struct hdcp_port_data *data,
				 struct hdcp2_rep_stream_ready *stream_ready)
{
	struct wired_cmd_repeater_auth_stream_req_in *verify_mprime_in;
	struct wired_cmd_repeater_auth_stream_req_out verify_mprime_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;
	size_t cmd_size;

	if (!dev || !stream_ready || !data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	cmd_size = struct_size(verify_mprime_in, streams, data->k);
	if (cmd_size == SIZE_MAX)
		return -EINVAL;

	verify_mprime_in = kzalloc(cmd_size, GFP_KERNEL);
	if (!verify_mprime_in)
		return -ENOMEM;

	verify_mprime_in->header.api_version = HDCP_API_VERSION;
	verify_mprime_in->header.command_id = WIRED_REPEATER_AUTH_STREAM_REQ;
	verify_mprime_in->header.status = FW_HDCP_STATUS_SUCCESS;
	verify_mprime_in->header.buffer_len = cmd_size  - sizeof(verify_mprime_in->header);

	verify_mprime_in->port.integrated_port_type = data->port_type;
	verify_mprime_in->port.physical_port = (u8)data->hdcp_ddi;
	verify_mprime_in->port.attached_transcoder = (u8)data->hdcp_transcoder;

	memcpy(verify_mprime_in->m_prime, stream_ready->m_prime, HDCP_2_2_MPRIME_LEN);
	drm_hdcp_cpu_to_be24(verify_mprime_in->seq_num_m, data->seq_num_m);

	memcpy(verify_mprime_in->streams, data->streams,
	       array_size(data->k, sizeof(*data->streams)));

	verify_mprime_in->k = cpu_to_be16(data->k);

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)verify_mprime_in, cmd_size,
				       (u8 *)&verify_mprime_out,
				       sizeof(verify_mprime_out));
	kfree(verify_mprime_in);
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (verify_mprime_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. status: 0x%X\n",
			    WIRED_REPEATER_AUTH_STREAM_REQ,
			    verify_mprime_out.header.status);
		return -EIO;
	}

	return 0;
}

int intel_hdcp_gsc_enable_authentication(struct device *dev,
					 struct hdcp_port_data *data)
{
	struct wired_cmd_enable_auth_in enable_auth_in = {};
	struct wired_cmd_enable_auth_out enable_auth_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	enable_auth_in.header.api_version = HDCP_API_VERSION;
	enable_auth_in.header.command_id = WIRED_ENABLE_AUTH;
	enable_auth_in.header.status = FW_HDCP_STATUS_SUCCESS;
	enable_auth_in.header.buffer_len = WIRED_CMD_BUF_LEN_ENABLE_AUTH_IN;

	enable_auth_in.port.integrated_port_type = data->port_type;
	enable_auth_in.port.physical_port = (u8)data->hdcp_ddi;
	enable_auth_in.port.attached_transcoder = (u8)data->hdcp_transcoder;
	enable_auth_in.stream_type = data->streams[0].stream_type;

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&enable_auth_in,
				       sizeof(enable_auth_in),
				       (u8 *)&enable_auth_out,
				       sizeof(enable_auth_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (enable_auth_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "FW cmd 0x%08X failed. status: 0x%X\n",
			    WIRED_ENABLE_AUTH, enable_auth_out.header.status);
		return -EIO;
	}

	return 0;
}

int
intel_hdcp_gsc_close_session(struct device *dev, struct hdcp_port_data *data)
{
	struct wired_cmd_close_session_in session_close_in = {};
	struct wired_cmd_close_session_out session_close_out = {};
	struct intel_display *display;
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	display = to_intel_display(dev);
	if (!display) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}
	i915 = to_i915(display->drm);

	session_close_in.header.api_version = HDCP_API_VERSION;
	session_close_in.header.command_id = WIRED_CLOSE_SESSION;
	session_close_in.header.status = FW_HDCP_STATUS_SUCCESS;
	session_close_in.header.buffer_len =
				WIRED_CMD_BUF_LEN_CLOSE_SESSION_IN;

	session_close_in.port.integrated_port_type = data->port_type;
	session_close_in.port.physical_port = (u8)data->hdcp_ddi;
	session_close_in.port.attached_transcoder = (u8)data->hdcp_transcoder;

	byte = intel_hdcp_gsc_msg_send(i915, (u8 *)&session_close_in,
				       sizeof(session_close_in),
				       (u8 *)&session_close_out,
				       sizeof(session_close_out));
	if (byte < 0) {
		drm_dbg_kms(&i915->drm, "intel_hdcp_gsc_msg_send failed. %zd\n", byte);
		return byte;
	}

	if (session_close_out.header.status != FW_HDCP_STATUS_SUCCESS) {
		drm_dbg_kms(&i915->drm, "Session Close Failed. status: 0x%X\n",
			    session_close_out.header.status);
		return -EIO;
	}

	return 0;
}
