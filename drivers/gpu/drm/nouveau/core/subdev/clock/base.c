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

#include <core/option.h>

#include <subdev/clock.h>
#include <subdev/therm.h>
#include <subdev/volt.h>
#include <subdev/fb.h>

#include <subdev/bios.h>
#include <subdev/bios/boost.h>
#include <subdev/bios/cstep.h>
#include <subdev/bios/perf.h>

/******************************************************************************
 * misc
 *****************************************************************************/
static u32
nouveau_clock_adjust(struct nouveau_clock *clk, bool adjust,
		     u8 pstate, u8 domain, u32 input)
{
	struct nouveau_bios *bios = nouveau_bios(clk);
	struct nvbios_boostE boostE;
	u8  ver, hdr, cnt, len;
	u16 data;

	data = nvbios_boostEm(bios, pstate, &ver, &hdr, &cnt, &len, &boostE);
	if (data) {
		struct nvbios_boostS boostS;
		u8  idx = 0, sver, shdr;
		u16 subd;

		input = max(boostE.min, input);
		input = min(boostE.max, input);
		do {
			sver = ver;
			shdr = hdr;
			subd = nvbios_boostSp(bios, idx++, data, &sver, &shdr,
					      cnt, len, &boostS);
			if (subd && boostS.domain == domain) {
				if (adjust)
					input = input * boostS.percent / 100;
				input = max(boostS.min, input);
				input = min(boostS.max, input);
				break;
			}
		} while (subd);
	}

	return input;
}

/******************************************************************************
 * C-States
 *****************************************************************************/
static int
nouveau_cstate_prog(struct nouveau_clock *clk,
		    struct nouveau_pstate *pstate, int cstatei)
{
	struct nouveau_therm *ptherm = nouveau_therm(clk);
	struct nouveau_volt *volt = nouveau_volt(clk);
	struct nouveau_cstate *cstate;
	int ret;

	if (!list_empty(&pstate->list)) {
		cstate = list_entry(pstate->list.prev, typeof(*cstate), head);
	} else {
		cstate = &pstate->base;
	}

	ret = nouveau_therm_cstate(ptherm, pstate->fanspeed, +1);
	if (ret && ret != -ENODEV) {
		nv_error(clk, "failed to raise fan speed: %d\n", ret);
		return ret;
	}

	ret = volt->set_id(volt, cstate->voltage, +1);
	if (ret && ret != -ENODEV) {
		nv_error(clk, "failed to raise voltage: %d\n", ret);
		return ret;
	}

	ret = clk->calc(clk, cstate);
	if (ret == 0) {
		ret = clk->prog(clk);
		clk->tidy(clk);
	}

	ret = volt->set_id(volt, cstate->voltage, -1);
	if (ret && ret != -ENODEV)
		nv_error(clk, "failed to lower voltage: %d\n", ret);

	ret = nouveau_therm_cstate(ptherm, pstate->fanspeed, -1);
	if (ret && ret != -ENODEV)
		nv_error(clk, "failed to lower fan speed: %d\n", ret);

	return 0;
}

static void
nouveau_cstate_del(struct nouveau_cstate *cstate)
{
	list_del(&cstate->head);
	kfree(cstate);
}

static int
nouveau_cstate_new(struct nouveau_clock *clk, int idx,
		   struct nouveau_pstate *pstate)
{
	struct nouveau_bios *bios = nouveau_bios(clk);
	struct nouveau_clocks *domain = clk->domains;
	struct nouveau_cstate *cstate = NULL;
	struct nvbios_cstepX cstepX;
	u8  ver, hdr;
	u16 data;

	data = nvbios_cstepXp(bios, idx, &ver, &hdr, &cstepX);
	if (!data)
		return -ENOENT;

	cstate = kzalloc(sizeof(*cstate), GFP_KERNEL);
	if (!cstate)
		return -ENOMEM;

	*cstate = pstate->base;
	cstate->voltage = cstepX.voltage;

	while (domain && domain->name != nv_clk_src_max) {
		if (domain->flags & NVKM_CLK_DOM_FLAG_CORE) {
			u32 freq = nouveau_clock_adjust(clk, true,
							pstate->pstate,
							domain->bios,
							cstepX.freq);
			cstate->domain[domain->name] = freq;
		}
		domain++;
	}

	list_add(&cstate->head, &pstate->list);
	return 0;
}

/******************************************************************************
 * P-States
 *****************************************************************************/
static int
nouveau_pstate_prog(struct nouveau_clock *clk, int pstatei)
{
	struct nouveau_fb *pfb = nouveau_fb(clk);
	struct nouveau_pstate *pstate;
	int ret, idx = 0;

	list_for_each_entry(pstate, &clk->states, head) {
		if (idx++ == pstatei)
			break;
	}

	nv_debug(clk, "setting performance state %d\n", pstatei);
	clk->pstate = pstatei;

	if (pfb->ram->calc) {
		int khz = pstate->base.domain[nv_clk_src_mem];
		do {
			ret = pfb->ram->calc(pfb, khz);
			if (ret == 0)
				ret = pfb->ram->prog(pfb);
		} while (ret > 0);
		pfb->ram->tidy(pfb);
	}

	return nouveau_cstate_prog(clk, pstate, 0);
}

static int
nouveau_pstate_calc(struct nouveau_clock *clk)
{
	int pstate, ret = 0;

	nv_trace(clk, "P %d U %d A %d T %d D %d\n", clk->pstate,
		 clk->ustate, clk->astate, clk->tstate, clk->dstate);

	if (clk->state_nr && clk->ustate != -1) {
		pstate = (clk->ustate < 0) ? clk->astate : clk->ustate;
		pstate = min(pstate, clk->state_nr - 1 - clk->tstate);
		pstate = max(pstate, clk->dstate);
	} else {
		pstate = clk->pstate = -1;
	}

	nv_trace(clk, "-> %d\n", pstate);
	if (pstate != clk->pstate)
		ret = nouveau_pstate_prog(clk, pstate);
	return ret;
}

static void
nouveau_pstate_info(struct nouveau_clock *clk, struct nouveau_pstate *pstate)
{
	struct nouveau_clocks *clock = clk->domains - 1;
	struct nouveau_cstate *cstate;
	char info[3][32] = { "", "", "" };
	char name[4] = "--";
	int i = -1;

	if (pstate->pstate != 0xff)
		snprintf(name, sizeof(name), "%02x", pstate->pstate);

	while ((++clock)->name != nv_clk_src_max) {
		u32 lo = pstate->base.domain[clock->name];
		u32 hi = lo;
		if (hi == 0)
			continue;

		nv_debug(clk, "%02x: %10d KHz\n", clock->name, lo);
		list_for_each_entry(cstate, &pstate->list, head) {
			u32 freq = cstate->domain[clock->name];
			lo = min(lo, freq);
			hi = max(hi, freq);
			nv_debug(clk, "%10d KHz\n", freq);
		}

		if (clock->mname && ++i < ARRAY_SIZE(info)) {
			lo /= clock->mdiv;
			hi /= clock->mdiv;
			if (lo == hi) {
				snprintf(info[i], sizeof(info[i]), "%s %d MHz",
					 clock->mname, lo);
			} else {
				snprintf(info[i], sizeof(info[i]),
					 "%s %d-%d MHz", clock->mname, lo, hi);
			}
		}
	}

	nv_info(clk, "%s: %s %s %s\n", name, info[0], info[1], info[2]);
}

static void
nouveau_pstate_del(struct nouveau_pstate *pstate)
{
	struct nouveau_cstate *cstate, *temp;

	list_for_each_entry_safe(cstate, temp, &pstate->list, head) {
		nouveau_cstate_del(cstate);
	}

	list_del(&pstate->head);
	kfree(pstate);
}

static int
nouveau_pstate_new(struct nouveau_clock *clk, int idx)
{
	struct nouveau_bios *bios = nouveau_bios(clk);
	struct nouveau_clocks *domain = clk->domains - 1;
	struct nouveau_pstate *pstate;
	struct nouveau_cstate *cstate;
	struct nvbios_cstepE cstepE;
	struct nvbios_perfE perfE;
	u8  ver, hdr, cnt, len;
	u16 data;

	data = nvbios_perfEp(bios, idx, &ver, &hdr, &cnt, &len, &perfE);
	if (!data)
		return -EINVAL;
	if (perfE.pstate == 0xff)
		return 0;

	pstate = kzalloc(sizeof(*pstate), GFP_KERNEL);
	cstate = &pstate->base;
	if (!pstate)
		return -ENOMEM;

	INIT_LIST_HEAD(&pstate->list);

	pstate->pstate = perfE.pstate;
	pstate->fanspeed = perfE.fanspeed;
	cstate->voltage = perfE.voltage;
	cstate->domain[nv_clk_src_core] = perfE.core;
	cstate->domain[nv_clk_src_shader] = perfE.shader;
	cstate->domain[nv_clk_src_mem] = perfE.memory;
	cstate->domain[nv_clk_src_vdec] = perfE.vdec;
	cstate->domain[nv_clk_src_dom6] = perfE.disp;

	while (ver >= 0x40 && (++domain)->name != nv_clk_src_max) {
		struct nvbios_perfS perfS;
		u8  sver = ver, shdr = hdr;
		u32 perfSe = nvbios_perfSp(bios, data, domain->bios,
					  &sver, &shdr, cnt, len, &perfS);
		if (perfSe == 0 || sver != 0x40)
			continue;

		if (domain->flags & NVKM_CLK_DOM_FLAG_CORE) {
			perfS.v40.freq = nouveau_clock_adjust(clk, false,
							      pstate->pstate,
							      domain->bios,
							      perfS.v40.freq);
		}

		cstate->domain[domain->name] = perfS.v40.freq;
	}

	data = nvbios_cstepEm(bios, pstate->pstate, &ver, &hdr, &cstepE);
	if (data) {
		int idx = cstepE.index;
		do {
			nouveau_cstate_new(clk, idx, pstate);
		} while(idx--);
	}

	nouveau_pstate_info(clk, pstate);
	list_add_tail(&pstate->head, &clk->states);
	clk->state_nr++;
	return 0;
}

/******************************************************************************
 * Adjustment triggers
 *****************************************************************************/
static int
nouveau_clock_ustate_update(struct nouveau_clock *clk, int req)
{
	struct nouveau_pstate *pstate;
	int i = 0;

	/* YKW repellant */
	return -ENOSYS;

	if (req != -1 && req != -2) {
		list_for_each_entry(pstate, &clk->states, head) {
			if (pstate->pstate == req)
				break;
			i++;
		}

		if (pstate->pstate != req)
			return -EINVAL;
		req = i;
	}

	clk->ustate = req;
	return 0;
}

int
nouveau_clock_ustate(struct nouveau_clock *clk, int req)
{
	int ret = nouveau_clock_ustate_update(clk, req);
	if (ret)
		return ret;
	return nouveau_pstate_calc(clk);
}

int
nouveau_clock_astate(struct nouveau_clock *clk, int req, int rel)
{
	if (!rel) clk->astate  = req;
	if ( rel) clk->astate += rel;
	clk->astate = min(clk->astate, clk->state_nr - 1);
	clk->astate = max(clk->astate, 0);
	return nouveau_pstate_calc(clk);
}

int
nouveau_clock_tstate(struct nouveau_clock *clk, int req, int rel)
{
	if (!rel) clk->tstate  = req;
	if ( rel) clk->tstate += rel;
	clk->tstate = min(clk->tstate, 0);
	clk->tstate = max(clk->tstate, -(clk->state_nr - 1));
	return nouveau_pstate_calc(clk);
}

int
nouveau_clock_dstate(struct nouveau_clock *clk, int req, int rel)
{
	if (!rel) clk->dstate  = req;
	if ( rel) clk->dstate += rel;
	clk->dstate = min(clk->dstate, clk->state_nr - 1);
	clk->dstate = max(clk->dstate, 0);
	return nouveau_pstate_calc(clk);
}

/******************************************************************************
 * subdev base class implementation
 *****************************************************************************/
int
_nouveau_clock_init(struct nouveau_object *object)
{
	struct nouveau_clock *clk = (void *)object;
	struct nouveau_clocks *clock = clk->domains;
	int ret;

	memset(&clk->bstate, 0x00, sizeof(clk->bstate));
	INIT_LIST_HEAD(&clk->bstate.list);
	clk->bstate.pstate = 0xff;

	while (clock->name != nv_clk_src_max) {
		ret = clk->read(clk, clock->name);
		if (ret < 0) {
			nv_error(clk, "%02x freq unknown\n", clock->name);
			return ret;
		}
		clk->bstate.base.domain[clock->name] = ret;
		clock++;
	}

	nouveau_pstate_info(clk, &clk->bstate);

	clk->astate = clk->state_nr - 1;
	clk->tstate = 0;
	clk->dstate = 0;
	clk->pstate = -1;
	nouveau_pstate_calc(clk);
	return 0;
}

void
_nouveau_clock_dtor(struct nouveau_object *object)
{
	struct nouveau_clock *clk = (void *)object;
	struct nouveau_pstate *pstate, *temp;

	list_for_each_entry_safe(pstate, temp, &clk->states, head) {
		nouveau_pstate_del(pstate);
	}

	nouveau_subdev_destroy(&clk->base);
}

int
nouveau_clock_create_(struct nouveau_object *parent,
		      struct nouveau_object *engine,
		      struct nouveau_oclass *oclass,
		      struct nouveau_clocks *clocks,
		      int length, void **object)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_clock *clk;
	int ret, idx, arglen;
	const char *mode;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "CLK",
				     "clock", length, object);
	clk = *object;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&clk->states);
	clk->domains = clocks;
	clk->ustate = -1;

	idx = 0;
	do {
		ret = nouveau_pstate_new(clk, idx++);
	} while (ret == 0);

	mode = nouveau_stropt(device->cfgopt, "NvClkMode", &arglen);
	if (mode) {
		if (!strncasecmpz(mode, "disabled", arglen)) {
			clk->ustate = -1;
		} else {
			char save = mode[arglen];
			long v;

			((char *)mode)[arglen] = '\0';
			if (!kstrtol(mode, 0, &v))
				nouveau_clock_ustate_update(clk, v);
			((char *)mode)[arglen] = save;
		}
	}

	return 0;
}
