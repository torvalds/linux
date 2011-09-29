#ifndef _ARCH_POWERPC_MM_ICSWX_H_
#define _ARCH_POWERPC_MM_ICSWX_H_

/*
 *  ICSWX and ACOP Management
 *
 *  Copyright (C) 2011 Anton Blanchard, IBM Corp. <anton@samba.org>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <asm/mmu_context.h>

/* also used to denote that PIDs are not used */
#define COP_PID_NONE 0

static inline void sync_cop(void *arg)
{
	struct mm_struct *mm = arg;

	if (mm == current->active_mm)
		switch_cop(current->active_mm);
}

#ifdef CONFIG_PPC_ICSWX_PID
extern int get_cop_pid(struct mm_struct *mm);
extern int disable_cop_pid(struct mm_struct *mm);
extern void free_cop_pid(int free_pid);
#else
#define get_cop_pid(m) (COP_PID_NONE)
#define disable_cop_pid(m) (COP_PID_NONE)
#define free_cop_pid(p)
#endif

#endif /* !_ARCH_POWERPC_MM_ICSWX_H_ */
