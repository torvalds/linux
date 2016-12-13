/*
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <subdev/mc.h>
#include <subdev/timer.h>
#include <subdev/pmu.h>

static const char *
managed_falcons_names[] = {
	[NVKM_SECBOOT_FALCON_PMU] = "PMU",
	[NVKM_SECBOOT_FALCON_RESERVED] = "<reserved>",
	[NVKM_SECBOOT_FALCON_FECS] = "FECS",
	[NVKM_SECBOOT_FALCON_GPCCS] = "GPCCS",
	[NVKM_SECBOOT_FALCON_END] = "<invalid>",
};
/**
 * nvkm_secboot_reset() - reset specified falcon
 */
int
nvkm_secboot_reset(struct nvkm_secboot *sb, enum nvkm_secboot_falcon falcon)
{
	/* Unmanaged falcon? */
	if (!(BIT(falcon) & sb->func->managed_falcons)) {
		nvkm_error(&sb->subdev, "cannot reset unmanaged falcon!\n");
		return -EINVAL;
	}

	return sb->func->reset(sb, falcon);
}

/**
 * nvkm_secboot_is_managed() - check whether a given falcon is securely-managed
 */
bool
nvkm_secboot_is_managed(struct nvkm_secboot *secboot,
			enum nvkm_secboot_falcon fid)
{
	if (!secboot)
		return false;

	return secboot->func->managed_falcons & BIT(fid);
}

static int
nvkm_secboot_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	int ret = 0;

	switch (sb->func->boot_falcon) {
	case NVKM_SECBOOT_FALCON_PMU:
		sb->boot_falcon = subdev->device->pmu->falcon;
		break;
	default:
		nvkm_error(subdev, "Unmanaged boot falcon %s!\n",
			   managed_falcons_names[sb->func->boot_falcon]);
		return -EINVAL;
	}

	/* Call chip-specific init function */
	if (sb->func->init)
		ret = sb->func->init(sb);
	if (ret) {
		nvkm_error(subdev, "Secure Boot initialization failed: %d\n",
			   ret);
		return ret;
	}

	return 0;
}

static int
nvkm_secboot_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	int ret = 0;

	if (sb->func->fini)
		ret = sb->func->fini(sb, suspend);

	return ret;
}

static void *
nvkm_secboot_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_secboot *sb = nvkm_secboot(subdev);
	void *ret = NULL;

	if (sb->func->dtor)
		ret = sb->func->dtor(sb);

	return ret;
}

static const struct nvkm_subdev_func
nvkm_secboot = {
	.oneinit = nvkm_secboot_oneinit,
	.fini = nvkm_secboot_fini,
	.dtor = nvkm_secboot_dtor,
};

int
nvkm_secboot_ctor(const struct nvkm_secboot_func *func,
		  struct nvkm_device *device, int index,
		  struct nvkm_secboot *sb)
{
	unsigned long fid;

	nvkm_subdev_ctor(&nvkm_secboot, device, index, &sb->subdev);
	sb->func = func;

	nvkm_debug(&sb->subdev, "securely managed falcons:\n");
	for_each_set_bit(fid, &sb->func->managed_falcons,
			 NVKM_SECBOOT_FALCON_END)
		nvkm_debug(&sb->subdev, "- %s\n", managed_falcons_names[fid]);

	return 0;
}
