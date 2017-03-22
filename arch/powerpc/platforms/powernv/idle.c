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

/* Power ISA 3.0 allows for stop states 0x0 - 0xF */
#define MAX_STOP_STATE	0xF

static u32 supported_cpuidle_states;

static int pnv_save_sprs_for_deep_states(void)
{
	int cpu;
	int rc;

	/*
	 * hid0, hid1, hid4, hid5, hmeer and lpcr values are symmetric across
	 * all cpus at boot. Get these reg values of current cpu and use the
	 * same across all cpus.
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

		if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
			/*
			 * HSPRG0 is used to store the cpu's pointer to paca.
			 * Hence last 3 bits are guaranteed to be 0. Program
			 * slw to restore HSPRG0 with 63rd bit set, so that
			 * when a thread wakes up at 0x100 we can use this bit
			 * to distinguish between fastsleep and deep winkle.
			 * This is not necessary with stop/psscr since PLS
			 * field of psscr indicates which state we are waking
			 * up from.
			 */
			hsprg0_val |= 1;
		}
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

	if (supported_cpuidle_states & OPAL_PM_LOSE_FULL_CONTEXT)
		pnv_save_sprs_for_deep_states();
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
	 * 2. Sending ipi to all the cores which have at least one online thread
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

/*
 * The default stop state that will be used by ppc_md.power_save
 * function on platforms that support stop instruction.
 */
u64 pnv_default_stop_val;
u64 pnv_default_stop_mask;

/*
 * Used for ppc_md.power_save which needs a function with no parameters
 */
static void power9_idle(void)
{
	power9_idle_stop(pnv_default_stop_val, pnv_default_stop_mask);
}

/*
 * First deep stop state. Used to figure out when to save/restore
 * hypervisor context.
 */
u64 pnv_first_deep_stop_state = MAX_STOP_STATE;

/*
 * psscr value and mask of the deepest stop idle state.
 * Used when a cpu is offlined.
 */
u64 pnv_deepest_stop_psscr_val;
u64 pnv_deepest_stop_psscr_mask;

/*
 * pnv_cpu_offline: A function that puts the CPU into the deepest
 * available platform idle state on a CPU-Offline.
 */
unsigned long pnv_cpu_offline(unsigned int cpu)
{
	unsigned long srr1;

	u32 idle_states = pnv_get_supported_cpuidle_states();

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		srr1 = power9_idle_stop(pnv_deepest_stop_psscr_val,
					pnv_deepest_stop_psscr_mask);
	} else if (idle_states & OPAL_PM_WINKLE_ENABLED) {
		srr1 = power7_winkle();
	} else if ((idle_states & OPAL_PM_SLEEP_ENABLED) ||
		   (idle_states & OPAL_PM_SLEEP_ENABLED_ER1)) {
		srr1 = power7_sleep();
	} else {
		srr1 = power7_nap(1);
	}

	return srr1;
}

/*
 * Power ISA 3.0 idle initialization.
 *
 * POWER ISA 3.0 defines a new SPR Processor stop Status and Control
 * Register (PSSCR) to control idle behavior.
 *
 * PSSCR layout:
 * ----------------------------------------------------------
 * | PLS | /// | SD | ESL | EC | PSLL | /// | TR | MTL | RL |
 * ----------------------------------------------------------
 * 0      4     41   42    43   44     48    54   56    60
 *
 * PSSCR key fields:
 *	Bits 0:3  - Power-Saving Level Status (PLS). This field indicates the
 *	lowest power-saving state the thread entered since stop instruction was
 *	last executed.
 *
 *	Bit 41 - Status Disable(SD)
 *	0 - Shows PLS entries
 *	1 - PLS entries are all 0
 *
 *	Bit 42 - Enable State Loss
 *	0 - No state is lost irrespective of other fields
 *	1 - Allows state loss
 *
 *	Bit 43 - Exit Criterion
 *	0 - Exit from power-save mode on any interrupt
 *	1 - Exit from power-save mode controlled by LPCR's PECE bits
 *
 *	Bits 44:47 - Power-Saving Level Limit
 *	This limits the power-saving level that can be entered into.
 *
 *	Bits 60:63 - Requested Level
 *	Used to specify which power-saving level must be entered on executing
 *	stop instruction
 */

int validate_psscr_val_mask(u64 *psscr_val, u64 *psscr_mask, u32 flags)
{
	int err = 0;

	/*
	 * psscr_mask == 0xf indicates an older firmware.
	 * Set remaining fields of psscr to the default values.
	 * See NOTE above definition of PSSCR_HV_DEFAULT_VAL
	 */
	if (*psscr_mask == 0xf) {
		*psscr_val = *psscr_val | PSSCR_HV_DEFAULT_VAL;
		*psscr_mask = PSSCR_HV_DEFAULT_MASK;
		return err;
	}

	/*
	 * New firmware is expected to set the psscr_val bits correctly.
	 * Validate that the following invariants are correctly maintained by
	 * the new firmware.
	 * - ESL bit value matches the EC bit value.
	 * - ESL bit is set for all the deep stop states.
	 */
	if (GET_PSSCR_ESL(*psscr_val) != GET_PSSCR_EC(*psscr_val)) {
		err = ERR_EC_ESL_MISMATCH;
	} else if ((flags & OPAL_PM_LOSE_FULL_CONTEXT) &&
		GET_PSSCR_ESL(*psscr_val) == 0) {
		err = ERR_DEEP_STATE_ESL_MISMATCH;
	}

	return err;
}

/*
 * pnv_arch300_idle_init: Initializes the default idle state, first
 *                        deep idle state and deepest idle state on
 *                        ISA 3.0 CPUs.
 *
 * @np: /ibm,opal/power-mgt device node
 * @flags: cpu-idle-state-flags array
 * @dt_idle_states: Number of idle state entries
 * Returns 0 on success
 */
static int __init pnv_power9_idle_init(struct device_node *np, u32 *flags,
					int dt_idle_states)
{
	u64 *psscr_val = NULL;
	u64 *psscr_mask = NULL;
	u32 *residency_ns = NULL;
	u64 max_residency_ns = 0;
	int rc = 0, i;
	bool default_stop_found = false, deepest_stop_found = false;

	psscr_val = kcalloc(dt_idle_states, sizeof(*psscr_val), GFP_KERNEL);
	psscr_mask = kcalloc(dt_idle_states, sizeof(*psscr_mask), GFP_KERNEL);
	residency_ns = kcalloc(dt_idle_states, sizeof(*residency_ns),
			       GFP_KERNEL);

	if (!psscr_val || !psscr_mask || !residency_ns) {
		rc = -1;
		goto out;
	}

	if (of_property_read_u64_array(np,
		"ibm,cpu-idle-state-psscr",
		psscr_val, dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-psscr in DT\n");
		rc = -1;
		goto out;
	}

	if (of_property_read_u64_array(np,
				       "ibm,cpu-idle-state-psscr-mask",
				       psscr_mask, dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-psscr-mask in DT\n");
		rc = -1;
		goto out;
	}

	if (of_property_read_u32_array(np,
				       "ibm,cpu-idle-state-residency-ns",
					residency_ns, dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-residency-ns in DT\n");
		rc = -1;
		goto out;
	}

	/*
	 * Set pnv_first_deep_stop_state, pnv_deepest_stop_psscr_{val,mask},
	 * and the pnv_default_stop_{val,mask}.
	 *
	 * pnv_first_deep_stop_state should be set to the first stop
	 * level to cause hypervisor state loss.
	 *
	 * pnv_deepest_stop_{val,mask} should be set to values corresponding to
	 * the deepest stop state.
	 *
	 * pnv_default_stop_{val,mask} should be set to values corresponding to
	 * the shallowest (OPAL_PM_STOP_INST_FAST) loss-less stop state.
	 */
	pnv_first_deep_stop_state = MAX_STOP_STATE;
	for (i = 0; i < dt_idle_states; i++) {
		int err;
		u64 psscr_rl = psscr_val[i] & PSSCR_RL_MASK;

		if ((flags[i] & OPAL_PM_LOSE_FULL_CONTEXT) &&
		     (pnv_first_deep_stop_state > psscr_rl))
			pnv_first_deep_stop_state = psscr_rl;

		err = validate_psscr_val_mask(&psscr_val[i], &psscr_mask[i],
					      flags[i]);
		if (err) {
			report_invalid_psscr_val(psscr_val[i], err);
			continue;
		}

		if (max_residency_ns < residency_ns[i]) {
			max_residency_ns = residency_ns[i];
			pnv_deepest_stop_psscr_val = psscr_val[i];
			pnv_deepest_stop_psscr_mask = psscr_mask[i];
			deepest_stop_found = true;
		}

		if (!default_stop_found &&
		    (flags[i] & OPAL_PM_STOP_INST_FAST)) {
			pnv_default_stop_val = psscr_val[i];
			pnv_default_stop_mask = psscr_mask[i];
			default_stop_found = true;
		}
	}

	if (!default_stop_found) {
		pnv_default_stop_val = PSSCR_HV_DEFAULT_VAL;
		pnv_default_stop_mask = PSSCR_HV_DEFAULT_MASK;
		pr_warn("Setting default stop psscr val=0x%016llx,mask=0x%016llx\n",
			pnv_default_stop_val, pnv_default_stop_mask);
	}

	if (!deepest_stop_found) {
		pnv_deepest_stop_psscr_val = PSSCR_HV_DEFAULT_VAL;
		pnv_deepest_stop_psscr_mask = PSSCR_HV_DEFAULT_MASK;
		pr_warn("Setting default stop psscr val=0x%016llx,mask=0x%016llx\n",
			pnv_deepest_stop_psscr_val,
			pnv_deepest_stop_psscr_mask);
	}

out:
	kfree(psscr_val);
	kfree(psscr_mask);
	kfree(residency_ns);
	return rc;
}

/*
 * Probe device tree for supported idle states
 */
static void __init pnv_probe_idle_states(void)
{
	struct device_node *np;
	int dt_idle_states;
	u32 *flags = NULL;
	int i;

	np = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!np) {
		pr_warn("opal: PowerMgmt Node not found\n");
		goto out;
	}
	dt_idle_states = of_property_count_u32_elems(np,
			"ibm,cpu-idle-state-flags");
	if (dt_idle_states < 0) {
		pr_warn("cpuidle-powernv: no idle states found in the DT\n");
		goto out;
	}

	flags = kcalloc(dt_idle_states, sizeof(*flags),  GFP_KERNEL);

	if (of_property_read_u32_array(np,
			"ibm,cpu-idle-state-flags", flags, dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-flags in DT\n");
		goto out;
	}

	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (pnv_power9_idle_init(np, flags, dt_idle_states))
			goto out;
	}

	for (i = 0; i < dt_idle_states; i++)
		supported_cpuidle_states |= flags[i];

out:
	kfree(flags);
}
static int __init pnv_init_idle_states(void)
{

	supported_cpuidle_states = 0;

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		goto out;

	pnv_probe_idle_states();

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

	if (supported_cpuidle_states & OPAL_PM_NAP_ENABLED)
		ppc_md.power_save = power7_idle;
	else if (supported_cpuidle_states & OPAL_PM_STOP_INST_FAST)
		ppc_md.power_save = power9_idle;

out:
	return 0;
}
machine_subsys_initcall(powernv, pnv_init_idle_states);
