/*
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Dave Airlie
 *    Jerome Glisse <glisse@freedesktop.org>
 */
#include <drm/drmP.h>
#include "radeon.h"
#include <drm/radeon_drm.h>

#if __OS_HAS_AGP

struct radeon_agpmode_quirk {
	u32 hostbridge_vendor;
	u32 hostbridge_device;
	u32 chip_vendor;
	u32 chip_device;
	u32 subsys_vendor;
	u32 subsys_device;
	u32 default_mode;
};

static struct radeon_agpmode_quirk radeon_agpmode_quirk_list[] = {
	/* Intel E7505 Memory Controller Hub / RV350 AR [Radeon 9600XT] Needs AGPMode 4 (deb #515326) */
	{ PCI_VENDOR_ID_INTEL, 0x2550, PCI_VENDOR_ID_ATI, 0x4152, 0x1458, 0x4038, 4},
	/* Intel 82865G/PE/P DRAM Controller/Host-Hub / Mobility 9800 Needs AGPMode 4 (deb #462590) */
	{ PCI_VENDOR_ID_INTEL, 0x2570, PCI_VENDOR_ID_ATI, 0x4a4e, PCI_VENDOR_ID_DELL, 0x5106, 4},
	/* Intel 82865G/PE/P DRAM Controller/Host-Hub / RV280 [Radeon 9200 SE] Needs AGPMode 4 (lp #300304) */
	{ PCI_VENDOR_ID_INTEL, 0x2570, PCI_VENDOR_ID_ATI, 0x5964,
		0x148c, 0x2073, 4},
	/* Intel 82855PM Processor to I/O Controller / Mobility M6 LY Needs AGPMode 1 (deb #467235) */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x4c59,
		PCI_VENDOR_ID_IBM, 0x052f, 1},
	/* Intel 82855PM host bridge / Mobility 9600 M10 RV350 Needs AGPMode 1 (lp #195051) */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x4e50,
		PCI_VENDOR_ID_IBM, 0x0550, 1},
	/* Intel 82855PM host bridge / Mobility M7 needs AGPMode 1 */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x4c57,
		PCI_VENDOR_ID_IBM, 0x0530, 1},
	/* Intel 82855PM host bridge / FireGL Mobility T2 RV350 Needs AGPMode 2 (fdo #20647) */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x4e54,
		PCI_VENDOR_ID_IBM, 0x054f, 2},
	/* Intel 82855PM host bridge / Mobility M9+ / VaioPCG-V505DX Needs AGPMode 2 (fdo #17928) */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x5c61,
		PCI_VENDOR_ID_SONY, 0x816b, 2},
	/* Intel 82855PM Processor to I/O Controller / Mobility M9+ Needs AGPMode 8 (phoronix forum) */
	{ PCI_VENDOR_ID_INTEL, 0x3340, PCI_VENDOR_ID_ATI, 0x5c61,
		PCI_VENDOR_ID_SONY, 0x8195, 8},
	/* Intel 82830 830 Chipset Host Bridge / Mobility M6 LY Needs AGPMode 2 (fdo #17360)*/
	{ PCI_VENDOR_ID_INTEL, 0x3575, PCI_VENDOR_ID_ATI, 0x4c59,
		PCI_VENDOR_ID_DELL, 0x00e3, 2},
	/* Intel 82852/82855 host bridge / Mobility FireGL 9000 R250 Needs AGPMode 1 (lp #296617) */
	{ PCI_VENDOR_ID_INTEL, 0x3580, PCI_VENDOR_ID_ATI, 0x4c66,
		PCI_VENDOR_ID_DELL, 0x0149, 1},
	/* Intel 82852/82855 host bridge / Mobility 9600 M10 RV350 Needs AGPMode 1 (deb #467460) */
	{ PCI_VENDOR_ID_INTEL, 0x3580, PCI_VENDOR_ID_ATI, 0x4e50,
		0x1025, 0x0061, 1},
	/* Intel 82852/82855 host bridge / Mobility 9600 M10 RV350 Needs AGPMode 1 (lp #203007) */
	{ PCI_VENDOR_ID_INTEL, 0x3580, PCI_VENDOR_ID_ATI, 0x4e50,
		0x1025, 0x0064, 1},
	/* Intel 82852/82855 host bridge / Mobility 9600 M10 RV350 Needs AGPMode 1 (lp #141551) */
	{ PCI_VENDOR_ID_INTEL, 0x3580, PCI_VENDOR_ID_ATI, 0x4e50,
		PCI_VENDOR_ID_ASUSTEK, 0x1942, 1},
	/* Intel 82852/82855 host bridge / Mobility 9600/9700 Needs AGPMode 1 (deb #510208) */
	{ PCI_VENDOR_ID_INTEL, 0x3580, PCI_VENDOR_ID_ATI, 0x4e50,
		0x10cf, 0x127f, 1},
	/* ASRock K7VT4A+ AGP 8x / ATI Radeon 9250 AGP Needs AGPMode 4 (lp #133192) */
	{ 0x1849, 0x3189, PCI_VENDOR_ID_ATI, 0x5960,
		0x1787, 0x5960, 4},
	/* VIA K8M800 Host Bridge / RV280 [Radeon 9200 PRO] Needs AGPMode 4 (fdo #12544) */
	{ PCI_VENDOR_ID_VIA, 0x0204, PCI_VENDOR_ID_ATI, 0x5960,
		0x17af, 0x2020, 4},
	/* VIA KT880 Host Bridge / RV350 [Radeon 9550] Needs AGPMode 4 (fdo #19981) */
	{ PCI_VENDOR_ID_VIA, 0x0269, PCI_VENDOR_ID_ATI, 0x4153,
		PCI_VENDOR_ID_ASUSTEK, 0x003c, 4},
	/* VIA VT8363 Host Bridge / R200 QL [Radeon 8500] Needs AGPMode 2 (lp #141551) */
	{ PCI_VENDOR_ID_VIA, 0x0305, PCI_VENDOR_ID_ATI, 0x514c,
		PCI_VENDOR_ID_ATI, 0x013a, 2},
	/* VIA VT82C693A Host Bridge / RV280 [Radeon 9200 PRO] Needs AGPMode 2 (deb #515512) */
	{ PCI_VENDOR_ID_VIA, 0x0691, PCI_VENDOR_ID_ATI, 0x5960,
		PCI_VENDOR_ID_ASUSTEK, 0x004c, 2},
	/* VIA VT82C693A Host Bridge / RV280 [Radeon 9200 PRO] Needs AGPMode 2 */
	{ PCI_VENDOR_ID_VIA, 0x0691, PCI_VENDOR_ID_ATI, 0x5960,
		PCI_VENDOR_ID_ASUSTEK, 0x0054, 2},
	/* VIA VT8377 Host Bridge / R200 QM [Radeon 9100] Needs AGPMode 4 (deb #461144) */
	{ PCI_VENDOR_ID_VIA, 0x3189, PCI_VENDOR_ID_ATI, 0x514d,
		0x174b, 0x7149, 4},
	/* VIA VT8377 Host Bridge / RV280 [Radeon 9200 PRO] Needs AGPMode 4 (lp #312693) */
	{ PCI_VENDOR_ID_VIA, 0x3189, PCI_VENDOR_ID_ATI, 0x5960,
		0x1462, 0x0380, 4},
	/* VIA VT8377 Host Bridge / RV280 Needs AGPMode 4 (ati ML) */
	{ PCI_VENDOR_ID_VIA, 0x3189, PCI_VENDOR_ID_ATI, 0x5964,
		0x148c, 0x2073, 4},
	/* ATI Host Bridge / RV280 [M9+] Needs AGPMode 1 (phoronix forum) */
	{ PCI_VENDOR_ID_ATI, 0xcbb2, PCI_VENDOR_ID_ATI, 0x5c61,
		PCI_VENDOR_ID_SONY, 0x8175, 1},
	/* HP Host Bridge / R300 [FireGL X1] Needs AGPMode 2 (fdo #7770) */
	{ PCI_VENDOR_ID_HP, 0x122e, PCI_VENDOR_ID_ATI, 0x4e47,
		PCI_VENDOR_ID_ATI, 0x0152, 2},
	{ 0, 0, 0, 0, 0, 0, 0 },
};
#endif

int radeon_agp_init(struct radeon_device *rdev)
{
#if __OS_HAS_AGP
	struct radeon_agpmode_quirk *p = radeon_agpmode_quirk_list;
	struct drm_agp_mode mode;
	struct drm_agp_info info;
	uint32_t agp_status;
	int default_mode;
	bool is_v3;
	int ret;

	/* Acquire AGP. */
	ret = drm_agp_acquire(rdev->ddev);
	if (ret) {
		DRM_ERROR("Unable to acquire AGP: %d\n", ret);
		return ret;
	}

	ret = drm_agp_info(rdev->ddev, &info);
	if (ret) {
		drm_agp_release(rdev->ddev);
		DRM_ERROR("Unable to get AGP info: %d\n", ret);
		return ret;
	}

	if (rdev->ddev->agp->agp_info.aper_size < 32) {
		drm_agp_release(rdev->ddev);
		dev_warn(rdev->dev, "AGP aperture too small (%zuM) "
			"need at least 32M, disabling AGP\n",
			rdev->ddev->agp->agp_info.aper_size);
		return -EINVAL;
	}

	mode.mode = info.mode;
	/* chips with the agp to pcie bridge don't have the AGP_STATUS register
	 * Just use the whatever mode the host sets up.
	 */
	if (rdev->family <= CHIP_RV350)
		agp_status = (RREG32(RADEON_AGP_STATUS) | RADEON_AGPv3_MODE) & mode.mode;
	else
		agp_status = mode.mode;
	is_v3 = !!(agp_status & RADEON_AGPv3_MODE);

	if (is_v3) {
		default_mode = (agp_status & RADEON_AGPv3_8X_MODE) ? 8 : 4;
	} else {
		if (agp_status & RADEON_AGP_4X_MODE) {
			default_mode = 4;
		} else if (agp_status & RADEON_AGP_2X_MODE) {
			default_mode = 2;
		} else {
			default_mode = 1;
		}
	}

	/* Apply AGPMode Quirks */
	while (p && p->chip_device != 0) {
		if (info.id_vendor == p->hostbridge_vendor &&
		    info.id_device == p->hostbridge_device &&
		    rdev->pdev->vendor == p->chip_vendor &&
		    rdev->pdev->device == p->chip_device &&
		    rdev->pdev->subsystem_vendor == p->subsys_vendor &&
		    rdev->pdev->subsystem_device == p->subsys_device) {
			default_mode = p->default_mode;
		}
		++p;
	}

	if (radeon_agpmode > 0) {
		if ((radeon_agpmode < (is_v3 ? 4 : 1)) ||
		    (radeon_agpmode > (is_v3 ? 8 : 4)) ||
		    (radeon_agpmode & (radeon_agpmode - 1))) {
			DRM_ERROR("Illegal AGP Mode: %d (valid %s), leaving at %d\n",
				  radeon_agpmode, is_v3 ? "4, 8" : "1, 2, 4",
				  default_mode);
			radeon_agpmode = default_mode;
		} else {
			DRM_INFO("AGP mode requested: %d\n", radeon_agpmode);
		}
	} else {
		radeon_agpmode = default_mode;
	}

	mode.mode &= ~RADEON_AGP_MODE_MASK;
	if (is_v3) {
		switch (radeon_agpmode) {
		case 8:
			mode.mode |= RADEON_AGPv3_8X_MODE;
			break;
		case 4:
		default:
			mode.mode |= RADEON_AGPv3_4X_MODE;
			break;
		}
	} else {
		switch (radeon_agpmode) {
		case 4:
			mode.mode |= RADEON_AGP_4X_MODE;
			break;
		case 2:
			mode.mode |= RADEON_AGP_2X_MODE;
			break;
		case 1:
		default:
			mode.mode |= RADEON_AGP_1X_MODE;
			break;
		}
	}

	mode.mode &= ~RADEON_AGP_FW_MODE; /* disable fw */
	ret = drm_agp_enable(rdev->ddev, mode);
	if (ret) {
		DRM_ERROR("Unable to enable AGP (mode = 0x%lx)\n", mode.mode);
		drm_agp_release(rdev->ddev);
		return ret;
	}

	rdev->mc.agp_base = rdev->ddev->agp->agp_info.aper_base;
	rdev->mc.gtt_size = rdev->ddev->agp->agp_info.aper_size << 20;
	rdev->mc.gtt_start = rdev->mc.agp_base;
	rdev->mc.gtt_end = rdev->mc.gtt_start + rdev->mc.gtt_size - 1;
	dev_info(rdev->dev, "GTT: %lluM 0x%08llX - 0x%08llX\n",
		rdev->mc.gtt_size >> 20, rdev->mc.gtt_start, rdev->mc.gtt_end);

	/* workaround some hw issues */
	if (rdev->family < CHIP_R200) {
		WREG32(RADEON_AGP_CNTL, RREG32(RADEON_AGP_CNTL) | 0x000e0000);
	}
	return 0;
#else
	return 0;
#endif
}

void radeon_agp_resume(struct radeon_device *rdev)
{
#if __OS_HAS_AGP
	int r;
	if (rdev->flags & RADEON_IS_AGP) {
		r = radeon_agp_init(rdev);
		if (r)
			dev_warn(rdev->dev, "radeon AGP reinit failed\n");
	}
#endif
}

void radeon_agp_fini(struct radeon_device *rdev)
{
#if __OS_HAS_AGP
	if (rdev->ddev->agp && rdev->ddev->agp->acquired) {
		drm_agp_release(rdev->ddev);
	}
#endif
}

void radeon_agp_suspend(struct radeon_device *rdev)
{
	radeon_agp_fini(rdev);
}
