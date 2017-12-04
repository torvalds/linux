/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _H8300_PAGE_H
#define _H8300_PAGE_H

#include <asm-generic/page.h>
#include <linux/types.h>

#define MAP_NR(addr) (((uintptr_t)(addr)-PAGE_OFFSET) >> PAGE_SHIFT)
#define VM_DATA_DEFAULT_FLAGS	(VM_READ | VM_WRITE | VM_EXEC | \
				 VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC)

#ifndef __ASSEMBLY__
extern unsigned long rom_length;
extern unsigned long memory_start;
extern unsigned long memory_end;
extern unsigned long _ramend;
#endif

#endif
