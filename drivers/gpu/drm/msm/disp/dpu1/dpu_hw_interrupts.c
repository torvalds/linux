// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <drm/drm_managed.h>

#include "dpu_core_irq.h"
#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_util.h"
#include "dpu_hw_mdss.h"
#include "dpu_trace.h"

/*
 * Register offsets in MDSS register file for the interrupt registers
 * w.r.t. the MDP base
 */
#define MDP_INTF_OFF(intf)				(0x6A000 + 0x800 * (intf))
#define MDP_INTF_INTR_EN(intf)				(MDP_INTF_OFF(intf) + 0x1c0)
#define MDP_INTF_INTR_STATUS(intf)			(MDP_INTF_OFF(intf) + 0x1c4)
#define MDP_INTF_INTR_CLEAR(intf)			(MDP_INTF_OFF(intf) + 0x1c8)
#define MDP_INTF_TEAR_OFF(intf)				(0x6D700 + 0x100 * (intf))
#define MDP_INTF_INTR_TEAR_EN(intf)			(MDP_INTF_TEAR_OFF(intf) + 0x000)
#define MDP_INTF_INTR_TEAR_STATUS(intf)			(MDP_INTF_TEAR_OFF(intf) + 0x004)
#define MDP_INTF_INTR_TEAR_CLEAR(intf)			(MDP_INTF_TEAR_OFF(intf) + 0x008)
#define MDP_AD4_OFF(ad4)				(0x7C000 + 0x1000 * (ad4))
#define MDP_AD4_INTR_EN_OFF(ad4)			(MDP_AD4_OFF(ad4) + 0x41c)
#define MDP_AD4_INTR_CLEAR_OFF(ad4)			(MDP_AD4_OFF(ad4) + 0x424)
#define MDP_AD4_INTR_STATUS_OFF(ad4)			(MDP_AD4_OFF(ad4) + 0x420)
#define MDP_INTF_REV_7xxx_OFF(intf)			(0x34000 + 0x1000 * (intf))
#define MDP_INTF_REV_7xxx_INTR_EN(intf)			(MDP_INTF_REV_7xxx_OFF(intf) + 0x1c0)
#define MDP_INTF_REV_7xxx_INTR_STATUS(intf)		(MDP_INTF_REV_7xxx_OFF(intf) + 0x1c4)
#define MDP_INTF_REV_7xxx_INTR_CLEAR(intf)		(MDP_INTF_REV_7xxx_OFF(intf) + 0x1c8)
#define MDP_INTF_REV_7xxx_TEAR_OFF(intf)		(0x34800 + 0x1000 * (intf))
#define MDP_INTF_REV_7xxx_INTR_TEAR_EN(intf)		(MDP_INTF_REV_7xxx_TEAR_OFF(intf) + 0x000)
#define MDP_INTF_REV_7xxx_INTR_TEAR_STATUS(intf)	(MDP_INTF_REV_7xxx_TEAR_OFF(intf) + 0x004)
#define MDP_INTF_REV_7xxx_INTR_TEAR_CLEAR(intf)		(MDP_INTF_REV_7xxx_TEAR_OFF(intf) + 0x008)

/**
 * struct dpu_intr_reg - array of DPU register sets
 * @clr_off:	offset to CLEAR reg
 * @en_off:	offset to ENABLE reg
 * @status_off:	offset to STATUS reg
 */
struct dpu_intr_reg {
	u32 clr_off;
	u32 en_off;
	u32 status_off;
};

/*
 * dpu_intr_set_legacy -  List of DPU interrupt registers for DPU <= 6.x
 */
static const struct dpu_intr_reg dpu_intr_set_legacy[] = {
	[MDP_SSPP_TOP0_INTR] = {
		INTR_CLEAR,
		INTR_EN,
		INTR_STATUS
	},
	[MDP_SSPP_TOP0_INTR2] = {
		INTR2_CLEAR,
		INTR2_EN,
		INTR2_STATUS
	},
	[MDP_SSPP_TOP0_HIST_INTR] = {
		HIST_INTR_CLEAR,
		HIST_INTR_EN,
		HIST_INTR_STATUS
	},
	[MDP_INTF0_INTR] = {
		MDP_INTF_INTR_CLEAR(0),
		MDP_INTF_INTR_EN(0),
		MDP_INTF_INTR_STATUS(0)
	},
	[MDP_INTF1_INTR] = {
		MDP_INTF_INTR_CLEAR(1),
		MDP_INTF_INTR_EN(1),
		MDP_INTF_INTR_STATUS(1)
	},
	[MDP_INTF2_INTR] = {
		MDP_INTF_INTR_CLEAR(2),
		MDP_INTF_INTR_EN(2),
		MDP_INTF_INTR_STATUS(2)
	},
	[MDP_INTF3_INTR] = {
		MDP_INTF_INTR_CLEAR(3),
		MDP_INTF_INTR_EN(3),
		MDP_INTF_INTR_STATUS(3)
	},
	[MDP_INTF4_INTR] = {
		MDP_INTF_INTR_CLEAR(4),
		MDP_INTF_INTR_EN(4),
		MDP_INTF_INTR_STATUS(4)
	},
	[MDP_INTF5_INTR] = {
		MDP_INTF_INTR_CLEAR(5),
		MDP_INTF_INTR_EN(5),
		MDP_INTF_INTR_STATUS(5)
	},
	[MDP_INTF1_TEAR_INTR] = {
		MDP_INTF_INTR_TEAR_CLEAR(1),
		MDP_INTF_INTR_TEAR_EN(1),
		MDP_INTF_INTR_TEAR_STATUS(1)
	},
	[MDP_INTF2_TEAR_INTR] = {
		MDP_INTF_INTR_TEAR_CLEAR(2),
		MDP_INTF_INTR_TEAR_EN(2),
		MDP_INTF_INTR_TEAR_STATUS(2)
	},
	[MDP_AD4_0_INTR] = {
		MDP_AD4_INTR_CLEAR_OFF(0),
		MDP_AD4_INTR_EN_OFF(0),
		MDP_AD4_INTR_STATUS_OFF(0),
	},
	[MDP_AD4_1_INTR] = {
		MDP_AD4_INTR_CLEAR_OFF(1),
		MDP_AD4_INTR_EN_OFF(1),
		MDP_AD4_INTR_STATUS_OFF(1),
	},
};

/*
 * dpu_intr_set_7xxx -  List of DPU interrupt registers for DPU >= 7.0
 */
static const struct dpu_intr_reg dpu_intr_set_7xxx[] = {
	[MDP_SSPP_TOP0_INTR] = {
		INTR_CLEAR,
		INTR_EN,
		INTR_STATUS
	},
	[MDP_SSPP_TOP0_INTR2] = {
		INTR2_CLEAR,
		INTR2_EN,
		INTR2_STATUS
	},
	[MDP_SSPP_TOP0_HIST_INTR] = {
		HIST_INTR_CLEAR,
		HIST_INTR_EN,
		HIST_INTR_STATUS
	},
	[MDP_INTF0_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(0),
		MDP_INTF_REV_7xxx_INTR_EN(0),
		MDP_INTF_REV_7xxx_INTR_STATUS(0)
	},
	[MDP_INTF1_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(1),
		MDP_INTF_REV_7xxx_INTR_EN(1),
		MDP_INTF_REV_7xxx_INTR_STATUS(1)
	},
	[MDP_INTF1_TEAR_INTR] = {
		MDP_INTF_REV_7xxx_INTR_TEAR_CLEAR(1),
		MDP_INTF_REV_7xxx_INTR_TEAR_EN(1),
		MDP_INTF_REV_7xxx_INTR_TEAR_STATUS(1)
	},
	[MDP_INTF2_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(2),
		MDP_INTF_REV_7xxx_INTR_EN(2),
		MDP_INTF_REV_7xxx_INTR_STATUS(2)
	},
	[MDP_INTF2_TEAR_INTR] = {
		MDP_INTF_REV_7xxx_INTR_TEAR_CLEAR(2),
		MDP_INTF_REV_7xxx_INTR_TEAR_EN(2),
		MDP_INTF_REV_7xxx_INTR_TEAR_STATUS(2)
	},
	[MDP_INTF3_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(3),
		MDP_INTF_REV_7xxx_INTR_EN(3),
		MDP_INTF_REV_7xxx_INTR_STATUS(3)
	},
	[MDP_INTF4_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(4),
		MDP_INTF_REV_7xxx_INTR_EN(4),
		MDP_INTF_REV_7xxx_INTR_STATUS(4)
	},
	[MDP_INTF5_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(5),
		MDP_INTF_REV_7xxx_INTR_EN(5),
		MDP_INTF_REV_7xxx_INTR_STATUS(5)
	},
	[MDP_INTF6_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(6),
		MDP_INTF_REV_7xxx_INTR_EN(6),
		MDP_INTF_REV_7xxx_INTR_STATUS(6)
	},
	[MDP_INTF7_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(7),
		MDP_INTF_REV_7xxx_INTR_EN(7),
		MDP_INTF_REV_7xxx_INTR_STATUS(7)
	},
	[MDP_INTF8_INTR] = {
		MDP_INTF_REV_7xxx_INTR_CLEAR(8),
		MDP_INTF_REV_7xxx_INTR_EN(8),
		MDP_INTF_REV_7xxx_INTR_STATUS(8)
	},
};

#define DPU_IRQ_MASK(irq_idx)	(BIT(DPU_IRQ_BIT(irq_idx)))

static inline bool dpu_core_irq_is_valid(unsigned int irq_idx)
{
	return irq_idx && irq_idx <= DPU_NUM_IRQS;
}

static inline struct dpu_hw_intr_entry *dpu_core_irq_get_entry(struct dpu_hw_intr *intr,
							       unsigned int irq_idx)
{
	return &intr->irq_tbl[irq_idx - 1];
}

/**
 * dpu_core_irq_callback_handler - dispatch core interrupts
 * @dpu_kms:		Pointer to DPU's KMS structure
 * @irq_idx:		interrupt index
 */
static void dpu_core_irq_callback_handler(struct dpu_kms *dpu_kms, unsigned int irq_idx)
{
	struct dpu_hw_intr_entry *irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, irq_idx);

	VERB("IRQ=[%d, %d]\n", DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	if (!irq_entry->cb) {
		DRM_ERROR("no registered cb, IRQ=[%d, %d]\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return;
	}

	atomic_inc(&irq_entry->count);

	/*
	 * Perform registered function callback
	 */
	irq_entry->cb(irq_entry->arg);
}

irqreturn_t dpu_core_irq(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int reg_idx;
	unsigned int irq_idx;
	u32 irq_status;
	u32 enable_mask;
	int bit;
	unsigned long irq_flags;

	if (!intr)
		return IRQ_NONE;

	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	for (reg_idx = 0; reg_idx < MDP_INTR_MAX; reg_idx++) {
		if (!test_bit(reg_idx, &intr->irq_mask))
			continue;

		/* Read interrupt status */
		irq_status = DPU_REG_READ(&intr->hw, intr->intr_set[reg_idx].status_off);

		/* Read enable mask */
		enable_mask = DPU_REG_READ(&intr->hw, intr->intr_set[reg_idx].en_off);

		/* and clear the interrupt */
		if (irq_status)
			DPU_REG_WRITE(&intr->hw, intr->intr_set[reg_idx].clr_off,
				     irq_status);

		/* Finally update IRQ status based on enable mask */
		irq_status &= enable_mask;

		if (!irq_status)
			continue;

		/*
		 * Search through matching intr status.
		 */
		while ((bit = ffs(irq_status)) != 0) {
			irq_idx = DPU_IRQ_IDX(reg_idx, bit - 1);

			dpu_core_irq_callback_handler(dpu_kms, irq_idx);

			/*
			 * When callback finish, clear the irq_status
			 * with the matching mask. Once irq_status
			 * is all cleared, the search can be stopped.
			 */
			irq_status &= ~BIT(bit - 1);
		}
	}

	/* ensure register writes go through */
	wmb();

	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	return IRQ_HANDLED;
}

static int dpu_hw_intr_enable_irq_locked(struct dpu_hw_intr *intr,
					 unsigned int irq_idx)
{
	int reg_idx;
	const struct dpu_intr_reg *reg;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (!dpu_core_irq_is_valid(irq_idx)) {
		pr_err("invalid IRQ=[%d, %d]\n",
		       DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return -EINVAL;
	}

	/*
	 * The cache_irq_mask and hardware RMW operations needs to be done
	 * under irq_lock and it's the caller's responsibility to ensure that's
	 * held.
	 */
	assert_spin_locked(&intr->irq_lock);

	reg_idx = DPU_IRQ_REG(irq_idx);
	reg = &intr->intr_set[reg_idx];

	/* Is this interrupt register supported on the platform */
	if (WARN_ON(!reg->en_off))
		return -EINVAL;

	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if (cache_irq_mask & DPU_IRQ_MASK(irq_idx)) {
		dbgstr = "already ";
	} else {
		dbgstr = "";

		cache_irq_mask |= DPU_IRQ_MASK(irq_idx);
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, DPU_IRQ_MASK(irq_idx));
		/* Enabling interrupts with the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}

	pr_debug("DPU IRQ=[%d, %d] %senabled: MASK:0x%.8lx, CACHE-MASK:0x%.8x\n",
		 DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), dbgstr,
			DPU_IRQ_MASK(irq_idx), cache_irq_mask);

	return 0;
}

static int dpu_hw_intr_disable_irq_locked(struct dpu_hw_intr *intr,
					  unsigned int irq_idx)
{
	int reg_idx;
	const struct dpu_intr_reg *reg;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (!dpu_core_irq_is_valid(irq_idx)) {
		pr_err("invalid IRQ=[%d, %d]\n",
		       DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return -EINVAL;
	}

	/*
	 * The cache_irq_mask and hardware RMW operations needs to be done
	 * under irq_lock and it's the caller's responsibility to ensure that's
	 * held.
	 */
	assert_spin_locked(&intr->irq_lock);

	reg_idx = DPU_IRQ_REG(irq_idx);
	reg = &intr->intr_set[reg_idx];

	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if ((cache_irq_mask & DPU_IRQ_MASK(irq_idx)) == 0) {
		dbgstr = "already ";
	} else {
		dbgstr = "";

		cache_irq_mask &= ~DPU_IRQ_MASK(irq_idx);
		/* Disable interrupts based on the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, DPU_IRQ_MASK(irq_idx));

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}

	pr_debug("DPU IRQ=[%d, %d] %sdisabled: MASK:0x%.8lx, CACHE-MASK:0x%.8x\n",
		 DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), dbgstr,
			DPU_IRQ_MASK(irq_idx), cache_irq_mask);

	return 0;
}

static void dpu_clear_irqs(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int i;

	if (!intr)
		return;

	for (i = 0; i < MDP_INTR_MAX; i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					intr->intr_set[i].clr_off, 0xffffffff);
	}

	/* ensure register writes go through */
	wmb();
}

static void dpu_disable_all_irqs(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int i;

	if (!intr)
		return;

	for (i = 0; i < MDP_INTR_MAX; i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					intr->intr_set[i].en_off, 0x00000000);
	}

	/* ensure register writes go through */
	wmb();
}

u32 dpu_core_irq_read(struct dpu_kms *dpu_kms,
		      unsigned int irq_idx)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int reg_idx;
	unsigned long irq_flags;
	u32 intr_status;

	if (!intr)
		return 0;

	if (!dpu_core_irq_is_valid(irq_idx)) {
		pr_err("invalid IRQ=[%d, %d]\n", DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return 0;
	}

	spin_lock_irqsave(&intr->irq_lock, irq_flags);

	reg_idx = DPU_IRQ_REG(irq_idx);
	intr_status = DPU_REG_READ(&intr->hw,
			intr->intr_set[reg_idx].status_off) &
		DPU_IRQ_MASK(irq_idx);
	if (intr_status)
		DPU_REG_WRITE(&intr->hw, intr->intr_set[reg_idx].clr_off,
				intr_status);

	/* ensure register writes go through */
	wmb();

	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	return intr_status;
}

struct dpu_hw_intr *dpu_hw_intr_init(struct drm_device *dev,
				     void __iomem *addr,
				     const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_intr *intr;
	unsigned int i;

	if (!addr || !m)
		return ERR_PTR(-EINVAL);

	intr = drmm_kzalloc(dev, sizeof(*intr), GFP_KERNEL);
	if (!intr)
		return ERR_PTR(-ENOMEM);

	if (m->mdss_ver->core_major_ver >= 7)
		intr->intr_set = dpu_intr_set_7xxx;
	else
		intr->intr_set = dpu_intr_set_legacy;

	intr->hw.blk_addr = addr + m->mdp[0].base;

	intr->irq_mask = BIT(MDP_SSPP_TOP0_INTR) |
			 BIT(MDP_SSPP_TOP0_INTR2) |
			 BIT(MDP_SSPP_TOP0_HIST_INTR);
	for (i = 0; i < m->intf_count; i++) {
		const struct dpu_intf_cfg *intf = &m->intf[i];

		if (intf->type == INTF_NONE)
			continue;

		intr->irq_mask |= BIT(MDP_INTFn_INTR(intf->id));

		if (intf->intr_tear_rd_ptr)
			intr->irq_mask |= BIT(DPU_IRQ_REG(intf->intr_tear_rd_ptr));
	}

	spin_lock_init(&intr->irq_lock);

	return intr;
}

int dpu_core_irq_register_callback(struct dpu_kms *dpu_kms,
				   unsigned int irq_idx,
				   void (*irq_cb)(void *arg),
				   void *irq_arg)
{
	struct dpu_hw_intr_entry *irq_entry;
	unsigned long irq_flags;
	int ret;

	if (!irq_cb) {
		DPU_ERROR("IRQ=[%d, %d] NULL callback\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return -EINVAL;
	}

	if (!dpu_core_irq_is_valid(irq_idx)) {
		DPU_ERROR("invalid IRQ=[%d, %d] irq_cb:%ps\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), irq_cb);
		return -EINVAL;
	}

	VERB("[%pS] IRQ=[%d, %d]\n", __builtin_return_address(0),
	     DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);

	irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, irq_idx);
	if (unlikely(WARN_ON(irq_entry->cb))) {
		spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

		return -EBUSY;
	}

	trace_dpu_core_irq_register_callback(DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), irq_cb);
	irq_entry->arg = irq_arg;
	irq_entry->cb = irq_cb;

	ret = dpu_hw_intr_enable_irq_locked(
				dpu_kms->hw_intr,
				irq_idx);
	if (ret)
		DPU_ERROR("Failed/ to enable IRQ=[%d, %d]\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
	spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

	trace_dpu_irq_register_success(DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	return 0;
}

int dpu_core_irq_unregister_callback(struct dpu_kms *dpu_kms,
				     unsigned int irq_idx)
{
	struct dpu_hw_intr_entry *irq_entry;
	unsigned long irq_flags;
	int ret;

	if (!dpu_core_irq_is_valid(irq_idx)) {
		DPU_ERROR("invalid IRQ=[%d, %d]\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));
		return -EINVAL;
	}

	VERB("[%pS] IRQ=[%d, %d]\n", __builtin_return_address(0),
	     DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);
	trace_dpu_core_irq_unregister_callback(DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	ret = dpu_hw_intr_disable_irq_locked(dpu_kms->hw_intr, irq_idx);
	if (ret)
		DPU_ERROR("Failed to disable IRQ=[%d, %d]: %d\n",
			  DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx), ret);

	irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, irq_idx);
	irq_entry->cb = NULL;
	irq_entry->arg = NULL;

	spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

	trace_dpu_irq_unregister_success(DPU_IRQ_REG(irq_idx), DPU_IRQ_BIT(irq_idx));

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int dpu_debugfs_core_irq_show(struct seq_file *s, void *v)
{
	struct dpu_kms *dpu_kms = s->private;
	struct dpu_hw_intr_entry *irq_entry;
	unsigned long irq_flags;
	int i, irq_count;
	void *cb;

	for (i = 1; i <= DPU_NUM_IRQS; i++) {
		spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);
		irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, i);
		irq_count = atomic_read(&irq_entry->count);
		cb = irq_entry->cb;
		spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

		if (irq_count || cb)
			seq_printf(s, "IRQ=[%d, %d] count:%d cb:%ps\n",
				   DPU_IRQ_REG(i), DPU_IRQ_BIT(i), irq_count, cb);
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(dpu_debugfs_core_irq);

void dpu_debugfs_core_irq_init(struct dpu_kms *dpu_kms,
		struct dentry *parent)
{
	debugfs_create_file("core_irq", 0600, parent, dpu_kms,
		&dpu_debugfs_core_irq_fops);
}
#endif

void dpu_core_irq_preinstall(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct dpu_hw_intr_entry *irq_entry;
	int i;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	dpu_clear_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	for (i = 1; i <= DPU_NUM_IRQS; i++) {
		irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, i);
		atomic_set(&irq_entry->count, 0);
	}
}

void dpu_core_irq_uninstall(struct msm_kms *kms)
{
	struct dpu_kms *dpu_kms = to_dpu_kms(kms);
	struct dpu_hw_intr_entry *irq_entry;
	int i;

	if (!dpu_kms->hw_intr)
		return;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	for (i = 1; i <= DPU_NUM_IRQS; i++) {
		irq_entry = dpu_core_irq_get_entry(dpu_kms->hw_intr, i);
		if (irq_entry->cb)
			DPU_ERROR("IRQ=[%d, %d] still enabled/registered\n",
				  DPU_IRQ_REG(i), DPU_IRQ_BIT(i));
	}

	dpu_clear_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}
