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
#include <subdev/clk.h>
#include <subdev/bios.h>
#include <subdev/bios/boost.h>
#include <subdev/bios/cstep.h>
#include <subdev/bios/perf.h>
#include <subdev/fb.h>
#include <subdev/therm.h>
#include <subdev/volt.h>

#include <core/device.h>
#include <core/option.h>

/******************************************************************************
 * misc
 *****************************************************************************/
static u32
nvkm_clk_adjust(struct nvkm_clk *clk, bool adjust,
		u8 pstate, u8 domain, u32 input)
{
	struct nvkm_bios *bios = nvkm_bios(clk);
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
nvkm_cstate_prog(struct nvkm_clk *clk, struct nvkm_pstate *pstate, int cstatei)
{
	struct nvkm_therm *ptherm = nvkm_therm(clk);
	struct nvkm_volt *volt = nvkm_volt(clk);
	struct nvkm_cstate *cstate;
	int ret;

	if (!list_empty(&pstate->list)) {
		cstate = list_entry(pstate->list.prev, typeof(*cstate), head);
	} else {
		cstate = &pstate->base;
	}

	if (ptherm) {
		ret = nvkm_therm_cstate(ptherm, pstate->fanspeed, +1);
		if (ret && ret != -ENODEV) {
			nv_error(clk, "failed to raise fan speed: %d\n", ret);
			return ret;
		}
	}

	if (volt) {
		ret = volt->set_id(volt, cstate->voltage, +1);
		if (ret && ret != -ENODEV) {
			nv_error(clk, "failed to raise voltage: %d\n", ret);
			return ret;
		}
	}

	ret = clk->calc(clk, cstate);
	if (ret == 0) {
		ret = clk->prog(clk);
		clk->tidy(clk);
	}

	if (volt) {
		ret = volt->set_id(volt, cstate->voltage, -1);
		if (ret && ret != -ENODEV)
			nv_error(clk, "failed to lower voltage: %d\n", ret);
	}

	if (ptherm) {
		ret = nvkm_therm_cstate(ptherm, pstate->fanspeed, -1);
		if (ret && ret != -ENODEV)
			nv_error(clk, "failed to lower fan speed: %d\n", ret);
	}

	return 0;
}

static void
nvkm_cstate_del(struct nvkm_cstate *cstate)
{
	list_del(&cstate->head);
	kfree(cstate);
}

static int
nvkm_cstate_new(struct nvkm_clk *clk, int idx, struct nvkm_pstate *pstate)
{
	struct nvkm_bios *bios = nvkm_bios(clk);
	struct nvkm_domain *domain = clk->domains;
	struct nvkm_cstate *cstate = NULL;
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
			u32 freq = nvkm_clk_adjust(clk, true, pstate->pstate,
						   domain->bios, cstepX.freq);
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
nvkm_pstate_prog(struct nvkm_clk *clk, int pstatei)
{
	struct nvkm_fb *pfb = nvkm_fb(clk);
	struct nvkm_pstate *pstate;
	int ret, idx = 0;

	list_for_each_entry(pstate, &clk->states, head) {
		if (idx++ == pstatei)
			break;
	}

	nv_debug(clk, "setting performance state %d\n", pstatei);
	clk->pstate = pstatei;

	if (pfb->ram && pfb->ram->calc) {
		int khz = pstate->base.domain[nv_clk_src_mem];
		do {
			ret = pfb->ram->calc(pfb, khz);
			if (ret == 0)
				ret = pfb->ram->prog(pfb);
		} while (ret > 0);
		pfb->ram->tidy(pfb);
	}

	return nvkm_cstate_prog(clk, pstate, 0);
}

static void
nvkm_pstate_work(struct work_struct *work)
{
	struct nvkm_clk *clk = container_of(work, typeof(*clk), work);
	int pstate;

	if (!atomic_xchg(&clk->waiting, 0))
		return;
	clk->pwrsrc = power_supply_is_system_supplied();

	nv_trace(clk, "P %d PWR %d U(AC) %d U(DC) %d A %d T %d D %d\n",
		 clk->pstate, clk->pwrsrc, clk->ustate_ac, clk->ustate_dc,
		 clk->astate, clk->tstate, clk->dstate);

	pstate = clk->pwrsrc ? clk->ustate_ac : clk->ustate_dc;
	if (clk->state_nr && pstate != -1) {
		pstate = (pstate < 0) ? clk->astate : pstate;
		pstate = min(pstate, clk->state_nr - 1 - clk->tstate);
		pstate = max(pstate, clk->dstate);
	} else {
		pstate = clk->pstate = -1;
	}

	nv_trace(clk, "-> %d\n", pstate);
	if (pstate != clk->pstate) {
		int ret = nvkm_pstate_prog(clk, pstate);
		if (ret) {
			nv_error(clk, "error setting pstate %d: %d\n",
				 pstate, ret);
		}
	}

	wake_up_all(&clk->wait);
	nvkm_notify_get(&clk->pwrsrc_ntfy);
}

static int
nvkm_pstate_calc(struct nvkm_clk *clk, bool wait)
{
	atomic_set(&clk->waiting, 1);
	schedule_work(&clk->work);
	if (wait)
		wait_event(clk->wait, !atomic_read(&clk->waiting));
	return 0;
}

static void
nvkm_pstate_info(struct nvkm_clk *clk, struct nvkm_pstate *pstate)
{
	struct nvkm_domain *clock = clk->domains - 1;
	struct nvkm_cstate *cstate;
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
nvkm_pstate_del(struct nvkm_pstate *pstate)
{
	struct nvkm_cstate *cstate, *temp;

	list_for_each_entry_safe(cstate, temp, &pstate->list, head) {
		nvkm_cstate_del(cstate);
	}

	list_del(&pstate->head);
	kfree(pstate);
}

static int
nvkm_pstate_new(struct nvkm_clk *clk, int idx)
{
	struct nvkm_bios *bios = nvkm_bios(clk);
	struct nvkm_domain *domain = clk->domains - 1;
	struct nvkm_pstate *pstate;
	struct nvkm_cstate *cstate;
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
			perfS.v40.freq = nvkm_clk_adjust(clk, false,
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
			nvkm_cstate_new(clk, idx, pstate);
		} while(idx--);
	}

	nvkm_pstate_info(clk, pstate);
	list_add_tail(&pstate->head, &clk->states);
	clk->state_nr++;
	return 0;
}

/******************************************************************************
 * Adjustment triggers
 *****************************************************************************/
static int
nvkm_clk_ustate_update(struct nvkm_clk *clk, int req)
{
	struct nvkm_pstate *pstate;
	int i = 0;

	if (!clk->allow_reclock)
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

	return req + 2;
}

static int
nvkm_clk_nstate(struct nvkm_clk *clk, const char *mode, int arglen)
{
	int ret = 1;

	if (clk->allow_reclock && !strncasecmpz(mode, "auto", arglen))
		return -2;

	if (strncasecmpz(mode, "disabled", arglen)) {
		char save = mode[arglen];
		long v;

		((char *)mode)[arglen] = '\0';
		if (!kstrtol(mode, 0, &v)) {
			ret = nvkm_clk_ustate_update(clk, v);
			if (ret < 0)
				ret = 1;
		}
		((char *)mode)[arglen] = save;
	}

	return ret - 2;
}

int
nvkm_clk_ustate(struct nvkm_clk *clk, int req, int pwr)
{
	int ret = nvkm_clk_ustate_update(clk, req);
	if (ret >= 0) {
		if (ret -= 2, pwr) clk->ustate_ac = ret;
		else		   clk->ustate_dc = ret;
		return nvkm_pstate_calc(clk, true);
	}
	return ret;
}

int
nvkm_clk_astate(struct nvkm_clk *clk, int req, int rel, bool wait)
{
	if (!rel) clk->astate  = req;
	if ( rel) clk->astate += rel;
	clk->astate = min(clk->astate, clk->state_nr - 1);
	clk->astate = max(clk->astate, 0);
	return nvkm_pstate_calc(clk, wait);
}

int
nvkm_clk_tstate(struct nvkm_clk *clk, int req, int rel)
{
	if (!rel) clk->tstate  = req;
	if ( rel) clk->tstate += rel;
	clk->tstate = min(clk->tstate, 0);
	clk->tstate = max(clk->tstate, -(clk->state_nr - 1));
	return nvkm_pstate_calc(clk, true);
}

int
nvkm_clk_dstate(struct nvkm_clk *clk, int req, int rel)
{
	if (!rel) clk->dstate  = req;
	if ( rel) clk->dstate += rel;
	clk->dstate = min(clk->dstate, clk->state_nr - 1);
	clk->dstate = max(clk->dstate, 0);
	return nvkm_pstate_calc(clk, true);
}

static int
nvkm_clk_pwrsrc(struct nvkm_notify *notify)
{
	struct nvkm_clk *clk =
		container_of(notify, typeof(*clk), pwrsrc_ntfy);
	nvkm_pstate_calc(clk, false);
	return NVKM_NOTIFY_DROP;
}

/******************************************************************************
 * subdev base class implementation
 *****************************************************************************/

int
_nvkm_clk_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_clk *clk = (void *)object;
	nvkm_notify_put(&clk->pwrsrc_ntfy);
	return nvkm_subdev_fini(&clk->base, suspend);
}

int
_nvkm_clk_init(struct nvkm_object *object)
{
	struct nvkm_clk *clk = (void *)object;
	struct nvkm_domain *clock = clk->domains;
	int ret;

	ret = nvkm_subdev_init(&clk->base);
	if (ret)
		return ret;

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

	nvkm_pstate_info(clk, &clk->bstate);

	clk->astate = clk->state_nr - 1;
	clk->tstate = 0;
	clk->dstate = 0;
	clk->pstate = -1;
	nvkm_pstate_calc(clk, true);
	return 0;
}

void
_nvkm_clk_dtor(struct nvkm_object *object)
{
	struct nvkm_clk *clk = (void *)object;
	struct nvkm_pstate *pstate, *temp;

	nvkm_notify_fini(&clk->pwrsrc_ntfy);

	list_for_each_entry_safe(pstate, temp, &clk->states, head) {
		nvkm_pstate_del(pstate);
	}

	nvkm_subdev_destroy(&clk->base);
}

int
nvkm_clk_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, struct nvkm_domain *clocks,
		 struct nvkm_pstate *pstates, int nb_pstates,
		 bool allow_reclock, int length, void **object)
{
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_clk *clk;
	int ret, idx, arglen;
	const char *mode;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "CLK",
				  "clock", length, object);
	clk = *object;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&clk->states);
	clk->domains = clocks;
	clk->ustate_ac = -1;
	clk->ustate_dc = -1;

	INIT_WORK(&clk->work, nvkm_pstate_work);
	init_waitqueue_head(&clk->wait);
	atomic_set(&clk->waiting, 0);

	/* If no pstates are provided, try and fetch them from the BIOS */
	if (!pstates) {
		idx = 0;
		do {
			ret = nvkm_pstate_new(clk, idx++);
		} while (ret == 0);
	} else {
		for (idx = 0; idx < nb_pstates; idx++)
			list_add_tail(&pstates[idx].head, &clk->states);
		clk->state_nr = nb_pstates;
	}

	clk->allow_reclock = allow_reclock;

	ret = nvkm_notify_init(NULL, &device->event, nvkm_clk_pwrsrc, true,
			       NULL, 0, 0, &clk->pwrsrc_ntfy);
	if (ret)
		return ret;

	mode = nvkm_stropt(device->cfgopt, "NvClkMode", &arglen);
	if (mode) {
		clk->ustate_ac = nvkm_clk_nstate(clk, mode, arglen);
		clk->ustate_dc = nvkm_clk_nstate(clk, mode, arglen);
	}

	mode = nvkm_stropt(device->cfgopt, "NvClkModeAC", &arglen);
	if (mode)
		clk->ustate_ac = nvkm_clk_nstate(clk, mode, arglen);

	mode = nvkm_stropt(device->cfgopt, "NvClkModeDC", &arglen);
	if (mode)
		clk->ustate_dc = nvkm_clk_nstate(clk, mode, arglen);

	return 0;
}
