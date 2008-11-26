/* tick14.c
 *
 * Copyright (C) 1996 David Redman (djhr@tadpole.co.uk)
 *
 * This file handles the Sparc specific level14 ticker
 * This is really useful for profiling OBP uses it for keyboard
 * aborts and other stuff.
 */
#include <linux/kernel.h>

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
