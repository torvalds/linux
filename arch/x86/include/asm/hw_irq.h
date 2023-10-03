/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HW_IRQ_H
#define _ASM_X86_HW_IRQ_H

/*
 * (C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 * moved some of the old arch/i386/kernel/irq.h to here. VY
 *
 * IRQ/IPI changes taken from work by Thomas Radke
 * <tomsoft@informatik.tu-chemnitz.de>
 *
 * hacked by Andi Kleen for x86-64.
 * unified by tglx
 */

#include <asm/irq_vectors.h>

#define IRQ_MATRIX_BITS		NR_VECTORS

#ifndef __ASSEMBLY__

#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/smp.h>

#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/sections.h>

#ifdef	CONFIG_X86_LOCAL_APIC
struct irq_data;
struct pci_dev;
struct msi_desc;

enum irq_alloc_type {
	X86_IRQ_ALLOC_TYPE_IOAPIC = 1,
	X86_IRQ_ALLOC_TYPE_HPET,
	X86_IRQ_ALLOC_TYPE_PCI_MSI,
	X86_IRQ_ALLOC_TYPE_PCI_MSIX,
	X86_IRQ_ALLOC_TYPE_DMAR,
	X86_IRQ_ALLOC_TYPE_AMDVI,
	X86_IRQ_ALLOC_TYPE_UV,
};

struct ioapic_alloc_info {
	int		pin;
	int		node;
	u32		is_level	: 1;
	u32		active_low	: 1;
	u32		valid		: 1;
};

struct uv_alloc_info {
	int		limit;
	int		blade;
	unsigned long	offset;
	char		*name;

};

/**
 * irq_alloc_info - X86 specific interrupt allocation info
 * @type:	X86 specific allocation type
 * @flags:	Flags for allocation tweaks
 * @devid:	Device ID for allocations
 * @hwirq:	Associated hw interrupt number in the domain
 * @mask:	CPU mask for vector allocation
 * @desc:	Pointer to msi descriptor
 * @data:	Allocation specific data
 *
 * @ioapic:	IOAPIC specific allocation data
 * @uv:		UV specific allocation data
*/
struct irq_alloc_info {
	enum irq_alloc_type	type;
	u32			flags;
	u32			devid;
	irq_hw_number_t		hwirq;
	const struct cpumask	*mask;
	struct msi_desc		*desc;
	void			*data;

	union {
		struct ioapic_alloc_info	ioapic;
		struct uv_alloc_info		uv;
	};
};

struct irq_cfg {
	unsigned int		dest_apicid;
	unsigned int		vector;
};

extern struct irq_cfg *irq_cfg(unsigned int irq);
extern struct irq_cfg *irqd_cfg(struct irq_data *irq_data);
extern void lock_vector_lock(void);
extern void unlock_vector_lock(void);
#ifdef CONFIG_SMP
extern void vector_schedule_cleanup(struct irq_cfg *);
extern void irq_complete_move(struct irq_cfg *cfg);
#else
static inline void vector_schedule_cleanup(struct irq_cfg *c) { }
static inline void irq_complete_move(struct irq_cfg *c) { }
#endif

extern void apic_ack_edge(struct irq_data *data);
#else	/*  CONFIG_X86_LOCAL_APIC */
static inline void lock_vector_lock(void) {}
static inline void unlock_vector_lock(void) {}
#endif	/* CONFIG_X86_LOCAL_APIC */

/* Statistics */
extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

extern void elcr_set_level_irq(unsigned int irq);

extern char irq_entries_start[];
#ifdef CONFIG_TRACING
#define trace_irq_entries_start irq_entries_start
#endif

extern char spurious_entries_start[];

#define VECTOR_UNUSED		NULL
#define VECTOR_SHUTDOWN		((void *)-1L)
#define VECTOR_RETRIGGERED	((void *)-2L)

typedef struct irq_desc* vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);

#endif /* !ASSEMBLY_ */

#endif /* _ASM_X86_HW_IRQ_H */
