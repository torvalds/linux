// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV cpuidle code
 *
 * Copyright 2015 IBM Corp.
 */

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/cpu.h>

#include <asm/asm-prototypes.h>
#include <asm/firmware.h>
#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/cputhreads.h>
#include <asm/cpuidle.h>
#include <asm/code-patching.h>
#include <asm/smp.h>
#include <asm/runlatch.h>
#include <asm/dbell.h>

#include "powernv.h"
#include "subcore.h"

/* Power ISA 3.0 allows for stop states 0x0 - 0xF */
#define MAX_STOP_STATE	0xF

#define P9_STOP_SPR_MSR 2000
#define P9_STOP_SPR_PSSCR      855

static u32 supported_cpuidle_states;
struct pnv_idle_states_t *pnv_idle_states;
int nr_pnv_idle_states;

/*
 * The default stop state that will be used by ppc_md.power_save
 * function on platforms that support stop instruction.
 */
static u64 pnv_default_stop_val;
static u64 pnv_default_stop_mask;
static bool default_stop_found;

/*
 * First stop state levels when SPR and TB loss can occur.
 */
static u64 pnv_first_tb_loss_level = MAX_STOP_STATE + 1;
static u64 pnv_first_spr_loss_level = MAX_STOP_STATE + 1;

/*
 * psscr value and mask of the deepest stop idle state.
 * Used when a cpu is offlined.
 */
static u64 pnv_deepest_stop_psscr_val;
static u64 pnv_deepest_stop_psscr_mask;
static u64 pnv_deepest_stop_flag;
static bool deepest_stop_found;

static unsigned long power7_offline_type;

static int pnv_save_sprs_for_deep_states(void)
{
	int cpu;
	int rc;

	/*
	 * hid0, hid1, hid4, hid5, hmeer and lpcr values are symmetric across
	 * all cpus at boot. Get these reg values of current cpu and use the
	 * same across all cpus.
	 */
	uint64_t lpcr_val	= mfspr(SPRN_LPCR);
	uint64_t hid0_val	= mfspr(SPRN_HID0);
	uint64_t hid1_val	= mfspr(SPRN_HID1);
	uint64_t hid4_val	= mfspr(SPRN_HID4);
	uint64_t hid5_val	= mfspr(SPRN_HID5);
	uint64_t hmeer_val	= mfspr(SPRN_HMEER);
	uint64_t msr_val = MSR_IDLE;
	uint64_t psscr_val = pnv_deepest_stop_psscr_val;

	for_each_present_cpu(cpu) {
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

static bool power7_fastsleep_workaround_entry = true;
static bool power7_fastsleep_workaround_exit = true;

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
	 * 1. Disable the 'undo' workaround in fastsleep exit path
	 * 2. Sendi IPIs to all the cores which have at least one online thread
	 * 3. Disable the 'apply' workaround in fastsleep entry path
	 *
	 * There is no need to send ipi to cores which have all threads
	 * offlined, as last thread of the core entering fastsleep or deeper
	 * state would have applied workaround.
	 */
	power7_fastsleep_workaround_exit = false;

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

	power7_fastsleep_workaround_entry = false;

	fastsleep_workaround_applyonce = 1;

	return count;
fail:
	return -EIO;
}

static DEVICE_ATTR(fastsleep_workaround_applyonce, 0600,
			show_fastsleep_workaround_applyonce,
			store_fastsleep_workaround_applyonce);

static inline void atomic_start_thread_idle(void)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	int thread_nr = cpu_thread_in_core(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;

	clear_bit(thread_nr, state);
}

static inline void atomic_stop_thread_idle(void)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	int thread_nr = cpu_thread_in_core(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;

	set_bit(thread_nr, state);
}

static inline void atomic_lock_thread_idle(void)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;

	while (unlikely(test_and_set_bit_lock(NR_PNV_CORE_IDLE_LOCK_BIT, state)))
		barrier();
}

static inline void atomic_unlock_and_stop_thread_idle(void)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	unsigned long thread = 1UL << cpu_thread_in_core(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;
	u64 s = READ_ONCE(*state);
	u64 new, tmp;

	BUG_ON(!(s & PNV_CORE_IDLE_LOCK_BIT));
	BUG_ON(s & thread);

again:
	new = (s | thread) & ~PNV_CORE_IDLE_LOCK_BIT;
	tmp = cmpxchg(state, s, new);
	if (unlikely(tmp != s)) {
		s = tmp;
		goto again;
	}
}

static inline void atomic_unlock_thread_idle(void)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;

	BUG_ON(!test_bit(NR_PNV_CORE_IDLE_LOCK_BIT, state));
	clear_bit_unlock(NR_PNV_CORE_IDLE_LOCK_BIT, state);
}

/* P7 and P8 */
struct p7_sprs {
	/* per core */
	u64 tscr;
	u64 worc;

	/* per subcore */
	u64 sdr1;
	u64 rpr;

	/* per thread */
	u64 lpcr;
	u64 hfscr;
	u64 fscr;
	u64 purr;
	u64 spurr;
	u64 dscr;
	u64 wort;

	/* per thread SPRs that get lost in shallow states */
	u64 amr;
	u64 iamr;
	u64 amor;
	u64 uamor;
};

static unsigned long power7_idle_insn(unsigned long type)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;
	unsigned long thread = 1UL << cpu_thread_in_core(cpu);
	unsigned long core_thread_mask = (1UL << threads_per_core) - 1;
	unsigned long srr1;
	bool full_winkle;
	struct p7_sprs sprs = {}; /* avoid false use-uninitialised */
	bool sprs_saved = false;
	int rc;

	if (unlikely(type != PNV_THREAD_NAP)) {
		atomic_lock_thread_idle();

		BUG_ON(!(*state & thread));
		*state &= ~thread;

		if (power7_fastsleep_workaround_entry) {
			if ((*state & core_thread_mask) == 0) {
				rc = opal_config_cpu_idle_state(
						OPAL_CONFIG_IDLE_FASTSLEEP,
						OPAL_CONFIG_IDLE_APPLY);
				BUG_ON(rc);
			}
		}

		if (type == PNV_THREAD_WINKLE) {
			sprs.tscr	= mfspr(SPRN_TSCR);
			sprs.worc	= mfspr(SPRN_WORC);

			sprs.sdr1	= mfspr(SPRN_SDR1);
			sprs.rpr	= mfspr(SPRN_RPR);

			sprs.lpcr	= mfspr(SPRN_LPCR);
			if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
				sprs.hfscr	= mfspr(SPRN_HFSCR);
				sprs.fscr	= mfspr(SPRN_FSCR);
			}
			sprs.purr	= mfspr(SPRN_PURR);
			sprs.spurr	= mfspr(SPRN_SPURR);
			sprs.dscr	= mfspr(SPRN_DSCR);
			sprs.wort	= mfspr(SPRN_WORT);

			sprs_saved = true;

			/*
			 * Increment winkle counter and set all winkle bits if
			 * all threads are winkling. This allows wakeup side to
			 * distinguish between fast sleep and winkle state
			 * loss. Fast sleep still has to resync the timebase so
			 * this may not be a really big win.
			 */
			*state += 1 << PNV_CORE_IDLE_WINKLE_COUNT_SHIFT;
			if ((*state & PNV_CORE_IDLE_WINKLE_COUNT_BITS)
					>> PNV_CORE_IDLE_WINKLE_COUNT_SHIFT
					== threads_per_core)
				*state |= PNV_CORE_IDLE_THREAD_WINKLE_BITS;
			WARN_ON((*state & PNV_CORE_IDLE_WINKLE_COUNT_BITS) == 0);
		}

		atomic_unlock_thread_idle();
	}

	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		sprs.amr	= mfspr(SPRN_AMR);
		sprs.iamr	= mfspr(SPRN_IAMR);
		sprs.amor	= mfspr(SPRN_AMOR);
		sprs.uamor	= mfspr(SPRN_UAMOR);
	}

	local_paca->thread_idle_state = type;
	srr1 = isa206_idle_insn_mayloss(type);		/* go idle */
	local_paca->thread_idle_state = PNV_THREAD_RUNNING;

	WARN_ON_ONCE(!srr1);
	WARN_ON_ONCE(mfmsr() & (MSR_IR|MSR_DR));

	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		if ((srr1 & SRR1_WAKESTATE) != SRR1_WS_NOLOSS) {
			/*
			 * We don't need an isync after the mtsprs here because
			 * the upcoming mtmsrd is execution synchronizing.
			 */
			mtspr(SPRN_AMR,		sprs.amr);
			mtspr(SPRN_IAMR,	sprs.iamr);
			mtspr(SPRN_AMOR,	sprs.amor);
			mtspr(SPRN_UAMOR,	sprs.uamor);
		}
	}

	if (unlikely((srr1 & SRR1_WAKEMASK_P8) == SRR1_WAKEHMI))
		hmi_exception_realmode(NULL);

	if (likely((srr1 & SRR1_WAKESTATE) != SRR1_WS_HVLOSS)) {
		if (unlikely(type != PNV_THREAD_NAP)) {
			atomic_lock_thread_idle();
			if (type == PNV_THREAD_WINKLE) {
				WARN_ON((*state & PNV_CORE_IDLE_WINKLE_COUNT_BITS) == 0);
				*state -= 1 << PNV_CORE_IDLE_WINKLE_COUNT_SHIFT;
				*state &= ~(thread << PNV_CORE_IDLE_THREAD_WINKLE_BITS_SHIFT);
			}
			atomic_unlock_and_stop_thread_idle();
		}
		return srr1;
	}

	/* HV state loss */
	BUG_ON(type == PNV_THREAD_NAP);

	atomic_lock_thread_idle();

	full_winkle = false;
	if (type == PNV_THREAD_WINKLE) {
		WARN_ON((*state & PNV_CORE_IDLE_WINKLE_COUNT_BITS) == 0);
		*state -= 1 << PNV_CORE_IDLE_WINKLE_COUNT_SHIFT;
		if (*state & (thread << PNV_CORE_IDLE_THREAD_WINKLE_BITS_SHIFT)) {
			*state &= ~(thread << PNV_CORE_IDLE_THREAD_WINKLE_BITS_SHIFT);
			full_winkle = true;
			BUG_ON(!sprs_saved);
		}
	}

	WARN_ON(*state & thread);

	if ((*state & core_thread_mask) != 0)
		goto core_woken;

	/* Per-core SPRs */
	if (full_winkle) {
		mtspr(SPRN_TSCR,	sprs.tscr);
		mtspr(SPRN_WORC,	sprs.worc);
	}

	if (power7_fastsleep_workaround_exit) {
		rc = opal_config_cpu_idle_state(OPAL_CONFIG_IDLE_FASTSLEEP,
						OPAL_CONFIG_IDLE_UNDO);
		BUG_ON(rc);
	}

	/* TB */
	if (opal_resync_timebase() != OPAL_SUCCESS)
		BUG();

core_woken:
	if (!full_winkle)
		goto subcore_woken;

	if ((*state & local_paca->subcore_sibling_mask) != 0)
		goto subcore_woken;

	/* Per-subcore SPRs */
	mtspr(SPRN_SDR1,	sprs.sdr1);
	mtspr(SPRN_RPR,		sprs.rpr);

subcore_woken:
	/*
	 * isync after restoring shared SPRs and before unlocking. Unlock
	 * only contains hwsync which does not necessarily do the right
	 * thing for SPRs.
	 */
	isync();
	atomic_unlock_and_stop_thread_idle();

	/* Fast sleep does not lose SPRs */
	if (!full_winkle)
		return srr1;

	/* Per-thread SPRs */
	mtspr(SPRN_LPCR,	sprs.lpcr);
	if (cpu_has_feature(CPU_FTR_ARCH_207S)) {
		mtspr(SPRN_HFSCR,	sprs.hfscr);
		mtspr(SPRN_FSCR,	sprs.fscr);
	}
	mtspr(SPRN_PURR,	sprs.purr);
	mtspr(SPRN_SPURR,	sprs.spurr);
	mtspr(SPRN_DSCR,	sprs.dscr);
	mtspr(SPRN_WORT,	sprs.wort);

	mtspr(SPRN_SPRG3,	local_paca->sprg_vdso);

	/*
	 * The SLB has to be restored here, but it sometimes still
	 * contains entries, so the __ variant must be used to prevent
	 * multi hits.
	 */
	__slb_restore_bolted_realmode();

	return srr1;
}

extern unsigned long idle_kvm_start_guest(unsigned long srr1);

#ifdef CONFIG_HOTPLUG_CPU
static unsigned long power7_offline(void)
{
	unsigned long srr1;

	mtmsr(MSR_IDLE);

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	/* Tell KVM we're entering idle. */
	/******************************************************/
	/*  N O T E   W E L L    ! ! !    N O T E   W E L L   */
	/* The following store to HSTATE_HWTHREAD_STATE(r13)  */
	/* MUST occur in real mode, i.e. with the MMU off,    */
	/* and the MMU must stay off until we clear this flag */
	/* and test HSTATE_HWTHREAD_REQ(r13) in               */
	/* pnv_powersave_wakeup in this file.                 */
	/* The reason is that another thread can switch the   */
	/* MMU to a guest context whenever this flag is set   */
	/* to KVM_HWTHREAD_IN_IDLE, and if the MMU was on,    */
	/* that would potentially cause this thread to start  */
	/* executing instructions from guest memory in        */
	/* hypervisor mode, leading to a host crash or data   */
	/* corruption, or worse.                              */
	/******************************************************/
	local_paca->kvm_hstate.hwthread_state = KVM_HWTHREAD_IN_IDLE;
#endif

	__ppc64_runlatch_off();
	srr1 = power7_idle_insn(power7_offline_type);
	__ppc64_runlatch_on();

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	local_paca->kvm_hstate.hwthread_state = KVM_HWTHREAD_IN_KERNEL;
	/* Order setting hwthread_state vs. testing hwthread_req */
	smp_mb();
	if (local_paca->kvm_hstate.hwthread_req)
		srr1 = idle_kvm_start_guest(srr1);
#endif

	mtmsr(MSR_KERNEL);

	return srr1;
}
#endif

void power7_idle_type(unsigned long type)
{
	unsigned long srr1;

	if (!prep_irq_for_idle_irqsoff())
		return;

	mtmsr(MSR_IDLE);
	__ppc64_runlatch_off();
	srr1 = power7_idle_insn(type);
	__ppc64_runlatch_on();
	mtmsr(MSR_KERNEL);

	fini_irq_for_idle_irqsoff();
	irq_set_pending_from_srr1(srr1);
}

void power7_idle(void)
{
	if (!powersave_nap)
		return;

	power7_idle_type(PNV_THREAD_NAP);
}

struct p9_sprs {
	/* per core */
	u64 ptcr;
	u64 rpr;
	u64 tscr;
	u64 ldbar;

	/* per thread */
	u64 lpcr;
	u64 hfscr;
	u64 fscr;
	u64 pid;
	u64 purr;
	u64 spurr;
	u64 dscr;
	u64 wort;

	u64 mmcra;
	u32 mmcr0;
	u32 mmcr1;
	u64 mmcr2;

	/* per thread SPRs that get lost in shallow states */
	u64 amr;
	u64 iamr;
	u64 amor;
	u64 uamor;
};

static unsigned long power9_idle_stop(unsigned long psscr, bool mmu_on)
{
	int cpu = raw_smp_processor_id();
	int first = cpu_first_thread_sibling(cpu);
	unsigned long *state = &paca_ptrs[first]->idle_state;
	unsigned long core_thread_mask = (1UL << threads_per_core) - 1;
	unsigned long srr1;
	unsigned long pls;
	unsigned long mmcr0 = 0;
	struct p9_sprs sprs = {}; /* avoid false used-uninitialised */
	bool sprs_saved = false;

	if (!(psscr & (PSSCR_EC|PSSCR_ESL))) {
		/* EC=ESL=0 case */

		BUG_ON(!mmu_on);

		/*
		 * Wake synchronously. SRESET via xscom may still cause
		 * a 0x100 powersave wakeup with SRR1 reason!
		 */
		srr1 = isa300_idle_stop_noloss(psscr);		/* go idle */
		if (likely(!srr1))
			return 0;

		/*
		 * Registers not saved, can't recover!
		 * This would be a hardware bug
		 */
		BUG_ON((srr1 & SRR1_WAKESTATE) != SRR1_WS_NOLOSS);

		goto out;
	}

	/* EC=ESL=1 case */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	if (cpu_has_feature(CPU_FTR_P9_TM_XER_SO_BUG)) {
		local_paca->requested_psscr = psscr;
		/* order setting requested_psscr vs testing dont_stop */
		smp_mb();
		if (atomic_read(&local_paca->dont_stop)) {
			local_paca->requested_psscr = 0;
			return 0;
		}
	}
#endif

	if (!cpu_has_feature(CPU_FTR_POWER9_DD2_1)) {
		 /*
		  * POWER9 DD2 can incorrectly set PMAO when waking up
		  * after a state-loss idle. Saving and restoring MMCR0
		  * over idle is a workaround.
		  */
		mmcr0		= mfspr(SPRN_MMCR0);
	}
	if ((psscr & PSSCR_RL_MASK) >= pnv_first_spr_loss_level) {
		sprs.lpcr	= mfspr(SPRN_LPCR);
		sprs.hfscr	= mfspr(SPRN_HFSCR);
		sprs.fscr	= mfspr(SPRN_FSCR);
		sprs.pid	= mfspr(SPRN_PID);
		sprs.purr	= mfspr(SPRN_PURR);
		sprs.spurr	= mfspr(SPRN_SPURR);
		sprs.dscr	= mfspr(SPRN_DSCR);
		sprs.wort	= mfspr(SPRN_WORT);

		sprs.mmcra	= mfspr(SPRN_MMCRA);
		sprs.mmcr0	= mfspr(SPRN_MMCR0);
		sprs.mmcr1	= mfspr(SPRN_MMCR1);
		sprs.mmcr2	= mfspr(SPRN_MMCR2);

		sprs.ptcr	= mfspr(SPRN_PTCR);
		sprs.rpr	= mfspr(SPRN_RPR);
		sprs.tscr	= mfspr(SPRN_TSCR);
		sprs.ldbar	= mfspr(SPRN_LDBAR);

		sprs_saved = true;

		atomic_start_thread_idle();
	}

	sprs.amr	= mfspr(SPRN_AMR);
	sprs.iamr	= mfspr(SPRN_IAMR);
	sprs.amor	= mfspr(SPRN_AMOR);
	sprs.uamor	= mfspr(SPRN_UAMOR);

	srr1 = isa300_idle_stop_mayloss(psscr);		/* go idle */

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	local_paca->requested_psscr = 0;
#endif

	psscr = mfspr(SPRN_PSSCR);

	WARN_ON_ONCE(!srr1);
	WARN_ON_ONCE(mfmsr() & (MSR_IR|MSR_DR));

	if ((srr1 & SRR1_WAKESTATE) != SRR1_WS_NOLOSS) {
		unsigned long mmcra;

		/*
		 * We don't need an isync after the mtsprs here because the
		 * upcoming mtmsrd is execution synchronizing.
		 */
		mtspr(SPRN_AMR,		sprs.amr);
		mtspr(SPRN_IAMR,	sprs.iamr);
		mtspr(SPRN_AMOR,	sprs.amor);
		mtspr(SPRN_UAMOR,	sprs.uamor);

		/*
		 * Workaround for POWER9 DD2.0, if we lost resources, the ERAT
		 * might have been corrupted and needs flushing. We also need
		 * to reload MMCR0 (see mmcr0 comment above).
		 */
		if (!cpu_has_feature(CPU_FTR_POWER9_DD2_1)) {
			asm volatile(PPC_ISA_3_0_INVALIDATE_ERAT);
			mtspr(SPRN_MMCR0, mmcr0);
		}

		/*
		 * DD2.2 and earlier need to set then clear bit 60 in MMCRA
		 * to ensure the PMU starts running.
		 */
		mmcra = mfspr(SPRN_MMCRA);
		mmcra |= PPC_BIT(60);
		mtspr(SPRN_MMCRA, mmcra);
		mmcra &= ~PPC_BIT(60);
		mtspr(SPRN_MMCRA, mmcra);
	}

	if (unlikely((srr1 & SRR1_WAKEMASK_P8) == SRR1_WAKEHMI))
		hmi_exception_realmode(NULL);

	/*
	 * On POWER9, SRR1 bits do not match exactly as expected.
	 * SRR1_WS_GPRLOSS (10b) can also result in SPR loss, so
	 * just always test PSSCR for SPR/TB state loss.
	 */
	pls = (psscr & PSSCR_PLS) >> PSSCR_PLS_SHIFT;
	if (likely(pls < pnv_first_spr_loss_level)) {
		if (sprs_saved)
			atomic_stop_thread_idle();
		goto out;
	}

	/* HV state loss */
	BUG_ON(!sprs_saved);

	atomic_lock_thread_idle();

	if ((*state & core_thread_mask) != 0)
		goto core_woken;

	/* Per-core SPRs */
	mtspr(SPRN_PTCR,	sprs.ptcr);
	mtspr(SPRN_RPR,		sprs.rpr);
	mtspr(SPRN_TSCR,	sprs.tscr);

	if (pls >= pnv_first_tb_loss_level) {
		/* TB loss */
		if (opal_resync_timebase() != OPAL_SUCCESS)
			BUG();
	}

	/*
	 * isync after restoring shared SPRs and before unlocking. Unlock
	 * only contains hwsync which does not necessarily do the right
	 * thing for SPRs.
	 */
	isync();

core_woken:
	atomic_unlock_and_stop_thread_idle();

	/* Per-thread SPRs */
	mtspr(SPRN_LPCR,	sprs.lpcr);
	mtspr(SPRN_HFSCR,	sprs.hfscr);
	mtspr(SPRN_FSCR,	sprs.fscr);
	mtspr(SPRN_PID,		sprs.pid);
	mtspr(SPRN_PURR,	sprs.purr);
	mtspr(SPRN_SPURR,	sprs.spurr);
	mtspr(SPRN_DSCR,	sprs.dscr);
	mtspr(SPRN_WORT,	sprs.wort);

	mtspr(SPRN_MMCRA,	sprs.mmcra);
	mtspr(SPRN_MMCR0,	sprs.mmcr0);
	mtspr(SPRN_MMCR1,	sprs.mmcr1);
	mtspr(SPRN_MMCR2,	sprs.mmcr2);
	mtspr(SPRN_LDBAR,	sprs.ldbar);

	mtspr(SPRN_SPRG3,	local_paca->sprg_vdso);

	if (!radix_enabled())
		__slb_restore_bolted_realmode();

out:
	if (mmu_on)
		mtmsr(MSR_KERNEL);

	return srr1;
}

#ifdef CONFIG_HOTPLUG_CPU
static unsigned long power9_offline_stop(unsigned long psscr)
{
	unsigned long srr1;

#ifndef CONFIG_KVM_BOOK3S_HV_POSSIBLE
	__ppc64_runlatch_off();
	srr1 = power9_idle_stop(psscr, true);
	__ppc64_runlatch_on();
#else
	/*
	 * Tell KVM we're entering idle.
	 * This does not have to be done in real mode because the P9 MMU
	 * is independent per-thread. Some steppings share radix/hash mode
	 * between threads, but in that case KVM has a barrier sync in real
	 * mode before and after switching between radix and hash.
	 *
	 * kvm_start_guest must still be called in real mode though, hence
	 * the false argument.
	 */
	local_paca->kvm_hstate.hwthread_state = KVM_HWTHREAD_IN_IDLE;

	__ppc64_runlatch_off();
	srr1 = power9_idle_stop(psscr, false);
	__ppc64_runlatch_on();

	local_paca->kvm_hstate.hwthread_state = KVM_HWTHREAD_IN_KERNEL;
	/* Order setting hwthread_state vs. testing hwthread_req */
	smp_mb();
	if (local_paca->kvm_hstate.hwthread_req)
		srr1 = idle_kvm_start_guest(srr1);
	mtmsr(MSR_KERNEL);
#endif

	return srr1;
}
#endif

void power9_idle_type(unsigned long stop_psscr_val,
				      unsigned long stop_psscr_mask)
{
	unsigned long psscr;
	unsigned long srr1;

	if (!prep_irq_for_idle_irqsoff())
		return;

	psscr = mfspr(SPRN_PSSCR);
	psscr = (psscr & ~stop_psscr_mask) | stop_psscr_val;

	__ppc64_runlatch_off();
	srr1 = power9_idle_stop(psscr, true);
	__ppc64_runlatch_on();

	fini_irq_for_idle_irqsoff();

	irq_set_pending_from_srr1(srr1);
}

/*
 * Used for ppc_md.power_save which needs a function with no parameters
 */
void power9_idle(void)
{
	power9_idle_type(pnv_default_stop_val, pnv_default_stop_mask);
}

#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
/*
 * This is used in working around bugs in thread reconfiguration
 * on POWER9 (at least up to Nimbus DD2.2) relating to transactional
 * memory and the way that XER[SO] is checkpointed.
 * This function forces the core into SMT4 in order by asking
 * all other threads not to stop, and sending a message to any
 * that are in a stop state.
 * Must be called with preemption disabled.
 */
void pnv_power9_force_smt4_catch(void)
{
	int cpu, cpu0, thr;
	int awake_threads = 1;		/* this thread is awake */
	int poke_threads = 0;
	int need_awake = threads_per_core;

	cpu = smp_processor_id();
	cpu0 = cpu & ~(threads_per_core - 1);
	for (thr = 0; thr < threads_per_core; ++thr) {
		if (cpu != cpu0 + thr)
			atomic_inc(&paca_ptrs[cpu0+thr]->dont_stop);
	}
	/* order setting dont_stop vs testing requested_psscr */
	smp_mb();
	for (thr = 0; thr < threads_per_core; ++thr) {
		if (!paca_ptrs[cpu0+thr]->requested_psscr)
			++awake_threads;
		else
			poke_threads |= (1 << thr);
	}

	/* If at least 3 threads are awake, the core is in SMT4 already */
	if (awake_threads < need_awake) {
		/* We have to wake some threads; we'll use msgsnd */
		for (thr = 0; thr < threads_per_core; ++thr) {
			if (poke_threads & (1 << thr)) {
				ppc_msgsnd_sync();
				ppc_msgsnd(PPC_DBELL_MSGTYPE, 0,
					   paca_ptrs[cpu0+thr]->hw_cpu_id);
			}
		}
		/* now spin until at least 3 threads are awake */
		do {
			for (thr = 0; thr < threads_per_core; ++thr) {
				if ((poke_threads & (1 << thr)) &&
				    !paca_ptrs[cpu0+thr]->requested_psscr) {
					++awake_threads;
					poke_threads &= ~(1 << thr);
				}
			}
		} while (awake_threads < need_awake);
	}
}
EXPORT_SYMBOL_GPL(pnv_power9_force_smt4_catch);

void pnv_power9_force_smt4_release(void)
{
	int cpu, cpu0, thr;

	cpu = smp_processor_id();
	cpu0 = cpu & ~(threads_per_core - 1);

	/* clear all the dont_stop flags */
	for (thr = 0; thr < threads_per_core; ++thr) {
		if (cpu != cpu0 + thr)
			atomic_dec(&paca_ptrs[cpu0+thr]->dont_stop);
	}
}
EXPORT_SYMBOL_GPL(pnv_power9_force_smt4_release);
#endif /* CONFIG_KVM_BOOK3S_HV_POSSIBLE */

#ifdef CONFIG_HOTPLUG_CPU

void pnv_program_cpu_hotplug_lpcr(unsigned int cpu, u64 lpcr_val)
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

	__ppc64_runlatch_off();

	if (cpu_has_feature(CPU_FTR_ARCH_300) && deepest_stop_found) {
		unsigned long psscr;

		psscr = mfspr(SPRN_PSSCR);
		psscr = (psscr & ~pnv_deepest_stop_psscr_mask) |
						pnv_deepest_stop_psscr_val;
		srr1 = power9_offline_stop(psscr);
	} else if (cpu_has_feature(CPU_FTR_ARCH_206) && power7_offline_type) {
		srr1 = power7_offline();
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
static void __init pnv_power9_idle_init(void)
{
	u64 max_residency_ns = 0;
	int i;

	/*
	 * pnv_deepest_stop_{val,mask} should be set to values corresponding to
	 * the deepest stop state.
	 *
	 * pnv_default_stop_{val,mask} should be set to values corresponding to
	 * the deepest loss-less (OPAL_PM_STOP_INST_FAST) stop state.
	 */
	pnv_first_tb_loss_level = MAX_STOP_STATE + 1;
	pnv_first_spr_loss_level = MAX_STOP_STATE + 1;
	for (i = 0; i < nr_pnv_idle_states; i++) {
		int err;
		struct pnv_idle_states_t *state = &pnv_idle_states[i];
		u64 psscr_rl = state->psscr_val & PSSCR_RL_MASK;

		if ((state->flags & OPAL_PM_TIMEBASE_STOP) &&
		     (pnv_first_tb_loss_level > psscr_rl))
			pnv_first_tb_loss_level = psscr_rl;

		if ((state->flags & OPAL_PM_LOSE_FULL_CONTEXT) &&
		     (pnv_first_spr_loss_level > psscr_rl))
			pnv_first_spr_loss_level = psscr_rl;

		/*
		 * The idle code does not deal with TB loss occurring
		 * in a shallower state than SPR loss, so force it to
		 * behave like SPRs are lost if TB is lost. POWER9 would
		 * never encouter this, but a POWER8 core would if it
		 * implemented the stop instruction. So this is for forward
		 * compatibility.
		 */
		if ((state->flags & OPAL_PM_TIMEBASE_STOP) &&
		     (pnv_first_spr_loss_level > psscr_rl))
			pnv_first_spr_loss_level = psscr_rl;

		err = validate_psscr_val_mask(&state->psscr_val,
					      &state->psscr_mask,
					      state->flags);
		if (err) {
			report_invalid_psscr_val(state->psscr_val, err);
			continue;
		}

		state->valid = true;

		if (max_residency_ns < state->residency_ns) {
			max_residency_ns = state->residency_ns;
			pnv_deepest_stop_psscr_val = state->psscr_val;
			pnv_deepest_stop_psscr_mask = state->psscr_mask;
			pnv_deepest_stop_flag = state->flags;
			deepest_stop_found = true;
		}

		if (!default_stop_found &&
		    (state->flags & OPAL_PM_STOP_INST_FAST)) {
			pnv_default_stop_val = state->psscr_val;
			pnv_default_stop_mask = state->psscr_mask;
			default_stop_found = true;
			WARN_ON(state->flags & OPAL_PM_LOSE_FULL_CONTEXT);
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

	pr_info("cpuidle-powernv: First stop level that may lose SPRs = 0x%llx\n",
		pnv_first_spr_loss_level);

	pr_info("cpuidle-powernv: First stop level that may lose timebase = 0x%llx\n",
		pnv_first_tb_loss_level);
}

static void __init pnv_disable_deep_states(void)
{
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
			pnv_deepest_stop_psscr_val = pnv_default_stop_val;
			pnv_deepest_stop_psscr_mask = pnv_default_stop_mask;
			pr_warn("cpuidle-powernv: Offlined CPUs will stop with psscr = 0x%016llx\n",
				pnv_deepest_stop_psscr_val);
		} else { /* Fallback to snooze loop for CPU-Hotplug */
			deepest_stop_found = false;
			pr_warn("cpuidle-powernv: Offlined CPUs will busy wait\n");
		}
	}
}

/*
 * Probe device tree for supported idle states
 */
static void __init pnv_probe_idle_states(void)
{
	int i;

	if (nr_pnv_idle_states < 0) {
		pr_warn("cpuidle-powernv: no idle states found in the DT\n");
		return;
	}

	if (cpu_has_feature(CPU_FTR_ARCH_300))
		pnv_power9_idle_init();

	for (i = 0; i < nr_pnv_idle_states; i++)
		supported_cpuidle_states |= pnv_idle_states[i].flags;
}

/*
 * This function parses device-tree and populates all the information
 * into pnv_idle_states structure. It also sets up nr_pnv_idle_states
 * which is the number of cpuidle states discovered through device-tree.
 */

static int pnv_parse_cpuidle_dt(void)
{
	struct device_node *np;
	int nr_idle_states, i;
	int rc = 0;
	u32 *temp_u32;
	u64 *temp_u64;
	const char **temp_string;

	np = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!np) {
		pr_warn("opal: PowerMgmt Node not found\n");
		return -ENODEV;
	}
	nr_idle_states = of_property_count_u32_elems(np,
						"ibm,cpu-idle-state-flags");

	pnv_idle_states = kcalloc(nr_idle_states, sizeof(*pnv_idle_states),
				  GFP_KERNEL);
	temp_u32 = kcalloc(nr_idle_states, sizeof(u32),  GFP_KERNEL);
	temp_u64 = kcalloc(nr_idle_states, sizeof(u64),  GFP_KERNEL);
	temp_string = kcalloc(nr_idle_states, sizeof(char *),  GFP_KERNEL);

	if (!(pnv_idle_states && temp_u32 && temp_u64 && temp_string)) {
		pr_err("Could not allocate memory for dt parsing\n");
		rc = -ENOMEM;
		goto out;
	}

	/* Read flags */
	if (of_property_read_u32_array(np, "ibm,cpu-idle-state-flags",
				       temp_u32, nr_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-flags in DT\n");
		rc = -EINVAL;
		goto out;
	}
	for (i = 0; i < nr_idle_states; i++)
		pnv_idle_states[i].flags = temp_u32[i];

	/* Read latencies */
	if (of_property_read_u32_array(np, "ibm,cpu-idle-state-latencies-ns",
				       temp_u32, nr_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-latencies-ns in DT\n");
		rc = -EINVAL;
		goto out;
	}
	for (i = 0; i < nr_idle_states; i++)
		pnv_idle_states[i].latency_ns = temp_u32[i];

	/* Read residencies */
	if (of_property_read_u32_array(np, "ibm,cpu-idle-state-residency-ns",
				       temp_u32, nr_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-latencies-ns in DT\n");
		rc = -EINVAL;
		goto out;
	}
	for (i = 0; i < nr_idle_states; i++)
		pnv_idle_states[i].residency_ns = temp_u32[i];

	/* For power9 */
	if (cpu_has_feature(CPU_FTR_ARCH_300)) {
		/* Read pm_crtl_val */
		if (of_property_read_u64_array(np, "ibm,cpu-idle-state-psscr",
					       temp_u64, nr_idle_states)) {
			pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-psscr in DT\n");
			rc = -EINVAL;
			goto out;
		}
		for (i = 0; i < nr_idle_states; i++)
			pnv_idle_states[i].psscr_val = temp_u64[i];

		/* Read pm_crtl_mask */
		if (of_property_read_u64_array(np, "ibm,cpu-idle-state-psscr-mask",
					       temp_u64, nr_idle_states)) {
			pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-psscr-mask in DT\n");
			rc = -EINVAL;
			goto out;
		}
		for (i = 0; i < nr_idle_states; i++)
			pnv_idle_states[i].psscr_mask = temp_u64[i];
	}

	/*
	 * power8 specific properties ibm,cpu-idle-state-pmicr-mask and
	 * ibm,cpu-idle-state-pmicr-val were never used and there is no
	 * plan to use it in near future. Hence, not parsing these properties
	 */

	if (of_property_read_string_array(np, "ibm,cpu-idle-state-names",
					  temp_string, nr_idle_states) < 0) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-names in DT\n");
		rc = -EINVAL;
		goto out;
	}
	for (i = 0; i < nr_idle_states; i++)
		strlcpy(pnv_idle_states[i].name, temp_string[i],
			PNV_IDLE_NAME_LEN);
	nr_pnv_idle_states = nr_idle_states;
	rc = 0;
out:
	kfree(temp_u32);
	kfree(temp_u64);
	kfree(temp_string);
	return rc;
}

static int __init pnv_init_idle_states(void)
{
	int cpu;
	int rc = 0;

	/* Set up PACA fields */
	for_each_present_cpu(cpu) {
		struct paca_struct *p = paca_ptrs[cpu];

		p->idle_state = 0;
		if (cpu == cpu_first_thread_sibling(cpu))
			p->idle_state = (1 << threads_per_core) - 1;

		if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
			/* P7/P8 nap */
			p->thread_idle_state = PNV_THREAD_RUNNING;
		} else {
			/* P9 stop */
#ifdef CONFIG_KVM_BOOK3S_HV_POSSIBLE
			p->requested_psscr = 0;
			atomic_set(&p->dont_stop, 0);
#endif
		}
	}

	/* In case we error out nr_pnv_idle_states will be zero */
	nr_pnv_idle_states = 0;
	supported_cpuidle_states = 0;

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		goto out;
	rc = pnv_parse_cpuidle_dt();
	if (rc)
		return rc;
	pnv_probe_idle_states();

	if (!cpu_has_feature(CPU_FTR_ARCH_300)) {
		if (!(supported_cpuidle_states & OPAL_PM_SLEEP_ENABLED_ER1)) {
			power7_fastsleep_workaround_entry = false;
			power7_fastsleep_workaround_exit = false;
		} else {
			/*
			 * OPAL_PM_SLEEP_ENABLED_ER1 is set. It indicates that
			 * workaround is needed to use fastsleep. Provide sysfs
			 * control to choose how this workaround has to be
			 * applied.
			 */
			device_create_file(cpu_subsys.dev_root,
				&dev_attr_fastsleep_workaround_applyonce);
		}

		update_subcore_sibling_mask();

		if (supported_cpuidle_states & OPAL_PM_NAP_ENABLED) {
			ppc_md.power_save = power7_idle;
			power7_offline_type = PNV_THREAD_NAP;
		}

		if ((supported_cpuidle_states & OPAL_PM_WINKLE_ENABLED) &&
			   (supported_cpuidle_states & OPAL_PM_LOSE_FULL_CONTEXT))
			power7_offline_type = PNV_THREAD_WINKLE;
		else if ((supported_cpuidle_states & OPAL_PM_SLEEP_ENABLED) ||
			   (supported_cpuidle_states & OPAL_PM_SLEEP_ENABLED_ER1))
			power7_offline_type = PNV_THREAD_SLEEP;
	}

	if (supported_cpuidle_states & OPAL_PM_LOSE_FULL_CONTEXT) {
		if (pnv_save_sprs_for_deep_states())
			pnv_disable_deep_states();
	}

out:
	return 0;
}
machine_subsys_initcall(powernv, pnv_init_idle_states);
