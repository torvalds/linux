// SPDX-License-Identifier: MIT
/*
 * Copyright 2023, Intel Corporation.
 */

#include <drm/i915_hdcp_interface.h>

#include "gem/i915_gem_region.h"
#include "gt/uc/intel_gsc_uc_heci_cmd_submit.h"
#include "i915_drv.h"
#include "i915_utils.h"
#include "intel_hdcp_gsc.h"

bool intel_hdcp_gsc_cs_required(struct drm_i915_private *i915)
{
	return DISPLAY_VER(i915) >= 14;
}

static int
gsc_hdcp_initiate_session(struct device *dev, struct hdcp_port_data *data,
			  struct hdcp2_ake_init *ake_data)
{
	struct wired_cmd_initiate_hdcp2_session_in session_init_in = { { 0 } };
	struct wired_cmd_initiate_hdcp2_session_out
						session_init_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !ake_data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_verify_receiver_cert_prepare_km(struct device *dev,
					 struct hdcp_port_data *data,
					 struct hdcp2_ake_send_cert *rx_cert,
					 bool *km_stored,
					 struct hdcp2_ake_no_stored_km
								*ek_pub_km,
					 size_t *msg_sz)
{
	struct wired_cmd_verify_receiver_cert_in verify_rxcert_in = { { 0 } };
	struct wired_cmd_verify_receiver_cert_out verify_rxcert_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_cert || !km_stored || !ek_pub_km || !msg_sz)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_verify_hprime(struct device *dev, struct hdcp_port_data *data,
		       struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct wired_cmd_ake_send_hprime_in send_hprime_in = { { 0 } };
	struct wired_cmd_ake_send_hprime_out send_hprime_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_hprime)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_store_pairing_info(struct device *dev, struct hdcp_port_data *data,
			    struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct wired_cmd_ake_send_pairing_info_in pairing_info_in = { { 0 } };
	struct wired_cmd_ake_send_pairing_info_out pairing_info_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !pairing_info)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_initiate_locality_check(struct device *dev,
				 struct hdcp_port_data *data,
				 struct hdcp2_lc_init *lc_init_data)
{
	struct wired_cmd_init_locality_check_in lc_init_in = { { 0 } };
	struct wired_cmd_init_locality_check_out lc_init_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !lc_init_data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_verify_lprime(struct device *dev, struct hdcp_port_data *data,
		       struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct wired_cmd_validate_locality_in verify_lprime_in = { { 0 } };
	struct wired_cmd_validate_locality_out verify_lprime_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !rx_lprime)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int gsc_hdcp_get_session_key(struct device *dev,
				    struct hdcp_port_data *data,
				    struct hdcp2_ske_send_eks *ske_data)
{
	struct wired_cmd_get_session_key_in get_skey_in = { { 0 } };
	struct wired_cmd_get_session_key_out get_skey_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data || !ske_data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_repeater_check_flow_prepare_ack(struct device *dev,
					 struct hdcp_port_data *data,
					 struct hdcp2_rep_send_receiverid_list
							*rep_topology,
					 struct hdcp2_rep_send_ack
							*rep_send_ack)
{
	struct wired_cmd_verify_repeater_in verify_repeater_in = { { 0 } };
	struct wired_cmd_verify_repeater_out verify_repeater_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !rep_topology || !rep_send_ack || !data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int gsc_hdcp_verify_mprime(struct device *dev,
				  struct hdcp_port_data *data,
				  struct hdcp2_rep_stream_ready *stream_ready)
{
	struct wired_cmd_repeater_auth_stream_req_in *verify_mprime_in;
	struct wired_cmd_repeater_auth_stream_req_out
					verify_mprime_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;
	size_t cmd_size;

	if (!dev || !stream_ready || !data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int gsc_hdcp_enable_authentication(struct device *dev,
					  struct hdcp_port_data *data)
{
	struct wired_cmd_enable_auth_in enable_auth_in = { { 0 } };
	struct wired_cmd_enable_auth_out enable_auth_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static int
gsc_hdcp_close_session(struct device *dev, struct hdcp_port_data *data)
{
	struct wired_cmd_close_session_in session_close_in = { { 0 } };
	struct wired_cmd_close_session_out session_close_out = { { 0 } };
	struct drm_i915_private *i915;
	ssize_t byte;

	if (!dev || !data)
		return -EINVAL;

	i915 = kdev_to_i915(dev);
	if (!i915) {
		dev_err(dev, "DRM not initialized, aborting HDCP.\n");
		return -ENODEV;
	}

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

static const struct i915_hdcp_ops gsc_hdcp_ops = {
	.initiate_hdcp2_session = gsc_hdcp_initiate_session,
	.verify_receiver_cert_prepare_km =
				gsc_hdcp_verify_receiver_cert_prepare_km,
	.verify_hprime = gsc_hdcp_verify_hprime,
	.store_pairing_info = gsc_hdcp_store_pairing_info,
	.initiate_locality_check = gsc_hdcp_initiate_locality_check,
	.verify_lprime = gsc_hdcp_verify_lprime,
	.get_session_key = gsc_hdcp_get_session_key,
	.repeater_check_flow_prepare_ack =
				gsc_hdcp_repeater_check_flow_prepare_ack,
	.verify_mprime = gsc_hdcp_verify_mprime,
	.enable_hdcp_authentication = gsc_hdcp_enable_authentication,
	.close_hdcp_session = gsc_hdcp_close_session,
};

/*This function helps allocate memory for the command that we will send to gsc cs */
static int intel_hdcp_gsc_initialize_message(struct drm_i915_private *i915,
					     struct intel_hdcp_gsc_message *hdcp_message)
{
	struct intel_gt *gt = i915->media_gt;
	struct drm_i915_gem_object *obj = NULL;
	struct i915_vma *vma = NULL;
	void *cmd;
	int err;

	/* allocate object of one page for HDCP command memory and store it */
	obj = i915_gem_object_create_shmem(i915, PAGE_SIZE);

	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Failed to allocate HDCP streaming command!\n");
		return PTR_ERR(obj);
	}

	cmd = i915_gem_object_pin_map_unlocked(obj, i915_coherent_map_type(i915, obj, true));
	if (IS_ERR(cmd)) {
		drm_err(&i915->drm, "Failed to map gsc message page!\n");
		err = PTR_ERR(cmd);
		goto out_unpin;
	}

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_unmap;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto out_unmap;

	memset(cmd, 0, obj->base.size);

	hdcp_message->hdcp_cmd = cmd;
	hdcp_message->vma = vma;

	return 0;

out_unmap:
	i915_gem_object_unpin_map(obj);
out_unpin:
	i915_gem_object_put(obj);
	return err;
}

static int intel_hdcp_gsc_hdcp2_init(struct drm_i915_private *i915)
{
	struct intel_hdcp_gsc_message *hdcp_message;
	int ret;

	hdcp_message = kzalloc(sizeof(*hdcp_message), GFP_KERNEL);

	if (!hdcp_message)
		return -ENOMEM;

	/*
	 * NOTE: No need to lock the comp mutex here as it is already
	 * going to be taken before this function called
	 */
	i915->display.hdcp.hdcp_message = hdcp_message;
	ret = intel_hdcp_gsc_initialize_message(i915, hdcp_message);

	if (ret)
		drm_err(&i915->drm, "Could not initialize hdcp_message\n");

	return ret;
}

static void intel_hdcp_gsc_free_message(struct drm_i915_private *i915)
{
	struct intel_hdcp_gsc_message *hdcp_message =
					i915->display.hdcp.hdcp_message;

	i915_vma_unpin_and_release(&hdcp_message->vma, I915_VMA_RELEASE_MAP);
	kfree(hdcp_message);
}

int intel_hdcp_gsc_init(struct drm_i915_private *i915)
{
	struct i915_hdcp_arbiter *data;
	int ret;

	data = kzalloc(sizeof(struct i915_hdcp_arbiter), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_lock(&i915->display.hdcp.hdcp_mutex);
	i915->display.hdcp.arbiter = data;
	i915->display.hdcp.arbiter->hdcp_dev = i915->drm.dev;
	i915->display.hdcp.arbiter->ops = &gsc_hdcp_ops;
	ret = intel_hdcp_gsc_hdcp2_init(i915);
	mutex_unlock(&i915->display.hdcp.hdcp_mutex);

	return ret;
}

void intel_hdcp_gsc_fini(struct drm_i915_private *i915)
{
	intel_hdcp_gsc_free_message(i915);
	kfree(i915->display.hdcp.arbiter);
}

static int intel_gsc_send_sync(struct drm_i915_private *i915,
			       struct intel_gsc_mtl_header *header, u64 addr,
			       size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	int ret;

	header->flags = 0;
	ret = intel_gsc_uc_heci_cmd_submit_packet(&gt->uc.gsc, addr,
						  header->message_size,
						  addr,
						  msg_out_len + sizeof(*header));
	if (ret) {
		drm_err(&i915->drm, "failed to send gsc HDCP msg (%d)\n", ret);
		return ret;
	}

	/*
	 * Checking validity marker for memory sanity
	 */
	if (header->validity_marker != GSC_HECI_VALIDITY_MARKER) {
		drm_err(&i915->drm, "invalid validity marker\n");
		return -EINVAL;
	}

	if (header->status != 0) {
		drm_err(&i915->drm, "header status indicates error %d\n",
			header->status);
		return -EINVAL;
	}

	if (header->flags & GSC_OUTFLAG_MSG_PENDING)
		return -EAGAIN;

	return 0;
}

/*
 * This function can now be used for sending requests and will also handle
 * receipt of reply messages hence no different function of message retrieval
 * is required. We will initialize intel_hdcp_gsc_message structure then add
 * gsc cs memory header as stated in specs after which the normal HDCP payload
 * will follow
 */
ssize_t intel_hdcp_gsc_msg_send(struct drm_i915_private *i915, u8 *msg_in,
				size_t msg_in_len, u8 *msg_out,
				size_t msg_out_len)
{
	struct intel_gt *gt = i915->media_gt;
	struct intel_gsc_mtl_header *header;
	const size_t max_msg_size = PAGE_SIZE - sizeof(*header);
	struct intel_hdcp_gsc_message *hdcp_message;
	u64 addr, host_session_id;
	u32 reply_size, msg_size;
	int ret, tries = 0;

	if (!intel_uc_uses_gsc_uc(&gt->uc))
		return -ENODEV;

	if (msg_in_len > max_msg_size || msg_out_len > max_msg_size)
		return -ENOSPC;

	hdcp_message = i915->display.hdcp.hdcp_message;
	header = hdcp_message->hdcp_cmd;
	addr = i915_ggtt_offset(hdcp_message->vma);

	msg_size = msg_in_len + sizeof(*header);
	memset(header, 0, msg_size);
	get_random_bytes(&host_session_id, sizeof(u64));
	intel_gsc_uc_heci_cmd_emit_mtl_header(header, HECI_MEADDRESS_HDCP,
					      msg_size, host_session_id);
	memcpy(hdcp_message->hdcp_cmd + sizeof(*header), msg_in, msg_in_len);

	/*
	 * Keep sending request in case the pending bit is set no need to add
	 * message handle as we are using same address hence loc. of header is
	 * same and it will contain the message handle. we will send the message
	 * 20 times each message 50 ms apart
	 */
	do {
		ret = intel_gsc_send_sync(i915, header, addr, msg_out_len);

		/* Only try again if gsc says so */
		if (ret != -EAGAIN)
			break;

		msleep(50);

	} while (++tries < 20);

	if (ret)
		goto err;

	/* we use the same mem for the reply, so header is in the same loc */
	reply_size = header->message_size - sizeof(*header);
	if (reply_size > msg_out_len) {
		drm_warn(&i915->drm, "caller with insufficient HDCP reply size %u (%d)\n",
			 reply_size, (u32)msg_out_len);
		reply_size = msg_out_len;
	} else if (reply_size != msg_out_len) {
		drm_dbg_kms(&i915->drm, "caller unexpected HCDP reply size %u (%d)\n",
			    reply_size, (u32)msg_out_len);
	}

	memcpy(msg_out, hdcp_message->hdcp_cmd + sizeof(*header), msg_out_len);

err:
	return ret;
}
