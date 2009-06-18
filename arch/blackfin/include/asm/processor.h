#ifndef __ASM_BFIN_PROCESSOR_H
#define __ASM_BFIN_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <asm/blackfin.h>
#include <asm/segment.h>
#include <linux/compiler.h>

static inline unsigned long rdusp(void)
{
	unsigned long usp;

	__asm__ __volatile__("%0 = usp;\n\t":"=da"(usp));
	return usp;
}

static inline void wrusp(unsigned long usp)
{
	__asm__ __volatile__("usp = %0;\n\t"::"da"(usp));
}

static inline unsigned long __get_SP(void)
{
	unsigned long sp;

	__asm__ __volatile__("%0 = sp;\n\t" : "=da"(sp));
	return sp;
}

/*
 * User space process size: 1st byte beyond user address space.
 * Fairly meaningless on nommu.  Parts of user programs can be scattered
 * in a lot of places, so just disable this by setting it to 0xFFFFFFFF.
 */
#define TASK_SIZE	0xFFFFFFFF

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#endif

#define TASK_UNMAPPED_BASE	0

struct thread_struct {
	unsigned long ksp;	/* kernel stack pointer */
	unsigned long usp;	/* user stack pointer */
	unsigned short seqstat;	/* saved status register */
	unsigned long esp0;	/* points to SR of stack frame pt_regs */
	unsigned long pc;	/* instruction pointer */
	void *        debuggerinfo;
};

#define INIT_THREAD  {						\
	sizeof(init_stack) + (unsigned long) init_stack, 0,	\
	PS_S, 0, 0						\
}

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * pass the data segment into user programs if it exists,
 * it can't hurt anything as far as I can tell
 */
#ifndef CONFIG_SMP
#define start_thread(_regs, _pc, _usp)					\
do {									\
	set_fs(USER_DS);						\
	(_regs)->pc = (_pc);						\
	if (current->mm)						\
		(_regs)->p5 = current->mm->start_data;			\
	task_thread_info(current)->l1_task_info.stack_start		\
		= (void *)current->mm->context.stack_start;		\
	task_thread_info(current)->l1_task_info.lowest_sp = (void *)(_usp); \
	memcpy(L1_SCRATCH_TASK_INFO, &task_thread_info(current)->l1_task_info, \
		sizeof(*L1_SCRATCH_TASK_INFO));				\
	wrusp(_usp);							\
} while(0)
#else
#define start_thread(_regs, _pc, _usp)					\
do {									\
	set_fs(USER_DS);						\
	(_regs)->pc = (_pc);						\
	if (current->mm)						\
		(_regs)->p5 = current->mm->start_data;			\
	wrusp(_usp);							\
} while (0)
#endif

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

#define prepare_to_copy(tsk)	do { } while (0)

extern int kernel_thread(int (*fn) (void *), void *arg, unsigned long flags);

/*
 * Free current thread data structures etc..
 */
static inline void exit_thread(void)
{
}

/*
 * Return saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk)	(tsk->thread.pc)

unsigned long get_wchan(struct task_struct *p);

#define	KSTK_EIP(tsk)							\
    ({									\
	unsigned long eip = 0;						\
	if ((tsk)->thread.esp0 > PAGE_SIZE &&				\
	    MAP_NR((tsk)->thread.esp0) < max_mapnr)			\
	      eip = ((struct pt_regs *) (tsk)->thread.esp0)->pc;	\
	eip; })
#define	KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->thread.usp)

#define cpu_relax()    	smp_mb()


/* Get the Silicon Revision of the chip */
static inline uint32_t __pure bfin_revid(void)
{
	/* Always use CHIPID, to work around ANOMALY_05000234 */
	uint32_t revid = (bfin_read_CHIPID() & CHIPID_VERSION) >> 28;

#ifdef CONFIG_BF52x
	/* ANOMALY_05000357
	 * Incorrect Revision Number in DSPID Register
	 */
	if (revid == 0)
		switch (bfin_read16(_BOOTROM_GET_DXE_ADDRESS_TWI)) {
		case 0x0010:
			revid = 0;
			break;
		case 0x2796:
			revid = 1;
			break;
		default:
			revid = 0xFFFF;
			break;
		}
#endif
	return revid;
}

static inline uint16_t __pure bfin_cpuid(void)
{
	return (bfin_read_CHIPID() & CHIPID_FAMILY) >> 12;
}

static inline uint32_t __pure bfin_dspid(void)
{
	return bfin_read_DSPID();
}

static inline uint32_t __pure bfin_compiled_revid(void)
{
#if defined(CONFIG_BF_REV_0_0)
	return 0;
#elif defined(CONFIG_BF_REV_0_1)
	return 1;
#elif defined(CONFIG_BF_REV_0_2)
	return 2;
#elif defined(CONFIG_BF_REV_0_3)
	return 3;
#elif defined(CONFIG_BF_REV_0_4)
	return 4;
#elif defined(CONFIG_BF_REV_0_5)
	return 5;
#elif defined(CONFIG_BF_REV_0_6)
	return 6;
#elif defined(CONFIG_BF_REV_ANY)
	return 0xffff;
#else
	return -1;
#endif
}

#endif
