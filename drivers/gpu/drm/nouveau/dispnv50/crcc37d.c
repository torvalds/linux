// SPDX-License-Identifier: MIT
#include <drm/drm_crtc.h>

#include "crc.h"
#include "crcc37d.h"
#include "core.h"
#include "disp.h"
#include "head.h"

#include <nvif/pushc37b.h>

#include <nvhw/class/clc37d.h>

static int
crcc37d_set_src(struct nv50_head *head, int or, enum nv50_crc_source_type source,
		struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = &nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 crc_args = NVVAL(NVC37D, HEAD_SET_CRC_CONTROL, CONTROLLING_CHANNEL, i * 4) |
		       NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, EXPECT_BUFFER_COLLAPSE, FALSE) |
		       NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, SECONDARY_CRC, NONE) |
		       NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, CRC_DURING_SNOOZE, DISABLE);
	int ret;

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		crc_args |= NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, SOR(or));
		break;
	case NV50_CRC_SOURCE_TYPE_PIOR:
		crc_args |= NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, PIOR(or));
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		crc_args |= NVDEF(NVC37D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, SF);
		break;
	default:
		break;
	}

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	if (source) {
		PUSH_MTHD(push, NVC37D, HEAD_SET_CONTEXT_DMA_CRC(i), ctx->ntfy.handle);
		PUSH_MTHD(push, NVC37D, HEAD_SET_CRC_CONTROL(i), crc_args);
	} else {
		PUSH_MTHD(push, NVC37D, HEAD_SET_CRC_CONTROL(i), 0);
		PUSH_MTHD(push, NVC37D, HEAD_SET_CONTEXT_DMA_CRC(i), 0);
	}

	return 0;
}

int crcc37d_set_ctx(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = &nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NVC37D, HEAD_SET_CONTEXT_DMA_CRC(i), ctx ? ctx->ntfy.handle : 0);
	return 0;
}

u32 crcc37d_get_entry(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx,
		      enum nv50_crc_source source, int idx)
{
	struct crcc37d_notifier __iomem *notifier = ctx->mem.object.map.ptr;
	struct crcc37d_entry __iomem *entry = &notifier->entries[idx];
	u32 __iomem *crc_addr;

	if (source == NV50_CRC_SOURCE_RG)
		crc_addr = &entry->rg_crc;
	else
		crc_addr = &entry->output_crc[0];

	return ioread32_native(crc_addr);
}

bool crcc37d_ctx_finished(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx)
{
	struct nouveau_drm *drm = nouveau_drm(head->base.base.dev);
	struct crcc37d_notifier __iomem *notifier = ctx->mem.object.map.ptr;
	const u32 status = ioread32_native(&notifier->status);
	const u32 overflow = status & 0x0000007e;

	if (!(status & 0x00000001))
		return false;

	if (overflow) {
		const char *engine = NULL;

		switch (overflow) {
		case 0x00000004: engine = "Front End"; break;
		case 0x00000008: engine = "Compositor"; break;
		case 0x00000010: engine = "RG"; break;
		case 0x00000020: engine = "CRC output 1"; break;
		case 0x00000040: engine = "CRC output 2"; break;
		}

		if (engine)
			NV_ERROR(drm,
				 "CRC notifier context for head %d overflowed on %s: %x\n",
				 head->base.index, engine, status);
		else
			NV_ERROR(drm,
				 "CRC notifier context for head %d overflowed: %x\n",
				 head->base.index, status);
	}

	NV_DEBUG(drm, "Head %d CRC context status: %x\n",
		 head->base.index, status);

	return true;
}

const struct nv50_crc_func crcc37d = {
	.set_src = crcc37d_set_src,
	.set_ctx = crcc37d_set_ctx,
	.get_entry = crcc37d_get_entry,
	.ctx_finished = crcc37d_ctx_finished,
	.flip_threshold = CRCC37D_FLIP_THRESHOLD,
	.num_entries = CRCC37D_MAX_ENTRIES,
	.notifier_len = sizeof(struct crcc37d_notifier),
};
