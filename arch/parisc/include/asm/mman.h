/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMAN_H__
#define __ASM_MMAN_H__

#include <uapi/asm/mman.h>

/* PARISC cannot allow mdwe as it needs writable stacks */
static inline bool arch_memory_deny_write_exec_supported(void)
{
	return false;
}
#define arch_memory_deny_write_exec_supported arch_memory_deny_write_exec_supported

#endif /* __ASM_MMAN_H__ */
