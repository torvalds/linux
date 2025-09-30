/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_CURRENT_H
#define _ASM_MICROBLAZE_CURRENT_H

/*
 * Register used to hold the current task pointer while in the kernel.
 * Any `call clobbered' register without a special meaning should be OK,
 * but check asm/microblaze/kernel/entry.S to be sure.
 */
#define CURRENT_TASK	r31
# ifndef __ASSEMBLER__
/*
 * Dedicate r31 to keeping the current task pointer
 */
register struct task_struct *current asm("r31");

# define get_current()	current
# endif /* __ASSEMBLER__ */

#endif /* _ASM_MICROBLAZE_CURRENT_H */
