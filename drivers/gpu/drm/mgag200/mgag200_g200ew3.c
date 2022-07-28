// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>

#include "mgag200_drv.h"

static void mgag200_g200ew3_init_registers(struct mga_device *mdev)
{
	mgag200_g200wb_init_registers(mdev); // same as G200WB

	WREG_ECRT(0x34, 0x5); // G200EW3 specific
}

/*
 * PIXPLLC
 */

static int mgag200_g200ew3_pixpllc_atomic_check(struct drm_crtc *crtc,
						struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 800000;
	static const unsigned int vcomin = 400000;
	static const unsigned int pllreffreq = 25000;

	struct drm_crtc_state *new_crtc_state = drm_atomic_get_new_crtc_state(new_state, crtc);
	struct mgag200_crtc_state *new_mgag200_crtc_state = to_mgag200_crtc_state(new_crtc_state);
	long clock = new_crtc_state->mode.clock;
	struct mgag200_pll_values *pixpllc = &new_mgag200_crtc_state->pixpllc;
	unsigned int delta, tmpdelta;
	unsigned int testp, testm, testn, testp2;
	unsigned int p, m, n, s;
	unsigned int computed;

	m = n = p = s = 0;
	delta = 0xffffffff;

	for (testp = 1; testp < 8; testp++) {
		for (testp2 = 1; testp2 < 8; testp2++) {
			if (testp < testp2)
				continue;
			if ((clock * testp * testp2) > vcomax)
				continue;
			if ((clock * testp * testp2) < vcomin)
				continue;
			for (testm = 1; testm < 26; testm++) {
				for (testn = 32; testn < 2048 ; testn++) {
					computed = (pllreffreq * testn) / (testm * testp * testp2);
					if (computed > clock)
						tmpdelta = computed - clock;
					else
						tmpdelta = clock - computed;
					if (tmpdelta < delta) {
						delta = tmpdelta;
						m = testm + 1;
						n = testn + 1;
						p = testp + 1;
						s = testp2;
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

/*
 * DRM device
 */

static const struct mgag200_device_info mgag200_g200ew3_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 0, true, 0, 1, false);

static const struct mgag200_device_funcs mgag200_g200ew3_device_funcs = {
	.disable_vidrst = mgag200_bmc_disable_vidrst,
	.enable_vidrst = mgag200_bmc_enable_vidrst,
	.pixpllc_atomic_check = mgag200_g200ew3_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200wb_pixpllc_atomic_update, // same as G200WB
};

static resource_size_t mgag200_g200ew3_device_probe_vram(struct mga_device *mdev)
{
	resource_size_t vram_size = resource_size(mdev->vram_res);

	if (vram_size >= 0x1000000)
		vram_size = vram_size - 0x400000;
	return mgag200_probe_vram(mdev->vram, vram_size);
}

struct mga_device *mgag200_g200ew3_device_create(struct pci_dev *pdev,
						 const struct drm_driver *drv,
						 enum mga_type type)
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

	ret = mgag200_device_init(mdev, type, &mgag200_g200ew3_device_info,
				  &mgag200_g200ew3_device_funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200ew3_init_registers(mdev);

	vram_available = mgag200_g200ew3_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
