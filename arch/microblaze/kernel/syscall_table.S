/* SPDX-License-Identifier: GPL-2.0 */

#define __SYSCALL(nr, entry) .long entry
ENTRY(sys_call_table)
#include <asm/syscall_table.h>
