/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KVM_NVHE_ALLOC__
#define __KVM_NVHE_ALLOC__
#include <linux/types.h>

#include <asm/kvm_host.h>

void *hyp_alloc(size_t size);
int hyp_alloc_errno(void);
u32 hyp_alloc_topup_needed(void);
void hyp_free(void *addr);

int hyp_alloc_init(size_t size);
int hyp_alloc_topup(struct kvm_hyp_memcache *host_mc);
unsigned long hyp_alloc_reclaimable(void);
void hyp_alloc_reclaim(struct kvm_hyp_memcache *host_mc, unsigned long target);

#ifdef CONFIG_NVHE_EL2_DEBUG
int hyp_allocator_selftest(void);
u32 hyp_alloc_selftest_topup_needed(void);
int hyp_alloc_selftest_topup(struct kvm_hyp_memcache *host_mc);
void hyp_alloc_selftest_reclaim(struct kvm_hyp_memcache *host_mc, unsigned long target);
#else
static inline int hyp_alloc_selftest_topup(struct kvm_hyp_memcache *host_mc) { return 0; };
static inline void
hyp_alloc_selftest_reclaim(struct kvm_hyp_memcache *host_mc, unsigned long target) { };
#endif
#endif
