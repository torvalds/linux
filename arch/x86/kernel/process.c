#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/sched.h>

struct kmem_cache *task_xstate_cachep;

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	*dst = *src;
	if (src->thread.xstate) {
		dst->thread.xstate = kmem_cache_alloc(task_xstate_cachep,
						      GFP_KERNEL);
		if (!dst->thread.xstate)
			return -ENOMEM;
		WARN_ON((unsigned long)dst->thread.xstate & 15);
		memcpy(dst->thread.xstate, src->thread.xstate, xstate_size);
	}
	return 0;
}

void free_thread_xstate(struct task_struct *tsk)
{
	if (tsk->thread.xstate) {
		kmem_cache_free(task_xstate_cachep, tsk->thread.xstate);
		tsk->thread.xstate = NULL;
	}
}

void free_thread_info(struct thread_info *ti)
{
	free_thread_xstate(ti->task);
	free_pages((unsigned long)ti, get_order(THREAD_SIZE));
}

void arch_task_cache_init(void)
{
        task_xstate_cachep =
        	kmem_cache_create("task_xstate", xstate_size,
				  __alignof__(union thread_xstate),
				  SLAB_PANIC, NULL);
}
