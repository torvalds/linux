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
#include <asm/runlatch.h>

#include "powernv.h"
#include "subcore.h"

/* Power ISA 3.0 allows for stop states 0x0 - 0xF */
#define MAX_STOP_STATE	0xF

#define P9_STOP_SPR_MSR 2000
#define P9_STOP_SPR_PSSCR      855

static u32 supported_cpuidle_states;

/*
 * The default stop state that will be used by ppc_md.power_save
 * function on platforms that support stop instruction.
 */
static u64 pnv_default_stop_val;
static u64 pnv_default_stop_mask;
static bool default_stop_found;

/*
 * First deep stop state. Used to figure out when to save/restore
 * hypervisor context.
 */
u64 pnv_first_deep_stop_state = MAX_STOP_STATE;

/*
 * psscr value and mask of the deepest stop idle state.
 * Used when a cpu is offlined.
 */
static u64 pnv_deepest_stop_psscr_val;
static u64 pnv_deepest_stop_psscr_mask;
static u64 pnv_deepest_stop_flag;
static bool deepest_stop_found;

static int pnv_save_sprs_for_deep_states(void)
{
	int cpu;
	int rc;

	/*
	 * hid0, hid1, hid4, hid5, hmeer and lpcr values are symmetric across
	 * all cpus at boot. Get these reg values of current cpu and use the
	 * same across all cpus.
	 */
	uint64_t lpcr_val = mfspr(SPRN_LPCR);
	uint64_t hid0_val = mfspr(SPRN_HID0);
	uint64_t hid1_val = mfspr(SPRN_HID1);
	uint64_t hid4_val = mfspr(SPRN_HID4);
	uint64_t hid5_val = mfspr(SPRN_HID5);
	uint64_t hmeer_val = mfspr(SPRN_HMEER);
	uint64_t msr_val = MSR_IDLE;
	uint64_t psscr_val = pnv_deepest_stop_psscr_val;

	for_each_possible_cpu(cpu) {
		uint64_t pir = get_hard_smp_processor_id(cpu);
		uint64_t hsprg0_val = (uint64_t)paca_ptrs[cpu];

		rc = opal_slw_set_reg(pir, SPRN_HSPRG0, hsprg0_val);
		if (rc != 0)
			return rc;

		rc = opal_slw_set_reg(pir, SPRN_LPCR, lpcr_val);
		if (rc != 0)
			return rc;

		if (cpu_has_feature(CPU_FTR_ARCH_300)) {
			rc = opal_slw_set_reg(pir, P9_STOP_SPR_MSR, msr_val);
			if (rc)
				return rc;

			rc = opal_slw_set_reg(pir,
					      P9_STOP_SPR_PSSCR, psscr_val);

			if (rc)
				return rc;
		}

		/* HIDs are per core registers */
		if (cpu_thread_in_core(cpu) == 0) {

			rc = opal_slw_set_reg(pir, SPRN_HMEER, hmeer_val);
			if (rc != 0)
				return rc;

			rc = opal_slw_set_reg(pir, SPRN_HID0, hid0_val);
			if (rc != 0)
				return rc;

			/* Only p8 needs to set extra HID regiters */
			if (!cpu_has_feature(CPU_FTR_ARCH_300)) {

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
	}

	return 0;
}

static void pnv_alloc_idle_core_states(void)
{
	int i, j;
	int nr_cores = cpu_nr_cores();
	u32 *core_idle_state;

	/*
	 * core_idle_state - The lower 8 bits track the idle state of
	 * each thread of the core.
	 *
	 * The most significant bit is the lock bit.
	 *
	 * Initially all the bits corresponding to threads_per_core
	 * are set. They are cleared when the thread enters deep idle
	 * state like sleep and winkle/stop.
	 *
	 * Initially the lock bit is cleared.  The lock bit has 2
	 * purposes:
	 * 	a. While the first thread in the core waking up from
	 * 	   idle is restoring core state, it prevents other
	 * 	   threads in the core from switching to process
	 * 	   context.
	 * 	b. While the last thread in the core is saving the
	 *	   core state, it prevents a different thread from
	 *	   waking up.
	 */
	for (i = 0; i < nr_cores; i++) {
		int first_cpu = i * threads_per_core;
		int node = cpu_to_node(first_cpu);
		size_t paca_ptr_array_size;

		core_idle_state = kmalloc_node(sizeof(u32), GFP_KERNEL, node);
		*core_idle_state = (1 << threads_per_core) - 1;
		paca_ptr_array_size = (threads_per_core *
				       sizeof(struct paca_struct *));

		for (j = 0; j < threads_per_core; j++) {
			int cpu = first_cpu + j;

			paca_ptrs[cpu]->core_idle_state_ptr = core_idle_state;
			paca_ptrs[cpu]->thread_idle_state = PNV_THREAD_RUNNING;
			paca_ptrs[cpu]->thread_mask = 1 << j;
			if (!cpu_has_feature(CPU_FTR_POWER9_DD1))
				continue;
			paca_ptrs[cpu]->thread_sibling_pacas =
				kmalloc_node(paca_ptr_array_size,
					     GFP_KERNEL, node);
		}
	}

	update_subcore_sibling_mask();

	if (supported_cpuidle_states & OPAL_PM_LOSE_FULL_CONTEXT) {
		int rc = pnv_save_sprs_for_deep_states();

		if (likely(!rc))
			return;

		/*
		 * The stop-api is unable to restore hypervisor
		 * resources on wakeup from platform idle states which
		 * lose full context. So disable such states.
		 */
		supported_cpuidle_states &= ~OPAL_PM_LOSE_FULL_CONTEXT;
		pr_warn("cpuidle-powernv: Disabling idle states that lose full context\n");
		pr_warn("cpuidle-powernv: Idle power-savings, CPU-Hotplug affected\n");

		if (cpu_has_feature(CPU_FTR_ARCH_300) &&
		    (pnv_deepest_stop_flag & OPAL_PM_LOSE_FULL_CONTEXT)) {
			/*
			 * Use the default stop state for CPU-Hotplug
			 * if available.
			 */
			if (default_stop_found) {
				pnv_deepest_stop_psscr_val =
					pnv_default_stop_val;
				pnv_deepest_stop_psscr_mask =
					pnv_default_stop_mask;
				pr_warn("cpuidle-powernv: Offlined CPUs will stop with psscr = 0x%016llx\n",
					pnv_deepest_stop_psscr_val);
			} else { /* Fallback to snooze loop for CPU-Hotplug */
				deepest_stop_found = false;
				pr_warn("cpuidle-powernv: Offlined CPUs will busy wait\n");
			}
		}
	}
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

static unsigned long __power7_idle_type(unsigned long type)
{
	unsigned long srr1;

	if (!prep_irq_for_idle_irqsoff())
		return 0;

	__ppc64_runlatch_off();
	srr1 = power7_idle_insn(type);
	__ppc64_runlatch_on();

	fini_irq_for_idle_irqsoff();

	return srr1;
}

void power7_idle_type(unsigned long type)
{
	unsigned long srr1;

	srr1 = __power7_idle_type(type);
	irq_set_pending_from_srr1(srr1);
}

void power7_idle(void)
{
	if (!powersave_nap)
		return;

	power7_idle_type(PNV_THREAD_NAP);
}

static unsigned long __power9_idle_type(unsigned long stop_psscr_val,
				      unsigned long stop_psscr_mask)
{
	unsigned long psscr;
	unsigned long srr1;

	if (!prep_irq_for_idle_irqsoff())
		return 0;

	psscr = mfspr(SPRN_PSSCR);
	psscr = (psscr & ~stop_psscr_mask) | stop_psscr_val;

	__ppc64_runlatch_off();
	srr1 = power9_idle_stop(psscr);
	__ppc64_runlatch_on();

	fini_irq_for_idle_irqsoff();

	return srr1;
}

void power9_idle_type(unsigned long stop_psscr_val,
				      unsigned long stop_psscr_mask)
{
	unsigned long srr1;

	srr1 = __power9_idle_type(stop_psscr_val, stop_psscr_mask);
	irq_set_pending_from_srr1(srr1);
}

/*
 * Used for ppc_md.power_save which needs a function with no parameters
 */
void power9_idle(void)
{
	power9_idle_type(pnv_default_stop_val, pnv_default_stop_mask);
}

#ifdef CONFIG_HOTPLUG_CPU
static void pnv_program_cpu_hotplug_lpcr(unsigned int cpu, u64 lpcr_val)
{
	u64 pir = get_hard_smp_processor_id(cpu);

	mtspr(SPRN_LPCR, lpcr_val);

	/*
	 * Program the LPCR via stop-api only if the deepest stop state
	 * can lose hypervisor context.
	 */
	if (supported_cpuidle_states & OPAL_PM_LOSE_FULL_CONTEXT)
		opal_slw_set_reg(pir, SPRN_LPCR, lpcr_val);
}

/*
 * pnv_cpu_offline: A function that puts the CPU into the deepest
 * available platform idle state on a CPU-Offline.
 * interrupts hard disabled and no lazy irq pending.
 */
unsigned long pnv_cpu_offline(unsigned int cpu)
{
	unsigned long srr1;
	u32 idle_states = pnv_get_supported_cpuidle_states();
	u64 lpcr_val;

	/*
	 * We don't want to take decrementer interrupts while we are
	 * offline, so clear LPCR:PECE1. We keep PECE2 (and
	 * LPCR_PECE_HVEE on P9) enabled as to let IPIs in.
	 *
	 * If the CPU gets woken up by a special wakeup, ensure that
	 * the SLW engine sets LPCR with decrementer bit cleared, else
	 * the CPU will come back to the kernel due to a spurious
	 * wakeup.
	 */
	lpcr_val = mfspr(SPRN_LPCR) & ~(u64)LPCR_PECE1;
	pnv_program_cpu_hotplug_lpcr(cpu, lpcr_val);

	__ppc64_runlatch_off();

	if (cpu_has_feature(CPU_FTR_ARCH_300) && deepest_stop_found) {
		unsigned long psscr;

		psscr = mfspr(SPRN_PSSCR);
		psscr = (psscr & ~pnv_deepest_stop_psscr_mask) |
						pnv_deepest_stop_psscr_val;
		srr1 = power9_idle_stop(psscr);

	} else if ((idle_states & OPAL_PM_WINKLE_ENABLED) &&
		   (idle_states & OPAL_PM_LOSE_FULL_CONTEXT)) {
		srr1 = power7_idle_insn(PNV_THREAD_WINKLE);
	} else if ((idle_states & OPAL_PM_SLEEP_ENABLED) ||
		   (idle_states & OPAL_PM_SLEEP_ENABLED_ER1)) {
		srr1 = power7_idle_insn(PNV_THREAD_SLEEP);
	} else if (idle_states & OPAL_PM_NAP_ENABLED) {
		srr1 = power7_idle_insn(PNV_THREAD_NAP);
	} else {
		/* This is the fallback method. We emulate snooze */
		while (!generic_check_cpu_restart(cpu)) {
			HMT_low();
			HMT_very_low();
		}
		srr1 = 0;
		HMT_medium();
	}

	__ppc64_runlatch_on();

	/*
	 * Re-enable decrementer interrupts in LPCR.
	 *
	 * Further, we want stop states to be woken up by decrementer
	 * for non-hotplug cases. So program the LPCR via stop api as
	 * well.
	 */
	lpcr_val = mfspr(SPRN_LPCR) | (u64)LPCR_PECE1;
	pnv_program_cpu_hotplug_lpcr(cpu, lpcr_val);

	return srr1;
}
#endif

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
			pnv_deepest_stop_flag = flags[i];
			deepest_stop_found = true;
		}

		if (!default_stop_found &&
		    (flags[i] & OPAL_PM_STOP_INST_FAST)) {
			pnv_default_stop_val = psscr_val[i];
			pnv_default_stop_mask = psscr_mask[i];
			default_stop_found = true;
		}
	}

	if (unlikely(!default_stop_found)) {
		pr_warn("cpuidle-powernv: No suitable default stop state found. Disabling platform idle.\n");
	} else {
		ppc_md.power_save = power9_idle;
		pr_info("cpuidle-powernv: Default stop: psscr = 0x%016llx,mask=0x%016llx\n",
			pnv_default_stop_val, pnv_default_stop_mask);
	}

	if (unlikely(!deepest_stop_found)) {
		pr_warn("cpuidle-powernv: No suitable stop state for CPU-Hotplug. Offlined CPUs will busy wait");
	} else {
		pr_info("cpuidle-powernv: Deepest stop: psscr = 0x%016llx,mask=0x%016llx\n",
			pnv_deepest_stop_psscr_val,
			pnv_deepest_stop_psscr_mask);
	}

	pr_info("cpuidle-powernv: Requested Level (RL) value of first deep stop = 0x%llx\n",
		pnv_first_deep_stop_state);
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

	/*
	 * For each CPU, record its PACA address in each of it's
	 * sibling thread's PACA at the slot corresponding to this
	 * CPU's index in the core.
	 */
	if (cpu_has_feature(CPU_FTR_POWER9_DD1)) {
		int cpu;

		pr_info("powernv: idle: Saving PACA pointers of all CPUs in their thread sibling PACA\n");
		for_each_possible_cpu(cpu) {
			int base_cpu = cpu_first_thread_sibling(cpu);
			int idx = cpu_thread_in_core(cpu);
			int i;

			for (i = 0; i < threads_per_core; i++) {
				int j = base_cpu + i;

				paca_ptrs[j]->thread_sibling_pacas[idx] =
					paca_ptrs[cpu];
			}
		}
	}

	if (supported_cpuidle_states & OPAL_PM_NAP_ENABLED)
		ppc_md.power_save = power7_idle;

out:
	return 0;
}
machine_subsys_initcall(powernv, pnv_init_idle_states);
