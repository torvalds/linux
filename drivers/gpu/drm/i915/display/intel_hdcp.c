/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2017 Google, Inc.
 * Copyright _ 2017-2019, Intel Corporation.
 *
 * Authors:
 * Sean Paul <seanpaul@chromium.org>
 * Ramalingam C <ramalingam.c@intel.com>
 */

#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/random.h>

#include <drm/display/drm_hdcp_helper.h>
#include <drm/drm_print.h>
#include <drm/intel/i915_component.h>

#include "i915_reg.h"
#include "i915_utils.h"
#include "intel_connector.h"
#include "intel_de.h"
#include "intel_display_power.h"
#include "intel_display_power_well.h"
#include "intel_display_regs.h"
#include "intel_display_rpm.h"
#include "intel_display_types.h"
#include "intel_dp_mst.h"
#include "intel_hdcp.h"
#include "intel_hdcp_gsc.h"
#include "intel_hdcp_gsc_message.h"
#include "intel_hdcp_regs.h"
#include "intel_hdcp_shim.h"
#include "intel_pcode.h"
#include "intel_step.h"

#define USE_HDCP_GSC(__display)		(DISPLAY_VER(__display) >= 14)

#define KEY_LOAD_TRIES	5
#define HDCP2_LC_RETRY_CNT			3

static void
intel_hdcp_adjust_hdcp_line_rekeying(struct intel_encoder *encoder,
				     struct intel_hdcp *hdcp,
				     bool enable)
{
	struct intel_display *display = to_intel_display(encoder);
	i915_reg_t rekey_reg;
	u32 rekey_bit = 0;

	/* Here we assume HDMI is in TMDS mode of operation */
	if (!intel_encoder_is_hdmi(encoder))
		return;

	if (DISPLAY_VER(display) >= 30) {
		rekey_reg = TRANS_DDI_FUNC_CTL(display, hdcp->cpu_transcoder);
		rekey_bit = XE3_TRANS_DDI_HDCP_LINE_REKEY_DISABLE;
	} else if (IS_DISPLAY_VERx100_STEP(display, 1401, STEP_B0, STEP_FOREVER) ||
		   IS_DISPLAY_VERx100_STEP(display, 2000, STEP_B0, STEP_FOREVER)) {
		rekey_reg = TRANS_DDI_FUNC_CTL(display, hdcp->cpu_transcoder);
		rekey_bit = TRANS_DDI_HDCP_LINE_REKEY_DISABLE;
	} else if (IS_DISPLAY_VERx100_STEP(display, 1400, STEP_D0, STEP_FOREVER)) {
		rekey_reg = CHICKEN_TRANS(display, hdcp->cpu_transcoder);
		rekey_bit = HDCP_LINE_REKEY_DISABLE;
	}

	if (rekey_bit)
		intel_de_rmw(display, rekey_reg, rekey_bit, enable ? 0 : rekey_bit);
}

static int intel_conn_to_vcpi(struct intel_atomic_state *state,
			      struct intel_connector *connector)
{
	struct drm_dp_mst_topology_mgr *mgr;
	struct drm_dp_mst_atomic_payload *payload;
	struct drm_dp_mst_topology_state *mst_state;
	int vcpi = 0;

	/* For HDMI this is forced to be 0x0. For DP SST also this is 0x0. */
	if (!connector->mst.port)
		return 0;
	mgr = connector->mst.port->mgr;

	drm_modeset_lock(&mgr->base.lock, state->base.acquire_ctx);
	mst_state = to_drm_dp_mst_topology_state(mgr->base.state);
	payload = drm_atomic_get_mst_payload_state(mst_state, connector->mst.port);
	if (drm_WARN_ON(mgr->dev, !payload))
		goto out;

	vcpi = payload->vcpi;
	if (drm_WARN_ON(mgr->dev, vcpi < 0)) {
		vcpi = 0;
		goto out;
	}
out:
	return vcpi;
}

/*
 * intel_hdcp_required_content_stream selects the most highest common possible HDCP
 * content_type for all streams in DP MST topology because security f/w doesn't
 * have any provision to mark content_type for each stream separately, it marks
 * all available streams with the content_type proivided at the time of port
 * authentication. This may prohibit the userspace to use type1 content on
 * HDCP 2.2 capable sink because of other sink are not capable of HDCP 2.2 in
 * DP MST topology. Though it is not compulsory, security fw should change its
 * policy to mark different content_types for different streams.
 */
static int
intel_hdcp_required_content_stream(struct intel_atomic_state *state,
				   struct intel_digital_port *dig_port)
{
	struct intel_display *display = to_intel_display(state);
	struct drm_connector_list_iter conn_iter;
	struct intel_digital_port *conn_dig_port;
	struct intel_connector *connector;
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	bool enforce_type0 = false;
	int k;

	if (dig_port->hdcp.auth_status)
		return 0;

	data->k = 0;

	if (!dig_port->hdcp.mst_type1_capable)
		enforce_type0 = true;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		if (connector->base.status == connector_status_disconnected)
			continue;

		if (!intel_encoder_is_mst(intel_attached_encoder(connector)))
			continue;

		conn_dig_port = intel_attached_dig_port(connector);
		if (conn_dig_port != dig_port)
			continue;

		data->streams[data->k].stream_id =
			intel_conn_to_vcpi(state, connector);
		data->k++;

		/* if there is only one active stream */
		if (intel_dp_mst_active_streams(&dig_port->dp) <= 1)
			break;
	}
	drm_connector_list_iter_end(&conn_iter);

	if (drm_WARN_ON(display->drm, data->k > INTEL_NUM_PIPES(display) || data->k == 0))
		return -EINVAL;

	/*
	 * Apply common protection level across all streams in DP MST Topology.
	 * Use highest supported content type for all streams in DP MST Topology.
	 */
	for (k = 0; k < data->k; k++)
		data->streams[k].stream_type =
			enforce_type0 ? DRM_MODE_HDCP_CONTENT_TYPE0 : DRM_MODE_HDCP_CONTENT_TYPE1;

	return 0;
}

static int intel_hdcp_prepare_streams(struct intel_atomic_state *state,
				      struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (intel_encoder_is_mst(intel_attached_encoder(connector)))
		return intel_hdcp_required_content_stream(state, dig_port);

	data->k = 1;
	data->streams[0].stream_id = 0;
	data->streams[0].stream_type = hdcp->content_type;

	return 0;
}

static
bool intel_hdcp_is_ksv_valid(u8 *ksv)
{
	int i, ones = 0;
	/* KSV has 20 1's and 20 0's */
	for (i = 0; i < DRM_HDCP_KSV_LEN; i++)
		ones += hweight8(ksv[i]);
	if (ones != 20)
		return false;

	return true;
}

static
int intel_hdcp_read_valid_bksv(struct intel_digital_port *dig_port,
			       const struct intel_hdcp_shim *shim, u8 *bksv)
{
	struct intel_display *display = to_intel_display(dig_port);
	int ret, i, tries = 2;

	/* HDCP spec states that we must retry the bksv if it is invalid */
	for (i = 0; i < tries; i++) {
		ret = shim->read_bksv(dig_port, bksv);
		if (ret)
			return ret;
		if (intel_hdcp_is_ksv_valid(bksv))
			break;
	}
	if (i == tries) {
		drm_dbg_kms(display->drm, "Bksv is invalid\n");
		return -ENODEV;
	}

	return 0;
}

/* Is HDCP1.4 capable on Platform and Sink */
static bool intel_hdcp_get_capability(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port;
	const struct intel_hdcp_shim *shim = connector->hdcp.shim;
	bool capable = false;
	u8 bksv[5];

	if (!intel_attached_encoder(connector))
		return capable;

	dig_port = intel_attached_dig_port(connector);

	if (!shim)
		return capable;

	if (shim->hdcp_get_capability) {
		shim->hdcp_get_capability(dig_port, &capable);
	} else {
		if (!intel_hdcp_read_valid_bksv(dig_port, shim, bksv))
			capable = true;
	}

	return capable;
}

/*
 * Check if the source has all the building blocks ready to make
 * HDCP 2.2 work
 */
static bool intel_hdcp2_prerequisite(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;

	/* I915 support for HDCP2.2 */
	if (!hdcp->hdcp2_supported)
		return false;

	/* If MTL+ make sure gsc is loaded and proxy is setup */
	if (USE_HDCP_GSC(display)) {
		if (!intel_hdcp_gsc_check_status(display->drm))
			return false;
	}

	/* MEI/GSC interface is solid depending on which is used */
	mutex_lock(&display->hdcp.hdcp_mutex);
	if (!display->hdcp.comp_added || !display->hdcp.arbiter) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return false;
	}
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return true;
}

/* Is HDCP2.2 capable on Platform and Sink */
static bool intel_hdcp2_get_capability(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	bool capable = false;

	if (!intel_hdcp2_prerequisite(connector))
		return false;

	/* Sink's capability for HDCP2.2 */
	hdcp->shim->hdcp_2_2_get_capability(connector, &capable);

	return capable;
}

static void intel_hdcp_get_remote_capability(struct intel_connector *connector,
					     bool *hdcp_capable,
					     bool *hdcp2_capable)
{
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!hdcp->shim->get_remote_hdcp_capability)
		return;

	hdcp->shim->get_remote_hdcp_capability(connector, hdcp_capable,
					       hdcp2_capable);

	if (!intel_hdcp2_prerequisite(connector))
		*hdcp2_capable = false;
}

static bool intel_hdcp_in_use(struct intel_display *display,
			      enum transcoder cpu_transcoder, enum port port)
{
	return intel_de_read(display,
			     HDCP_STATUS(display, cpu_transcoder, port)) &
		HDCP_STATUS_ENC;
}

static bool intel_hdcp2_in_use(struct intel_display *display,
			       enum transcoder cpu_transcoder, enum port port)
{
	return intel_de_read(display,
			     HDCP2_STATUS(display, cpu_transcoder, port)) &
		LINK_ENCRYPTION_STATUS;
}

static int intel_hdcp_poll_ksv_fifo(struct intel_digital_port *dig_port,
				    const struct intel_hdcp_shim *shim)
{
	int ret, read_ret;
	bool ksv_ready;

	/* Poll for ksv list ready (spec says max time allowed is 5s) */
	ret = poll_timeout_us(read_ret = shim->read_ksv_ready(dig_port, &ksv_ready),
			      read_ret || ksv_ready,
			      100 * 1000, 5 * 1000 * 1000, false);
	if (ret)
		return ret;
	if (read_ret)
		return read_ret;

	return 0;
}

static bool hdcp_key_loadable(struct intel_display *display)
{
	enum i915_power_well_id id;
	bool enabled = false;

	/*
	 * On HSW and BDW, Display HW loads the Key as soon as Display resumes.
	 * On all BXT+, SW can load the keys only when the PW#1 is turned on.
	 */
	if (display->platform.haswell || display->platform.broadwell)
		id = HSW_DISP_PW_GLOBAL;
	else
		id = SKL_DISP_PW_1;

	/* PG1 (power well #1) needs to be enabled */
	with_intel_display_rpm(display)
		enabled = intel_display_power_well_is_enabled(display, id);

	/*
	 * Another req for hdcp key loadability is enabled state of pll for
	 * cdclk. Without active crtc we won't land here. So we are assuming that
	 * cdclk is already on.
	 */

	return enabled;
}

static void intel_hdcp_clear_keys(struct intel_display *display)
{
	intel_de_write(display, HDCP_KEY_CONF, HDCP_CLEAR_KEYS_TRIGGER);
	intel_de_write(display, HDCP_KEY_STATUS,
		       HDCP_KEY_LOAD_DONE | HDCP_KEY_LOAD_STATUS | HDCP_FUSE_IN_PROGRESS | HDCP_FUSE_ERROR | HDCP_FUSE_DONE);
}

static int intel_hdcp_load_keys(struct intel_display *display)
{
	int ret;
	u32 val;

	val = intel_de_read(display, HDCP_KEY_STATUS);
	if ((val & HDCP_KEY_LOAD_DONE) && (val & HDCP_KEY_LOAD_STATUS))
		return 0;

	/*
	 * On HSW and BDW HW loads the HDCP1.4 Key when Display comes
	 * out of reset. So if Key is not already loaded, its an error state.
	 */
	if (display->platform.haswell || display->platform.broadwell)
		if (!(intel_de_read(display, HDCP_KEY_STATUS) & HDCP_KEY_LOAD_DONE))
			return -ENXIO;

	/*
	 * Initiate loading the HDCP key from fuses.
	 *
	 * BXT+ platforms, HDCP key needs to be loaded by SW. Only display
	 * version 9 platforms (minus BXT) differ in the key load trigger
	 * process from other platforms. These platforms use the GT Driver
	 * Mailbox interface.
	 */
	if (DISPLAY_VER(display) == 9 && !display->platform.broxton) {
		ret = intel_pcode_write(display->drm, SKL_PCODE_LOAD_HDCP_KEYS, 1);
		if (ret) {
			drm_err(display->drm,
				"Failed to initiate HDCP key load (%d)\n",
				ret);
			return ret;
		}
	} else {
		intel_de_write(display, HDCP_KEY_CONF, HDCP_KEY_LOAD_TRIGGER);
	}

	/* Wait for the keys to load (500us) */
	ret = intel_de_wait_custom(display, HDCP_KEY_STATUS,
				   HDCP_KEY_LOAD_DONE, HDCP_KEY_LOAD_DONE,
				   10, 1, &val);
	if (ret)
		return ret;
	else if (!(val & HDCP_KEY_LOAD_STATUS))
		return -ENXIO;

	/* Send Aksv over to PCH display for use in authentication */
	intel_de_write(display, HDCP_KEY_CONF, HDCP_AKSV_SEND_TRIGGER);

	return 0;
}

/* Returns updated SHA-1 index */
static int intel_write_sha_text(struct intel_display *display, u32 sha_text)
{
	intel_de_write(display, HDCP_SHA_TEXT, sha_text);
	if (intel_de_wait_for_set(display, HDCP_REP_CTL, HDCP_SHA1_READY, 1)) {
		drm_err(display->drm, "Timed out waiting for SHA1 ready\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static
u32 intel_hdcp_get_repeater_ctl(struct intel_display *display,
				enum transcoder cpu_transcoder, enum port port)
{
	if (DISPLAY_VER(display) >= 12) {
		switch (cpu_transcoder) {
		case TRANSCODER_A:
			return HDCP_TRANSA_REP_PRESENT |
			       HDCP_TRANSA_SHA1_M0;
		case TRANSCODER_B:
			return HDCP_TRANSB_REP_PRESENT |
			       HDCP_TRANSB_SHA1_M0;
		case TRANSCODER_C:
			return HDCP_TRANSC_REP_PRESENT |
			       HDCP_TRANSC_SHA1_M0;
		case TRANSCODER_D:
			return HDCP_TRANSD_REP_PRESENT |
			       HDCP_TRANSD_SHA1_M0;
		default:
			drm_err(display->drm, "Unknown transcoder %d\n",
				cpu_transcoder);
			return 0;
		}
	}

	switch (port) {
	case PORT_A:
		return HDCP_DDIA_REP_PRESENT | HDCP_DDIA_SHA1_M0;
	case PORT_B:
		return HDCP_DDIB_REP_PRESENT | HDCP_DDIB_SHA1_M0;
	case PORT_C:
		return HDCP_DDIC_REP_PRESENT | HDCP_DDIC_SHA1_M0;
	case PORT_D:
		return HDCP_DDID_REP_PRESENT | HDCP_DDID_SHA1_M0;
	case PORT_E:
		return HDCP_DDIE_REP_PRESENT | HDCP_DDIE_SHA1_M0;
	default:
		drm_err(display->drm, "Unknown port %d\n", port);
		return 0;
	}
}

static
int intel_hdcp_validate_v_prime(struct intel_connector *connector,
				const struct intel_hdcp_shim *shim,
				u8 *ksv_fifo, u8 num_downstream, u8 *bstatus)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;
	u32 vprime, sha_text, sha_leftovers, rep_ctl;
	int ret, i, j, sha_idx;

	/* Process V' values from the receiver */
	for (i = 0; i < DRM_HDCP_V_PRIME_NUM_PARTS; i++) {
		ret = shim->read_v_prime_part(dig_port, i, &vprime);
		if (ret)
			return ret;
		intel_de_write(display, HDCP_SHA_V_PRIME(i), vprime);
	}

	/*
	 * We need to write the concatenation of all device KSVs, BINFO (DP) ||
	 * BSTATUS (HDMI), and M0 (which is added via HDCP_REP_CTL). This byte
	 * stream is written via the HDCP_SHA_TEXT register in 32-bit
	 * increments. Every 64 bytes, we need to write HDCP_REP_CTL again. This
	 * index will keep track of our progress through the 64 bytes as well as
	 * helping us work the 40-bit KSVs through our 32-bit register.
	 *
	 * NOTE: data passed via HDCP_SHA_TEXT should be big-endian
	 */
	sha_idx = 0;
	sha_text = 0;
	sha_leftovers = 0;
	rep_ctl = intel_hdcp_get_repeater_ctl(display, cpu_transcoder, port);
	intel_de_write(display, HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	for (i = 0; i < num_downstream; i++) {
		unsigned int sha_empty;
		u8 *ksv = &ksv_fifo[i * DRM_HDCP_KSV_LEN];

		/* Fill up the empty slots in sha_text and write it out */
		sha_empty = sizeof(sha_text) - sha_leftovers;
		for (j = 0; j < sha_empty; j++) {
			u8 off = ((sizeof(sha_text) - j - 1 - sha_leftovers) * 8);
			sha_text |= ksv[j] << off;
		}

		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;

		/* Programming guide writes this every 64 bytes */
		sha_idx += sizeof(sha_text);
		if (!(sha_idx % 64))
			intel_de_write(display, HDCP_REP_CTL,
				       rep_ctl | HDCP_SHA1_TEXT_32);

		/* Store the leftover bytes from the ksv in sha_text */
		sha_leftovers = DRM_HDCP_KSV_LEN - sha_empty;
		sha_text = 0;
		for (j = 0; j < sha_leftovers; j++)
			sha_text |= ksv[sha_empty + j] <<
					((sizeof(sha_text) - j - 1) * 8);

		/*
		 * If we still have room in sha_text for more data, continue.
		 * Otherwise, write it out immediately.
		 */
		if (sizeof(sha_text) > sha_leftovers)
			continue;

		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;
		sha_leftovers = 0;
		sha_text = 0;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * We need to write BINFO/BSTATUS, and M0 now. Depending on how many
	 * bytes are leftover from the last ksv, we might be able to fit them
	 * all in sha_text (first 2 cases), or we might need to split them up
	 * into 2 writes (last 2 cases).
	 */
	if (sha_leftovers == 0) {
		/* Write 16 bits of text, 16 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(display,
					   bstatus[0] << 8 | bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 16 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_16);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 1) {
		/* Write 24 bits of text, 8 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_24);
		sha_text |= bstatus[0] << 16 | bstatus[1] << 8;
		/* Only 24-bits of data, must be in the LSB */
		sha_text = (sha_text & 0xffffff00) >> 8;
		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 24 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

	} else if (sha_leftovers == 2) {
		/* Write 32 bits of text */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0] << 8 | bstatus[1];
		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 64 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		for (i = 0; i < 2; i++) {
			ret = intel_write_sha_text(display, 0);
			if (ret < 0)
				return ret;
			sha_idx += sizeof(sha_text);
		}

		/*
		 * Terminate the SHA-1 stream by hand. For the other leftover
		 * cases this is appended by the hardware.
		 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text = DRM_HDCP_SHA1_TERMINATOR << 24;
		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	} else if (sha_leftovers == 3) {
		/* Write 32 bits of text (filled from LSB) */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_32);
		sha_text |= bstatus[0];
		ret = intel_write_sha_text(display, sha_text);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of text (filled from LSB), 24 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_8);
		ret = intel_write_sha_text(display, bstatus[1]);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 32 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_0);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);

		/* Write 8 bits of M0 */
		intel_de_write(display, HDCP_REP_CTL,
			       rep_ctl | HDCP_SHA1_TEXT_24);
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	} else {
		drm_dbg_kms(display->drm, "Invalid number of leftovers %d\n",
			    sha_leftovers);
		return -EINVAL;
	}

	intel_de_write(display, HDCP_REP_CTL, rep_ctl | HDCP_SHA1_TEXT_32);
	/* Fill up to 64-4 bytes with zeros (leave the last write for length) */
	while ((sha_idx % 64) < (64 - sizeof(sha_text))) {
		ret = intel_write_sha_text(display, 0);
		if (ret < 0)
			return ret;
		sha_idx += sizeof(sha_text);
	}

	/*
	 * Last write gets the length of the concatenation in bits. That is:
	 *  - 5 bytes per device
	 *  - 10 bytes for BINFO/BSTATUS(2), M0(8)
	 */
	sha_text = (num_downstream * 5 + 10) * 8;
	ret = intel_write_sha_text(display, sha_text);
	if (ret < 0)
		return ret;

	/* Tell the HW we're done with the hash and wait for it to ACK */
	intel_de_write(display, HDCP_REP_CTL,
		       rep_ctl | HDCP_SHA1_COMPLETE_HASH);
	if (intel_de_wait_for_set(display, HDCP_REP_CTL,
				  HDCP_SHA1_COMPLETE, 1)) {
		drm_err(display->drm, "Timed out waiting for SHA1 complete\n");
		return -ETIMEDOUT;
	}
	if (!(intel_de_read(display, HDCP_REP_CTL) & HDCP_SHA1_V_MATCH)) {
		drm_dbg_kms(display->drm, "SHA-1 mismatch, HDCP failed\n");
		return -ENXIO;
	}

	return 0;
}

/* Implements Part 2 of the HDCP authorization procedure */
static
int intel_hdcp_auth_downstream(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	const struct intel_hdcp_shim *shim = connector->hdcp.shim;
	u8 bstatus[2], num_downstream, *ksv_fifo;
	int ret, i, tries = 3;

	ret = intel_hdcp_poll_ksv_fifo(dig_port, shim);
	if (ret) {
		drm_dbg_kms(display->drm,
			    "KSV list failed to become ready (%d)\n", ret);
		return ret;
	}

	ret = shim->read_bstatus(dig_port, bstatus);
	if (ret)
		return ret;

	if (DRM_HDCP_MAX_DEVICE_EXCEEDED(bstatus[0]) ||
	    DRM_HDCP_MAX_CASCADE_EXCEEDED(bstatus[1])) {
		drm_dbg_kms(display->drm, "Max Topology Limit Exceeded\n");
		return -EPERM;
	}

	/*
	 * When repeater reports 0 device count, HDCP1.4 spec allows disabling
	 * the HDCP encryption. That implies that repeater can't have its own
	 * display. As there is no consumption of encrypted content in the
	 * repeater with 0 downstream devices, we are failing the
	 * authentication.
	 */
	num_downstream = DRM_HDCP_NUM_DOWNSTREAM(bstatus[0]);
	if (num_downstream == 0) {
		drm_dbg_kms(display->drm,
			    "Repeater with zero downstream devices\n");
		return -EINVAL;
	}

	ksv_fifo = kcalloc(DRM_HDCP_KSV_LEN, num_downstream, GFP_KERNEL);
	if (!ksv_fifo) {
		drm_dbg_kms(display->drm, "Out of mem: ksv_fifo\n");
		return -ENOMEM;
	}

	ret = shim->read_ksv_fifo(dig_port, num_downstream, ksv_fifo);
	if (ret)
		goto err;

	if (drm_hdcp_check_ksvs_revoked(display->drm, ksv_fifo,
					num_downstream) > 0) {
		drm_err(display->drm, "Revoked Ksv(s) in ksv_fifo\n");
		ret = -EPERM;
		goto err;
	}

	/*
	 * When V prime mismatches, DP Spec mandates re-read of
	 * V prime atleast twice.
	 */
	for (i = 0; i < tries; i++) {
		ret = intel_hdcp_validate_v_prime(connector, shim,
						  ksv_fifo, num_downstream,
						  bstatus);
		if (!ret)
			break;
	}

	if (i == tries) {
		drm_dbg_kms(display->drm,
			    "V Prime validation failed.(%d)\n", ret);
		goto err;
	}

	drm_dbg_kms(display->drm, "HDCP is enabled (%d downstream devices)\n",
		    num_downstream);
	ret = 0;
err:
	kfree(ksv_fifo);
	return ret;
}

/* Implements Part 1 of the HDCP authorization procedure */
static int intel_hdcp_auth(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	enum transcoder cpu_transcoder = connector->hdcp.cpu_transcoder;
	enum port port = dig_port->base.port;
	unsigned long r0_prime_gen_start;
	int ret, i, tries = 2;
	u32 val;
	union {
		u32 reg[2];
		u8 shim[DRM_HDCP_AN_LEN];
	} an;
	union {
		u32 reg[2];
		u8 shim[DRM_HDCP_KSV_LEN];
	} bksv;
	union {
		u32 reg;
		u8 shim[DRM_HDCP_RI_LEN];
	} ri;
	bool repeater_present, hdcp_capable;

	/*
	 * Detects whether the display is HDCP capable. Although we check for
	 * valid Bksv below, the HDCP over DP spec requires that we check
	 * whether the display supports HDCP before we write An. For HDMI
	 * displays, this is not necessary.
	 */
	if (shim->hdcp_get_capability) {
		ret = shim->hdcp_get_capability(dig_port, &hdcp_capable);
		if (ret)
			return ret;
		if (!hdcp_capable) {
			drm_dbg_kms(display->drm,
				    "Panel is not HDCP capable\n");
			return -EINVAL;
		}
	}

	/* Initialize An with 2 random values and acquire it */
	for (i = 0; i < 2; i++)
		intel_de_write(display,
			       HDCP_ANINIT(display, cpu_transcoder, port),
			       get_random_u32());
	intel_de_write(display, HDCP_CONF(display, cpu_transcoder, port),
		       HDCP_CONF_CAPTURE_AN);

	/* Wait for An to be acquired */
	if (intel_de_wait_for_set(display,
				  HDCP_STATUS(display, cpu_transcoder, port),
				  HDCP_STATUS_AN_READY, 1)) {
		drm_err(display->drm, "Timed out waiting for An\n");
		return -ETIMEDOUT;
	}

	an.reg[0] = intel_de_read(display,
				  HDCP_ANLO(display, cpu_transcoder, port));
	an.reg[1] = intel_de_read(display,
				  HDCP_ANHI(display, cpu_transcoder, port));
	ret = shim->write_an_aksv(dig_port, an.shim);
	if (ret)
		return ret;

	r0_prime_gen_start = jiffies;

	memset(&bksv, 0, sizeof(bksv));

	ret = intel_hdcp_read_valid_bksv(dig_port, shim, bksv.shim);
	if (ret < 0)
		return ret;

	if (drm_hdcp_check_ksvs_revoked(display->drm, bksv.shim, 1) > 0) {
		drm_err(display->drm, "BKSV is revoked\n");
		return -EPERM;
	}

	intel_de_write(display, HDCP_BKSVLO(display, cpu_transcoder, port),
		       bksv.reg[0]);
	intel_de_write(display, HDCP_BKSVHI(display, cpu_transcoder, port),
		       bksv.reg[1]);

	ret = shim->repeater_present(dig_port, &repeater_present);
	if (ret)
		return ret;
	if (repeater_present)
		intel_de_write(display, HDCP_REP_CTL,
			       intel_hdcp_get_repeater_ctl(display, cpu_transcoder, port));

	ret = shim->toggle_signalling(dig_port, cpu_transcoder, true);
	if (ret)
		return ret;

	intel_de_write(display, HDCP_CONF(display, cpu_transcoder, port),
		       HDCP_CONF_AUTH_AND_ENC);

	/* Wait for R0 ready */
	ret = poll_timeout_us(val = intel_de_read(display, HDCP_STATUS(display, cpu_transcoder, port)),
			      val & (HDCP_STATUS_R0_READY | HDCP_STATUS_ENC),
			      100, 1000, false);
	if (ret) {
		drm_err(display->drm, "Timed out waiting for R0 ready\n");
		return -ETIMEDOUT;
	}

	/*
	 * Wait for R0' to become available. The spec says 100ms from Aksv, but
	 * some monitors can take longer than this. We'll set the timeout at
	 * 300ms just to be sure.
	 *
	 * On DP, there's an R0_READY bit available but no such bit
	 * exists on HDMI. Since the upper-bound is the same, we'll just do
	 * the stupid thing instead of polling on one and not the other.
	 */
	wait_remaining_ms_from_jiffies(r0_prime_gen_start, 300);

	tries = 3;

	/*
	 * DP HDCP Spec mandates the two more reattempt to read R0, incase
	 * of R0 mismatch.
	 */
	for (i = 0; i < tries; i++) {
		ri.reg = 0;
		ret = shim->read_ri_prime(dig_port, ri.shim);
		if (ret)
			return ret;
		intel_de_write(display,
			       HDCP_RPRIME(display, cpu_transcoder, port),
			       ri.reg);

		/* Wait for Ri prime match */
		ret = poll_timeout_us(val = intel_de_read(display, HDCP_STATUS(display, cpu_transcoder, port)),
				      val & (HDCP_STATUS_RI_MATCH | HDCP_STATUS_ENC),
				      100, 1000, false);
		if (!ret)
			break;
	}

	if (i == tries) {
		drm_dbg_kms(display->drm,
			    "Timed out waiting for Ri prime match (%x)\n", val);
		return -ETIMEDOUT;
	}

	/* Wait for encryption confirmation */
	if (intel_de_wait_for_set(display,
				  HDCP_STATUS(display, cpu_transcoder, port),
				  HDCP_STATUS_ENC,
				  HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(display->drm, "Timed out waiting for encryption\n");
		return -ETIMEDOUT;
	}

	/* DP MST Auth Part 1 Step 2.a and Step 2.b */
	if (shim->stream_encryption) {
		ret = shim->stream_encryption(connector, true);
		if (ret) {
			drm_err(display->drm, "[CONNECTOR:%d:%s] Failed to enable HDCP 1.4 stream enc\n",
				connector->base.base.id, connector->base.name);
			return ret;
		}
		drm_dbg_kms(display->drm, "HDCP 1.4 transcoder: %s stream encrypted\n",
			    transcoder_name(hdcp->stream_transcoder));
	}

	if (repeater_present)
		return intel_hdcp_auth_downstream(connector);

	drm_dbg_kms(display->drm, "HDCP is enabled (no repeater present)\n");
	return 0;
}

static int _intel_hdcp_disable(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	u32 repeater_ctl;
	int ret;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] HDCP is being disabled...\n",
		    connector->base.base.id, connector->base.name);

	if (hdcp->shim->stream_encryption) {
		ret = hdcp->shim->stream_encryption(connector, false);
		if (ret) {
			drm_err(display->drm, "[CONNECTOR:%d:%s] Failed to disable HDCP 1.4 stream enc\n",
				connector->base.base.id, connector->base.name);
			return ret;
		}
		drm_dbg_kms(display->drm, "HDCP 1.4 transcoder: %s stream encryption disabled\n",
			    transcoder_name(hdcp->stream_transcoder));
		/*
		 * If there are other connectors on this port using HDCP,
		 * don't disable it until it disabled HDCP encryption for
		 * all connectors in MST topology.
		 */
		if (dig_port->hdcp.num_streams > 0)
			return 0;
	}

	hdcp->hdcp_encrypted = false;
	intel_de_write(display, HDCP_CONF(display, cpu_transcoder, port), 0);
	if (intel_de_wait_for_clear(display,
				    HDCP_STATUS(display, cpu_transcoder, port),
				    ~0, HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS)) {
		drm_err(display->drm,
			"Failed to disable HDCP, timeout clearing status\n");
		return -ETIMEDOUT;
	}

	repeater_ctl = intel_hdcp_get_repeater_ctl(display, cpu_transcoder,
						   port);
	intel_de_rmw(display, HDCP_REP_CTL, repeater_ctl, 0);

	ret = hdcp->shim->toggle_signalling(dig_port, cpu_transcoder, false);
	if (ret) {
		drm_err(display->drm, "Failed to disable HDCP signalling\n");
		return ret;
	}

	drm_dbg_kms(display->drm, "HDCP is disabled\n");
	return 0;
}

static int intel_hdcp1_enable(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int i, ret, tries = 3;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] HDCP is being enabled...\n",
		    connector->base.base.id, connector->base.name);

	if (!hdcp_key_loadable(display)) {
		drm_err(display->drm, "HDCP key Load is not possible\n");
		return -ENXIO;
	}

	for (i = 0; i < KEY_LOAD_TRIES; i++) {
		ret = intel_hdcp_load_keys(display);
		if (!ret)
			break;
		intel_hdcp_clear_keys(display);
	}
	if (ret) {
		drm_err(display->drm, "Could not load HDCP keys, (%d)\n",
			ret);
		return ret;
	}

	intel_hdcp_adjust_hdcp_line_rekeying(connector->encoder, hdcp, true);

	/* Incase of authentication failures, HDCP spec expects reauth. */
	for (i = 0; i < tries; i++) {
		ret = intel_hdcp_auth(connector);
		if (!ret) {
			hdcp->hdcp_encrypted = true;
			return 0;
		}

		drm_dbg_kms(display->drm, "HDCP Auth failure (%d)\n", ret);

		/* Ensuring HDCP encryption and signalling are stopped. */
		_intel_hdcp_disable(connector);
	}

	drm_dbg_kms(display->drm,
		    "HDCP authentication failed (%d tries/%d)\n", tries, ret);
	return ret;
}

static struct intel_connector *intel_hdcp_to_connector(struct intel_hdcp *hdcp)
{
	return container_of(hdcp, struct intel_connector, hdcp);
}

static void intel_hdcp_update_value(struct intel_connector *connector,
				    u64 value, bool update_property)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;

	drm_WARN_ON(display->drm, !mutex_is_locked(&hdcp->mutex));

	if (hdcp->value == value)
		return;

	drm_WARN_ON(display->drm, !mutex_is_locked(&dig_port->hdcp.mutex));

	if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		if (!drm_WARN_ON(display->drm, dig_port->hdcp.num_streams == 0))
			dig_port->hdcp.num_streams--;
	} else if (value == DRM_MODE_CONTENT_PROTECTION_ENABLED) {
		dig_port->hdcp.num_streams++;
	}

	hdcp->value = value;
	if (update_property) {
		drm_connector_get(&connector->base);
		if (!queue_work(display->wq.unordered, &hdcp->prop_work))
			drm_connector_put(&connector->base);
	}
}

/* Implements Part 3 of the HDCP authorization procedure */
static int intel_hdcp_check_link(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder;
	int ret = 0;

	mutex_lock(&hdcp->mutex);
	mutex_lock(&dig_port->hdcp.mutex);

	cpu_transcoder = hdcp->cpu_transcoder;

	/* Check_link valid only when HDCP1.4 is enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp_encrypted) {
		ret = -EINVAL;
		goto out;
	}

	if (drm_WARN_ON(display->drm,
			!intel_hdcp_in_use(display, cpu_transcoder, port))) {
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] HDCP link stopped encryption,%x\n",
			connector->base.base.id, connector->base.name,
			intel_de_read(display, HDCP_STATUS(display, cpu_transcoder, port)));
		ret = -ENXIO;
		intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_DESIRED,
					true);
		goto out;
	}

	if (hdcp->shim->check_link(dig_port, connector)) {
		if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
			intel_hdcp_update_value(connector,
				DRM_MODE_CONTENT_PROTECTION_ENABLED, true);
		}
		goto out;
	}

	drm_dbg_kms(display->drm,
		    "[CONNECTOR:%d:%s] HDCP link failed, retrying authentication\n",
		    connector->base.base.id, connector->base.name);

	ret = _intel_hdcp_disable(connector);
	if (ret) {
		drm_err(display->drm, "Failed to disable hdcp (%d)\n", ret);
		intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_DESIRED,
					true);
		goto out;
	}

	ret = intel_hdcp1_enable(connector);
	if (ret) {
		drm_err(display->drm, "Failed to enable hdcp (%d)\n", ret);
		intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_DESIRED,
					true);
		goto out;
	}

out:
	mutex_unlock(&dig_port->hdcp.mutex);
	mutex_unlock(&hdcp->mutex);
	return ret;
}

static void intel_hdcp_prop_work(struct work_struct *work)
{
	struct intel_hdcp *hdcp = container_of(work, struct intel_hdcp,
					       prop_work);
	struct intel_connector *connector = intel_hdcp_to_connector(hdcp);
	struct intel_display *display = to_intel_display(connector);

	drm_modeset_lock(&display->drm->mode_config.connection_mutex, NULL);
	mutex_lock(&hdcp->mutex);

	/*
	 * This worker is only used to flip between ENABLED/DESIRED. Either of
	 * those to UNDESIRED is handled by core. If value == UNDESIRED,
	 * we're running just after hdcp has been disabled, so just exit
	 */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		drm_hdcp_update_content_protection(&connector->base,
						   hdcp->value);

	mutex_unlock(&hdcp->mutex);
	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	drm_connector_put(&connector->base);
}

bool is_hdcp_supported(struct intel_display *display, enum port port)
{
	return DISPLAY_RUNTIME_INFO(display)->has_hdcp &&
		(DISPLAY_VER(display) >= 12 || port < PORT_E);
}

static int
hdcp2_prepare_ake_init(struct intel_connector *connector,
		       struct hdcp2_ake_init *ake_data)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->initiate_hdcp2_session(arbiter->hdcp_dev, data, ake_data);
	if (ret)
		drm_dbg_kms(display->drm, "Prepare_ake_init failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_verify_rx_cert_prepare_km(struct intel_connector *connector,
				struct hdcp2_ake_send_cert *rx_cert,
				bool *paired,
				struct hdcp2_ake_no_stored_km *ek_pub_km,
				size_t *msg_sz)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->verify_receiver_cert_prepare_km(arbiter->hdcp_dev, data,
							 rx_cert, paired,
							 ek_pub_km, msg_sz);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Verify rx_cert failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int hdcp2_verify_hprime(struct intel_connector *connector,
			       struct hdcp2_ake_send_hprime *rx_hprime)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->verify_hprime(arbiter->hdcp_dev, data, rx_hprime);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Verify hprime failed. %d\n", ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_store_pairing_info(struct intel_connector *connector,
			 struct hdcp2_ake_send_pairing_info *pairing_info)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->store_pairing_info(arbiter->hdcp_dev, data, pairing_info);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Store pairing info failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_prepare_lc_init(struct intel_connector *connector,
		      struct hdcp2_lc_init *lc_init)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->initiate_locality_check(arbiter->hdcp_dev, data, lc_init);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Prepare lc_init failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_verify_lprime(struct intel_connector *connector,
		    struct hdcp2_lc_send_lprime *rx_lprime)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->verify_lprime(arbiter->hdcp_dev, data, rx_lprime);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Verify L_Prime failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int hdcp2_prepare_skey(struct intel_connector *connector,
			      struct hdcp2_ske_send_eks *ske_data)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->get_session_key(arbiter->hdcp_dev, data, ske_data);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Get session key failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_verify_rep_topology_prepare_ack(struct intel_connector *connector,
				      struct hdcp2_rep_send_receiverid_list
								*rep_topology,
				      struct hdcp2_rep_send_ack *rep_send_ack)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->repeater_check_flow_prepare_ack(arbiter->hdcp_dev,
							    data,
							    rep_topology,
							    rep_send_ack);
	if (ret < 0)
		drm_dbg_kms(display->drm,
			    "Verify rep topology failed. %d\n", ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int
hdcp2_verify_mprime(struct intel_connector *connector,
		    struct hdcp2_rep_stream_ready *stream_ready)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->verify_mprime(arbiter->hdcp_dev, data, stream_ready);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Verify mprime failed. %d\n", ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int hdcp2_authenticate_port(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->enable_hdcp_authentication(arbiter->hdcp_dev, data);
	if (ret < 0)
		drm_dbg_kms(display->drm, "Enable hdcp auth failed. %d\n",
			    ret);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int hdcp2_close_session(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct i915_hdcp_arbiter *arbiter;
	int ret;

	mutex_lock(&display->hdcp.hdcp_mutex);
	arbiter = display->hdcp.arbiter;

	if (!arbiter || !arbiter->ops) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return -EINVAL;
	}

	ret = arbiter->ops->close_hdcp_session(arbiter->hdcp_dev,
					     &dig_port->hdcp.port_data);
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return ret;
}

static int hdcp2_deauthenticate_port(struct intel_connector *connector)
{
	return hdcp2_close_session(connector);
}

/* Authentication flow starts from here */
static int hdcp2_authentication_key_exchange(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port =
		intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_ake_init ake_init;
		struct hdcp2_ake_send_cert send_cert;
		struct hdcp2_ake_no_stored_km no_stored_km;
		struct hdcp2_ake_send_hprime send_hprime;
		struct hdcp2_ake_send_pairing_info pairing_info;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	size_t size;
	int ret, i, max_retries;

	/* Init for seq_num */
	hdcp->seq_num_v = 0;
	hdcp->seq_num_m = 0;

	if (intel_encoder_is_dp(&dig_port->base) ||
	    intel_encoder_is_mst(&dig_port->base))
		max_retries = 10;
	else
		max_retries = 1;

	ret = hdcp2_prepare_ake_init(connector, &msgs.ake_init);
	if (ret < 0)
		return ret;

	/*
	 * Retry the first read and write to downstream at least 10 times
	 * with a 50ms delay if not hdcp2 capable for DP/DPMST encoders
	 * (dock decides to stop advertising hdcp2 capability for some reason).
	 * The reason being that during suspend resume dock usually keeps the
	 * HDCP2 registers inaccessible causing AUX error. This wouldn't be a
	 * big problem if the userspace just kept retrying with some delay while
	 * it continues to play low value content but most userspace applications
	 * end up throwing an error when it receives one from KMD. This makes
	 * sure we give the dock and the sink devices to complete its power cycle
	 * and then try HDCP authentication. The values of 10 and delay of 50ms
	 * was decided based on multiple trial and errors.
	 */
	for (i = 0; i < max_retries; i++) {
		if (!intel_hdcp2_get_capability(connector)) {
			msleep(50);
			continue;
		}

		ret = shim->write_2_2_msg(connector, &msgs.ake_init,
					  sizeof(msgs.ake_init));
		if (ret < 0)
			continue;

		ret = shim->read_2_2_msg(connector, HDCP_2_2_AKE_SEND_CERT,
					 &msgs.send_cert, sizeof(msgs.send_cert));
		if (ret > 0)
			break;
	}

	if (ret < 0)
		return ret;

	if (msgs.send_cert.rx_caps[0] != HDCP_2_2_RX_CAPS_VERSION_VAL) {
		drm_dbg_kms(display->drm, "cert.rx_caps dont claim HDCP2.2\n");
		return -EINVAL;
	}

	hdcp->is_repeater = HDCP_2_2_RX_REPEATER(msgs.send_cert.rx_caps[2]);

	if (drm_hdcp_check_ksvs_revoked(display->drm,
					msgs.send_cert.cert_rx.receiver_id,
					1) > 0) {
		drm_err(display->drm, "Receiver ID is revoked\n");
		return -EPERM;
	}

	/*
	 * Here msgs.no_stored_km will hold msgs corresponding to the km
	 * stored also.
	 */
	ret = hdcp2_verify_rx_cert_prepare_km(connector, &msgs.send_cert,
					      &hdcp->is_paired,
					      &msgs.no_stored_km, &size);
	if (ret < 0)
		return ret;

	ret = shim->write_2_2_msg(connector, &msgs.no_stored_km, size);
	if (ret < 0)
		return ret;

	ret = shim->read_2_2_msg(connector, HDCP_2_2_AKE_SEND_HPRIME,
				 &msgs.send_hprime, sizeof(msgs.send_hprime));
	if (ret < 0)
		return ret;

	ret = hdcp2_verify_hprime(connector, &msgs.send_hprime);
	if (ret < 0)
		return ret;

	if (!hdcp->is_paired) {
		/* Pairing is required */
		ret = shim->read_2_2_msg(connector,
					 HDCP_2_2_AKE_SEND_PAIRING_INFO,
					 &msgs.pairing_info,
					 sizeof(msgs.pairing_info));
		if (ret < 0)
			return ret;

		ret = hdcp2_store_pairing_info(connector, &msgs.pairing_info);
		if (ret < 0)
			return ret;
		hdcp->is_paired = true;
	}

	return 0;
}

static int hdcp2_locality_check(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_lc_init lc_init;
		struct hdcp2_lc_send_lprime send_lprime;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int tries = HDCP2_LC_RETRY_CNT, ret, i;

	for (i = 0; i < tries; i++) {
		ret = hdcp2_prepare_lc_init(connector, &msgs.lc_init);
		if (ret < 0)
			continue;

		ret = shim->write_2_2_msg(connector, &msgs.lc_init,
				      sizeof(msgs.lc_init));
		if (ret < 0)
			continue;

		ret = shim->read_2_2_msg(connector,
					 HDCP_2_2_LC_SEND_LPRIME,
					 &msgs.send_lprime,
					 sizeof(msgs.send_lprime));
		if (ret < 0)
			continue;

		ret = hdcp2_verify_lprime(connector, &msgs.send_lprime);
		if (!ret)
			break;
	}

	return ret;
}

static int hdcp2_session_key_exchange(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct hdcp2_ske_send_eks send_eks;
	int ret;

	ret = hdcp2_prepare_skey(connector, &send_eks);
	if (ret < 0)
		return ret;

	ret = hdcp->shim->write_2_2_msg(connector, &send_eks,
					sizeof(send_eks));
	if (ret < 0)
		return ret;

	return 0;
}

static
int _hdcp2_propagate_stream_management_info(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_stream_manage stream_manage;
		struct hdcp2_rep_stream_ready stream_ready;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret, streams_size_delta, i;

	if (connector->hdcp.seq_num_m > HDCP_2_2_SEQ_NUM_MAX)
		return -ERANGE;

	/* Prepare RepeaterAuth_Stream_Manage msg */
	msgs.stream_manage.msg_id = HDCP_2_2_REP_STREAM_MANAGE;
	drm_hdcp_cpu_to_be24(msgs.stream_manage.seq_num_m, hdcp->seq_num_m);

	msgs.stream_manage.k = cpu_to_be16(data->k);

	for (i = 0; i < data->k; i++) {
		msgs.stream_manage.streams[i].stream_id = data->streams[i].stream_id;
		msgs.stream_manage.streams[i].stream_type = data->streams[i].stream_type;
	}

	streams_size_delta = (HDCP_2_2_MAX_CONTENT_STREAMS_CNT - data->k) *
				sizeof(struct hdcp2_streamid_type);
	/* Send it to Repeater */
	ret = shim->write_2_2_msg(connector, &msgs.stream_manage,
				  sizeof(msgs.stream_manage) - streams_size_delta);
	if (ret < 0)
		goto out;

	ret = shim->read_2_2_msg(connector, HDCP_2_2_REP_STREAM_READY,
				 &msgs.stream_ready, sizeof(msgs.stream_ready));
	if (ret < 0)
		goto out;

	data->seq_num_m = hdcp->seq_num_m;

	ret = hdcp2_verify_mprime(connector, &msgs.stream_ready);

out:
	hdcp->seq_num_m++;

	return ret;
}

static
int hdcp2_authenticate_repeater_topology(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	union {
		struct hdcp2_rep_send_receiverid_list recvid_list;
		struct hdcp2_rep_send_ack rep_ack;
	} msgs;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	u32 seq_num_v, device_cnt;
	u8 *rx_info;
	int ret;

	ret = shim->read_2_2_msg(connector, HDCP_2_2_REP_SEND_RECVID_LIST,
				 &msgs.recvid_list, sizeof(msgs.recvid_list));
	if (ret < 0)
		return ret;

	rx_info = msgs.recvid_list.rx_info;

	if (HDCP_2_2_MAX_CASCADE_EXCEEDED(rx_info[1]) ||
	    HDCP_2_2_MAX_DEVS_EXCEEDED(rx_info[1])) {
		drm_dbg_kms(display->drm, "Topology Max Size Exceeded\n");
		return -EINVAL;
	}

	/*
	 * MST topology is not Type 1 capable if it contains a downstream
	 * device that is only HDCP 1.x or Legacy HDCP 2.0/2.1 compliant.
	 */
	dig_port->hdcp.mst_type1_capable =
		!HDCP_2_2_HDCP1_DEVICE_CONNECTED(rx_info[1]) &&
		!HDCP_2_2_HDCP_2_0_REP_CONNECTED(rx_info[1]);

	if (!dig_port->hdcp.mst_type1_capable && hdcp->content_type) {
		drm_dbg_kms(display->drm,
			    "HDCP1.x or 2.0 Legacy Device Downstream\n");
		return -EINVAL;
	}

	/* Converting and Storing the seq_num_v to local variable as DWORD */
	seq_num_v =
		drm_hdcp_be24_to_cpu((const u8 *)msgs.recvid_list.seq_num_v);

	if (!hdcp->hdcp2_encrypted && seq_num_v) {
		drm_dbg_kms(display->drm,
			    "Non zero Seq_num_v at first RecvId_List msg\n");
		return -EINVAL;
	}

	if (seq_num_v < hdcp->seq_num_v) {
		/* Roll over of the seq_num_v from repeater. Reauthenticate. */
		drm_dbg_kms(display->drm, "Seq_num_v roll over.\n");
		return -EINVAL;
	}

	device_cnt = (HDCP_2_2_DEV_COUNT_HI(rx_info[0]) << 4 |
		      HDCP_2_2_DEV_COUNT_LO(rx_info[1]));
	if (drm_hdcp_check_ksvs_revoked(display->drm,
					msgs.recvid_list.receiver_ids,
					device_cnt) > 0) {
		drm_err(display->drm, "Revoked receiver ID(s) is in list\n");
		return -EPERM;
	}

	ret = hdcp2_verify_rep_topology_prepare_ack(connector,
						    &msgs.recvid_list,
						    &msgs.rep_ack);
	if (ret < 0)
		return ret;

	hdcp->seq_num_v = seq_num_v;
	ret = shim->write_2_2_msg(connector, &msgs.rep_ack,
				  sizeof(msgs.rep_ack));
	if (ret < 0)
		return ret;

	return 0;
}

static int hdcp2_authenticate_sink(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	const struct intel_hdcp_shim *shim = hdcp->shim;
	int ret;

	ret = hdcp2_authentication_key_exchange(connector);
	if (ret < 0) {
		drm_dbg_kms(display->drm, "AKE Failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_locality_check(connector);
	if (ret < 0) {
		drm_dbg_kms(display->drm,
			    "Locality Check failed. Err : %d\n", ret);
		return ret;
	}

	ret = hdcp2_session_key_exchange(connector);
	if (ret < 0) {
		drm_dbg_kms(display->drm, "SKE Failed. Err : %d\n", ret);
		return ret;
	}

	if (shim->config_stream_type) {
		ret = shim->config_stream_type(connector,
					       hdcp->is_repeater,
					       hdcp->content_type);
		if (ret < 0)
			return ret;
	}

	if (hdcp->is_repeater) {
		ret = hdcp2_authenticate_repeater_topology(connector);
		if (ret < 0) {
			drm_dbg_kms(display->drm,
				    "Repeater Auth Failed. Err: %d\n", ret);
			return ret;
		}
	}

	return ret;
}

static int hdcp2_enable_stream_encryption(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	enum port port = dig_port->base.port;
	int ret = 0;

	if (!(intel_de_read(display, HDCP2_STATUS(display, cpu_transcoder, port)) &
			    LINK_ENCRYPTION_STATUS)) {
		drm_err(display->drm, "[CONNECTOR:%d:%s] HDCP 2.2 Link is not encrypted\n",
			connector->base.base.id, connector->base.name);
		ret = -EPERM;
		goto link_recover;
	}

	if (hdcp->shim->stream_2_2_encryption) {
		ret = hdcp->shim->stream_2_2_encryption(connector, true);
		if (ret) {
			drm_err(display->drm, "[CONNECTOR:%d:%s] Failed to enable HDCP 2.2 stream enc\n",
				connector->base.base.id, connector->base.name);
			return ret;
		}
		drm_dbg_kms(display->drm, "HDCP 2.2 transcoder: %s stream encrypted\n",
			    transcoder_name(hdcp->stream_transcoder));
	}

	return 0;

link_recover:
	if (hdcp2_deauthenticate_port(connector) < 0)
		drm_dbg_kms(display->drm, "Port deauth failed.\n");

	dig_port->hdcp.auth_status = false;
	data->k = 0;

	return ret;
}

static int hdcp2_enable_encryption(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	int ret;

	drm_WARN_ON(display->drm,
		    intel_de_read(display, HDCP2_STATUS(display, cpu_transcoder, port)) &
		    LINK_ENCRYPTION_STATUS);
	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(dig_port, cpu_transcoder,
						    true);
		if (ret) {
			drm_err(display->drm,
				"Failed to enable HDCP signalling. %d\n",
				ret);
			return ret;
		}
	}

	if (intel_de_read(display, HDCP2_STATUS(display, cpu_transcoder, port)) &
	    LINK_AUTH_STATUS)
		/* Link is Authenticated. Now set for Encryption */
		intel_de_rmw(display, HDCP2_CTL(display, cpu_transcoder, port),
			     0, CTL_LINK_ENCRYPTION_REQ);

	ret = intel_de_wait_for_set(display,
				    HDCP2_STATUS(display, cpu_transcoder,
						 port),
				    LINK_ENCRYPTION_STATUS,
				    HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);
	dig_port->hdcp.auth_status = true;

	return ret;
}

static int hdcp2_disable_encryption(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder = hdcp->cpu_transcoder;
	int ret;

	drm_WARN_ON(display->drm,
		    !(intel_de_read(display, HDCP2_STATUS(display, cpu_transcoder, port)) &
				    LINK_ENCRYPTION_STATUS));

	intel_de_rmw(display, HDCP2_CTL(display, cpu_transcoder, port),
		     CTL_LINK_ENCRYPTION_REQ, 0);

	ret = intel_de_wait_for_clear(display,
				      HDCP2_STATUS(display, cpu_transcoder,
						   port),
				      LINK_ENCRYPTION_STATUS,
				      HDCP_ENCRYPT_STATUS_CHANGE_TIMEOUT_MS);
	if (ret == -ETIMEDOUT)
		drm_dbg_kms(display->drm, "Disable Encryption Timedout");

	if (hdcp->shim->toggle_signalling) {
		ret = hdcp->shim->toggle_signalling(dig_port, cpu_transcoder,
						    false);
		if (ret) {
			drm_err(display->drm,
				"Failed to disable HDCP signalling. %d\n",
				ret);
			return ret;
		}
	}

	return ret;
}

static int
hdcp2_propagate_stream_management_info(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	int i, tries = 3, ret;

	if (!connector->hdcp.is_repeater)
		return 0;

	for (i = 0; i < tries; i++) {
		ret = _hdcp2_propagate_stream_management_info(connector);
		if (!ret)
			break;

		/* Lets restart the auth incase of seq_num_m roll over */
		if (connector->hdcp.seq_num_m > HDCP_2_2_SEQ_NUM_MAX) {
			drm_dbg_kms(display->drm,
				    "seq_num_m roll over.(%d)\n", ret);
			break;
		}

		drm_dbg_kms(display->drm,
			    "HDCP2 stream management %d of %d Failed.(%d)\n",
			    i + 1, tries, ret);
	}

	return ret;
}

static int hdcp2_authenticate_and_encrypt(struct intel_atomic_state *state,
					  struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	int ret = 0, i, tries = 3;

	for (i = 0; i < tries && !dig_port->hdcp.auth_status; i++) {
		ret = hdcp2_authenticate_sink(connector);
		if (!ret) {
			ret = intel_hdcp_prepare_streams(state, connector);
			if (ret) {
				drm_dbg_kms(display->drm,
					    "Prepare stream failed.(%d)\n",
					    ret);
				break;
			}

			ret = hdcp2_propagate_stream_management_info(connector);
			if (ret) {
				drm_dbg_kms(display->drm,
					    "Stream management failed.(%d)\n",
					    ret);
				break;
			}

			ret = hdcp2_authenticate_port(connector);
			if (!ret)
				break;
			drm_dbg_kms(display->drm, "HDCP2 port auth failed.(%d)\n",
				    ret);
		}

		/* Clearing the mei hdcp session */
		drm_dbg_kms(display->drm, "HDCP2.2 Auth %d of %d Failed.(%d)\n",
			    i + 1, tries, ret);
		if (hdcp2_deauthenticate_port(connector) < 0)
			drm_dbg_kms(display->drm, "Port deauth failed.\n");
	}

	if (!ret && !dig_port->hdcp.auth_status) {
		/*
		 * Ensuring the required 200mSec min time interval between
		 * Session Key Exchange and encryption.
		 */
		msleep(HDCP_2_2_DELAY_BEFORE_ENCRYPTION_EN);
		ret = hdcp2_enable_encryption(connector);
		if (ret < 0) {
			drm_dbg_kms(display->drm,
				    "Encryption Enable Failed.(%d)\n", ret);
			if (hdcp2_deauthenticate_port(connector) < 0)
				drm_dbg_kms(display->drm, "Port deauth failed.\n");
		}
	}

	if (!ret)
		ret = hdcp2_enable_stream_encryption(connector);

	return ret;
}

static int _intel_hdcp2_enable(struct intel_atomic_state *state,
			       struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] HDCP2.2 is being enabled. Type: %d\n",
		    connector->base.base.id, connector->base.name,
		    hdcp->content_type);

	intel_hdcp_adjust_hdcp_line_rekeying(connector->encoder, hdcp, false);

	ret = hdcp2_authenticate_and_encrypt(state, connector);
	if (ret) {
		drm_dbg_kms(display->drm, "HDCP2 Type%d  Enabling Failed. (%d)\n",
			    hdcp->content_type, ret);
		return ret;
	}

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] HDCP2.2 is enabled. Type %d\n",
		    connector->base.base.id, connector->base.name,
		    hdcp->content_type);

	hdcp->hdcp2_encrypted = true;
	return 0;
}

static int
_intel_hdcp2_disable(struct intel_connector *connector, bool hdcp2_link_recovery)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	drm_dbg_kms(display->drm, "[CONNECTOR:%d:%s] HDCP2.2 is being Disabled\n",
		    connector->base.base.id, connector->base.name);

	if (hdcp->shim->stream_2_2_encryption) {
		ret = hdcp->shim->stream_2_2_encryption(connector, false);
		if (ret) {
			drm_err(display->drm, "[CONNECTOR:%d:%s] Failed to disable HDCP 2.2 stream enc\n",
				connector->base.base.id, connector->base.name);
			return ret;
		}
		drm_dbg_kms(display->drm, "HDCP 2.2 transcoder: %s stream encryption disabled\n",
			    transcoder_name(hdcp->stream_transcoder));

		if (dig_port->hdcp.num_streams > 0 && !hdcp2_link_recovery)
			return 0;
	}

	ret = hdcp2_disable_encryption(connector);

	if (hdcp2_deauthenticate_port(connector) < 0)
		drm_dbg_kms(display->drm, "Port deauth failed.\n");

	connector->hdcp.hdcp2_encrypted = false;
	dig_port->hdcp.auth_status = false;
	data->k = 0;

	return ret;
}

/* Implements the Link Integrity Check for HDCP2.2 */
static int intel_hdcp2_check_link(struct intel_connector *connector)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	enum port port = dig_port->base.port;
	enum transcoder cpu_transcoder;
	int ret = 0;

	mutex_lock(&hdcp->mutex);
	mutex_lock(&dig_port->hdcp.mutex);
	cpu_transcoder = hdcp->cpu_transcoder;

	/* hdcp2_check_link is expected only when HDCP2.2 is Enabled */
	if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED ||
	    !hdcp->hdcp2_encrypted) {
		ret = -EINVAL;
		goto out;
	}

	if (drm_WARN_ON(display->drm,
			!intel_hdcp2_in_use(display, cpu_transcoder, port))) {
		drm_err(display->drm,
			"HDCP2.2 link stopped the encryption, %x\n",
			intel_de_read(display, HDCP2_STATUS(display, cpu_transcoder, port)));
		ret = -ENXIO;
		_intel_hdcp2_disable(connector, true);
		intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_DESIRED,
					true);
		goto out;
	}

	ret = hdcp->shim->check_2_2_link(dig_port, connector);
	if (ret == HDCP_LINK_PROTECTED) {
		if (hdcp->value != DRM_MODE_CONTENT_PROTECTION_UNDESIRED) {
			intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_ENABLED,
					true);
		}
		goto out;
	}

	if (ret == HDCP_TOPOLOGY_CHANGE) {
		if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
			goto out;

		drm_dbg_kms(display->drm,
			    "HDCP2.2 Downstream topology change\n");

		ret = hdcp2_authenticate_repeater_topology(connector);
		if (!ret) {
			intel_hdcp_update_value(connector,
						DRM_MODE_CONTENT_PROTECTION_ENABLED,
						true);
			goto out;
		}

		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] Repeater topology auth failed.(%d)\n",
			    connector->base.base.id, connector->base.name,
			    ret);
	} else {
		drm_dbg_kms(display->drm,
			    "[CONNECTOR:%d:%s] HDCP2.2 link failed, retrying auth\n",
			    connector->base.base.id, connector->base.name);
	}

	ret = _intel_hdcp2_disable(connector, true);
	if (ret) {
		drm_err(display->drm,
			"[CONNECTOR:%d:%s] Failed to disable hdcp2.2 (%d)\n",
			connector->base.base.id, connector->base.name, ret);
		intel_hdcp_update_value(connector,
				DRM_MODE_CONTENT_PROTECTION_DESIRED, true);
		goto out;
	}

	intel_hdcp_update_value(connector,
				DRM_MODE_CONTENT_PROTECTION_DESIRED, true);
out:
	mutex_unlock(&dig_port->hdcp.mutex);
	mutex_unlock(&hdcp->mutex);
	return ret;
}

static void intel_hdcp_check_work(struct work_struct *work)
{
	struct intel_hdcp *hdcp = container_of(to_delayed_work(work),
					       struct intel_hdcp,
					       check_work);
	struct intel_connector *connector = intel_hdcp_to_connector(hdcp);
	struct intel_display *display = to_intel_display(connector);

	if (drm_connector_is_unregistered(&connector->base))
		return;

	if (!intel_hdcp2_check_link(connector))
		queue_delayed_work(display->wq.unordered, &hdcp->check_work,
				   DRM_HDCP2_CHECK_PERIOD_MS);
	else if (!intel_hdcp_check_link(connector))
		queue_delayed_work(display->wq.unordered, &hdcp->check_work,
				   DRM_HDCP_CHECK_PERIOD_MS);
}

static int i915_hdcp_component_bind(struct device *drv_kdev,
				    struct device *mei_kdev, void *data)
{
	struct intel_display *display = to_intel_display(drv_kdev);

	drm_dbg(display->drm, "I915 HDCP comp bind\n");
	mutex_lock(&display->hdcp.hdcp_mutex);
	display->hdcp.arbiter = (struct i915_hdcp_arbiter *)data;
	display->hdcp.arbiter->hdcp_dev = mei_kdev;
	mutex_unlock(&display->hdcp.hdcp_mutex);

	return 0;
}

static void i915_hdcp_component_unbind(struct device *drv_kdev,
				       struct device *mei_kdev, void *data)
{
	struct intel_display *display = to_intel_display(drv_kdev);

	drm_dbg(display->drm, "I915 HDCP comp unbind\n");
	mutex_lock(&display->hdcp.hdcp_mutex);
	display->hdcp.arbiter = NULL;
	mutex_unlock(&display->hdcp.hdcp_mutex);
}

static const struct component_ops i915_hdcp_ops = {
	.bind   = i915_hdcp_component_bind,
	.unbind = i915_hdcp_component_unbind,
};

static enum hdcp_ddi intel_get_hdcp_ddi_index(enum port port)
{
	switch (port) {
	case PORT_A:
		return HDCP_DDI_A;
	case PORT_B ... PORT_F:
		return (enum hdcp_ddi)port;
	default:
		return HDCP_DDI_INVALID_PORT;
	}
}

static enum hdcp_transcoder intel_get_hdcp_transcoder(enum transcoder cpu_transcoder)
{
	switch (cpu_transcoder) {
	case TRANSCODER_A ... TRANSCODER_D:
		return (enum hdcp_transcoder)(cpu_transcoder | 0x10);
	default: /* eDP, DSI TRANSCODERS are non HDCP capable */
		return HDCP_INVALID_TRANSCODER;
	}
}

static int initialize_hdcp_port_data(struct intel_connector *connector,
				     struct intel_digital_port *dig_port,
				     const struct intel_hdcp_shim *shim)
{
	struct intel_display *display = to_intel_display(connector);
	struct hdcp_port_data *data = &dig_port->hdcp.port_data;
	enum port port = dig_port->base.port;

	if (DISPLAY_VER(display) < 12)
		data->hdcp_ddi = intel_get_hdcp_ddi_index(port);
	else
		/*
		 * As per ME FW API expectation, for GEN 12+, hdcp_ddi is filled
		 * with zero(INVALID PORT index).
		 */
		data->hdcp_ddi = HDCP_DDI_INVALID_PORT;

	/*
	 * As associated transcoder is set and modified at modeset, here hdcp_transcoder
	 * is initialized to zero (invalid transcoder index). This will be
	 * retained for <Gen12 forever.
	 */
	data->hdcp_transcoder = HDCP_INVALID_TRANSCODER;

	data->port_type = (u8)HDCP_PORT_TYPE_INTEGRATED;
	data->protocol = (u8)shim->protocol;

	if (!data->streams)
		data->streams = kcalloc(INTEL_NUM_PIPES(display),
					sizeof(struct hdcp2_streamid_type),
					GFP_KERNEL);
	if (!data->streams) {
		drm_err(display->drm, "Out of Memory\n");
		return -ENOMEM;
	}

	return 0;
}

static bool is_hdcp2_supported(struct intel_display *display)
{
	if (USE_HDCP_GSC(display))
		return true;

	if (!IS_ENABLED(CONFIG_INTEL_MEI_HDCP))
		return false;

	return DISPLAY_VER(display) >= 10 ||
		display->platform.kabylake ||
		display->platform.coffeelake ||
		display->platform.cometlake;
}

void intel_hdcp_component_init(struct intel_display *display)
{
	int ret;

	if (!is_hdcp2_supported(display))
		return;

	mutex_lock(&display->hdcp.hdcp_mutex);
	drm_WARN_ON(display->drm, display->hdcp.comp_added);

	display->hdcp.comp_added = true;
	mutex_unlock(&display->hdcp.hdcp_mutex);
	if (USE_HDCP_GSC(display))
		ret = intel_hdcp_gsc_init(display);
	else
		ret = component_add_typed(display->drm->dev, &i915_hdcp_ops,
					  I915_COMPONENT_HDCP);

	if (ret < 0) {
		drm_dbg_kms(display->drm, "Failed at fw component add(%d)\n",
			    ret);
		mutex_lock(&display->hdcp.hdcp_mutex);
		display->hdcp.comp_added = false;
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return;
	}
}

static void intel_hdcp2_init(struct intel_connector *connector,
			     struct intel_digital_port *dig_port,
			     const struct intel_hdcp_shim *shim)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	ret = initialize_hdcp_port_data(connector, dig_port, shim);
	if (ret) {
		drm_dbg_kms(display->drm, "Mei hdcp data init failed\n");
		return;
	}

	hdcp->hdcp2_supported = true;
}

int intel_hdcp_init(struct intel_connector *connector,
		    struct intel_digital_port *dig_port,
		    const struct intel_hdcp_shim *shim)
{
	struct intel_display *display = to_intel_display(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret;

	if (!shim)
		return -EINVAL;

	if (is_hdcp2_supported(display))
		intel_hdcp2_init(connector, dig_port, shim);

	ret = drm_connector_attach_content_protection_property(&connector->base,
							       hdcp->hdcp2_supported);
	if (ret) {
		hdcp->hdcp2_supported = false;
		kfree(dig_port->hdcp.port_data.streams);
		return ret;
	}

	hdcp->shim = shim;
	mutex_init(&hdcp->mutex);
	INIT_DELAYED_WORK(&hdcp->check_work, intel_hdcp_check_work);
	INIT_WORK(&hdcp->prop_work, intel_hdcp_prop_work);
	init_waitqueue_head(&hdcp->cp_irq_queue);

	return 0;
}

static int _intel_hdcp_enable(struct intel_atomic_state *state,
			      struct intel_encoder *encoder,
			      const struct intel_crtc_state *pipe_config,
			      const struct drm_connector_state *conn_state)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	unsigned long check_link_interval = DRM_HDCP_CHECK_PERIOD_MS;
	int ret = -EINVAL;

	if (!hdcp->shim)
		return -ENOENT;

	mutex_lock(&hdcp->mutex);
	mutex_lock(&dig_port->hdcp.mutex);
	drm_WARN_ON(display->drm,
		    hdcp->value == DRM_MODE_CONTENT_PROTECTION_ENABLED);
	hdcp->content_type = (u8)conn_state->hdcp_content_type;

	if (intel_crtc_has_type(pipe_config, INTEL_OUTPUT_DP_MST)) {
		hdcp->cpu_transcoder = pipe_config->mst_master_transcoder;
		hdcp->stream_transcoder = pipe_config->cpu_transcoder;
	} else {
		hdcp->cpu_transcoder = pipe_config->cpu_transcoder;
		hdcp->stream_transcoder = INVALID_TRANSCODER;
	}

	if (DISPLAY_VER(display) >= 12)
		dig_port->hdcp.port_data.hdcp_transcoder =
			intel_get_hdcp_transcoder(hdcp->cpu_transcoder);

	/*
	 * Considering that HDCP2.2 is more secure than HDCP1.4, If the setup
	 * is capable of HDCP2.2, it is preferred to use HDCP2.2.
	 */
	if (!hdcp->force_hdcp14 && intel_hdcp2_get_capability(connector)) {
		ret = _intel_hdcp2_enable(state, connector);
		if (!ret)
			check_link_interval =
				DRM_HDCP2_CHECK_PERIOD_MS;
	}

	if (hdcp->force_hdcp14)
		drm_dbg_kms(display->drm, "Forcing HDCP 1.4\n");

	/*
	 * When HDCP2.2 fails and Content Type is not Type1, HDCP1.4 will
	 * be attempted.
	 */
	if (ret && intel_hdcp_get_capability(connector) &&
	    hdcp->content_type != DRM_MODE_HDCP_CONTENT_TYPE1) {
		ret = intel_hdcp1_enable(connector);
	}

	if (!ret) {
		queue_delayed_work(display->wq.unordered, &hdcp->check_work,
				   check_link_interval);
		intel_hdcp_update_value(connector,
					DRM_MODE_CONTENT_PROTECTION_ENABLED,
					true);
	}

	mutex_unlock(&dig_port->hdcp.mutex);
	mutex_unlock(&hdcp->mutex);
	return ret;
}

void intel_hdcp_enable(struct intel_atomic_state *state,
		       struct intel_encoder *encoder,
		       const struct intel_crtc_state *crtc_state,
		       const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector =
		to_intel_connector(conn_state->connector);
	struct intel_hdcp *hdcp = &connector->hdcp;

	/*
	 * Enable hdcp if it's desired or if userspace is enabled and
	 * driver set its state to undesired
	 */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED ||
	    (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_ENABLED && hdcp->value ==
	    DRM_MODE_CONTENT_PROTECTION_UNDESIRED))
		_intel_hdcp_enable(state, encoder, crtc_state, conn_state);
}

int intel_hdcp_disable(struct intel_connector *connector)
{
	struct intel_digital_port *dig_port = intel_attached_dig_port(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	int ret = 0;

	if (!hdcp->shim)
		return -ENOENT;

	mutex_lock(&hdcp->mutex);
	mutex_lock(&dig_port->hdcp.mutex);

	if (hdcp->value == DRM_MODE_CONTENT_PROTECTION_UNDESIRED)
		goto out;

	intel_hdcp_update_value(connector,
				DRM_MODE_CONTENT_PROTECTION_UNDESIRED, false);
	if (hdcp->hdcp2_encrypted)
		ret = _intel_hdcp2_disable(connector, false);
	else if (hdcp->hdcp_encrypted)
		ret = _intel_hdcp_disable(connector);

out:
	mutex_unlock(&dig_port->hdcp.mutex);
	mutex_unlock(&hdcp->mutex);
	cancel_delayed_work_sync(&hdcp->check_work);
	return ret;
}

void intel_hdcp_update_pipe(struct intel_atomic_state *state,
			    struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    const struct drm_connector_state *conn_state)
{
	struct intel_connector *connector =
				to_intel_connector(conn_state->connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	bool content_protection_type_changed, desired_and_not_enabled = false;
	struct intel_display *display = to_intel_display(connector);

	if (!connector->hdcp.shim)
		return;

	content_protection_type_changed =
		(conn_state->hdcp_content_type != hdcp->content_type &&
		 conn_state->content_protection !=
		 DRM_MODE_CONTENT_PROTECTION_UNDESIRED);

	/*
	 * During the HDCP encryption session if Type change is requested,
	 * disable the HDCP and re-enable it with new TYPE value.
	 */
	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_UNDESIRED ||
	    content_protection_type_changed)
		intel_hdcp_disable(connector);

	/*
	 * Mark the hdcp state as DESIRED after the hdcp disable of type
	 * change procedure.
	 */
	if (content_protection_type_changed) {
		mutex_lock(&hdcp->mutex);
		hdcp->value = DRM_MODE_CONTENT_PROTECTION_DESIRED;
		drm_connector_get(&connector->base);
		if (!queue_work(display->wq.unordered, &hdcp->prop_work))
			drm_connector_put(&connector->base);
		mutex_unlock(&hdcp->mutex);
	}

	if (conn_state->content_protection ==
	    DRM_MODE_CONTENT_PROTECTION_DESIRED) {
		mutex_lock(&hdcp->mutex);
		/* Avoid enabling hdcp, if it already ENABLED */
		desired_and_not_enabled =
			hdcp->value != DRM_MODE_CONTENT_PROTECTION_ENABLED;
		mutex_unlock(&hdcp->mutex);
		/*
		 * If HDCP already ENABLED and CP property is DESIRED, schedule
		 * prop_work to update correct CP property to user space.
		 */
		if (!desired_and_not_enabled && !content_protection_type_changed) {
			drm_connector_get(&connector->base);
			if (!queue_work(display->wq.unordered, &hdcp->prop_work))
				drm_connector_put(&connector->base);

		}
	}

	if (desired_and_not_enabled || content_protection_type_changed)
		_intel_hdcp_enable(state, encoder, crtc_state, conn_state);
}

void intel_hdcp_cancel_works(struct intel_connector *connector)
{
	if (!connector->hdcp.shim)
		return;

	cancel_delayed_work_sync(&connector->hdcp.check_work);
	cancel_work_sync(&connector->hdcp.prop_work);
}

void intel_hdcp_component_fini(struct intel_display *display)
{
	mutex_lock(&display->hdcp.hdcp_mutex);
	if (!display->hdcp.comp_added) {
		mutex_unlock(&display->hdcp.hdcp_mutex);
		return;
	}

	display->hdcp.comp_added = false;
	mutex_unlock(&display->hdcp.hdcp_mutex);

	if (USE_HDCP_GSC(display))
		intel_hdcp_gsc_fini(display);
	else
		component_del(display->drm->dev, &i915_hdcp_ops);
}

void intel_hdcp_cleanup(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;

	if (!hdcp->shim)
		return;

	/*
	 * If the connector is registered, it's possible userspace could kick
	 * off another HDCP enable, which would re-spawn the workers.
	 */
	drm_WARN_ON(connector->base.dev,
		connector->base.registration_state == DRM_CONNECTOR_REGISTERED);

	/*
	 * Now that the connector is not registered, check_work won't be run,
	 * but cancel any outstanding instances of it
	 */
	cancel_delayed_work_sync(&hdcp->check_work);

	/*
	 * We don't cancel prop_work in the same way as check_work since it
	 * requires connection_mutex which could be held while calling this
	 * function. Instead, we rely on the connector references grabbed before
	 * scheduling prop_work to ensure the connector is alive when prop_work
	 * is run. So if we're in the destroy path (which is where this
	 * function should be called), we're "guaranteed" that prop_work is not
	 * active (tl;dr This Should Never Happen).
	 */
	drm_WARN_ON(connector->base.dev, work_pending(&hdcp->prop_work));

	mutex_lock(&hdcp->mutex);
	hdcp->shim = NULL;
	mutex_unlock(&hdcp->mutex);
}

void intel_hdcp_atomic_check(struct drm_connector *connector,
			     struct drm_connector_state *old_state,
			     struct drm_connector_state *new_state)
{
	u64 old_cp = old_state->content_protection;
	u64 new_cp = new_state->content_protection;
	struct drm_crtc_state *crtc_state;

	if (!new_state->crtc) {
		/*
		 * If the connector is being disabled with CP enabled, mark it
		 * desired so it's re-enabled when the connector is brought back
		 */
		if (old_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED)
			new_state->content_protection =
				DRM_MODE_CONTENT_PROTECTION_DESIRED;
		return;
	}

	crtc_state = drm_atomic_get_new_crtc_state(new_state->state,
						   new_state->crtc);
	/*
	 * Fix the HDCP uapi content protection state in case of modeset.
	 * FIXME: As per HDCP content protection property uapi doc, an uevent()
	 * need to be sent if there is transition from ENABLED->DESIRED.
	 */
	if (drm_atomic_crtc_needs_modeset(crtc_state) &&
	    (old_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED &&
	    new_cp != DRM_MODE_CONTENT_PROTECTION_UNDESIRED))
		new_state->content_protection =
			DRM_MODE_CONTENT_PROTECTION_DESIRED;

	/*
	 * Nothing to do if the state didn't change, or HDCP was activated since
	 * the last commit. And also no change in hdcp content type.
	 */
	if (old_cp == new_cp ||
	    (old_cp == DRM_MODE_CONTENT_PROTECTION_DESIRED &&
	     new_cp == DRM_MODE_CONTENT_PROTECTION_ENABLED)) {
		if (old_state->hdcp_content_type ==
				new_state->hdcp_content_type)
			return;
	}

	crtc_state->mode_changed = true;
}

/* Handles the CP_IRQ raised from the DP HDCP sink */
void intel_hdcp_handle_cp_irq(struct intel_connector *connector)
{
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct intel_display *display = to_intel_display(connector);

	if (!hdcp->shim)
		return;

	atomic_inc(&connector->hdcp.cp_irq_count);
	wake_up_all(&connector->hdcp.cp_irq_queue);

	queue_delayed_work(display->wq.unordered, &hdcp->check_work, 0);
}

static void __intel_hdcp_info(struct seq_file *m, struct intel_connector *connector,
			      bool remote_req)
{
	bool hdcp_cap = false, hdcp2_cap = false;

	if (!connector->hdcp.shim) {
		seq_puts(m, "No Connector Support");
		goto out;
	}

	if (remote_req) {
		intel_hdcp_get_remote_capability(connector, &hdcp_cap, &hdcp2_cap);
	} else {
		hdcp_cap = intel_hdcp_get_capability(connector);
		hdcp2_cap = intel_hdcp2_get_capability(connector);
	}

	if (hdcp_cap)
		seq_puts(m, "HDCP1.4 ");
	if (hdcp2_cap)
		seq_puts(m, "HDCP2.2 ");

	if (!hdcp_cap && !hdcp2_cap)
		seq_puts(m, "None");

out:
	seq_puts(m, "\n");
}

void intel_hdcp_info(struct seq_file *m, struct intel_connector *connector)
{
	seq_puts(m, "\tHDCP version: ");
	if (connector->mst.dp) {
		__intel_hdcp_info(m, connector, true);
		seq_puts(m, "\tMST Hub HDCP version: ");
	}
	__intel_hdcp_info(m, connector, false);
}

static int intel_hdcp_sink_capability_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	int ret;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	if (!connector->base.encoder ||
	    connector->base.status != connector_status_connected) {
		ret = -ENODEV;
		goto out;
	}

	seq_printf(m, "%s:%d HDCP version: ", connector->base.name,
		   connector->base.base.id);
	__intel_hdcp_info(m, connector, false);

out:
	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}
DEFINE_SHOW_ATTRIBUTE(intel_hdcp_sink_capability);

static ssize_t intel_hdcp_force_14_write(struct file *file,
					 const char __user *ubuf,
					 size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_connector *connector = m->private;
	struct intel_hdcp *hdcp = &connector->hdcp;
	bool force_hdcp14 = false;
	int ret;

	if (len == 0)
		return 0;

	ret = kstrtobool_from_user(ubuf, len, &force_hdcp14);
	if (ret < 0)
		return ret;

	hdcp->force_hdcp14 = force_hdcp14;
	*offp += len;

	return len;
}

static int intel_hdcp_force_14_show(struct seq_file *m, void *data)
{
	struct intel_connector *connector = m->private;
	struct intel_display *display = to_intel_display(connector);
	struct intel_encoder *encoder = intel_attached_encoder(connector);
	struct intel_hdcp *hdcp = &connector->hdcp;
	struct drm_crtc *crtc;
	int ret;

	if (!encoder)
		return -ENODEV;

	ret = drm_modeset_lock_single_interruptible(&display->drm->mode_config.connection_mutex);
	if (ret)
		return ret;

	crtc = connector->base.state->crtc;
	if (connector->base.status != connector_status_connected || !crtc) {
		ret = -ENODEV;
		goto out;
	}

	seq_printf(m, "%s\n",
		   str_yes_no(hdcp->force_hdcp14));
out:
	drm_modeset_unlock(&display->drm->mode_config.connection_mutex);

	return ret;
}

static int intel_hdcp_force_14_open(struct inode *inode,
				    struct file *file)
{
	return single_open(file, intel_hdcp_force_14_show,
			   inode->i_private);
}

static const struct file_operations intel_hdcp_force_14_fops = {
	.owner = THIS_MODULE,
	.open = intel_hdcp_force_14_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = intel_hdcp_force_14_write
};

void intel_hdcp_connector_debugfs_add(struct intel_connector *connector)
{
	struct dentry *root = connector->base.debugfs_entry;
	int connector_type = connector->base.connector_type;

	if (connector_type == DRM_MODE_CONNECTOR_DisplayPort ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIA ||
	    connector_type == DRM_MODE_CONNECTOR_HDMIB) {
		debugfs_create_file("i915_hdcp_sink_capability", 0444, root,
				    connector, &intel_hdcp_sink_capability_fops);
		debugfs_create_file("i915_force_hdcp14", 0644, root,
				    connector, &intel_hdcp_force_14_fops);
	}
}
