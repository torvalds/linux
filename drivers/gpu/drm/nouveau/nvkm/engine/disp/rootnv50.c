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
#include "channv50.h"
#include "dp.h"
#include "head.h"
#include "ior.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/cl5070.h>
#include <nvif/unpack.h>

static int
nv50_disp_root_mthd_(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	union {
		struct nv50_disp_mthd_v0 v0;
		struct nv50_disp_mthd_v1 v1;
	} *args = data;
	struct nv50_disp_root *root = nv50_disp_root(object);
	struct nv50_disp *disp = root->disp;
	struct nvkm_outp *temp, *outp = NULL;
	struct nvkm_head *head;
	u16 type, mask = 0;
	int hidx, ret = -ENOSYS;

	if (mthd != NV50_DISP_MTHD)
		return -EINVAL;

	nvif_ioctl(object, "disp mthd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			   args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		hidx = args->v0.head;
	} else
	if (!(ret = nvif_unpack(ret, &data, &size, args->v1, 1, 1, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x "
				   "type %04x mask %04x\n",
			   args->v1.version, args->v1.method,
			   args->v1.hasht, args->v1.hashm);
		mthd = args->v1.method;
		type = args->v1.hasht;
		mask = args->v1.hashm;
		hidx = ffs((mask >> 8) & 0x0f) - 1;
	} else
		return ret;

	if (!(head = nvkm_head_find(&disp->base, hidx)))
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
	case NV50_DISP_SCANOUTPOS: {
		return nvkm_head_mthd_scanoutpos(object, head, data, size);
	}
	default:
		break;
	}

	switch (mthd * !!outp) {
	case NV50_DISP_MTHD_V1_ACQUIRE: {
		union {
			struct nv50_disp_acquire_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			ret = nvkm_outp_acquire(outp, NVKM_OUTP_USER, args->v0.hda);
			if (ret == 0) {
				args->v0.or = outp->ior->id;
				args->v0.link = outp->ior->asy.link;
			}
		}
		return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_RELEASE:
		nvkm_outp_release(outp, NVKM_OUTP_USER);
		return 0;
	case NV50_DISP_MTHD_V1_DAC_LOAD: {
		union {
			struct nv50_disp_dac_load_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			if (args->v0.data & 0xfff00000)
				return -EINVAL;
			ret = nvkm_outp_acquire(outp, NVKM_OUTP_PRIV, false);
			if (ret)
				return ret;
			ret = outp->ior->func->sense(outp->ior, args->v0.data);
			nvkm_outp_release(outp, NVKM_OUTP_PRIV);
			if (ret < 0)
				return ret;
			args->v0.load = ret;
			return 0;
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_HDA_ELD: {
		union {
			struct nv50_disp_sor_hda_eld_v0 v0;
		} *args = data;
		struct nvkm_ior *ior = outp->ior;
		int ret = -ENOSYS;

		nvif_ioctl(object, "disp sor hda eld size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
			nvif_ioctl(object, "disp sor hda eld vers %d\n",
				   args->v0.version);
			if (size > 0x60)
				return -E2BIG;
		} else
			return ret;

		if (!ior->func->hda.hpd)
			return -ENODEV;

		if (size && args->v0.data[0]) {
			if (outp->info.type == DCB_OUTPUT_DP)
				ior->func->dp.audio(ior, hidx, true);
			ior->func->hda.hpd(ior, hidx, true);
			ior->func->hda.eld(ior, hidx, data, size);
		} else {
			if (outp->info.type == DCB_OUTPUT_DP)
				ior->func->dp.audio(ior, hidx, false);
			ior->func->hda.hpd(ior, hidx, false);
		}

		return 0;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_HDMI_PWR: {
		union {
			struct nv50_disp_sor_hdmi_pwr_v0 v0;
		} *args = data;
		u8 *vendor, vendor_size;
		u8 *avi, avi_size;
		int ret = -ENOSYS;

		nvif_ioctl(object, "disp sor hdmi ctrl size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
			nvif_ioctl(object, "disp sor hdmi ctrl vers %d state %d "
					   "max_ac_packet %d rekey %d scdc %d\n",
				   args->v0.version, args->v0.state,
				   args->v0.max_ac_packet, args->v0.rekey,
				   args->v0.scdc);
			if (args->v0.max_ac_packet > 0x1f || args->v0.rekey > 0x7f)
				return -EINVAL;
			if ((args->v0.avi_infoframe_length
			     + args->v0.vendor_infoframe_length) > size)
				return -EINVAL;
			else
			if ((args->v0.avi_infoframe_length
			     + args->v0.vendor_infoframe_length) < size)
				return -E2BIG;
			avi = data;
			avi_size = args->v0.avi_infoframe_length;
			vendor = avi + avi_size;
			vendor_size = args->v0.vendor_infoframe_length;
		} else
			return ret;

		if (!outp->ior->func->hdmi.ctrl)
			return -ENODEV;

		outp->ior->func->hdmi.ctrl(outp->ior, hidx, args->v0.state,
					   args->v0.max_ac_packet,
					   args->v0.rekey, avi, avi_size,
					   vendor, vendor_size);

		if (outp->ior->func->hdmi.scdc)
			outp->ior->func->hdmi.scdc(outp->ior, args->v0.scdc);

		return 0;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_LVDS_SCRIPT: {
		union {
			struct nv50_disp_sor_lvds_script_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		nvif_ioctl(object, "disp sor lvds script size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			nvif_ioctl(object, "disp sor lvds script "
					   "vers %d name %04x\n",
				   args->v0.version, args->v0.script);
			disp->sor.lvdsconf = args->v0.script;
			return 0;
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_DP_MST_LINK: {
		struct nvkm_dp *dp = nvkm_dp(outp);
		union {
			struct nv50_disp_sor_dp_mst_link_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		nvif_ioctl(object, "disp sor dp mst link size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			nvif_ioctl(object, "disp sor dp mst link vers %d state %d\n",
				   args->v0.version, args->v0.state);
			dp->lt.mst = !!args->v0.state;
			return 0;
		} else
			return ret;
	}
		break;
	case NV50_DISP_MTHD_V1_SOR_DP_MST_VCPI: {
		union {
			struct nv50_disp_sor_dp_mst_vcpi_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		nvif_ioctl(object, "disp sor dp mst vcpi size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			nvif_ioctl(object, "disp sor dp mst vcpi vers %d "
					   "slot %02x/%02x pbn %04x/%04x\n",
				   args->v0.version, args->v0.start_slot,
				   args->v0.num_slots, args->v0.pbn,
				   args->v0.aligned_pbn);
			if (!outp->ior->func->dp.vcpi)
				return -ENODEV;
			outp->ior->func->dp.vcpi(outp->ior, hidx,
						 args->v0.start_slot,
						 args->v0.num_slots,
						 args->v0.pbn,
						 args->v0.aligned_pbn);
			return 0;
		} else
			return ret;
	}
		break;
	default:
		break;
	}

	return -EINVAL;
}

static int
nv50_disp_root_child_new_(const struct nvkm_oclass *oclass,
			  void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nv50_disp *disp = nv50_disp_root(oclass->parent)->disp;
	const struct nv50_disp_user *user = oclass->priv;
	return user->ctor(oclass, argv, argc, disp, pobject);
}

static int
nv50_disp_root_child_get_(struct nvkm_object *object, int index,
			  struct nvkm_oclass *sclass)
{
	struct nv50_disp_root *root = nv50_disp_root(object);

	if (root->func->user[index].ctor) {
		sclass->base = root->func->user[index].base;
		sclass->priv = root->func->user + index;
		sclass->ctor = nv50_disp_root_child_new_;
		return 0;
	}

	return -EINVAL;
}

static void *
nv50_disp_root_dtor_(struct nvkm_object *object)
{
	struct nv50_disp_root *root = nv50_disp_root(object);
	return root;
}

static const struct nvkm_object_func
nv50_disp_root_ = {
	.dtor = nv50_disp_root_dtor_,
	.mthd = nv50_disp_root_mthd_,
	.ntfy = nvkm_disp_ntfy,
	.sclass = nv50_disp_root_child_get_,
};

int
nv50_disp_root_new_(const struct nv50_disp_root_func *func,
		    struct nvkm_disp *base, const struct nvkm_oclass *oclass,
		    void *data, u32 size, struct nvkm_object **pobject)
{
	struct nv50_disp *disp = nv50_disp(base);
	struct nv50_disp_root *root;

	if (!(root = kzalloc(sizeof(*root), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &root->object;

	nvkm_object_ctor(&nv50_disp_root_, oclass, &root->object);
	root->func = func;
	root->disp = disp;
	return 0;
}

static const struct nv50_disp_root_func
nv50_disp_root = {
	.user = {
		{{0,0,NV50_DISP_CURSOR             }, nv50_disp_curs_new },
		{{0,0,NV50_DISP_OVERLAY            }, nv50_disp_oimm_new },
		{{0,0,NV50_DISP_BASE_CHANNEL_DMA   }, nv50_disp_base_new },
		{{0,0,NV50_DISP_CORE_CHANNEL_DMA   }, nv50_disp_core_new },
		{{0,0,NV50_DISP_OVERLAY_CHANNEL_DMA}, nv50_disp_ovly_new },
		{}
	},
};

static int
nv50_disp_root_new(struct nvkm_disp *disp, const struct nvkm_oclass *oclass,
		   void *data, u32 size, struct nvkm_object **pobject)
{
	return nv50_disp_root_new_(&nv50_disp_root, disp, oclass,
				   data, size, pobject);
}

const struct nvkm_disp_oclass
nv50_disp_root_oclass = {
	.base.oclass = NV50_DISP,
	.base.minver = -1,
	.base.maxver = -1,
	.ctor = nv50_disp_root_new,
};
