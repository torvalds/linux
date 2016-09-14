/*
 * Common powerpc suspend code for 32 and 64 bits
 *
 * Copyright 2007	Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
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
