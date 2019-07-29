/* SPDX-License-Identifier: GPL-2.0 */

#define __SYSCALL(nr, entry, nargs) .long entry
ENTRY(sys_call_table)
#include <asm/syscall_table.h>
#undef __SYSCALL
