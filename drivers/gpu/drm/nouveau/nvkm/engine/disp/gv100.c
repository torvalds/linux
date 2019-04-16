/*
 * Copyright 2018 Red Hat Inc.
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
#include "nv50.h"
#include "head.h"
#include "ior.h"
#include "channv50.h"
#include "rootnv50.h"

#include <core/gpuobj.h>
#include <subdev/timer.h>

int
gv100_disp_wndw_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = nvkm_rd32(device, 0x610064);
	return (nvkm_rd32(device, 0x610074) & 0x03f00000) >> 20;
}

void
gv100_disp_super(struct work_struct *work)
{
	struct nv50_disp *disp =
		container_of(work, struct nv50_disp, supervisor);
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 stat = nvkm_rd32(device, 0x6107a8);
	u32 mask[4];

	nvkm_debug(subdev, "supervisor %d: %08x\n", ffs(disp->super), stat);
	list_for_each_entry(head, &disp->base.head, head) {
		mask[head->id] = nvkm_rd32(device, 0x6107ac + (head->id * 4));
		HEAD_DBG(head, "%08x", mask[head->id]);
	}

	if (disp->super & 0x00000001) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		nv50_disp_super_1(disp);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_1_0(disp, head);
		}
	} else
	if (disp->super & 0x00000002) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_0(disp, head);
		}
		nvkm_outp_route(&disp->base);
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00010000))
				continue;
			nv50_disp_super_2_1(disp, head);
		}
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_2_2(disp, head);
		}
	} else
	if (disp->super & 0x00000004) {
		list_for_each_entry(head, &disp->base.head, head) {
			if (!(mask[head->id] & 0x00001000))
				continue;
			nv50_disp_super_3_0(disp, head);
		}
	}

	list_for_each_entry(head, &disp->base.head, head)
		nvkm_wr32(device, 0x6107ac + (head->id * 4), 0x00000000);
	nvkm_wr32(device, 0x6107a8, 0x80000000);
}

static void
gv100_disp_exception(struct nv50_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611020 + (chid * 12));
	u32 type = (stat & 0x00007000) >> 12;
	u32 mthd = (stat & 0x00000fff) << 2;
	u32 data = nvkm_rd32(device, 0x611024 + (chid * 12));
	u32 code = nvkm_rd32(device, 0x611028 + (chid * 12));
	const struct nvkm_enum *reason =
		nvkm_enum_find(nv50_disp_intr_error_type, type);

	nvkm_error(subdev, "chid %d stat %08x reason %d [%s] mthd %04x "
			   "data %08x code %08x\n",
		   chid, stat, type, reason ? reason->name : "",
		   mthd, data, code);

	if (chid < ARRAY_SIZE(disp->chan) && disp->chan[chid]) {
		switch (mthd) {
		case 0x0200:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x611020 + (chid * 12), 0x90000000);
}

static void
gv100_disp_intr_ctrl_disp(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611c30);

	if (stat & 0x00000007) {
		disp->super = (stat & 0x00000007);
		queue_work(disp->wq, &disp->supervisor);
		nvkm_wr32(device, 0x611860, disp->super);
		stat &= ~0x00000007;
	}

	/*TODO: I would guess this is VBIOS_RELEASE, however, NFI how to
	 *      ACK it, nor does RM appear to bother.
	 */
	if (stat & 0x00000008)
		stat &= ~0x00000008;

	if (stat & 0x00000100) {
		unsigned long wndws = nvkm_rd32(device, 0x611858);
		unsigned long other = nvkm_rd32(device, 0x61185c);
		int wndw;

		nvkm_wr32(device, 0x611858, wndws);
		nvkm_wr32(device, 0x61185c, other);

		/* AWAKEN_OTHER_CORE. */
		if (other & 0x00000001)
			nv50_disp_chan_uevent_send(disp, 0);

		/* AWAKEN_WIN_CH(n). */
		for_each_set_bit(wndw, &wndws, disp->wndw.nr) {
			nv50_disp_chan_uevent_send(disp, 1 + wndw);
		}
	}

	if (stat)
		nvkm_warn(subdev, "ctrl %08x\n", stat);
}

static void
gv100_disp_intr_exc_other(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611854);
	unsigned long mask;
	int head;

	if (stat & 0x00000001) {
		nvkm_wr32(device, 0x611854, 0x00000001);
		gv100_disp_exception(disp, 0);
		stat &= ~0x00000001;
	}

	if ((mask = (stat & 0x00ff0000) >> 16)) {
		for_each_set_bit(head, &mask, disp->wndw.nr) {
			nvkm_wr32(device, 0x611854, 0x00010000 << head);
			gv100_disp_exception(disp, 73 + head);
			stat &= ~(0x00010000 << head);
		}
	}

	if (stat) {
		nvkm_warn(subdev, "exception %08x\n", stat);
		nvkm_wr32(device, 0x611854, stat);
	}
}

static void
gv100_disp_intr_exc_winim(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long stat = nvkm_rd32(device, 0x611850);
	int wndw;

	for_each_set_bit(wndw, &stat, disp->wndw.nr) {
		nvkm_wr32(device, 0x611850, BIT(wndw));
		gv100_disp_exception(disp, 33 + wndw);
		stat &= ~BIT(wndw);
	}

	if (stat) {
		nvkm_warn(subdev, "wimm %08x\n", (u32)stat);
		nvkm_wr32(device, 0x611850, stat);
	}
}

static void
gv100_disp_intr_exc_win(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	unsigned long stat = nvkm_rd32(device, 0x61184c);
	int wndw;

	for_each_set_bit(wndw, &stat, disp->wndw.nr) {
		nvkm_wr32(device, 0x61184c, BIT(wndw));
		gv100_disp_exception(disp, 1 + wndw);
		stat &= ~BIT(wndw);
	}

	if (stat) {
		nvkm_warn(subdev, "wndw %08x\n", (u32)stat);
		nvkm_wr32(device, 0x61184c, stat);
	}
}

static void
gv100_disp_intr_head_timing(struct nv50_disp *disp, int head)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611800 + (head * 0x04));

	/* LAST_DATA, LOADV. */
	if (stat & 0x00000003) {
		nvkm_wr32(device, 0x611800 + (head * 0x04), stat & 0x00000003);
		stat &= ~0x00000003;
	}

	if (stat & 0x00000004) {
		nvkm_disp_vblank(&disp->base, head);
		nvkm_wr32(device, 0x611800 + (head * 0x04), 0x00000004);
		stat &= ~0x00000004;
	}

	if (stat) {
		nvkm_warn(subdev, "head %08x\n", stat);
		nvkm_wr32(device, 0x611800 + (head * 0x04), stat);
	}
}

void
gv100_disp_intr(struct nv50_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x611ec0);
	unsigned long mask;
	int head;

	if ((mask = (stat & 0x000000ff))) {
		for_each_set_bit(head, &mask, 8) {
			gv100_disp_intr_head_timing(disp, head);
			stat &= ~BIT(head);
		}
	}

	if (stat & 0x00000200) {
		gv100_disp_intr_exc_win(disp);
		stat &= ~0x00000200;
	}

	if (stat & 0x00000400) {
		gv100_disp_intr_exc_winim(disp);
		stat &= ~0x00000400;
	}

	if (stat & 0x00000800) {
		gv100_disp_intr_exc_other(disp);
		stat &= ~0x00000800;
	}

	if (stat & 0x00001000) {
		gv100_disp_intr_ctrl_disp(disp);
		stat &= ~0x00001000;
	}

	if (stat)
		nvkm_warn(subdev, "intr %08x\n", stat);
}

void
gv100_disp_fini(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_wr32(device, 0x611db0, 0x00000000);
}

static int
gv100_disp_init(struct nv50_disp *disp)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	struct nvkm_head *head;
	int i, j;
	u32 tmp;

	/* Claim ownership of display. */
	if (nvkm_rd32(device, 0x6254e8) & 0x00000002) {
		nvkm_mask(device, 0x6254e8, 0x00000001, 0x00000000);
		if (nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x6254e8) & 0x00000002))
				break;
		) < 0)
			return -EBUSY;
	}

	/* Lock pin capabilities. */
	tmp = nvkm_rd32(device, 0x610068);
	nvkm_wr32(device, 0x640008, tmp);

	/* SOR capabilities. */
	for (i = 0; i < disp->sor.nr; i++) {
		tmp = nvkm_rd32(device, 0x61c000 + (i * 0x800));
		nvkm_mask(device, 0x640000, 0x00000100 << i, 0x00000100 << i);
		nvkm_wr32(device, 0x640144 + (i * 0x08), tmp);
	}

	/* Head capabilities. */
	list_for_each_entry(head, &disp->base.head, head) {
		const int id = head->id;

		/* RG. */
		tmp = nvkm_rd32(device, 0x616300 + (id * 0x800));
		nvkm_wr32(device, 0x640048 + (id * 0x020), tmp);

		/* POSTCOMP. */
		for (j = 0; j < 6 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x616100 + (id * 0x800) + j);
			nvkm_wr32(device, 0x640030 + (id * 0x20) + j, tmp);
		}
	}

	/* Window capabilities. */
	for (i = 0; i < disp->wndw.nr; i++) {
		nvkm_mask(device, 0x640004, 1 << i, 1 << i);
		for (j = 0; j < 6 * 4; j += 4) {
			tmp = nvkm_rd32(device, 0x630050 + (i * 0x800) + j);
			nvkm_wr32(device, 0x6401e4 + (i * 0x20) + j, tmp);
		}
	}

	/* IHUB capabilities. */
	for (i = 0; i < 4; i++) {
		tmp = nvkm_rd32(device, 0x62e000 + (i * 0x04));
		nvkm_wr32(device, 0x640010 + (i * 0x04), tmp);
	}

	nvkm_mask(device, 0x610078, 0x00000001, 0x00000001);

	/* Setup instance memory. */
	switch (nvkm_memory_target(disp->inst->memory)) {
	case NVKM_MEM_TARGET_VRAM: tmp = 0x00000001; break;
	case NVKM_MEM_TARGET_NCOH: tmp = 0x00000002; break;
	case NVKM_MEM_TARGET_HOST: tmp = 0x00000003; break;
	default:
		break;
	}
	nvkm_wr32(device, 0x610010, 0x00000008 | tmp);
	nvkm_wr32(device, 0x610014, disp->inst->addr >> 16);

	/* CTRL_DISP: AWAKEN, ERROR, SUPERVISOR[1-3]. */
	nvkm_wr32(device, 0x611cf0, 0x00000187); /* MSK. */
	nvkm_wr32(device, 0x611db0, 0x00000187); /* EN. */

	/* EXC_OTHER: CURSn, CORE. */
	nvkm_wr32(device, 0x611cec, disp->head.mask << 16 |
				    0x00000001); /* MSK. */
	nvkm_wr32(device, 0x611dac, 0x00000000); /* EN. */

	/* EXC_WINIM. */
	nvkm_wr32(device, 0x611ce8, disp->wndw.mask); /* MSK. */
	nvkm_wr32(device, 0x611da8, 0x00000000); /* EN. */

	/* EXC_WIN. */
	nvkm_wr32(device, 0x611ce4, disp->wndw.mask); /* MSK. */
	nvkm_wr32(device, 0x611da4, 0x00000000); /* EN. */

	/* HEAD_TIMING(n): VBLANK. */
	list_for_each_entry(head, &disp->base.head, head) {
		const u32 hoff = head->id * 4;
		nvkm_wr32(device, 0x611cc0 + hoff, 0x00000004); /* MSK. */
		nvkm_wr32(device, 0x611d80 + hoff, 0x00000000); /* EN. */
	}

	/* OR. */
	nvkm_wr32(device, 0x611cf4, 0x00000000); /* MSK. */
	nvkm_wr32(device, 0x611db4, 0x00000000); /* EN. */
	return 0;
}

static const struct nv50_disp_func
gv100_disp = {
	.init = gv100_disp_init,
	.fini = gv100_disp_fini,
	.intr = gv100_disp_intr,
	.uevent = &gv100_disp_chan_uevent,
	.super = gv100_disp_super,
	.root = &gv100_disp_root_oclass,
	.wndw = { .cnt = gv100_disp_wndw_cnt },
	.head = { .cnt = gv100_head_cnt, .new = gv100_head_new },
	.sor = { .cnt = gv100_sor_cnt, .new = gv100_sor_new },
	.ramht_size = 0x2000,
};

int
gv100_disp_new(struct nvkm_device *device, int index, struct nvkm_disp **pdisp)
{
	return nv50_disp_new_(&gv100_disp, device, index, pdisp);
}
