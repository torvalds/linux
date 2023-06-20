/* SPDX-License-Identifier: GPL-2.0-only */

#define	vdso_start	compat_vdso_start
#define	vdso_end	compat_vdso_end

#define	__VDSO_PATH	"arch/riscv/kernel/compat_vdso/compat_vdso.so"

#include "../vdso/vdso.S"
