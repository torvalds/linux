/*
 *  linux/arch/arm/kernel/fiq.c
 *
 *  Copyright (C) 1998 Russell King
 *  Copyright (C) 1998, 1999 Phil Blundell
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>

#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/fiq.h>
#include <asm/irq.h>
#include <asm/traps.h>

#define FIQ_OFFSET ({					\
		extern void *vector_fiq_offset;		\
		(unsigned)&vector_fiq_offset;		\
	})

static unsigned long no_fiq_insn;

/* Default reacquire function
 * - we always relinquish FIQ control
 * - we always reacquire FIQ control
 */
static int fiq_def_op(void *ref, int relinquish)
{
	if (!relinquish)
		set_fiq_handler(&no_fiq_insn, sizeof(no_fiq_insn));

	return 0;
}

static struct fiq_handler default_owner = {
	.name	= "default",
	.fiq_op = fiq_def_op,
};

static struct fiq_handler *current_fiq = &default_owner;

int show_fiq_list(struct seq_file *p, int prec)
{
	if (current_fiq != &default_owner)
		seq_printf(p, "%*s:              %s\n", prec, "FIQ",
			current_fiq->name);

	return 0;
}

void set_fiq_handler(void *start, unsigned int length)
{
	void *base = vectors_page;
	unsigned offset = FIQ_OFFSET;

	memcpy(base + offset, start, length);
	if (!cache_is_vipt_nonaliasing())
		flush_icache_range((unsigned long)base + offset, offset +
				   length);
	flush_icache_range(0xffff0000 + offset, 0xffff0000 + offset + length);
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
		dump_stack();
		return;
	}

	do
		current_fiq = current_fiq->next;
	while (current_fiq->fiq_op(current_fiq->dev_id, 0));
}

static int fiq_start;

void enable_fiq(int fiq)
{
	enable_irq(fiq + fiq_start);
}

void disable_fiq(int fiq)
{
	disable_irq(fiq + fiq_start);
}

EXPORT_SYMBOL(set_fiq_handler);
EXPORT_SYMBOL(__set_fiq_regs);	/* defined in fiqasm.S */
EXPORT_SYMBOL(__get_fiq_regs);	/* defined in fiqasm.S */
EXPORT_SYMBOL(claim_fiq);
EXPORT_SYMBOL(release_fiq);
EXPORT_SYMBOL(enable_fiq);
EXPORT_SYMBOL(disable_fiq);

void __init init_FIQ(int start)
{
	unsigned offset = FIQ_OFFSET;
	no_fiq_insn = *(unsigned long *)(0xffff0000 + offset);
	fiq_start = start;
}
