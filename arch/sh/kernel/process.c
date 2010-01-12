#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/sched.h>

#if THREAD_SHIFT < PAGE_SHIFT
static struct kmem_cache *thread_info_cache;

struct thread_info *alloc_thread_info(struct task_struct *tsk)
{
	struct thread_info *ti;

	ti = kmem_cache_alloc(thread_info_cache, GFP_KERNEL);
	if (unlikely(ti == NULL))
		return NULL;
#ifdef CONFIG_DEBUG_STACK_USAGE
	memset(ti, 0, THREAD_SIZE);
#endif
	return ti;
}

void free_thread_info(struct thread_info *ti)
{
	kmem_cache_free(thread_info_cache, ti);
}

void thread_info_cache_init(void)
{
	thread_info_cache = kmem_cache_create("thread_info", THREAD_SIZE,
					      THREAD_SIZE, SLAB_PANIC, NULL);
}
#else
struct thread_info *alloc_thread_info(struct task_struct *tsk)
{
#ifdef CONFIG_DEBUG_STACK_USAGE
	gfp_t mask = GFP_KERNEL | __GFP_ZERO;
#else
	gfp_t mask = GFP_KERNEL;
#endif
	return (struct thread_info *)__get_free_pages(mask, THREAD_SIZE_ORDER);
}

void free_thread_info(struct thread_info *ti)
{
	free_pages((unsigned long)ti, THREAD_SIZE_ORDER);
}
#endif /* THREAD_SHIFT < PAGE_SHIFT */
