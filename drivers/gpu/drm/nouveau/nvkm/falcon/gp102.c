/*
 * Copyright 2022 Red Hat Inc.
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

static void
gp102_flcn_pio_emem_rd(struct nvkm_falcon *falcon, u8 port, const u8 *img, int len)
{
	while (len >= 4) {
		*(u32 *)img = nvkm_falcon_rd32(falcon, 0xac4 + (port * 8));
		img += 4;
		len -= 4;
	}
}

static void
gp102_flcn_pio_emem_rd_init(struct nvkm_falcon *falcon, u8 port, u32 dmem_base)
{
	nvkm_falcon_wr32(falcon, 0xac0 + (port * 8), BIT(25) | dmem_base);
}

static void
gp102_flcn_pio_emem_wr(struct nvkm_falcon *falcon, u8 port, const u8 *img, int len, u16 tag)
{
	while (len >= 4) {
		nvkm_falcon_wr32(falcon, 0xac4 + (port * 8), *(u32 *)img);
		img += 4;
		len -= 4;
	}
}

static void
gp102_flcn_pio_emem_wr_init(struct nvkm_falcon *falcon, u8 port, bool sec, u32 emem_base)
{
	nvkm_falcon_wr32(falcon, 0xac0 + (port * 8), BIT(24) | emem_base);
}

const struct nvkm_falcon_func_pio
gp102_flcn_emem_pio = {
	.min = 4,
	.max = 0x100,
	.wr_init = gp102_flcn_pio_emem_wr_init,
	.wr = gp102_flcn_pio_emem_wr,
	.rd_init = gp102_flcn_pio_emem_rd_init,
	.rd = gp102_flcn_pio_emem_rd,
};

int
gp102_flcn_reset_eng(struct nvkm_falcon *falcon)
{
	int ret;

	if (falcon->func->reset_prep) {
		ret = falcon->func->reset_prep(falcon);
		if (ret)
			return ret;
	}

	nvkm_falcon_mask(falcon, 0x3c0, 0x00000001, 0x00000001);
	udelay(10);
	nvkm_falcon_mask(falcon, 0x3c0, 0x00000001, 0x00000000);

	return falcon->func->reset_wait_mem_scrubbing(falcon);
}
