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
#include <subdev/bios.h>
#include <subdev/bios/conn.h>

#include "outp.h"

int
_nvkm_output_fini(struct nouveau_object *object, bool suspend)
{
	struct nvkm_output *outp = (void *)object;
	nv_ofuncs(outp->conn)->fini(nv_object(outp->conn), suspend);
	return nouveau_object_fini(&outp->base, suspend);
}

int
_nvkm_output_init(struct nouveau_object *object)
{
	struct nvkm_output *outp = (void *)object;
	int ret = nouveau_object_init(&outp->base);
	if (ret == 0)
		nv_ofuncs(outp->conn)->init(nv_object(outp->conn));
	return 0;
}

void
_nvkm_output_dtor(struct nouveau_object *object)
{
	struct nvkm_output *outp = (void *)object;
	list_del(&outp->head);
	nouveau_object_ref(NULL, (void *)&outp->conn);
	nouveau_object_destroy(&outp->base);
}

int
nvkm_output_create_(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass,
		    struct dcb_output *dcbE, int index,
		    int length, void **pobject)
{
	struct nouveau_bios *bios = nouveau_bios(engine);
	struct nouveau_i2c *i2c = nouveau_i2c(parent);
	struct nouveau_disp *disp = (void *)engine;
	struct nvbios_connE connE;
	struct nvkm_output *outp;
	u8  ver, hdr;
	u32 data;
	int ret;

	ret = nouveau_object_create_(parent, engine, oclass, 0, length, pobject);
	outp = *pobject;
	if (ret)
		return ret;

	outp->info = *dcbE;
	outp->index = index;

	DBG("type %02x loc %d or %d link %d con %x edid %x bus %d head %x\n",
	    dcbE->type, dcbE->location, dcbE->or, dcbE->type >= 2 ?
	    dcbE->sorconf.link : 0, dcbE->connector, dcbE->i2c_index,
	    dcbE->bus, dcbE->heads);

	outp->port = i2c->find(i2c, outp->info.i2c_index);
	outp->edid = outp->port;

	data = nvbios_connEp(bios, outp->info.connector, &ver, &hdr, &connE);
	if (!data) {
		DBG("vbios connector data not found\n");
		memset(&connE, 0x00, sizeof(connE));
		connE.type = DCB_CONNECTOR_NONE;
	}

	ret = nouveau_object_ctor(parent, engine, nvkm_connector_oclass,
				 &connE, outp->info.connector,
				 (struct nouveau_object **)&outp->conn);
	if (ret < 0) {
		ERR("error %d creating connector, disabling\n", ret);
		return ret;
	}

	list_add_tail(&outp->head, &disp->outp);
	return 0;
}

int
_nvkm_output_ctor(struct nouveau_object *parent,
		  struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *dcbE, u32 index,
		  struct nouveau_object **pobject)
{
	struct nvkm_output *outp;
	int ret;

	ret = nvkm_output_create(parent, engine, oclass, dcbE, index, &outp);
	*pobject = nv_object(outp);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
nvkm_output_oclass = &(struct nvkm_output_impl) {
	.base = {
		.handle = 0,
		.ofuncs = &(struct nouveau_ofuncs) {
			.ctor = _nvkm_output_ctor,
			.dtor = _nvkm_output_dtor,
			.init = _nvkm_output_init,
			.fini = _nvkm_output_fini,
		},
	},
}.base;
