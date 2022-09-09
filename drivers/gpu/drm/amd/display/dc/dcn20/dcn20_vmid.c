/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include <linux/delay.h>

#include "dcn20_vmid.h"
#include "reg_helper.h"

#define REG(reg)\
	vmid->regs->reg

#define CTX \
	vmid->ctx

#undef FN
#define FN(reg_name, field_name) \
	vmid->shifts->field_name, vmid->masks->field_name

static void dcn20_wait_for_vmid_ready(struct dcn20_vmid *vmid)
{
	/* According the hardware spec, we need to poll for the lowest
	 * bit of PAGE_TABLE_BASE_ADDR_LO32 = 1 any time a GPUVM
	 * context is updated. We can't use REG_WAIT here since we
	 * don't have a seperate field to wait on.
	 *
	 * TODO: Confirm timeout / poll interval with hardware team
	 */

	int max_times = 10000;
	int delay_us  = 5;
	int i;

	for (i = 0; i < max_times; ++i) {
		uint32_t entry_lo32;

		REG_GET(PAGE_TABLE_BASE_ADDR_LO32,
			VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_LO32,
			&entry_lo32);

		if (entry_lo32 & 0x1)
			return;

		udelay(delay_us);
	}

	/* VM setup timed out */
	DC_LOG_WARNING("Timeout while waiting for GPUVM context update\n");
	ASSERT(0);
}

void dcn20_vmid_setup(struct dcn20_vmid *vmid, const struct dcn_vmid_page_table_config *config)
{
	REG_SET(PAGE_TABLE_START_ADDR_HI32, 0,
			VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_HI4, (config->page_table_start_addr >> 32) & 0xF);
	REG_SET(PAGE_TABLE_START_ADDR_LO32, 0,
			VM_CONTEXT0_START_LOGICAL_PAGE_NUMBER_LO32, config->page_table_start_addr & 0xFFFFFFFF);

	REG_SET(PAGE_TABLE_END_ADDR_HI32, 0,
			VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_HI4, (config->page_table_end_addr >> 32) & 0xF);
	REG_SET(PAGE_TABLE_END_ADDR_LO32, 0,
			VM_CONTEXT0_END_LOGICAL_PAGE_NUMBER_LO32, config->page_table_end_addr & 0xFFFFFFFF);

	REG_SET_2(CNTL, 0,
			VM_CONTEXT0_PAGE_TABLE_DEPTH, config->depth,
			VM_CONTEXT0_PAGE_TABLE_BLOCK_SIZE, config->block_size);

	REG_SET(PAGE_TABLE_BASE_ADDR_HI32, 0,
			VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_HI32, (config->page_table_base_addr >> 32) & 0xFFFFFFFF);
	/* Note: per hardware spec PAGE_TABLE_BASE_ADDR_LO32 must be programmed last in sequence */
	REG_SET(PAGE_TABLE_BASE_ADDR_LO32, 0,
			VM_CONTEXT0_PAGE_DIRECTORY_ENTRY_LO32, config->page_table_base_addr & 0xFFFFFFFF);

	dcn20_wait_for_vmid_ready(vmid);
}
