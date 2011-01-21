/**
 * arch/s390/oprofile/init.c
 *
 * S390 Version
 *   Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 * @remark Copyright 2002 OProfile authors
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/fs.h>

extern int oprofile_hwsampler_init(struct oprofile_operations* ops);
extern void oprofile_hwsampler_exit(void);

extern void s390_backtrace(struct pt_regs * const regs, unsigned int depth);

int __init oprofile_arch_init(struct oprofile_operations* ops)
{
	ops->backtrace = s390_backtrace;

	return oprofile_hwsampler_init(ops);
}

void oprofile_arch_exit(void)
{
	oprofile_hwsampler_exit();
}
