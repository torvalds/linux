/* SPDX-License-Identifier: GPL-2.0 */
#if !defined(_TRACE_HANDLE_EXIT_ARM64_KVM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HANDLE_EXIT_ARM64_KVM_H

#include <linux/tracepoint.h>
#include "sys_regs.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm

TRACE_EVENT(kvm_wfx_arm64,
	TP_PROTO(unsigned long vcpu_pc, bool is_wfe),
	TP_ARGS(vcpu_pc, is_wfe),

	TP_STRUCT__entry(
		__field(unsigned long,	vcpu_pc)
		__field(bool,		is_wfe)
	),

	TP_fast_assign(
		__entry->vcpu_pc = vcpu_pc;
		__entry->is_wfe  = is_wfe;
	),

	TP_printk("guest executed wf%c at: 0x%08lx",
		  __entry->is_wfe ? 'e' : 'i', __entry->vcpu_pc)
);

TRACE_EVENT(kvm_hvc_arm64,
	TP_PROTO(unsigned long vcpu_pc, unsigned long r0, unsigned long imm),
	TP_ARGS(vcpu_pc, r0, imm),

	TP_STRUCT__entry(
		__field(unsigned long, vcpu_pc)
		__field(unsigned long, r0)
		__field(unsigned long, imm)
	),

	TP_fast_assign(
		__entry->vcpu_pc = vcpu_pc;
		__entry->r0 = r0;
		__entry->imm = imm;
	),

	TP_printk("HVC at 0x%08lx (r0: 0x%08lx, imm: 0x%lx)",
		  __entry->vcpu_pc, __entry->r0, __entry->imm)
);

TRACE_EVENT(kvm_arm_setup_debug,
	TP_PROTO(struct kvm_vcpu *vcpu, __u32 guest_debug),
	TP_ARGS(vcpu, guest_debug),

	TP_STRUCT__entry(
		__field(struct kvm_vcpu *, vcpu)
		__field(__u32, guest_debug)
	),

	TP_fast_assign(
		__entry->vcpu = vcpu;
		__entry->guest_debug = guest_debug;
	),

	TP_printk("vcpu: %p, flags: 0x%08x", __entry->vcpu, __entry->guest_debug)
);

TRACE_EVENT(kvm_arm_clear_debug,
	TP_PROTO(__u32 guest_debug),
	TP_ARGS(guest_debug),

	TP_STRUCT__entry(
		__field(__u32, guest_debug)
	),

	TP_fast_assign(
		__entry->guest_debug = guest_debug;
	),

	TP_printk("flags: 0x%08x", __entry->guest_debug)
);

TRACE_EVENT(kvm_arm_set_dreg32,
	TP_PROTO(const char *name, __u32 value),
	TP_ARGS(name, value),

	TP_STRUCT__entry(
		__field(const char *, name)
		__field(__u32, value)
	),

	TP_fast_assign(
		__entry->name = name;
		__entry->value = value;
	),

	TP_printk("%s: 0x%08x", __entry->name, __entry->value)
);

TRACE_DEFINE_SIZEOF(__u64);

TRACE_EVENT(kvm_arm_set_regset,
	TP_PROTO(const char *type, int len, __u64 *control, __u64 *value),
	TP_ARGS(type, len, control, value),
	TP_STRUCT__entry(
		__field(const char *, name)
		__field(int, len)
		__array(u64, ctrls, 16)
		__array(u64, values, 16)
	),
	TP_fast_assign(
		__entry->name = type;
		__entry->len = len;
		memcpy(__entry->ctrls, control, len << 3);
		memcpy(__entry->values, value, len << 3);
	),
	TP_printk("%d %s CTRL:%s VALUE:%s", __entry->len, __entry->name,
		__print_array(__entry->ctrls, __entry->len, sizeof(__u64)),
		__print_array(__entry->values, __entry->len, sizeof(__u64)))
);

TRACE_EVENT(trap_reg,
	TP_PROTO(const char *fn, int reg, bool is_write, u64 write_value),
	TP_ARGS(fn, reg, is_write, write_value),

	TP_STRUCT__entry(
		__field(const char *, fn)
		__field(int, reg)
		__field(bool, is_write)
		__field(u64, write_value)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->reg = reg;
		__entry->is_write = is_write;
		__entry->write_value = write_value;
	),

	TP_printk("%s %s reg %d (0x%08llx)", __entry->fn,  __entry->is_write?"write to":"read from", __entry->reg, __entry->write_value)
);

TRACE_EVENT(kvm_handle_sys_reg,
	TP_PROTO(unsigned long hsr),
	TP_ARGS(hsr),

	TP_STRUCT__entry(
		__field(unsigned long,	hsr)
	),

	TP_fast_assign(
		__entry->hsr = hsr;
	),

	TP_printk("HSR 0x%08lx", __entry->hsr)
);

TRACE_EVENT(kvm_sys_access,
	TP_PROTO(unsigned long vcpu_pc, struct sys_reg_params *params, const struct sys_reg_desc *reg),
	TP_ARGS(vcpu_pc, params, reg),

	TP_STRUCT__entry(
		__field(unsigned long,			vcpu_pc)
		__field(bool,				is_write)
		__field(const char *,			name)
		__field(u8,				Op0)
		__field(u8,				Op1)
		__field(u8,				CRn)
		__field(u8,				CRm)
		__field(u8,				Op2)
	),

	TP_fast_assign(
		__entry->vcpu_pc = vcpu_pc;
		__entry->is_write = params->is_write;
		__entry->name = reg->name;
		__entry->Op0 = reg->Op0;
		__entry->Op0 = reg->Op0;
		__entry->Op1 = reg->Op1;
		__entry->CRn = reg->CRn;
		__entry->CRm = reg->CRm;
		__entry->Op2 = reg->Op2;
	),

	TP_printk("PC: %lx %s (%d,%d,%d,%d,%d) %s",
		  __entry->vcpu_pc, __entry->name ?: "UNKN",
		  __entry->Op0, __entry->Op1, __entry->CRn,
		  __entry->CRm, __entry->Op2,
		  __entry->is_write ? "write" : "read")
);

TRACE_EVENT(kvm_set_guest_debug,
	TP_PROTO(struct kvm_vcpu *vcpu, __u32 guest_debug),
	TP_ARGS(vcpu, guest_debug),

	TP_STRUCT__entry(
		__field(struct kvm_vcpu *, vcpu)
		__field(__u32, guest_debug)
	),

	TP_fast_assign(
		__entry->vcpu = vcpu;
		__entry->guest_debug = guest_debug;
	),

	TP_printk("vcpu: %p, flags: 0x%08x", __entry->vcpu, __entry->guest_debug)
);

#endif /* _TRACE_HANDLE_EXIT_ARM64_KVM_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace_handle_exit

/* This part must be outside protection */
#include <trace/define_trace.h>
