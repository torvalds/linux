// SPDX-License-Identifier: MIT
#include <drm/drm_crtc.h>

#include "crc.h"
#include "core.h"
#include "disp.h"
#include "head.h"

#define CRCC37D_MAX_ENTRIES 2047

struct crcc37d_notifier {
	u32 status;

	/* reserved */
	u32 :32;
	u32 :32;
	u32 :32;
	u32 :32;
	u32 :32;
	u32 :32;
	u32 :32;

	struct crcc37d_entry {
		u32 status[2];
		u32 :32; /* reserved */
		u32 compositor_crc;
		u32 rg_crc;
		u32 output_crc[2];
		u32 :32; /* reserved */
	} entries[CRCC37D_MAX_ENTRIES];
} __packed;

static void
crcc37d_set_src(struct nv50_head *head, int or,
		enum nv50_crc_source_type source,
		struct nv50_crc_notifier_ctx *ctx, u32 wndw)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	const u32 hoff = head->base.index * 0x400;
	u32 *push;
	u32 crc_args;

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		crc_args = (0x00000050 + or) << 12;
		break;
	case NV50_CRC_SOURCE_TYPE_PIOR:
		crc_args = (0x00000060 + or) << 12;
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		crc_args = 0x00000030 << 12;
		break;
	default:
		crc_args = 0;
		break;
	}

	push = evo_wait(core, 4);
	if (!push)
		return;

	if (source) {
		evo_mthd(push, 0x2180 + hoff, 1);
		evo_data(push, ctx->ntfy.handle);
		evo_mthd(push, 0x2184 + hoff, 1);
		evo_data(push, crc_args | wndw);
	} else {
		evo_mthd(push, 0x2184 + hoff, 1);
		evo_data(push, 0);
		evo_mthd(push, 0x2180 + hoff, 1);
		evo_data(push, 0);
	}

	evo_kick(push, core);
}

static void crcc37d_set_ctx(struct nv50_head *head,
			    struct nv50_crc_notifier_ctx *ctx)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push = evo_wait(core, 2);

	if (!push)
		return;

	evo_mthd(push, 0x2180 + (head->base.index * 0x400), 1);
	evo_data(push, ctx ? ctx->ntfy.handle : 0);
	evo_kick(push, core);
}

static u32 crcc37d_get_entry(struct nv50_head *head,
			     struct nv50_crc_notifier_ctx *ctx,
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

static bool crcc37d_ctx_finished(struct nv50_head *head,
				 struct nv50_crc_notifier_ctx *ctx)
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
	.flip_threshold = CRCC37D_MAX_ENTRIES - 30,
	.num_entries = CRCC37D_MAX_ENTRIES,
	.notifier_len = sizeof(struct crcc37d_notifier),
};
