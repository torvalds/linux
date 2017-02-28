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
#include <linux/time.h>
#include <linux/export.h>
#include <asm/lowcore.h>
#include <asm/smp.h>
#include <asm/stp.h>
#include <asm/cputime.h>
#include <asm/nmi.h>
#include <asm/crw.h>
#include <asm/switch_to.h>
#include <asm/ctl_reg.h>

struct mcck_struct {
	unsigned int kill_task : 1;
	unsigned int channel_report : 1;
	unsigned int warning : 1;
	unsigned int stp_queue : 1;
	unsigned long mcck_code;
};

static DEFINE_PER_CPU(struct mcck_struct, cpu_mcck);

static void s390_handle_damage(void)
{
	smp_send_stop();
	disabled_wait((unsigned long) __builtin_return_address(0));
	while (1);
}

/*
 * Main machine check handler function. Will be called with interrupts enabled
 * or disabled and machine checks enabled or disabled.
 */
void s390_handle_mcck(void)
{
	unsigned long flags;
	struct mcck_struct mcck;

	/*
	 * Disable machine checks and get the current state of accumulated
	 * machine checks. Afterwards delete the old state and enable machine
	 * checks again.
	 */
	local_irq_save(flags);
	local_mcck_disable();
	mcck = *this_cpu_ptr(&cpu_mcck);
	memset(this_cpu_ptr(&cpu_mcck), 0, sizeof(mcck));
	clear_cpu_flag(CIF_MCCK_PENDING);
	local_mcck_enable();
	local_irq_restore(flags);

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
		do_exit(SIGSEGV);
	}
}
EXPORT_SYMBOL_GPL(s390_handle_mcck);

/*
 * returns 0 if all registers could be validated
 * returns 1 otherwise
 */
static int notrace s390_validate_registers(union mci mci, int umode)
{
	int kill_task;
	u64 zero;
	void *fpt_save_area;

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
	/* Validate control registers */
	if (!mci.cr) {
		/*
		 * Control registers have unknown contents.
		 * Can't recover and therefore stopping machine.
		 */
		s390_handle_damage();
	} else {
		asm volatile(
			"	lctlg	0,15,0(%0)\n"
			"	ptlb\n"
			: : "a" (&S390_lowcore.cregs_save_area) : "memory");
	}
	if (!mci.fp) {
		/*
		 * Floating point registers can't be restored. If the
		 * kernel currently uses floating point registers the
		 * system is stopped. If the process has its floating
		 * pointer registers loaded it is terminated.
		 * Otherwise just revalidate the registers.
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
		 * registers loaded it is terminated. Otherwiese the
		 * FPC is just revalidated.
		 */
		if (S390_lowcore.fpu_flags & KERNEL_FPC)
			s390_handle_damage();
		asm volatile("lfpc %0" : : "Q" (zero));
		if (!test_cpu_flag(CIF_FPU))
			kill_task = 1;
	} else {
		asm volatile("lfpc %0"
			     : : "Q" (S390_lowcore.fpt_creg_save_area));
	}

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
			: : "a" (fpt_save_area) : "memory");
	} else {
		/* Validate vector registers */
		union ctlreg0 cr0;

		if (!mci.vr) {
			/*
			 * Vector registers can't be restored. If the kernel
			 * currently uses vector registers the system is
			 * stopped. If the process has its vector registers
			 * loaded it is terminated. Otherwise just revalidate
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
			"	.word	0xe70f,0x1000,0x0036\n"	/* vlm 0,15,0(1) */
			"	.word	0xe70f,0x1100,0x0c36\n"	/* vlm 16,31,256(1) */
			: : "Q" (*(struct vx_array *)
				 &S390_lowcore.vector_save_area) : "1");
		__ctl_load(S390_lowcore.cregs_save_area[0], 0, 0);
	}
	/* Validate access registers */
	asm volatile(
		"	lam	0,15,0(%0)"
		: : "a" (&S390_lowcore.access_regs_save_area));
	if (!mci.ar) {
		/*
		 * Access registers have unknown contents.
		 * Terminating task.
		 */
		kill_task = 1;
	}
	/*
	 * We don't even try to validate the TOD register, since we simply
	 * can't write something sensible into that register.
	 */
	/*
	 * See if we can validate the TOD programmable register with its
	 * old contents (should be zero) otherwise set it to zero.
	 */
	if (!mci.pr)
		asm volatile(
			"	sr	0,0\n"
			"	sckpf"
			: : : "0", "cc");
	else
		asm volatile(
			"	l	0,%0\n"
			"	sckpf"
			: : "Q" (S390_lowcore.tod_progreg_save_area)
			: "0", "cc");
	/* Validate clock comparator register */
	set_clock_comparator(S390_lowcore.clock_comparator);
	/* Check if old PSW is valid */
	if (!mci.wp)
		/*
		 * Can't tell if we come from user or kernel mode
		 * -> stopping machine.
		 */
		s390_handle_damage();

	if (!mci.ms || !mci.pm || !mci.ia)
		kill_task = 1;

	return kill_task;
}

#define MAX_IPD_COUNT	29
#define MAX_IPD_TIME	(5 * 60 * USEC_PER_SEC) /* 5 minutes */

#define ED_STP_ISLAND	6	/* External damage STP island check */
#define ED_STP_SYNC	7	/* External damage STP sync check */

/*
 * machine check handler.
 */
void notrace s390_do_machine_check(struct pt_regs *regs)
{
	static int ipd_count;
	static DEFINE_SPINLOCK(ipd_lock);
	static unsigned long long last_ipd;
	struct mcck_struct *mcck;
	unsigned long long tmp;
	union mci mci;

	nmi_enter();
	inc_irq_stat(NMI_NMI);
	mci.val = S390_lowcore.mcck_interruption_code;
	mcck = this_cpu_ptr(&cpu_mcck);

	if (mci.sd) {
		/* System damage -> stopping machine */
		s390_handle_damage();
	}
	if (mci.pd) {
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
		set_cpu_flag(CIF_MCCK_PENDING);
	}
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
		if (mcck->stp_queue)
			set_cpu_flag(CIF_MCCK_PENDING);
	}
	if (mci.se)
		/* Storage error uncorrected */
		s390_handle_damage();
	if (mci.ke)
		/* Storage key-error uncorrected */
		s390_handle_damage();
	if (mci.ds && mci.fa)
		/* Storage degradation */
		s390_handle_damage();
	if (mci.cp) {
		/* Channel report word pending */
		mcck->channel_report = 1;
		set_cpu_flag(CIF_MCCK_PENDING);
	}
	if (mci.w) {
		/* Warning pending */
		mcck->warning = 1;
		set_cpu_flag(CIF_MCCK_PENDING);
	}
	nmi_exit();
}

static int __init machine_check_init(void)
{
	ctl_set_bit(14, 25);	/* enable external damage MCH */
	ctl_set_bit(14, 27);	/* enable system recovery MCH */
	ctl_set_bit(14, 24);	/* enable warning MCH */
	return 0;
}
early_initcall(machine_check_init);
