// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>
#include <linux/vmalloc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

static int mgag200_g200_init_pci_options(struct pci_dev *pdev)
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

	if (has_sgram)
		option = 0x4049cd21;
	else
		option = 0x40499121;

	return mgag200_init_pci_options(pdev, option, 0x00008000);
}

static void mgag200_g200_init_registers(struct mgag200_g200_device *g200)
{
	static const u8 dacvalue[] = {
		MGAG200_DAC_DEFAULT(0x00, 0xc9, 0x1f,
				    0x04, 0x2d, 0x19)
	};

	struct mga_device *mdev = &g200->base;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(dacvalue); ++i) {
		if ((i <= 0x17) ||
		    (i == 0x1b) ||
		    (i == 0x1c) ||
		    ((i >= 0x1f) && (i <= 0x29)) ||
		    ((i >= 0x30) && (i <= 0x37)))
			continue;
		WREG_DAC(i, dacvalue[i]);
	}

	mgag200_init_registers(mdev);
}

/*
 * PIXPLLC
 */

static int mgag200_g200_pixpllc_atomic_check(struct drm_crtc *crtc, struct drm_atomic_state *new_state)
{
	static const int post_div_max = 7;
	static const int in_div_min = 1;
	static const int in_div_max = 6;
	static const int feed_div_min = 7;
	static const int feed_div_max = 127;

	struct drm_device *dev = crtc->dev;
	struct mgag200_g200_device *g200 = to_mgag200_g200_device(dev);
	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	u8 testp, testm, testn;
	u8 n = 0, m = 0, p, s;
	long f_vco;
	long computed;
	long delta, tmp_delta;
	long ref_clk = g200->ref_clk;
	long p_clk_min = g200->pclk_min;
	long p_clk_max = g200->pclk_max;

	if (clock > p_clk_max) {
		drm_err(dev, "Pixel Clock %ld too high\n", clock);
		return -EINVAL;
	}

	if (clock < p_clk_min >> 3)
		clock = p_clk_min >> 3;

	f_vco = clock;
	for (testp = 0;
	     testp <= post_div_max && f_vco < p_clk_min;
	     testp = (testp << 1) + 1, f_vco <<= 1)
		;
	p = testp + 1;

	delta = clock;

	for (testm = in_div_min; testm <= in_div_max; testm++) {
		for (testn = feed_div_min; testn <= feed_div_max; testn++) {
			computed = ref_clk * (testn + 1) / (testm + 1);
			if (computed < f_vco)
				tmp_delta = f_vco - computed;
			else
				tmp_delta = computed - f_vco;
			if (tmp_delta < delta) {
				delta = tmp_delta;
				m = testm + 1;
				n = testn + 1;
			}
		}
	}
	f_vco = ref_clk * n / m;
	if (f_vco < 100000)
		s = 0;
	else if (f_vco < 140000)
		s = 1;
	else if (f_vco < 180000)
		s = 2;
	else
		s = 3;

	drm_dbg_kms(dev, "clock: %ld vco: %ld m: %d n: %d p: %d s: %d\n",
		    clock, f_vco, m, n, p, s);

	pixpllc->m = m;
	pixpllc->n = n;
	pixpllc->p = p;
	pixpllc->s = s;

	return 0;
}

static void mgag200_g200_pixpllc_atomic_update(struct drm_crtc *crtc,
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

	xpixpllcm = pixpllcm;
	xpixpllcn = pixpllcn;
	xpixpllcp = (pixpllcs << 3) | pixpllcp;

	WREG_MISC_MASKED(MGAREG_MISC_CLKSEL_MGA, MGAREG_MISC_CLKSEL_MASK);

	WREG_DAC(MGA1064_PIX_PLLC_M, xpixpllcm);
	WREG_DAC(MGA1064_PIX_PLLC_N, xpixpllcn);
	WREG_DAC(MGA1064_PIX_PLLC_P, xpixpllcp);
}

/*
 * Mode-setting pipeline
 */

static const struct drm_plane_helper_funcs mgag200_g200_primary_plane_helper_funcs = {
	MGAG200_PRIMARY_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mgag200_g200_primary_plane_funcs = {
	MGAG200_PRIMARY_PLANE_FUNCS,
};

static const struct drm_crtc_helper_funcs mgag200_g200_crtc_helper_funcs = {
	MGAG200_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs mgag200_g200_crtc_funcs = {
	MGAG200_CRTC_FUNCS,
};

static const struct drm_encoder_funcs mgag200_g200_dac_encoder_funcs = {
	MGAG200_DAC_ENCODER_FUNCS,
};

static const struct drm_connector_helper_funcs mgag200_g200_vga_connector_helper_funcs = {
	MGAG200_VGA_CONNECTOR_HELPER_FUNCS,
};

static const struct drm_connector_funcs mgag200_g200_vga_connector_funcs = {
	MGAG200_VGA_CONNECTOR_FUNCS,
};

static int mgag200_g200_pipeline_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_plane *primary_plane = &mdev->primary_plane;
	struct drm_crtc *crtc = &mdev->crtc;
	struct drm_encoder *encoder = &mdev->encoder;
	struct mga_i2c_chan *i2c = &mdev->i2c;
	struct drm_connector *connector = &mdev->connector;
	int ret;

	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &mgag200_g200_primary_plane_funcs,
				       mgag200_primary_plane_formats,
				       mgag200_primary_plane_formats_size,
				       mgag200_primary_plane_fmtmods,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &mgag200_g200_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&mgag200_g200_crtc_funcs, NULL);
	if (ret) {
		drm_err(dev, "drm_crtc_init_with_planes() failed: %d\n", ret);
		return ret;
	}
	drm_crtc_helper_add(crtc, &mgag200_g200_crtc_helper_funcs);

	/* FIXME: legacy gamma tables, but atomic gamma doesn't work without */
	drm_mode_crtc_set_gamma_size(crtc, MGAG200_LUT_SIZE);
	drm_crtc_enable_color_mgmt(crtc, 0, false, MGAG200_LUT_SIZE);

	encoder->possible_crtcs = drm_crtc_mask(crtc);
	ret = drm_encoder_init(dev, encoder, &mgag200_g200_dac_encoder_funcs,
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
					  &mgag200_g200_vga_connector_funcs,
					  DRM_MODE_CONNECTOR_VGA,
					  &i2c->adapter);
	if (ret) {
		drm_err(dev, "drm_connector_init_with_ddc() failed: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(connector, &mgag200_g200_vga_connector_helper_funcs);

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		drm_err(dev, "drm_connector_attach_encoder() failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * DRM Device
 */

static const struct mgag200_device_info mgag200_g200_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 0, false, 1, 3, false);

static void mgag200_g200_interpret_bios(struct mgag200_g200_device *g200,
					const unsigned char *bios, size_t size)
{
	static const char matrox[] = {'M', 'A', 'T', 'R', 'O', 'X'};
	static const unsigned int expected_length[6] = {
		0, 64, 64, 64, 128, 128
	};
	struct mga_device *mdev = &g200->base;
	struct drm_device *dev = &mdev->base;
	const unsigned char *pins;
	unsigned int pins_len, version;
	int offset;
	int tmp;

	/* Test for MATROX string. */
	if (size < 45 + sizeof(matrox))
		return;
	if (memcmp(&bios[45], matrox, sizeof(matrox)) != 0)
		return;

	/* Get the PInS offset. */
	if (size < MGA_BIOS_OFFSET + 2)
		return;
	offset = (bios[MGA_BIOS_OFFSET + 1] << 8) | bios[MGA_BIOS_OFFSET];

	/* Get PInS data structure. */

	if (size < offset + 6)
		return;
	pins = bios + offset;
	if (pins[0] == 0x2e && pins[1] == 0x41) {
		version = pins[5];
		pins_len = pins[2];
	} else {
		version = 1;
		pins_len = pins[0] + (pins[1] << 8);
	}

	if (version < 1 || version > 5) {
		drm_warn(dev, "Unknown BIOS PInS version: %d\n", version);
		return;
	}
	if (pins_len != expected_length[version]) {
		drm_warn(dev, "Unexpected BIOS PInS size: %d expected: %d\n",
			 pins_len, expected_length[version]);
		return;
	}
	if (size < offset + pins_len)
		return;

	drm_dbg_kms(dev, "MATROX BIOS PInS version %d size: %d found\n", version, pins_len);

	/* Extract the clock values */

	switch (version) {
	case 1:
		tmp = pins[24] + (pins[25] << 8);
		if (tmp)
			g200->pclk_max = tmp * 10;
		break;
	case 2:
		if (pins[41] != 0xff)
			g200->pclk_max = (pins[41] + 100) * 1000;
		break;
	case 3:
		if (pins[36] != 0xff)
			g200->pclk_max = (pins[36] + 100) * 1000;
		if (pins[52] & 0x20)
			g200->ref_clk = 14318;
		break;
	case 4:
		if (pins[39] != 0xff)
			g200->pclk_max = pins[39] * 4 * 1000;
		if (pins[92] & 0x01)
			g200->ref_clk = 14318;
		break;
	case 5:
		tmp = pins[4] ? 8000 : 6000;
		if (pins[123] != 0xff)
			g200->pclk_min = pins[123] * tmp;
		if (pins[38] != 0xff)
			g200->pclk_max = pins[38] * tmp;
		if (pins[110] & 0x01)
			g200->ref_clk = 14318;
		break;
	default:
		break;
	}
}

static void mgag200_g200_init_refclk(struct mgag200_g200_device *g200)
{
	struct mga_device *mdev = &g200->base;
	struct drm_device *dev = &mdev->base;
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	unsigned char __iomem *rom;
	unsigned char *bios;
	size_t size;

	g200->pclk_min = 50000;
	g200->pclk_max = 230000;
	g200->ref_clk = 27050;

	rom = pci_map_rom(pdev, &size);
	if (!rom)
		return;

	bios = vmalloc(size);
	if (!bios)
		goto out;
	memcpy_fromio(bios, rom, size);

	if (size != 0 && bios[0] == 0x55 && bios[1] == 0xaa)
		mgag200_g200_interpret_bios(g200, bios, size);

	drm_dbg_kms(dev, "pclk_min: %ld pclk_max: %ld ref_clk: %ld\n",
		    g200->pclk_min, g200->pclk_max, g200->ref_clk);

	vfree(bios);
out:
	pci_unmap_rom(pdev, rom);
}

static const struct mgag200_device_funcs mgag200_g200_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200_pixpllc_atomic_update,
};

struct mga_device *mgag200_g200_device_create(struct pci_dev *pdev, const struct drm_driver *drv)
{
	struct mgag200_g200_device *g200;
	struct mga_device *mdev;
	struct drm_device *dev;
	resource_size_t vram_available;
	int ret;

	g200 = devm_drm_dev_alloc(&pdev->dev, drv, struct mgag200_g200_device, base.base);
	if (IS_ERR(g200))
		return ERR_CAST(g200);
	mdev = &g200->base;
	dev = &mdev->base;

	pci_set_drvdata(pdev, dev);

	ret = mgag200_g200_init_pci_options(pdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200_init_refclk(g200);

	ret = mgag200_device_init(mdev, &mgag200_g200_device_info,
				  &mgag200_g200_device_funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200_init_registers(g200);

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_mode_config_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200_pipeline_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);

	return mdev;
}
