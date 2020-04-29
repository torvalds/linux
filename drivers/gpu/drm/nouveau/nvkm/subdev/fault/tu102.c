/*
 * Copyright 2018 Red Hat Inc.
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

#include <core/memory.h>
#include <subdev/mmu.h>
#include <engine/fifo.h>

#include <nvif/class.h>

static void
tu102_fault_buffer_intr(struct nvkm_fault_buffer *buffer, bool enable)
{
	/*XXX: Earlier versions of RM touched the old regs on Turing,
	 *     which don't appear to actually work anymore, but newer
	 *     versions of RM don't appear to touch anything at all..
	 */
}

static void
tu102_fault_buffer_fini(struct nvkm_fault_buffer *buffer)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	const u32 foff = buffer->id * 0x20;
	nvkm_mask(device, 0xb83010 + foff, 0x80000000, 0x00000000);
}

static void
tu102_fault_buffer_init(struct nvkm_fault_buffer *buffer)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	const u32 foff = buffer->id * 0x20;

	nvkm_mask(device, 0xb83010 + foff, 0xc0000000, 0x40000000);
	nvkm_wr32(device, 0xb83004 + foff, upper_32_bits(buffer->addr));
	nvkm_wr32(device, 0xb83000 + foff, lower_32_bits(buffer->addr));
	nvkm_mask(device, 0xb83010 + foff, 0x80000000, 0x80000000);
}

static void
tu102_fault_buffer_info(struct nvkm_fault_buffer *buffer)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	const u32 foff = buffer->id * 0x20;

	nvkm_mask(device, 0xb83010 + foff, 0x40000000, 0x40000000);

	buffer->entries = nvkm_rd32(device, 0xb83010 + foff) & 0x000fffff;
	buffer->get = 0xb83008 + foff;
	buffer->put = 0xb8300c + foff;
}

static void
tu102_fault_intr_fault(struct nvkm_fault *fault)
{
	struct nvkm_subdev *subdev = &fault->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_fault_data info;
	const u32 addrlo = nvkm_rd32(device, 0xb83080);
	const u32 addrhi = nvkm_rd32(device, 0xb83084);
	const u32  info0 = nvkm_rd32(device, 0xb83088);
	const u32 insthi = nvkm_rd32(device, 0xb8308c);
	const u32  info1 = nvkm_rd32(device, 0xb83090);

	info.addr = ((u64)addrhi << 32) | addrlo;
	info.inst = ((u64)insthi << 32) | (info0 & 0xfffff000);
	info.time = 0;
	info.engine = (info0 & 0x000000ff);
	info.valid  = (info1 & 0x80000000) >> 31;
	info.gpc    = (info1 & 0x1f000000) >> 24;
	info.hub    = (info1 & 0x00100000) >> 20;
	info.access = (info1 & 0x000f0000) >> 16;
	info.client = (info1 & 0x00007f00) >> 8;
	info.reason = (info1 & 0x0000001f);

	nvkm_fifo_fault(device->fifo, &info);
}

static void
tu102_fault_intr(struct nvkm_fault *fault)
{
	struct nvkm_subdev *subdev = &fault->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0xb83094);

	if (stat & 0x80000000) {
		tu102_fault_intr_fault(fault);
		nvkm_wr32(device, 0xb83094, 0x80000000);
		stat &= ~0x80000000;
	}

	if (stat & 0x00000200) {
		if (fault->buffer[0]) {
			nvkm_event_send(&fault->event, 1, 0, NULL, 0);
			stat &= ~0x00000200;
		}
	}

	/*XXX: guess, can't confirm until we get fw... */
	if (stat & 0x00000100) {
		if (fault->buffer[1]) {
			nvkm_event_send(&fault->event, 1, 1, NULL, 0);
			stat &= ~0x00000100;
		}
	}

	if (stat) {
		nvkm_debug(subdev, "intr %08x\n", stat);
	}
}

static void
tu102_fault_fini(struct nvkm_fault *fault)
{
	nvkm_notify_put(&fault->nrpfb);
	if (fault->buffer[0])
		fault->func->buffer.fini(fault->buffer[0]);
	/*XXX: disable priv faults */
}

static void
tu102_fault_init(struct nvkm_fault *fault)
{
	/*XXX: enable priv faults */
	fault->func->buffer.init(fault->buffer[0]);
	nvkm_notify_get(&fault->nrpfb);
}

static const struct nvkm_fault_func
tu102_fault = {
	.oneinit = gv100_fault_oneinit,
	.init = tu102_fault_init,
	.fini = tu102_fault_fini,
	.intr = tu102_fault_intr,
	.buffer.nr = 2,
	.buffer.entry_size = 32,
	.buffer.info = tu102_fault_buffer_info,
	.buffer.pin = gp100_fault_buffer_pin,
	.buffer.init = tu102_fault_buffer_init,
	.buffer.fini = tu102_fault_buffer_fini,
	.buffer.intr = tu102_fault_buffer_intr,
	.user = { { 0, 0, VOLTA_FAULT_BUFFER_A }, 1 },
};

int
tu102_fault_new(struct nvkm_device *device, int index,
		struct nvkm_fault **pfault)
{
	return nvkm_fault_new_(&tu102_fault, device, index, pfault);
}
