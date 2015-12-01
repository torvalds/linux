/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include "priv.h"

#include <subdev/bios.h>
#include <subdev/bios/vmap.h>
#include <subdev/bios/volt.h>

int
nvkm_volt_get(struct nvkm_volt *volt)
{
	int ret, i;

	if (volt->func->volt_get)
		return volt->func->volt_get(volt);

	ret = volt->func->vid_get(volt);
	if (ret >= 0) {
		for (i = 0; i < volt->vid_nr; i++) {
			if (volt->vid[i].vid == ret)
				return volt->vid[i].uv;
		}
		ret = -EINVAL;
	}
	return ret;
}

static int
nvkm_volt_set(struct nvkm_volt *volt, u32 uv)
{
	struct nvkm_subdev *subdev = &volt->subdev;
	int i, ret = -EINVAL;

	if (volt->func->volt_set)
		return volt->func->volt_set(volt, uv);

	for (i = 0; i < volt->vid_nr; i++) {
		if (volt->vid[i].uv == uv) {
			ret = volt->func->vid_set(volt, volt->vid[i].vid);
			nvkm_debug(subdev, "set %duv: %d\n", uv, ret);
			break;
		}
	}
	return ret;
}

static int
nvkm_volt_map(struct nvkm_volt *volt, u8 id)
{
	struct nvkm_bios *bios = volt->subdev.device->bios;
	struct nvbios_vmap_entry info;
	u8  ver, len;
	u16 vmap;

	vmap = nvbios_vmap_entry_parse(bios, id, &ver, &len, &info);
	if (vmap) {
		if (info.link != 0xff) {
			int ret = nvkm_volt_map(volt, info.link);
			if (ret < 0)
				return ret;
			info.min += ret;
		}
		return info.min;
	}

	return id ? id * 10000 : -ENODEV;
}

int
nvkm_volt_set_id(struct nvkm_volt *volt, u8 id, int condition)
{
	int ret;

	if (volt->func->set_id)
		return volt->func->set_id(volt, id, condition);

	ret = nvkm_volt_map(volt, id);
	if (ret >= 0) {
		int prev = nvkm_volt_get(volt);
		if (!condition || prev < 0 ||
		    (condition < 0 && ret < prev) ||
		    (condition > 0 && ret > prev)) {
			ret = nvkm_volt_set(volt, ret);
		} else {
			ret = 0;
		}
	}
	return ret;
}

static void
nvkm_volt_parse_bios(struct nvkm_bios *bios, struct nvkm_volt *volt)
{
	struct nvbios_volt_entry ivid;
	struct nvbios_volt info;
	u8  ver, hdr, cnt, len;
	u16 data;
	int i;

	data = nvbios_volt_parse(bios, &ver, &hdr, &cnt, &len, &info);
	if (data && info.vidmask && info.base && info.step) {
		for (i = 0; i < info.vidmask + 1; i++) {
			if (info.base >= info.min &&
				info.base <= info.max) {
				volt->vid[volt->vid_nr].uv = info.base;
				volt->vid[volt->vid_nr].vid = i;
				volt->vid_nr++;
			}
			info.base += info.step;
		}
		volt->vid_mask = info.vidmask;
	} else if (data && info.vidmask) {
		for (i = 0; i < cnt; i++) {
			data = nvbios_volt_entry_parse(bios, i, &ver, &hdr,
						       &ivid);
			if (data) {
				volt->vid[volt->vid_nr].uv = ivid.voltage;
				volt->vid[volt->vid_nr].vid = ivid.vid;
				volt->vid_nr++;
			}
		}
		volt->vid_mask = info.vidmask;
	}
}

static int
nvkm_volt_init(struct nvkm_subdev *subdev)
{
	struct nvkm_volt *volt = nvkm_volt(subdev);
	int ret = nvkm_volt_get(volt);
	if (ret < 0) {
		if (ret != -ENODEV)
			nvkm_debug(subdev, "current voltage unknown\n");
		return 0;
	}
	nvkm_debug(subdev, "current voltage: %duv\n", ret);
	return 0;
}

static void *
nvkm_volt_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_volt(subdev);
}

static const struct nvkm_subdev_func
nvkm_volt = {
	.dtor = nvkm_volt_dtor,
	.init = nvkm_volt_init,
};

void
nvkm_volt_ctor(const struct nvkm_volt_func *func, struct nvkm_device *device,
	       int index, struct nvkm_volt *volt)
{
	struct nvkm_bios *bios = device->bios;
	int i;

	nvkm_subdev_ctor(&nvkm_volt, device, index, 0, &volt->subdev);
	volt->func = func;

	/* Assuming the non-bios device should build the voltage table later */
	if (bios)
		nvkm_volt_parse_bios(bios, volt);

	if (volt->vid_nr) {
		for (i = 0; i < volt->vid_nr; i++) {
			nvkm_debug(&volt->subdev, "VID %02x: %duv\n",
				   volt->vid[i].vid, volt->vid[i].uv);
		}
	}
}

int
nvkm_volt_new_(const struct nvkm_volt_func *func, struct nvkm_device *device,
	       int index, struct nvkm_volt **pvolt)
{
	if (!(*pvolt = kzalloc(sizeof(**pvolt), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_volt_ctor(func, device, index, *pvolt);
	return 0;
}
