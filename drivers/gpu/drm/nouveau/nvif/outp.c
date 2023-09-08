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
nvif_outp_dp_retrain(struct nvif_outp *outp)
{
	int ret = nvif_object_mthd(&outp->object, NVIF_OUTP_V0_DP_RETRAIN, NULL, 0);
	NVIF_ERRON(ret, &outp->object, "[DP_RETRAIN]");
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
	struct {
		struct nvif_outp_hda_eld_v0 mthd;
		u8 data[128];
	} args;
	int ret;

	if (WARN_ON(size > ARRAY_SIZE(args.data)))
		return -EINVAL;

	args.mthd.version = 0;
	args.mthd.head = head;

	memcpy(args.data, data, size);
	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_HDA_ELD, &args, sizeof(args.mthd) + size);
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

void
nvif_outp_release(struct nvif_outp *outp)
{
	int ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_RELEASE, NULL, 0);
	NVIF_ERRON(ret, &outp->object, "[RELEASE]");
	outp->or.id = -1;
}

static inline int
nvif_outp_acquire(struct nvif_outp *outp, u8 proto, struct nvif_outp_acquire_v0 *args)
{
	int ret;

	args->version = 0;
	args->proto = proto;

	ret = nvif_mthd(&outp->object, NVIF_OUTP_V0_ACQUIRE, args, sizeof(*args));
	if (ret)
		return ret;

	outp->or.id = args->or;
	outp->or.link = args->link;
	return 0;
}

int
nvif_outp_acquire_dp(struct nvif_outp *outp, u8 dpcd[DP_RECEIVER_CAP_SIZE],
		     int link_nr, int link_bw, bool hda, bool mst)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	args.dp.link_nr = link_nr;
	args.dp.link_bw = link_bw;
	args.dp.hda = hda;
	args.dp.mst = mst;
	memcpy(args.dp.dpcd, dpcd, sizeof(args.dp.dpcd));

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_DP, &args);
	NVIF_ERRON(ret, &outp->object,
		   "[ACQUIRE proto:DP link_nr:%d link_bw:%02x hda:%d mst:%d] or:%d link:%d",
		   args.dp.link_nr, args.dp.link_bw, args.dp.hda, args.dp.mst, args.or, args.link);
	return ret;
}

int
nvif_outp_acquire_lvds(struct nvif_outp *outp, bool dual, bool bpc8)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	args.lvds.dual = dual;
	args.lvds.bpc8 = bpc8;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_LVDS, &args);
	NVIF_ERRON(ret, &outp->object,
		   "[ACQUIRE proto:LVDS dual:%d 8bpc:%d] or:%d link:%d",
		   args.lvds.dual, args.lvds.bpc8, args.or, args.link);
	return ret;
}

int
nvif_outp_acquire_tmds(struct nvif_outp *outp, int head,
		       bool hdmi, u8 max_ac_packet, u8 rekey, u8 scdc, bool hda)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	args.tmds.head = head;
	args.tmds.hdmi = hdmi;
	args.tmds.hdmi_max_ac_packet = max_ac_packet;
	args.tmds.hdmi_rekey = rekey;
	args.tmds.hdmi_scdc = scdc;
	args.tmds.hdmi_hda = hda;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_TMDS, &args);
	NVIF_ERRON(ret, &outp->object,
		   "[ACQUIRE proto:TMDS head:%d hdmi:%d max_ac_packet:%d rekey:%d scdc:%d hda:%d]"
		   " or:%d link:%d", args.tmds.head, args.tmds.hdmi, args.tmds.hdmi_max_ac_packet,
		   args.tmds.hdmi_rekey, args.tmds.hdmi_scdc, args.tmds.hdmi_hda,
		   args.or, args.link);
	return ret;
}

int
nvif_outp_acquire_rgb_crt(struct nvif_outp *outp)
{
	struct nvif_outp_acquire_v0 args;
	int ret;

	ret = nvif_outp_acquire(outp, NVIF_OUTP_ACQUIRE_V0_RGB_CRT, &args);
	NVIF_ERRON(ret, &outp->object, "[ACQUIRE proto:RGB_CRT] or:%d", args.or);
	return ret;
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

	outp->or.id = -1;
	return 0;
}
