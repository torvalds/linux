/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS: Support for hardware virtualization extensions
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Yann Le Du <ledu@kymasys.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/vmalloc.h>
#include <asm/cacheflush.h>
#include <asm/cacheops.h>
#include <asm/cmpxchg.h>
#include <asm/fpu.h>
#include <asm/hazards.h>
#include <asm/inst.h>
#include <asm/mmu_context.h>
#include <asm/r4kcache.h>
#include <asm/time.h>
#include <asm/tlb.h>
#include <asm/tlbex.h>

#include <linux/kvm_host.h>

#include "interrupt.h"
#ifdef CONFIG_CPU_LOONGSON64
#include "loongson_regs.h"
#endif

#include "trace.h"

/* Pointers to last VCPU loaded on each physical CPU */
static struct kvm_vcpu *last_vcpu[NR_CPUS];
/* Pointers to last VCPU executed on each physical CPU */
static struct kvm_vcpu *last_exec_vcpu[NR_CPUS];

/*
 * Number of guest VTLB entries to use, so we can catch inconsistency between
 * CPUs.
 */
static unsigned int kvm_vz_guest_vtlb_size;

static inline long kvm_vz_read_gc0_ebase(void)
{
	if (sizeof(long) == 8 && cpu_has_ebase_wg)
		return read_gc0_ebase_64();
	else
		return read_gc0_ebase();
}

static inline void kvm_vz_write_gc0_ebase(long v)
{
	/*
	 * First write with WG=1 to write upper bits, then write again in case
	 * WG should be left at 0.
	 * write_gc0_ebase_64() is no longer UNDEFINED since R6.
	 */
	if (sizeof(long) == 8 &&
	    (cpu_has_mips64r6 || cpu_has_ebase_wg)) {
		write_gc0_ebase_64(v | MIPS_EBASE_WG);
		write_gc0_ebase_64(v);
	} else {
		write_gc0_ebase(v | MIPS_EBASE_WG);
		write_gc0_ebase(v);
	}
}

/*
 * These Config bits may be writable by the guest:
 * Config:	[K23, KU] (!TLB), K0
 * Config1:	(none)
 * Config2:	[TU, SU] (impl)
 * Config3:	ISAOnExc
 * Config4:	FTLBPageSize
 * Config5:	K, CV, MSAEn, UFE, FRE, SBRI, UFR
 */

static inline unsigned int kvm_vz_config_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return CONF_CM_CMASK;
}

static inline unsigned int kvm_vz_config1_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline unsigned int kvm_vz_config2_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline unsigned int kvm_vz_config3_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return MIPS_CONF3_ISA_OE;
}

static inline unsigned int kvm_vz_config4_guest_wrmask(struct kvm_vcpu *vcpu)
{
	/* no need to be exact */
	return MIPS_CONF4_VFTLBPAGESIZE;
}

static inline unsigned int kvm_vz_config5_guest_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = MIPS_CONF5_K | MIPS_CONF5_CV | MIPS_CONF5_SBRI;

	/* Permit MSAEn changes if MSA supported and enabled */
	if (kvm_mips_guest_has_msa(&vcpu->arch))
		mask |= MIPS_CONF5_MSAEN;

	/*
	 * Permit guest FPU mode changes if FPU is enabled and the relevant
	 * feature exists according to FIR register.
	 */
	if (kvm_mips_guest_has_fpu(&vcpu->arch)) {
		if (cpu_has_ufr)
			mask |= MIPS_CONF5_UFR;
		if (cpu_has_fre)
			mask |= MIPS_CONF5_FRE | MIPS_CONF5_UFE;
	}

	return mask;
}

static inline unsigned int kvm_vz_config6_guest_wrmask(struct kvm_vcpu *vcpu)
{
	return LOONGSON_CONF6_INTIMER | LOONGSON_CONF6_EXTIMER;
}

/*
 * VZ optionally allows these additional Config bits to be written by root:
 * Config:	M, [MT]
 * Config1:	M, [MMUSize-1, C2, MD, PC, WR, CA], FP
 * Config2:	M
 * Config3:	M, MSAP, [BPG], ULRI, [DSP2P, DSPP], CTXTC, [ITL, LPA, VEIC,
 *		VInt, SP, CDMM, MT, SM, TL]
 * Config4:	M, [VTLBSizeExt, MMUSizeExt]
 * Config5:	MRP
 */

static inline unsigned int kvm_vz_config_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config1_user_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = kvm_vz_config1_guest_wrmask(vcpu) | MIPS_CONF_M;

	/* Permit FPU to be present if FPU is supported */
	if (kvm_mips_guest_can_have_fpu(&vcpu->arch))
		mask |= MIPS_CONF1_FP;

	return mask;
}

static inline unsigned int kvm_vz_config2_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config2_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config3_user_wrmask(struct kvm_vcpu *vcpu)
{
	unsigned int mask = kvm_vz_config3_guest_wrmask(vcpu) | MIPS_CONF_M |
		MIPS_CONF3_ULRI | MIPS_CONF3_CTXTC;

	/* Permit MSA to be present if MSA is supported */
	if (kvm_mips_guest_can_have_msa(&vcpu->arch))
		mask |= MIPS_CONF3_MSA;

	return mask;
}

static inline unsigned int kvm_vz_config4_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config4_guest_wrmask(vcpu) | MIPS_CONF_M;
}

static inline unsigned int kvm_vz_config5_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config5_guest_wrmask(vcpu) | MIPS_CONF5_MRP;
}

static inline unsigned int kvm_vz_config6_user_wrmask(struct kvm_vcpu *vcpu)
{
	return kvm_vz_config6_guest_wrmask(vcpu) |
		LOONGSON_CONF6_SFBEN | LOONGSON_CONF6_FTLBDIS;
}

static gpa_t kvm_vz_gva_to_gpa_cb(gva_t gva)
{
	/* VZ guest has already converted gva to gpa */
	return gva;
}

static void kvm_vz_queue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	set_bit(priority, &vcpu->arch.pending_exceptions);
	clear_bit(priority, &vcpu->arch.pending_exceptions_clr);
}

static void kvm_vz_dequeue_irq(struct kvm_vcpu *vcpu, unsigned int priority)
{
	clear_bit(priority, &vcpu->arch.pending_exceptions);
	set_bit(priority, &vcpu->arch.pending_exceptions_clr);
}

static void kvm_vz_queue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	/*
	 * timer expiry is asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

static void kvm_vz_dequeue_timer_int_cb(struct kvm_vcpu *vcpu)
{
	/*
	 * timer expiry is asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_dequeue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

static void kvm_vz_queue_io_int_cb(struct kvm_vcpu *vcpu,
				   struct kvm_mips_interrupt *irq)
{
	int intr = (int)irq->irq;

	/*
	 * interrupts are asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_queue_irq(vcpu, kvm_irq_to_priority(intr));
}

static void kvm_vz_dequeue_io_int_cb(struct kvm_vcpu *vcpu,
				     struct kvm_mips_interrupt *irq)
{
	int intr = (int)irq->irq;

	/*
	 * interrupts are asynchronous to vcpu execution therefore defer guest
	 * cp0 accesses
	 */
	kvm_vz_dequeue_irq(vcpu, kvm_irq_to_priority(-intr));
}

static int kvm_vz_irq_deliver_cb(struct kvm_vcpu *vcpu, unsigned int priority,
				 u32 cause)
{
	u32 irq = (priority < MIPS_EXC_MAX) ?
		kvm_priority_to_irq[priority] : 0;

	switch (priority) {
	case MIPS_EXC_INT_TIMER:
		set_gc0_cause(C_TI);
		break;

	case MIPS_EXC_INT_IO_1:
	case MIPS_EXC_INT_IO_2:
	case MIPS_EXC_INT_IPI_1:
	case MIPS_EXC_INT_IPI_2:
		if (cpu_has_guestctl2)
			set_c0_guestctl2(irq);
		else
			set_gc0_cause(irq);
		break;

	default:
		break;
	}

	clear_bit(priority, &vcpu->arch.pending_exceptions);
	return 1;
}

static int kvm_vz_irq_clear_cb(struct kvm_vcpu *vcpu, unsigned int priority,
			       u32 cause)
{
	u32 irq = (priority < MIPS_EXC_MAX) ?
		kvm_priority_to_irq[priority] : 0;

	switch (priority) {
	case MIPS_EXC_INT_TIMER:
		/*
		 * Explicitly clear irq associated with Cause.IP[IPTI]
		 * if GuestCtl2 virtual interrupt register not
		 * supported or if not using GuestCtl2 Hardware Clear.
		 */
		if (cpu_has_guestctl2) {
			if (!(read_c0_guestctl2() & (irq << 14)))
				clear_c0_guestctl2(irq);
		} else {
			clear_gc0_cause(irq);
		}
		break;

	case MIPS_EXC_INT_IO_1:
	case MIPS_EXC_INT_IO_2:
	case MIPS_EXC_INT_IPI_1:
	case MIPS_EXC_INT_IPI_2:
		/* Clear GuestCtl2.VIP irq if not using Hardware Clear */
		if (cpu_has_guestctl2) {
			if (!(read_c0_guestctl2() & (irq << 14)))
				clear_c0_guestctl2(irq);
		} else {
			clear_gc0_cause(irq);
		}
		break;

	default:
		break;
	}

	clear_bit(priority, &vcpu->arch.pending_exceptions_clr);
	return 1;
}

/*
 * VZ guest timer handling.
 */

/**
 * kvm_vz_should_use_htimer() - Find whether to use the VZ hard guest timer.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	true if the VZ GTOffset & real guest CP0_Count should be used
 *		instead of software emulation of guest timer.
 *		false otherwise.
 */
static bool kvm_vz_should_use_htimer(struct kvm_vcpu *vcpu)
{
	if (kvm_mips_count_disabled(vcpu))
		return false;

	/* Chosen frequency must match real frequency */
	if (mips_hpt_frequency != vcpu->arch.count_hz)
		return false;

	/* We don't support a CP0_GTOffset with fewer bits than CP0_Count */
	if (current_cpu_data.gtoffset_mask != 0xffffffff)
		return false;

	return true;
}

/**
 * _kvm_vz_restore_stimer() - Restore soft timer state.
 * @vcpu:	Virtual CPU.
 * @compare:	CP0_Compare register value, restored by caller.
 * @cause:	CP0_Cause register to restore.
 *
 * Restore VZ state relating to the soft timer. The hard timer can be enabled
 * later.
 */
static void _kvm_vz_restore_stimer(struct kvm_vcpu *vcpu, u32 compare,
				   u32 cause)
{
	/*
	 * Avoid spurious counter interrupts by setting Guest CP0_Count to just
	 * after Guest CP0_Compare.
	 */
	write_c0_gtoffset(compare - read_c0_count());

	back_to_back_c0_hazard();
	write_gc0_cause(cause);
}

/**
 * _kvm_vz_restore_htimer() - Restore hard timer state.
 * @vcpu:	Virtual CPU.
 * @compare:	CP0_Compare register value, restored by caller.
 * @cause:	CP0_Cause register to restore.
 *
 * Restore hard timer Guest.Count & Guest.Cause taking care to preserve the
 * value of Guest.CP0_Cause.TI while restoring Guest.CP0_Cause.
 */
static void _kvm_vz_restore_htimer(struct kvm_vcpu *vcpu,
				   u32 compare, u32 cause)
{
	u32 start_count, after_count;
	ktime_t freeze_time;
	unsigned long flags;

	/*
	 * Freeze the soft-timer and sync the guest CP0_Count with it. We do
	 * this with interrupts disabled to avoid latency.
	 */
	local_irq_save(flags);
	freeze_time = kvm_mips_freeze_hrtimer(vcpu, &start_count);
	write_c0_gtoffset(start_count - read_c0_count());
	local_irq_restore(flags);

	/* restore guest CP0_Cause, as TI may already be set */
	back_to_back_c0_hazard();
	write_gc0_cause(cause);

	/*
	 * The above sequence isn't atomic and would result in lost timer
	 * interrupts if we're not careful. Detect if a timer interrupt is due
	 * and assert it.
	 */
	back_to_back_c0_hazard();
	after_count = read_gc0_count();
	if (after_count - start_count > compare - start_count - 1)
		kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);
}

/**
 * kvm_vz_restore_timer() - Restore timer state.
 * @vcpu:	Virtual CPU.
 *
 * Restore soft timer state from saved context.
 */
static void kvm_vz_restore_timer(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 cause, compare;

	compare = kvm_read_sw_gc0_compare(cop0);
	cause = kvm_read_sw_gc0_cause(cop0);

	write_gc0_compare(compare);
	_kvm_vz_restore_stimer(vcpu, compare, cause);
}

/**
 * kvm_vz_acquire_htimer() - Switch to hard timer state.
 * @vcpu:	Virtual CPU.
 *
 * Restore hard timer state on top of existing soft timer state if possible.
 *
 * Since hard timer won't remain active over preemption, preemption should be
 * disabled by the caller.
 */
void kvm_vz_acquire_htimer(struct kvm_vcpu *vcpu)
{
	u32 gctl0;

	gctl0 = read_c0_guestctl0();
	if (!(gctl0 & MIPS_GCTL0_GT) && kvm_vz_should_use_htimer(vcpu)) {
		/* enable guest access to hard timer */
		write_c0_guestctl0(gctl0 | MIPS_GCTL0_GT);

		_kvm_vz_restore_htimer(vcpu, read_gc0_compare(),
				       read_gc0_cause());
	}
}

/**
 * _kvm_vz_save_htimer() - Switch to software emulation of guest timer.
 * @vcpu:	Virtual CPU.
 * @compare:	Pointer to write compare value to.
 * @cause:	Pointer to write cause value to.
 *
 * Save VZ guest timer state and switch to software emulation of guest CP0
 * timer. The hard timer must already be in use, so preemption should be
 * disabled.
 */
static void _kvm_vz_save_htimer(struct kvm_vcpu *vcpu,
				u32 *out_compare, u32 *out_cause)
{
	u32 cause, compare, before_count, end_count;
	ktime_t before_time;

	compare = read_gc0_compare();
	*out_compare = compare;

	before_time = ktime_get();

	/*
	 * Record the CP0_Count *prior* to saving CP0_Cause, so we have a time
	 * at which no pending timer interrupt is missing.
	 */
	before_count = read_gc0_count();
	back_to_back_c0_hazard();
	cause = read_gc0_cause();
	*out_cause = cause;

	/*
	 * Record a final CP0_Count which we will transfer to the soft-timer.
	 * This is recorded *after* saving CP0_Cause, so we don't get any timer
	 * interrupts from just after the final CP0_Count point.
	 */
	back_to_back_c0_hazard();
	end_count = read_gc0_count();

	/*
	 * The above sequence isn't atomic, so we could miss a timer interrupt
	 * between reading CP0_Cause and end_count. Detect and record any timer
	 * interrupt due between before_count and end_count.
	 */
	if (end_count - before_count > compare - before_count - 1)
		kvm_vz_queue_irq(vcpu, MIPS_EXC_INT_TIMER);

	/*
	 * Restore soft-timer, ignoring a small amount of negative drift due to
	 * delay between freeze_hrtimer and setting CP0_GTOffset.
	 */
	kvm_mips_restore_hrtimer(vcpu, before_time, end_count, -0x10000);
}

/**
 * kvm_vz_save_timer() - Save guest timer state.
 * @vcpu:	Virtual CPU.
 *
 * Save VZ guest timer state and switch to soft guest timer if hard timer was in
 * use.
 */
static void kvm_vz_save_timer(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	u32 gctl0, compare, cause;

	gctl0 = read_c0_guestctl0();
	if (gctl0 & MIPS_GCTL0_GT) {
		/* disable guest use of hard timer */
		write_c0_guestctl0(gctl0 & ~MIPS_GCTL0_GT);

		/* save hard timer state */
		_kvm_vz_save_htimer(vcpu, &compare, &cause);
	} else {
		compare = read_gc0_compare();
		cause = read_gc0_cause();
	}

	/* save timer-related state to VCPU context */
	kvm_write_sw_gc0_cause(cop0, cause);
	kvm_write_sw_gc0_compare(cop0, compare);
}

/**
 * kvm_vz_lose_htimer() - Ensure hard guest timer is not in use.
 * @vcpu:	Virtual CPU.
 *
 * Transfers the state of the hard guest timer to the soft guest timer, leaving
 * guest state intact so it can continue to be used with the soft timer.
 */
void kvm_vz_lose_htimer(struct kvm_vcpu *vcpu)
{
	u32 gctl0, compare, cause;

	preempt_disable();
	gctl0 = read_c0_guestctl0();
	if (gctl0 & MIPS_GCTL0_GT) {
		/* disable guest use of timer */
		write_c0_guestctl0(gctl0 & ~MIPS_GCTL0_GT);

		/* switch to soft timer */
		_kvm_vz_save_htimer(vcpu, &compare, &cause);

		/* leave soft timer in usable state */
		_kvm_vz_restore_stimer(vcpu, compare, cause);
	}
	preempt_enable();
}

/**
 * is_eva_access() - Find whether an instruction is an EVA memory accessor.
 * @inst:	32-bit instruction encoding.
 *
 * Finds whether @inst encodes an EVA memory access instruction, which would
 * indicate that emulation of it should access the user mode address space
 * instead of the kernel mode address space. This matters for MUSUK segments
 * which are TLB mapped for user mode but unmapped for kernel mode.
 *
 * Returns:	Whether @inst encodes an EVA accessor instruction.
 */
static bool is_eva_access(union mips_instruction inst)
{
	if (inst.spec3_format.opcode != spec3_op)
		return false;

	switch (inst.spec3_format.func) {
	case lwle_op:
	case lwre_op:
	case cachee_op:
	case sbe_op:
	case she_op:
	case sce_op:
	case swe_op:
	case swle_op:
	case swre_op:
	case prefe_op:
	case lbue_op:
	case lhue_op:
	case lbe_op:
	case lhe_op:
	case lle_op:
	case lwe_op:
		return true;
	default:
		return false;
	}
}

/**
 * is_eva_am_mapped() - Find whether an access mode is mapped.
 * @vcpu:	KVM VCPU state.
 * @am:		3-bit encoded access mode.
 * @eu:		Segment becomes unmapped and uncached when Status.ERL=1.
 *
 * Decode @am to find whether it encodes a mapped segment for the current VCPU
 * state. Where necessary @eu and the actual instruction causing the fault are
 * taken into account to make the decision.
 *
 * Returns:	Whether the VCPU faulted on a TLB mapped address.
 */
static bool is_eva_am_mapped(struct kvm_vcpu *vcpu, unsigned int am, bool eu)
{
	u32 am_lookup;
	int err;

	/*
	 * Interpret access control mode. We assume address errors will already
	 * have been caught by the guest, leaving us with:
	 *      AM      UM  SM  KM  31..24 23..16
	 * UK    0 000          Unm   0      0
	 * MK    1 001          TLB   1
	 * MSK   2 010      TLB TLB   1
	 * MUSK  3 011  TLB TLB TLB   1
	 * MUSUK 4 100  TLB TLB Unm   0      1
	 * USK   5 101      Unm Unm   0      0
	 * -     6 110                0      0
	 * UUSK  7 111  Unm Unm Unm   0      0
	 *
	 * We shift a magic value by AM across the sign bit to find if always
	 * TLB mapped, and if not shift by 8 again to find if it depends on KM.
	 */
	am_lookup = 0x70080000 << am;
	if ((s32)am_lookup < 0) {
		/*
		 * MK, MSK, MUSK
		 * Always TLB mapped, unless SegCtl.EU && ERL
		 */
		if (!eu || !(read_gc0_status() & ST0_ERL))
			return true;
	} else {
		am_lookup <<= 8;
		if ((s32)am_lookup < 0) {
			union mips_instruction inst;
			unsigned int status;
			u32 *opc;

			/*
			 * MUSUK
			 * TLB mapped if not in kernel mode
			 */
			status = read_gc0_status();
			if (!(status & (ST0_EXL | ST0_ERL)) &&
			    (status & ST0_KSU))
				return true;
			/*
			 * EVA access instructions in kernel
			 * mode access user address space.
			 */
			opc = (u32 *)vcpu->arch.pc;
			if (vcpu->arch.host_cp0_cause & CAUSEF_BD)
				opc += 1;
			err = kvm_get_badinstr(opc, vcpu, &inst.word);
			if (!err && is_eva_access(inst))
				return true;
		}
	}

	return false;
}

/**
 * kvm_vz_gva_to_gpa() - Convert valid GVA to GPA.
 * @vcpu:	KVM VCPU state.
 * @gva:	Guest virtual address to convert.
 * @gpa:	Output guest physical address.
 *
 * Convert a guest virtual address (GVA) which is valid according to the guest
 * context, to a guest physical address (GPA).
 *
 * Returns:	0 on success.
 *		-errno on failure.
 */
static int kvm_vz_gva_to_gpa(struct kvm_vcpu *vcpu, unsigned long gva,
			     unsigned long *gpa)
{
	u32 gva32 = gva;
	unsigned long segctl;

	if ((long)gva == (s32)gva32) {
		/* Handle canonical 32-bit virtual address */
		if (cpu_guest_has_segments) {
			unsigned long mask, pa;

			switch (gva32 >> 29) {
			case 0:
			case 1: /* CFG5 (1GB) */
				segctl = read_gc0_segctl2() >> 16;
				mask = (unsigned long)0xfc0000000ull;
				break;
			case 2:
			case 3: /* CFG4 (1GB) */
				segctl = read_gc0_segctl2();
				mask = (unsigned long)0xfc0000000ull;
				break;
			case 4: /* CFG3 (512MB) */
				segctl = read_gc0_segctl1() >> 16;
				mask = (unsigned long)0xfe0000000ull;
				break;
			case 5: /* CFG2 (512MB) */
				segctl = read_gc0_segctl1();
				mask = (unsigned long)0xfe0000000ull;
				break;
			case 6: /* CFG1 (512MB) */
				segctl = read_gc0_segctl0() >> 16;
				mask = (unsigned long)0xfe0000000ull;
				break;
			case 7: /* CFG0 (512MB) */
				segctl = read_gc0_segctl0();
				mask = (unsigned long)0xfe0000000ull;
				break;
			default:
				/*
				 * GCC 4.9 isn't smart enough to figure out that
				 * segctl and mask are always initialised.
				 */
				unreachable();
			}

			if (is_eva_am_mapped(vcpu, (segctl >> 4) & 0x7,
					     segctl & 0x0008))
				goto tlb_mapped;

			/* Unmapped, find guest physical address */
			pa = (segctl << 20) & mask;
			pa |= gva32 & ~mask;
			*gpa = pa;
			return 0;
		} else if ((s32)gva32 < (s32)0xc0000000) {
			/* legacy unmapped KSeg0 or KSeg1 */
			*gpa = gva32 & 0x1fffffff;
			return 0;
		}
#ifdef CONFIG_64BIT
	} else if ((gva & 0xc000000000000000) == 0x8000000000000000) {
		/* XKPHYS */
		if (cpu_guest_has_segments) {
			/*
			 * Each of the 8 regions can be overridden by SegCtl2.XR
			 * to use SegCtl1.XAM.
			 */
			segctl = read_gc0_segctl2();
			if (segctl & (1ull << (56 + ((gva >> 59) & 0x7)))) {
				segctl = read_gc0_segctl1();
				if (is_eva_am_mapped(vcpu, (segctl >> 59) & 0x7,
						     0))
					goto tlb_mapped;
			}

		}
		/*
		 * Traditionally fully unmapped.
		 * Bits 61:59 specify the CCA, which we can just mask off here.
		 * Bits 58:PABITS should be zero, but we shouldn't have got here
		 * if it wasn't.
		 */
		*gpa = gva & 0x07ffffffffffffff;
		return 0;
#endif
	}

tlb_mapped:
	return kvm_vz_guest_tlb_lookup(vcpu, gva, gpa);
}

/**
 * kvm_vz_badvaddr_to_gpa() - Convert GVA BadVAddr from root exception to GPA.
 * @vcpu:	KVM VCPU state.
 * @badvaddr:	Root BadVAddr.
 * @gpa:	Output guest physical address.
 *
 * VZ implementations are permitted to report guest virtual addresses (GVA) in
 * BadVAddr on a root exception during guest execution, instead of the more
 * convenient guest physical addresses (GPA). When we get a GVA, this function
 * converts it to a GPA, taking into account guest segmentation and guest TLB
 * state.
 *
 * Returns:	0 on success.
 *		-errno on failure.
 */
static int kvm_vz_badvaddr_to_gpa(struct kvm_vcpu *vcpu, unsigned long badvaddr,
				  unsigned long *gpa)
{
	unsigned int gexccode = (vcpu->arch.host_cp0_guestctl0 &
				 MIPS_GCTL0_GEXC) >> MIPS_GCTL0_GEXC_SHIFT;

	/* If BadVAddr is GPA, then all is well in the world */
	if (likely(gexccode == MIPS_GCTL0_GEXC_GPA)) {
		*gpa = badvaddr;
		return 0;
	}

	/* Otherwise we'd expect it to be GVA ... */
	if (WARN(gexccode != MIPS_GCTL0_GEXC_GVA,
		 "Unexpected gexccode %#x\n", gexccode))
		return -EINVAL;

	/* ... and we need to perform the GVA->GPA translation in software */
	return kvm_vz_gva_to_gpa(vcpu, badvaddr, gpa);
}

static int kvm_trap_vz_no_handler(struct kvm_vcpu *vcpu)
{
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	u32 exccode = (cause & CAUSEF_EXCCODE) >> CAUSEB_EXCCODE;
	unsigned long badvaddr = vcpu->arch.host_cp0_badvaddr;
	u32 inst = 0;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;
	kvm_get_badinstr(opc, vcpu, &inst);

	kvm_err("Exception Code: %d not handled @ PC: %p, inst: 0x%08x BadVaddr: %#lx Status: %#x\n",
		exccode, opc, inst, badvaddr,
		read_gc0_status());
	kvm_arch_vcpu_dump_regs(vcpu);
	vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
	return RESUME_HOST;
}

static unsigned long mips_process_maar(unsigned int op, unsigned long val)
{
	/* Mask off unused bits */
	unsigned long mask = 0xfffff000 | MIPS_MAAR_S | MIPS_MAAR_VL;

	if (read_gc0_pagegrain() & PG_ELPA)
		mask |= 0x00ffffff00000000ull;
	if (cpu_guest_has_mvh)
		mask |= MIPS_MAAR_VH;

	/* Set or clear VH */
	if (op == mtc_op) {
		/* clear VH */
		val &= ~MIPS_MAAR_VH;
	} else if (op == dmtc_op) {
		/* set VH to match VL */
		val &= ~MIPS_MAAR_VH;
		if (val & MIPS_MAAR_VL)
			val |= MIPS_MAAR_VH;
	}

	return val & mask;
}

static void kvm_write_maari(struct kvm_vcpu *vcpu, unsigned long val)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	val &= MIPS_MAARI_INDEX;
	if (val == MIPS_MAARI_INDEX)
		kvm_write_sw_gc0_maari(cop0, ARRAY_SIZE(vcpu->arch.maar) - 1);
	else if (val < ARRAY_SIZE(vcpu->arch.maar))
		kvm_write_sw_gc0_maari(cop0, val);
}

static enum emulation_result kvm_vz_gpsi_cop0(union mips_instruction inst,
					      u32 *opc, u32 cause,
					      struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	enum emulation_result er = EMULATE_DONE;
	u32 rt, rd, sel;
	unsigned long curr_pc;
	unsigned long val;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	if (inst.co_format.co) {
		switch (inst.co_format.func) {
		case wait_op:
			er = kvm_mips_emul_wait(vcpu);
			break;
		default:
			er = EMULATE_FAIL;
		}
	} else {
		rt = inst.c0r_format.rt;
		rd = inst.c0r_format.rd;
		sel = inst.c0r_format.sel;

		switch (inst.c0r_format.rs) {
		case dmfc_op:
		case mfc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			if (rd == MIPS_CP0_COUNT &&
			    sel == 0) {			/* Count */
				val = kvm_mips_read_count(vcpu);
			} else if (rd == MIPS_CP0_COMPARE &&
				   sel == 0) {		/* Compare */
				val = read_gc0_compare();
			} else if (rd == MIPS_CP0_LLADDR &&
				   sel == 0) {		/* LLAddr */
				if (cpu_guest_has_rw_llb)
					val = read_gc0_lladdr() &
						MIPS_LLADDR_LLB;
				else
					val = 0;
			} else if (rd == MIPS_CP0_LLADDR &&
				   sel == 1 &&		/* MAAR */
				   cpu_guest_has_maar &&
				   !cpu_guest_has_dyn_maar) {
				/* MAARI must be in range */
				BUG_ON(kvm_read_sw_gc0_maari(cop0) >=
						ARRAY_SIZE(vcpu->arch.maar));
				val = vcpu->arch.maar[
					kvm_read_sw_gc0_maari(cop0)];
			} else if ((rd == MIPS_CP0_PRID &&
				    (sel == 0 ||	/* PRid */
				     sel == 2 ||	/* CDMMBase */
				     sel == 3)) ||	/* CMGCRBase */
				   (rd == MIPS_CP0_STATUS &&
				    (sel == 2 ||	/* SRSCtl */
				     sel == 3)) ||	/* SRSMap */
				   (rd == MIPS_CP0_CONFIG &&
				    (sel == 6 ||	/* Config6 */
				     sel == 7)) ||	/* Config7 */
				   (rd == MIPS_CP0_LLADDR &&
				    (sel == 2) &&	/* MAARI */
				    cpu_guest_has_maar &&
				    !cpu_guest_has_dyn_maar) ||
				   (rd == MIPS_CP0_ERRCTL &&
				    (sel == 0))) {	/* ErrCtl */
				val = cop0->reg[rd][sel];
#ifdef CONFIG_CPU_LOONGSON64
			} else if (rd == MIPS_CP0_DIAG &&
				   (sel == 0)) {	/* Diag */
				val = cop0->reg[rd][sel];
#endif
			} else {
				val = 0;
				er = EMULATE_FAIL;
			}

			if (er != EMULATE_FAIL) {
				/* Sign extend */
				if (inst.c0r_format.rs == mfc_op)
					val = (int)val;
				vcpu->arch.gprs[rt] = val;
			}

			trace_kvm_hwr(vcpu, (inst.c0r_format.rs == mfc_op) ?
					KVM_TRACE_MFC0 : KVM_TRACE_DMFC0,
				      KVM_TRACE_COP0(rd, sel), val);
			break;

		case dmtc_op:
		case mtc_op:
#ifdef CONFIG_KVM_MIPS_DEBUG_COP0_COUNTERS
			cop0->stat[rd][sel]++;
#endif
			val = vcpu->arch.gprs[rt];
			trace_kvm_hwr(vcpu, (inst.c0r_format.rs == mtc_op) ?
					KVM_TRACE_MTC0 : KVM_TRACE_DMTC0,
				      KVM_TRACE_COP0(rd, sel), val);

			if (rd == MIPS_CP0_COUNT &&
			    sel == 0) {			/* Count */
				kvm_vz_lose_htimer(vcpu);
				kvm_mips_write_count(vcpu, vcpu->arch.gprs[rt]);
			} else if (rd == MIPS_CP0_COMPARE &&
				   sel == 0) {		/* Compare */
				kvm_mips_write_compare(vcpu,
						       vcpu->arch.gprs[rt],
						       true);
			} else if (rd == MIPS_CP0_LLADDR &&
				   sel == 0) {		/* LLAddr */
				/*
				 * P5600 generates GPSI on guest MTC0 LLAddr.
				 * Only allow the guest to clear LLB.
				 */
				if (cpu_guest_has_rw_llb &&
				    !(val & MIPS_LLADDR_LLB))
					write_gc0_lladdr(0);
			} else if (rd == MIPS_CP0_LLADDR &&
				   sel == 1 &&		/* MAAR */
				   cpu_guest_has_maar &&
				   !cpu_guest_has_dyn_maar) {
				val = mips_process_maar(inst.c0r_format.rs,
							val);

				/* MAARI must be in range */
				BUG_ON(kvm_read_sw_gc0_maari(cop0) >=
						ARRAY_SIZE(vcpu->arch.maar));
				vcpu->arch.maar[kvm_read_sw_gc0_maari(cop0)] =
									val;
			} else if (rd == MIPS_CP0_LLADDR &&
				   (sel == 2) &&	/* MAARI */
				   cpu_guest_has_maar &&
				   !cpu_guest_has_dyn_maar) {
				kvm_write_maari(vcpu, val);
			} else if (rd == MIPS_CP0_CONFIG &&
				   (sel == 6)) {
				cop0->reg[rd][sel] = (int)val;
			} else if (rd == MIPS_CP0_ERRCTL &&
				   (sel == 0)) {	/* ErrCtl */
				/* ignore the written value */
#ifdef CONFIG_CPU_LOONGSON64
			} else if (rd == MIPS_CP0_DIAG &&
				   (sel == 0)) {	/* Diag */
				unsigned long flags;

				local_irq_save(flags);
				if (val & LOONGSON_DIAG_BTB) {
					/* Flush BTB */
					set_c0_diag(LOONGSON_DIAG_BTB);
				}
				if (val & LOONGSON_DIAG_ITLB) {
					/* Flush ITLB */
					set_c0_diag(LOONGSON_DIAG_ITLB);
				}
				if (val & LOONGSON_DIAG_DTLB) {
					/* Flush DTLB */
					set_c0_diag(LOONGSON_DIAG_DTLB);
				}
				if (val & LOONGSON_DIAG_VTLB) {
					/* Flush VTLB */
					kvm_loongson_clear_guest_vtlb();
				}
				if (val & LOONGSON_DIAG_FTLB) {
					/* Flush FTLB */
					kvm_loongson_clear_guest_ftlb();
				}
				local_irq_restore(flags);
#endif
			} else {
				er = EMULATE_FAIL;
			}
			break;

		default:
			er = EMULATE_FAIL;
			break;
		}
	}
	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL) {
		kvm_err("[%#lx]%s: unsupported cop0 instruction 0x%08x\n",
			curr_pc, __func__, inst.word);

		vcpu->arch.pc = curr_pc;
	}

	return er;
}

static enum emulation_result kvm_vz_gpsi_cache(union mips_instruction inst,
					       u32 *opc, u32 cause,
					       struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	u32 cache, op_inst, op, base;
	s16 offset;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	unsigned long va, curr_pc;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	base = inst.i_format.rs;
	op_inst = inst.i_format.rt;
	if (cpu_has_mips_r6)
		offset = inst.spec3_format.simmediate;
	else
		offset = inst.i_format.simmediate;
	cache = op_inst & CacheOp_Cache;
	op = op_inst & CacheOp_Op;

	va = arch->gprs[base] + offset;

	kvm_debug("CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		  cache, op, base, arch->gprs[base], offset);

	/* Secondary or tirtiary cache ops ignored */
	if (cache != Cache_I && cache != Cache_D)
		return EMULATE_DONE;

	switch (op_inst) {
	case Index_Invalidate_I:
		flush_icache_line_indexed(va);
		return EMULATE_DONE;
	case Index_Writeback_Inv_D:
		flush_dcache_line_indexed(va);
		return EMULATE_DONE;
	case Hit_Invalidate_I:
	case Hit_Invalidate_D:
	case Hit_Writeback_Inv_D:
		if (boot_cpu_type() == CPU_CAVIUM_OCTEON3) {
			/* We can just flush entire icache */
			local_flush_icache_range(0, 0);
			return EMULATE_DONE;
		}

		/* So far, other platforms support guest hit cache ops */
		break;
	default:
		break;
	}

	kvm_err("@ %#lx/%#lx CACHE (cache: %#x, op: %#x, base[%d]: %#lx, offset: %#x\n",
		curr_pc, vcpu->arch.gprs[31], cache, op, base, arch->gprs[base],
		offset);
	/* Rollback PC */
	vcpu->arch.pc = curr_pc;

	return EMULATE_FAIL;
}

#ifdef CONFIG_CPU_LOONGSON64
static enum emulation_result kvm_vz_gpsi_lwc2(union mips_instruction inst,
					      u32 *opc, u32 cause,
					      struct kvm_vcpu *vcpu)
{
	unsigned int rs, rd;
	unsigned int hostcfg;
	unsigned long curr_pc;
	enum emulation_result er = EMULATE_DONE;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	rs = inst.loongson3_lscsr_format.rs;
	rd = inst.loongson3_lscsr_format.rd;
	switch (inst.loongson3_lscsr_format.fr) {
	case 0x8:  /* Read CPUCFG */
		++vcpu->stat.vz_cpucfg_exits;
		hostcfg = read_cpucfg(vcpu->arch.gprs[rs]);

		switch (vcpu->arch.gprs[rs]) {
		case LOONGSON_CFG0:
			vcpu->arch.gprs[rd] = 0x14c000;
			break;
		case LOONGSON_CFG1:
			hostcfg &= (LOONGSON_CFG1_FP | LOONGSON_CFG1_MMI |
				    LOONGSON_CFG1_MSA1 | LOONGSON_CFG1_MSA2 |
				    LOONGSON_CFG1_SFBP);
			vcpu->arch.gprs[rd] = hostcfg;
			break;
		case LOONGSON_CFG2:
			hostcfg &= (LOONGSON_CFG2_LEXT1 | LOONGSON_CFG2_LEXT2 |
				    LOONGSON_CFG2_LEXT3 | LOONGSON_CFG2_LSPW);
			vcpu->arch.gprs[rd] = hostcfg;
			break;
		case LOONGSON_CFG3:
			vcpu->arch.gprs[rd] = hostcfg;
			break;
		default:
			/* Don't export any other advanced features to guest */
			vcpu->arch.gprs[rd] = 0;
			break;
		}
		break;

	default:
		kvm_err("lwc2 emulate not impl %d rs %lx @%lx\n",
			inst.loongson3_lscsr_format.fr, vcpu->arch.gprs[rs], curr_pc);
		er = EMULATE_FAIL;
		break;
	}

	/* Rollback PC only if emulation was unsuccessful */
	if (er == EMULATE_FAIL) {
		kvm_err("[%#lx]%s: unsupported lwc2 instruction 0x%08x 0x%08x\n",
			curr_pc, __func__, inst.word, inst.loongson3_lscsr_format.fr);

		vcpu->arch.pc = curr_pc;
	}

	return er;
}
#endif

static enum emulation_result kvm_trap_vz_handle_gpsi(u32 cause, u32 *opc,
						     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	union mips_instruction inst;
	int rd, rt, sel;
	int err;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err)
		return EMULATE_FAIL;

	switch (inst.r_format.opcode) {
	case cop0_op:
		er = kvm_vz_gpsi_cop0(inst, opc, cause, vcpu);
		break;
#ifndef CONFIG_CPU_MIPSR6
	case cache_op:
		trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
		er = kvm_vz_gpsi_cache(inst, opc, cause, vcpu);
		break;
#endif
#ifdef CONFIG_CPU_LOONGSON64
	case lwc2_op:
		er = kvm_vz_gpsi_lwc2(inst, opc, cause, vcpu);
		break;
#endif
	case spec3_op:
		switch (inst.spec3_format.func) {
#ifdef CONFIG_CPU_MIPSR6
		case cache6_op:
			trace_kvm_exit(vcpu, KVM_TRACE_EXIT_CACHE);
			er = kvm_vz_gpsi_cache(inst, opc, cause, vcpu);
			break;
#endif
		case rdhwr_op:
			if (inst.r_format.rs || (inst.r_format.re >> 3))
				goto unknown;

			rd = inst.r_format.rd;
			rt = inst.r_format.rt;
			sel = inst.r_format.re & 0x7;

			switch (rd) {
			case MIPS_HWR_CC:	/* Read count register */
				arch->gprs[rt] =
					(long)(int)kvm_mips_read_count(vcpu);
				break;
			default:
				trace_kvm_hwr(vcpu, KVM_TRACE_RDHWR,
					      KVM_TRACE_HWR(rd, sel), 0);
				goto unknown;
			}

			trace_kvm_hwr(vcpu, KVM_TRACE_RDHWR,
				      KVM_TRACE_HWR(rd, sel), arch->gprs[rt]);

			er = update_pc(vcpu, cause);
			break;
		default:
			goto unknown;
		}
		break;
unknown:

	default:
		kvm_err("GPSI exception not supported (%p/%#x)\n",
				opc, inst.word);
		kvm_arch_vcpu_dump_regs(vcpu);
		er = EMULATE_FAIL;
		break;
	}

	return er;
}

static enum emulation_result kvm_trap_vz_handle_gsfc(u32 cause, u32 *opc,
						     struct kvm_vcpu *vcpu)
{
	enum emulation_result er = EMULATE_DONE;
	struct kvm_vcpu_arch *arch = &vcpu->arch;
	union mips_instruction inst;
	int err;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err)
		return EMULATE_FAIL;

	/* complete MTC0 on behalf of guest and advance EPC */
	if (inst.c0r_format.opcode == cop0_op &&
	    inst.c0r_format.rs == mtc_op &&
	    inst.c0r_format.z == 0) {
		int rt = inst.c0r_format.rt;
		int rd = inst.c0r_format.rd;
		int sel = inst.c0r_format.sel;
		unsigned int val = arch->gprs[rt];
		unsigned int old_val, change;

		trace_kvm_hwr(vcpu, KVM_TRACE_MTC0, KVM_TRACE_COP0(rd, sel),
			      val);

		if ((rd == MIPS_CP0_STATUS) && (sel == 0)) {
			/* FR bit should read as zero if no FPU */
			if (!kvm_mips_guest_has_fpu(&vcpu->arch))
				val &= ~(ST0_CU1 | ST0_FR);

			/*
			 * Also don't allow FR to be set if host doesn't support
			 * it.
			 */
			if (!(boot_cpu_data.fpu_id & MIPS_FPIR_F64))
				val &= ~ST0_FR;

			old_val = read_gc0_status();
			change = val ^ old_val;

			if (change & ST0_FR) {
				/*
				 * FPU and Vector register state is made
				 * UNPREDICTABLE by a change of FR, so don't
				 * even bother saving it.
				 */
				kvm_drop_fpu(vcpu);
			}

			/*
			 * If MSA state is already live, it is undefined how it
			 * interacts with FR=0 FPU state, and we don't want to
			 * hit reserved instruction exceptions trying to save
			 * the MSA state later when CU=1 && FR=1, so play it
			 * safe and save it first.
			 */
			if (change & ST0_CU1 && !(val & ST0_FR) &&
			    vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA)
				kvm_lose_fpu(vcpu);

			write_gc0_status(val);
		} else if ((rd == MIPS_CP0_CAUSE) && (sel == 0)) {
			u32 old_cause = read_gc0_cause();
			u32 change = old_cause ^ val;

			/* DC bit enabling/disabling timer? */
			if (change & CAUSEF_DC) {
				if (val & CAUSEF_DC) {
					kvm_vz_lose_htimer(vcpu);
					kvm_mips_count_disable_cause(vcpu);
				} else {
					kvm_mips_count_enable_cause(vcpu);
				}
			}

			/* Only certain bits are RW to the guest */
			change &= (CAUSEF_DC | CAUSEF_IV | CAUSEF_WP |
				   CAUSEF_IP0 | CAUSEF_IP1);

			/* WP can only be cleared */
			change &= ~CAUSEF_WP | old_cause;

			write_gc0_cause(old_cause ^ change);
		} else if ((rd == MIPS_CP0_STATUS) && (sel == 1)) { /* IntCtl */
			write_gc0_intctl(val);
		} else if ((rd == MIPS_CP0_CONFIG) && (sel == 5)) {
			old_val = read_gc0_config5();
			change = val ^ old_val;
			/* Handle changes in FPU/MSA modes */
			preempt_disable();

			/*
			 * Propagate FRE changes immediately if the FPU
			 * context is already loaded.
			 */
			if (change & MIPS_CONF5_FRE &&
			    vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)
				change_c0_config5(MIPS_CONF5_FRE, val);

			preempt_enable();

			val = old_val ^
				(change & kvm_vz_config5_guest_wrmask(vcpu));
			write_gc0_config5(val);
		} else {
			kvm_err("Handle GSFC, unsupported field change @ %p: %#x\n",
			    opc, inst.word);
			er = EMULATE_FAIL;
		}

		if (er != EMULATE_FAIL)
			er = update_pc(vcpu, cause);
	} else {
		kvm_err("Handle GSFC, unrecognized instruction @ %p: %#x\n",
			opc, inst.word);
		er = EMULATE_FAIL;
	}

	return er;
}

static enum emulation_result kvm_trap_vz_handle_ghfc(u32 cause, u32 *opc,
						     struct kvm_vcpu *vcpu)
{
	/*
	 * Presumably this is due to MC (guest mode change), so lets trace some
	 * relevant info.
	 */
	trace_kvm_guest_mode_change(vcpu);

	return EMULATE_DONE;
}

static enum emulation_result kvm_trap_vz_handle_hc(u32 cause, u32 *opc,
						   struct kvm_vcpu *vcpu)
{
	enum emulation_result er;
	union mips_instruction inst;
	unsigned long curr_pc;
	int err;

	if (cause & CAUSEF_BD)
		opc += 1;
	err = kvm_get_badinstr(opc, vcpu, &inst.word);
	if (err)
		return EMULATE_FAIL;

	/*
	 * Update PC and hold onto current PC in case there is
	 * an error and we want to rollback the PC
	 */
	curr_pc = vcpu->arch.pc;
	er = update_pc(vcpu, cause);
	if (er == EMULATE_FAIL)
		return er;

	er = kvm_mips_emul_hypcall(vcpu, inst);
	if (er == EMULATE_FAIL)
		vcpu->arch.pc = curr_pc;

	return er;
}

static enum emulation_result kvm_trap_vz_no_handler_guest_exit(u32 gexccode,
							u32 cause,
							u32 *opc,
							struct kvm_vcpu *vcpu)
{
	u32 inst;

	/*
	 *  Fetch the instruction.
	 */
	if (cause & CAUSEF_BD)
		opc += 1;
	kvm_get_badinstr(opc, vcpu, &inst);

	kvm_err("Guest Exception Code: %d not yet handled @ PC: %p, inst: 0x%08x  Status: %#x\n",
		gexccode, opc, inst, read_gc0_status());

	return EMULATE_FAIL;
}

static int kvm_trap_vz_handle_guest_exit(struct kvm_vcpu *vcpu)
{
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_DONE;
	u32 gexccode = (vcpu->arch.host_cp0_guestctl0 &
			MIPS_GCTL0_GEXC) >> MIPS_GCTL0_GEXC_SHIFT;
	int ret = RESUME_GUEST;

	trace_kvm_exit(vcpu, KVM_TRACE_EXIT_GEXCCODE_BASE + gexccode);
	switch (gexccode) {
	case MIPS_GCTL0_GEXC_GPSI:
		++vcpu->stat.vz_gpsi_exits;
		er = kvm_trap_vz_handle_gpsi(cause, opc, vcpu);
		break;
	case MIPS_GCTL0_GEXC_GSFC:
		++vcpu->stat.vz_gsfc_exits;
		er = kvm_trap_vz_handle_gsfc(cause, opc, vcpu);
		break;
	case MIPS_GCTL0_GEXC_HC:
		++vcpu->stat.vz_hc_exits;
		er = kvm_trap_vz_handle_hc(cause, opc, vcpu);
		break;
	case MIPS_GCTL0_GEXC_GRR:
		++vcpu->stat.vz_grr_exits;
		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
						       vcpu);
		break;
	case MIPS_GCTL0_GEXC_GVA:
		++vcpu->stat.vz_gva_exits;
		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
						       vcpu);
		break;
	case MIPS_GCTL0_GEXC_GHFC:
		++vcpu->stat.vz_ghfc_exits;
		er = kvm_trap_vz_handle_ghfc(cause, opc, vcpu);
		break;
	case MIPS_GCTL0_GEXC_GPA:
		++vcpu->stat.vz_gpa_exits;
		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
						       vcpu);
		break;
	default:
		++vcpu->stat.vz_resvd_exits;
		er = kvm_trap_vz_no_handler_guest_exit(gexccode, cause, opc,
						       vcpu);
		break;

	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_HYPERCALL) {
		ret = kvm_mips_handle_hypcall(vcpu);
	} else {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

/**
 * kvm_trap_vz_handle_cop_unusuable() - Guest used unusable coprocessor.
 * @vcpu:	Virtual CPU context.
 *
 * Handle when the guest attempts to use a coprocessor which hasn't been allowed
 * by the root context.
 */
static int kvm_trap_vz_handle_cop_unusable(struct kvm_vcpu *vcpu)
{
	u32 cause = vcpu->arch.host_cp0_cause;
	enum emulation_result er = EMULATE_FAIL;
	int ret = RESUME_GUEST;

	if (((cause & CAUSEF_CE) >> CAUSEB_CE) == 1) {
		/*
		 * If guest FPU not present, the FPU operation should have been
		 * treated as a reserved instruction!
		 * If FPU already in use, we shouldn't get this at all.
		 */
		if (WARN_ON(!kvm_mips_guest_has_fpu(&vcpu->arch) ||
			    vcpu->arch.aux_inuse & KVM_MIPS_AUX_FPU)) {
			preempt_enable();
			return EMULATE_FAIL;
		}

		kvm_own_fpu(vcpu);
		er = EMULATE_DONE;
	}
	/* other coprocessors not handled */

	switch (er) {
	case EMULATE_DONE:
		ret = RESUME_GUEST;
		break;

	case EMULATE_FAIL:
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
		break;

	default:
		BUG();
	}
	return ret;
}

/**
 * kvm_trap_vz_handle_msa_disabled() - Guest used MSA while disabled in root.
 * @vcpu:	Virtual CPU context.
 *
 * Handle when the guest attempts to use MSA when it is disabled in the root
 * context.
 */
static int kvm_trap_vz_handle_msa_disabled(struct kvm_vcpu *vcpu)
{
	/*
	 * If MSA not present or not exposed to guest or FR=0, the MSA operation
	 * should have been treated as a reserved instruction!
	 * Same if CU1=1, FR=0.
	 * If MSA already in use, we shouldn't get this at all.
	 */
	if (!kvm_mips_guest_has_msa(&vcpu->arch) ||
	    (read_gc0_status() & (ST0_CU1 | ST0_FR)) == ST0_CU1 ||
	    !(read_gc0_config5() & MIPS_CONF5_MSAEN) ||
	    vcpu->arch.aux_inuse & KVM_MIPS_AUX_MSA) {
		vcpu->run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		return RESUME_HOST;
	}

	kvm_own_msa(vcpu);

	return RESUME_GUEST;
}

static int kvm_trap_vz_handle_tlb_ld_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	ulong badvaddr = vcpu->arch.host_cp0_badvaddr;
	union mips_instruction inst;
	enum emulation_result er = EMULATE_DONE;
	int err, ret = RESUME_GUEST;

	if (kvm_mips_handle_vz_root_tlb_fault(badvaddr, vcpu, false)) {
		/* A code fetch fault doesn't count as an MMIO */
		if (kvm_is_ifetch_fault(&vcpu->arch)) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Fetch the instruction */
		if (cause & CAUSEF_BD)
			opc += 1;
		err = kvm_get_badinstr(opc, vcpu, &inst.word);
		if (err) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Treat as MMIO */
		er = kvm_mips_emulate_load(inst, cause, vcpu);
		if (er == EMULATE_FAIL) {
			kvm_err("Guest Emulate Load from MMIO space failed: PC: %p, BadVaddr: %#lx\n",
				opc, badvaddr);
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		}
	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static int kvm_trap_vz_handle_tlb_st_miss(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	u32 *opc = (u32 *) vcpu->arch.pc;
	u32 cause = vcpu->arch.host_cp0_cause;
	ulong badvaddr = vcpu->arch.host_cp0_badvaddr;
	union mips_instruction inst;
	enum emulation_result er = EMULATE_DONE;
	int err;
	int ret = RESUME_GUEST;

	/* Just try the access again if we couldn't do the translation */
	if (kvm_vz_badvaddr_to_gpa(vcpu, badvaddr, &badvaddr))
		return RESUME_GUEST;
	vcpu->arch.host_cp0_badvaddr = badvaddr;

	if (kvm_mips_handle_vz_root_tlb_fault(badvaddr, vcpu, true)) {
		/* Fetch the instruction */
		if (cause & CAUSEF_BD)
			opc += 1;
		err = kvm_get_badinstr(opc, vcpu, &inst.word);
		if (err) {
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
			return RESUME_HOST;
		}

		/* Treat as MMIO */
		er = kvm_mips_emulate_store(inst, cause, vcpu);
		if (er == EMULATE_FAIL) {
			kvm_err("Guest Emulate Store to MMIO space failed: PC: %p, BadVaddr: %#lx\n",
				opc, badvaddr);
			run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		}
	}

	if (er == EMULATE_DONE) {
		ret = RESUME_GUEST;
	} else if (er == EMULATE_DO_MMIO) {
		run->exit_reason = KVM_EXIT_MMIO;
		ret = RESUME_HOST;
	} else {
		run->exit_reason = KVM_EXIT_INTERNAL_ERROR;
		ret = RESUME_HOST;
	}
	return ret;
}

static u64 kvm_vz_get_one_regs[] = {
	KVM_REG_MIPS_CP0_INDEX,
	KVM_REG_MIPS_CP0_ENTRYLO0,
	KVM_REG_MIPS_CP0_ENTRYLO1,
	KVM_REG_MIPS_CP0_CONTEXT,
	KVM_REG_MIPS_CP0_PAGEMASK,
	KVM_REG_MIPS_CP0_PAGEGRAIN,
	KVM_REG_MIPS_CP0_WIRED,
	KVM_REG_MIPS_CP0_HWRENA,
	KVM_REG_MIPS_CP0_BADVADDR,
	KVM_REG_MIPS_CP0_COUNT,
	KVM_REG_MIPS_CP0_ENTRYHI,
	KVM_REG_MIPS_CP0_COMPARE,
	KVM_REG_MIPS_CP0_STATUS,
	KVM_REG_MIPS_CP0_INTCTL,
	KVM_REG_MIPS_CP0_CAUSE,
	KVM_REG_MIPS_CP0_EPC,
	KVM_REG_MIPS_CP0_PRID,
	KVM_REG_MIPS_CP0_EBASE,
	KVM_REG_MIPS_CP0_CONFIG,
	KVM_REG_MIPS_CP0_CONFIG1,
	KVM_REG_MIPS_CP0_CONFIG2,
	KVM_REG_MIPS_CP0_CONFIG3,
	KVM_REG_MIPS_CP0_CONFIG4,
	KVM_REG_MIPS_CP0_CONFIG5,
	KVM_REG_MIPS_CP0_CONFIG6,
#ifdef CONFIG_64BIT
	KVM_REG_MIPS_CP0_XCONTEXT,
#endif
	KVM_REG_MIPS_CP0_ERROREPC,

	KVM_REG_MIPS_COUNT_CTL,
	KVM_REG_MIPS_COUNT_RESUME,
	KVM_REG_MIPS_COUNT_HZ,
};

static u64 kvm_vz_get_one_regs_contextconfig[] = {
	KVM_REG_MIPS_CP0_CONTEXTCONFIG,
#ifdef CONFIG_64BIT
	KVM_REG_MIPS_CP0_XCONTEXTCONFIG,
#endif
};

static u64 kvm_vz_get_one_regs_segments[] = {
	KVM_REG_MIPS_CP0_SEGCTL0,
	KVM_REG_MIPS_CP0_SEGCTL1,
	KVM_REG_MIPS_CP0_SEGCTL2,
};

static u64 kvm_vz_get_one_regs_htw[] = {
	KVM_REG_MIPS_CP0_PWBASE,
	KVM_REG_MIPS_CP0_PWFIELD,
	KVM_REG_MIPS_CP0_PWSIZE,
	KVM_REG_MIPS_CP0_PWCTL,
};

static u64 kvm_vz_get_one_regs_kscratch[] = {
	KVM_REG_MIPS_CP0_KSCRATCH1,
	KVM_REG_MIPS_CP0_KSCRATCH2,
	KVM_REG_MIPS_CP0_KSCRATCH3,
	KVM_REG_MIPS_CP0_KSCRATCH4,
	KVM_REG_MIPS_CP0_KSCRATCH5,
	KVM_REG_MIPS_CP0_KSCRATCH6,
};

static unsigned long kvm_vz_num_regs(struct kvm_vcpu *vcpu)
{
	unsigned long ret;

	ret = ARRAY_SIZE(kvm_vz_get_one_regs);
	if (cpu_guest_has_userlocal)
		++ret;
	if (cpu_guest_has_badinstr)
		++ret;
	if (cpu_guest_has_badinstrp)
		++ret;
	if (cpu_guest_has_contextconfig)
		ret += ARRAY_SIZE(kvm_vz_get_one_regs_contextconfig);
	if (cpu_guest_has_segments)
		ret += ARRAY_SIZE(kvm_vz_get_one_regs_segments);
	if (cpu_guest_has_htw || cpu_guest_has_ldpte)
		ret += ARRAY_SIZE(kvm_vz_get_one_regs_htw);
	if (cpu_guest_has_maar && !cpu_guest_has_dyn_maar)
		ret += 1 + ARRAY_SIZE(vcpu->arch.maar);
	ret += __arch_hweight8(cpu_data[0].guest.kscratch_mask);

	return ret;
}

static int kvm_vz_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices)
{
	u64 index;
	unsigned int i;

	if (copy_to_user(indices, kvm_vz_get_one_regs,
			 sizeof(kvm_vz_get_one_regs)))
		return -EFAULT;
	indices += ARRAY_SIZE(kvm_vz_get_one_regs);

	if (cpu_guest_has_userlocal) {
		index = KVM_REG_MIPS_CP0_USERLOCAL;
		if (copy_to_user(indices, &index, sizeof(index)))
			return -EFAULT;
		++indices;
	}
	if (cpu_guest_has_badinstr) {
		index = KVM_REG_MIPS_CP0_BADINSTR;
		if (copy_to_user(indices, &index, sizeof(index)))
			return -EFAULT;
		++indices;
	}
	if (cpu_guest_has_badinstrp) {
		index = KVM_REG_MIPS_CP0_BADINSTRP;
		if (copy_to_user(indices, &index, sizeof(index)))
			return -EFAULT;
		++indices;
	}
	if (cpu_guest_has_contextconfig) {
		if (copy_to_user(indices, kvm_vz_get_one_regs_contextconfig,
				 sizeof(kvm_vz_get_one_regs_contextconfig)))
			return -EFAULT;
		indices += ARRAY_SIZE(kvm_vz_get_one_regs_contextconfig);
	}
	if (cpu_guest_has_segments) {
		if (copy_to_user(indices, kvm_vz_get_one_regs_segments,
				 sizeof(kvm_vz_get_one_regs_segments)))
			return -EFAULT;
		indices += ARRAY_SIZE(kvm_vz_get_one_regs_segments);
	}
	if (cpu_guest_has_htw || cpu_guest_has_ldpte) {
		if (copy_to_user(indices, kvm_vz_get_one_regs_htw,
				 sizeof(kvm_vz_get_one_regs_htw)))
			return -EFAULT;
		indices += ARRAY_SIZE(kvm_vz_get_one_regs_htw);
	}
	if (cpu_guest_has_maar && !cpu_guest_has_dyn_maar) {
		for (i = 0; i < ARRAY_SIZE(vcpu->arch.maar); ++i) {
			index = KVM_REG_MIPS_CP0_MAAR(i);
			if (copy_to_user(indices, &index, sizeof(index)))
				return -EFAULT;
			++indices;
		}

		index = KVM_REG_MIPS_CP0_MAARI;
		if (copy_to_user(indices, &index, sizeof(index)))
			return -EFAULT;
		++indices;
	}
	for (i = 0; i < 6; ++i) {
		if (!cpu_guest_has_kscr(i + 2))
			continue;

		if (copy_to_user(indices, &kvm_vz_get_one_regs_kscratch[i],
				 sizeof(kvm_vz_get_one_regs_kscratch[i])))
			return -EFAULT;
		++indices;
	}

	return 0;
}

static inline s64 entrylo_kvm_to_user(unsigned long v)
{
	s64 mask, ret = v;

	if (BITS_PER_LONG == 32) {
		/*
		 * KVM API exposes 64-bit version of the register, so move the
		 * RI/XI bits up into place.
		 */
		mask = MIPS_ENTRYLO_RI | MIPS_ENTRYLO_XI;
		ret &= ~mask;
		ret |= ((s64)v & mask) << 32;
	}
	return ret;
}

static inline unsigned long entrylo_user_to_kvm(s64 v)
{
	unsigned long mask, ret = v;

	if (BITS_PER_LONG == 32) {
		/*
		 * KVM API exposes 64-bit versiono of the register, so move the
		 * RI/XI bits down into place.
		 */
		mask = MIPS_ENTRYLO_RI | MIPS_ENTRYLO_XI;
		ret &= ~mask;
		ret |= (v >> 32) & mask;
	}
	return ret;
}

static int kvm_vz_get_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 *v)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned int idx;

	switch (reg->id) {
	case KVM_REG_MIPS_CP0_INDEX:
		*v = (long)read_gc0_index();
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO0:
		*v = entrylo_kvm_to_user(read_gc0_entrylo0());
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO1:
		*v = entrylo_kvm_to_user(read_gc0_entrylo1());
		break;
	case KVM_REG_MIPS_CP0_CONTEXT:
		*v = (long)read_gc0_context();
		break;
	case KVM_REG_MIPS_CP0_CONTEXTCONFIG:
		if (!cpu_guest_has_contextconfig)
			return -EINVAL;
		*v = read_gc0_contextconfig();
		break;
	case KVM_REG_MIPS_CP0_USERLOCAL:
		if (!cpu_guest_has_userlocal)
			return -EINVAL;
		*v = read_gc0_userlocal();
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXTCONFIG:
		if (!cpu_guest_has_contextconfig)
			return -EINVAL;
		*v = read_gc0_xcontextconfig();
		break;
#endif
	case KVM_REG_MIPS_CP0_PAGEMASK:
		*v = (long)read_gc0_pagemask();
		break;
	case KVM_REG_MIPS_CP0_PAGEGRAIN:
		*v = (long)read_gc0_pagegrain();
		break;
	case KVM_REG_MIPS_CP0_SEGCTL0:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		*v = read_gc0_segctl0();
		break;
	case KVM_REG_MIPS_CP0_SEGCTL1:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		*v = read_gc0_segctl1();
		break;
	case KVM_REG_MIPS_CP0_SEGCTL2:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		*v = read_gc0_segctl2();
		break;
	case KVM_REG_MIPS_CP0_PWBASE:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		*v = read_gc0_pwbase();
		break;
	case KVM_REG_MIPS_CP0_PWFIELD:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		*v = read_gc0_pwfield();
		break;
	case KVM_REG_MIPS_CP0_PWSIZE:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		*v = read_gc0_pwsize();
		break;
	case KVM_REG_MIPS_CP0_WIRED:
		*v = (long)read_gc0_wired();
		break;
	case KVM_REG_MIPS_CP0_PWCTL:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		*v = read_gc0_pwctl();
		break;
	case KVM_REG_MIPS_CP0_HWRENA:
		*v = (long)read_gc0_hwrena();
		break;
	case KVM_REG_MIPS_CP0_BADVADDR:
		*v = (long)read_gc0_badvaddr();
		break;
	case KVM_REG_MIPS_CP0_BADINSTR:
		if (!cpu_guest_has_badinstr)
			return -EINVAL;
		*v = read_gc0_badinstr();
		break;
	case KVM_REG_MIPS_CP0_BADINSTRP:
		if (!cpu_guest_has_badinstrp)
			return -EINVAL;
		*v = read_gc0_badinstrp();
		break;
	case KVM_REG_MIPS_CP0_COUNT:
		*v = kvm_mips_read_count(vcpu);
		break;
	case KVM_REG_MIPS_CP0_ENTRYHI:
		*v = (long)read_gc0_entryhi();
		break;
	case KVM_REG_MIPS_CP0_COMPARE:
		*v = (long)read_gc0_compare();
		break;
	case KVM_REG_MIPS_CP0_STATUS:
		*v = (long)read_gc0_status();
		break;
	case KVM_REG_MIPS_CP0_INTCTL:
		*v = read_gc0_intctl();
		break;
	case KVM_REG_MIPS_CP0_CAUSE:
		*v = (long)read_gc0_cause();
		break;
	case KVM_REG_MIPS_CP0_EPC:
		*v = (long)read_gc0_epc();
		break;
	case KVM_REG_MIPS_CP0_PRID:
		switch (boot_cpu_type()) {
		case CPU_CAVIUM_OCTEON3:
			/* Octeon III has a read-only guest.PRid */
			*v = read_gc0_prid();
			break;
		default:
			*v = (long)kvm_read_c0_guest_prid(cop0);
			break;
		}
		break;
	case KVM_REG_MIPS_CP0_EBASE:
		*v = kvm_vz_read_gc0_ebase();
		break;
	case KVM_REG_MIPS_CP0_CONFIG:
		*v = read_gc0_config();
		break;
	case KVM_REG_MIPS_CP0_CONFIG1:
		if (!cpu_guest_has_conf1)
			return -EINVAL;
		*v = read_gc0_config1();
		break;
	case KVM_REG_MIPS_CP0_CONFIG2:
		if (!cpu_guest_has_conf2)
			return -EINVAL;
		*v = read_gc0_config2();
		break;
	case KVM_REG_MIPS_CP0_CONFIG3:
		if (!cpu_guest_has_conf3)
			return -EINVAL;
		*v = read_gc0_config3();
		break;
	case KVM_REG_MIPS_CP0_CONFIG4:
		if (!cpu_guest_has_conf4)
			return -EINVAL;
		*v = read_gc0_config4();
		break;
	case KVM_REG_MIPS_CP0_CONFIG5:
		if (!cpu_guest_has_conf5)
			return -EINVAL;
		*v = read_gc0_config5();
		break;
	case KVM_REG_MIPS_CP0_CONFIG6:
		*v = kvm_read_sw_gc0_config6(cop0);
		break;
	case KVM_REG_MIPS_CP0_MAAR(0) ... KVM_REG_MIPS_CP0_MAAR(0x3f):
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_CP0_MAAR(0);
		if (idx >= ARRAY_SIZE(vcpu->arch.maar))
			return -EINVAL;
		*v = vcpu->arch.maar[idx];
		break;
	case KVM_REG_MIPS_CP0_MAARI:
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		*v = kvm_read_sw_gc0_maari(vcpu->arch.cop0);
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXT:
		*v = read_gc0_xcontext();
		break;
#endif
	case KVM_REG_MIPS_CP0_ERROREPC:
		*v = (long)read_gc0_errorepc();
		break;
	case KVM_REG_MIPS_CP0_KSCRATCH1 ... KVM_REG_MIPS_CP0_KSCRATCH6:
		idx = reg->id - KVM_REG_MIPS_CP0_KSCRATCH1 + 2;
		if (!cpu_guest_has_kscr(idx))
			return -EINVAL;
		switch (idx) {
		case 2:
			*v = (long)read_gc0_kscratch1();
			break;
		case 3:
			*v = (long)read_gc0_kscratch2();
			break;
		case 4:
			*v = (long)read_gc0_kscratch3();
			break;
		case 5:
			*v = (long)read_gc0_kscratch4();
			break;
		case 6:
			*v = (long)read_gc0_kscratch5();
			break;
		case 7:
			*v = (long)read_gc0_kscratch6();
			break;
		}
		break;
	case KVM_REG_MIPS_COUNT_CTL:
		*v = vcpu->arch.count_ctl;
		break;
	case KVM_REG_MIPS_COUNT_RESUME:
		*v = ktime_to_ns(vcpu->arch.count_resume);
		break;
	case KVM_REG_MIPS_COUNT_HZ:
		*v = vcpu->arch.count_hz;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int kvm_vz_set_one_reg(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      s64 v)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned int idx;
	int ret = 0;
	unsigned int cur, change;

	switch (reg->id) {
	case KVM_REG_MIPS_CP0_INDEX:
		write_gc0_index(v);
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO0:
		write_gc0_entrylo0(entrylo_user_to_kvm(v));
		break;
	case KVM_REG_MIPS_CP0_ENTRYLO1:
		write_gc0_entrylo1(entrylo_user_to_kvm(v));
		break;
	case KVM_REG_MIPS_CP0_CONTEXT:
		write_gc0_context(v);
		break;
	case KVM_REG_MIPS_CP0_CONTEXTCONFIG:
		if (!cpu_guest_has_contextconfig)
			return -EINVAL;
		write_gc0_contextconfig(v);
		break;
	case KVM_REG_MIPS_CP0_USERLOCAL:
		if (!cpu_guest_has_userlocal)
			return -EINVAL;
		write_gc0_userlocal(v);
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXTCONFIG:
		if (!cpu_guest_has_contextconfig)
			return -EINVAL;
		write_gc0_xcontextconfig(v);
		break;
#endif
	case KVM_REG_MIPS_CP0_PAGEMASK:
		write_gc0_pagemask(v);
		break;
	case KVM_REG_MIPS_CP0_PAGEGRAIN:
		write_gc0_pagegrain(v);
		break;
	case KVM_REG_MIPS_CP0_SEGCTL0:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		write_gc0_segctl0(v);
		break;
	case KVM_REG_MIPS_CP0_SEGCTL1:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		write_gc0_segctl1(v);
		break;
	case KVM_REG_MIPS_CP0_SEGCTL2:
		if (!cpu_guest_has_segments)
			return -EINVAL;
		write_gc0_segctl2(v);
		break;
	case KVM_REG_MIPS_CP0_PWBASE:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		write_gc0_pwbase(v);
		break;
	case KVM_REG_MIPS_CP0_PWFIELD:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		write_gc0_pwfield(v);
		break;
	case KVM_REG_MIPS_CP0_PWSIZE:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		write_gc0_pwsize(v);
		break;
	case KVM_REG_MIPS_CP0_WIRED:
		change_gc0_wired(MIPSR6_WIRED_WIRED, v);
		break;
	case KVM_REG_MIPS_CP0_PWCTL:
		if (!cpu_guest_has_htw && !cpu_guest_has_ldpte)
			return -EINVAL;
		write_gc0_pwctl(v);
		break;
	case KVM_REG_MIPS_CP0_HWRENA:
		write_gc0_hwrena(v);
		break;
	case KVM_REG_MIPS_CP0_BADVADDR:
		write_gc0_badvaddr(v);
		break;
	case KVM_REG_MIPS_CP0_BADINSTR:
		if (!cpu_guest_has_badinstr)
			return -EINVAL;
		write_gc0_badinstr(v);
		break;
	case KVM_REG_MIPS_CP0_BADINSTRP:
		if (!cpu_guest_has_badinstrp)
			return -EINVAL;
		write_gc0_badinstrp(v);
		break;
	case KVM_REG_MIPS_CP0_COUNT:
		kvm_mips_write_count(vcpu, v);
		break;
	case KVM_REG_MIPS_CP0_ENTRYHI:
		write_gc0_entryhi(v);
		break;
	case KVM_REG_MIPS_CP0_COMPARE:
		kvm_mips_write_compare(vcpu, v, false);
		break;
	case KVM_REG_MIPS_CP0_STATUS:
		write_gc0_status(v);
		break;
	case KVM_REG_MIPS_CP0_INTCTL:
		write_gc0_intctl(v);
		break;
	case KVM_REG_MIPS_CP0_CAUSE:
		/*
		 * If the timer is stopped or started (DC bit) it must look
		 * atomic with changes to the timer interrupt pending bit (TI).
		 * A timer interrupt should not happen in between.
		 */
		if ((read_gc0_cause() ^ v) & CAUSEF_DC) {
			if (v & CAUSEF_DC) {
				/* disable timer first */
				kvm_mips_count_disable_cause(vcpu);
				change_gc0_cause((u32)~CAUSEF_DC, v);
			} else {
				/* enable timer last */
				change_gc0_cause((u32)~CAUSEF_DC, v);
				kvm_mips_count_enable_cause(vcpu);
			}
		} else {
			write_gc0_cause(v);
		}
		break;
	case KVM_REG_MIPS_CP0_EPC:
		write_gc0_epc(v);
		break;
	case KVM_REG_MIPS_CP0_PRID:
		switch (boot_cpu_type()) {
		case CPU_CAVIUM_OCTEON3:
			/* Octeon III has a guest.PRid, but its read-only */
			break;
		default:
			kvm_write_c0_guest_prid(cop0, v);
			break;
		}
		break;
	case KVM_REG_MIPS_CP0_EBASE:
		kvm_vz_write_gc0_ebase(v);
		break;
	case KVM_REG_MIPS_CP0_CONFIG:
		cur = read_gc0_config();
		change = (cur ^ v) & kvm_vz_config_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG1:
		if (!cpu_guest_has_conf1)
			break;
		cur = read_gc0_config1();
		change = (cur ^ v) & kvm_vz_config1_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config1(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG2:
		if (!cpu_guest_has_conf2)
			break;
		cur = read_gc0_config2();
		change = (cur ^ v) & kvm_vz_config2_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config2(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG3:
		if (!cpu_guest_has_conf3)
			break;
		cur = read_gc0_config3();
		change = (cur ^ v) & kvm_vz_config3_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config3(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG4:
		if (!cpu_guest_has_conf4)
			break;
		cur = read_gc0_config4();
		change = (cur ^ v) & kvm_vz_config4_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config4(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG5:
		if (!cpu_guest_has_conf5)
			break;
		cur = read_gc0_config5();
		change = (cur ^ v) & kvm_vz_config5_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			write_gc0_config5(v);
		}
		break;
	case KVM_REG_MIPS_CP0_CONFIG6:
		cur = kvm_read_sw_gc0_config6(cop0);
		change = (cur ^ v) & kvm_vz_config6_user_wrmask(vcpu);
		if (change) {
			v = cur ^ change;
			kvm_write_sw_gc0_config6(cop0, (int)v);
		}
		break;
	case KVM_REG_MIPS_CP0_MAAR(0) ... KVM_REG_MIPS_CP0_MAAR(0x3f):
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		idx = reg->id - KVM_REG_MIPS_CP0_MAAR(0);
		if (idx >= ARRAY_SIZE(vcpu->arch.maar))
			return -EINVAL;
		vcpu->arch.maar[idx] = mips_process_maar(dmtc_op, v);
		break;
	case KVM_REG_MIPS_CP0_MAARI:
		if (!cpu_guest_has_maar || cpu_guest_has_dyn_maar)
			return -EINVAL;
		kvm_write_maari(vcpu, v);
		break;
#ifdef CONFIG_64BIT
	case KVM_REG_MIPS_CP0_XCONTEXT:
		write_gc0_xcontext(v);
		break;
#endif
	case KVM_REG_MIPS_CP0_ERROREPC:
		write_gc0_errorepc(v);
		break;
	case KVM_REG_MIPS_CP0_KSCRATCH1 ... KVM_REG_MIPS_CP0_KSCRATCH6:
		idx = reg->id - KVM_REG_MIPS_CP0_KSCRATCH1 + 2;
		if (!cpu_guest_has_kscr(idx))
			return -EINVAL;
		switch (idx) {
		case 2:
			write_gc0_kscratch1(v);
			break;
		case 3:
			write_gc0_kscratch2(v);
			break;
		case 4:
			write_gc0_kscratch3(v);
			break;
		case 5:
			write_gc0_kscratch4(v);
			break;
		case 6:
			write_gc0_kscratch5(v);
			break;
		case 7:
			write_gc0_kscratch6(v);
			break;
		}
		break;
	case KVM_REG_MIPS_COUNT_CTL:
		ret = kvm_mips_set_count_ctl(vcpu, v);
		break;
	case KVM_REG_MIPS_COUNT_RESUME:
		ret = kvm_mips_set_count_resume(vcpu, v);
		break;
	case KVM_REG_MIPS_COUNT_HZ:
		ret = kvm_mips_set_count_hz(vcpu, v);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

#define guestid_cache(cpu)	(cpu_data[cpu].guestid_cache)
static void kvm_vz_get_new_guestid(unsigned long cpu, struct kvm_vcpu *vcpu)
{
	unsigned long guestid = guestid_cache(cpu);

	if (!(++guestid & GUESTID_MASK)) {
		if (cpu_has_vtag_icache)
			flush_icache_all();

		if (!guestid)		/* fix version if needed */
			guestid = GUESTID_FIRST_VERSION;

		++guestid;		/* guestid 0 reserved for root */

		/* start new guestid cycle */
		kvm_vz_local_flush_roottlb_all_guests();
		kvm_vz_local_flush_guesttlb_all();
	}

	guestid_cache(cpu) = guestid;
}

/* Returns 1 if the guest TLB may be clobbered */
static int kvm_vz_check_requests(struct kvm_vcpu *vcpu, int cpu)
{
	int ret = 0;
	int i;

	if (!kvm_request_pending(vcpu))
		return 0;

	if (kvm_check_request(KVM_REQ_TLB_FLUSH, vcpu)) {
		if (cpu_has_guestid) {
			/* Drop all GuestIDs for this VCPU */
			for_each_possible_cpu(i)
				vcpu->arch.vzguestid[i] = 0;
			/* This will clobber guest TLB contents too */
			ret = 1;
		}
		/*
		 * For Root ASID Dealias (RAD) we don't do anything here, but we
		 * still need the request to ensure we recheck asid_flush_mask.
		 * We can still return 0 as only the root TLB will be affected
		 * by a root ASID flush.
		 */
	}

	return ret;
}

static void kvm_vz_vcpu_save_wired(struct kvm_vcpu *vcpu)
{
	unsigned int wired = read_gc0_wired();
	struct kvm_mips_tlb *tlbs;
	int i;

	/* Expand the wired TLB array if necessary */
	wired &= MIPSR6_WIRED_WIRED;
	if (wired > vcpu->arch.wired_tlb_limit) {
		tlbs = krealloc(vcpu->arch.wired_tlb, wired *
				sizeof(*vcpu->arch.wired_tlb), GFP_ATOMIC);
		if (WARN_ON(!tlbs)) {
			/* Save whatever we can */
			wired = vcpu->arch.wired_tlb_limit;
		} else {
			vcpu->arch.wired_tlb = tlbs;
			vcpu->arch.wired_tlb_limit = wired;
		}
	}

	if (wired)
		/* Save wired entries from the guest TLB */
		kvm_vz_save_guesttlb(vcpu->arch.wired_tlb, 0, wired);
	/* Invalidate any dropped entries since last time */
	for (i = wired; i < vcpu->arch.wired_tlb_used; ++i) {
		vcpu->arch.wired_tlb[i].tlb_hi = UNIQUE_GUEST_ENTRYHI(i);
		vcpu->arch.wired_tlb[i].tlb_lo[0] = 0;
		vcpu->arch.wired_tlb[i].tlb_lo[1] = 0;
		vcpu->arch.wired_tlb[i].tlb_mask = 0;
	}
	vcpu->arch.wired_tlb_used = wired;
}

static void kvm_vz_vcpu_load_wired(struct kvm_vcpu *vcpu)
{
	/* Load wired entries into the guest TLB */
	if (vcpu->arch.wired_tlb)
		kvm_vz_load_guesttlb(vcpu->arch.wired_tlb, 0,
				     vcpu->arch.wired_tlb_used);
}

static void kvm_vz_vcpu_load_tlb(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm *kvm = vcpu->kvm;
	struct mm_struct *gpa_mm = &kvm->arch.gpa_mm;
	bool migrated;

	/*
	 * Are we entering guest context on a different CPU to last time?
	 * If so, the VCPU's guest TLB state on this CPU may be stale.
	 */
	migrated = (vcpu->arch.last_exec_cpu != cpu);
	vcpu->arch.last_exec_cpu = cpu;

	/*
	 * A vcpu's GuestID is set in GuestCtl1.ID when the vcpu is loaded and
	 * remains set until another vcpu is loaded in.  As a rule GuestRID
	 * remains zeroed when in root context unless the kernel is busy
	 * manipulating guest tlb entries.
	 */
	if (cpu_has_guestid) {
		/*
		 * Check if our GuestID is of an older version and thus invalid.
		 *
		 * We also discard the stored GuestID if we've executed on
		 * another CPU, as the guest mappings may have changed without
		 * hypervisor knowledge.
		 */
		if (migrated ||
		    (vcpu->arch.vzguestid[cpu] ^ guestid_cache(cpu)) &
					GUESTID_VERSION_MASK) {
			kvm_vz_get_new_guestid(cpu, vcpu);
			vcpu->arch.vzguestid[cpu] = guestid_cache(cpu);
			trace_kvm_guestid_change(vcpu,
						 vcpu->arch.vzguestid[cpu]);
		}

		/* Restore GuestID */
		change_c0_guestctl1(GUESTID_MASK, vcpu->arch.vzguestid[cpu]);
	} else {
		/*
		 * The Guest TLB only stores a single guest's TLB state, so
		 * flush it if another VCPU has executed on this CPU.
		 *
		 * We also flush if we've executed on another CPU, as the guest
		 * mappings may have changed without hypervisor knowledge.
		 */
		if (migrated || last_exec_vcpu[cpu] != vcpu)
			kvm_vz_local_flush_guesttlb_all();
		last_exec_vcpu[cpu] = vcpu;

		/*
		 * Root ASID dealiases guest GPA mappings in the root TLB.
		 * Allocate new root ASID if needed.
		 */
		if (cpumask_test_and_clear_cpu(cpu, &kvm->arch.asid_flush_mask))
			get_new_mmu_context(gpa_mm);
		else
			check_mmu_context(gpa_mm);
	}
}

static int kvm_vz_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	bool migrated, all;

	/*
	 * Have we migrated to a different CPU?
	 * If so, any old guest TLB state may be stale.
	 */
	migrated = (vcpu->arch.last_sched_cpu != cpu);

	/*
	 * Was this the last VCPU to run on this CPU?
	 * If not, any old guest state from this VCPU will have been clobbered.
	 */
	all = migrated || (last_vcpu[cpu] != vcpu);
	last_vcpu[cpu] = vcpu;

	/*
	 * Restore CP0_Wired unconditionally as we clear it after use, and
	 * restore wired guest TLB entries (while in guest context).
	 */
	kvm_restore_gc0_wired(cop0);
	if (current->flags & PF_VCPU) {
		tlbw_use_hazard();
		kvm_vz_vcpu_load_tlb(vcpu, cpu);
		kvm_vz_vcpu_load_wired(vcpu);
	}

	/*
	 * Restore timer state regardless, as e.g. Cause.TI can change over time
	 * if left unmaintained.
	 */
	kvm_vz_restore_timer(vcpu);

	/* Set MC bit if we want to trace guest mode changes */
	if (kvm_trace_guest_mode_change)
		set_c0_guestctl0(MIPS_GCTL0_MC);
	else
		clear_c0_guestctl0(MIPS_GCTL0_MC);

	/* Don't bother restoring registers multiple times unless necessary */
	if (!all)
		return 0;

	/*
	 * Restore config registers first, as some implementations restrict
	 * writes to other registers when the corresponding feature bits aren't
	 * set. For example Status.CU1 cannot be set unless Config1.FP is set.
	 */
	kvm_restore_gc0_config(cop0);
	if (cpu_guest_has_conf1)
		kvm_restore_gc0_config1(cop0);
	if (cpu_guest_has_conf2)
		kvm_restore_gc0_config2(cop0);
	if (cpu_guest_has_conf3)
		kvm_restore_gc0_config3(cop0);
	if (cpu_guest_has_conf4)
		kvm_restore_gc0_config4(cop0);
	if (cpu_guest_has_conf5)
		kvm_restore_gc0_config5(cop0);
	if (cpu_guest_has_conf6)
		kvm_restore_gc0_config6(cop0);
	if (cpu_guest_has_conf7)
		kvm_restore_gc0_config7(cop0);

	kvm_restore_gc0_index(cop0);
	kvm_restore_gc0_entrylo0(cop0);
	kvm_restore_gc0_entrylo1(cop0);
	kvm_restore_gc0_context(cop0);
	if (cpu_guest_has_contextconfig)
		kvm_restore_gc0_contextconfig(cop0);
#ifdef CONFIG_64BIT
	kvm_restore_gc0_xcontext(cop0);
	if (cpu_guest_has_contextconfig)
		kvm_restore_gc0_xcontextconfig(cop0);
#endif
	kvm_restore_gc0_pagemask(cop0);
	kvm_restore_gc0_pagegrain(cop0);
	kvm_restore_gc0_hwrena(cop0);
	kvm_restore_gc0_badvaddr(cop0);
	kvm_restore_gc0_entryhi(cop0);
	kvm_restore_gc0_status(cop0);
	kvm_restore_gc0_intctl(cop0);
	kvm_restore_gc0_epc(cop0);
	kvm_vz_write_gc0_ebase(kvm_read_sw_gc0_ebase(cop0));
	if (cpu_guest_has_userlocal)
		kvm_restore_gc0_userlocal(cop0);

	kvm_restore_gc0_errorepc(cop0);

	/* restore KScratch registers if enabled in guest */
	if (cpu_guest_has_conf4) {
		if (cpu_guest_has_kscr(2))
			kvm_restore_gc0_kscratch1(cop0);
		if (cpu_guest_has_kscr(3))
			kvm_restore_gc0_kscratch2(cop0);
		if (cpu_guest_has_kscr(4))
			kvm_restore_gc0_kscratch3(cop0);
		if (cpu_guest_has_kscr(5))
			kvm_restore_gc0_kscratch4(cop0);
		if (cpu_guest_has_kscr(6))
			kvm_restore_gc0_kscratch5(cop0);
		if (cpu_guest_has_kscr(7))
			kvm_restore_gc0_kscratch6(cop0);
	}

	if (cpu_guest_has_badinstr)
		kvm_restore_gc0_badinstr(cop0);
	if (cpu_guest_has_badinstrp)
		kvm_restore_gc0_badinstrp(cop0);

	if (cpu_guest_has_segments) {
		kvm_restore_gc0_segctl0(cop0);
		kvm_restore_gc0_segctl1(cop0);
		kvm_restore_gc0_segctl2(cop0);
	}

	/* restore HTW registers */
	if (cpu_guest_has_htw || cpu_guest_has_ldpte) {
		kvm_restore_gc0_pwbase(cop0);
		kvm_restore_gc0_pwfield(cop0);
		kvm_restore_gc0_pwsize(cop0);
		kvm_restore_gc0_pwctl(cop0);
	}

	/* restore Root.GuestCtl2 from unused Guest guestctl2 register */
	if (cpu_has_guestctl2)
		write_c0_guestctl2(
			cop0->reg[MIPS_CP0_GUESTCTL2][MIPS_CP0_GUESTCTL2_SEL]);

	/*
	 * We should clear linked load bit to break interrupted atomics. This
	 * prevents a SC on the next VCPU from succeeding by matching a LL on
	 * the previous VCPU.
	 */
	if (vcpu->kvm->created_vcpus > 1)
		write_gc0_lladdr(0);

	return 0;
}

static int kvm_vz_vcpu_put(struct kvm_vcpu *vcpu, int cpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;

	if (current->flags & PF_VCPU)
		kvm_vz_vcpu_save_wired(vcpu);

	kvm_lose_fpu(vcpu);

	kvm_save_gc0_index(cop0);
	kvm_save_gc0_entrylo0(cop0);
	kvm_save_gc0_entrylo1(cop0);
	kvm_save_gc0_context(cop0);
	if (cpu_guest_has_contextconfig)
		kvm_save_gc0_contextconfig(cop0);
#ifdef CONFIG_64BIT
	kvm_save_gc0_xcontext(cop0);
	if (cpu_guest_has_contextconfig)
		kvm_save_gc0_xcontextconfig(cop0);
#endif
	kvm_save_gc0_pagemask(cop0);
	kvm_save_gc0_pagegrain(cop0);
	kvm_save_gc0_wired(cop0);
	/* allow wired TLB entries to be overwritten */
	clear_gc0_wired(MIPSR6_WIRED_WIRED);
	kvm_save_gc0_hwrena(cop0);
	kvm_save_gc0_badvaddr(cop0);
	kvm_save_gc0_entryhi(cop0);
	kvm_save_gc0_status(cop0);
	kvm_save_gc0_intctl(cop0);
	kvm_save_gc0_epc(cop0);
	kvm_write_sw_gc0_ebase(cop0, kvm_vz_read_gc0_ebase());
	if (cpu_guest_has_userlocal)
		kvm_save_gc0_userlocal(cop0);

	/* only save implemented config registers */
	kvm_save_gc0_config(cop0);
	if (cpu_guest_has_conf1)
		kvm_save_gc0_config1(cop0);
	if (cpu_guest_has_conf2)
		kvm_save_gc0_config2(cop0);
	if (cpu_guest_has_conf3)
		kvm_save_gc0_config3(cop0);
	if (cpu_guest_has_conf4)
		kvm_save_gc0_config4(cop0);
	if (cpu_guest_has_conf5)
		kvm_save_gc0_config5(cop0);
	if (cpu_guest_has_conf6)
		kvm_save_gc0_config6(cop0);
	if (cpu_guest_has_conf7)
		kvm_save_gc0_config7(cop0);

	kvm_save_gc0_errorepc(cop0);

	/* save KScratch registers if enabled in guest */
	if (cpu_guest_has_conf4) {
		if (cpu_guest_has_kscr(2))
			kvm_save_gc0_kscratch1(cop0);
		if (cpu_guest_has_kscr(3))
			kvm_save_gc0_kscratch2(cop0);
		if (cpu_guest_has_kscr(4))
			kvm_save_gc0_kscratch3(cop0);
		if (cpu_guest_has_kscr(5))
			kvm_save_gc0_kscratch4(cop0);
		if (cpu_guest_has_kscr(6))
			kvm_save_gc0_kscratch5(cop0);
		if (cpu_guest_has_kscr(7))
			kvm_save_gc0_kscratch6(cop0);
	}

	if (cpu_guest_has_badinstr)
		kvm_save_gc0_badinstr(cop0);
	if (cpu_guest_has_badinstrp)
		kvm_save_gc0_badinstrp(cop0);

	if (cpu_guest_has_segments) {
		kvm_save_gc0_segctl0(cop0);
		kvm_save_gc0_segctl1(cop0);
		kvm_save_gc0_segctl2(cop0);
	}

	/* save HTW registers if enabled in guest */
	if (cpu_guest_has_ldpte || (cpu_guest_has_htw &&
	    kvm_read_sw_gc0_config3(cop0) & MIPS_CONF3_PW)) {
		kvm_save_gc0_pwbase(cop0);
		kvm_save_gc0_pwfield(cop0);
		kvm_save_gc0_pwsize(cop0);
		kvm_save_gc0_pwctl(cop0);
	}

	kvm_vz_save_timer(vcpu);

	/* save Root.GuestCtl2 in unused Guest guestctl2 register */
	if (cpu_has_guestctl2)
		cop0->reg[MIPS_CP0_GUESTCTL2][MIPS_CP0_GUESTCTL2_SEL] =
			read_c0_guestctl2();

	return 0;
}

/**
 * kvm_vz_resize_guest_vtlb() - Attempt to resize guest VTLB.
 * @size:	Number of guest VTLB entries (0 < @size <= root VTLB entries).
 *
 * Attempt to resize the guest VTLB by writing guest Config registers. This is
 * necessary for cores with a shared root/guest TLB to avoid overlap with wired
 * entries in the root VTLB.
 *
 * Returns:	The resulting guest VTLB size.
 */
static unsigned int kvm_vz_resize_guest_vtlb(unsigned int size)
{
	unsigned int config4 = 0, ret = 0, limit;

	/* Write MMUSize - 1 into guest Config registers */
	if (cpu_guest_has_conf1)
		change_gc0_config1(MIPS_CONF1_TLBS,
				   (size - 1) << MIPS_CONF1_TLBS_SHIFT);
	if (cpu_guest_has_conf4) {
		config4 = read_gc0_config4();
		if (cpu_has_mips_r6 || (config4 & MIPS_CONF4_MMUEXTDEF) ==
		    MIPS_CONF4_MMUEXTDEF_VTLBSIZEEXT) {
			config4 &= ~MIPS_CONF4_VTLBSIZEEXT;
			config4 |= ((size - 1) >> MIPS_CONF1_TLBS_SIZE) <<
				MIPS_CONF4_VTLBSIZEEXT_SHIFT;
		} else if ((config4 & MIPS_CONF4_MMUEXTDEF) ==
			   MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT) {
			config4 &= ~MIPS_CONF4_MMUSIZEEXT;
			config4 |= ((size - 1) >> MIPS_CONF1_TLBS_SIZE) <<
				MIPS_CONF4_MMUSIZEEXT_SHIFT;
		}
		write_gc0_config4(config4);
	}

	/*
	 * Set Guest.Wired.Limit = 0 (no limit up to Guest.MMUSize-1), unless it
	 * would exceed Root.Wired.Limit (clearing Guest.Wired.Wired so write
	 * not dropped)
	 */
	if (cpu_has_mips_r6) {
		limit = (read_c0_wired() & MIPSR6_WIRED_LIMIT) >>
						MIPSR6_WIRED_LIMIT_SHIFT;
		if (size - 1 <= limit)
			limit = 0;
		write_gc0_wired(limit << MIPSR6_WIRED_LIMIT_SHIFT);
	}

	/* Read back MMUSize - 1 */
	back_to_back_c0_hazard();
	if (cpu_guest_has_conf1)
		ret = (read_gc0_config1() & MIPS_CONF1_TLBS) >>
						MIPS_CONF1_TLBS_SHIFT;
	if (config4) {
		if (cpu_has_mips_r6 || (config4 & MIPS_CONF4_MMUEXTDEF) ==
		    MIPS_CONF4_MMUEXTDEF_VTLBSIZEEXT)
			ret |= ((config4 & MIPS_CONF4_VTLBSIZEEXT) >>
				MIPS_CONF4_VTLBSIZEEXT_SHIFT) <<
				MIPS_CONF1_TLBS_SIZE;
		else if ((config4 & MIPS_CONF4_MMUEXTDEF) ==
			 MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT)
			ret |= ((config4 & MIPS_CONF4_MMUSIZEEXT) >>
				MIPS_CONF4_MMUSIZEEXT_SHIFT) <<
				MIPS_CONF1_TLBS_SIZE;
	}
	return ret + 1;
}

static int kvm_vz_hardware_enable(void)
{
	unsigned int mmu_size, guest_mmu_size, ftlb_size;
	u64 guest_cvmctl, cvmvmconfig;

	switch (current_cpu_type()) {
	case CPU_CAVIUM_OCTEON3:
		/* Set up guest timer/perfcount IRQ lines */
		guest_cvmctl = read_gc0_cvmctl();
		guest_cvmctl &= ~CVMCTL_IPTI;
		guest_cvmctl |= 7ull << CVMCTL_IPTI_SHIFT;
		guest_cvmctl &= ~CVMCTL_IPPCI;
		guest_cvmctl |= 6ull << CVMCTL_IPPCI_SHIFT;
		write_gc0_cvmctl(guest_cvmctl);

		cvmvmconfig = read_c0_cvmvmconfig();
		/* No I/O hole translation. */
		cvmvmconfig |= CVMVMCONF_DGHT;
		/* Halve the root MMU size */
		mmu_size = ((cvmvmconfig & CVMVMCONF_MMUSIZEM1)
			    >> CVMVMCONF_MMUSIZEM1_S) + 1;
		guest_mmu_size = mmu_size / 2;
		mmu_size -= guest_mmu_size;
		cvmvmconfig &= ~CVMVMCONF_RMMUSIZEM1;
		cvmvmconfig |= mmu_size - 1;
		write_c0_cvmvmconfig(cvmvmconfig);

		/* Update our records */
		current_cpu_data.tlbsize = mmu_size;
		current_cpu_data.tlbsizevtlb = mmu_size;
		current_cpu_data.guest.tlbsize = guest_mmu_size;

		/* Flush moved entries in new (guest) context */
		kvm_vz_local_flush_guesttlb_all();
		break;
	default:
		/*
		 * ImgTec cores tend to use a shared root/guest TLB. To avoid
		 * overlap of root wired and guest entries, the guest TLB may
		 * need resizing.
		 */
		mmu_size = current_cpu_data.tlbsizevtlb;
		ftlb_size = current_cpu_data.tlbsize - mmu_size;

		/* Try switching to maximum guest VTLB size for flush */
		guest_mmu_size = kvm_vz_resize_guest_vtlb(mmu_size);
		current_cpu_data.guest.tlbsize = guest_mmu_size + ftlb_size;
		kvm_vz_local_flush_guesttlb_all();

		/*
		 * Reduce to make space for root wired entries and at least 2
		 * root non-wired entries. This does assume that long-term wired
		 * entries won't be added later.
		 */
		guest_mmu_size = mmu_size - num_wired_entries() - 2;
		guest_mmu_size = kvm_vz_resize_guest_vtlb(guest_mmu_size);
		current_cpu_data.guest.tlbsize = guest_mmu_size + ftlb_size;

		/*
		 * Write the VTLB size, but if another CPU has already written,
		 * check it matches or we won't provide a consistent view to the
		 * guest. If this ever happens it suggests an asymmetric number
		 * of wired entries.
		 */
		if (cmpxchg(&kvm_vz_guest_vtlb_size, 0, guest_mmu_size) &&
		    WARN(guest_mmu_size != kvm_vz_guest_vtlb_size,
			 "Available guest VTLB size mismatch"))
			return -EINVAL;
		break;
	}

	/*
	 * Enable virtualization features granting guest direct control of
	 * certain features:
	 * CP0=1:	Guest coprocessor 0 context.
	 * AT=Guest:	Guest MMU.
	 * CG=1:	Hit (virtual address) CACHE operations (optional).
	 * CF=1:	Guest Config registers.
	 * CGI=1:	Indexed flush CACHE operations (optional).
	 */
	write_c0_guestctl0(MIPS_GCTL0_CP0 |
			   (MIPS_GCTL0_AT_GUEST << MIPS_GCTL0_AT_SHIFT) |
			   MIPS_GCTL0_CG | MIPS_GCTL0_CF);
	if (cpu_has_guestctl0ext) {
		if (current_cpu_type() != CPU_LOONGSON64)
			set_c0_guestctl0ext(MIPS_GCTL0EXT_CGI);
		else
			clear_c0_guestctl0ext(MIPS_GCTL0EXT_CGI);
	}

	if (cpu_has_guestid) {
		write_c0_guestctl1(0);
		kvm_vz_local_flush_roottlb_all_guests();

		GUESTID_MASK = current_cpu_data.guestid_mask;
		GUESTID_FIRST_VERSION = GUESTID_MASK + 1;
		GUESTID_VERSION_MASK = ~GUESTID_MASK;

		current_cpu_data.guestid_cache = GUESTID_FIRST_VERSION;
	}

	/* clear any pending injected virtual guest interrupts */
	if (cpu_has_guestctl2)
		clear_c0_guestctl2(0x3f << 10);

#ifdef CONFIG_CPU_LOONGSON64
	/* Control guest CCA attribute */
	if (cpu_has_csr())
		csr_writel(csr_readl(0xffffffec) | 0x1, 0xffffffec);
#endif

	return 0;
}

static void kvm_vz_hardware_disable(void)
{
	u64 cvmvmconfig;
	unsigned int mmu_size;

	/* Flush any remaining guest TLB entries */
	kvm_vz_local_flush_guesttlb_all();

	switch (current_cpu_type()) {
	case CPU_CAVIUM_OCTEON3:
		/*
		 * Allocate whole TLB for root. Existing guest TLB entries will
		 * change ownership to the root TLB. We should be safe though as
		 * they've already been flushed above while in guest TLB.
		 */
		cvmvmconfig = read_c0_cvmvmconfig();
		mmu_size = ((cvmvmconfig & CVMVMCONF_MMUSIZEM1)
			    >> CVMVMCONF_MMUSIZEM1_S) + 1;
		cvmvmconfig &= ~CVMVMCONF_RMMUSIZEM1;
		cvmvmconfig |= mmu_size - 1;
		write_c0_cvmvmconfig(cvmvmconfig);

		/* Update our records */
		current_cpu_data.tlbsize = mmu_size;
		current_cpu_data.tlbsizevtlb = mmu_size;
		current_cpu_data.guest.tlbsize = 0;

		/* Flush moved entries in new (root) context */
		local_flush_tlb_all();
		break;
	}

	if (cpu_has_guestid) {
		write_c0_guestctl1(0);
		kvm_vz_local_flush_roottlb_all_guests();
	}
}

static int kvm_vz_check_extension(struct kvm *kvm, long ext)
{
	int r;

	switch (ext) {
	case KVM_CAP_MIPS_VZ:
		/* we wouldn't be here unless cpu_has_vz */
		r = 1;
		break;
#ifdef CONFIG_64BIT
	case KVM_CAP_MIPS_64BIT:
		/* We support 64-bit registers/operations and addresses */
		r = 2;
		break;
#endif
	case KVM_CAP_IOEVENTFD:
		r = 1;
		break;
	default:
		r = 0;
		break;
	}

	return r;
}

static int kvm_vz_vcpu_init(struct kvm_vcpu *vcpu)
{
	int i;

	for_each_possible_cpu(i)
		vcpu->arch.vzguestid[i] = 0;

	return 0;
}

static void kvm_vz_vcpu_uninit(struct kvm_vcpu *vcpu)
{
	int cpu;

	/*
	 * If the VCPU is freed and reused as another VCPU, we don't want the
	 * matching pointer wrongly hanging around in last_vcpu[] or
	 * last_exec_vcpu[].
	 */
	for_each_possible_cpu(cpu) {
		if (last_vcpu[cpu] == vcpu)
			last_vcpu[cpu] = NULL;
		if (last_exec_vcpu[cpu] == vcpu)
			last_exec_vcpu[cpu] = NULL;
	}
}

static int kvm_vz_vcpu_setup(struct kvm_vcpu *vcpu)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	unsigned long count_hz = 100*1000*1000; /* default to 100 MHz */

	/*
	 * Start off the timer at the same frequency as the host timer, but the
	 * soft timer doesn't handle frequencies greater than 1GHz yet.
	 */
	if (mips_hpt_frequency && mips_hpt_frequency <= NSEC_PER_SEC)
		count_hz = mips_hpt_frequency;
	kvm_mips_init_count(vcpu, count_hz);

	/*
	 * Initialize guest register state to valid architectural reset state.
	 */

	/* PageGrain */
	if (cpu_has_mips_r5 || cpu_has_mips_r6)
		kvm_write_sw_gc0_pagegrain(cop0, PG_RIE | PG_XIE | PG_IEC);
	/* Wired */
	if (cpu_has_mips_r6)
		kvm_write_sw_gc0_wired(cop0,
				       read_gc0_wired() & MIPSR6_WIRED_LIMIT);
	/* Status */
	kvm_write_sw_gc0_status(cop0, ST0_BEV | ST0_ERL);
	if (cpu_has_mips_r5 || cpu_has_mips_r6)
		kvm_change_sw_gc0_status(cop0, ST0_FR, read_gc0_status());
	/* IntCtl */
	kvm_write_sw_gc0_intctl(cop0, read_gc0_intctl() &
				(INTCTLF_IPFDC | INTCTLF_IPPCI | INTCTLF_IPTI));
	/* PRId */
	kvm_write_sw_gc0_prid(cop0, boot_cpu_data.processor_id);
	/* EBase */
	kvm_write_sw_gc0_ebase(cop0, (s32)0x80000000 | vcpu->vcpu_id);
	/* Config */
	kvm_save_gc0_config(cop0);
	/* architecturally writable (e.g. from guest) */
	kvm_change_sw_gc0_config(cop0, CONF_CM_CMASK,
				 _page_cachable_default >> _CACHE_SHIFT);
	/* architecturally read only, but maybe writable from root */
	kvm_change_sw_gc0_config(cop0, MIPS_CONF_MT, read_c0_config());
	if (cpu_guest_has_conf1) {
		kvm_set_sw_gc0_config(cop0, MIPS_CONF_M);
		/* Config1 */
		kvm_save_gc0_config1(cop0);
		/* architecturally read only, but maybe writable from root */
		kvm_clear_sw_gc0_config1(cop0, MIPS_CONF1_C2	|
					       MIPS_CONF1_MD	|
					       MIPS_CONF1_PC	|
					       MIPS_CONF1_WR	|
					       MIPS_CONF1_CA	|
					       MIPS_CONF1_FP);
	}
	if (cpu_guest_has_conf2) {
		kvm_set_sw_gc0_config1(cop0, MIPS_CONF_M);
		/* Config2 */
		kvm_save_gc0_config2(cop0);
	}
	if (cpu_guest_has_conf3) {
		kvm_set_sw_gc0_config2(cop0, MIPS_CONF_M);
		/* Config3 */
		kvm_save_gc0_config3(cop0);
		/* architecturally writable (e.g. from guest) */
		kvm_clear_sw_gc0_config3(cop0, MIPS_CONF3_ISA_OE);
		/* architecturally read only, but maybe writable from root */
		kvm_clear_sw_gc0_config3(cop0, MIPS_CONF3_MSA	|
					       MIPS_CONF3_BPG	|
					       MIPS_CONF3_ULRI	|
					       MIPS_CONF3_DSP	|
					       MIPS_CONF3_CTXTC	|
					       MIPS_CONF3_ITL	|
					       MIPS_CONF3_LPA	|
					       MIPS_CONF3_VEIC	|
					       MIPS_CONF3_VINT	|
					       MIPS_CONF3_SP	|
					       MIPS_CONF3_CDMM	|
					       MIPS_CONF3_MT	|
					       MIPS_CONF3_SM	|
					       MIPS_CONF3_TL);
	}
	if (cpu_guest_has_conf4) {
		kvm_set_sw_gc0_config3(cop0, MIPS_CONF_M);
		/* Config4 */
		kvm_save_gc0_config4(cop0);
	}
	if (cpu_guest_has_conf5) {
		kvm_set_sw_gc0_config4(cop0, MIPS_CONF_M);
		/* Config5 */
		kvm_save_gc0_config5(cop0);
		/* architecturally writable (e.g. from guest) */
		kvm_clear_sw_gc0_config5(cop0, MIPS_CONF5_K	|
					       MIPS_CONF5_CV	|
					       MIPS_CONF5_MSAEN	|
					       MIPS_CONF5_UFE	|
					       MIPS_CONF5_FRE	|
					       MIPS_CONF5_SBRI	|
					       MIPS_CONF5_UFR);
		/* architecturally read only, but maybe writable from root */
		kvm_clear_sw_gc0_config5(cop0, MIPS_CONF5_MRP);
	}

	if (cpu_guest_has_contextconfig) {
		/* ContextConfig */
		kvm_write_sw_gc0_contextconfig(cop0, 0x007ffff0);
#ifdef CONFIG_64BIT
		/* XContextConfig */
		/* bits SEGBITS-13+3:4 set */
		kvm_write_sw_gc0_xcontextconfig(cop0,
					((1ull << (cpu_vmbits - 13)) - 1) << 4);
#endif
	}

	/* Implementation dependent, use the legacy layout */
	if (cpu_guest_has_segments) {
		/* SegCtl0, SegCtl1, SegCtl2 */
		kvm_write_sw_gc0_segctl0(cop0, 0x00200010);
		kvm_write_sw_gc0_segctl1(cop0, 0x00000002 |
				(_page_cachable_default >> _CACHE_SHIFT) <<
						(16 + MIPS_SEGCFG_C_SHIFT));
		kvm_write_sw_gc0_segctl2(cop0, 0x00380438);
	}

	/* reset HTW registers */
	if (cpu_guest_has_htw && (cpu_has_mips_r5 || cpu_has_mips_r6)) {
		/* PWField */
		kvm_write_sw_gc0_pwfield(cop0, 0x0c30c302);
		/* PWSize */
		kvm_write_sw_gc0_pwsize(cop0, 1 << MIPS_PWSIZE_PTW_SHIFT);
	}

	/* start with no pending virtual guest interrupts */
	if (cpu_has_guestctl2)
		cop0->reg[MIPS_CP0_GUESTCTL2][MIPS_CP0_GUESTCTL2_SEL] = 0;

	/* Put PC at reset vector */
	vcpu->arch.pc = CKSEG1ADDR(0x1fc00000);

	return 0;
}

static void kvm_vz_prepare_flush_shadow(struct kvm *kvm)
{
	if (!cpu_has_guestid) {
		/*
		 * For each CPU there is a single GPA ASID used by all VCPUs in
		 * the VM, so it doesn't make sense for the VCPUs to handle
		 * invalidation of these ASIDs individually.
		 *
		 * Instead mark all CPUs as needing ASID invalidation in
		 * asid_flush_mask, and kvm_flush_remote_tlbs(kvm) will
		 * kick any running VCPUs so they check asid_flush_mask.
		 */
		cpumask_setall(&kvm->arch.asid_flush_mask);
	}
}

static void kvm_vz_vcpu_reenter(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();
	int preserve_guest_tlb;

	preserve_guest_tlb = kvm_vz_check_requests(vcpu, cpu);

	if (preserve_guest_tlb)
		kvm_vz_vcpu_save_wired(vcpu);

	kvm_vz_vcpu_load_tlb(vcpu, cpu);

	if (preserve_guest_tlb)
		kvm_vz_vcpu_load_wired(vcpu);
}

static int kvm_vz_vcpu_run(struct kvm_vcpu *vcpu)
{
	int cpu = smp_processor_id();
	int r;

	kvm_vz_acquire_htimer(vcpu);
	/* Check if we have any exceptions/interrupts pending */
	kvm_mips_deliver_interrupts(vcpu, read_gc0_cause());

	kvm_vz_check_requests(vcpu, cpu);
	kvm_vz_vcpu_load_tlb(vcpu, cpu);
	kvm_vz_vcpu_load_wired(vcpu);

	r = vcpu->arch.vcpu_run(vcpu);

	kvm_vz_vcpu_save_wired(vcpu);

	return r;
}

static struct kvm_mips_callbacks kvm_vz_callbacks = {
	.handle_cop_unusable = kvm_trap_vz_handle_cop_unusable,
	.handle_tlb_mod = kvm_trap_vz_handle_tlb_st_miss,
	.handle_tlb_ld_miss = kvm_trap_vz_handle_tlb_ld_miss,
	.handle_tlb_st_miss = kvm_trap_vz_handle_tlb_st_miss,
	.handle_addr_err_st = kvm_trap_vz_no_handler,
	.handle_addr_err_ld = kvm_trap_vz_no_handler,
	.handle_syscall = kvm_trap_vz_no_handler,
	.handle_res_inst = kvm_trap_vz_no_handler,
	.handle_break = kvm_trap_vz_no_handler,
	.handle_msa_disabled = kvm_trap_vz_handle_msa_disabled,
	.handle_guest_exit = kvm_trap_vz_handle_guest_exit,

	.hardware_enable = kvm_vz_hardware_enable,
	.hardware_disable = kvm_vz_hardware_disable,
	.check_extension = kvm_vz_check_extension,
	.vcpu_init = kvm_vz_vcpu_init,
	.vcpu_uninit = kvm_vz_vcpu_uninit,
	.vcpu_setup = kvm_vz_vcpu_setup,
	.prepare_flush_shadow = kvm_vz_prepare_flush_shadow,
	.gva_to_gpa = kvm_vz_gva_to_gpa_cb,
	.queue_timer_int = kvm_vz_queue_timer_int_cb,
	.dequeue_timer_int = kvm_vz_dequeue_timer_int_cb,
	.queue_io_int = kvm_vz_queue_io_int_cb,
	.dequeue_io_int = kvm_vz_dequeue_io_int_cb,
	.irq_deliver = kvm_vz_irq_deliver_cb,
	.irq_clear = kvm_vz_irq_clear_cb,
	.num_regs = kvm_vz_num_regs,
	.copy_reg_indices = kvm_vz_copy_reg_indices,
	.get_one_reg = kvm_vz_get_one_reg,
	.set_one_reg = kvm_vz_set_one_reg,
	.vcpu_load = kvm_vz_vcpu_load,
	.vcpu_put = kvm_vz_vcpu_put,
	.vcpu_run = kvm_vz_vcpu_run,
	.vcpu_reenter = kvm_vz_vcpu_reenter,
};

int kvm_mips_emulation_init(struct kvm_mips_callbacks **install_callbacks)
{
	if (!cpu_has_vz)
		return -ENODEV;

	/*
	 * VZ requires at least 2 KScratch registers, so it should have been
	 * possible to allocate pgd_reg.
	 */
	if (WARN(pgd_reg == -1,
		 "pgd_reg not allocated even though cpu_has_vz\n"))
		return -ENODEV;

	pr_info("Starting KVM with MIPS VZ extensions\n");

	*install_callbacks = &kvm_vz_callbacks;
	return 0;
}
