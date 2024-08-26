// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

static int mgag200_g200se_init_pci_options(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	bool has_sgram;
	u32 option;
	int err;

	err = pci_read_config_dword(pdev, PCI_MGA_OPTION, &option);
	if (err != PCIBIOS_SUCCESSFUL) {
		dev_err(dev, "pci_read_config_dword(PCI_MGA_OPTION) failed: %d\n", err);
		return pcibios_err_to_errno(err);
	}

	has_sgram = !!(option & PCI_MGA_OPTION_HARDPWMSK);

	option = 0x40049120;
	if (has_sgram)
		option |= PCI_MGA_OPTION_HARDPWMSK;

	return mgag200_init_pci_options(pdev, option, 0x00008000);
}

static void mgag200_g200se_init_registers(struct mgag200_g200se_device *g200se)
{
	static const u8 dacvalue[] = {
		MGAG200_DAC_DEFAULT(0x03,
				    MGA1064_PIX_CLK_CTL_SEL_PLL,
				    MGA1064_MISC_CTL_DAC_EN |
				    MGA1064_MISC_CTL_VGA8 |
				    MGA1064_MISC_CTL_DAC_RAM_CS,
				    0x00, 0x00, 0x00)
	};

	struct mga_device *mdev = &g200se->base;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(dacvalue); i++) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i == 0x2c) || (i == 0x2d) || (i == 0x2e)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		WREG_DAC(i, dacvalue[i]);
	}

	mgag200_init_registers(mdev);
}

static void mgag200_g200se_set_hiprilvl(struct mga_device *mdev,
					const struct drm_display_mode *mode,
					const struct drm_format_info *format)
{
	struct mgag200_g200se_device *g200se = to_mgag200_g200se_device(&mdev->base);
	unsigned int hiprilvl;
	u8 crtcext6;

	if  (g200se->unique_rev_id >= 0x04) {
		hiprilvl = 0;
	} else if (g200se->unique_rev_id >= 0x02) {
		unsigned int bpp;
		unsigned long mb;

		if (format->cpp[0] * 8 > 16)
			bpp = 32;
		else if (format->cpp[0] * 8 > 8)
			bpp = 16;
		else
			bpp = 8;

		mb = (mode->clock * bpp) / 1000;
		if (mb > 3100)
			hiprilvl = 0;
		else if (mb > 2600)
			hiprilvl = 1;
		else if (mb > 1900)
			hiprilvl = 2;
		else if (mb > 1160)
			hiprilvl = 3;
		else if (mb > 440)
			hiprilvl = 4;
		else
			hiprilvl = 5;

	} else if (g200se->unique_rev_id >= 0x01) {
		hiprilvl = 3;
	} else {
		hiprilvl = 4;
	}

	crtcext6 = hiprilvl; /* implicitly sets maxhipri to 0 */

	WREG_ECRT(0x06, crtcext6);
}

/*
 * PIXPLLC
 */

static int mgag200_g200se_00_pixpllc_atomic_check(struct drm_crtc *crtc,
						  struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 320000;
	static const unsigned int vcomin = 160000;
	static const unsigned int pllreffreq = 25000;

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;
	permitteddelta = clock * 5 / 1000;

	for (testp = 8; testp > 0; testp /= 2) {
		if (clock * testp > vcomax)
			continue;
		if (clock * testp < vcomin)
			continue;

		for (testn = 17; testn < 256; testn++) {
			for (testm = 1; testm < 32; testm++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;
				if (tmpdelta < delta) {
					delta = tmpdelta;
					m = testm;
					n = testn;
					p = testp;
				}
			}
		}
	}

	if (delta > permitteddelta) {
		pr_warn("PLL delta too large\n");
		return -EINVAL;
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_g200se_00_pixpllc_atomic_update(struct drm_crtc *crtc,
						    struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_pll_values *pixpllc = &mgag200_crtc_state->pixpllc;
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	xpixpllcm = pixpllcm | ((pixpllcn & BIT(8)) >> 1);
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);
}

static int mgag200_g200se_04_pixpllc_atomic_check(struct drm_crtc *crtc,
						  struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 1600000;
	static const unsigned int vcomin = 800000;
	static const unsigned int pllreffreq = 25000;
	static const unsigned int pvalues_e4[] = {16, 14, 12, 10, 8, 6, 4, 2, 1};

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	unsigned int delta, tmpdelta, permitteddelta;
	unsigned int testp, testm, testn;
	unsigned int p, m, n, s;
	unsigned int computed;
	unsigned int fvv;
	unsigned int i;

	m = n = p = s = 0;
	delta = 0xffffffff;

	if (clock < 25000)
		clock = 25000;
	clock = clock * 2;

	/* Permited delta is 0.5% as VESA Specification */
	permitteddelta = clock * 5 / 1000;

	for (i = 0 ; i < ARRAY_SIZE(pvalues_e4); i++) {
		testp = pvalues_e4[i];

		if ((clock * testp) > vcomax)
			continue;
		if ((clock * testp) < vcomin)
			continue;

		for (testn = 50; testn <= 256; testn++) {
			for (testm = 1; testm <= 32; testm++) {
				computed = (pllreffreq * testn) / (testm * testp);
				if (computed > clock)
					tmpdelta = computed - clock;
				else
					tmpdelta = clock - computed;

				if (tmpdelta < delta) {
					delta = tmpdelta;
					m = testm;
					n = testn;
					p = testp;
				}
			}
		}
	}

	fvv = pllreffreq * n / m;
	fvv = (fvv - 800000) / 50000;
	if (fvv > 15)
		fvv = 15;
	s = fvv << 1;

	if (delta > permitteddelta) {
		pr_warn("PLL delta too large\n");
		return -EINVAL;
	}

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_g200se_04_pixpllc_atomic_update(struct drm_crtc *crtc,
						    struct drm_atomic_state *old_state)
{
	struct drm_device *dev = crtc->dev;
	struct mga_device *mdev = to_mga_device(dev);
	struct drm_crtc_state *crtc_state = crtc->state;
	struct mgag200_crtc_state *mgag200_crtc_state = to_mgag200_crtc_state(crtc_state);
	struct mgag200_pll_values *pixpllc = &mgag200_crtc_state->pixpllc;
	unsigned int pixpllcm, pixpllcn, pixpllcp, pixpllcs;
	u8 xpixpllcm, xpixpllcn, xpixpllcp;

	pixpllcm = pixpllc->m - 1;
	pixpllcn = pixpllc->n - 1;
	pixpllcp = pixpllc->p - 1;
	pixpllcs = pixpllc->s;

	// For G200SE A, BIT(7) should be set unconditionally.
	xpixpllcm = BIT(7) | pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);

	WREG_DAC(0x1a, 0x09);
	msleep(20);
	WREG_DAC(0x1a, 0x01);
}

/*
 * Mode-setting pipeline
 */

static const struct drm_plane_helper_funcs mgag200_g200se_primary_plane_helper_funcs = {
	MGAG200_PRIMARY_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mgag200_g200se_primary_plane_funcs = {
	MGAG200_PRIMARY_PLANE_FUNCS,
};

static void mgag200_g200se_crtc_helper_atomic_enable(struct drm_crtc *crtc,
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

	mgag200_g200se_set_hiprilvl(mdev, adjusted_mode, format);

	if (crtc_state->gamma_lut)
		mgag200_crtc_set_gamma(mdev, format, crtc_state->gamma_lut->data);
	else
		mgag200_crtc_set_gamma_linear(mdev, format);

	mgag200_enable_display(mdev);

	if (funcs->enable_vidrst)
		funcs->enable_vidrst(mdev);
}

static const struct drm_crtc_helper_funcs mgag200_g200se_crtc_helper_funcs = {
	.mode_valid = mgag200_crtc_helper_mode_valid,
	.atomic_check = mgag200_crtc_helper_atomic_check,
	.atomic_flush = mgag200_crtc_helper_atomic_flush,
	.atomic_enable = mgag200_g200se_crtc_helper_atomic_enable,
	.atomic_disable = mgag200_crtc_helper_atomic_disable
};

static const struct drm_crtc_funcs mgag200_g200se_crtc_funcs = {
	MGAG200_CRTC_FUNCS,
};

static int mgag200_g200se_pipeline_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_plane *primary_plane = &mdev->primary_plane;
	struct drm_crtc *crtc = &mdev->crtc;
	int ret;

	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &mgag200_g200se_primary_plane_funcs,
				       mgag200_primary_plane_formats,
				       mgag200_primary_plane_formats_size,
				       mgag200_primary_plane_fmtmods,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &mgag200_g200se_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&mgag200_g200se_crtc_funcs, NULL);
	if (ret) {
		drm_err(dev, "drm_crtc_init_with_planes() failed: %d\n", ret);
		return ret;
	}
	drm_crtc_helper_add(crtc, &mgag200_g200se_crtc_helper_funcs);

	/* FIXME: legacy gamma tables, but atomic gamma doesn't work without */
	drm_mode_crtc_set_gamma_size(crtc, MGAG200_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, MGAG200_LUT_SIZE);

	ret = mgag200_vga_output_init(mdev);
	if (ret)
		return ret;

	ret = mgag200_bmc_output_init(mdev, &mdev->output.vga.connector);
	if (ret)
		return ret;

	return 0;
}

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200se_a_01_device_info =
	MGAG200_DEVICE_INFO_INIT(1600, 1200, 24400, false, 0, 1, true);

static const struct mgag200_device_info mgag200_g200se_a_02_device_info =
	MGAG200_DEVICE_INFO_INIT(1920, 1200, 30100, false, 0, 1, true);

static const struct mgag200_device_info mgag200_g200se_a_03_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 55000, false, 0, 1, false);

static const struct mgag200_device_info mgag200_g200se_b_01_device_info =
	MGAG200_DEVICE_INFO_INIT(1600, 1200, 24400, false, 0, 1, false);

static const struct mgag200_device_info mgag200_g200se_b_02_device_info =
	MGAG200_DEVICE_INFO_INIT(1920, 1200, 30100, false, 0, 1, false);

static const struct mgag200_device_info mgag200_g200se_b_03_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 55000, false, 0, 1, false);

static int mgag200_g200se_init_unique_rev_id(struct mgag200_g200se_device *g200se)
{
	struct mga_device *mdev = &g200se->base;
	struct drm_device *dev = &mdev->base;

	/* stash G200 SE model number for later use */
	g200se->unique_rev_id = RREG32(0x1e24);
	if (!g200se->unique_rev_id)
		return -ENODEV;

	drm_dbg(dev, "G200 SE unique revision id is 0x%x\n", g200se->unique_rev_id);

	return 0;
}

static const struct mgag200_device_funcs mgag200_g200se_00_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200se_00_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200se_00_pixpllc_atomic_update,
};

static const struct mgag200_device_funcs mgag200_g200se_04_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200se_04_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200se_04_pixpllc_atomic_update,
};

struct mga_device *mgag200_g200se_device_create(struct pci_dev *pdev, const struct drm_driver *drv,
						enum mga_type type)
{
	struct mgag200_g200se_device *g200se;
	const struct mgag200_device_info *info;
	const struct mgag200_device_funcs *funcs;
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	g200se = devm_drm_dev_alloc(&pdev->dev, drv, struct mgag200_g200se_device, base.base);
	if (IS_ERR(g200se))
		return ERR_CAST(g200se);
	mdev = &g200se->base;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_g200se_init_pci_options(pdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200se_init_unique_rev_id(g200se);
	if (ret)
		return ERR_PTR(ret);

	switch (type) {
	case G200_SE_A:
		if (g200se->unique_rev_id >= 0x03)
			info = &mgag200_g200se_a_03_device_info;
		else if (g200se->unique_rev_id >= 0x02)
			info = &mgag200_g200se_a_02_device_info;
		else
			info = &mgag200_g200se_a_01_device_info;
		break;
	case G200_SE_B:
		if (g200se->unique_rev_id >= 0x03)
			info = &mgag200_g200se_b_03_device_info;
		else if (g200se->unique_rev_id >= 0x02)
			info = &mgag200_g200se_b_02_device_info;
		else
			info = &mgag200_g200se_b_01_device_info;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	if (g200se->unique_rev_id >= 0x04)
		funcs = &mgag200_g200se_04_device_funcs;
	else
		funcs = &mgag200_g200se_00_device_funcs;

	ret = mgag200_device_init(mdev, info, funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200se_init_registers(g200se);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_mode_config_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200se_pipeline_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);
	drm_kms_helper_poll_init(dev);

	return mdev;
}
