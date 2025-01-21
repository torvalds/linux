// SPDX-License-Identifier: MIT
/* Copyright © 2024 Intel Corporation */

#include <linux/debugfs.h>

#include <drm/display/drm_dp.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_ddi.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dp.h"
#include "intel_dp_link_training.h"
#include "intel_dp_mst.h"
#include "intel_dp_test.h"

void intel_dp_test_reset(struct intel_dp *intel_dp)
{
	/*
	 * Clearing compliance test variables to allow capturing
	 * of values for next automated test request.
	 */
	memset(&intel_dp->compliance, 0, sizeof(intel_dp->compliance));
}

/* Adjust link config limits based on compliance test requests. */
void intel_dp_test_compute_config(struct intel_dp *intel_dp,
				  struct intel_crtc_state *pipe_config,
				  struct link_config_limits *limits)
{
	struct intel_display *display = to_intel_display(intel_dp);

	/* For DP Compliance we override the computed bpp for the pipe */
	if (intel_dp->compliance.test_data.bpc != 0) {
		int bpp = 3 * intel_dp->compliance.test_data.bpc;

		limits->pipe.min_bpp = bpp;
		limits->pipe.max_bpp = bpp;
		pipe_config->dither_force_disable = bpp == 6 * 3;

		drm_dbg_kms(display->drm, "Setting pipe_bpp to %d\n", bpp);
	}

	/* Use values requested by Compliance Test Request */
	if (intel_dp->compliance.test_type == DP_TEST_LINK_TRAINING) {
		int index;

		/* Validate the compliance test data since max values
		 * might have changed due to link train fallback.
		 */
		if (intel_dp_link_params_valid(intel_dp, intel_dp->compliance.test_link_rate,
					       intel_dp->compliance.test_lane_count)) {
			index = intel_dp_rate_index(intel_dp->common_rates,
						    intel_dp->num_common_rates,
						    intel_dp->compliance.test_link_rate);
			if (index >= 0) {
				limits->min_rate = intel_dp->compliance.test_link_rate;
				limits->max_rate = intel_dp->compliance.test_link_rate;
			}
			limits->min_lane_count = intel_dp->compliance.test_lane_count;
			limits->max_lane_count = intel_dp->compliance.test_lane_count;
		}
	}
}

/* Compliance test status bits  */
#define INTEL_DP_RESOLUTION_PREFERRED	1
#define INTEL_DP_RESOLUTION_STANDARD	2
#define INTEL_DP_RESOLUTION_FAILSAFE	3

static u8 intel_dp_autotest_link_training(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	int status = 0;
	int test_link_rate;
	u8 test_lane_count, test_link_bw;
	/* (DP CTS 1.2)
	 * 4.3.1.11
	 */
	/* Read the TEST_LANE_COUNT and TEST_LINK_RTAE fields (DP CTS 3.1.4) */
	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_LANE_COUNT,
				   &test_lane_count);

	if (status <= 0) {
		drm_dbg_kms(display->drm, "Lane count read failed\n");
		return DP_TEST_NAK;
	}
	test_lane_count &= DP_MAX_LANE_COUNT_MASK;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_LINK_RATE,
				   &test_link_bw);
	if (status <= 0) {
		drm_dbg_kms(display->drm, "Link Rate read failed\n");
		return DP_TEST_NAK;
	}
	test_link_rate = drm_dp_bw_code_to_link_rate(test_link_bw);

	/* Validate the requested link rate and lane count */
	if (!intel_dp_link_params_valid(intel_dp, test_link_rate,
					test_lane_count))
		return DP_TEST_NAK;

	intel_dp->compliance.test_lane_count = test_lane_count;
	intel_dp->compliance.test_link_rate = test_link_rate;

	return DP_TEST_ACK;
}

static u8 intel_dp_autotest_video_pattern(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u8 test_pattern;
	u8 test_misc;
	__be16 h_width, v_height;
	int status = 0;

	/* Read the TEST_PATTERN (DP CTS 3.1.5) */
	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_PATTERN,
				   &test_pattern);
	if (status <= 0) {
		drm_dbg_kms(display->drm, "Test pattern read failed\n");
		return DP_TEST_NAK;
	}
	if (test_pattern != DP_COLOR_RAMP)
		return DP_TEST_NAK;

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_H_WIDTH_HI,
				  &h_width, 2);
	if (status <= 0) {
		drm_dbg_kms(display->drm, "H Width read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_read(&intel_dp->aux, DP_TEST_V_HEIGHT_HI,
				  &v_height, 2);
	if (status <= 0) {
		drm_dbg_kms(display->drm, "V Height read failed\n");
		return DP_TEST_NAK;
	}

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_MISC0,
				   &test_misc);
	if (status <= 0) {
		drm_dbg_kms(display->drm, "TEST MISC read failed\n");
		return DP_TEST_NAK;
	}
	if ((test_misc & DP_TEST_COLOR_FORMAT_MASK) != DP_COLOR_FORMAT_RGB)
		return DP_TEST_NAK;
	if (test_misc & DP_TEST_DYNAMIC_RANGE_CEA)
		return DP_TEST_NAK;
	switch (test_misc & DP_TEST_BIT_DEPTH_MASK) {
	case DP_TEST_BIT_DEPTH_6:
		intel_dp->compliance.test_data.bpc = 6;
		break;
	case DP_TEST_BIT_DEPTH_8:
		intel_dp->compliance.test_data.bpc = 8;
		break;
	default:
		return DP_TEST_NAK;
	}

	intel_dp->compliance.test_data.video_pattern = test_pattern;
	intel_dp->compliance.test_data.hdisplay = be16_to_cpu(h_width);
	intel_dp->compliance.test_data.vdisplay = be16_to_cpu(v_height);
	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return DP_TEST_ACK;
}

static u8 intel_dp_autotest_edid(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u8 test_result = DP_TEST_ACK;
	struct intel_connector *intel_connector = intel_dp->attached_connector;
	struct drm_connector *connector = &intel_connector->base;

	if (!intel_connector->detect_edid || connector->edid_corrupt ||
	    intel_dp->aux.i2c_defer_count > 6) {
		/* Check EDID read for NACKs, DEFERs and corruption
		 * (DP CTS 1.2 Core r1.1)
		 *    4.2.2.4 : Failed EDID read, I2C_NAK
		 *    4.2.2.5 : Failed EDID read, I2C_DEFER
		 *    4.2.2.6 : EDID corruption detected
		 * Use failsafe mode for all cases
		 */
		if (intel_dp->aux.i2c_nack_count > 0 ||
		    intel_dp->aux.i2c_defer_count > 0)
			drm_dbg_kms(display->drm,
				    "EDID read had %d NACKs, %d DEFERs\n",
				    intel_dp->aux.i2c_nack_count,
				    intel_dp->aux.i2c_defer_count);
		intel_dp->compliance.test_data.edid = INTEL_DP_RESOLUTION_FAILSAFE;
	} else {
		/* FIXME: Get rid of drm_edid_raw() */
		const struct edid *block = drm_edid_raw(intel_connector->detect_edid);

		/* We have to write the checksum of the last block read */
		block += block->extensions;

		if (drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_EDID_CHECKSUM,
				       block->checksum) <= 0)
			drm_dbg_kms(display->drm,
				    "Failed to write EDID checksum\n");

		test_result = DP_TEST_ACK | DP_TEST_EDID_CHECKSUM_WRITE;
		intel_dp->compliance.test_data.edid = INTEL_DP_RESOLUTION_PREFERRED;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return test_result;
}

static void intel_dp_phy_pattern_update(struct intel_dp *intel_dp,
					const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_dp_phy_test_params *data =
			&intel_dp->compliance.test_data.phytest;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct intel_encoder *encoder = &dp_to_dig_port(intel_dp)->base;
	enum pipe pipe = crtc->pipe;
	u32 pattern_val;

	switch (data->phy_pattern) {
	case DP_LINK_QUAL_PATTERN_DISABLE:
		drm_dbg_kms(display->drm, "Disable Phy Test Pattern\n");
		intel_de_write(display, DDI_DP_COMP_CTL(pipe), 0x0);
		if (DISPLAY_VER(display) >= 10)
			intel_de_rmw(display, dp_tp_ctl_reg(encoder, crtc_state),
				     DP_TP_CTL_TRAIN_PAT4_SEL_MASK | DP_TP_CTL_LINK_TRAIN_MASK,
				     DP_TP_CTL_LINK_TRAIN_NORMAL);
		break;
	case DP_LINK_QUAL_PATTERN_D10_2:
		drm_dbg_kms(display->drm, "Set D10.2 Phy Test Pattern\n");
		intel_de_write(display, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_D10_2);
		break;
	case DP_LINK_QUAL_PATTERN_ERROR_RATE:
		drm_dbg_kms(display->drm,
			    "Set Error Count Phy Test Pattern\n");
		intel_de_write(display, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE |
			       DDI_DP_COMP_CTL_SCRAMBLED_0);
		break;
	case DP_LINK_QUAL_PATTERN_PRBS7:
		drm_dbg_kms(display->drm, "Set PRBS7 Phy Test Pattern\n");
		intel_de_write(display, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_PRBS7);
		break;
	case DP_LINK_QUAL_PATTERN_80BIT_CUSTOM:
		/*
		 * FIXME: Ideally pattern should come from DPCD 0x250. As
		 * current firmware of DPR-100 could not set it, so hardcoding
		 * now for complaince test.
		 */
		drm_dbg_kms(display->drm,
			    "Set 80Bit Custom Phy Test Pattern 0x3e0f83e0 0x0f83e0f8 0x0000f83e\n");
		pattern_val = 0x3e0f83e0;
		intel_de_write(display, DDI_DP_COMP_PAT(pipe, 0), pattern_val);
		pattern_val = 0x0f83e0f8;
		intel_de_write(display, DDI_DP_COMP_PAT(pipe, 1), pattern_val);
		pattern_val = 0x0000f83e;
		intel_de_write(display, DDI_DP_COMP_PAT(pipe, 2), pattern_val);
		intel_de_write(display, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE |
			       DDI_DP_COMP_CTL_CUSTOM80);
		break;
	case DP_LINK_QUAL_PATTERN_CP2520_PAT_1:
		/*
		 * FIXME: Ideally pattern should come from DPCD 0x24A. As
		 * current firmware of DPR-100 could not set it, so hardcoding
		 * now for complaince test.
		 */
		drm_dbg_kms(display->drm,
			    "Set HBR2 compliance Phy Test Pattern\n");
		pattern_val = 0xFB;
		intel_de_write(display, DDI_DP_COMP_CTL(pipe),
			       DDI_DP_COMP_CTL_ENABLE | DDI_DP_COMP_CTL_HBR2 |
			       pattern_val);
		break;
	case DP_LINK_QUAL_PATTERN_CP2520_PAT_3:
		if (DISPLAY_VER(display) < 10)  {
			drm_warn(display->drm,
				 "Platform does not support TPS4\n");
			break;
		}
		drm_dbg_kms(display->drm,
			    "Set TPS4 compliance Phy Test Pattern\n");
		intel_de_write(display, DDI_DP_COMP_CTL(pipe), 0x0);
		intel_de_rmw(display, dp_tp_ctl_reg(encoder, crtc_state),
			     DP_TP_CTL_TRAIN_PAT4_SEL_MASK | DP_TP_CTL_LINK_TRAIN_MASK,
			     DP_TP_CTL_TRAIN_PAT4_SEL_TP4A | DP_TP_CTL_LINK_TRAIN_PAT4);
		break;
	default:
		drm_warn(display->drm, "Invalid Phy Test Pattern\n");
	}
}

static void intel_dp_process_phy_request(struct intel_dp *intel_dp,
					 const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (drm_dp_dpcd_read_phy_link_status(&intel_dp->aux, DP_PHY_DPRX,
					     link_status) < 0) {
		drm_dbg_kms(display->drm, "failed to get link status\n");
		return;
	}

	/* retrieve vswing & pre-emphasis setting */
	intel_dp_get_adjust_train(intel_dp, crtc_state, DP_PHY_DPRX,
				  link_status);

	intel_dp_set_signal_levels(intel_dp, crtc_state, DP_PHY_DPRX);

	intel_dp_phy_pattern_update(intel_dp, crtc_state);

	drm_dp_dpcd_write(&intel_dp->aux, DP_TRAINING_LANE0_SET,
			  intel_dp->train_set, crtc_state->lane_count);

	drm_dp_set_phy_test_pattern(&intel_dp->aux, data,
				    intel_dp->dpcd[DP_DPCD_REV]);
}

static u8 intel_dp_autotest_phy_pattern(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_dp_phy_test_params *data =
		&intel_dp->compliance.test_data.phytest;

	if (drm_dp_get_phy_test_pattern(&intel_dp->aux, data)) {
		drm_dbg_kms(display->drm,
			    "DP Phy Test pattern AUX read failure\n");
		return DP_TEST_NAK;
	}

	/* Set test active flag here so userspace doesn't interrupt things */
	intel_dp->compliance.test_active = true;

	return DP_TEST_ACK;
}

void intel_dp_test_request(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	u8 response = DP_TEST_NAK;
	u8 request = 0;
	int status;

	status = drm_dp_dpcd_readb(&intel_dp->aux, DP_TEST_REQUEST, &request);
	if (status <= 0) {
		drm_dbg_kms(display->drm,
			    "Could not read test request from sink\n");
		goto update_status;
	}

	switch (request) {
	case DP_TEST_LINK_TRAINING:
		drm_dbg_kms(display->drm, "LINK_TRAINING test requested\n");
		response = intel_dp_autotest_link_training(intel_dp);
		break;
	case DP_TEST_LINK_VIDEO_PATTERN:
		drm_dbg_kms(display->drm, "TEST_PATTERN test requested\n");
		response = intel_dp_autotest_video_pattern(intel_dp);
		break;
	case DP_TEST_LINK_EDID_READ:
		drm_dbg_kms(display->drm, "EDID test requested\n");
		response = intel_dp_autotest_edid(intel_dp);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		drm_dbg_kms(display->drm, "PHY_PATTERN test requested\n");
		response = intel_dp_autotest_phy_pattern(intel_dp);
		break;
	default:
		drm_dbg_kms(display->drm, "Invalid test request '%02x'\n",
			    request);
		break;
	}

	if (response & DP_TEST_ACK)
		intel_dp->compliance.test_type = request;

update_status:
	status = drm_dp_dpcd_writeb(&intel_dp->aux, DP_TEST_RESPONSE, response);
	if (status <= 0)
		drm_dbg_kms(display->drm,
			    "Could not write test response to sink\n");
}

/* phy test */

static int intel_dp_prep_phy_test(struct intel_dp *intel_dp,
				  struct drm_modeset_acquire_ctx *ctx,
				  u8 *pipe_mask)
{
	struct intel_display *display = to_intel_display(intel_dp);
	struct drm_connector_list_iter conn_iter;
	struct intel_connector *connector;
	int ret = 0;

	*pipe_mask = 0;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	for_each_intel_connector_iter(connector, &conn_iter) {
		struct drm_connector_state *conn_state =
			connector->base.state;
		struct intel_crtc_state *crtc_state;
		struct intel_crtc *crtc;

		if (!intel_dp_has_connector(intel_dp, conn_state))
			continue;

		crtc = to_intel_crtc(conn_state->crtc);
		if (!crtc)
			continue;

		ret = drm_modeset_lock(&crtc->base.mutex, ctx);
		if (ret)
			break;

		crtc_state = to_intel_crtc_state(crtc->base.state);

		drm_WARN_ON(display->drm,
			    !intel_crtc_has_dp_encoder(crtc_state));

		if (!crtc_state->hw.active)
			continue;

		if (conn_state->commit &&
		    !try_wait_for_completion(&conn_state->commit->hw_done))
			continue;

		*pipe_mask |= BIT(crtc->pipe);
	}
	drm_connector_list_iter_end(&conn_iter);

	return ret;
}

static int intel_dp_do_phy_test(struct intel_encoder *encoder,
				struct drm_modeset_acquire_ctx *ctx)
{
	struct intel_display *display = to_intel_display(encoder);
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_crtc *crtc;
	u8 pipe_mask;
	int ret;

	ret = drm_modeset_lock(&display->drm->mode_config.connection_mutex,
			       ctx);
	if (ret)
		return ret;

	ret = intel_dp_prep_phy_test(intel_dp, ctx, &pipe_mask);
	if (ret)
		return ret;

	if (pipe_mask == 0)
		return 0;

	drm_dbg_kms(display->drm, "[ENCODER:%d:%s] PHY test\n",
		    encoder->base.base.id, encoder->base.name);

	for_each_intel_crtc_in_pipe_mask(display->drm, crtc, pipe_mask) {
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		/* test on the MST master transcoder */
		if (DISPLAY_VER(display) >= 12 &&
		    intel_crtc_has_type(crtc_state, INTEL_OUTPUT_DP_MST) &&
		    !intel_dp_mst_is_master_trans(crtc_state))
			continue;

		intel_dp_process_phy_request(intel_dp, crtc_state);
		break;
	}

	return 0;
}

bool intel_dp_test_phy(struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port = dp_to_dig_port(intel_dp);
	struct intel_encoder *encoder = &dig_port->base;
	struct drm_modeset_acquire_ctx ctx;
	int ret;

	if (!intel_dp->compliance.test_active ||
	    intel_dp->compliance.test_type != DP_TEST_LINK_PHY_TEST_PATTERN)
		return false;

	drm_modeset_acquire_init(&ctx, 0);

	for (;;) {
		ret = intel_dp_do_phy_test(encoder, &ctx);

		if (ret == -EDEADLK) {
			drm_modeset_backoff(&ctx);
			continue;
		}

		break;
	}

	drm_modeset_drop_locks(&ctx);
	drm_modeset_acquire_fini(&ctx);
	drm_WARN(encoder->base.dev, ret,
		 "Acquiring modeset locks failed with %i\n", ret);

	return true;
}

bool intel_dp_test_short_pulse(struct intel_dp *intel_dp)
{
	struct intel_display *display = to_intel_display(intel_dp);
	bool reprobe_needed = false;

	switch (intel_dp->compliance.test_type) {
	case DP_TEST_LINK_TRAINING:
		drm_dbg_kms(display->drm,
			    "Link Training Compliance Test requested\n");
		/* Send a Hotplug Uevent to userspace to start modeset */
		drm_kms_helper_hotplug_event(display->drm);
		break;
	case DP_TEST_LINK_PHY_TEST_PATTERN:
		drm_dbg_kms(display->drm,
			    "PHY test pattern Compliance Test requested\n");
		/*
		 * Schedule long hpd to do the test
		 *
		 * FIXME get rid of the ad-hoc phy test modeset code
		 * and properly incorporate it into the normal modeset.
		 */
		reprobe_needed = true;
	}

	return reprobe_needed;
}

static ssize_t i915_displayport_test_active_write(struct file *file,
						  const char __user *ubuf,
						  size_t len, loff_t *offp)
{
	struct seq_file *m = file->private_data;
	struct intel_display *display = m->private;
	char *input_buffer;
	int status = 0;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;
	int val = 0;

	if (len == 0)
		return 0;

	input_buffer = memdup_user_nul(ubuf, len);
	if (IS_ERR(input_buffer))
		return PTR_ERR(input_buffer);

	drm_dbg_kms(display->drm, "Copied %d bytes from user\n", (unsigned int)len);

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			status = kstrtoint(input_buffer, 10, &val);
			if (status < 0)
				break;
			drm_dbg_kms(display->drm, "Got %d for test active\n", val);
			/* To prevent erroneous activation of the compliance
			 * testing code, only accept an actual value of 1 here
			 */
			if (val == 1)
				intel_dp->compliance.test_active = true;
			else
				intel_dp->compliance.test_active = false;
		}
	}
	drm_connector_list_iter_end(&conn_iter);
	kfree(input_buffer);
	if (status < 0)
		return status;

	*offp += len;
	return len;
}

static int i915_displayport_test_active_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			if (intel_dp->compliance.test_active)
				seq_puts(m, "1");
			else
				seq_puts(m, "0");
		} else {
			seq_puts(m, "0");
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}

static int i915_displayport_test_active_open(struct inode *inode,
					     struct file *file)
{
	return single_open(file, i915_displayport_test_active_show,
			   inode->i_private);
}

static const struct file_operations i915_displayport_test_active_fops = {
	.owner = THIS_MODULE,
	.open = i915_displayport_test_active_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = i915_displayport_test_active_write
};

static int i915_displayport_test_data_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			if (intel_dp->compliance.test_type ==
			    DP_TEST_LINK_EDID_READ)
				seq_printf(m, "%lx",
					   intel_dp->compliance.test_data.edid);
			else if (intel_dp->compliance.test_type ==
				 DP_TEST_LINK_VIDEO_PATTERN) {
				seq_printf(m, "hdisplay: %d\n",
					   intel_dp->compliance.test_data.hdisplay);
				seq_printf(m, "vdisplay: %d\n",
					   intel_dp->compliance.test_data.vdisplay);
				seq_printf(m, "bpc: %u\n",
					   intel_dp->compliance.test_data.bpc);
			} else if (intel_dp->compliance.test_type ==
				   DP_TEST_LINK_PHY_TEST_PATTERN) {
				seq_printf(m, "pattern: %d\n",
					   intel_dp->compliance.test_data.phytest.phy_pattern);
				seq_printf(m, "Number of lanes: %d\n",
					   intel_dp->compliance.test_data.phytest.num_lanes);
				seq_printf(m, "Link Rate: %d\n",
					   intel_dp->compliance.test_data.phytest.link_rate);
				seq_printf(m, "level: %02x\n",
					   intel_dp->train_set[0]);
			}
		} else {
			seq_puts(m, "0");
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_data);

static int i915_displayport_test_type_show(struct seq_file *m, void *data)
{
	struct intel_display *display = m->private;
	struct drm_connector *connector;
	struct drm_connector_list_iter conn_iter;
	struct intel_dp *intel_dp;

	drm_connector_list_iter_begin(display->drm, &conn_iter);
	drm_for_each_connector_iter(connector, &conn_iter) {
		struct intel_encoder *encoder;

		if (connector->connector_type !=
		    DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		encoder = to_intel_encoder(connector->encoder);
		if (encoder && encoder->type == INTEL_OUTPUT_DP_MST)
			continue;

		if (encoder && connector->status == connector_status_connected) {
			intel_dp = enc_to_intel_dp(encoder);
			seq_printf(m, "%02lx\n", intel_dp->compliance.test_type);
		} else {
			seq_puts(m, "0");
		}
	}
	drm_connector_list_iter_end(&conn_iter);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(i915_displayport_test_type);

static const struct {
	const char *name;
	const struct file_operations *fops;
} intel_display_debugfs_files[] = {
	{"i915_dp_test_data", &i915_displayport_test_data_fops},
	{"i915_dp_test_type", &i915_displayport_test_type_fops},
	{"i915_dp_test_active", &i915_displayport_test_active_fops},
};

void intel_dp_test_debugfs_register(struct intel_display *display)
{
	struct drm_minor *minor = display->drm->primary;
	int i;

	for (i = 0; i < ARRAY_SIZE(intel_display_debugfs_files); i++) {
		debugfs_create_file(intel_display_debugfs_files[i].name,
				    0644,
				    minor->debugfs_root,
				    display,
				    intel_display_debugfs_files[i].fops);
	}
}
