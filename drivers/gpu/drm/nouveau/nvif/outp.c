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
#include <nvif/outp.h>
#include <nvif/disp.h>
#include <nvif/printf.h>

#include <nvif/class.h>

int
nvif_outp_dp_mst_vcpi(struct nvif_outp *outp, int head,
		      u8 start_slot, u8 num_slots, u16 pbn, u16 aligned_pbn)
{
	struct nvif_outp_dp_mst_vcpi_v0 args;
	int ret;

	args.version = 0;
	args.head = head;
	args.start_slot = start_slot;
	args.num_slots = num_slots;
	args.pbn = pbn;
	args.aligned_pbn = aligned_pbn;

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_MST_VCPI, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object,
		   "[DP_MST_VCPI head:%d start_slot:%02x num_slots:%02x pbn:%04x aligned_pbn:%04x]",
		   args.head, args.start_slot, args.num_slots, args.pbn, args.aligned_pbn);
	return ret;
}

int
nvif_outp_dp_mst_id_put(struct nvif_outp *outp, u32 id)
{
	struct nvif_outp_dp_mst_id_get_v0 args;
	int ret;

	args.version = 0;
	args.id = id;
	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_MST_ID_PUT, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DP_MST_ID_PUT id:%08x]", args.id);
	return ret;
}

int
nvif_outp_dp_mst_id_get(struct nvif_outp *outp, u32 *id)
{
	struct nvif_outp_dp_mst_id_get_v0 args;
	int ret;

	args.version = 0;
	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_MST_ID_GET, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DP_MST_ID_GET] id:%08x", args.id);
	if (ret)
		return ret;

	*id = args.id;
	return 0;
}

int
nvif_outp_dp_sst(struct nvif_outp *outp, int head, u32 watermark, u32 hblanksym, u32 vblanksym)
{
	struct nvif_outp_dp_sst_v0 args;
	int ret;

	args.version = 0;
	args.head = head;
	args.watermark = watermark;
	args.hblanksym = hblanksym;
	args.vblanksym = vblanksym;
	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_SST, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object,
		   "[DP_SST head:%d watermark:%d hblanksym:%d vblanksym:%d]",
		   args.head, args.watermark, args.hblanksym, args.vblanksym);
	return ret;
}

int
nvif_outp_dp_drive(struct nvif_outp *outp, u8 link_nr, u8 pe[4], u8 vs[4])
{
	struct nvif_outp_dp_drive_v0 args;
	int ret;

	args.version = 0;
	args.lanes   = link_nr;
	memcpy(args.pe, pe, sizeof(args.pe));
	memcpy(args.vs, vs, sizeof(args.vs));

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_DRIVE, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DP_DRIVE lanes:%d]", args.lanes);
	return ret;
}

int
nvif_outp_dp_train(struct nvif_outp *outp, u8 dpcd[DP_RECEIVER_CAP_SIZE], u8 lttprs,
		   u8 link_nr, u32 link_bw, bool mst, bool post_lt_adj, bool retrain)
{
	struct nvif_outp_dp_train_v0 args;
	int ret;

	args.version = 0;
	args.retrain = retrain;
	args.mst = mst;
	args.lttprs = lttprs;
	args.post_lt_adj = post_lt_adj;
	args.link_nr = link_nr;
	args.link_bw = link_bw;
	memcpy(args.dpcd, dpcd, sizeof(args.dpcd));

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_TRAIN, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object,
		   "[DP_TRAIN retrain:%d mst:%d lttprs:%d post_lt_adj:%d nr:%d bw:%d]",
		   args.retrain, args.mst, args.lttprs, args.post_lt_adj, args.link_nr,
		   args.link_bw);
	return ret;
}

int
nvif_outp_dp_rates(struct nvif_outp *outp, struct nvif_outp_dp_rate *rate, int rate_nr)
{
	struct nvif_outp_dp_rates_v0 args;
	int ret;

	if (rate_nr > ARRAY_SIZE(args.rate))
		return -EINVAL;

	args.version = 0;
	args.rates = rate_nr;
	for (int i = 0; i < args.rates; i++, rate++) {
		args.rate[i].dpcd = rate->dpcd;
		args.rate[i].rate = rate->rate;
	}

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_RATES, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DP_RATES rates:%d]", args.rates);
	return ret;
}

int
nvif_outp_dp_aux_xfer(struct nvif_outp *outp, u8 type, u8 *psize, u32 addr, u8 *data)
{
	struct nvif_outp_dp_aux_xfer_v0 args;
	u8 size = *psize;
	int ret;

	args.version = 0;
	args.type = type;
	args.size = size;
	args.addr = addr;
	memcpy(args.data, data, size);
	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_AUX_XFER, &args, sizeof(args));
	NVIF_DEBUG(&outp->object, "[DP_AUX_XFER type:%d size:%d addr:%05x] %d size:%d (ret: %d)",
		   args.type, size, args.addr, ret, args.size, ret);
	if (ret < 0)
		return ret;

	*psize = args.size;

	memcpy(data, args.data, size);
	return ret;
}

int
nvif_outp_dp_aux_pwr(struct nvif_outp *outp, bool enable)
{
	struct nvif_outp_dp_aux_pwr_v0 args;
	int ret;

	args.version = 0;
	args.state = enable;

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_AUX_PWR, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DP_AUX_PWR state:%d]", args.state);
	return ret;
}

int
nvif_outp_hda_eld(struct nvif_outp *outp, int head, void *data, u32 size)
{
	DEFINE_RAW_FLEX(struct nvif_outp_hda_eld_v0, mthd, data, 128);
	int ret;

	if (WARN_ON(size > __member_size(mthd->data)))
		return -EINVAL;

	mthd->version = 0;
	mthd->head = head;

	memcpy(mthd->data, data, size);
	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_HDA_ELD, mthd, sizeof(*mthd) + size);
	NVIF_ERRON(ret, &outp->object, "[HDA_ELD head:%d size:%d]", head, size);
	return ret;
}

int
nvif_outp_infoframe(struct nvif_outp *outp, u8 type, struct nvif_outp_infoframe_v0 *args, u32 size)
{
	int ret;

	args->type = type;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_INFOFRAME, args, sizeof(*args) + size);
	NVIF_ERRON(ret, &outp->object, "[INFOFRAME type:%d size:%d]", type, size);
	return ret;
}

int
nvif_outp_hdmi(struct nvif_outp *outp, int head, bool enable, u8 max_ac_packet, u8 rekey,
	       u32 khz, bool scdc, bool scdc_scrambling, bool scdc_low_rates)
{
	struct nvif_outp_hdmi_v0 args;
	int ret;

	args.version = 0;
	args.head = head;
	args.enable = enable;
	args.max_ac_packet = max_ac_packet;
	args.rekey = rekey;
	args.khz = khz;
	args.scdc = scdc;
	args.scdc_scrambling = scdc_scrambling;
	args.scdc_low_rates = scdc_low_rates;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_HDMI, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object,
		   "[HDMI head:%d enable:%d max_ac_packet:%d rekey:%d khz:%d scdc:%d "
		   "scdc_scrambling:%d scdc_low_rates:%d]",
		   args.head, args.enable, args.max_ac_packet, args.rekey, args.khz,
		   args.scdc, args.scdc_scrambling, args.scdc_low_rates);
	return ret;
}

int
nvif_outp_lvds(struct nvif_outp *outp, bool dual, bool bpc8)
{
	struct nvif_outp_lvds_v0 args;
	int ret;

	args.version = 0;
	args.dual = dual;
	args.bpc8 = bpc8;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_LVDS, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[LVDS dual:%d 8bpc:%d]", args.dual, args.bpc8);
	return ret;
}

int
nvif_outp_bl_set(struct nvif_outp *outp, int level)
{
	struct nvif_outp_bl_set_v0 args;
	int ret;

	args.version = 0;
	args.level = level;

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_BL_SET, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[BL_SET level:%d]", args.level);
	return ret;
}

int
nvif_outp_bl_get(struct nvif_outp *outp)
{
	struct nvif_outp_bl_get_v0 args;
	int ret;

	args.version = 0;

	ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_BL_GET, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[BL_GET level:%d]", args.level);
	return ret ? ret : args.level;
}

void
nvif_outp_release(struct nvif_outp *outp)
{
	int ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_RELEASE, NULL, 0);
	NVIF_ERRON(ret, &outp->object, "[RELEASE]");
	outp->or.id = -1;
}

static inline int
nvif_outp_acquire(struct nvif_outp *outp, u8 type, struct nvif_outp_acquire_v0 *args)
{
	int ret;

	args->version = 0;
	args->type = type;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_ACQUIRE, args, sizeof(*args));
	if (ret)
		return ret;

	outp->or.id = args->or;
	outp->or.link = args->link;
	return 0;
}

int
nvif_outp_acquire_pior(struct nvif_outp *outp)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_PIOR, &args);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE PIOR] or:%d", args.or);
	return ret;
}

int
nvif_outp_acquire_sor(struct nvif_outp *outp, bool hda)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	args.sor.hda = hda;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_SOR, &args);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE SOR] or:%d link:%d", args.or, args.link);
	return ret;
}

int
nvif_outp_acquire_dac(struct nvif_outp *outp)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_DAC, &args);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE DAC] or:%d", args.or);
	return ret;
}

static int
nvif_outp_inherit(struct nvif_outp *outp,
		  u8 proto,
		  struct nvif_outp_inherit_v0 *args,
		  u8 *proto_out)
{
	int ret;

	args->version = 0;
	args->proto = proto;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_INHERIT, args, sizeof(*args));
	if (ret)
		return ret;

	outp->or.id = args->or;
	outp->or.link = args->link;
	*proto_out = args->proto;
	return 0;
}

int
nvif_outp_inherit_lvds(struct nvif_outp *outp, u8 *proto_out)
{
	struct nvif_outp_inherit_v0 args;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_INHERIT_V0_LVDS, &args, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:LVDS] ret:%d", ret);
	return ret ?: args.head;
}

int
nvif_outp_inherit_tmds(struct nvif_outp *outp, u8 *proto_out)
{
	struct nvif_outp_inherit_v0 args;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_INHERIT_V0_TMDS, &args, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:TMDS] ret:%d", ret);
	return ret ?: args.head;
}

int
nvif_outp_inherit_dp(struct nvif_outp *outp, u8 *proto_out)
{
	struct nvif_outp_inherit_v0 args;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_INHERIT_V0_DP, &args, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:DP] ret:%d", ret);

	// TODO: Get current link info

	return ret ?: args.head;
}

int
nvif_outp_inherit_rgb_crt(struct nvif_outp *outp, u8 *proto_out)
{
	struct nvif_outp_inherit_v0 args;
	int ret;

	ret = nvif_outp_inherit(outp, NVIF_OUTP_INHERIT_V0_RGB_CRT, &args, proto_out);
	NVIF_ERRON(ret && ret != -ENODEV, &outp->object, "[INHERIT proto:RGB_CRT] ret:%d", ret);
	return ret ?: args.head;
}

int
nvif_outp_load_detect(struct nvif_outp *outp, u32 loadval)
{
	struct nvif_outp_load_detect_v0 args;
	int ret;

	args.version = 0;
	args.data = loadval;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_LOAD_DETECT, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[LOAD_DETECT data:%08x] load:%02x", args.data, args.load);
	return ret < 0 ? ret : args.load;
}

int
nvif_outp_edid_get(struct nvif_outp *outp, u8 **pedid)
{
	struct nvif_outp_edid_get_v0 *args;
	int ret;

	args = kmalloc(sizeof(*args), GFP_KERNEL);
	if (!args)
		return -ENOMEM;

	args->version = 0;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_EDID_GET, args, sizeof(*args));
	NVIF_ERRON(ret, &outp->object, "[EDID_GET] size:%d", args->size);
	if (ret)
		goto done;

	*pedid = kmemdup(args->data, args->size, GFP_KERNEL);
	if (!*pedid) {
		ret = -ENOMEM;
		goto done;
	}

	ret = args->size;
done:
	kfree(args);
	return ret;
}

enum nvif_outp_detect_status
nvif_outp_detect(struct nvif_outp *outp)
{
	struct nvif_outp_detect_v0 args;
	int ret;

	args.version = 0;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_DETECT, &args, sizeof(args));
	NVIF_ERRON(ret, &outp->object, "[DETECT] status:%02x", args.status);
	if (ret)
		return UNKNOWN;

	switch (args.status) {
	case NVIF_OUTP_DETECT_V0_NOT_PRESENT: return NOT_PRESENT;
	case NVIF_OUTP_DETECT_V0_PRESENT: return PRESENT;
	case NVIF_OUTP_DETECT_V0_UNKNOWN: return UNKNOWN;
	default:
		WARN_ON(1);
		break;
	}

	return UNKNOWN;
}

void
nvif_outp_dtor(struct nvif_outp *outp)
{
	nvif_object_dtor(&outp->object);
}

int
nvif_outp_ctor(struct nvif_disp *disp, const char *name, int id, struct nvif_outp *outp)
{
	struct nvif_outp_v0 args;
	int ret;

	args.version = 0;
	args.id = id;

	ret = nvif_object_ctor(&disp->object, name ?: "nvifOutp", id, NVIF_CLASS_OUTP,
			       &args, sizeof(args), &outp->object);
	NVIF_ERRON(ret, &disp->object, "[NEW outp id:%d]", id);
	if (ret)
		return ret;

	outp->id = args.id;

	switch (args.type) {
	case NVIF_OUTP_V0_TYPE_DAC : outp->info.type = NVIF_OUTP_DAC; break;
	case NVIF_OUTP_V0_TYPE_SOR : outp->info.type = NVIF_OUTP_SOR; break;
	case NVIF_OUTP_V0_TYPE_PIOR: outp->info.type = NVIF_OUTP_PIOR; break;
		break;
	default:
		WARN_ON(1);
		nvif_outp_dtor(outp);
		return -EINVAL;
	}

	switch (args.proto) {
	case NVIF_OUTP_V0_PROTO_RGB_CRT:
		outp->info.proto = NVIF_OUTP_RGB_CRT;
		outp->info.rgb_crt.freq_max = args.rgb_crt.freq_max;
		break;
	case NVIF_OUTP_V0_PROTO_TMDS:
		outp->info.proto = NVIF_OUTP_TMDS;
		outp->info.tmds.dual = args.tmds.dual;
		break;
	case NVIF_OUTP_V0_PROTO_LVDS:
		outp->info.proto = NVIF_OUTP_LVDS;
		outp->info.lvds.acpi_edid = args.lvds.acpi_edid;
		break;
	case NVIF_OUTP_V0_PROTO_DP:
		outp->info.proto = NVIF_OUTP_DP;
		outp->info.dp.aux = args.dp.aux;
		outp->info.dp.mst = args.dp.mst;
		outp->info.dp.increased_wm = args.dp.increased_wm;
		outp->info.dp.link_nr = args.dp.link_nr;
		outp->info.dp.link_bw = args.dp.link_bw;
		break;
	default:
		WARN_ON(1);
		nvif_outp_dtor(outp);
		return -EINVAL;
	}

	outp->info.heads = args.heads;
	outp->info.ddc = args.ddc;
	outp->info.conn = args.conn;

	outp->or.id = -1;
	return 0;
}
