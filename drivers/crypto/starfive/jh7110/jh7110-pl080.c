// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 StarFive, Inc <huan.feng@starfivetech.com>
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING
 * CUSTOMERS WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER
 * FOR THEM TO SAVE TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE
 * FOR ANY DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY
 * CLAIMS ARISING FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE
 * BY CUSTOMERS OF THE CODING INFORMATION CONTAINED HEREIN IN CONNECTION
 * WITH THEIR PRODUCTS.
 */
#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/dma-direct.h>
#include <crypto/scatterwalk.h>

#include "jh7110-pl080.h"
#include "jh7110-str.h"

static volatile u32 g_dmac_done;
static volatile u32 g_dmac_err;
struct jh7110_pl080_lli_build_data *bd;

/* Maximum times we call dma_pool_alloc on this pool without freeing */
#define MAX_NUM_TSFR_LLIS	512

static inline int jh7110_dma_wait_done(struct jh7110_sec_dev *sdev, int chan)
{
	int ret = -1;

	mutex_lock(&sdev->pl080_doing);
	if (g_dmac_done & BIT(chan))
		ret = 0;
	mutex_unlock(&sdev->pl080_doing);

	return ret;
}

static inline int jh7110_dmac_channel_wait_busy(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 status;

	return readl_relaxed_poll_timeout(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG, status,
					!(status & PL080_CONFIG_ACTIVE), 10, 100000);
}

static irqreturn_t jh7110_pl080_irq_thread(int irq, void *arg)
{
	struct jh7110_sec_dev *sdev = (struct jh7110_sec_dev *) arg;

	pr_debug("this is debug  mutex_unlock doing  ---------------- %s %s %d\n", __FILE__, __func__, __LINE__);
	mutex_unlock(&sdev->pl080_doing);

	return IRQ_HANDLED;
}

static irqreturn_t jh7110_pl080_irq(int irq, void *dev)
{
	struct jh7110_sec_dev *sdev = (struct jh7110_sec_dev *) dev;
	u32 err, tc, i;

	/* check & clear - ERR & TC interrupts */
	err = readl_relaxed(sdev->dma_base + PL080_ERR_STATUS);
	if (err) {
		pr_err("%s error interrupt, register value 0x%08x\n",
		       __func__, err);
		writel_relaxed(err, sdev->dma_base + PL080_ERR_CLEAR);
	}
	tc = readl_relaxed(sdev->dma_base + PL080_TC_STATUS);
	if (tc)
		writel_relaxed(tc, sdev->dma_base + PL080_TC_CLEAR);

	if (!err && !tc)
		return IRQ_NONE;

	for (i = 0; i < PL080_CHANNELS_NUM; i++) {
		if (BIT(i) & err)
			g_dmac_err |= BIT(i);
		if (BIT(i) & tc)
			g_dmac_done |= BIT(i);
	}

	return IRQ_WAKE_THREAD;
}

static int jh7110_dmac_enable(struct jh7110_sec_dev *sdev)
{
	writel_relaxed(PL080_CONFIG_ENABLE, sdev->dma_base + PL080_CONFIG);

	return 0;
}

static int jh7110_dmac_disable(struct jh7110_sec_dev *sdev)
{
	writel_relaxed(0, sdev->dma_base + PL080_CONFIG);
	return 0;
}

static int jh7110_dmac_channel_enable(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 control;

	control = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	control |= PL080_CONFIG_ENABLE;
	writel_relaxed(control, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);

	return 0;
}

static int jh7110_dmac_channel_disable(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 control;

	control = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	control &= (~PL080_CONFIG_ENABLE);
	writel_relaxed(control, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);

	return 0;
}

static int jh7110_dmac_channel_halt(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 val;

	val = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	val |=  PL080_CONFIG_HALT;
	writel_relaxed(val, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);

	return 0;
}

static int jh7110_dmac_channel_resume(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 val;

	val = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	val &= (~PL080_CONFIG_HALT);
	writel_relaxed(val, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);

	return 0;
}

static void jh7110_dmac_interrupt_enable(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 val;

	val = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	val |= PL080_CONFIG_TC_IRQ_MASK | PL080_CONFIG_ERR_IRQ_MASK;
	writel_relaxed(val, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
}

static void jh7110_dmac_interrupt_disable(struct jh7110_sec_dev *sdev, u8 chan)
{
	u32 val;

	val = readl_relaxed(sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
	val &= ~(PL080_CONFIG_TC_IRQ_MASK | PL080_CONFIG_ERR_IRQ_MASK);
	writel_relaxed(val, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);
}

static u32 jh7110_dmac_cctl_bits(struct jh7110_pl080_chan_config *config, u32 tsize)
{
	u32 cctl = 0;

	if (config->src_width == PL08X_BUS_WIDTH_16_BITS)
		tsize >>= 1;
	else if (config->src_width == PL08X_BUS_WIDTH_32_BITS)
		tsize >>= 2;

	cctl = (1 << PL080_CONTROL_PROT_SYS) | (config->di<<27) | (config->si<<26) | (config->dst_ahb << 25)
		| (config->src_ahb << 24) | (config->dst_width << 21) | (config->src_width << 18) | (config->dst_bsize<<15)
		| (config->src_bsize<<12) | tsize;
	return cctl;
}

static int jh7110_dmac_fill_llis(struct jh7110_pl080_lli_build_data *bd, int num_llis,
									struct jh7110_pl080_chan_config *config)
{
	u32 cctl;
	u32 llis_addr = bd->llis_phy_addr;
	struct jh7110_pl080_lli *llis = bd->llis;

	cctl = jh7110_dmac_cctl_bits(config, bd->tsize);

	llis[num_llis].control0 = cctl;
	llis[num_llis].src_addr = bd->src_addr;
	llis[num_llis].dst_addr = bd->dst_addr;
	llis[num_llis].next_lli = llis_addr + (num_llis + 1) * sizeof(struct jh7110_pl080_lli);

	bd->remainder -= bd->tsize;
	if (bd->remainder == 0) {
		llis[num_llis].next_lli = 0;
		llis[num_llis].control0 |= PL080_CONTROL_TC_IRQ_EN;
	}

	return 0;
}

static int jh7110_dmac_llis_prep(struct jh7110_pl080_lli_build_data *bd, struct jh7110_pl080_chan_config *config)
{
	u32 llisize;
	u32 buswidth, width;
	u32 max_bytes_per_lli, tsize;
	u32 src_tsize, dst_tsize;
	u32 num_llis = 0;

	bd->src_addr = config->src_addr;
	bd->dst_addr = config->dst_addr;
	bd->remainder = config->xfer_size;

	buswidth = min(config->src_width, config->dst_width);
	switch (buswidth) {
	case PL08X_BUS_WIDTH_8_BITS:
		width = 1;
		break;
	case PL08X_BUS_WIDTH_16_BITS:
		width = 2;
		break;
	case PL08X_BUS_WIDTH_32_BITS:
		width = 4;
		break;
	default:
		break;
	}
	max_bytes_per_lli = width * PL080_CONTROL_TRANSFER_SIZE_MASK;
	tsize = max_bytes_per_lli;
	llisize = bd->remainder % tsize;
	if (llisize == 0)
		llisize = bd->remainder / tsize;
	else
		llisize = (bd->remainder / tsize) + 1;

	if (llisize > MAX_NUM_TSFR_LLIS)
		return -1;

	llisize += 1;

	bd->llis = kmalloc(llisize * sizeof(struct jh7110_pl080_lli), GFP_KERNEL);
	if (bd->llis == NULL)
		return -1;
	memset(bd->llis, 0, llisize * sizeof(struct jh7110_pl080_lli));
	bd->llis_phy_addr = (u32)bd->llis;

	while (1) {
		if (!bd->remainder)
			break;

		if (bd->remainder > tsize)
			bd->tsize = tsize;
		else
			bd->tsize = bd->remainder;

		jh7110_dmac_fill_llis(bd, num_llis++, config);

		if (config->si)
			bd->src_addr += bd->tsize;
		if (config->di)
			bd->dst_addr += bd->tsize;
	}
	config->src_addr = bd->src_addr;
	config->dst_addr = bd->dst_addr;

	return 0;
}

/*dmac_setup_xfer support dma link item*/
int jh7110_dmac_setup_xfer(struct jh7110_sec_dev *sdev, u8 chan, struct jh7110_pl080_chan_config *config)
{
	u32 val;
	struct jh7110_pl080_lli *lli = NULL;

	bd = kmalloc(sizeof(struct jh7110_pl080_lli_build_data), GFP_KERNEL);
	if (bd == NULL)
		return -1;

	memset(bd, 0, sizeof(struct jh7110_pl080_lli_build_data));
	jh7110_dmac_llis_prep(bd, config);
	lli = &bd->llis[0];

	writel_relaxed(lli->src_addr, sdev->pl080->phy_chans[chan].base + PL080_CH_SRC_ADDR);
	writel_relaxed(lli->dst_addr, sdev->pl080->phy_chans[chan].base + PL080_CH_DST_ADDR);
	writel_relaxed(lli->next_lli, sdev->pl080->phy_chans[chan].base + PL080_CH_LLI);
	writel_relaxed(lli->control0, sdev->pl080->phy_chans[chan].base + PL080_CH_CONTROL);

	val = (config->flow << 11)|(config->src_peri<<1)|(config->dst_peri<<6);

	writel_relaxed(val, sdev->pl080->phy_chans[chan].base + PL080_CH_CONFIG);

	jh7110_dmac_interrupt_enable(sdev, chan);

	g_dmac_done &= ~BIT(chan);
	g_dmac_err &= ~BIT(chan);


	if (chan)
		mutex_lock(&sdev->pl080_doing);

	return 0;
}

void jh7110_dmac_free_llis(void)
{
	if (bd) {
		kfree(bd->llis);
		kfree(bd);
		bd = NULL;
	}
}

int jh7110_dmac_wait_done(struct jh7110_sec_dev *sdev, u8 chan)
{
	if (jh7110_dma_wait_done(sdev, chan)) {
		pr_debug("this is debug for lophyel status = %x err = %x control0 = %x control1 = %x  %s %s %d\n",
				readl_relaxed(sdev->dma_base + PL080_TC_STATUS), readl_relaxed(sdev->dma_base + PL080_ERR_STATUS),
				readl_relaxed(sdev->dma_base + 0x10c), readl_relaxed(sdev->dma_base + 0x12c),
				__FILE__, __func__, __LINE__);

		return -1;
	}

	g_dmac_done &= ~BIT(chan);
	jh7110_dmac_free_llis();

	return 0;
}

int jh7110_dmac_secdata_in(struct jh7110_sec_dev *sdev, u8 chan, u32 src, u32 dst, u32 size)
{
	struct jh7110_pl080_chan_config config;
	int ret;

	config.si = PL08X_INCREMENT;
	config.di = PL08X_INCREMENT_FIX;
	config.src_ahb = PL08X_AHB1;
	config.dst_ahb = PL08X_AHB2;
	config.src_width = PL08X_BUS_WIDTH_32_BITS;
	config.dst_width = PL08X_BUS_WIDTH_32_BITS;
	config.src_bsize = PL08X_BURST_SZ_32;
	config.dst_bsize = PL08X_BURST_SZ_32;
	config.src_peri = 1;
	config.dst_peri = 1;
	config.src_addr = src;
	config.dst_addr = dst;
	config.xfer_size = size;
	config.flow = PL080_FLOW_MEM2PER;

	if (jh7110_dmac_channel_wait_busy(sdev, chan))
		return -ETIMEDOUT;

	ret = jh7110_dmac_setup_xfer(sdev, chan, &config);
	if (ret != 0)
		return ret;

	jh7110_dmac_channel_enable(sdev, chan);

	return 0;
}

int jh7110_dmac_secdata_out(struct jh7110_sec_dev *sdev, u8 chan, u32 src, u32 dst, u32 size)
{
	struct jh7110_pl080_chan_config config;
	int ret;

	config.si = PL08X_INCREMENT_FIX;
	config.di = PL08X_INCREMENT;
	config.src_ahb = PL08X_AHB2;
	config.dst_ahb = PL08X_AHB1;
	config.src_width = PL08X_BUS_WIDTH_32_BITS;
	config.dst_width = PL08X_BUS_WIDTH_32_BITS;
	config.src_bsize = PL08X_BURST_SZ_4;//follow hardware limit
	config.dst_bsize = PL08X_BURST_SZ_4;
	config.src_peri = 0;
	config.dst_peri = 0;
	config.src_addr = src;
	config.dst_addr = dst;
	config.xfer_size = size;
	config.flow = PL080_FLOW_PER2MEM;

	if (jh7110_dmac_channel_wait_busy(sdev, chan))
		return -ETIMEDOUT;

	ret = jh7110_dmac_setup_xfer(sdev, chan, &config);
	if (ret != 0)
		return ret;

	jh7110_dmac_channel_enable(sdev, chan);

	return 0;
}

int jh7110_dmac_init(struct jh7110_sec_dev *sdev, int irq)
{
	int chan;
	int ret;

	ret = devm_request_threaded_irq(sdev->dev, irq, jh7110_pl080_irq,
					jh7110_pl080_irq_thread, 0,
					dev_name(sdev->dev), sdev);
	if (ret) {
		dev_err(sdev->dev, "Can't get interrupt working.\n");
		return ret;
	}

	sdev->pl080 = kmalloc(sizeof(struct jh7110_pl08x_device), GFP_KERNEL);
	if (!sdev->pl080)
		return -ENOMEM;

	memset(sdev->pl080, 0, sizeof(struct jh7110_pl08x_device));

	for (chan = 0; chan < PL080_CHANNELS_NUM; chan++) {
		struct jh7110_pl08x_phy_chan *ch = &sdev->pl080->phy_chans[chan];

		ch->id = chan;
		ch->base = sdev->dma_base + PL080_Cx_BASE(chan);
		ch->reg_config = ch->base + PL080_CH_CONFIG;
		ch->reg_control = ch->base + PL080_CH_CONTROL;
		ch->reg_src = ch->base + PL080_CH_SRC_ADDR;
		ch->reg_dst = ch->base + PL080_CH_DST_ADDR;
		ch->reg_lli = ch->base + PL080_CH_LLI;
		}

	jh7110_dmac_enable(sdev);
	g_dmac_done = 0;
	g_dmac_err = 0;

	return 0;
}
