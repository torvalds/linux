/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/coldfire/vectors.c
 *
 *	Copyright (C) 1999-2007, Greg Ungerer <gerg@snapgear.com>
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfwdebug.h>

/***************************************************************************/

#ifdef TRAP_DBG_INTERRUPT

asmlinkage void dbginterrupt_c(struct frame *fp)
{
	extern void dump(struct pt_regs *fp);
	printk(KERN_DEBUG "%s(%d): BUS ERROR TRAP\n", __FILE__, __LINE__);
	dump((struct pt_regs *) fp);
	asm("halt");
}

#endif

/***************************************************************************/

extern e_vector	*_ramvec;

/* Assembler routines */
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void system_call(void);
asmlinkage void inthandler(void);

void __init trap_init(void)
{
	int i;

	/*
	 *	There is a common trap handler and common interrupt
	 *	handler that handle almost every vector. We treat
	 *	the system call and bus error special, they get their
	 *	own first level handlers.
	 */
	for (i = 3; (i <= 23); i++)
		_ramvec[i] = trap;
	for (i = 33; (i <= 63); i++)
		_ramvec[i] = trap;
	for (i = 24; (i <= 31); i++)
		_ramvec[i] = inthandler;
	for (i = 64; (i < 255); i++)
		_ramvec[i] = inthandler;
	_ramvec[255] = 0;

	_ramvec[2] = buserr;
	_ramvec[32] = system_call;

#ifdef TRAP_DBG_INTERRUPT
	_ramvec[12] = dbginterrupt;
#endif
}

/***************************************************************************/
