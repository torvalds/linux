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
	DW_HDMA_V0_LWIE					= BIT(3),
	DW_HDMA_V0_RWIE					= BIT(4),
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

static u32 dw_hdma_v0_core_int_setup(struct dw_edma_chan *chan, u32 val)
{
	val &= ~(HDMA_V0_LOCAL_ABORT_INT_EN | HDMA_V0_REMOTE_ABORT_INT_EN |
		 HDMA_V0_LOCAL_STOP_INT_EN | HDMA_V0_REMOTE_STOP_INT_EN |
		 HDMA_V0_ABORT_INT_MASK | HDMA_V0_STOP_INT_MASK);

	/*
	 * HDMA_INT_STATUS.STOP and .ABORT are latched only when LSIE and
	 * LAIE are enabled. A remote handler needs those status bits to
	 * identify the source of the IMWr, so keep local generation enabled
	 * and mask the local interrupt pins instead.
	 */
	val |= HDMA_V0_LOCAL_ABORT_INT_EN | HDMA_V0_LOCAL_STOP_INT_EN;

	if (chan->irq_mode == DW_EDMA_CH_IRQ_REMOTE)
		val |= HDMA_V0_REMOTE_ABORT_INT_EN |
		       HDMA_V0_REMOTE_STOP_INT_EN |
		       HDMA_V0_ABORT_INT_MASK | HDMA_V0_STOP_INT_MASK;

	return val;
}

/* HDMA management callbacks */
static void dw_hdma_v0_core_ch_off(struct dw_edma *dw, enum dw_edma_dir dir,
				   u16 id)
{
	SET_CH_32(dw, dir, id, int_setup,
		  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
	SET_CH_32(dw, dir, id, ch_en, 0);
	SET_CH_32(dw, dir, id, int_clear,
		  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
}

static void dw_hdma_v0_core_off(struct dw_edma *dw)
{
	int id;
	enum dw_edma_dir dir;

	dir = EDMA_DIR_WRITE;
	for (id = 0; id < dw->wr_ch_cnt; id++) {
		SET_CH_32(dw, dir, id, int_setup,
			  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_CH_32(dw, dir, id, int_clear,
			  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_CH_32(dw, dir, id, ch_en, 0);
	}

	dir = EDMA_DIR_READ;
	for (id = 0; id < dw->rd_ch_cnt; id++) {
		SET_CH_32(dw, dir, id, int_setup,
			  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_CH_32(dw, dir, id, int_clear,
			  HDMA_V0_STOP_INT_MASK | HDMA_V0_ABORT_INT_MASK);
		SET_CH_32(dw, dir, id, ch_en, 0);
	}
}

static int dw_hdma_v0_core_quiesce(struct dw_edma *dw)
{
	int id;

	for (id = 0; id < dw->wr_ch_cnt; id++)
		dw_hdma_v0_core_ch_off(dw, EDMA_DIR_WRITE, id);
	for (id = 0; id < dw->rd_ch_cnt; id++)
		dw_hdma_v0_core_ch_off(dw, EDMA_DIR_READ, id);

	return 0;
}

static int dw_hdma_v0_core_ch_quiesce(struct dw_edma_chan *chan)
{
	dw_hdma_v0_core_ch_off(chan->dw, chan->dir, chan->id);

	return 0;
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
			GET_CH_32(dw, chan->dir, chan->id, ch_stat));

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
	unsigned long off, *mask;

	if (dir == EDMA_DIR_WRITE) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	for_each_set_bit(pos, mask, total) {
		chan = &dw->chan[pos + off];
		if (unlikely(dw_edma_core_ch_ignore_irq(chan)))
			continue;

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

static void dw_hdma_v0_write_ll_data(struct dw_edma_chan *chan, int i,
				     u32 control, u32 size, u64 sar, u64 dar)
{
	ptrdiff_t ofs = i * sizeof(struct dw_hdma_v0_lli);

	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_hdma_v0_lli *lli = chan->ll_region.vaddr.mem + ofs;

		lli->transfer_size = size;
		lli->sar.reg = sar;
		lli->dar.reg = dar;
		dma_wmb();
		lli->control = control;
	} else {
		struct dw_hdma_v0_lli __iomem *lli = chan->ll_region.vaddr.io + ofs;

		writel(size, &lli->transfer_size);
		writeq(sar, &lli->sar.reg);
		writeq(dar, &lli->dar.reg);
		writel(control, &lli->control);
	}
}

static void dw_hdma_v0_write_ll_link(struct dw_edma_chan *chan,
				     int i, u32 control, u64 pointer)
{
	ptrdiff_t ofs = i * sizeof(struct dw_hdma_v0_lli);

	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_hdma_v0_llp *llp = chan->ll_region.vaddr.mem + ofs;

		llp->llp.reg = pointer;
		dma_wmb();
		llp->control = control;
	} else {
		struct dw_hdma_v0_llp __iomem *llp = chan->ll_region.vaddr.io + ofs;

		writeq(pointer, &llp->llp.reg);
		writel(control, &llp->control);
	}
}

static void dw_hdma_v0_core_ch_enable(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	u32 tmp;

	/* Enable engine */
	SET_CH_32(dw, chan->dir, chan->id, ch_en, BIT(0));
	/* Interrupt unmask - stop, abort */
	tmp = GET_CH_32(dw, chan->dir, chan->id, int_setup);
	tmp = dw_hdma_v0_core_int_setup(chan, tmp);
	SET_CH_32(dw, chan->dir, chan->id, int_setup, tmp);
	/* Channel control */
	SET_CH_32(dw, chan->dir, chan->id, control1, HDMA_V0_LINKLIST_EN);
	/* Linked list */
	/* llp is not aligned on 64bit -> keep 32bit accesses */
	SET_CH_32(dw, chan->dir, chan->id, llp.lsb,
		  lower_32_bits(chan->ll_region.paddr));
	SET_CH_32(dw, chan->dir, chan->id, llp.msb,
		  upper_32_bits(chan->ll_region.paddr));
	/* Set consumer cycle */
	SET_CH_32(dw, chan->dir, chan->id, cycle_sync,
		  HDMA_V0_CONSUMER_CYCLE_STAT | HDMA_V0_CONSUMER_CYCLE_BIT);
}

static void dw_hdma_v0_sync_ll_data(struct dw_edma_chan *chan)
{
	/*
	 * In case of remote HDMA engine setup, the DW PCIe RP/EP internal
	 * configuration registers and application memory are normally accessed
	 * over different buses. Ensure LL-data reaches the memory before the
	 * doorbell register is toggled by issuing the dummy-read from the remote
	 * LL memory in a hope that the MRd TLP will return only after the
	 * last MWr TLP is completed
	 */
	if (!(chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL))
		readl(chan->ll_region.vaddr.io);
}

static void dw_hdma_v0_core_non_ll_start(struct dw_edma_chan *chan,
					 struct dw_edma_burst *child)
{
	struct dw_edma *dw = chan->dw;
	u32 val;

	SET_CH_32(dw, chan->dir, chan->id, ch_en, HDMA_V0_CH_EN);

	/* Source address */
	SET_CH_32(dw, chan->dir, chan->id, sar.lsb,
		  lower_32_bits(child->sar));
	SET_CH_32(dw, chan->dir, chan->id, sar.msb,
		  upper_32_bits(child->sar));

	/* Destination address */
	SET_CH_32(dw, chan->dir, chan->id, dar.lsb,
		  lower_32_bits(child->dar));
	SET_CH_32(dw, chan->dir, chan->id, dar.msb,
		  upper_32_bits(child->dar));

	/* Transfer size */
	SET_CH_32(dw, chan->dir, chan->id, transfer_size, child->sz);

	/* Interrupt setup */
	val = GET_CH_32(dw, chan->dir, chan->id, int_setup);
	val = dw_hdma_v0_core_int_setup(chan, val);
	SET_CH_32(dw, chan->dir, chan->id, int_setup, val);

	/* Channel control setup */
	val = GET_CH_32(dw, chan->dir, chan->id, control1);
	val &= ~HDMA_V0_LINKLIST_EN;
	SET_CH_32(dw, chan->dir, chan->id, control1, val);

	SET_CH_32(dw, chan->dir, chan->id, doorbell,
		  HDMA_V0_DOORBELL_START);
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
	/* Configure the requester function number used by outbound TLPs. */
	SET_CH_32(dw, chan->dir, chan->id, func_num,
		  FIELD_PREP(HDMA_V0_FUNC_NUM_PF_MASK, chan->func_no));
}

static void
dw_hdma_v0_core_ll_data(struct dw_edma_chan *chan, struct dw_edma_burst *burst,
			u32 idx, bool cb, bool irq)
{
	u32 control = 0;

	if (cb)
		control |= DW_HDMA_V0_CB;

	dw_hdma_v0_write_ll_data(chan, idx, control, burst->sz, burst->sar,
				 burst->dar);
}

static void
dw_hdma_v0_core_ll_link(struct dw_edma_chan *chan, u32 idx, bool cb, u64 addr)
{
	u32 control = DW_HDMA_V0_LLP | DW_HDMA_V0_TCB;

	if (!cb)
		control |= DW_HDMA_V0_CB;

	dw_hdma_v0_write_ll_link(chan, idx, control, addr);
}

static void dw_hdma_v0_core_ch_doorbell(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	dw_hdma_v0_sync_ll_data(chan);

	/* Doorbell */
	SET_CH_32(dw, chan->dir, chan->id, doorbell, HDMA_V0_DOORBELL_START);
}

/* HDMA debugfs callbacks */
static void dw_hdma_v0_core_debugfs_on(struct dw_edma *dw)
{
	dw_hdma_v0_debugfs_on(dw);
}

static resource_size_t dw_hdma_v0_core_db_offset(struct dw_edma *dw)
{
	/* Implement once the correct offset is known. */
	return ~0;
}

static const struct dw_edma_core_ops dw_hdma_v0_core = {
	.off = dw_hdma_v0_core_off,
	.quiesce = dw_hdma_v0_core_quiesce,
	.ch_quiesce = dw_hdma_v0_core_ch_quiesce,
	.ch_count = dw_hdma_v0_core_ch_count,
	.ch_status = dw_hdma_v0_core_ch_status,
	.handle_int = dw_hdma_v0_core_handle_int,
	.non_ll_start = dw_hdma_v0_core_non_ll_start,
	.ll_data = dw_hdma_v0_core_ll_data,
	.ll_link = dw_hdma_v0_core_ll_link,
	.ch_doorbell = dw_hdma_v0_core_ch_doorbell,
	.ch_enable = dw_hdma_v0_core_ch_enable,
	.ch_config = dw_hdma_v0_core_ch_config,
	.debugfs_on = dw_hdma_v0_core_debugfs_on,
	.db_offset = dw_hdma_v0_core_db_offset,
};

void dw_hdma_v0_core_register(struct dw_edma *dw)
{
	dw->core = &dw_hdma_v0_core;
}
