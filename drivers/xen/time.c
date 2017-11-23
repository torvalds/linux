// SPDX-License-Identifier: GPL-2.0
/*
 * Xen stolen ticks accounting.
 */
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/math64.h>
#include <linux/gfp.h>
#include <linux/slab.h>

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

static DEFINE_PER_CPU(u64[4], old_runstate_time);

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

static void xen_get_runstate_snapshot_cpu_delta(
			      struct vcpu_runstate_info *res, unsigned int cpu)
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

static void xen_get_runstate_snapshot_cpu(struct vcpu_runstate_info *res,
					  unsigned int cpu)
{
	int i;

	xen_get_runstate_snapshot_cpu_delta(res, cpu);

	for (i = 0; i < 4; i++)
		res->time[i] += per_cpu(old_runstate_time, cpu)[i];
}

void xen_manage_runstate_time(int action)
{
	static struct vcpu_runstate_info *runstate_delta;
	struct vcpu_runstate_info state;
	int cpu, i;

	switch (action) {
	case -1: /* backup runstate time before suspend */
		if (unlikely(runstate_delta))
			pr_warn_once("%s: memory leak as runstate_delta is not NULL\n",
					__func__);

		runstate_delta = kmalloc_array(num_possible_cpus(),
					sizeof(*runstate_delta),
					GFP_ATOMIC);
		if (unlikely(!runstate_delta)) {
			pr_warn("%s: failed to allocate runstate_delta\n",
					__func__);
			return;
		}

		for_each_possible_cpu(cpu) {
			xen_get_runstate_snapshot_cpu_delta(&state, cpu);
			memcpy(runstate_delta[cpu].time, state.time,
					sizeof(runstate_delta[cpu].time));
		}

		break;

	case 0: /* backup runstate time after resume */
		if (unlikely(!runstate_delta)) {
			pr_warn("%s: cannot accumulate runstate time as runstate_delta is NULL\n",
					__func__);
			return;
		}

		for_each_possible_cpu(cpu) {
			for (i = 0; i < 4; i++)
				per_cpu(old_runstate_time, cpu)[i] +=
					runstate_delta[cpu].time[i];
		}

		break;

	default: /* do not accumulate runstate time for checkpointing */
		break;
	}

	if (action != -1 && runstate_delta) {
		kfree(runstate_delta);
		runstate_delta = NULL;
	}
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
