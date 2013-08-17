/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>

/*
 * We support CPUs that have performance counters like the Pentium Pro
 * with the NMI mode driver.
 */

#ifdef CONFIG_X86_LOCAL_APIC
extern int op_nmi_init(struct oprofile_operations *ops);
extern void op_nmi_exit(void);
#else
static int op_nmi_init(struct oprofile_operations *ops) { return -ENODEV; }
static void op_nmi_exit(void) { }
#endif

extern void x86_backtrace(struct pt_regs * const regs, unsigned int depth);

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = x86_backtrace;
	return op_nmi_init(ops);
}

void oprofile_arch_exit(void)
{
	op_nmi_exit();
}
