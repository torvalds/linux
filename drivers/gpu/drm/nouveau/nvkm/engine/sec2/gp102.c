/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <core/memory.h>
#include <subdev/acr.h>
#include <subdev/timer.h>

#include <nvfw/flcn.h>
#include <nvfw/sec2.h>

int
gp102_sec2_nofw(struct nvkm_sec2 *sec2, int ver,
		const struct nvkm_sec2_fwif *fwif)
{
	nvkm_warn(&sec2->engine.subdev, "firmware unavailable\n");
	return 0;
}

static int
gp102_sec2_acr_bootstrap_falcon_callback(void *priv, struct nvfw_falcon_msg *hdr)
{
	struct nv_sec2_acr_bootstrap_falcon_msg *msg =
		container_of(hdr, typeof(*msg), msg.hdr);
	struct nvkm_subdev *subdev = priv;
	const char *name = nvkm_acr_lsf_id(msg->falcon_id);

	if (msg->error_code) {
		nvkm_error(subdev, "ACR_BOOTSTRAP_FALCON failed for "
				   "falcon %d [%s]: %08x\n",
			   msg->falcon_id, name, msg->error_code);
		return -EINVAL;
	}

	nvkm_debug(subdev, "%s booted\n", name);
	return 0;
}

static int
gp102_sec2_acr_bootstrap_falcon(struct nvkm_falcon *falcon,
			        enum nvkm_acr_lsf_id id)
{
	struct nvkm_sec2 *sec2 = container_of(falcon, typeof(*sec2), falcon);
	struct nv_sec2_acr_bootstrap_falcon_cmd cmd = {
		.cmd.hdr.unit_id = sec2->func->unit_acr,
		.cmd.hdr.size = sizeof(cmd),
		.cmd.cmd_type = NV_SEC2_ACR_CMD_BOOTSTRAP_FALCON,
		.flags = NV_SEC2_ACR_BOOTSTRAP_FALCON_FLAGS_RESET_YES,
		.falcon_id = id,
	};

	return nvkm_falcon_cmdq_send(sec2->cmdq, &cmd.cmd.hdr,
				     gp102_sec2_acr_bootstrap_falcon_callback,
				     &sec2->engine.subdev,
				     msecs_to_jiffies(1000));
}

static void
gp102_sec2_acr_bld_patch(struct nvkm_acr *acr, u32 bld, s64 adjust)
{
	struct loader_config_v1 hdr;
	nvkm_robj(acr->wpr, bld, &hdr, sizeof(hdr));
	hdr.code_dma_base = hdr.code_dma_base + adjust;
	hdr.data_dma_base = hdr.data_dma_base + adjust;
	hdr.overlay_dma_base = hdr.overlay_dma_base + adjust;
	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
	loader_config_v1_dump(&acr->subdev, &hdr);
}

static void
gp102_sec2_acr_bld_write(struct nvkm_acr *acr, u32 bld,
			 struct nvkm_acr_lsfw *lsfw)
{
	const struct loader_config_v1 hdr = {
		.dma_idx = FALCON_SEC2_DMAIDX_UCODE,
		.code_dma_base = lsfw->offset.img + lsfw->app_start_offset,
		.code_size_total = lsfw->app_size,
		.code_size_to_load = lsfw->app_resident_code_size,
		.code_entry_point = lsfw->app_imem_entry,
		.data_dma_base = lsfw->offset.img + lsfw->app_start_offset +
				 lsfw->app_resident_data_offset,
		.data_size = lsfw->app_resident_data_size,
		.overlay_dma_base = lsfw->offset.img + lsfw->app_start_offset,
		.argc = 1,
		.argv = lsfw->falcon->func->emem_addr,
	};

	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
}

static const struct nvkm_acr_lsf_func
gp102_sec2_acr_0 = {
	.bld_size = sizeof(struct loader_config_v1),
	.bld_write = gp102_sec2_acr_bld_write,
	.bld_patch = gp102_sec2_acr_bld_patch,
	.bootstrap_falcons = BIT_ULL(NVKM_ACR_LSF_FECS) |
			     BIT_ULL(NVKM_ACR_LSF_GPCCS) |
			     BIT_ULL(NVKM_ACR_LSF_SEC2),
	.bootstrap_falcon = gp102_sec2_acr_bootstrap_falcon,
};

int
gp102_sec2_initmsg(struct nvkm_sec2 *sec2)
{
	struct nv_sec2_init_msg msg;
	int ret, i;

	ret = nvkm_falcon_msgq_recv_initmsg(sec2->msgq, &msg, sizeof(msg));
	if (ret)
		return ret;

	if (msg.hdr.unit_id != NV_SEC2_UNIT_INIT ||
	    msg.msg_type != NV_SEC2_INIT_MSG_INIT)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(msg.queue_info); i++) {
		if (msg.queue_info[i].id == NV_SEC2_INIT_MSG_QUEUE_ID_MSGQ) {
			nvkm_falcon_msgq_init(sec2->msgq,
					      msg.queue_info[i].index,
					      msg.queue_info[i].offset,
					      msg.queue_info[i].size);
		} else {
			nvkm_falcon_cmdq_init(sec2->cmdq,
					      msg.queue_info[i].index,
					      msg.queue_info[i].offset,
					      msg.queue_info[i].size);
		}
	}

	return 0;
}

irqreturn_t
gp102_sec2_intr(struct nvkm_inth *inth)
{
	struct nvkm_sec2 *sec2 = container_of(inth, typeof(*sec2), engine.subdev.inth);
	struct nvkm_subdev *subdev = &sec2->engine.subdev;
	struct nvkm_falcon *falcon = &sec2->falcon;
	u32 disp = nvkm_falcon_rd32(falcon, 0x01c);
	u32 intr = nvkm_falcon_rd32(falcon, 0x008) & disp & ~(disp >> 16);

	if (intr & 0x00000040) {
		if (unlikely(atomic_read(&sec2->initmsg) == 0)) {
			int ret = sec2->func->initmsg(sec2);

			if (ret)
				nvkm_error(subdev, "error parsing init message: %d\n", ret);

			atomic_set(&sec2->initmsg, ret ?: 1);
		}

		if (atomic_read(&sec2->initmsg) > 0) {
			if (!nvkm_falcon_msgq_empty(sec2->msgq))
				nvkm_falcon_msgq_recv(sec2->msgq);
		}

		nvkm_falcon_wr32(falcon, 0x004, 0x00000040);
		intr &= ~0x00000040;
	}

	if (intr & 0x00000010) {
		if (atomic_read(&sec2->running)) {
			FLCN_ERR(falcon, "halted");
			gm200_flcn_tracepc(falcon);
		}

		nvkm_falcon_wr32(falcon, 0x004, 0x00000010);
		intr &= ~0x00000010;
	}

	if (intr) {
		nvkm_error(subdev, "unhandled intr %08x\n", intr);
		nvkm_falcon_wr32(falcon, 0x004, intr);
	}

	return IRQ_HANDLED;
}

static const struct nvkm_falcon_func
gp102_sec2_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.reset_pmc = true,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_wait_mem_scrubbing = gm200_flcn_reset_wait_mem_scrubbing,
	.debug = 0x408,
	.bind_inst = gm200_flcn_bind_inst,
	.bind_stat = gm200_flcn_bind_stat,
	.bind_intr = true,
	.imem_pio = &gm200_flcn_imem_pio,
	.dmem_pio = &gm200_flcn_dmem_pio,
	.emem_addr = 0x01000000,
	.emem_pio = &gp102_flcn_emem_pio,
	.start = nvkm_falcon_v1_start,
	.cmdq = { 0xa00, 0xa04, 8 },
	.msgq = { 0xa30, 0xa34, 8 },
};

const struct nvkm_sec2_func
gp102_sec2 = {
	.flcn = &gp102_sec2_flcn,
	.unit_unload = NV_SEC2_UNIT_UNLOAD,
	.unit_acr = NV_SEC2_UNIT_ACR,
	.intr = gp102_sec2_intr,
	.initmsg = gp102_sec2_initmsg,
};

MODULE_FIRMWARE("nvidia/gp102/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig.bin");

void
gp102_sec2_acr_bld_patch_1(struct nvkm_acr *acr, u32 bld, s64 adjust)
{
	struct flcn_bl_dmem_desc_v2 hdr;
	nvkm_robj(acr->wpr, bld, &hdr, sizeof(hdr));
	hdr.code_dma_base = hdr.code_dma_base + adjust;
	hdr.data_dma_base = hdr.data_dma_base + adjust;
	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
	flcn_bl_dmem_desc_v2_dump(&acr->subdev, &hdr);
}

void
gp102_sec2_acr_bld_write_1(struct nvkm_acr *acr, u32 bld,
			   struct nvkm_acr_lsfw *lsfw)
{
	const struct flcn_bl_dmem_desc_v2 hdr = {
		.ctx_dma = FALCON_SEC2_DMAIDX_UCODE,
		.code_dma_base = lsfw->offset.img + lsfw->app_start_offset,
		.non_sec_code_off = lsfw->app_resident_code_offset,
		.non_sec_code_size = lsfw->app_resident_code_size,
		.code_entry_point = lsfw->app_imem_entry,
		.data_dma_base = lsfw->offset.img + lsfw->app_start_offset +
				 lsfw->app_resident_data_offset,
		.data_size = lsfw->app_resident_data_size,
		.argc = 1,
		.argv = lsfw->falcon->func->emem_addr,
	};

	nvkm_wobj(acr->wpr, bld, &hdr, sizeof(hdr));
}

const struct nvkm_acr_lsf_func
gp102_sec2_acr_1 = {
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v2),
	.bld_write = gp102_sec2_acr_bld_write_1,
	.bld_patch = gp102_sec2_acr_bld_patch_1,
	.bootstrap_falcons = BIT_ULL(NVKM_ACR_LSF_FECS) |
			     BIT_ULL(NVKM_ACR_LSF_GPCCS) |
			     BIT_ULL(NVKM_ACR_LSF_SEC2),
	.bootstrap_falcon = gp102_sec2_acr_bootstrap_falcon,
};

int
gp102_sec2_load(struct nvkm_sec2 *sec2, int ver,
		const struct nvkm_sec2_fwif *fwif)
{
	return nvkm_acr_lsfw_load_sig_image_desc_v1(&sec2->engine.subdev,
						    &sec2->falcon,
						    NVKM_ACR_LSF_SEC2, "sec2/",
						    ver, fwif->acr);
}

MODULE_FIRMWARE("nvidia/gp102/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp102/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp104/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp106/sec2/sig-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/desc-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/image-1.bin");
MODULE_FIRMWARE("nvidia/gp107/sec2/sig-1.bin");

static const struct nvkm_sec2_fwif
gp102_sec2_fwif[] = {
	{  1, gp102_sec2_load, &gp102_sec2, &gp102_sec2_acr_1 },
	{  0, gp102_sec2_load, &gp102_sec2, &gp102_sec2_acr_0 },
	{ -1, gp102_sec2_nofw, &gp102_sec2 },
	{}
};

int
gp102_sec2_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_sec2 **psec2)
{
	return nvkm_sec2_new_(gp102_sec2_fwif, device, type, inst, 0, psec2);
}
