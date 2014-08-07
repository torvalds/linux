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

#include <core/os.h>
#include <core/class.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/timer.h>
#include <subdev/i2c.h>

#include "nv50.h"

/******************************************************************************
 * TMDS
 *****************************************************************************/

static int
nv50_pior_tmds_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *info, u32 index,
		    struct nouveau_object **pobject)
{
	struct nouveau_i2c *i2c = nouveau_i2c(parent);
	struct nvkm_output *outp;
	int ret;

	ret = nvkm_output_create(parent, engine, oclass, info, index, &outp);
	*pobject = nv_object(outp);
	if (ret)
		return ret;

	outp->edid = i2c->find_type(i2c, NV_I2C_TYPE_EXTDDC(outp->info.extdev));
	return 0;
}

struct nvkm_output_impl
nv50_pior_tmds_impl = {
	.base.handle = DCB_OUTPUT_TMDS | 0x0100,
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_pior_tmds_ctor,
		.dtor = _nvkm_output_dtor,
		.init = _nvkm_output_init,
		.fini = _nvkm_output_fini,
	},
};

/******************************************************************************
 * DisplayPort
 *****************************************************************************/

static int
nv50_pior_dp_pattern(struct nvkm_output_dp *outp, int pattern)
{
	struct nouveau_i2c_port *port = outp->base.edid;
	if (port && port->func->pattern)
		return port->func->pattern(port, pattern);
	return port ? 0 : -ENODEV;
}

static int
nv50_pior_dp_lnk_pwr(struct nvkm_output_dp *outp, int nr)
{
	return 0;
}

static int
nv50_pior_dp_lnk_ctl(struct nvkm_output_dp *outp, int nr, int bw, bool ef)
{
	struct nouveau_i2c_port *port = outp->base.edid;
	if (port && port->func->lnk_ctl)
		return port->func->lnk_ctl(port, nr, bw, ef);
	return port ? 0 : -ENODEV;
}

static int
nv50_pior_dp_drv_ctl(struct nvkm_output_dp *outp, int ln, int vs, int pe, int pc)
{
	struct nouveau_i2c_port *port = outp->base.edid;
	if (port && port->func->drv_ctl)
		return port->func->drv_ctl(port, ln, vs, pe);
	return port ? 0 : -ENODEV;
}

static int
nv50_pior_dp_ctor(struct nouveau_object *parent,
		  struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *info, u32 index,
		  struct nouveau_object **pobject)
{
	struct nouveau_i2c *i2c = nouveau_i2c(parent);
	struct nvkm_output_dp *outp;
	int ret;

	ret = nvkm_output_dp_create(parent, engine, oclass, info, index, &outp);
	*pobject = nv_object(outp);
	if (ret)
		return ret;

	outp->base.edid = i2c->find_type(i2c, NV_I2C_TYPE_EXTAUX(
					 outp->base.info.extdev));
	return 0;
}

struct nvkm_output_dp_impl
nv50_pior_dp_impl = {
	.base.base.handle = DCB_OUTPUT_DP | 0x0010,
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_pior_dp_ctor,
		.dtor = _nvkm_output_dp_dtor,
		.init = _nvkm_output_dp_init,
		.fini = _nvkm_output_dp_fini,
	},
	.pattern = nv50_pior_dp_pattern,
	.lnk_pwr = nv50_pior_dp_lnk_pwr,
	.lnk_ctl = nv50_pior_dp_lnk_ctl,
	.drv_ctl = nv50_pior_dp_drv_ctl,
};

/******************************************************************************
 * General PIOR handling
 *****************************************************************************/

int
nv50_pior_power(struct nv50_disp_priv *priv, int or, u32 data)
{
	const u32 stat = data & NV50_DISP_PIOR_PWR_STATE;
	const u32 soff = (or * 0x800);
	nv_wait(priv, 0x61e004 + soff, 0x80000000, 0x00000000);
	nv_mask(priv, 0x61e004 + soff, 0x80000101, 0x80000000 | stat);
	nv_wait(priv, 0x61e004 + soff, 0x80000000, 0x00000000);
	return 0;
}

int
nv50_pior_mthd(struct nouveau_object *object, u32 mthd, void *args, u32 size)
{
	struct nv50_disp_priv *priv = (void *)object->engine;
	const u8 type = (mthd & NV50_DISP_PIOR_MTHD_TYPE) >> 12;
	const u8 or   = (mthd & NV50_DISP_PIOR_MTHD_OR);
	u32 *data = args;
	int ret;

	if (size < sizeof(u32))
		return -EINVAL;

	mthd &= ~NV50_DISP_PIOR_MTHD_TYPE;
	mthd &= ~NV50_DISP_PIOR_MTHD_OR;
	switch (mthd) {
	case NV50_DISP_PIOR_PWR:
		ret = priv->pior.power(priv, or, data[0]);
		priv->pior.type[or] = type;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}
