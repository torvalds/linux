/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 */

#ifndef _ASM_NIOS2_SETUP_H
#define _ASM_NIOS2_SETUP_H

#include <asm-generic/setup.h>

#ifndef __ASSEMBLY__
#ifdef __KERNEL__

extern char exception_handler_hook[];
extern char fast_handler[];
extern char fast_handler_end[];

extern void pagetable_init(void);

#endif/* __KERNEL__ */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_NIOS2_SETUP_H */
