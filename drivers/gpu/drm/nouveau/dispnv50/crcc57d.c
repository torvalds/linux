// SPDX-License-Identifier: MIT

#include "crc.h"
#include "crcc37d.h"
#include "core.h"
#include "disp.h"
#include "head.h"

#include <nvif/pushc37b.h>

#include <nvhw/class/clc57d.h>

static int crcc57d_set_src(struct nv50_head *head, int or, enum nv50_crc_source_type source,
			   struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = nv50_disp(head->base.base.dev)->core->chan.push;
	const int i = head->base.index;
	u32 crc_args = NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, CONTROLLING_CHANNEL, CORE) |
		       NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, EXPECT_BUFFER_COLLAPSE, FALSE) |
		       NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, SECONDARY_CRC, NONE) |
		       NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, CRC_DURING_SNOOZE, DISABLE);
	int ret;

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		crc_args |= NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, SOR(or));
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		crc_args |= NVDEF(NVC57D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, SF);
		break;
	default:
		break;
	}

	ret = PUSH_WAIT(push, 4);
	if (ret)
		return ret;

	if (source) {
		PUSH_MTHD(push, NVC57D, HEAD_SET_CONTEXT_DMA_CRC(i), ctx->ntfy.handle);
		PUSH_MTHD(push, NVC57D, HEAD_SET_CRC_CONTROL(i), crc_args);
	} else {
		PUSH_MTHD(push, NVC57D, HEAD_SET_CRC_CONTROL(i), 0);
		PUSH_MTHD(push, NVC57D, HEAD_SET_CONTEXT_DMA_CRC(i), 0);
	}

	return 0;
}

const struct nv50_crc_func crcc57d = {
	.set_src = crcc57d_set_src,
	.set_ctx = crcc37d_set_ctx,
	.get_entry = crcc37d_get_entry,
	.ctx_finished = crcc37d_ctx_finished,
	.flip_threshold = CRCC37D_FLIP_THRESHOLD,
	.num_entries = CRCC37D_MAX_ENTRIES,
	.notifier_len = sizeof(struct crcc37d_notifier),
};
