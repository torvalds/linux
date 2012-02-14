#ifdef __KERNEL__
#ifndef _ASM_POWERPC_IRQ_H
#define _ASM_POWERPC_IRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/irqdomain.h>
#include <linux/threads.h>
#include <linux/list.h>
#include <linux/radix-tree.h>

#include <asm/types.h>
#include <linux/atomic.h>


/* Define a way to iterate across irqs. */
#define for_each_irq(i) \
	for ((i) = 0; (i) < NR_IRQS; ++(i))

extern atomic_t ppc_n_lost_interrupts;

/* This number is used when no interrupt has been assigned */
#define NO_IRQ			(0)

/* This is a special irq number to return from get_irq() to tell that
 * no interrupt happened _and_ ignore it (don't count it as bad). Some
 * platforms like iSeries rely on that.
 */
#define NO_IRQ_IGNORE		((unsigned int)-1)

/* Total number of virq in the platform */
#define NR_IRQS		CONFIG_NR_IRQS

/* Number of irqs reserved for the legacy controller */
#define NUM_ISA_INTERRUPTS	16

/* Same thing, used by the generic IRQ code */
#define NR_IRQS_LEGACY		NUM_ISA_INTERRUPTS

/*
 * The host code and data structures are fairly agnostic to the fact that
 * we use an open firmware device-tree. We do have references to struct
 * device_node in two places: in irq_find_host() to find the host matching
 * a given interrupt controller node, and of course as an argument to its
 * counterpart host->ops->match() callback. However, those are treated as
 * generic pointers by the core and the fact that it's actually a device-node
 * pointer is purely a convention between callers and implementation. This
 * code could thus be used on other architectures by replacing those two
 * by some sort of arch-specific void * "token" used to identify interrupt
 * controllers.
 */

struct irq_data;
extern irq_hw_number_t irqd_to_hwirq(struct irq_data *d);
extern irq_hw_number_t virq_to_hw(unsigned int virq);

/**
 * irq_alloc_host - Allocate a new irq_domain data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @revmap_type: type of reverse mapping to use
 * @revmap_arg: for IRQ_DOMAIN_MAP_LINEAR linear only: size of the map
 * @ops: map/unmap host callbacks
 * @inval_irq: provide a hw number in that host space that is always invalid
 *
 * Allocates and initialize and irq_domain structure. Note that in the case of
 * IRQ_DOMAIN_MAP_LEGACY, the map() callback will be called before this returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller). For a IRQ_DOMAIN_MAP_LINEAR, the map is allocated by
 * this call as well. For a IRQ_DOMAIN_MAP_TREE, the radix tree will be allocated
 * later during boot automatically (the reverse mapping will use the slow path
 * until that happens).
 */
extern struct irq_domain *irq_alloc_host(struct device_node *of_node,
				       unsigned int revmap_type,
				       unsigned int revmap_arg,
				       struct irq_domain_ops *ops,
				       irq_hw_number_t inval_irq);


/**
 * irq_find_host - Locates a host for a given device node
 * @node: device-tree node of the interrupt controller
 */
extern struct irq_domain *irq_find_host(struct device_node *node);


/**
 * irq_set_default_host - Set a "default" host
 * @host: default host pointer
 *
 * For convenience, it's possible to set a "default" host that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
extern void irq_set_default_host(struct irq_domain *host);


/**
 * irq_set_virq_count - Set the maximum number of virt irqs
 * @count: number of linux virtual irqs, capped with NR_IRQS
 *
 * This is mainly for use by platforms like iSeries who want to program
 * the virtual irq number in the controller to avoid the reverse mapping
 */
extern void irq_set_virq_count(unsigned int count);


/**
 * irq_create_mapping - Map a hardware interrupt into linux virq space
 * @host: host owning this hardware interrupt or NULL for default host
 * @hwirq: hardware irq number in that host space
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * virq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
extern unsigned int irq_create_mapping(struct irq_domain *host,
				       irq_hw_number_t hwirq);


/**
 * irq_dispose_mapping - Unmap an interrupt
 * @virq: linux virq number of the interrupt to unmap
 */
extern void irq_dispose_mapping(unsigned int virq);

/**
 * irq_find_mapping - Find a linux virq from an hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a slow path, for use by generic code. It's expected that an
 * irq controller implementation directly calls the appropriate low level
 * mapping function.
 */
extern unsigned int irq_find_mapping(struct irq_domain *host,
				     irq_hw_number_t hwirq);

/**
 * irq_create_direct_mapping - Allocate a virq for direct mapping
 * @host: host to allocate the virq for or NULL for default host
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux virq as the hardware interrupt number.
 */
extern unsigned int irq_create_direct_mapping(struct irq_domain *host);

/**
 * irq_radix_revmap_insert - Insert a hw irq to linux virq number mapping.
 * @host: host owning this hardware interrupt
 * @virq: linux irq number
 * @hwirq: hardware irq number in that host space
 *
 * This is for use by irq controllers that use a radix tree reverse
 * mapping for fast lookup.
 */
extern void irq_radix_revmap_insert(struct irq_domain *host, unsigned int virq,
				    irq_hw_number_t hwirq);

/**
 * irq_radix_revmap_lookup - Find a linux virq from a hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a fast path, for use by irq controller code that uses radix tree
 * revmaps
 */
extern unsigned int irq_radix_revmap_lookup(struct irq_domain *host,
					    irq_hw_number_t hwirq);

/**
 * irq_linear_revmap - Find a linux virq from a hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a fast path, for use by irq controller code that uses linear
 * revmaps. It does fallback to the slow path if the revmap doesn't exist
 * yet and will create the revmap entry with appropriate locking
 */

extern unsigned int irq_linear_revmap(struct irq_domain *host,
				      irq_hw_number_t hwirq);


/**
 * irq_early_init - Init irq remapping subsystem
 */
extern void irq_early_init(void);

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern int distribute_irqs;

struct irqaction;
struct pt_regs;

#define __ARCH_HAS_DO_SOFTIRQ

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
/*
 * Per-cpu stacks for handling critical, debug and machine check
 * level interrupts.
 */
extern struct thread_info *critirq_ctx[NR_CPUS];
extern struct thread_info *dbgirq_ctx[NR_CPUS];
extern struct thread_info *mcheckirq_ctx[NR_CPUS];
extern void exc_lvl_ctx_init(void);
#else
#define exc_lvl_ctx_init()
#endif

/*
 * Per-cpu stacks for handling hard and soft interrupts.
 */
extern struct thread_info *hardirq_ctx[NR_CPUS];
extern struct thread_info *softirq_ctx[NR_CPUS];

extern void irq_ctx_init(void);
extern void call_do_softirq(struct thread_info *tp);
extern int call_handle_irq(int irq, void *p1,
			   struct thread_info *tp, void *func);
extern void do_IRQ(struct pt_regs *regs);

int irq_choose_cpu(const struct cpumask *mask);

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
