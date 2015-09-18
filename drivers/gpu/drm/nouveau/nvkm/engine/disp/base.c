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
#include "conn.h"
#include "outp.h"

#include <core/client.h>
#include <core/notify.h>
#include <core/oproxy.h>
#include <subdev/bios.h>
#include <subdev/bios/dcb.h>

#include <nvif/class.h>
#include <nvif/event.h>
#include <nvif/unpack.h>

static void
nvkm_disp_vblank_fini(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	disp->func->head.vblank_fini(disp, head);
}

static void
nvkm_disp_vblank_init(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	disp->func->head.vblank_init(disp, head);
}

static int
nvkm_disp_vblank_ctor(struct nvkm_object *object, void *data, u32 size,
		      struct nvkm_notify *notify)
{
	struct nvkm_disp *disp =
		container_of(notify->event, typeof(*disp), vblank);
	union {
		struct nvif_notify_head_req_v0 v0;
	} *req = data;
	int ret;

	if (nvif_unpack(req->v0, 0, 0, false)) {
		notify->size = sizeof(struct nvif_notify_head_rep_v0);
		if (ret = -ENXIO, req->v0.head <= disp->vblank.index_nr) {
			notify->types = 1;
			notify->index = req->v0.head;
			return 0;
		}
	}

	return ret;
}

static const struct nvkm_event_func
nvkm_disp_vblank_func = {
	.ctor = nvkm_disp_vblank_ctor,
	.init = nvkm_disp_vblank_init,
	.fini = nvkm_disp_vblank_fini,
};

void
nvkm_disp_vblank(struct nvkm_disp *disp, int head)
{
	struct nvif_notify_head_rep_v0 rep = {};
	nvkm_event_send(&disp->vblank, 1, head, &rep, sizeof(rep));
}

static int
nvkm_disp_hpd_ctor(struct nvkm_object *object, void *data, u32 size,
		   struct nvkm_notify *notify)
{
	struct nvkm_disp *disp =
		container_of(notify->event, typeof(*disp), hpd);
	union {
		struct nvif_notify_conn_req_v0 v0;
	} *req = data;
	struct nvkm_output *outp;
	int ret;

	if (nvif_unpack(req->v0, 0, 0, false)) {
		notify->size = sizeof(struct nvif_notify_conn_rep_v0);
		list_for_each_entry(outp, &disp->outp, head) {
			if (ret = -ENXIO, outp->conn->index == req->v0.conn) {
				if (ret = -ENODEV, outp->conn->hpd.event) {
					notify->types = req->v0.mask;
					notify->index = req->v0.conn;
					ret = 0;
				}
				break;
			}
		}
	}

	return ret;
}

static const struct nvkm_event_func
nvkm_disp_hpd_func = {
	.ctor = nvkm_disp_hpd_ctor
};

int
nvkm_disp_ntfy(struct nvkm_object *object, u32 type, struct nvkm_event **event)
{
	struct nvkm_disp *disp = nvkm_disp(object->engine);
	switch (type) {
	case NV04_DISP_NTFY_VBLANK:
		*event = &disp->vblank;
		return 0;
	case NV04_DISP_NTFY_CONN:
		*event = &disp->hpd;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

static void
nvkm_disp_class_del(struct nvkm_oproxy *oproxy)
{
	struct nvkm_disp *disp = nvkm_disp(oproxy->base.engine);
	mutex_lock(&disp->engine.subdev.mutex);
	if (disp->client == oproxy)
		disp->client = NULL;
	mutex_unlock(&disp->engine.subdev.mutex);
}

static const struct nvkm_oproxy_func
nvkm_disp_class = {
	.dtor[1] = nvkm_disp_class_del,
};

static int
nvkm_disp_class_new(struct nvkm_device *device,
		    const struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	const struct nvkm_disp_oclass *sclass = oclass->engn;
	struct nvkm_disp *disp = nvkm_disp(oclass->engine);
	struct nvkm_oproxy *oproxy;
	int ret;

	ret = nvkm_oproxy_new_(&nvkm_disp_class, oclass, &oproxy);
	if (ret)
		return ret;
	*pobject = &oproxy->base;

	mutex_lock(&disp->engine.subdev.mutex);
	if (disp->client) {
		mutex_unlock(&disp->engine.subdev.mutex);
		return -EBUSY;
	}
	disp->client = oproxy;
	mutex_unlock(&disp->engine.subdev.mutex);

	return sclass->ctor(disp, oclass, data, size, &oproxy->object);
}

static const struct nvkm_device_oclass
nvkm_disp_sclass = {
	.ctor = nvkm_disp_class_new,
};

static int
nvkm_disp_class_get(struct nvkm_oclass *oclass, int index,
		    const struct nvkm_device_oclass **class)
{
	struct nvkm_disp *disp = nvkm_disp(oclass->engine);
	if (index == 0) {
		const struct nvkm_disp_oclass *root = disp->func->root(disp);
		oclass->base = root->base;
		oclass->engn = root;
		*class = &nvkm_disp_sclass;
		return 0;
	}
	return 1;
}

static void
nvkm_disp_intr(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	disp->func->intr(disp);
}

static int
nvkm_disp_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_connector *conn;
	struct nvkm_output *outp;

	list_for_each_entry(outp, &disp->outp, head) {
		nvkm_output_fini(outp);
	}

	list_for_each_entry(conn, &disp->conn, head) {
		nvkm_connector_fini(conn);
	}

	return 0;
}

static int
nvkm_disp_init(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_connector *conn;
	struct nvkm_output *outp;

	list_for_each_entry(conn, &disp->conn, head) {
		nvkm_connector_init(conn);
	}

	list_for_each_entry(outp, &disp->outp, head) {
		nvkm_output_init(outp);
	}

	return 0;
}

static void *
nvkm_disp_dtor(struct nvkm_engine *engine)
{
	struct nvkm_disp *disp = nvkm_disp(engine);
	struct nvkm_connector *conn;
	struct nvkm_output *outp;
	void *data = disp;

	if (disp->func->dtor)
		data = disp->func->dtor(disp);

	nvkm_event_fini(&disp->vblank);
	nvkm_event_fini(&disp->hpd);

	while (!list_empty(&disp->outp)) {
		outp = list_first_entry(&disp->outp, typeof(*outp), head);
		list_del(&outp->head);
		nvkm_output_del(&outp);
	}

	while (!list_empty(&disp->conn)) {
		conn = list_first_entry(&disp->conn, typeof(*conn), head);
		list_del(&conn->head);
		nvkm_connector_del(&conn);
	}

	return data;
}

static const struct nvkm_engine_func
nvkm_disp = {
	.dtor = nvkm_disp_dtor,
	.init = nvkm_disp_init,
	.fini = nvkm_disp_fini,
	.intr = nvkm_disp_intr,
	.base.sclass = nvkm_disp_class_get,
};

int
nvkm_disp_ctor(const struct nvkm_disp_func *func, struct nvkm_device *device,
	       int index, int heads, struct nvkm_disp *disp)
{
	struct nvkm_bios *bios = device->bios;
	struct nvkm_output *outp, *outt, *pair;
	struct nvkm_connector *conn;
	struct nvbios_connE connE;
	struct dcb_output dcbE;
	u8  hpd = 0, ver, hdr;
	u32 data;
	int ret, i;

	INIT_LIST_HEAD(&disp->outp);
	INIT_LIST_HEAD(&disp->conn);
	disp->func = func;
	disp->head.nr = heads;

	ret = nvkm_engine_ctor(&nvkm_disp, device, index, 0,
			       true, &disp->engine);
	if (ret)
		return ret;

	/* create output objects for each display path in the vbios */
	i = -1;
	while ((data = dcb_outp_parse(bios, ++i, &ver, &hdr, &dcbE))) {
		const struct nvkm_disp_func_outp *outps;
		int (*ctor)(struct nvkm_disp *, int, struct dcb_output *,
			    struct nvkm_output **);

		if (dcbE.type == DCB_OUTPUT_UNUSED)
			continue;
		if (dcbE.type == DCB_OUTPUT_EOL)
			break;
		outp = NULL;

		switch (dcbE.location) {
		case 0: outps = &disp->func->outp.internal; break;
		case 1: outps = &disp->func->outp.external; break;
		default:
			nvkm_warn(&disp->engine.subdev,
				  "dcb %d locn %d unknown\n", i, dcbE.location);
			continue;
		}

		switch (dcbE.type) {
		case DCB_OUTPUT_ANALOG: ctor = outps->crt ; break;
		case DCB_OUTPUT_TV    : ctor = outps->tv  ; break;
		case DCB_OUTPUT_TMDS  : ctor = outps->tmds; break;
		case DCB_OUTPUT_LVDS  : ctor = outps->lvds; break;
		case DCB_OUTPUT_DP    : ctor = outps->dp  ; break;
		default:
			nvkm_warn(&disp->engine.subdev,
				  "dcb %d type %d unknown\n", i, dcbE.type);
			continue;
		}

		if (ctor)
			ret = ctor(disp, i, &dcbE, &outp);
		else
			ret = -ENODEV;

		if (ret) {
			if (ret == -ENODEV) {
				nvkm_debug(&disp->engine.subdev,
					   "dcb %d %d/%d not supported\n",
					   i, dcbE.location, dcbE.type);
				continue;
			}
			nvkm_error(&disp->engine.subdev,
				   "failed to create output %d\n", i);
			nvkm_output_del(&outp);
			continue;
		}

		list_add_tail(&outp->head, &disp->outp);
		hpd = max(hpd, (u8)(dcbE.connector + 1));
	}

	/* create connector objects based on the outputs we support */
	list_for_each_entry_safe(outp, outt, &disp->outp, head) {
		/* bios data *should* give us the most useful information */
		data = nvbios_connEp(bios, outp->info.connector, &ver, &hdr,
				     &connE);

		/* no bios connector data... */
		if (!data) {
			/* heuristic: anything with the same ccb index is
			 * considered to be on the same connector, any
			 * output path without an associated ccb entry will
			 * be put on its own connector
			 */
			int ccb_index = outp->info.i2c_index;
			if (ccb_index != 0xf) {
				list_for_each_entry(pair, &disp->outp, head) {
					if (pair->info.i2c_index == ccb_index) {
						outp->conn = pair->conn;
						break;
					}
				}
			}

			/* connector shared with another output path */
			if (outp->conn)
				continue;

			memset(&connE, 0x00, sizeof(connE));
			connE.type = DCB_CONNECTOR_NONE;
			i = -1;
		} else {
			i = outp->info.connector;
		}

		/* check that we haven't already created this connector */
		list_for_each_entry(conn, &disp->conn, head) {
			if (conn->index == outp->info.connector) {
				outp->conn = conn;
				break;
			}
		}

		if (outp->conn)
			continue;

		/* apparently we need to create a new one! */
		ret = nvkm_connector_new(disp, i, &connE, &outp->conn);
		if (ret) {
			nvkm_error(&disp->engine.subdev,
				   "failed to create output %d conn: %d\n",
				   outp->index, ret);
			nvkm_connector_del(&outp->conn);
			list_del(&outp->head);
			nvkm_output_del(&outp);
			continue;
		}

		list_add_tail(&outp->conn->head, &disp->conn);
	}

	ret = nvkm_event_init(&nvkm_disp_hpd_func, 3, hpd, &disp->hpd);
	if (ret)
		return ret;

	ret = nvkm_event_init(&nvkm_disp_vblank_func, 1, heads, &disp->vblank);
	if (ret)
		return ret;

	return 0;
}

int
nvkm_disp_new_(const struct nvkm_disp_func *func, struct nvkm_device *device,
	       int index, int heads, struct nvkm_disp **pdisp)
{
	if (!(*pdisp = kzalloc(sizeof(**pdisp), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_disp_ctor(func, device, index, heads, *pdisp);
}
