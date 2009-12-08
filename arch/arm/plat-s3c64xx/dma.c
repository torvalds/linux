/* linux/arch/arm/plat-s3c64xx/dma.c
 *
 * Copyright 2009 Openmoko, Inc.
 * Copyright 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * S3C64XX DMA core
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/dmapool.h>
#include <linux/sysdev.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#include <mach/dma.h>
#include <mach/map.h>
#include <mach/irqs.h>

#include <plat/dma-plat.h>
#include <plat/regs-sys.h>

#include <asm/hardware/pl080.h>

/* dma channel state information */

struct s3c64xx_dmac {
	struct sys_device	 sysdev;
	struct clk		*clk;
	void __iomem		*regs;
	struct s3c2410_dma_chan *channels;
	enum dma_ch		 chanbase;
};

/* pool to provide LLI buffers */
static struct dma_pool *dma_pool;

/* Debug configuration and code */

static unsigned char debug_show_buffs = 0;

static void dbg_showchan(struct s3c2410_dma_chan *chan)
{
	pr_debug("DMA%d: %08x->%08x L %08x C %08x,%08x S %08x\n",
		 chan->number,
		 readl(chan->regs + PL080_CH_SRC_ADDR),
		 readl(chan->regs + PL080_CH_DST_ADDR),
		 readl(chan->regs + PL080_CH_LLI),
		 readl(chan->regs + PL080_CH_CONTROL),
		 readl(chan->regs + PL080S_CH_CONTROL2),
		 readl(chan->regs + PL080S_CH_CONFIG));
}

static void show_lli(struct pl080s_lli *lli)
{
	pr_debug("LLI[%p] %08x->%08x, NL %08x C %08x,%08x\n",
		 lli, lli->src_addr, lli->dst_addr, lli->next_lli,
		 lli->control0, lli->control1);
}

static void dbg_showbuffs(struct s3c2410_dma_chan *chan)
{
	struct s3c64xx_dma_buff *ptr;
	struct s3c64xx_dma_buff *end;

	pr_debug("DMA%d: buffs next %p, curr %p, end %p\n",
		 chan->number, chan->next, chan->curr, chan->end);

	ptr = chan->next;
	end = chan->end;

	if (debug_show_buffs) {
		for (; ptr != NULL; ptr = ptr->next) {
			pr_debug("DMA%d: %08x ",
				 chan->number, ptr->lli_dma);
			show_lli(ptr->lli);
		}
	}
}

/* End of Debug */

static struct s3c2410_dma_chan *s3c64xx_dma_map_channel(unsigned int channel)
{
	struct s3c2410_dma_chan *chan;
	unsigned int start, offs;

	start = 0;

	if (channel >= DMACH_PCM1_TX)
		start = 8;

	for (offs = 0; offs < 8; offs++) {
		chan = &s3c2410_chans[start + offs];
		if (!chan->in_use)
			goto found;
	}

	return NULL;

found:
	s3c_dma_chan_map[channel] = chan;
	return chan;
}

int s3c2410_dma_config(unsigned int channel, int xferunit)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	if (chan == NULL)
		return -EINVAL;

	switch (xferunit) {
	case 1:
		chan->hw_width = 0;
		break;
	case 2:
		chan->hw_width = 1;
		break;
	case 4:
		chan->hw_width = 2;
		break;
	default:
		printk(KERN_ERR "%s: illegal width %d\n", __func__, xferunit);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_config);

static void s3c64xx_dma_fill_lli(struct s3c2410_dma_chan *chan,
				 struct pl080s_lli *lli,
				 dma_addr_t data, int size)
{
	dma_addr_t src, dst;
	u32 control0, control1;

	switch (chan->source) {
	case S3C2410_DMASRC_HW:
		src = chan->dev_addr;
		dst = data;
		control0 = PL080_CONTROL_SRC_AHB2;
		control0 |= PL080_CONTROL_DST_INCR;
		break;

	case S3C2410_DMASRC_MEM:
		src = data;
		dst = chan->dev_addr;
		control0 = PL080_CONTROL_DST_AHB2;
		control0 |= PL080_CONTROL_SRC_INCR;
		break;
	default:
		BUG();
	}

	/* note, we do not currently setup any of the burst controls */

	control1 = size >> chan->hw_width;	/* size in no of xfers */
	control0 |= PL080_CONTROL_PROT_SYS;	/* always in priv. mode */
	control0 |= PL080_CONTROL_TC_IRQ_EN;	/* always fire IRQ */
	control0 |= (u32)chan->hw_width << PL080_CONTROL_DWIDTH_SHIFT;
	control0 |= (u32)chan->hw_width << PL080_CONTROL_SWIDTH_SHIFT;

	lli->src_addr = src;
	lli->dst_addr = dst;
	lli->next_lli = 0;
	lli->control0 = control0;
	lli->control1 = control1;
}

static void s3c64xx_lli_to_regs(struct s3c2410_dma_chan *chan,
				struct pl080s_lli *lli)
{
	void __iomem *regs = chan->regs;

	pr_debug("%s: LLI %p => regs\n", __func__, lli);
	show_lli(lli);

	writel(lli->src_addr, regs + PL080_CH_SRC_ADDR);
	writel(lli->dst_addr, regs + PL080_CH_DST_ADDR);
	writel(lli->next_lli, regs + PL080_CH_LLI);
	writel(lli->control0, regs + PL080_CH_CONTROL);
	writel(lli->control1, regs + PL080S_CH_CONTROL2);
}

static int s3c64xx_dma_start(struct s3c2410_dma_chan *chan)
{
	struct s3c64xx_dmac *dmac = chan->dmac;
	u32 config;
	u32 bit = chan->bit;

	dbg_showchan(chan);

	pr_debug("%s: clearing interrupts\n", __func__);

	/* clear interrupts */
	writel(bit, dmac->regs + PL080_TC_CLEAR);
	writel(bit, dmac->regs + PL080_ERR_CLEAR);

	pr_debug("%s: starting channel\n", __func__);

	config = readl(chan->regs + PL080S_CH_CONFIG);
	config |= PL080_CONFIG_ENABLE;

	pr_debug("%s: writing config %08x\n", __func__, config);
	writel(config, chan->regs + PL080S_CH_CONFIG);

	return 0;
}

static int s3c64xx_dma_stop(struct s3c2410_dma_chan *chan)
{
	u32 config;
	int timeout;

	pr_debug("%s: stopping channel\n", __func__);

	dbg_showchan(chan);

	config = readl(chan->regs + PL080S_CH_CONFIG);
	config |= PL080_CONFIG_HALT;
	writel(config, chan->regs + PL080S_CH_CONFIG);

	timeout = 1000;
	do {
		config = readl(chan->regs + PL080S_CH_CONFIG);
		pr_debug("%s: %d - config %08x\n", __func__, timeout, config);
		if (config & PL080_CONFIG_ACTIVE)
			udelay(10);
		else
			break;
		} while (--timeout > 0);

	if (config & PL080_CONFIG_ACTIVE) {
		printk(KERN_ERR "%s: channel still active\n", __func__);
		return -EFAULT;
	}

	config = readl(chan->regs + PL080S_CH_CONFIG);
	config &= ~PL080_CONFIG_ENABLE;
	writel(config, chan->regs + PL080S_CH_CONFIG);

	return 0;
}

static inline void s3c64xx_dma_bufffdone(struct s3c2410_dma_chan *chan,
					 struct s3c64xx_dma_buff *buf,
					 enum s3c2410_dma_buffresult result)
{
	if (chan->callback_fn != NULL)
		(chan->callback_fn)(chan, buf->pw, 0, result);
}

static void s3c64xx_dma_freebuff(struct s3c64xx_dma_buff *buff)
{
	dma_pool_free(dma_pool, buff->lli, buff->lli_dma);
	kfree(buff);
}

static int s3c64xx_dma_flush(struct s3c2410_dma_chan *chan)
{
	struct s3c64xx_dma_buff *buff, *next;
	u32 config;

	dbg_showchan(chan);

	pr_debug("%s: flushing channel\n", __func__);

	config = readl(chan->regs + PL080S_CH_CONFIG);
	config &= ~PL080_CONFIG_ENABLE;
	writel(config, chan->regs + PL080S_CH_CONFIG);

	/* dump all the buffers associated with this channel */

	for (buff = chan->curr; buff != NULL; buff = next) {
		next = buff->next;
		pr_debug("%s: buff %p (next %p)\n", __func__, buff, buff->next);

		s3c64xx_dma_bufffdone(chan, buff, S3C2410_RES_ABORT);
		s3c64xx_dma_freebuff(buff);
	}

	chan->curr = chan->next = chan->end = NULL;

	return 0;
}

int s3c2410_dma_ctrl(unsigned int channel, enum s3c2410_chan_op op)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	WARN_ON(!chan);
	if (!chan)
		return -EINVAL;

	switch (op) {
	case S3C2410_DMAOP_START:
		return s3c64xx_dma_start(chan);

	case S3C2410_DMAOP_STOP:
		return s3c64xx_dma_stop(chan);

	case S3C2410_DMAOP_FLUSH:
		return s3c64xx_dma_flush(chan);

	/* belive PAUSE/RESUME are no-ops */
	case S3C2410_DMAOP_PAUSE:
	case S3C2410_DMAOP_RESUME:
	case S3C2410_DMAOP_STARTED:
	case S3C2410_DMAOP_TIMEOUT:
		return 0;
	}

	return -ENOENT;
}
EXPORT_SYMBOL(s3c2410_dma_ctrl);

/* s3c2410_dma_enque
 *
 */

int s3c2410_dma_enqueue(unsigned int channel, void *id,
			dma_addr_t data, int size)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	struct s3c64xx_dma_buff *next;
	struct s3c64xx_dma_buff *buff;
	struct pl080s_lli *lli;
	unsigned long flags;
	int ret;

	WARN_ON(!chan);
	if (!chan)
		return -EINVAL;

	buff = kzalloc(sizeof(struct s3c64xx_dma_buff), GFP_ATOMIC);
	if (!buff) {
		printk(KERN_ERR "%s: no memory for buffer\n", __func__);
		return -ENOMEM;
	}

	lli = dma_pool_alloc(dma_pool, GFP_ATOMIC, &buff->lli_dma);
	if (!lli) {
		printk(KERN_ERR "%s: no memory for lli\n", __func__);
		ret = -ENOMEM;
		goto err_buff;
	}

	pr_debug("%s: buff %p, dp %08x lli (%p, %08x) %d\n",
		 __func__, buff, data, lli, (u32)buff->lli_dma, size);

	buff->lli = lli;
	buff->pw = id;

	s3c64xx_dma_fill_lli(chan, lli, data, size);

	local_irq_save(flags);

	if ((next = chan->next) != NULL) {
		struct s3c64xx_dma_buff *end = chan->end;
		struct pl080s_lli *endlli = end->lli;

		pr_debug("enquing onto channel\n");

		end->next = buff;
		endlli->next_lli = buff->lli_dma;

		if (chan->flags & S3C2410_DMAF_CIRCULAR) {
			struct s3c64xx_dma_buff *curr = chan->curr;
			lli->next_lli = curr->lli_dma;
		}

		if (next == chan->curr) {
			writel(buff->lli_dma, chan->regs + PL080_CH_LLI);
			chan->next = buff;
		}

		show_lli(endlli);
		chan->end = buff;
	} else {
		pr_debug("enquing onto empty channel\n");

		chan->curr = buff;
		chan->next = buff;
		chan->end = buff;

		s3c64xx_lli_to_regs(chan, lli);
	}

	local_irq_restore(flags);

	show_lli(lli);

	dbg_showchan(chan);
	dbg_showbuffs(chan);
	return 0;

err_buff:
	kfree(buff);
	return ret;
}

EXPORT_SYMBOL(s3c2410_dma_enqueue);


int s3c2410_dma_devconfig(int channel,
			  enum s3c2410_dmasrc source,
			  unsigned long devaddr)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	u32 peripheral;
	u32 config = 0;

	pr_debug("%s: channel %d, source %d, dev %08lx, chan %p\n",
		 __func__, channel, source, devaddr, chan);

	WARN_ON(!chan);
	if (!chan)
		return -EINVAL;

	peripheral = (chan->peripheral & 0xf);
	chan->source = source;
	chan->dev_addr = devaddr;

	pr_debug("%s: peripheral %d\n", __func__, peripheral);

	switch (source) {
	case S3C2410_DMASRC_HW:
		config = 2 << PL080_CONFIG_FLOW_CONTROL_SHIFT;
		config |= peripheral << PL080_CONFIG_SRC_SEL_SHIFT;
		break;
	case S3C2410_DMASRC_MEM:
		config = 1 << PL080_CONFIG_FLOW_CONTROL_SHIFT;
		config |= peripheral << PL080_CONFIG_DST_SEL_SHIFT;
		break;
	default:
		printk(KERN_ERR "%s: bad source\n", __func__);
		return -EINVAL;
	}

	/* allow TC and ERR interrupts */
	config |= PL080_CONFIG_TC_IRQ_MASK;
	config |= PL080_CONFIG_ERR_IRQ_MASK;

	pr_debug("%s: config %08x\n", __func__, config);

	writel(config, chan->regs + PL080S_CH_CONFIG);

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_devconfig);


int s3c2410_dma_getposition(unsigned int channel,
			    dma_addr_t *src, dma_addr_t *dst)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);

	WARN_ON(!chan);
	if (!chan)
		return -EINVAL;

	if (src != NULL)
		*src = readl(chan->regs + PL080_CH_SRC_ADDR);

	if (dst != NULL)
		*dst = readl(chan->regs + PL080_CH_DST_ADDR);

	return 0;
}
EXPORT_SYMBOL(s3c2410_dma_getposition);

/* s3c2410_request_dma
 *
 * get control of an dma channel
*/

int s3c2410_dma_request(unsigned int channel,
			struct s3c2410_dma_client *client,
			void *dev)
{
	struct s3c2410_dma_chan *chan;
	unsigned long flags;

	pr_debug("dma%d: s3c2410_request_dma: client=%s, dev=%p\n",
		 channel, client->name, dev);

	local_irq_save(flags);

	chan = s3c64xx_dma_map_channel(channel);
	if (chan == NULL) {
		local_irq_restore(flags);
		return -EBUSY;
	}

	dbg_showchan(chan);

	chan->client = client;
	chan->in_use = 1;
	chan->peripheral = channel;

	local_irq_restore(flags);

	/* need to setup */

	pr_debug("%s: channel initialised, %p\n", __func__, chan);

	return chan->number | DMACH_LOW_LEVEL;
}

EXPORT_SYMBOL(s3c2410_dma_request);

/* s3c2410_dma_free
 *
 * release the given channel back to the system, will stop and flush
 * any outstanding transfers, and ensure the channel is ready for the
 * next claimant.
 *
 * Note, although a warning is currently printed if the freeing client
 * info is not the same as the registrant's client info, the free is still
 * allowed to go through.
*/

int s3c2410_dma_free(unsigned int channel, struct s3c2410_dma_client *client)
{
	struct s3c2410_dma_chan *chan = s3c_dma_lookup_channel(channel);
	unsigned long flags;

	if (chan == NULL)
		return -EINVAL;

	local_irq_save(flags);

	if (chan->client != client) {
		printk(KERN_WARNING "dma%d: possible free from different client (channel %p, passed %p)\n",
		       channel, chan->client, client);
	}

	/* sort out stopping and freeing the channel */


	chan->client = NULL;
	chan->in_use = 0;

	if (!(channel & DMACH_LOW_LEVEL))
		s3c_dma_chan_map[channel] = NULL;

	local_irq_restore(flags);

	return 0;
}

EXPORT_SYMBOL(s3c2410_dma_free);

static irqreturn_t s3c64xx_dma_irq(int irq, void *pw)
{
	struct s3c64xx_dmac *dmac = pw;
	struct s3c2410_dma_chan *chan;
	enum s3c2410_dma_buffresult res;
	u32 tcstat, errstat;
	u32 bit;
	int offs;

	tcstat = readl(dmac->regs + PL080_TC_STATUS);
	errstat = readl(dmac->regs + PL080_ERR_STATUS);

	for (offs = 0, bit = 1; offs < 8; offs++, bit <<= 1) {
		struct s3c64xx_dma_buff *buff;

		if (!(errstat & bit) && !(tcstat & bit))
			continue;

		chan = dmac->channels + offs;
		res = S3C2410_RES_ERR;

		if (tcstat & bit) {
			writel(bit, dmac->regs + PL080_TC_CLEAR);
			res = S3C2410_RES_OK;
		}

		if (errstat & bit)
			writel(bit, dmac->regs + PL080_ERR_CLEAR);

		/* 'next' points to the buffer that is next to the
		 * currently active buffer.
		 * For CIRCULAR queues, 'next' will be same as 'curr'
		 * when 'end' is the active buffer.
		 */
		buff = chan->curr;
		while (buff && buff != chan->next
				&& buff->next != chan->next)
			buff = buff->next;

		if (!buff)
			BUG();

		if (buff == chan->next)
			buff = chan->end;

		s3c64xx_dma_bufffdone(chan, buff, res);

		/* Free the node and update curr, if non-circular queue */
		if (!(chan->flags & S3C2410_DMAF_CIRCULAR)) {
			chan->curr = buff->next;
			s3c64xx_dma_freebuff(buff);
		}

		/* Update 'next' */
		buff = chan->next;
		if (chan->next == chan->end) {
			chan->next = chan->curr;
			if (!(chan->flags & S3C2410_DMAF_CIRCULAR))
				chan->end = NULL;
		} else {
			chan->next = buff->next;
		}
	}

	return IRQ_HANDLED;
}

static struct sysdev_class dma_sysclass = {
	.name		= "s3c64xx-dma",
};

static int s3c64xx_dma_init1(int chno, enum dma_ch chbase,
			     int irq, unsigned int base)
{
	struct s3c2410_dma_chan *chptr = &s3c2410_chans[chno];
	struct s3c64xx_dmac *dmac;
	char clkname[16];
	void __iomem *regs;
	void __iomem *regptr;
	int err, ch;

	dmac = kzalloc(sizeof(struct s3c64xx_dmac), GFP_KERNEL);
	if (!dmac) {
		printk(KERN_ERR "%s: failed to alloc mem\n", __func__);
		return -ENOMEM;
	}

	dmac->sysdev.id = chno / 8;
	dmac->sysdev.cls = &dma_sysclass;

	err = sysdev_register(&dmac->sysdev);
	if (err) {
		printk(KERN_ERR "%s: failed to register sysdevice\n", __func__);
		goto err_alloc;
	}

	regs = ioremap(base, 0x200);
	if (!regs) {
		printk(KERN_ERR "%s: failed to ioremap()\n", __func__);
		err = -ENXIO;
		goto err_dev;
	}

	snprintf(clkname, sizeof(clkname), "dma%d", dmac->sysdev.id);

	dmac->clk = clk_get(NULL, clkname);
	if (IS_ERR(dmac->clk)) {
		printk(KERN_ERR "%s: failed to get clock %s\n", __func__, clkname);
		err = PTR_ERR(dmac->clk);
		goto err_map;
	}

	clk_enable(dmac->clk);

	dmac->regs = regs;
	dmac->chanbase = chbase;
	dmac->channels = chptr;

	err = request_irq(irq, s3c64xx_dma_irq, 0, "DMA", dmac);
	if (err < 0) {
		printk(KERN_ERR "%s: failed to get irq\n", __func__);
		goto err_clk;
	}

	regptr = regs + PL080_Cx_BASE(0);

	for (ch = 0; ch < 8; ch++, chno++, chptr++) {
		printk(KERN_INFO "%s: registering DMA %d (%p)\n",
		       __func__, chno, regptr);

		chptr->bit = 1 << ch;
		chptr->number = chno;
		chptr->dmac = dmac;
		chptr->regs = regptr;
		regptr += PL008_Cx_STRIDE;
	}

	/* for the moment, permanently enable the controller */
	writel(PL080_CONFIG_ENABLE, regs + PL080_CONFIG);

	printk(KERN_INFO "PL080: IRQ %d, at %p\n", irq, regs);

	return 0;

err_clk:
	clk_disable(dmac->clk);
	clk_put(dmac->clk);
err_map:
	iounmap(regs);
err_dev:
	sysdev_unregister(&dmac->sysdev);
err_alloc:
	kfree(dmac);
	return err;
}

static int __init s3c64xx_dma_init(void)
{
	int ret;

	printk(KERN_INFO "%s: Registering DMA channels\n", __func__);

	dma_pool = dma_pool_create("DMA-LLI", NULL, sizeof(struct pl080s_lli), 16, 0);
	if (!dma_pool) {
		printk(KERN_ERR "%s: failed to create pool\n", __func__);
		return -ENOMEM;
	}

	ret = sysdev_class_register(&dma_sysclass);
	if (ret) {
		printk(KERN_ERR "%s: failed to create sysclass\n", __func__);
		return -ENOMEM;
	}

	/* Set all DMA configuration to be DMA, not SDMA */
	writel(0xffffff, S3C_SYSREG(0x110));

	/* Register standard DMA controlers */
	s3c64xx_dma_init1(0, DMACH_UART0, IRQ_DMA0, 0x75000000);
	s3c64xx_dma_init1(8, DMACH_PCM1_TX, IRQ_DMA1, 0x75100000);

	return 0;
}

arch_initcall(s3c64xx_dma_init);
