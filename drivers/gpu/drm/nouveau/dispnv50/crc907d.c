// SPDX-License-Identifier: MIT
#include <drm/drm_crtc.h>

#include "crc.h"
#include "core.h"
#include "disp.h"
#include "head.h"

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

static void
crc907d_set_src(struct nv50_head *head, int or,
		enum nv50_crc_source_type source,
		struct nv50_crc_notifier_ctx *ctx, u32 wndw)
{
	struct drm_crtc *crtc = &head->base.base;
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	const u32 hoff = head->base.index * 0x300;
	u32 *push;
	u32 crc_args = 0xfff00000;

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		crc_args |= (0x00000f0f + or * 16) << 8;
		break;
	case NV50_CRC_SOURCE_TYPE_PIOR:
		crc_args |= (0x000000ff + or * 256) << 8;
		break;
	case NV50_CRC_SOURCE_TYPE_DAC:
		crc_args |= (0x00000ff0 + or) << 8;
		break;
	case NV50_CRC_SOURCE_TYPE_RG:
		crc_args |= (0x00000ff8 + drm_crtc_index(crtc)) << 8;
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		crc_args |= (0x00000f8f + drm_crtc_index(crtc) * 16) << 8;
		break;
	case NV50_CRC_SOURCE_NONE:
		crc_args |= 0x000fff00;
		break;
	}

	push = evo_wait(core, 4);
	if (!push)
		return;

	if (source) {
		evo_mthd(push, 0x0438 + hoff, 1);
		evo_data(push, ctx->ntfy.handle);
		evo_mthd(push, 0x0430 + hoff, 1);
		evo_data(push, crc_args);
	} else {
		evo_mthd(push, 0x0430 + hoff, 1);
		evo_data(push, crc_args);
		evo_mthd(push, 0x0438 + hoff, 1);
		evo_data(push, 0);
	}
	evo_kick(push, core);
}

static void crc907d_set_ctx(struct nv50_head *head,
			    struct nv50_crc_notifier_ctx *ctx)
{
	struct nv50_dmac *core = &nv50_disp(head->base.base.dev)->core->chan;
	u32 *push = evo_wait(core, 2);

	if (!push)
		return;

	evo_mthd(push, 0x0438 + (head->base.index * 0x300), 1);
	evo_data(push, ctx ? ctx->ntfy.handle : 0);
	evo_kick(push, core);
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
