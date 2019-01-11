/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_XTENSA_UNISTD_H
#define _UAPI_XTENSA_UNISTD_H

#include <asm/unistd_32.h>

#define __ARCH_WANT_SYS_OLDUMOUNT

/*
 * sysxtensa syscall handler
 *
 * int sysxtensa (SYS_XTENSA_ATOMIC_SET,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_ADD,     ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_EXG_ADD, ptr, val,    unused);
 * int sysxtensa (SYS_XTENSA_ATOMIC_CMP_SWP, ptr, oldval, newval);
 *        a2            a6                   a3    a4      a5
 */

#define SYS_XTENSA_RESERVED               0     /* don't use this */
#define SYS_XTENSA_ATOMIC_SET             1     /* set variable */
#define SYS_XTENSA_ATOMIC_EXG_ADD         2     /* exchange memory and add */
#define SYS_XTENSA_ATOMIC_ADD             3     /* add to memory */
#define SYS_XTENSA_ATOMIC_CMP_SWP         4     /* compare and swap */
#define SYS_XTENSA_COUNT                  5     /* count */

#endif /* _UAPI_XTENSA_UNISTD_H */
