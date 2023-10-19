// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

static void mgag200_g200er_init_registers(struct mga_device *mdev)
{
	static const u8 dacvalue[] = {
		MGAG200_DAC_DEFAULT(0x00, 0xc9, 0x1f, 0x00, 0x00, 0x00)
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		WREG_DAC(i, dacvalue[i]);
	}

	WREG_DAC(0x90, 0); /* G200ER specific */

	mgag200_init_registers(mdev);

	WREG_ECRT(0x24, 0x5); /* G200ER specific */
}

static void mgag200_g200er_reset_tagfifo(struct mga_device *mdev)
{
	static const uint32_t RESET_FLAG = 0x00200000; /* undocumented magic value */
	u32 memctl;

	memctl = RREG32(MGAREG_MEMCTL);

	memctl |= RESET_FLAG;
	WREG32(MGAREG_MEMCTL, memctl);

	udelay(1000);

	memctl &= ~RESET_FLAG;
	WREG32(MGAREG_MEMCTL, memctl);
}

/*
 * PIXPLLC
 */

static int mgag200_g200er_pixpllc_atomic_check(struct drm_crtc *crtc,
					       struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 1488000;
	static const unsigned int vcomin = 1056000;
	static const unsigned int pllreffreq = 48000;
	static const unsigned int m_div_val[] = { 1, 2, 4, 8 };

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	unsigned int delta, tmpdelta;
	int testr, testn, testm, testo;
	unsigned int p, m, n, s;
	unsigned int computed, vco;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testr = 0; testr < 4; testr++) {
		if (delta == 0)
			break;
		for (testn = 5; testn < 129; testn++) {
			if (delta == 0)
				break;
			for (testm = 3; testm >= 0; testm--) {
				if (delta == 0)
					break;
				for (testo = 5; testo < 33; testo++) {
					vco = pllreffreq * (testn + 1) /
						(testr + 1);
					if (vco < vcomin)
						continue;
					if (vco > vcomax)
						continue;
					computed = vco / (m_div_val[testm] * (testo + 1));
					if (computed > clock)
						tmpdelta = computed - clock;
					else
						tmpdelta = clock - computed;
					if (tmpdelta < delta) {
						delta = tmpdelta;
						m = (testm | (testo << 3)) + 1;
						n = testn + 1;
						p = testr + 1;
						s = testr;
					}
				}
			}
		}
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_g200er_pixpllc_atomic_update(struct drm_crtc *crtc,
						 struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_pll_values *pixpllc = &mgag200_crtc_state->pixpllc;
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
	WREG8(DAC_DATA, tmp);

	WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
	tmp = RREG8(DAC_DATA);
	tmp |= MGA1064_REMHEADCTL_CLKDIS;
	WREG8(DAC_DATA, tmp);

	tmp = RREG8(MGAREG_MEM_MISC_READ);
	tmp |= (0x3<<2) | 0xc0;
	WREG8(MGAREG_MEM_MISC_WRITE, tmp);

	WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
	tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN;
	WREG8(DAC_DATA, tmp);

	udelay(500);

	WREG_DAC(MGA1064_ER_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_ER_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_ER_PIX_PLLC_P, xpixpllcp);

	udelay(50);
}

/*
 * Mode-setting pipeline
 */

static const struct drm_plane_helper_funcs mgag200_g200er_primary_plane_helper_funcs = {
	MGAG200_PRIMARY_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mgag200_g200er_primary_plane_funcs = {
	MGAG200_PRIMARY_PLANE_FUNCS,
};

static void mgag200_g200er_crtc_helper_atomic_enable(struct drm_crtc *crtc,
						     struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	const struct mgag200_device_funcs *funcs = mdev->funcs;
	struct drm_crtc_state *crtc_state = crtc->state;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	const struct drm_format_info *format = mgag200_crtc_state->format;

	if (funcs->disable_vidrst)
		funcs->disable_vidrst(mdev);

	mgag200_set_format_regs(mdev, format);
	mgag200_set_mode_regs(mdev, adjusted_mode);

	if (funcs->pixpllc_atomic_update)
		funcs->pixpllc_atomic_update(crtc, old_state);

	mgag200_g200er_reset_tagfifo(mdev);

	mgag200_enable_display(mdev);

	if (funcs->enable_vidrst)
		funcs->enable_vidrst(mdev);
}

static const struct drm_crtc_helper_funcs mgag200_g200er_crtc_helper_funcs = {
	.mode_valid = mgag200_crtc_helper_mode_valid,
	.atomic_check = mgag200_crtc_helper_atomic_check,
	.atomic_flush = mgag200_crtc_helper_atomic_flush,
	.atomic_enable = mgag200_g200er_crtc_helper_atomic_enable,
	.atomic_disable = mgag200_crtc_helper_atomic_disable
};

static const struct drm_crtc_funcs mgag200_g200er_crtc_funcs = {
	MGAG200_CRTC_FUNCS,
};

static const struct drm_encoder_funcs mgag200_g200er_dac_encoder_funcs = {
	MGAG200_DAC_ENCODER_FUNCS,
};

static const struct drm_connector_helper_funcs mgag200_g200er_vga_connector_helper_funcs = {
	MGAG200_VGA_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs mgag200_g200er_vga_connector_funcs = {
	MGAG200_VGA_CONNECTOR_FUNCS,
};

static int mgag200_g200er_pipeline_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_plane *primary_plane = &mdev->primary_plane;
	struct drm_crtc *crtc = &mdev->crtc;
	struct drm_encoder *encoder = &mdev->encoder;
	struct mga_i2c_chan *i2c = &mdev->i2c;
	struct drm_connector *connector = &mdev->connector;
	int ret;

	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &mgag200_g200er_primary_plane_funcs,
				       mgag200_primary_plane_formats,
				       mgag200_primary_plane_formats_size,
				       mgag200_primary_plane_fmtmods,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &mgag200_g200er_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&mgag200_g200er_crtc_funcs, NULL);
	if (ret) {
		drm_err(dev, "drm_crtc_init_with_planes() failed: %d\n", ret);
		return ret;
	}
	drm_crtc_helper_add(crtc, &mgag200_g200er_crtc_helper_funcs);

	/* FIXME: legacy gamma tables, but atomic gamma doesn't work without */
	drm_mode_crtc_set_gamma_size(crtc, MGAG200_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, MGAG200_LUT_SIZE);

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder, &mgag200_g200er_dac_encoder_funcs,
			       DRM_MODE_ENCODER_DAC, NULL);
	if (ret) {
		drm_err(dev, "drm_encoder_init() failed: %d\n", ret);
		return ret;
	}

	ret = mgag200_i2c_init(mdev, i2c);
	if (ret) {
		drm_err(dev, "failed to add DDC bus: %d\n", ret);
		return ret;
	}

	ret = drm_connector_init_with_ddc(dev, connector,
					  &mgag200_g200er_vga_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  &i2c->adapter);
	if (ret) {
		drm_err(dev, "drm_connector_init_with_ddc() failed: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(connector, &mgag200_g200er_vga_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		drm_err(dev, "drm_connector_attach_encoder() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200er_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 55000, false, 1, 0, false);

static const struct mgag200_device_funcs mgag200_g200er_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200er_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200er_pixpllc_atomic_update,
};

struct mga_device *mgag200_g200er_device_create(struct pci_dev *pdev, const struct drm_driver *drv)
{
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	mdev = devm_drm_dev_alloc(&pdev->dev, drv, struct mga_device, base);
	if (IS_ERR(mdev))
		return mdev;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_init(mdev, &mgag200_g200er_device_info,
				  &mgag200_g200er_device_funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200er_init_registers(mdev);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_mode_config_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200er_pipeline_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return mdev;
}
