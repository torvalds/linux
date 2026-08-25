// SPDX-License-Identifier: MIT
/*
 * Copyright 2026 Valve Corp.
 */
#include "priv.h"
#include "head.h"
#include "ior.h"

#include <subdev/timer.h>

/* GB20x (NVD5.0) reorganised the SF HDMI packet units. The AVI unit is
 * unchanged from GV100, but the legacy VSI unit is gone. Vendor infoframes
 * are sent through the shared generic infoframe units instead. Register
 * layout per NVIDIA's clc971.h/clca71.h, programming sequence per
 * nvhdmipkt_C971.c:programAdvancedInfoframeC971().
 */
static void
gb202_sor_hdmi_infoframe_vsi(struct nvkm_ior *ior, int head, void *data, u32 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 hoff = head * 0x400;
	/* Generic infoframe unit 1, the slot NVIDIA's driver uses for the VSI. */
	const u32 ctrl = 0x6f0138 + hoff;
	u8 buf[36] = {};
	int i;

	/* Disable the unit and wait for it to go idle. */
	nvkm_mask(device, ctrl, 0x00000001, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, ctrl) & 0x00400000))
			break;
	) < 0)
		return;

	if (!size)
		return;

	/* Clear SENT status, and point the data port at unit 1's slot. */
	nvkm_mask(device, ctrl, 0x00800000, 0x00800000);
	nvkm_wr32(device, 0x6f03f0 + hoff, 0x00000001);

	/* The data port takes the raw packet, except that a zero is inserted
	 * in HB3 after the three header bytes. A slot is 9 dwords (HB0-3 plus
	 * up to 32 payload bytes). An HDMI infoframe carries at most PB0-27,
	 * so the tail stays zero, and we always write the whole slot.
	 */
	size = min_t(u32, size, 31);
	memcpy(buf, data, min_t(u32, size, 3));
	if (size > 3)
		memcpy(&buf[4], (u8 *)data + 3, size - 3);

	for (i = 0; i < 36; i += 4) {
		nvkm_wr32(device, 0x6f03f4 + hoff, buf[i + 0] | buf[i + 1] << 8 |
						   buf[i + 2] << 16 |
						   (u32)buf[i + 3] << 24);
	}

	/* No flip ID or scanline matching. */
	nvkm_wr32(device, 0x6f013c + hoff, 0x00000000);

	/* ENABLE | RUN_MODE=ALWAYS | LOC=VBLANK | OFFSET=1 | SIZE=0. */
	nvkm_wr32(device, ctrl, 0x00000041);

	/* Audio priority low (the init value). */
	nvkm_wr32(device, 0x6f03f8 + hoff, 0x00000002);
}

/* General Control Packet AVMute bracket. The GCP unit moved to slot 1 on
 * NVD5.0. Only SB0 (the AVMute bit) is ours to write so we must not do a
 * full write here: SB1 carries the deep-color CD/PP fields, and SB1_CTRL
 * (bit 24, new with clc871.h) controls where their generation happens (HW
 * or driver) on these chips, with the default being HW.
 */
static void
gb202_sor_hdmi_gcp(struct nvkm_ior *sor, int head, bool enable)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hdmi = head * 0x400;

	nvkm_mask(device, 0x6f0040 + hdmi, 0x00000001, 0x00000000);
	nvkm_mask(device, 0x6f004c + hdmi, 0x000000ff, !enable ? 0x00000001 :
								 0x00000010);
	nvkm_mask(device, 0x6f0040 + hdmi, 0x00000001, 0x00000001);
}

/* Same core-channel state mirror as gv100_head_state() (assembly at 0x680000,
 * armed at +0x8000, per-head method offsets unchanged), but NVD5.0 spaces
 * heads 0x800 apart (see NVCA7D_HEAD_SET_*(a) in clca7d.h).
 */
static void
gb202_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = (state == &head->arm) * 0x8000 + head->id * 0x800;
	u32 data;

	data = nvkm_rd32(device, 0x682064 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682068 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x68206c + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682070 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	/* Bit 31 is ADJ1000DIV1001, not a HERTZ bit. We don't have enough bits
	 * to add the full clock in hz on Blackwell (35 bits), but state->hz
	 * is unused and obsolete under GSP so this is fine.
	 */
	state->hz = nvkm_rd32(device, 0x68200c + hoff) & 0x7fffffff;

	data = nvkm_rd32(device, 0x682004 + hoff);
	switch ((data & 0x000000f0) >> 4) {
	case 5: state->or.depth = 30; break;
	case 4: state->or.depth = 24; break;
	case 1: state->or.depth = 18; break;
	default:
		state->or.depth = 18;
		WARN_ON(1);
		break;
	}
}

static const struct nvkm_head_func
gb202_gsp_head = {
	.state = gb202_head_state,
	.rgpos = gv100_head_rgpos,
	.vblank_get = tu102_head_vblank_get,
	.vblank_put = tu102_head_vblank_put,
};

/* GB20x is GSP-only. This table supplies the register programming the
 * GSP-RM display path needs from the chip.
 */
static const struct nvkm_disp_func
gb202_gsp_disp = {
	.uevent = &gv100_disp_chan_uevent,
	.ramht_size = 0x2000,
	.gsp.intr = tu102_disp_intr,
	.gsp.head = &gb202_gsp_head,
	.gsp.hdmi_gcp = gb202_sor_hdmi_gcp,
	/* The legacy AVI unit is unchanged on GB20x. */
	.gsp.hdmi_infoframe_avi = gv100_sor_hdmi_infoframe_avi,
	.gsp.hdmi_infoframe_vsi = gb202_sor_hdmi_infoframe_vsi,
};

int
gb202_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return r535_disp_new(&gb202_gsp_disp, device, type, inst, pdisp);
}
