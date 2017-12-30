/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Amit Bhor, Sameer Dhavale: Codito Technologies 2004
 */

#ifndef _ASM_ARC_SIGNAL_H
#define _ASM_ARC_SIGNAL_H

/*
 * This is much needed for ARC sigreturn optimization.
 * This allows uClibc to piggback the addr of a sigreturn stub in sigaction,
 * which allows sigreturn based re-entry into kernel after handling signal.
 * W/o this kernel needs to "synthesize" the sigreturn trampoline on user
 * mode stack which in turn forces the following:
 * -TLB Flush (after making the stack page executable)
 * -Cache line Flush (to make I/D Cache lines coherent)
 */
#define SA_RESTORER	0x04000000

#include <asm-generic/signal.h>

#endif /* _ASM_ARC_SIGNAL_H */
