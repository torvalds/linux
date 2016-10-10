/*
 * Xen stolen ticks accounting.
 */
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/math64.h>
#include <linux/gfp.h>

#include <asm/paravirt.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>

#include <xen/events.h>
#include <xen/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>
#include <xen/xen-ops.h>

/* runstate info updated by Xen */
static DEFINE_PER_CPU(struct vcpu_runstate_info, xen_runstate);

/* return an consistent snapshot of 64-bit time/counter value */
static u64 get64(const u64 *p)
{
	u64 ret;

	if (BITS_PER_LONG < 64) {
		u32 *p32 = (u32 *)p;
		u32 h, l, h2;

		/*
		 * Read high then low, and then make sure high is
		 * still the same; this will only loop if low wraps
		 * and carries into high.
		 * XXX some clean way to make this endian-proof?
		 */
		do {
			h = READ_ONCE(p32[1]);
			l = READ_ONCE(p32[0]);
			h2 = READ_ONCE(p32[1]);
		} while(h2 != h);

		ret = (((u64)h) << 32) | l;
	} else
		ret = READ_ONCE(*p);

	return ret;
}

static void xen_get_runstate_snapshot_cpu(struct vcpu_runstate_info *res,
					  unsigned int cpu)
{
	u64 state_time;
	struct vcpu_runstate_info *state;

	BUG_ON(preemptible());

	state = per_cpu_ptr(&xen_runstate, cpu);

	do {
		state_time = get64(&state->state_entry_time);
		rmb();	/* Hypervisor might update data. */
		*res = READ_ONCE(*state);
		rmb();	/* Hypervisor might update data. */
	} while (get64(&state->state_entry_time) != state_time ||
		 (state_time & XEN_RUNSTATE_UPDATE));
}

/*
 * Runstate accounting
 */
void xen_get_runstate_snapshot(struct vcpu_runstate_info *res)
{
	xen_get_runstate_snapshot_cpu(res, smp_processor_id());
}

/* return true when a vcpu could run but has no real cpu to run on */
bool xen_vcpu_stolen(int vcpu)
{
	return per_cpu(xen_runstate, vcpu).state == RUNSTATE_runnable;
}

u64 xen_steal_clock(int cpu)
{
	struct vcpu_runstate_info state;

	xen_get_runstate_snapshot_cpu(&state, cpu);
	return state.time[RUNSTATE_runnable] + state.time[RUNSTATE_offline];
}

void xen_setup_runstate_info(int cpu)
{
	struct vcpu_register_runstate_memory_area area;

	area.addr.v = &per_cpu(xen_runstate, cpu);

	if (HYPERVISOR_vcpu_op(VCPUOP_register_runstate_memory_area,
			       xen_vcpu_nr(cpu), &area))
		BUG();
}

void __init xen_time_setup_guest(void)
{
	bool xen_runstate_remote;

	xen_runstate_remote = !HYPERVISOR_vm_assist(VMASST_CMD_enable,
					VMASST_TYPE_runstate_update_flag);

	pv_time_ops.steal_clock = xen_steal_clock;

	static_key_slow_inc(&paravirt_steal_enabled);
	if (xen_runstate_remote)
		static_key_slow_inc(&paravirt_steal_rq_enabled);
}
