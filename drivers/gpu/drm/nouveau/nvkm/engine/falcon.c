/*
 * Copyright 2012 Red Hat Inc.
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
#include <engine/falcon.h>

#include <core/gpuobj.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

static int
nvkm_falcon_oclass_get(struct nvkm_oclass *oclass, int index)
{
	struct nvkm_falcon *falcon = nvkm_falcon(oclass->engine);
	int c = 0;

	while (falcon->func->sclass[c].oclass) {
		if (c++ == index) {
			oclass->base = falcon->func->sclass[index];
			return index;
		}
	}

	return c;
}

static int
nvkm_falcon_cclass_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
			int align, struct nvkm_gpuobj **pgpuobj)
{
	return nvkm_gpuobj_new(object->engine->subdev.device, 256,
			       align, true, parent, pgpuobj);
}

static const struct nvkm_object_func
nvkm_falcon_cclass = {
	.bind = nvkm_falcon_cclass_bind,
};

static void
nvkm_falcon_intr(struct nvkm_engine *engine)
{
	struct nvkm_falcon *falcon = nvkm_falcon(engine);
	struct nvkm_subdev *subdev = &falcon->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 base = falcon->addr;
	u32 dest = nvkm_rd32(device, base + 0x01c);
	u32 intr = nvkm_rd32(device, base + 0x008) & dest & ~(dest >> 16);
	u32 inst = nvkm_rd32(device, base + 0x050) & 0x3fffffff;
	struct nvkm_fifo_chan *chan;
	unsigned long flags;

	chan = nvkm_fifo_chan_inst(device->fifo, (u64)inst << 12, &flags);

	if (intr & 0x00000040) {
		if (falcon->func->intr) {
			falcon->func->intr(falcon, chan);
			nvkm_wr32(device, base + 0x004, 0x00000040);
			intr &= ~0x00000040;
		}
	}

	if (intr & 0x00000010) {
		nvkm_debug(subdev, "ucode halted\n");
		nvkm_wr32(device, base + 0x004, 0x00000010);
		intr &= ~0x00000010;
	}

	if (intr)  {
		nvkm_error(subdev, "intr %08x\n", intr);
		nvkm_wr32(device, base + 0x004, intr);
	}

	nvkm_fifo_chan_put(device->fifo, flags, &chan);
}

static int
nvkm_falcon_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_falcon *falcon = nvkm_falcon(engine);
	struct nvkm_device *device = falcon->engine.subdev.device;
	const u32 base = falcon->addr;

	if (!suspend) {
		nvkm_memory_unref(&falcon->core);
		if (falcon->external) {
			vfree(falcon->data.data);
			vfree(falcon->code.data);
			falcon->code.data = NULL;
		}
	}

	nvkm_mask(device, base + 0x048, 0x00000003, 0x00000000);
	nvkm_wr32(device, base + 0x014, 0xffffffff);
	return 0;
}

static void *
vmemdup(const void *src, size_t len)
{
	void *p = vmalloc(len);

	if (p)
		memcpy(p, src, len);
	return p;
}

static int
nvkm_falcon_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_falcon *falcon = nvkm_falcon(engine);
	struct nvkm_subdev *subdev = &falcon->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 base = falcon->addr;
	u32 caps;

	/* determine falcon capabilities */
	if (device->chipset <  0xa3 ||
	    device->chipset == 0xaa || device->chipset == 0xac) {
		falcon->version = 0;
		falcon->secret  = (falcon->addr == 0x087000) ? 1 : 0;
	} else {
		caps = nvkm_rd32(device, base + 0x12c);
		falcon->version = (caps & 0x0000000f);
		falcon->secret  = (caps & 0x00000030) >> 4;
	}

	caps = nvkm_rd32(device, base + 0x108);
	falcon->code.limit = (caps & 0x000001ff) << 8;
	falcon->data.limit = (caps & 0x0003fe00) >> 1;

	nvkm_debug(subdev, "falcon version: %d\n", falcon->version);
	nvkm_debug(subdev, "secret level: %d\n", falcon->secret);
	nvkm_debug(subdev, "code limit: %d\n", falcon->code.limit);
	nvkm_debug(subdev, "data limit: %d\n", falcon->data.limit);
	return 0;
}

static int
nvkm_falcon_init(struct nvkm_engine *engine)
{
	struct nvkm_falcon *falcon = nvkm_falcon(engine);
	struct nvkm_subdev *subdev = &falcon->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const struct firmware *fw;
	char name[32] = "internal";
	const u32 base = falcon->addr;
	int ret, i;

	/* wait for 'uc halted' to be signalled before continuing */
	if (falcon->secret && falcon->version < 4) {
		if (!falcon->version) {
			nvkm_msec(device, 2000,
				if (nvkm_rd32(device, base + 0x008) & 0x00000010)
					break;
			);
		} else {
			nvkm_msec(device, 2000,
				if (!(nvkm_rd32(device, base + 0x180) & 0x80000000))
					break;
			);
		}
		nvkm_wr32(device, base + 0x004, 0x00000010);
	}

	/* disable all interrupts */
	nvkm_wr32(device, base + 0x014, 0xffffffff);

	/* no default ucode provided by the engine implementation, try and
	 * locate a "self-bootstrapping" firmware image for the engine
	 */
	if (!falcon->code.data) {
		snprintf(name, sizeof(name), "nouveau/nv%02x_fuc%03x",
			 device->chipset, falcon->addr >> 12);

		ret = request_firmware(&fw, name, device->dev);
		if (ret == 0) {
			falcon->code.data = vmemdup(fw->data, fw->size);
			falcon->code.size = fw->size;
			falcon->data.data = NULL;
			falcon->data.size = 0;
			release_firmware(fw);
		}

		falcon->external = true;
	}

	/* next step is to try and load "static code/data segment" firmware
	 * images for the engine
	 */
	if (!falcon->code.data) {
		snprintf(name, sizeof(name), "nouveau/nv%02x_fuc%03xd",
			 device->chipset, falcon->addr >> 12);

		ret = request_firmware(&fw, name, device->dev);
		if (ret) {
			nvkm_error(subdev, "unable to load firmware data\n");
			return -ENODEV;
		}

		falcon->data.data = vmemdup(fw->data, fw->size);
		falcon->data.size = fw->size;
		release_firmware(fw);
		if (!falcon->data.data)
			return -ENOMEM;

		snprintf(name, sizeof(name), "nouveau/nv%02x_fuc%03xc",
			 device->chipset, falcon->addr >> 12);

		ret = request_firmware(&fw, name, device->dev);
		if (ret) {
			nvkm_error(subdev, "unable to load firmware code\n");
			return -ENODEV;
		}

		falcon->code.data = vmemdup(fw->data, fw->size);
		falcon->code.size = fw->size;
		release_firmware(fw);
		if (!falcon->code.data)
			return -ENOMEM;
	}

	nvkm_debug(subdev, "firmware: %s (%s)\n", name, falcon->data.data ?
		   "static code/data segments" : "self-bootstrapping");

	/* ensure any "self-bootstrapping" firmware image is in vram */
	if (!falcon->data.data && !falcon->core) {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      falcon->code.size, 256, false,
				      &falcon->core);
		if (ret) {
			nvkm_error(subdev, "core allocation failed, %d\n", ret);
			return ret;
		}

		nvkm_kmap(falcon->core);
		for (i = 0; i < falcon->code.size; i += 4)
			nvkm_wo32(falcon->core, i, falcon->code.data[i / 4]);
		nvkm_done(falcon->core);
	}

	/* upload firmware bootloader (or the full code segments) */
	if (falcon->core) {
		u64 addr = nvkm_memory_addr(falcon->core);
		if (device->card_type < NV_C0)
			nvkm_wr32(device, base + 0x618, 0x04000000);
		else
			nvkm_wr32(device, base + 0x618, 0x00000114);
		nvkm_wr32(device, base + 0x11c, 0);
		nvkm_wr32(device, base + 0x110, addr >> 8);
		nvkm_wr32(device, base + 0x114, 0);
		nvkm_wr32(device, base + 0x118, 0x00006610);
	} else {
		if (falcon->code.size > falcon->code.limit ||
		    falcon->data.size > falcon->data.limit) {
			nvkm_error(subdev, "ucode exceeds falcon limit(s)\n");
			return -EINVAL;
		}

		if (falcon->version < 3) {
			nvkm_wr32(device, base + 0xff8, 0x00100000);
			for (i = 0; i < falcon->code.size / 4; i++)
				nvkm_wr32(device, base + 0xff4, falcon->code.data[i]);
		} else {
			nvkm_wr32(device, base + 0x180, 0x01000000);
			for (i = 0; i < falcon->code.size / 4; i++) {
				if ((i & 0x3f) == 0)
					nvkm_wr32(device, base + 0x188, i >> 6);
				nvkm_wr32(device, base + 0x184, falcon->code.data[i]);
			}
		}
	}

	/* upload data segment (if necessary), zeroing the remainder */
	if (falcon->version < 3) {
		nvkm_wr32(device, base + 0xff8, 0x00000000);
		for (i = 0; !falcon->core && i < falcon->data.size / 4; i++)
			nvkm_wr32(device, base + 0xff4, falcon->data.data[i]);
		for (; i < falcon->data.limit; i += 4)
			nvkm_wr32(device, base + 0xff4, 0x00000000);
	} else {
		nvkm_wr32(device, base + 0x1c0, 0x01000000);
		for (i = 0; !falcon->core && i < falcon->data.size / 4; i++)
			nvkm_wr32(device, base + 0x1c4, falcon->data.data[i]);
		for (; i < falcon->data.limit / 4; i++)
			nvkm_wr32(device, base + 0x1c4, 0x00000000);
	}

	/* start it running */
	nvkm_wr32(device, base + 0x10c, 0x00000001); /* BLOCK_ON_FIFO */
	nvkm_wr32(device, base + 0x104, 0x00000000); /* ENTRY */
	nvkm_wr32(device, base + 0x100, 0x00000002); /* TRIGGER */
	nvkm_wr32(device, base + 0x048, 0x00000003); /* FIFO | CHSW */

	if (falcon->func->init)
		falcon->func->init(falcon);
	return 0;
}

static void *
nvkm_falcon_dtor(struct nvkm_engine *engine)
{
	return nvkm_falcon(engine);
}

static const struct nvkm_engine_func
nvkm_falcon = {
	.dtor = nvkm_falcon_dtor,
	.oneinit = nvkm_falcon_oneinit,
	.init = nvkm_falcon_init,
	.fini = nvkm_falcon_fini,
	.intr = nvkm_falcon_intr,
	.fifo.sclass = nvkm_falcon_oclass_get,
	.cclass = &nvkm_falcon_cclass,
};

int
nvkm_falcon_new_(const struct nvkm_falcon_func *func,
		 struct nvkm_device *device, int index, bool enable,
		 u32 addr, struct nvkm_engine **pengine)
{
	struct nvkm_falcon *falcon;

	if (!(falcon = kzalloc(sizeof(*falcon), GFP_KERNEL)))
		return -ENOMEM;
	falcon->func = func;
	falcon->addr = addr;
	falcon->code.data = func->code.data;
	falcon->code.size = func->code.size;
	falcon->data.data = func->data.data;
	falcon->data.size = func->data.size;
	*pengine = &falcon->engine;

	return nvkm_engine_ctor(&nvkm_falcon, device, index,
				enable, &falcon->engine);
}
