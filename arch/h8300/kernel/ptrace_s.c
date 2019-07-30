/*
 *  linux/arch/h8300/kernel/ptrace_h8s.c
 *    ptrace cpu depend helper functions
 *
 *  Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/linkage.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <asm/ptrace.h>

#define CCR_MASK  0x6f
#define EXR_TRACE 0x80

/* disable singlestep */
void user_disable_single_step(struct task_struct *child)
{
	unsigned char exr;

	exr = h8300_get_reg(child, PT_EXR);
	exr &= ~EXR_TRACE;
	h8300_put_reg(child, PT_EXR, exr);
}

/* enable singlestep */
void user_enable_single_step(struct task_struct *child)
{
	unsigned char exr;

	exr = h8300_get_reg(child, PT_EXR);
	exr |= EXR_TRACE;
	h8300_put_reg(child, PT_EXR, exr);
}

asmlinkage void trace_trap(unsigned long bp)
{
	(void)bp;
	force_sig(SIGTRAP);
}
