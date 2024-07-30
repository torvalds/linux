// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

static struct mgag200_bmc_connector *to_mgag200_bmc_connector(struct drm_connector *connector)
{
	return container_of(connector, struct mgag200_bmc_connector, base);
}

void mgag200_bmc_disable_vidrst(struct mga_device *mdev)
{
	u8 tmp;
	int iter_max;

	/*
	 * 1 - The first step is to inform the BMC of an upcoming mode
	 * change. We are putting the misc<0> to output.
	 */

	WREG8(DAC_INDEX, MGA1064_GEN_IO_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_CTL, tmp);

	/* we are putting a 1 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);

	/*
	 * 2- Second step to mask any further scan request. This is
	 * done by asserting the remfreqmsk bit (XSPAREREG<7>)
	 */

	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x80;
	WREG_DAC(MGA1064_SPAREREG, tmp);

	/*
	 * 3a- The third step is to verify if there is an active scan.
	 * We are waiting for a 0 on remhsyncsts <XSPAREREG<0>).
	 */
	iter_max = 300;
	while (!(tmp & 0x1) && iter_max) {
		WREG8(DAC_INDEX, MGA1064_SPAREREG);
		tmp = RREG8(DAC_DATA);
		udelay(1000);
		iter_max--;
	}

	/*
	 * 3b- This step occurs only if the remove is actually
	 * scanning. We are waiting for the end of the frame which is
	 * a 1 on remvsyncsts (XSPAREREG<1>)
	 */
	if (iter_max) {
		iter_max = 300;
		while ((tmp & 0x2) && iter_max) {
			WREG8(DAC_INDEX, MGA1064_SPAREREG);
			tmp = RREG8(DAC_DATA);
			udelay(1000);
			iter_max--;
		}
	}
}

void mgag200_bmc_enable_vidrst(struct mga_device *mdev)
{
	u8 tmp;

	/* Ensure that the vrsten and hrsten are set */
	WREG8(MGAREG_CRTCEXT_INDEX, 1);
	tmp = RREG8(MGAREG_CRTCEXT_DATA);
	WREG8(MGAREG_CRTCEXT_DATA, tmp | 0x88);

	/* Assert rstlvl2 */
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	tmp = RREG8(DAC_DATA);
	tmp |= 0x8;
	WREG8(DAC_DATA, tmp);

	udelay(10);

	/* Deassert rstlvl2 */
	tmp &= ~0x08;
	WREG8(DAC_INDEX, MGA1064_REMHEADCTL2);
	WREG8(DAC_DATA, tmp);

	/* Remove mask of scan request */
	WREG8(DAC_INDEX, MGA1064_SPAREREG);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x80;
	WREG8(DAC_DATA, tmp);

	/* Put back a 0 on the misc<0> line */
	WREG8(DAC_INDEX, MGA1064_GEN_IO_DATA);
	tmp = RREG8(DAC_DATA);
	tmp &= ~0x10;
	WREG_DAC(MGA1064_GEN_IO_DATA, tmp);
}

static const struct drm_encoder_funcs mgag200_bmc_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int mgag200_bmc_connector_helper_detect_ctx(struct drm_connector *connector,
						   struct drm_modeset_acquire_ctx *ctx,
						   bool force)
{
	struct mgag200_bmc_connector *bmc_connector = to_mgag200_bmc_connector(connector);
	struct drm_connector *physical_connector = bmc_connector->physical_connector;

	/*
	 * Most user-space compositors cannot handle more than one connected
	 * connector per CRTC. Hence, we only mark the BMC as connected if the
	 * physical connector is disconnected. If the physical connector's status
	 * is connected or unknown, the BMC remains disconnected. This has no
	 * effect on the output of the BMC.
	 *
	 * FIXME: Remove this logic once user-space compositors can handle more
	 *        than one connector per CRTC. The BMC should always be connected.
	 */

	if (physical_connector && physical_connector->status == connector_status_disconnected)
		return connector_status_connected;

	return connector_status_disconnected;
}

static int mgag200_bmc_connector_helper_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct mga_device *mdev = to_mga_device(dev);
	const struct mgag200_device_info *minfo = mdev->info;

	return drm_add_modes_noedid(connector, minfo->max_hdisplay, minfo->max_vdisplay);
}

static const struct drm_connector_helper_funcs mgag200_bmc_connector_helper_funcs = {
	.get_modes = mgag200_bmc_connector_helper_get_modes,
	.detect_ctx = mgag200_bmc_connector_helper_detect_ctx,
};

static const struct drm_connector_funcs mgag200_bmc_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int mgag200_bmc_connector_init(struct drm_device *dev,
				      struct mgag200_bmc_connector *bmc_connector,
				      struct drm_connector *physical_connector)
{
	struct drm_connector *connector = &bmc_connector->base;
	int ret;

	ret = drm_connector_init(dev, connector, &mgag200_bmc_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret)
		return ret;
	drm_connector_helper_add(connector, &mgag200_bmc_connector_helper_funcs);

	bmc_connector->physical_connector = physical_connector;

	return 0;
}

int mgag200_bmc_output_init(struct mga_device *mdev, struct drm_connector *physical_connector)
{
	struct drm_device *dev = &mdev->base;
	struct drm_crtc *crtc = &mdev->crtc;
	struct drm_encoder *encoder;
	struct mgag200_bmc_connector *bmc_connector;
	struct drm_connector *connector;
	int ret;

	encoder = &mdev->output.bmc.encoder;
	ret = drm_encoder_init(dev, encoder, &mgag200_bmc_encoder_funcs,
			       DRM_MODE_ENCODER_VIRTUAL, NULL);
	if (ret)
		return ret;
	encoder->possible_crtcs = drm_crtc_mask(crtc);

	bmc_connector = &mdev->output.bmc.bmc_connector;
	ret = mgag200_bmc_connector_init(dev, bmc_connector, physical_connector);
	if (ret)
		return ret;
	connector = &bmc_connector->base;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret)
		return ret;

	return 0;
}
