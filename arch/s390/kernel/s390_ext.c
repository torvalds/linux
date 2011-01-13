/*
 *    Copyright IBM Corp. 1999,2010
 *    Author(s): Holger Smolinski <Holger.Smolinski@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/s390_ext.h>
#include <asm/irq_regs.h>
#include <asm/cputime.h>
#include <asm/lowcore.h>
#include <asm/irq.h>
#include "entry.h"

struct ext_int_info {
	struct ext_int_info *next;
	ext_int_handler_t handler;
	__u16 code;
};

/*
 * ext_int_hash[index] is the start of the list for all external interrupts
 * that hash to this index. With the current set of external interrupts 
 * (0x1202 external call, 0x1004 cpu timer, 0x2401 hwc console, 0x4000
 * iucv and 0x2603 pfault) this is always the first element. 
 */
static struct ext_int_info *ext_int_hash[256];

static inline int ext_hash(__u16 code)
{
	return (code + (code >> 9)) & 0xff;
}

int register_external_interrupt(__u16 code, ext_int_handler_t handler)
{
	struct ext_int_info *p;
	int index;

	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (!p)
		return -ENOMEM;
	p->code = code;
	p->handler = handler;
	index = ext_hash(code);
	p->next = ext_int_hash[index];
	ext_int_hash[index] = p;
	return 0;
}
EXPORT_SYMBOL(register_external_interrupt);

int unregister_external_interrupt(__u16 code, ext_int_handler_t handler)
{
	struct ext_int_info *p, *q;
	int index;

	index = ext_hash(code);
	q = NULL;
	p = ext_int_hash[index];
	while (p) {
		if (p->code == code && p->handler == handler)
			break;
		q = p;
		p = p->next;
	}
	if (!p)
		return -ENOENT;
	if (q)
		q->next = p->next;
	else
		ext_int_hash[index] = p->next;
	kfree(p);
	return 0;
}
EXPORT_SYMBOL(unregister_external_interrupt);

void __irq_entry do_extint(struct pt_regs *regs, unsigned int ext_int_code,
			   unsigned int param32, unsigned long param64)
{
	struct pt_regs *old_regs;
	unsigned short code;
	struct ext_int_info *p;
	int index;

	code = (unsigned short) ext_int_code;
	old_regs = set_irq_regs(regs);
	s390_idle_check(regs, S390_lowcore.int_clock,
			S390_lowcore.async_enter_timer);
	irq_enter();
	if (S390_lowcore.int_clock >= S390_lowcore.clock_comparator)
		/* Serve timer interrupts first. */
		clock_comparator_work();
	kstat_cpu(smp_processor_id()).irqs[EXTERNAL_INTERRUPT]++;
	if (code != 0x1004)
		__get_cpu_var(s390_idle).nohz_delay = 1;
	index = ext_hash(code);
	for (p = ext_int_hash[index]; p; p = p->next) {
		if (likely(p->code == code))
			p->handler(ext_int_code, param32, param64);
	}
	irq_exit();
	set_irq_regs(old_regs);
}
