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
#include <subdev/mc.h>

#include <nvif/class.h>

void
gp100_fault_buffer_intr(struct nvkm_fault_buffer *buffer, bool enable)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	nvkm_mc_intr_mask(device, NVKM_SUBDEV_FAULT, enable);
}

void
gp100_fault_buffer_fini(struct nvkm_fault_buffer *buffer)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	nvkm_mask(device, 0x002a70, 0x00000001, 0x00000000);
}

void
gp100_fault_buffer_init(struct nvkm_fault_buffer *buffer)
{
	struct nvkm_device *device = buffer->fault->subdev.device;
	nvkm_wr32(device, 0x002a74, upper_32_bits(buffer->addr));
	nvkm_wr32(device, 0x002a70, lower_32_bits(buffer->addr));
	nvkm_mask(device, 0x002a70, 0x00000001, 0x00000001);
}

u64 gp100_fault_buffer_pin(struct nvkm_fault_buffer *buffer)
{
	return nvkm_memory_bar2(buffer->mem);
}

void
gp100_fault_buffer_info(struct nvkm_fault_buffer *buffer)
{
	buffer->entries = nvkm_rd32(buffer->fault->subdev.device, 0x002a78);
	buffer->get = 0x002a7c;
	buffer->put = 0x002a80;
}

void
gp100_fault_intr(struct nvkm_fault *fault)
{
	nvkm_event_send(&fault->event, 1, 0, NULL, 0);
}

static const struct nvkm_fault_func
gp100_fault = {
	.intr = gp100_fault_intr,
	.buffer.nr = 1,
	.buffer.entry_size = 32,
	.buffer.info = gp100_fault_buffer_info,
	.buffer.pin = gp100_fault_buffer_pin,
	.buffer.init = gp100_fault_buffer_init,
	.buffer.fini = gp100_fault_buffer_fini,
	.buffer.intr = gp100_fault_buffer_intr,
	.user = { { 0, 0, MAXWELL_FAULT_BUFFER_A }, 0 },
};

int
gp100_fault_new(struct nvkm_device *device, int index,
		struct nvkm_fault **pfault)
{
	return nvkm_fault_new_(&gp100_fault, device, index, pfault);
}
