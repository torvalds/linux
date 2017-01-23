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


#ifndef _DCE_DMCU_H_
#define _DCE_DMCU_H_

#include "dmcu.h"

#define DMCU_COMMON_REG_LIST_DCE_BASE() \
	SR(DMCU_RAM_ACCESS_CTRL), \
	SR(DMCU_IRAM_WR_CTRL), \
	SR(DMCU_IRAM_WR_DATA)

#define DMCU_DCE110_COMMON_REG_LIST() \
	DMCU_COMMON_REG_LIST_DCE_BASE(), \
	SR(DCI_MEM_PWR_STATUS)

#define DMCU_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DMCU_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_HOST_ACCESS_EN, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_WR_ADDR_AUTO_INC, mask_sh)

#define DMCU_MASK_SH_LIST_DCE110(mask_sh) \
	DMCU_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh), \
	DMCU_SF(DCI_MEM_PWR_STATUS, \
			DMCU_IRAM_MEM_PWR_STATE, mask_sh)

#define DMCU_REG_FIELD_LIST(type) \
	type DMCU_IRAM_MEM_PWR_STATE; \
	type IRAM_HOST_ACCESS_EN; \
	type IRAM_WR_ADDR_AUTO_INC

struct dce_dmcu_shift {
	DMCU_REG_FIELD_LIST(uint8_t);
};

struct dce_dmcu_mask {
	DMCU_REG_FIELD_LIST(uint32_t);
};

struct dce_dmcu_registers {
	uint32_t DMCU_RAM_ACCESS_CTRL;
	uint32_t DCI_MEM_PWR_STATUS;
	uint32_t DMU_MEM_PWR_CNTL;
	uint32_t DMCU_IRAM_WR_CTRL;
	uint32_t DMCU_IRAM_WR_DATA;
};

struct dce_dmcu {
	struct dmcu base;
	const struct dce_dmcu_registers *regs;
	const struct dce_dmcu_shift *dmcu_shift;
	const struct dce_dmcu_mask *dmcu_mask;
};

struct dmcu *dce_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask);

void dce_dmcu_destroy(struct dmcu **dmcu);

#endif /* _DCE_ABM_H_ */
