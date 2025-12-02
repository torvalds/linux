/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Chen Miao
 */

#ifndef _ASM_OPENRISC_PATCHING_H
#define _ASM_OPENRISC_PATCHING_H

#include <linux/types.h>

int patch_insn_write(void *addr, u32 insn);

#endif /* _ASM_OPENRISC_PATCHING_H */
