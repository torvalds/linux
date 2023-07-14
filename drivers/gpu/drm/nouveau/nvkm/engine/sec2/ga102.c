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
#include "priv.h"
#include <subdev/acr.h>
#include <subdev/vfn.h>

#include <nvfw/flcn.h>
#include <nvfw/sec2.h>

static int
ga102_sec2_initmsg(struct nvkm_sec2 *sec2)
{
	struct nv_sec2_init_msg_v1 msg;
	int ret, i;

	ret = nvkm_falcon_msgq_recv_initmsg(sec2->msgq, &msg, sizeof(msg));
	if (ret)
		return ret;

	if (msg.hdr.unit_id != NV_SEC2_UNIT_INIT ||
	    msg.msg_type != NV_SEC2_INIT_MSG_INIT)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(msg.queue_info); i++) {
		if (msg.queue_info[i].id == NV_SEC2_INIT_MSG_QUEUE_ID_MSGQ) {
			nvkm_falcon_msgq_init(sec2->msgq, msg.queue_info[i].index,
							  msg.queue_info[i].offset,
							  msg.queue_info[i].size);
		} else {
			nvkm_falcon_cmdq_init(sec2->cmdq, msg.queue_info[i].index,
							  msg.queue_info[i].offset,
							  msg.queue_info[i].size);
		}
	}

	return 0;
}

static struct nvkm_intr *
ga102_sec2_intr_vector(struct nvkm_sec2 *sec2, enum nvkm_intr_type *pvector)
{
	struct nvkm_device *device = sec2->engine.subdev.device;
	struct nvkm_falcon *falcon = &sec2->falcon;
	int ret;

	ret = ga102_flcn_select(falcon);
	if (ret)
		return ERR_PTR(ret);

	*pvector = nvkm_rd32(device, 0x8403e0) & 0x000000ff;
	return &device->vfn->intr;
}

static int
ga102_sec2_acr_bootstrap_falcon_callback(void *priv, struct nvfw_falcon_msg *hdr)
{
	struct nv_sec2_acr_bootstrap_falcon_msg_v1 *msg =
		container_of(hdr, typeof(*msg), msg.hdr);
	struct nvkm_subdev *subdev = priv;
	const char *name = nvkm_acr_lsf_id(msg->falcon_id);

	if (msg->error_code) {
		nvkm_error(subdev, "ACR_BOOTSTRAP_FALCON failed for falcon %d [%s]: %08x %08x\n",
			   msg->falcon_id, name, msg->error_code, msg->unkn08);
		return -EINVAL;
	}

	nvkm_debug(subdev, "%s booted\n", name);
	return 0;
}

static int
ga102_sec2_acr_bootstrap_falcon(struct nvkm_falcon *falcon, enum nvkm_acr_lsf_id id)
{
	struct nvkm_sec2 *sec2 = container_of(falcon, typeof(*sec2), falcon);
	struct nv_sec2_acr_bootstrap_falcon_cmd_v1 cmd = {
		.cmd.hdr.unit_id = sec2->func->unit_acr,
		.cmd.hdr.size = sizeof(cmd),
		.cmd.cmd_type = NV_SEC2_ACR_CMD_BOOTSTRAP_FALCON,
		.flags = NV_SEC2_ACR_BOOTSTRAP_FALCON_FLAGS_RESET_YES,
		.falcon_id = id,
	};

	return nvkm_falcon_cmdq_send(sec2->cmdq, &cmd.cmd.hdr,
				     ga102_sec2_acr_bootstrap_falcon_callback,
				     &sec2->engine.subdev,
				     msecs_to_jiffies(1000));
}

static const struct nvkm_acr_lsf_func
ga102_sec2_acr_0 = {
	.bld_size = sizeof(struct flcn_bl_dmem_desc_v2),
	.bld_write = gp102_sec2_acr_bld_write_1,
	.bld_patch = gp102_sec2_acr_bld_patch_1,
	.bootstrap_falcons = BIT_ULL(NVKM_ACR_LSF_FECS) |
			     BIT_ULL(NVKM_ACR_LSF_GPCCS) |
			     BIT_ULL(NVKM_ACR_LSF_SEC2),
	.bootstrap_falcon = ga102_sec2_acr_bootstrap_falcon,
};

static const struct nvkm_falcon_func
ga102_sec2_flcn = {
	.disable = gm200_flcn_disable,
	.enable = gm200_flcn_enable,
	.select = ga102_flcn_select,
	.addr2 = 0x1000,
	.reset_pmc = true,
	.reset_eng = gp102_flcn_reset_eng,
	.reset_prep = ga102_flcn_reset_prep,
	.reset_wait_mem_scrubbing = ga102_flcn_reset_wait_mem_scrubbing,
	.imem_dma = &ga102_flcn_dma,
	.dmem_pio = &gm200_flcn_dmem_pio,
	.dmem_dma = &ga102_flcn_dma,
	.emem_addr = 0x01000000,
	.emem_pio = &gp102_flcn_emem_pio,
	.start = nvkm_falcon_v1_start,
	.cmdq = { 0xc00, 0xc04, 8 },
	.msgq = { 0xc80, 0xc84, 8 },
};

static const struct nvkm_sec2_func
ga102_sec2 = {
	.flcn = &ga102_sec2_flcn,
	.intr_vector = ga102_sec2_intr_vector,
	.intr = gp102_sec2_intr,
	.initmsg = ga102_sec2_initmsg,
	.unit_acr = NV_SEC2_UNIT_V2_ACR,
	.unit_unload = NV_SEC2_UNIT_V2_UNLOAD,
};

MODULE_FIRMWARE("nvidia/ga102/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/ga102/sec2/image.bin");
MODULE_FIRMWARE("nvidia/ga102/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/ga102/sec2/hs_bl_sig.bin");

MODULE_FIRMWARE("nvidia/ga103/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/ga103/sec2/image.bin");
MODULE_FIRMWARE("nvidia/ga103/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/ga103/sec2/hs_bl_sig.bin");

MODULE_FIRMWARE("nvidia/ga104/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/ga104/sec2/image.bin");
MODULE_FIRMWARE("nvidia/ga104/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/ga104/sec2/hs_bl_sig.bin");

MODULE_FIRMWARE("nvidia/ga106/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/ga106/sec2/image.bin");
MODULE_FIRMWARE("nvidia/ga106/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/ga106/sec2/hs_bl_sig.bin");

MODULE_FIRMWARE("nvidia/ga107/sec2/desc.bin");
MODULE_FIRMWARE("nvidia/ga107/sec2/image.bin");
MODULE_FIRMWARE("nvidia/ga107/sec2/sig.bin");
MODULE_FIRMWARE("nvidia/ga107/sec2/hs_bl_sig.bin");

static int
ga102_sec2_load(struct nvkm_sec2 *sec2, int ver,
		const struct nvkm_sec2_fwif *fwif)
{
	return nvkm_acr_lsfw_load_sig_image_desc_v2(&sec2->engine.subdev, &sec2->falcon,
						    NVKM_ACR_LSF_SEC2, "sec2/", ver, fwif->acr);
}

static const struct nvkm_sec2_fwif
ga102_sec2_fwif[] = {
	{  0, ga102_sec2_load, &ga102_sec2, &ga102_sec2_acr_0 },
	{ -1, gp102_sec2_nofw, &ga102_sec2 }
};

int
ga102_sec2_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_sec2 **psec2)
{
	/* TOP info wasn't updated on Turing to reflect the PRI
	 * address change for some reason.  We override it here.
	 */
	return nvkm_sec2_new_(ga102_sec2_fwif, device, type, inst, 0x840000, psec2);
}
