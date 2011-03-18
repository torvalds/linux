/* Kernel debugger for MN10300
 *
 * Copyright (C) 2011 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_DEBUGGER_H
#define _ASM_DEBUGGER_H

#if defined(CONFIG_KERNEL_DEBUGGER)

extern int debugger_intercept(enum exception_code, int, int, struct pt_regs *);
extern int at_debugger_breakpoint(struct pt_regs *);

#ifndef CONFIG_MN10300_DEBUGGER_CACHE_NO_FLUSH
extern void debugger_local_cache_flushinv(void);
extern void debugger_local_cache_flushinv_one(u8 *);
#else
static inline void debugger_local_cache_flushinv(void) {}
static inline void debugger_local_cache_flushinv_one(u8 *addr) {}
#endif

#else /* CONFIG_KERNEL_DEBUGGER */

static inline int debugger_intercept(enum exception_code excep,
				     int signo, int si_code,
				     struct pt_regs *regs)
{
	return 0;
}

static inline int at_debugger_breakpoint(struct pt_regs *regs)
{
	return 0;
}

#endif /* CONFIG_KERNEL_DEBUGGER */
#endif /* _ASM_DEBUGGER_H */
