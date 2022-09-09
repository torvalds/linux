/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/init.h>
#include <linux/linkage.h>

__INITDATA

SYM_DATA_START(vdso_start)
	.incbin "arch/x86/um/vdso/vdso.so"
SYM_DATA_END_LABEL(vdso_start, SYM_L_GLOBAL, vdso_end)

__FINIT
