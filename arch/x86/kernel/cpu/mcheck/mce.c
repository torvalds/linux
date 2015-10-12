/*
 * Machine check handler.
 *
 * K8 parts Copyright 2002,2003 Andi Kleen, SuSE Labs.
 * Rest from unknown author(s).
 * 2004 Andi Kleen. Rewrote most of it.
 * Copyright 2008 Intel Corporation
 * Author: Andi Kleen
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/device.h>
#include <linux/syscore_ops.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kmod.h>
#include <linux/poll.h>
#include <linux/nmi.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <linux/irq_work.h>
#include <linux/export.h>

#include <asm/processor.h>
#include <asm/traps.h>
#include <asm/tlbflush.h>
#include <asm/mce.h>
#include <asm/msr.h>

#include "mce-internal.h"

static DEFINE_MUTEX(mce_chrdev_read_mutex);

#define mce_log_get_idx_check(p) \
({ \
	RCU_LOCKDEP_WARN(!rcu_read_lock_sched_held() && \
			 !lockdep_is_held(&mce_chrdev_read_mutex), \
			 "suspicious mce_log_get_idx_check() usage"); \
	smp_load_acquire(&(p)); \
})

#define CREATE_TRACE_POINTS
#include <trace/events/mce.h>

#define SPINUNIT		100	/* 100ns */

DEFINE_PER_CPU(unsigned, mce_exception_count);

struct mce_bank *mce_banks __read_mostly;
struct mce_vendor_flags mce_flags __read_mostly;

struct mca_config mca_cfg __read_mostly = {
	.bootlog  = -1,
	/*
	 * Tolerant levels:
	 * 0: always panic on uncorrected errors, log corrected errors
	 * 1: panic or SIGBUS on uncorrected errors, log corrected errors
	 * 2: SIGBUS or log uncorrected errors (if possible), log corr. errors
	 * 3: never panic or SIGBUS, log all errors (for testing only)
	 */
	.tolerant = 1,
	.monarch_timeout = -1
};

/* User mode helper program triggered by machine check event */
static unsigned long		mce_need_notify;
static char			mce_helper[128];
static char			*mce_helper_argv[2] = { mce_helper, NULL };

static DECLARE_WAIT_QUEUE_HEAD(mce_chrdev_wait);

static DEFINE_PER_CPU(struct mce, mces_seen);
static int			cpu_missing;

/*
 * MCA banks polled by the period polling timer for corrected events.
 * With Intel CMCI, this only has MCA banks which do not support CMCI (if any).
 */
DEFINE_PER_CPU(mce_banks_t, mce_poll_banks) = {
	[0 ... BITS_TO_LONGS(MAX_NR_BANKS)-1] = ~0UL
};

/*
 * MCA banks controlled through firmware first for corrected errors.
 * This is a global list of banks for which we won't enable CMCI and we
 * won't poll. Firmware controls these banks and is responsible for
 * reporting corrected errors through GHES. Uncorrected/recoverable
 * errors are still notified through a machine check.
 */
mce_banks_t mce_banks_ce_disabled;

static struct work_struct mce_work;
static struct irq_work mce_irq_work;

static void (*quirk_no_way_out)(int bank, struct mce *m, struct pt_regs *regs);
static int mce_usable_address(struct mce *m);

/*
 * CPU/chipset specific EDAC code can register a notifier call here to print
 * MCE errors in a human-readable form.
 */
ATOMIC_NOTIFIER_HEAD(x86_mce_decoder_chain);

/* Do initial initialization of a struct mce */
void mce_setup(struct mce *m)
{
	memset(m, 0, sizeof(struct mce));
	m->cpu = m->extcpu = smp_processor_id();
	m->tsc = rdtsc();
	/* We hope get_seconds stays lockless */
	m->time = get_seconds();
	m->cpuvendor = boot_cpu_data.x86_vendor;
	m->cpuid = cpuid_eax(1);
	m->socketid = cpu_data(m->extcpu).phys_proc_id;
	m->apicid = cpu_data(m->extcpu).initial_apicid;
	rdmsrl(MSR_IA32_MCG_CAP, m->mcgcap);
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

	/* Emit the trace record: */
	trace_mce_record(mce);

	if (!mce_gen_pool_add(mce))
		irq_work_queue(&mce_irq_work);

	mce->finished = 0;
	wmb();
	for (;;) {
		entry = mce_log_get_idx_check(mcelog.next);
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

	mce->finished = 1;
	set_bit(0, &mce_need_notify);
}

void mce_inject_log(struct mce *m)
{
	mutex_lock(&mce_chrdev_read_mutex);
	mce_log(m);
	mutex_unlock(&mce_chrdev_read_mutex);
}
EXPORT_SYMBOL_GPL(mce_inject_log);

static struct notifier_block mce_srao_nb;

void mce_register_decode_chain(struct notifier_block *nb)
{
	/* Ensure SRAO notifier has the highest priority in the decode chain. */
	if (nb != &mce_srao_nb && nb->priority == INT_MAX)
		nb->priority -= 1;

	atomic_notifier_chain_register(&x86_mce_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(mce_register_decode_chain);

void mce_unregister_decode_chain(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&x86_mce_decoder_chain, nb);
}
EXPORT_SYMBOL_GPL(mce_unregister_decode_chain);

static void print_mce(struct mce *m)
{
	int ret = 0;

	pr_emerg(HW_ERR "CPU %d: Machine Check Exception: %Lx Bank %d: %016Lx\n",
	       m->extcpu, m->mcgstatus, m->bank, m->status);

	if (m->ip) {
		pr_emerg(HW_ERR "RIP%s %02x:<%016Lx> ",
			!(m->mcgstatus & MCG_STATUS_EIPV) ? " !INEXACT!" : "",
				m->cs, m->ip);

		if (m->cs == __KERNEL_CS)
			print_symbol("{%s}", m->ip);
		pr_cont("\n");
	}

	pr_emerg(HW_ERR "TSC %llx ", m->tsc);
	if (m->addr)
		pr_cont("ADDR %llx ", m->addr);
	if (m->misc)
		pr_cont("MISC %llx ", m->misc);

	pr_cont("\n");
	/*
	 * Note this output is parsed by external tools and old fields
	 * should not be changed.
	 */
	pr_emerg(HW_ERR "PROCESSOR %u:%x TIME %llu SOCKET %u APIC %x microcode %x\n",
		m->cpuvendor, m->cpuid, m->time, m->socketid, m->apicid,
		cpu_data(m->extcpu).microcode);

	/*
	 * Print out human-readable details about the MCE error,
	 * (if the CPU has an implementation for that)
	 */
	ret = atomic_notifier_call_chain(&x86_mce_decoder_chain, 0, m);
	if (ret == NOTIFY_STOP)
		return;

	pr_emerg_ratelimited(HW_ERR "Run the above through 'mcelog --ascii'\n");
}

#define PANIC_TIMEOUT 5 /* 5 seconds */

static atomic_t mce_panicked;

static int fake_panic;
static atomic_t mce_fake_panicked;

/* Panic in progress. Enable interrupts and wait for final IPI */
static void wait_for_panic(void)
{
	long timeout = PANIC_TIMEOUT*USEC_PER_SEC;

	preempt_disable();
	local_irq_enable();
	while (timeout-- > 0)
		udelay(1);
	if (panic_timeout == 0)
		panic_timeout = mca_cfg.panic_timeout;
	panic("Panicing machine check CPU died");
}

static void mce_panic(const char *msg, struct mce *final, char *exp)
{
	int i, apei_err = 0;

	if (!fake_panic) {
		/*
		 * Make sure only one CPU runs in machine check panic
		 */
		if (atomic_inc_return(&mce_panicked) > 1)
			wait_for_panic();
		barrier();

		bust_spinlocks(1);
		console_verbose();
	} else {
		/* Don't log too much for fake panic */
		if (atomic_inc_return(&mce_fake_panicked) > 1)
			return;
	}
	/* First print corrected ones that are still unlogged */
	for (i = 0; i < MCE_LOG_LEN; i++) {
		struct mce *m = &mcelog.entry[i];
		if (!(m->status & MCI_STATUS_VAL))
			continue;
		if (!(m->status & MCI_STATUS_UC)) {
			print_mce(m);
			if (!apei_err)
				apei_err = apei_write_mce(m);
		}
	}
	/* Now print uncorrected but with the final one last */
	for (i = 0; i < MCE_LOG_LEN; i++) {
		struct mce *m = &mcelog.entry[i];
		if (!(m->status & MCI_STATUS_VAL))
			continue;
		if (!(m->status & MCI_STATUS_UC))
			continue;
		if (!final || memcmp(m, final, sizeof(struct mce))) {
			print_mce(m);
			if (!apei_err)
				apei_err = apei_write_mce(m);
		}
	}
	if (final) {
		print_mce(final);
		if (!apei_err)
			apei_err = apei_write_mce(final);
	}
	if (cpu_missing)
		pr_emerg(HW_ERR "Some CPUs didn't answer in synchronization\n");
	if (exp)
		pr_emerg(HW_ERR "Machine check: %s\n", exp);
	if (!fake_panic) {
		if (panic_timeout == 0)
			panic_timeout = mca_cfg.panic_timeout;
		panic(msg);
	} else
		pr_emerg(HW_ERR "Fake kernel panic: %s\n", msg);
}

/* Support code for software error injection */

static int msr_to_offset(u32 msr)
{
	unsigned bank = __this_cpu_read(injectm.bank);

	if (msr == mca_cfg.rip_msr)
		return offsetof(struct mce, ip);
	if (msr == MSR_IA32_MCx_STATUS(bank))
		return offsetof(struct mce, status);
	if (msr == MSR_IA32_MCx_ADDR(bank))
		return offsetof(struct mce, addr);
	if (msr == MSR_IA32_MCx_MISC(bank))
		return offsetof(struct mce, misc);
	if (msr == MSR_IA32_MCG_STATUS)
		return offsetof(struct mce, mcgstatus);
	return -1;
}

/* MSR access wrappers used for error injection */
static u64 mce_rdmsrl(u32 msr)
{
	u64 v;

	if (__this_cpu_read(injectm.finished)) {
		int offset = msr_to_offset(msr);

		if (offset < 0)
			return 0;
		return *(u64 *)((char *)this_cpu_ptr(&injectm) + offset);
	}

	if (rdmsrl_safe(msr, &v)) {
		WARN_ONCE(1, "mce: Unable to read msr %d!\n", msr);
		/*
		 * Return zero in case the access faulted. This should
		 * not happen normally but can happen if the CPU does
		 * something weird, or if the code is buggy.
		 */
		v = 0;
	}

	return v;
}

static void mce_wrmsrl(u32 msr, u64 v)
{
	if (__this_cpu_read(injectm.finished)) {
		int offset = msr_to_offset(msr);

		if (offset >= 0)
			*(u64 *)((char *)this_cpu_ptr(&injectm) + offset) = v;
		return;
	}
	wrmsrl(msr, v);
}

/*
 * Collect all global (w.r.t. this processor) status about this machine
 * check into our "mce" struct so that we can use it later to assess
 * the severity of the problem as we read per-bank specific details.
 */
static inline void mce_gather_info(struct mce *m, struct pt_regs *regs)
{
	mce_setup(m);

	m->mcgstatus = mce_rdmsrl(MSR_IA32_MCG_STATUS);
	if (regs) {
		/*
		 * Get the address of the instruction at the time of
		 * the machine check error.
		 */
		if (m->mcgstatus & (MCG_STATUS_RIPV|MCG_STATUS_EIPV)) {
			m->ip = regs->ip;
			m->cs = regs->cs;

			/*
			 * When in VM86 mode make the cs look like ring 3
			 * always. This is a lie, but it's better than passing
			 * the additional vm86 bit around everywhere.
			 */
			if (v8086_mode(regs))
				m->cs |= 3;
		}
		/* Use accurate RIP reporting if available. */
		if (mca_cfg.rip_msr)
			m->ip = mce_rdmsrl(mca_cfg.rip_msr);
	}
}

int mce_available(struct cpuinfo_x86 *c)
{
	if (mca_cfg.disabled)
		return 0;
	return cpu_has(c, X86_FEATURE_MCE) && cpu_has(c, X86_FEATURE_MCA);
}

static void mce_schedule_work(void)
{
	if (!mce_gen_pool_empty() && keventd_up())
		schedule_work(&mce_work);
}

static void mce_irq_work_cb(struct irq_work *entry)
{
	mce_notify_irq();
	mce_schedule_work();
}

static void mce_report_event(struct pt_regs *regs)
{
	if (regs->flags & (X86_VM_MASK|X86_EFLAGS_IF)) {
		mce_notify_irq();
		/*
		 * Triggering the work queue here is just an insurance
		 * policy in case the syscall exit notify handler
		 * doesn't run soon enough or ends up running on the
		 * wrong CPU (can happen when audit sleeps)
		 */
		mce_schedule_work();
		return;
	}

	irq_work_queue(&mce_irq_work);
}

static int srao_decode_notifier(struct notifier_block *nb, unsigned long val,
				void *data)
{
	struct mce *mce = (struct mce *)data;
	unsigned long pfn;

	if (!mce)
		return NOTIFY_DONE;

	if (mce->usable_addr && (mce->severity == MCE_AO_SEVERITY)) {
		pfn = mce->addr >> PAGE_SHIFT;
		memory_failure(pfn, MCE_VECTOR, 0);
	}

	return NOTIFY_OK;
}
static struct notifier_block mce_srao_nb = {
	.notifier_call	= srao_decode_notifier,
	.priority = INT_MAX,
};

/*
 * Read ADDR and MISC registers.
 */
static void mce_read_aux(struct mce *m, int i)
{
	if (m->status & MCI_STATUS_MISCV)
		m->misc = mce_rdmsrl(MSR_IA32_MCx_MISC(i));
	if (m->status & MCI_STATUS_ADDRV) {
		m->addr = mce_rdmsrl(MSR_IA32_MCx_ADDR(i));

		/*
		 * Mask the reported address by the reported granularity.
		 */
		if (mca_cfg.ser && (m->status & MCI_STATUS_MISCV)) {
			u8 shift = MCI_MISC_ADDR_LSB(m->misc);
			m->addr >>= shift;
			m->addr <<= shift;
		}
	}
}

static bool memory_error(struct mce *m)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	if (c->x86_vendor == X86_VENDOR_AMD) {
		/*
		 * coming soon
		 */
		return false;
	} else if (c->x86_vendor == X86_VENDOR_INTEL) {
		/*
		 * Intel SDM Volume 3B - 15.9.2 Compound Error Codes
		 *
		 * Bit 7 of the MCACOD field of IA32_MCi_STATUS is used for
		 * indicating a memory error. Bit 8 is used for indicating a
		 * cache hierarchy error. The combination of bit 2 and bit 3
		 * is used for indicating a `generic' cache hierarchy error
		 * But we can't just blindly check the above bits, because if
		 * bit 11 is set, then it is a bus/interconnect error - and
		 * either way the above bits just gives more detail on what
		 * bus/interconnect error happened. Note that bit 12 can be
		 * ignored, as it's the "filter" bit.
		 */
		return (m->status & 0xef80) == BIT(7) ||
		       (m->status & 0xef00) == BIT(8) ||
		       (m->status & 0xeffc) == 0xc;
	}

	return false;
}

DEFINE_PER_CPU(unsigned, mce_poll_count);

/*
 * Poll for corrected events or events that happened before reset.
 * Those are just logged through /dev/mcelog.
 *
 * This is executed in standard interrupt context.
 *
 * Note: spec recommends to panic for fatal unsignalled
 * errors here. However this would be quite problematic --
 * we would need to reimplement the Monarch handling and
 * it would mess up the exclusion between exception handler
 * and poll hander -- * so we skip this for now.
 * These cases should not happen anyways, or only when the CPU
 * is already totally * confused. In this case it's likely it will
 * not fully execute the machine check handler either.
 */
bool machine_check_poll(enum mcp_flags flags, mce_banks_t *b)
{
	bool error_logged = false;
	struct mce m;
	int severity;
	int i;

	this_cpu_inc(mce_poll_count);

	mce_gather_info(&m, NULL);

	for (i = 0; i < mca_cfg.banks; i++) {
		if (!mce_banks[i].ctl || !test_bit(i, *b))
			continue;

		m.misc = 0;
		m.addr = 0;
		m.bank = i;
		m.tsc = 0;

		barrier();
		m.status = mce_rdmsrl(MSR_IA32_MCx_STATUS(i));
		if (!(m.status & MCI_STATUS_VAL))
			continue;


		/*
		 * Uncorrected or signalled events are handled by the exception
		 * handler when it is enabled, so don't process those here.
		 *
		 * TBD do the same check for MCI_STATUS_EN here?
		 */
		if (!(flags & MCP_UC) &&
		    (m.status & (mca_cfg.ser ? MCI_STATUS_S : MCI_STATUS_UC)))
			continue;

		mce_read_aux(&m, i);

		if (!(flags & MCP_TIMESTAMP))
			m.tsc = 0;

		severity = mce_severity(&m, mca_cfg.tolerant, NULL, false);

		/*
		 * In the cases where we don't have a valid address after all,
		 * do not add it into the ring buffer.
		 */
		if (severity == MCE_DEFERRED_SEVERITY && memory_error(&m)) {
			if (m.status & MCI_STATUS_ADDRV) {
				m.severity = severity;
				m.usable_addr = mce_usable_address(&m);

				if (!mce_gen_pool_add(&m))
					mce_schedule_work();
			}
		}

		/*
		 * Don't get the IP here because it's unlikely to
		 * have anything to do with the actual error location.
		 */
		if (!(flags & MCP_DONTLOG) && !mca_cfg.dont_log_ce) {
			error_logged = true;
			mce_log(&m);
		}

		/*
		 * Clear state for this bank.
		 */
		mce_wrmsrl(MSR_IA32_MCx_STATUS(i), 0);
	}

	/*
	 * Don't clear MCG_STATUS here because it's only defined for
	 * exceptions.
	 */

	sync_core();

	return error_logged;
}
EXPORT_SYMBOL_GPL(machine_check_poll);

/*
 * Do a quick check if any of the events requires a panic.
 * This decides if we keep the events around or clear them.
 */
static int mce_no_way_out(struct mce *m, char **msg, unsigned long *validp,
			  struct pt_regs *regs)
{
	int i, ret = 0;
	char *tmp;

	for (i = 0; i < mca_cfg.banks; i++) {
		m->status = mce_rdmsrl(MSR_IA32_MCx_STATUS(i));
		if (m->status & MCI_STATUS_VAL) {
			__set_bit(i, validp);
			if (quirk_no_way_out)
				quirk_no_way_out(i, m, regs);
		}

		if (mce_severity(m, mca_cfg.tolerant, &tmp, true) >= MCE_PANIC_SEVERITY) {
			*msg = tmp;
			ret = 1;
		}
	}
	return ret;
}

/*
 * Variable to establish order between CPUs while scanning.
 * Each CPU spins initially until executing is equal its number.
 */
static atomic_t mce_executing;

/*
 * Defines order of CPUs on entry. First CPU becomes Monarch.
 */
static atomic_t mce_callin;

/*
 * Check if a timeout waiting for other CPUs happened.
 */
static int mce_timed_out(u64 *t, const char *msg)
{
	/*
	 * The others already did panic for some reason.
	 * Bail out like in a timeout.
	 * rmb() to tell the compiler that system_state
	 * might have been modified by someone else.
	 */
	rmb();
	if (atomic_read(&mce_panicked))
		wait_for_panic();
	if (!mca_cfg.monarch_timeout)
		goto out;
	if ((s64)*t < SPINUNIT) {
		if (mca_cfg.tolerant <= 1)
			mce_panic(msg, NULL, NULL);
		cpu_missing = 1;
		return 1;
	}
	*t -= SPINUNIT;
out:
	touch_nmi_watchdog();
	return 0;
}

/*
 * The Monarch's reign.  The Monarch is the CPU who entered
 * the machine check handler first. It waits for the others to
 * raise the exception too and then grades them. When any
 * error is fatal panic. Only then let the others continue.
 *
 * The other CPUs entering the MCE handler will be controlled by the
 * Monarch. They are called Subjects.
 *
 * This way we prevent any potential data corruption in a unrecoverable case
 * and also makes sure always all CPU's errors are examined.
 *
 * Also this detects the case of a machine check event coming from outer
 * space (not detected by any CPUs) In this case some external agent wants
 * us to shut down, so panic too.
 *
 * The other CPUs might still decide to panic if the handler happens
 * in a unrecoverable place, but in this case the system is in a semi-stable
 * state and won't corrupt anything by itself. It's ok to let the others
 * continue for a bit first.
 *
 * All the spin loops have timeouts; when a timeout happens a CPU
 * typically elects itself to be Monarch.
 */
static void mce_reign(void)
{
	int cpu;
	struct mce *m = NULL;
	int global_worst = 0;
	char *msg = NULL;
	char *nmsg = NULL;

	/*
	 * This CPU is the Monarch and the other CPUs have run
	 * through their handlers.
	 * Grade the severity of the errors of all the CPUs.
	 */
	for_each_possible_cpu(cpu) {
		int severity = mce_severity(&per_cpu(mces_seen, cpu),
					    mca_cfg.tolerant,
					    &nmsg, true);
		if (severity > global_worst) {
			msg = nmsg;
			global_worst = severity;
			m = &per_cpu(mces_seen, cpu);
		}
	}

	/*
	 * Cannot recover? Panic here then.
	 * This dumps all the mces in the log buffer and stops the
	 * other CPUs.
	 */
	if (m && global_worst >= MCE_PANIC_SEVERITY && mca_cfg.tolerant < 3)
		mce_panic("Fatal machine check", m, msg);

	/*
	 * For UC somewhere we let the CPU who detects it handle it.
	 * Also must let continue the others, otherwise the handling
	 * CPU could deadlock on a lock.
	 */

	/*
	 * No machine check event found. Must be some external
	 * source or one CPU is hung. Panic.
	 */
	if (global_worst <= MCE_KEEP_SEVERITY && mca_cfg.tolerant < 3)
		mce_panic("Fatal machine check from unknown source", NULL, NULL);

	/*
	 * Now clear all the mces_seen so that they don't reappear on
	 * the next mce.
	 */
	for_each_possible_cpu(cpu)
		memset(&per_cpu(mces_seen, cpu), 0, sizeof(struct mce));
}

static atomic_t global_nwo;

/*
 * Start of Monarch synchronization. This waits until all CPUs have
 * entered the exception handler and then determines if any of them
 * saw a fatal event that requires panic. Then it executes them
 * in the entry order.
 * TBD double check parallel CPU hotunplug
 */
static int mce_start(int *no_way_out)
{
	int order;
	int cpus = num_online_cpus();
	u64 timeout = (u64)mca_cfg.monarch_timeout * NSEC_PER_USEC;

	if (!timeout)
		return -1;

	atomic_add(*no_way_out, &global_nwo);
	/*
	 * global_nwo should be updated before mce_callin
	 */
	smp_wmb();
	order = atomic_inc_return(&mce_callin);

	/*
	 * Wait for everyone.
	 */
	while (atomic_read(&mce_callin) != cpus) {
		if (mce_timed_out(&timeout,
				  "Timeout: Not all CPUs entered broadcast exception handler")) {
			atomic_set(&global_nwo, 0);
			return -1;
		}
		ndelay(SPINUNIT);
	}

	/*
	 * mce_callin should be read before global_nwo
	 */
	smp_rmb();

	if (order == 1) {
		/*
		 * Monarch: Starts executing now, the others wait.
		 */
		atomic_set(&mce_executing, 1);
	} else {
		/*
		 * Subject: Now start the scanning loop one by one in
		 * the original callin order.
		 * This way when there are any shared banks it will be
		 * only seen by one CPU before cleared, avoiding duplicates.
		 */
		while (atomic_read(&mce_executing) < order) {
			if (mce_timed_out(&timeout,
					  "Timeout: Subject CPUs unable to finish machine check processing")) {
				atomic_set(&global_nwo, 0);
				return -1;
			}
			ndelay(SPINUNIT);
		}
	}

	/*
	 * Cache the global no_way_out state.
	 */
	*no_way_out = atomic_read(&global_nwo);

	return order;
}

/*
 * Synchronize between CPUs after main scanning loop.
 * This invokes the bulk of the Monarch processing.
 */
static int mce_end(int order)
{
	int ret = -1;
	u64 timeout = (u64)mca_cfg.monarch_timeout * NSEC_PER_USEC;

	if (!timeout)
		goto reset;
	if (order < 0)
		goto reset;

	/*
	 * Allow others to run.
	 */
	atomic_inc(&mce_executing);

	if (order == 1) {
		/* CHECKME: Can this race with a parallel hotplug? */
		int cpus = num_online_cpus();

		/*
		 * Monarch: Wait for everyone to go through their scanning
		 * loops.
		 */
		while (atomic_read(&mce_executing) <= cpus) {
			if (mce_timed_out(&timeout,
					  "Timeout: Monarch CPU unable to finish machine check processing"))
				goto reset;
			ndelay(SPINUNIT);
		}

		mce_reign();
		barrier();
		ret = 0;
	} else {
		/*
		 * Subject: Wait for Monarch to finish.
		 */
		while (atomic_read(&mce_executing) != 0) {
			if (mce_timed_out(&timeout,
					  "Timeout: Monarch CPU did not finish machine check processing"))
				goto reset;
			ndelay(SPINUNIT);
		}

		/*
		 * Don't reset anything. That's done by the Monarch.
		 */
		return 0;
	}

	/*
	 * Reset all global state.
	 */
reset:
	atomic_set(&global_nwo, 0);
	atomic_set(&mce_callin, 0);
	barrier();

	/*
	 * Let others run again.
	 */
	atomic_set(&mce_executing, 0);
	return ret;
}

/*
 * Check if the address reported by the CPU is in a format we can parse.
 * It would be possible to add code for most other cases, but all would
 * be somewhat complicated (e.g. segment offset would require an instruction
 * parser). So only support physical addresses up to page granuality for now.
 */
static int mce_usable_address(struct mce *m)
{
	if (!(m->status & MCI_STATUS_MISCV) || !(m->status & MCI_STATUS_ADDRV))
		return 0;
	if (MCI_MISC_ADDR_LSB(m->misc) > PAGE_SHIFT)
		return 0;
	if (MCI_MISC_ADDR_MODE(m->misc) != MCI_MISC_ADDR_PHYS)
		return 0;
	return 1;
}

static void mce_clear_state(unsigned long *toclear)
{
	int i;

	for (i = 0; i < mca_cfg.banks; i++) {
		if (test_bit(i, toclear))
			mce_wrmsrl(MSR_IA32_MCx_STATUS(i), 0);
	}
}

/*
 * The actual machine check handler. This only handles real
 * exceptions when something got corrupted coming in through int 18.
 *
 * This is executed in NMI context not subject to normal locking rules. This
 * implies that most kernel services cannot be safely used. Don't even
 * think about putting a printk in there!
 *
 * On Intel systems this is entered on all CPUs in parallel through
 * MCE broadcast. However some CPUs might be broken beyond repair,
 * so be always careful when synchronizing with others.
 */
void do_machine_check(struct pt_regs *regs, long error_code)
{
	struct mca_config *cfg = &mca_cfg;
	struct mce m, *final;
	int i;
	int worst = 0;
	int severity;
	/*
	 * Establish sequential order between the CPUs entering the machine
	 * check handler.
	 */
	int order;
	/*
	 * If no_way_out gets set, there is no safe way to recover from this
	 * MCE.  If mca_cfg.tolerant is cranked up, we'll try anyway.
	 */
	int no_way_out = 0;
	/*
	 * If kill_it gets set, there might be a way to recover from this
	 * error.
	 */
	int kill_it = 0;
	DECLARE_BITMAP(toclear, MAX_NR_BANKS);
	DECLARE_BITMAP(valid_banks, MAX_NR_BANKS);
	char *msg = "Unknown";
	u64 recover_paddr = ~0ull;
	int flags = MF_ACTION_REQUIRED;
	int lmce = 0;

	ist_enter(regs);

	this_cpu_inc(mce_exception_count);

	if (!cfg->banks)
		goto out;

	mce_gather_info(&m, regs);

	final = this_cpu_ptr(&mces_seen);
	*final = m;

	memset(valid_banks, 0, sizeof(valid_banks));
	no_way_out = mce_no_way_out(&m, &msg, valid_banks, regs);

	barrier();

	/*
	 * When no restart IP might need to kill or panic.
	 * Assume the worst for now, but if we find the
	 * severity is MCE_AR_SEVERITY we have other options.
	 */
	if (!(m.mcgstatus & MCG_STATUS_RIPV))
		kill_it = 1;

	/*
	 * Check if this MCE is signaled to only this logical processor
	 */
	if (m.mcgstatus & MCG_STATUS_LMCES)
		lmce = 1;
	else {
		/*
		 * Go through all the banks in exclusion of the other CPUs.
		 * This way we don't report duplicated events on shared banks
		 * because the first one to see it will clear it.
		 * If this is a Local MCE, then no need to perform rendezvous.
		 */
		order = mce_start(&no_way_out);
	}

	for (i = 0; i < cfg->banks; i++) {
		__clear_bit(i, toclear);
		if (!test_bit(i, valid_banks))
			continue;
		if (!mce_banks[i].ctl)
			continue;

		m.misc = 0;
		m.addr = 0;
		m.bank = i;

		m.status = mce_rdmsrl(MSR_IA32_MCx_STATUS(i));
		if ((m.status & MCI_STATUS_VAL) == 0)
			continue;

		/*
		 * Non uncorrected or non signaled errors are handled by
		 * machine_check_poll. Leave them alone, unless this panics.
		 */
		if (!(m.status & (cfg->ser ? MCI_STATUS_S : MCI_STATUS_UC)) &&
			!no_way_out)
			continue;

		/*
		 * Set taint even when machine check was not enabled.
		 */
		add_taint(TAINT_MACHINE_CHECK, LOCKDEP_NOW_UNRELIABLE);

		severity = mce_severity(&m, cfg->tolerant, NULL, true);

		/*
		 * When machine check was for corrected/deferred handler don't
		 * touch, unless we're panicing.
		 */
		if ((severity == MCE_KEEP_SEVERITY ||
		     severity == MCE_UCNA_SEVERITY) && !no_way_out)
			continue;
		__set_bit(i, toclear);
		if (severity == MCE_NO_SEVERITY) {
			/*
			 * Machine check event was not enabled. Clear, but
			 * ignore.
			 */
			continue;
		}

		mce_read_aux(&m, i);

		/* assuming valid severity level != 0 */
		m.severity = severity;
		m.usable_addr = mce_usable_address(&m);

		mce_log(&m);

		if (severity > worst) {
			*final = m;
			worst = severity;
		}
	}

	/* mce_clear_state will clear *final, save locally for use later */
	m = *final;

	if (!no_way_out)
		mce_clear_state(toclear);

	/*
	 * Do most of the synchronization with other CPUs.
	 * When there's any problem use only local no_way_out state.
	 */
	if (!lmce) {
		if (mce_end(order) < 0)
			no_way_out = worst >= MCE_PANIC_SEVERITY;
	} else {
		/*
		 * Local MCE skipped calling mce_reign()
		 * If we found a fatal error, we need to panic here.
		 */
		 if (worst >= MCE_PANIC_SEVERITY && mca_cfg.tolerant < 3)
			mce_panic("Machine check from unknown source",
				NULL, NULL);
	}

	/*
	 * At insane "tolerant" levels we take no action. Otherwise
	 * we only die if we have no other choice. For less serious
	 * issues we try to recover, or limit damage to the current
	 * process.
	 */
	if (cfg->tolerant < 3) {
		if (no_way_out)
			mce_panic("Fatal machine check on current CPU", &m, msg);
		if (worst == MCE_AR_SEVERITY) {
			recover_paddr = m.addr;
			if (!(m.mcgstatus & MCG_STATUS_RIPV))
				flags |= MF_MUST_KILL;
		} else if (kill_it) {
			force_sig(SIGBUS, current);
		}
	}

	if (worst > 0)
		mce_report_event(regs);
	mce_wrmsrl(MSR_IA32_MCG_STATUS, 0);
out:
	sync_core();

	if (recover_paddr == ~0ull)
		goto done;

	pr_err("Uncorrected hardware memory error in user-access at %llx",
		 recover_paddr);
	/*
	 * We must call memory_failure() here even if the current process is
	 * doomed. We still need to mark the page as poisoned and alert any
	 * other users of the page.
	 */
	ist_begin_non_atomic(regs);
	local_irq_enable();
	if (memory_failure(recover_paddr >> PAGE_SHIFT, MCE_VECTOR, flags) < 0) {
		pr_err("Memory error not recovered");
		force_sig(SIGBUS, current);
	}
	local_irq_disable();
	ist_end_non_atomic();
done:
	ist_exit(regs);
}
EXPORT_SYMBOL_GPL(do_machine_check);

#ifndef CONFIG_MEMORY_FAILURE
int memory_failure(unsigned long pfn, int vector, int flags)
{
	/* mce_severity() should not hand us an ACTION_REQUIRED error */
	BUG_ON(flags & MF_ACTION_REQUIRED);
	pr_err("Uncorrected memory error in page 0x%lx ignored\n"
	       "Rebuild kernel with CONFIG_MEMORY_FAILURE=y for smarter handling\n",
	       pfn);

	return 0;
}
#endif

/*
 * Action optional processing happens here (picking up
 * from the list of faulting pages that do_machine_check()
 * placed into the genpool).
 */
static void mce_process_work(struct work_struct *dummy)
{
	mce_gen_pool_process();
}

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
static unsigned long check_interval = INITIAL_CHECK_INTERVAL;

static DEFINE_PER_CPU(unsigned long, mce_next_interval); /* in jiffies */
static DEFINE_PER_CPU(struct timer_list, mce_timer);

static unsigned long mce_adjust_timer_default(unsigned long interval)
{
	return interval;
}

static unsigned long (*mce_adjust_timer)(unsigned long interval) = mce_adjust_timer_default;

static void __restart_timer(struct timer_list *t, unsigned long interval)
{
	unsigned long when = jiffies + interval;
	unsigned long flags;

	local_irq_save(flags);

	if (timer_pending(t)) {
		if (time_before(when, t->expires))
			mod_timer_pinned(t, when);
	} else {
		t->expires = round_jiffies(when);
		add_timer_on(t, smp_processor_id());
	}

	local_irq_restore(flags);
}

static void mce_timer_fn(unsigned long data)
{
	struct timer_list *t = this_cpu_ptr(&mce_timer);
	int cpu = smp_processor_id();
	unsigned long iv;

	WARN_ON(cpu != data);

	iv = __this_cpu_read(mce_next_interval);

	if (mce_available(this_cpu_ptr(&cpu_info))) {
		machine_check_poll(MCP_TIMESTAMP, this_cpu_ptr(&mce_poll_banks));

		if (mce_intel_cmci_poll()) {
			iv = mce_adjust_timer(iv);
			goto done;
		}
	}

	/*
	 * Alert userspace if needed. If we logged an MCE, reduce the polling
	 * interval, otherwise increase the polling interval.
	 */
	if (mce_notify_irq())
		iv = max(iv / 2, (unsigned long) HZ/100);
	else
		iv = min(iv * 2, round_jiffies_relative(check_interval * HZ));

done:
	__this_cpu_write(mce_next_interval, iv);
	__restart_timer(t, iv);
}

/*
 * Ensure that the timer is firing in @interval from now.
 */
void mce_timer_kick(unsigned long interval)
{
	struct timer_list *t = this_cpu_ptr(&mce_timer);
	unsigned long iv = __this_cpu_read(mce_next_interval);

	__restart_timer(t, interval);

	if (interval < iv)
		__this_cpu_write(mce_next_interval, interval);
}

/* Must not be called in IRQ context where del_timer_sync() can deadlock */
static void mce_timer_delete_all(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		del_timer_sync(&per_cpu(mce_timer, cpu));
}

static void mce_do_trigger(struct work_struct *work)
{
	call_usermodehelper(mce_helper, mce_helper_argv, NULL, UMH_NO_WAIT);
}

static DECLARE_WORK(mce_trigger_work, mce_do_trigger);

/*
 * Notify the user(s) about new machine check events.
 * Can be called from interrupt context, but not from machine check/NMI
 * context.
 */
int mce_notify_irq(void)
{
	/* Not more than two messages every minute */
	static DEFINE_RATELIMIT_STATE(ratelimit, 60*HZ, 2);

	if (test_and_clear_bit(0, &mce_need_notify)) {
		/* wake processes polling /dev/mcelog */
		wake_up_interruptible(&mce_chrdev_wait);

		if (mce_helper[0])
			schedule_work(&mce_trigger_work);

		if (__ratelimit(&ratelimit))
			pr_info(HW_ERR "Machine check events logged\n");

		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mce_notify_irq);

static int __mcheck_cpu_mce_banks_init(void)
{
	int i;
	u8 num_banks = mca_cfg.banks;

	mce_banks = kzalloc(num_banks * sizeof(struct mce_bank), GFP_KERNEL);
	if (!mce_banks)
		return -ENOMEM;

	for (i = 0; i < num_banks; i++) {
		struct mce_bank *b = &mce_banks[i];

		b->ctl = -1ULL;
		b->init = 1;
	}
	return 0;
}

/*
 * Initialize Machine Checks for a CPU.
 */
static int __mcheck_cpu_cap_init(void)
{
	unsigned b;
	u64 cap;

	rdmsrl(MSR_IA32_MCG_CAP, cap);

	b = cap & MCG_BANKCNT_MASK;
	if (!mca_cfg.banks)
		pr_info("CPU supports %d MCE banks\n", b);

	if (b > MAX_NR_BANKS) {
		pr_warn("Using only %u machine check banks out of %u\n",
			MAX_NR_BANKS, b);
		b = MAX_NR_BANKS;
	}

	/* Don't support asymmetric configurations today */
	WARN_ON(mca_cfg.banks != 0 && b != mca_cfg.banks);
	mca_cfg.banks = b;

	if (!mce_banks) {
		int err = __mcheck_cpu_mce_banks_init();

		if (err)
			return err;
	}

	/* Use accurate RIP reporting if available. */
	if ((cap & MCG_EXT_P) && MCG_EXT_CNT(cap) >= 9)
		mca_cfg.rip_msr = MSR_IA32_MCG_EIP;

	if (cap & MCG_SER_P)
		mca_cfg.ser = true;

	return 0;
}

static void __mcheck_cpu_init_generic(void)
{
	enum mcp_flags m_fl = 0;
	mce_banks_t all_banks;
	u64 cap;
	int i;

	if (!mca_cfg.bootlog)
		m_fl = MCP_DONTLOG;

	/*
	 * Log the machine checks left over from the previous reset.
	 */
	bitmap_fill(all_banks, MAX_NR_BANKS);
	machine_check_poll(MCP_UC | m_fl, &all_banks);

	cr4_set_bits(X86_CR4_MCE);

	rdmsrl(MSR_IA32_MCG_CAP, cap);
	if (cap & MCG_CTL_P)
		wrmsr(MSR_IA32_MCG_CTL, 0xffffffff, 0xffffffff);

	for (i = 0; i < mca_cfg.banks; i++) {
		struct mce_bank *b = &mce_banks[i];

		if (!b->init)
			continue;
		wrmsrl(MSR_IA32_MCx_CTL(i), b->ctl);
		wrmsrl(MSR_IA32_MCx_STATUS(i), 0);
	}
}

/*
 * During IFU recovery Sandy Bridge -EP4S processors set the RIPV and
 * EIPV bits in MCG_STATUS to zero on the affected logical processor (SDM
 * Vol 3B Table 15-20). But this confuses both the code that determines
 * whether the machine check occurred in kernel or user mode, and also
 * the severity assessment code. Pretend that EIPV was set, and take the
 * ip/cs values from the pt_regs that mce_gather_info() ignored earlier.
 */
static void quirk_sandybridge_ifu(int bank, struct mce *m, struct pt_regs *regs)
{
	if (bank != 0)
		return;
	if ((m->mcgstatus & (MCG_STATUS_EIPV|MCG_STATUS_RIPV)) != 0)
		return;
	if ((m->status & (MCI_STATUS_OVER|MCI_STATUS_UC|
		          MCI_STATUS_EN|MCI_STATUS_MISCV|MCI_STATUS_ADDRV|
			  MCI_STATUS_PCC|MCI_STATUS_S|MCI_STATUS_AR|
			  MCACOD)) !=
			 (MCI_STATUS_UC|MCI_STATUS_EN|
			  MCI_STATUS_MISCV|MCI_STATUS_ADDRV|MCI_STATUS_S|
			  MCI_STATUS_AR|MCACOD_INSTR))
		return;

	m->mcgstatus |= MCG_STATUS_EIPV;
	m->ip = regs->ip;
	m->cs = regs->cs;
}

/* Add per CPU specific workarounds here */
static int __mcheck_cpu_apply_quirks(struct cpuinfo_x86 *c)
{
	struct mca_config *cfg = &mca_cfg;

	if (c->x86_vendor == X86_VENDOR_UNKNOWN) {
		pr_info("unknown CPU type - not enabling MCE support\n");
		return -EOPNOTSUPP;
	}

	/* This should be disabled by the BIOS, but isn't always */
	if (c->x86_vendor == X86_VENDOR_AMD) {
		if (c->x86 == 15 && cfg->banks > 4) {
			/*
			 * disable GART TBL walk error reporting, which
			 * trips off incorrectly with the IOMMU & 3ware
			 * & Cerberus:
			 */
			clear_bit(10, (unsigned long *)&mce_banks[4].ctl);
		}
		if (c->x86 <= 17 && cfg->bootlog < 0) {
			/*
			 * Lots of broken BIOS around that don't clear them
			 * by default and leave crap in there. Don't log:
			 */
			cfg->bootlog = 0;
		}
		/*
		 * Various K7s with broken bank 0 around. Always disable
		 * by default.
		 */
		if (c->x86 == 6 && cfg->banks > 0)
			mce_banks[0].ctl = 0;

		/*
		 * overflow_recov is supported for F15h Models 00h-0fh
		 * even though we don't have a CPUID bit for it.
		 */
		if (c->x86 == 0x15 && c->x86_model <= 0xf)
			mce_flags.overflow_recov = 1;

		/*
		 * Turn off MC4_MISC thresholding banks on those models since
		 * they're not supported there.
		 */
		if (c->x86 == 0x15 &&
		    (c->x86_model >= 0x10 && c->x86_model <= 0x1f)) {
			int i;
			u64 hwcr;
			bool need_toggle;
			u32 msrs[] = {
				0x00000413, /* MC4_MISC0 */
				0xc0000408, /* MC4_MISC1 */
			};

			rdmsrl(MSR_K7_HWCR, hwcr);

			/* McStatusWrEn has to be set */
			need_toggle = !(hwcr & BIT(18));

			if (need_toggle)
				wrmsrl(MSR_K7_HWCR, hwcr | BIT(18));

			/* Clear CntP bit safely */
			for (i = 0; i < ARRAY_SIZE(msrs); i++)
				msr_clear_bit(msrs[i], 62);

			/* restore old settings */
			if (need_toggle)
				wrmsrl(MSR_K7_HWCR, hwcr);
		}
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

		if (c->x86 == 6 && c->x86_model < 0x1A && cfg->banks > 0)
			mce_banks[0].init = 0;

		/*
		 * All newer Intel systems support MCE broadcasting. Enable
		 * synchronization with a one second timeout.
		 */
		if ((c->x86 > 6 || (c->x86 == 6 && c->x86_model >= 0xe)) &&
			cfg->monarch_timeout < 0)
			cfg->monarch_timeout = USEC_PER_SEC;

		/*
		 * There are also broken BIOSes on some Pentium M and
		 * earlier systems:
		 */
		if (c->x86 == 6 && c->x86_model <= 13 && cfg->bootlog < 0)
			cfg->bootlog = 0;

		if (c->x86 == 6 && c->x86_model == 45)
			quirk_no_way_out = quirk_sandybridge_ifu;
	}
	if (cfg->monarch_timeout < 0)
		cfg->monarch_timeout = 0;
	if (cfg->bootlog != 0)
		cfg->panic_timeout = 30;

	return 0;
}

static int __mcheck_cpu_ancient_init(struct cpuinfo_x86 *c)
{
	if (c->x86 != 5)
		return 0;

	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		intel_p5_mcheck_init(c);
		return 1;
		break;
	case X86_VENDOR_CENTAUR:
		winchip_mcheck_init(c);
		return 1;
		break;
	}

	return 0;
}

static void __mcheck_cpu_init_vendor(struct cpuinfo_x86 *c)
{
	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		mce_intel_feature_init(c);
		mce_adjust_timer = cmci_intel_adjust_timer;
		break;

	case X86_VENDOR_AMD: {
		u32 ebx = cpuid_ebx(0x80000007);

		mce_amd_feature_init(c);
		mce_flags.overflow_recov = !!(ebx & BIT(0));
		mce_flags.succor	 = !!(ebx & BIT(1));
		break;
		}

	default:
		break;
	}
}

static void __mcheck_cpu_clear_vendor(struct cpuinfo_x86 *c)
{
	switch (c->x86_vendor) {
	case X86_VENDOR_INTEL:
		mce_intel_feature_clear(c);
		break;
	default:
		break;
	}
}

static void mce_start_timer(unsigned int cpu, struct timer_list *t)
{
	unsigned long iv = check_interval * HZ;

	if (mca_cfg.ignore_ce || !iv)
		return;

	per_cpu(mce_next_interval, cpu) = iv;

	t->expires = round_jiffies(jiffies + iv);
	add_timer_on(t, cpu);
}

static void __mcheck_cpu_init_timer(void)
{
	struct timer_list *t = this_cpu_ptr(&mce_timer);
	unsigned int cpu = smp_processor_id();

	setup_timer(t, mce_timer_fn, cpu);
	mce_start_timer(cpu, t);
}

/* Handle unconfigured int18 (should never happen) */
static void unexpected_machine_check(struct pt_regs *regs, long error_code)
{
	pr_err("CPU#%d: Unexpected int18 (Machine Check)\n",
	       smp_processor_id());
}

/* Call the installed machine check handler for this CPU setup. */
void (*machine_check_vector)(struct pt_regs *, long error_code) =
						unexpected_machine_check;

/*
 * Called for each booted CPU to set up machine checks.
 * Must be called with preempt off:
 */
void mcheck_cpu_init(struct cpuinfo_x86 *c)
{
	if (mca_cfg.disabled)
		return;

	if (__mcheck_cpu_ancient_init(c))
		return;

	if (!mce_available(c))
		return;

	if (__mcheck_cpu_cap_init() < 0 || __mcheck_cpu_apply_quirks(c) < 0) {
		mca_cfg.disabled = true;
		return;
	}

	if (mce_gen_pool_init()) {
		mca_cfg.disabled = true;
		pr_emerg("Couldn't allocate MCE records pool!\n");
		return;
	}

	machine_check_vector = do_machine_check;

	__mcheck_cpu_init_generic();
	__mcheck_cpu_init_vendor(c);
	__mcheck_cpu_init_timer();
}

/*
 * Called for each booted CPU to clear some machine checks opt-ins
 */
void mcheck_cpu_clear(struct cpuinfo_x86 *c)
{
	if (mca_cfg.disabled)
		return;

	if (!mce_available(c))
		return;

	/*
	 * Possibly to clear general settings generic to x86
	 * __mcheck_cpu_clear_generic(c);
	 */
	__mcheck_cpu_clear_vendor(c);

}

/*
 * mce_chrdev: Character device /dev/mcelog to read and clear the MCE log.
 */

static DEFINE_SPINLOCK(mce_chrdev_state_lock);
static int mce_chrdev_open_count;	/* #times opened */
static int mce_chrdev_open_exclu;	/* already open exclusive? */

static int mce_chrdev_open(struct inode *inode, struct file *file)
{
	spin_lock(&mce_chrdev_state_lock);

	if (mce_chrdev_open_exclu ||
	    (mce_chrdev_open_count && (file->f_flags & O_EXCL))) {
		spin_unlock(&mce_chrdev_state_lock);

		return -EBUSY;
	}

	if (file->f_flags & O_EXCL)
		mce_chrdev_open_exclu = 1;
	mce_chrdev_open_count++;

	spin_unlock(&mce_chrdev_state_lock);

	return nonseekable_open(inode, file);
}

static int mce_chrdev_release(struct inode *inode, struct file *file)
{
	spin_lock(&mce_chrdev_state_lock);

	mce_chrdev_open_count--;
	mce_chrdev_open_exclu = 0;

	spin_unlock(&mce_chrdev_state_lock);

	return 0;
}

static void collect_tscs(void *data)
{
	unsigned long *cpu_tsc = (unsigned long *)data;

	cpu_tsc[smp_processor_id()] = rdtsc();
}

static int mce_apei_read_done;

/* Collect MCE record of previous boot in persistent storage via APEI ERST. */
static int __mce_read_apei(char __user **ubuf, size_t usize)
{
	int rc;
	u64 record_id;
	struct mce m;

	if (usize < sizeof(struct mce))
		return -EINVAL;

	rc = apei_read_mce(&m, &record_id);
	/* Error or no more MCE record */
	if (rc <= 0) {
		mce_apei_read_done = 1;
		/*
		 * When ERST is disabled, mce_chrdev_read() should return
		 * "no record" instead of "no device."
		 */
		if (rc == -ENODEV)
			return 0;
		return rc;
	}
	rc = -EFAULT;
	if (copy_to_user(*ubuf, &m, sizeof(struct mce)))
		return rc;
	/*
	 * In fact, we should have cleared the record after that has
	 * been flushed to the disk or sent to network in
	 * /sbin/mcelog, but we have no interface to support that now,
	 * so just clear it to avoid duplication.
	 */
	rc = apei_clear_mce(record_id);
	if (rc) {
		mce_apei_read_done = 1;
		return rc;
	}
	*ubuf += sizeof(struct mce);

	return 0;
}

static ssize_t mce_chrdev_read(struct file *filp, char __user *ubuf,
				size_t usize, loff_t *off)
{
	char __user *buf = ubuf;
	unsigned long *cpu_tsc;
	unsigned prev, next;
	int i, err;

	cpu_tsc = kmalloc(nr_cpu_ids * sizeof(long), GFP_KERNEL);
	if (!cpu_tsc)
		return -ENOMEM;

	mutex_lock(&mce_chrdev_read_mutex);

	if (!mce_apei_read_done) {
		err = __mce_read_apei(&buf, usize);
		if (err || buf != ubuf)
			goto out;
	}

	next = mce_log_get_idx_check(mcelog.next);

	/* Only supports full reads right now */
	err = -EINVAL;
	if (*off != 0 || usize < MCE_LOG_LEN*sizeof(struct mce))
		goto out;

	err = 0;
	prev = 0;
	do {
		for (i = prev; i < next; i++) {
			unsigned long start = jiffies;
			struct mce *m = &mcelog.entry[i];

			while (!m->finished) {
				if (time_after_eq(jiffies, start + 2)) {
					memset(m, 0, sizeof(*m));
					goto timeout;
				}
				cpu_relax();
			}
			smp_rmb();
			err |= copy_to_user(buf, m, sizeof(*m));
			buf += sizeof(*m);
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
		struct mce *m = &mcelog.entry[i];

		if (m->finished && m->tsc < cpu_tsc[m->cpu]) {
			err |= copy_to_user(buf, m, sizeof(*m));
			smp_rmb();
			buf += sizeof(*m);
			memset(m, 0, sizeof(*m));
		}
	}

	if (err)
		err = -EFAULT;

out:
	mutex_unlock(&mce_chrdev_read_mutex);
	kfree(cpu_tsc);

	return err ? err : buf - ubuf;
}

static unsigned int mce_chrdev_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &mce_chrdev_wait, wait);
	if (READ_ONCE(mcelog.next))
		return POLLIN | POLLRDNORM;
	if (!mce_apei_read_done && apei_check_mce())
		return POLLIN | POLLRDNORM;
	return 0;
}

static long mce_chrdev_ioctl(struct file *f, unsigned int cmd,
				unsigned long arg)
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

static ssize_t (*mce_write)(struct file *filp, const char __user *ubuf,
			    size_t usize, loff_t *off);

void register_mce_write_callback(ssize_t (*fn)(struct file *filp,
			     const char __user *ubuf,
			     size_t usize, loff_t *off))
{
	mce_write = fn;
}
EXPORT_SYMBOL_GPL(register_mce_write_callback);

static ssize_t mce_chrdev_write(struct file *filp, const char __user *ubuf,
				size_t usize, loff_t *off)
{
	if (mce_write)
		return mce_write(filp, ubuf, usize, off);
	else
		return -EINVAL;
}

static const struct file_operations mce_chrdev_ops = {
	.open			= mce_chrdev_open,
	.release		= mce_chrdev_release,
	.read			= mce_chrdev_read,
	.write			= mce_chrdev_write,
	.poll			= mce_chrdev_poll,
	.unlocked_ioctl		= mce_chrdev_ioctl,
	.llseek			= no_llseek,
};

static struct miscdevice mce_chrdev_device = {
	MISC_MCELOG_MINOR,
	"mcelog",
	&mce_chrdev_ops,
};

static void __mce_disable_bank(void *arg)
{
	int bank = *((int *)arg);
	__clear_bit(bank, this_cpu_ptr(mce_poll_banks));
	cmci_disable_bank(bank);
}

void mce_disable_bank(int bank)
{
	if (bank >= mca_cfg.banks) {
		pr_warn(FW_BUG
			"Ignoring request to disable invalid MCA bank %d.\n",
			bank);
		return;
	}
	set_bit(bank, mce_banks_ce_disabled);
	on_each_cpu(__mce_disable_bank, &bank, 1);
}

/*
 * mce=off Disables machine check
 * mce=no_cmci Disables CMCI
 * mce=no_lmce Disables LMCE
 * mce=dont_log_ce Clears corrected events silently, no log created for CEs.
 * mce=ignore_ce Disables polling and CMCI, corrected events are not cleared.
 * mce=TOLERANCELEVEL[,monarchtimeout] (number, see above)
 *	monarchtimeout is how long to wait for other CPUs on machine
 *	check, or 0 to not wait
 * mce=bootlog Log MCEs from before booting. Disabled by default on AMD.
 * mce=nobootlog Don't log MCEs from before booting.
 * mce=bios_cmci_threshold Don't program the CMCI threshold
 */
static int __init mcheck_enable(char *str)
{
	struct mca_config *cfg = &mca_cfg;

	if (*str == 0) {
		enable_p5_mce();
		return 1;
	}
	if (*str == '=')
		str++;
	if (!strcmp(str, "off"))
		cfg->disabled = true;
	else if (!strcmp(str, "no_cmci"))
		cfg->cmci_disabled = true;
	else if (!strcmp(str, "no_lmce"))
		cfg->lmce_disabled = true;
	else if (!strcmp(str, "dont_log_ce"))
		cfg->dont_log_ce = true;
	else if (!strcmp(str, "ignore_ce"))
		cfg->ignore_ce = true;
	else if (!strcmp(str, "bootlog") || !strcmp(str, "nobootlog"))
		cfg->bootlog = (str[0] == 'b');
	else if (!strcmp(str, "bios_cmci_threshold"))
		cfg->bios_cmci_threshold = true;
	else if (isdigit(str[0])) {
		if (get_option(&str, &cfg->tolerant) == 2)
			get_option(&str, &(cfg->monarch_timeout));
	} else {
		pr_info("mce argument %s ignored. Please use /sys\n", str);
		return 0;
	}
	return 1;
}
__setup("mce", mcheck_enable);

int __init mcheck_init(void)
{
	mcheck_intel_therm_init();
	mce_register_decode_chain(&mce_srao_nb);
	mcheck_vendor_init_severity();

	INIT_WORK(&mce_work, mce_process_work);
	init_irq_work(&mce_irq_work, mce_irq_work_cb);

	return 0;
}

/*
 * mce_syscore: PM support
 */

/*
 * Disable machine checks on suspend and shutdown. We can't really handle
 * them later.
 */
static int mce_disable_error_reporting(void)
{
	int i;

	for (i = 0; i < mca_cfg.banks; i++) {
		struct mce_bank *b = &mce_banks[i];

		if (b->init)
			wrmsrl(MSR_IA32_MCx_CTL(i), 0);
	}
	return 0;
}

static int mce_syscore_suspend(void)
{
	return mce_disable_error_reporting();
}

static void mce_syscore_shutdown(void)
{
	mce_disable_error_reporting();
}

/*
 * On resume clear all MCE state. Don't want to see leftovers from the BIOS.
 * Only one CPU is active at this time, the others get re-added later using
 * CPU hotplug:
 */
static void mce_syscore_resume(void)
{
	__mcheck_cpu_init_generic();
	__mcheck_cpu_init_vendor(raw_cpu_ptr(&cpu_info));
}

static struct syscore_ops mce_syscore_ops = {
	.suspend	= mce_syscore_suspend,
	.shutdown	= mce_syscore_shutdown,
	.resume		= mce_syscore_resume,
};

/*
 * mce_device: Sysfs support
 */

static void mce_cpu_restart(void *data)
{
	if (!mce_available(raw_cpu_ptr(&cpu_info)))
		return;
	__mcheck_cpu_init_generic();
	__mcheck_cpu_init_timer();
}

/* Reinit MCEs after user configuration changes */
static void mce_restart(void)
{
	mce_timer_delete_all();
	on_each_cpu(mce_cpu_restart, NULL, 1);
}

/* Toggle features for corrected errors */
static void mce_disable_cmci(void *data)
{
	if (!mce_available(raw_cpu_ptr(&cpu_info)))
		return;
	cmci_clear();
}

static void mce_enable_ce(void *all)
{
	if (!mce_available(raw_cpu_ptr(&cpu_info)))
		return;
	cmci_reenable();
	cmci_recheck();
	if (all)
		__mcheck_cpu_init_timer();
}

static struct bus_type mce_subsys = {
	.name		= "machinecheck",
	.dev_name	= "machinecheck",
};

DEFINE_PER_CPU(struct device *, mce_device);

void (*threshold_cpu_callback)(unsigned long action, unsigned int cpu);

static inline struct mce_bank *attr_to_bank(struct device_attribute *attr)
{
	return container_of(attr, struct mce_bank, attr);
}

static ssize_t show_bank(struct device *s, struct device_attribute *attr,
			 char *buf)
{
	return sprintf(buf, "%llx\n", attr_to_bank(attr)->ctl);
}

static ssize_t set_bank(struct device *s, struct device_attribute *attr,
			const char *buf, size_t size)
{
	u64 new;

	if (kstrtou64(buf, 0, &new) < 0)
		return -EINVAL;

	attr_to_bank(attr)->ctl = new;
	mce_restart();

	return size;
}

static ssize_t
show_trigger(struct device *s, struct device_attribute *attr, char *buf)
{
	strcpy(buf, mce_helper);
	strcat(buf, "\n");
	return strlen(mce_helper) + 1;
}

static ssize_t set_trigger(struct device *s, struct device_attribute *attr,
				const char *buf, size_t siz)
{
	char *p;

	strncpy(mce_helper, buf, sizeof(mce_helper));
	mce_helper[sizeof(mce_helper)-1] = 0;
	p = strchr(mce_helper, '\n');

	if (p)
		*p = 0;

	return strlen(mce_helper) + !!p;
}

static ssize_t set_ignore_ce(struct device *s,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	u64 new;

	if (kstrtou64(buf, 0, &new) < 0)
		return -EINVAL;

	if (mca_cfg.ignore_ce ^ !!new) {
		if (new) {
			/* disable ce features */
			mce_timer_delete_all();
			on_each_cpu(mce_disable_cmci, NULL, 1);
			mca_cfg.ignore_ce = true;
		} else {
			/* enable ce features */
			mca_cfg.ignore_ce = false;
			on_each_cpu(mce_enable_ce, (void *)1, 1);
		}
	}
	return size;
}

static ssize_t set_cmci_disabled(struct device *s,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	u64 new;

	if (kstrtou64(buf, 0, &new) < 0)
		return -EINVAL;

	if (mca_cfg.cmci_disabled ^ !!new) {
		if (new) {
			/* disable cmci */
			on_each_cpu(mce_disable_cmci, NULL, 1);
			mca_cfg.cmci_disabled = true;
		} else {
			/* enable cmci */
			mca_cfg.cmci_disabled = false;
			on_each_cpu(mce_enable_ce, NULL, 1);
		}
	}
	return size;
}

static ssize_t store_int_with_restart(struct device *s,
				      struct device_attribute *attr,
				      const char *buf, size_t size)
{
	ssize_t ret = device_store_int(s, attr, buf, size);
	mce_restart();
	return ret;
}

static DEVICE_ATTR(trigger, 0644, show_trigger, set_trigger);
static DEVICE_INT_ATTR(tolerant, 0644, mca_cfg.tolerant);
static DEVICE_INT_ATTR(monarch_timeout, 0644, mca_cfg.monarch_timeout);
static DEVICE_BOOL_ATTR(dont_log_ce, 0644, mca_cfg.dont_log_ce);

static struct dev_ext_attribute dev_attr_check_interval = {
	__ATTR(check_interval, 0644, device_show_int, store_int_with_restart),
	&check_interval
};

static struct dev_ext_attribute dev_attr_ignore_ce = {
	__ATTR(ignore_ce, 0644, device_show_bool, set_ignore_ce),
	&mca_cfg.ignore_ce
};

static struct dev_ext_attribute dev_attr_cmci_disabled = {
	__ATTR(cmci_disabled, 0644, device_show_bool, set_cmci_disabled),
	&mca_cfg.cmci_disabled
};

static struct device_attribute *mce_device_attrs[] = {
	&dev_attr_tolerant.attr,
	&dev_attr_check_interval.attr,
	&dev_attr_trigger,
	&dev_attr_monarch_timeout.attr,
	&dev_attr_dont_log_ce.attr,
	&dev_attr_ignore_ce.attr,
	&dev_attr_cmci_disabled.attr,
	NULL
};

static cpumask_var_t mce_device_initialized;

static void mce_device_release(struct device *dev)
{
	kfree(dev);
}

/* Per cpu device init. All of the cpus still share the same ctrl bank: */
static int mce_device_create(unsigned int cpu)
{
	struct device *dev;
	int err;
	int i, j;

	if (!mce_available(&boot_cpu_data))
		return -EIO;

	dev = kzalloc(sizeof *dev, GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->id  = cpu;
	dev->bus = &mce_subsys;
	dev->release = &mce_device_release;

	err = device_register(dev);
	if (err) {
		put_device(dev);
		return err;
	}

	for (i = 0; mce_device_attrs[i]; i++) {
		err = device_create_file(dev, mce_device_attrs[i]);
		if (err)
			goto error;
	}
	for (j = 0; j < mca_cfg.banks; j++) {
		err = device_create_file(dev, &mce_banks[j].attr);
		if (err)
			goto error2;
	}
	cpumask_set_cpu(cpu, mce_device_initialized);
	per_cpu(mce_device, cpu) = dev;

	return 0;
error2:
	while (--j >= 0)
		device_remove_file(dev, &mce_banks[j].attr);
error:
	while (--i >= 0)
		device_remove_file(dev, mce_device_attrs[i]);

	device_unregister(dev);

	return err;
}

static void mce_device_remove(unsigned int cpu)
{
	struct device *dev = per_cpu(mce_device, cpu);
	int i;

	if (!cpumask_test_cpu(cpu, mce_device_initialized))
		return;

	for (i = 0; mce_device_attrs[i]; i++)
		device_remove_file(dev, mce_device_attrs[i]);

	for (i = 0; i < mca_cfg.banks; i++)
		device_remove_file(dev, &mce_banks[i].attr);

	device_unregister(dev);
	cpumask_clear_cpu(cpu, mce_device_initialized);
	per_cpu(mce_device, cpu) = NULL;
}

/* Make sure there are no machine checks on offlined CPUs. */
static void mce_disable_cpu(void *h)
{
	unsigned long action = *(unsigned long *)h;
	int i;

	if (!mce_available(raw_cpu_ptr(&cpu_info)))
		return;

	if (!(action & CPU_TASKS_FROZEN))
		cmci_clear();
	for (i = 0; i < mca_cfg.banks; i++) {
		struct mce_bank *b = &mce_banks[i];

		if (b->init)
			wrmsrl(MSR_IA32_MCx_CTL(i), 0);
	}
}

static void mce_reenable_cpu(void *h)
{
	unsigned long action = *(unsigned long *)h;
	int i;

	if (!mce_available(raw_cpu_ptr(&cpu_info)))
		return;

	if (!(action & CPU_TASKS_FROZEN))
		cmci_reenable();
	for (i = 0; i < mca_cfg.banks; i++) {
		struct mce_bank *b = &mce_banks[i];

		if (b->init)
			wrmsrl(MSR_IA32_MCx_CTL(i), b->ctl);
	}
}

/* Get notified when a cpu comes on/off. Be hotplug friendly. */
static int
mce_cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;
	struct timer_list *t = &per_cpu(mce_timer, cpu);

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		mce_device_create(cpu);
		if (threshold_cpu_callback)
			threshold_cpu_callback(action, cpu);
		break;
	case CPU_DEAD:
		if (threshold_cpu_callback)
			threshold_cpu_callback(action, cpu);
		mce_device_remove(cpu);
		mce_intel_hcpu_update(cpu);

		/* intentionally ignoring frozen here */
		if (!(action & CPU_TASKS_FROZEN))
			cmci_rediscover();
		break;
	case CPU_DOWN_PREPARE:
		smp_call_function_single(cpu, mce_disable_cpu, &action, 1);
		del_timer_sync(t);
		break;
	case CPU_DOWN_FAILED:
		smp_call_function_single(cpu, mce_reenable_cpu, &action, 1);
		mce_start_timer(cpu, t);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block mce_cpu_notifier = {
	.notifier_call = mce_cpu_callback,
};

static __init void mce_init_banks(void)
{
	int i;

	for (i = 0; i < mca_cfg.banks; i++) {
		struct mce_bank *b = &mce_banks[i];
		struct device_attribute *a = &b->attr;

		sysfs_attr_init(&a->attr);
		a->attr.name	= b->attrname;
		snprintf(b->attrname, ATTR_LEN, "bank%d", i);

		a->attr.mode	= 0644;
		a->show		= show_bank;
		a->store	= set_bank;
	}
}

static __init int mcheck_init_device(void)
{
	int err;
	int i = 0;

	if (!mce_available(&boot_cpu_data)) {
		err = -EIO;
		goto err_out;
	}

	if (!zalloc_cpumask_var(&mce_device_initialized, GFP_KERNEL)) {
		err = -ENOMEM;
		goto err_out;
	}

	mce_init_banks();

	err = subsys_system_register(&mce_subsys, NULL);
	if (err)
		goto err_out_mem;

	cpu_notifier_register_begin();
	for_each_online_cpu(i) {
		err = mce_device_create(i);
		if (err) {
			/*
			 * Register notifier anyway (and do not unreg it) so
			 * that we don't leave undeleted timers, see notifier
			 * callback above.
			 */
			__register_hotcpu_notifier(&mce_cpu_notifier);
			cpu_notifier_register_done();
			goto err_device_create;
		}
	}

	__register_hotcpu_notifier(&mce_cpu_notifier);
	cpu_notifier_register_done();

	register_syscore_ops(&mce_syscore_ops);

	/* register character device /dev/mcelog */
	err = misc_register(&mce_chrdev_device);
	if (err)
		goto err_register;

	return 0;

err_register:
	unregister_syscore_ops(&mce_syscore_ops);

err_device_create:
	/*
	 * We didn't keep track of which devices were created above, but
	 * even if we had, the set of online cpus might have changed.
	 * Play safe and remove for every possible cpu, since
	 * mce_device_remove() will do the right thing.
	 */
	for_each_possible_cpu(i)
		mce_device_remove(i);

err_out_mem:
	free_cpumask_var(mce_device_initialized);

err_out:
	pr_err("Unable to init device /dev/mcelog (rc: %d)\n", err);

	return err;
}
device_initcall_sync(mcheck_init_device);

/*
 * Old style boot options parsing. Only for compatibility.
 */
static int __init mcheck_disable(char *str)
{
	mca_cfg.disabled = true;
	return 1;
}
__setup("nomce", mcheck_disable);

#ifdef CONFIG_DEBUG_FS
struct dentry *mce_get_debugfs_dir(void)
{
	static struct dentry *dmce;

	if (!dmce)
		dmce = debugfs_create_dir("mce", NULL);

	return dmce;
}

static void mce_reset(void)
{
	cpu_missing = 0;
	atomic_set(&mce_fake_panicked, 0);
	atomic_set(&mce_executing, 0);
	atomic_set(&mce_callin, 0);
	atomic_set(&global_nwo, 0);
}

static int fake_panic_get(void *data, u64 *val)
{
	*val = fake_panic;
	return 0;
}

static int fake_panic_set(void *data, u64 val)
{
	mce_reset();
	fake_panic = val;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(fake_panic_fops, fake_panic_get,
			fake_panic_set, "%llu\n");

static int __init mcheck_debugfs_init(void)
{
	struct dentry *dmce, *ffake_panic;

	dmce = mce_get_debugfs_dir();
	if (!dmce)
		return -ENOMEM;
	ffake_panic = debugfs_create_file("fake_panic", 0444, dmce, NULL,
					  &fake_panic_fops);
	if (!ffake_panic)
		return -ENOMEM;

	return 0;
}
#else
static int __init mcheck_debugfs_init(void) { return -EINVAL; }
#endif

static int __init mcheck_late_init(void)
{
	mcheck_debugfs_init();

	/*
	 * Flush out everything that has been logged during early boot, now that
	 * everything has been initialized (workqueues, decoders, ...).
	 */
	mce_schedule_work();

	return 0;
}
late_initcall(mcheck_late_init);
