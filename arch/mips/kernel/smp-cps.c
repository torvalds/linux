/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/sched.h>
#include <linux/sched/hotplug.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/bcache.h>
#include <asm/mips-cm.h>
#include <asm/mips-cpc.h>
#include <asm/mips_mt.h>
#include <asm/mipsregs.h>
#include <asm/pm-cps.h>
#include <asm/r4kcache.h>
#include <asm/smp-cps.h>
#include <asm/time.h>
#include <asm/uasm.h>

static bool threads_disabled;
static DECLARE_BITMAP(core_power, NR_CPUS);

struct core_boot_config *mips_cps_core_bootcfg;

static int __init setup_nothreads(char *s)
{
	threads_disabled = true;
	return 0;
}
early_param("nothreads", setup_nothreads);

static unsigned core_vpe_count(unsigned core)
{
	unsigned cfg;

	if (threads_disabled)
		return 1;

	if ((!IS_ENABLED(CONFIG_MIPS_MT_SMP) || !cpu_has_mipsmt)
		&& (!IS_ENABLED(CONFIG_CPU_MIPSR6) || !cpu_has_vp))
		return 1;

	mips_cm_lock_other(core, 0);
	cfg = read_gcr_co_config() & CM_GCR_Cx_CONFIG_PVPE_MSK;
	mips_cm_unlock_other();
	return (cfg >> CM_GCR_Cx_CONFIG_PVPE_SHF) + 1;
}

static void __init cps_smp_setup(void)
{
	unsigned int ncores, nvpes, core_vpes;
	unsigned long core_entry;
	int c, v;

	/* Detect & record VPE topology */
	ncores = mips_cm_numcores();
	pr_info("%s topology ", cpu_has_mips_r6 ? "VP" : "VPE");
	for (c = nvpes = 0; c < ncores; c++) {
		core_vpes = core_vpe_count(c);
		pr_cont("%c%u", c ? ',' : '{', core_vpes);

		/* Use the number of VPEs in core 0 for smp_num_siblings */
		if (!c)
			smp_num_siblings = core_vpes;

		for (v = 0; v < min_t(int, core_vpes, NR_CPUS - nvpes); v++) {
			cpu_data[nvpes + v].core = c;
#if defined(CONFIG_MIPS_MT_SMP) || defined(CONFIG_CPU_MIPSR6)
			cpu_data[nvpes + v].vpe_id = v;
#endif
		}

		nvpes += core_vpes;
	}
	pr_cont("} total %u\n", nvpes);

	/* Indicate present CPUs (CPU being synonymous with VPE) */
	for (v = 0; v < min_t(unsigned, nvpes, NR_CPUS); v++) {
		set_cpu_possible(v, true);
		set_cpu_present(v, true);
		__cpu_number_map[v] = v;
		__cpu_logical_map[v] = v;
	}

	/* Set a coherent default CCA (CWB) */
	change_c0_config(CONF_CM_CMASK, 0x5);

	/* Core 0 is powered up (we're running on it) */
	bitmap_set(core_power, 0, 1);

	/* Initialise core 0 */
	mips_cps_core_init();

	/* Make core 0 coherent with everything */
	write_gcr_cl_coherence(0xff);

	if (mips_cm_revision() >= CM_REV_CM3) {
		core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);
		write_gcr_bev_base(core_entry);
	}

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(0, &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	unsigned ncores, core_vpes, c, cca;
	bool cca_unsuitable;
	u32 *entry_code;

	mips_mt_set_cpuoptions();

	/* Detect whether the CCA is unsuited to multi-core SMP */
	cca = read_c0_config() & CONF_CM_CMASK;
	switch (cca) {
	case 0x4: /* CWBE */
	case 0x5: /* CWB */
		/* The CCA is coherent, multi-core is fine */
		cca_unsuitable = false;
		break;

	default:
		/* CCA is not coherent, multi-core is not usable */
		cca_unsuitable = true;
	}

	/* Warn the user if the CCA prevents multi-core */
	ncores = mips_cm_numcores();
	if (cca_unsuitable && ncores > 1) {
		pr_warn("Using only one core due to unsuitable CCA 0x%x\n",
			cca);

		for_each_present_cpu(c) {
			if (cpu_data[c].core)
				set_cpu_present(c, false);
		}
	}

	/*
	 * Patch the start of mips_cps_core_entry to provide:
	 *
	 * s0 = kseg0 CCA
	 */
	entry_code = (u32 *)&mips_cps_core_entry;
	uasm_i_addiu(&entry_code, 16, 0, cca);
	blast_dcache_range((unsigned long)&mips_cps_core_entry,
			   (unsigned long)entry_code);
	bc_wback_inv((unsigned long)&mips_cps_core_entry,
		     (void *)entry_code - (void *)&mips_cps_core_entry);
	__sync();

	/* Allocate core boot configuration structs */
	mips_cps_core_bootcfg = kcalloc(ncores, sizeof(*mips_cps_core_bootcfg),
					GFP_KERNEL);
	if (!mips_cps_core_bootcfg) {
		pr_err("Failed to allocate boot config for %u cores\n", ncores);
		goto err_out;
	}

	/* Allocate VPE boot configuration structs */
	for (c = 0; c < ncores; c++) {
		core_vpes = core_vpe_count(c);
		mips_cps_core_bootcfg[c].vpe_config = kcalloc(core_vpes,
				sizeof(*mips_cps_core_bootcfg[c].vpe_config),
				GFP_KERNEL);
		if (!mips_cps_core_bootcfg[c].vpe_config) {
			pr_err("Failed to allocate %u VPE boot configs\n",
			       core_vpes);
			goto err_out;
		}
	}

	/* Mark this CPU as booted */
	atomic_set(&mips_cps_core_bootcfg[current_cpu_data.core].vpe_mask,
		   1 << cpu_vpe_id(&current_cpu_data));

	return;
err_out:
	/* Clean up allocations */
	if (mips_cps_core_bootcfg) {
		for (c = 0; c < ncores; c++)
			kfree(mips_cps_core_bootcfg[c].vpe_config);
		kfree(mips_cps_core_bootcfg);
		mips_cps_core_bootcfg = NULL;
	}

	/* Effectively disable SMP by declaring CPUs not present */
	for_each_possible_cpu(c) {
		if (c == 0)
			continue;
		set_cpu_present(c, false);
	}
}

static void boot_core(unsigned int core, unsigned int vpe_id)
{
	u32 access, stat, seq_state;
	unsigned timeout;

	/* Select the appropriate core */
	mips_cm_lock_other(core, 0);

	/* Set its reset vector */
	write_gcr_co_reset_base(CKSEG1ADDR((unsigned long)mips_cps_core_entry));

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Start it with the legacy memory map and exception base */
	write_gcr_co_reset_ext_base(CM_GCR_RESET_EXT_BASE_UEB);

	/* Ensure the core can access the GCRs */
	access = read_gcr_access();
	access |= 1 << (CM_GCR_ACCESS_ACCESSEN_SHF + core);
	write_gcr_access(access);

	if (mips_cpc_present()) {
		/* Reset the core */
		mips_cpc_lock_other(core);

		if (mips_cm_revision() >= CM_REV_CM3) {
			/* Run only the requested VP following the reset */
			write_cpc_co_vp_stop(0xf);
			write_cpc_co_vp_run(1 << vpe_id);

			/*
			 * Ensure that the VP_RUN register is written before the
			 * core leaves reset.
			 */
			wmb();
		}

		write_cpc_co_cmd(CPC_Cx_CMD_RESET);

		timeout = 100;
		while (true) {
			stat = read_cpc_co_stat_conf();
			seq_state = stat & CPC_Cx_STAT_CONF_SEQSTATE_MSK;

			/* U6 == coherent execution, ie. the core is up */
			if (seq_state == CPC_Cx_STAT_CONF_SEQSTATE_U6)
				break;

			/* Delay a little while before we start warning */
			if (timeout) {
				timeout--;
				mdelay(10);
				continue;
			}

			pr_warn("Waiting for core %u to start... STAT_CONF=0x%x\n",
				core, stat);
			mdelay(1000);
		}

		mips_cpc_unlock_other();
	} else {
		/* Take the core out of reset */
		write_gcr_co_reset_release(0);
	}

	mips_cm_unlock_other();

	/* The core is now powered up */
	bitmap_set(core_power, core, 1);
}

static void remote_vpe_boot(void *dummy)
{
	unsigned core = current_cpu_data.core;
	struct core_boot_config *core_cfg = &mips_cps_core_bootcfg[core];

	mips_cps_boot_vpes(core_cfg, cpu_vpe_id(&current_cpu_data));
}

static void cps_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned core = cpu_data[cpu].core;
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	struct core_boot_config *core_cfg = &mips_cps_core_bootcfg[core];
	struct vpe_boot_config *vpe_cfg = &core_cfg->vpe_config[vpe_id];
	unsigned long core_entry;
	unsigned int remote;
	int err;

	vpe_cfg->pc = (unsigned long)&smp_bootstrap;
	vpe_cfg->sp = __KSTK_TOS(idle);
	vpe_cfg->gp = (unsigned long)task_thread_info(idle);

	atomic_or(1 << cpu_vpe_id(&cpu_data[cpu]), &core_cfg->vpe_mask);

	preempt_disable();

	if (!test_bit(core, core_power)) {
		/* Boot a VPE on a powered down core */
		boot_core(core, vpe_id);
		goto out;
	}

	if (cpu_has_vp) {
		mips_cm_lock_other(core, vpe_id);
		core_entry = CKSEG1ADDR((unsigned long)mips_cps_core_entry);
		write_gcr_co_reset_base(core_entry);
		mips_cm_unlock_other();
	}

	if (core != current_cpu_data.core) {
		/* Boot a VPE on another powered up core */
		for (remote = 0; remote < NR_CPUS; remote++) {
			if (cpu_data[remote].core != core)
				continue;
			if (cpu_online(remote))
				break;
		}
		if (remote >= NR_CPUS) {
			pr_crit("No online CPU in core %u to start CPU%d\n",
				core, cpu);
			goto out;
		}

		err = smp_call_function_single(remote, remote_vpe_boot,
					       NULL, 1);
		if (err)
			panic("Failed to call remote CPU\n");
		goto out;
	}

	BUG_ON(!cpu_has_mipsmt && !cpu_has_vp);

	/* Boot a VPE on this core */
	mips_cps_boot_vpes(core_cfg, vpe_id);
out:
	preempt_enable();
}

static void cps_init_secondary(void)
{
	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	if (mips_cm_revision() >= CM_REV_CM3) {
		unsigned ident = gic_read_local_vp_id();

		/*
		 * Ensure that our calculation of the VP ID matches up with
		 * what the GIC reports, otherwise we'll have configured
		 * interrupts incorrectly.
		 */
		BUG_ON(ident != mips_cm_vp_id(smp_processor_id()));
	}

	if (cpu_has_veic)
		clear_c0_status(ST0_IM);
	else
		change_c0_status(ST0_IM, STATUSF_IP2 | STATUSF_IP3 |
					 STATUSF_IP4 | STATUSF_IP5 |
					 STATUSF_IP6 | STATUSF_IP7);
}

static void cps_smp_finish(void)
{
	write_c0_compare(read_c0_count() + (8 * mips_hpt_frequency / HZ));

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(smp_processor_id(), &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */

	local_irq_enable();
}

#ifdef CONFIG_HOTPLUG_CPU

static int cps_cpu_disable(void)
{
	unsigned cpu = smp_processor_id();
	struct core_boot_config *core_cfg;

	if (!cpu)
		return -EBUSY;

	if (!cps_pm_support_state(CPS_PM_POWER_GATED))
		return -EINVAL;

	core_cfg = &mips_cps_core_bootcfg[current_cpu_data.core];
	atomic_sub(1 << cpu_vpe_id(&current_cpu_data), &core_cfg->vpe_mask);
	smp_mb__after_atomic();
	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();

	return 0;
}

static DECLARE_COMPLETION(cpu_death_chosen);
static unsigned cpu_death_sibling;
static enum {
	CPU_DEATH_HALT,
	CPU_DEATH_POWER,
} cpu_death;

void play_dead(void)
{
	unsigned int cpu, core, vpe_id;

	local_irq_disable();
	idle_task_exit();
	cpu = smp_processor_id();
	cpu_death = CPU_DEATH_POWER;

	pr_debug("CPU%d going offline\n", cpu);

	if (cpu_has_mipsmt || cpu_has_vp) {
		core = cpu_data[cpu].core;

		/* Look for another online VPE within the core */
		for_each_online_cpu(cpu_death_sibling) {
			if (cpu_data[cpu_death_sibling].core != core)
				continue;

			/*
			 * There is an online VPE within the core. Just halt
			 * this TC and leave the core alone.
			 */
			cpu_death = CPU_DEATH_HALT;
			break;
		}
	}

	/* This CPU has chosen its way out */
	complete(&cpu_death_chosen);

	if (cpu_death == CPU_DEATH_HALT) {
		vpe_id = cpu_vpe_id(&cpu_data[cpu]);

		pr_debug("Halting core %d VP%d\n", core, vpe_id);
		if (cpu_has_mipsmt) {
			/* Halt this TC */
			write_c0_tchalt(TCHALT_H);
			instruction_hazard();
		} else if (cpu_has_vp) {
			write_cpc_cl_vp_stop(1 << vpe_id);

			/* Ensure that the VP_STOP register is written */
			wmb();
		}
	} else {
		pr_debug("Gating power to core %d\n", core);
		/* Power down the core */
		cps_pm_enter_state(CPS_PM_POWER_GATED);
	}

	/* This should never be reached */
	panic("Failed to offline CPU %u", cpu);
}

static void wait_for_sibling_halt(void *ptr_cpu)
{
	unsigned cpu = (unsigned long)ptr_cpu;
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	unsigned halted;
	unsigned long flags;

	do {
		local_irq_save(flags);
		settc(vpe_id);
		halted = read_tc_c0_tchalt();
		local_irq_restore(flags);
	} while (!(halted & TCHALT_H));
}

static void cps_cpu_die(unsigned int cpu)
{
	unsigned core = cpu_data[cpu].core;
	unsigned int vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	unsigned stat;
	int err;

	/* Wait for the cpu to choose its way out */
	if (!wait_for_completion_timeout(&cpu_death_chosen,
					 msecs_to_jiffies(5000))) {
		pr_err("CPU%u: didn't offline\n", cpu);
		return;
	}

	/*
	 * Now wait for the CPU to actually offline. Without doing this that
	 * offlining may race with one or more of:
	 *
	 *   - Onlining the CPU again.
	 *   - Powering down the core if another VPE within it is offlined.
	 *   - A sibling VPE entering a non-coherent state.
	 *
	 * In the non-MT halt case (ie. infinite loop) the CPU is doing nothing
	 * with which we could race, so do nothing.
	 */
	if (cpu_death == CPU_DEATH_POWER) {
		/*
		 * Wait for the core to enter a powered down or clock gated
		 * state, the latter happening when a JTAG probe is connected
		 * in which case the CPC will refuse to power down the core.
		 */
		do {
			mips_cm_lock_other(core, 0);
			mips_cpc_lock_other(core);
			stat = read_cpc_co_stat_conf();
			stat &= CPC_Cx_STAT_CONF_SEQSTATE_MSK;
			mips_cpc_unlock_other();
			mips_cm_unlock_other();
		} while (stat != CPC_Cx_STAT_CONF_SEQSTATE_D0 &&
			 stat != CPC_Cx_STAT_CONF_SEQSTATE_D2 &&
			 stat != CPC_Cx_STAT_CONF_SEQSTATE_U2);

		/* Indicate the core is powered off */
		bitmap_clear(core_power, core, 1);
	} else if (cpu_has_mipsmt) {
		/*
		 * Have a CPU with access to the offlined CPUs registers wait
		 * for its TC to halt.
		 */
		err = smp_call_function_single(cpu_death_sibling,
					       wait_for_sibling_halt,
					       (void *)(unsigned long)cpu, 1);
		if (err)
			panic("Failed to call remote sibling CPU\n");
	} else if (cpu_has_vp) {
		do {
			mips_cm_lock_other(core, vpe_id);
			stat = read_cpc_co_vp_running();
			mips_cm_unlock_other();
		} while (stat & (1 << vpe_id));
	}
}

#endif /* CONFIG_HOTPLUG_CPU */

static struct plat_smp_ops cps_smp_ops = {
	.smp_setup		= cps_smp_setup,
	.prepare_cpus		= cps_prepare_cpus,
	.boot_secondary		= cps_boot_secondary,
	.init_secondary		= cps_init_secondary,
	.smp_finish		= cps_smp_finish,
	.send_ipi_single	= mips_smp_send_ipi_single,
	.send_ipi_mask		= mips_smp_send_ipi_mask,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= cps_cpu_disable,
	.cpu_die		= cps_cpu_die,
#endif
};

bool mips_cps_smp_in_use(void)
{
	extern struct plat_smp_ops *mp_ops;
	return mp_ops == &cps_smp_ops;
}

int register_cps_smp_ops(void)
{
	if (!mips_cm_present()) {
		pr_warn("MIPS CPS SMP unable to proceed without a CM\n");
		return -ENODEV;
	}

	/* check we have a GIC - we need one for IPIs */
	if (!(read_gcr_gic_status() & CM_GCR_GIC_STATUS_EX_MSK)) {
		pr_warn("MIPS CPS SMP unable to proceed without a GIC\n");
		return -ENODEV;
	}

	register_smp_ops(&cps_smp_ops);
	return 0;
}
