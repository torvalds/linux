/*
 * File:         arch/blackfin/kernel/process.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  Blackfin architecture-dependent process handling.
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/smp_lock.h>
#include <linux/unistd.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/err.h>

#include <asm/blackfin.h>
#include <asm/fixed_code.h>

#define	LED_ON	0
#define	LED_OFF	1

asmlinkage void ret_from_fork(void);

/* Points to the SDRAM backup memory for the stack that is currently in
 * L1 scratchpad memory.
 */
void *current_l1_stack_save;

/* The number of tasks currently using a L1 stack area.  The SRAM is
 * allocated/deallocated whenever this changes from/to zero.
 */
int nr_l1stack_tasks;

/* Start and length of the area in L1 scratchpad memory which we've allocated
 * for process stacks.
 */
void *l1_stack_base;
unsigned long l1_stack_len;

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void) = NULL;
EXPORT_SYMBOL(pm_idle);

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);

/*
 * We are using a different LED from the one used to indicate timer interrupt.
 */
#if defined(CONFIG_BFIN_IDLE_LED)
static inline void leds_switch(int flag)
{
	unsigned short tmp = 0;

	tmp = bfin_read_CONFIG_BFIN_IDLE_LED_PORT();
	SSYNC();

	if (flag == LED_ON)
		tmp &= ~CONFIG_BFIN_IDLE_LED_PIN;	/* light on */
	else
		tmp |= CONFIG_BFIN_IDLE_LED_PIN;	/* light off */

	bfin_write_CONFIG_BFIN_IDLE_LED_PORT(tmp);
	SSYNC();

}
#else
static inline void leds_switch(int flag)
{
}
#endif

/*
 * The idle loop on BFIN
 */
#ifdef CONFIG_IDLE_L1
void default_idle(void)__attribute__((l1_text));
void cpu_idle(void)__attribute__((l1_text));
#endif

void default_idle(void)
{
	while (!need_resched()) {
		leds_switch(LED_OFF);
		local_irq_disable();
		if (likely(!need_resched()))
			idle_with_irq_disabled();
		local_irq_enable();
		leds_switch(LED_ON);
	}
}

void (*idle)(void) = default_idle;

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		idle();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

/* Fill in the fpu structure for a core dump.  */

int dump_fpu(struct pt_regs *regs, elf_fpregset_t * fpregs)
{
	return 1;
}

/*
 * This gets run with P1 containing the
 * function to call, and R1 containing
 * the "args".  Note P0 is clobbered on the way here.
 */
void kernel_thread_helper(void);
__asm__(".section .text\n"
	".align 4\n"
	"_kernel_thread_helper:\n\t"
	"\tsp += -12;\n\t"
	"\tr0 = r1;\n\t" "\tcall (p1);\n\t" "\tcall _do_exit;\n" ".previous");

/*
 * Create a kernel thread.
 */
pid_t kernel_thread(int (*fn) (void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.r1 = (unsigned long)arg;
	regs.p1 = (unsigned long)fn;
	regs.pc = (unsigned long)kernel_thread_helper;
	regs.orig_p0 = -1;
	/* Set bit 2 to tell ret_from_fork we should be returning to kernel
	   mode.  */
	regs.ipend = 0x8002;
	__asm__ __volatile__("%0 = syscfg;":"=da"(regs.syscfg):);
	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, 0, &regs, 0, NULL,
		       NULL);
}

void flush_thread(void)
{
}

asmlinkage int bfin_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, rdusp(), regs, 0, NULL,
		       NULL);
}

asmlinkage int bfin_clone(struct pt_regs *regs)
{
	unsigned long clone_flags;
	unsigned long newsp;

	/* syscall2 puts clone_flags in r0 and usp in r1 */
	clone_flags = regs->r0;
	newsp = regs->r1;
	if (!newsp)
		newsp = rdusp();
	else
		newsp -= 12;
	return do_fork(clone_flags, newsp, regs, 0, NULL, NULL);
}

int
copy_thread(int nr, unsigned long clone_flags,
	    unsigned long usp, unsigned long topstk,
	    struct task_struct *p, struct pt_regs *regs)
{
	struct pt_regs *childregs;

	childregs = (struct pt_regs *) (task_stack_page(p) + THREAD_SIZE) - 1;
	*childregs = *regs;
	childregs->r0 = 0;

	p->thread.usp = usp;
	p->thread.ksp = (unsigned long)childregs;
	p->thread.pc = (unsigned long)ret_from_fork;

	return 0;
}

/*
 * sys_execve() executes a new program.
 */

asmlinkage int sys_execve(char *name, char **argv, char **envp)
{
	int error;
	char *filename;
	struct pt_regs *regs = (struct pt_regs *)((&name) + 6);

	lock_kernel();
	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
 out:
	unlock_kernel();
	return error;
}

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	stack_page = (unsigned long)p;
	fp = p->thread.usp;
	do {
		if (fp < stack_page + sizeof(struct thread_info) ||
		    fp >= 8184 + stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *)fp;
	}
	while (count++ < 16);
	return 0;
}

void finish_atomic_sections (struct pt_regs *regs)
{
	if (regs->pc < ATOMIC_SEQS_START || regs->pc >= ATOMIC_SEQS_END)
		return;

	switch (regs->pc) {
	case ATOMIC_XCHG32 + 2:
		put_user(regs->r1, (int *)regs->p0);
		regs->pc += 2;
		break;

	case ATOMIC_CAS32 + 2:
	case ATOMIC_CAS32 + 4:
		if (regs->r0 == regs->r1)
			put_user(regs->r2, (int *)regs->p0);
		regs->pc = ATOMIC_CAS32 + 8;
		break;
	case ATOMIC_CAS32 + 6:
		put_user(regs->r2, (int *)regs->p0);
		regs->pc += 2;
		break;

	case ATOMIC_ADD32 + 2:
		regs->r0 = regs->r1 + regs->r0;
		/* fall through */
	case ATOMIC_ADD32 + 4:
		put_user(regs->r0, (int *)regs->p0);
		regs->pc = ATOMIC_ADD32 + 6;
		break;

	case ATOMIC_SUB32 + 2:
		regs->r0 = regs->r1 - regs->r0;
		/* fall through */
	case ATOMIC_SUB32 + 4:
		put_user(regs->r0, (int *)regs->p0);
		regs->pc = ATOMIC_SUB32 + 6;
		break;

	case ATOMIC_IOR32 + 2:
		regs->r0 = regs->r1 | regs->r0;
		/* fall through */
	case ATOMIC_IOR32 + 4:
		put_user(regs->r0, (int *)regs->p0);
		regs->pc = ATOMIC_IOR32 + 6;
		break;

	case ATOMIC_AND32 + 2:
		regs->r0 = regs->r1 & regs->r0;
		/* fall through */
	case ATOMIC_AND32 + 4:
		put_user(regs->r0, (int *)regs->p0);
		regs->pc = ATOMIC_AND32 + 6;
		break;

	case ATOMIC_XOR32 + 2:
		regs->r0 = regs->r1 ^ regs->r0;
		/* fall through */
	case ATOMIC_XOR32 + 4:
		put_user(regs->r0, (int *)regs->p0);
		regs->pc = ATOMIC_XOR32 + 6;
		break;
	}
}

#if defined(CONFIG_ACCESS_CHECK)
int _access_ok(unsigned long addr, unsigned long size)
{
	if (size == 0)
		return 1;
	if (addr > (addr + size))
		return 0;
	if (segment_eq(get_fs(), KERNEL_DS))
		return 1;
#ifdef CONFIG_MTD_UCLINUX
	if (addr >= memory_start && (addr + size) <= memory_end)
		return 1;
	if (addr >= memory_mtd_end && (addr + size) <= physical_mem_end)
		return 1;
#else
	if (addr >= memory_start && (addr + size) <= physical_mem_end)
		return 1;
#endif
	if (addr >= (unsigned long)__init_begin &&
	    addr + size <= (unsigned long)__init_end)
		return 1;
	if (addr >= L1_SCRATCH_START
	    && addr + size <= L1_SCRATCH_START + L1_SCRATCH_LENGTH)
		return 1;
#if L1_CODE_LENGTH != 0
	if (addr >= L1_CODE_START + (_etext_l1 - _stext_l1)
	    && addr + size <= L1_CODE_START + L1_CODE_LENGTH)
		return 1;
#endif
#if L1_DATA_A_LENGTH != 0
	if (addr >= L1_DATA_A_START + (_ebss_l1 - _sdata_l1)
	    && addr + size <= L1_DATA_A_START + L1_DATA_A_LENGTH)
		return 1;
#endif
#if L1_DATA_B_LENGTH != 0
	if (addr >= L1_DATA_B_START
	    && addr + size <= L1_DATA_B_START + L1_DATA_B_LENGTH)
		return 1;
#endif
	return 0;
}
EXPORT_SYMBOL(_access_ok);
#endif /* CONFIG_ACCESS_CHECK */
