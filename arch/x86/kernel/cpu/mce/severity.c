// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCE grading rules.
 * Copyright 2008, 2009 Intel Corporation.
 *
 * Author: Andi Kleen
 */
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <asm/mce.h>
#include <asm/intel-family.h>

#include "internal.h"

/*
 * Grade an mce by severity. In general the most severe ones are processed
 * first. Since there are quite a lot of combinations test the bits in a
 * table-driven way. The rules are simply processed in order, first
 * match wins.
 *
 * Note this is only used for machine check exceptions, the corrected
 * errors use much simpler rules. The exceptions still check for the corrected
 * errors, but only to leave them alone for the CMCI handler (except for
 * panic situations)
 */

enum context { IN_KERNEL = 1, IN_USER = 2, IN_KERNEL_RECOV = 3 };
enum ser { SER_REQUIRED = 1, NO_SER = 2 };
enum exception { EXCP_CONTEXT = 1, NO_EXCP = 2 };

static struct severity {
	u64 mask;
	u64 result;
	unsigned char sev;
	unsigned char mcgmask;
	unsigned char mcgres;
	unsigned char ser;
	unsigned char context;
	unsigned char excp;
	unsigned char covered;
	unsigned char cpu_model;
	unsigned char cpu_minstepping;
	unsigned char bank_lo, bank_hi;
	char *msg;
} severities[] = {
#define MCESEV(s, m, c...) { .sev = MCE_ ## s ## _SEVERITY, .msg = m, ## c }
#define BANK_RANGE(l, h) .bank_lo = l, .bank_hi = h
#define MODEL_STEPPING(m, s) .cpu_model = m, .cpu_minstepping = s
#define  KERNEL		.context = IN_KERNEL
#define  USER		.context = IN_USER
#define  KERNEL_RECOV	.context = IN_KERNEL_RECOV
#define  SER		.ser = SER_REQUIRED
#define  NOSER		.ser = NO_SER
#define  EXCP		.excp = EXCP_CONTEXT
#define  NOEXCP		.excp = NO_EXCP
#define  BITCLR(x)	.mask = x, .result = 0
#define  BITSET(x)	.mask = x, .result = x
#define  MCGMASK(x, y)	.mcgmask = x, .mcgres = y
#define  MASK(x, y)	.mask = x, .result = y
#define MCI_UC_S (MCI_STATUS_UC|MCI_STATUS_S)
#define MCI_UC_AR (MCI_STATUS_UC|MCI_STATUS_AR)
#define MCI_UC_SAR (MCI_STATUS_UC|MCI_STATUS_S|MCI_STATUS_AR)
#define	MCI_ADDR (MCI_STATUS_ADDRV|MCI_STATUS_MISCV)

	MCESEV(
		NO, "Invalid",
		BITCLR(MCI_STATUS_VAL)
		),
	MCESEV(
		NO, "Not enabled",
		EXCP, BITCLR(MCI_STATUS_EN)
		),
	MCESEV(
		PANIC, "Processor context corrupt",
		BITSET(MCI_STATUS_PCC)
		),
	/* When MCIP is not set something is very confused */
	MCESEV(
		PANIC, "MCIP not set in MCA handler",
		EXCP, MCGMASK(MCG_STATUS_MCIP, 0)
		),
	/* Neither return not error IP -- no chance to recover -> PANIC */
	MCESEV(
		PANIC, "Neither restart nor error IP",
		EXCP, MCGMASK(MCG_STATUS_RIPV|MCG_STATUS_EIPV, 0)
		),
	MCESEV(
		PANIC, "In kernel and no restart IP",
		EXCP, KERNEL, MCGMASK(MCG_STATUS_RIPV, 0)
		),
	MCESEV(
		PANIC, "In kernel and no restart IP",
		EXCP, KERNEL_RECOV, MCGMASK(MCG_STATUS_RIPV, 0)
		),
	MCESEV(
		KEEP, "Corrected error",
		NOSER, BITCLR(MCI_STATUS_UC)
		),
	/*
	 * known AO MCACODs reported via MCE or CMC:
	 *
	 * SRAO could be signaled either via a machine check exception or
	 * CMCI with the corresponding bit S 1 or 0. So we don't need to
	 * check bit S for SRAO.
	 */
	MCESEV(
		AO, "Action optional: memory scrubbing error",
		SER, MASK(MCI_UC_AR|MCACOD_SCRUBMSK, MCI_STATUS_UC|MCACOD_SCRUB)
		),
	MCESEV(
		AO, "Action optional: last level cache writeback error",
		SER, MASK(MCI_UC_AR|MCACOD, MCI_STATUS_UC|MCACOD_L3WB)
		),
	/*
	 * Quirk for Skylake/Cascade Lake. Patrol scrubber may be configured
	 * to report uncorrected errors using CMCI with a special signature.
	 * UC=0, MSCOD=0x0010, MCACOD=binary(000X 0000 1100 XXXX) reported
	 * in one of the memory controller banks.
	 * Set severity to "AO" for same action as normal patrol scrub error.
	 */
	MCESEV(
		AO, "Uncorrected Patrol Scrub Error",
		SER, MASK(MCI_STATUS_UC|MCI_ADDR|0xffffeff0, MCI_ADDR|0x001000c0),
		MODEL_STEPPING(INTEL_FAM6_SKYLAKE_X, 4), BANK_RANGE(13, 18)
	),

	/* ignore OVER for UCNA */
	MCESEV(
		UCNA, "Uncorrected no action required",
		SER, MASK(MCI_UC_SAR, MCI_STATUS_UC)
		),
	MCESEV(
		PANIC, "Illegal combination (UCNA with AR=1)",
		SER,
		MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_STATUS_UC|MCI_STATUS_AR)
		),
	MCESEV(
		KEEP, "Non signalled machine check",
		SER, BITCLR(MCI_STATUS_S)
		),

	MCESEV(
		PANIC, "Action required with lost events",
		SER, BITSET(MCI_STATUS_OVER|MCI_UC_SAR)
		),

	/* known AR MCACODs: */
#ifdef	CONFIG_MEMORY_FAILURE
	MCESEV(
		KEEP, "Action required but unaffected thread is continuable",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR, MCI_UC_SAR|MCI_ADDR),
		MCGMASK(MCG_STATUS_RIPV|MCG_STATUS_EIPV, MCG_STATUS_RIPV)
		),
	MCESEV(
		AR, "Action required: data load in error recoverable area of kernel",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_DATA),
		KERNEL_RECOV
		),
	MCESEV(
		AR, "Action required: data load error in a user process",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_DATA),
		USER
		),
	MCESEV(
		AR, "Action required: instruction fetch error in a user process",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_INSTR),
		USER
		),
	MCESEV(
		PANIC, "Data load in unrecoverable area of kernel",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_DATA),
		KERNEL
		),
	MCESEV(
		PANIC, "Instruction fetch error in kernel",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_INSTR),
		KERNEL
		),
#endif
	MCESEV(
		PANIC, "Action required: unknown MCACOD",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_UC_SAR)
		),

	MCESEV(
		SOME, "Action optional: unknown MCACOD",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_UC_S)
		),
	MCESEV(
		SOME, "Action optional with lost events",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR, MCI_STATUS_OVER|MCI_UC_S)
		),

	MCESEV(
		PANIC, "Overflowed uncorrected",
		BITSET(MCI_STATUS_OVER|MCI_STATUS_UC)
		),
	MCESEV(
		UC, "Uncorrected",
		BITSET(MCI_STATUS_UC)
		),
	MCESEV(
		SOME, "No match",
		BITSET(0)
		)	/* always matches. keep at end */
};

#define mc_recoverable(mcg) (((mcg) & (MCG_STATUS_RIPV|MCG_STATUS_EIPV)) == \
				(MCG_STATUS_RIPV|MCG_STATUS_EIPV))

/*
 * If mcgstatus indicated that ip/cs on the stack were
 * no good, then "m->cs" will be zero and we will have
 * to assume the worst case (IN_KERNEL) as we actually
 * have no idea what we were executing when the machine
 * check hit.
 * If we do have a good "m->cs" (or a faked one in the
 * case we were executing in VM86 mode) we can use it to
 * distinguish an exception taken in user from from one
 * taken in the kernel.
 */
static int error_context(struct mce *m, struct pt_regs *regs)
{
	enum handler_type t;

	if ((m->cs & 3) == 3)
		return IN_USER;

	t = ex_get_fault_handler_type(m->ip);
	if (mc_recoverable(m->mcgstatus) && t == EX_HANDLER_FAULT) {
		m->kflags |= MCE_IN_KERNEL_RECOV;
		return IN_KERNEL_RECOV;
	}

	return IN_KERNEL;
}

static int mce_severity_amd_smca(struct mce *m, enum context err_ctx)
{
	u32 addr = MSR_AMD64_SMCA_MCx_CONFIG(m->bank);
	u32 low, high;

	/*
	 * We need to look at the following bits:
	 * - "succor" bit (data poisoning support), and
	 * - TCC bit (Task Context Corrupt)
	 * in MCi_STATUS to determine error severity.
	 */
	if (!mce_flags.succor)
		return MCE_PANIC_SEVERITY;

	if (rdmsr_safe(addr, &low, &high))
		return MCE_PANIC_SEVERITY;

	/* TCC (Task context corrupt). If set and if IN_KERNEL, panic. */
	if ((low & MCI_CONFIG_MCAX) &&
	    (m->status & MCI_STATUS_TCC) &&
	    (err_ctx == IN_KERNEL))
		return MCE_PANIC_SEVERITY;

	 /* ...otherwise invoke hwpoison handler. */
	return MCE_AR_SEVERITY;
}

/*
 * See AMD Error Scope Hierarchy table in a newer BKDG. For example
 * 49125_15h_Models_30h-3Fh_BKDG.pdf, section "RAS Features"
 */
static int mce_severity_amd(struct mce *m, struct pt_regs *regs, int tolerant,
			    char **msg, bool is_excp)
{
	enum context ctx = error_context(m, regs);

	/* Processor Context Corrupt, no need to fumble too much, die! */
	if (m->status & MCI_STATUS_PCC)
		return MCE_PANIC_SEVERITY;

	if (m->status & MCI_STATUS_UC) {

		if (ctx == IN_KERNEL)
			return MCE_PANIC_SEVERITY;

		/*
		 * On older systems where overflow_recov flag is not present, we
		 * should simply panic if an error overflow occurs. If
		 * overflow_recov flag is present and set, then software can try
		 * to at least kill process to prolong system operation.
		 */
		if (mce_flags.overflow_recov) {
			if (mce_flags.smca)
				return mce_severity_amd_smca(m, ctx);

			/* kill current process */
			return MCE_AR_SEVERITY;
		} else {
			/* at least one error was not logged */
			if (m->status & MCI_STATUS_OVER)
				return MCE_PANIC_SEVERITY;
		}

		/*
		 * For any other case, return MCE_UC_SEVERITY so that we log the
		 * error and exit #MC handler.
		 */
		return MCE_UC_SEVERITY;
	}

	/*
	 * deferred error: poll handler catches these and adds to mce_ring so
	 * memory-failure can take recovery actions.
	 */
	if (m->status & MCI_STATUS_DEFERRED)
		return MCE_DEFERRED_SEVERITY;

	/*
	 * corrected error: poll handler catches these and passes responsibility
	 * of decoding the error to EDAC
	 */
	return MCE_KEEP_SEVERITY;
}

static int mce_severity_intel(struct mce *m, struct pt_regs *regs,
			      int tolerant, char **msg, bool is_excp)
{
	enum exception excp = (is_excp ? EXCP_CONTEXT : NO_EXCP);
	enum context ctx = error_context(m, regs);
	struct severity *s;

	for (s = severities;; s++) {
		if ((m->status & s->mask) != s->result)
			continue;
		if ((m->mcgstatus & s->mcgmask) != s->mcgres)
			continue;
		if (s->ser == SER_REQUIRED && !mca_cfg.ser)
			continue;
		if (s->ser == NO_SER && mca_cfg.ser)
			continue;
		if (s->context && ctx != s->context)
			continue;
		if (s->excp && excp != s->excp)
			continue;
		if (s->cpu_model && boot_cpu_data.x86_model != s->cpu_model)
			continue;
		if (s->cpu_minstepping && boot_cpu_data.x86_stepping < s->cpu_minstepping)
			continue;
		if (s->bank_lo && (m->bank < s->bank_lo || m->bank > s->bank_hi))
			continue;
		if (msg)
			*msg = s->msg;
		s->covered = 1;
		if (s->sev >= MCE_UC_SEVERITY && ctx == IN_KERNEL) {
			if (tolerant < 1)
				return MCE_PANIC_SEVERITY;
		}
		return s->sev;
	}
}

/* Default to mce_severity_intel */
int (*mce_severity)(struct mce *m, struct pt_regs *regs, int tolerant, char **msg, bool is_excp) =
		    mce_severity_intel;

void __init mcheck_vendor_init_severity(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		mce_severity = mce_severity_amd;
}

#ifdef CONFIG_DEBUG_FS
static void *s_start(struct seq_file *f, loff_t *pos)
{
	if (*pos >= ARRAY_SIZE(severities))
		return NULL;
	return &severities[*pos];
}

static void *s_next(struct seq_file *f, void *data, loff_t *pos)
{
	if (++(*pos) >= ARRAY_SIZE(severities))
		return NULL;
	return &severities[*pos];
}

static void s_stop(struct seq_file *f, void *data)
{
}

static int s_show(struct seq_file *f, void *data)
{
	struct severity *ser = data;
	seq_printf(f, "%d\t%s\n", ser->covered, ser->msg);
	return 0;
}

static const struct seq_operations severities_seq_ops = {
	.start	= s_start,
	.next	= s_next,
	.stop	= s_stop,
	.show	= s_show,
};

static int severities_coverage_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &severities_seq_ops);
}

static ssize_t severities_coverage_write(struct file *file,
					 const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(severities); i++)
		severities[i].covered = 0;
	return count;
}

static const struct file_operations severities_coverage_fops = {
	.open		= severities_coverage_open,
	.release	= seq_release,
	.read		= seq_read,
	.write		= severities_coverage_write,
	.llseek		= seq_lseek,
};

static int __init severities_debugfs_init(void)
{
	struct dentry *dmce;

	dmce = mce_get_debugfs_dir();

	debugfs_create_file("severities-coverage", 0444, dmce, NULL,
			    &severities_coverage_fops);
	return 0;
}
late_initcall(severities_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
