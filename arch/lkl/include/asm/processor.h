#ifndef _ASM_LKL_PROCESSOR_H
#define _ASM_LKL_PROCESSOR_H

struct task_struct;

#define cpu_relax() barrier()

#define current_text_addr() ({ __label__ _l; _l: &&_l; })

static inline unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return 0;
}

static inline void release_thread(struct task_struct *dead_task)
{
}

static inline void prepare_to_copy(struct task_struct *tsk)
{
}

static inline unsigned long get_wchan(struct task_struct *p)
{
	return 0;
}

static inline void flush_thread(void)
{
}

static inline void exit_thread(void)
{
}

static inline void trap_init(void)
{
}

struct thread_struct { };

#define INIT_THREAD { }

#define task_pt_regs(tsk) (struct pt_regs *)(NULL)

/* We don't have strict user/kernel spaces */
#define TASK_SIZE ((unsigned long)-1)
#define TASK_UNMAPPED_BASE	0

#define KSTK_EIP(tsk)	(0)
#define KSTK_ESP(tsk)	(0)

#endif
