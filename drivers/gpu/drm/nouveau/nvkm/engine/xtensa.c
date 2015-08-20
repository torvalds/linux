/*
 * Copyright 2013 Ilia Mirkin
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
#include <engine/xtensa.h>
#include <engine/fifo.h>

static int
nvkm_xtensa_oclass_get(struct nvkm_oclass *oclass, int index)
{
	struct nvkm_xtensa *xtensa = nvkm_xtensa(oclass->engine);
	int c = 0;

	while (xtensa->func->sclass[c].oclass) {
		if (c++ == index) {
			oclass->base = xtensa->func->sclass[index];
			return index;
		}
	}

	return c;
}

static int
nvkm_xtensa_cclass_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
			int align, struct nvkm_gpuobj **pgpuobj)
{
	return nvkm_gpuobj_new(object->engine->subdev.device, 0x10000, align,
			       true, parent, pgpuobj);
}

static const struct nvkm_object_func
nvkm_xtensa_cclass = {
	.bind = nvkm_xtensa_cclass_bind,
};

void
_nvkm_xtensa_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_xtensa *xtensa = (void *)subdev;
	struct nvkm_device *device = xtensa->engine.subdev.device;
	const u32 base = xtensa->addr;
	u32 unk104 = nvkm_rd32(device, base + 0xd04);
	u32 intr = nvkm_rd32(device, base + 0xc20);
	u32 chan = nvkm_rd32(device, base + 0xc28);
	u32 unk10c = nvkm_rd32(device, base + 0xd0c);

	if (intr & 0x10)
		nvkm_warn(subdev, "Watchdog interrupt, engine hung.\n");
	nvkm_wr32(device, base + 0xc20, intr);
	intr = nvkm_rd32(device, base + 0xc20);
	if (unk104 == 0x10001 && unk10c == 0x200 && chan && !intr) {
		nvkm_debug(subdev, "Enabling FIFO_CTRL\n");
		nvkm_mask(device, xtensa->addr + 0xd94, 0, xtensa->fifo_val);
	}
}

static const struct nvkm_engine_func
nvkm_xtensa = {
	.fifo.sclass = nvkm_xtensa_oclass_get,
	.cclass = &nvkm_xtensa_cclass,
};

int
nvkm_xtensa_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 addr, bool enable,
		    const char *iname, const char *fname,
		    int length, void **pobject)
{
	struct nvkm_xtensa *xtensa;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, enable, iname,
				  fname, length, pobject);
	xtensa = *pobject;
	if (ret)
		return ret;

	xtensa->engine.subdev.intr = _nvkm_xtensa_intr;
	xtensa->engine.func = &nvkm_xtensa;
	xtensa->addr = addr;
	return 0;
}

int
_nvkm_xtensa_init(struct nvkm_object *object)
{
	struct nvkm_xtensa *xtensa = (void *)object;
	struct nvkm_subdev *subdev = &xtensa->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 base = xtensa->addr;
	const struct firmware *fw;
	char name[32];
	int i, ret;
	u64 addr, size;
	u32 tmp;

	ret = nvkm_engine_init_old(&xtensa->engine);
	if (ret)
		return ret;

	if (!xtensa->gpu_fw) {
		snprintf(name, sizeof(name), "nouveau/nv84_xuc%03x",
			 xtensa->addr >> 12);

		ret = request_firmware(&fw, name, nv_device_base(device));
		if (ret) {
			nvkm_warn(subdev, "unable to load firmware %s\n", name);
			return ret;
		}

		if (fw->size > 0x40000) {
			nvkm_warn(subdev, "firmware %s too large\n", name);
			release_firmware(fw);
			return -EINVAL;
		}

		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      0x40000, 0x1000, false,
				      &xtensa->gpu_fw);
		if (ret) {
			release_firmware(fw);
			return ret;
		}

		nvkm_kmap(xtensa->gpu_fw);
		for (i = 0; i < fw->size / 4; i++)
			nvkm_wo32(xtensa->gpu_fw, i * 4, *((u32 *)fw->data + i));
		nvkm_done(xtensa->gpu_fw);
		release_firmware(fw);
	}

	addr = nvkm_memory_addr(xtensa->gpu_fw);
	size = nvkm_memory_size(xtensa->gpu_fw);

	nvkm_wr32(device, base + 0xd10, 0x1fffffff); /* ?? */
	nvkm_wr32(device, base + 0xd08, 0x0fffffff); /* ?? */

	nvkm_wr32(device, base + 0xd28, xtensa->unkd28); /* ?? */
	nvkm_wr32(device, base + 0xc20, 0x3f); /* INTR */
	nvkm_wr32(device, base + 0xd84, 0x3f); /* INTR_EN */

	nvkm_wr32(device, base + 0xcc0, addr >> 8); /* XT_REGION_BASE */
	nvkm_wr32(device, base + 0xcc4, 0x1c); /* XT_REGION_SETUP */
	nvkm_wr32(device, base + 0xcc8, size >> 8); /* XT_REGION_LIMIT */

	tmp = nvkm_rd32(device, 0x0);
	nvkm_wr32(device, base + 0xde0, tmp); /* SCRATCH_H2X */

	nvkm_wr32(device, base + 0xce8, 0xf); /* XT_REGION_SETUP */

	nvkm_wr32(device, base + 0xc20, 0x3f); /* INTR */
	nvkm_wr32(device, base + 0xd84, 0x3f); /* INTR_EN */
	return 0;
}

int
_nvkm_xtensa_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_xtensa *xtensa = (void *)object;
	struct nvkm_device *device = xtensa->engine.subdev.device;
	const u32 base = xtensa->addr;

	nvkm_wr32(device, base + 0xd84, 0); /* INTR_EN */
	nvkm_wr32(device, base + 0xd94, 0); /* FIFO_CTRL */

	if (!suspend)
		nvkm_memory_del(&xtensa->gpu_fw);

	return nvkm_engine_fini_old(&xtensa->engine, suspend);
}
