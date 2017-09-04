
#include <drm/drmP.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_fb_helper.h>

#include "radeon.h"
#include "atom.h"
#include "ni_reg.h"

static struct radeon_encoder *radeon_dp_create_fake_mst_encoder(struct radeon_connector *connector);

static int radeon_atom_set_enc_offset(int id)
{
	static const int offsets[] = { EVERGREEN_CRTC0_REGISTER_OFFSET,
				       EVERGREEN_CRTC1_REGISTER_OFFSET,
				       EVERGREEN_CRTC2_REGISTER_OFFSET,
				       EVERGREEN_CRTC3_REGISTER_OFFSET,
				       EVERGREEN_CRTC4_REGISTER_OFFSET,
				       EVERGREEN_CRTC5_REGISTER_OFFSET,
				       0x13830 - 0x7030 };

	return offsets[id];
}

static int radeon_dp_mst_set_be_cntl(struct radeon_encoder *primary,
				     struct radeon_encoder_mst *mst_enc,
				     enum radeon_hpd_id hpd, bool enable)
{
	struct drm_device *dev = primary->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	uint32_t reg;
	int retries = 0;
	uint32_t temp;

	reg = RREG32(NI_DIG_BE_CNTL + primary->offset);

	/* set MST mode */
	reg &= ~NI_DIG_FE_DIG_MODE(7);
	reg |= NI_DIG_FE_DIG_MODE(NI_DIG_MODE_DP_MST);

	if (enable)
		reg |= NI_DIG_FE_SOURCE_SELECT(1 << mst_enc->fe);
	else
		reg &= ~NI_DIG_FE_SOURCE_SELECT(1 << mst_enc->fe);

	reg |= NI_DIG_HPD_SELECT(hpd);
	DRM_DEBUG_KMS("writing 0x%08x 0x%08x\n", NI_DIG_BE_CNTL + primary->offset, reg);
	WREG32(NI_DIG_BE_CNTL + primary->offset, reg);

	if (enable) {
		uint32_t offset = radeon_atom_set_enc_offset(mst_enc->fe);

		do {
			temp = RREG32(NI_DIG_FE_CNTL + offset);
		} while ((temp & NI_DIG_SYMCLK_FE_ON) && retries++ < 10000);
		if (retries == 10000)
			DRM_ERROR("timed out waiting for FE %d %d\n", primary->offset, mst_enc->fe);
	}
	return 0;
}

static int radeon_dp_mst_set_stream_attrib(struct radeon_encoder *primary,
					   int stream_number,
					   int fe,
					   int slots)
{
	struct drm_device *dev = primary->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	u32 temp, val;
	int retries  = 0;
	int satreg, satidx;

	satreg = stream_number >> 1;
	satidx = stream_number & 1;

	temp = RREG32(NI_DP_MSE_SAT0 + satreg + primary->offset);

	val = NI_DP_MSE_SAT_SLOT_COUNT0(slots) | NI_DP_MSE_SAT_SRC0(fe);

	val <<= (16 * satidx);

	temp &= ~(0xffff << (16 * satidx));

	temp |= val;

	DRM_DEBUG_KMS("writing 0x%08x 0x%08x\n", NI_DP_MSE_SAT0 + satreg + primary->offset, temp);
	WREG32(NI_DP_MSE_SAT0 + satreg + primary->offset, temp);

	WREG32(NI_DP_MSE_SAT_UPDATE + primary->offset, 1);

	do {
		unsigned value1, value2;
		udelay(10);
		temp = RREG32(NI_DP_MSE_SAT_UPDATE + primary->offset);

		value1 = temp & NI_DP_MSE_SAT_UPDATE_MASK;
		value2 = temp & NI_DP_MSE_16_MTP_KEEPOUT;

		if (!value1 && !value2)
			break;
	} while (retries++ < 50);

	if (retries == 10000)
		DRM_ERROR("timed out waitin for SAT update %d\n", primary->offset);

	/* MTP 16 ? */
	return 0;
}

static int radeon_dp_mst_update_stream_attribs(struct radeon_connector *mst_conn,
					       struct radeon_encoder *primary)
{
	struct drm_device *dev = mst_conn->base.dev;
	struct stream_attribs new_attribs[6];
	int i;
	int idx = 0;
	struct radeon_connector *radeon_connector;
	struct drm_connector *connector;

	memset(new_attribs, 0, sizeof(new_attribs));
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_encoder *subenc;
		struct radeon_encoder_mst *mst_enc;

		radeon_connector = to_radeon_connector(connector);
		if (!radeon_connector->is_mst_connector)
			continue;

		if (radeon_connector->mst_port != mst_conn)
			continue;

		subenc = radeon_connector->mst_encoder;
		mst_enc = subenc->enc_priv;

		if (!mst_enc->enc_active)
			continue;

		new_attribs[idx].fe = mst_enc->fe;
		new_attribs[idx].slots = drm_dp_mst_get_vcpi_slots(&mst_conn->mst_mgr, mst_enc->port);
		idx++;
	}

	for (i = 0; i < idx; i++) {
		if (new_attribs[i].fe != mst_conn->cur_stream_attribs[i].fe ||
		    new_attribs[i].slots != mst_conn->cur_stream_attribs[i].slots) {
			radeon_dp_mst_set_stream_attrib(primary, i, new_attribs[i].fe, new_attribs[i].slots);
			mst_conn->cur_stream_attribs[i].fe = new_attribs[i].fe;
			mst_conn->cur_stream_attribs[i].slots = new_attribs[i].slots;
		}
	}

	for (i = idx; i < mst_conn->enabled_attribs; i++) {
		radeon_dp_mst_set_stream_attrib(primary, i, 0, 0);
		mst_conn->cur_stream_attribs[i].fe = 0;
		mst_conn->cur_stream_attribs[i].slots = 0;
	}
	mst_conn->enabled_attribs = idx;
	return 0;
}

static int radeon_dp_mst_set_vcp_size(struct radeon_encoder *mst, s64 avg_time_slots_per_mtp)
{
	struct drm_device *dev = mst->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder_mst *mst_enc = mst->enc_priv;
	uint32_t val, temp;
	uint32_t offset = radeon_atom_set_enc_offset(mst_enc->fe);
	int retries = 0;
	uint32_t x = drm_fixp2int(avg_time_slots_per_mtp);
	uint32_t y = drm_fixp2int_ceil((avg_time_slots_per_mtp - x) << 26);

	val = NI_DP_MSE_RATE_X(x) | NI_DP_MSE_RATE_Y(y);

	WREG32(NI_DP_MSE_RATE_CNTL + offset, val);

	do {
		temp = RREG32(NI_DP_MSE_RATE_UPDATE + offset);
		udelay(10);
	} while ((temp & 0x1) && (retries++ < 10000));

	if (retries >= 10000)
		DRM_ERROR("timed out wait for rate cntl %d\n", mst_enc->fe);
	return 0;
}

static int radeon_dp_mst_get_ddc_modes(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector *master = radeon_connector->mst_port;
	struct edid *edid;
	int ret = 0;

	edid = drm_dp_mst_get_edid(connector, &master->mst_mgr, radeon_connector->port);
	radeon_connector->edid = edid;
	DRM_DEBUG_KMS("edid retrieved %p\n", edid);
	if (radeon_connector->edid) {
		drm_mode_connector_update_edid_property(&radeon_connector->base, radeon_connector->edid);
		ret = drm_add_edid_modes(&radeon_connector->base, radeon_connector->edid);
		drm_edid_to_eld(&radeon_connector->base, radeon_connector->edid);
		return ret;
	}
	drm_mode_connector_update_edid_property(&radeon_connector->base, NULL);

	return ret;
}

static int radeon_dp_mst_get_modes(struct drm_connector *connector)
{
	return radeon_dp_mst_get_ddc_modes(connector);
}

static enum drm_mode_status
radeon_dp_mst_mode_valid(struct drm_connector *connector,
			struct drm_display_mode *mode)
{
	/* TODO - validate mode against available PBN for link */
	if (mode->clock < 10000)
		return MODE_CLOCK_LOW;

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		return MODE_H_ILLEGAL;

	return MODE_OK;
}

static struct
drm_encoder *radeon_mst_best_encoder(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);

	return &radeon_connector->mst_encoder->base;
}

static const struct drm_connector_helper_funcs radeon_dp_mst_connector_helper_funcs = {
	.get_modes = radeon_dp_mst_get_modes,
	.mode_valid = radeon_dp_mst_mode_valid,
	.best_encoder = radeon_mst_best_encoder,
};

static enum drm_connector_status
radeon_dp_mst_detect(struct drm_connector *connector, bool force)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_connector *master = radeon_connector->mst_port;

	return drm_dp_mst_detect_port(connector, &master->mst_mgr, radeon_connector->port);
}

static void
radeon_dp_mst_connector_destroy(struct drm_connector *connector)
{
	struct radeon_connector *radeon_connector = to_radeon_connector(connector);
	struct radeon_encoder *radeon_encoder = radeon_connector->mst_encoder;

	drm_encoder_cleanup(&radeon_encoder->base);
	kfree(radeon_encoder);
	drm_connector_cleanup(connector);
	kfree(radeon_connector);
}

static const struct drm_connector_funcs radeon_dp_mst_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.detect = radeon_dp_mst_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = radeon_dp_mst_connector_destroy,
};

static struct drm_connector *radeon_dp_add_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
							 struct drm_dp_mst_port *port,
							 const char *pathprop)
{
	struct radeon_connector *master = container_of(mgr, struct radeon_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct radeon_connector *radeon_connector;
	struct drm_connector *connector;

	radeon_connector = kzalloc(sizeof(*radeon_connector), GFP_KERNEL);
	if (!radeon_connector)
		return NULL;

	radeon_connector->is_mst_connector = true;
	connector = &radeon_connector->base;
	radeon_connector->port = port;
	radeon_connector->mst_port = master;
	DRM_DEBUG_KMS("\n");

	drm_connector_init(dev, connector, &radeon_dp_mst_connector_funcs, DRM_MODE_CONNECTOR_DisplayPort);
	drm_connector_helper_add(connector, &radeon_dp_mst_connector_helper_funcs);
	radeon_connector->mst_encoder = radeon_dp_create_fake_mst_encoder(master);

	drm_object_attach_property(&connector->base, dev->mode_config.path_property, 0);
	drm_object_attach_property(&connector->base, dev->mode_config.tile_property, 0);
	drm_mode_connector_set_path_property(connector, pathprop);

	return connector;
}

static void radeon_dp_register_mst_connector(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct radeon_device *rdev = dev->dev_private;

	radeon_fb_add_connector(rdev, connector);

	drm_connector_register(connector);
}

static void radeon_dp_destroy_mst_connector(struct drm_dp_mst_topology_mgr *mgr,
					    struct drm_connector *connector)
{
	struct radeon_connector *master = container_of(mgr, struct radeon_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;
	struct radeon_device *rdev = dev->dev_private;

	drm_connector_unregister(connector);
	radeon_fb_remove_connector(rdev, connector);
	drm_connector_cleanup(connector);

	kfree(connector);
	DRM_DEBUG_KMS("\n");
}

static void radeon_dp_mst_hotplug(struct drm_dp_mst_topology_mgr *mgr)
{
	struct radeon_connector *master = container_of(mgr, struct radeon_connector, mst_mgr);
	struct drm_device *dev = master->base.dev;

	drm_kms_helper_hotplug_event(dev);
}

const struct drm_dp_mst_topology_cbs mst_cbs = {
	.add_connector = radeon_dp_add_mst_connector,
	.register_connector = radeon_dp_register_mst_connector,
	.destroy_connector = radeon_dp_destroy_mst_connector,
	.hotplug = radeon_dp_mst_hotplug,
};

static struct
radeon_connector *radeon_mst_find_connector(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct radeon_connector *radeon_connector = to_radeon_connector(connector);
		if (!connector->encoder)
			continue;
		if (!radeon_connector->is_mst_connector)
			continue;

		DRM_DEBUG_KMS("checking %p vs %p\n", connector->encoder, encoder);
		if (connector->encoder == encoder)
			return radeon_connector;
	}
	return NULL;
}

void radeon_dp_mst_prepare_pll(struct drm_crtc *crtc, struct drm_display_mode *mode)
{
	struct radeon_crtc *radeon_crtc = to_radeon_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(radeon_crtc->encoder);
	struct radeon_encoder_mst *mst_enc = radeon_encoder->enc_priv;
	struct radeon_connector *radeon_connector = radeon_mst_find_connector(&radeon_encoder->base);
	int dp_clock;
	struct radeon_connector_atom_dig *dig_connector = mst_enc->connector->con_priv;

	if (radeon_connector) {
		radeon_connector->pixelclock_for_modeset = mode->clock;
		if (radeon_connector->base.display_info.bpc)
			radeon_crtc->bpc = radeon_connector->base.display_info.bpc;
		else
			radeon_crtc->bpc = 8;
	}

	DRM_DEBUG_KMS("dp_clock %p %d\n", dig_connector, dig_connector->dp_clock);
	dp_clock = dig_connector->dp_clock;
	radeon_crtc->ss_enabled =
		radeon_atombios_get_asic_ss_info(rdev, &radeon_crtc->ss,
						 ASIC_INTERNAL_SS_ON_DP,
						 dp_clock);
}

static void
radeon_mst_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder, *primary;
	struct radeon_encoder_mst *mst_enc;
	struct radeon_encoder_atom_dig *dig_enc;
	struct radeon_connector *radeon_connector;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;
	int ret, slots;
	s64 fixed_pbn, fixed_pbn_per_slot, avg_time_slots_per_mtp;
	if (!ASIC_IS_DCE5(rdev)) {
		DRM_ERROR("got mst dpms on non-DCE5\n");
		return;
	}

	radeon_connector = radeon_mst_find_connector(encoder);
	if (!radeon_connector)
		return;

	radeon_encoder = to_radeon_encoder(encoder);

	mst_enc = radeon_encoder->enc_priv;

	primary = mst_enc->primary;

	dig_enc = primary->enc_priv;

	crtc = encoder->crtc;
	DRM_DEBUG_KMS("got connector %d\n", dig_enc->active_mst_links);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		dig_enc->active_mst_links++;

		radeon_crtc = to_radeon_crtc(crtc);

		if (dig_enc->active_mst_links == 1) {
			mst_enc->fe = dig_enc->dig_encoder;
			mst_enc->fe_from_be = true;
			atombios_set_mst_encoder_crtc_source(encoder, mst_enc->fe);

			atombios_dig_encoder_setup(&primary->base, ATOM_ENCODER_CMD_SETUP, 0);
			atombios_dig_transmitter_setup2(&primary->base, ATOM_TRANSMITTER_ACTION_ENABLE,
							0, 0, dig_enc->dig_encoder);

			if (radeon_dp_needs_link_train(mst_enc->connector) ||
			    dig_enc->active_mst_links == 1) {
				radeon_dp_link_train(&primary->base, &mst_enc->connector->base);
			}

		} else {
			mst_enc->fe = radeon_atom_pick_dig_encoder(encoder, radeon_crtc->crtc_id);
			if (mst_enc->fe == -1)
				DRM_ERROR("failed to get frontend for dig encoder\n");
			mst_enc->fe_from_be = false;
			atombios_set_mst_encoder_crtc_source(encoder, mst_enc->fe);
		}

		DRM_DEBUG_KMS("dig encoder is %d %d %d\n", dig_enc->dig_encoder,
			      dig_enc->linkb, radeon_crtc->crtc_id);

		slots = drm_dp_find_vcpi_slots(&radeon_connector->mst_port->mst_mgr,
					       mst_enc->pbn);
		ret = drm_dp_mst_allocate_vcpi(&radeon_connector->mst_port->mst_mgr,
					       radeon_connector->port,
					       mst_enc->pbn, slots);
		ret = drm_dp_update_payload_part1(&radeon_connector->mst_port->mst_mgr);

		radeon_dp_mst_set_be_cntl(primary, mst_enc,
					  radeon_connector->mst_port->hpd.hpd, true);

		mst_enc->enc_active = true;
		radeon_dp_mst_update_stream_attribs(radeon_connector->mst_port, primary);

		fixed_pbn = drm_int2fixp(mst_enc->pbn);
		fixed_pbn_per_slot = drm_int2fixp(radeon_connector->mst_port->mst_mgr.pbn_div);
		avg_time_slots_per_mtp = drm_fixp_div(fixed_pbn, fixed_pbn_per_slot);
		radeon_dp_mst_set_vcp_size(radeon_encoder, avg_time_slots_per_mtp);

		atombios_dig_encoder_setup2(&primary->base, ATOM_ENCODER_CMD_DP_VIDEO_ON, 0,
					    mst_enc->fe);
		ret = drm_dp_check_act_status(&radeon_connector->mst_port->mst_mgr);

		ret = drm_dp_update_payload_part2(&radeon_connector->mst_port->mst_mgr);

		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		DRM_ERROR("DPMS OFF %d\n", dig_enc->active_mst_links);

		if (!mst_enc->enc_active)
			return;

		drm_dp_mst_reset_vcpi_slots(&radeon_connector->mst_port->mst_mgr, mst_enc->port);
		ret = drm_dp_update_payload_part1(&radeon_connector->mst_port->mst_mgr);

		drm_dp_check_act_status(&radeon_connector->mst_port->mst_mgr);
		/* and this can also fail */
		drm_dp_update_payload_part2(&radeon_connector->mst_port->mst_mgr);

		drm_dp_mst_deallocate_vcpi(&radeon_connector->mst_port->mst_mgr, mst_enc->port);

		mst_enc->enc_active = false;
		radeon_dp_mst_update_stream_attribs(radeon_connector->mst_port, primary);

		radeon_dp_mst_set_be_cntl(primary, mst_enc,
					  radeon_connector->mst_port->hpd.hpd, false);
		atombios_dig_encoder_setup2(&primary->base, ATOM_ENCODER_CMD_DP_VIDEO_OFF, 0,
					    mst_enc->fe);

		if (!mst_enc->fe_from_be)
			radeon_atom_release_dig_encoder(rdev, mst_enc->fe);

		mst_enc->fe_from_be = false;
		dig_enc->active_mst_links--;
		if (dig_enc->active_mst_links == 0) {
			/* drop link */
		}

		break;
	}

}

static bool radeon_mst_mode_fixup(struct drm_encoder *encoder,
				   const struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct radeon_encoder_mst *mst_enc;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_connector_atom_dig *dig_connector;
	int bpp = 24;

	mst_enc = radeon_encoder->enc_priv;

	mst_enc->pbn = drm_dp_calc_pbn_mode(adjusted_mode->clock, bpp);

	mst_enc->primary->active_device = mst_enc->primary->devices & mst_enc->connector->devices;
	DRM_DEBUG_KMS("setting active device to %08x from %08x %08x for encoder %d\n",
		      mst_enc->primary->active_device, mst_enc->primary->devices,
		      mst_enc->connector->devices, mst_enc->primary->base.encoder_type);


	drm_mode_set_crtcinfo(adjusted_mode, 0);
	dig_connector = mst_enc->connector->con_priv;
	dig_connector->dp_lane_count = drm_dp_max_lane_count(dig_connector->dpcd);
	dig_connector->dp_clock = drm_dp_max_link_rate(dig_connector->dpcd);
	DRM_DEBUG_KMS("dig clock %p %d %d\n", dig_connector,
		      dig_connector->dp_lane_count, dig_connector->dp_clock);
	return true;
}

static void radeon_mst_encoder_prepare(struct drm_encoder *encoder)
{
	struct radeon_connector *radeon_connector;
	struct radeon_encoder *radeon_encoder, *primary;
	struct radeon_encoder_mst *mst_enc;
	struct radeon_encoder_atom_dig *dig_enc;

	radeon_connector = radeon_mst_find_connector(encoder);
	if (!radeon_connector) {
		DRM_DEBUG_KMS("failed to find connector %p\n", encoder);
		return;
	}
	radeon_encoder = to_radeon_encoder(encoder);

	radeon_mst_encoder_dpms(encoder, DRM_MODE_DPMS_OFF);

	mst_enc = radeon_encoder->enc_priv;

	primary = mst_enc->primary;

	dig_enc = primary->enc_priv;

	mst_enc->port = radeon_connector->port;

	if (dig_enc->dig_encoder == -1) {
		dig_enc->dig_encoder = radeon_atom_pick_dig_encoder(&primary->base, -1);
		primary->offset = radeon_atom_set_enc_offset(dig_enc->dig_encoder);
		atombios_set_mst_encoder_crtc_source(encoder, dig_enc->dig_encoder);


	}
	DRM_DEBUG_KMS("%d %d\n", dig_enc->dig_encoder, primary->offset);
}

static void
radeon_mst_encoder_mode_set(struct drm_encoder *encoder,
			     struct drm_display_mode *mode,
			     struct drm_display_mode *adjusted_mode)
{
	DRM_DEBUG_KMS("\n");
}

static void radeon_mst_encoder_commit(struct drm_encoder *encoder)
{
	radeon_mst_encoder_dpms(encoder, DRM_MODE_DPMS_ON);
	DRM_DEBUG_KMS("\n");
}

static const struct drm_encoder_helper_funcs radeon_mst_helper_funcs = {
	.dpms = radeon_mst_encoder_dpms,
	.mode_fixup = radeon_mst_mode_fixup,
	.prepare = radeon_mst_encoder_prepare,
	.mode_set = radeon_mst_encoder_mode_set,
	.commit = radeon_mst_encoder_commit,
};

static void radeon_dp_mst_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs radeon_dp_mst_enc_funcs = {
	.destroy = radeon_dp_mst_encoder_destroy,
};

static struct radeon_encoder *
radeon_dp_create_fake_mst_encoder(struct radeon_connector *connector)
{
	struct drm_device *dev = connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder;
	struct radeon_encoder_mst *mst_enc;
	struct drm_encoder *encoder;
	const struct drm_connector_helper_funcs *connector_funcs = connector->base.helper_private;
	struct drm_encoder *enc_master = connector_funcs->best_encoder(&connector->base);

	DRM_DEBUG_KMS("enc master is %p\n", enc_master);
	radeon_encoder = kzalloc(sizeof(*radeon_encoder), GFP_KERNEL);
	if (!radeon_encoder)
		return NULL;

	radeon_encoder->enc_priv = kzalloc(sizeof(*mst_enc), GFP_KERNEL);
	if (!radeon_encoder->enc_priv) {
		kfree(radeon_encoder);
		return NULL;
	}
	encoder = &radeon_encoder->base;
	switch (rdev->num_crtc) {
	case 1:
		encoder->possible_crtcs = 0x1;
		break;
	case 2:
	default:
		encoder->possible_crtcs = 0x3;
		break;
	case 4:
		encoder->possible_crtcs = 0xf;
		break;
	case 6:
		encoder->possible_crtcs = 0x3f;
		break;
	}

	drm_encoder_init(dev, &radeon_encoder->base, &radeon_dp_mst_enc_funcs,
			 DRM_MODE_ENCODER_DPMST, NULL);
	drm_encoder_helper_add(encoder, &radeon_mst_helper_funcs);

	mst_enc = radeon_encoder->enc_priv;
	mst_enc->connector = connector;
	mst_enc->primary = to_radeon_encoder(enc_master);
	radeon_encoder->is_mst_encoder = true;
	return radeon_encoder;
}

int
radeon_dp_mst_init(struct radeon_connector *radeon_connector)
{
	struct drm_device *dev = radeon_connector->base.dev;

	if (!radeon_connector->ddc_bus->has_aux)
		return 0;

	radeon_connector->mst_mgr.cbs = &mst_cbs;
	return drm_dp_mst_topology_mgr_init(&radeon_connector->mst_mgr, dev,
					    &radeon_connector->ddc_bus->aux, 16, 6,
					    radeon_connector->base.base.id);
}

int
radeon_dp_mst_probe(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	int ret;
	u8 msg[1];

	if (!radeon_mst)
		return 0;

	if (!ASIC_IS_DCE5(rdev))
		return 0;

	if (dig_connector->dpcd[DP_DPCD_REV] < 0x12)
		return 0;

	ret = drm_dp_dpcd_read(&radeon_connector->ddc_bus->aux, DP_MSTM_CAP, msg,
			       1);
	if (ret) {
		if (msg[0] & DP_MST_CAP) {
			DRM_DEBUG_KMS("Sink is MST capable\n");
			dig_connector->is_mst = true;
		} else {
			DRM_DEBUG_KMS("Sink is not MST capable\n");
			dig_connector->is_mst = false;
		}

	}
	drm_dp_mst_topology_mgr_set_mst(&radeon_connector->mst_mgr,
					dig_connector->is_mst);
	return dig_connector->is_mst;
}

int
radeon_dp_mst_check_status(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	int retry;

	if (dig_connector->is_mst) {
		u8 esi[16] = { 0 };
		int dret;
		int ret = 0;
		bool handled;

		dret = drm_dp_dpcd_read(&radeon_connector->ddc_bus->aux,
				       DP_SINK_COUNT_ESI, esi, 8);
go_again:
		if (dret == 8) {
			DRM_DEBUG_KMS("got esi %02x %02x %02x\n", esi[0], esi[1], esi[2]);
			ret = drm_dp_mst_hpd_irq(&radeon_connector->mst_mgr, esi, &handled);

			if (handled) {
				for (retry = 0; retry < 3; retry++) {
					int wret;
					wret = drm_dp_dpcd_write(&radeon_connector->ddc_bus->aux,
								 DP_SINK_COUNT_ESI + 1, &esi[1], 3);
					if (wret == 3)
						break;
				}

				dret = drm_dp_dpcd_read(&radeon_connector->ddc_bus->aux,
							DP_SINK_COUNT_ESI, esi, 8);
				if (dret == 8) {
					DRM_DEBUG_KMS("got esi2 %02x %02x %02x\n", esi[0], esi[1], esi[2]);
					goto go_again;
				}
			} else
				ret = 0;

			return ret;
		} else {
			DRM_DEBUG_KMS("failed to get ESI - device may have failed %d\n", ret);
			dig_connector->is_mst = false;
			drm_dp_mst_topology_mgr_set_mst(&radeon_connector->mst_mgr,
							dig_connector->is_mst);
			/* send a hotplug event */
		}
	}
	return -EINVAL;
}

#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_mst_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct drm_connector *connector;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;
	int i;

	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort)
			continue;

		radeon_connector = to_radeon_connector(connector);
		dig_connector = radeon_connector->con_priv;
		if (radeon_connector->is_mst_connector)
			continue;
		if (!dig_connector->is_mst)
			continue;
		drm_dp_mst_dump_topology(m, &radeon_connector->mst_mgr);

		for (i = 0; i < radeon_connector->enabled_attribs; i++)
			seq_printf(m, "attrib %d: %d %d\n", i,
				   radeon_connector->cur_stream_attribs[i].fe,
				   radeon_connector->cur_stream_attribs[i].slots);
	}
	drm_modeset_unlock_all(dev);
	return 0;
}

static struct drm_info_list radeon_debugfs_mst_list[] = {
	{"radeon_mst_info", &radeon_debugfs_mst_info, 0, NULL},
};
#endif

int radeon_mst_debugfs_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, radeon_debugfs_mst_list, 1);
#endif
	return 0;
}
