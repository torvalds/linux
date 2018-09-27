/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GCC stack protector support.
 *
 */

#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H

#include <linux/random.h>
#include <linux/version.h>
#include <asm/reg.h>
#include <asm/current.h>

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	unsigned long canary;

	/* Try to get a semi random initial value. */
	canary = get_random_canary();
	canary ^= mftb();
	canary ^= LINUX_VERSION_CODE;
	canary &= CANARY_MASK;

	current->stack_canary = canary;
}

#endif	/* _ASM_STACKPROTECTOR_H */
