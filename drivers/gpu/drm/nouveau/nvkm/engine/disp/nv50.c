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
#include "priv.h"
#include "chan.h"
#include "conn.h"
#include "head.h"
#include "dp.h"
#include "ior.h"
#include "outp.h"

#include <core/client.h>
#include <core/ramht.h>
#include <subdev/bios.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/devinit.h>
#include <subdev/i2c.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static void
nv50_pior_clock(struct nvkm_ior *pior)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32 poff = nv50_ior_base(pior);

	nvkm_mask(device, 0x614380 + poff, 0x00000707, 0x00000001);
}

static int
nv50_pior_dp_links(struct nvkm_ior *pior, struct nvkm_i2c_aux *aux)
{
	int ret = nvkm_i2c_aux_lnk_ctl(aux, pior->dp.nr, pior->dp.bw, pior->dp.ef);
	if (ret)
		return ret;

	return 1;
}

static const struct nvkm_ior_func_dp
nv50_pior_dp = {
	.links = nv50_pior_dp_links,
};

static void
nv50_pior_power_wait(struct nvkm_device *device, u32 poff)
{
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61e004 + poff) & 0x80000000))
			break;
	);
}

static void
nv50_pior_power(struct nvkm_ior *pior, bool normal, bool pu, bool data, bool vsync, bool hsync)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32  poff = nv50_ior_base(pior);
	const u32 shift = normal ? 0 : 16;
	const u32 state = 0x80000000 | (0x00000001 * !!pu) << shift;
	const u32 field = 0x80000000 | (0x00000101 << shift);

	nv50_pior_power_wait(device, poff);
	nvkm_mask(device, 0x61e004 + poff, field, state);
	nv50_pior_power_wait(device, poff);
}

void
nv50_pior_depth(struct nvkm_ior *ior, struct nvkm_ior_state *state, u32 ctrl)
{
	/* GF119 moves this information to per-head methods, which is
	 * a lot more convenient, and where our shared code expect it.
	 */
	if (state->head && state == &ior->asy) {
		struct nvkm_head *head = nvkm_head_find(ior->disp, __ffs(state->head));

		if (!WARN_ON(!head)) {
			struct nvkm_head_state *state = &head->asy;
			switch ((ctrl & 0x000f0000) >> 16) {
			case 6: state->or.depth = 30; break;
			case 5: state->or.depth = 24; break;
			case 2: state->or.depth = 18; break;
			case 0: state->or.depth = 18; break; /*XXX*/
			default:
				state->or.depth = 18;
				WARN_ON(1);
				break;
			}
		}
	}
}

static void
nv50_pior_state(struct nvkm_ior *pior, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32 coff = pior->id * 8 + (state == &pior->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b80 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	state->rgdiv = 1;
	switch (state->proto_evo) {
	case 0: state->proto = TMDS; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
	nv50_pior_depth(pior, state, ctrl);
}

static const struct nvkm_ior_func
nv50_pior = {
	.state = nv50_pior_state,
	.power = nv50_pior_power,
	.clock = nv50_pior_clock,
	.dp = &nv50_pior_dp,
};

int
nv50_pior_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_pior, disp, PIOR, id, false);
}

int
nv50_pior_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = (nvkm_rd32(device, 0x610184) & 0x70000000) >> 28;
	return 3;
}

static int
nv50_sor_bl_set(struct nvkm_ior *ior, int lvl)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(ior);
	u32 div = 1025;
	u32 val = (lvl * div) / 100;

	nvkm_wr32(device, 0x61c084 + soff, 0x80000000 | val);
	return 0;
}

static int
nv50_sor_bl_get(struct nvkm_ior *ior)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(ior);
	u32 div = 1025;
	u32 val;

	val  = nvkm_rd32(device, 0x61c084 + soff);
	val &= 0x000007ff;
	return ((val * 100) + (div / 2)) / div;
}

const struct nvkm_ior_func_bl
nv50_sor_bl = {
	.get = nv50_sor_bl_get,
	.set = nv50_sor_bl_set,
};

void
nv50_sor_clock(struct nvkm_ior *sor)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const int  div = sor->asy.link == 3;
	const u32 soff = nv50_ior_base(sor);

	nvkm_mask(device, 0x614300 + soff, 0x00000707, (div << 8) | div);
}

static void
nv50_sor_power_wait(struct nvkm_device *device, u32 soff)
{
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c004 + soff) & 0x80000000))
			break;
	);
}

void
nv50_sor_power(struct nvkm_ior *sor, bool normal, bool pu, bool data, bool vsync, bool hsync)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  soff = nv50_ior_base(sor);
	const u32 shift = normal ? 0 : 16;
	const u32 state = 0x80000000 | (0x00000001 * !!pu) << shift;
	const u32 field = 0x80000000 | (0x00000001 << shift);

	nv50_sor_power_wait(device, soff);
	nvkm_mask(device, 0x61c004 + soff, field, state);
	nv50_sor_power_wait(device, soff);

	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
			break;
	);
}

void
nv50_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = sor->id * 8 + (state == &sor->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b70 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = LVDS; state->link = 1; break;
	case 1: state->proto = TMDS; state->link = 1; break;
	case 2: state->proto = TMDS; state->link = 2; break;
	case 5: state->proto = TMDS; state->link = 3; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
}

static const struct nvkm_ior_func
nv50_sor = {
	.state = nv50_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.bl = &nv50_sor_bl,
};

static int
nv50_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_sor, disp, SOR, id, false);
}

int
nv50_sor_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = (nvkm_rd32(device, 0x610184) & 0x03000000) >> 24;
	return 2;
}

static void
nv50_dac_clock(struct nvkm_ior *dac)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);

	nvkm_mask(device, 0x614280 + doff, 0x07070707, 0x00000000);
}

int
nv50_dac_sense(struct nvkm_ior *dac, u32 loadval)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);

	dac->func->power(dac, false, true, false, false, false);

	nvkm_wr32(device, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	loadval = nvkm_mask(device, 0x61a00c + doff, 0xffffffff, 0x00000000);

	dac->func->power(dac, false, false, false, false, false);
	if (!(loadval & 0x80000000))
		return -ETIMEDOUT;

	return (loadval & 0x38000000) >> 27;
}

static void
nv50_dac_power_wait(struct nvkm_device *device, const u32 doff)
{
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);
}

void
nv50_dac_power(struct nvkm_ior *dac, bool normal, bool pu, bool data, bool vsync, bool hsync)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32  doff = nv50_ior_base(dac);
	const u32 shift = normal ? 0 : 16;
	const u32 state = 0x80000000 | (0x00000040 * !    pu |
					0x00000010 * !  data |
					0x00000004 * ! vsync |
					0x00000001 * ! hsync) << shift;
	const u32 field = 0xc0000000 | (0x00000055 << shift);

	nv50_dac_power_wait(device, doff);
	nvkm_mask(device, 0x61a004 + doff, field, state);
	nv50_dac_power_wait(device, doff);
}

static void
nv50_dac_state(struct nvkm_ior *dac, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 coff = dac->id * 8 + (state == &dac->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b58 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = CRT; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
}

static const struct nvkm_ior_func
nv50_dac = {
	.state = nv50_dac_state,
	.power = nv50_dac_power,
	.sense = nv50_dac_sense,
	.clock = nv50_dac_clock,
};

int
nv50_dac_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_dac, disp, DAC, id, false);
}

int
nv50_dac_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;

	*pmask = (nvkm_rd32(device, 0x610184) & 0x00700000) >> 20;
	return 3;
}

static void
nv50_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;

	nvkm_mask(device, 0x61002c, (4 << head->id), 0);
}

static void
nv50_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;

	nvkm_mask(device, 0x61002c, (4 << head->id), (4 << head->id));
}

static void
nv50_head_rgclk(struct nvkm_head *head, int div)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;

	nvkm_mask(device, 0x614200 + (head->id * 0x800), 0x0000000f, div);
}

void
nv50_head_rgpos(struct nvkm_head *head, u16 *hline, u16 *vline)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;

	/* vline read locks hline. */
	*vline = nvkm_rd32(device, 0x616340 + hoff) & 0x0000ffff;
	*hline = nvkm_rd32(device, 0x616344 + hoff) & 0x0000ffff;
}

static void
nv50_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x540 + (state == &head->arm) * 4;
	u32 data;

	data = nvkm_rd32(device, 0x610ae8 + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610af0 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610af8 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610b00 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	state->hz = (nvkm_rd32(device, 0x610ad0 + hoff) & 0x003fffff) * 1000;
}

static const struct nvkm_head_func
nv50_head = {
	.state = nv50_head_state,
	.rgpos = nv50_head_rgpos,
	.rgclk = nv50_head_rgclk,
	.vblank_get = nv50_head_vblank_get,
	.vblank_put = nv50_head_vblank_put,
};

int
nv50_head_new(struct nvkm_disp *disp, int id)
{
	return nvkm_head_new_(&nv50_head, disp, id);
}

int
nv50_head_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	*pmask = 3;
	return 2;
}


static void
nv50_disp_mthd_list(struct nvkm_disp *disp, int debug, u32 base, int c,
		    const struct nvkm_disp_mthd_list *list, int inst)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int i;

	for (i = 0; list->data[i].mthd; i++) {
		if (list->data[i].addr) {
			u32 next = nvkm_rd32(device, list->data[i].addr + base + 0);
			u32 prev = nvkm_rd32(device, list->data[i].addr + base + c);
			u32 mthd = list->data[i].mthd + (list->mthd * inst);
			const char *name = list->data[i].name;
			char mods[16];

			if (prev != next)
				snprintf(mods, sizeof(mods), "-> %08x", next);
			else
				snprintf(mods, sizeof(mods), "%13c", ' ');

			nvkm_printk_(subdev, debug, info,
				     "\t%04x: %08x %s%s%s\n",
				     mthd, prev, mods, name ? " // " : "",
				     name ? name : "");
		}
	}
}

void
nv50_disp_chan_mthd(struct nvkm_disp_chan *chan, int debug)
{
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	const struct nvkm_disp_chan_mthd *mthd = chan->mthd;
	const struct nvkm_disp_mthd_list *list;
	int i, j;

	if (debug > subdev->debug)
		return;
	if (!mthd)
		return;

	for (i = 0; (list = mthd->data[i].mthd) != NULL; i++) {
		u32 base = chan->head * mthd->addr;
		for (j = 0; j < mthd->data[i].nr; j++, base += list->addr) {
			const char *cname = mthd->name;
			const char *sname = "";
			char cname_[16], sname_[16];

			if (mthd->addr) {
				snprintf(cname_, sizeof(cname_), "%s %d",
					 mthd->name, chan->chid.user);
				cname = cname_;
			}

			if (mthd->data[i].nr > 1) {
				snprintf(sname_, sizeof(sname_), " - %s %d",
					 mthd->data[i].name, j);
				sname = sname_;
			}

			nvkm_printk_(subdev, debug, info, "%s%s:\n", cname, sname);
			nv50_disp_mthd_list(disp, debug, base, mthd->prev,
					    list, j);
		}
	}
}

static void
nv50_disp_chan_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_mask(device, 0x610028, 0x00000001 << index, 0x00000000 << index);
	nvkm_wr32(device, 0x610020, 0x00000001 << index);
}

static void
nv50_disp_chan_uevent_init(struct nvkm_event *event, int types, int index)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_wr32(device, 0x610020, 0x00000001 << index);
	nvkm_mask(device, 0x610028, 0x00000001 << index, 0x00000001 << index);
}

void
nv50_disp_chan_uevent_send(struct nvkm_disp *disp, int chid)
{
	nvkm_event_ntfy(&disp->uevent, chid, NVKM_DISP_EVENT_CHAN_AWAKEN);
}

const struct nvkm_event_func
nv50_disp_chan_uevent = {
	.init = nv50_disp_chan_uevent_init,
	.fini = nv50_disp_chan_uevent_fini,
};

u64
nv50_disp_chan_user(struct nvkm_disp_chan *chan, u64 *psize)
{
	*psize = 0x1000;
	return 0x640000 + (chan->chid.user * 0x1000);
}

void
nv50_disp_chan_intr(struct nvkm_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->engine.subdev.device;
	const u32 mask = 0x00010001 << chan->chid.user;
	const u32 data = en ? 0x00010000 << chan->chid.user : 0x00000000;
	nvkm_mask(device, 0x610028, mask, data);
}

static void
nv50_disp_pioc_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	nvkm_mask(device, 0x610200 + (ctrl * 0x10), 0x00000001, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x00030000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d timeout: %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
	}
}

static int
nv50_disp_pioc_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	nvkm_wr32(device, 0x610200 + (ctrl * 0x10), 0x00002000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x00030000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d timeout0: %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
		return -EBUSY;
	}

	nvkm_wr32(device, 0x610200 + (ctrl * 0x10), 0x00000001);
	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x610200 + (ctrl * 0x10));
		if ((tmp & 0x00030000) == 0x00010000)
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d timeout1: %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nvkm_disp_chan_func
nv50_disp_pioc_func = {
	.init = nv50_disp_pioc_init,
	.fini = nv50_disp_pioc_fini,
	.intr = nv50_disp_chan_intr,
	.user = nv50_disp_chan_user,
};

int
nv50_disp_dmac_bind(struct nvkm_disp_chan *chan, struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object, chan->chid.user, -10, handle,
				 chan->chid.user << 28 | chan->chid.user);
}

static void
nv50_disp_dmac_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* deactivate channel */
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00001010, 0x00001000);
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini timeout, %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
	}

	chan->suspend_put = nvkm_rd32(device, 0x640000 + (ctrl * 0x1000));
}

static int
nv50_disp_dmac_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610204 + (ctrl * 0x0010), chan->push);
	nvkm_wr32(device, 0x610208 + (ctrl * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61020c + (ctrl * 0x0010), ctrl);
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000 + (ctrl * 0x1000), chan->suspend_put);
	nvkm_wr32(device, 0x610200 + (ctrl * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init timeout, %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

int
nv50_disp_dmac_push(struct nvkm_disp_chan *chan, u64 object)
{
	chan->memory = nvkm_umem_search(chan->object.client, object);
	if (IS_ERR(chan->memory))
		return PTR_ERR(chan->memory);

	if (nvkm_memory_size(chan->memory) < 0x1000)
		return -EINVAL;

	switch (nvkm_memory_target(chan->memory)) {
	case NVKM_MEM_TARGET_VRAM: chan->push = 0x00000001; break;
	case NVKM_MEM_TARGET_NCOH: chan->push = 0x00000002; break;
	case NVKM_MEM_TARGET_HOST: chan->push = 0x00000003; break;
	default:
		return -EINVAL;
	}

	chan->push |= nvkm_memory_addr(chan->memory) >> 8;
	return 0;
}

const struct nvkm_disp_chan_func
nv50_disp_dmac_func = {
	.push = nv50_disp_dmac_push,
	.init = nv50_disp_dmac_init,
	.fini = nv50_disp_dmac_fini,
	.intr = nv50_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = nv50_disp_dmac_bind,
};

const struct nvkm_disp_chan_user
nv50_disp_curs = {
	.func = &nv50_disp_pioc_func,
	.ctrl = 7,
	.user = 7,
};

const struct nvkm_disp_chan_user
nv50_disp_oimm = {
	.func = &nv50_disp_pioc_func,
	.ctrl = 5,
	.user = 5,
};

static const struct nvkm_disp_mthd_list
nv50_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0009a0 },
		{ 0x0088, 0x0009c0 },
		{ 0x008c, 0x0009c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
nv50_disp_ovly_mthd = {
	.name = "Overlay",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_ovly_mthd_base },
		{}
	}
};

static const struct nvkm_disp_chan_user
nv50_disp_ovly = {
	.func = &nv50_disp_dmac_func,
	.ctrl = 3,
	.user = 3,
	.mthd = &nv50_disp_ovly_mthd,
};

static const struct nvkm_disp_mthd_list
nv50_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x0100, 0x610894 },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

const struct nvkm_disp_mthd_list
nv50_disp_base_mthd_image = {
	.mthd = 0x0400,
	.addr = 0x000000,
	.data = {
		{ 0x0800, 0x6108f0 },
		{ 0x0804, 0x6108fc },
		{ 0x0808, 0x61090c },
		{ 0x080c, 0x610914 },
		{ 0x0810, 0x610904 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
nv50_disp_base_mthd = {
	.name = "Base",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_base_mthd_base },
		{  "Image", 2, &nv50_disp_base_mthd_image },
		{}
	}
};

static const struct nvkm_disp_chan_user
nv50_disp_base = {
	.func = &nv50_disp_dmac_func,
	.ctrl = 1,
	.user = 1,
	.mthd = &nv50_disp_base_mthd,
};

const struct nvkm_disp_mthd_list
nv50_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x610bb8 },
		{ 0x0088, 0x610b9c },
		{ 0x008c, 0x000000 },
		{}
	}
};

static const struct nvkm_disp_mthd_list
nv50_disp_core_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610828 },
		{}
	}
};

const struct nvkm_disp_mthd_list
nv50_disp_core_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610b70 },
		{}
	}
};

const struct nvkm_disp_mthd_list
nv50_disp_core_mthd_pior = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0700, 0x610b80 },
		{}
	}
};

static const struct nvkm_disp_mthd_list
nv50_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{}
	}
};

static const struct nvkm_disp_chan_mthd
nv50_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &nv50_disp_core_mthd_dac  },
		{    "SOR", 2, &nv50_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &nv50_disp_core_mthd_head },
		{}
	}
};

static void
nv50_disp_core_fini(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* deactivate channel */
	nvkm_mask(device, 0x610200, 0x00000010, 0x00000000);
	nvkm_mask(device, 0x610200, 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "core fini: %08x\n",
			   nvkm_rd32(device, 0x610200));
	}

	chan->suspend_put = nvkm_rd32(device, 0x640000);
}

static int
nv50_disp_core_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* attempt to unstick channel from some unknown state */
	if ((nvkm_rd32(device, 0x610200) & 0x009f0000) == 0x00020000)
		nvkm_mask(device, 0x610200, 0x00800000, 0x00800000);
	if ((nvkm_rd32(device, 0x610200) & 0x003f0000) == 0x00030000)
		nvkm_mask(device, 0x610200, 0x00600000, 0x00600000);

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610204, chan->push);
	nvkm_wr32(device, 0x610208, 0x00010000);
	nvkm_wr32(device, 0x61020c, 0x00000000);
	nvkm_mask(device, 0x610200, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000, chan->suspend_put);
	nvkm_wr32(device, 0x610200, 0x01000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "core init: %08x\n",
			   nvkm_rd32(device, 0x610200));
		return -EBUSY;
	}

	return 0;
}

const struct nvkm_disp_chan_func
nv50_disp_core_func = {
	.push = nv50_disp_dmac_push,
	.init = nv50_disp_core_init,
	.fini = nv50_disp_core_fini,
	.intr = nv50_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = nv50_disp_dmac_bind,
};

static const struct nvkm_disp_chan_user
nv50_disp_core = {
	.func = &nv50_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &nv50_disp_core_mthd,
};

static u32
nv50_disp_super_iedt(struct nvkm_head *head, struct nvkm_outp *outp,
		     u8 *ver, u8 *hdr, u8 *cnt, u8 *len,
		     struct nvbios_outp *iedt)
{
	struct nvkm_bios *bios = head->disp->engine.subdev.device->bios;
	const u8  l = ffs(outp->info.link);
	const u16 t = outp->info.hasht;
	const u16 m = (0x0100 << head->id) | (l << 6) | outp->info.or;
	u32 data = nvbios_outp_match(bios, t, m, ver, hdr, cnt, len, iedt);
	if (!data)
		OUTP_DBG(outp, "missing IEDT for %04x:%04x", t, m);
	return data;
}

static void
nv50_disp_super_ied_on(struct nvkm_head *head,
		       struct nvkm_ior *ior, int id, u32 khz)
{
	struct nvkm_subdev *subdev = &head->disp->engine.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvkm_outp *outp = ior->asy.outp;
	struct nvbios_ocfg iedtrs;
	struct nvbios_outp iedt;
	u8  ver, hdr, cnt, len, flags = 0x00;
	u32 data;

	if (!outp) {
		IOR_DBG(ior, "nothing to attach");
		return;
	}

	/* Lookup IED table for the device. */
	data = nv50_disp_super_iedt(head, outp, &ver, &hdr, &cnt, &len, &iedt);
	if (!data)
		return;

	/* Lookup IEDT runtime settings for the current configuration. */
	if (ior->type == SOR) {
		if (ior->asy.proto == LVDS) {
			if (head->asy.or.depth == 24)
				flags |= 0x02;
		}
		if (ior->asy.link == 3)
			flags |= 0x01;
	}

	data = nvbios_ocfg_match(bios, data, ior->asy.proto_evo, flags,
				 &ver, &hdr, &cnt, &len, &iedtrs);
	if (!data) {
		OUTP_DBG(outp, "missing IEDT RS for %02x:%02x",
			 ior->asy.proto_evo, flags);
		return;
	}

	/* Execute the OnInt[23] script for the current frequency. */
	data = nvbios_oclk_match(bios, iedtrs.clkcmp[id], khz);
	if (!data) {
		OUTP_DBG(outp, "missing IEDT RSS %d for %02x:%02x %d khz",
			 id, ior->asy.proto_evo, flags, khz);
		return;
	}

	nvbios_init(subdev, data,
		init.outp = &outp->info;
		init.or   = ior->id;
		init.link = ior->asy.link;
		init.head = head->id;
	);
}

static void
nv50_disp_super_ied_off(struct nvkm_head *head, struct nvkm_ior *ior, int id)
{
	struct nvkm_outp *outp = ior->arm.outp;
	struct nvbios_outp iedt;
	u8  ver, hdr, cnt, len;
	u32 data;

	if (!outp) {
		IOR_DBG(ior, "nothing attached");
		return;
	}

	data = nv50_disp_super_iedt(head, outp, &ver, &hdr, &cnt, &len, &iedt);
	if (!data)
		return;

	nvbios_init(&head->disp->engine.subdev, iedt.script[id],
		init.outp = &outp->info;
		init.or   = ior->id;
		init.link = ior->arm.link;
		init.head = head->id;
	);
}

static struct nvkm_ior *
nv50_disp_super_ior_asy(struct nvkm_head *head)
{
	struct nvkm_ior *ior;
	list_for_each_entry(ior, &head->disp->iors, head) {
		if (ior->asy.head & (1 << head->id)) {
			HEAD_DBG(head, "to %s", ior->name);
			return ior;
		}
	}
	HEAD_DBG(head, "nothing to attach");
	return NULL;
}

static struct nvkm_ior *
nv50_disp_super_ior_arm(struct nvkm_head *head)
{
	struct nvkm_ior *ior;
	list_for_each_entry(ior, &head->disp->iors, head) {
		if (ior->arm.head & (1 << head->id)) {
			HEAD_DBG(head, "on %s", ior->name);
			return ior;
		}
	}
	HEAD_DBG(head, "nothing attached");
	return NULL;
}

void
nv50_disp_super_3_0(struct nvkm_disp *disp, struct nvkm_head *head)
{
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're attaching to the head. */
	HEAD_DBG(head, "supervisor 3.0");
	ior = nv50_disp_super_ior_asy(head);
	if (!ior)
		return;

	/* Execute OnInt3 IED script. */
	nv50_disp_super_ied_on(head, ior, 1, head->asy.hz / 1000);

	/* OR-specific handling. */
	if (ior->func->war_3)
		ior->func->war_3(ior);
}

static void
nv50_disp_super_2_2_dp(struct nvkm_head *head, struct nvkm_ior *ior)
{
	struct nvkm_subdev *subdev = &head->disp->engine.subdev;
	const u32      khz = head->asy.hz / 1000;
	const u32 linkKBps = ior->dp.bw * 27000;
	const u32   symbol = 100000;
	int bestTU = 0, bestVTUi = 0, bestVTUf = 0, bestVTUa = 0;
	int TU, VTUi, VTUf, VTUa;
	u64 link_data_rate, link_ratio, unk;
	u32 best_diff = 64 * symbol;
	u64 h, v;

	/* symbols/hblank - algorithm taken from comments in tegra driver */
	h = head->asy.hblanke + head->asy.htotal - head->asy.hblanks - 7;
	h = h * linkKBps;
	do_div(h, khz);
	h = h - (3 * ior->dp.ef) - (12 / ior->dp.nr);

	/* symbols/vblank - algorithm taken from comments in tegra driver */
	v = head->asy.vblanks - head->asy.vblanke - 25;
	v = v * linkKBps;
	do_div(v, khz);
	v = v - ((36 / ior->dp.nr) + 3) - 1;

	ior->func->dp->audio_sym(ior, head->id, h, v);

	/* watermark / activesym */
	link_data_rate = (khz * head->asy.or.depth / 8) / ior->dp.nr;

	/* calculate ratio of packed data rate to link symbol rate */
	link_ratio = link_data_rate * symbol;
	do_div(link_ratio, linkKBps);

	for (TU = 64; ior->func->dp->activesym && TU >= 32; TU--) {
		/* calculate average number of valid symbols in each TU */
		u32 tu_valid = link_ratio * TU;
		u32 calc, diff;

		/* find a hw representation for the fraction.. */
		VTUi = tu_valid / symbol;
		calc = VTUi * symbol;
		diff = tu_valid - calc;
		if (diff) {
			if (diff >= (symbol / 2)) {
				VTUf = symbol / (symbol - diff);
				if (symbol - (VTUf * diff))
					VTUf++;

				if (VTUf <= 15) {
					VTUa  = 1;
					calc += symbol - (symbol / VTUf);
				} else {
					VTUa  = 0;
					VTUf  = 1;
					calc += symbol;
				}
			} else {
				VTUa  = 0;
				VTUf  = min((int)(symbol / diff), 15);
				calc += symbol / VTUf;
			}

			diff = calc - tu_valid;
		} else {
			/* no remainder, but the hw doesn't like the fractional
			 * part to be zero.  decrement the integer part and
			 * have the fraction add a whole symbol back
			 */
			VTUa = 0;
			VTUf = 1;
			VTUi--;
		}

		if (diff < best_diff) {
			best_diff = diff;
			bestTU = TU;
			bestVTUa = VTUa;
			bestVTUf = VTUf;
			bestVTUi = VTUi;
			if (diff == 0)
				break;
		}
	}

	if (ior->func->dp->activesym) {
		if (!bestTU) {
			nvkm_error(subdev, "unable to determine dp config\n");
			return;
		}

		ior->func->dp->activesym(ior, head->id, bestTU, bestVTUa, bestVTUf, bestVTUi);
	} else {
		bestTU = 64;
	}

	/* XXX close to vbios numbers, but not right */
	unk  = (symbol - link_ratio) * bestTU;
	unk *= link_ratio;
	do_div(unk, symbol);
	do_div(unk, symbol);
	unk += 6;

	ior->func->dp->watermark(ior, head->id, unk);
}

void
nv50_disp_super_2_2(struct nvkm_disp *disp, struct nvkm_head *head)
{
	const u32 khz = head->asy.hz / 1000;
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're attaching from the head. */
	HEAD_DBG(head, "supervisor 2.2");
	ior = nv50_disp_super_ior_asy(head);
	if (!ior)
		return;

	outp = ior->asy.outp;

	/* For some reason, NVIDIA decided not to:
	 *
	 * A) Give dual-link LVDS a separate EVO protocol, like for TMDS.
	 *  and
	 * B) Use SetControlOutputResource.PixelDepth on LVDS.
	 *
	 * Override the values we usually read from HW with the same
	 * data we pass though an ioctl instead.
	 */
	if (outp && ior->type == SOR && ior->asy.proto == LVDS) {
		head->asy.or.depth = outp->lvds.bpc8 ? 24 : 18;
		ior->asy.link      = outp->lvds.dual ? 3 : 1;
	}

	/* Execute OnInt2 IED script. */
	nv50_disp_super_ied_on(head, ior, 0, khz);

	/* Program RG clock divider. */
	head->func->rgclk(head, ior->asy.rgdiv);

	/* Mode-specific internal DP configuration. */
	if (ior->type == SOR && ior->asy.proto == DP)
		nv50_disp_super_2_2_dp(head, ior);

	/* OR-specific handling. */
	ior->func->clock(ior);
	if (ior->func->war_2)
		ior->func->war_2(ior);
}

void
nv50_disp_super_2_1(struct nvkm_disp *disp, struct nvkm_head *head)
{
	struct nvkm_devinit *devinit = disp->engine.subdev.device->devinit;
	const u32 khz = head->asy.hz / 1000;
	HEAD_DBG(head, "supervisor 2.1 - %d khz", khz);
	if (khz)
		nvkm_devinit_pll_set(devinit, PLL_VPLL0 + head->id, khz);
}

void
nv50_disp_super_2_0(struct nvkm_disp *disp, struct nvkm_head *head)
{
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're detaching from the head. */
	HEAD_DBG(head, "supervisor 2.0");
	ior = nv50_disp_super_ior_arm(head);
	if (!ior)
		return;

	/* Execute OffInt2 IED script. */
	nv50_disp_super_ied_off(head, ior, 2);
}

void
nv50_disp_super_1_0(struct nvkm_disp *disp, struct nvkm_head *head)
{
	struct nvkm_ior *ior;

	/* Determine which OR, if any, we're detaching from the head. */
	HEAD_DBG(head, "supervisor 1.0");
	ior = nv50_disp_super_ior_arm(head);
	if (!ior)
		return;

	/* Execute OffInt1 IED script. */
	nv50_disp_super_ied_off(head, ior, 1);
}

void
nv50_disp_super_1(struct nvkm_disp *disp)
{
	struct nvkm_head *head;
	struct nvkm_ior *ior;

	list_for_each_entry(head, &disp->heads, head) {
		head->func->state(head, &head->arm);
		head->func->state(head, &head->asy);
	}

	list_for_each_entry(ior, &disp->iors, head) {
		ior->func->state(ior, &ior->arm);
		ior->func->state(ior, &ior->asy);
	}
}

void
nv50_disp_super(struct work_struct *work)
{
	struct nvkm_disp *disp = container_of(work, struct nvkm_disp, super.work);
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_head *head;
	u32 super;

	mutex_lock(&disp->super.mutex);
	super = nvkm_rd32(device, 0x610030);

	nvkm_debug(subdev, "supervisor %08x %08x\n", disp->super.pending, super);

	if (disp->super.pending & 0x00000010) {
		nv50_disp_chan_mthd(disp->chan[0], NV_DBG_DEBUG);
		nv50_disp_super_1(disp);
		list_for_each_entry(head, &disp->heads, head) {
			if (!(super & (0x00000020 << head->id)))
				continue;
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_1_0(disp, head);
		}
	} else
	if (disp->super.pending & 0x00000020) {
		list_for_each_entry(head, &disp->heads, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_2_0(disp, head);
		}
		list_for_each_entry(head, &disp->heads, head) {
			if (!(super & (0x00000200 << head->id)))
				continue;
			nv50_disp_super_2_1(disp, head);
		}
		list_for_each_entry(head, &disp->heads, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_2_2(disp, head);
		}
	} else
	if (disp->super.pending & 0x00000040) {
		list_for_each_entry(head, &disp->heads, head) {
			if (!(super & (0x00000080 << head->id)))
				continue;
			nv50_disp_super_3_0(disp, head);
		}
	}

	nvkm_wr32(device, 0x610030, 0x80000000);
	mutex_unlock(&disp->super.mutex);
}

const struct nvkm_enum
nv50_disp_intr_error_type[] = {
	{ 0, "NONE" },
	{ 1, "PUSHBUFFER_ERR" },
	{ 2, "TRAP" },
	{ 3, "RESERVED_METHOD" },
	{ 4, "INVALID_ARG" },
	{ 5, "INVALID_STATE" },
	{ 7, "UNRESOLVABLE_HANDLE" },
	{}
};

static const struct nvkm_enum
nv50_disp_intr_error_code[] = {
	{ 0x00, "" },
	{}
};

static void
nv50_disp_intr_error(struct nvkm_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 data = nvkm_rd32(device, 0x610084 + (chid * 0x08));
	u32 addr = nvkm_rd32(device, 0x610080 + (chid * 0x08));
	u32 code = (addr & 0x00ff0000) >> 16;
	u32 type = (addr & 0x00007000) >> 12;
	u32 mthd = (addr & 0x00000ffc);
	const struct nvkm_enum *ec, *et;

	et = nvkm_enum_find(nv50_disp_intr_error_type, type);
	ec = nvkm_enum_find(nv50_disp_intr_error_code, code);

	nvkm_error(subdev,
		   "ERROR %d [%s] %02x [%s] chid %d mthd %04x data %08x\n",
		   type, et ? et->name : "", code, ec ? ec->name : "",
		   chid, mthd, data);

	if (chid < ARRAY_SIZE(disp->chan)) {
		switch (mthd) {
		case 0x0080:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x610020, 0x00010000 << chid);
	nvkm_wr32(device, 0x610080 + (chid * 0x08), 0x90000000);
}

void
nv50_disp_intr(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	u32 intr0 = nvkm_rd32(device, 0x610020);
	u32 intr1 = nvkm_rd32(device, 0x610024);

	while (intr0 & 0x001f0000) {
		u32 chid = __ffs(intr0 & 0x001f0000) - 16;
		nv50_disp_intr_error(disp, chid);
		intr0 &= ~(0x00010000 << chid);
	}

	while (intr0 & 0x0000001f) {
		u32 chid = __ffs(intr0 & 0x0000001f);
		nv50_disp_chan_uevent_send(disp, chid);
		intr0 &= ~(0x00000001 << chid);
	}

	if (intr1 & 0x00000004) {
		nvkm_disp_vblank(disp, 0);
		nvkm_wr32(device, 0x610024, 0x00000004);
	}

	if (intr1 & 0x00000008) {
		nvkm_disp_vblank(disp, 1);
		nvkm_wr32(device, 0x610024, 0x00000008);
	}

	if (intr1 & 0x00000070) {
		disp->super.pending = (intr1 & 0x00000070);
		queue_work(disp->super.wq, &disp->super.work);
		nvkm_wr32(device, 0x610024, disp->super.pending);
	}
}

void
nv50_disp_fini(struct nvkm_disp *disp, bool suspend)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	/* disable all interrupts */
	nvkm_wr32(device, 0x610024, 0x00000000);
	nvkm_wr32(device, 0x610020, 0x00000000);
}

int
nv50_disp_init(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_head *head;
	u32 tmp;
	int i;

	/* The below segments of code copying values from one register to
	 * another appear to inform EVO of the display capabilities or
	 * something similar.  NFI what the 0x614004 caps are for..
	 */
	tmp = nvkm_rd32(device, 0x614004);
	nvkm_wr32(device, 0x610184, tmp);

	/* ... CRTC caps */
	list_for_each_entry(head, &disp->heads, head) {
		tmp = nvkm_rd32(device, 0x616100 + (head->id * 0x800));
		nvkm_wr32(device, 0x610190 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616104 + (head->id * 0x800));
		nvkm_wr32(device, 0x610194 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x616108 + (head->id * 0x800));
		nvkm_wr32(device, 0x610198 + (head->id * 0x10), tmp);
		tmp = nvkm_rd32(device, 0x61610c + (head->id * 0x800));
		nvkm_wr32(device, 0x61019c + (head->id * 0x10), tmp);
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
	nvkm_wr32(device, 0x610010, (disp->inst->addr >> 8) | 9);

	/* enable supervisor interrupts, disable everything else */
	nvkm_wr32(device, 0x61002c, 0x00000370);
	nvkm_wr32(device, 0x610028, 0x00000000);
	return 0;
}

int
nv50_disp_oneinit(struct nvkm_disp *disp)
{
	const struct nvkm_disp_func *func = disp->func;
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_outp *outp, *outt, *pair;
	struct nvkm_conn *conn;
	struct nvkm_ior *ior;
	int ret, i;
	u8  ver, hdr;
	u32 data;
	struct dcb_output dcbE;
	struct nvbios_connE connE;

	if (func->wndw.cnt) {
		disp->wndw.nr = func->wndw.cnt(disp, &disp->wndw.mask);
		nvkm_debug(subdev, "Window(s): %d (%08lx)\n", disp->wndw.nr, disp->wndw.mask);
	}

	disp->head.nr = func->head.cnt(disp, &disp->head.mask);
	nvkm_debug(subdev, "  Head(s): %d (%02lx)\n", disp->head.nr, disp->head.mask);
	for_each_set_bit(i, &disp->head.mask, disp->head.nr) {
		ret = func->head.new(disp, i);
		if (ret)
			return ret;
	}

	if (func->dac.cnt) {
		disp->dac.nr = func->dac.cnt(disp, &disp->dac.mask);
		nvkm_debug(subdev, "   DAC(s): %d (%02lx)\n", disp->dac.nr, disp->dac.mask);
		for_each_set_bit(i, &disp->dac.mask, disp->dac.nr) {
			ret = func->dac.new(disp, i);
			if (ret)
				return ret;
		}
	}

	if (func->pior.cnt) {
		disp->pior.nr = func->pior.cnt(disp, &disp->pior.mask);
		nvkm_debug(subdev, "  PIOR(s): %d (%02lx)\n", disp->pior.nr, disp->pior.mask);
		for_each_set_bit(i, &disp->pior.mask, disp->pior.nr) {
			ret = func->pior.new(disp, i);
			if (ret)
				return ret;
		}
	}

	disp->sor.nr = func->sor.cnt(disp, &disp->sor.mask);
	nvkm_debug(subdev, "   SOR(s): %d (%02lx)\n", disp->sor.nr, disp->sor.mask);
	for_each_set_bit(i, &disp->sor.mask, disp->sor.nr) {
		ret = func->sor.new(disp, i);
		if (ret)
			return ret;
	}

	ret = nvkm_gpuobj_new(device, 0x10000, 0x10000, false, NULL, &disp->inst);
	if (ret)
		return ret;

	ret = nvkm_ramht_new(device, func->ramht_size ? func->ramht_size : 0x1000, 0, disp->inst,
			     &disp->ramht);
	if (ret)
		return ret;

	/* Create output path objects for each VBIOS display path. */
	i = -1;
	while ((data = dcb_outp_parse(bios, ++i, &ver, &hdr, &dcbE))) {
		if (WARN_ON((ver & 0xf0) != 0x40))
			return -EINVAL;
		if (dcbE.type == DCB_OUTPUT_UNUSED)
			continue;
		if (dcbE.type == DCB_OUTPUT_EOL)
			break;
		outp = NULL;

		switch (dcbE.type) {
		case DCB_OUTPUT_ANALOG:
		case DCB_OUTPUT_TMDS:
		case DCB_OUTPUT_LVDS:
			ret = nvkm_outp_new(disp, i, &dcbE, &outp);
			break;
		case DCB_OUTPUT_DP:
			ret = nvkm_dp_new(disp, i, &dcbE, &outp);
			break;
		case DCB_OUTPUT_TV:
		case DCB_OUTPUT_WFD:
			/* No support for WFD yet. */
			ret = -ENODEV;
			continue;
		default:
			nvkm_warn(subdev, "dcb %d type %d unknown\n",
				  i, dcbE.type);
			continue;
		}

		if (ret) {
			if (outp) {
				if (ret != -ENODEV)
					OUTP_ERR(outp, "ctor failed: %d", ret);
				else
					OUTP_DBG(outp, "not supported");
				nvkm_outp_del(&outp);
				continue;
			}
			nvkm_error(subdev, "failed to create outp %d\n", i);
			continue;
		}

		list_add_tail(&outp->head, &disp->outps);
	}

	/* Create connector objects based on available output paths. */
	list_for_each_entry_safe(outp, outt, &disp->outps, head) {
		/* VBIOS data *should* give us the most useful information. */
		data = nvbios_connEp(bios, outp->info.connector, &ver, &hdr,
				     &connE);

		/* No bios connector data... */
		if (!data) {
			/* Heuristic: anything with the same ccb index is
			 * considered to be on the same connector, any
			 * output path without an associated ccb entry will
			 * be put on its own connector.
			 */
			int ccb_index = outp->info.i2c_index;
			if (ccb_index != 0xf) {
				list_for_each_entry(pair, &disp->outps, head) {
					if (pair->info.i2c_index == ccb_index) {
						outp->conn = pair->conn;
						break;
					}
				}
			}

			/* Connector shared with another output path. */
			if (outp->conn)
				continue;

			memset(&connE, 0x00, sizeof(connE));
			connE.type = DCB_CONNECTOR_NONE;
			i = -1;
		} else {
			i = outp->info.connector;
		}

		/* Check that we haven't already created this connector. */
		list_for_each_entry(conn, &disp->conns, head) {
			if (conn->index == outp->info.connector) {
				outp->conn = conn;
				break;
			}
		}

		if (outp->conn)
			continue;

		/* Apparently we need to create a new one! */
		ret = nvkm_conn_new(disp, i, &connE, &outp->conn);
		if (ret) {
			nvkm_error(subdev, "failed to create outp %d conn: %d\n", outp->index, ret);
			nvkm_conn_del(&outp->conn);
			list_del(&outp->head);
			nvkm_outp_del(&outp);
			continue;
		}

		list_add_tail(&outp->conn->head, &disp->conns);
	}

	/* Enforce identity-mapped SOR assignment for panels, which have
	 * certain bits (ie. backlight controls) wired to a specific SOR.
	 */
	list_for_each_entry(outp, &disp->outps, head) {
		if (outp->conn->info.type == DCB_CONNECTOR_LVDS ||
		    outp->conn->info.type == DCB_CONNECTOR_eDP) {
			ior = nvkm_ior_find(disp, SOR, ffs(outp->info.or) - 1);
			if (!WARN_ON(!ior))
				ior->identity = true;
			outp->identity = true;
		}
	}

	return 0;
}

static const struct nvkm_disp_func
nv50_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = nv50_disp_init,
	.fini = nv50_disp_fini,
	.intr = nv50_disp_intr,
	.super = nv50_disp_super,
	.uevent = &nv50_disp_chan_uevent,
	.head = { .cnt = nv50_head_cnt, .new = nv50_head_new },
	.dac = { .cnt = nv50_dac_cnt, .new = nv50_dac_new },
	.sor = { .cnt = nv50_sor_cnt, .new = nv50_sor_new },
	.pior = { .cnt = nv50_pior_cnt, .new = nv50_pior_new },
	.root = { 0, 0, NV50_DISP },
	.user = {
		{{0,0,NV50_DISP_CURSOR             }, nvkm_disp_chan_new, &nv50_disp_curs },
		{{0,0,NV50_DISP_OVERLAY            }, nvkm_disp_chan_new, &nv50_disp_oimm },
		{{0,0,NV50_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &nv50_disp_base },
		{{0,0,NV50_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &nv50_disp_core },
		{{0,0,NV50_DISP_OVERLAY_CHANNEL_DMA}, nvkm_disp_chan_new, &nv50_disp_ovly },
		{}
	}
};

int
nv50_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&nv50_disp, device, type, inst, pdisp);
}
