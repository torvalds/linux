/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include "crcc37d.h"
#include "core.h"
#include "head.h"

#include <nvif/pushc97b.h>

#include <nvhw/class/clca7d.h>

static int
crcca7d_set_ctx(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = &head->disp->core->chan.push;
	const int i = head->base.index;
	int ret;

	ret = PUSH_WAIT(push, ctx ? 3 : 2);
	if (ret)
		return ret;

	if (ctx) {
		const u32 crc_hi = upper_32_bits(ctx->mem.addr);
		const u32 crc_lo = lower_32_bits(ctx->mem.addr);

		PUSH_MTHD(push, NVCA7D, HEAD_SET_SURFACE_ADDRESS_HI_CRC(i), crc_hi,

					HEAD_SET_SURFACE_ADDRESS_LO_CRC(i),
			  NVVAL(NVCA7D, HEAD_SET_SURFACE_ADDRESS_LO_CRC, ADDRESS_LO, crc_lo >> 4) |
			  NVDEF(NVCA7D, HEAD_SET_SURFACE_ADDRESS_LO_CRC, TARGET, PHYSICAL_NVM) |
			  NVDEF(NVCA7D, HEAD_SET_SURFACE_ADDRESS_LO_CRC, ENABLE, ENABLE));
	} else {
		PUSH_MTHD(push, NVCA7D, HEAD_SET_SURFACE_ADDRESS_LO_CRC(i),
			  NVDEF(NVCA7D, HEAD_SET_SURFACE_ADDRESS_LO_CRC, ENABLE, DISABLE));
	}

	return 0;
}

static int
crcca7d_set_src(struct nv50_head *head, int or, enum nv50_crc_source_type source,
		struct nv50_crc_notifier_ctx *ctx)
{
	struct nvif_push *push = &head->disp->core->chan.push;
	const int i = head->base.index;
	int primary_crc, ret;

	if (!source) {
		ret = PUSH_WAIT(push, 1);
		if (ret)
			return ret;

		PUSH_MTHD(push, NVCA7D, HEAD_SET_CRC_CONTROL(i), 0);

		return crcca7d_set_ctx(head, NULL);
	}

	switch (source) {
	case NV50_CRC_SOURCE_TYPE_SOR:
		primary_crc = NVCA7D_HEAD_SET_CRC_CONTROL_PRIMARY_CRC_SOR(or);
		break;
	case NV50_CRC_SOURCE_TYPE_SF:
		primary_crc = NVCA7D_HEAD_SET_CRC_CONTROL_PRIMARY_CRC_SF;
		break;
	default:
		break;
	}

	ret = crcca7d_set_ctx(head, ctx);
	if (ret)
		return ret;

	ret = PUSH_WAIT(push, 2);
	if (ret)
		return ret;

	PUSH_MTHD(push, NVCA7D, HEAD_SET_CRC_CONTROL(i),
		  NVDEF(NVCA7D, HEAD_SET_CRC_CONTROL, CONTROLLING_CHANNEL, CORE) |
		  NVDEF(NVCA7D, HEAD_SET_CRC_CONTROL, EXPECT_BUFFER_COLLAPSE, FALSE) |
		  NVVAL(NVCA7D, HEAD_SET_CRC_CONTROL, PRIMARY_CRC, primary_crc) |
		  NVDEF(NVCA7D, HEAD_SET_CRC_CONTROL, SECONDARY_CRC, NONE) |
		  NVDEF(NVCA7D, HEAD_SET_CRC_CONTROL, CRC_DURING_SNOOZE, DISABLE));

	return 0;
}

const struct nv50_crc_func
crcca7d = {
	.set_src = crcca7d_set_src,
	.set_ctx = crcca7d_set_ctx,
	.get_entry = crcc37d_get_entry,
	.ctx_finished = crcc37d_ctx_finished,
	.flip_threshold = CRCC37D_FLIP_THRESHOLD,
	.num_entries = CRCC37D_MAX_ENTRIES,
	.notifier_len = sizeof(struct crcc37d_notifier),
};
