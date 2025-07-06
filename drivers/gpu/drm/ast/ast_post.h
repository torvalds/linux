/* SPDX-License-Identifier: MIT */

#ifndef AST_POST_H
#define AST_POST_H

#include <linux/types.h>

struct ast_device;

u32 __ast_mindwm(void __iomem *regs, u32 r);
void __ast_moutdwm(void __iomem *regs, u32 r, u32 v);

bool mmc_test(struct ast_device *ast, u32 datagen, u8 test_ctl);
bool mmc_test_burst(struct ast_device *ast, u32 datagen);

#endif
