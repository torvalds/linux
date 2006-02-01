/*
 *  linux/arch/arm26/kernel/fiq.c
 *
 *  Copyright (C) 1998 Russell King
 *  Copyright (C) 1998, 1999 Phil Blundell
 *  Copyright (C) 2003 Ian Molton
 *
 *  FIQ support written by Philip Blundell <philb@gnu.org>, 1998.
 *
 *  FIQ support re-written by Russell King to be more generic
 *
 * We now properly support a method by which the FIQ handlers can
 * be stacked onto the vector.  We still do not support sharing
 * the FIQ vector itself.
 *
 * Operation is as follows:
 *  1. Owner A claims FIQ:
 *     - default_fiq relinquishes control.
 *  2. Owner A:
 *     - inserts code.
 *     - sets any registers,
 *     - enables FIQ.
 *  3. Owner B claims FIQ:
 *     - if owner A has a relinquish function.
 *       - disable FIQs.
 *       - saves any registers.
 *       - returns zero.
 *  4. Owner B:
 *     - inserts code.
 *     - sets any registers,
 *     - enables FIQ.
 *  5. Owner B releases FIQ:
 *     - Owner A is asked to reacquire FIQ:
 *	 - inserts code.
 *	 - restores saved registers.
 *	 - enables FIQ.
 *  6. Goto 3
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/init.h>
#include <linux/seq_file.h>

#include <asm/fiq.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#define FIQ_VECTOR (vectors_base() + 0x1c)

static unsigned long no_fiq_insn;

#define unprotect_page_0()
#define protect_page_0()

/* Default reacquire function
 * - we always relinquish FIQ control
 * - we always reacquire FIQ control
 */
static int fiq_def_op(void *ref, int relinquish)
{
	if (!relinquish) {
		unprotect_page_0();
		*(unsigned long *)FIQ_VECTOR = no_fiq_insn;
		protect_page_0();
	}

	return 0;
}

static struct fiq_handler default_owner = {
	.name	= "default",
	.fiq_op = fiq_def_op,
};

static struct fiq_handler *current_fiq = &default_owner;

int show_fiq_list(struct seq_file *p, void *v)
{
	if (current_fiq != &default_owner)
		seq_printf(p, "FIQ:              %s\n", current_fiq->name);

	return 0;
}

void set_fiq_handler(void *start, unsigned int length)
{
	unprotect_page_0();

	memcpy((void *)FIQ_VECTOR, start, length);

	protect_page_0();
}

/*
 * Taking an interrupt in FIQ mode is death, so both these functions
 * disable irqs for the duration. 
 */
void set_fiq_regs(struct pt_regs *regs)
{
	register unsigned long tmp, tmp2;
	__asm__ volatile (
	"mov	%0, pc					\n"
	"bic	%1, %0, #0x3				\n"
	"orr	%1, %1, %3				\n"
	"teqp	%1, #0		@ select FIQ mode	\n"
	"mov	r0, r0					\n"
	"ldmia	%2, {r8 - r14}				\n"
	"teqp	%0, #0		@ return to SVC mode	\n"
	"mov	r0, r0					"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&regs->ARM_r8), "I" (PSR_I_BIT | PSR_F_BIT | MODE_FIQ26)
	/* These registers aren't modified by the above code in a way
	   visible to the compiler, but we mark them as clobbers anyway
	   so that GCC won't put any of the input or output operands in
	   them.  */
	: "r8", "r9", "r10", "r11", "r12", "r13", "r14");
}

void get_fiq_regs(struct pt_regs *regs)
{
	register unsigned long tmp, tmp2;
	__asm__ volatile (
	"mov	%0, pc					\n"
	"bic	%1, %0, #0x3				\n"
	"orr	%1, %1, %3				\n"
	"teqp	%1, #0		@ select FIQ mode	\n"
	"mov	r0, r0					\n"
	"stmia	%2, {r8 - r14}				\n"
	"teqp	%0, #0		@ return to SVC mode	\n"
	"mov	r0, r0					"
	: "=&r" (tmp), "=&r" (tmp2)
	: "r" (&regs->ARM_r8), "I" (PSR_I_BIT | PSR_F_BIT | MODE_FIQ26)
	/* These registers aren't modified by the above code in a way
	   visible to the compiler, but we mark them as clobbers anyway
	   so that GCC won't put any of the input or output operands in
	   them.  */
	: "r8", "r9", "r10", "r11", "r12", "r13", "r14");
}

int claim_fiq(struct fiq_handler *f)
{
	int ret = 0;

	if (current_fiq) {
		ret = -EBUSY;

		if (current_fiq->fiq_op != NULL)
			ret = current_fiq->fiq_op(current_fiq->dev_id, 1);
	}

	if (!ret) {
		f->next = current_fiq;
		current_fiq = f;
	}

	return ret;
}

void release_fiq(struct fiq_handler *f)
{
	if (current_fiq != f) {
		printk(KERN_ERR "%s FIQ trying to release %s FIQ\n",
		       f->name, current_fiq->name);
#ifdef CONFIG_DEBUG_ERRORS
		__backtrace();
#endif
		return;
	}

	do
		current_fiq = current_fiq->next;
	while (current_fiq->fiq_op(current_fiq->dev_id, 0));
}

void enable_fiq(int fiq)
{
	enable_irq(fiq + FIQ_START);
}

void disable_fiq(int fiq)
{
	disable_irq(fiq + FIQ_START);
}

EXPORT_SYMBOL(set_fiq_handler);
EXPORT_SYMBOL(set_fiq_regs);
EXPORT_SYMBOL(get_fiq_regs);
EXPORT_SYMBOL(claim_fiq);
EXPORT_SYMBOL(release_fiq);
EXPORT_SYMBOL(enable_fiq);
EXPORT_SYMBOL(disable_fiq);

void __init init_FIQ(void)
{
	no_fiq_insn = *(unsigned long *)FIQ_VECTOR;
	set_fs(get_fs());
}
