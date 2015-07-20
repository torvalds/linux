/*
 * PowerNV cpuidle code
 *
 * Copyright 2015 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/cpu.h>

#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/cputhreads.h>
#include <asm/cpuidle.h>
#include <asm/code-patching.h>
#include <asm/smp.h>

#include "powernv.h"
#include "subcore.h"

static u32 supported_cpuidle_states;

int pnv_save_sprs_for_winkle(void)
{
	int cpu;
	int rc;

	/*
	 * hid0, hid1, hid4, hid5, hmeer and lpcr values are symmetric accross
	 * all cpus at boot. Get these reg values of current cpu and use the
	 * same accross all cpus.
	 */
	uint64_t lpcr_val = mfspr(SPRN_LPCR) & ~(u64)LPCR_PECE1;
	uint64_t hid0_val = mfspr(SPRN_HID0);
	uint64_t hid1_val = mfspr(SPRN_HID1);
	uint64_t hid4_val = mfspr(SPRN_HID4);
	uint64_t hid5_val = mfspr(SPRN_HID5);
	uint64_t hmeer_val = mfspr(SPRN_HMEER);

	for_each_possible_cpu(cpu) {
		uint64_t pir = get_hard_smp_processor_id(cpu);
		uint64_t hsprg0_val = (uint64_t)&paca[cpu];

		/*
		 * HSPRG0 is used to store the cpu's pointer to paca. Hence last
		 * 3 bits are guaranteed to be 0. Program slw to restore HSPRG0
		 * with 63rd bit set, so that when a thread wakes up at 0x100 we
		 * can use this bit to distinguish between fastsleep and
		 * deep winkle.
		 */
		hsprg0_val |= 1;

		rc = opal_slw_set_reg(pir, SPRN_HSPRG0, hsprg0_val);
		if (rc != 0)
			return rc;

		rc = opal_slw_set_reg(pir, SPRN_LPCR, lpcr_val);
		if (rc != 0)
			return rc;

		/* HIDs are per core registers */
		if (cpu_thread_in_core(cpu) == 0) {

			rc = opal_slw_set_reg(pir, SPRN_HMEER, hmeer_val);
			if (rc != 0)
				return rc;

			rc = opal_slw_set_reg(pir, SPRN_HID0, hid0_val);
			if (rc != 0)
				return rc;

			rc = opal_slw_set_reg(pir, SPRN_HID1, hid1_val);
			if (rc != 0)
				return rc;

			rc = opal_slw_set_reg(pir, SPRN_HID4, hid4_val);
			if (rc != 0)
				return rc;

			rc = opal_slw_set_reg(pir, SPRN_HID5, hid5_val);
			if (rc != 0)
				return rc;
		}
	}

	return 0;
}

static void pnv_alloc_idle_core_states(void)
{
	int i, j;
	int nr_cores = cpu_nr_cores();
	u32 *core_idle_state;

	/*
	 * core_idle_state - First 8 bits track the idle state of each thread
	 * of the core. The 8th bit is the lock bit. Initially all thread bits
	 * are set. They are cleared when the thread enters deep idle state
	 * like sleep and winkle. Initially the lock bit is cleared.
	 * The lock bit has 2 purposes
	 * a. While the first thread is restoring core state, it prevents
	 * other threads in the core from switching to process context.
	 * b. While the last thread in the core is saving the core state, it
	 * prevents a different thread from waking up.
	 */
	for (i = 0; i < nr_cores; i++) {
		int first_cpu = i * threads_per_core;
		int node = cpu_to_node(first_cpu);

		core_idle_state = kmalloc_node(sizeof(u32), GFP_KERNEL, node);
		*core_idle_state = PNV_CORE_IDLE_THREAD_BITS;

		for (j = 0; j < threads_per_core; j++) {
			int cpu = first_cpu + j;

			paca[cpu].core_idle_state_ptr = core_idle_state;
			paca[cpu].thread_idle_state = PNV_THREAD_RUNNING;
			paca[cpu].thread_mask = 1 << j;
		}
	}

	update_subcore_sibling_mask();

	if (supported_cpuidle_states & OPAL_PM_WINKLE_ENABLED)
		pnv_save_sprs_for_winkle();
}

u32 pnv_get_supported_cpuidle_states(void)
{
	return supported_cpuidle_states;
}
EXPORT_SYMBOL_GPL(pnv_get_supported_cpuidle_states);


static void pnv_fastsleep_workaround_apply(void *info)

{
	int rc;
	int *err = info;

	rc = opal_config_cpu_idle_state(OPAL_CONFIG_IDLE_FASTSLEEP,
					OPAL_CONFIG_IDLE_APPLY);
	if (rc)
		*err = 1;
}

/*
 * Used to store fastsleep workaround state
 * 0 - Workaround applied/undone at fastsleep entry/exit path (Default)
 * 1 - Workaround applied once, never undone.
 */
static u8 fastsleep_workaround_applyonce;

static ssize_t show_fastsleep_workaround_applyonce(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", fastsleep_workaround_applyonce);
}

static ssize_t store_fastsleep_workaround_applyonce(struct device *dev,
		struct device_attribute *attr, const char *buf,
		size_t count)
{
	cpumask_t primary_thread_mask;
	int err;
	u8 val;

	if (kstrtou8(buf, 0, &val) || val != 1)
		return -EINVAL;

	if (fastsleep_workaround_applyonce == 1)
		return count;

	/*
	 * fastsleep_workaround_applyonce = 1 implies
	 * fastsleep workaround needs to be left in 'applied' state on all
	 * the cores. Do this by-
	 * 1. Patching out the call to 'undo' workaround in fastsleep exit path
	 * 2. Sending ipi to all the cores which have atleast one online thread
	 * 3. Patching out the call to 'apply' workaround in fastsleep entry
	 * path
	 * There is no need to send ipi to cores which have all threads
	 * offlined, as last thread of the core entering fastsleep or deeper
	 * state would have applied workaround.
	 */
	err = patch_instruction(
		(unsigned int *)pnv_fastsleep_workaround_at_exit,
		PPC_INST_NOP);
	if (err) {
		pr_err("fastsleep_workaround_applyonce change failed while patching pnv_fastsleep_workaround_at_exit");
		goto fail;
	}

	get_online_cpus();
	primary_thread_mask = cpu_online_cores_map();
	on_each_cpu_mask(&primary_thread_mask,
				pnv_fastsleep_workaround_apply,
				&err, 1);
	put_online_cpus();
	if (err) {
		pr_err("fastsleep_workaround_applyonce change failed while running pnv_fastsleep_workaround_apply");
		goto fail;
	}

	err = patch_instruction(
		(unsigned int *)pnv_fastsleep_workaround_at_entry,
		PPC_INST_NOP);
	if (err) {
		pr_err("fastsleep_workaround_applyonce change failed while patching pnv_fastsleep_workaround_at_entry");
		goto fail;
	}

	fastsleep_workaround_applyonce = 1;

	return count;
fail:
	return -EIO;
}

static DEVICE_ATTR(fastsleep_workaround_applyonce, 0600,
			show_fastsleep_workaround_applyonce,
			store_fastsleep_workaround_applyonce);

static int __init pnv_init_idle_states(void)
{
	struct device_node *power_mgt;
	int dt_idle_states;
	u32 *flags;
	int i;

	supported_cpuidle_states = 0;

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		goto out;

	if (!firmware_has_feature(FW_FEATURE_OPALv3))
		goto out;

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("opal: PowerMgmt Node not found\n");
		goto out;
	}
	dt_idle_states = of_property_count_u32_elems(power_mgt,
			"ibm,cpu-idle-state-flags");
	if (dt_idle_states < 0) {
		pr_warn("cpuidle-powernv: no idle states found in the DT\n");
		goto out;
	}

	flags = kzalloc(sizeof(*flags) * dt_idle_states, GFP_KERNEL);
	if (of_property_read_u32_array(power_mgt,
			"ibm,cpu-idle-state-flags", flags, dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-flags in DT\n");
		goto out_free;
	}

	for (i = 0; i < dt_idle_states; i++)
		supported_cpuidle_states |= flags[i];

	if (!(supported_cpuidle_states & OPAL_PM_SLEEP_ENABLED_ER1)) {
		patch_instruction(
			(unsigned int *)pnv_fastsleep_workaround_at_entry,
			PPC_INST_NOP);
		patch_instruction(
			(unsigned int *)pnv_fastsleep_workaround_at_exit,
			PPC_INST_NOP);
	} else {
		/*
		 * OPAL_PM_SLEEP_ENABLED_ER1 is set. It indicates that
		 * workaround is needed to use fastsleep. Provide sysfs
		 * control to choose how this workaround has to be applied.
		 */
		device_create_file(cpu_subsys.dev_root,
				&dev_attr_fastsleep_workaround_applyonce);
	}

	pnv_alloc_idle_core_states();
out_free:
	kfree(flags);
out:
	return 0;
}
machine_subsys_initcall(powernv, pnv_init_idle_states);
