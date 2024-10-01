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
 * Authors: Karol Herbst <git@karolherbst.de>
 */
#include "priv.h"

static char *nvkm_pcie_speeds[] = {
	"2.5GT/s",
	"5.0GT/s",
	"8.0GT/s",
};

static enum nvkm_pcie_speed
nvkm_pcie_speed(enum pci_bus_speed speed)
{
	switch (speed) {
	case PCIE_SPEED_2_5GT:
		return NVKM_PCIE_SPEED_2_5;
	case PCIE_SPEED_5_0GT:
		return NVKM_PCIE_SPEED_5_0;
	case PCIE_SPEED_8_0GT:
		return NVKM_PCIE_SPEED_8_0;
	default:
		/* XXX 0x16 is 8_0, assume 0x17 will be 16_0 for now */
		if (speed == 0x17)
			return NVKM_PCIE_SPEED_8_0;
		return -1;
	}
}

static int
nvkm_pcie_get_version(struct nvkm_pci *pci)
{
	if (!pci->func->pcie.version)
		return -ENOSYS;

	return pci->func->pcie.version(pci);
}

static int
nvkm_pcie_get_max_version(struct nvkm_pci *pci)
{
	if (!pci->func->pcie.version_supported)
		return -ENOSYS;

	return pci->func->pcie.version_supported(pci);
}

static int
nvkm_pcie_set_version(struct nvkm_pci *pci, int version)
{
	if (!pci->func->pcie.set_version)
		return -ENOSYS;

	nvkm_trace(&pci->subdev, "set to version %i\n", version);
	pci->func->pcie.set_version(pci, version);
	return nvkm_pcie_get_version(pci);
}

int
nvkm_pcie_oneinit(struct nvkm_pci *pci)
{
	if (pci->func->pcie.max_speed)
		nvkm_debug(&pci->subdev, "pcie max speed: %s\n",
			   nvkm_pcie_speeds[pci->func->pcie.max_speed(pci)]);
	return 0;
}

int
nvkm_pcie_init(struct nvkm_pci *pci)
{
	struct nvkm_subdev *subdev = &pci->subdev;
	int ret;

	/* raise pcie version first */
	ret = nvkm_pcie_get_version(pci);
	if (ret > 0) {
		int max_version = nvkm_pcie_get_max_version(pci);
		if (max_version > 0 && max_version > ret)
			ret = nvkm_pcie_set_version(pci, max_version);

		if (ret < max_version)
			nvkm_error(subdev, "couldn't raise version: %i\n", ret);
	}

	if (pci->func->pcie.init)
		pci->func->pcie.init(pci);

	if (pci->pcie.speed != -1)
		nvkm_pcie_set_link(pci, pci->pcie.speed, pci->pcie.width);

	return 0;
}

int
nvkm_pcie_set_link(struct nvkm_pci *pci, enum nvkm_pcie_speed speed, u8 width)
{
	struct nvkm_subdev *subdev = &pci->subdev;
	enum nvkm_pcie_speed cur_speed, max_speed;
	struct pci_bus *pbus;
	int ret;

	if (!pci || !pci_is_pcie(pci->pdev))
		return 0;
	pbus = pci->pdev->bus;

	if (!pci->func->pcie.set_link)
		return -ENOSYS;

	nvkm_trace(subdev, "requested %s\n", nvkm_pcie_speeds[speed]);

	if (pci->func->pcie.version(pci) < 2) {
		nvkm_error(subdev, "setting link failed due to low version\n");
		return -ENODEV;
	}

	cur_speed = pci->func->pcie.cur_speed(pci);
	max_speed = min(nvkm_pcie_speed(pbus->max_bus_speed),
			pci->func->pcie.max_speed(pci));

	nvkm_trace(subdev, "current speed: %s\n", nvkm_pcie_speeds[cur_speed]);

	if (speed > max_speed) {
		nvkm_debug(subdev, "%s not supported by bus or card, dropping"
			   "requested speed to %s", nvkm_pcie_speeds[speed],
			   nvkm_pcie_speeds[max_speed]);
		speed = max_speed;
	}

	pci->pcie.speed = speed;
	pci->pcie.width = width;

	if (speed == cur_speed) {
		nvkm_debug(subdev, "requested matches current speed\n");
		return speed;
	}

	nvkm_debug(subdev, "set link to %s x%i\n",
		   nvkm_pcie_speeds[speed], width);

	ret = pci->func->pcie.set_link(pci, speed, width);
	if (ret < 0)
		nvkm_error(subdev, "setting link failed: %i\n", ret);

	return ret;
}
