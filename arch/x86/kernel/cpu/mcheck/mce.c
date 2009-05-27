/*
 * Machine check handler.
 *
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s).
 * 2004 Andi Kleen. Rewrote most of it.
 * Copyright 2008 Intel Corporation
 * Author: Andi Kleen
 */
#include <linux/thread_info.h>
#include <linux/capability.h>
#include <linux/miscdevice.h>
#include <linux/ratelimit.h>
#include <linux/kallsyms.h>
#include <linux/rcupdate.h>
#include <linux/kobject.h>
#include <linux/uaccess.h>
#include <linux/kdebug.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/string.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/poll.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/fs.h>

#include <asm/processor.h>
#include <asm/idle.h>
#include <asm/mce.h>
#include <asm/msr.h>

#include "mce.h"

/* Handle unconfigured int18 (should never happen) */
static void unexpected_machine_check(struct pt_regs *regs, long error_code)
{
	printk(KERN_ERR "CPU#%d: Unexpected int18 (Machine Check).\n",
	       smp_processor_id());
}

/* Call the installed machine check handler for this CPU setup. */
void (*machine_check_vector)(struct pt_regs *, long error_code) =
						unexpected_machine_check;

int				mce_disabled;

#ifdef CONFIG_X86_NEW_MCE

#define MISC_MCELOG_MINOR	227

atomic_t mce_entry;

DEFINE_PER_CPU(unsigned, mce_exception_count);

/*
 * Tolerant levels:
 *   0: always panic on uncorrected errors, log corrected errors
 *   1: panic or SIGBUS on uncorrected errors, log corrected errors
 *   2: SIGBUS or log uncorrected errors (if possible), log corrected errors
 *   3: never panic or SIGBUS, log all errors (for testing only)
 */
static int			tolerant = 1;
static int			banks;
static u64			*bank;
static unsigned long		notify_user;
static int			rip_msr;
static int			mce_bootlog = -1;

static char			trigger[128];
static char			*trigger_argv[2] = { trigger, NULL };

static unsigned long		dont_init_banks;

static DECLARE_WAIT_QUEUE_HEAD(mce_wait);

/* MCA banks polled by the period polling timer for corrected events */
DEFINE_PER_CPU(mce_banks_t, mce_poll_banks) = {
	[0 ... BITS_TO_LONGS(MAX_NR_BANKS)-1] = ~0UL
};

static inline int skip_bank_init(int i)
{
	return i < BITS_PER_LONG && test_bit(i, &dont_init_banks);
}

/* Do initial initialization of a struct mce */
void mce_setup(struct mce *m)
{
	memset(m, 0, sizeof(struct mce));
	m->cpu = m->extcpu = smp_processor_id();
	rdtscll(m->tsc);
}

DEFINE_PER_CPU(struct mce, injectm);
EXPORT_PER_CPU_SYMBOL_GPL(injectm);

/*
 * Lockless MCE logging infrastructure.
 * This avoids deadlocks on printk locks without having to break locks. Also
 * separate MCEs from kernel messages to avoid bogus bug reports.
 */

static struct mce_log mcelog = {
	.signature	= MCE_LOG_SIGNATURE,
	.len		= MCE_LOG_LEN,
	.recordlen	= sizeof(struct mce),
};

void mce_log(struct mce *mce)
{
	unsigned next, entry;

	mce->finished = 0;
	wmb();
	for (;;) {
		entry = rcu_dereference(mcelog.next);
		for (;;) {
			/*
			 * When the buffer fills up discard new entries.
			 * Assume that the earlier errors are the more
			 * interesting ones:
			 */
			if (entry >= MCE_LOG_LEN) {
				set_bit(MCE_OVERFLOW,
					(unsigned long *)&mcelog.flags);
				return;
			}
			/* Old left over entry. Skip: */
			if (mcelog.entry[entry].finished) {
				entry++;
				continue;
			}
			break;
		}
		smp_rmb();
		next = entry + 1;
		if (cmpxchg(&mcelog.next, entry, next) == entry)
			break;
	}
	memcpy(mcelog.entry + entry, mce, sizeof(struct mce));
	wmb();
	mcelog.entry[entry].finished = 1;
	wmb();

	set_bit(0, &notify_user);
}

static void print_mce(struct mce *m)
{
	printk(KERN_EMERG "\n"
	       KERN_EMERG "HARDWARE ERROR\n"
	       KERN_EMERG
	       "CPU %d: Machine Check Exception: %16Lx Bank %d: %016Lx\n",
	       m->extcpu, m->mcgstatus, m->bank, m->status);
	if (m->ip) {
		printk(KERN_EMERG "RIP%s %02x:<%016Lx> ",
		       !(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
		       m->cs, m->ip);
		if (m->cs == __KERNEL_CS)
			print_symbol("{%s}", m->ip);
		printk("\n");
	}
	printk(KERN_EMERG "TSC %llx ", m->tsc);
	if (m->addr)
		printk("ADDR %llx ", m->addr);
	if (m->misc)
		printk("MISC %llx ", m->misc);
	printk("\n");
	printk(KERN_EMERG "This is not a software problem!\n");
	printk(KERN_EMERG "Run through mcelog --ascii to decode "
	       "and contact your hardware vendor\n");
}

static void mce_panic(char *msg, struct mce *backup, u64 start)
{
	int i;

	bust_spinlocks(1);
	console_verbose();
	for (i = 0; i < MCE_LOG_LEN; i++) {
		u64 tsc = mcelog.entry[i].tsc;

		if ((s64)(tsc - start) < 0)
			continue;
		print_mce(&mcelog.entry[i]);
		if (backup && mcelog.entry[i].tsc == backup->tsc)
			backup = NULL;
	}
	if (backup)
		print_mce(backup);
	panic(msg);
}

/* Support code for software error injection */

static int msr_to_offset(u32 msr)
{
	unsigned bank = __get_cpu_var(injectm.bank);
	if (msr == rip_msr)
		return offsetof(struct mce, ip);
	if (msr == MSR_IA32_MC0_STATUS + bank*4)
		return offsetof(struct mce, status);
	if (msr == MSR_IA32_MC0_ADDR + bank*4)
		return offsetof(struct mce, addr);
	if (msr == MSR_IA32_MC0_MISC + bank*4)
		return offsetof(struct mce, misc);
	if (msr == MSR_IA32_MCG_STATUS)
		return offsetof(struct mce, mcgstatus);
	return -1;
}

/* MSR access wrappers used for error injection */
static u64 mce_rdmsrl(u32 msr)
{
	u64 v;
	if (__get_cpu_var(injectm).finished) {
		int offset = msr_to_offset(msr);
		if (offset < 0)
			return 0;
		return *(u64 *)((char *)&__get_cpu_var(injectm) + offset);
	}
	rdmsrl(msr, v);
	return v;
}

static void mce_wrmsrl(u32 msr, u64 v)
{
	if (__get_cpu_var(injectm).finished) {
		int offset = msr_to_offset(msr);
		if (offset >= 0)
			*(u64 *)((char *)&__get_cpu_var(injectm) + offset) = v;
		return;
	}
	wrmsrl(msr, v);
}

int mce_available(struct cpuinfo_x86 *c)
{
	if (mce_disabled)
		return 0;
	return cpu_has(c, X86_FEATURE_MCE) && cpu_has(c, X86_FEATURE_MCA);
}

static inline void mce_get_rip(struct mce *m, struct pt_regs *regs)
{
	if (regs && (m->mcgstatus & MCG_STATUS_RIPV)) {
		m->ip = regs->ip;
		m->cs = regs->cs;
	} else {
		m->ip = 0;
		m->cs = 0;
	}
	if (rip_msr) {
		/* Assume the RIP in the MSR is exact. Is this true? */
		m->mcgstatus |= MCG_STATUS_EIPV;
		m->ip = mce_rdmsrl(rip_msr);
		m->cs = 0;
	}
}

DEFINE_PER_CPU(unsigned, mce_poll_count);

/*
 * Poll for corrected events or events that happened before reset.
 * Those are just logged through /dev/mcelog.
 *
 * This is executed in standard interrupt context.
 */
void machine_check_poll(enum mcp_flags flags, mce_banks_t *b)
{
	struct mce m;
	int i;

	__get_cpu_var(mce_poll_count)++;

	mce_setup(&m);

	m.mcgstatus = mce_rdmsrl(MSR_IA32_MCG_STATUS);
	for (i = 0; i < banks; i++) {
		if (!bank[i] || !test_bit(i, *b))
			continue;

		m.misc = 0;
		m.addr = 0;
		m.bank = i;
		m.tsc = 0;

		barrier();
		m.status = mce_rdmsrl(MSR_IA32_MC0_STATUS + i*4);
		if (!(m.status & MCI_STATUS_VAL))
			continue;

		/*
		 * Uncorrected events are handled by the exception handler
		 * when it is enabled. But when the exception is disabled log
		 * everything.
		 *
		 * TBD do the same check for MCI_STATUS_EN here?
		 */
		if ((m.status & MCI_STATUS_UC) && !(flags & MCP_UC))
			continue;

		if (m.status & MCI_STATUS_MISCV)
			m.misc = mce_rdmsrl(MSR_IA32_MC0_MISC + i*4);
		if (m.status & MCI_STATUS_ADDRV)
			m.addr = mce_rdmsrl(MSR_IA32_MC0_ADDR + i*4);

		if (!(flags & MCP_TIMESTAMP))
			m.tsc = 0;
		/*
		 * Don't get the IP here because it's unlikely to
		 * have anything to do with the actual error location.
		 */
		if (!(flags & MCP_DONTLOG)) {
			mce_log(&m);
			add_taint(TAINT_MACHINE_CHECK);
		}

		/*
		 * Clear state for this bank.
		 */
		mce_wrmsrl(MSR_IA32_MC0_STATUS+4*i, 0);
	}

	/*
	 * Don't clear MCG_STATUS here because it's only defined for
	 * exceptions.
	 */

	sync_core();
}
EXPORT_SYMBOL_GPL(machine_check_poll);

/*
 * The actual machine check handler. This only handles real
 * exceptions when something got corrupted coming in through int 18.
 *
 * This is executed in NMI context not subject to normal locking rules. This
 * implies that most kernel services cannot be safely used. Don't even
 * think about putting a printk in there!
 */
void do_machine_check(struct pt_regs *regs, long error_code)
{
	struct mce m, panicm;
	int panicm_found = 0;
	u64 mcestart = 0;
	int i;
	/*
	 * If no_way_out gets set, there is no safe way to recover from this
	 * MCE.  If tolerant is cranked up, we'll try anyway.
	 */
	int no_way_out = 0;
	/*
	 * If kill_it gets set, there might be a way to recover from this
	 * error.
	 */
	int kill_it = 0;
	DECLARE_BITMAP(toclear, MAX_NR_BANKS);

	atomic_inc(&mce_entry);

	__get_cpu_var(mce_exception_count)++;

	if (notify_die(DIE_NMI, "machine check", regs, error_code,
			   18, SIGKILL) == NOTIFY_STOP)
		goto out;
	if (!banks)
		goto out;

	mce_setup(&m);

	m.mcgstatus = mce_rdmsrl(MSR_IA32_MCG_STATUS);

	/* if the restart IP is not valid, we're done for */
	if (!(m.mcgstatus & MCG_STATUS_RIPV))
		no_way_out = 1;

	rdtscll(mcestart);
	barrier();

	for (i = 0; i < banks; i++) {
		__clear_bit(i, toclear);
		if (!bank[i])
			continue;

		m.misc = 0;
		m.addr = 0;
		m.bank = i;

		m.status = mce_rdmsrl(MSR_IA32_MC0_STATUS + i*4);
		if ((m.status & MCI_STATUS_VAL) == 0)
			continue;

		/*
		 * Non uncorrected errors are handled by machine_check_poll
		 * Leave them alone.
		 */
		if ((m.status & MCI_STATUS_UC) == 0)
			continue;

		/*
		 * Set taint even when machine check was not enabled.
		 */
		add_taint(TAINT_MACHINE_CHECK);

		__set_bit(i, toclear);

		if (m.status & MCI_STATUS_EN) {
			/* if PCC was set, there's no way out */
			no_way_out |= !!(m.status & MCI_STATUS_PCC);
			/*
			 * If this error was uncorrectable and there was
			 * an overflow, we're in trouble.  If no overflow,
			 * we might get away with just killing a task.
			 */
			if (m.status & MCI_STATUS_UC) {
				if (tolerant < 1 || m.status & MCI_STATUS_OVER)
					no_way_out = 1;
				kill_it = 1;
			}
		} else {
			/*
			 * Machine check event was not enabled. Clear, but
			 * ignore.
			 */
			continue;
		}

		if (m.status & MCI_STATUS_MISCV)
			m.misc = mce_rdmsrl(MSR_IA32_MC0_MISC + i*4);
		if (m.status & MCI_STATUS_ADDRV)
			m.addr = mce_rdmsrl(MSR_IA32_MC0_ADDR + i*4);

		mce_get_rip(&m, regs);
		mce_log(&m);

		/*
		 * Did this bank cause the exception?
		 *
		 * Assume that the bank with uncorrectable errors did it,
		 * and that there is only a single one:
		 */
		if ((m.status & MCI_STATUS_UC) &&
					(m.status & MCI_STATUS_EN)) {
			panicm = m;
			panicm_found = 1;
		}
	}

	/*
	 * If we didn't find an uncorrectable error, pick
	 * the last one (shouldn't happen, just being safe).
	 */
	if (!panicm_found)
		panicm = m;

	/*
	 * If we have decided that we just CAN'T continue, and the user
	 * has not set tolerant to an insane level, give up and die.
	 */
	if (no_way_out && tolerant < 3)
		mce_panic("Machine check", &panicm, mcestart);

	/*
	 * If the error seems to be unrecoverable, something should be
	 * done.  Try to kill as little as possible.  If we can kill just
	 * one task, do that.  If the user has set the tolerance very
	 * high, don't try to do anything at all.
	 */
	if (kill_it && tolerant < 3) {
		int user_space = 0;

		/*
		 * If the EIPV bit is set, it means the saved IP is the
		 * instruction which caused the MCE.
		 */
		if (m.mcgstatus & MCG_STATUS_EIPV)
			user_space = panicm.ip && (panicm.cs & 3);

		/*
		 * If we know that the error was in user space, send a
		 * SIGBUS.  Otherwise, panic if tolerance is low.
		 *
		 * force_sig() takes an awful lot of locks and has a slight
		 * risk of deadlocking.
		 */
		if (user_space) {
			force_sig(SIGBUS, current);
		} else if (panic_on_oops || tolerant < 2) {
			mce_panic("Uncorrected machine check",
				&panicm, mcestart);
		}
	}

	/* notify userspace ASAP */
	set_thread_flag(TIF_MCE_NOTIFY);

	/* the last thing we do is clear state */
	for (i = 0; i < banks; i++) {
		if (test_bit(i, toclear))
			mce_wrmsrl(MSR_IA32_MC0_STATUS+4*i, 0);
	}
	mce_wrmsrl(MSR_IA32_MCG_STATUS, 0);
out:
	atomic_dec(&mce_entry);
	sync_core();
}
EXPORT_SYMBOL_GPL(do_machine_check);

#ifdef CONFIG_X86_MCE_INTEL
/***
 * mce_log_therm_throt_event - Logs the thermal throttling event to mcelog
 * @cpu: The CPU on which the event occurred.
 * @status: Event status information
 *
 * This function should be called by the thermal interrupt after the
 * event has been processed and the decision was made to log the event
 * further.
 *
 * The status parameter will be saved to the 'status' field of 'struct mce'
 * and historically has been the register value of the
 * MSR_IA32_THERMAL_STATUS (Intel) msr.
 */
void mce_log_therm_throt_event(__u64 status)
{
	struct mce m;

	mce_setup(&m);
	m.bank = MCE_THERMAL_BANK;
	m.status = status;
	mce_log(&m);
}
#endif /* CONFIG_X86_MCE_INTEL */

/*
 * Periodic polling timer for "silent" machine check errors.  If the
 * poller finds an MCE, poll 2x faster.  When the poller finds no more
 * errors, poll 2x slower (up to check_interval seconds).
 */
static int check_interval = 5 * 60; /* 5 minutes */

static DEFINE_PER_CPU(int, next_interval); /* in jiffies */
static DEFINE_PER_CPU(struct timer_list, mce_timer);

static void mcheck_timer(unsigned long data)
{
	struct timer_list *t = &per_cpu(mce_timer, data);
	int *n;

	WARN_ON(smp_processor_id() != data);

	if (mce_available(&current_cpu_data)) {
		machine_check_poll(MCP_TIMESTAMP,
				&__get_cpu_var(mce_poll_banks));
	}

	/*
	 * Alert userspace if needed.  If we logged an MCE, reduce the
	 * polling interval, otherwise increase the polling interval.
	 */
	n = &__get_cpu_var(next_interval);
	if (mce_notify_user())
		*n = max(*n/2, HZ/100);
	else
		*n = min(*n*2, (int)round_jiffies_relative(check_interval*HZ));

	t->expires = jiffies + *n;
	add_timer(t);
}

static void mce_do_trigger(struct work_struct *work)
{
	call_usermodehelper(trigger, trigger_argv, NULL, UMH_NO_WAIT);
}

static DECLARE_WORK(mce_trigger_work, mce_do_trigger);

/*
 * Notify the user(s) about new machine check events.
 * Can be called from interrupt context, but not from machine check/NMI
 * context.
 */
int mce_notify_user(void)
{
	/* Not more than two messages every minute */
	static DEFINE_RATELIMIT_STATE(ratelimit, 60*HZ, 2);

	clear_thread_flag(TIF_MCE_NOTIFY);

	if (test_and_clear_bit(0, &notify_user)) {
		wake_up_interruptible(&mce_wait);

		/*
		 * There is no risk of missing notifications because
		 * work_pending is always cleared before the function is
		 * executed.
		 */
		if (trigger[0] && !work_pending(&mce_trigger_work))
			schedule_work(&mce_trigger_work);

		if (__ratelimit(&ratelimit))
			printk(KERN_INFO "Machine check events logged\n");

		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mce_notify_user);

/*
 * Initialize Machine Checks for a CPU.
 */
static int mce_cap_init(void)
{
	unsigned b;
	u64 cap;

	rdmsrl(MSR_IA32_MCG_CAP, cap);

	b = cap & MCG_BANKCNT_MASK;
	printk(KERN_INFO "mce: CPU supports %d MCE banks\n", b);

	if (b > MAX_NR_BANKS) {
		printk(KERN_WARNING
		       "MCE: Using only %u machine check banks out of %u\n",
			MAX_NR_BANKS, b);
		b = MAX_NR_BANKS;
	}

	/* Don't support asymmetric configurations today */
	WARN_ON(banks != 0 && b != banks);
	banks = b;
	if (!bank) {
		bank = kmalloc(banks * sizeof(u64), GFP_KERNEL);
		if (!bank)
			return -ENOMEM;
		memset(bank, 0xff, banks * sizeof(u64));
	}

	/* Use accurate RIP reporting if available. */
	if ((cap & MCG_EXT_P) && MCG_EXT_CNT(cap) >= 9)
		rip_msr = MSR_IA32_MCG_EIP;

	return 0;
}

static void mce_init(void)
{
	mce_banks_t all_banks;
	u64 cap;
	int i;

	/*
	 * Log the machine checks left over from the previous reset.
	 */
	bitmap_fill(all_banks, MAX_NR_BANKS);
	machine_check_poll(MCP_UC|(!mce_bootlog ? MCP_DONTLOG : 0), &all_banks);

	set_in_cr4(X86_CR4_MCE);

	rdmsrl(MSR_IA32_MCG_CAP, cap);
	if (cap & MCG_CTL_P)
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);

	for (i = 0; i < banks; i++) {
		if (skip_bank_init(i))
			continue;
		wrmsrl(MSR_IA32_MC0_CTL+4*i, bank[i]);
		wrmsrl(MSR_IA32_MC0_STATUS+4*i, 0);
	}
}

/* Add per CPU specific workarounds here */
static void mce_cpu_quirks(struct cpuinfo_x86 *c)
{
	/* This should be disabled by the BIOS, but isn't always */
	if (c->x86_vendor == X86_VENDOR_AMD) {
		if (c->x86 == 15 && banks > 4) {
			/*
			 * disable GART TBL walk error reporting, which
			 * trips off incorrectly with the IOMMU & 3ware
			 * & Cerberus:
			 */
			clear_bit(10, (unsigned long *)&bank[4]);
		}
		if (c->x86 <= 17 && mce_bootlog < 0) {
			/*
			 * Lots of broken BIOS around that don't clear them
			 * by default and leave crap in there. Don't log:
			 */
			mce_bootlog = 0;
		}
		/*
		 * Various K7s with broken bank 0 around. Always disable
		 * by default.
		 */
		 if (c->x86 == 6)
			bank[0] = 0;
	}

	if (c->x86_vendor == X86_VENDOR_INTEL) {
		/*
		 * SDM documents that on family 6 bank 0 should not be written
		 * because it aliases to another special BIOS controlled
		 * register.
		 * But it's not aliased anymore on model 0x1a+
		 * Don't ignore bank 0 completely because there could be a
		 * valid event later, merely don't write CTL0.
		 */

		if (c->x86 == 6 && c->x86_model < 0x1A)
			__set_bit(0, &dont_init_banks);
	}
}

static void __cpuinit mce_ancient_init(struct cpuinfo_x86 *c)
{
	if (c->x86 != 5)
		return;
	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		if (mce_p5_enabled())
			intel_p5_mcheck_init(c);
		break;
	case X86_VENDOR_CENTAUR:
		winchip_mcheck_init(c);
		break;
	}
}

static void mce_cpu_features(struct cpuinfo_x86 *c)
{
	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		mce_intel_feature_init(c);
		break;
	case X86_VENDOR_AMD:
		mce_amd_feature_init(c);
		break;
	default:
		break;
	}
}

static void mce_init_timer(void)
{
	struct timer_list *t = &__get_cpu_var(mce_timer);
	int *n = &__get_cpu_var(next_interval);

	*n = check_interval * HZ;
	if (!*n)
		return;
	setup_timer(t, mcheck_timer, smp_processor_id());
	t->expires = round_jiffies(jiffies + *n);
	add_timer(t);
}

/*
 * Called for each booted CPU to set up machine checks.
 * Must be called with preempt off:
 */
void __cpuinit mcheck_init(struct cpuinfo_x86 *c)
{
	if (mce_disabled)
		return;

	mce_ancient_init(c);

	if (!mce_available(c))
		return;

	if (mce_cap_init() < 0) {
		mce_disabled = 1;
		return;
	}
	mce_cpu_quirks(c);

	machine_check_vector = do_machine_check;

	mce_init();
	mce_cpu_features(c);
	mce_init_timer();
}

/*
 * Character device to read and clear the MCE log.
 */

static DEFINE_SPINLOCK(mce_state_lock);
static int		open_count;		/* #times opened */
static int		open_exclu;		/* already open exclusive? */

static int mce_open(struct inode *inode, struct file *file)
{
	spin_lock(&mce_state_lock);

	if (open_exclu || (open_count && (file->f_flags & O_EXCL))) {
		spin_unlock(&mce_state_lock);

		return -EBUSY;
	}

	if (file->f_flags & O_EXCL)
		open_exclu = 1;
	open_count++;

	spin_unlock(&mce_state_lock);

	return nonseekable_open(inode, file);
}

static int mce_release(struct inode *inode, struct file *file)
{
	spin_lock(&mce_state_lock);

	open_count--;
	open_exclu = 0;

	spin_unlock(&mce_state_lock);

	return 0;
}

static void collect_tscs(void *data)
{
	unsigned long *cpu_tsc = (unsigned long *)data;

	rdtscll(cpu_tsc[smp_processor_id()]);
}

static DEFINE_MUTEX(mce_read_mutex);

static ssize_t mce_read(struct file *filp, char __user *ubuf, size_t usize,
			loff_t *off)
{
	char __user *buf = ubuf;
	unsigned long *cpu_tsc;
	unsigned prev, next;
	int i, err;

	cpu_tsc = kmalloc(nr_cpu_ids * sizeof(long), GFP_KERNEL);
	if (!cpu_tsc)
		return -ENOMEM;

	mutex_lock(&mce_read_mutex);
	next = rcu_dereference(mcelog.next);

	/* Only supports full reads right now */
	if (*off != 0 || usize < MCE_LOG_LEN*sizeof(struct mce)) {
		mutex_unlock(&mce_read_mutex);
		kfree(cpu_tsc);

		return -EINVAL;
	}

	err = 0;
	prev = 0;
	do {
		for (i = prev; i < next; i++) {
			unsigned long start = jiffies;

			while (!mcelog.entry[i].finished) {
				if (time_after_eq(jiffies, start + 2)) {
					memset(mcelog.entry + i, 0,
					       sizeof(struct mce));
					goto timeout;
				}
				cpu_relax();
			}
			smp_rmb();
			err |= copy_to_user(buf, mcelog.entry + i,
					    sizeof(struct mce));
			buf += sizeof(struct mce);
timeout:
			;
		}

		memset(mcelog.entry + prev, 0,
		       (next - prev) * sizeof(struct mce));
		prev = next;
		next = cmpxchg(&mcelog.next, prev, 0);
	} while (next != prev);

	synchronize_sched();

	/*
	 * Collect entries that were still getting written before the
	 * synchronize.
	 */
	on_each_cpu(collect_tscs, cpu_tsc, 1);

	for (i = next; i < MCE_LOG_LEN; i++) {
		if (mcelog.entry[i].finished &&
		    mcelog.entry[i].tsc < cpu_tsc[mcelog.entry[i].cpu]) {
			err |= copy_to_user(buf, mcelog.entry+i,
					    sizeof(struct mce));
			smp_rmb();
			buf += sizeof(struct mce);
			memset(&mcelog.entry[i], 0, sizeof(struct mce));
		}
	}
	mutex_unlock(&mce_read_mutex);
	kfree(cpu_tsc);

	return err ? -EFAULT : buf - ubuf;
}

static unsigned int mce_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &mce_wait, wait);
	if (rcu_dereference(mcelog.next))
		return POLLIN | POLLRDNORM;
	return 0;
}

static long mce_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	int __user *p = (int __user *)arg;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case MCE_GET_RECORD_LEN:
		return put_user(sizeof(struct mce), p);
	case MCE_GET_LOG_LEN:
		return put_user(MCE_LOG_LEN, p);
	case MCE_GETCLEAR_FLAGS: {
		unsigned flags;

		do {
			flags = mcelog.flags;
		} while (cmpxchg(&mcelog.flags, flags, 0) != flags);

		return put_user(flags, p);
	}
	default:
		return -ENOTTY;
	}
}

/* Modified in mce-inject.c, so not static or const */
struct file_operations mce_chrdev_ops = {
	.open			= mce_open,
	.release		= mce_release,
	.read			= mce_read,
	.poll			= mce_poll,
	.unlocked_ioctl		= mce_ioctl,
};
EXPORT_SYMBOL_GPL(mce_chrdev_ops);

static struct miscdevice mce_log_device = {
	MISC_MCELOG_MINOR,
	"mcelog",
	&mce_chrdev_ops,
};

/*
 * mce=off disables machine check
 * mce=TOLERANCELEVEL (number, see above)
 * mce=bootlog Log MCEs from before booting. Disabled by default on AMD.
 * mce=nobootlog Don't log MCEs from before booting.
 */
static int __init mcheck_enable(char *str)
{
	if (*str == 0)
		enable_p5_mce();
	if (*str == '=')
		str++;
	if (!strcmp(str, "off"))
		mce_disabled = 1;
	else if (!strcmp(str, "bootlog") || !strcmp(str, "nobootlog"))
		mce_bootlog = (str[0] == 'b');
	else if (isdigit(str[0]))
		get_option(&str, &tolerant);
	else {
		printk(KERN_INFO "mce argument %s ignored. Please use /sys\n",
		       str);
		return 0;
	}
	return 1;
}
__setup("mce", mcheck_enable);

/*
 * Sysfs support
 */

/*
 * Disable machine checks on suspend and shutdown. We can't really handle
 * them later.
 */
static int mce_disable(void)
{
	int i;

	for (i = 0; i < banks; i++) {
		if (!skip_bank_init(i))
			wrmsrl(MSR_IA32_MC0_CTL + i*4, 0);
	}
	return 0;
}

static int mce_suspend(struct sys_device *dev, pm_message_t state)
{
	return mce_disable();
}

static int mce_shutdown(struct sys_device *dev)
{
	return mce_disable();
}

/*
 * On resume clear all MCE state. Don't want to see leftovers from the BIOS.
 * Only one CPU is active at this time, the others get re-added later using
 * CPU hotplug:
 */
static int mce_resume(struct sys_device *dev)
{
	mce_init();
	mce_cpu_features(&current_cpu_data);

	return 0;
}

static void mce_cpu_restart(void *data)
{
	del_timer_sync(&__get_cpu_var(mce_timer));
	if (mce_available(&current_cpu_data))
		mce_init();
	mce_init_timer();
}

/* Reinit MCEs after user configuration changes */
static void mce_restart(void)
{
	on_each_cpu(mce_cpu_restart, NULL, 1);
}

static struct sysdev_class mce_sysclass = {
	.suspend	= mce_suspend,
	.shutdown	= mce_shutdown,
	.resume		= mce_resume,
	.name		= "machinecheck",
};

DEFINE_PER_CPU(struct sys_device, mce_dev);

__cpuinitdata
void (*threshold_cpu_callback)(unsigned long action, unsigned int cpu);

static struct sysdev_attribute *bank_attrs;

static ssize_t show_bank(struct sys_device *s, struct sysdev_attribute *attr,
			 char *buf)
{
	u64 b = bank[attr - bank_attrs];

	return sprintf(buf, "%llx\n", b);
}

static ssize_t set_bank(struct sys_device *s, struct sysdev_attribute *attr,
			const char *buf, size_t size)
{
	u64 new;

	if (strict_strtoull(buf, 0, &new) < 0)
		return -EINVAL;

	bank[attr - bank_attrs] = new;
	mce_restart();

	return size;
}

static ssize_t
show_trigger(struct sys_device *s, struct sysdev_attribute *attr, char *buf)
{
	strcpy(buf, trigger);
	strcat(buf, "\n");
	return strlen(trigger) + 1;
}

static ssize_t set_trigger(struct sys_device *s, struct sysdev_attribute *attr,
				const char *buf, size_t siz)
{
	char *p;
	int len;

	strncpy(trigger, buf, sizeof(trigger));
	trigger[sizeof(trigger)-1] = 0;
	len = strlen(trigger);
	p = strchr(trigger, '\n');

	if (*p)
		*p = 0;

	return len;
}

static ssize_t store_int_with_restart(struct sys_device *s,
				      struct sysdev_attribute *attr,
				      const char *buf, size_t size)
{
	ssize_t ret = sysdev_store_int(s, attr, buf, size);
	mce_restart();
	return ret;
}

static SYSDEV_ATTR(trigger, 0644, show_trigger, set_trigger);
static SYSDEV_INT_ATTR(tolerant, 0644, tolerant);

static struct sysdev_ext_attribute attr_check_interval = {
	_SYSDEV_ATTR(check_interval, 0644, sysdev_show_int,
		     store_int_with_restart),
	&check_interval
};

static struct sysdev_attribute *mce_attrs[] = {
	&attr_tolerant.attr, &attr_check_interval.attr, &attr_trigger,
	NULL
};

static cpumask_var_t mce_dev_initialized;

/* Per cpu sysdev init. All of the cpus still share the same ctrl bank: */
static __cpuinit int mce_create_device(unsigned int cpu)
{
	int err;
	int i;

	if (!mce_available(&boot_cpu_data))
		return -EIO;

	memset(&per_cpu(mce_dev, cpu).kobj, 0, sizeof(struct kobject));
	per_cpu(mce_dev, cpu).id	= cpu;
	per_cpu(mce_dev, cpu).cls	= &mce_sysclass;

	err = sysdev_register(&per_cpu(mce_dev, cpu));
	if (err)
		return err;

	for (i = 0; mce_attrs[i]; i++) {
		err = sysdev_create_file(&per_cpu(mce_dev, cpu), mce_attrs[i]);
		if (err)
			goto error;
	}
	for (i = 0; i < banks; i++) {
		err = sysdev_create_file(&per_cpu(mce_dev, cpu),
					&bank_attrs[i]);
		if (err)
			goto error2;
	}
	cpumask_set_cpu(cpu, mce_dev_initialized);

	return 0;
error2:
	while (--i >= 0)
		sysdev_remove_file(&per_cpu(mce_dev, cpu), &bank_attrs[i]);
error:
	while (--i >= 0)
		sysdev_remove_file(&per_cpu(mce_dev, cpu), mce_attrs[i]);

	sysdev_unregister(&per_cpu(mce_dev, cpu));

	return err;
}

static __cpuinit void mce_remove_device(unsigned int cpu)
{
	int i;

	if (!cpumask_test_cpu(cpu, mce_dev_initialized))
		return;

	for (i = 0; mce_attrs[i]; i++)
		sysdev_remove_file(&per_cpu(mce_dev, cpu), mce_attrs[i]);

	for (i = 0; i < banks; i++)
		sysdev_remove_file(&per_cpu(mce_dev, cpu), &bank_attrs[i]);

	sysdev_unregister(&per_cpu(mce_dev, cpu));
	cpumask_clear_cpu(cpu, mce_dev_initialized);
}

/* Make sure there are no machine checks on offlined CPUs. */
static void mce_disable_cpu(void *h)
{
	unsigned long action = *(unsigned long *)h;
	int i;

	if (!mce_available(&current_cpu_data))
		return;
	if (!(action & CPU_TASKS_FROZEN))
		cmci_clear();
	for (i = 0; i < banks; i++) {
		if (!skip_bank_init(i))
			wrmsrl(MSR_IA32_MC0_CTL + i*4, 0);
	}
}

static void mce_reenable_cpu(void *h)
{
	unsigned long action = *(unsigned long *)h;
	int i;

	if (!mce_available(&current_cpu_data))
		return;

	if (!(action & CPU_TASKS_FROZEN))
		cmci_reenable();
	for (i = 0; i < banks; i++) {
		if (!skip_bank_init(i))
			wrmsrl(MSR_IA32_MC0_CTL + i*4, bank[i]);
	}
}

/* Get notified when a cpu comes on/off. Be hotplug friendly. */
static int __cpuinit
mce_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct timer_list *t = &per_cpu(mce_timer, cpu);

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		mce_create_device(cpu);
		if (threshold_cpu_callback)
			threshold_cpu_callback(action, cpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		if (threshold_cpu_callback)
			threshold_cpu_callback(action, cpu);
		mce_remove_device(cpu);
		break;
	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		del_timer_sync(t);
		smp_call_function_single(cpu, mce_disable_cpu, &action, 1);
		break;
	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		t->expires = round_jiffies(jiffies +
						__get_cpu_var(next_interval));
		add_timer_on(t, cpu);
		smp_call_function_single(cpu, mce_reenable_cpu, &action, 1);
		break;
	case CPU_POST_DEAD:
		/* intentionally ignoring frozen here */
		cmci_rediscover(cpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block mce_cpu_notifier __cpuinitdata = {
	.notifier_call = mce_cpu_callback,
};

static __init int mce_init_banks(void)
{
	int i;

	bank_attrs = kzalloc(sizeof(struct sysdev_attribute) * banks,
				GFP_KERNEL);
	if (!bank_attrs)
		return -ENOMEM;

	for (i = 0; i < banks; i++) {
		struct sysdev_attribute *a = &bank_attrs[i];

		a->attr.name	= kasprintf(GFP_KERNEL, "bank%d", i);
		if (!a->attr.name)
			goto nomem;

		a->attr.mode	= 0644;
		a->show		= show_bank;
		a->store	= set_bank;
	}
	return 0;

nomem:
	while (--i >= 0)
		kfree(bank_attrs[i].attr.name);
	kfree(bank_attrs);
	bank_attrs = NULL;

	return -ENOMEM;
}

static __init int mce_init_device(void)
{
	int err;
	int i = 0;

	if (!mce_available(&boot_cpu_data))
		return -EIO;

	alloc_cpumask_var(&mce_dev_initialized, GFP_KERNEL);

	err = mce_init_banks();
	if (err)
		return err;

	err = sysdev_class_register(&mce_sysclass);
	if (err)
		return err;

	for_each_online_cpu(i) {
		err = mce_create_device(i);
		if (err)
			return err;
	}

	register_hotcpu_notifier(&mce_cpu_notifier);
	misc_register(&mce_log_device);

	return err;
}

device_initcall(mce_init_device);

#else /* CONFIG_X86_OLD_MCE: */

int nr_mce_banks;
EXPORT_SYMBOL_GPL(nr_mce_banks);	/* non-fatal.o */

/* This has to be run for each processor */
void mcheck_init(struct cpuinfo_x86 *c)
{
	if (mce_disabled == 1)
		return;

	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		amd_mcheck_init(c);
		break;

	case X86_VENDOR_INTEL:
		if (c->x86 == 5)
			intel_p5_mcheck_init(c);
		if (c->x86 == 6)
			intel_p6_mcheck_init(c);
		if (c->x86 == 15)
			intel_p4_mcheck_init(c);
		break;

	case X86_VENDOR_CENTAUR:
		if (c->x86 == 5)
			winchip_mcheck_init(c);
		break;

	default:
		break;
	}
	printk(KERN_INFO "mce: CPU supports %d MCE banks\n", nr_mce_banks);
}

static int __init mcheck_enable(char *str)
{
	mce_disabled = -1;
	return 1;
}

__setup("mce", mcheck_enable);

#endif /* CONFIG_X86_OLD_MCE */

/*
 * Old style boot options parsing. Only for compatibility.
 */
static int __init mcheck_disable(char *str)
{
	mce_disabled = 1;
	return 1;
}
__setup("nomce", mcheck_disable);
