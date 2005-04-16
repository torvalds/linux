/*
 * Software emulation of some PPC instructions for the 8xx core.
 *
 * Copyright (C) 1998 Dan Malek (dmalek@jlc.net)
 *
 * Software floating emuation for the MPC8xx processor.  I did this mostly
 * because it was easier than trying to get the libraries compiled for
 * software floating point.  The goal is still to get the libraries done,
 * but I lost patience and needed some hacks to at least get init and
 * shells running.  The first problem is the setjmp/longjmp that save
 * and restore the floating point registers.
 *
 * For this emulation, our working registers are found on the register
 * save area.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/interrupt.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

extern void
print_8xx_pte(struct mm_struct *mm, unsigned long addr);
extern int
get_8xx_pte(struct mm_struct *mm, unsigned long addr);

/* Eventually we may need a look-up table, but this works for now.
*/
#define LFS	48
#define LFD	50
#define LFDU	51
#define STFD	54
#define STFDU	55
#define FMR	63

/*
 * We return 0 on success, 1 on unimplemented instruction, and EFAULT
 * if a load/store faulted.
 */
int
Soft_emulate_8xx(struct pt_regs *regs)
{
	uint	inst, instword;
	uint	flreg, idxreg, disp;
	uint	retval;
	signed short sdisp;
	uint	*ea, *ip;

	retval = 0;

	instword = *((uint *)regs->nip);
	inst = instword >> 26;

	flreg = (instword >> 21) & 0x1f;
	idxreg = (instword >> 16) & 0x1f;
	disp = instword & 0xffff;

	ea = (uint *)(regs->gpr[idxreg] + disp);
	ip = (uint *)&current->thread.fpr[flreg];

	switch ( inst )
	{
	case LFD:
		/* this is a 16 bit quantity that is sign extended
		 * so use a signed short here -- Cort
		 */
		sdisp = (instword & 0xffff);
		ea = (uint *)(regs->gpr[idxreg] + sdisp);
		if (copy_from_user(ip, ea, sizeof(double)))
			retval = -EFAULT;
		break;
		
	case LFDU:
		if (copy_from_user(ip, ea, sizeof(double)))
			retval = -EFAULT;
		else
			regs->gpr[idxreg] = (uint)ea;
		break;
	case LFS:
		sdisp = (instword & 0xffff);
		ea = (uint *)(regs->gpr[idxreg] + sdisp);
		if (copy_from_user(ip, ea, sizeof(float)))
			retval = -EFAULT;
		break;
	case STFD:
		/* this is a 16 bit quantity that is sign extended
		 * so use a signed short here -- Cort
		 */
		sdisp = (instword & 0xffff);
		ea = (uint *)(regs->gpr[idxreg] + sdisp);
		if (copy_to_user(ea, ip, sizeof(double)))
			retval = -EFAULT;
		break;

	case STFDU:
		if (copy_to_user(ea, ip, sizeof(double)))
			retval = -EFAULT;
		else
			regs->gpr[idxreg] = (uint)ea;
		break;
	case FMR:
		/* assume this is a fp move -- Cort */
		memcpy( ip, &current->thread.fpr[(instword>>11)&0x1f],
			sizeof(double) );
		break;
	default:
		retval = 1;
		printk("Bad emulation %s/%d\n"
		       " NIP: %08lx instruction: %08x opcode: %x "
		       "A: %x B: %x C: %x code: %x rc: %x\n",
		       current->comm,current->pid,
		       regs->nip,
		       instword,inst,
		       (instword>>16)&0x1f,
		       (instword>>11)&0x1f,
		       (instword>>6)&0x1f,
		       (instword>>1)&0x3ff,
		       instword&1);
		{
			int pa;
			print_8xx_pte(current->mm,regs->nip);
			pa = get_8xx_pte(current->mm,regs->nip) & PAGE_MASK;
			pa |= (regs->nip & ~PAGE_MASK);
			pa = (unsigned long)__va(pa);
			printk("Kernel VA for NIP %x ", pa);
			print_8xx_pte(current->mm,pa);
		}
		
	}

	if (retval == 0)
		regs->nip += 4;
	return(retval);
}

