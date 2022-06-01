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
#include "chan.h"
#include "head.h"
#include "ior.h"
#include "outp.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/cl5070.h>
#include <nvif/unpack.h>

int
nv50_disp_root_mthd_(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	union {
		struct nv50_disp_mthd_v0 v0;
		struct nv50_disp_mthd_v1 v1;
	} *args = data;
	struct nvkm_disp *disp = nvkm_udisp(object);
	struct nvkm_outp *temp, *outp = NULL;
	struct nvkm_head *head;
	u16 type, mask = 0;
	int hidx, ret = -ENOSYS;

	if (mthd != NV50_DISP_MTHD)
		return -EINVAL;

	nvif_ioctl(object, "disp mthd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			   args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		hidx = args->v0.head;
	} else
	if (!(ret = nvif_unpack(ret, &data, &size, args->v1, 1, 1, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x "
				   "type %04x mask %04x\n",
			   args->v1.version, args->v1.method,
			   args->v1.hasht, args->v1.hashm);
		mthd = args->v1.method;
		type = args->v1.hasht;
		mask = args->v1.hashm;
		hidx = ffs((mask >> 8) & 0x0f) - 1;
	} else
		return ret;

	if (!(head = nvkm_head_find(disp, hidx)))
		return -ENXIO;

	if (mask) {
		list_for_each_entry(temp, &disp->outps, head) {
			if ((temp->info.hasht         == type) &&
			    (temp->info.hashm & mask) == mask) {
				outp = temp;
				break;
			}
		}
		if (outp == NULL)
			return -ENXIO;
	}

	switch (mthd) {
	case NV50_DISP_SCANOUTPOS: {
		return nvkm_head_mthd_scanoutpos(object, head, data, size);
	}
	default:
		break;
	}

	switch (mthd * !!outp) {
	case NV50_DISP_MTHD_V1_SOR_DP_MST_VCPI: {
		union {
			struct nv50_disp_sor_dp_mst_vcpi_v0 v0;
		} *args = data;
		int ret = -ENOSYS;
		nvif_ioctl(object, "disp sor dp mst vcpi size %d\n", size);
		if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
			nvif_ioctl(object, "disp sor dp mst vcpi vers %d "
					   "slot %02x/%02x pbn %04x/%04x\n",
				   args->v0.version, args->v0.start_slot,
				   args->v0.num_slots, args->v0.pbn,
				   args->v0.aligned_pbn);
			if (!outp->ior->func->dp->vcpi)
				return -ENODEV;
			outp->ior->func->dp->vcpi(outp->ior, hidx,
						 args->v0.start_slot,
						 args->v0.num_slots,
						 args->v0.pbn,
						 args->v0.aligned_pbn);
			return 0;
		} else
			return ret;
	}
		break;
	default:
		break;
	}

	return -EINVAL;
}
