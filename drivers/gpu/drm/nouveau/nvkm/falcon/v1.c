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

#include <core/gpuobj.h>
#include <core/memory.h>
#include <subdev/timer.h>

static void
nvkm_falcon_v1_load_imem(struct nvkm_falcon *falcon, void *data, u32 start,
			 u32 size, u16 tag, u8 port, bool secure)
{
	u8 rem = size % 4;
	u32 reg;
	int i;

	size -= rem;

	reg = start | BIT(24) | (secure ? BIT(28) : 0);
	nvkm_falcon_wr32(falcon, 0x180 + (port * 16), reg);
	for (i = 0; i < size / 4; i++) {
		/* write new tag every 256B */
		if ((i & 0x3f) == 0)
			nvkm_falcon_wr32(falcon, 0x188, tag++);
		nvkm_falcon_wr32(falcon, 0x184, ((u32 *)data)[i]);
	}

	/*
	 * If size is not a multiple of 4, mask the last work to ensure garbage
	 * does not get written
	 */
	if (rem) {
		u32 extra = ((u32 *)data)[i];

		/* write new tag every 256B */
		if ((i & 0x3f) == 0)
			nvkm_falcon_wr32(falcon, 0x188, tag++);
		nvkm_falcon_wr32(falcon, 0x184, extra & (BIT(rem * 8) - 1));
		++i;
	}

	/* code must be padded to 0x40 words */
	for (; i & 0x3f; i++)
		nvkm_falcon_wr32(falcon, 0x184, 0);
}

static void
nvkm_falcon_v1_load_dmem(struct nvkm_falcon *falcon, void *data, u32 start,
		      u32 size, u8 port)
{
	u8 rem = size % 4;
	int i;

	size -= rem;

	nvkm_falcon_wr32(falcon, 0x1c0 + (port * 16), start | (0x1 << 24));
	for (i = 0; i < size / 4; i++)
		nvkm_falcon_wr32(falcon, 0x1c4, ((u32 *)data)[i]);

	/*
	 * If size is not a multiple of 4, mask the last work to ensure garbage
	 * does not get read
	 */
	if (rem) {
		u32 extra = ((u32 *)data)[i];

		nvkm_falcon_wr32(falcon, 0x1c4, extra & (BIT(rem * 8) - 1));
	}
}

static void
nvkm_falcon_v1_read_dmem(struct nvkm_falcon *falcon, u32 start, u32 size,
			 u8 port, void *data)
{
	u8 rem = size % 4;
	int i;

	size -= rem;

	nvkm_falcon_wr32(falcon, 0x1c0 + (port * 16), start | (0x1 << 25));
	for (i = 0; i < size / 4; i++)
		((u32 *)data)[i] = nvkm_falcon_rd32(falcon, 0x1c4);

	/*
	 * If size is not a multiple of 4, mask the last work to ensure garbage
	 * does not get read
	 */
	if (rem) {
		u32 extra = nvkm_falcon_rd32(falcon, 0x1c4);

		for (i = size; i < size + rem; i++) {
			((u8 *)data)[i] = (u8)(extra & 0xff);
			extra >>= 8;
		}
	}
}

static void
nvkm_falcon_v1_bind_context(struct nvkm_falcon *falcon, struct nvkm_gpuobj *ctx)
{
	u32 inst_loc;

	/* disable instance block binding */
	if (ctx == NULL) {
		nvkm_falcon_wr32(falcon, 0x10c, 0x0);
		return;
	}

	nvkm_falcon_wr32(falcon, 0x10c, 0x1);

	/* setup apertures - virtual */
	nvkm_falcon_wr32(falcon, 0xe00 + 4 * FALCON_DMAIDX_UCODE, 0x4);
	nvkm_falcon_wr32(falcon, 0xe00 + 4 * FALCON_DMAIDX_VIRT, 0x0);
	/* setup apertures - physical */
	nvkm_falcon_wr32(falcon, 0xe00 + 4 * FALCON_DMAIDX_PHYS_VID, 0x4);
	nvkm_falcon_wr32(falcon, 0xe00 + 4 * FALCON_DMAIDX_PHYS_SYS_COH, 0x5);
	nvkm_falcon_wr32(falcon, 0xe00 + 4 * FALCON_DMAIDX_PHYS_SYS_NCOH, 0x6);

	/* Set context */
	if (nvkm_memory_target(ctx->memory) == NVKM_MEM_TARGET_VRAM)
		inst_loc = 0x0; /* FB */
	else
		inst_loc = 0x3; /* Non-coherent sysmem */

	/* Enable context */
	nvkm_falcon_mask(falcon, 0x048, 0x1, 0x1);
	nvkm_falcon_wr32(falcon, 0x480,
			 ((ctx->addr >> 12) & 0xfffffff) |
			 (inst_loc << 28) | (1 << 30));
}

static void
nvkm_falcon_v1_set_start_addr(struct nvkm_falcon *falcon, u32 start_addr)
{
	nvkm_falcon_wr32(falcon, 0x104, start_addr);
}

static void
nvkm_falcon_v1_start(struct nvkm_falcon *falcon)
{
	u32 reg = nvkm_falcon_rd32(falcon, 0x100);

	if (reg & BIT(6))
		nvkm_falcon_wr32(falcon, 0x130, 0x2);
	else
		nvkm_falcon_wr32(falcon, 0x100, 0x2);
}

static int
nvkm_falcon_v1_wait_for_halt(struct nvkm_falcon *falcon, u32 ms)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	ret = nvkm_wait_msec(device, ms, falcon->addr + 0x100, 0x10, 0x10);
	if (ret < 0)
		return ret;

	return 0;
}

static int
nvkm_falcon_v1_clear_interrupt(struct nvkm_falcon *falcon, u32 mask)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	/* clear interrupt(s) */
	nvkm_falcon_mask(falcon, 0x004, mask, mask);
	/* wait until interrupts are cleared */
	ret = nvkm_wait_msec(device, 10, falcon->addr + 0x008, mask, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static int
falcon_v1_wait_idle(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	ret = nvkm_wait_msec(device, 10, falcon->addr + 0x04c, 0xffff, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}

static int
nvkm_falcon_v1_enable(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	ret = nvkm_wait_msec(device, 10, falcon->addr + 0x10c, 0x6, 0x0);
	if (ret < 0) {
		nvkm_error(falcon->user, "Falcon mem scrubbing timeout\n");
		return ret;
	}

	ret = falcon_v1_wait_idle(falcon);
	if (ret)
		return ret;

	/* enable IRQs */
	nvkm_falcon_wr32(falcon, 0x010, 0xff);

	return 0;
}

static void
nvkm_falcon_v1_disable(struct nvkm_falcon *falcon)
{
	/* disable IRQs and wait for any previous code to complete */
	nvkm_falcon_wr32(falcon, 0x014, 0xff);
	falcon_v1_wait_idle(falcon);
}

static const struct nvkm_falcon_func
nvkm_falcon_v1 = {
	.load_imem = nvkm_falcon_v1_load_imem,
	.load_dmem = nvkm_falcon_v1_load_dmem,
	.read_dmem = nvkm_falcon_v1_read_dmem,
	.bind_context = nvkm_falcon_v1_bind_context,
	.start = nvkm_falcon_v1_start,
	.wait_for_halt = nvkm_falcon_v1_wait_for_halt,
	.clear_interrupt = nvkm_falcon_v1_clear_interrupt,
	.enable = nvkm_falcon_v1_enable,
	.disable = nvkm_falcon_v1_disable,
	.set_start_addr = nvkm_falcon_v1_set_start_addr,
};

int
nvkm_falcon_v1_new(struct nvkm_subdev *owner, const char *name, u32 addr,
		   struct nvkm_falcon **pfalcon)
{
	struct nvkm_falcon *falcon;
	if (!(falcon = *pfalcon = kzalloc(sizeof(*falcon), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_falcon_ctor(&nvkm_falcon_v1, owner, name, addr, falcon);
	return 0;
}
