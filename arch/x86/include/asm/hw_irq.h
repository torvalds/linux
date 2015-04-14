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

#ifndef __ASSEMBLY__

#include <linux/percpu.h>
#include <linux/profile.h>
#include <linux/smp.h>

#include <linux/atomic.h>
#include <asm/irq.h>
#include <asm/sections.h>

/* Interrupt handlers registered during init_IRQ */
extern asmlinkage void apic_timer_interrupt(void);
extern asmlinkage void x86_platform_ipi(void);
extern asmlinkage void kvm_posted_intr_ipi(void);
extern asmlinkage void error_interrupt(void);
extern asmlinkage void irq_work_interrupt(void);

extern asmlinkage void spurious_interrupt(void);
extern asmlinkage void thermal_interrupt(void);
extern asmlinkage void reschedule_interrupt(void);

extern asmlinkage void invalidate_interrupt(void);
extern asmlinkage void invalidate_interrupt0(void);
extern asmlinkage void invalidate_interrupt1(void);
extern asmlinkage void invalidate_interrupt2(void);
extern asmlinkage void invalidate_interrupt3(void);
extern asmlinkage void invalidate_interrupt4(void);
extern asmlinkage void invalidate_interrupt5(void);
extern asmlinkage void invalidate_interrupt6(void);
extern asmlinkage void invalidate_interrupt7(void);
extern asmlinkage void invalidate_interrupt8(void);
extern asmlinkage void invalidate_interrupt9(void);
extern asmlinkage void invalidate_interrupt10(void);
extern asmlinkage void invalidate_interrupt11(void);
extern asmlinkage void invalidate_interrupt12(void);
extern asmlinkage void invalidate_interrupt13(void);
extern asmlinkage void invalidate_interrupt14(void);
extern asmlinkage void invalidate_interrupt15(void);
extern asmlinkage void invalidate_interrupt16(void);
extern asmlinkage void invalidate_interrupt17(void);
extern asmlinkage void invalidate_interrupt18(void);
extern asmlinkage void invalidate_interrupt19(void);
extern asmlinkage void invalidate_interrupt20(void);
extern asmlinkage void invalidate_interrupt21(void);
extern asmlinkage void invalidate_interrupt22(void);
extern asmlinkage void invalidate_interrupt23(void);
extern asmlinkage void invalidate_interrupt24(void);
extern asmlinkage void invalidate_interrupt25(void);
extern asmlinkage void invalidate_interrupt26(void);
extern asmlinkage void invalidate_interrupt27(void);
extern asmlinkage void invalidate_interrupt28(void);
extern asmlinkage void invalidate_interrupt29(void);
extern asmlinkage void invalidate_interrupt30(void);
extern asmlinkage void invalidate_interrupt31(void);

extern asmlinkage void irq_move_cleanup_interrupt(void);
extern asmlinkage void reboot_interrupt(void);
extern asmlinkage void threshold_interrupt(void);

extern asmlinkage void call_function_interrupt(void);
extern asmlinkage void call_function_single_interrupt(void);

#ifdef CONFIG_TRACING
/* Interrupt handlers registered during init_IRQ */
extern void trace_apic_timer_interrupt(void);
extern void trace_x86_platform_ipi(void);
extern void trace_error_interrupt(void);
extern void trace_irq_work_interrupt(void);
extern void trace_spurious_interrupt(void);
extern void trace_thermal_interrupt(void);
extern void trace_reschedule_interrupt(void);
extern void trace_threshold_interrupt(void);
extern void trace_call_function_interrupt(void);
extern void trace_call_function_single_interrupt(void);
#define trace_irq_move_cleanup_interrupt  irq_move_cleanup_interrupt
#define trace_reboot_interrupt  reboot_interrupt
#define trace_kvm_posted_intr_ipi kvm_posted_intr_ipi
#endif /* CONFIG_TRACING */

struct irq_domain;

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
#ifdef	CONFIG_HT_IRQ
		struct {
			int		ht_pos;
			int		ht_idx;
			struct pci_dev	*ht_dev;
			void		*ht_update;
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
	};
};

enum {
	/* Allocate contiguous CPU vectors */
	X86_IRQ_ALLOC_CONTIGUOUS_VECTORS		= 0x1,
};

struct irq_cfg {
	unsigned int		dest_apicid;
	u8			vector;
};

extern struct irq_domain *x86_vector_domain;

extern void init_irq_alloc_info(struct irq_alloc_info *info,
				const struct cpumask *mask);
extern void copy_irq_alloc_info(struct irq_alloc_info *dst,
				struct irq_alloc_info *src);
extern struct irq_cfg *irq_cfg(unsigned int irq);
extern struct irq_cfg *irqd_cfg(struct irq_data *irq_data);
extern void lock_vector_lock(void);
extern void unlock_vector_lock(void);
extern void setup_vector_irq(int cpu);
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

#ifdef	CONFIG_PCI_MSI
extern void arch_init_msi_domain(struct irq_domain *domain);
#else
static inline void arch_init_msi_domain(struct irq_domain *domain) { }
#endif
#ifdef	CONFIG_HT_IRQ
extern void arch_init_htirq_domain(struct irq_domain *domain);
#else
static inline void arch_init_htirq_domain(struct irq_domain *domain) { }
#endif

/* Statistics */
extern atomic_t irq_err_count;
extern atomic_t irq_mis_count;

/* EISA */
extern void eisa_set_level_irq(unsigned int irq);

/* SMP */
extern __visible void smp_apic_timer_interrupt(struct pt_regs *);
extern __visible void smp_spurious_interrupt(struct pt_regs *);
extern __visible void smp_x86_platform_ipi(struct pt_regs *);
extern __visible void smp_error_interrupt(struct pt_regs *);
#ifdef CONFIG_X86_IO_APIC
extern asmlinkage void smp_irq_move_cleanup_interrupt(void);
#endif
#ifdef CONFIG_SMP
extern __visible void smp_reschedule_interrupt(struct pt_regs *);
extern __visible void smp_call_function_interrupt(struct pt_regs *);
extern __visible void smp_call_function_single_interrupt(struct pt_regs *);
extern __visible void smp_invalidate_interrupt(struct pt_regs *);
#endif

extern char irq_entries_start[];
#ifdef CONFIG_TRACING
#define trace_irq_entries_start irq_entries_start
#endif

#define VECTOR_UNDEFINED	(-1)
#define VECTOR_RETRIGGERED	(-2)

typedef int vector_irq_t[NR_VECTORS];
DECLARE_PER_CPU(vector_irq_t, vector_irq);

#endif /* !ASSEMBLY_ */

#endif /* _ASM_X86_HW_IRQ_H */
