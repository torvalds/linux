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
	X86_IRQ_ALLOC_TYPE_MSI,
	X86_IRQ_ALLOC_TYPE_MSIX,
	X86_IRQ_ALLOC_TYPE_DMAR,
	X86_IRQ_ALLOC_TYPE_UV,
};

struct irq_alloc_info {
	enum irq_alloc_type	type;
	u32			flags;
	const struct cpumask	*mask;	/* CPU mask for vector allocation */
	union {
		int		unused;
#ifdef	CONFIG_HPET_TIMER
		struct {
			int		hpet_id;
			int		hpet_index;
			void		*hpet_data;
		};
#endif
#ifdef	CONFIG_PCI_MSI
		struct {
			struct pci_dev	*msi_dev;
			irq_hw_number_t	msi_hwirq;
		};
#endif
#ifdef	CONFIG_X86_IO_APIC
		struct {
			int		ioapic_id;
			int		ioapic_pin;
			int		ioapic_node;
			u32		ioapic_trigger : 1;
			u32		ioapic_polarity : 1;
			u32		ioapic_valid : 1;
			struct IO_APIC_route_entry *ioapic_entry;
		};
#endif
#ifdef	CONFIG_DMAR_TABLE
		struct {
			int		dmar_id;
			void		*dmar_data;
		};
#endif
#ifdef	CONFIG_X86_UV
		struct {
			int		uv_limit;
			int		uv_blade;
			unsigned long	uv_offset;
			char		*uv_name;
		};
#endif
#if IS_ENABLED(CONFIG_VMD)
		struct {
			struct msi_desc *desc;
		};
#endif
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
extern void send_cleanup_vector(struct irq_cfg *);
extern void irq_complete_move(struct irq_cfg *cfg);
#else
static inline void send_cleanup_vector(struct irq_cfg *c) { }
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
