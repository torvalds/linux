/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 SiFive
 */

#ifndef _ASM_RISCV_PATCH_H
#define _ASM_RISCV_PATCH_H

int patch_text_nosync(void *addr, const void *insns, size_t len);
int patch_text(void *addr, u32 *insns, int ninsns);

extern int riscv_patch_in_stop_machine;

#endif /* _ASM_RISCV_PATCH_H */
