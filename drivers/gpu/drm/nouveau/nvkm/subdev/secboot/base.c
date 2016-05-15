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
#include <subdev/timer.h>

static const char *
managed_falcons_names[] = {
	[NVKM_SECBOOT_FALCON_PMU] = "PMU",
	[NVKM_SECBOOT_FALCON_RESERVED] = "<reserved>",
	[NVKM_SECBOOT_FALCON_FECS] = "FECS",
	[NVKM_SECBOOT_FALCON_GPCCS] = "GPCCS",
	[NVKM_SECBOOT_FALCON_END] = "<invalid>",
};

/*
 * Helper falcon functions
 */

static int
falcon_clear_halt_interrupt(struct nvkm_device *device, u32 base)
{
	int ret;

	/* clear halt interrupt */
	nvkm_mask(device, base + 0x004, 0x10, 0x10);
	/* wait until halt interrupt is cleared */
	ret = nvkm_wait_msec(device, 10, base + 0x008, 0x10, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static int
falcon_wait_idle(struct nvkm_device *device, u32 base)
{
	int ret;

	ret = nvkm_wait_msec(device, 10, base + 0x04c, 0xffff, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static int
nvkm_secboot_falcon_enable(struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;
	int ret;

	/* enable engine */
	nvkm_mask(device, 0x200, sb->enable_mask, sb->enable_mask);
	nvkm_rd32(device, 0x200);
	ret = nvkm_wait_msec(device, 10, sb->base + 0x10c, 0x6, 0x0);
	if (ret < 0) {
		nvkm_mask(device, 0x200, sb->enable_mask, 0x0);
		nvkm_error(&sb->subdev, "Falcon mem scrubbing timeout\n");
		return ret;
	}

	ret = falcon_wait_idle(device, sb->base);
	if (ret)
		return ret;

	/* enable IRQs */
	nvkm_wr32(device, sb->base + 0x010, 0xff);
	nvkm_mask(device, 0x640, sb->irq_mask, sb->irq_mask);
	nvkm_mask(device, 0x644, sb->irq_mask, sb->irq_mask);

	return 0;
}

static int
nvkm_secboot_falcon_disable(struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;

	/* disable IRQs and wait for any previous code to complete */
	nvkm_mask(device, 0x644, sb->irq_mask, 0x0);
	nvkm_mask(device, 0x640, sb->irq_mask, 0x0);
	nvkm_wr32(device, sb->base + 0x014, 0xff);

	falcon_wait_idle(device, sb->base);

	/* disable engine */
	nvkm_mask(device, 0x200, sb->enable_mask, 0x0);

	return 0;
}

int
nvkm_secboot_falcon_reset(struct nvkm_secboot *sb)
{
	int ret;

	ret = nvkm_secboot_falcon_disable(sb);
	if (ret)
		return ret;

	ret = nvkm_secboot_falcon_enable(sb);
	if (ret)
		return ret;

	return 0;
}

/**
 * nvkm_secboot_falcon_run - run the falcon that will perform secure boot
 *
 * This function is to be called after all chip-specific preparations have
 * been completed. It will start the falcon to perform secure boot, wait for
 * it to halt, and report if an error occurred.
 */
int
nvkm_secboot_falcon_run(struct nvkm_secboot *sb)
{
	struct nvkm_device *device = sb->subdev.device;
	int ret;

	/* Start falcon */
	nvkm_wr32(device, sb->base + 0x100, 0x2);

	/* Wait for falcon halt */
	ret = nvkm_wait_msec(device, 100, sb->base + 0x100, 0x10, 0x10);
	if (ret < 0)
		return ret;

	/* If mailbox register contains an error code, then ACR has failed */
	ret = nvkm_rd32(device, sb->base + 0x040);
	if (ret) {
		nvkm_error(&sb->subdev, "ACR boot failed, ret 0x%08x", ret);
		falcon_clear_halt_interrupt(device, sb->base);
		return -EINVAL;
	}

	return 0;
}


/**
 * nvkm_secboot_reset() - reset specified falcon
 */
int
nvkm_secboot_reset(struct nvkm_secboot *sb, u32 falcon)
{
	/* Unmanaged falcon? */
	if (!(BIT(falcon) & sb->func->managed_falcons)) {
		nvkm_error(&sb->subdev, "cannot reset unmanaged falcon!\n");
		return -EINVAL;
	}

	return sb->func->reset(sb, falcon);
}

/**
 * nvkm_secboot_start() - start specified falcon
 */
int
nvkm_secboot_start(struct nvkm_secboot *sb, u32 falcon)
{
	/* Unmanaged falcon? */
	if (!(BIT(falcon) & sb->func->managed_falcons)) {
		nvkm_error(&sb->subdev, "cannot start unmanaged falcon!\n");
		return -EINVAL;
	}

	return sb->func->start(sb, falcon);
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

	/* Call chip-specific init function */
	if (sb->func->init)
		ret = sb->func->init(sb);
	if (ret) {
		nvkm_error(subdev, "Secure Boot initialization failed: %d\n",
			   ret);
		return ret;
	}

	/*
	 * Build all blobs - the same blobs can be used to perform secure boot
	 * multiple times
	 */
	if (sb->func->prepare_blobs)
		ret = sb->func->prepare_blobs(sb);

	return ret;
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

	nvkm_subdev_ctor(&nvkm_secboot, device, index, 0, &sb->subdev);
	sb->func = func;

	/* setup the performing falcon's base address and masks */
	switch (func->boot_falcon) {
	case NVKM_SECBOOT_FALCON_PMU:
		sb->base = 0x10a000;
		sb->irq_mask = 0x1000000;
		sb->enable_mask = 0x2000;
		break;
	default:
		nvkm_error(&sb->subdev, "invalid secure boot falcon\n");
		return -EINVAL;
	};

	nvkm_debug(&sb->subdev, "securely managed falcons:\n");
	for_each_set_bit(fid, &sb->func->managed_falcons,
			 NVKM_SECBOOT_FALCON_END)
		nvkm_debug(&sb->subdev, "- %s\n", managed_falcons_names[fid]);

	return 0;
}
