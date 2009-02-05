/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * @file crypto4xx_sa.c
 *
 * This file implements the security context
 * assoicate format.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/spinlock_types.h>
#include <linux/highmem.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/algapi.h>
#include <crypto/des.h>
#include "crypto4xx_reg_def.h"
#include "crypto4xx_sa.h"
#include "crypto4xx_core.h"

u32 get_dynamic_sa_offset_iv_field(struct crypto4xx_ctx *ctx)
{
	u32 offset;
	union dynamic_sa_contents cts;

	if (ctx->direction == DIR_INBOUND)
		cts.w = ((struct dynamic_sa_ctl *)(ctx->sa_in))->sa_contents;
	else
		cts.w = ((struct dynamic_sa_ctl *)(ctx->sa_out))->sa_contents;
	offset = cts.bf.key_size
		+ cts.bf.inner_size
		+ cts.bf.outer_size
		+ cts.bf.spi
		+ cts.bf.seq_num0
		+ cts.bf.seq_num1
		+ cts.bf.seq_num_mask0
		+ cts.bf.seq_num_mask1
		+ cts.bf.seq_num_mask2
		+ cts.bf.seq_num_mask3;

	return sizeof(struct dynamic_sa_ctl) + offset * 4;
}

u32 get_dynamic_sa_offset_state_ptr_field(struct crypto4xx_ctx *ctx)
{
	u32 offset;
	union dynamic_sa_contents cts;

	if (ctx->direction == DIR_INBOUND)
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_in)->sa_contents;
	else
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_out)->sa_contents;
	offset = cts.bf.key_size
		+ cts.bf.inner_size
		+ cts.bf.outer_size
		+ cts.bf.spi
		+ cts.bf.seq_num0
		+ cts.bf.seq_num1
		+ cts.bf.seq_num_mask0
		+ cts.bf.seq_num_mask1
		+ cts.bf.seq_num_mask2
		+ cts.bf.seq_num_mask3
		+ cts.bf.iv0
		+ cts.bf.iv1
		+ cts.bf.iv2
		+ cts.bf.iv3;

	return sizeof(struct dynamic_sa_ctl) + offset * 4;
}

u32 get_dynamic_sa_iv_size(struct crypto4xx_ctx *ctx)
{
	union dynamic_sa_contents cts;

	if (ctx->direction == DIR_INBOUND)
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_in)->sa_contents;
	else
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_out)->sa_contents;
	return (cts.bf.iv0 + cts.bf.iv1 + cts.bf.iv2 + cts.bf.iv3) * 4;
}

u32 get_dynamic_sa_offset_key_field(struct crypto4xx_ctx *ctx)
{
	union dynamic_sa_contents cts;

	if (ctx->direction == DIR_INBOUND)
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_in)->sa_contents;
	else
		cts.w = ((struct dynamic_sa_ctl *) ctx->sa_out)->sa_contents;

	return sizeof(struct dynamic_sa_ctl);
}
