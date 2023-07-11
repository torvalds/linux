/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_LOONGARCH_KDEBUG_H
#define _ASM_LOONGARCH_KDEBUG_H

#include <linux/notifier.h>

enum die_val {
	DIE_OOPS = 1,
	DIE_RI,
	DIE_FP,
	DIE_SIMD,
	DIE_TRAP,
};

#endif /* _ASM_LOONGARCH_KDEBUG_H */
