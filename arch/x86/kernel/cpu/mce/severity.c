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
#include <asm/cpu_device_id.h>
#include <asm/traps.h>
#include <asm/insn.h>
#include <asm/insn-eval.h>

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
	unsigned short mcgmask;
	unsigned short mcgres;
	unsigned char ser;
	unsigned char context;
	unsigned char excp;
	unsigned char covered;
	unsigned int cpu_vfm;
	unsigned char cpu_minstepping;
	unsigned char bank_lo, bank_hi;
	char *msg;
} severities[] = {
#define MCESEV(s, m, c...) { .sev = MCE_ ## s ## _SEVERITY, .msg = m, ## c }
#define BANK_RANGE(l, h) .bank_lo = l, .bank_hi = h
#define VFM_STEPPING(m, s) .cpu_vfm = m, .cpu_minstepping = s
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
		VFM_STEPPING(INTEL_SKYLAKE_X, 4), BANK_RANGE(13, 18)
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
		KEEP, "Non signaled machine check",
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
		AR, "Data load error in SEAM non-root mode",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_DATA),
		MCGMASK(MCG_STATUS_SEAM_NR, MCG_STATUS_SEAM_NR),
		KERNEL
		),
	MCESEV(
		AR, "Instruction fetch error in SEAM non-root mode",
		SER, MASK(MCI_STATUS_OVER|MCI_UC_SAR|MCI_ADDR|MCACOD, MCI_UC_SAR|MCI_ADDR|MCACOD_INSTR),
		MCGMASK(MCG_STATUS_SEAM_NR, MCG_STATUS_SEAM_NR),
		KERNEL
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
		PANIC, "Uncorrected in kernel",
		BITSET(MCI_STATUS_UC),
		KERNEL
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

static bool is_copy_from_user(struct pt_regs *regs)
{
	u8 insn_buf[MAX_INSN_SIZE];
	unsigned long addr;
	struct insn insn;
	int ret;

	if (!regs)
		return false;

	if (copy_from_kernel_nofault(insn_buf, (void *)regs->ip, MAX_INSN_SIZE))
		return false;

	ret = insn_decode_kernel(&insn, insn_buf);
	if (ret < 0)
		return false;

	switch (insn.opcode.value) {
	/* MOV mem,reg */
	case 0x8A: case 0x8B:
	/* MOVZ mem,reg */
	case 0xB60F: case 0xB70F:
		addr = (unsigned long)insn_get_addr_ref(&insn, regs);
		break;
	/* REP MOVS */
	case 0xA4: case 0xA5:
		addr = regs->si;
		break;
	default:
		return false;
	}

	if (fault_in_kernel_space(addr))
		return false;

	current->mce_vaddr = (void __user *)addr;

	return true;
}

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
static noinstr int error_context(struct mce *m, struct pt_regs *regs)
{
	int fixup_type;
	bool copy_user;

	if ((m->cs & 3) == 3)
		return IN_USER;

	if (!mc_recoverable(m->mcgstatus))
		return IN_KERNEL;

	/* Allow instrumentation around external facilities usage. */
	instrumentation_begin();
	fixup_type = ex_get_fixup_type(m->ip);
	copy_user  = is_copy_from_user(regs);
	instrumentation_end();

	if (copy_user) {
		m->kflags |= MCE_IN_KERNEL_COPYIN | MCE_IN_KERNEL_RECOV;
		return IN_KERNEL_RECOV;
	}

	switch (fixup_type) {
	case EX_TYPE_FAULT_MCE_SAFE:
	case EX_TYPE_DEFAULT_MCE_SAFE:
		m->kflags |= MCE_IN_KERNEL_RECOV;
		return IN_KERNEL_RECOV;

	default:
		return IN_KERNEL;
	}
}

/* See AMD PPR(s) section Machine Check Error Handling. */
static noinstr int mce_severity_amd(struct mce *m, struct pt_regs *regs, char **msg, bool is_excp)
{
	char *panic_msg = NULL;
	int ret;

	/*
	 * Default return value: Action required, the error must be handled
	 * immediately.
	 */
	ret = MCE_AR_SEVERITY;

	/* Processor Context Corrupt, no need to fumble too much, die! */
	if (m->status & MCI_STATUS_PCC) {
		panic_msg = "Processor Context Corrupt";
		ret = MCE_PANIC_SEVERITY;
		goto out;
	}

	if (m->status & MCI_STATUS_DEFERRED) {
		ret = MCE_DEFERRED_SEVERITY;
		goto out;
	}

	/*
	 * If the UC bit is not set, the system either corrected or deferred
	 * the error. No action will be required after logging the error.
	 */
	if (!(m->status & MCI_STATUS_UC)) {
		ret = MCE_KEEP_SEVERITY;
		goto out;
	}

	/*
	 * On MCA overflow, without the MCA overflow recovery feature the
	 * system will not be able to recover, panic.
	 */
	if ((m->status & MCI_STATUS_OVER) && !mce_flags.overflow_recov) {
		panic_msg = "Overflowed uncorrected error without MCA Overflow Recovery";
		ret = MCE_PANIC_SEVERITY;
		goto out;
	}

	if (!mce_flags.succor) {
		panic_msg = "Uncorrected error without MCA Recovery";
		ret = MCE_PANIC_SEVERITY;
		goto out;
	}

	if (error_context(m, regs) == IN_KERNEL) {
		panic_msg = "Uncorrected unrecoverable error in kernel context";
		ret = MCE_PANIC_SEVERITY;
	}

out:
	if (msg && panic_msg)
		*msg = panic_msg;

	return ret;
}

static noinstr int mce_severity_intel(struct mce *m, struct pt_regs *regs, char **msg, bool is_excp)
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
		if (s->cpu_vfm && boot_cpu_data.x86_vfm != s->cpu_vfm)
			continue;
		if (s->cpu_minstepping && boot_cpu_data.x86_stepping < s->cpu_minstepping)
			continue;
		if (s->bank_lo && (m->bank < s->bank_lo || m->bank > s->bank_hi))
			continue;
		if (msg)
			*msg = s->msg;
		s->covered = 1;

		return s->sev;
	}
}

int noinstr mce_severity(struct mce *m, struct pt_regs *regs, char **msg, bool is_excp)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD ||
	    boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		return mce_severity_amd(m, regs, msg, is_excp);
	else
		return mce_severity_intel(m, regs, msg, is_excp);
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
