/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/cpuhotplug.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/slab.h>

#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/cacheops.h>
#include <asm/idle.h>
#include <asm/mips-cps.h>
#include <asm/mipsmtregs.h>
#include <asm/pm.h>
#include <asm/pm-cps.h>
#include <asm/smp-cps.h>
#include <asm/uasm.h>

/*
 * cps_nc_entry_fn - type of a generated non-coherent state entry function
 * @online: the count of online coupled VPEs
 * @nc_ready_count: pointer to a non-coherent mapping of the core ready_count
 *
 * The code entering & exiting non-coherent states is generated at runtime
 * using uasm, in order to ensure that the compiler cannot insert a stray
 * memory access at an unfortunate time and to allow the generation of optimal
 * core-specific code particularly for cache routines. If coupled_coherence
 * is non-zero and this is the entry function for the CPS_PM_NC_WAIT state,
 * returns the number of VPEs that were in the wait state at the point this
 * VPE left it. Returns garbage if coupled_coherence is zero or this is not
 * the entry function for CPS_PM_NC_WAIT.
 */
typedef unsigned (*cps_nc_entry_fn)(unsigned online, u32 *nc_ready_count);

/*
 * The entry point of the generated non-coherent idle state entry/exit
 * functions. Actually per-core rather than per-CPU.
 */
static DEFINE_PER_CPU_READ_MOSTLY(cps_nc_entry_fn[CPS_PM_STATE_COUNT],
				  nc_asm_enter);

/* Bitmap indicating which states are supported by the system */
static DECLARE_BITMAP(state_support, CPS_PM_STATE_COUNT);

/*
 * Indicates the number of coupled VPEs ready to operate in a non-coherent
 * state. Actually per-core rather than per-CPU.
 */
static DEFINE_PER_CPU_ALIGNED(u32*, ready_count);

/* Indicates online CPUs coupled with the current CPU */
static DEFINE_PER_CPU_ALIGNED(cpumask_t, online_coupled);

/*
 * Used to synchronize entry to deep idle states. Actually per-core rather
 * than per-CPU.
 */
static DEFINE_PER_CPU_ALIGNED(atomic_t, pm_barrier);

/* Saved CPU state across the CPS_PM_POWER_GATED state */
DEFINE_PER_CPU_ALIGNED(struct mips_static_suspend_state, cps_cpu_state);

/* A somewhat arbitrary number of labels & relocs for uasm */
static struct uasm_label labels[32];
static struct uasm_reloc relocs[32];

enum mips_reg {
	zero, at, v0, v1, a0, a1, a2, a3,
	t0, t1, t2, t3, t4, t5, t6, t7,
	s0, s1, s2, s3, s4, s5, s6, s7,
	t8, t9, k0, k1, gp, sp, fp, ra,
};

bool cps_pm_support_state(enum cps_pm_state state)
{
	return test_bit(state, state_support);
}

static void coupled_barrier(atomic_t *a, unsigned online)
{
	/*
	 * This function is effectively the same as
	 * cpuidle_coupled_parallel_barrier, which can't be used here since
	 * there's no cpuidle device.
	 */

	if (!coupled_coherence)
		return;

	smp_mb__before_atomic();
	atomic_inc(a);

	while (atomic_read(a) < online)
		cpu_relax();

	if (atomic_inc_return(a) == online * 2) {
		atomic_set(a, 0);
		return;
	}

	while (atomic_read(a) > online)
		cpu_relax();
}

int cps_pm_enter_state(enum cps_pm_state state)
{
	unsigned cpu = smp_processor_id();
	unsigned core = cpu_core(&current_cpu_data);
	unsigned online, left;
	cpumask_t *coupled_mask = this_cpu_ptr(&online_coupled);
	u32 *core_ready_count, *nc_core_ready_count;
	void *nc_addr;
	cps_nc_entry_fn entry;
	struct core_boot_config *core_cfg;
	struct vpe_boot_config *vpe_cfg;

	/* Check that there is an entry function for this state */
	entry = per_cpu(nc_asm_enter, core)[state];
	if (!entry)
		return -EINVAL;

	/* Calculate which coupled CPUs (VPEs) are online */
#if defined(CONFIG_MIPS_MT) || defined(CONFIG_CPU_MIPSR6)
	if (cpu_online(cpu)) {
		cpumask_and(coupled_mask, cpu_online_mask,
			    &cpu_sibling_map[cpu]);
		online = cpumask_weight(coupled_mask);
		cpumask_clear_cpu(cpu, coupled_mask);
	} else
#endif
	{
		cpumask_clear(coupled_mask);
		online = 1;
	}

	/* Setup the VPE to run mips_cps_pm_restore when started again */
	if (IS_ENABLED(CONFIG_CPU_PM) && state == CPS_PM_POWER_GATED) {
		/* Power gating relies upon CPS SMP */
		if (!mips_cps_smp_in_use())
			return -EINVAL;

		core_cfg = &mips_cps_core_bootcfg[core];
		vpe_cfg = &core_cfg->vpe_config[cpu_vpe_id(&current_cpu_data)];
		vpe_cfg->pc = (unsigned long)mips_cps_pm_restore;
		vpe_cfg->gp = (unsigned long)current_thread_info();
		vpe_cfg->sp = 0;
	}

	/* Indicate that this CPU might not be coherent */
	cpumask_clear_cpu(cpu, &cpu_coherent_mask);
	smp_mb__after_atomic();

	/* Create a non-coherent mapping of the core ready_count */
	core_ready_count = per_cpu(ready_count, core);
	nc_addr = kmap_noncoherent(virt_to_page(core_ready_count),
				   (unsigned long)core_ready_count);
	nc_addr += ((unsigned long)core_ready_count & ~PAGE_MASK);
	nc_core_ready_count = nc_addr;

	/* Ensure ready_count is zero-initialised before the assembly runs */
	ACCESS_ONCE(*nc_core_ready_count) = 0;
	coupled_barrier(&per_cpu(pm_barrier, core), online);

	/* Run the generated entry code */
	left = entry(online, nc_core_ready_count);

	/* Remove the non-coherent mapping of ready_count */
	kunmap_noncoherent();

	/* Indicate that this CPU is definitely coherent */
	cpumask_set_cpu(cpu, &cpu_coherent_mask);

	/*
	 * If this VPE is the first to leave the non-coherent wait state then
	 * it needs to wake up any coupled VPEs still running their wait
	 * instruction so that they return to cpuidle, which can then complete
	 * coordination between the coupled VPEs & provide the governor with
	 * a chance to reflect on the length of time the VPEs were in the
	 * idle state.
	 */
	if (coupled_coherence && (state == CPS_PM_NC_WAIT) && (left == online))
		arch_send_call_function_ipi_mask(coupled_mask);

	return 0;
}

static void cps_gen_cache_routine(u32 **pp, struct uasm_label **pl,
				  struct uasm_reloc **pr,
				  const struct cache_desc *cache,
				  unsigned op, int lbl)
{
	unsigned cache_size = cache->ways << cache->waybit;
	unsigned i;
	const unsigned unroll_lines = 32;

	/* If the cache isn't present this function has it easy */
	if (cache->flags & MIPS_CACHE_NOT_PRESENT)
		return;

	/* Load base address */
	UASM_i_LA(pp, t0, (long)CKSEG0);

	/* Calculate end address */
	if (cache_size < 0x8000)
		uasm_i_addiu(pp, t1, t0, cache_size);
	else
		UASM_i_LA(pp, t1, (long)(CKSEG0 + cache_size));

	/* Start of cache op loop */
	uasm_build_label(pl, *pp, lbl);

	/* Generate the cache ops */
	for (i = 0; i < unroll_lines; i++) {
		if (cpu_has_mips_r6) {
			uasm_i_cache(pp, op, 0, t0);
			uasm_i_addiu(pp, t0, t0, cache->linesz);
		} else {
			uasm_i_cache(pp, op, i * cache->linesz, t0);
		}
	}

	if (!cpu_has_mips_r6)
		/* Update the base address */
		uasm_i_addiu(pp, t0, t0, unroll_lines * cache->linesz);

	/* Loop if we haven't reached the end address yet */
	uasm_il_bne(pp, pr, t0, t1, lbl);
	uasm_i_nop(pp);
}

static int cps_gen_flush_fsb(u32 **pp, struct uasm_label **pl,
			     struct uasm_reloc **pr,
			     const struct cpuinfo_mips *cpu_info,
			     int lbl)
{
	unsigned i, fsb_size = 8;
	unsigned num_loads = (fsb_size * 3) / 2;
	unsigned line_stride = 2;
	unsigned line_size = cpu_info->dcache.linesz;
	unsigned perf_counter, perf_event;
	unsigned revision = cpu_info->processor_id & PRID_REV_MASK;

	/*
	 * Determine whether this CPU requires an FSB flush, and if so which
	 * performance counter/event reflect stalls due to a full FSB.
	 */
	switch (__get_cpu_type(cpu_info->cputype)) {
	case CPU_INTERAPTIV:
		perf_counter = 1;
		perf_event = 51;
		break;

	case CPU_PROAPTIV:
		/* Newer proAptiv cores don't require this workaround */
		if (revision >= PRID_REV_ENCODE_332(1, 1, 0))
			return 0;

		/* On older ones it's unavailable */
		return -1;

	default:
		/* Assume that the CPU does not need this workaround */
		return 0;
	}

	/*
	 * Ensure that the fill/store buffer (FSB) is not holding the results
	 * of a prefetch, since if it is then the CPC sequencer may become
	 * stuck in the D3 (ClrBus) state whilst entering a low power state.
	 */

	/* Preserve perf counter setup */
	uasm_i_mfc0(pp, t2, 25, (perf_counter * 2) + 0); /* PerfCtlN */
	uasm_i_mfc0(pp, t3, 25, (perf_counter * 2) + 1); /* PerfCntN */

	/* Setup perf counter to count FSB full pipeline stalls */
	uasm_i_addiu(pp, t0, zero, (perf_event << 5) | 0xf);
	uasm_i_mtc0(pp, t0, 25, (perf_counter * 2) + 0); /* PerfCtlN */
	uasm_i_ehb(pp);
	uasm_i_mtc0(pp, zero, 25, (perf_counter * 2) + 1); /* PerfCntN */
	uasm_i_ehb(pp);

	/* Base address for loads */
	UASM_i_LA(pp, t0, (long)CKSEG0);

	/* Start of clear loop */
	uasm_build_label(pl, *pp, lbl);

	/* Perform some loads to fill the FSB */
	for (i = 0; i < num_loads; i++)
		uasm_i_lw(pp, zero, i * line_size * line_stride, t0);

	/*
	 * Invalidate the new D-cache entries so that the cache will need
	 * refilling (via the FSB) if the loop is executed again.
	 */
	for (i = 0; i < num_loads; i++) {
		uasm_i_cache(pp, Hit_Invalidate_D,
			     i * line_size * line_stride, t0);
		uasm_i_cache(pp, Hit_Writeback_Inv_SD,
			     i * line_size * line_stride, t0);
	}

	/* Barrier ensuring previous cache invalidates are complete */
	uasm_i_sync(pp, STYPE_SYNC);
	uasm_i_ehb(pp);

	/* Check whether the pipeline stalled due to the FSB being full */
	uasm_i_mfc0(pp, t1, 25, (perf_counter * 2) + 1); /* PerfCntN */

	/* Loop if it didn't */
	uasm_il_beqz(pp, pr, t1, lbl);
	uasm_i_nop(pp);

	/* Restore perf counter 1. The count may well now be wrong... */
	uasm_i_mtc0(pp, t2, 25, (perf_counter * 2) + 0); /* PerfCtlN */
	uasm_i_ehb(pp);
	uasm_i_mtc0(pp, t3, 25, (perf_counter * 2) + 1); /* PerfCntN */
	uasm_i_ehb(pp);

	return 0;
}

static void cps_gen_set_top_bit(u32 **pp, struct uasm_label **pl,
				struct uasm_reloc **pr,
				unsigned r_addr, int lbl)
{
	uasm_i_lui(pp, t0, uasm_rel_hi(0x80000000));
	uasm_build_label(pl, *pp, lbl);
	uasm_i_ll(pp, t1, 0, r_addr);
	uasm_i_or(pp, t1, t1, t0);
	uasm_i_sc(pp, t1, 0, r_addr);
	uasm_il_beqz(pp, pr, t1, lbl);
	uasm_i_nop(pp);
}

static void *cps_gen_entry_code(unsigned cpu, enum cps_pm_state state)
{
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
	u32 *buf, *p;
	const unsigned r_online = a0;
	const unsigned r_nc_count = a1;
	const unsigned r_pcohctl = t7;
	const unsigned max_instrs = 256;
	unsigned cpc_cmd;
	int err;
	enum {
		lbl_incready = 1,
		lbl_poll_cont,
		lbl_secondary_hang,
		lbl_disable_coherence,
		lbl_flush_fsb,
		lbl_invicache,
		lbl_flushdcache,
		lbl_hang,
		lbl_set_cont,
		lbl_secondary_cont,
		lbl_decready,
	};

	/* Allocate a buffer to hold the generated code */
	p = buf = kcalloc(max_instrs, sizeof(u32), GFP_KERNEL);
	if (!buf)
		return NULL;

	/* Clear labels & relocs ready for (re)use */
	memset(labels, 0, sizeof(labels));
	memset(relocs, 0, sizeof(relocs));

	if (IS_ENABLED(CONFIG_CPU_PM) && state == CPS_PM_POWER_GATED) {
		/* Power gating relies upon CPS SMP */
		if (!mips_cps_smp_in_use())
			goto out_err;

		/*
		 * Save CPU state. Note the non-standard calling convention
		 * with the return address placed in v0 to avoid clobbering
		 * the ra register before it is saved.
		 */
		UASM_i_LA(&p, t0, (long)mips_cps_pm_save);
		uasm_i_jalr(&p, v0, t0);
		uasm_i_nop(&p);
	}

	/*
	 * Load addresses of required CM & CPC registers. This is done early
	 * because they're needed in both the enable & disable coherence steps
	 * but in the coupled case the enable step will only run on one VPE.
	 */
	UASM_i_LA(&p, r_pcohctl, (long)addr_gcr_cl_coherence());

	if (coupled_coherence) {
		/* Increment ready_count */
		uasm_i_sync(&p, STYPE_SYNC_MB);
		uasm_build_label(&l, p, lbl_incready);
		uasm_i_ll(&p, t1, 0, r_nc_count);
		uasm_i_addiu(&p, t2, t1, 1);
		uasm_i_sc(&p, t2, 0, r_nc_count);
		uasm_il_beqz(&p, &r, t2, lbl_incready);
		uasm_i_addiu(&p, t1, t1, 1);

		/* Barrier ensuring all CPUs see the updated r_nc_count value */
		uasm_i_sync(&p, STYPE_SYNC_MB);

		/*
		 * If this is the last VPE to become ready for non-coherence
		 * then it should branch below.
		 */
		uasm_il_beq(&p, &r, t1, r_online, lbl_disable_coherence);
		uasm_i_nop(&p);

		if (state < CPS_PM_POWER_GATED) {
			/*
			 * Otherwise this is not the last VPE to become ready
			 * for non-coherence. It needs to wait until coherence
			 * has been disabled before proceeding, which it will do
			 * by polling for the top bit of ready_count being set.
			 */
			uasm_i_addiu(&p, t1, zero, -1);
			uasm_build_label(&l, p, lbl_poll_cont);
			uasm_i_lw(&p, t0, 0, r_nc_count);
			uasm_il_bltz(&p, &r, t0, lbl_secondary_cont);
			uasm_i_ehb(&p);
			if (cpu_has_mipsmt)
				uasm_i_yield(&p, zero, t1);
			uasm_il_b(&p, &r, lbl_poll_cont);
			uasm_i_nop(&p);
		} else {
			/*
			 * The core will lose power & this VPE will not continue
			 * so it can simply halt here.
			 */
			if (cpu_has_mipsmt) {
				/* Halt the VPE via C0 tchalt register */
				uasm_i_addiu(&p, t0, zero, TCHALT_H);
				uasm_i_mtc0(&p, t0, 2, 4);
			} else if (cpu_has_vp) {
				/* Halt the VP via the CPC VP_STOP register */
				unsigned int vpe_id;

				vpe_id = cpu_vpe_id(&cpu_data[cpu]);
				uasm_i_addiu(&p, t0, zero, 1 << vpe_id);
				UASM_i_LA(&p, t1, (long)addr_cpc_cl_vp_stop());
				uasm_i_sw(&p, t0, 0, t1);
			} else {
				BUG();
			}
			uasm_build_label(&l, p, lbl_secondary_hang);
			uasm_il_b(&p, &r, lbl_secondary_hang);
			uasm_i_nop(&p);
		}
	}

	/*
	 * This is the point of no return - this VPE will now proceed to
	 * disable coherence. At this point we *must* be sure that no other
	 * VPE within the core will interfere with the L1 dcache.
	 */
	uasm_build_label(&l, p, lbl_disable_coherence);

	/* Invalidate the L1 icache */
	cps_gen_cache_routine(&p, &l, &r, &cpu_data[cpu].icache,
			      Index_Invalidate_I, lbl_invicache);

	/* Writeback & invalidate the L1 dcache */
	cps_gen_cache_routine(&p, &l, &r, &cpu_data[cpu].dcache,
			      Index_Writeback_Inv_D, lbl_flushdcache);

	/* Barrier ensuring previous cache invalidates are complete */
	uasm_i_sync(&p, STYPE_SYNC);
	uasm_i_ehb(&p);

	if (mips_cm_revision() < CM_REV_CM3) {
		/*
		* Disable all but self interventions. The load from COHCTL is
		* defined by the interAptiv & proAptiv SUMs as ensuring that the
		*  operation resulting from the preceding store is complete.
		*/
		uasm_i_addiu(&p, t0, zero, 1 << cpu_core(&cpu_data[cpu]));
		uasm_i_sw(&p, t0, 0, r_pcohctl);
		uasm_i_lw(&p, t0, 0, r_pcohctl);

		/* Barrier to ensure write to coherence control is complete */
		uasm_i_sync(&p, STYPE_SYNC);
		uasm_i_ehb(&p);
	}

	/* Disable coherence */
	uasm_i_sw(&p, zero, 0, r_pcohctl);
	uasm_i_lw(&p, t0, 0, r_pcohctl);

	if (state >= CPS_PM_CLOCK_GATED) {
		err = cps_gen_flush_fsb(&p, &l, &r, &cpu_data[cpu],
					lbl_flush_fsb);
		if (err)
			goto out_err;

		/* Determine the CPC command to issue */
		switch (state) {
		case CPS_PM_CLOCK_GATED:
			cpc_cmd = CPC_Cx_CMD_CLOCKOFF;
			break;
		case CPS_PM_POWER_GATED:
			cpc_cmd = CPC_Cx_CMD_PWRDOWN;
			break;
		default:
			BUG();
			goto out_err;
		}

		/* Issue the CPC command */
		UASM_i_LA(&p, t0, (long)addr_cpc_cl_cmd());
		uasm_i_addiu(&p, t1, zero, cpc_cmd);
		uasm_i_sw(&p, t1, 0, t0);

		if (state == CPS_PM_POWER_GATED) {
			/* If anything goes wrong just hang */
			uasm_build_label(&l, p, lbl_hang);
			uasm_il_b(&p, &r, lbl_hang);
			uasm_i_nop(&p);

			/*
			 * There's no point generating more code, the core is
			 * powered down & if powered back up will run from the
			 * reset vector not from here.
			 */
			goto gen_done;
		}

		/* Barrier to ensure write to CPC command is complete */
		uasm_i_sync(&p, STYPE_SYNC);
		uasm_i_ehb(&p);
	}

	if (state == CPS_PM_NC_WAIT) {
		/*
		 * At this point it is safe for all VPEs to proceed with
		 * execution. This VPE will set the top bit of ready_count
		 * to indicate to the other VPEs that they may continue.
		 */
		if (coupled_coherence)
			cps_gen_set_top_bit(&p, &l, &r, r_nc_count,
					    lbl_set_cont);

		/*
		 * VPEs which did not disable coherence will continue
		 * executing, after coherence has been disabled, from this
		 * point.
		 */
		uasm_build_label(&l, p, lbl_secondary_cont);

		/* Now perform our wait */
		uasm_i_wait(&p, 0);
	}

	/*
	 * Re-enable coherence. Note that for CPS_PM_NC_WAIT all coupled VPEs
	 * will run this. The first will actually re-enable coherence & the
	 * rest will just be performing a rather unusual nop.
	 */
	uasm_i_addiu(&p, t0, zero, mips_cm_revision() < CM_REV_CM3
				? CM_GCR_Cx_COHERENCE_COHDOMAINEN
				: CM3_GCR_Cx_COHERENCE_COHEN);

	uasm_i_sw(&p, t0, 0, r_pcohctl);
	uasm_i_lw(&p, t0, 0, r_pcohctl);

	/* Barrier to ensure write to coherence control is complete */
	uasm_i_sync(&p, STYPE_SYNC);
	uasm_i_ehb(&p);

	if (coupled_coherence && (state == CPS_PM_NC_WAIT)) {
		/* Decrement ready_count */
		uasm_build_label(&l, p, lbl_decready);
		uasm_i_sync(&p, STYPE_SYNC_MB);
		uasm_i_ll(&p, t1, 0, r_nc_count);
		uasm_i_addiu(&p, t2, t1, -1);
		uasm_i_sc(&p, t2, 0, r_nc_count);
		uasm_il_beqz(&p, &r, t2, lbl_decready);
		uasm_i_andi(&p, v0, t1, (1 << fls(smp_num_siblings)) - 1);

		/* Barrier ensuring all CPUs see the updated r_nc_count value */
		uasm_i_sync(&p, STYPE_SYNC_MB);
	}

	if (coupled_coherence && (state == CPS_PM_CLOCK_GATED)) {
		/*
		 * At this point it is safe for all VPEs to proceed with
		 * execution. This VPE will set the top bit of ready_count
		 * to indicate to the other VPEs that they may continue.
		 */
		cps_gen_set_top_bit(&p, &l, &r, r_nc_count, lbl_set_cont);

		/*
		 * This core will be reliant upon another core sending a
		 * power-up command to the CPC in order to resume operation.
		 * Thus an arbitrary VPE can't trigger the core leaving the
		 * idle state and the one that disables coherence might as well
		 * be the one to re-enable it. The rest will continue from here
		 * after that has been done.
		 */
		uasm_build_label(&l, p, lbl_secondary_cont);

		/* Barrier ensuring all CPUs see the updated r_nc_count value */
		uasm_i_sync(&p, STYPE_SYNC_MB);
	}

	/* The core is coherent, time to return to C code */
	uasm_i_jr(&p, ra);
	uasm_i_nop(&p);

gen_done:
	/* Ensure the code didn't exceed the resources allocated for it */
	BUG_ON((p - buf) > max_instrs);
	BUG_ON((l - labels) > ARRAY_SIZE(labels));
	BUG_ON((r - relocs) > ARRAY_SIZE(relocs));

	/* Patch branch offsets */
	uasm_resolve_relocs(relocs, labels);

	/* Flush the icache */
	local_flush_icache_range((unsigned long)buf, (unsigned long)p);

	return buf;
out_err:
	kfree(buf);
	return NULL;
}

static int cps_pm_online_cpu(unsigned int cpu)
{
	enum cps_pm_state state;
	unsigned core = cpu_core(&cpu_data[cpu]);
	void *entry_fn, *core_rc;

	for (state = CPS_PM_NC_WAIT; state < CPS_PM_STATE_COUNT; state++) {
		if (per_cpu(nc_asm_enter, core)[state])
			continue;
		if (!test_bit(state, state_support))
			continue;

		entry_fn = cps_gen_entry_code(cpu, state);
		if (!entry_fn) {
			pr_err("Failed to generate core %u state %u entry\n",
			       core, state);
			clear_bit(state, state_support);
		}

		per_cpu(nc_asm_enter, core)[state] = entry_fn;
	}

	if (!per_cpu(ready_count, core)) {
		core_rc = kmalloc(sizeof(u32), GFP_KERNEL);
		if (!core_rc) {
			pr_err("Failed allocate core %u ready_count\n", core);
			return -ENOMEM;
		}
		per_cpu(ready_count, core) = core_rc;
	}

	return 0;
}

static int __init cps_pm_init(void)
{
	/* A CM is required for all non-coherent states */
	if (!mips_cm_present()) {
		pr_warn("pm-cps: no CM, non-coherent states unavailable\n");
		return 0;
	}

	/*
	 * If interrupts were enabled whilst running a wait instruction on a
	 * non-coherent core then the VPE may end up processing interrupts
	 * whilst non-coherent. That would be bad.
	 */
	if (cpu_wait == r4k_wait_irqoff)
		set_bit(CPS_PM_NC_WAIT, state_support);
	else
		pr_warn("pm-cps: non-coherent wait unavailable\n");

	/* Detect whether a CPC is present */
	if (mips_cpc_present()) {
		/* Detect whether clock gating is implemented */
		if (read_cpc_cl_stat_conf() & CPC_Cx_STAT_CONF_CLKGAT_IMPL)
			set_bit(CPS_PM_CLOCK_GATED, state_support);
		else
			pr_warn("pm-cps: CPC does not support clock gating\n");

		/* Power gating is available with CPS SMP & any CPC */
		if (mips_cps_smp_in_use())
			set_bit(CPS_PM_POWER_GATED, state_support);
		else
			pr_warn("pm-cps: CPS SMP not in use, power gating unavailable\n");
	} else {
		pr_warn("pm-cps: no CPC, clock & power gating unavailable\n");
	}

	return cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "mips/cps_pm:online",
				 cps_pm_online_cpu, NULL);
}
arch_initcall(cps_pm_init);
