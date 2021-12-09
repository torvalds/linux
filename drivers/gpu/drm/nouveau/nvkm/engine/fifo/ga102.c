/*
 * Copyright 2021 Red Hat Inc.
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
 */
#define ga102_fifo(p) container_of((p), struct ga102_fifo, base.engine)
#define ga102_chan(p) container_of((p), struct ga102_chan, object)
#include <engine/fifo.h>
#include "user.h"

#include <core/memory.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>
#include <subdev/top.h>

#include <nvif/cl0080.h>
#include <nvif/clc36f.h>
#include <nvif/class.h>

struct ga102_fifo {
	struct nvkm_fifo base;
};

struct ga102_chan {
	struct nvkm_object object;

	struct {
		u32 runl;
		u32 chan;
	} ctrl;

	struct nvkm_memory *mthd;
	struct nvkm_memory *inst;
	struct nvkm_memory *user;
	struct nvkm_memory *runl;

	struct nvkm_vmm *vmm;
};

static int
ga102_chan_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *oclass)
{
	if (index == 0) {
		oclass->ctor = nvkm_object_new;
		oclass->base = (struct nvkm_sclass) { -1, -1, AMPERE_DMA_COPY_B };
		return 0;
	}

	return -EINVAL;
}

static int
ga102_chan_map(struct nvkm_object *object, void *argv, u32 argc,
	       enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct ga102_chan *chan = ga102_chan(object);
	struct nvkm_device *device = chan->object.engine->subdev.device;
	u64 bar2 = nvkm_memory_bar2(chan->user);

	if (bar2 == ~0ULL)
		return -EFAULT;

	*type = NVKM_OBJECT_MAP_IO;
	*addr = device->func->resource_addr(device, 3) + bar2;
	*size = 0x1000;
	return 0;
}

static int
ga102_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct ga102_chan *chan = ga102_chan(object);
	struct nvkm_device *device = chan->object.engine->subdev.device;

	nvkm_wr32(device, chan->ctrl.chan, 0x00000003);

	nvkm_wr32(device, chan->ctrl.runl + 0x098, 0x01000000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, chan->ctrl.runl + 0x098) & 0x00100000))
			break;
	);

	nvkm_wr32(device, chan->ctrl.runl + 0x088, 0);

	nvkm_wr32(device, chan->ctrl.chan, 0xffffffff);
	return 0;
}

static int
ga102_chan_init(struct nvkm_object *object)
{
	struct ga102_chan *chan = ga102_chan(object);
	struct nvkm_device *device = chan->object.engine->subdev.device;

	nvkm_mask(device, chan->ctrl.runl + 0x300, 0x80000000, 0x80000000);

	nvkm_wr32(device, chan->ctrl.runl + 0x080, lower_32_bits(nvkm_memory_addr(chan->runl)));
	nvkm_wr32(device, chan->ctrl.runl + 0x084, upper_32_bits(nvkm_memory_addr(chan->runl)));
	nvkm_wr32(device, chan->ctrl.runl + 0x088, 2);

	nvkm_wr32(device, chan->ctrl.chan, 0x00000002);
	nvkm_wr32(device, chan->ctrl.runl + 0x0090, 0);
	return 0;
}

static void *
ga102_chan_dtor(struct nvkm_object *object)
{
	struct ga102_chan *chan = ga102_chan(object);

	if (chan->vmm) {
		nvkm_vmm_part(chan->vmm, chan->inst);
		nvkm_vmm_unref(&chan->vmm);
	}

	nvkm_memory_unref(&chan->runl);
	nvkm_memory_unref(&chan->user);
	nvkm_memory_unref(&chan->inst);
	nvkm_memory_unref(&chan->mthd);
	return chan;
}

static const struct nvkm_object_func
ga102_chan = {
	.dtor = ga102_chan_dtor,
	.init = ga102_chan_init,
	.fini = ga102_chan_fini,
	.map = ga102_chan_map,
	.sclass = ga102_chan_sclass,
};

static int
ga102_chan_new(struct nvkm_device *device,
	       const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct volta_channel_gpfifo_a_v0 *args = argv;
	struct nvkm_top_device *tdev;
	struct nvkm_vmm *vmm;
	struct ga102_chan *chan;
	int ret;

	if (argc != sizeof(*args))
		return -ENOSYS;

	vmm = nvkm_uvmm_search(oclass->client, args->vmm);
	if (IS_ERR(vmm))
		return PTR_ERR(vmm);

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&ga102_chan, oclass, &chan->object);
	*pobject = &chan->object;

	list_for_each_entry(tdev, &device->top->device, head) {
		if (tdev->type == NVKM_ENGINE_CE) {
			chan->ctrl.runl = tdev->runlist;
			break;
		}
	}

	if (!chan->ctrl.runl)
		return -ENODEV;

	chan->ctrl.chan = nvkm_rd32(device, chan->ctrl.runl + 0x004) & 0xfffffff0;

	args->chid = 0;
	args->inst = 0;
	args->token = nvkm_rd32(device, chan->ctrl.runl + 0x008) & 0xffff0000;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000, true, &chan->mthd);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000, true, &chan->inst);
	if (ret)
		return ret;

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, 0x010, 0x0000face);
	nvkm_wo32(chan->inst, 0x030, 0x7ffff902);
	nvkm_wo32(chan->inst, 0x048, lower_32_bits(args->ioffset));
	nvkm_wo32(chan->inst, 0x04c, upper_32_bits(args->ioffset) |
				     (order_base_2(args->ilength / 8) << 16));
	nvkm_wo32(chan->inst, 0x084, 0x20400000);
	nvkm_wo32(chan->inst, 0x094, 0x30000001);
	nvkm_wo32(chan->inst, 0x0ac, 0x00020000);
	nvkm_wo32(chan->inst, 0x0e4, 0x00000000);
	nvkm_wo32(chan->inst, 0x0e8, 0);
	nvkm_wo32(chan->inst, 0x0f4, 0x00001000);
	nvkm_wo32(chan->inst, 0x0f8, 0x10003080);
	nvkm_mo32(chan->inst, 0x218, 0x00000000, 0x00000000);
	nvkm_wo32(chan->inst, 0x220, lower_32_bits(nvkm_memory_bar2(chan->mthd)));
	nvkm_wo32(chan->inst, 0x224, upper_32_bits(nvkm_memory_bar2(chan->mthd)));
	nvkm_done(chan->inst);

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000, true, &chan->user);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0x1000, true, &chan->runl);
	if (ret)
		return ret;

	nvkm_kmap(chan->runl);
	nvkm_wo32(chan->runl, 0x00, 0x80030001);
	nvkm_wo32(chan->runl, 0x04, 1);
	nvkm_wo32(chan->runl, 0x08, 0);
	nvkm_wo32(chan->runl, 0x0c, 0x00000000);
	nvkm_wo32(chan->runl, 0x10, lower_32_bits(nvkm_memory_addr(chan->user)));
	nvkm_wo32(chan->runl, 0x14, upper_32_bits(nvkm_memory_addr(chan->user)));
	nvkm_wo32(chan->runl, 0x18, lower_32_bits(nvkm_memory_addr(chan->inst)));
	nvkm_wo32(chan->runl, 0x1c, upper_32_bits(nvkm_memory_addr(chan->inst)));
	nvkm_done(chan->runl);

	ret = nvkm_vmm_join(vmm, chan->inst);
	if (ret)
		return ret;

	chan->vmm = nvkm_vmm_ref(vmm);
	return 0;
}

static const struct nvkm_device_oclass
ga102_chan_oclass = {
	.ctor = ga102_chan_new,
};

static int
ga102_user_new(struct nvkm_device *device,
	       const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	return tu102_fifo_user_new(oclass, argv, argc, pobject);
}

static const struct nvkm_device_oclass
ga102_user_oclass = {
	.ctor = ga102_user_new,
};

static int
ga102_fifo_sclass(struct nvkm_oclass *oclass, int index, const struct nvkm_device_oclass **class)
{
	if (index == 0) {
		oclass->base = (struct nvkm_sclass) { -1, -1, VOLTA_USERMODE_A };
		*class = &ga102_user_oclass;
		return 0;
	} else
	if (index == 1) {
		oclass->base = (struct nvkm_sclass) { 0, 0, AMPERE_CHANNEL_GPFIFO_B };
		*class = &ga102_chan_oclass;
		return 0;
	}

	return 2;
}

static int
ga102_fifo_info(struct nvkm_engine *engine, u64 mthd, u64 *data)
{
	switch (mthd) {
	case NV_DEVICE_HOST_CHANNELS: *data = 1; return 0;
	default:
		break;
	}

	return -ENOSYS;
}

static void *
ga102_fifo_dtor(struct nvkm_engine *engine)
{
	return ga102_fifo(engine);
}

static const struct nvkm_engine_func
ga102_fifo = {
	.dtor = ga102_fifo_dtor,
	.info = ga102_fifo_info,
	.base.sclass = ga102_fifo_sclass,
};

int
ga102_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_fifo **pfifo)
{
	struct ga102_fifo *fifo;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_engine_ctor(&ga102_fifo, device, type, inst, true, &fifo->base.engine);
	*pfifo = &fifo->base;
	return 0;
}
