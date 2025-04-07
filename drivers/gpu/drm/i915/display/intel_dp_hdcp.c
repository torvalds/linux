/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2020 Google, Inc.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 */

#include <drm/display/drm_dp_helper.h>
#include <drm/display/drm_dp_mst_helper.h>
#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_print.h>

#include "i915_reg.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_hdcp.h"
#include "intel_hdcp.h"
#include "intel_hdcp_regs.h"
#include "intel_hdcp_shim.h"

static u32 transcoder_to_stream_enc_status(enum transcoder cpu_transcoder)
{
	switch (cpu_transcoder) {
	case TRANSCODER_A:
		return HDCP_STATUS_STREAM_A_ENC;
	case TRANSCODER_B:
		return HDCP_STATUS_STREAM_B_ENC;
	case TRANSCODER_C:
		return HDCP_STATUS_STREAM_C_ENC;
	case TRANSCODER_D:
		return HDCP_STATUS_STREAM_D_ENC;
	default:
		return 0;
	}
}

static void intel_dp_hdcp_wait_for_cp_irq(struct intel_connector *connector,
					  int timeout)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_dp *dp = &dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	long ret;

#define C (hdcp->cp_irq_count_cached != atomic_read(&hdcp->cp_irq_count))
	ret = wait_event_interruptible_timeout(hdcp->cp_irq_queue, C,
					       msecs_to_jiffies(timeout));

	if (!ret)
		drm_dbg_kms(connector->base.dev,
			    "Timedout at waiting for CP_IRQ\n");
}

static
int intel_dp_hdcp_write_an_aksv(struct intel_digital_port *dig_port,
				u8 *an)
{
	struct intel_display *display = to_intel_display(dig_port);
	u8 aksv[DRM_HDCP_KSV_LEN] = {};
	ssize_t dpcd_ret;

	/* Output An first, that's easy */
	dpcd_ret = drm_dp_dpcd_write(&dig_port->dp.aux, DP_AUX_HDCP_AN,
				     an, DRM_HDCP_AN_LEN);
	if (dpcd_ret != DRM_HDCP_AN_LEN) {
		drm_dbg_kms(display->drm,
			    "Failed to write An over DP/AUX (%zd)\n",
			    dpcd_ret);
		return dpcd_ret >= 0 ? -EIO : dpcd_ret;
	}

	/*
	 * Since Aksv is Oh-So-Secret, we can't access it in software. So we
	 * send an empty buffer of the correct length through the DP helpers. On
	 * the other side, in the transfer hook, we'll generate a flag based on
	 * the destination address which will tickle the hardware to output the
	 * Aksv on our behalf after the header is sent.
	 */
	dpcd_ret = drm_dp_dpcd_write(&dig_port->dp.aux, DP_AUX_HDCP_AKSV,
				     aksv, DRM_HDCP_KSV_LEN);
	if (dpcd_ret != DRM_HDCP_KSV_LEN) {
		drm_dbg_kms(display->drm,
			    "Failed to write Aksv over DP/AUX (%zd)\n",
			    dpcd_ret);
		return dpcd_ret >= 0 ? -EIO : dpcd_ret;
	}
	return 0;
}

static int intel_dp_hdcp_read_bksv(struct intel_digital_port *dig_port,
				   u8 *bksv)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux, DP_AUX_HDCP_BKSV, bksv,
			       DRM_HDCP_KSV_LEN);
	if (ret != DRM_HDCP_KSV_LEN) {
		drm_dbg_kms(display->drm,
			    "Read Bksv from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static int intel_dp_hdcp_read_bstatus(struct intel_digital_port *dig_port,
				      u8 *bstatus)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;

	/*
	 * For some reason the HDMI and DP HDCP specs call this register
	 * definition by different names. In the HDMI spec, it's called BSTATUS,
	 * but in DP it's called BINFO.
	 */
	ret = drm_dp_dpcd_read(&dig_port->dp.aux, DP_AUX_HDCP_BINFO,
			       bstatus, DRM_HDCP_BSTATUS_LEN);
	if (ret != DRM_HDCP_BSTATUS_LEN) {
		drm_dbg_kms(display->drm,
			    "Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_read_bcaps(struct drm_dp_aux *aux,
			     struct intel_display *display,
			     u8 *bcaps)
{
	ssize_t ret;

	ret = drm_dp_dpcd_read(aux, DP_AUX_HDCP_BCAPS,
			       bcaps, 1);
	if (ret != 1) {
		drm_dbg_kms(display->drm,
			    "Read bcaps from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static
int intel_dp_hdcp_repeater_present(struct intel_digital_port *dig_port,
				   bool *repeater_present)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;
	u8 bcaps;

	ret = intel_dp_hdcp_read_bcaps(&dig_port->dp.aux, display, &bcaps);
	if (ret)
		return ret;

	*repeater_present = bcaps & DP_BCAPS_REPEATER_PRESENT;
	return 0;
}

static
int intel_dp_hdcp_read_ri_prime(struct intel_digital_port *dig_port,
				u8 *ri_prime)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux, DP_AUX_HDCP_RI_PRIME,
			       ri_prime, DRM_HDCP_RI_LEN);
	if (ret != DRM_HDCP_RI_LEN) {
		drm_dbg_kms(display->drm,
			    "Read Ri' from DP/AUX failed (%zd)\n",
			    ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_read_ksv_ready(struct intel_digital_port *dig_port,
				 bool *ksv_ready)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;
	u8 bstatus;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux, DP_AUX_HDCP_BSTATUS,
			       &bstatus, 1);
	if (ret != 1) {
		drm_dbg_kms(display->drm,
			    "Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}
	*ksv_ready = bstatus & DP_BSTATUS_READY;
	return 0;
}

static
int intel_dp_hdcp_read_ksv_fifo(struct intel_digital_port *dig_port,
				int num_downstream, u8 *ksv_fifo)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;
	int i;

	/* KSV list is read via 15 byte window (3 entries @ 5 bytes each) */
	for (i = 0; i < num_downstream; i += 3) {
		size_t len = min(num_downstream - i, 3) * DRM_HDCP_KSV_LEN;
		ret = drm_dp_dpcd_read(&dig_port->dp.aux,
				       DP_AUX_HDCP_KSV_FIFO,
				       ksv_fifo + i * DRM_HDCP_KSV_LEN,
				       len);
		if (ret != len) {
			drm_dbg_kms(display->drm,
				    "Read ksv[%d] from DP/AUX failed (%zd)\n",
				    i, ret);
			return ret >= 0 ? -EIO : ret;
		}
	}
	return 0;
}

static
int intel_dp_hdcp_read_v_prime_part(struct intel_digital_port *dig_port,
				    int i, u32 *part)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;

	if (i >= DRM_HDCP_V_PRIME_NUM_PARTS)
		return -EINVAL;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux,
			       DP_AUX_HDCP_V_PRIME(i), part,
			       DRM_HDCP_V_PRIME_PART_LEN);
	if (ret != DRM_HDCP_V_PRIME_PART_LEN) {
		drm_dbg_kms(display->drm,
			    "Read v'[%d] from DP/AUX failed (%zd)\n", i, ret);
		return ret >= 0 ? -EIO : ret;
	}
	return 0;
}

static
int intel_dp_hdcp_toggle_signalling(struct intel_digital_port *dig_port,
				    enum transcoder cpu_transcoder,
				    bool enable)
{
	/* Not used for single stream DisplayPort setups */
	return 0;
}

static
bool intel_dp_hdcp_check_link(struct intel_digital_port *dig_port,
			      struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;
	u8 bstatus;

	ret = drm_dp_dpcd_read(&dig_port->dp.aux, DP_AUX_HDCP_BSTATUS,
			       &bstatus, 1);
	if (ret != 1) {
		drm_dbg_kms(display->drm,
			    "Read bstatus from DP/AUX failed (%zd)\n", ret);
		return false;
	}

	return !(bstatus & (DP_BSTATUS_LINK_FAILURE | DP_BSTATUS_REAUTH_REQ));
}

static
int intel_dp_hdcp_get_capability(struct intel_digital_port *dig_port,
				 bool *hdcp_capable)
{
	struct intel_display *display = to_intel_display(dig_port);
	ssize_t ret;
	u8 bcaps;

	ret = intel_dp_hdcp_read_bcaps(&dig_port->dp.aux, display, &bcaps);
	if (ret)
		return ret;

	*hdcp_capable = bcaps & DP_BCAPS_HDCP_CAPABLE;
	return 0;
}

struct hdcp2_dp_errata_stream_type {
	u8	msg_id;
	u8	stream_type;
} __packed;

struct hdcp2_dp_msg_data {
	u8 msg_id;
	u32 offset;
	bool msg_detectable;
	u32 timeout;
	u32 timeout2; /* Added for non_paired situation */
	/* Timeout to read entire msg */
	u32 msg_read_timeout;
};

static const struct hdcp2_dp_msg_data hdcp2_dp_msg_data[] = {
	{ HDCP_2_2_AKE_INIT, DP_HDCP_2_2_AKE_INIT_OFFSET, false, 0, 0, 0},
	{ HDCP_2_2_AKE_SEND_CERT, DP_HDCP_2_2_AKE_SEND_CERT_OFFSET,
	  false, HDCP_2_2_CERT_TIMEOUT_MS, 0, HDCP_2_2_DP_CERT_READ_TIMEOUT_MS},
	{ HDCP_2_2_AKE_NO_STORED_KM, DP_HDCP_2_2_AKE_NO_STORED_KM_OFFSET,
	  false, 0, 0, 0 },
	{ HDCP_2_2_AKE_STORED_KM, DP_HDCP_2_2_AKE_STORED_KM_OFFSET,
	  false, 0, 0, 0 },
	{ HDCP_2_2_AKE_SEND_HPRIME, DP_HDCP_2_2_AKE_SEND_HPRIME_OFFSET,
	  true, HDCP_2_2_HPRIME_PAIRED_TIMEOUT_MS,
	  HDCP_2_2_HPRIME_NO_PAIRED_TIMEOUT_MS, HDCP_2_2_DP_HPRIME_READ_TIMEOUT_MS},
	{ HDCP_2_2_AKE_SEND_PAIRING_INFO,
	  DP_HDCP_2_2_AKE_SEND_PAIRING_INFO_OFFSET, true,
	  HDCP_2_2_PAIRING_TIMEOUT_MS, 0, HDCP_2_2_DP_PAIRING_READ_TIMEOUT_MS },
	{ HDCP_2_2_LC_INIT, DP_HDCP_2_2_LC_INIT_OFFSET, false, 0, 0, 0 },
	{ HDCP_2_2_LC_SEND_LPRIME, DP_HDCP_2_2_LC_SEND_LPRIME_OFFSET,
	  false, HDCP_2_2_DP_LPRIME_TIMEOUT_MS, 0, 0 },
	{ HDCP_2_2_SKE_SEND_EKS, DP_HDCP_2_2_SKE_SEND_EKS_OFFSET, false,
	  0, 0, 0 },
	{ HDCP_2_2_REP_SEND_RECVID_LIST,
	  DP_HDCP_2_2_REP_SEND_RECVID_LIST_OFFSET, true,
	  HDCP_2_2_RECVID_LIST_TIMEOUT_MS, 0, 0 },
	{ HDCP_2_2_REP_SEND_ACK, DP_HDCP_2_2_REP_SEND_ACK_OFFSET, false,
	  0, 0, 0 },
	{ HDCP_2_2_REP_STREAM_MANAGE,
	  DP_HDCP_2_2_REP_STREAM_MANAGE_OFFSET, false,
	  0, 0, 0},
	{ HDCP_2_2_REP_STREAM_READY, DP_HDCP_2_2_REP_STREAM_READY_OFFSET,
	  false, HDCP_2_2_STREAM_READY_TIMEOUT_MS, 0, 0 },
/* local define to shovel this through the write_2_2 interface */
#define HDCP_2_2_ERRATA_DP_STREAM_TYPE	50
	{ HDCP_2_2_ERRATA_DP_STREAM_TYPE,
	  DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET, false,
	  0, 0 },
};

static int
intel_dp_hdcp2_read_rx_status(struct intel_connector *connector,
			      u8 *rx_status)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_dp_aux *aux = &dig_port->dp.aux;
	ssize_t ret;

	ret = drm_dp_dpcd_read(aux,
			       DP_HDCP_2_2_REG_RXSTATUS_OFFSET, rx_status,
			       HDCP_2_2_DP_RXSTATUS_LEN);
	if (ret != HDCP_2_2_DP_RXSTATUS_LEN) {
		drm_dbg_kms(display->drm,
			    "Read bstatus from DP/AUX failed (%zd)\n", ret);
		return ret >= 0 ? -EIO : ret;
	}

	return 0;
}

static
int hdcp2_detect_msg_availability(struct intel_connector *connector,
				  u8 msg_id, bool *msg_ready)
{
	u8 rx_status;
	int ret;

	*msg_ready = false;
	ret = intel_dp_hdcp2_read_rx_status(connector, &rx_status);
	if (ret < 0)
		return ret;

	switch (msg_id) {
	case HDCP_2_2_AKE_SEND_HPRIME:
		if (HDCP_2_2_DP_RXSTATUS_H_PRIME(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_AKE_SEND_PAIRING_INFO:
		if (HDCP_2_2_DP_RXSTATUS_PAIRING(rx_status))
			*msg_ready = true;
		break;
	case HDCP_2_2_REP_SEND_RECVID_LIST:
		if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
			*msg_ready = true;
		break;
	default:
		drm_err(connector->base.dev,
			"Unidentified msg_id: %d\n", msg_id);
		return -EINVAL;
	}

	return 0;
}

static ssize_t
intel_dp_hdcp2_wait_for_msg(struct intel_connector *connector,
			    const struct hdcp2_dp_msg_data *hdcp2_msg_data)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_dp *dp = &dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	u8 msg_id = hdcp2_msg_data->msg_id;
	int ret, timeout;
	bool msg_ready = false;

	if (msg_id == HDCP_2_2_AKE_SEND_HPRIME && !hdcp->is_paired)
		timeout = hdcp2_msg_data->timeout2;
	else
		timeout = hdcp2_msg_data->timeout;

	/*
	 * There is no way to detect the CERT, LPRIME and STREAM_READY
	 * availability. So Wait for timeout and read the msg.
	 */
	if (!hdcp2_msg_data->msg_detectable) {
		mdelay(timeout);
		ret = 0;
	} else {
		/*
		 * As we want to check the msg availability at timeout, Ignoring
		 * the timeout at wait for CP_IRQ.
		 */
		intel_dp_hdcp_wait_for_cp_irq(connector, timeout);
		ret = hdcp2_detect_msg_availability(connector, msg_id,
						    &msg_ready);
		if (!msg_ready)
			ret = -ETIMEDOUT;
	}

	if (ret)
		drm_dbg_kms(display->drm,
			    "msg_id %d, ret %d, timeout(mSec): %d\n",
			    hdcp2_msg_data->msg_id, ret, timeout);

	return ret;
}

static const struct hdcp2_dp_msg_data *get_hdcp2_dp_msg_data(u8 msg_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hdcp2_dp_msg_data); i++)
		if (hdcp2_dp_msg_data[i].msg_id == msg_id)
			return &hdcp2_dp_msg_data[i];

	return NULL;
}

static
int intel_dp_hdcp2_write_msg(struct intel_connector *connector,
			     void *buf, size_t size)
{
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_write, len;
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_dp_aux *aux = &dig_port->dp.aux;
	const struct hdcp2_dp_msg_data *hdcp2_msg_data;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(*byte);
	if (!hdcp2_msg_data)
		return -EINVAL;

	offset = hdcp2_msg_data->offset;

	/* No msg_id in DP HDCP2.2 msgs */
	bytes_to_write = size - 1;
	byte++;

	while (bytes_to_write) {
		len = bytes_to_write > DP_AUX_MAX_PAYLOAD_BYTES ?
				DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_write;

		ret = drm_dp_dpcd_write(aux,
					offset, (void *)byte, len);
		if (ret < 0)
			return ret;

		bytes_to_write -= ret;
		byte += ret;
		offset += ret;
	}

	return size;
}

static
ssize_t get_receiver_id_list_rx_info(struct intel_connector *connector,
				     u32 *dev_cnt, u8 *byte)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_dp_aux *aux = &dig_port->dp.aux;
	ssize_t ret;
	u8 *rx_info = byte;

	ret = drm_dp_dpcd_read(aux,
			       DP_HDCP_2_2_REG_RXINFO_OFFSET,
			       (void *)rx_info, HDCP_2_2_RXINFO_LEN);
	if (ret != HDCP_2_2_RXINFO_LEN)
		return ret >= 0 ? -EIO : ret;

	*dev_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		   HDCP_2_2_DEV_COUNT_LO(rx_info[1]));

	if (*dev_cnt > HDCP_2_2_MAX_DEVICE_COUNT)
		*dev_cnt = HDCP_2_2_MAX_DEVICE_COUNT;

	return ret;
}

static
int intel_dp_hdcp2_read_msg(struct intel_connector *connector,
			    u8 msg_id, void *buf, size_t size)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct drm_dp_aux *aux = &dig_port->dp.aux;
	struct intel_dp *dp = &dig_port->dp;
	struct intel_hdcp *hdcp = &dp->attached_connector->hdcp;
	unsigned int offset;
	u8 *byte = buf;
	ssize_t ret, bytes_to_recv, len;
	const struct hdcp2_dp_msg_data *hdcp2_msg_data;
	ktime_t msg_end = ktime_set(0, 0);
	bool msg_expired;
	u32 dev_cnt;

	hdcp2_msg_data = get_hdcp2_dp_msg_data(msg_id);
	if (!hdcp2_msg_data)
		return -EINVAL;
	offset = hdcp2_msg_data->offset;

	ret = intel_dp_hdcp2_wait_for_msg(connector, hdcp2_msg_data);
	if (ret < 0)
		return ret;

	hdcp->cp_irq_count_cached = atomic_read(&hdcp->cp_irq_count);

	/* DP adaptation msgs has no msg_id */
	byte++;

	if (msg_id == HDCP_2_2_REP_SEND_RECVID_LIST) {
		ret = get_receiver_id_list_rx_info(connector, &dev_cnt, byte);
		if (ret < 0)
			return ret;

		byte += ret;
		size = sizeof(struct hdcp2_rep_send_receiverid_list) -
		HDCP_2_2_RXINFO_LEN - HDCP_2_2_RECEIVER_IDS_MAX_LEN +
		(dev_cnt * HDCP_2_2_RECEIVER_ID_LEN);
		offset += HDCP_2_2_RXINFO_LEN;
	}

	bytes_to_recv = size - 1;

	while (bytes_to_recv) {
		len = bytes_to_recv > DP_AUX_MAX_PAYLOAD_BYTES ?
		      DP_AUX_MAX_PAYLOAD_BYTES : bytes_to_recv;

		/* Entire msg read timeout since initiate of msg read */
		if (bytes_to_recv == size - 1 && hdcp2_msg_data->msg_read_timeout > 0) {
			msg_end = ktime_add_ms(ktime_get_raw(),
					       hdcp2_msg_data->msg_read_timeout);
		}

		ret = drm_dp_dpcd_read(aux, offset,
				       (void *)byte, len);
		if (ret < 0) {
			drm_dbg_kms(display->drm, "msg_id %d, ret %zd\n",
				    msg_id, ret);
			return ret;
		}

		bytes_to_recv -= ret;
		byte += ret;
		offset += ret;
	}

	if (hdcp2_msg_data->msg_read_timeout > 0) {
		msg_expired = ktime_after(ktime_get_raw(), msg_end);
		if (msg_expired) {
			drm_dbg_kms(display->drm,
				    "msg_id %d, entire msg read timeout(mSec): %d\n",
				    msg_id, hdcp2_msg_data->msg_read_timeout);
			return -ETIMEDOUT;
		}
	}

	byte = buf;
	*byte = msg_id;

	return size;
}

static
int intel_dp_hdcp2_config_stream_type(struct intel_connector *connector,
				      bool is_repeater, u8 content_type)
{
	int ret;
	struct hdcp2_dp_errata_stream_type stream_type_msg;

	if (is_repeater)
		return 0;

	/*
	 * Errata for DP: As Stream type is used for encryption, Receiver
	 * should be communicated with stream type for the decryption of the
	 * content.
	 * Repeater will be communicated with stream type as a part of it's
	 * auth later in time.
	 */
	stream_type_msg.msg_id = HDCP_2_2_ERRATA_DP_STREAM_TYPE;
	stream_type_msg.stream_type = content_type;

	ret =  intel_dp_hdcp2_write_msg(connector, &stream_type_msg,
					sizeof(stream_type_msg));

	return ret < 0 ? ret : 0;

}

static
int intel_dp_hdcp2_check_link(struct intel_digital_port *dig_port,
			      struct intel_connector *connector)
{
	u8 rx_status;
	int ret;

	ret = intel_dp_hdcp2_read_rx_status(connector,
					    &rx_status);
	if (ret)
		return ret;

	if (HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(rx_status))
		ret = HDCP_REAUTH_REQUEST;
	else if (HDCP_2_2_DP_RXSTATUS_LINK_FAILED(rx_status))
		ret = HDCP_LINK_INTEGRITY_FAILURE;
	else if (HDCP_2_2_DP_RXSTATUS_READY(rx_status))
		ret = HDCP_TOPOLOGY_CHANGE;

	return ret;
}

static
int _intel_dp_hdcp2_get_capability(struct drm_dp_aux *aux,
				   bool *capable)
{
	u8 rx_caps[3];
	int ret, i;

	*capable = false;

	/*
	 * Some HDCP monitors act really shady by not giving the correct hdcp
	 * capability on the first rx_caps read and usually take an extra read
	 * to give the capability. We read rx_caps three times before we
	 * declare a monitor not capable of HDCP 2.2.
	 */
	for (i = 0; i < 3; i++) {
		ret = drm_dp_dpcd_read(aux,
				       DP_HDCP_2_2_REG_RX_CAPS_OFFSET,
				       rx_caps, HDCP_2_2_RXCAPS_LEN);
		if (ret != HDCP_2_2_RXCAPS_LEN)
			return ret >= 0 ? -EIO : ret;

		if (rx_caps[0] == HDCP_2_2_RX_CAPS_VERSION_VAL &&
		    HDCP_2_2_DP_HDCP_CAPABLE(rx_caps[2])) {
			*capable = true;
			break;
		}
	}

	return 0;
}

static
int intel_dp_hdcp2_get_capability(struct intel_connector *connector,
				  bool *capable)
{
	struct intel_digital_port *dig_port;
	struct drm_dp_aux *aux;

	*capable = false;
	if (!intel_attached_encoder(connector))
		return -EINVAL;

	dig_port = intel_attached_dig_port(connector);
	aux = &dig_port->dp.aux;

	return _intel_dp_hdcp2_get_capability(aux, capable);
}

static
int intel_dp_hdcp_get_remote_capability(struct intel_connector *connector,
					bool *hdcp_capable,
					bool *hdcp2_capable)
{
	struct intel_display *display = to_intel_display(connector);
	struct drm_dp_aux *aux;
	u8 bcaps;
	int ret;

	*hdcp_capable = false;
	*hdcp2_capable = false;
	if (!connector->mst.dp)
		return -EINVAL;

	aux = &connector->mst.port->aux;
	ret =  _intel_dp_hdcp2_get_capability(aux, hdcp2_capable);
	if (ret)
		drm_dbg_kms(display->drm,
			    "HDCP2 DPCD capability read failed err: %d\n", ret);

	ret = intel_dp_hdcp_read_bcaps(aux, display, &bcaps);
	if (ret)
		return ret;

	*hdcp_capable = bcaps & DP_BCAPS_HDCP_CAPABLE;

	return 0;
}

static const struct intel_hdcp_shim intel_dp_hdcp_shim = {
	.write_an_aksv = intel_dp_hdcp_write_an_aksv,
	.read_bksv = intel_dp_hdcp_read_bksv,
	.read_bstatus = intel_dp_hdcp_read_bstatus,
	.repeater_present = intel_dp_hdcp_repeater_present,
	.read_ri_prime = intel_dp_hdcp_read_ri_prime,
	.read_ksv_ready = intel_dp_hdcp_read_ksv_ready,
	.read_ksv_fifo = intel_dp_hdcp_read_ksv_fifo,
	.read_v_prime_part = intel_dp_hdcp_read_v_prime_part,
	.toggle_signalling = intel_dp_hdcp_toggle_signalling,
	.check_link = intel_dp_hdcp_check_link,
	.hdcp_get_capability = intel_dp_hdcp_get_capability,
	.write_2_2_msg = intel_dp_hdcp2_write_msg,
	.read_2_2_msg = intel_dp_hdcp2_read_msg,
	.config_stream_type = intel_dp_hdcp2_config_stream_type,
	.check_2_2_link = intel_dp_hdcp2_check_link,
	.hdcp_2_2_get_capability = intel_dp_hdcp2_get_capability,
	.protocol = HDCP_PROTOCOL_DP,
};

static int
intel_dp_mst_toggle_hdcp_stream_select(struct intel_connector *connector,
				       bool enable)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	ret = intel_ddi_toggle_hdcp_bits(&dig_port->base,
					 hdcp->stream_transcoder, enable,
					 TRANS_DDI_HDCP_SELECT);
	if (ret)
		drm_err(display->drm, "%s HDCP stream select failed (%d)\n",
			enable ? "Enable" : "Disable", ret);
	return ret;
}

static int
intel_dp_mst_hdcp_stream_encryption(struct intel_connector *connector,
				    bool enable)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->stream_transcoder;
	u32 stream_enc_status;
	int ret;

	ret = intel_dp_mst_toggle_hdcp_stream_select(connector, enable);
	if (ret)
		return ret;

	stream_enc_status =  transcoder_to_stream_enc_status(cpu_transcoder);
	if (!stream_enc_status)
		return -EINVAL;

	/* Wait for encryption confirmation */
	if (intel_de_wait(display, HDCP_STATUS(display, cpu_transcoder, port),
			  stream_enc_status, enable ? stream_enc_status : 0,
			  HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(display->drm, "Timed out waiting for transcoder: %s stream encryption %s\n",
			transcoder_name(cpu_transcoder), str_enabled_disabled(enable));
		return -ETIMEDOUT;
	}

	return 0;
}

static int
intel_dp_mst_hdcp2_stream_encryption(struct intel_connector *connector,
				     bool enable)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum transcoder cpu_transcoder = hdcp->stream_transcoder;
	enum pipe pipe = (enum pipe)cpu_transcoder;
	enum port port = dig_port->base.port;
	int ret;

	drm_WARN_ON(display->drm, enable &&
		    !!(intel_de_read(display, HDCP2_AUTH_STREAM(display, cpu_transcoder, port))
		    & AUTH_STREAM_TYPE) != data->streams[0].stream_type);

	ret = intel_dp_mst_toggle_hdcp_stream_select(connector, enable);
	if (ret)
		return ret;

	/* Wait for encryption confirmation */
	if (intel_de_wait(display, HDCP2_STREAM_STATUS(display, cpu_transcoder, pipe),
			  STREAM_ENCRYPTION_STATUS,
			  enable ? STREAM_ENCRYPTION_STATUS : 0,
			  HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(display->drm, "Timed out waiting for transcoder: %s stream encryption %s\n",
			transcoder_name(cpu_transcoder), str_enabled_disabled(enable));
		return -ETIMEDOUT;
	}

	return 0;
}

static
int intel_dp_mst_hdcp2_check_link(struct intel_digital_port *dig_port,
				  struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	/*
	 * We do need to do the Link Check only for the connector involved with
	 * HDCP port authentication and encryption.
	 * We can re-use the hdcp->is_repeater flag to know that the connector
	 * involved with HDCP port authentication and encryption.
	 */
	if (hdcp->is_repeater) {
		ret = intel_dp_hdcp2_check_link(dig_port, connector);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct intel_hdcp_shim intel_dp_mst_hdcp_shim = {
	.write_an_aksv = intel_dp_hdcp_write_an_aksv,
	.read_bksv = intel_dp_hdcp_read_bksv,
	.read_bstatus = intel_dp_hdcp_read_bstatus,
	.repeater_present = intel_dp_hdcp_repeater_present,
	.read_ri_prime = intel_dp_hdcp_read_ri_prime,
	.read_ksv_ready = intel_dp_hdcp_read_ksv_ready,
	.read_ksv_fifo = intel_dp_hdcp_read_ksv_fifo,
	.read_v_prime_part = intel_dp_hdcp_read_v_prime_part,
	.toggle_signalling = intel_dp_hdcp_toggle_signalling,
	.stream_encryption = intel_dp_mst_hdcp_stream_encryption,
	.check_link = intel_dp_hdcp_check_link,
	.hdcp_get_capability = intel_dp_hdcp_get_capability,
	.write_2_2_msg = intel_dp_hdcp2_write_msg,
	.read_2_2_msg = intel_dp_hdcp2_read_msg,
	.config_stream_type = intel_dp_hdcp2_config_stream_type,
	.stream_2_2_encryption = intel_dp_mst_hdcp2_stream_encryption,
	.check_2_2_link = intel_dp_mst_hdcp2_check_link,
	.hdcp_2_2_get_capability = intel_dp_hdcp2_get_capability,
	.get_remote_hdcp_capability = intel_dp_hdcp_get_remote_capability,
	.protocol = HDCP_PROTOCOL_DP,
};

int intel_dp_hdcp_init(struct intel_digital_port *dig_port,
		       struct intel_connector *intel_connector)
{
	struct intel_display *display = to_intel_display(dig_port);
	struct intel_encoder *intel_encoder = &dig_port->base;
	enum port port = intel_encoder->port;
	struct intel_dp *intel_dp = &dig_port->dp;

	if (!is_hdcp_supported(display, port))
		return 0;

	if (intel_connector->mst.dp)
		return intel_hdcp_init(intel_connector, dig_port,
				       &intel_dp_mst_hdcp_shim);
	else if (!intel_dp_is_edp(intel_dp))
		return intel_hdcp_init(intel_connector, dig_port,
				       &intel_dp_hdcp_shim);

	return 0;
}
