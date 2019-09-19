// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/bitfield.h>

#include "dw-edma-core.h"
#include "dw-edma-v0-core.h"
#include "dw-edma-v0-regs.h"
#include "dw-edma-v0-debugfs.h"

enum dw_edma_control {
	DW_EDMA_V0_CB					= BIT(0),
	DW_EDMA_V0_TCB					= BIT(1),
	DW_EDMA_V0_LLP					= BIT(2),
	DW_EDMA_V0_LIE					= BIT(3),
	DW_EDMA_V0_RIE					= BIT(4),
	DW_EDMA_V0_CCS					= BIT(8),
	DW_EDMA_V0_LLE					= BIT(9),
};

static inline struct dw_edma_v0_regs __iomem *__dw_regs(struct dw_edma *dw)
{
	return dw->rg_region.vaddr;
}

#define SET(dw, name, value)				\
	writel(value, &(__dw_regs(dw)->name))

#define GET(dw, name)					\
	readl(&(__dw_regs(dw)->name))

#define SET_RW(dw, dir, name, value)			\
	do {						\
		if ((dir) == EDMA_DIR_WRITE)		\
			SET(dw, wr_##name, value);	\
		else					\
			SET(dw, rd_##name, value);	\
	} while (0)

#define GET_RW(dw, dir, name)				\
	((dir) == EDMA_DIR_WRITE			\
	  ? GET(dw, wr_##name)				\
	  : GET(dw, rd_##name))

#define SET_BOTH(dw, name, value)			\
	do {						\
		SET(dw, wr_##name, value);		\
		SET(dw, rd_##name, value);		\
	} while (0)

static inline struct dw_edma_v0_ch_regs __iomem *
__dw_ch_regs(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch)
{
	if (dw->mode == EDMA_MODE_LEGACY)
		return &(__dw_regs(dw)->type.legacy.ch);

	if (dir == EDMA_DIR_WRITE)
		return &__dw_regs(dw)->type.unroll.ch[ch].wr;

	return &__dw_regs(dw)->type.unroll.ch[ch].rd;
}

static inline void writel_ch(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch,
			     u32 value, void __iomem *addr)
{
	if (dw->mode == EDMA_MODE_LEGACY) {
		u32 viewport_sel;
		unsigned long flags;

		raw_spin_lock_irqsave(&dw->lock, flags);

		viewport_sel = FIELD_PREP(EDMA_V0_VIEWPORT_MASK, ch);
		if (dir == EDMA_DIR_READ)
			viewport_sel |= BIT(31);

		writel(viewport_sel,
		       &(__dw_regs(dw)->type.legacy.viewport_sel));
		writel(value, addr);

		raw_spin_unlock_irqrestore(&dw->lock, flags);
	} else {
		writel(value, addr);
	}
}

static inline u32 readl_ch(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch,
			   const void __iomem *addr)
{
	u32 value;

	if (dw->mode == EDMA_MODE_LEGACY) {
		u32 viewport_sel;
		unsigned long flags;

		raw_spin_lock_irqsave(&dw->lock, flags);

		viewport_sel = FIELD_PREP(EDMA_V0_VIEWPORT_MASK, ch);
		if (dir == EDMA_DIR_READ)
			viewport_sel |= BIT(31);

		writel(viewport_sel,
		       &(__dw_regs(dw)->type.legacy.viewport_sel));
		value = readl(addr);

		raw_spin_unlock_irqrestore(&dw->lock, flags);
	} else {
		value = readl(addr);
	}

	return value;
}

#define SET_CH(dw, dir, ch, name, value) \
	writel_ch(dw, dir, ch, value, &(__dw_ch_regs(dw, dir, ch)->name))

#define GET_CH(dw, dir, ch, name) \
	readl_ch(dw, dir, ch, &(__dw_ch_regs(dw, dir, ch)->name))

#define SET_LL(ll, value) \
	writel(value, ll)

/* eDMA management callbacks */
void dw_edma_v0_core_off(struct dw_edma *dw)
{
	SET_BOTH(dw, int_mask, EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH(dw, int_clear, EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH(dw, engine_en, 0);
}

u16 dw_edma_v0_core_ch_count(struct dw_edma *dw, enum dw_edma_dir dir)
{
	u32 num_ch;

	if (dir == EDMA_DIR_WRITE)
		num_ch = FIELD_GET(EDMA_V0_WRITE_CH_COUNT_MASK, GET(dw, ctrl));
	else
		num_ch = FIELD_GET(EDMA_V0_READ_CH_COUNT_MASK, GET(dw, ctrl));

	if (num_ch > EDMA_V0_MAX_NR_CH)
		num_ch = EDMA_V0_MAX_NR_CH;

	return (u16)num_ch;
}

enum dma_status dw_edma_v0_core_ch_status(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->chip->dw;
	u32 tmp;

	tmp = FIELD_GET(EDMA_V0_CH_STATUS_MASK,
			GET_CH(dw, chan->dir, chan->id, ch_control1));

	if (tmp == 1)
		return DMA_IN_PROGRESS;
	else if (tmp == 3)
		return DMA_COMPLETE;
	else
		return DMA_ERROR;
}

void dw_edma_v0_core_clear_done_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->chip->dw;

	SET_RW(dw, chan->dir, int_clear,
	       FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id)));
}

void dw_edma_v0_core_clear_abort_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->chip->dw;

	SET_RW(dw, chan->dir, int_clear,
	       FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id)));
}

u32 dw_edma_v0_core_status_done_int(struct dw_edma *dw, enum dw_edma_dir dir)
{
	return FIELD_GET(EDMA_V0_DONE_INT_MASK, GET_RW(dw, dir, int_status));
}

u32 dw_edma_v0_core_status_abort_int(struct dw_edma *dw, enum dw_edma_dir dir)
{
	return FIELD_GET(EDMA_V0_ABORT_INT_MASK, GET_RW(dw, dir, int_status));
}

static void dw_edma_v0_core_write_chunk(struct dw_edma_chunk *chunk)
{
	struct dw_edma_burst *child;
	struct dw_edma_v0_lli __iomem *lli;
	struct dw_edma_v0_llp __iomem *llp;
	u32 control = 0, i = 0;
	int j;

	lli = chunk->ll_region.vaddr;

	if (chunk->cb)
		control = DW_EDMA_V0_CB;

	j = chunk->bursts_alloc;
	list_for_each_entry(child, &chunk->burst->list, list) {
		j--;
		if (!j)
			control |= (DW_EDMA_V0_LIE | DW_EDMA_V0_RIE);

		/* Channel control */
		SET_LL(&lli[i].control, control);
		/* Transfer size */
		SET_LL(&lli[i].transfer_size, child->sz);
		/* SAR - low, high */
		SET_LL(&lli[i].sar_low, lower_32_bits(child->sar));
		SET_LL(&lli[i].sar_high, upper_32_bits(child->sar));
		/* DAR - low, high */
		SET_LL(&lli[i].dar_low, lower_32_bits(child->dar));
		SET_LL(&lli[i].dar_high, upper_32_bits(child->dar));
		i++;
	}

	llp = (void __iomem *)&lli[i];
	control = DW_EDMA_V0_LLP | DW_EDMA_V0_TCB;
	if (!chunk->cb)
		control |= DW_EDMA_V0_CB;

	/* Channel control */
	SET_LL(&llp->control, control);
	/* Linked list  - low, high */
	SET_LL(&llp->llp_low, lower_32_bits(chunk->ll_region.paddr));
	SET_LL(&llp->llp_high, upper_32_bits(chunk->ll_region.paddr));
}

void dw_edma_v0_core_start(struct dw_edma_chunk *chunk, bool first)
{
	struct dw_edma_chan *chan = chunk->chan;
	struct dw_edma *dw = chan->chip->dw;
	u32 tmp;

	dw_edma_v0_core_write_chunk(chunk);

	if (first) {
		/* Enable engine */
		SET_RW(dw, chan->dir, engine_en, BIT(0));
		/* Interrupt unmask - done, abort */
		tmp = GET_RW(dw, chan->dir, int_mask);
		tmp &= ~FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id));
		tmp &= ~FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id));
		SET_RW(dw, chan->dir, int_mask, tmp);
		/* Linked list error */
		tmp = GET_RW(dw, chan->dir, linked_list_err_en);
		tmp |= FIELD_PREP(EDMA_V0_LINKED_LIST_ERR_MASK, BIT(chan->id));
		SET_RW(dw, chan->dir, linked_list_err_en, tmp);
		/* Channel control */
		SET_CH(dw, chan->dir, chan->id, ch_control1,
		       (DW_EDMA_V0_CCS | DW_EDMA_V0_LLE));
		/* Linked list - low, high */
		SET_CH(dw, chan->dir, chan->id, llp_low,
		       lower_32_bits(chunk->ll_region.paddr));
		SET_CH(dw, chan->dir, chan->id, llp_high,
		       upper_32_bits(chunk->ll_region.paddr));
	}
	/* Doorbell */
	SET_RW(dw, chan->dir, doorbell,
	       FIELD_PREP(EDMA_V0_DOORBELL_CH_MASK, chan->id));
}

int dw_edma_v0_core_device_config(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->chip->dw;
	u32 tmp = 0;

	/* MSI done addr - low, high */
	SET_RW(dw, chan->dir, done_imwr_low, chan->msi.address_lo);
	SET_RW(dw, chan->dir, done_imwr_high, chan->msi.address_hi);
	/* MSI abort addr - low, high */
	SET_RW(dw, chan->dir, abort_imwr_low, chan->msi.address_lo);
	SET_RW(dw, chan->dir, abort_imwr_high, chan->msi.address_hi);
	/* MSI data - low, high */
	switch (chan->id) {
	case 0:
	case 1:
		tmp = GET_RW(dw, chan->dir, ch01_imwr_data);
		break;

	case 2:
	case 3:
		tmp = GET_RW(dw, chan->dir, ch23_imwr_data);
		break;

	case 4:
	case 5:
		tmp = GET_RW(dw, chan->dir, ch45_imwr_data);
		break;

	case 6:
	case 7:
		tmp = GET_RW(dw, chan->dir, ch67_imwr_data);
		break;
	}

	if (chan->id & BIT(0)) {
		/* Channel odd {1, 3, 5, 7} */
		tmp &= EDMA_V0_CH_EVEN_MSI_DATA_MASK;
		tmp |= FIELD_PREP(EDMA_V0_CH_ODD_MSI_DATA_MASK,
				  chan->msi.data);
	} else {
		/* Channel even {0, 2, 4, 6} */
		tmp &= EDMA_V0_CH_ODD_MSI_DATA_MASK;
		tmp |= FIELD_PREP(EDMA_V0_CH_EVEN_MSI_DATA_MASK,
				  chan->msi.data);
	}

	switch (chan->id) {
	case 0:
	case 1:
		SET_RW(dw, chan->dir, ch01_imwr_data, tmp);
		break;

	case 2:
	case 3:
		SET_RW(dw, chan->dir, ch23_imwr_data, tmp);
		break;

	case 4:
	case 5:
		SET_RW(dw, chan->dir, ch45_imwr_data, tmp);
		break;

	case 6:
	case 7:
		SET_RW(dw, chan->dir, ch67_imwr_data, tmp);
		break;
	}

	return 0;
}

/* eDMA debugfs callbacks */
void dw_edma_v0_core_debugfs_on(struct dw_edma_chip *chip)
{
	dw_edma_v0_debugfs_on(chip);
}

void dw_edma_v0_core_debugfs_off(void)
{
	dw_edma_v0_debugfs_off();
}
