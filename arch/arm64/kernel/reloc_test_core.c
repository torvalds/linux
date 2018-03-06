/*
 * Copyright (C) 2017 Linaro, Ltd. <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>

int sym64_rel;

#define SYM64_ABS_VAL		0xffff880000cccccc
#define SYM32_ABS_VAL		0xf800cccc
#define SYM16_ABS_VAL		0xf8cc

#define __SET_ABS(name, val)	asm(".globl " #name "; .set "#name ", " #val)
#define SET_ABS(name, val)	__SET_ABS(name, val)

SET_ABS(sym64_abs, SYM64_ABS_VAL);
SET_ABS(sym32_abs, SYM32_ABS_VAL);
SET_ABS(sym16_abs, SYM16_ABS_VAL);

asmlinkage u64 absolute_data64(void);
asmlinkage u64 absolute_data32(void);
asmlinkage u64 absolute_data16(void);
asmlinkage u64 signed_movw(void);
asmlinkage u64 unsigned_movw(void);
asmlinkage u64 relative_adrp(void);
asmlinkage u64 relative_adrp_far(void);
asmlinkage u64 relative_adr(void);
asmlinkage u64 relative_data64(void);
asmlinkage u64 relative_data32(void);
asmlinkage u64 relative_data16(void);

static struct {
	char	name[32];
	u64	(*f)(void);
	u64	expect;
} const funcs[] = {
	{ "R_AARCH64_ABS64",		absolute_data64, UL(SYM64_ABS_VAL) },
	{ "R_AARCH64_ABS32",		absolute_data32, UL(SYM32_ABS_VAL) },
	{ "R_AARCH64_ABS16",		absolute_data16, UL(SYM16_ABS_VAL) },
	{ "R_AARCH64_MOVW_SABS_Gn",	signed_movw, UL(SYM64_ABS_VAL) },
	{ "R_AARCH64_MOVW_UABS_Gn",	unsigned_movw, UL(SYM64_ABS_VAL) },
	{ "R_AARCH64_ADR_PREL_PG_HI21",	relative_adrp, (u64)&sym64_rel },
	{ "R_AARCH64_ADR_PREL_PG_HI21",	relative_adrp_far, (u64)&printk },
	{ "R_AARCH64_ADR_PREL_LO21",	relative_adr, (u64)&sym64_rel },
	{ "R_AARCH64_PREL64",		relative_data64, (u64)&sym64_rel },
	{ "R_AARCH64_PREL32",		relative_data32, (u64)&sym64_rel },
	{ "R_AARCH64_PREL16",		relative_data16, (u64)&sym64_rel },
};

static int reloc_test_init(void)
{
	int i;

	pr_info("Relocation test:\n");
	pr_info("-------------------------------------------------------\n");

	for (i = 0; i < ARRAY_SIZE(funcs); i++) {
		u64 ret = funcs[i].f();

		pr_info("%-31s 0x%016llx %s\n", funcs[i].name, ret,
			ret == funcs[i].expect ? "pass" : "fail");
		if (ret != funcs[i].expect)
			pr_err("Relocation failed, expected 0x%016llx, not 0x%016llx\n",
			       funcs[i].expect, ret);
	}
	return 0;
}

static void reloc_test_exit(void)
{
}

module_init(reloc_test_init);
module_exit(reloc_test_exit);

MODULE_LICENSE("GPL v2");
