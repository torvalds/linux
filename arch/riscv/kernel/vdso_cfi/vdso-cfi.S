/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2025 Rivos, Inc
 */

#define	vdso_start	vdso_cfi_start
#define	vdso_end	vdso_cfi_end

#define __VDSO_PATH "arch/riscv/kernel/vdso_cfi/vdso-cfi.so"

#include "../vdso/vdso.S"
