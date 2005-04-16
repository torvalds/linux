/*
 * File:	mca.c
 * Purpose:	Generic MCA handling layer
 *
 * Updated for latest kernel
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Copyright (C) 2002 Dell Inc.
 * Copyright (C) Matt Domsch (Matt_Domsch@dell.com)
 *
 * Copyright (C) 2002 Intel
 * Copyright (C) Jenna Hall (jenna.s.hall@intel.com)
 *
 * Copyright (C) 2001 Intel
 * Copyright (C) Fred Lewis (frederick.v.lewis@intel.com)
 *
 * Copyright (C) 2000 Intel
 * Copyright (C) Chuck Fleckenstein (cfleck@co.intel.com)
 *
 * Copyright (C) 1999, 2004 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander(vijay@engr.sgi.com)
 *
 * 03/04/15 D. Mosberger Added INIT backtrace support.
 * 02/03/25 M. Domsch	GUID cleanups
 *
 * 02/01/04 J. Hall	Aligned MCA stack to 16 bytes, added platform vs. CPU
 *			error flag, set SAL default return values, changed
 *			error record structure to linked list, added init call
 *			to sal_get_state_info_size().
 *
 * 01/01/03 F. Lewis    Added setup of CMCI and CPEI IRQs, logging of corrected
 *                      platform errors, completed code for logging of
 *                      corrected & uncorrected machine check errors, and
 *                      updated for conformance with Nov. 2000 revision of the
 *                      SAL 3.0 spec.
 * 00/03/29 C. Fleckenstein  Fixed PAL/SAL update issues, began MCA bug fixes, logging issues,
 *                           added min save state dump, added INIT handler.
 *
 * 2003-12-08 Keith Owens <kaos@sgi.com>
 *            smp_call_function() must not be called from interrupt context (can
 *            deadlock on tasklist_lock).  Use keventd to call smp_call_function().
 *
 * 2004-02-01 Keith Owens <kaos@sgi.com>
 *            Avoid deadlock when using printk() for MCA and INIT records.
 *            Delete all record printing code, moved to salinfo_decode in user space.
 *            Mark variables and functions static where possible.
 *            Delete dead variables and functions.
 *            Reorder to remove the need for forward declarations and to consolidate
 *            related code.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kallsyms.h>
#include <linux/smp_lock.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/workqueue.h>

#include <asm/delay.h>
#include <asm/machvec.h>
#include <asm/meminit.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/sal.h>
#include <asm/mca.h>

#include <asm/irq.h>
#include <asm/hw_irq.h>

#if defined(IA64_MCA_DEBUG_INFO)
# define IA64_MCA_DEBUG(fmt...)	printk(fmt)
#else
# define IA64_MCA_DEBUG(fmt...)
#endif

/* Used by mca_asm.S */
ia64_mca_sal_to_os_state_t	ia64_sal_to_os_handoff_state;
ia64_mca_os_to_sal_state_t	ia64_os_to_sal_handoff_state;
u64				ia64_mca_serialize;
DEFINE_PER_CPU(u64, ia64_mca_data); /* == __per_cpu_mca[smp_processor_id()] */
DEFINE_PER_CPU(u64, ia64_mca_per_cpu_pte); /* PTE to map per-CPU area */
DEFINE_PER_CPU(u64, ia64_mca_pal_pte);	    /* PTE to map PAL code */
DEFINE_PER_CPU(u64, ia64_mca_pal_base);    /* vaddr PAL code granule */

unsigned long __per_cpu_mca[NR_CPUS];

/* In mca_asm.S */
extern void			ia64_monarch_init_handler (void);
extern void			ia64_slave_init_handler (void);

static ia64_mc_info_t		ia64_mc_info;

#define MAX_CPE_POLL_INTERVAL (15*60*HZ) /* 15 minutes */
#define MIN_CPE_POLL_INTERVAL (2*60*HZ)  /* 2 minutes */
#define CMC_POLL_INTERVAL     (1*60*HZ)  /* 1 minute */
#define CPE_HISTORY_LENGTH    5
#define CMC_HISTORY_LENGTH    5

static struct timer_list cpe_poll_timer;
static struct timer_list cmc_poll_timer;
/*
 * This variable tells whether we are currently in polling mode.
 * Start with this in the wrong state so we won't play w/ timers
 * before the system is ready.
 */
static int cmc_polling_enabled = 1;

/*
 * Clearing this variable prevents CPE polling from getting activated
 * in mca_late_init.  Use it if your system doesn't provide a CPEI,
 * but encounters problems retrieving CPE logs.  This should only be
 * necessary for debugging.
 */
static int cpe_poll_enabled = 1;

extern void salinfo_log_wakeup(int type, u8 *buffer, u64 size, int irqsafe);

static int mca_init;

/*
 * IA64_MCA log support
 */
#define IA64_MAX_LOGS		2	/* Double-buffering for nested MCAs */
#define IA64_MAX_LOG_TYPES      4   /* MCA, INIT, CMC, CPE */

typedef struct ia64_state_log_s
{
	spinlock_t	isl_lock;
	int		isl_index;
	unsigned long	isl_count;
	ia64_err_rec_t  *isl_log[IA64_MAX_LOGS]; /* need space to store header + error log */
} ia64_state_log_t;

static ia64_state_log_t ia64_state_log[IA64_MAX_LOG_TYPES];

#define IA64_LOG_ALLOCATE(it, size) \
	{ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)] = \
		(ia64_err_rec_t *)alloc_bootmem(size); \
	ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)] = \
		(ia64_err_rec_t *)alloc_bootmem(size);}
#define IA64_LOG_LOCK_INIT(it) spin_lock_init(&ia64_state_log[it].isl_lock)
#define IA64_LOG_LOCK(it)      spin_lock_irqsave(&ia64_state_log[it].isl_lock, s)
#define IA64_LOG_UNLOCK(it)    spin_unlock_irqrestore(&ia64_state_log[it].isl_lock,s)
#define IA64_LOG_NEXT_INDEX(it)    ia64_state_log[it].isl_index
#define IA64_LOG_CURR_INDEX(it)    1 - ia64_state_log[it].isl_index
#define IA64_LOG_INDEX_INC(it) \
    {ia64_state_log[it].isl_index = 1 - ia64_state_log[it].isl_index; \
    ia64_state_log[it].isl_count++;}
#define IA64_LOG_INDEX_DEC(it) \
    ia64_state_log[it].isl_index = 1 - ia64_state_log[it].isl_index
#define IA64_LOG_NEXT_BUFFER(it)   (void *)((ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)]))
#define IA64_LOG_CURR_BUFFER(it)   (void *)((ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)]))
#define IA64_LOG_COUNT(it)         ia64_state_log[it].isl_count

/*
 * ia64_log_init
 *	Reset the OS ia64 log buffer
 * Inputs   :   info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 * Outputs	:	None
 */
static void
ia64_log_init(int sal_info_type)
{
	u64	max_size = 0;

	IA64_LOG_NEXT_INDEX(sal_info_type) = 0;
	IA64_LOG_LOCK_INIT(sal_info_type);

	// SAL will tell us the maximum size of any error record of this type
	max_size = ia64_sal_get_state_info_size(sal_info_type);
	if (!max_size)
		/* alloc_bootmem() doesn't like zero-sized allocations! */
		return;

	// set up OS data structures to hold error info
	IA64_LOG_ALLOCATE(sal_info_type, max_size);
	memset(IA64_LOG_CURR_BUFFER(sal_info_type), 0, max_size);
	memset(IA64_LOG_NEXT_BUFFER(sal_info_type), 0, max_size);
}

/*
 * ia64_log_get
 *
 *	Get the current MCA log from SAL and copy it into the OS log buffer.
 *
 *  Inputs  :   info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 *              irq_safe    whether you can use printk at this point
 *  Outputs :   size        (total record length)
 *              *buffer     (ptr to error record)
 *
 */
static u64
ia64_log_get(int sal_info_type, u8 **buffer, int irq_safe)
{
	sal_log_record_header_t     *log_buffer;
	u64                         total_len = 0;
	int                         s;

	IA64_LOG_LOCK(sal_info_type);

	/* Get the process state information */
	log_buffer = IA64_LOG_NEXT_BUFFER(sal_info_type);

	total_len = ia64_sal_get_state_info(sal_info_type, (u64 *)log_buffer);

	if (total_len) {
		IA64_LOG_INDEX_INC(sal_info_type);
		IA64_LOG_UNLOCK(sal_info_type);
		if (irq_safe) {
			IA64_MCA_DEBUG("%s: SAL error record type %d retrieved. "
				       "Record length = %ld\n", __FUNCTION__, sal_info_type, total_len);
		}
		*buffer = (u8 *) log_buffer;
		return total_len;
	} else {
		IA64_LOG_UNLOCK(sal_info_type);
		return 0;
	}
}

/*
 *  ia64_mca_log_sal_error_record
 *
 *  This function retrieves a specified error record type from SAL
 *  and wakes up any processes waiting for error records.
 *
 *  Inputs  :   sal_info_type   (Type of error record MCA/CMC/CPE/INIT)
 */
static void
ia64_mca_log_sal_error_record(int sal_info_type)
{
	u8 *buffer;
	sal_log_record_header_t *rh;
	u64 size;
	int irq_safe = sal_info_type != SAL_INFO_TYPE_MCA && sal_info_type != SAL_INFO_TYPE_INIT;
#ifdef IA64_MCA_DEBUG_INFO
	static const char * const rec_name[] = { "MCA", "INIT", "CMC", "CPE" };
#endif

	size = ia64_log_get(sal_info_type, &buffer, irq_safe);
	if (!size)
		return;

	salinfo_log_wakeup(sal_info_type, buffer, size, irq_safe);

	if (irq_safe)
		IA64_MCA_DEBUG("CPU %d: SAL log contains %s error record\n",
			smp_processor_id(),
			sal_info_type < ARRAY_SIZE(rec_name) ? rec_name[sal_info_type] : "UNKNOWN");

	/* Clear logs from corrected errors in case there's no user-level logger */
	rh = (sal_log_record_header_t *)buffer;
	if (rh->severity == sal_log_severity_corrected)
		ia64_sal_clear_state_info(sal_info_type);
}

/*
 * platform dependent error handling
 */
#ifndef PLATFORM_MCA_HANDLERS

#ifdef CONFIG_ACPI

static int cpe_vector = -1;

static irqreturn_t
ia64_mca_cpe_int_handler (int cpe_irq, void *arg, struct pt_regs *ptregs)
{
	static unsigned long	cpe_history[CPE_HISTORY_LENGTH];
	static int		index;
	static DEFINE_SPINLOCK(cpe_history_lock);

	IA64_MCA_DEBUG("%s: received interrupt vector = %#x on CPU %d\n",
		       __FUNCTION__, cpe_irq, smp_processor_id());

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

	/* Get the CPE error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CPE);

	spin_lock(&cpe_history_lock);
	if (!cpe_poll_enabled && cpe_vector >= 0) {

		int i, count = 1; /* we know 1 happened now */
		unsigned long now = jiffies;

		for (i = 0; i < CPE_HISTORY_LENGTH; i++) {
			if (now - cpe_history[i] <= HZ)
				count++;
		}

		IA64_MCA_DEBUG(KERN_INFO "CPE threshold %d/%d\n", count, CPE_HISTORY_LENGTH);
		if (count >= CPE_HISTORY_LENGTH) {

			cpe_poll_enabled = 1;
			spin_unlock(&cpe_history_lock);
			disable_irq_nosync(local_vector_to_irq(IA64_CPE_VECTOR));

			/*
			 * Corrected errors will still be corrected, but
			 * make sure there's a log somewhere that indicates
			 * something is generating more than we can handle.
			 */
			printk(KERN_WARNING "WARNING: Switching to polling CPE handler; error records may be lost\n");

			mod_timer(&cpe_poll_timer, jiffies + MIN_CPE_POLL_INTERVAL);

			/* lock already released, get out now */
			return IRQ_HANDLED;
		} else {
			cpe_history[index++] = now;
			if (index == CPE_HISTORY_LENGTH)
				index = 0;
		}
	}
	spin_unlock(&cpe_history_lock);
	return IRQ_HANDLED;
}

#endif /* CONFIG_ACPI */

static void
show_min_state (pal_min_state_area_t *minstate)
{
	u64 iip = minstate->pmsa_iip + ((struct ia64_psr *)(&minstate->pmsa_ipsr))->ri;
	u64 xip = minstate->pmsa_xip + ((struct ia64_psr *)(&minstate->pmsa_xpsr))->ri;

	printk("NaT bits\t%016lx\n", minstate->pmsa_nat_bits);
	printk("pr\t\t%016lx\n", minstate->pmsa_pr);
	printk("b0\t\t%016lx ", minstate->pmsa_br0); print_symbol("%s\n", minstate->pmsa_br0);
	printk("ar.rsc\t\t%016lx\n", minstate->pmsa_rsc);
	printk("cr.iip\t\t%016lx ", iip); print_symbol("%s\n", iip);
	printk("cr.ipsr\t\t%016lx\n", minstate->pmsa_ipsr);
	printk("cr.ifs\t\t%016lx\n", minstate->pmsa_ifs);
	printk("xip\t\t%016lx ", xip); print_symbol("%s\n", xip);
	printk("xpsr\t\t%016lx\n", minstate->pmsa_xpsr);
	printk("xfs\t\t%016lx\n", minstate->pmsa_xfs);
	printk("b1\t\t%016lx ", minstate->pmsa_br1);
	print_symbol("%s\n", minstate->pmsa_br1);

	printk("\nstatic registers r0-r15:\n");
	printk(" r0- 3 %016lx %016lx %016lx %016lx\n",
	       0UL, minstate->pmsa_gr[0], minstate->pmsa_gr[1], minstate->pmsa_gr[2]);
	printk(" r4- 7 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[3], minstate->pmsa_gr[4],
	       minstate->pmsa_gr[5], minstate->pmsa_gr[6]);
	printk(" r8-11 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[7], minstate->pmsa_gr[8],
	       minstate->pmsa_gr[9], minstate->pmsa_gr[10]);
	printk("r12-15 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_gr[11], minstate->pmsa_gr[12],
	       minstate->pmsa_gr[13], minstate->pmsa_gr[14]);

	printk("\nbank 0:\n");
	printk("r16-19 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[0], minstate->pmsa_bank0_gr[1],
	       minstate->pmsa_bank0_gr[2], minstate->pmsa_bank0_gr[3]);
	printk("r20-23 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[4], minstate->pmsa_bank0_gr[5],
	       minstate->pmsa_bank0_gr[6], minstate->pmsa_bank0_gr[7]);
	printk("r24-27 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[8], minstate->pmsa_bank0_gr[9],
	       minstate->pmsa_bank0_gr[10], minstate->pmsa_bank0_gr[11]);
	printk("r28-31 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank0_gr[12], minstate->pmsa_bank0_gr[13],
	       minstate->pmsa_bank0_gr[14], minstate->pmsa_bank0_gr[15]);

	printk("\nbank 1:\n");
	printk("r16-19 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[0], minstate->pmsa_bank1_gr[1],
	       minstate->pmsa_bank1_gr[2], minstate->pmsa_bank1_gr[3]);
	printk("r20-23 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[4], minstate->pmsa_bank1_gr[5],
	       minstate->pmsa_bank1_gr[6], minstate->pmsa_bank1_gr[7]);
	printk("r24-27 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[8], minstate->pmsa_bank1_gr[9],
	       minstate->pmsa_bank1_gr[10], minstate->pmsa_bank1_gr[11]);
	printk("r28-31 %016lx %016lx %016lx %016lx\n",
	       minstate->pmsa_bank1_gr[12], minstate->pmsa_bank1_gr[13],
	       minstate->pmsa_bank1_gr[14], minstate->pmsa_bank1_gr[15]);
}

static void
fetch_min_state (pal_min_state_area_t *ms, struct pt_regs *pt, struct switch_stack *sw)
{
	u64 *dst_banked, *src_banked, bit, shift, nat_bits;
	int i;

	/*
	 * First, update the pt-regs and switch-stack structures with the contents stored
	 * in the min-state area:
	 */
	if (((struct ia64_psr *) &ms->pmsa_ipsr)->ic == 0) {
		pt->cr_ipsr = ms->pmsa_xpsr;
		pt->cr_iip = ms->pmsa_xip;
		pt->cr_ifs = ms->pmsa_xfs;
	} else {
		pt->cr_ipsr = ms->pmsa_ipsr;
		pt->cr_iip = ms->pmsa_iip;
		pt->cr_ifs = ms->pmsa_ifs;
	}
	pt->ar_rsc = ms->pmsa_rsc;
	pt->pr = ms->pmsa_pr;
	pt->r1 = ms->pmsa_gr[0];
	pt->r2 = ms->pmsa_gr[1];
	pt->r3 = ms->pmsa_gr[2];
	sw->r4 = ms->pmsa_gr[3];
	sw->r5 = ms->pmsa_gr[4];
	sw->r6 = ms->pmsa_gr[5];
	sw->r7 = ms->pmsa_gr[6];
	pt->r8 = ms->pmsa_gr[7];
	pt->r9 = ms->pmsa_gr[8];
	pt->r10 = ms->pmsa_gr[9];
	pt->r11 = ms->pmsa_gr[10];
	pt->r12 = ms->pmsa_gr[11];
	pt->r13 = ms->pmsa_gr[12];
	pt->r14 = ms->pmsa_gr[13];
	pt->r15 = ms->pmsa_gr[14];
	dst_banked = &pt->r16;		/* r16-r31 are contiguous in struct pt_regs */
	src_banked = ms->pmsa_bank1_gr;
	for (i = 0; i < 16; ++i)
		dst_banked[i] = src_banked[i];
	pt->b0 = ms->pmsa_br0;
	sw->b1 = ms->pmsa_br1;

	/* construct the NaT bits for the pt-regs structure: */
#	define PUT_NAT_BIT(dst, addr)					\
	do {								\
		bit = nat_bits & 1; nat_bits >>= 1;			\
		shift = ((unsigned long) addr >> 3) & 0x3f;		\
		dst = ((dst) & ~(1UL << shift)) | (bit << shift);	\
	} while (0)

	/* Rotate the saved NaT bits such that bit 0 corresponds to pmsa_gr[0]: */
	shift = ((unsigned long) &ms->pmsa_gr[0] >> 3) & 0x3f;
	nat_bits = (ms->pmsa_nat_bits >> shift) | (ms->pmsa_nat_bits << (64 - shift));

	PUT_NAT_BIT(sw->caller_unat, &pt->r1);
	PUT_NAT_BIT(sw->caller_unat, &pt->r2);
	PUT_NAT_BIT(sw->caller_unat, &pt->r3);
	PUT_NAT_BIT(sw->ar_unat, &sw->r4);
	PUT_NAT_BIT(sw->ar_unat, &sw->r5);
	PUT_NAT_BIT(sw->ar_unat, &sw->r6);
	PUT_NAT_BIT(sw->ar_unat, &sw->r7);
	PUT_NAT_BIT(sw->caller_unat, &pt->r8);	PUT_NAT_BIT(sw->caller_unat, &pt->r9);
	PUT_NAT_BIT(sw->caller_unat, &pt->r10);	PUT_NAT_BIT(sw->caller_unat, &pt->r11);
	PUT_NAT_BIT(sw->caller_unat, &pt->r12);	PUT_NAT_BIT(sw->caller_unat, &pt->r13);
	PUT_NAT_BIT(sw->caller_unat, &pt->r14);	PUT_NAT_BIT(sw->caller_unat, &pt->r15);
	nat_bits >>= 16;	/* skip over bank0 NaT bits */
	PUT_NAT_BIT(sw->caller_unat, &pt->r16);	PUT_NAT_BIT(sw->caller_unat, &pt->r17);
	PUT_NAT_BIT(sw->caller_unat, &pt->r18);	PUT_NAT_BIT(sw->caller_unat, &pt->r19);
	PUT_NAT_BIT(sw->caller_unat, &pt->r20);	PUT_NAT_BIT(sw->caller_unat, &pt->r21);
	PUT_NAT_BIT(sw->caller_unat, &pt->r22);	PUT_NAT_BIT(sw->caller_unat, &pt->r23);
	PUT_NAT_BIT(sw->caller_unat, &pt->r24);	PUT_NAT_BIT(sw->caller_unat, &pt->r25);
	PUT_NAT_BIT(sw->caller_unat, &pt->r26);	PUT_NAT_BIT(sw->caller_unat, &pt->r27);
	PUT_NAT_BIT(sw->caller_unat, &pt->r28);	PUT_NAT_BIT(sw->caller_unat, &pt->r29);
	PUT_NAT_BIT(sw->caller_unat, &pt->r30);	PUT_NAT_BIT(sw->caller_unat, &pt->r31);
}

static void
init_handler_platform (pal_min_state_area_t *ms,
		       struct pt_regs *pt, struct switch_stack *sw)
{
	struct unw_frame_info info;

	/* if a kernel debugger is available call it here else just dump the registers */

	/*
	 * Wait for a bit.  On some machines (e.g., HP's zx2000 and zx6000, INIT can be
	 * generated via the BMC's command-line interface, but since the console is on the
	 * same serial line, the user will need some time to switch out of the BMC before
	 * the dump begins.
	 */
	printk("Delaying for 5 seconds...\n");
	udelay(5*1000000);
	show_min_state(ms);

	printk("Backtrace of current task (pid %d, %s)\n", current->pid, current->comm);
	fetch_min_state(ms, pt, sw);
	unw_init_from_interruption(&info, current, pt, sw);
	ia64_do_show_stack(&info, NULL);

#ifdef CONFIG_SMP
	/* read_trylock() would be handy... */
	if (!tasklist_lock.write_lock)
		read_lock(&tasklist_lock);
#endif
	{
		struct task_struct *g, *t;
		do_each_thread (g, t) {
			if (t == current)
				continue;

			printk("\nBacktrace of pid %d (%s)\n", t->pid, t->comm);
			show_stack(t, NULL);
		} while_each_thread (g, t);
	}
#ifdef CONFIG_SMP
	if (!tasklist_lock.write_lock)
		read_unlock(&tasklist_lock);
#endif

	printk("\nINIT dump complete.  Please reboot now.\n");
	while (1);			/* hang city if no debugger */
}

#ifdef CONFIG_ACPI
/*
 * ia64_mca_register_cpev
 *
 *  Register the corrected platform error vector with SAL.
 *
 *  Inputs
 *      cpev        Corrected Platform Error Vector number
 *
 *  Outputs
 *      None
 */
static void
ia64_mca_register_cpev (int cpev)
{
	/* Register the CPE interrupt vector with SAL */
	struct ia64_sal_retval isrv;

	isrv = ia64_sal_mc_set_params(SAL_MC_PARAM_CPE_INT, SAL_MC_PARAM_MECHANISM_INT, cpev, 0, 0);
	if (isrv.status) {
		printk(KERN_ERR "Failed to register Corrected Platform "
		       "Error interrupt vector with SAL (status %ld)\n", isrv.status);
		return;
	}

	IA64_MCA_DEBUG("%s: corrected platform error "
		       "vector %#x registered\n", __FUNCTION__, cpev);
}
#endif /* CONFIG_ACPI */

#endif /* PLATFORM_MCA_HANDLERS */

/*
 * ia64_mca_cmc_vector_setup
 *
 *  Setup the corrected machine check vector register in the processor.
 *  (The interrupt is masked on boot. ia64_mca_late_init unmask this.)
 *  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      None
 *
 * Outputs
 *	None
 */
void
ia64_mca_cmc_vector_setup (void)
{
	cmcv_reg_t	cmcv;

	cmcv.cmcv_regval	= 0;
	cmcv.cmcv_mask		= 1;        /* Mask/disable interrupt at first */
	cmcv.cmcv_vector	= IA64_CMC_VECTOR;
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval);

	IA64_MCA_DEBUG("%s: CPU %d corrected "
		       "machine check vector %#x registered.\n",
		       __FUNCTION__, smp_processor_id(), IA64_CMC_VECTOR);

	IA64_MCA_DEBUG("%s: CPU %d CMCV = %#016lx\n",
		       __FUNCTION__, smp_processor_id(), ia64_getreg(_IA64_REG_CR_CMCV));
}

/*
 * ia64_mca_cmc_vector_disable
 *
 *  Mask the corrected machine check vector register in the processor.
 *  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      dummy(unused)
 *
 * Outputs
 *	None
 */
static void
ia64_mca_cmc_vector_disable (void *dummy)
{
	cmcv_reg_t	cmcv;

	cmcv.cmcv_regval = ia64_getreg(_IA64_REG_CR_CMCV);

	cmcv.cmcv_mask = 1; /* Mask/disable interrupt */
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval);

	IA64_MCA_DEBUG("%s: CPU %d corrected "
		       "machine check vector %#x disabled.\n",
		       __FUNCTION__, smp_processor_id(), cmcv.cmcv_vector);
}

/*
 * ia64_mca_cmc_vector_enable
 *
 *  Unmask the corrected machine check vector register in the processor.
 *  This function is invoked on a per-processor basis.
 *
 * Inputs
 *      dummy(unused)
 *
 * Outputs
 *	None
 */
static void
ia64_mca_cmc_vector_enable (void *dummy)
{
	cmcv_reg_t	cmcv;

	cmcv.cmcv_regval = ia64_getreg(_IA64_REG_CR_CMCV);

	cmcv.cmcv_mask = 0; /* Unmask/enable interrupt */
	ia64_setreg(_IA64_REG_CR_CMCV, cmcv.cmcv_regval);

	IA64_MCA_DEBUG("%s: CPU %d corrected "
		       "machine check vector %#x enabled.\n",
		       __FUNCTION__, smp_processor_id(), cmcv.cmcv_vector);
}

/*
 * ia64_mca_cmc_vector_disable_keventd
 *
 * Called via keventd (smp_call_function() is not safe in interrupt context) to
 * disable the cmc interrupt vector.
 */
static void
ia64_mca_cmc_vector_disable_keventd(void *unused)
{
	on_each_cpu(ia64_mca_cmc_vector_disable, NULL, 1, 0);
}

/*
 * ia64_mca_cmc_vector_enable_keventd
 *
 * Called via keventd (smp_call_function() is not safe in interrupt context) to
 * enable the cmc interrupt vector.
 */
static void
ia64_mca_cmc_vector_enable_keventd(void *unused)
{
	on_each_cpu(ia64_mca_cmc_vector_enable, NULL, 1, 0);
}

/*
 * ia64_mca_wakeup_ipi_wait
 *
 *	Wait for the inter-cpu interrupt to be sent by the
 *	monarch processor once it is done with handling the
 *	MCA.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
static void
ia64_mca_wakeup_ipi_wait(void)
{
	int	irr_num = (IA64_MCA_WAKEUP_VECTOR >> 6);
	int	irr_bit = (IA64_MCA_WAKEUP_VECTOR & 0x3f);
	u64	irr = 0;

	do {
		switch(irr_num) {
		      case 0:
			irr = ia64_getreg(_IA64_REG_CR_IRR0);
			break;
		      case 1:
			irr = ia64_getreg(_IA64_REG_CR_IRR1);
			break;
		      case 2:
			irr = ia64_getreg(_IA64_REG_CR_IRR2);
			break;
		      case 3:
			irr = ia64_getreg(_IA64_REG_CR_IRR3);
			break;
		}
		cpu_relax();
	} while (!(irr & (1UL << irr_bit))) ;
}

/*
 * ia64_mca_wakeup
 *
 *	Send an inter-cpu interrupt to wake-up a particular cpu
 *	and mark that cpu to be out of rendez.
 *
 *  Inputs  :   cpuid
 *  Outputs :   None
 */
static void
ia64_mca_wakeup(int cpu)
{
	platform_send_ipi(cpu, IA64_MCA_WAKEUP_VECTOR, IA64_IPI_DM_INT, 0);
	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;

}

/*
 * ia64_mca_wakeup_all
 *
 *	Wakeup all the cpus which have rendez'ed previously.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
static void
ia64_mca_wakeup_all(void)
{
	int cpu;

	/* Clear the Rendez checkin flag for all cpus */
	for(cpu = 0; cpu < NR_CPUS; cpu++) {
		if (!cpu_online(cpu))
			continue;
		if (ia64_mc_info.imi_rendez_checkin[cpu] == IA64_MCA_RENDEZ_CHECKIN_DONE)
			ia64_mca_wakeup(cpu);
	}

}

/*
 * ia64_mca_rendez_interrupt_handler
 *
 *	This is handler used to put slave processors into spinloop
 *	while the monarch processor does the mca handling and later
 *	wake each slave up once the monarch is done.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
static irqreturn_t
ia64_mca_rendez_int_handler(int rendez_irq, void *arg, struct pt_regs *ptregs)
{
	unsigned long flags;
	int cpu = smp_processor_id();

	/* Mask all interrupts */
	local_irq_save(flags);

	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_DONE;
	/* Register with the SAL monarch that the slave has
	 * reached SAL
	 */
	ia64_sal_mc_rendez();

	/* Wait for the wakeup IPI from the monarch
	 * This waiting is done by polling on the wakeup-interrupt
	 * vector bit in the processor's IRRs
	 */
	ia64_mca_wakeup_ipi_wait();

	/* Enable all interrupts */
	local_irq_restore(flags);
	return IRQ_HANDLED;
}

/*
 * ia64_mca_wakeup_int_handler
 *
 *	The interrupt handler for processing the inter-cpu interrupt to the
 *	slave cpu which was spinning in the rendez loop.
 *	Since this spinning is done by turning off the interrupts and
 *	polling on the wakeup-interrupt bit in the IRR, there is
 *	nothing useful to be done in the handler.
 *
 *  Inputs  :   wakeup_irq  (Wakeup-interrupt bit)
 *	arg		(Interrupt handler specific argument)
 *	ptregs		(Exception frame at the time of the interrupt)
 *  Outputs :   None
 *
 */
static irqreturn_t
ia64_mca_wakeup_int_handler(int wakeup_irq, void *arg, struct pt_regs *ptregs)
{
	return IRQ_HANDLED;
}

/*
 * ia64_return_to_sal_check
 *
 *	This is function called before going back from the OS_MCA handler
 *	to the OS_MCA dispatch code which finally takes the control back
 *	to the SAL.
 *	The main purpose of this routine is to setup the OS_MCA to SAL
 *	return state which can be used by the OS_MCA dispatch code
 *	just before going back to SAL.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */

static void
ia64_return_to_sal_check(int recover)
{

	/* Copy over some relevant stuff from the sal_to_os_mca_handoff
	 * so that it can be used at the time of os_mca_to_sal_handoff
	 */
	ia64_os_to_sal_handoff_state.imots_sal_gp =
		ia64_sal_to_os_handoff_state.imsto_sal_gp;

	ia64_os_to_sal_handoff_state.imots_sal_check_ra =
		ia64_sal_to_os_handoff_state.imsto_sal_check_ra;

	if (recover)
		ia64_os_to_sal_handoff_state.imots_os_status = IA64_MCA_CORRECTED;
	else
		ia64_os_to_sal_handoff_state.imots_os_status = IA64_MCA_COLD_BOOT;

	/* Default = tell SAL to return to same context */
	ia64_os_to_sal_handoff_state.imots_context = IA64_MCA_SAME_CONTEXT;

	ia64_os_to_sal_handoff_state.imots_new_min_state =
		(u64 *)ia64_sal_to_os_handoff_state.pal_min_state;

}

/* Function pointer for extra MCA recovery */
int (*ia64_mca_ucmc_extension)
	(void*,ia64_mca_sal_to_os_state_t*,ia64_mca_os_to_sal_state_t*)
	= NULL;

int
ia64_reg_MCA_extension(void *fn)
{
	if (ia64_mca_ucmc_extension)
		return 1;

	ia64_mca_ucmc_extension = fn;
	return 0;
}

void
ia64_unreg_MCA_extension(void)
{
	if (ia64_mca_ucmc_extension)
		ia64_mca_ucmc_extension = NULL;
}

EXPORT_SYMBOL(ia64_reg_MCA_extension);
EXPORT_SYMBOL(ia64_unreg_MCA_extension);

/*
 * ia64_mca_ucmc_handler
 *
 *	This is uncorrectable machine check handler called from OS_MCA
 *	dispatch code which is in turn called from SAL_CHECK().
 *	This is the place where the core of OS MCA handling is done.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format. This handler code is supposed to be run only on the
 *	monarch processor. Once the monarch is done with MCA handling
 *	further MCA logging is enabled by clearing logs.
 *	Monarch also has the duty of sending wakeup-IPIs to pull the
 *	slave processors out of rendezvous spinloop.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
void
ia64_mca_ucmc_handler(void)
{
	pal_processor_state_info_t *psp = (pal_processor_state_info_t *)
		&ia64_sal_to_os_handoff_state.proc_state_param;
	int recover; 

	/* Get the MCA error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_MCA);

	/* TLB error is only exist in this SAL error record */
	recover = (psp->tc && !(psp->cc || psp->bc || psp->rc || psp->uc))
	/* other error recovery */
	   || (ia64_mca_ucmc_extension 
		&& ia64_mca_ucmc_extension(
			IA64_LOG_CURR_BUFFER(SAL_INFO_TYPE_MCA),
			&ia64_sal_to_os_handoff_state,
			&ia64_os_to_sal_handoff_state)); 

	if (recover) {
		sal_log_record_header_t *rh = IA64_LOG_CURR_BUFFER(SAL_INFO_TYPE_MCA);
		rh->severity = sal_log_severity_corrected;
		ia64_sal_clear_state_info(SAL_INFO_TYPE_MCA);
	}
	/*
	 *  Wakeup all the processors which are spinning in the rendezvous
	 *  loop.
	 */
	ia64_mca_wakeup_all();

	/* Return to SAL */
	ia64_return_to_sal_check(recover);
}

static DECLARE_WORK(cmc_disable_work, ia64_mca_cmc_vector_disable_keventd, NULL);
static DECLARE_WORK(cmc_enable_work, ia64_mca_cmc_vector_enable_keventd, NULL);

/*
 * ia64_mca_cmc_int_handler
 *
 *  This is corrected machine check interrupt handler.
 *	Right now the logs are extracted and displayed in a well-defined
 *	format.
 *
 * Inputs
 *      interrupt number
 *      client data arg ptr
 *      saved registers ptr
 *
 * Outputs
 *	None
 */
static irqreturn_t
ia64_mca_cmc_int_handler(int cmc_irq, void *arg, struct pt_regs *ptregs)
{
	static unsigned long	cmc_history[CMC_HISTORY_LENGTH];
	static int		index;
	static DEFINE_SPINLOCK(cmc_history_lock);

	IA64_MCA_DEBUG("%s: received interrupt vector = %#x on CPU %d\n",
		       __FUNCTION__, cmc_irq, smp_processor_id());

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

	/* Get the CMC error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CMC);

	spin_lock(&cmc_history_lock);
	if (!cmc_polling_enabled) {
		int i, count = 1; /* we know 1 happened now */
		unsigned long now = jiffies;

		for (i = 0; i < CMC_HISTORY_LENGTH; i++) {
			if (now - cmc_history[i] <= HZ)
				count++;
		}

		IA64_MCA_DEBUG(KERN_INFO "CMC threshold %d/%d\n", count, CMC_HISTORY_LENGTH);
		if (count >= CMC_HISTORY_LENGTH) {

			cmc_polling_enabled = 1;
			spin_unlock(&cmc_history_lock);
			schedule_work(&cmc_disable_work);

			/*
			 * Corrected errors will still be corrected, but
			 * make sure there's a log somewhere that indicates
			 * something is generating more than we can handle.
			 */
			printk(KERN_WARNING "WARNING: Switching to polling CMC handler; error records may be lost\n");

			mod_timer(&cmc_poll_timer, jiffies + CMC_POLL_INTERVAL);

			/* lock already released, get out now */
			return IRQ_HANDLED;
		} else {
			cmc_history[index++] = now;
			if (index == CMC_HISTORY_LENGTH)
				index = 0;
		}
	}
	spin_unlock(&cmc_history_lock);
	return IRQ_HANDLED;
}

/*
 *  ia64_mca_cmc_int_caller
 *
 * 	Triggered by sw interrupt from CMC polling routine.  Calls
 * 	real interrupt handler and either triggers a sw interrupt
 * 	on the next cpu or does cleanup at the end.
 *
 * Inputs
 *	interrupt number
 *	client data arg ptr
 *	saved registers ptr
 * Outputs
 * 	handled
 */
static irqreturn_t
ia64_mca_cmc_int_caller(int cmc_irq, void *arg, struct pt_regs *ptregs)
{
	static int start_count = -1;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CMC);

	ia64_mca_cmc_int_handler(cmc_irq, arg, ptregs);

	for (++cpuid ; cpuid < NR_CPUS && !cpu_online(cpuid) ; cpuid++);

	if (cpuid < NR_CPUS) {
		platform_send_ipi(cpuid, IA64_CMCP_VECTOR, IA64_IPI_DM_INT, 0);
	} else {
		/* If no log record, switch out of polling mode */
		if (start_count == IA64_LOG_COUNT(SAL_INFO_TYPE_CMC)) {

			printk(KERN_WARNING "Returning to interrupt driven CMC handler\n");
			schedule_work(&cmc_enable_work);
			cmc_polling_enabled = 0;

		} else {

			mod_timer(&cmc_poll_timer, jiffies + CMC_POLL_INTERVAL);
		}

		start_count = -1;
	}

	return IRQ_HANDLED;
}

/*
 *  ia64_mca_cmc_poll
 *
 *	Poll for Corrected Machine Checks (CMCs)
 *
 * Inputs   :   dummy(unused)
 * Outputs  :   None
 *
 */
static void
ia64_mca_cmc_poll (unsigned long dummy)
{
	/* Trigger a CMC interrupt cascade  */
	platform_send_ipi(first_cpu(cpu_online_map), IA64_CMCP_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 *  ia64_mca_cpe_int_caller
 *
 * 	Triggered by sw interrupt from CPE polling routine.  Calls
 * 	real interrupt handler and either triggers a sw interrupt
 * 	on the next cpu or does cleanup at the end.
 *
 * Inputs
 *	interrupt number
 *	client data arg ptr
 *	saved registers ptr
 * Outputs
 * 	handled
 */
#ifdef CONFIG_ACPI

static irqreturn_t
ia64_mca_cpe_int_caller(int cpe_irq, void *arg, struct pt_regs *ptregs)
{
	static int start_count = -1;
	static int poll_time = MIN_CPE_POLL_INTERVAL;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CPE);

	ia64_mca_cpe_int_handler(cpe_irq, arg, ptregs);

	for (++cpuid ; cpuid < NR_CPUS && !cpu_online(cpuid) ; cpuid++);

	if (cpuid < NR_CPUS) {
		platform_send_ipi(cpuid, IA64_CPEP_VECTOR, IA64_IPI_DM_INT, 0);
	} else {
		/*
		 * If a log was recorded, increase our polling frequency,
		 * otherwise, backoff or return to interrupt mode.
		 */
		if (start_count != IA64_LOG_COUNT(SAL_INFO_TYPE_CPE)) {
			poll_time = max(MIN_CPE_POLL_INTERVAL, poll_time / 2);
		} else if (cpe_vector < 0) {
			poll_time = min(MAX_CPE_POLL_INTERVAL, poll_time * 2);
		} else {
			poll_time = MIN_CPE_POLL_INTERVAL;

			printk(KERN_WARNING "Returning to interrupt driven CPE handler\n");
			enable_irq(local_vector_to_irq(IA64_CPE_VECTOR));
			cpe_poll_enabled = 0;
		}

		if (cpe_poll_enabled)
			mod_timer(&cpe_poll_timer, jiffies + poll_time);
		start_count = -1;
	}

	return IRQ_HANDLED;
}

#endif /* CONFIG_ACPI */

/*
 *  ia64_mca_cpe_poll
 *
 *	Poll for Corrected Platform Errors (CPEs), trigger interrupt
 *	on first cpu, from there it will trickle through all the cpus.
 *
 * Inputs   :   dummy(unused)
 * Outputs  :   None
 *
 */
static void
ia64_mca_cpe_poll (unsigned long dummy)
{
	/* Trigger a CPE interrupt cascade  */
	platform_send_ipi(first_cpu(cpu_online_map), IA64_CPEP_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 * C portion of the OS INIT handler
 *
 * Called from ia64_monarch_init_handler
 *
 * Inputs: pointer to pt_regs where processor info was saved.
 *
 * Returns:
 *   0 if SAL must warm boot the System
 *   1 if SAL must return to interrupted context using PAL_MC_RESUME
 *
 */
void
ia64_init_handler (struct pt_regs *pt, struct switch_stack *sw)
{
	pal_min_state_area_t *ms;

	oops_in_progress = 1;	/* avoid deadlock in printk, but it makes recovery dodgy */
	console_loglevel = 15;	/* make sure printks make it to console */

	printk(KERN_INFO "Entered OS INIT handler. PSP=%lx\n",
		ia64_sal_to_os_handoff_state.proc_state_param);

	/*
	 * Address of minstate area provided by PAL is physical,
	 * uncacheable (bit 63 set). Convert to Linux virtual
	 * address in region 6.
	 */
	ms = (pal_min_state_area_t *)(ia64_sal_to_os_handoff_state.pal_min_state | (6ul<<61));

	init_handler_platform(ms, pt, sw);	/* call platform specific routines */
}

static int __init
ia64_mca_disable_cpe_polling(char *str)
{
	cpe_poll_enabled = 0;
	return 1;
}

__setup("disable_cpe_poll", ia64_mca_disable_cpe_polling);

static struct irqaction cmci_irqaction = {
	.handler =	ia64_mca_cmc_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"cmc_hndlr"
};

static struct irqaction cmcp_irqaction = {
	.handler =	ia64_mca_cmc_int_caller,
	.flags =	SA_INTERRUPT,
	.name =		"cmc_poll"
};

static struct irqaction mca_rdzv_irqaction = {
	.handler =	ia64_mca_rendez_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"mca_rdzv"
};

static struct irqaction mca_wkup_irqaction = {
	.handler =	ia64_mca_wakeup_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"mca_wkup"
};

#ifdef CONFIG_ACPI
static struct irqaction mca_cpe_irqaction = {
	.handler =	ia64_mca_cpe_int_handler,
	.flags =	SA_INTERRUPT,
	.name =		"cpe_hndlr"
};

static struct irqaction mca_cpep_irqaction = {
	.handler =	ia64_mca_cpe_int_caller,
	.flags =	SA_INTERRUPT,
	.name =		"cpe_poll"
};
#endif /* CONFIG_ACPI */

/* Do per-CPU MCA-related initialization.  */

void __devinit
ia64_mca_cpu_init(void *cpu_data)
{
	void *pal_vaddr;

	if (smp_processor_id() == 0) {
		void *mca_data;
		int cpu;

		mca_data = alloc_bootmem(sizeof(struct ia64_mca_cpu)
					 * NR_CPUS);
		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			__per_cpu_mca[cpu] = __pa(mca_data);
			mca_data += sizeof(struct ia64_mca_cpu);
		}
	}

        /*
         * The MCA info structure was allocated earlier and its
         * physical address saved in __per_cpu_mca[cpu].  Copy that
         * address * to ia64_mca_data so we can access it as a per-CPU
         * variable.
         */
	__get_cpu_var(ia64_mca_data) = __per_cpu_mca[smp_processor_id()];

	/*
	 * Stash away a copy of the PTE needed to map the per-CPU page.
	 * We may need it during MCA recovery.
	 */
	__get_cpu_var(ia64_mca_per_cpu_pte) =
		pte_val(mk_pte_phys(__pa(cpu_data), PAGE_KERNEL));

        /*
         * Also, stash away a copy of the PAL address and the PTE
         * needed to map it.
         */
        pal_vaddr = efi_get_pal_addr();
	if (!pal_vaddr)
		return;
	__get_cpu_var(ia64_mca_pal_base) =
		GRANULEROUNDDOWN((unsigned long) pal_vaddr);
	__get_cpu_var(ia64_mca_pal_pte) = pte_val(mk_pte_phys(__pa(pal_vaddr),
							      PAGE_KERNEL));
}

/*
 * ia64_mca_init
 *
 *  Do all the system level mca specific initialization.
 *
 *	1. Register spinloop and wakeup request interrupt vectors
 *
 *	2. Register OS_MCA handler entry point
 *
 *	3. Register OS_INIT handler entry point
 *
 *  4. Initialize MCA/CMC/INIT related log buffers maintained by the OS.
 *
 *  Note that this initialization is done very early before some kernel
 *  services are available.
 *
 *  Inputs  :   None
 *
 *  Outputs :   None
 */
void __init
ia64_mca_init(void)
{
	ia64_fptr_t *mon_init_ptr = (ia64_fptr_t *)ia64_monarch_init_handler;
	ia64_fptr_t *slave_init_ptr = (ia64_fptr_t *)ia64_slave_init_handler;
	ia64_fptr_t *mca_hldlr_ptr = (ia64_fptr_t *)ia64_os_mca_dispatch;
	int i;
	s64 rc;
	struct ia64_sal_retval isrv;
	u64 timeout = IA64_MCA_RENDEZ_TIMEOUT;	/* platform specific */

	IA64_MCA_DEBUG("%s: begin\n", __FUNCTION__);

	/* Clear the Rendez checkin flag for all cpus */
	for(i = 0 ; i < NR_CPUS; i++)
		ia64_mc_info.imi_rendez_checkin[i] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;

	/*
	 * Register the rendezvous spinloop and wakeup mechanism with SAL
	 */

	/* Register the rendezvous interrupt vector with SAL */
	while (1) {
		isrv = ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_INT,
					      SAL_MC_PARAM_MECHANISM_INT,
					      IA64_MCA_RENDEZ_VECTOR,
					      timeout,
					      SAL_MC_PARAM_RZ_ALWAYS);
		rc = isrv.status;
		if (rc == 0)
			break;
		if (rc == -2) {
			printk(KERN_INFO "Increasing MCA rendezvous timeout from "
				"%ld to %ld milliseconds\n", timeout, isrv.v0);
			timeout = isrv.v0;
			continue;
		}
		printk(KERN_ERR "Failed to register rendezvous interrupt "
		       "with SAL (status %ld)\n", rc);
		return;
	}

	/* Register the wakeup interrupt vector with SAL */
	isrv = ia64_sal_mc_set_params(SAL_MC_PARAM_RENDEZ_WAKEUP,
				      SAL_MC_PARAM_MECHANISM_INT,
				      IA64_MCA_WAKEUP_VECTOR,
				      0, 0);
	rc = isrv.status;
	if (rc) {
		printk(KERN_ERR "Failed to register wakeup interrupt with SAL "
		       "(status %ld)\n", rc);
		return;
	}

	IA64_MCA_DEBUG("%s: registered MCA rendezvous spinloop and wakeup mech.\n", __FUNCTION__);

	ia64_mc_info.imi_mca_handler        = ia64_tpa(mca_hldlr_ptr->fp);
	/*
	 * XXX - disable SAL checksum by setting size to 0; should be
	 *	ia64_tpa(ia64_os_mca_dispatch_end) - ia64_tpa(ia64_os_mca_dispatch);
	 */
	ia64_mc_info.imi_mca_handler_size	= 0;

	/* Register the os mca handler with SAL */
	if ((rc = ia64_sal_set_vectors(SAL_VECTOR_OS_MCA,
				       ia64_mc_info.imi_mca_handler,
				       ia64_tpa(mca_hldlr_ptr->gp),
				       ia64_mc_info.imi_mca_handler_size,
				       0, 0, 0)))
	{
		printk(KERN_ERR "Failed to register OS MCA handler with SAL "
		       "(status %ld)\n", rc);
		return;
	}

	IA64_MCA_DEBUG("%s: registered OS MCA handler with SAL at 0x%lx, gp = 0x%lx\n", __FUNCTION__,
		       ia64_mc_info.imi_mca_handler, ia64_tpa(mca_hldlr_ptr->gp));

	/*
	 * XXX - disable SAL checksum by setting size to 0, should be
	 * size of the actual init handler in mca_asm.S.
	 */
	ia64_mc_info.imi_monarch_init_handler		= ia64_tpa(mon_init_ptr->fp);
	ia64_mc_info.imi_monarch_init_handler_size	= 0;
	ia64_mc_info.imi_slave_init_handler		= ia64_tpa(slave_init_ptr->fp);
	ia64_mc_info.imi_slave_init_handler_size	= 0;

	IA64_MCA_DEBUG("%s: OS INIT handler at %lx\n", __FUNCTION__,
		       ia64_mc_info.imi_monarch_init_handler);

	/* Register the os init handler with SAL */
	if ((rc = ia64_sal_set_vectors(SAL_VECTOR_OS_INIT,
				       ia64_mc_info.imi_monarch_init_handler,
				       ia64_tpa(ia64_getreg(_IA64_REG_GP)),
				       ia64_mc_info.imi_monarch_init_handler_size,
				       ia64_mc_info.imi_slave_init_handler,
				       ia64_tpa(ia64_getreg(_IA64_REG_GP)),
				       ia64_mc_info.imi_slave_init_handler_size)))
	{
		printk(KERN_ERR "Failed to register m/s INIT handlers with SAL "
		       "(status %ld)\n", rc);
		return;
	}

	IA64_MCA_DEBUG("%s: registered OS INIT handler with SAL\n", __FUNCTION__);

	/*
	 *  Configure the CMCI/P vector and handler. Interrupts for CMC are
	 *  per-processor, so AP CMC interrupts are setup in smp_callin() (smpboot.c).
	 */
	register_percpu_irq(IA64_CMC_VECTOR, &cmci_irqaction);
	register_percpu_irq(IA64_CMCP_VECTOR, &cmcp_irqaction);
	ia64_mca_cmc_vector_setup();       /* Setup vector on BSP */

	/* Setup the MCA rendezvous interrupt vector */
	register_percpu_irq(IA64_MCA_RENDEZ_VECTOR, &mca_rdzv_irqaction);

	/* Setup the MCA wakeup interrupt vector */
	register_percpu_irq(IA64_MCA_WAKEUP_VECTOR, &mca_wkup_irqaction);

#ifdef CONFIG_ACPI
	/* Setup the CPEI/P vector and handler */
	cpe_vector = acpi_request_vector(ACPI_INTERRUPT_CPEI);
	register_percpu_irq(IA64_CPEP_VECTOR, &mca_cpep_irqaction);
#endif

	/* Initialize the areas set aside by the OS to buffer the
	 * platform/processor error states for MCA/INIT/CMC
	 * handling.
	 */
	ia64_log_init(SAL_INFO_TYPE_MCA);
	ia64_log_init(SAL_INFO_TYPE_INIT);
	ia64_log_init(SAL_INFO_TYPE_CMC);
	ia64_log_init(SAL_INFO_TYPE_CPE);

	mca_init = 1;
	printk(KERN_INFO "MCA related initialization done\n");
}

/*
 * ia64_mca_late_init
 *
 *	Opportunity to setup things that require initialization later
 *	than ia64_mca_init.  Setup a timer to poll for CPEs if the
 *	platform doesn't support an interrupt driven mechanism.
 *
 *  Inputs  :   None
 *  Outputs :   Status
 */
static int __init
ia64_mca_late_init(void)
{
	if (!mca_init)
		return 0;

	/* Setup the CMCI/P vector and handler */
	init_timer(&cmc_poll_timer);
	cmc_poll_timer.function = ia64_mca_cmc_poll;

	/* Unmask/enable the vector */
	cmc_polling_enabled = 0;
	schedule_work(&cmc_enable_work);

	IA64_MCA_DEBUG("%s: CMCI/P setup and enabled.\n", __FUNCTION__);

#ifdef CONFIG_ACPI
	/* Setup the CPEI/P vector and handler */
	init_timer(&cpe_poll_timer);
	cpe_poll_timer.function = ia64_mca_cpe_poll;

	{
		irq_desc_t *desc;
		unsigned int irq;

		if (cpe_vector >= 0) {
			/* If platform supports CPEI, enable the irq. */
			cpe_poll_enabled = 0;
			for (irq = 0; irq < NR_IRQS; ++irq)
				if (irq_to_vector(irq) == cpe_vector) {
					desc = irq_descp(irq);
					desc->status |= IRQ_PER_CPU;
					setup_irq(irq, &mca_cpe_irqaction);
				}
			ia64_mca_register_cpev(cpe_vector);
			IA64_MCA_DEBUG("%s: CPEI/P setup and enabled.\n", __FUNCTION__);
		} else {
			/* If platform doesn't support CPEI, get the timer going. */
			if (cpe_poll_enabled) {
				ia64_mca_cpe_poll(0UL);
				IA64_MCA_DEBUG("%s: CPEP setup and enabled.\n", __FUNCTION__);
			}
		}
	}
#endif

	return 0;
}

device_initcall(ia64_mca_late_init);
