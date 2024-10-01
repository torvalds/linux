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
 *
 * Authors: Lyude Paul
 */
#include <core/device.h>

#include "priv.h"

#define pack_for_each_init(init, pack, head)                          \
	for (pack = head; pack && pack->init; pack++)                 \
		  for (init = pack->init; init && init->count; init++)
void
gf100_clkgate_init(struct nvkm_therm *therm,
		   const struct nvkm_therm_clkgate_pack *p)
{
	struct nvkm_device *device = therm->subdev.device;
	const struct nvkm_therm_clkgate_pack *pack;
	const struct nvkm_therm_clkgate_init *init;
	u32 next, addr;

	pack_for_each_init(init, pack, p) {
		next = init->addr + init->count * 8;
		addr = init->addr;

		nvkm_trace(&therm->subdev, "{ 0x%06x, %d, 0x%08x }\n",
			   init->addr, init->count, init->data);
		while (addr < next) {
			nvkm_trace(&therm->subdev, "\t0x%06x = 0x%08x\n",
				   addr, init->data);
			nvkm_wr32(device, addr, init->data);
			addr += 8;
		}
	}
}

/*
 * TODO: Fermi clockgating isn't understood fully yet, so we don't specify any
 * clockgate functions to use
 */
