/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_STACKPROTECTOR_H
#define _ASM_RISCV_STACKPROTECTOR_H

extern unsigned long __stack_chk_guard;

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
	if (!IS_ENABLED(CONFIG_STACKPROTECTOR_PER_TASK))
		__stack_chk_guard = current->stack_canary;
}
#endif /* _ASM_RISCV_STACKPROTECTOR_H */
