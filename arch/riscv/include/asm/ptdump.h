/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 SiFive
 */

#ifndef _ASM_RISCV_PTDUMP_H
#define _ASM_RISCV_PTDUMP_H

void ptdump_check_wx(void);

#ifdef CONFIG_DEBUG_WX
static inline void debug_checkwx(void)
{
	ptdump_check_wx();
}
#else
static inline void debug_checkwx(void)
{
}
#endif

#endif /* _ASM_RISCV_PTDUMP_H */
