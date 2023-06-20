// SPDX-License-Identifier: GPL-2.0
/*
 * Lightning Mountain centralized DMA controller driver
 *
 * Copyright (c) 2016 - 2020 Intel Corporation.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "../dmaengine.h"
#include "../virt-dma.h"

#define DRIVER_NAME			"lgm-dma"

#define DMA_ID				0x0008
#define DMA_ID_REV			GENMASK(7, 0)
#define DMA_ID_PNR			GENMASK(19, 16)
#define DMA_ID_CHNR			GENMASK(26, 20)
#define DMA_ID_DW_128B			BIT(27)
#define DMA_ID_AW_36B			BIT(28)
#define DMA_VER32			0x32
#define DMA_VER31			0x31
#define DMA_VER22			0x0A

#define DMA_CTRL			0x0010
#define DMA_CTRL_RST			BIT(0)
#define DMA_CTRL_DSRAM_PATH		BIT(1)
#define DMA_CTRL_DBURST_WR		BIT(3)
#define DMA_CTRL_VLD_DF_ACK		BIT(4)
#define DMA_CTRL_CH_FL			BIT(6)
#define DMA_CTRL_DS_FOD			BIT(7)
#define DMA_CTRL_DRB			BIT(8)
#define DMA_CTRL_ENBE			BIT(9)
#define DMA_CTRL_DESC_TMOUT_CNT_V31	GENMASK(27, 16)
#define DMA_CTRL_DESC_TMOUT_EN_V31	BIT(30)
#define DMA_CTRL_PKTARB			BIT(31)

#define DMA_CPOLL			0x0014
#define DMA_CPOLL_CNT			GENMASK(15, 4)
#define DMA_CPOLL_EN			BIT(31)

#define DMA_CS				0x0018
#define DMA_CS_MASK			GENMASK(5, 0)

#define DMA_CCTRL			0x001C
#define DMA_CCTRL_ON			BIT(0)
#define DMA_CCTRL_RST			BIT(1)
#define DMA_CCTRL_CH_POLL_EN		BIT(2)
#define DMA_CCTRL_CH_ABC		BIT(3) /* Adaptive Burst Chop */
#define DMA_CDBA_MSB			GENMASK(7, 4)
#define DMA_CCTRL_DIR_TX		BIT(8)
#define DMA_CCTRL_CLASS			GENMASK(11, 9)
#define DMA_CCTRL_CLASSH		GENMASK(19, 18)
#define DMA_CCTRL_WR_NP_EN		BIT(21)
#define DMA_CCTRL_PDEN			BIT(23)
#define DMA_MAX_CLASS			(SZ_32 - 1)

#define DMA_CDBA			0x0020
#define DMA_CDLEN			0x0024
#define DMA_CIS				0x0028
#define DMA_CIE				0x002C
#define DMA_CI_EOP			BIT(1)
#define DMA_CI_DUR			BIT(2)
#define DMA_CI_DESCPT			BIT(3)
#define DMA_CI_CHOFF			BIT(4)
#define DMA_CI_RDERR			BIT(5)
#define DMA_CI_ALL							\
	(DMA_CI_EOP | DMA_CI_DUR | DMA_CI_DESCPT | DMA_CI_CHOFF | DMA_CI_RDERR)

#define DMA_PS				0x0040
#define DMA_PCTRL			0x0044
#define DMA_PCTRL_RXBL16		BIT(0)
#define DMA_PCTRL_TXBL16		BIT(1)
#define DMA_PCTRL_RXBL			GENMASK(3, 2)
#define DMA_PCTRL_RXBL_8		3
#define DMA_PCTRL_TXBL			GENMASK(5, 4)
#define DMA_PCTRL_TXBL_8		3
#define DMA_PCTRL_PDEN			BIT(6)
#define DMA_PCTRL_RXBL32		BIT(7)
#define DMA_PCTRL_RXENDI		GENMASK(9, 8)
#define DMA_PCTRL_TXENDI		GENMASK(11, 10)
#define DMA_PCTRL_TXBL32		BIT(15)
#define DMA_PCTRL_MEM_FLUSH		BIT(16)

#define DMA_IRNEN1			0x00E8
#define DMA_IRNCR1			0x00EC
#define DMA_IRNEN			0x00F4
#define DMA_IRNCR			0x00F8
#define DMA_C_DP_TICK			0x100
#define DMA_C_DP_TICK_TIKNARB		GENMASK(15, 0)
#define DMA_C_DP_TICK_TIKARB		GENMASK(31, 16)

#define DMA_C_HDRM			0x110
/*
 * If header mode is set in DMA descriptor,
 *   If bit 30 is disabled, HDR_LEN must be configured according to channel
 *     requirement.
 *   If bit 30 is enabled(checksum with heade mode), HDR_LEN has no need to
 *     be configured. It will enable check sum for switch
 * If header mode is not set in DMA descriptor,
 *   This register setting doesn't matter
 */
#define DMA_C_HDRM_HDR_SUM		BIT(30)

#define DMA_C_BOFF			0x120
#define DMA_C_BOFF_BOF_LEN		GENMASK(7, 0)
#define DMA_C_BOFF_EN			BIT(31)

#define DMA_ORRC			0x190
#define DMA_ORRC_ORRCNT			GENMASK(8, 4)
#define DMA_ORRC_EN			BIT(31)

#define DMA_C_ENDIAN			0x200
#define DMA_C_END_DATAENDI		GENMASK(1, 0)
#define DMA_C_END_DE_EN			BIT(7)
#define DMA_C_END_DESENDI		GENMASK(9, 8)
#define DMA_C_END_DES_EN		BIT(16)

/* DMA controller capability */
#define DMA_ADDR_36BIT			BIT(0)
#define DMA_DATA_128BIT			BIT(1)
#define DMA_CHAN_FLOW_CTL		BIT(2)
#define DMA_DESC_FOD			BIT(3)
#define DMA_DESC_IN_SRAM		BIT(4)
#define DMA_EN_BYTE_EN			BIT(5)
#define DMA_DBURST_WR			BIT(6)
#define DMA_VALID_DESC_FETCH_ACK	BIT(7)
#define DMA_DFT_DRB			BIT(8)

#define DMA_ORRC_MAX_CNT		(SZ_32 - 1)
#define DMA_DFT_POLL_CNT		SZ_4
#define DMA_DFT_BURST_V22		SZ_2
#define DMA_BURSTL_8DW			SZ_8
#define DMA_BURSTL_16DW			SZ_16
#define DMA_BURSTL_32DW			SZ_32
#define DMA_DFT_BURST			DMA_BURSTL_16DW
#define DMA_MAX_DESC_NUM		(SZ_8K - 1)
#define DMA_CHAN_BOFF_MAX		(SZ_256 - 1)
#define DMA_DFT_ENDIAN			0

#define DMA_DFT_DESC_TCNT		50
#define DMA_HDR_LEN_MAX			(SZ_16K - 1)

/* DMA flags */
#define DMA_TX_CH			BIT(0)
#define DMA_RX_CH			BIT(1)
#define DEVICE_ALLOC_DESC		BIT(2)
#define CHAN_IN_USE			BIT(3)
#define DMA_HW_DESC			BIT(4)

/* Descriptor fields */
#define DESC_DATA_LEN			GENMASK(15, 0)
#define DESC_BYTE_OFF			GENMASK(25, 23)
#define DESC_EOP			BIT(28)
#define DESC_SOP			BIT(29)
#define DESC_C				BIT(30)
#define DESC_OWN			BIT(31)

#define DMA_CHAN_RST			1
#define DMA_MAX_SIZE			(BIT(16) - 1)
#define MAX_LOWER_CHANS			32
#define MASK_LOWER_CHANS		GENMASK(4, 0)
#define DMA_OWN				1
#define HIGH_4_BITS			GENMASK(3, 0)
#define DMA_DFT_DESC_NUM		1
#define DMA_PKT_DROP_DIS		0

enum ldma_chan_on_off {
	DMA_CH_OFF = 0,
	DMA_CH_ON = 1,
};

enum {
	DMA_TYPE_TX = 0,
	DMA_TYPE_RX,
	DMA_TYPE_MCPY,
};

struct ldma_dev;
struct ldma_port;

struct ldma_chan {
	struct virt_dma_chan	vchan;
	struct ldma_port	*port; /* back pointer */
	char			name[8]; /* Channel name */
	int			nr; /* Channel id in hardware */
	u32			flags; /* central way or channel based way */
	enum ldma_chan_on_off	onoff;
	dma_addr_t		desc_phys;
	void			*desc_base; /* Virtual address */
	u32			desc_cnt; /* Number of descriptors */
	int			rst;
	u32			hdrm_len;
	bool			hdrm_csum;
	u32			boff_len;
	u32			data_endian;
	u32			desc_endian;
	bool			pden;
	bool			desc_rx_np;
	bool			data_endian_en;
	bool			desc_endian_en;
	bool			abc_en;
	bool			desc_init;
	struct dma_pool		*desc_pool; /* Descriptors pool */
	u32			desc_num;
	struct dw2_desc_sw	*ds;
	struct work_struct	work;
	struct dma_slave_config config;
};

struct ldma_port {
	struct ldma_dev		*ldev; /* back pointer */
	u32			portid;
	u32			rxbl;
	u32			txbl;
	u32			rxendi;
	u32			txendi;
	u32			pkt_drop;
};

/* Instance specific data */
struct ldma_inst_data {
	bool			desc_in_sram;
	bool			chan_fc;
	bool			desc_fod; /* Fetch On Demand */
	bool			valid_desc_fetch_ack;
	u32			orrc; /* Outstanding read count */
	const char		*name;
	u32			type;
};

struct ldma_dev {
	struct device		*dev;
	void __iomem		*base;
	struct reset_control	*rst;
	struct clk		*core_clk;
	struct dma_device	dma_dev;
	u32			ver;
	int			irq;
	struct ldma_port	*ports;
	struct ldma_chan	*chans; /* channel list on this DMA or port */
	spinlock_t		dev_lock; /* Controller register exclusive */
	u32			chan_nrs;
	u32			port_nrs;
	u32			channels_mask;
	u32			flags;
	u32			pollcnt;
	const struct ldma_inst_data *inst;
	struct workqueue_struct	*wq;
};

struct dw2_desc {
	u32 field;
	u32 addr;
} __packed __aligned(8);

struct dw2_desc_sw {
	struct virt_dma_desc	vdesc;
	struct ldma_chan	*chan;
	dma_addr_t		desc_phys;
	size_t			desc_cnt;
	size_t			size;
	struct dw2_desc		*desc_hw;
};

static inline void
ldma_update_bits(struct ldma_dev *d, u32 mask, u32 val, u32 ofs)
{
	u32 old_val, new_val;

	old_val = readl(d->base +  ofs);
	new_val = (old_val & ~mask) | (val & mask);

	if (new_val != old_val)
		writel(new_val, d->base + ofs);
}

static inline struct ldma_chan *to_ldma_chan(struct dma_chan *chan)
{
	return container_of(chan, struct ldma_chan, vchan.chan);
}

static inline struct ldma_dev *to_ldma_dev(struct dma_device *dma_dev)
{
	return container_of(dma_dev, struct ldma_dev, dma_dev);
}

static inline struct dw2_desc_sw *to_lgm_dma_desc(struct virt_dma_desc *vdesc)
{
	return container_of(vdesc, struct dw2_desc_sw, vdesc);
}

static inline bool ldma_chan_tx(struct ldma_chan *c)
{
	return !!(c->flags & DMA_TX_CH);
}

static inline bool ldma_chan_is_hw_desc(struct ldma_chan *c)
{
	return !!(c->flags & DMA_HW_DESC);
}

static void ldma_dev_reset(struct ldma_dev *d)

{
	unsigned long flags;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CTRL_RST, DMA_CTRL_RST, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_pkt_arb_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask = DMA_CTRL_PKTARB;
	u32 val = enable ? DMA_CTRL_PKTARB : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_sram_desc_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask = DMA_CTRL_DSRAM_PATH;
	u32 val = enable ? DMA_CTRL_DSRAM_PATH : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_chan_flow_ctl_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask, val;

	if (d->inst->type != DMA_TYPE_TX)
		return;

	mask = DMA_CTRL_CH_FL;
	val = enable ? DMA_CTRL_CH_FL : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_global_polling_enable(struct ldma_dev *d)
{
	unsigned long flags;
	u32 mask = DMA_CPOLL_EN | DMA_CPOLL_CNT;
	u32 val = DMA_CPOLL_EN;

	val |= FIELD_PREP(DMA_CPOLL_CNT, d->pollcnt);

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CPOLL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_desc_fetch_on_demand_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask, val;

	if (d->inst->type == DMA_TYPE_MCPY)
		return;

	mask = DMA_CTRL_DS_FOD;
	val = enable ? DMA_CTRL_DS_FOD : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_byte_enable_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask = DMA_CTRL_ENBE;
	u32 val = enable ? DMA_CTRL_ENBE : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_orrc_cfg(struct ldma_dev *d)
{
	unsigned long flags;
	u32 val = 0;
	u32 mask;

	if (d->inst->type == DMA_TYPE_RX)
		return;

	mask = DMA_ORRC_EN | DMA_ORRC_ORRCNT;
	if (d->inst->orrc > 0 && d->inst->orrc <= DMA_ORRC_MAX_CNT)
		val = DMA_ORRC_EN | FIELD_PREP(DMA_ORRC_ORRCNT, d->inst->orrc);

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_ORRC);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_df_tout_cfg(struct ldma_dev *d, bool enable, int tcnt)
{
	u32 mask = DMA_CTRL_DESC_TMOUT_CNT_V31;
	unsigned long flags;
	u32 val;

	if (enable)
		val = DMA_CTRL_DESC_TMOUT_EN_V31 | FIELD_PREP(DMA_CTRL_DESC_TMOUT_CNT_V31, tcnt);
	else
		val = 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_dburst_wr_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask, val;

	if (d->inst->type != DMA_TYPE_RX && d->inst->type != DMA_TYPE_MCPY)
		return;

	mask = DMA_CTRL_DBURST_WR;
	val = enable ? DMA_CTRL_DBURST_WR : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_vld_fetch_ack_cfg(struct ldma_dev *d, bool enable)
{
	unsigned long flags;
	u32 mask, val;

	if (d->inst->type != DMA_TYPE_TX)
		return;

	mask = DMA_CTRL_VLD_DF_ACK;
	val = enable ? DMA_CTRL_VLD_DF_ACK : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_dev_drb_cfg(struct ldma_dev *d, int enable)
{
	unsigned long flags;
	u32 mask = DMA_CTRL_DRB;
	u32 val = enable ? DMA_CTRL_DRB : 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, mask, val, DMA_CTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static int ldma_dev_cfg(struct ldma_dev *d)
{
	bool enable;

	ldma_dev_pkt_arb_cfg(d, true);
	ldma_dev_global_polling_enable(d);

	enable = !!(d->flags & DMA_DFT_DRB);
	ldma_dev_drb_cfg(d, enable);

	enable = !!(d->flags & DMA_EN_BYTE_EN);
	ldma_dev_byte_enable_cfg(d, enable);

	enable = !!(d->flags & DMA_CHAN_FLOW_CTL);
	ldma_dev_chan_flow_ctl_cfg(d, enable);

	enable = !!(d->flags & DMA_DESC_FOD);
	ldma_dev_desc_fetch_on_demand_cfg(d, enable);

	enable = !!(d->flags & DMA_DESC_IN_SRAM);
	ldma_dev_sram_desc_cfg(d, enable);

	enable = !!(d->flags & DMA_DBURST_WR);
	ldma_dev_dburst_wr_cfg(d, enable);

	enable = !!(d->flags & DMA_VALID_DESC_FETCH_ACK);
	ldma_dev_vld_fetch_ack_cfg(d, enable);

	if (d->ver > DMA_VER22) {
		ldma_dev_orrc_cfg(d);
		ldma_dev_df_tout_cfg(d, true, DMA_DFT_DESC_TCNT);
	}

	dev_dbg(d->dev, "%s Controller 0x%08x configuration done\n",
		d->inst->name, readl(d->base + DMA_CTRL));

	return 0;
}

static int ldma_chan_cctrl_cfg(struct ldma_chan *c, u32 val)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 class_low, class_high;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	reg = readl(d->base + DMA_CCTRL);
	/* Read from hardware */
	if (reg & DMA_CCTRL_DIR_TX)
		c->flags |= DMA_TX_CH;
	else
		c->flags |= DMA_RX_CH;

	/* Keep the class value unchanged */
	class_low = FIELD_GET(DMA_CCTRL_CLASS, reg);
	class_high = FIELD_GET(DMA_CCTRL_CLASSH, reg);
	val &= ~DMA_CCTRL_CLASS;
	val |= FIELD_PREP(DMA_CCTRL_CLASS, class_low);
	val &= ~DMA_CCTRL_CLASSH;
	val |= FIELD_PREP(DMA_CCTRL_CLASSH, class_high);
	writel(val, d->base + DMA_CCTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	return 0;
}

static void ldma_chan_irq_init(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;
	u32 enofs, crofs;
	u32 cn_bit;

	if (c->nr < MAX_LOWER_CHANS) {
		enofs = DMA_IRNEN;
		crofs = DMA_IRNCR;
	} else {
		enofs = DMA_IRNEN1;
		crofs = DMA_IRNCR1;
	}

	cn_bit = BIT(c->nr & MASK_LOWER_CHANS);
	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);

	/* Clear all interrupts and disabled it */
	writel(0, d->base + DMA_CIE);
	writel(DMA_CI_ALL, d->base + DMA_CIS);

	ldma_update_bits(d, cn_bit, 0, enofs);
	writel(cn_bit, d->base + crofs);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_chan_set_class(struct ldma_chan *c, u32 val)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 class_val;

	if (d->inst->type == DMA_TYPE_MCPY || val > DMA_MAX_CLASS)
		return;

	/* 3 bits low */
	class_val = FIELD_PREP(DMA_CCTRL_CLASS, val & 0x7);
	/* 2 bits high */
	class_val |= FIELD_PREP(DMA_CCTRL_CLASSH, (val >> 3) & 0x3);

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, DMA_CCTRL_CLASS | DMA_CCTRL_CLASSH, class_val,
			 DMA_CCTRL);
}

static int ldma_chan_on(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;

	/* If descriptors not configured, not allow to turn on channel */
	if (WARN_ON(!c->desc_init))
		return -EINVAL;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, DMA_CCTRL_ON, DMA_CCTRL_ON, DMA_CCTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	c->onoff = DMA_CH_ON;

	return 0;
}

static int ldma_chan_off(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;
	u32 val;
	int ret;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, DMA_CCTRL_ON, 0, DMA_CCTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	ret = readl_poll_timeout_atomic(d->base + DMA_CCTRL, val,
					!(val & DMA_CCTRL_ON), 0, 10000);
	if (ret)
		return ret;

	c->onoff = DMA_CH_OFF;

	return 0;
}

static void ldma_chan_desc_hw_cfg(struct ldma_chan *c, dma_addr_t desc_base,
				  int desc_num)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	writel(lower_32_bits(desc_base), d->base + DMA_CDBA);

	/* Higher 4 bits of 36 bit addressing */
	if (IS_ENABLED(CONFIG_64BIT)) {
		u32 hi = upper_32_bits(desc_base) & HIGH_4_BITS;

		ldma_update_bits(d, DMA_CDBA_MSB,
				 FIELD_PREP(DMA_CDBA_MSB, hi), DMA_CCTRL);
	}
	writel(desc_num, d->base + DMA_CDLEN);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	c->desc_init = true;
}

static struct dma_async_tx_descriptor *
ldma_chan_desc_cfg(struct dma_chan *chan, dma_addr_t desc_base, int desc_num)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	struct dma_async_tx_descriptor *tx;
	struct dw2_desc_sw *ds;

	if (!desc_num) {
		dev_err(d->dev, "Channel %d must allocate descriptor first\n",
			c->nr);
		return NULL;
	}

	if (desc_num > DMA_MAX_DESC_NUM) {
		dev_err(d->dev, "Channel %d descriptor number out of range %d\n",
			c->nr, desc_num);
		return NULL;
	}

	ldma_chan_desc_hw_cfg(c, desc_base, desc_num);

	c->flags |= DMA_HW_DESC;
	c->desc_cnt = desc_num;
	c->desc_phys = desc_base;

	ds = kzalloc(sizeof(*ds), GFP_NOWAIT);
	if (!ds)
		return NULL;

	tx = &ds->vdesc.tx;
	dma_async_tx_descriptor_init(tx, chan);

	return tx;
}

static int ldma_chan_reset(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;
	u32 val;
	int ret;

	ret = ldma_chan_off(c);
	if (ret)
		return ret;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, DMA_CCTRL_RST, DMA_CCTRL_RST, DMA_CCTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	ret = readl_poll_timeout_atomic(d->base + DMA_CCTRL, val,
					!(val & DMA_CCTRL_RST), 0, 10000);
	if (ret)
		return ret;

	c->rst = 1;
	c->desc_init = false;

	return 0;
}

static void ldma_chan_byte_offset_cfg(struct ldma_chan *c, u32 boff_len)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask = DMA_C_BOFF_EN | DMA_C_BOFF_BOF_LEN;
	u32 val;

	if (boff_len > 0 && boff_len <= DMA_CHAN_BOFF_MAX)
		val = FIELD_PREP(DMA_C_BOFF_BOF_LEN, boff_len) | DMA_C_BOFF_EN;
	else
		val = 0;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_C_BOFF);
}

static void ldma_chan_data_endian_cfg(struct ldma_chan *c, bool enable,
				      u32 endian_type)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask = DMA_C_END_DE_EN | DMA_C_END_DATAENDI;
	u32 val;

	if (enable)
		val = DMA_C_END_DE_EN | FIELD_PREP(DMA_C_END_DATAENDI, endian_type);
	else
		val = 0;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_C_ENDIAN);
}

static void ldma_chan_desc_endian_cfg(struct ldma_chan *c, bool enable,
				      u32 endian_type)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask = DMA_C_END_DES_EN | DMA_C_END_DESENDI;
	u32 val;

	if (enable)
		val = DMA_C_END_DES_EN | FIELD_PREP(DMA_C_END_DESENDI, endian_type);
	else
		val = 0;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_C_ENDIAN);
}

static void ldma_chan_hdr_mode_cfg(struct ldma_chan *c, u32 hdr_len, bool csum)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask, val;

	/* NB, csum disabled, hdr length must be provided */
	if (!csum && (!hdr_len || hdr_len > DMA_HDR_LEN_MAX))
		return;

	mask = DMA_C_HDRM_HDR_SUM;
	val = DMA_C_HDRM_HDR_SUM;

	if (!csum && hdr_len)
		val = hdr_len;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_C_HDRM);
}

static void ldma_chan_rxwr_np_cfg(struct ldma_chan *c, bool enable)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask, val;

	/* Only valid for RX channel */
	if (ldma_chan_tx(c))
		return;

	mask = DMA_CCTRL_WR_NP_EN;
	val = enable ? DMA_CCTRL_WR_NP_EN : 0;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_CCTRL);
}

static void ldma_chan_abc_cfg(struct ldma_chan *c, bool enable)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 mask, val;

	if (d->ver < DMA_VER32 || ldma_chan_tx(c))
		return;

	mask = DMA_CCTRL_CH_ABC;
	val = enable ? DMA_CCTRL_CH_ABC : 0;

	ldma_update_bits(d, DMA_CS_MASK, c->nr, DMA_CS);
	ldma_update_bits(d, mask, val, DMA_CCTRL);
}

static int ldma_port_cfg(struct ldma_port *p)
{
	unsigned long flags;
	struct ldma_dev *d;
	u32 reg;

	d = p->ldev;
	reg = FIELD_PREP(DMA_PCTRL_TXENDI, p->txendi);
	reg |= FIELD_PREP(DMA_PCTRL_RXENDI, p->rxendi);

	if (d->ver == DMA_VER22) {
		reg |= FIELD_PREP(DMA_PCTRL_TXBL, p->txbl);
		reg |= FIELD_PREP(DMA_PCTRL_RXBL, p->rxbl);
	} else {
		reg |= FIELD_PREP(DMA_PCTRL_PDEN, p->pkt_drop);

		if (p->txbl == DMA_BURSTL_32DW)
			reg |= DMA_PCTRL_TXBL32;
		else if (p->txbl == DMA_BURSTL_16DW)
			reg |= DMA_PCTRL_TXBL16;
		else
			reg |= FIELD_PREP(DMA_PCTRL_TXBL, DMA_PCTRL_TXBL_8);

		if (p->rxbl == DMA_BURSTL_32DW)
			reg |= DMA_PCTRL_RXBL32;
		else if (p->rxbl == DMA_BURSTL_16DW)
			reg |= DMA_PCTRL_RXBL16;
		else
			reg |= FIELD_PREP(DMA_PCTRL_RXBL, DMA_PCTRL_RXBL_8);
	}

	spin_lock_irqsave(&d->dev_lock, flags);
	writel(p->portid, d->base + DMA_PS);
	writel(reg, d->base + DMA_PCTRL);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	reg = readl(d->base + DMA_PCTRL); /* read back */
	dev_dbg(d->dev, "Port Control 0x%08x configuration done\n", reg);

	return 0;
}

static int ldma_chan_cfg(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;
	u32 reg;

	reg = c->pden ? DMA_CCTRL_PDEN : 0;
	reg |= c->onoff ? DMA_CCTRL_ON : 0;
	reg |= c->rst ? DMA_CCTRL_RST : 0;

	ldma_chan_cctrl_cfg(c, reg);
	ldma_chan_irq_init(c);

	if (d->ver <= DMA_VER22)
		return 0;

	spin_lock_irqsave(&d->dev_lock, flags);
	ldma_chan_set_class(c, c->nr);
	ldma_chan_byte_offset_cfg(c, c->boff_len);
	ldma_chan_data_endian_cfg(c, c->data_endian_en, c->data_endian);
	ldma_chan_desc_endian_cfg(c, c->desc_endian_en, c->desc_endian);
	ldma_chan_hdr_mode_cfg(c, c->hdrm_len, c->hdrm_csum);
	ldma_chan_rxwr_np_cfg(c, c->desc_rx_np);
	ldma_chan_abc_cfg(c, c->abc_en);
	spin_unlock_irqrestore(&d->dev_lock, flags);

	if (ldma_chan_is_hw_desc(c))
		ldma_chan_desc_hw_cfg(c, c->desc_phys, c->desc_cnt);

	return 0;
}

static void ldma_dev_init(struct ldma_dev *d)
{
	unsigned long ch_mask = (unsigned long)d->channels_mask;
	struct ldma_port *p;
	struct ldma_chan *c;
	int i;
	u32 j;

	spin_lock_init(&d->dev_lock);
	ldma_dev_reset(d);
	ldma_dev_cfg(d);

	/* DMA port initialization */
	for (i = 0; i < d->port_nrs; i++) {
		p = &d->ports[i];
		ldma_port_cfg(p);
	}

	/* DMA channel initialization */
	for_each_set_bit(j, &ch_mask, d->chan_nrs) {
		c = &d->chans[j];
		ldma_chan_cfg(c);
	}
}

static int ldma_cfg_init(struct ldma_dev *d)
{
	struct fwnode_handle *fwnode = dev_fwnode(d->dev);
	struct ldma_port *p;
	int i;

	if (fwnode_property_read_bool(fwnode, "intel,dma-byte-en"))
		d->flags |= DMA_EN_BYTE_EN;

	if (fwnode_property_read_bool(fwnode, "intel,dma-dburst-wr"))
		d->flags |= DMA_DBURST_WR;

	if (fwnode_property_read_bool(fwnode, "intel,dma-drb"))
		d->flags |= DMA_DFT_DRB;

	if (fwnode_property_read_u32(fwnode, "intel,dma-poll-cnt",
				     &d->pollcnt))
		d->pollcnt = DMA_DFT_POLL_CNT;

	if (d->inst->chan_fc)
		d->flags |= DMA_CHAN_FLOW_CTL;

	if (d->inst->desc_fod)
		d->flags |= DMA_DESC_FOD;

	if (d->inst->desc_in_sram)
		d->flags |= DMA_DESC_IN_SRAM;

	if (d->inst->valid_desc_fetch_ack)
		d->flags |= DMA_VALID_DESC_FETCH_ACK;

	if (d->ver > DMA_VER22) {
		if (!d->port_nrs)
			return -EINVAL;

		for (i = 0; i < d->port_nrs; i++) {
			p = &d->ports[i];
			p->rxendi = DMA_DFT_ENDIAN;
			p->txendi = DMA_DFT_ENDIAN;
			p->rxbl = DMA_DFT_BURST;
			p->txbl = DMA_DFT_BURST;
			p->pkt_drop = DMA_PKT_DROP_DIS;
		}
	}

	return 0;
}

static void dma_free_desc_resource(struct virt_dma_desc *vdesc)
{
	struct dw2_desc_sw *ds = to_lgm_dma_desc(vdesc);
	struct ldma_chan *c = ds->chan;

	dma_pool_free(c->desc_pool, ds->desc_hw, ds->desc_phys);
	kfree(ds);
}

static struct dw2_desc_sw *
dma_alloc_desc_resource(int num, struct ldma_chan *c)
{
	struct device *dev = c->vchan.chan.device->dev;
	struct dw2_desc_sw *ds;

	if (num > c->desc_num) {
		dev_err(dev, "sg num %d exceed max %d\n", num, c->desc_num);
		return NULL;
	}

	ds = kzalloc(sizeof(*ds), GFP_NOWAIT);
	if (!ds)
		return NULL;

	ds->chan = c;
	ds->desc_hw = dma_pool_zalloc(c->desc_pool, GFP_ATOMIC,
				      &ds->desc_phys);
	if (!ds->desc_hw) {
		dev_dbg(dev, "out of memory for link descriptor\n");
		kfree(ds);
		return NULL;
	}
	ds->desc_cnt = num;

	return ds;
}

static void ldma_chan_irq_en(struct ldma_chan *c)
{
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;

	spin_lock_irqsave(&d->dev_lock, flags);
	writel(c->nr, d->base + DMA_CS);
	writel(DMA_CI_EOP, d->base + DMA_CIE);
	writel(BIT(c->nr), d->base + DMA_IRNEN);
	spin_unlock_irqrestore(&d->dev_lock, flags);
}

static void ldma_issue_pending(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	unsigned long flags;

	if (d->ver == DMA_VER22) {
		spin_lock_irqsave(&c->vchan.lock, flags);
		if (vchan_issue_pending(&c->vchan)) {
			struct virt_dma_desc *vdesc;

			/* Get the next descriptor */
			vdesc = vchan_next_desc(&c->vchan);
			if (!vdesc) {
				c->ds = NULL;
				spin_unlock_irqrestore(&c->vchan.lock, flags);
				return;
			}
			list_del(&vdesc->node);
			c->ds = to_lgm_dma_desc(vdesc);
			ldma_chan_desc_hw_cfg(c, c->ds->desc_phys, c->ds->desc_cnt);
			ldma_chan_irq_en(c);
		}
		spin_unlock_irqrestore(&c->vchan.lock, flags);
	}
	ldma_chan_on(c);
}

static void ldma_synchronize(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);

	/*
	 * clear any pending work if any. In that
	 * case the resource needs to be free here.
	 */
	cancel_work_sync(&c->work);
	vchan_synchronize(&c->vchan);
	if (c->ds)
		dma_free_desc_resource(&c->ds->vdesc);
}

static int ldma_terminate_all(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&c->vchan.lock, flags);
	vchan_get_all_descriptors(&c->vchan, &head);
	spin_unlock_irqrestore(&c->vchan.lock, flags);
	vchan_dma_desc_free_list(&c->vchan, &head);

	return ldma_chan_reset(c);
}

static int ldma_resume_chan(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);

	ldma_chan_on(c);

	return 0;
}

static int ldma_pause_chan(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);

	return ldma_chan_off(c);
}

static enum dma_status
ldma_tx_status(struct dma_chan *chan, dma_cookie_t cookie,
	       struct dma_tx_state *txstate)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	enum dma_status status = DMA_COMPLETE;

	if (d->ver == DMA_VER22)
		status = dma_cookie_status(chan, cookie, txstate);

	return status;
}

static void dma_chan_irq(int irq, void *data)
{
	struct ldma_chan *c = data;
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	u32 stat;

	/* Disable channel interrupts  */
	writel(c->nr, d->base + DMA_CS);
	stat = readl(d->base + DMA_CIS);
	if (!stat)
		return;

	writel(readl(d->base + DMA_CIE) & ~DMA_CI_ALL, d->base + DMA_CIE);
	writel(stat, d->base + DMA_CIS);
	queue_work(d->wq, &c->work);
}

static irqreturn_t dma_interrupt(int irq, void *dev_id)
{
	struct ldma_dev *d = dev_id;
	struct ldma_chan *c;
	unsigned long irncr;
	u32 cid;

	irncr = readl(d->base + DMA_IRNCR);
	if (!irncr) {
		dev_err(d->dev, "dummy interrupt\n");
		return IRQ_NONE;
	}

	for_each_set_bit(cid, &irncr, d->chan_nrs) {
		/* Mask */
		writel(readl(d->base + DMA_IRNEN) & ~BIT(cid), d->base + DMA_IRNEN);
		/* Ack */
		writel(readl(d->base + DMA_IRNCR) | BIT(cid), d->base + DMA_IRNCR);

		c = &d->chans[cid];
		dma_chan_irq(irq, c);
	}

	return IRQ_HANDLED;
}

static void prep_slave_burst_len(struct ldma_chan *c)
{
	struct ldma_port *p = c->port;
	struct dma_slave_config *cfg = &c->config;

	if (cfg->dst_maxburst)
		cfg->src_maxburst = cfg->dst_maxburst;

	/* TX and RX has the same burst length */
	p->txbl = ilog2(cfg->src_maxburst);
	p->rxbl = p->txbl;
}

static struct dma_async_tx_descriptor *
ldma_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		   unsigned int sglen, enum dma_transfer_direction dir,
		   unsigned long flags, void *context)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	size_t len, avail, total = 0;
	struct dw2_desc *hw_ds;
	struct dw2_desc_sw *ds;
	struct scatterlist *sg;
	int num = sglen, i;
	dma_addr_t addr;

	if (!sgl)
		return NULL;

	if (d->ver > DMA_VER22)
		return ldma_chan_desc_cfg(chan, sgl->dma_address, sglen);

	for_each_sg(sgl, sg, sglen, i) {
		avail = sg_dma_len(sg);
		if (avail > DMA_MAX_SIZE)
			num += DIV_ROUND_UP(avail, DMA_MAX_SIZE) - 1;
	}

	ds = dma_alloc_desc_resource(num, c);
	if (!ds)
		return NULL;

	c->ds = ds;

	num = 0;
	/* sop and eop has to be handled nicely */
	for_each_sg(sgl, sg, sglen, i) {
		addr = sg_dma_address(sg);
		avail = sg_dma_len(sg);
		total += avail;

		do {
			len = min_t(size_t, avail, DMA_MAX_SIZE);

			hw_ds = &ds->desc_hw[num];
			switch (sglen) {
			case 1:
				hw_ds->field &= ~DESC_SOP;
				hw_ds->field |= FIELD_PREP(DESC_SOP, 1);

				hw_ds->field &= ~DESC_EOP;
				hw_ds->field |= FIELD_PREP(DESC_EOP, 1);
				break;
			default:
				if (num == 0) {
					hw_ds->field &= ~DESC_SOP;
					hw_ds->field |= FIELD_PREP(DESC_SOP, 1);

					hw_ds->field &= ~DESC_EOP;
					hw_ds->field |= FIELD_PREP(DESC_EOP, 0);
				} else if (num == (sglen - 1)) {
					hw_ds->field &= ~DESC_SOP;
					hw_ds->field |= FIELD_PREP(DESC_SOP, 0);
					hw_ds->field &= ~DESC_EOP;
					hw_ds->field |= FIELD_PREP(DESC_EOP, 1);
				} else {
					hw_ds->field &= ~DESC_SOP;
					hw_ds->field |= FIELD_PREP(DESC_SOP, 0);

					hw_ds->field &= ~DESC_EOP;
					hw_ds->field |= FIELD_PREP(DESC_EOP, 0);
				}
				break;
			}
			/* Only 32 bit address supported */
			hw_ds->addr = (u32)addr;

			hw_ds->field &= ~DESC_DATA_LEN;
			hw_ds->field |= FIELD_PREP(DESC_DATA_LEN, len);

			hw_ds->field &= ~DESC_C;
			hw_ds->field |= FIELD_PREP(DESC_C, 0);

			hw_ds->field &= ~DESC_BYTE_OFF;
			hw_ds->field |= FIELD_PREP(DESC_BYTE_OFF, addr & 0x3);

			/* Ensure data ready before ownership change */
			wmb();
			hw_ds->field &= ~DESC_OWN;
			hw_ds->field |= FIELD_PREP(DESC_OWN, DMA_OWN);

			/* Ensure ownership changed before moving forward */
			wmb();
			num++;
			addr += len;
			avail -= len;
		} while (avail);
	}

	ds->size = total;
	prep_slave_burst_len(c);

	return vchan_tx_prep(&c->vchan, &ds->vdesc, DMA_CTRL_ACK);
}

static int
ldma_slave_config(struct dma_chan *chan, struct dma_slave_config *cfg)
{
	struct ldma_chan *c = to_ldma_chan(chan);

	memcpy(&c->config, cfg, sizeof(c->config));

	return 0;
}

static int ldma_alloc_chan_resources(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);
	struct device *dev = c->vchan.chan.device->dev;
	size_t	desc_sz;

	if (d->ver > DMA_VER22) {
		c->flags |= CHAN_IN_USE;
		return 0;
	}

	if (c->desc_pool)
		return c->desc_num;

	desc_sz = c->desc_num * sizeof(struct dw2_desc);
	c->desc_pool = dma_pool_create(c->name, dev, desc_sz,
				       __alignof__(struct dw2_desc), 0);

	if (!c->desc_pool) {
		dev_err(dev, "unable to allocate descriptor pool\n");
		return -ENOMEM;
	}

	return c->desc_num;
}

static void ldma_free_chan_resources(struct dma_chan *chan)
{
	struct ldma_chan *c = to_ldma_chan(chan);
	struct ldma_dev *d = to_ldma_dev(c->vchan.chan.device);

	if (d->ver == DMA_VER22) {
		dma_pool_destroy(c->desc_pool);
		c->desc_pool = NULL;
		vchan_free_chan_resources(to_virt_chan(chan));
		ldma_chan_reset(c);
	} else {
		c->flags &= ~CHAN_IN_USE;
	}
}

static void dma_work(struct work_struct *work)
{
	struct ldma_chan *c = container_of(work, struct ldma_chan, work);
	struct dma_async_tx_descriptor *tx = &c->ds->vdesc.tx;
	struct virt_dma_chan *vc = &c->vchan;
	struct dmaengine_desc_callback cb;
	struct virt_dma_desc *vd, *_vd;
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&c->vchan.lock, flags);
	list_splice_tail_init(&vc->desc_completed, &head);
	spin_unlock_irqrestore(&c->vchan.lock, flags);
	dmaengine_desc_get_callback(tx, &cb);
	dma_cookie_complete(tx);
	dmaengine_desc_callback_invoke(&cb, NULL);

	list_for_each_entry_safe(vd, _vd, &head, node) {
		dmaengine_desc_get_callback(tx, &cb);
		dma_cookie_complete(tx);
		list_del(&vd->node);
		dmaengine_desc_callback_invoke(&cb, NULL);

		vchan_vdesc_fini(vd);
	}
	c->ds = NULL;
}

static void
update_burst_len_v22(struct ldma_chan *c, struct ldma_port *p, u32 burst)
{
	if (ldma_chan_tx(c))
		p->txbl = ilog2(burst);
	else
		p->rxbl = ilog2(burst);
}

static void
update_burst_len_v3X(struct ldma_chan *c, struct ldma_port *p, u32 burst)
{
	if (ldma_chan_tx(c))
		p->txbl = burst;
	else
		p->rxbl = burst;
}

static int
update_client_configs(struct of_dma *ofdma, struct of_phandle_args *spec)
{
	struct ldma_dev *d = ofdma->of_dma_data;
	u32 chan_id =  spec->args[0];
	u32 port_id =  spec->args[1];
	u32 burst = spec->args[2];
	struct ldma_port *p;
	struct ldma_chan *c;

	if (chan_id >= d->chan_nrs || port_id >= d->port_nrs)
		return 0;

	p = &d->ports[port_id];
	c = &d->chans[chan_id];
	c->port = p;

	if (d->ver == DMA_VER22)
		update_burst_len_v22(c, p, burst);
	else
		update_burst_len_v3X(c, p, burst);

	ldma_port_cfg(p);

	return 1;
}

static struct dma_chan *ldma_xlate(struct of_phandle_args *spec,
				   struct of_dma *ofdma)
{
	struct ldma_dev *d = ofdma->of_dma_data;
	u32 chan_id =  spec->args[0];
	int ret;

	if (!spec->args_count)
		return NULL;

	/* if args_count is 1 driver use default settings */
	if (spec->args_count > 1) {
		ret = update_client_configs(ofdma, spec);
		if (!ret)
			return NULL;
	}

	return dma_get_slave_channel(&d->chans[chan_id].vchan.chan);
}

static void ldma_dma_init_v22(int i, struct ldma_dev *d)
{
	struct ldma_chan *c;

	c = &d->chans[i];
	c->nr = i; /* Real channel number */
	c->rst = DMA_CHAN_RST;
	c->desc_num = DMA_DFT_DESC_NUM;
	snprintf(c->name, sizeof(c->name), "chan%d", c->nr);
	INIT_WORK(&c->work, dma_work);
	c->vchan.desc_free = dma_free_desc_resource;
	vchan_init(&c->vchan, &d->dma_dev);
}

static void ldma_dma_init_v3X(int i, struct ldma_dev *d)
{
	struct ldma_chan *c;

	c = &d->chans[i];
	c->data_endian = DMA_DFT_ENDIAN;
	c->desc_endian = DMA_DFT_ENDIAN;
	c->data_endian_en = false;
	c->desc_endian_en = false;
	c->desc_rx_np = false;
	c->flags |= DEVICE_ALLOC_DESC;
	c->onoff = DMA_CH_OFF;
	c->rst = DMA_CHAN_RST;
	c->abc_en = true;
	c->hdrm_csum = false;
	c->boff_len = 0;
	c->nr = i;
	c->vchan.desc_free = dma_free_desc_resource;
	vchan_init(&c->vchan, &d->dma_dev);
}

static int ldma_init_v22(struct ldma_dev *d, struct platform_device *pdev)
{
	int ret;

	ret = device_property_read_u32(d->dev, "dma-channels", &d->chan_nrs);
	if (ret < 0) {
		dev_err(d->dev, "unable to read dma-channels property\n");
		return ret;
	}

	d->irq = platform_get_irq(pdev, 0);
	if (d->irq < 0)
		return d->irq;

	ret = devm_request_irq(&pdev->dev, d->irq, dma_interrupt, 0,
			       DRIVER_NAME, d);
	if (ret)
		return ret;

	d->wq = alloc_ordered_workqueue("dma_wq", WQ_MEM_RECLAIM |
			WQ_HIGHPRI);
	if (!d->wq)
		return -ENOMEM;

	return 0;
}

static void ldma_clk_disable(void *data)
{
	struct ldma_dev *d = data;

	clk_disable_unprepare(d->core_clk);
	reset_control_assert(d->rst);
}

static const struct ldma_inst_data dma0 = {
	.name = "dma0",
	.chan_fc = false,
	.desc_fod = false,
	.desc_in_sram = false,
	.valid_desc_fetch_ack = false,
};

static const struct ldma_inst_data dma2tx = {
	.name = "dma2tx",
	.type = DMA_TYPE_TX,
	.orrc = 16,
	.chan_fc = true,
	.desc_fod = true,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = true,
};

static const struct ldma_inst_data dma1rx = {
	.name = "dma1rx",
	.type = DMA_TYPE_RX,
	.orrc = 16,
	.chan_fc = false,
	.desc_fod = true,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = false,
};

static const struct ldma_inst_data dma1tx = {
	.name = "dma1tx",
	.type = DMA_TYPE_TX,
	.orrc = 16,
	.chan_fc = true,
	.desc_fod = true,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = true,
};

static const struct ldma_inst_data dma0tx = {
	.name = "dma0tx",
	.type = DMA_TYPE_TX,
	.orrc = 16,
	.chan_fc = true,
	.desc_fod = true,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = true,
};

static const struct ldma_inst_data dma3 = {
	.name = "dma3",
	.type = DMA_TYPE_MCPY,
	.orrc = 16,
	.chan_fc = false,
	.desc_fod = false,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = false,
};

static const struct ldma_inst_data toe_dma30 = {
	.name = "toe_dma30",
	.type = DMA_TYPE_MCPY,
	.orrc = 16,
	.chan_fc = false,
	.desc_fod = false,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = true,
};

static const struct ldma_inst_data toe_dma31 = {
	.name = "toe_dma31",
	.type = DMA_TYPE_MCPY,
	.orrc = 16,
	.chan_fc = false,
	.desc_fod = false,
	.desc_in_sram = true,
	.valid_desc_fetch_ack = true,
};

static const struct of_device_id intel_ldma_match[] = {
	{ .compatible = "intel,lgm-cdma", .data = &dma0},
	{ .compatible = "intel,lgm-dma2tx", .data = &dma2tx},
	{ .compatible = "intel,lgm-dma1rx", .data = &dma1rx},
	{ .compatible = "intel,lgm-dma1tx", .data = &dma1tx},
	{ .compatible = "intel,lgm-dma0tx", .data = &dma0tx},
	{ .compatible = "intel,lgm-dma3", .data = &dma3},
	{ .compatible = "intel,lgm-toe-dma30", .data = &toe_dma30},
	{ .compatible = "intel,lgm-toe-dma31", .data = &toe_dma31},
	{}
};

static int intel_ldma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dma_device *dma_dev;
	unsigned long ch_mask;
	struct ldma_chan *c;
	struct ldma_port *p;
	struct ldma_dev *d;
	u32 id, bitn = 32, j;
	int i, ret;

	d = devm_kzalloc(dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	/* Link controller to platform device */
	d->dev = &pdev->dev;

	d->inst = device_get_match_data(dev);
	if (!d->inst) {
		dev_err(dev, "No device match found\n");
		return -ENODEV;
	}

	d->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(d->base))
		return PTR_ERR(d->base);

	/* Power up and reset the dma engine, some DMAs always on?? */
	d->core_clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(d->core_clk))
		return PTR_ERR(d->core_clk);

	d->rst = devm_reset_control_get_optional(dev, NULL);
	if (IS_ERR(d->rst))
		return PTR_ERR(d->rst);

	clk_prepare_enable(d->core_clk);
	reset_control_deassert(d->rst);

	ret = devm_add_action_or_reset(dev, ldma_clk_disable, d);
	if (ret) {
		dev_err(dev, "Failed to devm_add_action_or_reset, %d\n", ret);
		return ret;
	}

	id = readl(d->base + DMA_ID);
	d->chan_nrs = FIELD_GET(DMA_ID_CHNR, id);
	d->port_nrs = FIELD_GET(DMA_ID_PNR, id);
	d->ver = FIELD_GET(DMA_ID_REV, id);

	if (id & DMA_ID_AW_36B)
		d->flags |= DMA_ADDR_36BIT;

	if (IS_ENABLED(CONFIG_64BIT) && (id & DMA_ID_AW_36B))
		bitn = 36;

	if (id & DMA_ID_DW_128B)
		d->flags |= DMA_DATA_128BIT;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(bitn));
	if (ret) {
		dev_err(dev, "No usable DMA configuration\n");
		return ret;
	}

	if (d->ver == DMA_VER22) {
		ret = ldma_init_v22(d, pdev);
		if (ret)
			return ret;
	}

	ret = device_property_read_u32(dev, "dma-channel-mask", &d->channels_mask);
	if (ret < 0)
		d->channels_mask = GENMASK(d->chan_nrs - 1, 0);

	dma_dev = &d->dma_dev;

	dma_cap_zero(dma_dev->cap_mask);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);

	/* Channel initializations */
	INIT_LIST_HEAD(&dma_dev->channels);

	/* Port Initializations */
	d->ports = devm_kcalloc(dev, d->port_nrs, sizeof(*p), GFP_KERNEL);
	if (!d->ports)
		return -ENOMEM;

	/* Channels Initializations */
	d->chans = devm_kcalloc(d->dev, d->chan_nrs, sizeof(*c), GFP_KERNEL);
	if (!d->chans)
		return -ENOMEM;

	for (i = 0; i < d->port_nrs; i++) {
		p = &d->ports[i];
		p->portid = i;
		p->ldev = d;
	}

	ret = ldma_cfg_init(d);
	if (ret)
		return ret;

	dma_dev->dev = &pdev->dev;

	ch_mask = (unsigned long)d->channels_mask;
	for_each_set_bit(j, &ch_mask, d->chan_nrs) {
		if (d->ver == DMA_VER22)
			ldma_dma_init_v22(j, d);
		else
			ldma_dma_init_v3X(j, d);
	}

	dma_dev->device_alloc_chan_resources = ldma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = ldma_free_chan_resources;
	dma_dev->device_terminate_all = ldma_terminate_all;
	dma_dev->device_issue_pending = ldma_issue_pending;
	dma_dev->device_tx_status = ldma_tx_status;
	dma_dev->device_resume = ldma_resume_chan;
	dma_dev->device_pause = ldma_pause_chan;
	dma_dev->device_prep_slave_sg = ldma_prep_slave_sg;

	if (d->ver == DMA_VER22) {
		dma_dev->device_config = ldma_slave_config;
		dma_dev->device_synchronize = ldma_synchronize;
		dma_dev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
		dma_dev->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
		dma_dev->directions = BIT(DMA_MEM_TO_DEV) |
				      BIT(DMA_DEV_TO_MEM);
		dma_dev->residue_granularity =
					DMA_RESIDUE_GRANULARITY_DESCRIPTOR;
	}

	platform_set_drvdata(pdev, d);

	ldma_dev_init(d);

	ret = dma_async_device_register(dma_dev);
	if (ret) {
		dev_err(dev, "Failed to register slave DMA engine device\n");
		return ret;
	}

	ret = of_dma_controller_register(pdev->dev.of_node, ldma_xlate, d);
	if (ret) {
		dev_err(dev, "Failed to register of DMA controller\n");
		dma_async_device_unregister(dma_dev);
		return ret;
	}

	dev_info(dev, "Init done - rev: %x, ports: %d channels: %d\n", d->ver,
		 d->port_nrs, d->chan_nrs);

	return 0;
}

static struct platform_driver intel_ldma_driver = {
	.probe = intel_ldma_probe,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = intel_ldma_match,
	},
};

/*
 * Perform this driver as device_initcall to make sure initialization happens
 * before its DMA clients of some are platform specific and also to provide
 * registered DMA channels and DMA capabilities to clients before their
 * initialization.
 */
static int __init intel_ldma_init(void)
{
	return platform_driver_register(&intel_ldma_driver);
}

device_initcall(intel_ldma_init);
