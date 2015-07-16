/*
 * Copyright © 2008 Intel Corporation
 *             2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <drm/drmP.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

static bool intel_dp_mst_compute_config(struct intel_encoder *encoder,
					struct intel_crtc_state *pipe_config)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_atomic_state *state;
	int bpp, i;
	int lane_count, slots, rate;
	struct drm_display_mode *adjusted_mode = &pipe_config->base.adjusted_mode;
	struct drm_connector *drm_connector;
	struct intel_connector *connector, *found = NULL;
	struct drm_connector_state *connector_state;
	int mst_pbn;

	pipe_config->dp_encoder_is_mst = true;
	pipe_config->has_pch_encoder = false;
	pipe_config->has_dp_encoder = true;
	bpp = 24;
	/*
	 * for MST we always configure max link bw - the spec doesn't
	 * seem to suggest we should do otherwise.
	 */
	lane_count = drm_dp_max_lane_count(intel_dp->dpcd);

	rate = intel_dp_max_link_rate(intel_dp);

	if (intel_dp->num_sink_rates) {
		intel_dp->link_bw = 0;
		intel_dp->rate_select = intel_dp_rate_select(intel_dp, rate);
	} else {
		intel_dp->link_bw = drm_dp_link_rate_to_bw_code(rate);
		intel_dp->rate_select = 0;
	}

	intel_dp->lane_count = lane_count;

	pipe_config->pipe_bpp = 24;
	pipe_config->port_clock = rate;

	state = pipe_config->base.state;

	for_each_connector_in_state(state, drm_connector, connector_state, i) {
		connector = to_intel_connector(drm_connector);

		if (connector_state->best_encoder == &encoder->base) {
			found = connector;
			break;
		}
	}

	if (!found) {
		DRM_ERROR("can't find connector\n");
		return false;
	}

	mst_pbn = drm_dp_calc_pbn_mode(adjusted_mode->clock, bpp);

	pipe_config->pbn = mst_pbn;
	slots = drm_dp_find_vcpi_slots(&intel_dp->mst_mgr, mst_pbn);

	intel_link_compute_m_n(bpp, lane_count,
			       adjusted_mode->crtc_clock,
			       pipe_config->port_clock,
			       &pipe_config->dp_m_n);

	pipe_config->dp_m_n.tu = slots;
	return true;

}

static void intel_mst_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	int ret;

	DRM_DEBUG_KMS("%d\n", intel_dp->active_mst_links);

	drm_dp_mst_reset_vcpi_slots(&intel_dp->mst_mgr, intel_mst->port);

	ret = drm_dp_update_payload_part1(&intel_dp->mst_mgr);
	if (ret) {
		DRM_ERROR("failed to update payload %d\n", ret);
	}
}

static void intel_mst_post_disable_dp(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	DRM_DEBUG_KMS("%d\n", intel_dp->active_mst_links);

	/* this can fail */
	drm_dp_check_act_status(&intel_dp->mst_mgr);
	/* and this can also fail */
	drm_dp_update_payload_part2(&intel_dp->mst_mgr);

	drm_dp_mst_deallocate_vcpi(&intel_dp->mst_mgr, intel_mst->port);

	intel_dp->active_mst_links--;
	intel_mst->port = NULL;
	if (intel_dp->active_mst_links == 0) {
		intel_dig_port->base.post_disable(&intel_dig_port->base);
		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_OFF);
	}
}

static void intel_mst_pre_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	int ret;
	uint32_t temp;
	struct intel_connector *found = NULL, *connector;
	int slots;
	struct drm_crtc *crtc = encoder->base.crtc;
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	for_each_intel_connector(dev, connector) {
		if (connector->base.state->best_encoder == &encoder->base) {
			found = connector;
			break;
		}
	}

	if (!found) {
		DRM_ERROR("can't find connector\n");
		return;
	}

	DRM_DEBUG_KMS("%d\n", intel_dp->active_mst_links);
	intel_mst->port = found->port;

	if (intel_dp->active_mst_links == 0) {
		enum port port = intel_ddi_get_encoder_port(encoder);

		/* FIXME: add support for SKL */
		if (INTEL_INFO(dev)->gen < 9)
			I915_WRITE(PORT_CLK_SEL(port),
				   intel_crtc->config->ddi_pll_sel);

		intel_ddi_init_dp_buf_reg(&intel_dig_port->base);

		intel_dp_sink_dpms(intel_dp, DRM_MODE_DPMS_ON);


		intel_dp_start_link_train(intel_dp);
		intel_dp_complete_link_train(intel_dp);
		intel_dp_stop_link_train(intel_dp);
	}

	ret = drm_dp_mst_allocate_vcpi(&intel_dp->mst_mgr,
				       intel_mst->port,
				       intel_crtc->config->pbn, &slots);
	if (ret == false) {
		DRM_ERROR("failed to allocate vcpi\n");
		return;
	}


	intel_dp->active_mst_links++;
	temp = I915_READ(DP_TP_STATUS(port));
	I915_WRITE(DP_TP_STATUS(port), temp);

	ret = drm_dp_update_payload_part1(&intel_dp->mst_mgr);
}

static void intel_mst_enable_dp(struct intel_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum port port = intel_dig_port->port;
	int ret;

	DRM_DEBUG_KMS("%d\n", intel_dp->active_mst_links);

	if (wait_for((I915_READ(DP_TP_STATUS(port)) & DP_TP_STATUS_ACT_SENT),
		     1))
		DRM_ERROR("Timed out waiting for ACT sent\n");

	ret = drm_dp_check_act_status(&intel_dp->mst_mgr);

	ret = drm_dp_update_payload_part2(&intel_dp->mst_mgr);
}

static bool intel_dp_mst_enc_get_hw_state(struct intel_encoder *encoder,
				      enum pipe *pipe)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	*pipe = intel_mst->pipe;
	if (intel_mst->port)
		return true;
	return false;
}

static void intel_dp_mst_enc_get_config(struct intel_encoder *encoder,
					struct intel_crtc_state *pipe_config)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(&encoder->base);
	struct intel_digital_port *intel_dig_port = intel_mst->primary;
	struct intel_crtc *crtc = to_intel_crtc(encoder->base.crtc);
	struct drm_device *dev = encoder->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	enum transcoder cpu_transcoder = pipe_config->cpu_transcoder;
	u32 temp, flags = 0;

	pipe_config->has_dp_encoder = true;

	temp = I915_READ(TRANS_DDI_FUNC_CTL(cpu_transcoder));
	if (temp & TRANS_DDI_PHSYNC)
		flags |= DRM_MODE_FLAG_PHSYNC;
	else
		flags |= DRM_MODE_FLAG_NHSYNC;
	if (temp & TRANS_DDI_PVSYNC)
		flags |= DRM_MODE_FLAG_PVSYNC;
	else
		flags |= DRM_MODE_FLAG_NVSYNC;

	switch (temp & TRANS_DDI_BPC_MASK) {
	case TRANS_DDI_BPC_6:
		pipe_config->pipe_bpp = 18;
		break;
	case TRANS_DDI_BPC_8:
		pipe_config->pipe_bpp = 24;
		break;
	case TRANS_DDI_BPC_10:
		pipe_config->pipe_bpp = 30;
		break;
	case TRANS_DDI_BPC_12:
		pipe_config->pipe_bpp = 36;
		break;
	default:
		break;
	}
	pipe_config->base.adjusted_mode.flags |= flags;
	intel_dp_get_m_n(crtc, pipe_config);

	intel_ddi_clock_get(&intel_dig_port->base, pipe_config);
}

static int intel_dp_mst_get_ddc_modes(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	struct edid *edid;
	int ret;

	edid = drm_dp_mst_get_edid(connector, &intel_dp->mst_mgr, intel_connector->port);
	if (!edid)
		return 0;

	ret = intel_connector_update_modes(connector, edid);
	kfree(edid);

	return ret;
}

static enum drm_connector_status
intel_dp_mst_detect(struct drm_connector *connector, bool force)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;

	return drm_dp_mst_detect_port(connector, &intel_dp->mst_mgr, intel_connector->port);
}

static int
intel_dp_mst_set_property(struct drm_connector *connector,
			  struct drm_property *property,
			  uint64_t val)
{
	return 0;
}

static void
intel_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);

	if (!IS_ERR_OR_NULL(intel_connector->edid))
		kfree(intel_connector->edid);

	drm_connector_cleanup(connector);
	kfree(connector);
}

static const struct drm_connector_funcs intel_dp_mst_connector_funcs = {
	.dpms = intel_connector_dpms,
	.detect = intel_dp_mst_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = intel_dp_mst_set_property,
	.atomic_get_property = intel_connector_atomic_get_property,
	.destroy = intel_dp_mst_connector_destroy,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
};

static int intel_dp_mst_get_modes(struct drm_connector *connector)
{
	return intel_dp_mst_get_ddc_modes(connector);
}

static enum drm_mode_status
intel_dp_mst_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	/* TODO - validate mode against available PBN for link */
	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

static struct drm_encoder *intel_mst_best_encoder(struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct intel_dp *intel_dp = intel_connector->mst_port;
	return &intel_dp->mst_encoders[0]->base.base;
}

static const struct drm_connector_helper_funcs intel_dp_mst_connector_helper_funcs = {
	.get_modes = intel_dp_mst_get_modes,
	.mode_valid = intel_dp_mst_mode_valid,
	.best_encoder = intel_mst_best_encoder,
};

static void intel_dp_mst_encoder_destroy(struct drm_encoder *encoder)
{
	struct intel_dp_mst_encoder *intel_mst = enc_to_mst(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_mst);
}

static const struct drm_encoder_funcs intel_dp_mst_enc_funcs = {
	.destroy = intel_dp_mst_encoder_destroy,
};

static bool intel_dp_mst_get_hw_state(struct intel_connector *connector)
{
	if (connector->encoder) {
		enum pipe pipe;
		if (!connector->encoder->get_hw_state(connector->encoder, &pipe))
			return false;
		return true;
	}
	return false;
}

static void intel_connector_add_to_fbdev(struct intel_connector *connector)
{
#ifdef CONFIG_DRM_I915_FBDEV
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	drm_fb_helper_add_one_connector(&dev_priv->fbdev->helper, &connector->base);
#endif
}

static void intel_connector_remove_from_fbdev(struct intel_connector *connector)
{
#ifdef CONFIG_DRM_I915_FBDEV
	struct drm_i915_private *dev_priv = to_i915(connector->base.dev);
	drm_fb_helper_remove_one_connector(&dev_priv->fbdev->helper, &connector->base);
#endif
}

static struct drm_connector *intel_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr, struct drm_dp_mst_port *port, const char *pathprop)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;
	struct intel_connector *intel_connector;
	struct drm_connector *connector;
	int i;

	intel_connector = intel_connector_alloc();
	if (!intel_connector)
		return NULL;

	connector = &intel_connector->base;
	drm_connector_init(dev, connector, &intel_dp_mst_connector_funcs, DRM_MODE_CONNECTOR_DisplayPort);
	drm_connector_helper_add(connector, &intel_dp_mst_connector_helper_funcs);

	intel_connector->unregister = intel_connector_unregister;
	intel_connector->get_hw_state = intel_dp_mst_get_hw_state;
	intel_connector->mst_port = intel_dp;
	intel_connector->port = port;

	for (i = PIPE_A; i <= PIPE_C; i++) {
		drm_mode_connector_attach_encoder(&intel_connector->base,
						  &intel_dp->mst_encoders[i]->base.base);
	}
	intel_dp_add_properties(intel_dp, connector);

	drm_object_attach_property(&connector->base, dev->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base, dev->mode_config.tile_property, 0);

	drm_mode_connector_set_path_property(connector, pathprop);
	drm_reinit_primary_mode_group(dev);
	mutex_lock(&dev->mode_config.mutex);
	intel_connector_add_to_fbdev(intel_connector);
	mutex_unlock(&dev->mode_config.mutex);
	drm_connector_register(&intel_connector->base);
	return connector;
}

static void intel_dp_destroy_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
					   struct drm_connector *connector)
{
	struct intel_connector *intel_connector = to_intel_connector(connector);
	struct drm_device *dev = connector->dev;
	/* need to nuke the connector */
	mutex_lock(&dev->mode_config.mutex);
	intel_connector_dpms(connector, DRM_MODE_DPMS_OFF);
	mutex_unlock(&dev->mode_config.mutex);

	intel_connector->unregister(intel_connector);

	mutex_lock(&dev->mode_config.mutex);
	intel_connector_remove_from_fbdev(intel_connector);
	drm_connector_cleanup(connector);
	mutex_unlock(&dev->mode_config.mutex);

	drm_reinit_primary_mode_group(dev);

	kfree(intel_connector);
	DRM_DEBUG_KMS("\n");
}

static void intel_dp_mst_hotplug(struct drm_dp_mst_topology_mgr *mgr)
{
	struct intel_dp *intel_dp = container_of(mgr, struct intel_dp, mst_mgr);
	struct intel_digital_port *intel_dig_port = dp_to_dig_port(intel_dp);
	struct drm_device *dev = intel_dig_port->base.base.dev;

	drm_kms_helper_hotplug_event(dev);
}

static struct drm_dp_mst_topology_cbs mst_cbs = {
	.add_connector = intel_dp_add_mst_connector,
	.destroy_connector = intel_dp_destroy_mst_connector,
	.hotplug = intel_dp_mst_hotplug,
};

static struct intel_dp_mst_encoder *
intel_dp_create_fake_mst_encoder(struct intel_digital_port *intel_dig_port, enum pipe pipe)
{
	struct intel_dp_mst_encoder *intel_mst;
	struct intel_encoder *intel_encoder;
	struct drm_device *dev = intel_dig_port->base.base.dev;

	intel_mst = kzalloc(sizeof(*intel_mst), GFP_KERNEL);

	if (!intel_mst)
		return NULL;

	intel_mst->pipe = pipe;
	intel_encoder = &intel_mst->base;
	intel_mst->primary = intel_dig_port;

	drm_encoder_init(dev, &intel_encoder->base, &intel_dp_mst_enc_funcs,
			 DRM_MODE_ENCODER_DPMST);

	intel_encoder->type = INTEL_OUTPUT_DP_MST;
	intel_encoder->crtc_mask = 0x7;
	intel_encoder->cloneable = 0;

	intel_encoder->compute_config = intel_dp_mst_compute_config;
	intel_encoder->disable = intel_mst_disable_dp;
	intel_encoder->post_disable = intel_mst_post_disable_dp;
	intel_encoder->pre_enable = intel_mst_pre_enable_dp;
	intel_encoder->enable = intel_mst_enable_dp;
	intel_encoder->get_hw_state = intel_dp_mst_enc_get_hw_state;
	intel_encoder->get_config = intel_dp_mst_enc_get_config;

	return intel_mst;

}

static bool
intel_dp_create_fake_mst_encoders(struct intel_digital_port *intel_dig_port)
{
	int i;
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	for (i = PIPE_A; i <= PIPE_C; i++)
		intel_dp->mst_encoders[i] = intel_dp_create_fake_mst_encoder(intel_dig_port, i);
	return true;
}

int
intel_dp_mst_encoder_init(struct intel_digital_port *intel_dig_port, int conn_base_id)
{
	struct intel_dp *intel_dp = &intel_dig_port->dp;
	struct drm_device *dev = intel_dig_port->base.base.dev;
	int ret;

	intel_dp->can_mst = true;
	intel_dp->mst_mgr.cbs = &mst_cbs;

	/* create encoders */
	intel_dp_create_fake_mst_encoders(intel_dig_port);
	ret = drm_dp_mst_topology_mgr_init(&intel_dp->mst_mgr, dev->dev, &intel_dp->aux, 16, 3, conn_base_id);
	if (ret) {
		intel_dp->can_mst = false;
		return ret;
	}
	return 0;
}

void
intel_dp_mst_encoder_cleanup(struct intel_digital_port *intel_dig_port)
{
	struct intel_dp *intel_dp = &intel_dig_port->dp;

	if (!intel_dp->can_mst)
		return;

	drm_dp_mst_topology_mgr_destroy(&intel_dp->mst_mgr);
	/* encoders will get killed by normal cleanup */
}
