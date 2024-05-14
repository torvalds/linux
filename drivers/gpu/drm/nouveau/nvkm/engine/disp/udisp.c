/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include "priv.h"
#include "conn.h"
#include "outp.h"

#include <nvif/class.h>
#include <nvif/if0010.h>

static int
nvkm_udisp_sclass(struct nvkm_object *object, int index, struct nvkm_oclass *sclass)
{
	struct nvkm_disp *disp = nvkm_udisp(object);

	if (index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, NVIF_CLASS_CONN };
		sclass->ctor = nvkm_uconn_new;
		return 0;
	}

	if (index-- == 0) {
		sclass->base = (struct nvkm_sclass) { 0, 0, NVIF_CLASS_OUTP };
		sclass->ctor = nvkm_uoutp_new;
		return 0;
	}

	if (disp->func->user[index].ctor) {
		sclass->base = disp->func->user[index].base;
		sclass->ctor = disp->func->user[index].ctor;
		return 0;
	}

	return -EINVAL;
}

static int
nvkm_udisp_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_disp *disp = nvkm_udisp(object);

	if (disp->engine.subdev.device->card_type >= NV_50)
		return nv50_disp_root_mthd_(object, mthd, argv, argc);

	return nv04_disp_mthd(object, mthd, argv, argc);
}

static void *
nvkm_udisp_dtor(struct nvkm_object *object)
{
	struct nvkm_disp *disp = nvkm_udisp(object);

	spin_lock(&disp->client.lock);
	if (object == &disp->client.object)
		disp->client.object.func = NULL;
	spin_unlock(&disp->client.lock);
	return NULL;
}

static const struct nvkm_object_func
nvkm_udisp = {
	.dtor = nvkm_udisp_dtor,
	.mthd = nvkm_udisp_mthd,
	.ntfy = nvkm_disp_ntfy,
	.sclass = nvkm_udisp_sclass,
};

int
nvkm_udisp_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_disp(oclass->engine);
	struct nvkm_conn *conn;
	struct nvkm_outp *outp;
	union nvif_disp_args *args = argv;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	spin_lock(&disp->client.lock);
	if (disp->client.object.func) {
		spin_unlock(&disp->client.lock);
		return -EBUSY;
	}
	nvkm_object_ctor(&nvkm_udisp, oclass, &disp->client.object);
	*pobject = &disp->client.object;
	spin_unlock(&disp->client.lock);

	args->v0.conn_mask = 0;
	list_for_each_entry(conn, &disp->conns, head)
		args->v0.conn_mask |= BIT(conn->index);

	args->v0.outp_mask = 0;
	list_for_each_entry(outp, &disp->outps, head)
		args->v0.outp_mask |= BIT(outp->index);

	return 0;
}
