// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/bitfield.h>
#include <linux/irqreturn.h>
#include <linux/io-64-nonatomic-lo-hi.h>

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
	return dw->chip->reg_base;
}

#define SET_32(dw, name, value)				\
	writel(value, &(__dw_regs(dw)->name))

#define GET_32(dw, name)				\
	readl(&(__dw_regs(dw)->name))

#define SET_RW_32(dw, dir, name, value)			\
	do {						\
		if ((dir) == EDMA_DIR_WRITE)		\
			SET_32(dw, wr_##name, value);	\
		else					\
			SET_32(dw, rd_##name, value);	\
	} while (0)

#define GET_RW_32(dw, dir, name)			\
	((dir) == EDMA_DIR_WRITE			\
	  ? GET_32(dw, wr_##name)			\
	  : GET_32(dw, rd_##name))

#define SET_BOTH_32(dw, name, value)			\
	do {						\
		SET_32(dw, wr_##name, value);		\
		SET_32(dw, rd_##name, value);		\
	} while (0)

#define SET_64(dw, name, value)				\
	writeq(value, &(__dw_regs(dw)->name))

#define GET_64(dw, name)				\
	readq(&(__dw_regs(dw)->name))

#define SET_RW_64(dw, dir, name, value)			\
	do {						\
		if ((dir) == EDMA_DIR_WRITE)		\
			SET_64(dw, wr_##name, value);	\
		else					\
			SET_64(dw, rd_##name, value);	\
	} while (0)

#define GET_RW_64(dw, dir, name)			\
	((dir) == EDMA_DIR_WRITE			\
	  ? GET_64(dw, wr_##name)			\
	  : GET_64(dw, rd_##name))

#define SET_BOTH_64(dw, name, value)			\
	do {						\
		SET_64(dw, wr_##name, value);		\
		SET_64(dw, rd_##name, value);		\
	} while (0)

#define SET_COMPAT(dw, name, value)			\
	writel(value, &(__dw_regs(dw)->type.unroll.name))

#define SET_RW_COMPAT(dw, dir, name, value)		\
	do {						\
		if ((dir) == EDMA_DIR_WRITE)		\
			SET_COMPAT(dw, wr_##name, value); \
		else					\
			SET_COMPAT(dw, rd_##name, value); \
	} while (0)

static inline struct dw_edma_v0_ch_regs __iomem *
__dw_ch_regs(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch)
{
	if (dw->chip->mf == EDMA_MF_EDMA_LEGACY)
		return &(__dw_regs(dw)->type.legacy.ch);

	if (dir == EDMA_DIR_WRITE)
		return &__dw_regs(dw)->type.unroll.ch[ch].wr;

	return &__dw_regs(dw)->type.unroll.ch[ch].rd;
}

static inline void writel_ch(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch,
			     u32 value, void __iomem *addr)
{
	if (dw->chip->mf == EDMA_MF_EDMA_LEGACY) {
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

	if (dw->chip->mf == EDMA_MF_EDMA_LEGACY) {
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

#define SET_CH_32(dw, dir, ch, name, value) \
	writel_ch(dw, dir, ch, value, &(__dw_ch_regs(dw, dir, ch)->name))

#define GET_CH_32(dw, dir, ch, name) \
	readl_ch(dw, dir, ch, &(__dw_ch_regs(dw, dir, ch)->name))

/* eDMA management callbacks */
static void dw_edma_v0_core_off(struct dw_edma *dw)
{
	SET_BOTH_32(dw, int_mask,
		    EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH_32(dw, int_clear,
		    EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH_32(dw, engine_en, 0);
}

static u16 dw_edma_v0_core_ch_count(struct dw_edma *dw, enum dw_edma_dir dir)
{
	u32 num_ch;

	if (dir == EDMA_DIR_WRITE)
		num_ch = FIELD_GET(EDMA_V0_WRITE_CH_COUNT_MASK,
				   GET_32(dw, ctrl));
	else
		num_ch = FIELD_GET(EDMA_V0_READ_CH_COUNT_MASK,
				   GET_32(dw, ctrl));

	if (num_ch > EDMA_V0_MAX_NR_CH)
		num_ch = EDMA_V0_MAX_NR_CH;

	return (u16)num_ch;
}

static enum dma_status dw_edma_v0_core_ch_status(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	u32 tmp;

	tmp = FIELD_GET(EDMA_V0_CH_STATUS_MASK,
			GET_CH_32(dw, chan->dir, chan->id, ch_control1));

	if (tmp == 1)
		return DMA_IN_PROGRESS;
	else if (tmp == 3)
		return DMA_COMPLETE;
	else
		return DMA_ERROR;
}

static void dw_edma_v0_core_clear_done_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	SET_RW_32(dw, chan->dir, int_clear,
		  FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id)));
}

static void dw_edma_v0_core_clear_abort_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	SET_RW_32(dw, chan->dir, int_clear,
		  FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id)));
}

static u32 dw_edma_v0_core_status_done_int(struct dw_edma *dw, enum dw_edma_dir dir)
{
	return FIELD_GET(EDMA_V0_DONE_INT_MASK,
			 GET_RW_32(dw, dir, int_status));
}

static u32 dw_edma_v0_core_status_abort_int(struct dw_edma *dw, enum dw_edma_dir dir)
{
	return FIELD_GET(EDMA_V0_ABORT_INT_MASK,
			 GET_RW_32(dw, dir, int_status));
}

static irqreturn_t
dw_edma_v0_core_handle_int(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
			   dw_edma_handler_t done, dw_edma_handler_t abort)
{
	struct dw_edma *dw = dw_irq->dw;
	unsigned long total, pos, val;
	irqreturn_t ret = IRQ_NONE;
	struct dw_edma_chan *chan;
	unsigned long off;
	u32 mask;

	if (dir == EDMA_DIR_WRITE) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	val = dw_edma_v0_core_status_done_int(dw, dir);
	val &= mask;
	for_each_set_bit(pos, &val, total) {
		chan = &dw->chan[pos + off];

		dw_edma_v0_core_clear_done_int(chan);
		done(chan);

		ret = IRQ_HANDLED;
	}

	val = dw_edma_v0_core_status_abort_int(dw, dir);
	val &= mask;
	for_each_set_bit(pos, &val, total) {
		chan = &dw->chan[pos + off];

		dw_edma_v0_core_clear_abort_int(chan);
		abort(chan);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static void dw_edma_v0_write_ll_data(struct dw_edma_chunk *chunk, int i,
				     u32 control, u32 size, u64 sar, u64 dar)
{
	ptrdiff_t ofs = i * sizeof(struct dw_edma_v0_lli);

	if (chunk->chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_edma_v0_lli *lli = chunk->ll_region.vaddr.mem + ofs;

		lli->control = control;
		lli->transfer_size = size;
		lli->sar.reg = sar;
		lli->dar.reg = dar;
	} else {
		struct dw_edma_v0_lli __iomem *lli = chunk->ll_region.vaddr.io + ofs;

		writel(control, &lli->control);
		writel(size, &lli->transfer_size);
		writeq(sar, &lli->sar.reg);
		writeq(dar, &lli->dar.reg);
	}
}

static void dw_edma_v0_write_ll_link(struct dw_edma_chunk *chunk,
				     int i, u32 control, u64 pointer)
{
	ptrdiff_t ofs = i * sizeof(struct dw_edma_v0_lli);

	if (chunk->chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_edma_v0_llp *llp = chunk->ll_region.vaddr.mem + ofs;

		llp->control = control;
		llp->llp.reg = pointer;
	} else {
		struct dw_edma_v0_llp __iomem *llp = chunk->ll_region.vaddr.io + ofs;

		writel(control, &llp->control);
		writeq(pointer, &llp->llp.reg);
	}
}

static void dw_edma_v0_core_write_chunk(struct dw_edma_chunk *chunk)
{
	struct dw_edma_burst *child;
	struct dw_edma_chan *chan = chunk->chan;
	u32 control = 0, i = 0;
	int j;

	if (chunk->cb)
		control = DW_EDMA_V0_CB;

	j = chunk->bursts_alloc;
	list_for_each_entry(child, &chunk->burst->list, list) {
		j--;
		if (!j) {
			control |= DW_EDMA_V0_LIE;
			if (!(chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL))
				control |= DW_EDMA_V0_RIE;
		}

		dw_edma_v0_write_ll_data(chunk, i++, control, child->sz,
					 child->sar, child->dar);
	}

	control = DW_EDMA_V0_LLP | DW_EDMA_V0_TCB;
	if (!chunk->cb)
		control |= DW_EDMA_V0_CB;

	dw_edma_v0_write_ll_link(chunk, i, control, chunk->ll_region.paddr);
}

static void dw_edma_v0_core_start(struct dw_edma_chunk *chunk, bool first)
{
	struct dw_edma_chan *chan = chunk->chan;
	struct dw_edma *dw = chan->dw;
	u32 tmp;

	dw_edma_v0_core_write_chunk(chunk);

	if (first) {
		/* Enable engine */
		SET_RW_32(dw, chan->dir, engine_en, BIT(0));
		if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
			switch (chan->id) {
			case 0:
				SET_RW_COMPAT(dw, chan->dir, ch0_pwr_en,
					      BIT(0));
				break;
			case 1:
				SET_RW_COMPAT(dw, chan->dir, ch1_pwr_en,
					      BIT(0));
				break;
			case 2:
				SET_RW_COMPAT(dw, chan->dir, ch2_pwr_en,
					      BIT(0));
				break;
			case 3:
				SET_RW_COMPAT(dw, chan->dir, ch3_pwr_en,
					      BIT(0));
				break;
			case 4:
				SET_RW_COMPAT(dw, chan->dir, ch4_pwr_en,
					      BIT(0));
				break;
			case 5:
				SET_RW_COMPAT(dw, chan->dir, ch5_pwr_en,
					      BIT(0));
				break;
			case 6:
				SET_RW_COMPAT(dw, chan->dir, ch6_pwr_en,
					      BIT(0));
				break;
			case 7:
				SET_RW_COMPAT(dw, chan->dir, ch7_pwr_en,
					      BIT(0));
				break;
			}
		}
		/* Interrupt unmask - done, abort */
		tmp = GET_RW_32(dw, chan->dir, int_mask);
		tmp &= ~FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id));
		tmp &= ~FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id));
		SET_RW_32(dw, chan->dir, int_mask, tmp);
		/* Linked list error */
		tmp = GET_RW_32(dw, chan->dir, linked_list_err_en);
		tmp |= FIELD_PREP(EDMA_V0_LINKED_LIST_ERR_MASK, BIT(chan->id));
		SET_RW_32(dw, chan->dir, linked_list_err_en, tmp);
		/* Channel control */
		SET_CH_32(dw, chan->dir, chan->id, ch_control1,
			  (DW_EDMA_V0_CCS | DW_EDMA_V0_LLE));
		/* Linked list */
		/* llp is not aligned on 64bit -> keep 32bit accesses */
		SET_CH_32(dw, chan->dir, chan->id, llp.lsb,
			  lower_32_bits(chunk->ll_region.paddr));
		SET_CH_32(dw, chan->dir, chan->id, llp.msb,
			  upper_32_bits(chunk->ll_region.paddr));
	}
	/* Doorbell */
	SET_RW_32(dw, chan->dir, doorbell,
		  FIELD_PREP(EDMA_V0_DOORBELL_CH_MASK, chan->id));
}

static void dw_edma_v0_core_ch_config(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	u32 tmp = 0;

	/* MSI done addr - low, high */
	SET_RW_32(dw, chan->dir, done_imwr.lsb, chan->msi.address_lo);
	SET_RW_32(dw, chan->dir, done_imwr.msb, chan->msi.address_hi);
	/* MSI abort addr - low, high */
	SET_RW_32(dw, chan->dir, abort_imwr.lsb, chan->msi.address_lo);
	SET_RW_32(dw, chan->dir, abort_imwr.msb, chan->msi.address_hi);
	/* MSI data - low, high */
	switch (chan->id) {
	case 0:
	case 1:
		tmp = GET_RW_32(dw, chan->dir, ch01_imwr_data);
		break;

	case 2:
	case 3:
		tmp = GET_RW_32(dw, chan->dir, ch23_imwr_data);
		break;

	case 4:
	case 5:
		tmp = GET_RW_32(dw, chan->dir, ch45_imwr_data);
		break;

	case 6:
	case 7:
		tmp = GET_RW_32(dw, chan->dir, ch67_imwr_data);
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
		SET_RW_32(dw, chan->dir, ch01_imwr_data, tmp);
		break;

	case 2:
	case 3:
		SET_RW_32(dw, chan->dir, ch23_imwr_data, tmp);
		break;

	case 4:
	case 5:
		SET_RW_32(dw, chan->dir, ch45_imwr_data, tmp);
		break;

	case 6:
	case 7:
		SET_RW_32(dw, chan->dir, ch67_imwr_data, tmp);
		break;
	}
}

/* eDMA debugfs callbacks */
static void dw_edma_v0_core_debugfs_on(struct dw_edma *dw)
{
	dw_edma_v0_debugfs_on(dw);
}

static const struct dw_edma_core_ops dw_edma_v0_core = {
	.off = dw_edma_v0_core_off,
	.ch_count = dw_edma_v0_core_ch_count,
	.ch_status = dw_edma_v0_core_ch_status,
	.handle_int = dw_edma_v0_core_handle_int,
	.start = dw_edma_v0_core_start,
	.ch_config = dw_edma_v0_core_ch_config,
	.debugfs_on = dw_edma_v0_core_debugfs_on,
};

void dw_edma_v0_core_register(struct dw_edma *dw)
{
	dw->core = &dw_edma_v0_core;
}
