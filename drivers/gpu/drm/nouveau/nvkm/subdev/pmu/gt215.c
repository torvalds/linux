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
#include "fuc/gt215.fuc3.h"

#include <subdev/timer.h>

int
gt215_pmu_send(struct nvkm_pmu *pmu, u32 reply[2],
	       u32 process, u32 message, u32 data0, u32 data1)
{
	struct nvkm_subdev *subdev = &pmu->subdev;
	struct nvkm_device *device = subdev->device;
	u32 addr;

	mutex_lock(&subdev->mutex);
	/* wait for a free slot in the fifo */
	addr  = nvkm_rd32(device, 0x10a4a0);
	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x10a4b0);
		if (tmp != (addr ^ 8))
			break;
	) < 0) {
		mutex_unlock(&subdev->mutex);
		return -EBUSY;
	}

	/* we currently only support a single process at a time waiting
	 * on a synchronous reply, take the PMU mutex and tell the
	 * receive handler what we're waiting for
	 */
	if (reply) {
		pmu->recv.message = message;
		pmu->recv.process = process;
	}

	/* acquire data segment access */
	do {
		nvkm_wr32(device, 0x10a580, 0x00000001);
	} while (nvkm_rd32(device, 0x10a580) != 0x00000001);

	/* write the packet */
	nvkm_wr32(device, 0x10a1c0, 0x01000000 | (((addr & 0x07) << 4) +
				pmu->send.base));
	nvkm_wr32(device, 0x10a1c4, process);
	nvkm_wr32(device, 0x10a1c4, message);
	nvkm_wr32(device, 0x10a1c4, data0);
	nvkm_wr32(device, 0x10a1c4, data1);
	nvkm_wr32(device, 0x10a4a0, (addr + 1) & 0x0f);

	/* release data segment access */
	nvkm_wr32(device, 0x10a580, 0x00000000);

	/* wait for reply, if requested */
	if (reply) {
		wait_event(pmu->recv.wait, (pmu->recv.process == 0));
		reply[0] = pmu->recv.data[0];
		reply[1] = pmu->recv.data[1];
	}

	mutex_unlock(&subdev->mutex);
	return 0;
}

void
gt215_pmu_recv(struct nvkm_pmu *pmu)
{
	struct nvkm_subdev *subdev = &pmu->subdev;
	struct nvkm_device *device = subdev->device;
	u32 process, message, data0, data1;

	/* nothing to do if GET == PUT */
	u32 addr =  nvkm_rd32(device, 0x10a4cc);
	if (addr == nvkm_rd32(device, 0x10a4c8))
		return;

	/* acquire data segment access */
	do {
		nvkm_wr32(device, 0x10a580, 0x00000002);
	} while (nvkm_rd32(device, 0x10a580) != 0x00000002);

	/* read the packet */
	nvkm_wr32(device, 0x10a1c0, 0x02000000 | (((addr & 0x07) << 4) +
				pmu->recv.base));
	process = nvkm_rd32(device, 0x10a1c4);
	message = nvkm_rd32(device, 0x10a1c4);
	data0   = nvkm_rd32(device, 0x10a1c4);
	data1   = nvkm_rd32(device, 0x10a1c4);
	nvkm_wr32(device, 0x10a4cc, (addr + 1) & 0x0f);

	/* release data segment access */
	nvkm_wr32(device, 0x10a580, 0x00000000);

	/* wake process if it's waiting on a synchronous reply */
	if (pmu->recv.process) {
		if (process == pmu->recv.process &&
		    message == pmu->recv.message) {
			pmu->recv.data[0] = data0;
			pmu->recv.data[1] = data1;
			pmu->recv.process = 0;
			wake_up(&pmu->recv.wait);
			return;
		}
	}

	/* right now there's no other expected responses from the engine,
	 * so assume that any unexpected message is an error.
	 */
	nvkm_warn(subdev, "%c%c%c%c %08x %08x %08x %08x\n",
		  (char)((process & 0x000000ff) >>  0),
		  (char)((process & 0x0000ff00) >>  8),
		  (char)((process & 0x00ff0000) >> 16),
		  (char)((process & 0xff000000) >> 24),
		  process, message, data0, data1);
}

void
gt215_pmu_intr(struct nvkm_pmu *pmu)
{
	struct nvkm_subdev *subdev = &pmu->subdev;
	struct nvkm_device *device = subdev->device;
	u32 disp = nvkm_rd32(device, 0x10a01c);
	u32 intr = nvkm_rd32(device, 0x10a008) & disp & ~(disp >> 16);

	if (intr & 0x00000020) {
		u32 stat = nvkm_rd32(device, 0x10a16c);
		if (stat & 0x80000000) {
			nvkm_error(subdev, "UAS fault at %06x addr %08x\n",
				   stat & 0x00ffffff,
				   nvkm_rd32(device, 0x10a168));
			nvkm_wr32(device, 0x10a16c, 0x00000000);
			intr &= ~0x00000020;
		}
	}

	if (intr & 0x00000040) {
		schedule_work(&pmu->recv.work);
		nvkm_wr32(device, 0x10a004, 0x00000040);
		intr &= ~0x00000040;
	}

	if (intr & 0x00000080) {
		nvkm_info(subdev, "wr32 %06x %08x\n",
			  nvkm_rd32(device, 0x10a7a0),
			  nvkm_rd32(device, 0x10a7a4));
		nvkm_wr32(device, 0x10a004, 0x00000080);
		intr &= ~0x00000080;
	}

	if (intr) {
		nvkm_error(subdev, "intr %08x\n", intr);
		nvkm_wr32(device, 0x10a004, intr);
	}
}

void
gt215_pmu_fini(struct nvkm_pmu *pmu)
{
	nvkm_wr32(pmu->subdev.device, 0x10a014, 0x00000060);
}

static void
gt215_pmu_reset(struct nvkm_pmu *pmu)
{
	struct nvkm_device *device = pmu->subdev.device;
	nvkm_mask(device, 0x022210, 0x00000001, 0x00000000);
	nvkm_mask(device, 0x022210, 0x00000001, 0x00000001);
	nvkm_rd32(device, 0x022210);
}

static bool
gt215_pmu_enabled(struct nvkm_pmu *pmu)
{
	return nvkm_rd32(pmu->subdev.device, 0x022210) & 0x00000001;
}

int
gt215_pmu_init(struct nvkm_pmu *pmu)
{
	struct nvkm_device *device = pmu->subdev.device;
	int i;

	/* upload data segment */
	nvkm_wr32(device, 0x10a1c0, 0x01000000);
	for (i = 0; i < pmu->func->data.size / 4; i++)
		nvkm_wr32(device, 0x10a1c4, pmu->func->data.data[i]);

	/* upload code segment */
	nvkm_wr32(device, 0x10a180, 0x01000000);
	for (i = 0; i < pmu->func->code.size / 4; i++) {
		if ((i & 0x3f) == 0)
			nvkm_wr32(device, 0x10a188, i >> 6);
		nvkm_wr32(device, 0x10a184, pmu->func->code.data[i]);
	}

	/* start it running */
	nvkm_wr32(device, 0x10a10c, 0x00000000);
	nvkm_wr32(device, 0x10a104, 0x00000000);
	nvkm_wr32(device, 0x10a100, 0x00000002);

	/* wait for valid host->pmu ring configuration */
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x10a4d0))
			break;
	) < 0)
		return -EBUSY;
	pmu->send.base = nvkm_rd32(device, 0x10a4d0) & 0x0000ffff;
	pmu->send.size = nvkm_rd32(device, 0x10a4d0) >> 16;

	/* wait for valid pmu->host ring configuration */
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x10a4dc))
			break;
	) < 0)
		return -EBUSY;
	pmu->recv.base = nvkm_rd32(device, 0x10a4dc) & 0x0000ffff;
	pmu->recv.size = nvkm_rd32(device, 0x10a4dc) >> 16;

	nvkm_wr32(device, 0x10a010, 0x000000e0);
	return 0;
}

static const struct nvkm_pmu_func
gt215_pmu = {
	.code.data = gt215_pmu_code,
	.code.size = sizeof(gt215_pmu_code),
	.data.data = gt215_pmu_data,
	.data.size = sizeof(gt215_pmu_data),
	.enabled = gt215_pmu_enabled,
	.reset = gt215_pmu_reset,
	.init = gt215_pmu_init,
	.fini = gt215_pmu_fini,
	.intr = gt215_pmu_intr,
	.send = gt215_pmu_send,
	.recv = gt215_pmu_recv,
};

int
gt215_pmu_new(struct nvkm_device *device, int index, struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(&gt215_pmu, device, index, ppmu);
}
