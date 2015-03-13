#ifndef __BACKPORT__NET_FRAG_H__
#define __BACKPORT__NET_FRAG_H__
#include_next <net/inet_frag.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
/* Memory Tracking Functions. */
#define frag_mem_limit LINUX_BACKPORT(frag_mem_limit)
static inline int frag_mem_limit(struct netns_frags *nf)
{
	return atomic_read(&nf->mem);
}

#define sub_frag_mem_limit LINUX_BACKPORT(sub_frag_mem_limit)
static inline void sub_frag_mem_limit(struct inet_frag_queue *q, int i)
{
	atomic_sub(i, &q->net->mem);
}

#define add_frag_mem_limit LINUX_BACKPORT(add_frag_mem_limit)
static inline void add_frag_mem_limit(struct inet_frag_queue *q, int i)
{
	atomic_add(i, &q->net->mem);
}

#define init_frag_mem_limit LINUX_BACKPORT(init_frag_mem_limit)
static inline void init_frag_mem_limit(struct netns_frags *nf)
{
	atomic_set(&nf->mem, 0);
}

#define sum_frag_mem_limit LINUX_BACKPORT(sum_frag_mem_limit)
static inline int sum_frag_mem_limit(struct netns_frags *nf)
{
	return atomic_read(&nf->mem);
}

#define inet_frag_maybe_warn_overflow LINUX_BACKPORT(inet_frag_maybe_warn_overflow)
void inet_frag_maybe_warn_overflow(struct inet_frag_queue *q,
				   const char *prefix);
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0) */


#endif /* __BACKPORT__NET_FRAG_H__ */
