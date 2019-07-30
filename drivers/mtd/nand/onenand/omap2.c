// SPDX-License-Identifier: GPL-2.0-only
/*
 *  OneNAND driver for OMAP2 / OMAP3
 *
 *  Copyright © 2005-2006 Nokia Corporation
 *
 *  Author: Jarkko Lavinen <jarkko.lavinen@nokia.com> and Juha Yrjölä
 *  IRQ and DMA support written by Timo Teras
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <linux/of_device.h>
#include <linux/omap-gpmc.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>

#include <asm/mach/flash.h>

#define DRIVER_NAME "omap2-onenand"

#define ONENAND_BUFRAM_SIZE	(1024 * 5)

struct omap2_onenand {
	struct platform_device *pdev;
	int gpmc_cs;
	unsigned long phys_base;
	struct gpio_desc *int_gpiod;
	struct mtd_info mtd;
	struct onenand_chip onenand;
	struct completion irq_done;
	struct completion dma_done;
	struct dma_chan *dma_chan;
};

static void omap2_onenand_dma_complete_func(void *completion)
{
	complete(completion);
}

static irqreturn_t omap2_onenand_interrupt(int irq, void *dev_id)
{
	struct omap2_onenand *c = dev_id;

	complete(&c->irq_done);

	return IRQ_HANDLED;
}

static inline unsigned short read_reg(struct omap2_onenand *c, int reg)
{
	return readw(c->onenand.base + reg);
}

static inline void write_reg(struct omap2_onenand *c, unsigned short value,
			     int reg)
{
	writew(value, c->onenand.base + reg);
}

static int omap2_onenand_set_cfg(struct omap2_onenand *c,
				 bool sr, bool sw,
				 int latency, int burst_len)
{
	unsigned short reg = ONENAND_SYS_CFG1_RDY | ONENAND_SYS_CFG1_INT;

	reg |= latency << ONENAND_SYS_CFG1_BRL_SHIFT;

	switch (burst_len) {
	case 0:		/* continuous */
		break;
	case 4:
		reg |= ONENAND_SYS_CFG1_BL_4;
		break;
	case 8:
		reg |= ONENAND_SYS_CFG1_BL_8;
		break;
	case 16:
		reg |= ONENAND_SYS_CFG1_BL_16;
		break;
	case 32:
		reg |= ONENAND_SYS_CFG1_BL_32;
		break;
	default:
		return -EINVAL;
	}

	if (latency > 5)
		reg |= ONENAND_SYS_CFG1_HF;
	if (latency > 7)
		reg |= ONENAND_SYS_CFG1_VHF;
	if (sr)
		reg |= ONENAND_SYS_CFG1_SYNC_READ;
	if (sw)
		reg |= ONENAND_SYS_CFG1_SYNC_WRITE;

	write_reg(c, reg, ONENAND_REG_SYS_CFG1);

	return 0;
}

static int omap2_onenand_get_freq(int ver)
{
	switch ((ver >> 4) & 0xf) {
	case 0:
		return 40;
	case 1:
		return 54;
	case 2:
		return 66;
	case 3:
		return 83;
	case 4:
		return 104;
	}

	return -EINVAL;
}

static void wait_err(char *msg, int state, unsigned int ctrl, unsigned int intr)
{
	printk(KERN_ERR "onenand_wait: %s! state %d ctrl 0x%04x intr 0x%04x\n",
	       msg, state, ctrl, intr);
}

static void wait_warn(char *msg, int state, unsigned int ctrl,
		      unsigned int intr)
{
	printk(KERN_WARNING "onenand_wait: %s! state %d ctrl 0x%04x "
	       "intr 0x%04x\n", msg, state, ctrl, intr);
}

static int omap2_onenand_wait(struct mtd_info *mtd, int state)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	unsigned int intr = 0;
	unsigned int ctrl, ctrl_mask;
	unsigned long timeout;
	u32 syscfg;

	if (state == FL_RESETING || state == FL_PREPARING_ERASE ||
	    state == FL_VERIFYING_ERASE) {
		int i = 21;
		unsigned int intr_flags = ONENAND_INT_MASTER;

		switch (state) {
		case FL_RESETING:
			intr_flags |= ONENAND_INT_RESET;
			break;
		case FL_PREPARING_ERASE:
			intr_flags |= ONENAND_INT_ERASE;
			break;
		case FL_VERIFYING_ERASE:
			i = 101;
			break;
		}

		while (--i) {
			udelay(1);
			intr = read_reg(c, ONENAND_REG_INTERRUPT);
			if (intr & ONENAND_INT_MASTER)
				break;
		}
		ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
		if (ctrl & ONENAND_CTRL_ERROR) {
			wait_err("controller error", state, ctrl, intr);
			return -EIO;
		}
		if ((intr & intr_flags) == intr_flags)
			return 0;
		/* Continue in wait for interrupt branch */
	}

	if (state != FL_READING) {
		int result;

		/* Turn interrupts on */
		syscfg = read_reg(c, ONENAND_REG_SYS_CFG1);
		if (!(syscfg & ONENAND_SYS_CFG1_IOBE)) {
			syscfg |= ONENAND_SYS_CFG1_IOBE;
			write_reg(c, syscfg, ONENAND_REG_SYS_CFG1);
			/* Add a delay to let GPIO settle */
			syscfg = read_reg(c, ONENAND_REG_SYS_CFG1);
		}

		reinit_completion(&c->irq_done);
		result = gpiod_get_value(c->int_gpiod);
		if (result < 0) {
			ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
			intr = read_reg(c, ONENAND_REG_INTERRUPT);
			wait_err("gpio error", state, ctrl, intr);
			return result;
		} else if (result == 0) {
			int retry_cnt = 0;
retry:
			if (!wait_for_completion_io_timeout(&c->irq_done,
						msecs_to_jiffies(20))) {
				/* Timeout after 20ms */
				ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
				if (ctrl & ONENAND_CTRL_ONGO &&
				    !this->ongoing) {
					/*
					 * The operation seems to be still going
					 * so give it some more time.
					 */
					retry_cnt += 1;
					if (retry_cnt < 3)
						goto retry;
					intr = read_reg(c,
							ONENAND_REG_INTERRUPT);
					wait_err("timeout", state, ctrl, intr);
					return -EIO;
				}
				intr = read_reg(c, ONENAND_REG_INTERRUPT);
				if ((intr & ONENAND_INT_MASTER) == 0)
					wait_warn("timeout", state, ctrl, intr);
			}
		}
	} else {
		int retry_cnt = 0;

		/* Turn interrupts off */
		syscfg = read_reg(c, ONENAND_REG_SYS_CFG1);
		syscfg &= ~ONENAND_SYS_CFG1_IOBE;
		write_reg(c, syscfg, ONENAND_REG_SYS_CFG1);

		timeout = jiffies + msecs_to_jiffies(20);
		while (1) {
			if (time_before(jiffies, timeout)) {
				intr = read_reg(c, ONENAND_REG_INTERRUPT);
				if (intr & ONENAND_INT_MASTER)
					break;
			} else {
				/* Timeout after 20ms */
				ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
				if (ctrl & ONENAND_CTRL_ONGO) {
					/*
					 * The operation seems to be still going
					 * so give it some more time.
					 */
					retry_cnt += 1;
					if (retry_cnt < 3) {
						timeout = jiffies +
							  msecs_to_jiffies(20);
						continue;
					}
				}
				break;
			}
		}
	}

	intr = read_reg(c, ONENAND_REG_INTERRUPT);
	ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);

	if (intr & ONENAND_INT_READ) {
		int ecc = read_reg(c, ONENAND_REG_ECC_STATUS);

		if (ecc) {
			unsigned int addr1, addr8;

			addr1 = read_reg(c, ONENAND_REG_START_ADDRESS1);
			addr8 = read_reg(c, ONENAND_REG_START_ADDRESS8);
			if (ecc & ONENAND_ECC_2BIT_ALL) {
				printk(KERN_ERR "onenand_wait: ECC error = "
				       "0x%04x, addr1 %#x, addr8 %#x\n",
				       ecc, addr1, addr8);
				mtd->ecc_stats.failed++;
				return -EBADMSG;
			} else if (ecc & ONENAND_ECC_1BIT_ALL) {
				printk(KERN_NOTICE "onenand_wait: correctable "
				       "ECC error = 0x%04x, addr1 %#x, "
				       "addr8 %#x\n", ecc, addr1, addr8);
				mtd->ecc_stats.corrected++;
			}
		}
	} else if (state == FL_READING) {
		wait_err("timeout", state, ctrl, intr);
		return -EIO;
	}

	if (ctrl & ONENAND_CTRL_ERROR) {
		wait_err("controller error", state, ctrl, intr);
		if (ctrl & ONENAND_CTRL_LOCK)
			printk(KERN_ERR "onenand_wait: "
					"Device is write protected!!!\n");
		return -EIO;
	}

	ctrl_mask = 0xFE9F;
	if (this->ongoing)
		ctrl_mask &= ~0x8000;

	if (ctrl & ctrl_mask)
		wait_warn("unexpected controller status", state, ctrl, intr);

	return 0;
}

static inline int omap2_onenand_bufferram_offset(struct mtd_info *mtd, int area)
{
	struct onenand_chip *this = mtd->priv;

	if (ONENAND_CURRENT_BUFFERRAM(this)) {
		if (area == ONENAND_DATARAM)
			return this->writesize;
		if (area == ONENAND_SPARERAM)
			return mtd->oobsize;
	}

	return 0;
}

static inline int omap2_onenand_dma_transfer(struct omap2_onenand *c,
					     dma_addr_t src, dma_addr_t dst,
					     size_t count)
{
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t cookie;

	tx = dmaengine_prep_dma_memcpy(c->dma_chan, dst, src, count, 0);
	if (!tx) {
		dev_err(&c->pdev->dev, "Failed to prepare DMA memcpy\n");
		return -EIO;
	}

	reinit_completion(&c->dma_done);

	tx->callback = omap2_onenand_dma_complete_func;
	tx->callback_param = &c->dma_done;

	cookie = tx->tx_submit(tx);
	if (dma_submit_error(cookie)) {
		dev_err(&c->pdev->dev, "Failed to do DMA tx_submit\n");
		return -EIO;
	}

	dma_async_issue_pending(c->dma_chan);

	if (!wait_for_completion_io_timeout(&c->dma_done,
					    msecs_to_jiffies(20))) {
		dmaengine_terminate_sync(c->dma_chan);
		return -ETIMEDOUT;
	}

	return 0;
}

static int omap2_onenand_read_bufferram(struct mtd_info *mtd, int area,
					unsigned char *buffer, int offset,
					size_t count)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	struct device *dev = &c->pdev->dev;
	void *buf = (void *)buffer;
	dma_addr_t dma_src, dma_dst;
	int bram_offset, err;
	size_t xtra;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	/*
	 * If the buffer address is not DMA-able, len is not long enough to make
	 * DMA transfers profitable or panic_write() may be in an interrupt
	 * context fallback to PIO mode.
	 */
	if (!virt_addr_valid(buf) || bram_offset & 3 || (size_t)buf & 3 ||
	    count < 384 || in_interrupt() || oops_in_progress )
		goto out_copy;

	xtra = count & 3;
	if (xtra) {
		count -= xtra;
		memcpy(buf + count, this->base + bram_offset + count, xtra);
	}

	dma_dst = dma_map_single(dev, buf, count, DMA_FROM_DEVICE);
	dma_src = c->phys_base + bram_offset;

	if (dma_mapping_error(dev, dma_dst)) {
		dev_err(dev, "Couldn't DMA map a %d byte buffer\n", count);
		goto out_copy;
	}

	err = omap2_onenand_dma_transfer(c, dma_src, dma_dst, count);
	dma_unmap_single(dev, dma_dst, count, DMA_FROM_DEVICE);
	if (!err)
		return 0;

	dev_err(dev, "timeout waiting for DMA\n");

out_copy:
	memcpy(buf, this->base + bram_offset, count);
	return 0;
}

static int omap2_onenand_write_bufferram(struct mtd_info *mtd, int area,
					 const unsigned char *buffer,
					 int offset, size_t count)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	struct device *dev = &c->pdev->dev;
	void *buf = (void *)buffer;
	dma_addr_t dma_src, dma_dst;
	int bram_offset, err;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	/*
	 * If the buffer address is not DMA-able, len is not long enough to make
	 * DMA transfers profitable or panic_write() may be in an interrupt
	 * context fallback to PIO mode.
	 */
	if (!virt_addr_valid(buf) || bram_offset & 3 || (size_t)buf & 3 ||
	    count < 384 || in_interrupt() || oops_in_progress )
		goto out_copy;

	dma_src = dma_map_single(dev, buf, count, DMA_TO_DEVICE);
	dma_dst = c->phys_base + bram_offset;
	if (dma_mapping_error(dev, dma_src)) {
		dev_err(dev, "Couldn't DMA map a %d byte buffer\n", count);
		goto out_copy;
	}

	err = omap2_onenand_dma_transfer(c, dma_src, dma_dst, count);
	dma_unmap_page(dev, dma_src, count, DMA_TO_DEVICE);
	if (!err)
		return 0;

	dev_err(dev, "timeout waiting for DMA\n");

out_copy:
	memcpy(this->base + bram_offset, buf, count);
	return 0;
}

static void omap2_onenand_shutdown(struct platform_device *pdev)
{
	struct omap2_onenand *c = dev_get_drvdata(&pdev->dev);

	/* With certain content in the buffer RAM, the OMAP boot ROM code
	 * can recognize the flash chip incorrectly. Zero it out before
	 * soft reset.
	 */
	memset((__force void *)c->onenand.base, 0, ONENAND_BUFRAM_SIZE);
}

static int omap2_onenand_probe(struct platform_device *pdev)
{
	u32 val;
	dma_cap_mask_t mask;
	int freq, latency, r;
	struct resource *res;
	struct omap2_onenand *c;
	struct gpmc_onenand_info info;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "error getting memory resource\n");
		return -EINVAL;
	}

	r = of_property_read_u32(np, "reg", &val);
	if (r) {
		dev_err(dev, "reg not found in DT\n");
		return r;
	}

	c = devm_kzalloc(dev, sizeof(struct omap2_onenand), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	init_completion(&c->irq_done);
	init_completion(&c->dma_done);
	c->gpmc_cs = val;
	c->phys_base = res->start;

	c->onenand.base = devm_ioremap_resource(dev, res);
	if (IS_ERR(c->onenand.base))
		return PTR_ERR(c->onenand.base);

	c->int_gpiod = devm_gpiod_get_optional(dev, "int", GPIOD_IN);
	if (IS_ERR(c->int_gpiod)) {
		r = PTR_ERR(c->int_gpiod);
		/* Just try again if this happens */
		if (r != -EPROBE_DEFER)
			dev_err(dev, "error getting gpio: %d\n", r);
		return r;
	}

	if (c->int_gpiod) {
		r = devm_request_irq(dev, gpiod_to_irq(c->int_gpiod),
				     omap2_onenand_interrupt,
				     IRQF_TRIGGER_RISING, "onenand", c);
		if (r)
			return r;

		c->onenand.wait = omap2_onenand_wait;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	c->dma_chan = dma_request_channel(mask, NULL, NULL);
	if (c->dma_chan) {
		c->onenand.read_bufferram = omap2_onenand_read_bufferram;
		c->onenand.write_bufferram = omap2_onenand_write_bufferram;
	}

	c->pdev = pdev;
	c->mtd.priv = &c->onenand;
	c->mtd.dev.parent = dev;
	mtd_set_of_node(&c->mtd, dev->of_node);

	dev_info(dev, "initializing on CS%d (0x%08lx), va %p, %s mode\n",
		 c->gpmc_cs, c->phys_base, c->onenand.base,
		 c->dma_chan ? "DMA" : "PIO");

	if ((r = onenand_scan(&c->mtd, 1)) < 0)
		goto err_release_dma;

	freq = omap2_onenand_get_freq(c->onenand.version_id);
	if (freq > 0) {
		switch (freq) {
		case 104:
			latency = 7;
			break;
		case 83:
			latency = 6;
			break;
		case 66:
			latency = 5;
			break;
		case 56:
			latency = 4;
			break;
		default:	/* 40 MHz or lower */
			latency = 3;
			break;
		}

		r = gpmc_omap_onenand_set_timings(dev, c->gpmc_cs,
						  freq, latency, &info);
		if (r)
			goto err_release_onenand;

		r = omap2_onenand_set_cfg(c, info.sync_read, info.sync_write,
					  latency, info.burst_len);
		if (r)
			goto err_release_onenand;

		if (info.sync_read || info.sync_write)
			dev_info(dev, "optimized timings for %d MHz\n", freq);
	}

	r = mtd_device_register(&c->mtd, NULL, 0);
	if (r)
		goto err_release_onenand;

	platform_set_drvdata(pdev, c);

	return 0;

err_release_onenand:
	onenand_release(&c->mtd);
err_release_dma:
	if (c->dma_chan)
		dma_release_channel(c->dma_chan);

	return r;
}

static int omap2_onenand_remove(struct platform_device *pdev)
{
	struct omap2_onenand *c = dev_get_drvdata(&pdev->dev);

	onenand_release(&c->mtd);
	if (c->dma_chan)
		dma_release_channel(c->dma_chan);
	omap2_onenand_shutdown(pdev);

	return 0;
}

static const struct of_device_id omap2_onenand_id_table[] = {
	{ .compatible = "ti,omap2-onenand", },
	{},
};
MODULE_DEVICE_TABLE(of, omap2_onenand_id_table);

static struct platform_driver omap2_onenand_driver = {
	.probe		= omap2_onenand_probe,
	.remove		= omap2_onenand_remove,
	.shutdown	= omap2_onenand_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.of_match_table = omap2_onenand_id_table,
	},
};

module_platform_driver(omap2_onenand_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarkko Lavinen <jarkko.lavinen@nokia.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on OMAP2 / OMAP3");
