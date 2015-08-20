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
 *
 * Authors: Ben Skeggs
 */
#include "rootnv50.h"
#include "dmacnv50.h"

#include <core/client.h>
#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
nv50_disp_root_scanoutpos(NV50_DISP_MTHD_V0)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	const u32 blanke = nvkm_rd32(device, 0x610aec + (head * 0x540));
	const u32 blanks = nvkm_rd32(device, 0x610af4 + (head * 0x540));
	const u32 total  = nvkm_rd32(device, 0x610afc + (head * 0x540));
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	int ret;

	nvif_ioctl(object, "disp scanoutpos size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(object, "disp scanoutpos vers %d\n",
			   args->v0.version);
		args->v0.vblanke = (blanke & 0xffff0000) >> 16;
		args->v0.hblanke = (blanke & 0x0000ffff);
		args->v0.vblanks = (blanks & 0xffff0000) >> 16;
		args->v0.hblanks = (blanks & 0x0000ffff);
		args->v0.vtotal  = ( total & 0xffff0000) >> 16;
		args->v0.htotal  = ( total & 0x0000ffff);
		args->v0.time[0] = ktime_to_ns(ktime_get());
		args->v0.vline = /* vline read locks hline */
			nvkm_rd32(device, 0x616340 + (head * 0x800)) & 0xffff;
		args->v0.time[1] = ktime_to_ns(ktime_get());
		args->v0.hline =
			nvkm_rd32(device, 0x616344 + (head * 0x800)) & 0xffff;
	} else
		return ret;

	return 0;
}

int
nv50_disp_root_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	const struct nv50_disp_impl *impl = (void *)nv_oclass(object->engine);
	union {
		struct nv50_disp_mthd_v0 v0;
		struct nv50_disp_mthd_v1 v1;
	} *args = data;
	struct nv50_disp *disp = (void *)object->engine;
	struct nvkm_output *outp = NULL;
	struct nvkm_output *temp;
	u16 type, mask = 0;
	int head, ret;

	if (mthd != NV50_DISP_MTHD)
		return -EINVAL;

	nvif_ioctl(object, "disp mthd size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			   args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		head = args->v0.head;
	} else
	if (nvif_unpack(args->v1, 1, 1, true)) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x "
				   "type %04x mask %04x\n",
			   args->v1.version, args->v1.method,
			   args->v1.hasht, args->v1.hashm);
		mthd = args->v1.method;
		type = args->v1.hasht;
		mask = args->v1.hashm;
		head = ffs((mask >> 8) & 0x0f) - 1;
	} else
		return ret;

	if (head < 0 || head >= disp->head.nr)
		return -ENXIO;

	if (mask) {
		list_for_each_entry(temp, &disp->base.outp, head) {
			if ((temp->info.hasht         == type) &&
			    (temp->info.hashm & mask) == mask) {
				outp = temp;
				break;
			}
		}
		if (outp == NULL)
			return -ENXIO;
	}

	switch (mthd) {
	case NV50_DISP_SCANOUTPOS:
		return impl->head.scanoutpos(object, disp, data, size, head);
	default:
		break;
	}

	switch (mthd * !!outp) {
	case NV50_DISP_MTHD_V1_DAC_PWR:
		return disp->dac.power(object, disp, data, size, head, outp);
	case NV50_DISP_MTHD_V1_DAC_LOAD:
		return disp->dac.sense(object, disp, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_PWR:
		return disp->sor.power(object, disp, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_HDA_ELD:
		if (!disp->sor.hda_eld)
			return -ENODEV;
		return disp->sor.hda_eld(object, disp, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_HDMI_PWR:
		if (!disp->sor.hdmi)
			return -ENODEV;
		return disp->sor.hdmi(object, disp, data, size, head, outp);
	case NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT: {
		union {
			struct nv50_disp_sor_lvds_script_v0 v0;
		} *args = data;
		nvif_ioctl(object, "disp sor lvds script size %d\n", size);
		if (nvif_unpack(args->v0, 0, 0, false)) {
			nvif_ioctl(object, "disp sor lvds script "
					   "vers %d name %04x\n",
				   args->v0.version, args->v0.script);
			disp->sor.lvdsconf = args->v0.script;
			return 0;
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_DP_PWR: {
		struct nvkm_output_dp *outpdp = nvkm_output_dp(outp);
		union {
			struct nv50_disp_sor_dp_pwr_v0 v0;
		} *args = data;
		nvif_ioctl(object, "disp sor dp pwr size %d\n", size);
		if (nvif_unpack(args->v0, 0, 0, false)) {
			nvif_ioctl(object, "disp sor dp pwr vers %d state %d\n",
				   args->v0.version, args->v0.state);
			if (args->v0.state == 0) {
				nvkm_notify_put(&outpdp->irq);
				outpdp->func->lnk_pwr(outpdp, 0);
				atomic_set(&outpdp->lt.done, 0);
				return 0;
			} else
			if (args->v0.state != 0) {
				nvkm_output_dp_train(&outpdp->base, 0, true);
				return 0;
			}
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_PIOR_PWR:
		if (!disp->pior.power)
			return -ENODEV;
		return disp->pior.power(object, disp, data, size, head, outp);
	default:
		break;
	}

	return -EINVAL;
}

int
nv50_disp_root_ctor(struct nvkm_object *parent,
		    struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	struct nv50_disp *disp = (void *)engine;
	struct nv50_disp_root *root;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_gpuobj *instmem = (void *)parent;
	int ret;

	ret = nvkm_parent_create(parent, engine, oclass, 0,
				 disp->sclass, 0, &root);
	*pobject = nv_object(root);
	if (ret)
		return ret;


	return nvkm_ramht_new(device, 0x1000, 0, instmem, &root->ramht);
}

void
nv50_disp_root_dtor(struct nvkm_object *object)
{
	struct nv50_disp_root *root = (void *)object;
	nvkm_ramht_del(&root->ramht);
	nvkm_parent_destroy(&root->base);
}

static int
nv50_disp_root_init(struct nvkm_object *object)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_root *root = (void *)object;
	struct nvkm_device *device = disp->base.engine.subdev.device;
	int ret, i;
	u32 tmp;

	ret = nvkm_parent_init(&root->base);
	if (ret)
		return ret;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.  NFI what the 0x614004 caps are for..
	 */
	tmp = nvkm_rd32(device, 0x614004);
	nvkm_wr32(device, 0x610184, tmp);

	/* ... CRTC caps */
	for (i = 0; i < disp->head.nr; i++) {
		tmp = nvkm_rd32(device, 0x616100 + (i * 0x800));
		nvkm_wr32(device, 0x610190 + (i * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616104 + (i * 0x800));
		nvkm_wr32(device, 0x610194 + (i * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616108 + (i * 0x800));
		nvkm_wr32(device, 0x610198 + (i * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x61610c + (i * 0x800));
		nvkm_wr32(device, 0x61019c + (i * 0x10), tmp);
	}

	/* ... DAC caps */
	for (i = 0; i < disp->dac.nr; i++) {
		tmp = nvkm_rd32(device, 0x61a000 + (i * 0x800));
		nvkm_wr32(device, 0x6101d0 + (i * 0x04), tmp);
	}

	/* ... SOR caps */
	for (i = 0; i < disp->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_wr32(device, 0x6101e0 + (i * 0x04), tmp);
	}

	/* ... PIOR caps */
	for (i = 0; i < disp->pior.nr; i++) {
		tmp = nvkm_rd32(device, 0x61e000 + (i * 0x800));
		nvkm_wr32(device, 0x6101f0 + (i * 0x04), tmp);
	}

	/* steal display away from vbios, or something like that */
	if (nvkm_rd32(device, 0x610024) & 0x00000100) {
		nvkm_wr32(device, 0x610024, 0x00000100);
		nvkm_mask(device, 0x6194e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6194e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* point at display engine memory area (hash table, objects) */
	nvkm_wr32(device, 0x610010, (root->ramht->gpuobj->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nvkm_wr32(device, 0x61002c, 0x00000370);
	nvkm_wr32(device, 0x610028, 0x00000000);
	return 0;
}

static int
nv50_disp_root_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_root *root = (void *)object;
	struct nvkm_device *device = disp->base.engine.subdev.device;

	/* disable all interrupts */
	nvkm_wr32(device, 0x610024, 0x00000000);
	nvkm_wr32(device, 0x610020, 0x00000000);

	return nvkm_parent_fini(&root->base, suspend);
}

struct nvkm_ofuncs
nv50_disp_root_ofuncs = {
	.ctor = nv50_disp_root_ctor,
	.dtor = nv50_disp_root_dtor,
	.init = nv50_disp_root_init,
	.fini = nv50_disp_root_fini,
	.mthd = nv50_disp_root_mthd,
	.ntfy = nvkm_disp_ntfy,
};

struct nvkm_oclass
nv50_disp_root_oclass[] = {
	{ NV50_DISP, &nv50_disp_root_ofuncs },
	{}
};

struct nvkm_oclass
nv50_disp_sclass[] = {
	{ NV50_DISP_CORE_CHANNEL_DMA, &nv50_disp_core_ofuncs.base },
	{ NV50_DISP_BASE_CHANNEL_DMA, &nv50_disp_base_ofuncs.base },
	{ NV50_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ NV50_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ NV50_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};
