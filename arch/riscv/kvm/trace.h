// SPDX-License-Identifier: GPL-2.0
/*
 * Tracepoints for RISC-V KVM
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */
#if !defined(_TRACE_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_H

#include <linux/tracepoint.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

TRACE_EVENT(kvm_entry,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(unsigned long, pc)
	),

	TP_fast_assign(
		__entry->pc	= vcpu->arch.guest_context.sepc;
	),

	TP_printk("PC: 0x%016lx", __entry->pc)
);

TRACE_EVENT(kvm_exit,
	TP_PROTO(struct kvm_cpu_trap *trap),
	TP_ARGS(trap),

	TP_STRUCT__entry(
		__field(unsigned long, sepc)
		__field(unsigned long, scause)
		__field(unsigned long, stval)
		__field(unsigned long, htval)
		__field(unsigned long, htinst)
	),

	TP_fast_assign(
		__entry->sepc		= trap->sepc;
		__entry->scause		= trap->scause;
		__entry->stval		= trap->stval;
		__entry->htval		= trap->htval;
		__entry->htinst		= trap->htinst;
	),

	TP_printk("SEPC:0x%lx, SCAUSE:0x%lx, STVAL:0x%lx, HTVAL:0x%lx, HTINST:0x%lx",
		__entry->sepc,
		__entry->scause,
		__entry->stval,
		__entry->htval,
		__entry->htinst)
);

TRACE_EVENT(kvm_mmio_emulate,
	TP_PROTO(unsigned long vcpu_id, unsigned long sepc, unsigned long insn,
		 unsigned long fault_addr, bool write, int len),
	TP_ARGS(vcpu_id, sepc, insn, fault_addr, write, len),

	TP_STRUCT__entry(
		__field(unsigned long, vcpu_id)
		__field(unsigned long, sepc)
		__field(unsigned long, insn)
		__field(unsigned long, fault_addr)
		__field(bool, write)
		__field(int, len)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->sepc		= sepc;
		__entry->insn		= insn;
		__entry->fault_addr	= fault_addr;
		__entry->write		= write;
		__entry->len		= len;
	),

	TP_printk("VCPU: %lu, %s MMIO at 0x%lx, len %d, insn 0x%lx, sepc 0x%lx",
		  __entry->vcpu_id, __entry->write ? "Store" : "Load",
		  __entry->fault_addr, __entry->len, __entry->insn,
		  __entry->sepc)
);

TRACE_EVENT(kvm_vcpu_exit,
	TP_PROTO(unsigned long vcpu_id, unsigned long sepc, unsigned long scause,
		 unsigned long stval, unsigned long htval, unsigned long htinst),
	TP_ARGS(vcpu_id, sepc, scause, stval, htval, htinst),

	TP_STRUCT__entry(
		__field(unsigned long, vcpu_id)
		__field(unsigned long, sepc)
		__field(unsigned long, scause)
		__field(unsigned long, stval)
		__field(unsigned long, htval)
		__field(unsigned long, htinst)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->sepc		= sepc;
		__entry->scause		= scause;
		__entry->stval		= stval;
		__entry->htval		= htval;
		__entry->htinst		= htinst;
	),

	TP_printk("VCPU: %lu, SEPC: 0x%lx, SCAUSE: 0x%lx, STVAL: 0x%lx, HTVAL: 0x%lx, HTINST: 0x%lx",
		  __entry->vcpu_id, __entry->sepc, __entry->scause,
		  __entry->stval, __entry->htval, __entry->htinst)
);

TRACE_EVENT(kvm_vcpu_irq,
	TP_PROTO(unsigned long vcpu_id, unsigned int irq, int level),
	TP_ARGS(vcpu_id, irq, level),

	TP_STRUCT__entry(
		__field(unsigned long, vcpu_id)
		__field(unsigned int, irq)
		__field(int, level)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu_id;
		__entry->irq		= irq;
		__entry->level		= level;
	),

	TP_printk("VCPU: %lu, IRQ: %u, level: %d",
		  __entry->vcpu_id, __entry->irq, __entry->level)
);

#endif /* _TRACE_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

/* This part must be outside protection */
#include <trace/define_trace.h>
