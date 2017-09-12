/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#include "dm_services.h"

#include "atom.h"

#include "include/bios_parser_types.h"
#include "bios_parser_helper.h"
#include "command_table_helper.h"
#include "command_table.h"
#include "bios_parser_types_internal.h"

uint8_t *get_image(struct dc_bios *bp,
	uint32_t offset,
	uint32_t size)
{
	if (bp->bios && offset + size < bp->bios_size)
		return bp->bios + offset;
	else
		return NULL;
}

#include "reg_helper.h"

#define CTX \
	bios->ctx
#define REG(reg)\
	(bios->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
		ATOM_ ## field_name ## _SHIFT, ATOM_ ## field_name

bool bios_is_accelerated_mode(
	struct dc_bios *bios)
{
	uint32_t acc_mode;
	REG_GET(BIOS_SCRATCH_6, S6_ACC_MODE, &acc_mode);
	return (acc_mode == 1);
}


void bios_set_scratch_acc_mode_change(
	struct dc_bios *bios)
{
	REG_UPDATE(BIOS_SCRATCH_6, S6_ACC_MODE, 1);
}


void bios_set_scratch_critical_state(
	struct dc_bios *bios,
	bool state)
{
	uint32_t critial_state = state ? 1 : 0;
	REG_UPDATE(BIOS_SCRATCH_6, S6_CRITICAL_STATE, critial_state);
}



