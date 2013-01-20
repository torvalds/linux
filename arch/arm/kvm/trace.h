#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

/*
 * Tracepoints for entry/exit to guest
 */
TRACE_EVENT(kvm_entry,
	TP_PROTO(unsigned long vcpu_pc),
	TP_ARGS(vcpu_pc),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_pc		)
	),

	TP_fast_assign(
		__entry->vcpu_pc		= vcpu_pc;
	),

	TP_printk("PC: 0x%08lx", __entry->vcpu_pc)
);

TRACE_EVENT(kvm_exit,
	TP_PROTO(unsigned long vcpu_pc),
	TP_ARGS(vcpu_pc),

	TP_STRUCT__entry(
		__field(	unsigned long,	vcpu_pc		)
	),

	TP_fast_assign(
		__entry->vcpu_pc		= vcpu_pc;
	),

	TP_printk("PC: 0x%08lx", __entry->vcpu_pc)
);

TRACE_EVENT(kvm_irq_line,
	TP_PROTO(unsigned int type, int vcpu_idx, int irq_num, int level),
	TP_ARGS(type, vcpu_idx, irq_num, level),

	TP_STRUCT__entry(
		__field(	unsigned int,	type		)
		__field(	int,		vcpu_idx	)
		__field(	int,		irq_num		)
		__field(	int,		level		)
	),

	TP_fast_assign(
		__entry->type		= type;
		__entry->vcpu_idx	= vcpu_idx;
		__entry->irq_num	= irq_num;
		__entry->level		= level;
	),

	TP_printk("Inject %s interrupt (%d), vcpu->idx: %d, num: %d, level: %d",
		  (__entry->type == KVM_ARM_IRQ_TYPE_CPU) ? "CPU" :
		  (__entry->type == KVM_ARM_IRQ_TYPE_PPI) ? "VGIC PPI" :
		  (__entry->type == KVM_ARM_IRQ_TYPE_SPI) ? "VGIC SPI" : "UNKNOWN",
		  __entry->type, __entry->vcpu_idx, __entry->irq_num, __entry->level)
);

TRACE_EVENT(kvm_unmap_hva,
	TP_PROTO(unsigned long hva),
	TP_ARGS(hva),

	TP_STRUCT__entry(
		__field(	unsigned long,	hva		)
	),

	TP_fast_assign(
		__entry->hva		= hva;
	),

	TP_printk("mmu notifier unmap hva: %#08lx", __entry->hva)
);

TRACE_EVENT(kvm_unmap_hva_range,
	TP_PROTO(unsigned long start, unsigned long end),
	TP_ARGS(start, end),

	TP_STRUCT__entry(
		__field(	unsigned long,	start		)
		__field(	unsigned long,	end		)
	),

	TP_fast_assign(
		__entry->start		= start;
		__entry->end		= end;
	),

	TP_printk("mmu notifier unmap range: %#08lx -- %#08lx",
		  __entry->start, __entry->end)
);

TRACE_EVENT(kvm_set_spte_hva,
	TP_PROTO(unsigned long hva),
	TP_ARGS(hva),

	TP_STRUCT__entry(
		__field(	unsigned long,	hva		)
	),

	TP_fast_assign(
		__entry->hva		= hva;
	),

	TP_printk("mmu notifier set pte hva: %#08lx", __entry->hva)
);

#endif /* _TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH arch/arm/kvm
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
