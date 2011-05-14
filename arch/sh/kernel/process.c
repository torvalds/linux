#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>

struct kmem_cache *task_xstate_cachep = NULL;
unsigned int xstate_size;

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	*dst = *src;

	if (src->thread.xstate) {
		dst->thread.xstate = kmem_cache_alloc(task_xstate_cachep,
						      GFP_KERNEL);
		if (!dst->thread.xstate)
			return -ENOMEM;
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

#if THREAD_SHIFT < PAGE_SHIFT
static struct kmem_cache *thread_info_cache;

struct thread_info *alloc_thread_info_node(struct task_struct *tsk, int node)
{
	struct thread_info *ti;
#ifdef CONFIG_DEBUG_STACK_USAGE
	gfp_t mask = GFP_KERNEL | __GFP_ZERO;
#else
	gfp_t mask = GFP_KERNEL;
#endif

	ti = kmem_cache_alloc_node(thread_info_cache, mask, node);
	return ti;
}

void free_thread_info(struct thread_info *ti)
{
	free_thread_xstate(ti->task);
	kmem_cache_free(thread_info_cache, ti);
}

void thread_info_cache_init(void)
{
	thread_info_cache = kmem_cache_create("thread_info", THREAD_SIZE,
					      THREAD_SIZE, SLAB_PANIC, NULL);
}
#else
struct thread_info *alloc_thread_info_node(struct task_struct *tsk, int node)
{
#ifdef CONFIG_DEBUG_STACK_USAGE
	gfp_t mask = GFP_KERNEL | __GFP_ZERO;
#else
	gfp_t mask = GFP_KERNEL;
#endif
	struct page *page = alloc_pages_node(node, mask, THREAD_SIZE_ORDER);

	return page ? page_address(page) : NULL;
}

void free_thread_info(struct thread_info *ti)
{
	free_thread_xstate(ti->task);
	free_pages((unsigned long)ti, THREAD_SIZE_ORDER);
}
#endif /* THREAD_SHIFT < PAGE_SHIFT */

void arch_task_cache_init(void)
{
	if (!xstate_size)
		return;

	task_xstate_cachep = kmem_cache_create("task_xstate", xstate_size,
					       __alignof__(union thread_xstate),
					       SLAB_PANIC | SLAB_NOTRACK, NULL);
}

#ifdef CONFIG_SH_FPU_EMU
# define HAVE_SOFTFP	1
#else
# define HAVE_SOFTFP	0
#endif

void __cpuinit init_thread_xstate(void)
{
	if (boot_cpu_data.flags & CPU_HAS_FPU)
		xstate_size = sizeof(struct sh_fpu_hard_struct);
	else if (HAVE_SOFTFP)
		xstate_size = sizeof(struct sh_fpu_soft_struct);
	else
		xstate_size = 0;
}
