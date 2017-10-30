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
#include <subdev/therm.h>

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
	int i, ret = -EINVAL, best_err = volt->max_uv, best = -1;

	if (volt->func->volt_set)
		return volt->func->volt_set(volt, uv);

	for (i = 0; i < volt->vid_nr; i++) {
		int err = volt->vid[i].uv - uv;
		if (err < 0 || err > best_err)
			continue;

		best_err = err;
		best = i;
		if (best_err == 0)
			break;
	}

	if (best == -1) {
		nvkm_error(subdev, "couldn't set %iuv\n", uv);
		return ret;
	}

	ret = volt->func->vid_set(volt, volt->vid[best].vid);
	nvkm_debug(subdev, "set req %duv to %duv: %d\n", uv,
		   volt->vid[best].uv, ret);
	return ret;
}

int
nvkm_volt_map_min(struct nvkm_volt *volt, u8 id)
{
	struct nvkm_bios *bios = volt->subdev.device->bios;
	struct nvbios_vmap_entry info;
	u8  ver, len;
	u32 vmap;

	vmap = nvbios_vmap_entry_parse(bios, id, &ver, &len, &info);
	if (vmap) {
		if (info.link != 0xff) {
			int ret = nvkm_volt_map_min(volt, info.link);
			if (ret < 0)
				return ret;
			info.min += ret;
		}
		return info.min;
	}

	return id ? id * 10000 : -ENODEV;
}

int
nvkm_volt_map(struct nvkm_volt *volt, u8 id, u8 temp)
{
	struct nvkm_bios *bios = volt->subdev.device->bios;
	struct nvbios_vmap_entry info;
	u8  ver, len;
	u32 vmap;

	vmap = nvbios_vmap_entry_parse(bios, id, &ver, &len, &info);
	if (vmap) {
		s64 result;

		if (volt->speedo < 0)
			return volt->speedo;

		if (ver == 0x10 || (ver == 0x20 && info.mode == 0)) {
			result  = div64_s64((s64)info.arg[0], 10);
			result += div64_s64((s64)info.arg[1] * volt->speedo, 10);
			result += div64_s64((s64)info.arg[2] * volt->speedo * volt->speedo, 100000);
		} else if (ver == 0x20) {
			switch (info.mode) {
			/* 0x0 handled above! */
			case 0x1:
				result =  ((s64)info.arg[0] * 15625) >> 18;
				result += ((s64)info.arg[1] * volt->speedo * 15625) >> 18;
				result += ((s64)info.arg[2] * temp * 15625) >> 10;
				result += ((s64)info.arg[3] * volt->speedo * temp * 15625) >> 18;
				result += ((s64)info.arg[4] * volt->speedo * volt->speedo * 15625) >> 30;
				result += ((s64)info.arg[5] * temp * temp * 15625) >> 18;
				break;
			case 0x3:
				result = (info.min + info.max) / 2;
				break;
			case 0x2:
			default:
				result = info.min;
				break;
			}
		} else {
			return -ENODEV;
		}

		result = min(max(result, (s64)info.min), (s64)info.max);

		if (info.link != 0xff) {
			int ret = nvkm_volt_map(volt, info.link, temp);
			if (ret < 0)
				return ret;
			result += ret;
		}
		return result;
	}

	return id ? id * 10000 : -ENODEV;
}

int
nvkm_volt_set_id(struct nvkm_volt *volt, u8 id, u8 min_id, u8 temp,
		 int condition)
{
	int ret;

	if (volt->func->set_id)
		return volt->func->set_id(volt, id, condition);

	ret = nvkm_volt_map(volt, id, temp);
	if (ret >= 0) {
		int prev = nvkm_volt_get(volt);
		if (!condition || prev < 0 ||
		    (condition < 0 && ret < prev) ||
		    (condition > 0 && ret > prev)) {
			int min = nvkm_volt_map(volt, min_id, temp);
			if (min >= 0)
				ret = max(min, ret);
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
	struct nvkm_subdev *subdev = &bios->subdev;
	struct nvbios_volt_entry ivid;
	struct nvbios_volt info;
	u8  ver, hdr, cnt, len;
	u32 data;
	int i;

	data = nvbios_volt_parse(bios, &ver, &hdr, &cnt, &len, &info);
	if (data && info.vidmask && info.base && info.step && info.ranged) {
		nvkm_debug(subdev, "found ranged based VIDs\n");
		volt->min_uv = info.min;
		volt->max_uv = info.max;
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
	} else if (data && info.vidmask && !info.ranged) {
		nvkm_debug(subdev, "found entry based VIDs\n");
		volt->min_uv = 0xffffffff;
		volt->max_uv = 0;
		for (i = 0; i < cnt; i++) {
			data = nvbios_volt_entry_parse(bios, i, &ver, &hdr,
						       &ivid);
			if (data) {
				volt->vid[volt->vid_nr].uv = ivid.voltage;
				volt->vid[volt->vid_nr].vid = ivid.vid;
				volt->vid_nr++;
				volt->min_uv = min(volt->min_uv, ivid.voltage);
				volt->max_uv = max(volt->max_uv, ivid.voltage);
			}
		}
		volt->vid_mask = info.vidmask;
	} else if (data && info.type == NVBIOS_VOLT_PWM) {
		volt->min_uv = info.base;
		volt->max_uv = info.base + info.pwm_range;
	}
}

static int
nvkm_volt_speedo_read(struct nvkm_volt *volt)
{
	if (volt->func->speedo_read)
		return volt->func->speedo_read(volt);
	return -EINVAL;
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

static int
nvkm_volt_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_volt *volt = nvkm_volt(subdev);

	volt->speedo = nvkm_volt_speedo_read(volt);
	if (volt->speedo > 0)
		nvkm_debug(&volt->subdev, "speedo %x\n", volt->speedo);

	if (volt->func->oneinit)
		return volt->func->oneinit(volt);

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
	.oneinit = nvkm_volt_oneinit,
};

void
nvkm_volt_ctor(const struct nvkm_volt_func *func, struct nvkm_device *device,
	       int index, struct nvkm_volt *volt)
{
	struct nvkm_bios *bios = device->bios;
	int i;

	nvkm_subdev_ctor(&nvkm_volt, device, index, &volt->subdev);
	volt->func = func;

	/* Assuming the non-bios device should build the voltage table later */
	if (bios) {
		u8 ver, hdr, cnt, len;
		struct nvbios_vmap vmap;

		nvkm_volt_parse_bios(bios, volt);
		nvkm_debug(&volt->subdev, "min: %iuv max: %iuv\n",
			   volt->min_uv, volt->max_uv);

		if (nvbios_vmap_parse(bios, &ver, &hdr, &cnt, &len, &vmap)) {
			volt->max0_id = vmap.max0;
			volt->max1_id = vmap.max1;
			volt->max2_id = vmap.max2;
		} else {
			volt->max0_id = 0xff;
			volt->max1_id = 0xff;
			volt->max2_id = 0xff;
		}
	}

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
