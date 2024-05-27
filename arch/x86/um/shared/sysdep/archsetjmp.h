/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __X86_UM_SYSDEP_ARCHSETJMP_H
#define __X86_UM_SYSDEP_ARCHSETJMP_H

#ifdef __i386__
#include "archsetjmp_32.h"
#else
#include "archsetjmp_64.h"
#endif

unsigned long get_thread_reg(int reg, jmp_buf *buf);

#endif /* __X86_UM_SYSDEP_ARCHSETJMP_H */
