// SPDX-License-Identifier: GPL-2.0-only

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>

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

	xpixpllcm = pixpllcm | ((pixpllcn & BIT(8)) >> 1);
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

	ret = mgag200_device_init(mdev, type, info, funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200se_init_registers(g200se);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
