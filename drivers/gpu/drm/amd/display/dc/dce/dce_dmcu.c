/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
 *
 * Authors: AMD
 *
 */

#include "dce_dmcu.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed32_32.h"
#include "dc.h"

#define TO_DCE_DMCU(dmcu)\
	container_of(dmcu, struct dce_dmcu, base)

#define REG(reg) \
	(dmcu_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	dmcu_dce->dmcu_shift->field_name, dmcu_dce->dmcu_mask->field_name

#define CTX \
	dmcu_dce->base.ctx

bool dce_dmcu_load_iram(struct dmcu *dmcu,
		unsigned int start_offset,
		const char *src,
		unsigned int bytes)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int count = 0;
	uint32_t status;

	/* Enable write access to IRAM */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 1,
			IRAM_WR_ADDR_AUTO_INC, 1);

	do {
		dm_delay_in_microseconds(dmcu->ctx, 2);
		REG_GET(DCI_MEM_PWR_STATUS, DMCU_IRAM_MEM_PWR_STATE, &status);
		count++;
	} while
	((status & dmcu_dce->dmcu_mask->DMCU_IRAM_MEM_PWR_STATE) && count < 10);

	REG_WRITE(DMCU_IRAM_WR_CTRL, start_offset);

	for (count = 0; count < bytes; count++)
		REG_WRITE(DMCU_IRAM_WR_DATA, src[count]);

	/* Disable write access to IRAM to allow dynamic sleep state */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 0,
			IRAM_WR_ADDR_AUTO_INC, 0);

	return true;
}

static const struct dmcu_funcs dce_funcs = {
	.load_iram = dce_dmcu_load_iram,
};

static void dce_dmcu_construct(
	struct dce_dmcu *dmcu_dce,
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask)
{
	struct dmcu *base = &dmcu_dce->base;

	base->ctx = ctx;
	base->funcs = &dce_funcs;

	dmcu_dce->regs = regs;
	dmcu_dce->dmcu_shift = dmcu_shift;
	dmcu_dce->dmcu_mask = dmcu_mask;
}

struct dmcu *dce_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask)
{
	struct dce_dmcu *dmcu_dce = dm_alloc(sizeof(*dmcu_dce));

	if (dmcu_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_dmcu_construct(
		dmcu_dce, ctx, regs, dmcu_shift, dmcu_mask);

	dmcu_dce->base.funcs = &dce_funcs;

	return &dmcu_dce->base;
}

void dce_dmcu_destroy(struct dmcu **dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(*dmcu);

	dm_free(dmcu_dce);
	*dmcu = NULL;
}
