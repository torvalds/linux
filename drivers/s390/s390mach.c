/*
 *  drivers/s390/s390mach.c
 *   S/390 machine check handler
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ingo Adlung (adlung@de.ibm.com)
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <linux/time.h>
#include <linux/kthread.h>

#include <asm/lowcore.h>

#include "s390mach.h"

static struct semaphore m_sem;

extern int css_process_crw(int, int);
extern int chsc_process_crw(void);
extern int chp_process_crw(int, int);
extern void css_reiterate_subchannels(void);

extern struct workqueue_struct *slow_path_wq;
extern struct work_struct slow_path_work;

static NORET_TYPE void
s390_handle_damage(char *msg)
{
#ifdef CONFIG_SMP
	smp_send_stop();
#endif
	disabled_wait((unsigned long) __builtin_return_address(0));
	for(;;);
}

/*
 * Retrieve CRWs and call function to handle event.
 *
 * Note : we currently process CRWs for io and chsc subchannels only
 */
static int
s390_collect_crw_info(void *param)
{
	struct crw crw[2];
	int ccode, ret, slow;
	struct semaphore *sem;
	unsigned int chain;

	sem = (struct semaphore *)param;
repeat:
	down_interruptible(sem);
	slow = 0;
	chain = 0;
	while (1) {
		if (unlikely(chain > 1)) {
			struct crw tmp_crw;

			printk(KERN_WARNING"%s: Code does not support more "
			       "than two chained crws; please report to "
			       "linux390@de.ibm.com!\n", __FUNCTION__);
			ccode = stcrw(&tmp_crw);
			printk(KERN_WARNING"%s: crw reports slct=%d, oflw=%d, "
			       "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
			       __FUNCTION__, tmp_crw.slct, tmp_crw.oflw,
			       tmp_crw.chn, tmp_crw.rsc, tmp_crw.anc,
			       tmp_crw.erc, tmp_crw.rsid);
			printk(KERN_WARNING"%s: This was crw number %x in the "
			       "chain\n", __FUNCTION__, chain);
			if (ccode != 0)
				break;
			chain = tmp_crw.chn ? chain + 1 : 0;
			continue;
		}
		ccode = stcrw(&crw[chain]);
		if (ccode != 0)
			break;
		printk(KERN_DEBUG "crw_info : CRW reports slct=%d, oflw=%d, "
		       "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
		       crw[chain].slct, crw[chain].oflw, crw[chain].chn,
		       crw[chain].rsc, crw[chain].anc, crw[chain].erc,
		       crw[chain].rsid);
		/* Check for overflows. */
		if (crw[chain].oflw) {
			pr_debug("%s: crw overflow detected!\n", __FUNCTION__);
			css_reiterate_subchannels();
			chain = 0;
			slow = 1;
			continue;
		}
		switch (crw[chain].rsc) {
		case CRW_RSC_SCH:
			if (crw[0].chn && !chain)
				break;
			pr_debug("source is subchannel %04X\n", crw[0].rsid);
			ret = css_process_crw (crw[0].rsid,
					       chain ? crw[1].rsid : 0);
			if (ret == -EAGAIN)
				slow = 1;
			break;
		case CRW_RSC_MONITOR:
			pr_debug("source is monitoring facility\n");
			break;
		case CRW_RSC_CPATH:
			pr_debug("source is channel path %02X\n", crw[0].rsid);
			/*
			 * Check for solicited machine checks. These are
			 * created by reset channel path and need not be
			 * reported to the common I/O layer.
			 */
			if (crw[chain].slct) {
				pr_debug("solicited machine check for "
					 "channel path %02X\n", crw[0].rsid);
				break;
			}
			switch (crw[0].erc) {
			case CRW_ERC_IPARM: /* Path has come. */
				ret = chp_process_crw(crw[0].rsid, 1);
				break;
			case CRW_ERC_PERRI: /* Path has gone. */
			case CRW_ERC_PERRN:
				ret = chp_process_crw(crw[0].rsid, 0);
				break;
			default:
				pr_debug("Don't know how to handle erc=%x\n",
					 crw[0].erc);
				ret = 0;
			}
			if (ret == -EAGAIN)
				slow = 1;
			break;
		case CRW_RSC_CONFIG:
			pr_debug("source is configuration-alert facility\n");
			break;
		case CRW_RSC_CSS:
			pr_debug("source is channel subsystem\n");
			ret = chsc_process_crw();
			if (ret == -EAGAIN)
				slow = 1;
			break;
		default:
			pr_debug("unknown source\n");
			break;
		}
		/* chain is always 0 or 1 here. */
		chain = crw[chain].chn ? chain + 1 : 0;
	}
	if (slow)
		queue_work(slow_path_wq, &slow_path_work);
	goto repeat;
	return 0;
}

struct mcck_struct {
	int kill_task;
	int channel_report;
	int warning;
	unsigned long long mcck_code;
};

static DEFINE_PER_CPU(struct mcck_struct, cpu_mcck);

/*
 * Main machine check handler function. Will be called with interrupts enabled
 * or disabled and machine checks enabled or disabled.
 */
void
s390_handle_mcck(void)
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
	mcck = __get_cpu_var(cpu_mcck);
	memset(&__get_cpu_var(cpu_mcck), 0, sizeof(struct mcck_struct));
	clear_thread_flag(TIF_MCCK_PENDING);
	local_mcck_enable();
	local_irq_restore(flags);

	if (mcck.channel_report)
		up(&m_sem);

#ifdef CONFIG_MACHCHK_WARNING
/*
 * The warning may remain for a prolonged period on the bare iron.
 * (actually till the machine is powered off, or until the problem is gone)
 * So we just stop listening for the WARNING MCH and prevent continuously
 * being interrupted.  One caveat is however, that we must do this per
 * processor and cannot use the smp version of ctl_clear_bit().
 * On VM we only get one interrupt per virtally presented machinecheck.
 * Though one suffices, we may get one interrupt per (virtual) processor.
 */
	if (mcck.warning) {	/* WARNING pending ? */
		static int mchchk_wng_posted = 0;
		/*
		 * Use single machine clear, as we cannot handle smp right now
		 */
		__ctl_clear_bit(14, 24);	/* Disable WARNING MCH */
		if (xchg(&mchchk_wng_posted, 1) == 0)
			kill_cad_pid(SIGPWR, 1);
	}
#endif

	if (mcck.kill_task) {
		local_irq_enable();
		printk(KERN_EMERG "mcck: Terminating task because of machine "
		       "malfunction (code 0x%016llx).\n", mcck.mcck_code);
		printk(KERN_EMERG "mcck: task: %s, pid: %d.\n",
		       current->comm, current->pid);
		do_exit(SIGSEGV);
	}
}

/*
 * returns 0 if all registers could be validated
 * returns 1 otherwise
 */
static int
s390_revalidate_registers(struct mci *mci)
{
	int kill_task;
	u64 tmpclock;
	u64 zero;
	void *fpt_save_area, *fpt_creg_save_area;

	kill_task = 0;
	zero = 0;
	/* General purpose registers */
	if (!mci->gr)
		/*
		 * General purpose registers couldn't be restored and have
		 * unknown contents. Process needs to be terminated.
		 */
		kill_task = 1;

	/* Revalidate floating point registers */
	if (!mci->fp)
		/*
		 * Floating point registers can't be restored and
		 * therefore the process needs to be terminated.
		 */
		kill_task = 1;

#ifndef CONFIG_64BIT
	asm volatile(
		"	ld	0,0(%0)\n"
		"	ld	2,8(%0)\n"
		"	ld	4,16(%0)\n"
		"	ld	6,24(%0)"
		: : "a" (&S390_lowcore.floating_pt_save_area));
#endif

	if (MACHINE_HAS_IEEE) {
#ifdef CONFIG_64BIT
		fpt_save_area = &S390_lowcore.floating_pt_save_area;
		fpt_creg_save_area = &S390_lowcore.fpt_creg_save_area;
#else
		fpt_save_area = (void *) S390_lowcore.extended_save_area_addr;
		fpt_creg_save_area = fpt_save_area+128;
#endif
		/* Floating point control register */
		if (!mci->fc) {
			/*
			 * Floating point control register can't be restored.
			 * Task will be terminated.
			 */
			asm volatile("lfpc 0(%0)" : : "a" (&zero), "m" (zero));
			kill_task = 1;

		} else
			asm volatile("lfpc 0(%0)" : : "a" (fpt_creg_save_area));

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
			: : "a" (fpt_save_area));
	}

	/* Revalidate access registers */
	asm volatile(
		"	lam	0,15,0(%0)"
		: : "a" (&S390_lowcore.access_regs_save_area));
	if (!mci->ar)
		/*
		 * Access registers have unknown contents.
		 * Terminating task.
		 */
		kill_task = 1;

	/* Revalidate control registers */
	if (!mci->cr)
		/*
		 * Control registers have unknown contents.
		 * Can't recover and therefore stopping machine.
		 */
		s390_handle_damage("invalid control registers.");
	else
#ifdef CONFIG_64BIT
		asm volatile(
			"	lctlg	0,15,0(%0)"
			: : "a" (&S390_lowcore.cregs_save_area));
#else
		asm volatile(
			"	lctl	0,15,0(%0)"
			: : "a" (&S390_lowcore.cregs_save_area));
#endif

	/*
	 * We don't even try to revalidate the TOD register, since we simply
	 * can't write something sensible into that register.
	 */

#ifdef CONFIG_64BIT
	/*
	 * See if we can revalidate the TOD programmable register with its
	 * old contents (should be zero) otherwise set it to zero.
	 */
	if (!mci->pr)
		asm volatile(
			"	sr	0,0\n"
			"	sckpf"
			: : : "0", "cc");
	else
		asm volatile(
			"	l	0,0(%0)\n"
			"	sckpf"
			: : "a" (&S390_lowcore.tod_progreg_save_area)
			: "0", "cc");
#endif

	/* Revalidate clock comparator register */
	asm volatile(
		"	stck	0(%1)\n"
		"	sckc	0(%1)"
		: "=m" (tmpclock) : "a" (&(tmpclock)) : "cc", "memory");

	/* Check if old PSW is valid */
	if (!mci->wp)
		/*
		 * Can't tell if we come from user or kernel mode
		 * -> stopping machine.
		 */
		s390_handle_damage("old psw invalid.");

	if (!mci->ms || !mci->pm || !mci->ia)
		kill_task = 1;

	return kill_task;
}

#define MAX_IPD_COUNT	29
#define MAX_IPD_TIME	(5 * 60 * USEC_PER_SEC) /* 5 minutes */

/*
 * machine check handler.
 */
void
s390_do_machine_check(struct pt_regs *regs)
{
	static DEFINE_SPINLOCK(ipd_lock);
	static unsigned long long last_ipd;
	static int ipd_count;
	unsigned long long tmp;
	struct mci *mci;
	struct mcck_struct *mcck;
	int umode;

	lockdep_off();

	mci = (struct mci *) &S390_lowcore.mcck_interruption_code;
	mcck = &__get_cpu_var(cpu_mcck);
	umode = user_mode(regs);

	if (mci->sd)
		/* System damage -> stopping machine */
		s390_handle_damage("received system damage machine check.");

	if (mci->pd) {
		if (mci->b) {
			/* Processing backup -> verify if we can survive this */
			u64 z_mcic, o_mcic, t_mcic;
#ifdef CONFIG_64BIT
			z_mcic = (1ULL<<63 | 1ULL<<59 | 1ULL<<29);
			o_mcic = (1ULL<<43 | 1ULL<<42 | 1ULL<<41 | 1ULL<<40 |
				  1ULL<<36 | 1ULL<<35 | 1ULL<<34 | 1ULL<<32 |
				  1ULL<<30 | 1ULL<<21 | 1ULL<<20 | 1ULL<<17 |
				  1ULL<<16);
#else
			z_mcic = (1ULL<<63 | 1ULL<<59 | 1ULL<<57 | 1ULL<<50 |
				  1ULL<<29);
			o_mcic = (1ULL<<43 | 1ULL<<42 | 1ULL<<41 | 1ULL<<40 |
				  1ULL<<36 | 1ULL<<35 | 1ULL<<34 | 1ULL<<32 |
				  1ULL<<30 | 1ULL<<20 | 1ULL<<17 | 1ULL<<16);
#endif
			t_mcic = *(u64 *)mci;

			if (((t_mcic & z_mcic) != 0) ||
			    ((t_mcic & o_mcic) != o_mcic)) {
				s390_handle_damage("processing backup machine "
						   "check with damage.");
			}

			/*
			 * Nullifying exigent condition, therefore we might
			 * retry this instruction.
			 */

			spin_lock(&ipd_lock);

			tmp = get_clock();

			if (((tmp - last_ipd) >> 12) < MAX_IPD_TIME)
				ipd_count++;
			else
				ipd_count = 1;

			last_ipd = tmp;

			if (ipd_count == MAX_IPD_COUNT)
				s390_handle_damage("too many ipd retries.");

			spin_unlock(&ipd_lock);
		}
		else {
			/* Processing damage -> stopping machine */
			s390_handle_damage("received instruction processing "
					   "damage machine check.");
		}
	}
	if (s390_revalidate_registers(mci)) {
		if (umode) {
			/*
			 * Couldn't restore all register contents while in
			 * user mode -> mark task for termination.
			 */
			mcck->kill_task = 1;
			mcck->mcck_code = *(unsigned long long *) mci;
			set_thread_flag(TIF_MCCK_PENDING);
		}
		else
			/*
			 * Couldn't restore all register contents while in
			 * kernel mode -> stopping machine.
			 */
			s390_handle_damage("unable to revalidate registers.");
	}

	if (mci->se)
		/* Storage error uncorrected */
		s390_handle_damage("received storage error uncorrected "
				   "machine check.");

	if (mci->ke)
		/* Storage key-error uncorrected */
		s390_handle_damage("received storage key-error uncorrected "
				   "machine check.");

	if (mci->ds && mci->fa)
		/* Storage degradation */
		s390_handle_damage("received storage degradation machine "
				   "check.");

	if (mci->cp) {
		/* Channel report word pending */
		mcck->channel_report = 1;
		set_thread_flag(TIF_MCCK_PENDING);
	}

	if (mci->w) {
		/* Warning pending */
		mcck->warning = 1;
		set_thread_flag(TIF_MCCK_PENDING);
	}
	lockdep_on();
}

/*
 * s390_init_machine_check
 *
 * initialize machine check handling
 */
static int
machine_check_init(void)
{
	init_MUTEX_LOCKED(&m_sem);
	ctl_clear_bit(14, 25);	/* disable external damage MCH */
	ctl_set_bit(14, 27);    /* enable system recovery MCH */
#ifdef CONFIG_MACHCHK_WARNING
	ctl_set_bit(14, 24);	/* enable warning MCH */
#endif
	return 0;
}

/*
 * Initialize the machine check handler really early to be able to
 * catch all machine checks that happen during boot
 */
arch_initcall(machine_check_init);

/*
 * Machine checks for the channel subsystem must be enabled
 * after the channel subsystem is initialized
 */
static int __init
machine_check_crw_init (void)
{
	kthread_run(s390_collect_crw_info, &m_sem, "kmcheck");
	ctl_set_bit(14, 28);	/* enable channel report MCH */
	return 0;
}

device_initcall (machine_check_crw_init);
