/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_S390_STACKPROTECTOR_H
#define _ASM_S390_STACKPROTECTOR_H

#include <linux/sched.h>
#include <asm/current.h>
#include <asm/lowcore.h>

static __always_inline void boot_init_stack_canary(void)
{
	current->stack_canary = get_random_canary();
	get_lowcore()->stack_canary = current->stack_canary;
}

#endif /* _ASM_S390_STACKPROTECTOR_H */
