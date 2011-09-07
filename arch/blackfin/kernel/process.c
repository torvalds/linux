/*
 * Blackfin architecture-dependent process handling
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#include <linux/module.h>
#include <linux/unistd.h>
#include <linux/user.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/fs.h>
#include <linux/err.h>

#include <asm/blackfin.h>
#include <asm/fixed_code.h>
#include <asm/mem_map.h>

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
 * The idle loop on BFIN
 */
#ifdef CONFIG_IDLE_L1
static void default_idle(void)__attribute__((l1_text));
void cpu_idle(void)__attribute__((l1_text));
#endif

/*
 * This is our default idle handler.  We need to disable
 * interrupts here to ensure we don't miss a wakeup call.
 */
static void default_idle(void)
{
#ifdef CONFIG_IPIPE
	ipipe_suspend_domain();
#endif
	hard_local_irq_disable();
	if (!need_resched())
		idle_with_irq_disabled();

	hard_local_irq_enable();
}

/*
 * The idle thread.  We try to conserve power, while trying to keep
 * overall latency low.  The architecture specific idle is passed
 * a value to indicate the level of "idleness" of the system.
 */
void cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		void (*idle)(void) = pm_idle;

#ifdef CONFIG_HOTPLUG_CPU
		if (cpu_is_offline(smp_processor_id()))
			cpu_die();
#endif
		if (!idle)
			idle = default_idle;
		tick_nohz_stop_sched_tick(1);
		while (!need_resched())
			idle();
		tick_nohz_restart_sched_tick();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
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
EXPORT_SYMBOL(kernel_thread);

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * pass the data segment into user programs if it exists,
 * it can't hurt anything as far as I can tell
 */
void start_thread(struct pt_regs *regs, unsigned long new_ip, unsigned long new_sp)
{
	regs->pc = new_ip;
	if (current->mm)
		regs->p5 = current->mm->start_data;
#ifndef CONFIG_SMP
	task_thread_info(current)->l1_task_info.stack_start =
		(void *)current->mm->context.stack_start;
	task_thread_info(current)->l1_task_info.lowest_sp = (void *)new_sp;
	memcpy(L1_SCRATCH_TASK_INFO, &task_thread_info(current)->l1_task_info,
	       sizeof(*L1_SCRATCH_TASK_INFO));
#endif
	wrusp(new_sp);
}
EXPORT_SYMBOL_GPL(start_thread);

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

#ifdef __ARCH_SYNC_CORE_DCACHE
	if (current->rt.nr_cpus_allowed == num_possible_cpus())
		set_cpus_allowed_ptr(current, cpumask_of(smp_processor_id()));
#endif

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
copy_thread(unsigned long clone_flags,
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
asmlinkage int sys_execve(const char __user *name,
			  const char __user *const __user *argv,
			  const char __user *const __user *envp)
{
	int error;
	char *filename;
	struct pt_regs *regs = (struct pt_regs *)((&name) + 6);

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		return error;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
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
	int __user *up0 = (int __user *)regs->p0;

	switch (regs->pc) {
	default:
		/* not in middle of an atomic step, so resume like normal */
		return;

	case ATOMIC_XCHG32 + 2:
		put_user(regs->r1, up0);
		break;

	case ATOMIC_CAS32 + 2:
	case ATOMIC_CAS32 + 4:
		if (regs->r0 == regs->r1)
	case ATOMIC_CAS32 + 6:
			put_user(regs->r2, up0);
		break;

	case ATOMIC_ADD32 + 2:
		regs->r0 = regs->r1 + regs->r0;
		/* fall through */
	case ATOMIC_ADD32 + 4:
		put_user(regs->r0, up0);
		break;

	case ATOMIC_SUB32 + 2:
		regs->r0 = regs->r1 - regs->r0;
		/* fall through */
	case ATOMIC_SUB32 + 4:
		put_user(regs->r0, up0);
		break;

	case ATOMIC_IOR32 + 2:
		regs->r0 = regs->r1 | regs->r0;
		/* fall through */
	case ATOMIC_IOR32 + 4:
		put_user(regs->r0, up0);
		break;

	case ATOMIC_AND32 + 2:
		regs->r0 = regs->r1 & regs->r0;
		/* fall through */
	case ATOMIC_AND32 + 4:
		put_user(regs->r0, up0);
		break;

	case ATOMIC_XOR32 + 2:
		regs->r0 = regs->r1 ^ regs->r0;
		/* fall through */
	case ATOMIC_XOR32 + 4:
		put_user(regs->r0, up0);
		break;
	}

	/*
	 * We've finished the atomic section, and the only thing left for
	 * userspace is to do a RTS, so we might as well handle that too
	 * since we need to update the PC anyways.
	 */
	regs->pc = regs->rets;
}

static inline
int in_mem(unsigned long addr, unsigned long size,
           unsigned long start, unsigned long end)
{
	return addr >= start && addr + size <= end;
}
static inline
int in_mem_const_off(unsigned long addr, unsigned long size, unsigned long off,
                     unsigned long const_addr, unsigned long const_size)
{
	return const_size &&
	       in_mem(addr, size, const_addr + off, const_addr + const_size);
}
static inline
int in_mem_const(unsigned long addr, unsigned long size,
                 unsigned long const_addr, unsigned long const_size)
{
	return in_mem_const_off(addr, size, 0, const_addr, const_size);
}
#define ASYNC_ENABLED(bnum, bctlnum) \
({ \
	(bfin_read_EBIU_AMGCTL() & 0xe) < ((bnum + 1) << 1) ? 0 : \
	bfin_read_EBIU_AMBCTL##bctlnum() & B##bnum##RDYEN ? 0 : \
	1; \
})
/*
 * We can't read EBIU banks that aren't enabled or we end up hanging
 * on the access to the async space.  Make sure we validate accesses
 * that cross async banks too.
 *	0 - found, but unusable
 *	1 - found & usable
 *	2 - not found
 */
static
int in_async(unsigned long addr, unsigned long size)
{
	if (addr >= ASYNC_BANK0_BASE && addr < ASYNC_BANK0_BASE + ASYNC_BANK0_SIZE) {
		if (!ASYNC_ENABLED(0, 0))
			return 0;
		if (addr + size <= ASYNC_BANK0_BASE + ASYNC_BANK0_SIZE)
			return 1;
		size -= ASYNC_BANK0_BASE + ASYNC_BANK0_SIZE - addr;
		addr = ASYNC_BANK0_BASE + ASYNC_BANK0_SIZE;
	}
	if (addr >= ASYNC_BANK1_BASE && addr < ASYNC_BANK1_BASE + ASYNC_BANK1_SIZE) {
		if (!ASYNC_ENABLED(1, 0))
			return 0;
		if (addr + size <= ASYNC_BANK1_BASE + ASYNC_BANK1_SIZE)
			return 1;
		size -= ASYNC_BANK1_BASE + ASYNC_BANK1_SIZE - addr;
		addr = ASYNC_BANK1_BASE + ASYNC_BANK1_SIZE;
	}
	if (addr >= ASYNC_BANK2_BASE && addr < ASYNC_BANK2_BASE + ASYNC_BANK2_SIZE) {
		if (!ASYNC_ENABLED(2, 1))
			return 0;
		if (addr + size <= ASYNC_BANK2_BASE + ASYNC_BANK2_SIZE)
			return 1;
		size -= ASYNC_BANK2_BASE + ASYNC_BANK2_SIZE - addr;
		addr = ASYNC_BANK2_BASE + ASYNC_BANK2_SIZE;
	}
	if (addr >= ASYNC_BANK3_BASE && addr < ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE) {
		if (ASYNC_ENABLED(3, 1))
			return 0;
		if (addr + size <= ASYNC_BANK3_BASE + ASYNC_BANK3_SIZE)
			return 1;
		return 0;
	}

	/* not within async bounds */
	return 2;
}

int bfin_mem_access_type(unsigned long addr, unsigned long size)
{
	int cpu = raw_smp_processor_id();

	/* Check that things do not wrap around */
	if (addr > ULONG_MAX - size)
		return -EFAULT;

	if (in_mem(addr, size, FIXED_CODE_START, physical_mem_end))
		return BFIN_MEM_ACCESS_CORE;

	if (in_mem_const(addr, size, L1_CODE_START, L1_CODE_LENGTH))
		return cpu == 0 ? BFIN_MEM_ACCESS_ITEST : BFIN_MEM_ACCESS_IDMA;
	if (in_mem_const(addr, size, L1_SCRATCH_START, L1_SCRATCH_LENGTH))
		return cpu == 0 ? BFIN_MEM_ACCESS_CORE_ONLY : -EFAULT;
	if (in_mem_const(addr, size, L1_DATA_A_START, L1_DATA_A_LENGTH))
		return cpu == 0 ? BFIN_MEM_ACCESS_CORE : BFIN_MEM_ACCESS_IDMA;
	if (in_mem_const(addr, size, L1_DATA_B_START, L1_DATA_B_LENGTH))
		return cpu == 0 ? BFIN_MEM_ACCESS_CORE : BFIN_MEM_ACCESS_IDMA;
#ifdef COREB_L1_CODE_START
	if (in_mem_const(addr, size, COREB_L1_CODE_START, COREB_L1_CODE_LENGTH))
		return cpu == 1 ? BFIN_MEM_ACCESS_ITEST : BFIN_MEM_ACCESS_IDMA;
	if (in_mem_const(addr, size, COREB_L1_SCRATCH_START, L1_SCRATCH_LENGTH))
		return cpu == 1 ? BFIN_MEM_ACCESS_CORE_ONLY : -EFAULT;
	if (in_mem_const(addr, size, COREB_L1_DATA_A_START, COREB_L1_DATA_A_LENGTH))
		return cpu == 1 ? BFIN_MEM_ACCESS_CORE : BFIN_MEM_ACCESS_IDMA;
	if (in_mem_const(addr, size, COREB_L1_DATA_B_START, COREB_L1_DATA_B_LENGTH))
		return cpu == 1 ? BFIN_MEM_ACCESS_CORE : BFIN_MEM_ACCESS_IDMA;
#endif
	if (in_mem_const(addr, size, L2_START, L2_LENGTH))
		return BFIN_MEM_ACCESS_CORE;

	if (addr >= SYSMMR_BASE)
		return BFIN_MEM_ACCESS_CORE_ONLY;

	switch (in_async(addr, size)) {
	case 0: return -EFAULT;
	case 1: return BFIN_MEM_ACCESS_CORE;
	case 2: /* fall through */;
	}

	if (in_mem_const(addr, size, BOOT_ROM_START, BOOT_ROM_LENGTH))
		return BFIN_MEM_ACCESS_CORE;
	if (in_mem_const(addr, size, L1_ROM_START, L1_ROM_LENGTH))
		return BFIN_MEM_ACCESS_DMA;

	return -EFAULT;
}

#if defined(CONFIG_ACCESS_CHECK)
#ifdef CONFIG_ACCESS_OK_L1
__attribute__((l1_text))
#endif
/* Return 1 if access to memory range is OK, 0 otherwise */
int _access_ok(unsigned long addr, unsigned long size)
{
	int aret;

	if (size == 0)
		return 1;
	/* Check that things do not wrap around */
	if (addr > ULONG_MAX - size)
		return 0;
	if (segment_eq(get_fs(), KERNEL_DS))
		return 1;
#ifdef CONFIG_MTD_UCLINUX
	if (1)
#else
	if (0)
#endif
	{
		if (in_mem(addr, size, memory_start, memory_end))
			return 1;
		if (in_mem(addr, size, memory_mtd_end, physical_mem_end))
			return 1;
# ifndef CONFIG_ROMFS_ON_MTD
		if (0)
# endif
			/* For XIP, allow user space to use pointers within the ROMFS.  */
			if (in_mem(addr, size, memory_mtd_start, memory_mtd_end))
				return 1;
	} else {
		if (in_mem(addr, size, memory_start, physical_mem_end))
			return 1;
	}

	if (in_mem(addr, size, (unsigned long)__init_begin, (unsigned long)__init_end))
		return 1;

	if (in_mem_const(addr, size, L1_CODE_START, L1_CODE_LENGTH))
		return 1;
	if (in_mem_const_off(addr, size, _etext_l1 - _stext_l1, L1_CODE_START, L1_CODE_LENGTH))
		return 1;
	if (in_mem_const_off(addr, size, _ebss_l1 - _sdata_l1, L1_DATA_A_START, L1_DATA_A_LENGTH))
		return 1;
	if (in_mem_const_off(addr, size, _ebss_b_l1 - _sdata_b_l1, L1_DATA_B_START, L1_DATA_B_LENGTH))
		return 1;
#ifdef COREB_L1_CODE_START
	if (in_mem_const(addr, size, COREB_L1_CODE_START, COREB_L1_CODE_LENGTH))
		return 1;
	if (in_mem_const(addr, size, COREB_L1_SCRATCH_START, L1_SCRATCH_LENGTH))
		return 1;
	if (in_mem_const(addr, size, COREB_L1_DATA_A_START, COREB_L1_DATA_A_LENGTH))
		return 1;
	if (in_mem_const(addr, size, COREB_L1_DATA_B_START, COREB_L1_DATA_B_LENGTH))
		return 1;
#endif

#ifndef CONFIG_EXCEPTION_L1_SCRATCH
	if (in_mem_const(addr, size, (unsigned long)l1_stack_base, l1_stack_len))
		return 1;
#endif

	aret = in_async(addr, size);
	if (aret < 2)
		return aret;

	if (in_mem_const_off(addr, size, _ebss_l2 - _stext_l2, L2_START, L2_LENGTH))
		return 1;

	if (in_mem_const(addr, size, BOOT_ROM_START, BOOT_ROM_LENGTH))
		return 1;
	if (in_mem_const(addr, size, L1_ROM_START, L1_ROM_LENGTH))
		return 1;

	return 0;
}
EXPORT_SYMBOL(_access_ok);
#endif /* CONFIG_ACCESS_CHECK */
