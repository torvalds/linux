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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <core/object.h>
#include <core/class.h>

#include <subdev/clock.h>

#include "priv.h"

static int
nouveau_control_mthd_pstate_info(struct nouveau_object *object, u32 mthd,
				void *data, u32 size)
{
	struct nouveau_clock *clk = nouveau_clock(object);
	struct nv_control_pstate_info *args = data;

	if (size < sizeof(*args))
		return -EINVAL;

	if (clk) {
		args->count  = clk->state_nr;
		args->ustate = clk->ustate;
		args->pstate = clk->pstate;
	} else {
		args->count  = 0;
		args->ustate = NV_CONTROL_PSTATE_INFO_USTATE_DISABLE;
		args->pstate = NV_CONTROL_PSTATE_INFO_PSTATE_UNKNOWN;
	}

	return 0;
}

static int
nouveau_control_mthd_pstate_attr(struct nouveau_object *object, u32 mthd,
				void *data, u32 size)
{
	struct nouveau_clock *clk = nouveau_clock(object);
	struct nv_control_pstate_attr *args = data;
	struct nouveau_clocks *domain;
	struct nouveau_pstate *pstate;
	struct nouveau_cstate *cstate;
	int i = 0, j = -1;
	u32 lo, hi;

	if ((size < sizeof(*args)) || !clk ||
	    (args->state >= 0 && args->state >= clk->state_nr))
		return -EINVAL;
	domain = clk->domains;

	while (domain->name != nv_clk_src_max) {
		if (domain->mname && ++j == args->index)
			break;
		domain++;
	}

	if (domain->name == nv_clk_src_max)
		return -EINVAL;

	if (args->state != NV_CONTROL_PSTATE_ATTR_STATE_CURRENT) {
		list_for_each_entry(pstate, &clk->states, head) {
			if (i++ == args->state)
				break;
		}

		lo = pstate->base.domain[domain->name];
		hi = lo;
		list_for_each_entry(cstate, &pstate->list, head) {
			lo = min(lo, cstate->domain[domain->name]);
			hi = max(hi, cstate->domain[domain->name]);
		}

		args->state = pstate->pstate;
	} else {
		lo = max(clk->read(clk, domain->name), 0);
		hi = lo;
	}

	snprintf(args->name, sizeof(args->name), "%s", domain->mname);
	snprintf(args->unit, sizeof(args->unit), "MHz");
	args->min = lo / domain->mdiv;
	args->max = hi / domain->mdiv;

	args->index = 0;
	while ((++domain)->name != nv_clk_src_max) {
		if (domain->mname) {
			args->index = ++j;
			break;
		}
	}

	return 0;
}

static int
nouveau_control_mthd_pstate_user(struct nouveau_object *object, u32 mthd,
				void *data, u32 size)
{
	struct nouveau_clock *clk = nouveau_clock(object);
	struct nv_control_pstate_user *args = data;

	if (size < sizeof(*args) || !clk)
		return -EINVAL;

	return nouveau_clock_ustate(clk, args->state);
}

struct nouveau_oclass
nouveau_control_oclass[] = {
	{ .handle = NV_CONTROL_CLASS,
	  .ofuncs = &nouveau_object_ofuncs,
	  .omthds = (struct nouveau_omthds[]) {
		  { NV_CONTROL_PSTATE_INFO,
		    NV_CONTROL_PSTATE_INFO, nouveau_control_mthd_pstate_info },
		  { NV_CONTROL_PSTATE_ATTR,
		    NV_CONTROL_PSTATE_ATTR, nouveau_control_mthd_pstate_attr },
		  { NV_CONTROL_PSTATE_USER,
		    NV_CONTROL_PSTATE_USER, nouveau_control_mthd_pstate_user },
		  {},
	  },
	},
	{}
};
