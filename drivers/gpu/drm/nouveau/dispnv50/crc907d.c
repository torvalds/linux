// SPDX-License-Identifier: MIT
#include <drm/drm_crtc.h>

#include "crc.h"
#include "core.h"
#include "disp.h"
#include "head.h"

#include <nvif/push507c.h>

#include <nvhw/class/cl907d.h>

#define CRC907D_MAX_ENTRIES 255

struct crc907d_notifier {
	u32 status;
	u32 :32; /* reserved */
	struct crc907d_entry {
		u32 status;
		u32 compositor_crc;
		u32 output_crc[2];
	} entries[CRC907D_MAX_ENTRIES];
} __packed;

static int
crc907d_set_src(struct nv50_head *head, int or, enum nv50_crc_source_type source,
		struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 crc_args = NVDEF(NV907D, HEAD_SET_CRC_CONTROL, CONTROLLING_CHANNEL, CORE) |
		       NVDEF(NV907D, HEAD_SET_CRC_CONTROL, EXPECT_BUFFER_COLLAPSE, FALSE) |
		       NVDEF(NV907D, HEAD_SET_CRC_CONTROL, TIMESTAMP_MODE, FALSE) |
		       NVDEF(NV907D, HEAD_SET_CRC_CONTROL, SECONDARY_OUTPUT, NONE) |
		       NVDEF(NV907D, HEAD_SET_CRC_CONTROL, CRC_DURING_SNOOZE, DISABLE) |
		       NVDEF(NV907D, HEAD_SET_CRC_CONTROL, WIDE_PIPE_CRC, ENABLE);
	int ret;

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, SOR(or));
		break;
	case NV50_CRC_SOURCE_TYPE_PIOR:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, PIOR(or));
		break;
	case NV50_CRC_SOURCE_TYPE_DAC:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, DAC(or));
		break;
	case NV50_CRC_SOURCE_TYPE_RG:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, RG(i));
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, SF(i));
		break;
	case NV50_CRC_SOURCE_NONE:
		crc_args |= NVDEF(NV907D, HEAD_SET_CRC_CONTROL, PRIMARY_OUTPUT, NONE);
		break;
	}

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	if (source) {
		PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_CRC(i), ctx->ntfy.handle);
		PUSH_MTHD(push, NV907D, HEAD_SET_CRC_CONTROL(i), crc_args);
	} else {
		PUSH_MTHD(push, NV907D, HEAD_SET_CRC_CONTROL(i), crc_args);
		PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_CRC(i), 0);
	}

	return 0;
}

static int
crc907d_set_ctx(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907D, HEAD_SET_CONTEXT_DMA_CRC(i), ctx ? ctx->ntfy.handle : 0);
	return 0;
}

static u32 crc907d_get_entry(struct nv50_head *head,
			     struct nv50_crc_notifier_ctx *ctx,
			     enum nv50_crc_source source, int idx)
{
	struct crc907d_notifier __iomem *notifier = ctx->mem.object.map.ptr;

	return ioread32_native(&notifier->entries[idx].output_crc[0]);
}

static bool crc907d_ctx_finished(struct nv50_head *head,
				 struct nv50_crc_notifier_ctx *ctx)
{
	struct nouveau_drm *drm = nouveau_drm(head->base.base.dev);
	struct crc907d_notifier __iomem *notifier = ctx->mem.object.map.ptr;
	const u32 status = ioread32_native(&notifier->status);
	const u32 overflow = status & 0x0000003e;

	if (!(status & 0x00000001))
		return false;

	if (overflow) {
		const char *engine = NULL;

		switch (overflow) {
		case 0x00000004: engine = "DSI"; break;
		case 0x00000008: engine = "Compositor"; break;
		case 0x00000010: engine = "CRC output 1"; break;
		case 0x00000020: engine = "CRC output 2"; break;
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

const struct nv50_crc_func crc907d = {
	.set_src = crc907d_set_src,
	.set_ctx = crc907d_set_ctx,
	.get_entry = crc907d_get_entry,
	.ctx_finished = crc907d_ctx_finished,
	.flip_threshold = CRC907D_MAX_ENTRIES - 10,
	.num_entries = CRC907D_MAX_ENTRIES,
	.notifier_len = sizeof(struct crc907d_notifier),
};
