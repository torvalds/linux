/* SPDX-License-Identifier: GPL-2.0 */

/* *Copyright (C) 2022-2023 Linaro Ltd. */

#ifndef _REG_H_
#define _REG_H_

#include <linux/types.h>
#include <linux/bits.h>

/**
 * struct reg - A register descriptor
 * @offset:	Register offset relative to base of register memory
 * @stride:	Distance between two instances, if parameterized
 * @fcount:	Number of entries in the @fmask array
 * @fmask:	Array of mask values defining position and width of fields
 * @name:	Upper-case name of the register
 */
struct reg {
	u32 offset;
	u32 stride;
	u32 fcount;
	const u32 *fmask;			/* BIT(nr) or GENMASK(h, l) */
	const char *name;
};

/* Helper macro for defining "simple" (non-parameterized) registers */
#define REG(__NAME, __reg_id, __offset)					\
	REG_STRIDE(__NAME, __reg_id, __offset, 0)

/* Helper macro for defining parameterized registers, specifying stride */
#define REG_STRIDE(__NAME, __reg_id, __offset, __stride)		\
	static const struct reg reg_ ## __reg_id = {			\
		.name	= #__NAME,					\
		.offset	= __offset,					\
		.stride	= __stride,					\
	}

#define REG_FIELDS(__NAME, __name, __offset)				\
	REG_STRIDE_FIELDS(__NAME, __name, __offset, 0)

#define REG_STRIDE_FIELDS(__NAME, __name, __offset, __stride)		\
	static const struct reg reg_ ## __name = {			\
		.name   = #__NAME,					\
		.offset = __offset,					\
		.stride = __stride,					\
		.fcount = ARRAY_SIZE(reg_ ## __name ## _fmask),		\
		.fmask  = reg_ ## __name ## _fmask,			\
	}

/**
 * struct regs - Description of registers supported by hardware
 * @reg_count:	Number of registers in the @reg[] array
 * @reg:	Array of register descriptors
 */
struct regs {
	u32 reg_count;
	const struct reg **reg;
};

static inline const struct reg *reg(const struct regs *regs, u32 reg_id)
{
	if (WARN(reg_id >= regs->reg_count,
		 "reg out of range (%u > %u)\n", reg_id, regs->reg_count - 1))
		return NULL;

	return regs->reg[reg_id];
}

/* Returns 0 for NULL reg; warning should have already been issued */
static inline u32 reg_offset(const struct reg *reg)
{
	return reg ? reg->offset : 0;
}

/* Returns 0 for NULL reg; warning should have already been issued */
static inline u32 reg_n_offset(const struct reg *reg, u32 n)
{
	return reg ? reg->offset + n * reg->stride : 0;
}

#endif /* _REG_H_ */
