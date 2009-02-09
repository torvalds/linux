#ifndef _ASM_STACKPROTECTOR_H
#define _ASM_STACKPROTECTOR_H 1

#ifdef CONFIG_CC_STACKPROTECTOR

#include <asm/tsc.h>
#include <asm/processor.h>
#include <asm/percpu.h>
#include <linux/random.h>

/*
 * Initialize the stackprotector canary value.
 *
 * NOTE: this must only be called from functions that never return,
 * and it must always be inlined.
 */
static __always_inline void boot_init_stack_canary(void)
{
	u64 canary;
	u64 tsc;

	/*
	 * Build time only check to make sure the stack_canary is at
	 * offset 40 in the pda; this is a gcc ABI requirement
	 */
	BUILD_BUG_ON(offsetof(union irq_stack_union, stack_canary) != 40);

	/*
	 * We both use the random pool and the current TSC as a source
	 * of randomness. The TSC only matters for very early init,
	 * there it already has some randomness on most systems. Later
	 * on during the bootup the random pool has true entropy too.
	 */
	get_random_bytes(&canary, sizeof(canary));
	tsc = __native_read_tsc();
	canary += tsc + (tsc << 32UL);

	current->stack_canary = canary;
	percpu_write(irq_stack_union.stack_canary, canary);
}

#endif	/* CC_STACKPROTECTOR */
#endif	/* _ASM_STACKPROTECTOR_H */
