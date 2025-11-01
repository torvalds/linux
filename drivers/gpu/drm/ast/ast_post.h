/* SPDX-License-Identifier: MIT */

#ifndef AST_POST_H
#define AST_POST_H

#include <linux/limits.h>
#include <linux/types.h>

struct ast_device;

/* DRAM timing tables */
struct ast_dramstruct {
	u16 index;
	u32 data;
};

/* hardware fields */
#define __AST_DRAMSTRUCT_DRAM_TYPE      0x0004

/* control commands */
#define __AST_DRAMSTRUCT_UDELAY         0xff00
#define __AST_DRAMSTRUCT_INVALID        0xffff

#define __AST_DRAMSTRUCT_INDEX(_name) \
	(__AST_DRAMSTRUCT_ ## _name)

#define AST_DRAMSTRUCT_INIT(_name, _value) \
	{ __AST_DRAMSTRUCT_INDEX(_name), (_value) }

#define AST_DRAMSTRUCT_UDELAY(_usecs) \
	AST_DRAMSTRUCT_INIT(UDELAY, _usecs)
#define AST_DRAMSTRUCT_INVALID \
	AST_DRAMSTRUCT_INIT(INVALID, U32_MAX)

#define AST_DRAMSTRUCT_IS(_entry, _name) \
	((_entry)->index == __AST_DRAMSTRUCT_INDEX(_name))

u32 __ast_mindwm(void __iomem *regs, u32 r);
void __ast_moutdwm(void __iomem *regs, u32 r, u32 v);

bool mmc_test(struct ast_device *ast, u32 datagen, u8 test_ctl);
bool mmc_test_burst(struct ast_device *ast, u32 datagen);

/* ast_2000.c */
void ast_2000_set_def_ext_reg(struct ast_device *ast);

/* ast_2300.c */
void ast_2300_set_def_ext_reg(struct ast_device *ast);

#endif
