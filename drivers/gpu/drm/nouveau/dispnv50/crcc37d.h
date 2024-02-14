/* SPDX-License-Identifier: MIT */

#ifndef __CRCC37D_H__
#define __CRCC37D_H__

#include <linux/types.h>

#include "crc.h"

#define CRCC37D_MAX_ENTRIES 2047
#define CRCC37D_FLIP_THRESHOLD (CRCC37D_MAX_ENTRIES - 30)

struct crcc37d_notifier {
	u32 status;

	/* reserved */
	u32:32;
	u32:32;
	u32:32;
	u32:32;
	u32:32;
	u32:32;
	u32:32;

	struct crcc37d_entry {
		u32 status[2];
		u32:32; /* reserved */
		u32 compositor_crc;
		u32 rg_crc;
		u32 output_crc[2];
		u32:32; /* reserved */
	} entries[CRCC37D_MAX_ENTRIES];
} __packed;

int crcc37d_set_ctx(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx);
u32 crcc37d_get_entry(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx,
		      enum nv50_crc_source source, int idx);
bool crcc37d_ctx_finished(struct nv50_head *head, struct nv50_crc_notifier_ctx *ctx);

#endif /* !__CRCC37D_H__ */
