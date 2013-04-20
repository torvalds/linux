/**
 * @file init.c
 *
 * @remark Copyright 2008 Tensilica Inc.
 * @remark Read the file COPYING
 *
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/init.h>


extern void xtensa_backtrace(struct pt_regs *const regs, unsigned int depth);

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	ops->backtrace = xtensa_backtrace;
	return -ENODEV;
}


void oprofile_arch_exit(void)
{
}
