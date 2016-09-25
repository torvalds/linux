#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/host_ops.h>
#include <asm/cpu.h>

static volatile int threads_counter;

static int init_ti(struct thread_info *ti)
{
	ti->sched_sem = lkl_ops->sem_alloc(0);
	if (!ti->sched_sem)
		return -ENOMEM;

	ti->dead = false;
	ti->prev_sched = NULL;
	ti->tid = 0;

	return 0;
}

unsigned long *alloc_thread_stack_node(struct task_struct *task, int node)
{
	struct thread_info *ti;

	ti = kmalloc(sizeof(*ti), GFP_KERNEL);
	if (!ti)
		return NULL;

	if (init_ti(ti)) {
		kfree(ti);
		return NULL;
	}
	ti->task = task;


	return (unsigned long *)ti;
}

/*
 * The only new tasks created are kernel threads that have a predefined starting
 * point thus no stack copy is required.
 */
void setup_thread_stack(struct task_struct *p, struct task_struct *org)
{
	struct thread_info *ti = task_thread_info(p);
	struct thread_info *org_ti = task_thread_info(org);

	ti->flags = org_ti->flags;
	ti->preempt_count = org_ti->preempt_count;
	ti->addr_limit = org_ti->addr_limit;
}

static void kill_thread(struct thread_info *ti)
{
	ti->dead = true;
	lkl_ops->sem_up(ti->sched_sem);
	lkl_ops->thread_join(ti->tid);
	lkl_ops->sem_free(ti->sched_sem);

}

void free_thread_stack(unsigned long *stack)
{
	struct thread_info *ti = (struct thread_info *)stack;

	kill_thread(ti);
	kfree(ti);
}

struct thread_info *_current_thread_info = &init_thread_union.thread_info;

struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next)
{
	struct thread_info *_prev = task_thread_info(prev);
	struct thread_info *_next = task_thread_info(next);
	/*
	 * schedule() expects the return of this function to be the task that we
	 * switched away from. Returning prev is not going to work because we
	 * are actually going to return the previous taks that was scheduled
	 * before the task we are going to wake up, and not the current task,
	 * e.g.:
	 *
	 * swapper -> init: saved prev on swapper stack is swapper
	 * init -> ksoftirqd0: saved prev on init stack is init
	 * ksoftirqd0 -> swapper: returned prev is swapper
	 */
	static struct task_struct *abs_prev = &init_task;
	unsigned long _prev_flags = _prev->flags;

	_current_thread_info = task_thread_info(next);
	_next->prev_sched = prev;
	abs_prev = prev;

	BUG_ON(!_next->tid);
	lkl_cpu_change_owner(_next->tid);

	lkl_ops->sem_up(_next->sched_sem);
	if (test_bit(TIF_SCHED_JB, &_prev_flags)) {
		clear_ti_thread_flag(_prev, TIF_SCHED_JB);
		lkl_ops->jmp_buf_longjmp(&_prev->sched_jb, 1);
	} else if (test_bit(TIF_SCHED_EXIT, &_prev_flags)) {
		lkl_ops->thread_exit();
	} else {
		lkl_ops->sem_down(_prev->sched_sem);
	}

	if (_prev->dead) {
		__sync_fetch_and_sub(&threads_counter, 1);
		lkl_ops->thread_exit();
	}

	return abs_prev;
}

struct thread_bootstrap_arg {
	struct thread_info *ti;
	int (*f)(void *);
	void *arg;
};

static void thread_bootstrap(void *_tba)
{
	struct thread_bootstrap_arg *tba = (struct thread_bootstrap_arg *)_tba;
	struct thread_info *ti = tba->ti;
	int (*f)(void *) = tba->f;
	void *arg = tba->arg;

	lkl_ops->sem_down(ti->sched_sem);
	kfree(tba);
	if (ti->prev_sched)
		schedule_tail(ti->prev_sched);

	f(arg);
	do_exit(0);
}

int copy_thread(unsigned long clone_flags, unsigned long esp,
		unsigned long unused, struct task_struct *p)
{
	struct thread_info *ti = task_thread_info(p);
	struct thread_bootstrap_arg *tba;

	tba = kmalloc(sizeof(*tba), GFP_KERNEL);
	if (!tba)
		return -ENOMEM;

	tba->f = (int (*)(void *))esp;
	tba->arg = (void *)unused;
	tba->ti = ti;

	ti->tid = lkl_ops->thread_create(thread_bootstrap, tba);
	if (!ti->tid) {
		kfree(tba);
		return -ENOMEM;
	}

	__sync_fetch_and_add(&threads_counter, 1);

	return 0;
}

void show_stack(struct task_struct *task, unsigned long *esp)
{
}

/**
 * This is called before the kernel initializes, so no kernel calls (including
 * printk) can't be made yet.
 */
void threads_init(void)
{
	int ret;
	struct thread_info *ti = &init_thread_union.thread_info;

	ret = init_ti(ti);
	if (ret < 0)
		lkl_printf("lkl: failed to allocate init schedule semaphore\n");

	ti->tid = lkl_ops->thread_self();
}

void threads_cleanup(void)
{
	struct task_struct *p;

	for_each_process(p) {
		struct thread_info *ti = task_thread_info(p);

		if (p->pid != 1)
			WARN(!(p->flags & PF_KTHREAD),
			     "non kernel thread task %p\n", p->comm);
		WARN(p->state == TASK_RUNNING,
		     "thread %s still running while halting\n", p->comm);

		kill_thread(ti);
	}

	while (threads_counter)
		;

	lkl_ops->sem_free(init_thread_union.thread_info.sched_sem);
}
