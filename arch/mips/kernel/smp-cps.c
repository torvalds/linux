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

struct boot_config mips_cps_bootcfg;

static void init_core(void)
{
	unsigned int nvpes, t;
	u32 mvpconf0, vpeconf0, vpecontrol, tcstatus, tcbind, status;

	if (!cpu_has_mipsmt)
		return;

	/* Enter VPE configuration state */
	dvpe();
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	/* Retrieve the count of VPEs in this core */
	mvpconf0 = read_c0_mvpconf0();
	nvpes = ((mvpconf0 & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1;
	smp_num_siblings = nvpes;

	for (t = 1; t < nvpes; t++) {
		/* Use a 1:1 mapping of TC index to VPE index */
		settc(t);

		/* Bind 1 TC to this VPE */
		tcbind = read_tc_c0_tcbind();
		tcbind &= ~TCBIND_CURVPE;
		tcbind |= t << TCBIND_CURVPE_SHIFT;
		write_tc_c0_tcbind(tcbind);

		/* Set exclusive TC, non-active, master */
		vpeconf0 = read_vpe_c0_vpeconf0();
		vpeconf0 &= ~(VPECONF0_XTC | VPECONF0_VPA);
		vpeconf0 |= t << VPECONF0_XTC_SHIFT;
		vpeconf0 |= VPECONF0_MVP;
		write_vpe_c0_vpeconf0(vpeconf0);

		/* Declare TC non-active, non-allocatable & interrupt exempt */
		tcstatus = read_tc_c0_tcstatus();
		tcstatus &= ~(TCSTATUS_A | TCSTATUS_DA);
		tcstatus |= TCSTATUS_IXMT;
		write_tc_c0_tcstatus(tcstatus);

		/* Halt the TC */
		write_tc_c0_tchalt(TCHALT_H);

		/* Allow only 1 TC to execute */
		vpecontrol = read_vpe_c0_vpecontrol();
		vpecontrol &= ~VPECONTROL_TE;
		write_vpe_c0_vpecontrol(vpecontrol);

		/* Copy (most of) Status from VPE 0 */
		status = read_c0_status();
		status &= ~(ST0_IM | ST0_IE | ST0_KSU);
		status |= ST0_CU0;
		write_vpe_c0_status(status);

		/* Copy Config from VPE 0 */
		write_vpe_c0_config(read_c0_config());
		write_vpe_c0_config7(read_c0_config7());

		/* Ensure no software interrupts are pending */
		write_vpe_c0_cause(0);

		/* Sync Count */
		write_vpe_c0_count(read_c0_count());
	}

	/* Leave VPE configuration state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);
}

static void __init cps_smp_setup(void)
{
	unsigned int ncores, nvpes, core_vpes;
	int c, v;
	u32 core_cfg, *entry_code;

	/* Detect & record VPE topology */
	ncores = mips_cm_numcores();
	pr_info("VPE topology ");
	for (c = nvpes = 0; c < ncores; c++) {
		if (cpu_has_mipsmt && config_enabled(CONFIG_MIPS_MT_SMP)) {
			write_gcr_cl_other(c << CM_GCR_Cx_OTHER_CORENUM_SHF);
			core_cfg = read_gcr_co_config();
			core_vpes = ((core_cfg & CM_GCR_Cx_CONFIG_PVPE_MSK) >>
				     CM_GCR_Cx_CONFIG_PVPE_SHF) + 1;
		} else {
			core_vpes = 1;
		}

		pr_cont("%c%u", c ? ',' : '{', core_vpes);

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

	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	/* Initialise core 0 */
	init_core();

	/* Patch the start of mips_cps_core_entry to provide the CM base */
	entry_code = (u32 *)&mips_cps_core_entry;
	UASM_i_LA(&entry_code, 3, (long)mips_cm_base);

	/* Make core 0 coherent with everything */
	write_gcr_cl_coherence(0xff);
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	mips_mt_set_cpuoptions();
}

static void boot_core(struct boot_config *cfg)
{
	u32 access;

	/* Select the appropriate core */
	write_gcr_cl_other(cfg->core << CM_GCR_Cx_OTHER_CORENUM_SHF);

	/* Set its reset vector */
	write_gcr_co_reset_base(CKSEG1ADDR((unsigned long)mips_cps_core_entry));

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Ensure the core can access the GCRs */
	access = read_gcr_access();
	access |= 1 << (CM_GCR_ACCESS_ACCESSEN_SHF + cfg->core);
	write_gcr_access(access);

	/* Copy cfg */
	mips_cps_bootcfg = *cfg;

	if (mips_cpc_present()) {
		/* Select the appropriate core */
		write_cpc_cl_other(cfg->core << CPC_Cx_OTHER_CORENUM_SHF);

		/* Reset the core */
		write_cpc_co_cmd(CPC_Cx_CMD_RESET);
	} else {
		/* Take the core out of reset */
		write_gcr_co_reset_release(0);
	}

	/* The core is now powered up */
	bitmap_set(core_power, cfg->core, 1);
}

static void boot_vpe(void *info)
{
	struct boot_config *cfg = info;
	u32 tcstatus, vpeconf0;

	/* Enter VPE configuration state */
	dvpe();
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(cfg->vpe);

	/* Set the TC restart PC */
	write_tc_c0_tcrestart((unsigned long)&smp_bootstrap);

	/* Activate the TC, allow interrupts */
	tcstatus = read_tc_c0_tcstatus();
	tcstatus &= ~TCSTATUS_IXMT;
	tcstatus |= TCSTATUS_A;
	write_tc_c0_tcstatus(tcstatus);

	/* Clear the TC halt bit */
	write_tc_c0_tchalt(0);

	/* Activate the VPE */
	vpeconf0 = read_vpe_c0_vpeconf0();
	vpeconf0 |= VPECONF0_VPA;
	write_vpe_c0_vpeconf0(vpeconf0);

	/* Set the stack & global pointer registers */
	write_tc_gpr_sp(cfg->sp);
	write_tc_gpr_gp(cfg->gp);

	/* Leave VPE configuration state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	/* Enable other VPEs to execute */
	evpe(EVPE_ENABLE);
}

static void cps_boot_secondary(int cpu, struct task_struct *idle)
{
	struct boot_config cfg;
	unsigned int remote;
	int err;

	cfg.core = cpu_data[cpu].core;
	cfg.vpe = cpu_vpe_id(&cpu_data[cpu]);
	cfg.pc = (unsigned long)&smp_bootstrap;
	cfg.sp = __KSTK_TOS(idle);
	cfg.gp = (unsigned long)task_thread_info(idle);

	if (!test_bit(cfg.core, core_power)) {
		/* Boot a VPE on a powered down core */
		boot_core(&cfg);
		return;
	}

	if (cfg.core != current_cpu_data.core) {
		/* Boot a VPE on another powered up core */
		for (remote = 0; remote < NR_CPUS; remote++) {
			if (cpu_data[remote].core != cfg.core)
				continue;
			if (cpu_online(remote))
				break;
		}
		BUG_ON(remote >= NR_CPUS);

		err = smp_call_function_single(remote, boot_vpe, &cfg, 1);
		if (err)
			panic("Failed to call remote CPU\n");
		return;
	}

	BUG_ON(!cpu_has_mipsmt);

	/* Boot a VPE on this core */
	boot_vpe(&cfg);
}

static void cps_init_secondary(void)
{
	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	/* TODO: revisit this assumption once hotplug is implemented */
	if (cpu_vpe_id(&current_cpu_data) == 0)
		init_core();

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
