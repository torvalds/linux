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

#include <subdev/i2c.h>

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
nvkm_uoutp_mthd_dp_mst_id_put(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_mst_id_put_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
	        return -ENOSYS;
	if (!outp->func->dp.mst_id_put)
	        return -EINVAL;

	return outp->func->dp.mst_id_put(outp, args->v0.id);
}

static int
nvkm_uoutp_mthd_dp_mst_id_get(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_mst_id_get_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
	        return -ENOSYS;
	if (!outp->func->dp.mst_id_get)
	        return -EINVAL;

	return outp->func->dp.mst_id_get(outp, &args->v0.id);
}

static int
nvkm_uoutp_mthd_dp_sst(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_sst_args *args = argv;
	struct nvkm_disp *disp = outp->disp;
	struct nvkm_ior *ior = outp->ior;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	if (!ior->func->dp || !nvkm_head_find(disp, args->v0.head))
		return -EINVAL;
	if (!ior->func->dp->sst)
		return 0;

	return ior->func->dp->sst(ior, args->v0.head,
				  outp->dp.dpcd[DPCD_RC02] & DPCD_RC02_ENHANCED_FRAME_CAP,
				  args->v0.watermark, args->v0.hblanksym, args->v0.vblanksym);
}

static int
nvkm_uoutp_mthd_dp_drive(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_drive_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->dp.drive)
		return -EINVAL;

	return outp->func->dp.drive(outp, args->v0.lanes, args->v0.pe, args->v0.vs);
}

static int
nvkm_uoutp_mthd_dp_train(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_train_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->dp.train)
		return -EINVAL;

	if (!args->v0.retrain) {
		memcpy(outp->dp.dpcd, args->v0.dpcd, sizeof(outp->dp.dpcd));
		outp->dp.lttprs = args->v0.lttprs;
		outp->dp.lt.nr = args->v0.link_nr;
		outp->dp.lt.bw = args->v0.link_bw / 27000;
		outp->dp.lt.mst = args->v0.mst;
		outp->dp.lt.post_adj = args->v0.post_lt_adj;
	}

	return outp->func->dp.train(outp, args->v0.retrain);
}

static int
nvkm_uoutp_mthd_dp_rates(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_rates_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (args->v0.rates > ARRAY_SIZE(outp->dp.rate))
		return -EINVAL;

	for (int i = 0; i < args->v0.rates; i++) {
		outp->dp.rate[i].dpcd = args->v0.rate[i].dpcd;
		outp->dp.rate[i].rate = args->v0.rate[i].rate;
	}

	outp->dp.rates = args->v0.rates;

	if (outp->func->dp.rates)
		outp->func->dp.rates(outp);

	return 0;
}

static int
nvkm_uoutp_mthd_dp_aux_xfer(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_aux_xfer_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->dp.aux_xfer)
		return -EINVAL;

	return outp->func->dp.aux_xfer(outp, args->v0.type, args->v0.addr,
					     args->v0.data, &args->v0.size);
}

static int
nvkm_uoutp_mthd_dp_aux_pwr(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_dp_aux_pwr_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->dp.aux_pwr)
		return -EINVAL;

	return outp->func->dp.aux_pwr(outp, !!args->v0.state);
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
		else
		if (ior->func->hdmi->audio)
			ior->func->hdmi->audio(ior, args->v0.head, true);

		ior->func->hda->hpd(ior, args->v0.head, true);
		ior->func->hda->eld(ior, args->v0.head, args->v0.data, argc);
	} else {
		ior->func->hda->hpd(ior, args->v0.head, false);

		if (outp->info.type == DCB_OUTPUT_DP)
			ior->func->dp->audio(ior, args->v0.head, false);
		else
		if (ior->func->hdmi->audio)
			ior->func->hdmi->audio(ior, args->v0.head, false);
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
nvkm_uoutp_mthd_hdmi(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_hdmi_args *args = argv;
	struct nvkm_ior *ior = outp->ior;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	if (!(outp->asy.head = nvkm_head_find(outp->disp, args->v0.head)))
		return -EINVAL;

	if (!ior->func->hdmi ||
	    args->v0.max_ac_packet > 0x1f ||
	    args->v0.rekey > 0x7f ||
	    (args->v0.scdc && !ior->func->hdmi->scdc))
		return -EINVAL;

	if (!args->v0.enable) {
		ior->func->hdmi->infoframe_avi(ior, args->v0.head, NULL, 0);
		ior->func->hdmi->infoframe_vsi(ior, args->v0.head, NULL, 0);
		ior->func->hdmi->ctrl(ior, args->v0.head, false, 0, 0);
		return 0;
	}

	ior->func->hdmi->ctrl(ior, args->v0.head, args->v0.enable,
			      args->v0.max_ac_packet, args->v0.rekey);
	if (ior->func->hdmi->scdc)
		ior->func->hdmi->scdc(ior, args->v0.khz, args->v0.scdc, args->v0.scdc_scrambling,
				      args->v0.scdc_low_rates);

	return 0;
}

static int
nvkm_uoutp_mthd_lvds(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_lvds_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (outp->info.type != DCB_OUTPUT_LVDS)
		return -EINVAL;

	outp->lvds.dual = !!args->v0.dual;
	outp->lvds.bpc8 = !!args->v0.bpc8;
	return 0;
}

static int
nvkm_uoutp_mthd_bl_set(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_bl_get_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	if (outp->func->bl.set)
		ret = outp->func->bl.set(outp, args->v0.level);
	else
		ret = -EINVAL;

	return ret;
}

static int
nvkm_uoutp_mthd_bl_get(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_bl_get_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	if (outp->func->bl.get) {
		ret = outp->func->bl.get(outp);
		if (ret >= 0) {
			args->v0.level = ret;
			ret = 0;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int
nvkm_uoutp_mthd_release(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_release_args *args = argv;

	if (argc != sizeof(args->vn))
		return -ENOSYS;
	if (!outp->ior)
		return -EINVAL;

	outp->func->release(outp);
	return 0;
}

static int
nvkm_uoutp_mthd_acquire(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_acquire_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (outp->ior && args->v0.type <= NVIF_OUTP_ACQUIRE_V0_PIOR)
		return -EBUSY;

	switch (args->v0.type) {
	case NVIF_OUTP_ACQUIRE_V0_DAC:
	case NVIF_OUTP_ACQUIRE_V0_PIOR:
		ret = outp->func->acquire(outp, false);
		break;
	case NVIF_OUTP_ACQUIRE_V0_SOR:
		ret = outp->func->acquire(outp, args->v0.sor.hda);
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
nvkm_uoutp_mthd_inherit(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_inherit_args *args = argv;
	struct nvkm_ior *ior;
	int ret = 0;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	/* Ensure an ior is hooked up to this outp already */
	ior = outp->func->inherit(outp);
	if (!ior)
		return -ENODEV;

	/* With iors, there will be a separate output path for each type of connector - and all of
	 * them will appear to be hooked up. Figure out which one is actually the one we're using
	 * based on the protocol we were given over nvif
	 */
	switch (args->v0.proto) {
	case NVIF_OUTP_INHERIT_V0_TMDS:
		if (ior->arm.proto != TMDS)
			return -ENODEV;
		break;
	case NVIF_OUTP_INHERIT_V0_DP:
		if (ior->arm.proto != DP)
			return -ENODEV;
		break;
	case NVIF_OUTP_INHERIT_V0_LVDS:
		if (ior->arm.proto != LVDS)
			return -ENODEV;
		break;
	case NVIF_OUTP_INHERIT_V0_TV:
		if (ior->arm.proto != TV)
			return -ENODEV;
		break;
	case NVIF_OUTP_INHERIT_V0_RGB_CRT:
		if (ior->arm.proto != CRT)
			return -ENODEV;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	/* Make sure that userspace hasn't already acquired this */
	if (outp->acquired) {
		OUTP_ERR(outp, "cannot inherit an already acquired (%02x) outp", outp->acquired);
		return -EBUSY;
	}

	/* Mark the outp acquired by userspace now that we've confirmed it's already active */
	OUTP_TRACE(outp, "inherit %02x |= %02x %p", outp->acquired, NVKM_OUTP_USER, ior);
	nvkm_outp_acquire_ior(outp, NVKM_OUTP_USER, ior);

	args->v0.or = ior->id;
	args->v0.link = ior->arm.link;
	args->v0.head = ffs(ior->arm.head) - 1;
	args->v0.proto = ior->arm.proto_evo;

	return ret;
}

static int
nvkm_uoutp_mthd_load_detect(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_load_detect_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	ret = nvkm_outp_acquire_or(outp, NVKM_OUTP_PRIV, false);
	if (ret == 0) {
		if (outp->ior->func->sense) {
			ret = outp->ior->func->sense(outp->ior, args->v0.data);
			args->v0.load = ret < 0 ? 0 : ret;
		} else {
			ret = -EINVAL;
		}
		nvkm_outp_release_or(outp, NVKM_OUTP_PRIV);
	}

	return ret;
}

static int
nvkm_uoutp_mthd_edid_get(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_edid_get_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->edid_get)
		return -EINVAL;

	args->v0.size = ARRAY_SIZE(args->v0.data);
	return outp->func->edid_get(outp, args->v0.data, &args->v0.size);
}

static int
nvkm_uoutp_mthd_detect(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_detect_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;
	if (!outp->func->detect)
		return -EINVAL;

	ret = outp->func->detect(outp);
	switch (ret) {
	case 0: args->v0.status = NVIF_OUTP_DETECT_V0_NOT_PRESENT; break;
	case 1: args->v0.status = NVIF_OUTP_DETECT_V0_PRESENT; break;
	default:
		args->v0.status = NVIF_OUTP_DETECT_V0_UNKNOWN;
		break;
	}

	return 0;
}

static int
nvkm_uoutp_mthd_acquired(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case NVIF_OUTP_V0_RELEASE      : return nvkm_uoutp_mthd_release      (outp, argv, argc);
	case NVIF_OUTP_V0_LVDS         : return nvkm_uoutp_mthd_lvds         (outp, argv, argc);
	case NVIF_OUTP_V0_HDMI         : return nvkm_uoutp_mthd_hdmi         (outp, argv, argc);
	case NVIF_OUTP_V0_INFOFRAME    : return nvkm_uoutp_mthd_infoframe    (outp, argv, argc);
	case NVIF_OUTP_V0_HDA_ELD      : return nvkm_uoutp_mthd_hda_eld      (outp, argv, argc);
	case NVIF_OUTP_V0_DP_TRAIN     : return nvkm_uoutp_mthd_dp_train     (outp, argv, argc);
	case NVIF_OUTP_V0_DP_DRIVE     : return nvkm_uoutp_mthd_dp_drive     (outp, argv, argc);
	case NVIF_OUTP_V0_DP_SST       : return nvkm_uoutp_mthd_dp_sst       (outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_ID_GET: return nvkm_uoutp_mthd_dp_mst_id_get(outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_ID_PUT: return nvkm_uoutp_mthd_dp_mst_id_put(outp, argv, argc);
	case NVIF_OUTP_V0_DP_MST_VCPI  : return nvkm_uoutp_mthd_dp_mst_vcpi  (outp, argv, argc);
	default:
		break;
	}

	return -EINVAL;
}

static int
nvkm_uoutp_mthd_noacquire(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc, bool *invalid)
{
	switch (mthd) {
	case NVIF_OUTP_V0_DETECT     : return nvkm_uoutp_mthd_detect     (outp, argv, argc);
	case NVIF_OUTP_V0_EDID_GET   : return nvkm_uoutp_mthd_edid_get   (outp, argv, argc);
	case NVIF_OUTP_V0_INHERIT    : return nvkm_uoutp_mthd_inherit    (outp, argv, argc);
	case NVIF_OUTP_V0_ACQUIRE    : return nvkm_uoutp_mthd_acquire    (outp, argv, argc);
	case NVIF_OUTP_V0_LOAD_DETECT: return nvkm_uoutp_mthd_load_detect(outp, argv, argc);
	case NVIF_OUTP_V0_BL_GET     : return nvkm_uoutp_mthd_bl_get     (outp, argv, argc);
	case NVIF_OUTP_V0_BL_SET     : return nvkm_uoutp_mthd_bl_set     (outp, argv, argc);
	case NVIF_OUTP_V0_DP_AUX_PWR : return nvkm_uoutp_mthd_dp_aux_pwr (outp, argv, argc);
	case NVIF_OUTP_V0_DP_AUX_XFER: return nvkm_uoutp_mthd_dp_aux_xfer(outp, argv, argc);
	case NVIF_OUTP_V0_DP_RATES   : return nvkm_uoutp_mthd_dp_rates   (outp, argv, argc);
	default:
		break;
	}

	*invalid = true;
	return 0;
}

static int
nvkm_uoutp_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_outp *outp = nvkm_uoutp(object);
	struct nvkm_disp *disp = outp->disp;
	bool invalid = false;
	int ret;

	mutex_lock(&disp->super.mutex);

	ret = nvkm_uoutp_mthd_noacquire(outp, mthd, argv, argc, &invalid);
	if (!invalid)
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
		switch (outp->info.type) {
		case DCB_OUTPUT_ANALOG:
			args->v0.type = NVIF_OUTP_V0_TYPE_DAC;
			args->v0.proto = NVIF_OUTP_V0_PROTO_RGB_CRT;
			args->v0.rgb_crt.freq_max = outp->info.crtconf.maxfreq;
			break;
		case DCB_OUTPUT_TMDS:
			if (!outp->info.location) {
				args->v0.type = NVIF_OUTP_V0_TYPE_SOR;
				args->v0.tmds.dual = (outp->info.tmdsconf.sor.link == 3);
			} else {
				args->v0.type = NVIF_OUTP_V0_TYPE_PIOR;
				args->v0.tmds.dual = 0;
			}
			args->v0.proto = NVIF_OUTP_V0_PROTO_TMDS;
			break;
		case DCB_OUTPUT_LVDS:
			args->v0.type = NVIF_OUTP_V0_TYPE_SOR;
			args->v0.proto = NVIF_OUTP_V0_PROTO_LVDS;
			args->v0.lvds.acpi_edid = outp->info.lvdsconf.use_acpi_for_edid;
			break;
		case DCB_OUTPUT_DP:
			if (!outp->info.location) {
				args->v0.type = NVIF_OUTP_V0_TYPE_SOR;
				args->v0.dp.aux = outp->info.i2c_index;
			} else {
				args->v0.type = NVIF_OUTP_V0_TYPE_PIOR;
				args->v0.dp.aux = NVKM_I2C_AUX_EXT(outp->info.extdev);
			}
			args->v0.proto = NVIF_OUTP_V0_PROTO_DP;
			args->v0.dp.mst = outp->dp.mst;
			args->v0.dp.increased_wm = outp->dp.increased_wm;
			args->v0.dp.link_nr = outp->info.dpconf.link_nr;
			args->v0.dp.link_bw = outp->info.dpconf.link_bw * 27000;
			break;
		default:
			WARN_ON(1);
			ret = -EINVAL;
			goto done;
		}

		if (outp->info.location)
			args->v0.ddc = NVKM_I2C_BUS_EXT(outp->info.extdev);
		else
			args->v0.ddc = outp->info.i2c_index;
		args->v0.heads = outp->info.heads;
		args->v0.conn = outp->info.connector;

		nvkm_object_ctor(&nvkm_uoutp, oclass, &outp->object);
		*pobject = &outp->object;
		ret = 0;
	}

done:
	spin_unlock(&disp->client.lock);
	return ret;
}
