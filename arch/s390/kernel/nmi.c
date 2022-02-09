// SPDX-License-Identifier: GPL-2.0
/*
 *   Machine check handler
 *
 *    Copyright IBM Corp. 2000, 2009
 *    Author(s): Ingo Adlung <adlung@de.ibm.com>,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Cornelia Huck <cornelia.huck@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
 */

#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/hardirq.h>
#include <linux/log2.h>
#include <linux/kprobes.h>
#include <linux/kmemleak.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/sched/signal.h>

#include <linux/export.h>
#include <asm/lowcore.h>
#include <asm/smp.h>
#include <asm/stp.h>
#include <asm/cputime.h>
#include <asm/nmi.h>
#include <asm/crw.h>
#include <asm/switch_to.h>
#include <asm/ctl_reg.h>
#include <asm/asm-offsets.h>
#include <linux/kvm_host.h>

struct mcck_struct {
	unsigned int kill_task : 1;
	unsigned int channel_report : 1;
	unsigned int warning : 1;
	unsigned int stp_queue : 1;
	unsigned long mcck_code;
};

static DEFINE_PER_CPU(struct mcck_struct, cpu_mcck);
static struct kmem_cache *mcesa_cache;
static unsigned long mcesa_origin_lc;

static inline int nmi_needs_mcesa(void)
{
	return MACHINE_HAS_VX || MACHINE_HAS_GS;
}

static inline unsigned long nmi_get_mcesa_size(void)
{
	if (MACHINE_HAS_GS)
		return MCESA_MAX_SIZE;
	return MCESA_MIN_SIZE;
}

/*
 * The initial machine check extended save area for the boot CPU.
 * It will be replaced on the boot CPU reinit with an allocated
 * structure. The structure is required for machine check happening
 * early in the boot process.
 */
static struct mcesa boot_mcesa __initdata __aligned(MCESA_MAX_SIZE);

void __init nmi_alloc_mcesa_early(u64 *mcesad)
{
	if (!nmi_needs_mcesa())
		return;
	*mcesad = __pa(&boot_mcesa);
	if (MACHINE_HAS_GS)
		*mcesad |= ilog2(MCESA_MAX_SIZE);
}

static void __init nmi_alloc_cache(void)
{
	unsigned long size;

	if (!nmi_needs_mcesa())
		return;
	size = nmi_get_mcesa_size();
	if (size > MCESA_MIN_SIZE)
		mcesa_origin_lc = ilog2(size);
	/* create slab cache for the machine-check-extended-save-areas */
	mcesa_cache = kmem_cache_create("nmi_save_areas", size, size, 0, NULL);
	if (!mcesa_cache)
		panic("Couldn't create nmi save area cache");
}

int __ref nmi_alloc_mcesa(u64 *mcesad)
{
	unsigned long origin;

	*mcesad = 0;
	if (!nmi_needs_mcesa())
		return 0;
	if (!mcesa_cache)
		nmi_alloc_cache();
	origin = (unsigned long) kmem_cache_alloc(mcesa_cache, GFP_KERNEL);
	if (!origin)
		return -ENOMEM;
	/* The pointer is stored with mcesa_bits ORed in */
	kmemleak_not_leak((void *) origin);
	*mcesad = __pa(origin) | mcesa_origin_lc;
	return 0;
}

void nmi_free_mcesa(u64 *mcesad)
{
	if (!nmi_needs_mcesa())
		return;
	kmem_cache_free(mcesa_cache, __va(*mcesad & MCESA_ORIGIN_MASK));
}

static notrace void s390_handle_damage(void)
{
	smp_emergency_stop();
	disabled_wait();
	while (1);
}
NOKPROBE_SYMBOL(s390_handle_damage);

/*
 * Main machine check handler function. Will be called with interrupts disabled
 * and machine checks enabled.
 */
void __s390_handle_mcck(void)
{
	struct mcck_struct mcck;

	/*
	 * Disable machine checks and get the current state of accumulated
	 * machine checks. Afterwards delete the old state and enable machine
	 * checks again.
	 */
	local_mcck_disable();
	mcck = *this_cpu_ptr(&cpu_mcck);
	memset(this_cpu_ptr(&cpu_mcck), 0, sizeof(mcck));
	local_mcck_enable();

	if (mcck.channel_report)
		crw_handle_channel_report();
	/*
	 * A warning may remain for a prolonged period on the bare iron.
	 * (actually until the machine is powered off, or the problem is gone)
	 * So we just stop listening for the WARNING MCH and avoid continuously
	 * being interrupted.  One caveat is however, that we must do this per
	 * processor and cannot use the smp version of ctl_clear_bit().
	 * On VM we only get one interrupt per virtally presented machinecheck.
	 * Though one suffices, we may get one interrupt per (virtual) cpu.
	 */
	if (mcck.warning) {	/* WARNING pending ? */
		static int mchchk_wng_posted = 0;

		/* Use single cpu clear, as we cannot handle smp here. */
		__ctl_clear_bit(14, 24);	/* Disable WARNING MCH */
		if (xchg(&mchchk_wng_posted, 1) == 0)
			kill_cad_pid(SIGPWR, 1);
	}
	if (mcck.stp_queue)
		stp_queue_work();
	if (mcck.kill_task) {
		local_irq_enable();
		printk(KERN_EMERG "mcck: Terminating task because of machine "
		       "malfunction (code 0x%016lx).\n", mcck.mcck_code);
		printk(KERN_EMERG "mcck: task: %s, pid: %d.\n",
		       current->comm, current->pid);
		make_task_dead(SIGSEGV);
	}
}

void noinstr s390_handle_mcck(void)
{
	trace_hardirqs_off();
	__s390_handle_mcck();
	trace_hardirqs_on();
}
/*
 * returns 0 if all required registers are available
 * returns 1 otherwise
 */
static int notrace s390_validate_registers(union mci mci, int umode)
{
	struct mcesa *mcesa;
	void *fpt_save_area;
	union ctlreg2 cr2;
	int kill_task;
	u64 zero;

	kill_task = 0;
	zero = 0;

	if (!mci.gr) {
		/*
		 * General purpose registers couldn't be restored and have
		 * unknown contents. Stop system or terminate process.
		 */
		if (!umode)
			s390_handle_damage();
		kill_task = 1;
	}
	if (!mci.fp) {
		/*
		 * Floating point registers can't be restored. If the
		 * kernel currently uses floating point registers the
		 * system is stopped. If the process has its floating
		 * pointer registers loaded it is terminated.
		 */
		if (S390_lowcore.fpu_flags & KERNEL_VXR_V0V7)
			s390_handle_damage();
		if (!test_cpu_flag(CIF_FPU))
			kill_task = 1;
	}
	fpt_save_area = &S390_lowcore.floating_pt_save_area;
	if (!mci.fc) {
		/*
		 * Floating point control register can't be restored.
		 * If the kernel currently uses the floating pointer
		 * registers and needs the FPC register the system is
		 * stopped. If the process has its floating pointer
		 * registers loaded it is terminated. Otherwise the
		 * FPC is just validated.
		 */
		if (S390_lowcore.fpu_flags & KERNEL_FPC)
			s390_handle_damage();
		asm volatile(
			"	lfpc	%0\n"
			:
			: "Q" (zero));
		if (!test_cpu_flag(CIF_FPU))
			kill_task = 1;
	} else {
		asm volatile(
			"	lfpc	%0\n"
			:
			: "Q" (S390_lowcore.fpt_creg_save_area));
	}

	mcesa = __va(S390_lowcore.mcesad & MCESA_ORIGIN_MASK);
	if (!MACHINE_HAS_VX) {
		/* Validate floating point registers */
		asm volatile(
			"	ld	0,0(%0)\n"
			"	ld	1,8(%0)\n"
			"	ld	2,16(%0)\n"
			"	ld	3,24(%0)\n"
			"	ld	4,32(%0)\n"
			"	ld	5,40(%0)\n"
			"	ld	6,48(%0)\n"
			"	ld	7,56(%0)\n"
			"	ld	8,64(%0)\n"
			"	ld	9,72(%0)\n"
			"	ld	10,80(%0)\n"
			"	ld	11,88(%0)\n"
			"	ld	12,96(%0)\n"
			"	ld	13,104(%0)\n"
			"	ld	14,112(%0)\n"
			"	ld	15,120(%0)\n"
			:
			: "a" (fpt_save_area)
			: "memory");
	} else {
		/* Validate vector registers */
		union ctlreg0 cr0;

		/*
		 * The vector validity must only be checked if not running a
		 * KVM guest. For KVM guests the machine check is forwarded by
		 * KVM and it is the responsibility of the guest to take
		 * appropriate actions. The host vector or FPU values have been
		 * saved by KVM and will be restored by KVM.
		 */
		if (!mci.vr && !test_cpu_flag(CIF_MCCK_GUEST)) {
			/*
			 * Vector registers can't be restored. If the kernel
			 * currently uses vector registers the system is
			 * stopped. If the process has its vector registers
			 * loaded it is terminated. Otherwise just validate
			 * the registers.
			 */
			if (S390_lowcore.fpu_flags & KERNEL_VXR)
				s390_handle_damage();
			if (!test_cpu_flag(CIF_FPU))
				kill_task = 1;
		}
		cr0.val = S390_lowcore.cregs_save_area[0];
		cr0.afp = cr0.vx = 1;
		__ctl_load(cr0.val, 0, 0);
		asm volatile(
			"	la	1,%0\n"
			"	.word	0xe70f,0x1000,0x0036\n" /* vlm 0,15,0(1) */
			"	.word	0xe70f,0x1100,0x0c36\n" /* vlm 16,31,256(1) */
			:
			: "Q" (*(struct vx_array *)mcesa->vector_save_area)
			: "1");
		__ctl_load(S390_lowcore.cregs_save_area[0], 0, 0);
	}
	/* Validate access registers */
	asm volatile(
		"	lam	0,15,0(%0)\n"
		:
		: "a" (&S390_lowcore.access_regs_save_area)
		: "memory");
	if (!mci.ar) {
		/*
		 * Access registers have unknown contents.
		 * Terminating task.
		 */
		kill_task = 1;
	}
	/* Validate guarded storage registers */
	cr2.val = S390_lowcore.cregs_save_area[2];
	if (cr2.gse) {
		if (!mci.gs) {
			/*
			 * 2 cases:
			 * - machine check in kernel or userspace
			 * - machine check while running SIE (KVM guest)
			 * For kernel or userspace the userspace values of
			 * guarded storage control can not be recreated, the
			 * process must be terminated.
			 * For SIE the guest values of guarded storage can not
			 * be recreated. This is either due to a bug or due to
			 * GS being disabled in the guest. The guest will be
			 * notified by KVM code and the guests machine check
			 * handling must take care of this.  The host values
			 * are saved by KVM and are not affected.
			 */
			if (!test_cpu_flag(CIF_MCCK_GUEST))
				kill_task = 1;
		} else {
			load_gs_cb((struct gs_cb *)mcesa->guarded_storage_save_area);
		}
	}
	/*
	 * The getcpu vdso syscall reads CPU number from the programmable
	 * field of the TOD clock. Disregard the TOD programmable register
	 * validity bit and load the CPU number into the TOD programmable
	 * field unconditionally.
	 */
	set_tod_programmable_field(raw_smp_processor_id());
	/* Validate clock comparator register */
	set_clock_comparator(S390_lowcore.clock_comparator);

	if (!mci.ms || !mci.pm || !mci.ia)
		kill_task = 1;

	return kill_task;
}
NOKPROBE_SYMBOL(s390_validate_registers);

/*
 * Backup the guest's machine check info to its description block
 */
static void notrace s390_backup_mcck_info(struct pt_regs *regs)
{
	struct mcck_volatile_info *mcck_backup;
	struct sie_page *sie_page;

	/* r14 contains the sie block, which was set in sie64a */
	struct kvm_s390_sie_block *sie_block =
			(struct kvm_s390_sie_block *) regs->gprs[14];

	if (sie_block == NULL)
		/* Something's seriously wrong, stop system. */
		s390_handle_damage();

	sie_page = container_of(sie_block, struct sie_page, sie_block);
	mcck_backup = &sie_page->mcck_info;
	mcck_backup->mcic = S390_lowcore.mcck_interruption_code &
				~(MCCK_CODE_CP | MCCK_CODE_EXT_DAMAGE);
	mcck_backup->ext_damage_code = S390_lowcore.external_damage_code;
	mcck_backup->failing_storage_address
			= S390_lowcore.failing_storage_address;
}
NOKPROBE_SYMBOL(s390_backup_mcck_info);

#define MAX_IPD_COUNT	29
#define MAX_IPD_TIME	(5 * 60 * USEC_PER_SEC) /* 5 minutes */

#define ED_STP_ISLAND	6	/* External damage STP island check */
#define ED_STP_SYNC	7	/* External damage STP sync check */

#define MCCK_CODE_NO_GUEST	(MCCK_CODE_CP | MCCK_CODE_EXT_DAMAGE)

/*
 * machine check handler.
 */
int notrace s390_do_machine_check(struct pt_regs *regs)
{
	static int ipd_count;
	static DEFINE_SPINLOCK(ipd_lock);
	static unsigned long long last_ipd;
	struct mcck_struct *mcck;
	unsigned long long tmp;
	union mci mci;
	unsigned long mcck_dam_code;
	int mcck_pending = 0;

	nmi_enter();

	if (user_mode(regs))
		update_timer_mcck();
	inc_irq_stat(NMI_NMI);
	mci.val = S390_lowcore.mcck_interruption_code;
	mcck = this_cpu_ptr(&cpu_mcck);

	/*
	 * Reinject the instruction processing damages' machine checks
	 * including Delayed Access Exception into the guest
	 * instead of damaging the host if they happen in the guest.
	 */
	if (mci.pd && !test_cpu_flag(CIF_MCCK_GUEST)) {
		if (mci.b) {
			/* Processing backup -> verify if we can survive this */
			u64 z_mcic, o_mcic, t_mcic;
			z_mcic = (1ULL<<63 | 1ULL<<59 | 1ULL<<29);
			o_mcic = (1ULL<<43 | 1ULL<<42 | 1ULL<<41 | 1ULL<<40 |
				  1ULL<<36 | 1ULL<<35 | 1ULL<<34 | 1ULL<<32 |
				  1ULL<<30 | 1ULL<<21 | 1ULL<<20 | 1ULL<<17 |
				  1ULL<<16);
			t_mcic = mci.val;

			if (((t_mcic & z_mcic) != 0) ||
			    ((t_mcic & o_mcic) != o_mcic)) {
				s390_handle_damage();
			}

			/*
			 * Nullifying exigent condition, therefore we might
			 * retry this instruction.
			 */
			spin_lock(&ipd_lock);
			tmp = get_tod_clock();
			if (((tmp - last_ipd) >> 12) < MAX_IPD_TIME)
				ipd_count++;
			else
				ipd_count = 1;
			last_ipd = tmp;
			if (ipd_count == MAX_IPD_COUNT)
				s390_handle_damage();
			spin_unlock(&ipd_lock);
		} else {
			/* Processing damage -> stopping machine */
			s390_handle_damage();
		}
	}
	if (s390_validate_registers(mci, user_mode(regs))) {
		/*
		 * Couldn't restore all register contents for the
		 * user space process -> mark task for termination.
		 */
		mcck->kill_task = 1;
		mcck->mcck_code = mci.val;
		mcck_pending = 1;
	}

	/*
	 * Backup the machine check's info if it happens when the guest
	 * is running.
	 */
	if (test_cpu_flag(CIF_MCCK_GUEST))
		s390_backup_mcck_info(regs);

	if (mci.cd) {
		/* Timing facility damage */
		s390_handle_damage();
	}
	if (mci.ed && mci.ec) {
		/* External damage */
		if (S390_lowcore.external_damage_code & (1U << ED_STP_SYNC))
			mcck->stp_queue |= stp_sync_check();
		if (S390_lowcore.external_damage_code & (1U << ED_STP_ISLAND))
			mcck->stp_queue |= stp_island_check();
		mcck_pending = 1;
	}

	if (mci.cp) {
		/* Channel report word pending */
		mcck->channel_report = 1;
		mcck_pending = 1;
	}
	if (mci.w) {
		/* Warning pending */
		mcck->warning = 1;
		mcck_pending = 1;
	}

	/*
	 * If there are only Channel Report Pending and External Damage
	 * machine checks, they will not be reinjected into the guest
	 * because they refer to host conditions only.
	 */
	mcck_dam_code = (mci.val & MCIC_SUBCLASS_MASK);
	if (test_cpu_flag(CIF_MCCK_GUEST) &&
	(mcck_dam_code & MCCK_CODE_NO_GUEST) != mcck_dam_code) {
		/* Set exit reason code for host's later handling */
		*((long *)(regs->gprs[15] + __SF_SIE_REASON)) = -EINTR;
	}
	clear_cpu_flag(CIF_MCCK_GUEST);

	if (user_mode(regs) && mcck_pending) {
		nmi_exit();
		return 1;
	}

	if (mcck_pending)
		schedule_mcck_handler();

	nmi_exit();
	return 0;
}
NOKPROBE_SYMBOL(s390_do_machine_check);

static int __init machine_check_init(void)
{
	ctl_set_bit(14, 25);	/* enable external damage MCH */
	ctl_set_bit(14, 27);	/* enable system recovery MCH */
	ctl_set_bit(14, 24);	/* enable warning MCH */
	return 0;
}
early_initcall(machine_check_init);
