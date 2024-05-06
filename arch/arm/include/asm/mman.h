/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMAN_H__
#define __ASM_MMAN_H__

#include <asm/system_info.h>
#include <uapi/asm/mman.h>

static inline bool arch_memory_deny_write_exec_supported(void)
{
	return cpu_architecture() >= CPU_ARCH_ARMv6;
}
#define arch_memory_deny_write_exec_supported arch_memory_deny_write_exec_supported

#endif /* __ASM_MMAN_H__ */
