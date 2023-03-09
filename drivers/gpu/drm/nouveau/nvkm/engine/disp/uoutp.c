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
#define nvkm_uoutp(p) container_of((p), struct nvkm_outp, object)
#include "outp.h"
#include "dp.h"
#include "head.h"
#include "ior.h"

#include <nvif/if0012.h>

static int
nvkm_uoutp_mthd_dp_mst_vcpi(struct nvkm_outp *outp, void *argv, u32 argc)
{
	struct nvkm_ior *ior = outp->ior;
	union nvif_outp_dp_mst_vcpi_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!ior->func->dp || !ior->func->dp->vcpi || !nvkm_head_find(outp->disp, args->v0.head))
		return -EINVAL;

	ior->func->dp->vcpi(ior, args->v0.head, args->v0.start_slot, args->v0.num_slots,
				 args->v0.pbn, args->v0.aligned_pbn);
	return 0;
}

static int
nvkm_uoutp_mthd_dp_retrain(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_retrain_args *args = argv;

	if (argc != sizeof(args->vn))
		return -ENOSYS;

	if (!atomic_read(&outp->dp.lt.done))
		return 0;

	return outp->func->acquire(outp);
}

static int
nvkm_uoutp_mthd_dp_aux_pwr(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_aux_pwr_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	outp->dp.enabled = !!args->v0.state;
	nvkm_dp_enable(outp, outp->dp.enabled);
	return 0;
}

static int
nvkm_uoutp_mthd_hda_eld(struct nvkm_outp *outp, void *argv, u32 argc)
{
	struct nvkm_ior *ior = outp->ior;
	union nvif_outp_hda_eld_args *args = argv;

	if (argc < sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	argc -= sizeof(args->v0);

	if (!ior->hda || !nvkm_head_find(outp->disp, args->v0.head))
		return -EINVAL;
	if (argc > 0x60)
		return -E2BIG;

	if (argc && args->v0.data[0]) {
		if (outp->info.type == DCB_OUTPUT_DP)
			ior->func->dp->audio(ior, args->v0.head, true);
		ior->func->hda->hpd(ior, args->v0.head, true);
		ior->func->hda->eld(ior, args->v0.head, args->v0.data, argc);
	} else {
		if (outp->info.type == DCB_OUTPUT_DP)
			ior->func->dp->audio(ior, args->v0.head, false);
		ior->func->hda->hpd(ior, args->v0.head, false);
	}

	return 0;
}

static int
nvkm_uoutp_mthd_infoframe(struct nvkm_outp *outp, void *argv, u32 argc)
{
	struct nvkm_ior *ior = outp->ior;
	union nvif_outp_infoframe_args *args = argv;
	ssize_t size = argc - sizeof(*args);

	if (argc < sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!nvkm_head_find(outp->disp, args->v0.head))
		return -EINVAL;

	switch (ior->func->hdmi ? args->v0.type : 0xff) {
	case NVIF_OUTP_INFOFRAME_V0_AVI:
		ior->func->hdmi->infoframe_avi(ior, args->v0.head, &args->v0.data, size);
		return 0;
	case NVIF_OUTP_INFOFRAME_V0_VSI:
		ior->func->hdmi->infoframe_vsi(ior, args->v0.head, &args->v0.data, size);
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_uoutp_mthd_release(struct nvkm_outp *outp, void *argv, u32 argc)
{
	struct nvkm_head *head = outp->asy.head;
	struct nvkm_ior *ior = outp->ior;
	union nvif_outp_release_args *args = argv;

	if (argc != sizeof(args->vn))
		return -ENOSYS;

	if (ior->func->hdmi && head) {
		ior->func->hdmi->infoframe_avi(ior, head->id, NULL, 0);
		ior->func->hdmi->infoframe_vsi(ior, head->id, NULL, 0);
		ior->func->hdmi->ctrl(ior, head->id, false, 0, 0);
	}

	nvkm_outp_release(outp, NVKM_OUTP_USER);
	return 0;
}

static int
nvkm_uoutp_mthd_acquire_dp(struct nvkm_outp *outp, u8 dpcd[16],
			   u8 link_nr, u8 link_bw, bool hda, bool mst)
{
	int ret;

	ret = nvkm_outp_acquire(outp, NVKM_OUTP_USER, hda);
	if (ret)
		return ret;

	memcpy(outp->dp.dpcd, dpcd, sizeof(outp->dp.dpcd));
	outp->dp.lt.nr = link_nr;
	outp->dp.lt.bw = link_bw;
	outp->dp.lt.mst = mst;
	return 0;
}

static int
nvkm_uoutp_mthd_acquire_tmds(struct nvkm_outp *outp, u8 head, u8 hdmi, u8 hdmi_max_ac_packet,
			     u8 hdmi_rekey, u8 hdmi_scdc, u8 hdmi_hda)
{
	struct nvkm_ior *ior;
	int ret;

	if (!(outp->asy.head = nvkm_head_find(outp->disp, head)))
		return -EINVAL;

	ret = nvkm_outp_acquire(outp, NVKM_OUTP_USER, hdmi && hdmi_hda);
	if (ret)
		return ret;

	ior = outp->ior;

	if (hdmi) {
		if (!ior->func->hdmi ||
		    hdmi_max_ac_packet > 0x1f || hdmi_rekey > 0x7f ||
		    (hdmi_scdc && !ior->func->hdmi->scdc)) {
			nvkm_outp_release(outp, NVKM_OUTP_USER);
			return -EINVAL;
		}

		ior->func->hdmi->ctrl(ior, head, hdmi, hdmi_max_ac_packet, hdmi_rekey);
		if (ior->func->hdmi->scdc)
			ior->func->hdmi->scdc(ior, hdmi_scdc);
	}

	return 0;
}

static int
nvkm_uoutp_mthd_acquire_lvds(struct nvkm_outp *outp, bool dual, bool bpc8)
{
	if (outp->info.type != DCB_OUTPUT_LVDS)
		return -EINVAL;

	outp->lvds.dual = dual;
	outp->lvds.bpc8 = bpc8;

	return nvkm_outp_acquire(outp, NVKM_OUTP_USER, false);
}

static int
nvkm_uoutp_mthd_acquire(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_acquire_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (outp->ior)
		return -EBUSY;

	switch (args->v0.proto) {
	case NVIF_OUTP_ACQUIRE_V0_RGB_CRT:
		ret = nvkm_outp_acquire(outp, NVKM_OUTP_USER, false);
		break;
	case NVIF_OUTP_ACQUIRE_V0_TMDS:
		ret = nvkm_uoutp_mthd_acquire_tmds(outp, args->v0.tmds.head,
							 args->v0.tmds.hdmi,
							 args->v0.tmds.hdmi_max_ac_packet,
							 args->v0.tmds.hdmi_rekey,
							 args->v0.tmds.hdmi_scdc,
							 args->v0.tmds.hdmi_hda);
		break;
	case NVIF_OUTP_ACQUIRE_V0_LVDS:
		ret = nvkm_uoutp_mthd_acquire_lvds(outp, args->v0.lvds.dual, args->v0.lvds.bpc8);
		break;
	case NVIF_OUTP_ACQUIRE_V0_DP:
		ret = nvkm_uoutp_mthd_acquire_dp(outp, args->v0.dp.dpcd,
						       args->v0.dp.link_nr,
						       args->v0.dp.link_bw,
						       args->v0.dp.hda != 0,
						       args->v0.dp.mst != 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret)
		return ret;

	args->v0.or = outp->ior->id;
	args->v0.link = outp->ior->asy.link;
	return 0;
}

static int
nvkm_uoutp_mthd_load_detect(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_load_detect_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	ret = nvkm_outp_acquire(outp, NVKM_OUTP_PRIV, false);
	if (ret == 0) {
		if (outp->ior->func->sense) {
			ret = outp->ior->func->sense(outp->ior, args->v0.data);
			args->v0.load = ret < 0 ? 0 : ret;
		} else {
			ret = -EINVAL;
		}
		nvkm_outp_release(outp, NVKM_OUTP_PRIV);
	}

	return ret;
}

static int
nvkm_uoutp_mthd_acquired(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case NVIF_OUTP_V0_RELEASE    : return nvkm_uoutp_mthd_release    (outp, argv, argc);
	case NVIF_OUTP_V0_INFOFRAME  : return nvkm_uoutp_mthd_infoframe  (outp, argv, argc);
	case NVIF_OUTP_V0_HDA_ELD    : return nvkm_uoutp_mthd_hda_eld    (outp, argv, argc);
	case NVIF_OUTP_V0_DP_RETRAIN : return nvkm_uoutp_mthd_dp_retrain (outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_VCPI: return nvkm_uoutp_mthd_dp_mst_vcpi(outp, argv, argc);
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_uoutp_mthd_noacquire(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case NVIF_OUTP_V0_LOAD_DETECT: return nvkm_uoutp_mthd_load_detect(outp, argv, argc);
	case NVIF_OUTP_V0_ACQUIRE    : return nvkm_uoutp_mthd_acquire    (outp, argv, argc);
	case NVIF_OUTP_V0_DP_AUX_PWR : return nvkm_uoutp_mthd_dp_aux_pwr (outp, argv, argc);
	default:
		break;
	}

	return 1;
}

static int
nvkm_uoutp_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_outp *outp = nvkm_uoutp(object);
	struct nvkm_disp *disp = outp->disp;
	int ret;

	mutex_lock(&disp->super.mutex);

	ret = nvkm_uoutp_mthd_noacquire(outp, mthd, argv, argc);
	if (ret <= 0)
		goto done;

	if (outp->ior)
		ret = nvkm_uoutp_mthd_acquired(outp, mthd, argv, argc);
	else
		ret = -EIO;

done:
	mutex_unlock(&disp->super.mutex);
	return ret;
}

static void *
nvkm_uoutp_dtor(struct nvkm_object *object)
{
	struct nvkm_outp *outp = nvkm_uoutp(object);
	struct nvkm_disp *disp = outp->disp;

	spin_lock(&disp->client.lock);
	outp->object.func = NULL;
	spin_unlock(&disp->client.lock);
	return NULL;
}

static const struct nvkm_object_func
nvkm_uoutp = {
	.dtor = nvkm_uoutp_dtor,
	.mthd = nvkm_uoutp_mthd,
};

int
nvkm_uoutp_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);
	struct nvkm_outp *outt, *outp = NULL;
	union nvif_outp_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	list_for_each_entry(outt, &disp->outps, head) {
		if (outt->index == args->v0.id) {
			outp = outt;
			break;
		}
	}

	if (!outp)
		return -EINVAL;

	ret = -EBUSY;
	spin_lock(&disp->client.lock);
	if (!outp->object.func) {
		nvkm_object_ctor(&nvkm_uoutp, oclass, &outp->object);
		*pobject = &outp->object;
		ret = 0;
	}
	spin_unlock(&disp->client.lock);
	return ret;
}
