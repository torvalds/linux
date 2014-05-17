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

#include <subdev/i2c.h>

#include "outpdp.h"
#include "conn.h"

static int
nvkm_output_dp_service(void *data, u32 type, int index)
{
	struct nvkm_output_dp *outp = data;
	DBG("IRQ: %d\n", type);
	return NVKM_EVENT_KEEP;
}

static int
nvkm_output_dp_hotplug(void *data, u32 type, int index)
{
	struct nvkm_output_dp *outp = data;
	DBG("HPD: %d\n", type);
	return NVKM_EVENT_KEEP;
}

int
_nvkm_output_dp_fini(struct nouveau_object *object, bool suspend)
{
	struct nvkm_output_dp *outp = (void *)object;
	nouveau_event_put(outp->irq);
	return nvkm_output_fini(&outp->base, suspend);
}

int
_nvkm_output_dp_init(struct nouveau_object *object)
{
	struct nvkm_output_dp *outp = (void *)object;
	return nvkm_output_init(&outp->base);
}

void
_nvkm_output_dp_dtor(struct nouveau_object *object)
{
	struct nvkm_output_dp *outp = (void *)object;
	nouveau_event_ref(NULL, &outp->irq);
	nvkm_output_destroy(&outp->base);
}

int
nvkm_output_dp_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass,
		       struct dcb_output *info, int index,
		       int length, void **pobject)
{
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nouveau_i2c *i2c = nouveau_i2c(parent);
	struct nvkm_output_dp *outp;
	u8  hdr, cnt, len;
	u32 data;
	int ret;

	ret = nvkm_output_create_(parent, engine, oclass, info, index,
				  length, pobject);
	outp = *pobject;
	if (ret)
		return ret;

	nouveau_event_ref(NULL, &outp->base.conn->hpd.event);

	/* access to the aux channel is not optional... */
	if (!outp->base.edid) {
		ERR("aux channel not found\n");
		return -ENODEV;
	}

	/* nor is the bios data for this output... */
	data = nvbios_dpout_match(bios, outp->base.info.hasht,
				  outp->base.info.hashm, &outp->version,
				  &hdr, &cnt, &len, &outp->info);
	if (!data) {
		ERR("no bios dp data\n");
		return -ENODEV;
	}

	DBG("bios dp %02x %02x %02x %02x\n", outp->version, hdr, cnt, len);

	/* link maintenance */
	ret = nouveau_event_new(i2c->ntfy, NVKM_I2C_IRQ, outp->base.edid->index,
				nvkm_output_dp_service, outp, &outp->irq);
	if (ret) {
		ERR("error monitoring aux irq event: %d\n", ret);
		return ret;
	}

	/* hotplug detect, replaces gpio-based mechanism with aux events */
	ret = nouveau_event_new(i2c->ntfy, NVKM_I2C_PLUG | NVKM_I2C_UNPLUG,
				outp->base.edid->index,
				nvkm_output_dp_hotplug, outp,
			       &outp->base.conn->hpd.event);
	if (ret) {
		ERR("error monitoring aux hpd events: %d\n", ret);
		return ret;
	}

	return 0;
}

int
_nvkm_output_dp_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *info, u32 index,
		     struct nouveau_object **pobject)
{
	struct nvkm_output_dp *outp;
	int ret;

	ret = nvkm_output_dp_create(parent, engine, oclass, info, index, &outp);
	*pobject = nv_object(outp);
	if (ret)
		return ret;

	return 0;
}
