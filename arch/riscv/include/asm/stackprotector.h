/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_STACKPROTECTOR_H
#define _ASM_RISCV_STACKPROTECTOR_H

#include <linux/random.h>
#include <linux/version.h>
#include <asm/timex.h>

extern unsigned long __stack_chk_guard;

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	unsigned long canary;
	unsigned long tsc;

	/* Try to get a semi random initial value. */
	get_random_bytes(&canary, sizeof(canary));
	tsc = get_cycles();
	canary += tsc + (tsc << BITS_PER_LONG/2);
	canary ^= LINUX_VERSION_CODE;
	canary &= CANARY_MASK;

	current->stack_canary = canary;
	__stack_chk_guard = current->stack_canary;
}
#endif /* _ASM_RISCV_STACKPROTECTOR_H */
