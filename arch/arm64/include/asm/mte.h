/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 ARM Ltd.
 */
#ifndef __ASM_MTE_H
#define __ASM_MTE_H

#ifndef __ASSEMBLY__

#ifdef CONFIG_ARM64_MTE

void flush_mte_state(void);

#else

static inline void flush_mte_state(void)
{
}

#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_MTE_H  */
