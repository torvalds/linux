/* tick14.c
 * linux/arch/sparc/kernel/tick14.c
 *
 * Copyright (C) 1996 David Redman (djhr@tadpole.co.uk)
 *
 * This file handles the Sparc specific level14 ticker
 * This is really useful for profiling OBP uses it for keyboard
 * aborts and other stuff.
 *
 *
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/timex.h>
#include <linux/interrupt.h>

#include <asm/oplib.h>
#include <asm/timer.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>

extern unsigned long lvl14_save[5];
static unsigned long *linux_lvl14 = NULL;
static unsigned long obp_lvl14[4];
 
/*
 * Call with timer IRQ closed.
 * First time we do it with disable_irq, later prom code uses spin_lock_irq().
 */
void install_linux_ticker(void)
{

	if (!linux_lvl14)
		return;
	linux_lvl14[0] =  lvl14_save[0];
	linux_lvl14[1] =  lvl14_save[1];
	linux_lvl14[2] =  lvl14_save[2];
	linux_lvl14[3] =  lvl14_save[3];
}

void install_obp_ticker(void)
{

	if (!linux_lvl14)
		return;
	linux_lvl14[0] =  obp_lvl14[0];
	linux_lvl14[1] =  obp_lvl14[1];
	linux_lvl14[2] =  obp_lvl14[2];
	linux_lvl14[3] =  obp_lvl14[3]; 
}

void claim_ticker14(irq_handler_t handler,
		    int irq_nr, unsigned int timeout )
{
	int cpu = smp_processor_id();

	/* first we copy the obp handler instructions
	 */
	disable_irq(irq_nr);
	if (!handler)
		return;
    
	linux_lvl14 = (unsigned long *)lvl14_save[4];
	obp_lvl14[0] = linux_lvl14[0];
	obp_lvl14[1] = linux_lvl14[1];
	obp_lvl14[2] = linux_lvl14[2];
	obp_lvl14[3] = linux_lvl14[3];

	if (!request_irq(irq_nr,
			 handler,
			 (IRQF_DISABLED | SA_STATIC_ALLOC),
			 "counter14",
			 NULL)) {
		install_linux_ticker();
		load_profile_irq(cpu, timeout);
		enable_irq(irq_nr);
	}
}
