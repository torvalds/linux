/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/asm.h>
#include <asm/export.h>
#include <linux/linkage.h>

/*
 * unsigned long native_save_fl(void)
 */
SYM_FUNC_START(native_save_fl)
	pushf
	pop %_ASM_AX
	ret
SYM_FUNC_END(native_save_fl)
EXPORT_SYMBOL(native_save_fl)
