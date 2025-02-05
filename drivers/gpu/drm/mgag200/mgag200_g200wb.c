// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

void mgag200_g200wb_init_registers(struct mga_device *mdev)
{
	static const u8 dacvalue[] = {
		MGAG200_DAC_DEFAULT(0x07, 0xc9, 0x1f, 0x00, 0x00, 0x00)
	};

	size_t i;

	for (i = 0; i < ARRAY_SIZE(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)) ||
		    ((i >= 0x44) && (i <= 0x4e)))
			continue;
		WREG_DAC(i, dacvalue[i]);
	}

	mgag200_init_registers(mdev);
}

/*
 * PIXPLLC
 */

static int mgag200_g200wb_pixpllc_atomic_check(struct drm_crtc *crtc,
					       struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 550000;
	static const unsigned int vcomin = 150000;
	static const unsigned int pllreffreq = 48000;

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testp = 1; testp < 9; testp++) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testm = 1; testm < 17; testm++) {
			for (testn = 1; testn < 151; testn++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					n = testn;
					m = testm;
					p = testp;
					s = 0;
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

void mgag200_g200wb_pixpllc_atomic_update(struct drm_crtc *crtc,
					  struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_pll_values *pixpllc = &mgag200_crtc_state->pixpllc;
	bool pll_locked = false;
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp, tmp;
	int i, j, tmpcount, vcount;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = ((pixpllcn & BIT(8)) >> 1) | pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = ((pixpllcn & GENMASK(10, 9)) >> 3) | (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	for (i = 0; i <= 32 && pll_locked == false; i++) {
		if (i > 0) {
			WREG8(MGAREG_CRTC_INDEX, 0x1e);
			tmp = RREG8(MGAREG_CRTC_DATA);
			if (tmp < 0xff)
				WREG8(MGAREG_CRTC_DATA, tmp+1);
		}

		/* set pixclkdis to 1 */
		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_DIS;
		WREG8(DAC_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_REMHEADCTL_CLKDIS;
		WREG8(DAC_DATA, tmp);

		/* select PLL Set C */
		tmp = RREG8(MGAREG_MEM_MISC_READ);
		tmp |= 0x3 << 2;
		WREG8(MGAREG_MEM_MISC_WRITE, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= MGA1064_PIX_CLK_CTL_CLK_POW_DOWN | 0x80;
		WREG8(DAC_DATA, tmp);

		udelay(500);

		/* reset the PLL */
		WREG8(DAC_INDEX, MGA1064_VREF_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~0x04;
		WREG8(DAC_DATA, tmp);

		udelay(50);

		/* program pixel pll register */
		WREG_DAC(MGA1064_WB_PIX_PLLC_N, xpixpllcn);
		WREG_DAC(MGA1064_WB_PIX_PLLC_M, xpixpllcm);
		WREG_DAC(MGA1064_WB_PIX_PLLC_P, xpixpllcp);

		udelay(50);

		/* turn pll on */
		WREG8(DAC_INDEX, MGA1064_VREF_CTL);
		tmp = RREG8(DAC_DATA);
		tmp |= 0x04;
		WREG_DAC(MGA1064_VREF_CTL, tmp);

		udelay(500);

		/* select the pixel pll */
		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_SEL_MSK;
		tmp |= MGA1064_PIX_CLK_CTL_SEL_PLL;
		WREG8(DAC_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_REMHEADCTL_CLKSL_MSK;
		tmp |= MGA1064_REMHEADCTL_CLKSL_PLL;
		WREG8(DAC_DATA, tmp);

		/* reset dotclock rate bit */
		WREG8(MGAREG_SEQ_INDEX, 1);
		tmp = RREG8(MGAREG_SEQ_DATA);
		tmp &= ~0x8;
		WREG8(MGAREG_SEQ_DATA, tmp);

		WREG8(DAC_INDEX, MGA1064_PIX_CLK_CTL);
		tmp = RREG8(DAC_DATA);
		tmp &= ~MGA1064_PIX_CLK_CTL_CLK_DIS;
		WREG8(DAC_DATA, tmp);

		vcount = RREG8(MGAREG_VCOUNT);

		for (j = 0; j < 30 && pll_locked == false; j++) {
			tmpcount = RREG8(MGAREG_VCOUNT);
			if (tmpcount < vcount)
				vcount = 0;
			if ((tmpcount - vcount) > 2)
				pll_locked = true;
			else
				udelay(5);
		}
	}

	WREG8(DAC_INDEX, MGA1064_REMHEADCTL);
	tmp = RREG8(DAC_DATA);
	tmp &= ~MGA1064_REMHEADCTL_CLKDIS;
	WREG_DAC(MGA1064_REMHEADCTL, tmp);
}

/*
 * Mode-setting pipeline
 */

static const struct drm_plane_helper_funcs mgag200_g200wb_primary_plane_helper_funcs = {
	MGAG200_PRIMARY_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mgag200_g200wb_primary_plane_funcs = {
	MGAG200_PRIMARY_PLANE_FUNCS,
};

static const struct drm_crtc_helper_funcs mgag200_g200wb_crtc_helper_funcs = {
	MGAG200_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs mgag200_g200wb_crtc_funcs = {
	MGAG200_CRTC_FUNCS,
};

static int mgag200_g200wb_pipeline_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_plane *primary_plane = &mdev->primary_plane;
	struct drm_crtc *crtc = &mdev->crtc;
	int ret;

	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &mgag200_g200wb_primary_plane_funcs,
				       mgag200_primary_plane_formats,
				       mgag200_primary_plane_formats_size,
				       mgag200_primary_plane_fmtmods,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &mgag200_g200wb_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&mgag200_g200wb_crtc_funcs, NULL);
	if (ret) {
		drm_err(dev, "drm_crtc_init_with_planes() failed: %d\n", ret);
		return ret;
	}
	drm_crtc_helper_add(crtc, &mgag200_g200wb_crtc_helper_funcs);

	/* FIXME: legacy gamma tables, but atomic gamma doesn't work without */
	drm_mode_crtc_set_gamma_size(crtc, MGAG200_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, MGAG200_LUT_SIZE);

	ret = mgag200_vga_bmc_output_init(mdev);
	if (ret)
		return ret;

	return 0;
}

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200wb_device_info =
	MGAG200_DEVICE_INFO_INIT(1280, 1024, 31877, true, 0, 1, false);

static const struct mgag200_device_funcs mgag200_g200wb_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200wb_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200wb_pixpllc_atomic_update,
};

struct mga_device *mgag200_g200wb_device_create(struct pci_dev *pdev, const struct drm_driver *drv)
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

	ret = mgag200_init_pci_options(pdev, 0x41049120, 0x0000b000);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_init(mdev, &mgag200_g200wb_device_info,
				  &mgag200_g200wb_device_funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200wb_init_registers(mdev);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_mode_config_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200wb_pipeline_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);
	drm_kms_helper_poll_init(dev);

	return mdev;
}
