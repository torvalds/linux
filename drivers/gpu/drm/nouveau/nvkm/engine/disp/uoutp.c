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
#define nvkm_uoutp(p) container_of((p), struct nvkm_outp, object)
#include "outp.h"
#include "ior.h"

#include <nvif/if0012.h>

static int
nvkm_uoutp_mthd_load_detect(struct nvkm_outp *outp, void *argv, u32 argc)
{
	union nvif_outp_load_detect_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	ret = nvkm_outp_acquire(outp, NVKM_OUTP_PRIV, false);
	if (ret == 0) {
		if (outp->ior->func->sense) {
			ret = outp->ior->func->sense(outp->ior, args->v0.data);
			args->v0.load = ret < 0 ? 0 : ret;
		} else {
			ret = -EINVAL;
		}
		nvkm_outp_release(outp, NVKM_OUTP_PRIV);
	}

	return ret;
}

static int
nvkm_uoutp_mthd_noacquire(struct nvkm_outp *outp, u32 mthd, void *argv, u32 argc)
{
	switch (mthd) {
	case NVIF_OUTP_V0_LOAD_DETECT: return nvkm_uoutp_mthd_load_detect(outp, argv, argc);
	default:
		break;
	}

	return 1;
}

static int
nvkm_uoutp_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_outp *outp = nvkm_uoutp(object);
	struct nvkm_disp *disp = outp->disp;
	int ret;

	mutex_lock(&disp->super.mutex);

	ret = nvkm_uoutp_mthd_noacquire(outp, mthd, argv, argc);
	if (ret <= 0)
		goto done;

done:
	mutex_unlock(&disp->super.mutex);
	return ret;
}

static void *
nvkm_uoutp_dtor(struct nvkm_object *object)
{
	struct nvkm_outp *outp = nvkm_uoutp(object);
	struct nvkm_disp *disp = outp->disp;

	spin_lock(&disp->client.lock);
	outp->object.func = NULL;
	spin_unlock(&disp->client.lock);
	return NULL;
}

static const struct nvkm_object_func
nvkm_uoutp = {
	.dtor = nvkm_uoutp_dtor,
	.mthd = nvkm_uoutp_mthd,
};

int
nvkm_uoutp_new(const struct nvkm_oclass *oclass, void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_disp *disp = nvkm_udisp(oclass->parent);
	struct nvkm_outp *outt, *outp = NULL;
	union nvif_outp_args *args = argv;
	int ret;

	if (argc != sizeof(args->v0) || args->v0.version != 0)
		return -ENOSYS;

	list_for_each_entry(outt, &disp->outps, head) {
		if (outt->index == args->v0.id) {
			outp = outt;
			break;
		}
	}

	if (!outp)
		return -EINVAL;

	ret = -EBUSY;
	spin_lock(&disp->client.lock);
	if (!outp->object.func) {
		nvkm_object_ctor(&nvkm_uoutp, oclass, &outp->object);
		*pobject = &outp->object;
		ret = 0;
	}
	spin_unlock(&disp->client.lock);
	return ret;
}
