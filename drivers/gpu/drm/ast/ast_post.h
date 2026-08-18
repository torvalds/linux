/* SPDX-License-Identifier: MIT */

#ifndef AST_POST_H
#define AST_POST_H

#include <linux/limits.h>
#include <linux/types.h>

struct ast_device;

/* DRAM timing tables */
struct ast_dramstruct {
	u32 index;
	u32 data;
};

/* control commands */
#define __AST_DRAMSTRUCT_UDELAY         0xff00
#define __AST_DRAMSTRUCT_INVALID        0xffff

#define __AST_DRAMSTRUCT_INDEX(_name) \
	(__AST_DRAMSTRUCT_ ## _name)

#define __AST_DRAMSTRUCT_INIT(_index, _value) \
	{ (_index), (_value) }

#define AST_DRAMSTRUCT_REG(_reg, _value) \
	__AST_DRAMSTRUCT_INIT(_reg, _value)
#define AST_DRAMSTRUCT_UDELAY(_usecs) \
	__AST_DRAMSTRUCT_INIT(__AST_DRAMSTRUCT_UDELAY, _usecs)
#define AST_DRAMSTRUCT_INVALID \
	__AST_DRAMSTRUCT_INIT(__AST_DRAMSTRUCT_INVALID, U32_MAX)

#define AST_DRAMSTRUCT_IS_REG(_entry, _reg) \
	((_entry)->index == (_reg))
#define AST_DRAMSTRUCT_IS(_entry, _name) \
	((_entry)->index == __AST_DRAMSTRUCT_INDEX(_name))

bool mmc_test(struct ast_device *ast, u32 datagen, u8 test_ctl);
bool mmc_test_burst(struct ast_device *ast, u32 datagen);

/* ast_2000.c */
void ast_2000_set_def_ext_reg(struct ast_device *ast);

/* ast_2300.c */
void ast_2300_set_def_ext_reg(struct ast_device *ast);

#endif
