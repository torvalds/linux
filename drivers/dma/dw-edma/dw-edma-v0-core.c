// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/bitfield.h>
#include <linux/iopoll.h>
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

#define EDMA_V0_FUNC_NUM_MASK				GENMASK(16, 12)

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

static u32 dw_edma_v0_func_num(struct dw_edma_chan *chan)
{
	return FIELD_PREP(EDMA_V0_FUNC_NUM_MASK, chan->func_no);
}

/* eDMA management callbacks */
static void dw_edma_v0_core_ch_power(struct dw_edma *dw,
				     enum dw_edma_dir dir, u16 id, bool enable)
{
	u32 value = enable ? BIT(0) : 0;

	if (WARN_ON_ONCE(id >= EDMA_V0_MAX_NR_CH))
		return;

	switch (id) {
	case 0:
		SET_RW_COMPAT(dw, dir, ch0_pwr_en, value);
		break;
	case 1:
		SET_RW_COMPAT(dw, dir, ch1_pwr_en, value);
		break;
	case 2:
		SET_RW_COMPAT(dw, dir, ch2_pwr_en, value);
		break;
	case 3:
		SET_RW_COMPAT(dw, dir, ch3_pwr_en, value);
		break;
	case 4:
		SET_RW_COMPAT(dw, dir, ch4_pwr_en, value);
		break;
	case 5:
		SET_RW_COMPAT(dw, dir, ch5_pwr_en, value);
		break;
	case 6:
		SET_RW_COMPAT(dw, dir, ch6_pwr_en, value);
		break;
	case 7:
		SET_RW_COMPAT(dw, dir, ch7_pwr_en, value);
		break;
	}
}

static int dw_edma_v0_core_engine_disable(struct dw_edma *dw,
					  enum dw_edma_dir dir)
{
	u32 value;
	int ret;

	SET_RW_32(dw, dir, engine_en, 0);
	ret = read_poll_timeout(GET_RW_32, value, !(value & BIT(0)), 100,
				200000, false, dw, dir, engine_en);
	if (ret)
		dev_warn(dw->chip->dev, "%s engine did not stop within 200ms\n",
			 dir == EDMA_DIR_WRITE ? "write" : "read");

	return ret;
}

static int dw_edma_v0_core_dir_off(struct dw_edma *dw, enum dw_edma_dir dir)
{
	u16 count, id;
	int ret = 0;

	scoped_guard(raw_spinlock_irqsave, &dw->lock)
		SET_RW_32(dw, dir, int_mask,
			  EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);

	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
		/*
		 * DWC PCIe Controller Databook 6.10a-lca06, "Legacy DMA
		 * and HDMA Software Compatibility": HDMA compatibility mode
		 * does not implement ENGINE_EN, but retains CHi_PWR_EN for
		 * per-channel enable and disable.
		 */
		count = dir == EDMA_DIR_WRITE ? dw->wr_ch_cnt : dw->rd_ch_cnt;
		for (id = 0; id < count; id++)
			dw_edma_v0_core_ch_power(dw, dir, id, false);
	} else {
		ret = dw_edma_v0_core_engine_disable(dw, dir);
	}

	SET_RW_32(dw, dir, int_clear,
		  EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);

	return ret;
}

static void dw_edma_v0_core_off(struct dw_edma *dw)
{
	SET_BOTH_32(dw, int_mask,
		    EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH_32(dw, int_clear,
		    EDMA_V0_DONE_INT_MASK | EDMA_V0_ABORT_INT_MASK);
	SET_BOTH_32(dw, engine_en, 0);
}

static int dw_edma_v0_core_quiesce(struct dw_edma *dw)
{
	int ret = 0;
	int err;

	if (dw->wr_ch_cnt)
		ret = dw_edma_v0_core_dir_off(dw, EDMA_DIR_WRITE);
	if (dw->rd_ch_cnt) {
		err = dw_edma_v0_core_dir_off(dw, EDMA_DIR_READ);
		if (!ret)
			ret = err;
	}

	return ret;
}

/*
 * The unrolled eDMA and HDMA compatibility register maps share interrupt
 * control per direction, so the whole direction is quiesced. Callers must
 * own the direction entirely and prevent the peer from programming it after
 * this point. Partial ownership mode validates direction granularity.
 */
static int dw_edma_v0_core_ch_quiesce(struct dw_edma_chan *chan)
{
	return dw_edma_v0_core_dir_off(chan->dw, chan->dir);
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

static irqreturn_t
dw_edma_v0_core_handle_int(struct dw_edma_irq *dw_irq, enum dw_edma_dir dir,
			   dw_edma_handler_t done, dw_edma_handler_t abort)
{
	struct dw_edma *dw = dw_irq->dw;
	unsigned long total, pos, val;
	irqreturn_t ret = IRQ_NONE;
	struct dw_edma_chan *chan;
	unsigned long off;
	unsigned long *mask;
	u32 sts;

	if (dir == EDMA_DIR_WRITE) {
		total = dw->wr_ch_cnt;
		off = 0;
		mask = dw_irq->wr_mask;
	} else {
		total = dw->rd_ch_cnt;
		off = dw->wr_ch_cnt;
		mask = dw_irq->rd_mask;
	}

	/*
	 * DONE and ABORT status share one register, and on remote setups
	 * every read is a non-posted round trip across the PCIe link. Take
	 * one snapshot and derive both views from it. An abort raised
	 * after the snapshot is deferred, not lost: only bits observed in
	 * the snapshot are ever cleared below, so its status remains set and
	 * triggers another handler pass.
	 */
	sts = GET_RW_32(dw, dir, int_status);

	val = FIELD_GET(EDMA_V0_DONE_INT_MASK, sts);
	val &= *mask;
	for_each_set_bit(pos, &val, total) {
		chan = &dw->chan[pos + off];

		if (unlikely(dw_edma_core_ch_ignore_irq(chan)))
			continue;

		dw_edma_v0_core_clear_done_int(chan);
		done(chan);

		ret = IRQ_HANDLED;
	}

	val = FIELD_GET(EDMA_V0_ABORT_INT_MASK, sts);
	val &= *mask;
	for_each_set_bit(pos, &val, total) {
		chan = &dw->chan[pos + off];

		if (unlikely(dw_edma_core_ch_ignore_irq(chan)))
			continue;

		dw_edma_v0_core_clear_abort_int(chan);
		abort(chan);

		ret = IRQ_HANDLED;
	}

	return ret;
}

static void dw_edma_v0_write_ll_data(struct dw_edma_chan *chan, int i,
				     u32 control, u32 size, u64 sar, u64 dar)
{
	ptrdiff_t ofs = i * sizeof(struct dw_edma_v0_lli);

	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_edma_v0_lli *lli = chan->ll_region.vaddr.mem + ofs;

		lli->transfer_size = size;
		lli->sar.reg = sar;
		lli->dar.reg = dar;
		dma_wmb();
		lli->control = control;
	} else {
		struct dw_edma_v0_lli __iomem *lli = chan->ll_region.vaddr.io + ofs;

		writel(size, &lli->transfer_size);
		writeq(sar, &lli->sar.reg);
		writeq(dar, &lli->dar.reg);
		writel(control, &lli->control);
	}
}

static void dw_edma_v0_write_ll_link(struct dw_edma_chan *chan,
				     int i, u32 control, u64 pointer)
{
	ptrdiff_t ofs = i * sizeof(struct dw_edma_v0_lli);

	if (chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) {
		struct dw_edma_v0_llp *llp = chan->ll_region.vaddr.mem + ofs;

		llp->llp.reg = pointer;
		dma_wmb();
		llp->control = control;
	} else {
		struct dw_edma_v0_llp __iomem *llp = chan->ll_region.vaddr.io + ofs;

		writeq(pointer, &llp->llp.reg);
		writel(control, &llp->control);
	}
}

static void dw_edma_v0_core_ch_enable(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;
	unsigned long flags;
	u32 tmp;

	 /* Enable engine */
	SET_RW_32(dw, chan->dir, engine_en, BIT(0));
	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT)
		dw_edma_v0_core_ch_power(dw, chan->dir, chan->id, true);
	/* Interrupt mask/unmask - done, abort */
	raw_spin_lock_irqsave(&dw->lock, flags);

	tmp = GET_RW_32(dw, chan->dir, int_mask);
	if (chan->irq_mode == DW_EDMA_CH_IRQ_REMOTE) {
		tmp |= FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id));
		tmp |= FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id));
	} else {
		tmp &= ~FIELD_PREP(EDMA_V0_DONE_INT_MASK, BIT(chan->id));
		tmp &= ~FIELD_PREP(EDMA_V0_ABORT_INT_MASK, BIT(chan->id));
	}
	SET_RW_32(dw, chan->dir, int_mask, tmp);
	/* Linked list error */
	tmp = GET_RW_32(dw, chan->dir, linked_list_err_en);
	tmp |= FIELD_PREP(EDMA_V0_LINKED_LIST_ERR_MASK, BIT(chan->id));
	SET_RW_32(dw, chan->dir, linked_list_err_en, tmp);

	raw_spin_unlock_irqrestore(&dw->lock, flags);

	/* Channel control */
	SET_CH_32(dw, chan->dir, chan->id, ch_control1,
		  DW_EDMA_V0_CCS | DW_EDMA_V0_LLE |
		  dw_edma_v0_func_num(chan));
	/* Linked list */
	/* llp is not aligned on 64bit -> keep 32bit accesses */
	SET_CH_32(dw, chan->dir, chan->id, llp.lsb,
		  lower_32_bits(chan->ll_region.paddr));
	SET_CH_32(dw, chan->dir, chan->id, llp.msb,
		  upper_32_bits(chan->ll_region.paddr));
}

static void dw_edma_v0_sync_ll_data(struct dw_edma_chan *chan)
{
	/*
	 * In case of remote eDMA engine setup, the DW PCIe RP/EP internal
	 * configuration registers and application memory are normally accessed
	 * over different buses. Ensure LL-data reaches the memory before the
	 * doorbell register is toggled by issuing the dummy-read from the remote
	 * LL memory in a hope that the MRd TLP will return only after the
	 * last MWr TLP is completed
	 */
	if (!(chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL))
		readl(chan->ll_region.vaddr.io);
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

static void
dw_edma_v0_core_ll_data(struct dw_edma_chan *chan, struct dw_edma_burst *burst,
			u32 idx, bool cb, bool irq)
{
	u32 control = 0;

	if (cb)
		control |= DW_EDMA_V0_CB;

	if (irq) {
		control |= DW_EDMA_V0_LIE;

		/*
		 * A local instance never issues transfers on a remote-routed
		 * channel: on CHIP_LOCAL instances, REMOTE routing denotes a
		 * channel handed over to the remote side, which programs the
		 * linked list through its own instance. The remote-only
		 * recipe (LIE|RIE with the local interrupt masked) is thus
		 * applied by the instance that owns the transfer, and the
		 * LIE-only write below never executes for a remote-routed
		 * channel.
		 */
		if (!(chan->dw->chip->flags & DW_EDMA_CHIP_LOCAL) &&
		    chan->irq_mode == DW_EDMA_CH_IRQ_REMOTE)
			control |= DW_EDMA_V0_RIE;
	}

	dw_edma_v0_write_ll_data(chan, idx, control, burst->sz, burst->sar,
				 burst->dar);
}

static void
dw_edma_v0_core_ll_link(struct dw_edma_chan *chan, u32 idx, bool cb, u64 addr)
{
	u32 control = DW_EDMA_V0_LLP | DW_EDMA_V0_TCB;

	if (!cb)
		control |= DW_EDMA_V0_CB;

	dw_edma_v0_write_ll_link(chan, idx, control, addr);
}

static void dw_edma_v0_core_ch_doorbell(struct dw_edma_chan *chan)
{
	struct dw_edma *dw = chan->dw;

	dw_edma_v0_sync_ll_data(chan);

	/* Doorbell */
	SET_RW_32(dw, chan->dir, doorbell,
		  FIELD_PREP(EDMA_V0_DOORBELL_CH_MASK, chan->id));
}

/* eDMA debugfs callbacks */
static void dw_edma_v0_core_debugfs_on(struct dw_edma *dw)
{
	dw_edma_v0_debugfs_on(dw);
}

static void dw_edma_v0_core_ack_emulated_irq(struct dw_edma *dw)
{
	/*
	 * Interrupt emulation may assert the IRQ without setting
	 * DONE/ABORT status bits. A zero write to INT_CLEAR deasserts the
	 * emulated IRQ, while being a no-op for real interrupts.
	 */
	SET_BOTH_32(dw, int_clear, 0);
}

static resource_size_t dw_edma_v0_core_db_offset(struct dw_edma *dw)
{
	/*
	 * rd_int_status is chosen arbitrarily, but wr_int_status would be
	 * equally suitable.
	 */
	return offsetof(struct dw_edma_v0_regs, rd_int_status);
}

static const struct dw_edma_core_ops dw_edma_v0_core = {
	.off = dw_edma_v0_core_off,
	.quiesce = dw_edma_v0_core_quiesce,
	.ch_quiesce = dw_edma_v0_core_ch_quiesce,
	.ch_count = dw_edma_v0_core_ch_count,
	.ch_status = dw_edma_v0_core_ch_status,
	.handle_int = dw_edma_v0_core_handle_int,
	.ll_data = dw_edma_v0_core_ll_data,
	.ll_link = dw_edma_v0_core_ll_link,
	.ch_doorbell = dw_edma_v0_core_ch_doorbell,
	.ch_enable = dw_edma_v0_core_ch_enable,
	.ch_config = dw_edma_v0_core_ch_config,
	.debugfs_on = dw_edma_v0_core_debugfs_on,
	.ack_emulated_irq = dw_edma_v0_core_ack_emulated_irq,
	.db_offset = dw_edma_v0_core_db_offset,
};

void dw_edma_v0_core_register(struct dw_edma *dw)
{
	dw->core = &dw_edma_v0_core;
}
