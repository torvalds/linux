#ifndef __ASM_AVR32_HARDIRQ_H
#define __ASM_AVR32_HARDIRQ_H

#include <linux/threads.h>
#include <asm/irq.h>

#ifndef __ASSEMBLY__

#include <linux/cache.h>

/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

void ack_bad_irq(unsigned int irq);

/* Standard mappings for irq_cpustat_t above */
#include <linux/irq_cpustat.h>

#endif /* __ASSEMBLY__ */

#endif /* __ASM_AVR32_HARDIRQ_H */
