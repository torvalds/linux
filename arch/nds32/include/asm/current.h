/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASM_NDS32_CURRENT_H
#define _ASM_NDS32_CURRENT_H

#ifndef __ASSEMBLY__
register struct task_struct *current asm("$r25");
#endif /* __ASSEMBLY__ */
#define tsk $r25

#endif /* _ASM_NDS32_CURRENT_H */
