// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Cai Huoqing
 * Synopsys DesignWare HDMA v0 core
 */

#include <linux/bitfield.h>
#include <linux/irqreturn.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "dw-edma-core.h"
#include "dw-hdma-v0-core.h"
#include "dw-hdma-v0-regs.h"
#include "dw-hdma-v0-debugfs.h"

enum dw_hdma_control {
	DW_HDMA_V0_CB					= BIT(0),
	DW_HDMA_V0_TCB					= BIT(1),
	DW_HDMA_V0_LLP					= BIT(2),
	DW_HDMA_V0_LIE					= BIT(3),
	DW_HDMA_V0_RIE					= BIT(4),
	DW_HDMA_V0_CCS					= BIT(8),
	DW_HDMA_V0_LLE					= BIT(9),
};

static inline struct dw_hdma_v0_regs __iomem *__dw_regs(struct dw_edma *dw)
{
	return dw->chip->reg_base;
}

static inline struct dw_hdma_v0_ch_regs __iomem *
__dw_ch_regs(struct dw_edma *dw, enum dw_edma_dir dir, u16 ch)
{
	if (dir == EDMA_DIR_WRITE)
		return &(__dw_regs(dw)->ch[ch].wr);
	else
		return &(__dw_regs(dw)->ch[ch].rd);
}

#define SET_CH_32(dw, dir, ch, name, value) \
	writel(value, &(__dw_ch_regs(dw, dir, ch)->name))

#define GET_CH_32(dw, dir, ch, name) \
	readl(&(__dw_ch_regs(dw, dir, ch)->name))

#define SET_BOTH_CH_32(dw, ch, name, value) \
	do {					\
		writel(value, &(__dw_ch_regs(dw, EDMA_DIR_WRITE, ch)->name));	\
		writel(value, &(__dw_ch_regs(dw, EDMA_DIR_READ, ch)->name));	\
	} while (0)

/* HDMA management callbacks */
static void dw_hdma_v0_core_off(struct dw_edma *dw)
{
	int id;

	for (id = 0; id < HDMA_V0_MAX_NR_CH; id++) {
		SET_BOTH_CH_32(dw, id, int_setup,
			       HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_BOTH_CH_32(dw, id, int_clear,
			       HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_BOTH_CH_32(dw, id, ch_en, 0);
	}
}

static u16 dw_hdma_v0_core_ch_count(struct dw_edma *dw, enum dw_edma_dir dir)
{
	/*
	 * The HDMA IP have no way to know the number of hardware channels
	 * available, we set it to maximum channels and let the platform
	 * set the right number of channels.
	 */
	return HDMA_V0_MAX_NR_CH;
}

static enum dma_status dw_hdma_v0_core_ch_status(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	u32 tmp;

	tmp = FIELD_GET(HDMA_V0_CH_STATUS_MASK,
			GET_CH_32(dw, chan->id, chan->dir, ch_stat));

	if (tmp == 1)
		return DMA_IN_PROGRESS;
	else if (tmp == 3)
		return DMA_COMPLETE;
	else
		return DMA_ERROR;
}

static void dw_hdma_v0_core_clear_done_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	SET_CH_32(dw, chan->dir, chan->id, int_clear, HDMA_V0_STOP_INT_MASK);
}

static void dw_hdma_v0_core_clear_abort_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	SET_CH_32(dw, chan->dir, chan->id, int_clear, HDMA_V0_ABORT_INT_MASK);
}

static u32 dw_hdma_v0_core_status_int(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	return GET_CH_32(dw, chan->dir, chan->id, int_stat);
}

static irqreturn_t
dw_hdma_v0_core_handle_int(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
			   dw_edma_handler_t done, dw_edma_handler_t abort)
{
	struct dw_edma *dw = dw_irq->dw;
	unsigned long total, pos, val;
	irqreturn_t ret = IRQ_NONE;
	struct dw_edma_chan *chan;
	unsigned long off, mask;

	if (dir == EDMA_DIR_WRITE) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	for_each_set_bit(pos, &mask, total) {
		chan = &dw->chan[pos + off];

		val = dw_hdma_v0_core_status_int(chan);
		if (FIELD_GET(HDMA_V0_STOP_INT_MASK, val)) {
			dw_hdma_v0_core_clear_done_int(chan);
			done(chan);

			ret = IRQ_HANDLED;
		}

		if (FIELD_GET(HDMA_V0_ABORT_INT_MASK, val)) {
			dw_hdma_v0_core_clear_abort_int(chan);
			abort(chan);

			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static void dw_hdma_v0_write_ll_data(struct dw_edma_chunk *chunk, int i,
				     u32 control, u32 size, u64 sar, u64 dar)
{
	ptrdiff_t ofs = i * sizeof(struct dw_hdma_v0_lli);

	if (chunk->chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_hdma_v0_lli *lli = chunk->ll_region.vaddr.mem + ofs;

		lli->control = control;
		lli->transfer_size = size;
		lli->sar.reg = sar;
		lli->dar.reg = dar;
	} else {
		struct dw_hdma_v0_lli __iomem *lli = chunk->ll_region.vaddr.io + ofs;

		writel(control, &lli->control);
		writel(size, &lli->transfer_size);
		writeq(sar, &lli->sar.reg);
		writeq(dar, &lli->dar.reg);
	}
}

static void dw_hdma_v0_write_ll_link(struct dw_edma_chunk *chunk,
				     int i, u32 control, u64 pointer)
{
	ptrdiff_t ofs = i * sizeof(struct dw_hdma_v0_lli);

	if (chunk->chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_hdma_v0_llp *llp = chunk->ll_region.vaddr.mem + ofs;

		llp->control = control;
		llp->llp.reg = pointer;
	} else {
		struct dw_hdma_v0_llp __iomem *llp = chunk->ll_region.vaddr.io + ofs;

		writel(control, &llp->control);
		writeq(pointer, &llp->llp.reg);
	}
}

static void dw_hdma_v0_core_write_chunk(struct dw_edma_chunk *chunk)
{
	struct dw_edma_burst *child;
	struct dw_edma_chan *chan = chunk->chan;
	u32 control = 0, i = 0;
	int j;

	if (chunk->cb)
		control = DW_HDMA_V0_CB;

	j = chunk->bursts_alloc;
	list_for_each_entry(child, &chunk->burst->list, list) {
		j--;
		if (!j) {
			control |= DW_HDMA_V0_LIE;
			if (!(chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL))
				control |= DW_HDMA_V0_RIE;
		}

		dw_hdma_v0_write_ll_data(chunk, i++, control, child->sz,
					 child->sar, child->dar);
	}

	control = DW_HDMA_V0_LLP | DW_HDMA_V0_TCB;
	if (!chunk->cb)
		control |= DW_HDMA_V0_CB;

	dw_hdma_v0_write_ll_link(chunk, i, control, chunk->ll_region.paddr);
}

static void dw_hdma_v0_sync_ll_data(struct dw_edma_chunk *chunk)
{
	/*
	 * In case of remote HDMA engine setup, the DW PCIe RP/EP internal
	 * configuration registers and application memory are normally accessed
	 * over different buses. Ensure LL-data reaches the memory before the
	 * doorbell register is toggled by issuing the dummy-read from the remote
	 * LL memory in a hope that the MRd TLP will return only after the
	 * last MWr TLP is completed
	 */
	if (!(chunk->chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL))
		readl(chunk->ll_region.vaddr.io);
}

static void dw_hdma_v0_core_start(struct dw_edma_chunk *chunk, bool first)
{
	struct dw_edma_chan *chan = chunk->chan;
	struct dw_edma *dw = chan->dw;
	u32 tmp;

	dw_hdma_v0_core_write_chunk(chunk);

	if (first) {
		/* Enable engine */
		SET_CH_32(dw, chan->dir, chan->id, ch_en, BIT(0));
		/* Interrupt unmask - stop, abort */
		tmp = GET_CH_32(dw, chan->dir, chan->id, int_setup);
		tmp &= ~(HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		/* Interrupt enable - stop, abort */
		tmp |= HDMA_V0_LOCAL_STOP_INT_EN | HDMA_V0_LOCAL_ABORT_INT_EN;
		if (!(dw->chip->flags & DW_EDMA_CHIP_LOCAL))
			tmp |= HDMA_V0_REMOTE_STOP_INT_EN | HDMA_V0_REMOTE_ABORT_INT_EN;
		SET_CH_32(dw, chan->dir, chan->id, int_setup, tmp);
		/* Channel control */
		SET_CH_32(dw, chan->dir, chan->id, control1, HDMA_V0_LINKLIST_EN);
		/* Linked list */
		/* llp is not aligned on 64bit -> keep 32bit accesses */
		SET_CH_32(dw, chan->dir, chan->id, llp.lsb,
			  lower_32_bits(chunk->ll_region.paddr));
		SET_CH_32(dw, chan->dir, chan->id, llp.msb,
			  upper_32_bits(chunk->ll_region.paddr));
	}
	/* Set consumer cycle */
	SET_CH_32(dw, chan->dir, chan->id, cycle_sync,
		  HDMA_V0_CONSUMER_CYCLE_STAT | HDMA_V0_CONSUMER_CYCLE_BIT);

	dw_hdma_v0_sync_ll_data(chunk);

	/* Doorbell */
	SET_CH_32(dw, chan->dir, chan->id, doorbell, HDMA_V0_DOORBELL_START);
}

static void dw_hdma_v0_core_ch_config(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	/* MSI done addr - low, high */
	SET_CH_32(dw, chan->dir, chan->id, msi_stop.lsb, chan->msi.address_lo);
	SET_CH_32(dw, chan->dir, chan->id, msi_stop.msb, chan->msi.address_hi);
	/* MSI abort addr - low, high */
	SET_CH_32(dw, chan->dir, chan->id, msi_abort.lsb, chan->msi.address_lo);
	SET_CH_32(dw, chan->dir, chan->id, msi_abort.msb, chan->msi.address_hi);
	/* config MSI data */
	SET_CH_32(dw, chan->dir, chan->id, msi_msgdata, chan->msi.data);
}

/* HDMA debugfs callbacks */
static void dw_hdma_v0_core_debugfs_on(struct dw_edma *dw)
{
	dw_hdma_v0_debugfs_on(dw);
}

static const struct dw_edma_core_ops dw_hdma_v0_core = {
	.off = dw_hdma_v0_core_off,
	.ch_count = dw_hdma_v0_core_ch_count,
	.ch_status = dw_hdma_v0_core_ch_status,
	.handle_int = dw_hdma_v0_core_handle_int,
	.start = dw_hdma_v0_core_start,
	.ch_config = dw_hdma_v0_core_ch_config,
	.debugfs_on = dw_hdma_v0_core_debugfs_on,
};

void dw_hdma_v0_core_register(struct dw_edma *dw)
{
	dw->core = &dw_hdma_v0_core;
}
