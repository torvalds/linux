// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/slab.h>

#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_util.h"
#include "dpu_hw_mdss.h"

/**
 * Register offsets in MDSS register file for the interrupt registers
 * w.r.t. to the MDP base
 */
#define MDP_SSPP_TOP0_OFF		0x0
#define MDP_INTF_0_OFF			0x6A000
#define MDP_INTF_1_OFF			0x6A800
#define MDP_INTF_2_OFF			0x6B000
#define MDP_INTF_3_OFF			0x6B800
#define MDP_INTF_4_OFF			0x6C000
#define MDP_AD4_0_OFF			0x7C000
#define MDP_AD4_1_OFF			0x7D000
#define MDP_AD4_INTR_EN_OFF		0x41c
#define MDP_AD4_INTR_CLEAR_OFF		0x424
#define MDP_AD4_INTR_STATUS_OFF		0x420
#define MDP_INTF_0_OFF_REV_7xxx             0x34000
#define MDP_INTF_1_OFF_REV_7xxx             0x35000
#define MDP_INTF_5_OFF_REV_7xxx             0x39000

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
 * struct dpu_intr_reg -  List of DPU interrupt registers
 *
 * When making changes be sure to sync with dpu_hw_intr_reg
 */
static const struct dpu_intr_reg dpu_intr_set[] = {
	{
		MDP_SSPP_TOP0_OFF+INTR_CLEAR,
		MDP_SSPP_TOP0_OFF+INTR_EN,
		MDP_SSPP_TOP0_OFF+INTR_STATUS
	},
	{
		MDP_SSPP_TOP0_OFF+INTR2_CLEAR,
		MDP_SSPP_TOP0_OFF+INTR2_EN,
		MDP_SSPP_TOP0_OFF+INTR2_STATUS
	},
	{
		MDP_SSPP_TOP0_OFF+HIST_INTR_CLEAR,
		MDP_SSPP_TOP0_OFF+HIST_INTR_EN,
		MDP_SSPP_TOP0_OFF+HIST_INTR_STATUS
	},
	{
		MDP_INTF_0_OFF+INTF_INTR_CLEAR,
		MDP_INTF_0_OFF+INTF_INTR_EN,
		MDP_INTF_0_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_1_OFF+INTF_INTR_CLEAR,
		MDP_INTF_1_OFF+INTF_INTR_EN,
		MDP_INTF_1_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_2_OFF+INTF_INTR_CLEAR,
		MDP_INTF_2_OFF+INTF_INTR_EN,
		MDP_INTF_2_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_3_OFF+INTF_INTR_CLEAR,
		MDP_INTF_3_OFF+INTF_INTR_EN,
		MDP_INTF_3_OFF+INTF_INTR_STATUS
	},
	{
		MDP_INTF_4_OFF+INTF_INTR_CLEAR,
		MDP_INTF_4_OFF+INTF_INTR_EN,
		MDP_INTF_4_OFF+INTF_INTR_STATUS
	},
	{
		MDP_AD4_0_OFF + MDP_AD4_INTR_CLEAR_OFF,
		MDP_AD4_0_OFF + MDP_AD4_INTR_EN_OFF,
		MDP_AD4_0_OFF + MDP_AD4_INTR_STATUS_OFF,
	},
	{
		MDP_AD4_1_OFF + MDP_AD4_INTR_CLEAR_OFF,
		MDP_AD4_1_OFF + MDP_AD4_INTR_EN_OFF,
		MDP_AD4_1_OFF + MDP_AD4_INTR_STATUS_OFF,
	},
	{
		MDP_INTF_0_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_0_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_0_OFF_REV_7xxx+INTF_INTR_STATUS
	},
	{
		MDP_INTF_1_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_1_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_1_OFF_REV_7xxx+INTF_INTR_STATUS
	},
	{
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_STATUS
	},
};

#define DPU_IRQ_REG(irq_idx)	(irq_idx / 32)
#define DPU_IRQ_MASK(irq_idx)	(BIT(irq_idx % 32))

static void dpu_hw_intr_clear_intr_status_nolock(struct dpu_hw_intr *intr,
		int irq_idx)
{
	int reg_idx;

	if (!intr)
		return;

	reg_idx = DPU_IRQ_REG(irq_idx);
	DPU_REG_WRITE(&intr->hw, dpu_intr_set[reg_idx].clr_off, DPU_IRQ_MASK(irq_idx));

	/* ensure register writes go through */
	wmb();
}

static void dpu_hw_intr_dispatch_irq(struct dpu_hw_intr *intr,
		void (*cbfunc)(void *, int),
		void *arg)
{
	int reg_idx;
	int irq_idx;
	u32 irq_status;
	u32 enable_mask;
	int bit;
	unsigned long irq_flags;

	if (!intr)
		return;

	/*
	 * The dispatcher will save the IRQ status before calling here.
	 * Now need to go through each IRQ status and find matching
	 * irq lookup index.
	 */
	spin_lock_irqsave(&intr->irq_lock, irq_flags);
	for (reg_idx = 0; reg_idx < ARRAY_SIZE(dpu_intr_set); reg_idx++) {
		if (!test_bit(reg_idx, &intr->irq_mask))
			continue;

		/* Read interrupt status */
		irq_status = DPU_REG_READ(&intr->hw, dpu_intr_set[reg_idx].status_off);

		/* Read enable mask */
		enable_mask = DPU_REG_READ(&intr->hw, dpu_intr_set[reg_idx].en_off);

		/* and clear the interrupt */
		if (irq_status)
			DPU_REG_WRITE(&intr->hw, dpu_intr_set[reg_idx].clr_off,
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
			/*
			 * Once a match on irq mask, perform a callback
			 * to the given cbfunc. cbfunc will take care
			 * the interrupt status clearing. If cbfunc is
			 * not provided, then the interrupt clearing
			 * is here.
			 */
			if (cbfunc)
				cbfunc(arg, irq_idx);

			dpu_hw_intr_clear_intr_status_nolock(intr, irq_idx);

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
}

static int dpu_hw_intr_enable_irq_locked(struct dpu_hw_intr *intr, int irq_idx)
{
	int reg_idx;
	const struct dpu_intr_reg *reg;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (irq_idx < 0 || irq_idx >= intr->total_irqs) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	/*
	 * The cache_irq_mask and hardware RMW operations needs to be done
	 * under irq_lock and it's the caller's responsibility to ensure that's
	 * held.
	 */
	assert_spin_locked(&intr->irq_lock);

	reg_idx = DPU_IRQ_REG(irq_idx);
	reg = &dpu_intr_set[reg_idx];

	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if (cache_irq_mask & DPU_IRQ_MASK(irq_idx)) {
		dbgstr = "DPU IRQ already set:";
	} else {
		dbgstr = "DPU IRQ enabled:";

		cache_irq_mask |= DPU_IRQ_MASK(irq_idx);
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, DPU_IRQ_MASK(irq_idx));
		/* Enabling interrupts with the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}

	pr_debug("%s MASK:0x%.8lx, CACHE-MASK:0x%.8x\n", dbgstr,
			DPU_IRQ_MASK(irq_idx), cache_irq_mask);

	return 0;
}

static int dpu_hw_intr_disable_irq_locked(struct dpu_hw_intr *intr, int irq_idx)
{
	int reg_idx;
	const struct dpu_intr_reg *reg;
	const char *dbgstr = NULL;
	uint32_t cache_irq_mask;

	if (!intr)
		return -EINVAL;

	if (irq_idx < 0 || irq_idx >= intr->total_irqs) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	/*
	 * The cache_irq_mask and hardware RMW operations needs to be done
	 * under irq_lock and it's the caller's responsibility to ensure that's
	 * held.
	 */
	assert_spin_locked(&intr->irq_lock);

	reg_idx = DPU_IRQ_REG(irq_idx);
	reg = &dpu_intr_set[reg_idx];

	cache_irq_mask = intr->cache_irq_mask[reg_idx];
	if ((cache_irq_mask & DPU_IRQ_MASK(irq_idx)) == 0) {
		dbgstr = "DPU IRQ is already cleared:";
	} else {
		dbgstr = "DPU IRQ mask disable:";

		cache_irq_mask &= ~DPU_IRQ_MASK(irq_idx);
		/* Disable interrupts based on the new mask */
		DPU_REG_WRITE(&intr->hw, reg->en_off, cache_irq_mask);
		/* Cleaning any pending interrupt */
		DPU_REG_WRITE(&intr->hw, reg->clr_off, DPU_IRQ_MASK(irq_idx));

		/* ensure register write goes through */
		wmb();

		intr->cache_irq_mask[reg_idx] = cache_irq_mask;
	}

	pr_debug("%s MASK:0x%.8lx, CACHE-MASK:0x%.8x\n", dbgstr,
			DPU_IRQ_MASK(irq_idx), cache_irq_mask);

	return 0;
}

static int dpu_hw_intr_clear_irqs(struct dpu_hw_intr *intr)
{
	int i;

	if (!intr)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].clr_off, 0xffffffff);
	}

	/* ensure register writes go through */
	wmb();

	return 0;
}

static int dpu_hw_intr_disable_irqs(struct dpu_hw_intr *intr)
{
	int i;

	if (!intr)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].en_off, 0x00000000);
	}

	/* ensure register writes go through */
	wmb();

	return 0;
}

static u32 dpu_hw_intr_get_interrupt_status(struct dpu_hw_intr *intr,
		int irq_idx, bool clear)
{
	int reg_idx;
	unsigned long irq_flags;
	u32 intr_status;

	if (!intr)
		return 0;

	if (irq_idx < 0 || irq_idx >= intr->total_irqs) {
		pr_err("invalid IRQ index: [%d]\n", irq_idx);
		return 0;
	}

	spin_lock_irqsave(&intr->irq_lock, irq_flags);

	reg_idx = DPU_IRQ_REG(irq_idx);
	intr_status = DPU_REG_READ(&intr->hw,
			dpu_intr_set[reg_idx].status_off) &
		DPU_IRQ_MASK(irq_idx);
	if (intr_status && clear)
		DPU_REG_WRITE(&intr->hw, dpu_intr_set[reg_idx].clr_off,
				intr_status);

	/* ensure register writes go through */
	wmb();

	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);

	return intr_status;
}

static unsigned long dpu_hw_intr_lock(struct dpu_hw_intr *intr)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&intr->irq_lock, irq_flags);

	return irq_flags;
}

static void dpu_hw_intr_unlock(struct dpu_hw_intr *intr, unsigned long irq_flags)
{
	spin_unlock_irqrestore(&intr->irq_lock, irq_flags);
}

static void __setup_intr_ops(struct dpu_hw_intr_ops *ops)
{
	ops->enable_irq_locked = dpu_hw_intr_enable_irq_locked;
	ops->disable_irq_locked = dpu_hw_intr_disable_irq_locked;
	ops->dispatch_irqs = dpu_hw_intr_dispatch_irq;
	ops->clear_all_irqs = dpu_hw_intr_clear_irqs;
	ops->disable_all_irqs = dpu_hw_intr_disable_irqs;
	ops->get_interrupt_status = dpu_hw_intr_get_interrupt_status;
	ops->lock = dpu_hw_intr_lock;
	ops->unlock = dpu_hw_intr_unlock;
}

static void __intr_offset(struct dpu_mdss_cfg *m,
		void __iomem *addr, struct dpu_hw_blk_reg_map *hw)
{
	hw->base_off = addr;
	hw->blk_off = m->mdp[0].base;
	hw->hwversion = m->hwversion;
}

struct dpu_hw_intr *dpu_hw_intr_init(void __iomem *addr,
		struct dpu_mdss_cfg *m)
{
	struct dpu_hw_intr *intr;

	if (!addr || !m)
		return ERR_PTR(-EINVAL);

	intr = kzalloc(sizeof(*intr), GFP_KERNEL);
	if (!intr)
		return ERR_PTR(-ENOMEM);

	__intr_offset(m, addr, &intr->hw);
	__setup_intr_ops(&intr->ops);

	intr->total_irqs = ARRAY_SIZE(dpu_intr_set) * 32;

	intr->cache_irq_mask = kcalloc(ARRAY_SIZE(dpu_intr_set), sizeof(u32),
			GFP_KERNEL);
	if (intr->cache_irq_mask == NULL) {
		kfree(intr);
		return ERR_PTR(-ENOMEM);
	}

	intr->irq_mask = m->mdss_irqs;

	spin_lock_init(&intr->irq_lock);

	return intr;
}

void dpu_hw_intr_destroy(struct dpu_hw_intr *intr)
{
	if (intr) {
		kfree(intr->cache_irq_mask);
		kfree(intr);
	}
}

