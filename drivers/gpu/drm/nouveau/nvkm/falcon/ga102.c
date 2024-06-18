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

#include <subdev/mc.h>
#include <subdev/timer.h>

bool
ga102_flcn_riscv_active(struct nvkm_falcon *falcon)
{
	return (nvkm_falcon_rd32(falcon, falcon->addr2 + 0x388) & 0x00000080) != 0;
}

static bool
ga102_flcn_dma_done(struct nvkm_falcon *falcon)
{
	return !!(nvkm_falcon_rd32(falcon, 0x118) & 0x00000002);
}

static void
ga102_flcn_dma_xfer(struct nvkm_falcon *falcon, u32 mem_base, u32 dma_base, u32 cmd)
{
	nvkm_falcon_wr32(falcon, 0x114, mem_base);
	nvkm_falcon_wr32(falcon, 0x11c, dma_base);
	nvkm_falcon_wr32(falcon, 0x118, cmd);
}

static int
ga102_flcn_dma_init(struct nvkm_falcon *falcon, u64 dma_addr, int xfer_len,
		    enum nvkm_falcon_mem mem_type, bool sec, u32 *cmd)
{
	*cmd = (ilog2(xfer_len) - 2) << 8;
	if (mem_type == IMEM)
		*cmd |= 0x00000010;
	if (sec)
		*cmd |= 0x00000004;

	nvkm_falcon_wr32(falcon, 0x110, dma_addr >> 8);
	nvkm_falcon_wr32(falcon, 0x128, 0x00000000);
	return 0;
}

const struct nvkm_falcon_func_dma
ga102_flcn_dma = {
	.init = ga102_flcn_dma_init,
	.xfer = ga102_flcn_dma_xfer,
	.done = ga102_flcn_dma_done,
};

int
ga102_flcn_reset_wait_mem_scrubbing(struct nvkm_falcon *falcon)
{
	nvkm_falcon_mask(falcon, 0x040, 0x00000000, 0x00000000);

	if (nvkm_msec(falcon->owner->device, 20,
		if (!(nvkm_falcon_rd32(falcon, 0x0f4) & 0x00001000))
			break;
	) < 0)
		return -ETIMEDOUT;

	return 0;
}

int
ga102_flcn_reset_prep(struct nvkm_falcon *falcon)
{
	nvkm_falcon_rd32(falcon, 0x0f4);

	nvkm_usec(falcon->owner->device, 150,
		if (nvkm_falcon_rd32(falcon, 0x0f4) & 0x80000000)
			break;
		_warn = false;
	);

	return 0;
}

int
ga102_flcn_select(struct nvkm_falcon *falcon)
{
	if ((nvkm_falcon_rd32(falcon, falcon->addr2 + 0x668) & 0x00000010) != 0x00000000) {
		nvkm_falcon_wr32(falcon, falcon->addr2 + 0x668, 0x00000000);
		if (nvkm_msec(falcon->owner->device, 10,
			if (nvkm_falcon_rd32(falcon, falcon->addr2 + 0x668) & 0x00000001)
				break;
		) < 0)
			return -ETIMEDOUT;
	}

	return 0;
}

int
ga102_flcn_fw_boot(struct nvkm_falcon_fw *fw, u32 *mbox0, u32 *mbox1, u32 mbox0_ok, u32 irqsclr)
{
	struct nvkm_falcon *falcon = fw->falcon;

	nvkm_falcon_wr32(falcon, falcon->addr2 + 0x210, fw->dmem_sign);
	nvkm_falcon_wr32(falcon, falcon->addr2 + 0x19c, fw->engine_id);
	nvkm_falcon_wr32(falcon, falcon->addr2 + 0x198, fw->ucode_id);
	nvkm_falcon_wr32(falcon, falcon->addr2 + 0x180, 0x00000001);

	return gm200_flcn_fw_boot(fw, mbox0, mbox1, mbox0_ok, irqsclr);
}

int
ga102_flcn_fw_load(struct nvkm_falcon_fw *fw)
{
	struct nvkm_falcon *falcon = fw->falcon;
	int ret = 0;

	nvkm_falcon_mask(falcon, 0x624, 0x00000080, 0x00000080);
	nvkm_falcon_wr32(falcon, 0x10c, 0x00000000);
	nvkm_falcon_mask(falcon, 0x600, 0x00010007, (0 << 16) | (1 << 2) | 1);

	ret = nvkm_falcon_dma_wr(falcon, fw->fw.img, fw->fw.phys, fw->imem_base_img,
				 IMEM, fw->imem_base, fw->imem_size, true);
	if (ret)
		return ret;

	ret = nvkm_falcon_dma_wr(falcon, fw->fw.img, fw->fw.phys, fw->dmem_base_img,
				 DMEM, fw->dmem_base, fw->dmem_size, false);
	if (ret)
		return ret;

	return 0;
}

const struct nvkm_falcon_fw_func
ga102_flcn_fw = {
	.signature = ga100_flcn_fw_signature,
	.reset = gm200_flcn_fw_reset,
	.load = ga102_flcn_fw_load,
	.boot = ga102_flcn_fw_boot,
};
