/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/io.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/gic.h>
#include <asm/mips-cm.h>
#include <asm/mips-cpc.h>
#include <asm/mips_mt.h>
#include <asm/mipsregs.h>
#include <asm/smp-cps.h>
#include <asm/time.h>
#include <asm/uasm.h>

static DECLARE_BITMAP(core_power, NR_CPUS);

struct core_boot_config *mips_cps_core_bootcfg;

static unsigned core_vpe_count(unsigned core)
{
	unsigned cfg;

	if (!config_enabled(CONFIG_MIPS_MT_SMP) || !cpu_has_mipsmt)
		return 1;

	write_gcr_cl_other(core << CM_GCR_Cx_OTHER_CORENUM_SHF);
	cfg = read_gcr_co_config() & CM_GCR_Cx_CONFIG_PVPE_MSK;
	return (cfg >> CM_GCR_Cx_CONFIG_PVPE_SHF) + 1;
}

static void __init cps_smp_setup(void)
{
	unsigned int ncores, nvpes, core_vpes;
	int c, v;
	u32 *entry_code;

	/* Detect & record VPE topology */
	ncores = mips_cm_numcores();
	pr_info("VPE topology ");
	for (c = nvpes = 0; c < ncores; c++) {
		core_vpes = core_vpe_count(c);
		pr_cont("%c%u", c ? ',' : '{', core_vpes);

		/* Use the number of VPEs in core 0 for smp_num_siblings */
		if (!c)
			smp_num_siblings = core_vpes;

		for (v = 0; v < min_t(int, core_vpes, NR_CPUS - nvpes); v++) {
			cpu_data[nvpes + v].core = c;
#ifdef CONFIG_MIPS_MT_SMP
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

	/* Core 0 is powered up (we're running on it) */
	bitmap_set(core_power, 0, 1);

	/* Initialise core 0 */
	mips_cps_core_init();

	/* Patch the start of mips_cps_core_entry to provide the CM base */
	entry_code = (u32 *)&mips_cps_core_entry;
	UASM_i_LA(&entry_code, 3, (long)mips_cm_base);

	/* Make core 0 coherent with everything */
	write_gcr_cl_coherence(0xff);
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	unsigned ncores, core_vpes, c;

	mips_mt_set_cpuoptions();

	/* Allocate core boot configuration structs */
	ncores = mips_cm_numcores();
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

static void boot_core(unsigned core)
{
	u32 access;

	/* Select the appropriate core */
	write_gcr_cl_other(core << CM_GCR_Cx_OTHER_CORENUM_SHF);

	/* Set its reset vector */
	write_gcr_co_reset_base(CKSEG1ADDR((unsigned long)mips_cps_core_entry));

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Ensure the core can access the GCRs */
	access = read_gcr_access();
	access |= 1 << (CM_GCR_ACCESS_ACCESSEN_SHF + core);
	write_gcr_access(access);

	if (mips_cpc_present()) {
		/* Select the appropriate core */
		write_cpc_cl_other(core << CPC_Cx_OTHER_CORENUM_SHF);

		/* Reset the core */
		write_cpc_co_cmd(CPC_Cx_CMD_RESET);
	} else {
		/* Take the core out of reset */
		write_gcr_co_reset_release(0);
	}

	/* The core is now powered up */
	bitmap_set(core_power, core, 1);
}

static void remote_vpe_boot(void *dummy)
{
	mips_cps_boot_vpes();
}

static void cps_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned core = cpu_data[cpu].core;
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	struct core_boot_config *core_cfg = &mips_cps_core_bootcfg[core];
	struct vpe_boot_config *vpe_cfg = &core_cfg->vpe_config[vpe_id];
	unsigned int remote;
	int err;

	vpe_cfg->pc = (unsigned long)&smp_bootstrap;
	vpe_cfg->sp = __KSTK_TOS(idle);
	vpe_cfg->gp = (unsigned long)task_thread_info(idle);

	atomic_or(1 << cpu_vpe_id(&cpu_data[cpu]), &core_cfg->vpe_mask);

	if (!test_bit(core, core_power)) {
		/* Boot a VPE on a powered down core */
		boot_core(core);
		return;
	}

	if (core != current_cpu_data.core) {
		/* Boot a VPE on another powered up core */
		for (remote = 0; remote < NR_CPUS; remote++) {
			if (cpu_data[remote].core != core)
				continue;
			if (cpu_online(remote))
				break;
		}
		BUG_ON(remote >= NR_CPUS);

		err = smp_call_function_single(remote, remote_vpe_boot,
					       NULL, 1);
		if (err)
			panic("Failed to call remote CPU\n");
		return;
	}

	BUG_ON(!cpu_has_mipsmt);

	/* Boot a VPE on this core */
	mips_cps_boot_vpes();
}

static void cps_init_secondary(void)
{
	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	change_c0_status(ST0_IM, STATUSF_IP3 | STATUSF_IP4 |
				 STATUSF_IP6 | STATUSF_IP7);
}

static void cps_smp_finish(void)
{
	write_c0_compare(read_c0_count() + (8 * mips_hpt_frequency / HZ));

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpu_set(smp_processor_id(), mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */

	local_irq_enable();
}

static void cps_cpus_done(void)
{
}

static struct plat_smp_ops cps_smp_ops = {
	.smp_setup		= cps_smp_setup,
	.prepare_cpus		= cps_prepare_cpus,
	.boot_secondary		= cps_boot_secondary,
	.init_secondary		= cps_init_secondary,
	.smp_finish		= cps_smp_finish,
	.send_ipi_single	= gic_send_ipi_single,
	.send_ipi_mask		= gic_send_ipi_mask,
	.cpus_done		= cps_cpus_done,
};

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
