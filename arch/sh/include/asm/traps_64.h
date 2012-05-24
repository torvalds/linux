/*
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 * Copyright (C) 2004  Richard Curnow
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_SH_TRAPS_64_H
#define __ASM_SH_TRAPS_64_H

extern void phys_stext(void);

static inline void trigger_address_error(void)
{
	phys_stext();
}

#define BUILD_TRAP_HANDLER(name)	\
asmlinkage void name##_trap_handler(unsigned int vec, struct pt_regs *regs)
#define TRAP_HANDLER_DECL

#endif /* __ASM_SH_TRAPS_64_H */
