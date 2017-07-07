/*
 * Copyright 2013, Michael (Ellerman|Neuling), IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt)	"powernv: " fmt

#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/gfp.h>
#include <linux/smp.h>
#include <linux/stop_machine.h>

#include <asm/cputhreads.h>
#include <asm/kvm_ppc.h>
#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/smp.h>

#include "subcore.h"
#include "powernv.h"


/*
 * Split/unsplit procedure:
 *
 * A core can be in one of three states, unsplit, 2-way split, and 4-way split.
 *
 * The mapping to subcores_per_core is simple:
 *
 *  State       | subcores_per_core
 *  ------------|------------------
 *  Unsplit     |        1
 *  2-way split |        2
 *  4-way split |        4
 *
 * The core is split along thread boundaries, the mapping between subcores and
 * threads is as follows:
 *
 *  Unsplit:
 *          ----------------------------
 *  Subcore |            0             |
 *          ----------------------------
 *  Thread  |  0  1  2  3  4  5  6  7  |
 *          ----------------------------
 *
 *  2-way split:
 *          -------------------------------------
 *  Subcore |        0        |        1        |
 *          -------------------------------------
 *  Thread  |  0   1   2   3  |  4   5   6   7  |
 *          -------------------------------------
 *
 *  4-way split:
 *          -----------------------------------------
 *  Subcore |    0    |    1    |    2    |    3    |
 *          -----------------------------------------
 *  Thread  |  0   1  |  2   3  |  4   5  |  6   7  |
 *          -----------------------------------------
 *
 *
 * Transitions
 * -----------
 *
 * It is not possible to transition between either of the split states, the
 * core must first be unsplit. The legal transitions are:
 *
 *  -----------          ---------------
 *  |         |  <---->  | 2-way split |
 *  |         |          ---------------
 *  | Unsplit |
 *  |         |          ---------------
 *  |         |  <---->  | 4-way split |
 *  -----------          ---------------
 *
 * Unsplitting
 * -----------
 *
 * Unsplitting is the simpler procedure. It requires thread 0 to request the
 * unsplit while all other threads NAP.
 *
 * Thread 0 clears HID0_POWER8_DYNLPARDIS (Dynamic LPAR Disable). This tells
 * the hardware that if all threads except 0 are napping, the hardware should
 * unsplit the core.
 *
 * Non-zero threads are sent to a NAP loop, they don't exit the loop until they
 * see the core unsplit.
 *
 * Core 0 spins waiting for the hardware to see all the other threads napping
 * and perform the unsplit.
 *
 * Once thread 0 sees the unsplit, it IPIs the secondary threads to wake them
 * out of NAP. They will then see the core unsplit and exit the NAP loop.
 *
 * Splitting
 * ---------
 *
 * The basic splitting procedure is fairly straight forward. However it is
 * complicated by the fact that after the split occurs, the newly created
 * subcores are not in a fully initialised state.
 *
 * Most notably the subcores do not have the correct value for SDR1, which
 * means they must not be running in virtual mode when the split occurs. The
 * subcores have separate timebases SPRs but these are pre-synchronised by
 * opal.
 *
 * To begin with secondary threads are sent to an assembly routine. There they
 * switch to real mode, so they are immune to the uninitialised SDR1 value.
 * Once in real mode they indicate that they are in real mode, and spin waiting
 * to see the core split.
 *
 * Thread 0 waits to see that all secondaries are in real mode, and then begins
 * the splitting procedure. It firstly sets HID0_POWER8_DYNLPARDIS, which
 * prevents the hardware from unsplitting. Then it sets the appropriate HID bit
 * to request the split, and spins waiting to see that the split has happened.
 *
 * Concurrently the secondaries will notice the split. When they do they set up
 * their SPRs, notably SDR1, and then they can return to virtual mode and exit
 * the procedure.
 */

/* Initialised at boot by subcore_init() */
static int subcores_per_core;

/*
 * Used to communicate to offline cpus that we want them to pop out of the
 * offline loop and do a split or unsplit.
 *
 * 0 - no split happening
 * 1 - unsplit in progress
 * 2 - split to 2 in progress
 * 4 - split to 4 in progress
 */
static int new_split_mode;

static cpumask_var_t cpu_offline_mask;

struct split_state {
	u8 step;
	u8 master;
};

static DEFINE_PER_CPU(struct split_state, split_state);

static void wait_for_sync_step(int step)
{
	int i, cpu = smp_processor_id();

	for (i = cpu + 1; i < cpu + threads_per_core; i++)
		while(per_cpu(split_state, i).step < step)
			barrier();

	/* Order the wait loop vs any subsequent loads/stores. */
	mb();
}

static void update_hid_in_slw(u64 hid0)
{
	u64 idle_states = pnv_get_supported_cpuidle_states();

	if (idle_states & OPAL_PM_WINKLE_ENABLED) {
		/* OPAL call to patch slw with the new HID0 value */
		u64 cpu_pir = hard_smp_processor_id();

		opal_slw_set_reg(cpu_pir, SPRN_HID0, hid0);
	}
}

static void unsplit_core(void)
{
	u64 hid0, mask;
	int i, cpu;

	mask = HID0_POWER8_2LPARMODE | HID0_POWER8_4LPARMODE;

	cpu = smp_processor_id();
	if (cpu_thread_in_core(cpu) != 0) {
		while (mfspr(SPRN_HID0) & mask)
			power7_nap(0);

		per_cpu(split_state, cpu).step = SYNC_STEP_UNSPLIT;
		return;
	}

	hid0 = mfspr(SPRN_HID0);
	hid0 &= ~HID0_POWER8_DYNLPARDIS;
	update_power8_hid0(hid0);
	update_hid_in_slw(hid0);

	while (mfspr(SPRN_HID0) & mask)
		cpu_relax();

	/* Wake secondaries out of NAP */
	for (i = cpu + 1; i < cpu + threads_per_core; i++)
		smp_send_reschedule(i);

	wait_for_sync_step(SYNC_STEP_UNSPLIT);
}

static void split_core(int new_mode)
{
	struct {  u64 value; u64 mask; } split_parms[2] = {
		{ HID0_POWER8_1TO2LPAR, HID0_POWER8_2LPARMODE },
		{ HID0_POWER8_1TO4LPAR, HID0_POWER8_4LPARMODE }
	};
	int i, cpu;
	u64 hid0;

	/* Convert new_mode (2 or 4) into an index into our parms array */
	i = (new_mode >> 1) - 1;
	BUG_ON(i < 0 || i > 1);

	cpu = smp_processor_id();
	if (cpu_thread_in_core(cpu) != 0) {
		split_core_secondary_loop(&per_cpu(split_state, cpu).step);
		return;
	}

	wait_for_sync_step(SYNC_STEP_REAL_MODE);

	/* Write new mode */
	hid0  = mfspr(SPRN_HID0);
	hid0 |= HID0_POWER8_DYNLPARDIS | split_parms[i].value;
	update_power8_hid0(hid0);
	update_hid_in_slw(hid0);

	/* Wait for it to happen */
	while (!(mfspr(SPRN_HID0) & split_parms[i].mask))
		cpu_relax();
}

static void cpu_do_split(int new_mode)
{
	/*
	 * At boot subcores_per_core will be 0, so we will always unsplit at
	 * boot. In the usual case where the core is already unsplit it's a
	 * nop, and this just ensures the kernel's notion of the mode is
	 * consistent with the hardware.
	 */
	if (subcores_per_core != 1)
		unsplit_core();

	if (new_mode != 1)
		split_core(new_mode);

	mb();
	per_cpu(split_state, smp_processor_id()).step = SYNC_STEP_FINISHED;
}

bool cpu_core_split_required(void)
{
	smp_rmb();

	if (!new_split_mode)
		return false;

	cpu_do_split(new_split_mode);

	return true;
}

void update_subcore_sibling_mask(void)
{
	int cpu;
	/*
	 * sibling mask for the first cpu. Left shift this by required bits
	 * to get sibling mask for the rest of the cpus.
	 */
	int sibling_mask_first_cpu =  (1 << threads_per_subcore) - 1;

	for_each_possible_cpu(cpu) {
		int tid = cpu_thread_in_core(cpu);
		int offset = (tid / threads_per_subcore) * threads_per_subcore;
		int mask = sibling_mask_first_cpu << offset;

		paca[cpu].subcore_sibling_mask = mask;

	}
}

static int cpu_update_split_mode(void *data)
{
	int cpu, new_mode = *(int *)data;

	if (this_cpu_ptr(&split_state)->master) {
		new_split_mode = new_mode;
		smp_wmb();

		cpumask_andnot(cpu_offline_mask, cpu_present_mask,
			       cpu_online_mask);

		/* This should work even though the cpu is offline */
		for_each_cpu(cpu, cpu_offline_mask)
			smp_send_reschedule(cpu);
	}

	cpu_do_split(new_mode);

	if (this_cpu_ptr(&split_state)->master) {
		/* Wait for all cpus to finish before we touch subcores_per_core */
		for_each_present_cpu(cpu) {
			if (cpu >= setup_max_cpus)
				break;

			while(per_cpu(split_state, cpu).step < SYNC_STEP_FINISHED)
				barrier();
		}

		new_split_mode = 0;

		/* Make the new mode public */
		subcores_per_core = new_mode;
		threads_per_subcore = threads_per_core / subcores_per_core;
		update_subcore_sibling_mask();

		/* Make sure the new mode is written before we exit */
		mb();
	}

	return 0;
}

static int set_subcores_per_core(int new_mode)
{
	struct split_state *state;
	int cpu;

	if (kvm_hv_mode_active()) {
		pr_err("Unable to change split core mode while KVM active.\n");
		return -EBUSY;
	}

	/*
	 * We are only called at boot, or from the sysfs write. If that ever
	 * changes we'll need a lock here.
	 */
	BUG_ON(new_mode < 1 || new_mode > 4 || new_mode == 3);

	for_each_present_cpu(cpu) {
		state = &per_cpu(split_state, cpu);
		state->step = SYNC_STEP_INITIAL;
		state->master = 0;
	}

	cpus_read_lock();

	/* This cpu will update the globals before exiting stop machine */
	this_cpu_ptr(&split_state)->master = 1;

	/* Ensure state is consistent before we call the other cpus */
	mb();

	stop_machine_cpuslocked(cpu_update_split_mode, &new_mode,
				cpu_online_mask);

	cpus_read_unlock();

	return 0;
}

static ssize_t __used store_subcores_per_core(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	unsigned long val;
	int rc;

	/* We are serialised by the attribute lock */

	rc = sscanf(buf, "%lx", &val);
	if (rc != 1)
		return -EINVAL;

	switch (val) {
	case 1:
	case 2:
	case 4:
		if (subcores_per_core == val)
			/* Nothing to do */
			goto out;
		break;
	default:
		return -EINVAL;
	}

	rc = set_subcores_per_core(val);
	if (rc)
		return rc;

out:
	return count;
}

static ssize_t show_subcores_per_core(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%x\n", subcores_per_core);
}

static DEVICE_ATTR(subcores_per_core, 0644,
		show_subcores_per_core, store_subcores_per_core);

static int subcore_init(void)
{
	unsigned pvr_ver;

	pvr_ver = PVR_VER(mfspr(SPRN_PVR));

	if (pvr_ver != PVR_POWER8 &&
	    pvr_ver != PVR_POWER8E &&
	    pvr_ver != PVR_POWER8NVL)
		return 0;

	/*
	 * We need all threads in a core to be present to split/unsplit so
         * continue only if max_cpus are aligned to threads_per_core.
	 */
	if (setup_max_cpus % threads_per_core)
		return 0;

	BUG_ON(!alloc_cpumask_var(&cpu_offline_mask, GFP_KERNEL));

	set_subcores_per_core(1);

	return device_create_file(cpu_subsys.dev_root,
				  &dev_attr_subcores_per_core);
}
machine_device_initcall(powernv, subcore_init);
