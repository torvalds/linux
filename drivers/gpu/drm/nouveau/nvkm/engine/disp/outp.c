/*
 * Copyright 2014 Red Hat Inc.
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
#include "outp.h"
#include "ior.h"

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/i2c.h>

void
nvkm_outp_route(struct nvkm_disp *disp)
{
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	list_for_each_entry(ior, &disp->ior, head) {
		if ((outp = ior->arm.outp) && ior->arm.outp != ior->asy.outp) {
			OUTP_DBG(outp, "release %s", ior->name);
			if (ior->func->route.set)
				ior->func->route.set(outp, NULL);
			ior->arm.outp = NULL;
		}
	}

	list_for_each_entry(ior, &disp->ior, head) {
		if ((outp = ior->asy.outp)) {
			OUTP_DBG(outp, "acquire %s", ior->name);
			if (ior->asy.outp != ior->arm.outp) {
				if (ior->func->route.set)
					ior->func->route.set(outp, ior);
				ior->arm.outp = ior->asy.outp;
			}
		}
	}
}

static enum nvkm_ior_proto
nvkm_outp_xlat(struct nvkm_outp *outp, enum nvkm_ior_type *type)
{
	switch (outp->info.location) {
	case 0:
		switch (outp->info.type) {
		case DCB_OUTPUT_ANALOG: *type = DAC; return  CRT;
		case DCB_OUTPUT_TMDS  : *type = SOR; return TMDS;
		case DCB_OUTPUT_LVDS  : *type = SOR; return LVDS;
		case DCB_OUTPUT_DP    : *type = SOR; return   DP;
		default:
			break;
		}
		break;
	case 1:
		switch (outp->info.type) {
		case DCB_OUTPUT_TMDS: *type = PIOR; return TMDS;
		case DCB_OUTPUT_DP  : *type = PIOR; return TMDS; /* not a bug */
		default:
			break;
		}
		break;
	default:
		break;
	}
	WARN_ON(1);
	return UNKNOWN;
}

void
nvkm_outp_release(struct nvkm_outp *outp, u8 user)
{
	struct nvkm_ior *ior = outp->ior;
	OUTP_TRACE(outp, "release %02x &= %02x %p", outp->acquired, ~user, ior);
	if (ior) {
		outp->acquired &= ~user;
		if (!outp->acquired) {
			outp->ior->asy.outp = NULL;
			outp->ior = NULL;
		}
	}
}

static inline int
nvkm_outp_acquire_ior(struct nvkm_outp *outp, u8 user, struct nvkm_ior *ior)
{
	outp->ior = ior;
	outp->ior->asy.outp = outp;
	outp->ior->asy.link = outp->info.sorconf.link;
	outp->acquired |= user;
	return 0;
}

int
nvkm_outp_acquire(struct nvkm_outp *outp, u8 user)
{
	struct nvkm_ior *ior = outp->ior;
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;

	OUTP_TRACE(outp, "acquire %02x |= %02x %p", outp->acquired, user, ior);
	if (ior) {
		outp->acquired |= user;
		return 0;
	}

	/* Lookup a compatible, and unused, OR to assign to the device. */
	proto = nvkm_outp_xlat(outp, &type);
	if (proto == UNKNOWN)
		return -ENOSYS;

	/* First preference is to reuse the OR that is currently armed
	 * on HW, if any, in order to prevent unnecessary switching.
	 */
	list_for_each_entry(ior, &outp->disp->ior, head) {
		if (!ior->asy.outp && ior->arm.outp == outp)
			return nvkm_outp_acquire_ior(outp, user, ior);
	}

	/* Failing that, a completely unused OR is the next best thing. */
	list_for_each_entry(ior, &outp->disp->ior, head) {
		if (!ior->asy.outp && ior->type == type && !ior->arm.outp &&
		    (ior->func->route.set || ior->id == __ffs(outp->info.or)))
			return nvkm_outp_acquire_ior(outp, user, ior);
	}

	/* Last resort is to assign an OR that's already active on HW,
	 * but will be released during the next modeset.
	 */
	list_for_each_entry(ior, &outp->disp->ior, head) {
		if (!ior->asy.outp && ior->type == type &&
		    (ior->func->route.set || ior->id == __ffs(outp->info.or)))
			return nvkm_outp_acquire_ior(outp, user, ior);
	}

	return -ENOSPC;
}

void
nvkm_outp_fini(struct nvkm_outp *outp)
{
	if (outp->func->fini)
		outp->func->fini(outp);
}

static void
nvkm_outp_init_route(struct nvkm_outp *outp)
{
	struct nvkm_disp *disp = outp->disp;
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;
	struct nvkm_ior *ior;
	int id, link;

	/* Find any OR from the class that is able to support this device. */
	proto = nvkm_outp_xlat(outp, &type);
	if (proto == UNKNOWN)
		return;

	ior = nvkm_ior_find(disp, type, -1);
	if (!ior) {
		WARN_ON(1);
		return;
	}

	/* Determine the specific OR, if any, this device is attached to. */
	if (ior->func->route.get) {
		id = ior->func->route.get(outp, &link);
		if (id < 0) {
			OUTP_DBG(outp, "no route");
			return;
		}
	} else {
		/* Prior to DCB 4.1, this is hardwired like so. */
		id   = ffs(outp->info.or) - 1;
		link = (ior->type == SOR) ? outp->info.sorconf.link : 0;
	}

	ior = nvkm_ior_find(disp, type, id);
	if (!ior) {
		WARN_ON(1);
		return;
	}

	/* Determine if the OR is already configured for this device. */
	ior->func->state(ior, &ior->arm);
	if (!ior->arm.head || ior->arm.proto != proto) {
		OUTP_DBG(outp, "no heads (%x %d %d)", ior->arm.head,
			 ior->arm.proto, proto);
		return;
	}

	OUTP_DBG(outp, "on %s link %x", ior->name, ior->arm.link);
	ior->arm.outp = outp;
}

void
nvkm_outp_init(struct nvkm_outp *outp)
{
	nvkm_outp_init_route(outp);
	if (outp->func->init)
		outp->func->init(outp);
}

void
nvkm_outp_del(struct nvkm_outp **poutp)
{
	struct nvkm_outp *outp = *poutp;
	if (outp && !WARN_ON(!outp->func)) {
		if (outp->func->dtor)
			*poutp = outp->func->dtor(outp);
		kfree(*poutp);
		*poutp = NULL;
	}
}

int
nvkm_outp_ctor(const struct nvkm_outp_func *func, struct nvkm_disp *disp,
	       int index, struct dcb_output *dcbE, struct nvkm_outp *outp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;

	outp->func = func;
	outp->disp = disp;
	outp->index = index;
	outp->info = *dcbE;
	outp->i2c = nvkm_i2c_bus_find(i2c, dcbE->i2c_index);
	outp->or = ffs(outp->info.or) - 1;

	OUTP_DBG(outp, "type %02x loc %d or %d link %d con %x "
		       "edid %x bus %d head %x",
		 outp->info.type, outp->info.location, outp->info.or,
		 outp->info.type >= 2 ? outp->info.sorconf.link : 0,
		 outp->info.connector, outp->info.i2c_index,
		 outp->info.bus, outp->info.heads);

	/* Cull output paths we can't map to an output resource. */
	proto = nvkm_outp_xlat(outp, &type);
	if (proto == UNKNOWN)
		return -ENODEV;

	return 0;
}

static const struct nvkm_outp_func
nvkm_outp = {
};

int
nvkm_outp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
	      struct nvkm_outp **poutp)
{
	if (!(*poutp = kzalloc(sizeof(**poutp), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_outp_ctor(&nvkm_outp, disp, index, dcbE, *poutp);
}
