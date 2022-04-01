// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include "dpu_core_irq.h"
#include "dpu_kms.h"
#include "dpu_hw_interrupts.h"
#include "dpu_hw_util.h"
#include "dpu_hw_mdss.h"
#include "dpu_trace.h"

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
#define MDP_INTF_2_OFF_REV_7xxx             0x36000
#define MDP_INTF_3_OFF_REV_7xxx             0x37000
#define MDP_INTF_4_OFF_REV_7xxx             0x38000
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
		MDP_INTF_2_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_2_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_2_OFF_REV_7xxx+INTF_INTR_STATUS
	},
	{
		MDP_INTF_3_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_3_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_3_OFF_REV_7xxx+INTF_INTR_STATUS
	},
	{
		MDP_INTF_4_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_4_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_4_OFF_REV_7xxx+INTF_INTR_STATUS
	},
	{
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_CLEAR,
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_EN,
		MDP_INTF_5_OFF_REV_7xxx+INTF_INTR_STATUS
	},
};

#define DPU_IRQ_REG(irq_idx)	(irq_idx / 32)
#define DPU_IRQ_MASK(irq_idx)	(BIT(irq_idx % 32))

/**
 * dpu_core_irq_callback_handler - dispatch core interrupts
 * @arg:		private data of callback handler
 * @irq_idx:		interrupt index
 */
static void dpu_core_irq_callback_handler(struct dpu_kms *dpu_kms, int irq_idx)
{
	struct dpu_irq_callback *cb;

	VERB("irq_idx=%d\n", irq_idx);

	if (list_empty(&dpu_kms->hw_intr->irq_cb_tbl[irq_idx]))
		DRM_ERROR("no registered cb, idx:%d\n", irq_idx);

	atomic_inc(&dpu_kms->hw_intr->irq_counts[irq_idx]);

	/*
	 * Perform registered function callback
	 */
	list_for_each_entry(cb, &dpu_kms->hw_intr->irq_cb_tbl[irq_idx], list)
		if (cb->func)
			cb->func(cb->arg, irq_idx);
}

irqreturn_t dpu_core_irq(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int reg_idx;
	int irq_idx;
	u32 irq_status;
	u32 enable_mask;
	int bit;
	unsigned long irq_flags;

	if (!intr)
		return IRQ_NONE;

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

static void dpu_clear_irqs(struct dpu_kms *dpu_kms)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int i;

	if (!intr)
		return;

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].clr_off, 0xffffffff);
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

	for (i = 0; i < ARRAY_SIZE(dpu_intr_set); i++) {
		if (test_bit(i, &intr->irq_mask))
			DPU_REG_WRITE(&intr->hw,
					dpu_intr_set[i].en_off, 0x00000000);
	}

	/* ensure register writes go through */
	wmb();
}

u32 dpu_core_irq_read(struct dpu_kms *dpu_kms, int irq_idx, bool clear)
{
	struct dpu_hw_intr *intr = dpu_kms->hw_intr;
	int reg_idx;
	unsigned long irq_flags;
	u32 intr_status;

	if (!intr)
		return 0;

	if (irq_idx < 0) {
		DPU_ERROR("[%pS] invalid irq_idx=%d\n",
				__builtin_return_address(0), irq_idx);
		return 0;
	}

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

		kfree(intr->irq_cb_tbl);
		kfree(intr->irq_counts);

		kfree(intr);
	}
}

int dpu_core_irq_register_callback(struct dpu_kms *dpu_kms, int irq_idx,
		struct dpu_irq_callback *register_irq_cb)
{
	unsigned long irq_flags;

	if (!dpu_kms->hw_intr->irq_cb_tbl) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!register_irq_cb || !register_irq_cb->func) {
		DPU_ERROR("invalid irq_cb:%d func:%d\n",
				register_irq_cb != NULL,
				register_irq_cb ?
					register_irq_cb->func != NULL : -1);
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->total_irqs) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	VERB("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);
	trace_dpu_core_irq_register_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	list_add_tail(&register_irq_cb->list,
			&dpu_kms->hw_intr->irq_cb_tbl[irq_idx]);
	if (list_is_first(&register_irq_cb->list,
			&dpu_kms->hw_intr->irq_cb_tbl[irq_idx])) {
		int ret = dpu_hw_intr_enable_irq_locked(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to enable IRQ for irq_idx:%d\n",
					irq_idx);
	}
	spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

	return 0;
}

int dpu_core_irq_unregister_callback(struct dpu_kms *dpu_kms, int irq_idx,
		struct dpu_irq_callback *register_irq_cb)
{
	unsigned long irq_flags;

	if (!dpu_kms->hw_intr->irq_cb_tbl) {
		DPU_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!register_irq_cb || !register_irq_cb->func) {
		DPU_ERROR("invalid irq_cb:%d func:%d\n",
				register_irq_cb != NULL,
				register_irq_cb ?
					register_irq_cb->func != NULL : -1);
		return -EINVAL;
	}

	if (irq_idx < 0 || irq_idx >= dpu_kms->hw_intr->total_irqs) {
		DPU_ERROR("invalid IRQ index: [%d]\n", irq_idx);
		return -EINVAL;
	}

	VERB("[%pS] irq_idx=%d\n", __builtin_return_address(0), irq_idx);

	spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);
	trace_dpu_core_irq_unregister_callback(irq_idx, register_irq_cb);
	list_del_init(&register_irq_cb->list);
	/* empty callback list but interrupt is still enabled */
	if (list_empty(&dpu_kms->hw_intr->irq_cb_tbl[irq_idx])) {
		int ret = dpu_hw_intr_disable_irq_locked(
				dpu_kms->hw_intr,
				irq_idx);
		if (ret)
			DPU_ERROR("Fail to disable IRQ for irq_idx:%d\n",
					irq_idx);
		VERB("irq_idx=%d ret=%d\n", irq_idx, ret);
	}
	spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int dpu_debugfs_core_irq_show(struct seq_file *s, void *v)
{
	struct dpu_kms *dpu_kms = s->private;
	struct dpu_irq_callback *cb;
	unsigned long irq_flags;
	int i, irq_count, cb_count;

	if (WARN_ON(!dpu_kms->hw_intr->irq_cb_tbl))
		return 0;

	for (i = 0; i < dpu_kms->hw_intr->total_irqs; i++) {
		spin_lock_irqsave(&dpu_kms->hw_intr->irq_lock, irq_flags);
		cb_count = 0;
		irq_count = atomic_read(&dpu_kms->hw_intr->irq_counts[i]);
		list_for_each_entry(cb, &dpu_kms->hw_intr->irq_cb_tbl[i], list)
			cb_count++;
		spin_unlock_irqrestore(&dpu_kms->hw_intr->irq_lock, irq_flags);

		if (irq_count || cb_count)
			seq_printf(s, "idx:%d irq:%d cb:%d\n",
					i, irq_count, cb_count);
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

void dpu_core_irq_preinstall(struct dpu_kms *dpu_kms)
{
	int i;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	dpu_clear_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);

	/* Create irq callbacks for all possible irq_idx */
	dpu_kms->hw_intr->irq_cb_tbl = kcalloc(dpu_kms->hw_intr->total_irqs,
			sizeof(struct list_head), GFP_KERNEL);
	dpu_kms->hw_intr->irq_counts = kcalloc(dpu_kms->hw_intr->total_irqs,
			sizeof(atomic_t), GFP_KERNEL);
	for (i = 0; i < dpu_kms->hw_intr->total_irqs; i++) {
		INIT_LIST_HEAD(&dpu_kms->hw_intr->irq_cb_tbl[i]);
		atomic_set(&dpu_kms->hw_intr->irq_counts[i], 0);
	}
}

void dpu_core_irq_uninstall(struct dpu_kms *dpu_kms)
{
	int i;

	pm_runtime_get_sync(&dpu_kms->pdev->dev);
	for (i = 0; i < dpu_kms->hw_intr->total_irqs; i++)
		if (!list_empty(&dpu_kms->hw_intr->irq_cb_tbl[i]))
			DPU_ERROR("irq_idx=%d still enabled/registered\n", i);

	dpu_clear_irqs(dpu_kms);
	dpu_disable_all_irqs(dpu_kms);
	pm_runtime_put_sync(&dpu_kms->pdev->dev);
}
