#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <asm/host_ops.h>

static int threads_counter;
static void *threads_counter_lock;

static inline void threads_counter_inc(void)
{
	lkl_ops->sem_down(threads_counter_lock);
	threads_counter++;
	lkl_ops->sem_up(threads_counter_lock);
}

static inline void threads_counter_dec(void)
{
	lkl_ops->sem_down(threads_counter_lock);
	threads_counter--;
	lkl_ops->sem_up(threads_counter_lock);
}

static inline int threads_counter_get(void)
{
	int counter;

	lkl_ops->sem_down(threads_counter_lock);
	counter = threads_counter;
	lkl_ops->sem_up(threads_counter_lock);

	return counter;
}

struct thread_info *alloc_thread_info_node(struct task_struct *task, int node)
{
	struct thread_info *ti;

	ti = kmalloc(sizeof(*ti), GFP_KERNEL);
	if (!ti)
		return NULL;

	ti->exit_info = NULL;
	ti->prev_sched = NULL;
	ti->sched_sem = lkl_ops->sem_alloc(0);
	ti->task = task;
	if (!ti->sched_sem) {
		kfree(ti);
		return NULL;
	}

	return ti;
}

static void kill_thread(struct thread_exit_info *ei)
{
	if (WARN_ON(!ei))
		return;

	ei->dead = true;
	lkl_ops->sem_up(ei->sched_sem);
}

void free_thread_info(struct thread_info *ti)
{
	struct thread_exit_info *ei = ti->exit_info;

	kfree(ti);
	kill_thread(ei);
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
	/*
	 * We need to free the thread_info structure in free_thread_info to
	 * avoid races between the dying thread and other threads. We also need
	 * to cleanup sched_sem and signal to the prev thread that it needs to
	 * exit, and we use this stack varible to pass this info.
	 */
	struct thread_exit_info ei = {
		.dead = false,
		.sched_sem = _prev->sched_sem,
	};

	_current_thread_info = task_thread_info(next);
	_next->prev_sched = prev;
	abs_prev = prev;
	_prev->exit_info = &ei;

	lkl_ops->sem_up(_next->sched_sem);
	/* _next may be already gone so use ei instead */
	lkl_ops->sem_down(ei.sched_sem);

	if (ei.dead) {
		lkl_ops->sem_free(ei.sched_sem);
		threads_counter_dec();
		lkl_ops->thread_exit();
	}

	_prev->exit_info = NULL;

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
	int ret;

	tba = kmalloc(sizeof(*tba), GFP_KERNEL);
	if (!tba)
		return -ENOMEM;

	tba->f = (int (*)(void *))esp;
	tba->arg = (void *)unused;
	tba->ti = ti;

	ret = lkl_ops->thread_create(thread_bootstrap, tba);
	if (ret) {
		kfree(tba);
		return -ENOMEM;
	}

	threads_counter_inc();

	return 0;
}

void show_stack(struct task_struct *task, unsigned long *esp)
{
}

static inline void pr_early(const char *str)
{
	if (lkl_ops->print)
		lkl_ops->print(str, strlen(str));
}

/**
 * This is called before the kernel initializes, so no kernel calls (including
 * printk) can't be made yet.
 */
int threads_init(void)
{
	struct thread_info *ti = &init_thread_union.thread_info;
	int ret = 0;

	ti->exit_info = NULL;
	ti->prev_sched = NULL;

	ti->sched_sem = lkl_ops->sem_alloc(0);
	if (!ti->sched_sem) {
		pr_early("lkl: failed to allocate init schedule semaphore\n");
		ret = -ENOMEM;
		goto out;
	}

	threads_counter_lock = lkl_ops->sem_alloc(1);
	if (!threads_counter_lock) {
		pr_early("lkl: failed to alllocate threads counter lock\n");
		ret = -ENOMEM;
		goto out_free_init_sched_sem;
	}

	return 0;

out_free_init_sched_sem:
	lkl_ops->sem_free(ti->sched_sem);

out:
	return ret;
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

		kill_thread(ti->exit_info);
	}

	while (threads_counter_get())
		;

	lkl_ops->sem_free(init_thread_union.thread_info.sched_sem);
	lkl_ops->sem_free(threads_counter_lock);
}
