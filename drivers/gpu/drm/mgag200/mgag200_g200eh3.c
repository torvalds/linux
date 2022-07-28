// SPDX-License-Identifier: GPL-2.0-only

#include <linux/pci.h>

#include <drm/drm_atomic.h>
#include <drm/drm_drv.h>

#include "mgag200_drv.h"

/*
 * PIXPLLC
 */

static int mgag200_g200eh3_pixpllc_atomic_check(struct drm_crtc *crtc,
						struct drm_atomic_state *new_state)
{
	static const unsigned int vcomax = 3000000;
	static const unsigned int vcomin = 1500000;
	static const unsigned int pllreffreq = 25000;

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
	testp = 0;

	for (testm = 150; testm >= 6; testm--) {
		if (clock * testm > vcomax)
			continue;
		if (clock * testm < vcomin)
			continue;
		for (testn = 120; testn >= 60; testn--) {
			computed = (pllreffreq * testn) / testm;
			if (computed > clock)
				tmpdelta = computed - clock;
			else
				tmpdelta = clock - computed;
			if (tmpdelta < delta) {
				delta = tmpdelta;
				n = testn + 1;
				m = testm + 1;
				p = testp + 1;
			}
			if (delta == 0)
				break;
		}
		if (delta == 0)
			break;
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

static const struct mgag200_device_info mgag200_g200eh3_device_info =
	MGAG200_DEVICE_INFO_INIT(2048, 2048, 0, false, 1, 0, false);

static const struct mgag200_device_funcs mgag200_g200eh3_device_funcs = {
	.pixpllc_atomic_check = mgag200_g200eh3_pixpllc_atomic_check,
	.pixpllc_atomic_update = mgag200_g200eh_pixpllc_atomic_update, // same as G200EH
};

struct mga_device *mgag200_g200eh3_device_create(struct pci_dev *pdev,
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

	ret = mgag200_init_pci_options(pdev, 0x00000120, 0x0000b000);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_preinit(mdev);
	if (ret)
		return ERR_PTR(ret);

	ret = mgag200_device_init(mdev, type, &mgag200_g200eh3_device_info,
				  &mgag200_g200eh3_device_funcs);
	if (ret)
		return ERR_PTR(ret);

	mgag200_g200eh_init_registers(mdev); // same as G200EH

	vram_available = mgag200_device_probe_vram(mdev);

	ret = mgag200_modeset_init(mdev, vram_available);
	if (ret)
		return ERR_PTR(ret);

	return mdev;
}
