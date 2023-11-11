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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "priv.h"

struct nvkm_hwsq {
	struct nvkm_subdev *subdev;
	u32 addr;
	u32 data;
	struct {
		u8 data[512];
		u16 size;
	} c;
};

static void
hwsq_cmd(struct nvkm_hwsq *hwsq, int size, u8 data[])
{
	memcpy(&hwsq->c.data[hwsq->c.size], data, size * sizeof(data[0]));
	hwsq->c.size += size;
}

int
nvkm_hwsq_init(struct nvkm_subdev *subdev, struct nvkm_hwsq **phwsq)
{
	struct nvkm_hwsq *hwsq;

	hwsq = *phwsq = kmalloc(sizeof(*hwsq), GFP_KERNEL);
	if (hwsq) {
		hwsq->subdev = subdev;
		hwsq->addr = ~0;
		hwsq->data = ~0;
		memset(hwsq->c.data, 0x7f, sizeof(hwsq->c.data));
		hwsq->c.size = 0;
	}

	return hwsq ? 0 : -ENOMEM;
}

int
nvkm_hwsq_fini(struct nvkm_hwsq **phwsq, bool exec)
{
	struct nvkm_hwsq *hwsq = *phwsq;
	int ret = 0, i;
	if (hwsq) {
		struct nvkm_subdev *subdev = hwsq->subdev;
		struct nvkm_bus *bus = subdev->device->bus;
		hwsq->c.size = (hwsq->c.size + 4) / 4;
		if (hwsq->c.size <= bus->func->hwsq_size) {
			if (exec)
				ret = bus->func->hwsq_exec(bus,
							   (u32 *)hwsq->c.data,
								  hwsq->c.size);
			if (ret)
				nvkm_error(subdev, "hwsq exec failed: %d\n", ret);
		} else {
			nvkm_error(subdev, "hwsq ucode too large\n");
			ret = -ENOSPC;
		}

		for (i = 0; ret && i < hwsq->c.size; i++)
			nvkm_error(subdev, "\t%08x\n", ((u32 *)hwsq->c.data)[i]);

		*phwsq = NULL;
		kfree(hwsq);
	}
	return ret;
}

void
nvkm_hwsq_wr32(struct nvkm_hwsq *hwsq, u32 addr, u32 data)
{
	nvkm_debug(hwsq->subdev, "R[%06x] = %08x\n", addr, data);

	if (hwsq->data != data) {
		if ((data & 0xffff0000) != (hwsq->data & 0xffff0000)) {
			hwsq_cmd(hwsq, 5, (u8[]){ 0xe2, data, data >> 8,
						  data >> 16, data >> 24 });
		} else {
			hwsq_cmd(hwsq, 3, (u8[]){ 0x42, data, data >> 8 });
		}
	}

	if ((addr & 0xffff0000) != (hwsq->addr & 0xffff0000)) {
		hwsq_cmd(hwsq, 5, (u8[]){ 0xe0, addr, addr >> 8,
					  addr >> 16, addr >> 24 });
	} else {
		hwsq_cmd(hwsq, 3, (u8[]){ 0x40, addr, addr >> 8 });
	}

	hwsq->addr = addr;
	hwsq->data = data;
}

void
nvkm_hwsq_setf(struct nvkm_hwsq *hwsq, u8 flag, int data)
{
	nvkm_debug(hwsq->subdev, " FLAG[%02x] = %d\n", flag, data);
	flag += 0x80;
	if (data >= 0)
		flag += 0x20;
	if (data >= 1)
		flag += 0x20;
	hwsq_cmd(hwsq, 1, (u8[]){ flag });
}

void
nvkm_hwsq_wait(struct nvkm_hwsq *hwsq, u8 flag, u8 data)
{
	nvkm_debug(hwsq->subdev, " WAIT[%02x] = %d\n", flag, data);
	hwsq_cmd(hwsq, 3, (u8[]){ 0x5f, flag, data });
}

void
nvkm_hwsq_wait_vblank(struct nvkm_hwsq *hwsq)
{
	struct nvkm_subdev *subdev = hwsq->subdev;
	struct nvkm_device *device = subdev->device;
	u32 heads, x, y, px = 0;
	int i, head_sync;

	heads = nvkm_rd32(device, 0x610050);
	for (i = 0; i < 2; i++) {
		/* Heuristic: sync to head with biggest resolution */
		if (heads & (2 << (i << 3))) {
			x = nvkm_rd32(device, 0x610b40 + (0x540 * i));
			y = (x & 0xffff0000) >> 16;
			x &= 0x0000ffff;
			if ((x * y) > px) {
				px = (x * y);
				head_sync = i;
			}
		}
	}

	if (px == 0) {
		nvkm_debug(subdev, "WAIT VBLANK !NO ACTIVE HEAD\n");
		return;
	}

	nvkm_debug(subdev, "WAIT VBLANK HEAD%d\n", head_sync);
	nvkm_hwsq_wait(hwsq, head_sync ? 0x3 : 0x1, 0x0);
	nvkm_hwsq_wait(hwsq, head_sync ? 0x3 : 0x1, 0x1);
}

void
nvkm_hwsq_nsec(struct nvkm_hwsq *hwsq, u32 nsec)
{
	u8 shift = 0, usec = nsec / 1000;
	while (usec & ~3) {
		usec >>= 2;
		shift++;
	}

	nvkm_debug(hwsq->subdev, "    DELAY = %d ns\n", nsec);
	hwsq_cmd(hwsq, 1, (u8[]){ 0x00 | (shift << 2) | usec });
}
