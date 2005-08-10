
/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    (c) Philip Blundell, 1998-1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "fpa11.h"

#include <linux/module.h>
#include <linux/config.h>

/* XXX */
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
/* XXX */

#include "softfloat.h"
#include "fpopcode.h"
#include "fpmodule.h"
#include "fpa11.inl"

/* kernel symbols required for signal handling */
#ifdef CONFIG_FPE_NWFPE_XP
#define NWFPE_BITS "extended"
#else
#define NWFPE_BITS "double"
#endif

#ifdef MODULE
void fp_send_sig(unsigned long sig, struct task_struct *p, int priv);
#else
#define fp_send_sig	send_sig
#define kern_fp_enter	fp_enter

extern char fpe_type[];
#endif

/* kernel function prototypes required */
void fp_setup(void);

/* external declarations for saved kernel symbols */
extern void (*kern_fp_enter)(void);
extern void (*fp_init)(union fp_state *);

/* Original value of fp_enter from kernel before patched by fpe_init. */
static void (*orig_fp_enter)(void);
static void (*orig_fp_init)(union fp_state *);

/* forward declarations */
extern void nwfpe_enter(void);

static int __init fpe_init(void)
{
	if (sizeof(FPA11) > sizeof(union fp_state)) {
		printk(KERN_ERR "nwfpe: bad structure size\n");
		return -EINVAL;
	}

	if (sizeof(FPREG) != 12) {
		printk(KERN_ERR "nwfpe: bad register size\n");
		return -EINVAL;
	}
	if (fpe_type[0] && strcmp(fpe_type, "nwfpe"))
		return 0;

	/* Display title, version and copyright information. */
	printk(KERN_WARNING "NetWinder Floating Point Emulator V0.97 ("
	       NWFPE_BITS " precision)\n");

	/* Save pointer to the old FP handler and then patch ourselves in */
	orig_fp_enter = kern_fp_enter;
	orig_fp_init = fp_init;
	kern_fp_enter = nwfpe_enter;
	fp_init = nwfpe_init_fpa;

	return 0;
}

static void __exit fpe_exit(void)
{
	/* Restore the values we saved earlier. */
	kern_fp_enter = orig_fp_enter;
	fp_init = orig_fp_init;
}

/*
ScottB:  November 4, 1998

Moved this function out of softfloat-specialize into fpmodule.c.
This effectively isolates all the changes required for integrating with the
Linux kernel into fpmodule.c.  Porting to NetBSD should only require modifying
fpmodule.c to integrate with the NetBSD kernel (I hope!).

[1/1/99: Not quite true any more unfortunately.  There is Linux-specific
code to access data in user space in some other source files at the 
moment (grep for get_user / put_user calls).  --philb]

This function is called by the SoftFloat routines to raise a floating
point exception.  We check the trap enable byte in the FPSR, and raise
a SIGFPE exception if necessary.  If not the relevant bits in the 
cumulative exceptions flag byte are set and we return.
*/

void float_raise(signed char flags)
{
	register unsigned int fpsr, cumulativeTraps;

#ifdef CONFIG_DEBUG_USER
 	/* Ignore inexact errors as there are far too many of them to log */
 	if (flags & ~BIT_IXC)
 		printk(KERN_DEBUG
		       "NWFPE: %s[%d] takes exception %08x at %p from %08lx\n",
		       current->comm, current->pid, flags,
		       __builtin_return_address(0), GET_USERREG()->ARM_pc);
#endif

	/* Read fpsr and initialize the cumulativeTraps.  */
	fpsr = readFPSR();
	cumulativeTraps = 0;

	/* For each type of exception, the cumulative trap exception bit is only
	   set if the corresponding trap enable bit is not set.  */
	if ((!(fpsr & BIT_IXE)) && (flags & BIT_IXC))
		cumulativeTraps |= BIT_IXC;
	if ((!(fpsr & BIT_UFE)) && (flags & BIT_UFC))
		cumulativeTraps |= BIT_UFC;
	if ((!(fpsr & BIT_OFE)) && (flags & BIT_OFC))
		cumulativeTraps |= BIT_OFC;
	if ((!(fpsr & BIT_DZE)) && (flags & BIT_DZC))
		cumulativeTraps |= BIT_DZC;
	if ((!(fpsr & BIT_IOE)) && (flags & BIT_IOC))
		cumulativeTraps |= BIT_IOC;

	/* Set the cumulative exceptions flags.  */
	if (cumulativeTraps)
		writeFPSR(fpsr | cumulativeTraps);

	/* Raise an exception if necessary.  */
	if (fpsr & (flags << 16))
		fp_send_sig(SIGFPE, current, 1);
}

module_init(fpe_init);
module_exit(fpe_exit);

MODULE_AUTHOR("Scott Bambrough <scottb@rebel.com>");
MODULE_DESCRIPTION("NWFPE floating point emulator (" NWFPE_BITS " precision)");
MODULE_LICENSE("GPL");
