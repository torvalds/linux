#ifndef _ASM_X86_HARDIRQ_64_H
#define _ASM_X86_HARDIRQ_64_H

#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/pda.h>
#include <asm/apic.h>

/* We can have at most NR_VECTORS irqs routed to a cpu at a time */
#define MAX_HARDIRQS_PER_CPU NR_VECTORS

#define __ARCH_IRQ_STAT 1

#define inc_irq_stat(member)	add_pda(member, 1)

#define local_softirq_pending() read_pda(__softirq_pending)

#define __ARCH_SET_SOFTIRQ_PENDING 1

#define set_softirq_pending(x) write_pda(__softirq_pending, (x))
#define or_softirq_pending(x)  or_pda(__softirq_pending, (x))

extern void ack_bad_irq(unsigned int irq);

#endif /* _ASM_X86_HARDIRQ_64_H */
