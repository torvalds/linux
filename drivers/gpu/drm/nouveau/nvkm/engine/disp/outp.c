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
#include "conn.h"
#include "dp.h"
#include "ior.h"

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/gpio.h>
#include <subdev/i2c.h>

static void
nvkm_outp_route(struct nvkm_disp *disp)
{
	struct nvkm_outp *outp;
	struct nvkm_ior *ior;

	list_for_each_entry(ior, &disp->iors, head) {
		if ((outp = ior->arm.outp) && ior->arm.outp != ior->asy.outp) {
			OUTP_DBG(outp, "release %s", ior->name);
			if (ior->func->route.set)
				ior->func->route.set(outp, NULL);
			ior->arm.outp = NULL;
		}
	}

	list_for_each_entry(ior, &disp->iors, head) {
		if ((outp = ior->asy.outp)) {
			if (ior->asy.outp != ior->arm.outp) {
				OUTP_DBG(outp, "acquire %s", ior->name);
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
		case DCB_OUTPUT_TV    : *type = DAC; return   TV;
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
nvkm_outp_release_or(struct nvkm_outp *outp, u8 user)
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

int
nvkm_outp_acquire_ior(struct nvkm_outp *outp, u8 user, struct nvkm_ior *ior)
{
	outp->ior = ior;
	outp->ior->asy.outp = outp;
	outp->ior->asy.link = outp->info.sorconf.link;
	outp->acquired |= user;
	return 0;
}

static inline int
nvkm_outp_acquire_hda(struct nvkm_outp *outp, enum nvkm_ior_type type,
		      u8 user, bool hda)
{
	struct nvkm_ior *ior;

	/* Failing that, a completely unused OR is the next best thing. */
	list_for_each_entry(ior, &outp->disp->iors, head) {
		if (!ior->identity && ior->hda == hda &&
		    !ior->asy.outp && ior->type == type && !ior->arm.outp &&
		    (ior->func->route.set || ior->id == __ffs(outp->info.or)))
			return nvkm_outp_acquire_ior(outp, user, ior);
	}

	/* Last resort is to assign an OR that's already active on HW,
	 * but will be released during the next modeset.
	 */
	list_for_each_entry(ior, &outp->disp->iors, head) {
		if (!ior->identity && ior->hda == hda &&
		    !ior->asy.outp && ior->type == type &&
		    (ior->func->route.set || ior->id == __ffs(outp->info.or)))
			return nvkm_outp_acquire_ior(outp, user, ior);
	}

	return -ENOSPC;
}

int
nvkm_outp_acquire_or(struct nvkm_outp *outp, u8 user, bool hda)
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

	/* Deal with panels requiring identity-mapped SOR assignment. */
	if (outp->identity) {
		ior = nvkm_ior_find(outp->disp, SOR, ffs(outp->info.or) - 1);
		if (WARN_ON(!ior))
			return -ENOSPC;
		return nvkm_outp_acquire_ior(outp, user, ior);
	}

	/* First preference is to reuse the OR that is currently armed
	 * on HW, if any, in order to prevent unnecessary switching.
	 */
	list_for_each_entry(ior, &outp->disp->iors, head) {
		if (!ior->identity && !ior->asy.outp && ior->arm.outp == outp) {
			/*XXX: For various complicated reasons, we can't outright switch
			 *     the boot-time OR on the first modeset without some fairly
			 *     invasive changes.
			 *
			 *     The systems that were fixed by modifying the OR selection
			 *     code to account for HDA support shouldn't regress here as
			 *     the HDA-enabled ORs match the relevant output's pad macro
			 *     index, and the firmware seems to select an OR this way.
			 *
			 *     This warning is to make it obvious if that proves wrong.
			 */
			WARN_ON(hda && !ior->hda);
			return nvkm_outp_acquire_ior(outp, user, ior);
		}
	}

	/* If we don't need HDA, first try to acquire an OR that doesn't
	 * support it to leave free the ones that do.
	 */
	if (!hda) {
		if (!nvkm_outp_acquire_hda(outp, type, user, false))
			return 0;

		/* Use a HDA-supporting SOR anyway. */
		return nvkm_outp_acquire_hda(outp, type, user, true);
	}

	/* We want HDA, try to acquire an OR that supports it. */
	if (!nvkm_outp_acquire_hda(outp, type, user, true))
		return 0;

	/* There weren't any free ORs that support HDA, grab one that
	 * doesn't and at least allow display to work still.
	 */
	return nvkm_outp_acquire_hda(outp, type, user, false);
}

int
nvkm_outp_bl_set(struct nvkm_outp *outp, int level)
{
	int ret;

	ret = nvkm_outp_acquire_or(outp, NVKM_OUTP_PRIV, false);
	if (ret)
		return ret;

	if (outp->ior->func->bl)
		ret = outp->ior->func->bl->set(outp->ior, level);
	else
		ret = -EINVAL;

	nvkm_outp_release_or(outp, NVKM_OUTP_PRIV);
	return ret;
}

int
nvkm_outp_bl_get(struct nvkm_outp *outp)
{
	int ret;

	ret = nvkm_outp_acquire_or(outp, NVKM_OUTP_PRIV, false);
	if (ret)
		return ret;

	if (outp->ior->func->bl)
		ret = outp->ior->func->bl->get(outp->ior);
	else
		ret = -EINVAL;

	nvkm_outp_release_or(outp, NVKM_OUTP_PRIV);
	return ret;
}

int
nvkm_outp_detect(struct nvkm_outp *outp)
{
	struct nvkm_gpio *gpio = outp->disp->engine.subdev.device->gpio;
	int ret = -EINVAL;

	if (outp->conn->info.hpd != DCB_GPIO_UNUSED) {
		ret = nvkm_gpio_get(gpio, 0, DCB_GPIO_UNUSED, outp->conn->info.hpd);
		if (ret < 0)
			return ret;
		if (ret)
			return 1;

		/*TODO: Look into returning NOT_PRESENT if !HPD on DVI/HDMI.
		 *
		 *      It's uncertain whether this is accurate for all older chipsets,
		 *      so we're returning UNKNOWN, and the DRM will probe DDC instead.
		 */
		if (outp->info.type == DCB_OUTPUT_DP)
			return 0;
	}

	return ret;
}

void
nvkm_outp_release(struct nvkm_outp *outp)
{
	nvkm_outp_release_or(outp, NVKM_OUTP_USER);
	nvkm_outp_route(outp->disp);
}

int
nvkm_outp_acquire(struct nvkm_outp *outp, bool hda)
{
	int ret = nvkm_outp_acquire_or(outp, NVKM_OUTP_USER, hda);

	if (ret)
		return ret;

	nvkm_outp_route(outp->disp);
	return 0;
}

struct nvkm_ior *
nvkm_outp_inherit(struct nvkm_outp *outp)
{
	struct nvkm_disp *disp = outp->disp;
	struct nvkm_ior *ior;
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;
	int id, link;

	/* Find any OR from the class that is able to support this device. */
	proto = nvkm_outp_xlat(outp, &type);
	if (proto == UNKNOWN)
		return NULL;

	ior = nvkm_ior_find(disp, type, -1);
	if (WARN_ON(!ior))
		return NULL;

	/* Determine the specific OR, if any, this device is attached to. */
	if (ior->func->route.get) {
		id = ior->func->route.get(outp, &link);
		if (id < 0) {
			OUTP_DBG(outp, "no route");
			return NULL;
		}
	} else {
		/* Prior to DCB 4.1, this is hardwired like so. */
		id   = ffs(outp->info.or) - 1;
		link = (ior->type == SOR) ? outp->info.sorconf.link : 0;
	}

	ior = nvkm_ior_find(disp, type, id);
	if (WARN_ON(!ior))
		return NULL;

	return ior;
}

void
nvkm_outp_init(struct nvkm_outp *outp)
{
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;
	struct nvkm_ior *ior;

	/* Find any OR from the class that is able to support this device. */
	proto = nvkm_outp_xlat(outp, &type);
	ior = outp->func->inherit(outp);
	if (!ior)
		return;

	/* Determine if the OR is already configured for this device. */
	ior->func->state(ior, &ior->arm);
	if (!ior->arm.head || ior->arm.proto != proto) {
		OUTP_DBG(outp, "no heads (%x %d %d)", ior->arm.head,
			 ior->arm.proto, proto);

		/* The EFI GOP driver on Ampere can leave unused DP links routed,
		 * which we don't expect.  The DisableLT IED script *should* get
		 * us back to where we need to be.
		 */
		if (ior->func->route.get && !ior->arm.head && outp->info.type == DCB_OUTPUT_DP)
			nvkm_dp_disable(outp, ior);

		return;
	}

	OUTP_DBG(outp, "on %s link %x", ior->name, ior->arm.link);
	ior->arm.outp = outp;
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
nvkm_outp_new_(const struct nvkm_outp_func *func, struct nvkm_disp *disp,
	       int index, struct dcb_output *dcbE, struct nvkm_outp **poutp)
{
	struct nvkm_i2c *i2c = disp->engine.subdev.device->i2c;
	struct nvkm_outp *outp;
	enum nvkm_ior_proto proto;
	enum nvkm_ior_type type;

	if (!(outp = *poutp = kzalloc(sizeof(*outp), GFP_KERNEL)))
		return -ENOMEM;

	outp->func = func;
	outp->disp = disp;
	outp->index = index;
	outp->info = *dcbE;
	outp->i2c = nvkm_i2c_bus_find(i2c, dcbE->i2c_index);

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
	.init = nvkm_outp_init,
	.detect = nvkm_outp_detect,
	.inherit = nvkm_outp_inherit,
	.acquire = nvkm_outp_acquire,
	.release = nvkm_outp_release,
	.bl.get = nvkm_outp_bl_get,
	.bl.set = nvkm_outp_bl_set,
};

int
nvkm_outp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
	      struct nvkm_outp **poutp)
{
	return nvkm_outp_new_(&nvkm_outp, disp, index, dcbE, poutp);
}
