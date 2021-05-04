/*
 * Copyright 2015 Karol Herbst <nouveau@karolherbst.de>
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
 * Authors: Karol Herbst <nouveau@karolherbst.de>
 */
#include "priv.h"

static int
gk104_pcie_version_supported(struct nvkm_pci *pci)
{
	return (nvkm_rd32(pci->subdev.device, 0x8c1c0) & 0x4) == 0x4 ? 2 : 1;
}

static void
gk104_pcie_set_cap_speed(struct nvkm_pci *pci, enum nvkm_pcie_speed speed)
{
	struct nvkm_device *device = pci->subdev.device;

	switch (speed) {
	case NVKM_PCIE_SPEED_2_5:
		gf100_pcie_set_cap_speed(pci, false);
		nvkm_mask(device, 0x8c1c0, 0x30000, 0x10000);
		break;
	case NVKM_PCIE_SPEED_5_0:
		gf100_pcie_set_cap_speed(pci, true);
		nvkm_mask(device, 0x8c1c0, 0x30000, 0x20000);
		break;
	case NVKM_PCIE_SPEED_8_0:
		gf100_pcie_set_cap_speed(pci, true);
		nvkm_mask(device, 0x8c1c0, 0x30000, 0x30000);
		break;
	}
}

static enum nvkm_pcie_speed
gk104_pcie_cap_speed(struct nvkm_pci *pci)
{
	int speed = gf100_pcie_cap_speed(pci);

	if (speed == 0)
		return NVKM_PCIE_SPEED_2_5;

	if (speed >= 1) {
		int speed2 = nvkm_rd32(pci->subdev.device, 0x8c1c0) & 0x30000;
		switch (speed2) {
		case 0x00000:
		case 0x10000:
			return NVKM_PCIE_SPEED_2_5;
		case 0x20000:
			return NVKM_PCIE_SPEED_5_0;
		case 0x30000:
			return NVKM_PCIE_SPEED_8_0;
		}
	}

	return -EINVAL;
}

static void
gk104_pcie_set_lnkctl_speed(struct nvkm_pci *pci, enum nvkm_pcie_speed speed)
{
	u8 reg_v = 0;
	switch (speed) {
	case NVKM_PCIE_SPEED_2_5:
		reg_v = 1;
		break;
	case NVKM_PCIE_SPEED_5_0:
		reg_v = 2;
		break;
	case NVKM_PCIE_SPEED_8_0:
		reg_v = 3;
		break;
	}
	nvkm_pci_mask(pci, 0xa8, 0x3, reg_v);
}

static enum nvkm_pcie_speed
gk104_pcie_lnkctl_speed(struct nvkm_pci *pci)
{
	u8 reg_v = nvkm_pci_rd32(pci, 0xa8) & 0x3;
	switch (reg_v) {
	case 0:
	case 1:
		return NVKM_PCIE_SPEED_2_5;
	case 2:
		return NVKM_PCIE_SPEED_5_0;
	case 3:
		return NVKM_PCIE_SPEED_8_0;
	}
	return -1;
}

static enum nvkm_pcie_speed
gk104_pcie_max_speed(struct nvkm_pci *pci)
{
	u32 max_speed = nvkm_rd32(pci->subdev.device, 0x8c1c0) & 0x300000;
	switch (max_speed) {
	case 0x000000:
		return NVKM_PCIE_SPEED_8_0;
	case 0x100000:
		return NVKM_PCIE_SPEED_5_0;
	case 0x200000:
		return NVKM_PCIE_SPEED_2_5;
	}
	return NVKM_PCIE_SPEED_2_5;
}

static void
gk104_pcie_set_link_speed(struct nvkm_pci *pci, enum nvkm_pcie_speed speed)
{
	struct nvkm_device *device = pci->subdev.device;
	u32 mask_value;

	switch (speed) {
	case NVKM_PCIE_SPEED_8_0:
		mask_value = 0x00000;
		break;
	case NVKM_PCIE_SPEED_5_0:
		mask_value = 0x40000;
		break;
	case NVKM_PCIE_SPEED_2_5:
	default:
		mask_value = 0x80000;
		break;
	}

	nvkm_mask(device, 0x8c040, 0xc0000, mask_value);
	nvkm_mask(device, 0x8c040, 0x1, 0x1);
}

static int
gk104_pcie_init(struct nvkm_pci * pci)
{
	enum nvkm_pcie_speed lnkctl_speed, max_speed, cap_speed;
	struct nvkm_subdev *subdev = &pci->subdev;

	if (gf100_pcie_version(pci) < 2)
		return 0;

	lnkctl_speed = gk104_pcie_lnkctl_speed(pci);
	max_speed = gk104_pcie_max_speed(pci);
	cap_speed = gk104_pcie_cap_speed(pci);

	if (cap_speed != max_speed) {
		nvkm_trace(subdev, "adjusting cap to max speed\n");
		gk104_pcie_set_cap_speed(pci, max_speed);
		cap_speed = gk104_pcie_cap_speed(pci);
		if (cap_speed != max_speed)
			nvkm_warn(subdev, "failed to adjust cap speed\n");
	}

	if (lnkctl_speed != max_speed) {
		nvkm_debug(subdev, "adjusting lnkctl to max speed\n");
		gk104_pcie_set_lnkctl_speed(pci, max_speed);
		lnkctl_speed = gk104_pcie_lnkctl_speed(pci);
		if (lnkctl_speed != max_speed)
			nvkm_error(subdev, "failed to adjust lnkctl speed\n");
	}

	return 0;
}

static int
gk104_pcie_set_link(struct nvkm_pci *pci, enum nvkm_pcie_speed speed, u8 width)
{
	struct nvkm_subdev *subdev = &pci->subdev;
	enum nvkm_pcie_speed lnk_ctl_speed = gk104_pcie_lnkctl_speed(pci);
	enum nvkm_pcie_speed lnk_cap_speed = gk104_pcie_cap_speed(pci);

	if (speed > lnk_cap_speed) {
		speed = lnk_cap_speed;
		nvkm_warn(subdev, "dropping requested speed due too low cap"
			  " speed\n");
	}

	if (speed > lnk_ctl_speed) {
		speed = lnk_ctl_speed;
		nvkm_warn(subdev, "dropping requested speed due too low"
			  " lnkctl speed\n");
	}

	gk104_pcie_set_link_speed(pci, speed);
	return 0;
}


static const struct nvkm_pci_func
gk104_pci_func = {
	.init = g84_pci_init,
	.rd32 = nv40_pci_rd32,
	.wr08 = nv40_pci_wr08,
	.wr32 = nv40_pci_wr32,
	.msi_rearm = nv40_pci_msi_rearm,

	.pcie.init = gk104_pcie_init,
	.pcie.set_link = gk104_pcie_set_link,

	.pcie.max_speed = gk104_pcie_max_speed,
	.pcie.cur_speed = g84_pcie_cur_speed,

	.pcie.set_version = gf100_pcie_set_version,
	.pcie.version = gf100_pcie_version,
	.pcie.version_supported = gk104_pcie_version_supported,
};

int
gk104_pci_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pci **ppci)
{
	return nvkm_pci_new_(&gk104_pci_func, device, type, inst, ppci);
}
