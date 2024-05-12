/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 */

#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H

#include <asm/reg.h>
#include <asm/current.h>
#include <asm/paca.h>

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	unsigned long canary = get_random_canary();

	current->stack_canary = canary;
#ifdef CONFIG_PPC64
	get_paca()->canary = canary;
#endif
}

#endif	/* _ASM_STACKPROTECTOR_H */
