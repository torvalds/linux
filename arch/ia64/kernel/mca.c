// SPDX-License-Identifier: GPL-2.0-only
/*
 * File:	mca.c
 * Purpose:	Generic MCA handling layer
 *
 * Copyright (C) 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Copyright (C) 2002 Dell Inc.
 * Copyright (C) Matt Domsch <Matt_Domsch@dell.com>
 *
 * Copyright (C) 2002 Intel
 * Copyright (C) Jenna Hall <jenna.s.hall@intel.com>
 *
 * Copyright (C) 2001 Intel
 * Copyright (C) Fred Lewis <frederick.v.lewis@intel.com>
 *
 * Copyright (C) 2000 Intel
 * Copyright (C) Chuck Fleckenstein <cfleck@co.intel.com>
 *
 * Copyright (C) 1999, 2004-2008 Silicon Graphics, Inc.
 * Copyright (C) Vijay Chander <vijay@engr.sgi.com>
 *
 * Copyright (C) 2006 FUJITSU LIMITED
 * Copyright (C) Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 *
 * 2000-03-29 Chuck Fleckenstein <cfleck@co.intel.com>
 *	      Fixed PAL/SAL update issues, began MCA bug fixes, logging issues,
 *	      added min save state dump, added INIT handler.
 *
 * 2001-01-03 Fred Lewis <frederick.v.lewis@intel.com>
 *	      Added setup of CMCI and CPEI IRQs, logging of corrected platform
 *	      errors, completed code for logging of corrected & uncorrected
 *	      machine check errors, and updated for conformance with Nov. 2000
 *	      revision of the SAL 3.0 spec.
 *
 * 2002-01-04 Jenna Hall <jenna.s.hall@intel.com>
 *	      Aligned MCA stack to 16 bytes, added platform vs. CPU error flag,
 *	      set SAL default return values, changed error record structure to
 *	      linked list, added init call to sal_get_state_info_size().
 *
 * 2002-03-25 Matt Domsch <Matt_Domsch@dell.com>
 *	      GUID cleanups.
 *
 * 2003-04-15 David Mosberger-Tang <davidm@hpl.hp.com>
 *	      Added INIT backtrace support.
 *
 * 2003-12-08 Keith Owens <kaos@sgi.com>
 *	      smp_call_function() must not be called from interrupt context
 *	      (can deadlock on tasklist_lock).
 *	      Use keventd to call smp_call_function().
 *
 * 2004-02-01 Keith Owens <kaos@sgi.com>
 *	      Avoid deadlock when using printk() for MCA and INIT records.
 *	      Delete all record printing code, moved to salinfo_decode in user
 *	      space.  Mark variables and functions static where possible.
 *	      Delete dead variables and functions.  Reorder to remove the need
 *	      for forward declarations and to consolidate related code.
 *
 * 2005-08-12 Keith Owens <kaos@sgi.com>
 *	      Convert MCA/INIT handlers to use per event stacks and SAL/OS
 *	      state.
 *
 * 2005-10-07 Keith Owens <kaos@sgi.com>
 *	      Add notify_die() hooks.
 *
 * 2006-09-15 Hidetoshi Seto <seto.hidetoshi@jp.fujitsu.com>
 *	      Add printing support for MCA/INIT.
 *
 * 2007-04-27 Russ Anderson <rja@sgi.com>
 *	      Support multiple cpus going through OS_MCA in the same event.
 */
#include <linux/jiffies.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/acpi.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/workqueue.h>
#include <linux/cpumask.h>
#include <linux/kdebug.h>
#include <linux/cpu.h>
#include <linux/gfp.h>

#include <asm/delay.h>
#include <asm/efi.h>
#include <asm/meminit.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/sal.h>
#include <asm/mca.h>
#include <asm/mca_asm.h>
#include <asm/kexec.h>

#include <asm/irq.h>
#include <asm/hw_irq.h>
#include <asm/tlb.h>

#include "mca_drv.h"
#include "entry.h"
#include "irq.h"

#if defined(IA64_MCA_DEBUG_INFO)
# define IA64_MCA_DEBUG(fmt...) printk(fmt)
#else
# define IA64_MCA_DEBUG(fmt...) do {} while (0)
#endif

#define NOTIFY_INIT(event, regs, arg, spin)				\
do {									\
	if ((notify_die((event), "INIT", (regs), (arg), 0, 0)		\
			== NOTIFY_STOP) && ((spin) == 1))		\
		ia64_mca_spin(__func__);				\
} while (0)

#define NOTIFY_MCA(event, regs, arg, spin)				\
do {									\
	if ((notify_die((event), "MCA", (regs), (arg), 0, 0)		\
			== NOTIFY_STOP) && ((spin) == 1))		\
		ia64_mca_spin(__func__);				\
} while (0)

/* Used by mca_asm.S */
DEFINE_PER_CPU(u64, ia64_mca_data); /* == __per_cpu_mca[smp_processor_id()] */
DEFINE_PER_CPU(u64, ia64_mca_per_cpu_pte); /* PTE to map per-CPU area */
DEFINE_PER_CPU(u64, ia64_mca_pal_pte);	    /* PTE to map PAL code */
DEFINE_PER_CPU(u64, ia64_mca_pal_base);    /* vaddr PAL code granule */
DEFINE_PER_CPU(u64, ia64_mca_tr_reload);   /* Flag for TR reload */

unsigned long __per_cpu_mca[NR_CPUS];

/* In mca_asm.S */
extern void			ia64_os_init_dispatch_monarch (void);
extern void			ia64_os_init_dispatch_slave (void);

static int monarch_cpu = -1;

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

static int mca_init __initdata;

/*
 * limited & delayed printing support for MCA/INIT handler
 */

#define mprintk(fmt...) ia64_mca_printk(fmt)

#define MLOGBUF_SIZE (512+256*NR_CPUS)
#define MLOGBUF_MSGMAX 256
static char mlogbuf[MLOGBUF_SIZE];
static DEFINE_SPINLOCK(mlogbuf_wlock);	/* mca context only */
static DEFINE_SPINLOCK(mlogbuf_rlock);	/* normal context only */
static unsigned long mlogbuf_start;
static unsigned long mlogbuf_end;
static unsigned int mlogbuf_finished = 0;
static unsigned long mlogbuf_timestamp = 0;

static int loglevel_save = -1;
#define BREAK_LOGLEVEL(__console_loglevel)		\
	oops_in_progress = 1;				\
	if (loglevel_save < 0)				\
		loglevel_save = __console_loglevel;	\
	__console_loglevel = 15;

#define RESTORE_LOGLEVEL(__console_loglevel)		\
	if (loglevel_save >= 0) {			\
		__console_loglevel = loglevel_save;	\
		loglevel_save = -1;			\
	}						\
	mlogbuf_finished = 0;				\
	oops_in_progress = 0;

/*
 * Push messages into buffer, print them later if not urgent.
 */
void ia64_mca_printk(const char *fmt, ...)
{
	va_list args;
	int printed_len;
	char temp_buf[MLOGBUF_MSGMAX];
	char *p;

	va_start(args, fmt);
	printed_len = vscnprintf(temp_buf, sizeof(temp_buf), fmt, args);
	va_end(args);

	/* Copy the output into mlogbuf */
	if (oops_in_progress) {
		/* mlogbuf was abandoned, use printk directly instead. */
		printk("%s", temp_buf);
	} else {
		spin_lock(&mlogbuf_wlock);
		for (p = temp_buf; *p; p++) {
			unsigned long next = (mlogbuf_end + 1) % MLOGBUF_SIZE;
			if (next != mlogbuf_start) {
				mlogbuf[mlogbuf_end] = *p;
				mlogbuf_end = next;
			} else {
				/* buffer full */
				break;
			}
		}
		mlogbuf[mlogbuf_end] = '\0';
		spin_unlock(&mlogbuf_wlock);
	}
}
EXPORT_SYMBOL(ia64_mca_printk);

/*
 * Print buffered messages.
 *  NOTE: call this after returning normal context. (ex. from salinfod)
 */
void ia64_mlogbuf_dump(void)
{
	char temp_buf[MLOGBUF_MSGMAX];
	char *p;
	unsigned long index;
	unsigned long flags;
	unsigned int printed_len;

	/* Get output from mlogbuf */
	while (mlogbuf_start != mlogbuf_end) {
		temp_buf[0] = '\0';
		p = temp_buf;
		printed_len = 0;

		spin_lock_irqsave(&mlogbuf_rlock, flags);

		index = mlogbuf_start;
		while (index != mlogbuf_end) {
			*p = mlogbuf[index];
			index = (index + 1) % MLOGBUF_SIZE;
			if (!*p)
				break;
			p++;
			if (++printed_len >= MLOGBUF_MSGMAX - 1)
				break;
		}
		*p = '\0';
		if (temp_buf[0])
			printk("%s", temp_buf);
		mlogbuf_start = index;

		mlogbuf_timestamp = 0;
		spin_unlock_irqrestore(&mlogbuf_rlock, flags);
	}
}
EXPORT_SYMBOL(ia64_mlogbuf_dump);

/*
 * Call this if system is going to down or if immediate flushing messages to
 * console is required. (ex. recovery was failed, crash dump is going to be
 * invoked, long-wait rendezvous etc.)
 *  NOTE: this should be called from monarch.
 */
static void ia64_mlogbuf_finish(int wait)
{
	BREAK_LOGLEVEL(console_loglevel);

	ia64_mlogbuf_dump();
	printk(KERN_EMERG "mlogbuf_finish: printing switched to urgent mode, "
		"MCA/INIT might be dodgy or fail.\n");

	if (!wait)
		return;

	/* wait for console */
	printk("Delaying for 5 seconds...\n");
	udelay(5*1000000);

	mlogbuf_finished = 1;
}

/*
 * Print buffered messages from INIT context.
 */
static void ia64_mlogbuf_dump_from_init(void)
{
	if (mlogbuf_finished)
		return;

	if (mlogbuf_timestamp &&
			time_before(jiffies, mlogbuf_timestamp + 30 * HZ)) {
		printk(KERN_ERR "INIT: mlogbuf_dump is interrupted by INIT "
			" and the system seems to be messed up.\n");
		ia64_mlogbuf_finish(0);
		return;
	}

	if (!spin_trylock(&mlogbuf_rlock)) {
		printk(KERN_ERR "INIT: mlogbuf_dump is interrupted by INIT. "
			"Generated messages other than stack dump will be "
			"buffered to mlogbuf and will be printed later.\n");
		printk(KERN_ERR "INIT: If messages would not printed after "
			"this INIT, wait 30sec and assert INIT again.\n");
		if (!mlogbuf_timestamp)
			mlogbuf_timestamp = jiffies;
		return;
	}
	spin_unlock(&mlogbuf_rlock);
	ia64_mlogbuf_dump();
}

static inline void
ia64_mca_spin(const char *func)
{
	if (monarch_cpu == smp_processor_id())
		ia64_mlogbuf_finish(0);
	mprintk(KERN_EMERG "%s: spinning here, not returning to SAL\n", func);
	while (1)
		cpu_relax();
}
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

static inline void ia64_log_allocate(int it, u64 size)
{
	ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)] =
		(ia64_err_rec_t *)memblock_alloc(size, SMP_CACHE_BYTES);
	if (!ia64_state_log[it].isl_log[IA64_LOG_CURR_INDEX(it)])
		panic("%s: Failed to allocate %llu bytes\n", __func__, size);

	ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)] =
		(ia64_err_rec_t *)memblock_alloc(size, SMP_CACHE_BYTES);
	if (!ia64_state_log[it].isl_log[IA64_LOG_NEXT_INDEX(it)])
		panic("%s: Failed to allocate %llu bytes\n", __func__, size);
}

/*
 * ia64_log_init
 *	Reset the OS ia64 log buffer
 * Inputs   :   info_type   (SAL_INFO_TYPE_{MCA,INIT,CMC,CPE})
 * Outputs	:	None
 */
static void __init
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
	ia64_log_allocate(sal_info_type, max_size);
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
	unsigned long               s;

	IA64_LOG_LOCK(sal_info_type);

	/* Get the process state information */
	log_buffer = IA64_LOG_NEXT_BUFFER(sal_info_type);

	total_len = ia64_sal_get_state_info(sal_info_type, (u64 *)log_buffer);

	if (total_len) {
		IA64_LOG_INDEX_INC(sal_info_type);
		IA64_LOG_UNLOCK(sal_info_type);
		if (irq_safe) {
			IA64_MCA_DEBUG("%s: SAL error record type %d retrieved. Record length = %ld\n",
				       __func__, sal_info_type, total_len);
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
 *  Inputs  :   sal_info_type   (Type of error record MCA/CMC/CPE)
 *              FIXME: remove MCA and irq_safe.
 */
static void
ia64_mca_log_sal_error_record(int sal_info_type)
{
	u8 *buffer;
	sal_log_record_header_t *rh;
	u64 size;
	int irq_safe = sal_info_type != SAL_INFO_TYPE_MCA;
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
 * search_mca_table
 *  See if the MCA surfaced in an instruction range
 *  that has been tagged as recoverable.
 *
 *  Inputs
 *	first	First address range to check
 *	last	Last address range to check
 *	ip	Instruction pointer, address we are looking for
 *
 * Return value:
 *      1 on Success (in the table)/ 0 on Failure (not in the  table)
 */
int
search_mca_table (const struct mca_table_entry *first,
                const struct mca_table_entry *last,
                unsigned long ip)
{
        const struct mca_table_entry *curr;
        u64 curr_start, curr_end;

        curr = first;
        while (curr <= last) {
                curr_start = (u64) &curr->start_addr + curr->start_addr;
                curr_end = (u64) &curr->end_addr + curr->end_addr;

                if ((ip >= curr_start) && (ip <= curr_end)) {
                        return 1;
                }
                curr++;
        }
        return 0;
}

/* Given an address, look for it in the mca tables. */
int mca_recover_range(unsigned long addr)
{
	extern struct mca_table_entry __start___mca_table[];
	extern struct mca_table_entry __stop___mca_table[];

	return search_mca_table(__start___mca_table, __stop___mca_table-1, addr);
}
EXPORT_SYMBOL_GPL(mca_recover_range);

int cpe_vector = -1;
int ia64_cpe_irq = -1;

static irqreturn_t
ia64_mca_cpe_int_handler (int cpe_irq, void *arg)
{
	static unsigned long	cpe_history[CPE_HISTORY_LENGTH];
	static int		index;
	static DEFINE_SPINLOCK(cpe_history_lock);

	IA64_MCA_DEBUG("%s: received interrupt vector = %#x on CPU %d\n",
		       __func__, cpe_irq, smp_processor_id());

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

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
			goto out;
		} else {
			cpe_history[index++] = now;
			if (index == CPE_HISTORY_LENGTH)
				index = 0;
		}
	}
	spin_unlock(&cpe_history_lock);
out:
	/* Get the CPE error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CPE);

	local_irq_disable();

	return IRQ_HANDLED;
}

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
void
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
		       "vector %#x registered\n", __func__, cpev);
}

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

	IA64_MCA_DEBUG("%s: CPU %d corrected machine check vector %#x registered.\n",
		       __func__, smp_processor_id(), IA64_CMC_VECTOR);

	IA64_MCA_DEBUG("%s: CPU %d CMCV = %#016lx\n",
		       __func__, smp_processor_id(), ia64_getreg(_IA64_REG_CR_CMCV));
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

	IA64_MCA_DEBUG("%s: CPU %d corrected machine check vector %#x disabled.\n",
		       __func__, smp_processor_id(), cmcv.cmcv_vector);
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

	IA64_MCA_DEBUG("%s: CPU %d corrected machine check vector %#x enabled.\n",
		       __func__, smp_processor_id(), cmcv.cmcv_vector);
}

/*
 * ia64_mca_cmc_vector_disable_keventd
 *
 * Called via keventd (smp_call_function() is not safe in interrupt context) to
 * disable the cmc interrupt vector.
 */
static void
ia64_mca_cmc_vector_disable_keventd(struct work_struct *unused)
{
	on_each_cpu(ia64_mca_cmc_vector_disable, NULL, 0);
}

/*
 * ia64_mca_cmc_vector_enable_keventd
 *
 * Called via keventd (smp_call_function() is not safe in interrupt context) to
 * enable the cmc interrupt vector.
 */
static void
ia64_mca_cmc_vector_enable_keventd(struct work_struct *unused)
{
	on_each_cpu(ia64_mca_cmc_vector_enable, NULL, 0);
}

/*
 * ia64_mca_wakeup
 *
 *	Send an inter-cpu interrupt to wake-up a particular cpu.
 *
 *  Inputs  :   cpuid
 *  Outputs :   None
 */
static void
ia64_mca_wakeup(int cpu)
{
	ia64_send_ipi(cpu, IA64_MCA_WAKEUP_VECTOR, IA64_IPI_DM_INT, 0);
}

/*
 * ia64_mca_wakeup_all
 *
 *	Wakeup all the slave cpus which have rendez'ed previously.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
static void
ia64_mca_wakeup_all(void)
{
	int cpu;

	/* Clear the Rendez checkin flag for all cpus */
	for_each_online_cpu(cpu) {
		if (ia64_mc_info.imi_rendez_checkin[cpu] == IA64_MCA_RENDEZ_CHECKIN_DONE)
			ia64_mca_wakeup(cpu);
	}

}

/*
 * ia64_mca_rendez_interrupt_handler
 *
 *	This is handler used to put slave processors into spinloop
 *	while the monarch processor does the mca handling and later
 *	wake each slave up once the monarch is done.  The state
 *	IA64_MCA_RENDEZ_CHECKIN_DONE indicates the cpu is rendez'ed
 *	in SAL.  The state IA64_MCA_RENDEZ_CHECKIN_NOTDONE indicates
 *	the cpu has come out of OS rendezvous.
 *
 *  Inputs  :   None
 *  Outputs :   None
 */
static irqreturn_t
ia64_mca_rendez_int_handler(int rendez_irq, void *arg)
{
	unsigned long flags;
	int cpu = smp_processor_id();
	struct ia64_mca_notify_die nd =
		{ .sos = NULL, .monarch_cpu = &monarch_cpu };

	/* Mask all interrupts */
	local_irq_save(flags);

	NOTIFY_MCA(DIE_MCA_RENDZVOUS_ENTER, get_irq_regs(), (long)&nd, 1);

	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_DONE;
	/* Register with the SAL monarch that the slave has
	 * reached SAL
	 */
	ia64_sal_mc_rendez();

	NOTIFY_MCA(DIE_MCA_RENDZVOUS_PROCESS, get_irq_regs(), (long)&nd, 1);

	/* Wait for the monarch cpu to exit. */
	while (monarch_cpu != -1)
	       cpu_relax();	/* spin until monarch leaves */

	NOTIFY_MCA(DIE_MCA_RENDZVOUS_LEAVE, get_irq_regs(), (long)&nd, 1);

	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;
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
 *  Outputs :   None
 *
 */
static irqreturn_t
ia64_mca_wakeup_int_handler(int wakeup_irq, void *arg)
{
	return IRQ_HANDLED;
}

/* Function pointer for extra MCA recovery */
int (*ia64_mca_ucmc_extension)
	(void*,struct ia64_sal_os_state*)
	= NULL;

int
ia64_reg_MCA_extension(int (*fn)(void *, struct ia64_sal_os_state *))
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


static inline void
copy_reg(const u64 *fr, u64 fnat, unsigned long *tr, unsigned long *tnat)
{
	u64 fslot, tslot, nat;
	*tr = *fr;
	fslot = ((unsigned long)fr >> 3) & 63;
	tslot = ((unsigned long)tr >> 3) & 63;
	*tnat &= ~(1UL << tslot);
	nat = (fnat >> fslot) & 1;
	*tnat |= (nat << tslot);
}

/* Change the comm field on the MCA/INT task to include the pid that
 * was interrupted, it makes for easier debugging.  If that pid was 0
 * (swapper or nested MCA/INIT) then use the start of the previous comm
 * field suffixed with its cpu.
 */

static void
ia64_mca_modify_comm(const struct task_struct *previous_current)
{
	char *p, comm[sizeof(current->comm)];
	if (previous_current->pid)
		snprintf(comm, sizeof(comm), "%s %d",
			current->comm, previous_current->pid);
	else {
		int l;
		if ((p = strchr(previous_current->comm, ' ')))
			l = p - previous_current->comm;
		else
			l = strlen(previous_current->comm);
		snprintf(comm, sizeof(comm), "%s %*s %d",
			current->comm, l, previous_current->comm,
			task_thread_info(previous_current)->cpu);
	}
	memcpy(current->comm, comm, sizeof(current->comm));
}

static void
finish_pt_regs(struct pt_regs *regs, struct ia64_sal_os_state *sos,
		unsigned long *nat)
{
	const struct pal_min_state_area *ms = sos->pal_min_state;
	const u64 *bank;

	/* If ipsr.ic then use pmsa_{iip,ipsr,ifs}, else use
	 * pmsa_{xip,xpsr,xfs}
	 */
	if (ia64_psr(regs)->ic) {
		regs->cr_iip = ms->pmsa_iip;
		regs->cr_ipsr = ms->pmsa_ipsr;
		regs->cr_ifs = ms->pmsa_ifs;
	} else {
		regs->cr_iip = ms->pmsa_xip;
		regs->cr_ipsr = ms->pmsa_xpsr;
		regs->cr_ifs = ms->pmsa_xfs;

		sos->iip = ms->pmsa_iip;
		sos->ipsr = ms->pmsa_ipsr;
		sos->ifs = ms->pmsa_ifs;
	}
	regs->pr = ms->pmsa_pr;
	regs->b0 = ms->pmsa_br0;
	regs->ar_rsc = ms->pmsa_rsc;
	copy_reg(&ms->pmsa_gr[1-1], ms->pmsa_nat_bits, &regs->r1, nat);
	copy_reg(&ms->pmsa_gr[2-1], ms->pmsa_nat_bits, &regs->r2, nat);
	copy_reg(&ms->pmsa_gr[3-1], ms->pmsa_nat_bits, &regs->r3, nat);
	copy_reg(&ms->pmsa_gr[8-1], ms->pmsa_nat_bits, &regs->r8, nat);
	copy_reg(&ms->pmsa_gr[9-1], ms->pmsa_nat_bits, &regs->r9, nat);
	copy_reg(&ms->pmsa_gr[10-1], ms->pmsa_nat_bits, &regs->r10, nat);
	copy_reg(&ms->pmsa_gr[11-1], ms->pmsa_nat_bits, &regs->r11, nat);
	copy_reg(&ms->pmsa_gr[12-1], ms->pmsa_nat_bits, &regs->r12, nat);
	copy_reg(&ms->pmsa_gr[13-1], ms->pmsa_nat_bits, &regs->r13, nat);
	copy_reg(&ms->pmsa_gr[14-1], ms->pmsa_nat_bits, &regs->r14, nat);
	copy_reg(&ms->pmsa_gr[15-1], ms->pmsa_nat_bits, &regs->r15, nat);
	if (ia64_psr(regs)->bn)
		bank = ms->pmsa_bank1_gr;
	else
		bank = ms->pmsa_bank0_gr;
	copy_reg(&bank[16-16], ms->pmsa_nat_bits, &regs->r16, nat);
	copy_reg(&bank[17-16], ms->pmsa_nat_bits, &regs->r17, nat);
	copy_reg(&bank[18-16], ms->pmsa_nat_bits, &regs->r18, nat);
	copy_reg(&bank[19-16], ms->pmsa_nat_bits, &regs->r19, nat);
	copy_reg(&bank[20-16], ms->pmsa_nat_bits, &regs->r20, nat);
	copy_reg(&bank[21-16], ms->pmsa_nat_bits, &regs->r21, nat);
	copy_reg(&bank[22-16], ms->pmsa_nat_bits, &regs->r22, nat);
	copy_reg(&bank[23-16], ms->pmsa_nat_bits, &regs->r23, nat);
	copy_reg(&bank[24-16], ms->pmsa_nat_bits, &regs->r24, nat);
	copy_reg(&bank[25-16], ms->pmsa_nat_bits, &regs->r25, nat);
	copy_reg(&bank[26-16], ms->pmsa_nat_bits, &regs->r26, nat);
	copy_reg(&bank[27-16], ms->pmsa_nat_bits, &regs->r27, nat);
	copy_reg(&bank[28-16], ms->pmsa_nat_bits, &regs->r28, nat);
	copy_reg(&bank[29-16], ms->pmsa_nat_bits, &regs->r29, nat);
	copy_reg(&bank[30-16], ms->pmsa_nat_bits, &regs->r30, nat);
	copy_reg(&bank[31-16], ms->pmsa_nat_bits, &regs->r31, nat);
}

/* On entry to this routine, we are running on the per cpu stack, see
 * mca_asm.h.  The original stack has not been touched by this event.  Some of
 * the original stack's registers will be in the RBS on this stack.  This stack
 * also contains a partial pt_regs and switch_stack, the rest of the data is in
 * PAL minstate.
 *
 * The first thing to do is modify the original stack to look like a blocked
 * task so we can run backtrace on the original task.  Also mark the per cpu
 * stack as current to ensure that we use the correct task state, it also means
 * that we can do backtrace on the MCA/INIT handler code itself.
 */

static struct task_struct *
ia64_mca_modify_original_stack(struct pt_regs *regs,
		const struct switch_stack *sw,
		struct ia64_sal_os_state *sos,
		const char *type)
{
	char *p;
	ia64_va va;
	extern char ia64_leave_kernel[];	/* Need asm address, not function descriptor */
	const struct pal_min_state_area *ms = sos->pal_min_state;
	struct task_struct *previous_current;
	struct pt_regs *old_regs;
	struct switch_stack *old_sw;
	unsigned size = sizeof(struct pt_regs) +
			sizeof(struct switch_stack) + 16;
	unsigned long *old_bspstore, *old_bsp;
	unsigned long *new_bspstore, *new_bsp;
	unsigned long old_unat, old_rnat, new_rnat, nat;
	u64 slots, loadrs = regs->loadrs;
	u64 r12 = ms->pmsa_gr[12-1], r13 = ms->pmsa_gr[13-1];
	u64 ar_bspstore = regs->ar_bspstore;
	u64 ar_bsp = regs->ar_bspstore + (loadrs >> 16);
	const char *msg;
	int cpu = smp_processor_id();

	previous_current = curr_task(cpu);
	ia64_set_curr_task(cpu, current);
	if ((p = strchr(current->comm, ' ')))
		*p = '\0';

	/* Best effort attempt to cope with MCA/INIT delivered while in
	 * physical mode.
	 */
	regs->cr_ipsr = ms->pmsa_ipsr;
	if (ia64_psr(regs)->dt == 0) {
		va.l = r12;
		if (va.f.reg == 0) {
			va.f.reg = 7;
			r12 = va.l;
		}
		va.l = r13;
		if (va.f.reg == 0) {
			va.f.reg = 7;
			r13 = va.l;
		}
	}
	if (ia64_psr(regs)->rt == 0) {
		va.l = ar_bspstore;
		if (va.f.reg == 0) {
			va.f.reg = 7;
			ar_bspstore = va.l;
		}
		va.l = ar_bsp;
		if (va.f.reg == 0) {
			va.f.reg = 7;
			ar_bsp = va.l;
		}
	}

	/* mca_asm.S ia64_old_stack() cannot assume that the dirty registers
	 * have been copied to the old stack, the old stack may fail the
	 * validation tests below.  So ia64_old_stack() must restore the dirty
	 * registers from the new stack.  The old and new bspstore probably
	 * have different alignments, so loadrs calculated on the old bsp
	 * cannot be used to restore from the new bsp.  Calculate a suitable
	 * loadrs for the new stack and save it in the new pt_regs, where
	 * ia64_old_stack() can get it.
	 */
	old_bspstore = (unsigned long *)ar_bspstore;
	old_bsp = (unsigned long *)ar_bsp;
	slots = ia64_rse_num_regs(old_bspstore, old_bsp);
	new_bspstore = (unsigned long *)((u64)current + IA64_RBS_OFFSET);
	new_bsp = ia64_rse_skip_regs(new_bspstore, slots);
	regs->loadrs = (new_bsp - new_bspstore) * 8 << 16;

	/* Verify the previous stack state before we change it */
	if (user_mode(regs)) {
		msg = "occurred in user space";
		/* previous_current is guaranteed to be valid when the task was
		 * in user space, so ...
		 */
		ia64_mca_modify_comm(previous_current);
		goto no_mod;
	}

	if (r13 != sos->prev_IA64_KR_CURRENT) {
		msg = "inconsistent previous current and r13";
		goto no_mod;
	}

	if (!mca_recover_range(ms->pmsa_iip)) {
		if ((r12 - r13) >= KERNEL_STACK_SIZE) {
			msg = "inconsistent r12 and r13";
			goto no_mod;
		}
		if ((ar_bspstore - r13) >= KERNEL_STACK_SIZE) {
			msg = "inconsistent ar.bspstore and r13";
			goto no_mod;
		}
		va.p = old_bspstore;
		if (va.f.reg < 5) {
			msg = "old_bspstore is in the wrong region";
			goto no_mod;
		}
		if ((ar_bsp - r13) >= KERNEL_STACK_SIZE) {
			msg = "inconsistent ar.bsp and r13";
			goto no_mod;
		}
		size += (ia64_rse_skip_regs(old_bspstore, slots) - old_bspstore) * 8;
		if (ar_bspstore + size > r12) {
			msg = "no room for blocked state";
			goto no_mod;
		}
	}

	ia64_mca_modify_comm(previous_current);

	/* Make the original task look blocked.  First stack a struct pt_regs,
	 * describing the state at the time of interrupt.  mca_asm.S built a
	 * partial pt_regs, copy it and fill in the blanks using minstate.
	 */
	p = (char *)r12 - sizeof(*regs);
	old_regs = (struct pt_regs *)p;
	memcpy(old_regs, regs, sizeof(*regs));
	old_regs->loadrs = loadrs;
	old_unat = old_regs->ar_unat;
	finish_pt_regs(old_regs, sos, &old_unat);

	/* Next stack a struct switch_stack.  mca_asm.S built a partial
	 * switch_stack, copy it and fill in the blanks using pt_regs and
	 * minstate.
	 *
	 * In the synthesized switch_stack, b0 points to ia64_leave_kernel,
	 * ar.pfs is set to 0.
	 *
	 * unwind.c::unw_unwind() does special processing for interrupt frames.
	 * It checks if the PRED_NON_SYSCALL predicate is set, if the predicate
	 * is clear then unw_unwind() does _not_ adjust bsp over pt_regs.  Not
	 * that this is documented, of course.  Set PRED_NON_SYSCALL in the
	 * switch_stack on the original stack so it will unwind correctly when
	 * unwind.c reads pt_regs.
	 *
	 * thread.ksp is updated to point to the synthesized switch_stack.
	 */
	p -= sizeof(struct switch_stack);
	old_sw = (struct switch_stack *)p;
	memcpy(old_sw, sw, sizeof(*sw));
	old_sw->caller_unat = old_unat;
	old_sw->ar_fpsr = old_regs->ar_fpsr;
	copy_reg(&ms->pmsa_gr[4-1], ms->pmsa_nat_bits, &old_sw->r4, &old_unat);
	copy_reg(&ms->pmsa_gr[5-1], ms->pmsa_nat_bits, &old_sw->r5, &old_unat);
	copy_reg(&ms->pmsa_gr[6-1], ms->pmsa_nat_bits, &old_sw->r6, &old_unat);
	copy_reg(&ms->pmsa_gr[7-1], ms->pmsa_nat_bits, &old_sw->r7, &old_unat);
	old_sw->b0 = (u64)ia64_leave_kernel;
	old_sw->b1 = ms->pmsa_br1;
	old_sw->ar_pfs = 0;
	old_sw->ar_unat = old_unat;
	old_sw->pr = old_regs->pr | (1UL << PRED_NON_SYSCALL);
	previous_current->thread.ksp = (u64)p - 16;

	/* Finally copy the original stack's registers back to its RBS.
	 * Registers from ar.bspstore through ar.bsp at the time of the event
	 * are in the current RBS, copy them back to the original stack.  The
	 * copy must be done register by register because the original bspstore
	 * and the current one have different alignments, so the saved RNAT
	 * data occurs at different places.
	 *
	 * mca_asm does cover, so the old_bsp already includes all registers at
	 * the time of MCA/INIT.  It also does flushrs, so all registers before
	 * this function have been written to backing store on the MCA/INIT
	 * stack.
	 */
	new_rnat = ia64_get_rnat(ia64_rse_rnat_addr(new_bspstore));
	old_rnat = regs->ar_rnat;
	while (slots--) {
		if (ia64_rse_is_rnat_slot(new_bspstore)) {
			new_rnat = ia64_get_rnat(new_bspstore++);
		}
		if (ia64_rse_is_rnat_slot(old_bspstore)) {
			*old_bspstore++ = old_rnat;
			old_rnat = 0;
		}
		nat = (new_rnat >> ia64_rse_slot_num(new_bspstore)) & 1UL;
		old_rnat &= ~(1UL << ia64_rse_slot_num(old_bspstore));
		old_rnat |= (nat << ia64_rse_slot_num(old_bspstore));
		*old_bspstore++ = *new_bspstore++;
	}
	old_sw->ar_bspstore = (unsigned long)old_bspstore;
	old_sw->ar_rnat = old_rnat;

	sos->prev_task = previous_current;
	return previous_current;

no_mod:
	mprintk(KERN_INFO "cpu %d, %s %s, original stack not modified\n",
			smp_processor_id(), type, msg);
	old_unat = regs->ar_unat;
	finish_pt_regs(regs, sos, &old_unat);
	return previous_current;
}

/* The monarch/slave interaction is based on monarch_cpu and requires that all
 * slaves have entered rendezvous before the monarch leaves.  If any cpu has
 * not entered rendezvous yet then wait a bit.  The assumption is that any
 * slave that has not rendezvoused after a reasonable time is never going to do
 * so.  In this context, slave includes cpus that respond to the MCA rendezvous
 * interrupt, as well as cpus that receive the INIT slave event.
 */

static void
ia64_wait_for_slaves(int monarch, const char *type)
{
	int c, i , wait;

	/*
	 * wait 5 seconds total for slaves (arbitrary)
	 */
	for (i = 0; i < 5000; i++) {
		wait = 0;
		for_each_online_cpu(c) {
			if (c == monarch)
				continue;
			if (ia64_mc_info.imi_rendez_checkin[c]
					== IA64_MCA_RENDEZ_CHECKIN_NOTDONE) {
				udelay(1000);		/* short wait */
				wait = 1;
				break;
			}
		}
		if (!wait)
			goto all_in;
	}

	/*
	 * Maybe slave(s) dead. Print buffered messages immediately.
	 */
	ia64_mlogbuf_finish(0);
	mprintk(KERN_INFO "OS %s slave did not rendezvous on cpu", type);
	for_each_online_cpu(c) {
		if (c == monarch)
			continue;
		if (ia64_mc_info.imi_rendez_checkin[c] == IA64_MCA_RENDEZ_CHECKIN_NOTDONE)
			mprintk(" %d", c);
	}
	mprintk("\n");
	return;

all_in:
	mprintk(KERN_INFO "All OS %s slaves have reached rendezvous\n", type);
	return;
}

/*  mca_insert_tr
 *
 *  Switch rid when TR reload and needed!
 *  iord: 1: itr, 2: itr;
 *
*/
static void mca_insert_tr(u64 iord)
{

	int i;
	u64 old_rr;
	struct ia64_tr_entry *p;
	unsigned long psr;
	int cpu = smp_processor_id();

	if (!ia64_idtrs[cpu])
		return;

	psr = ia64_clear_ic();
	for (i = IA64_TR_ALLOC_BASE; i < IA64_TR_ALLOC_MAX; i++) {
		p = ia64_idtrs[cpu] + (iord - 1) * IA64_TR_ALLOC_MAX;
		if (p->pte & 0x1) {
			old_rr = ia64_get_rr(p->ifa);
			if (old_rr != p->rr) {
				ia64_set_rr(p->ifa, p->rr);
				ia64_srlz_d();
			}
			ia64_ptr(iord, p->ifa, p->itir >> 2);
			ia64_srlz_i();
			if (iord & 0x1) {
				ia64_itr(0x1, i, p->ifa, p->pte, p->itir >> 2);
				ia64_srlz_i();
			}
			if (iord & 0x2) {
				ia64_itr(0x2, i, p->ifa, p->pte, p->itir >> 2);
				ia64_srlz_i();
			}
			if (old_rr != p->rr) {
				ia64_set_rr(p->ifa, old_rr);
				ia64_srlz_d();
			}
		}
	}
	ia64_set_psr(psr);
}

/*
 * ia64_mca_handler
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
 *	If multiple processors call into OS_MCA, the first will become
 *	the monarch.  Subsequent cpus will be recorded in the mca_cpu
 *	bitmask.  After the first monarch has processed its MCA, it
 *	will wake up the next cpu in the mca_cpu bitmask and then go
 *	into the rendezvous loop.  When all processors have serviced
 *	their MCA, the last monarch frees up the rest of the processors.
 */
void
ia64_mca_handler(struct pt_regs *regs, struct switch_stack *sw,
		 struct ia64_sal_os_state *sos)
{
	int recover, cpu = smp_processor_id();
	struct task_struct *previous_current;
	struct ia64_mca_notify_die nd =
		{ .sos = sos, .monarch_cpu = &monarch_cpu, .data = &recover };
	static atomic_t mca_count;
	static cpumask_t mca_cpu;

	if (atomic_add_return(1, &mca_count) == 1) {
		monarch_cpu = cpu;
		sos->monarch = 1;
	} else {
		cpumask_set_cpu(cpu, &mca_cpu);
		sos->monarch = 0;
	}
	mprintk(KERN_INFO "Entered OS MCA handler. PSP=%lx cpu=%d "
		"monarch=%ld\n", sos->proc_state_param, cpu, sos->monarch);

	previous_current = ia64_mca_modify_original_stack(regs, sw, sos, "MCA");

	NOTIFY_MCA(DIE_MCA_MONARCH_ENTER, regs, (long)&nd, 1);

	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_CONCURRENT_MCA;
	if (sos->monarch) {
		ia64_wait_for_slaves(cpu, "MCA");

		/* Wakeup all the processors which are spinning in the
		 * rendezvous loop.  They will leave SAL, then spin in the OS
		 * with interrupts disabled until this monarch cpu leaves the
		 * MCA handler.  That gets control back to the OS so we can
		 * backtrace the other cpus, backtrace when spinning in SAL
		 * does not work.
		 */
		ia64_mca_wakeup_all();
	} else {
		while (cpumask_test_cpu(cpu, &mca_cpu))
			cpu_relax();	/* spin until monarch wakes us */
	}

	NOTIFY_MCA(DIE_MCA_MONARCH_PROCESS, regs, (long)&nd, 1);

	/* Get the MCA error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_MCA);

	/* MCA error recovery */
	recover = (ia64_mca_ucmc_extension
		&& ia64_mca_ucmc_extension(
			IA64_LOG_CURR_BUFFER(SAL_INFO_TYPE_MCA),
			sos));

	if (recover) {
		sal_log_record_header_t *rh = IA64_LOG_CURR_BUFFER(SAL_INFO_TYPE_MCA);
		rh->severity = sal_log_severity_corrected;
		ia64_sal_clear_state_info(SAL_INFO_TYPE_MCA);
		sos->os_status = IA64_MCA_CORRECTED;
	} else {
		/* Dump buffered message to console */
		ia64_mlogbuf_finish(1);
	}

	if (__this_cpu_read(ia64_mca_tr_reload)) {
		mca_insert_tr(0x1); /*Reload dynamic itrs*/
		mca_insert_tr(0x2); /*Reload dynamic itrs*/
	}

	NOTIFY_MCA(DIE_MCA_MONARCH_LEAVE, regs, (long)&nd, 1);

	if (atomic_dec_return(&mca_count) > 0) {
		int i;

		/* wake up the next monarch cpu,
		 * and put this cpu in the rendez loop.
		 */
		for_each_online_cpu(i) {
			if (cpumask_test_cpu(i, &mca_cpu)) {
				monarch_cpu = i;
				cpumask_clear_cpu(i, &mca_cpu);	/* wake next cpu */
				while (monarch_cpu != -1)
					cpu_relax();	/* spin until last cpu leaves */
				ia64_set_curr_task(cpu, previous_current);
				ia64_mc_info.imi_rendez_checkin[cpu]
						= IA64_MCA_RENDEZ_CHECKIN_NOTDONE;
				return;
			}
		}
	}
	ia64_set_curr_task(cpu, previous_current);
	ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;
	monarch_cpu = -1;	/* This frees the slaves and previous monarchs */
}

static DECLARE_WORK(cmc_disable_work, ia64_mca_cmc_vector_disable_keventd);
static DECLARE_WORK(cmc_enable_work, ia64_mca_cmc_vector_enable_keventd);

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
 *
 * Outputs
 *	None
 */
static irqreturn_t
ia64_mca_cmc_int_handler(int cmc_irq, void *arg)
{
	static unsigned long	cmc_history[CMC_HISTORY_LENGTH];
	static int		index;
	static DEFINE_SPINLOCK(cmc_history_lock);

	IA64_MCA_DEBUG("%s: received interrupt vector = %#x on CPU %d\n",
		       __func__, cmc_irq, smp_processor_id());

	/* SAL spec states this should run w/ interrupts enabled */
	local_irq_enable();

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
			/* If we're being hit with CMC interrupts, we won't
			 * ever execute the schedule_work() below.  Need to
			 * disable CMC interrupts on this processor now.
			 */
			ia64_mca_cmc_vector_disable(NULL);
			schedule_work(&cmc_disable_work);

			/*
			 * Corrected errors will still be corrected, but
			 * make sure there's a log somewhere that indicates
			 * something is generating more than we can handle.
			 */
			printk(KERN_WARNING "WARNING: Switching to polling CMC handler; error records may be lost\n");

			mod_timer(&cmc_poll_timer, jiffies + CMC_POLL_INTERVAL);

			/* lock already released, get out now */
			goto out;
		} else {
			cmc_history[index++] = now;
			if (index == CMC_HISTORY_LENGTH)
				index = 0;
		}
	}
	spin_unlock(&cmc_history_lock);
out:
	/* Get the CMC error record and log it */
	ia64_mca_log_sal_error_record(SAL_INFO_TYPE_CMC);

	local_irq_disable();

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
 * Outputs
 * 	handled
 */
static irqreturn_t
ia64_mca_cmc_int_caller(int cmc_irq, void *arg)
{
	static int start_count = -1;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CMC);

	ia64_mca_cmc_int_handler(cmc_irq, arg);

	cpuid = cpumask_next(cpuid+1, cpu_online_mask);

	if (cpuid < nr_cpu_ids) {
		ia64_send_ipi(cpuid, IA64_CMCP_VECTOR, IA64_IPI_DM_INT, 0);
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
ia64_mca_cmc_poll (struct timer_list *unused)
{
	/* Trigger a CMC interrupt cascade  */
	ia64_send_ipi(cpumask_first(cpu_online_mask), IA64_CMCP_VECTOR,
							IA64_IPI_DM_INT, 0);
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
 * Outputs
 * 	handled
 */
static irqreturn_t
ia64_mca_cpe_int_caller(int cpe_irq, void *arg)
{
	static int start_count = -1;
	static int poll_time = MIN_CPE_POLL_INTERVAL;
	unsigned int cpuid;

	cpuid = smp_processor_id();

	/* If first cpu, update count */
	if (start_count == -1)
		start_count = IA64_LOG_COUNT(SAL_INFO_TYPE_CPE);

	ia64_mca_cpe_int_handler(cpe_irq, arg);

	cpuid = cpumask_next(cpuid+1, cpu_online_mask);

	if (cpuid < NR_CPUS) {
		ia64_send_ipi(cpuid, IA64_CPEP_VECTOR, IA64_IPI_DM_INT, 0);
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
ia64_mca_cpe_poll (struct timer_list *unused)
{
	/* Trigger a CPE interrupt cascade  */
	ia64_send_ipi(cpumask_first(cpu_online_mask), IA64_CPEP_VECTOR,
							IA64_IPI_DM_INT, 0);
}

static int
default_monarch_init_process(struct notifier_block *self, unsigned long val, void *data)
{
	int c;
	struct task_struct *g, *t;
	if (val != DIE_INIT_MONARCH_PROCESS)
		return NOTIFY_DONE;
#ifdef CONFIG_KEXEC
	if (atomic_read(&kdump_in_progress))
		return NOTIFY_DONE;
#endif

	/*
	 * FIXME: mlogbuf will brim over with INIT stack dumps.
	 * To enable show_stack from INIT, we use oops_in_progress which should
	 * be used in real oops. This would cause something wrong after INIT.
	 */
	BREAK_LOGLEVEL(console_loglevel);
	ia64_mlogbuf_dump_from_init();

	printk(KERN_ERR "Processes interrupted by INIT -");
	for_each_online_cpu(c) {
		struct ia64_sal_os_state *s;
		t = __va(__per_cpu_mca[c] + IA64_MCA_CPU_INIT_STACK_OFFSET);
		s = (struct ia64_sal_os_state *)((char *)t + MCA_SOS_OFFSET);
		g = s->prev_task;
		if (g) {
			if (g->pid)
				printk(" %d", g->pid);
			else
				printk(" %d (cpu %d task 0x%p)", g->pid, task_cpu(g), g);
		}
	}
	printk("\n\n");
	if (read_trylock(&tasklist_lock)) {
		for_each_process_thread(g, t) {
			printk("\nBacktrace of pid %d (%s)\n", t->pid, t->comm);
			show_stack(t, NULL, KERN_DEFAULT);
		}
		read_unlock(&tasklist_lock);
	}
	/* FIXME: This will not restore zapped printk locks. */
	RESTORE_LOGLEVEL(console_loglevel);
	return NOTIFY_DONE;
}

/*
 * C portion of the OS INIT handler
 *
 * Called from ia64_os_init_dispatch
 *
 * Inputs: pointer to pt_regs where processor info was saved.  SAL/OS state for
 * this event.  This code is used for both monarch and slave INIT events, see
 * sos->monarch.
 *
 * All INIT events switch to the INIT stack and change the previous process to
 * blocked status.  If one of the INIT events is the monarch then we are
 * probably processing the nmi button/command.  Use the monarch cpu to dump all
 * the processes.  The slave INIT events all spin until the monarch cpu
 * returns.  We can also get INIT slave events for MCA, in which case the MCA
 * process is the monarch.
 */

void
ia64_init_handler(struct pt_regs *regs, struct switch_stack *sw,
		  struct ia64_sal_os_state *sos)
{
	static atomic_t slaves;
	static atomic_t monarchs;
	struct task_struct *previous_current;
	int cpu = smp_processor_id();
	struct ia64_mca_notify_die nd =
		{ .sos = sos, .monarch_cpu = &monarch_cpu };

	NOTIFY_INIT(DIE_INIT_ENTER, regs, (long)&nd, 0);

	mprintk(KERN_INFO "Entered OS INIT handler. PSP=%lx cpu=%d monarch=%ld\n",
		sos->proc_state_param, cpu, sos->monarch);
	salinfo_log_wakeup(SAL_INFO_TYPE_INIT, NULL, 0, 0);

	previous_current = ia64_mca_modify_original_stack(regs, sw, sos, "INIT");
	sos->os_status = IA64_INIT_RESUME;

	/* FIXME: Workaround for broken proms that drive all INIT events as
	 * slaves.  The last slave that enters is promoted to be a monarch.
	 * Remove this code in September 2006, that gives platforms a year to
	 * fix their proms and get their customers updated.
	 */
	if (!sos->monarch && atomic_add_return(1, &slaves) == num_online_cpus()) {
		mprintk(KERN_WARNING "%s: Promoting cpu %d to monarch.\n",
		        __func__, cpu);
		atomic_dec(&slaves);
		sos->monarch = 1;
	}

	/* FIXME: Workaround for broken proms that drive all INIT events as
	 * monarchs.  Second and subsequent monarchs are demoted to slaves.
	 * Remove this code in September 2006, that gives platforms a year to
	 * fix their proms and get their customers updated.
	 */
	if (sos->monarch && atomic_add_return(1, &monarchs) > 1) {
		mprintk(KERN_WARNING "%s: Demoting cpu %d to slave.\n",
			       __func__, cpu);
		atomic_dec(&monarchs);
		sos->monarch = 0;
	}

	if (!sos->monarch) {
		ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_INIT;

#ifdef CONFIG_KEXEC
		while (monarch_cpu == -1 && !atomic_read(&kdump_in_progress))
			udelay(1000);
#else
		while (monarch_cpu == -1)
			cpu_relax();	/* spin until monarch enters */
#endif

		NOTIFY_INIT(DIE_INIT_SLAVE_ENTER, regs, (long)&nd, 1);
		NOTIFY_INIT(DIE_INIT_SLAVE_PROCESS, regs, (long)&nd, 1);

#ifdef CONFIG_KEXEC
		while (monarch_cpu != -1 && !atomic_read(&kdump_in_progress))
			udelay(1000);
#else
		while (monarch_cpu != -1)
			cpu_relax();	/* spin until monarch leaves */
#endif

		NOTIFY_INIT(DIE_INIT_SLAVE_LEAVE, regs, (long)&nd, 1);

		mprintk("Slave on cpu %d returning to normal service.\n", cpu);
		ia64_set_curr_task(cpu, previous_current);
		ia64_mc_info.imi_rendez_checkin[cpu] = IA64_MCA_RENDEZ_CHECKIN_NOTDONE;
		atomic_dec(&slaves);
		return;
	}

	monarch_cpu = cpu;
	NOTIFY_INIT(DIE_INIT_MONARCH_ENTER, regs, (long)&nd, 1);

	/*
	 * Wait for a bit.  On some machines (e.g., HP's zx2000 and zx6000, INIT can be
	 * generated via the BMC's command-line interface, but since the console is on the
	 * same serial line, the user will need some time to switch out of the BMC before
	 * the dump begins.
	 */
	mprintk("Delaying for 5 seconds...\n");
	udelay(5*1000000);
	ia64_wait_for_slaves(cpu, "INIT");
	/* If nobody intercepts DIE_INIT_MONARCH_PROCESS then we drop through
	 * to default_monarch_init_process() above and just print all the
	 * tasks.
	 */
	NOTIFY_INIT(DIE_INIT_MONARCH_PROCESS, regs, (long)&nd, 1);
	NOTIFY_INIT(DIE_INIT_MONARCH_LEAVE, regs, (long)&nd, 1);

	mprintk("\nINIT dump complete.  Monarch on cpu %d returning to normal service.\n", cpu);
	atomic_dec(&monarchs);
	ia64_set_curr_task(cpu, previous_current);
	monarch_cpu = -1;
	return;
}

static int __init
ia64_mca_disable_cpe_polling(char *str)
{
	cpe_poll_enabled = 0;
	return 1;
}

__setup("disable_cpe_poll", ia64_mca_disable_cpe_polling);

/* Minimal format of the MCA/INIT stacks.  The pseudo processes that run on
 * these stacks can never sleep, they cannot return from the kernel to user
 * space, they do not appear in a normal ps listing.  So there is no need to
 * format most of the fields.
 */

static void
format_mca_init_stack(void *mca_data, unsigned long offset,
		const char *type, int cpu)
{
	struct task_struct *p = (struct task_struct *)((char *)mca_data + offset);
	struct thread_info *ti;
	memset(p, 0, KERNEL_STACK_SIZE);
	ti = task_thread_info(p);
	ti->flags = _TIF_MCA_INIT;
	ti->preempt_count = 1;
	ti->task = p;
	ti->cpu = cpu;
	p->stack = ti;
	p->__state = TASK_UNINTERRUPTIBLE;
	cpumask_set_cpu(cpu, &p->cpus_mask);
	INIT_LIST_HEAD(&p->tasks);
	p->parent = p->real_parent = p->group_leader = p;
	INIT_LIST_HEAD(&p->children);
	INIT_LIST_HEAD(&p->sibling);
	strscpy(p->comm, type, sizeof(p->comm)-1);
}

/* Caller prevents this from being called after init */
static void * __ref mca_bootmem(void)
{
	return memblock_alloc(sizeof(struct ia64_mca_cpu), KERNEL_STACK_SIZE);
}

/* Do per-CPU MCA-related initialization.  */
void
ia64_mca_cpu_init(void *cpu_data)
{
	void *pal_vaddr;
	void *data;
	long sz = sizeof(struct ia64_mca_cpu);
	int cpu = smp_processor_id();
	static int first_time = 1;

	/*
	 * Structure will already be allocated if cpu has been online,
	 * then offlined.
	 */
	if (__per_cpu_mca[cpu]) {
		data = __va(__per_cpu_mca[cpu]);
	} else {
		if (first_time) {
			data = mca_bootmem();
			first_time = 0;
		} else
			data = (void *)__get_free_pages(GFP_ATOMIC,
							get_order(sz));
		if (!data)
			panic("Could not allocate MCA memory for cpu %d\n",
					cpu);
	}
	format_mca_init_stack(data, offsetof(struct ia64_mca_cpu, mca_stack),
		"MCA", cpu);
	format_mca_init_stack(data, offsetof(struct ia64_mca_cpu, init_stack),
		"INIT", cpu);
	__this_cpu_write(ia64_mca_data, (__per_cpu_mca[cpu] = __pa(data)));

	/*
	 * Stash away a copy of the PTE needed to map the per-CPU page.
	 * We may need it during MCA recovery.
	 */
	__this_cpu_write(ia64_mca_per_cpu_pte,
		pte_val(mk_pte_phys(__pa(cpu_data), PAGE_KERNEL)));

	/*
	 * Also, stash away a copy of the PAL address and the PTE
	 * needed to map it.
	 */
	pal_vaddr = efi_get_pal_addr();
	if (!pal_vaddr)
		return;
	__this_cpu_write(ia64_mca_pal_base,
		GRANULEROUNDDOWN((unsigned long) pal_vaddr));
	__this_cpu_write(ia64_mca_pal_pte, pte_val(mk_pte_phys(__pa(pal_vaddr),
							      PAGE_KERNEL)));
}

static int ia64_mca_cpu_online(unsigned int cpu)
{
	unsigned long flags;

	local_irq_save(flags);
	if (!cmc_polling_enabled)
		ia64_mca_cmc_vector_enable(NULL);
	local_irq_restore(flags);
	return 0;
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
	ia64_fptr_t *init_hldlr_ptr_monarch = (ia64_fptr_t *)ia64_os_init_dispatch_monarch;
	ia64_fptr_t *init_hldlr_ptr_slave = (ia64_fptr_t *)ia64_os_init_dispatch_slave;
	ia64_fptr_t *mca_hldlr_ptr = (ia64_fptr_t *)ia64_os_mca_dispatch;
	int i;
	long rc;
	struct ia64_sal_retval isrv;
	unsigned long timeout = IA64_MCA_RENDEZ_TIMEOUT; /* platform specific */
	static struct notifier_block default_init_monarch_nb = {
		.notifier_call = default_monarch_init_process,
		.priority = 0/* we need to notified last */
	};

	IA64_MCA_DEBUG("%s: begin\n", __func__);

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
			NOTIFY_MCA(DIE_MCA_NEW_TIMEOUT, NULL, timeout, 0);
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

	IA64_MCA_DEBUG("%s: registered MCA rendezvous spinloop and wakeup mech.\n", __func__);

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

	IA64_MCA_DEBUG("%s: registered OS MCA handler with SAL at 0x%lx, gp = 0x%lx\n", __func__,
		       ia64_mc_info.imi_mca_handler, ia64_tpa(mca_hldlr_ptr->gp));

	/*
	 * XXX - disable SAL checksum by setting size to 0, should be
	 * size of the actual init handler in mca_asm.S.
	 */
	ia64_mc_info.imi_monarch_init_handler		= ia64_tpa(init_hldlr_ptr_monarch->fp);
	ia64_mc_info.imi_monarch_init_handler_size	= 0;
	ia64_mc_info.imi_slave_init_handler		= ia64_tpa(init_hldlr_ptr_slave->fp);
	ia64_mc_info.imi_slave_init_handler_size	= 0;

	IA64_MCA_DEBUG("%s: OS INIT handler at %lx\n", __func__,
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
	if (register_die_notifier(&default_init_monarch_nb)) {
		printk(KERN_ERR "Failed to register default monarch INIT process\n");
		return;
	}

	IA64_MCA_DEBUG("%s: registered OS INIT handler with SAL\n", __func__);

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
 * These pieces cannot be done in ia64_mca_init() because it is called before
 * early_irq_init() which would wipe out our percpu irq registrations. But we
 * cannot leave them until ia64_mca_late_init() because by then all the other
 * processors have been brought online and have set their own CMC vectors to
 * point at a non-existant action. Called from arch_early_irq_init().
 */
void __init ia64_mca_irq_init(void)
{
	/*
	 *  Configure the CMCI/P vector and handler. Interrupts for CMC are
	 *  per-processor, so AP CMC interrupts are setup in smp_callin() (smpboot.c).
	 */
	register_percpu_irq(IA64_CMC_VECTOR, ia64_mca_cmc_int_handler, 0,
			    "cmc_hndlr");
	register_percpu_irq(IA64_CMCP_VECTOR, ia64_mca_cmc_int_caller, 0,
			    "cmc_poll");
	ia64_mca_cmc_vector_setup();       /* Setup vector on BSP */

	/* Setup the MCA rendezvous interrupt vector */
	register_percpu_irq(IA64_MCA_RENDEZ_VECTOR, ia64_mca_rendez_int_handler,
			    0, "mca_rdzv");

	/* Setup the MCA wakeup interrupt vector */
	register_percpu_irq(IA64_MCA_WAKEUP_VECTOR, ia64_mca_wakeup_int_handler,
			    0, "mca_wkup");

	/* Setup the CPEI/P handler */
	register_percpu_irq(IA64_CPEP_VECTOR, ia64_mca_cpe_int_caller, 0,
			    "cpe_poll");
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
	timer_setup(&cmc_poll_timer, ia64_mca_cmc_poll, 0);

	/* Unmask/enable the vector */
	cmc_polling_enabled = 0;
	cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "ia64/mca:online",
			  ia64_mca_cpu_online, NULL);
	IA64_MCA_DEBUG("%s: CMCI/P setup and enabled.\n", __func__);

	/* Setup the CPEI/P vector and handler */
	cpe_vector = acpi_request_vector(ACPI_INTERRUPT_CPEI);
	timer_setup(&cpe_poll_timer, ia64_mca_cpe_poll, 0);

	{
		unsigned int irq;

		if (cpe_vector >= 0) {
			/* If platform supports CPEI, enable the irq. */
			irq = local_vector_to_irq(cpe_vector);
			if (irq > 0) {
				cpe_poll_enabled = 0;
				irq_set_status_flags(irq, IRQ_PER_CPU);
				if (request_irq(irq, ia64_mca_cpe_int_handler,
						0, "cpe_hndlr", NULL))
					pr_err("Failed to register cpe_hndlr interrupt\n");
				ia64_cpe_irq = irq;
				ia64_mca_register_cpev(cpe_vector);
				IA64_MCA_DEBUG("%s: CPEI/P setup and enabled.\n",
					__func__);
				return 0;
			}
			printk(KERN_ERR "%s: Failed to find irq for CPE "
					"interrupt handler, vector %d\n",
					__func__, cpe_vector);
		}
		/* If platform doesn't support CPEI, get the timer going. */
		if (cpe_poll_enabled) {
			ia64_mca_cpe_poll(0UL);
			IA64_MCA_DEBUG("%s: CPEP setup and enabled.\n", __func__);
		}
	}

	return 0;
}

device_initcall(ia64_mca_late_init);
