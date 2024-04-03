// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/hotplug.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/irq.h>

#include <asm/bcache.h>
#include <asm/mips-cps.h>
#include <asm/mips_mt.h>
#include <asm/mipsregs.h>
#include <asm/pm-cps.h>
#include <asm/r4kcache.h>
#include <asm/regdef.h>
#include <asm/smp.h>
#include <asm/smp-cps.h>
#include <asm/time.h>
#include <asm/uasm.h>

#define BEV_VEC_SIZE	0x500
#define BEV_VEC_ALIGN	0x1000

enum label_id {
	label_not_nmi = 1,
};

UASM_L_LA(_not_nmi)

static DECLARE_BITMAP(core_power, NR_CPUS);
static uint32_t core_entry_reg;
static phys_addr_t cps_vec_pa;

struct core_boot_config *mips_cps_core_bootcfg;

static unsigned __init core_vpe_count(unsigned int cluster, unsigned core)
{
	return min(smp_max_threads, mips_cps_numvps(cluster, core));
}

static void __init *mips_cps_build_core_entry(void *addr)
{
	extern void (*nmi_handler)(void);
	u32 *p = addr;
	u32 val;
	struct uasm_label labels[2];
	struct uasm_reloc relocs[2];
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;

	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	uasm_i_mfc0(&p, GPR_K0, C0_STATUS);
	UASM_i_LA(&p, GPR_T9, ST0_NMI);
	uasm_i_and(&p, GPR_K0, GPR_K0, GPR_T9);

	uasm_il_bnez(&p, &r, GPR_K0, label_not_nmi);
	uasm_i_nop(&p);
	UASM_i_LA(&p, GPR_K0, (long)&nmi_handler);

	uasm_l_not_nmi(&l, p);

	val = CAUSEF_IV;
	uasm_i_lui(&p, GPR_K0, val >> 16);
	uasm_i_ori(&p, GPR_K0, GPR_K0, val & 0xffff);
	uasm_i_mtc0(&p, GPR_K0, C0_CAUSE);
	val = ST0_CU1 | ST0_CU0 | ST0_BEV | ST0_KX_IF_64;
	uasm_i_lui(&p, GPR_K0, val >> 16);
	uasm_i_ori(&p, GPR_K0, GPR_K0, val & 0xffff);
	uasm_i_mtc0(&p, GPR_K0, C0_STATUS);
	uasm_i_ehb(&p);
	uasm_i_ori(&p, GPR_A0, 0, read_c0_config() & CONF_CM_CMASK);
	UASM_i_LA(&p, GPR_A1, (long)mips_gcr_base);
#if defined(KBUILD_64BIT_SYM32) || defined(CONFIG_32BIT)
	UASM_i_LA(&p, GPR_T9, CKSEG1ADDR(__pa_symbol(mips_cps_core_boot)));
#else
	UASM_i_LA(&p, GPR_T9, TO_UNCAC(__pa_symbol(mips_cps_core_boot)));
#endif
	uasm_i_jr(&p, GPR_T9);
	uasm_i_nop(&p);

	uasm_resolve_relocs(relocs, labels);

	return p;
}

static int __init allocate_cps_vecs(void)
{
	/* Try to allocate in KSEG1 first */
	cps_vec_pa = memblock_phys_alloc_range(BEV_VEC_SIZE, BEV_VEC_ALIGN,
						0x0, CSEGX_SIZE - 1);

	if (cps_vec_pa)
		core_entry_reg = CKSEG1ADDR(cps_vec_pa) &
					CM_GCR_Cx_RESET_BASE_BEVEXCBASE;

	if (!cps_vec_pa && mips_cm_is64) {
		cps_vec_pa = memblock_phys_alloc_range(BEV_VEC_SIZE, BEV_VEC_ALIGN,
							0x0, SZ_4G - 1);
		if (cps_vec_pa)
			core_entry_reg = (cps_vec_pa & CM_GCR_Cx_RESET_BASE_BEVEXCBASE) |
					CM_GCR_Cx_RESET_BASE_MODE;
	}

	if (!cps_vec_pa)
		return -ENOMEM;

	return 0;
}

static void __init setup_cps_vecs(void)
{
	void *cps_vec;

	cps_vec = (void *)CKSEG1ADDR_OR_64BIT(cps_vec_pa);
	mips_cps_build_core_entry(cps_vec);

	memcpy(cps_vec + 0x200, &excep_tlbfill, 0x80);
	memcpy(cps_vec + 0x280, &excep_xtlbfill, 0x80);
	memcpy(cps_vec + 0x300, &excep_cache, 0x80);
	memcpy(cps_vec + 0x380, &excep_genex, 0x80);
	memcpy(cps_vec + 0x400, &excep_intex, 0x80);
	memcpy(cps_vec + 0x480, &excep_ejtag, 0x80);

	/* Make sure no prefetched data in cache */
	blast_inv_dcache_range(CKSEG0ADDR_OR_64BIT(cps_vec_pa), CKSEG0ADDR_OR_64BIT(cps_vec_pa) + BEV_VEC_SIZE);
	bc_inv(CKSEG0ADDR_OR_64BIT(cps_vec_pa), BEV_VEC_SIZE);
	__sync();
}

static void __init cps_smp_setup(void)
{
	unsigned int nclusters, ncores, nvpes, core_vpes;
	int cl, c, v;

	/* Detect & record VPE topology */
	nvpes = 0;
	nclusters = mips_cps_numclusters();
	pr_info("%s topology ", cpu_has_mips_r6 ? "VP" : "VPE");
	for (cl = 0; cl < nclusters; cl++) {
		if (cl > 0)
			pr_cont(",");
		pr_cont("{");

		ncores = mips_cps_numcores(cl);
		for (c = 0; c < ncores; c++) {
			core_vpes = core_vpe_count(cl, c);

			if (c > 0)
				pr_cont(",");
			pr_cont("%u", core_vpes);

			/* Use the number of VPEs in cluster 0 core 0 for smp_num_siblings */
			if (!cl && !c)
				smp_num_siblings = core_vpes;

			for (v = 0; v < min_t(int, core_vpes, NR_CPUS - nvpes); v++) {
				cpu_set_cluster(&cpu_data[nvpes + v], cl);
				cpu_set_core(&cpu_data[nvpes + v], c);
				cpu_set_vpe_id(&cpu_data[nvpes + v], v);
			}

			nvpes += core_vpes;
		}

		pr_cont("}");
	}
	pr_cont(" total %u\n", nvpes);

	/* Indicate present CPUs (CPU being synonymous with VPE) */
	for (v = 0; v < min_t(unsigned, nvpes, NR_CPUS); v++) {
		set_cpu_possible(v, cpu_cluster(&cpu_data[v]) == 0);
		set_cpu_present(v, cpu_cluster(&cpu_data[v]) == 0);
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

	if (allocate_cps_vecs())
		pr_err("Failed to allocate CPS vectors\n");

	if (core_entry_reg && mips_cm_revision() >= CM_REV_CM3)
		write_gcr_bev_base(core_entry_reg);

#ifdef CONFIG_MIPS_MT_FPAFF
	/* If we have an FPU, enroll ourselves in the FPU-full mask */
	if (cpu_has_fpu)
		cpumask_set_cpu(0, &mt_fpu_cpumask);
#endif /* CONFIG_MIPS_MT_FPAFF */
}

static void __init cps_prepare_cpus(unsigned int max_cpus)
{
	unsigned ncores, core_vpes, c, cca;
	bool cca_unsuitable, cores_limited;

	mips_mt_set_cpuoptions();

	if (!core_entry_reg) {
		pr_err("core_entry address unsuitable, disabling smp-cps\n");
		goto err_out;
	}

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
	cores_limited = false;
	if (cca_unsuitable || cpu_has_dc_aliases) {
		for_each_present_cpu(c) {
			if (cpus_are_siblings(smp_processor_id(), c))
				continue;

			set_cpu_present(c, false);
			cores_limited = true;
		}
	}
	if (cores_limited)
		pr_warn("Using only one core due to %s%s%s\n",
			cca_unsuitable ? "unsuitable CCA" : "",
			(cca_unsuitable && cpu_has_dc_aliases) ? " & " : "",
			cpu_has_dc_aliases ? "dcache aliasing" : "");

	setup_cps_vecs();

	/* Allocate core boot configuration structs */
	ncores = mips_cps_numcores(0);
	mips_cps_core_bootcfg = kcalloc(ncores, sizeof(*mips_cps_core_bootcfg),
					GFP_KERNEL);
	if (!mips_cps_core_bootcfg) {
		pr_err("Failed to allocate boot config for %u cores\n", ncores);
		goto err_out;
	}

	/* Allocate VPE boot configuration structs */
	for (c = 0; c < ncores; c++) {
		core_vpes = core_vpe_count(0, c);
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
	atomic_set(&mips_cps_core_bootcfg[cpu_core(&current_cpu_data)].vpe_mask,
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
	u32 stat, seq_state;
	unsigned timeout;

	/* Select the appropriate core */
	mips_cm_lock_other(0, core, 0, CM_GCR_Cx_OTHER_BLOCK_LOCAL);

	/* Set its reset vector */
	write_gcr_co_reset_base(core_entry_reg);

	/* Ensure its coherency is disabled */
	write_gcr_co_coherence(0);

	/* Start it with the legacy memory map and exception base */
	write_gcr_co_reset_ext_base(CM_GCR_Cx_RESET_EXT_BASE_UEB);

	/* Ensure the core can access the GCRs */
	set_gcr_access(1 << core);

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
			seq_state = stat & CPC_Cx_STAT_CONF_SEQSTATE;
			seq_state >>= __ffs(CPC_Cx_STAT_CONF_SEQSTATE);

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
	unsigned core = cpu_core(&current_cpu_data);
	struct core_boot_config *core_cfg = &mips_cps_core_bootcfg[core];

	mips_cps_boot_vpes(core_cfg, cpu_vpe_id(&current_cpu_data));
}

static int cps_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned core = cpu_core(&cpu_data[cpu]);
	unsigned vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	struct core_boot_config *core_cfg = &mips_cps_core_bootcfg[core];
	struct vpe_boot_config *vpe_cfg = &core_cfg->vpe_config[vpe_id];
	unsigned int remote;
	int err;

	/* We don't yet support booting CPUs in other clusters */
	if (cpu_cluster(&cpu_data[cpu]) != cpu_cluster(&raw_current_cpu_data))
		return -ENOSYS;

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
		mips_cm_lock_other(0, core, vpe_id, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
		write_gcr_co_reset_base(core_entry_reg);
		mips_cm_unlock_other();
	}

	if (!cpus_are_siblings(cpu, smp_processor_id())) {
		/* Boot a VPE on another powered up core */
		for (remote = 0; remote < NR_CPUS; remote++) {
			if (!cpus_are_siblings(cpu, remote))
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
	return 0;
}

static void cps_init_secondary(void)
{
	int core = cpu_core(&current_cpu_data);

	/* Disable MT - we only want to run 1 TC per VPE */
	if (cpu_has_mipsmt)
		dmt();

	if (mips_cm_revision() >= CM_REV_CM3) {
		unsigned int ident = read_gic_vl_ident();

		/*
		 * Ensure that our calculation of the VP ID matches up with
		 * what the GIC reports, otherwise we'll have configured
		 * interrupts incorrectly.
		 */
		BUG_ON(ident != mips_cm_vp_id(smp_processor_id()));
	}

	if (core > 0 && !read_gcr_cl_coherence())
		pr_warn("Core %u is not in coherent domain\n", core);

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

#if defined(CONFIG_HOTPLUG_CPU) || defined(CONFIG_KEXEC_CORE)

enum cpu_death {
	CPU_DEATH_HALT,
	CPU_DEATH_POWER,
};

static void cps_shutdown_this_cpu(enum cpu_death death)
{
	unsigned int cpu, core, vpe_id;

	cpu = smp_processor_id();
	core = cpu_core(&cpu_data[cpu]);

	if (death == CPU_DEATH_HALT) {
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
		if (IS_ENABLED(CONFIG_HOTPLUG_CPU)) {
			pr_debug("Gating power to core %d\n", core);
			/* Power down the core */
			cps_pm_enter_state(CPS_PM_POWER_GATED);
		}
	}
}

#ifdef CONFIG_KEXEC_CORE

static void cps_kexec_nonboot_cpu(void)
{
	if (cpu_has_mipsmt || cpu_has_vp)
		cps_shutdown_this_cpu(CPU_DEATH_HALT);
	else
		cps_shutdown_this_cpu(CPU_DEATH_POWER);
}

#endif /* CONFIG_KEXEC_CORE */

#endif /* CONFIG_HOTPLUG_CPU || CONFIG_KEXEC_CORE */

#ifdef CONFIG_HOTPLUG_CPU

static int cps_cpu_disable(void)
{
	unsigned cpu = smp_processor_id();
	struct core_boot_config *core_cfg;

	if (!cps_pm_support_state(CPS_PM_POWER_GATED))
		return -EINVAL;

	core_cfg = &mips_cps_core_bootcfg[cpu_core(&current_cpu_data)];
	atomic_sub(1 << cpu_vpe_id(&current_cpu_data), &core_cfg->vpe_mask);
	smp_mb__after_atomic();
	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();
	irq_migrate_all_off_this_cpu();

	return 0;
}

static unsigned cpu_death_sibling;
static enum cpu_death cpu_death;

void play_dead(void)
{
	unsigned int cpu;

	local_irq_disable();
	idle_task_exit();
	cpu = smp_processor_id();
	cpu_death = CPU_DEATH_POWER;

	pr_debug("CPU%d going offline\n", cpu);

	if (cpu_has_mipsmt || cpu_has_vp) {
		/* Look for another online VPE within the core */
		for_each_online_cpu(cpu_death_sibling) {
			if (!cpus_are_siblings(cpu, cpu_death_sibling))
				continue;

			/*
			 * There is an online VPE within the core. Just halt
			 * this TC and leave the core alone.
			 */
			cpu_death = CPU_DEATH_HALT;
			break;
		}
	}

	cpuhp_ap_report_dead();

	cps_shutdown_this_cpu(cpu_death);

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

static void cps_cpu_die(unsigned int cpu) { }

static void cps_cleanup_dead_cpu(unsigned cpu)
{
	unsigned core = cpu_core(&cpu_data[cpu]);
	unsigned int vpe_id = cpu_vpe_id(&cpu_data[cpu]);
	ktime_t fail_time;
	unsigned stat;
	int err;

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
		fail_time = ktime_add_ms(ktime_get(), 2000);
		do {
			mips_cm_lock_other(0, core, 0, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
			mips_cpc_lock_other(core);
			stat = read_cpc_co_stat_conf();
			stat &= CPC_Cx_STAT_CONF_SEQSTATE;
			stat >>= __ffs(CPC_Cx_STAT_CONF_SEQSTATE);
			mips_cpc_unlock_other();
			mips_cm_unlock_other();

			if (stat == CPC_Cx_STAT_CONF_SEQSTATE_D0 ||
			    stat == CPC_Cx_STAT_CONF_SEQSTATE_D2 ||
			    stat == CPC_Cx_STAT_CONF_SEQSTATE_U2)
				break;

			/*
			 * The core ought to have powered down, but didn't &
			 * now we don't really know what state it's in. It's
			 * likely that its _pwr_up pin has been wired to logic
			 * 1 & it powered back up as soon as we powered it
			 * down...
			 *
			 * The best we can do is warn the user & continue in
			 * the hope that the core is doing nothing harmful &
			 * might behave properly if we online it later.
			 */
			if (WARN(ktime_after(ktime_get(), fail_time),
				 "CPU%u hasn't powered down, seq. state %u\n",
				 cpu, stat))
				break;
		} while (1);

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
			mips_cm_lock_other(0, core, vpe_id, CM_GCR_Cx_OTHER_BLOCK_LOCAL);
			stat = read_cpc_co_vp_running();
			mips_cm_unlock_other();
		} while (stat & (1 << vpe_id));
	}
}

#endif /* CONFIG_HOTPLUG_CPU */

static const struct plat_smp_ops cps_smp_ops = {
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
	.cleanup_dead_cpu	= cps_cleanup_dead_cpu,
#endif
#ifdef CONFIG_KEXEC_CORE
	.kexec_nonboot_cpu	= cps_kexec_nonboot_cpu,
#endif
};

bool mips_cps_smp_in_use(void)
{
	extern const struct plat_smp_ops *mp_ops;
	return mp_ops == &cps_smp_ops;
}

int register_cps_smp_ops(void)
{
	if (!mips_cm_present()) {
		pr_warn("MIPS CPS SMP unable to proceed without a CM\n");
		return -ENODEV;
	}

	/* check we have a GIC - we need one for IPIs */
	if (!(read_gcr_gic_status() & CM_GCR_GIC_STATUS_EX)) {
		pr_warn("MIPS CPS SMP unable to proceed without a GIC\n");
		return -ENODEV;
	}

	register_smp_ops(&cps_smp_ops);
	return 0;
}
