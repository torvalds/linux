#include <linux/ftrace.h>
#include <xen/interface/xen.h>

#define N(x)	[__HYPERVISOR_##x] = "("#x")"
static const char *xen_hypercall_names[] = {
	N(set_trap_table),
	N(mmu_update),
	N(set_gdt),
	N(stack_switch),
	N(set_callbacks),
	N(fpu_taskswitch),
	N(sched_op_compat),
	N(dom0_op),
	N(set_debugreg),
	N(get_debugreg),
	N(update_descriptor),
	N(memory_op),
	N(multicall),
	N(update_va_mapping),
	N(set_timer_op),
	N(event_channel_op_compat),
	N(xen_version),
	N(console_io),
	N(physdev_op_compat),
	N(grant_table_op),
	N(vm_assist),
	N(update_va_mapping_otherdomain),
	N(iret),
	N(vcpu_op),
	N(set_segment_base),
	N(mmuext_op),
	N(acm_op),
	N(nmi_op),
	N(sched_op),
	N(callback_op),
	N(xenoprof_op),
	N(event_channel_op),
	N(physdev_op),
	N(hvm_op),

/* Architecture-specific hypercall definitions. */
	N(arch_0),
	N(arch_1),
	N(arch_2),
	N(arch_3),
	N(arch_4),
	N(arch_5),
	N(arch_6),
	N(arch_7),
};
#undef N

static const char *xen_hypercall_name(unsigned op)
{
	if (op < ARRAY_SIZE(xen_hypercall_names) && xen_hypercall_names[op] != NULL)
		return xen_hypercall_names[op];

	return "";
}

#define CREATE_TRACE_POINTS
#include <trace/events/xen.h>
