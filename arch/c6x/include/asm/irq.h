/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2006, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Large parts taken directly from powerpc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_IRQ_H
#define _ASM_C6X_IRQ_H

#include <linux/threads.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <asm/percpu.h>

#define irq_canonicalize(irq)  (irq)

/*
 * The C64X+ core has 16 IRQ vectors. One each is used by Reset and NMI. Two
 * are reserved. The remaining 12 vectors are used to route SoC interrupts.
 * These interrupt vectors are prioritized with IRQ 4 having the highest
 * priority and IRQ 15 having the lowest.
 *
 * The C64x+ megamodule provides a PIC which combines SoC IRQ sources into a
 * single core IRQ vector. There are four combined sources, each of which
 * feed into one of the 12 general interrupt vectors. The remaining 8 vectors
 * can each route a single SoC interrupt directly.
 */
#define NR_PRIORITY_IRQS 16

#define NR_IRQS_LEGACY	NR_PRIORITY_IRQS

/* Total number of virq in the platform */
#define NR_IRQS		256

/* This number is used when no interrupt has been assigned */
#define NO_IRQ		0

/* This type is the placeholder for a hardware interrupt number. It has to
 * be big enough to enclose whatever representation is used by a given
 * platform.
 */
typedef unsigned long irq_hw_number_t;

/* Interrupt controller "host" data structure. This could be defined as a
 * irq domain controller. That is, it handles the mapping between hardware
 * and virtual interrupt numbers for a given interrupt domain. The host
 * structure is generally created by the PIC code for a given PIC instance
 * (though a host can cover more than one PIC if they have a flat number
 * model). It's the host callbacks that are responsible for setting the
 * irq_chip on a given irq_desc after it's been mapped.
 *
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
struct irq_host;
struct radix_tree_root;
struct device_node;

/* Functions below are provided by the host and called whenever a new mapping
 * is created or an old mapping is disposed. The host can then proceed to
 * whatever internal data structures management is required. It also needs
 * to setup the irq_desc when returning from map().
 */
struct irq_host_ops {
	/* Match an interrupt controller device node to a host, returns
	 * 1 on a match
	 */
	int (*match)(struct irq_host *h, struct device_node *node);

	/* Create or update a mapping between a virtual irq number and a hw
	 * irq number. This is called only once for a given mapping.
	 */
	int (*map)(struct irq_host *h, unsigned int virq, irq_hw_number_t hw);

	/* Dispose of such a mapping */
	void (*unmap)(struct irq_host *h, unsigned int virq);

	/* Translate device-tree interrupt specifier from raw format coming
	 * from the firmware to a irq_hw_number_t (interrupt line number) and
	 * type (sense) that can be passed to set_irq_type(). In the absence
	 * of this callback, irq_create_of_mapping() and irq_of_parse_and_map()
	 * will return the hw number in the first cell and IRQ_TYPE_NONE for
	 * the type (which amount to keeping whatever default value the
	 * interrupt controller has for that line)
	 */
	int (*xlate)(struct irq_host *h, struct device_node *ctrler,
		     const u32 *intspec, unsigned int intsize,
		     irq_hw_number_t *out_hwirq, unsigned int *out_type);
};

struct irq_host {
	struct list_head	link;

	/* type of reverse mapping technique */
	unsigned int		revmap_type;
#define IRQ_HOST_MAP_PRIORITY   0 /* core priority irqs, get irqs 1..15 */
#define IRQ_HOST_MAP_NOMAP	1 /* no fast reverse mapping */
#define IRQ_HOST_MAP_LINEAR	2 /* linear map of interrupts */
#define IRQ_HOST_MAP_TREE	3 /* radix tree */
	union {
		struct {
			unsigned int size;
			unsigned int *revmap;
		} linear;
		struct radix_tree_root tree;
	} revmap_data;
	struct irq_host_ops	*ops;
	void			*host_data;
	irq_hw_number_t		inval_irq;

	/* Optional device node pointer */
	struct device_node	*of_node;
};

struct irq_data;
extern irq_hw_number_t irqd_to_hwirq(struct irq_data *d);
extern irq_hw_number_t virq_to_hw(unsigned int virq);
extern bool virq_is_host(unsigned int virq, struct irq_host *host);

/**
 * irq_alloc_host - Allocate a new irq_host data structure
 * @of_node: optional device-tree node of the interrupt controller
 * @revmap_type: type of reverse mapping to use
 * @revmap_arg: for IRQ_HOST_MAP_LINEAR linear only: size of the map
 * @ops: map/unmap host callbacks
 * @inval_irq: provide a hw number in that host space that is always invalid
 *
 * Allocates and initialize and irq_host structure. Note that in the case of
 * IRQ_HOST_MAP_LEGACY, the map() callback will be called before this returns
 * for all legacy interrupts except 0 (which is always the invalid irq for
 * a legacy controller). For a IRQ_HOST_MAP_LINEAR, the map is allocated by
 * this call as well. For a IRQ_HOST_MAP_TREE, the radix tree will be allocated
 * later during boot automatically (the reverse mapping will use the slow path
 * until that happens).
 */
extern struct irq_host *irq_alloc_host(struct device_node *of_node,
				       unsigned int revmap_type,
				       unsigned int revmap_arg,
				       struct irq_host_ops *ops,
				       irq_hw_number_t inval_irq);


/**
 * irq_find_host - Locates a host for a given device node
 * @node: device-tree node of the interrupt controller
 */
extern struct irq_host *irq_find_host(struct device_node *node);


/**
 * irq_set_default_host - Set a "default" host
 * @host: default host pointer
 *
 * For convenience, it's possible to set a "default" host that will be used
 * whenever NULL is passed to irq_create_mapping(). It makes life easier for
 * platforms that want to manipulate a few hard coded interrupt numbers that
 * aren't properly represented in the device-tree.
 */
extern void irq_set_default_host(struct irq_host *host);


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
extern unsigned int irq_create_mapping(struct irq_host *host,
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
extern unsigned int irq_find_mapping(struct irq_host *host,
				     irq_hw_number_t hwirq);

/**
 * irq_create_direct_mapping - Allocate a virq for direct mapping
 * @host: host to allocate the virq for or NULL for default host
 *
 * This routine is used for irq controllers which can choose the hardware
 * interrupt numbers they generate. In such a case it's simplest to use
 * the linux virq as the hardware interrupt number.
 */
extern unsigned int irq_create_direct_mapping(struct irq_host *host);

/**
 * irq_radix_revmap_insert - Insert a hw irq to linux virq number mapping.
 * @host: host owning this hardware interrupt
 * @virq: linux irq number
 * @hwirq: hardware irq number in that host space
 *
 * This is for use by irq controllers that use a radix tree reverse
 * mapping for fast lookup.
 */
extern void irq_radix_revmap_insert(struct irq_host *host, unsigned int virq,
				    irq_hw_number_t hwirq);

/**
 * irq_radix_revmap_lookup - Find a linux virq from a hw irq number.
 * @host: host owning this hardware interrupt
 * @hwirq: hardware irq number in that host space
 *
 * This is a fast path, for use by irq controller code that uses radix tree
 * revmaps
 */
extern unsigned int irq_radix_revmap_lookup(struct irq_host *host,
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

extern unsigned int irq_linear_revmap(struct irq_host *host,
				      irq_hw_number_t hwirq);



/**
 * irq_alloc_virt - Allocate virtual irq numbers
 * @host: host owning these new virtual irqs
 * @count: number of consecutive numbers to allocate
 * @hint: pass a hint number, the allocator will try to use a 1:1 mapping
 *
 * This is a low level function that is used internally by irq_create_mapping()
 * and that can be used by some irq controllers implementations for things
 * like allocating ranges of numbers for MSIs. The revmaps are left untouched.
 */
extern unsigned int irq_alloc_virt(struct irq_host *host,
				   unsigned int count,
				   unsigned int hint);

/**
 * irq_free_virt - Free virtual irq numbers
 * @virq: virtual irq number of the first interrupt to free
 * @count: number of interrupts to free
 *
 * This function is the opposite of irq_alloc_virt. It will not clear reverse
 * maps, this should be done previously by unmap'ing the interrupt. In fact,
 * all interrupts covered by the range being freed should have been unmapped
 * prior to calling this.
 */
extern void irq_free_virt(unsigned int virq, unsigned int count);

extern void __init init_pic_c64xplus(void);

extern void init_IRQ(void);

struct pt_regs;

extern asmlinkage void c6x_do_IRQ(unsigned int prio, struct pt_regs *regs);

extern unsigned long irq_err_count;

#endif /* _ASM_C6X_IRQ_H */
