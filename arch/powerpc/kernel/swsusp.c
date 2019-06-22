// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Common powerpc suspend code for 32 and 64 bits
 *
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/sched.h>
#include <linux/suspend.h>
#include <asm/current.h>
#include <asm/mmu_context.h>
#include <asm/switch_to.h>

void save_processor_state(void)
{
	/*
	 * flush out all the special registers so we don't need
	 * to save them in the snapshot
	 */
	flush_all_to_thread(current);

#ifdef CONFIG_PPC64
	hard_irq_disable();
#endif

}

void restore_processor_state(void)
{
#ifdef CONFIG_PPC32
	switch_mmu_context(current->active_mm, current->active_mm, NULL);
#endif
}
