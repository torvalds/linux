/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_MMAN_H__
#define __ASM_MMAN_H__

#include <linux/fs.h>
#include <uapi/asm/mman.h>

/* PARISC cannot allow mdwe as it needs writable stacks */
static inline bool arch_memory_deny_write_exec_supported(void)
{
	return false;
}
#define arch_memory_deny_write_exec_supported arch_memory_deny_write_exec_supported

static inline unsigned long arch_calc_vm_flag_bits(struct file *file, unsigned long flags)
{
	/*
	 * The stack on parisc grows upwards, so if userspace requests memory
	 * for a stack, mark it with VM_GROWSUP so that the stack expansion in
	 * the fault handler will work.
	 */
	if (flags & MAP_STACK)
		return VM_GROWSUP;

	return 0;
}
#define arch_calc_vm_flag_bits(file, flags) arch_calc_vm_flag_bits(file, flags)

#endif /* __ASM_MMAN_H__ */
