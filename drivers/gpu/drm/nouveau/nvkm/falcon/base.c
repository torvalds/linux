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

void
nvkm_falcon_load_imem(struct nvkm_falcon *falcon, void *data, u32 start,
		      u32 size, u16 tag, u8 port, bool secure)
{
	if (secure && !falcon->secret) {
		nvkm_warn(falcon->user,
			  "writing with secure tag on a non-secure falcon!\n");
		return;
	}

	falcon->func->load_imem(falcon, data, start, size, tag, port,
				secure);
}

void
nvkm_falcon_load_dmem(struct nvkm_falcon *falcon, void *data, u32 start,
		      u32 size, u8 port)
{
	mutex_lock(&falcon->dmem_mutex);

	falcon->func->load_dmem(falcon, data, start, size, port);

	mutex_unlock(&falcon->dmem_mutex);
}

void
nvkm_falcon_read_dmem(struct nvkm_falcon *falcon, u32 start, u32 size, u8 port,
		      void *data)
{
	mutex_lock(&falcon->dmem_mutex);

	falcon->func->read_dmem(falcon, start, size, port, data);

	mutex_unlock(&falcon->dmem_mutex);
}

void
nvkm_falcon_bind_context(struct nvkm_falcon *falcon, struct nvkm_memory *inst)
{
	if (!falcon->func->bind_context) {
		nvkm_error(falcon->user,
			   "Context binding not supported on this falcon!\n");
		return;
	}

	falcon->func->bind_context(falcon, inst);
}

void
nvkm_falcon_set_start_addr(struct nvkm_falcon *falcon, u32 start_addr)
{
	falcon->func->set_start_addr(falcon, start_addr);
}

void
nvkm_falcon_start(struct nvkm_falcon *falcon)
{
	falcon->func->start(falcon);
}

int
nvkm_falcon_enable(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	enum nvkm_devidx id = falcon->owner->index;
	int ret;

	nvkm_mc_enable(device, id);
	ret = falcon->func->enable(falcon);
	if (ret) {
		nvkm_mc_disable(device, id);
		return ret;
	}

	return 0;
}

void
nvkm_falcon_disable(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	enum nvkm_devidx id = falcon->owner->index;

	/* already disabled, return or wait_idle will timeout */
	if (!nvkm_mc_enabled(device, id))
		return;

	falcon->func->disable(falcon);

	nvkm_mc_disable(device, id);
}

int
nvkm_falcon_reset(struct nvkm_falcon *falcon)
{
	nvkm_falcon_disable(falcon);
	return nvkm_falcon_enable(falcon);
}

int
nvkm_falcon_wait_for_halt(struct nvkm_falcon *falcon, u32 ms)
{
	return falcon->func->wait_for_halt(falcon, ms);
}

int
nvkm_falcon_clear_interrupt(struct nvkm_falcon *falcon, u32 mask)
{
	return falcon->func->clear_interrupt(falcon, mask);
}

void
nvkm_falcon_put(struct nvkm_falcon *falcon, const struct nvkm_subdev *user)
{
	if (unlikely(!falcon))
		return;

	mutex_lock(&falcon->mutex);
	if (falcon->user == user) {
		nvkm_debug(falcon->user, "released %s falcon\n", falcon->name);
		falcon->user = NULL;
	}
	mutex_unlock(&falcon->mutex);
}

int
nvkm_falcon_get(struct nvkm_falcon *falcon, const struct nvkm_subdev *user)
{
	mutex_lock(&falcon->mutex);
	if (falcon->user) {
		nvkm_error(user, "%s falcon already acquired by %s!\n",
			   falcon->name, nvkm_subdev_name[falcon->user->index]);
		mutex_unlock(&falcon->mutex);
		return -EBUSY;
	}

	nvkm_debug(user, "acquired %s falcon\n", falcon->name);
	falcon->user = user;
	mutex_unlock(&falcon->mutex);
	return 0;
}

void
nvkm_falcon_ctor(const struct nvkm_falcon_func *func,
		 struct nvkm_subdev *subdev, const char *name, u32 addr,
		 struct nvkm_falcon *falcon)
{
	u32 debug_reg;
	u32 reg;

	falcon->func = func;
	falcon->owner = subdev;
	falcon->name = name;
	falcon->addr = addr;
	mutex_init(&falcon->mutex);
	mutex_init(&falcon->dmem_mutex);

	reg = nvkm_falcon_rd32(falcon, 0x12c);
	falcon->version = reg & 0xf;
	falcon->secret = (reg >> 4) & 0x3;
	falcon->code.ports = (reg >> 8) & 0xf;
	falcon->data.ports = (reg >> 12) & 0xf;

	reg = nvkm_falcon_rd32(falcon, 0x108);
	falcon->code.limit = (reg & 0x1ff) << 8;
	falcon->data.limit = (reg & 0x3fe00) >> 1;

	switch (subdev->index) {
	case NVKM_ENGINE_GR:
		debug_reg = 0x0;
		break;
	case NVKM_SUBDEV_PMU:
		debug_reg = 0xc08;
		break;
	case NVKM_ENGINE_NVDEC:
		debug_reg = 0xd00;
		break;
	case NVKM_ENGINE_SEC2:
		debug_reg = 0x408;
		falcon->has_emem = true;
		break;
	default:
		nvkm_warn(subdev, "unsupported falcon %s!\n",
			  nvkm_subdev_name[subdev->index]);
		debug_reg = 0;
		break;
	}

	if (debug_reg) {
		u32 val = nvkm_falcon_rd32(falcon, debug_reg);
		falcon->debug = (val >> 20) & 0x1;
	}
}

void
nvkm_falcon_del(struct nvkm_falcon **pfalcon)
{
	if (*pfalcon) {
		kfree(*pfalcon);
		*pfalcon = NULL;
	}
}
