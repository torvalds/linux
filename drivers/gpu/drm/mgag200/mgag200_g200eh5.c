// SPDX-License-Identifier: GPL-2.0-only

#include <linux/limits.h>
#include <linux/pci.h>
#include <linux/units.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "mgag200_drv.h"

/*
 * PIXPLLC
 */

static int mgag200_g200eh5_pixpllc_atomic_check(struct drm_crtc *crtc,
						struct drm_atomic_state *new_state)
{
	const unsigned long long VCO_MAX = 10 * GIGA; // Hz
	const unsigned long long VCO_MIN = 2500 * MEGA; // Hz
	const unsigned long long PLL_FREQ_REF = 25 * MEGA; // Hz

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;

	unsigned long long fdelta = ULLONG_MAX;

	u16 mult_max = (u16)(VCO_MAX / PLL_FREQ_REF); // 400 (0x190)
	u16 mult_min = (u16)(VCO_MIN / PLL_FREQ_REF); // 100 (0x64)

	u64 ftmp_delta;
	u64 computed_fo;

	u16 test_m;
	u8 test_div_a;
	u8 test_div_b;
	u64 fo_hz;

	u8 uc_m = 0;
	u8 uc_n = 0;
	u8 uc_p = 0;

	fo_hz = (u64)clock * HZ_PER_KHZ;

	for (test_m = mult_min; test_m <= mult_max; test_m++) { // This gives 100 <= M <= 400
		for (test_div_a = 8; test_div_a > 0; test_div_a--) { // This gives 1 <= A <= 8
			for (test_div_b = 1; test_div_b <= test_div_a; test_div_b++) {
				// This gives 1 <= B <= A
				computed_fo = (PLL_FREQ_REF * test_m) /
					(4 * test_div_a * test_div_b);

				if (computed_fo > fo_hz)
					ftmp_delta = computed_fo - fo_hz;
				else
					ftmp_delta = fo_hz - computed_fo;

				if (ftmp_delta < fdelta) {
					fdelta = ftmp_delta;
					uc_m = (u8)(0xFF & test_m);
					uc_n = (u8)((0x7 & (test_div_a - 1))
						| (0x70 & (0x7 & (test_div_b - 1)) << 4));
					uc_p = (u8)(1 & (test_m >> 8));
				}
				if (fdelta == 0)
					break;
			}
			if (fdelta == 0)
				break;
		}
		if (fdelta == 0)
			break;
	}

	pixpllc->m = uc_m + 1;
	pixpllc->n = uc_n + 1;
	pixpllc->p = uc_p + 1;
	pixpllc->s = 0;

	return 0;
	}

/*
 * Mode-setting pipeline
 */

static const struct drm_plane_helper_funcs mgag200_g200eh5_primary_plane_helper_funcs = {
	MGAG200_PRIMARY_PLANE_HELPER_FUNCS,
};

static const struct drm_plane_funcs mgag200_g200eh5_primary_plane_funcs = {
	MGAG200_PRIMARY_PLANE_FUNCS,
};

static const struct drm_crtc_helper_funcs mgag200_g200eh5_crtc_helper_funcs = {
	MGAG200_CRTC_HELPER_FUNCS,
};

static const struct drm_crtc_funcs mgag200_g200eh5_crtc_funcs = {
	MGAG200_CRTC_FUNCS,
};

static int mgag200_g200eh5_pipeline_init(struct mga_device *mdev)
{
	struct drm_device *dev = &mdev->base;
	struct drm_plane *primary_plane = &mdev->primary_plane;
	struct drm_crtc *crtc = &mdev->crtc;
	int ret;

	ret = drm_universal_plane_init(dev, primary_plane, 0,
				       &mgag200_g200eh5_primary_plane_funcs,
				       mgag200_primary_plane_formats,
				       mgag200_primary_plane_formats_size,
				       mgag200_primary_plane_fmtmods,
				       DRM_PLANE_TYPE_PRIMARY, NULL);
	if (ret) {
		drm_err(dev, "drm_universal_plane_init() failed: %d\n", ret);
		return ret;
	}
	drm_plane_helper_add(primary_plane, &mgag200_g200eh5_primary_plane_helper_funcs);
	drm_plane_enable_fb_damage_clips(primary_plane);

	ret = drm_crtc_init_with_planes(dev, crtc, primary_plane, NULL,
					&mgag200_g200eh5_crtc_funcs, NULL);
	if (ret) {
		drm_err(dev, "drm_crtc_init_with_planes() failed: %d\n", ret);
		return ret;
	}

	drm_crtc_helper_add(crtc, &mgag200_g200eh5_crtc_helper_funcs);

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

static const struct mgag200_device_info mgag200_g200eh5_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 0, false, 1, 0, false);

static const struct mgag200_device_funcs mgag200_g200eh5_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200eh5_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200eh_pixpllc_atomic_update, // same as G200EH
};

struct mga_device *mgag200_g200eh5_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv)
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

	ret = mgag200_init_pci_options(pdev, 0x00000120, 0x0000b000);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_init(mdev, &mgag200_g200eh5_device_info,
				  &mgag200_g200eh5_device_funcs);

	if (ret)
		return ERR_PTR(ret);

	mgag200_g200eh_init_registers(mdev); // same as G200EH
	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_mode_config_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_g200eh5_pipeline_init(mdev);
	if (ret)
		return ERR_PTR(ret);

	drm_mode_config_reset(dev);
	drm_kms_helper_poll_init(dev);

	return mdev;
}
