/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines an spu hypervisor abstraction layer.
 *
 *  Copyright 2006 Sony Corp.
 */

#if !defined(_SPU_PRIV1_H)
#define _SPU_PRIV1_H
#if defined(__KERNEL__)

#include <linux/types.h>

struct spu;
struct spu_context;

/* access to priv1 registers */

struct spu_priv1_ops {
	void (*int_mask_and) (struct spu *spu, int class, u64 mask);
	void (*int_mask_or) (struct spu *spu, int class, u64 mask);
	void (*int_mask_set) (struct spu *spu, int class, u64 mask);
	u64 (*int_mask_get) (struct spu *spu, int class);
	void (*int_stat_clear) (struct spu *spu, int class, u64 stat);
	u64 (*int_stat_get) (struct spu *spu, int class);
	void (*cpu_affinity_set) (struct spu *spu, int cpu);
	u64 (*mfc_dar_get) (struct spu *spu);
	u64 (*mfc_dsisr_get) (struct spu *spu);
	void (*mfc_dsisr_set) (struct spu *spu, u64 dsisr);
	void (*mfc_sdr_setup) (struct spu *spu);
	void (*mfc_sr1_set) (struct spu *spu, u64 sr1);
	u64 (*mfc_sr1_get) (struct spu *spu);
	void (*mfc_tclass_id_set) (struct spu *spu, u64 tclass_id);
	u64 (*mfc_tclass_id_get) (struct spu *spu);
	void (*tlb_invalidate) (struct spu *spu);
	void (*resource_allocation_groupID_set) (struct spu *spu, u64 id);
	u64 (*resource_allocation_groupID_get) (struct spu *spu);
	void (*resource_allocation_enable_set) (struct spu *spu, u64 enable);
	u64 (*resource_allocation_enable_get) (struct spu *spu);
};

extern const struct spu_priv1_ops* spu_priv1_ops;

static inline void
spu_int_mask_and (struct spu *spu, int class, u64 mask)
{
	spu_priv1_ops->int_mask_and(spu, class, mask);
}

static inline void
spu_int_mask_or (struct spu *spu, int class, u64 mask)
{
	spu_priv1_ops->int_mask_or(spu, class, mask);
}

static inline void
spu_int_mask_set (struct spu *spu, int class, u64 mask)
{
	spu_priv1_ops->int_mask_set(spu, class, mask);
}

static inline u64
spu_int_mask_get (struct spu *spu, int class)
{
	return spu_priv1_ops->int_mask_get(spu, class);
}

static inline void
spu_int_stat_clear (struct spu *spu, int class, u64 stat)
{
	spu_priv1_ops->int_stat_clear(spu, class, stat);
}

static inline u64
spu_int_stat_get (struct spu *spu, int class)
{
	return spu_priv1_ops->int_stat_get (spu, class);
}

static inline void
spu_cpu_affinity_set (struct spu *spu, int cpu)
{
	spu_priv1_ops->cpu_affinity_set(spu, cpu);
}

static inline u64
spu_mfc_dar_get (struct spu *spu)
{
	return spu_priv1_ops->mfc_dar_get(spu);
}

static inline u64
spu_mfc_dsisr_get (struct spu *spu)
{
	return spu_priv1_ops->mfc_dsisr_get(spu);
}

static inline void
spu_mfc_dsisr_set (struct spu *spu, u64 dsisr)
{
	spu_priv1_ops->mfc_dsisr_set(spu, dsisr);
}

static inline void
spu_mfc_sdr_setup (struct spu *spu)
{
	spu_priv1_ops->mfc_sdr_setup(spu);
}

static inline void
spu_mfc_sr1_set (struct spu *spu, u64 sr1)
{
	spu_priv1_ops->mfc_sr1_set(spu, sr1);
}

static inline u64
spu_mfc_sr1_get (struct spu *spu)
{
	return spu_priv1_ops->mfc_sr1_get(spu);
}

static inline void
spu_mfc_tclass_id_set (struct spu *spu, u64 tclass_id)
{
	spu_priv1_ops->mfc_tclass_id_set(spu, tclass_id);
}

static inline u64
spu_mfc_tclass_id_get (struct spu *spu)
{
	return spu_priv1_ops->mfc_tclass_id_get(spu);
}

static inline void
spu_tlb_invalidate (struct spu *spu)
{
	spu_priv1_ops->tlb_invalidate(spu);
}

static inline void
spu_resource_allocation_groupID_set (struct spu *spu, u64 id)
{
	spu_priv1_ops->resource_allocation_groupID_set(spu, id);
}

static inline u64
spu_resource_allocation_groupID_get (struct spu *spu)
{
	return spu_priv1_ops->resource_allocation_groupID_get(spu);
}

static inline void
spu_resource_allocation_enable_set (struct spu *spu, u64 enable)
{
	spu_priv1_ops->resource_allocation_enable_set(spu, enable);
}

static inline u64
spu_resource_allocation_enable_get (struct spu *spu)
{
	return spu_priv1_ops->resource_allocation_enable_get(spu);
}

/* spu management abstraction */

struct spu_management_ops {
	int (*enumerate_spus)(int (*fn)(void *data));
	int (*create_spu)(struct spu *spu, void *data);
	int (*destroy_spu)(struct spu *spu);
	void (*enable_spu)(struct spu_context *ctx);
	void (*disable_spu)(struct spu_context *ctx);
	int (*init_affinity)(void);
};

extern const struct spu_management_ops* spu_management_ops;

static inline int
spu_enumerate_spus (int (*fn)(void *data))
{
	return spu_management_ops->enumerate_spus(fn);
}

static inline int
spu_create_spu (struct spu *spu, void *data)
{
	return spu_management_ops->create_spu(spu, data);
}

static inline int
spu_destroy_spu (struct spu *spu)
{
	return spu_management_ops->destroy_spu(spu);
}

static inline int
spu_init_affinity (void)
{
	return spu_management_ops->init_affinity();
}

static inline void
spu_enable_spu (struct spu_context *ctx)
{
	spu_management_ops->enable_spu(ctx);
}

static inline void
spu_disable_spu (struct spu_context *ctx)
{
	spu_management_ops->disable_spu(ctx);
}

/*
 * The declarations following are put here for convenience
 * and only intended to be used by the platform setup code.
 */

extern const struct spu_priv1_ops spu_priv1_mmio_ops;
extern const struct spu_priv1_ops spu_priv1_beat_ops;

extern const struct spu_management_ops spu_management_of_ops;

#endif /* __KERNEL__ */
#endif
