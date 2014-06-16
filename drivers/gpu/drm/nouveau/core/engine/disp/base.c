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

#include "priv.h"
#include "outp.h"
#include "conn.h"

static int
nouveau_disp_hpd_check(struct nouveau_event *event, u32 types, int index)
{
	struct nouveau_disp *disp = event->priv;
	struct nvkm_output *outp;
	list_for_each_entry(outp, &disp->outp, head) {
		if (outp->conn->index == index) {
			if (outp->conn->hpd.event)
				return 0;
			break;
		}
	}
	return -ENOSYS;
}

int
_nouveau_disp_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_disp *disp = (void *)object;
	struct nvkm_output *outp;
	int ret;

	list_for_each_entry(outp, &disp->outp, head) {
		ret = nv_ofuncs(outp)->fini(nv_object(outp), suspend);
		if (ret && suspend)
			goto fail_outp;
	}

	return nouveau_engine_fini(&disp->base, suspend);

fail_outp:
	list_for_each_entry_continue_reverse(outp, &disp->outp, head) {
		nv_ofuncs(outp)->init(nv_object(outp));
	}

	return ret;
}

int
_nouveau_disp_init(struct nouveau_object *object)
{
	struct nouveau_disp *disp = (void *)object;
	struct nvkm_output *outp;
	int ret;

	ret = nouveau_engine_init(&disp->base);
	if (ret)
		return ret;

	list_for_each_entry(outp, &disp->outp, head) {
		ret = nv_ofuncs(outp)->init(nv_object(outp));
		if (ret)
			goto fail_outp;
	}

	return ret;

fail_outp:
	list_for_each_entry_continue_reverse(outp, &disp->outp, head) {
		nv_ofuncs(outp)->fini(nv_object(outp), false);
	}

	return ret;
}

void
_nouveau_disp_dtor(struct nouveau_object *object)
{
	struct nouveau_disp *disp = (void *)object;
	struct nvkm_output *outp, *outt;

	nouveau_event_destroy(&disp->vblank);

	list_for_each_entry_safe(outp, outt, &disp->outp, head) {
		nouveau_object_ref(NULL, (struct nouveau_object **)&outp);
	}

	nouveau_engine_destroy(&disp->base);
}

int
nouveau_disp_create_(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, int heads,
		     const char *intname, const char *extname,
		     int length, void **pobject)
{
	struct nouveau_disp_impl *impl = (void *)oclass;
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nouveau_disp *disp;
	struct nouveau_oclass **sclass;
	struct nouveau_object *object;
	struct dcb_output dcbE;
	u8  hpd = 0, ver, hdr;
	u32 data;
	int ret, i;

	ret = nouveau_engine_create_(parent, engine, oclass, true,
				     intname, extname, length, pobject);
	disp = *pobject;
	if (ret)
		return ret;

	INIT_LIST_HEAD(&disp->outp);

	/* create output objects for each display path in the vbios */
	i = -1;
	while ((data = dcb_outp_parse(bios, ++i, &ver, &hdr, &dcbE))) {
		if (dcbE.type == DCB_OUTPUT_UNUSED)
			continue;
		if (dcbE.type == DCB_OUTPUT_EOL)
			break;
		data = dcbE.location << 4 | dcbE.type;

		oclass = nvkm_output_oclass;
		sclass = impl->outp;
		while (sclass && sclass[0]) {
			if (sclass[0]->handle == data) {
				oclass = sclass[0];
				break;
			}
			sclass++;
		}

		nouveau_object_ctor(*pobject, *pobject, oclass,
				    &dcbE, i, &object);
		hpd = max(hpd, (u8)(dcbE.connector + 1));
	}

	ret = nouveau_event_create(3, hpd, &disp->hpd);
	if (ret)
		return ret;

	disp->hpd->priv = disp;
	disp->hpd->check = nouveau_disp_hpd_check;

	ret = nouveau_event_create(1, heads, &disp->vblank);
	if (ret)
		return ret;

	return 0;
}
