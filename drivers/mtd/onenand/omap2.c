/*
 *  linux/drivers/mtd/onenand/omap2.c
 *
 *  OneNAND driver for OMAP2 / OMAP3
 *
 *  Copyright © 2005-2006 Nokia Corporation
 *
 *  Author: Jarkko Lavinen <jarkko.lavinen@nokia.com> and Juha Yrjölä
 *  IRQ and DMA support written by Timo Teras
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING. If not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/gpio.h>

#include <asm/mach/flash.h>
#include <linux/platform_data/mtd-onenand-omap2.h>

#define DRIVER_NAME "omap2-onenand"

#define ONENAND_BUFRAM_SIZE	(1024 * 5)

struct omap2_onenand {
	struct platform_device *pdev;
	int gpmc_cs;
	unsigned long phys_base;
	unsigned int mem_size;
	int gpio_irq;
	struct mtd_info mtd;
	struct onenand_chip onenand;
	struct completion irq_done;
	struct completion dma_done;
	struct dma_chan *dma_chan;
	int freq;
	int (*setup)(void __iomem *base, int *freq_ptr);
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
		result = gpio_get_value(c->gpio_irq);
		if (result < 0) {
			ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
			intr = read_reg(c, ONENAND_REG_INTERRUPT);
			wait_err("gpio error", state, ctrl, intr);
			return -EIO;
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
	dma_addr_t dma_src, dma_dst;
	int bram_offset;
	void *buf = (void *)buffer;
	size_t xtra;
	int ret;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	if (bram_offset & 3 || (size_t)buf & 3 || count < 384)
		goto out_copy;

	/* panic_write() may be in an interrupt context */
	if (in_interrupt() || oops_in_progress)
		goto out_copy;

	if (buf >= high_memory) {
		struct page *p1;

		if (((size_t)buf & PAGE_MASK) !=
		    ((size_t)(buf + count - 1) & PAGE_MASK))
			goto out_copy;
		p1 = vmalloc_to_page(buf);
		if (!p1)
			goto out_copy;
		buf = page_address(p1) + ((size_t)buf & ~PAGE_MASK);
	}

	xtra = count & 3;
	if (xtra) {
		count -= xtra;
		memcpy(buf + count, this->base + bram_offset + count, xtra);
	}

	dma_src = c->phys_base + bram_offset;
	dma_dst = dma_map_single(&c->pdev->dev, buf, count, DMA_FROM_DEVICE);
	if (dma_mapping_error(&c->pdev->dev, dma_dst)) {
		dev_err(&c->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n",
			count);
		goto out_copy;
	}

	ret = omap2_onenand_dma_transfer(c, dma_src, dma_dst, count);
	dma_unmap_single(&c->pdev->dev, dma_dst, count, DMA_FROM_DEVICE);

	if (ret) {
		dev_err(&c->pdev->dev, "timeout waiting for DMA\n");
		goto out_copy;
	}

	return 0;

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
	dma_addr_t dma_src, dma_dst;
	int bram_offset;
	void *buf = (void *)buffer;
	int ret;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	if (bram_offset & 3 || (size_t)buf & 3 || count < 384)
		goto out_copy;

	/* panic_write() may be in an interrupt context */
	if (in_interrupt() || oops_in_progress)
		goto out_copy;

	if (buf >= high_memory) {
		struct page *p1;

		if (((size_t)buf & PAGE_MASK) !=
		    ((size_t)(buf + count - 1) & PAGE_MASK))
			goto out_copy;
		p1 = vmalloc_to_page(buf);
		if (!p1)
			goto out_copy;
		buf = page_address(p1) + ((size_t)buf & ~PAGE_MASK);
	}

	dma_src = dma_map_single(&c->pdev->dev, buf, count, DMA_TO_DEVICE);
	dma_dst = c->phys_base + bram_offset;
	if (dma_mapping_error(&c->pdev->dev, dma_src)) {
		dev_err(&c->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n",
			count);
		return -1;
	}

	ret = omap2_onenand_dma_transfer(c, dma_src, dma_dst, count);
	dma_unmap_single(&c->pdev->dev, dma_src, count, DMA_TO_DEVICE);

	if (ret) {
		dev_err(&c->pdev->dev, "timeout waiting for DMA\n");
		goto out_copy;
	}

	return 0;

out_copy:
	memcpy(this->base + bram_offset, buf, count);
	return 0;
}

static struct platform_driver omap2_onenand_driver;

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
	dma_cap_mask_t mask;
	struct omap_onenand_platform_data *pdata;
	struct omap2_onenand *c;
	struct onenand_chip *this;
	int r;
	struct resource *res;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data missing\n");
		return -ENODEV;
	}

	c = kzalloc(sizeof(struct omap2_onenand), GFP_KERNEL);
	if (!c)
		return -ENOMEM;

	init_completion(&c->irq_done);
	init_completion(&c->dma_done);
	c->gpmc_cs = pdata->cs;
	c->gpio_irq = pdata->gpio_irq;
	if (pdata->dma_channel < 0) {
		/* if -1, don't use DMA */
		c->gpio_irq = 0;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		r = -EINVAL;
		dev_err(&pdev->dev, "error getting memory resource\n");
		goto err_kfree;
	}

	c->phys_base = res->start;
	c->mem_size = resource_size(res);

	if (request_mem_region(c->phys_base, c->mem_size,
			       pdev->dev.driver->name) == NULL) {
		dev_err(&pdev->dev, "Cannot reserve memory region at 0x%08lx, size: 0x%x\n",
						c->phys_base, c->mem_size);
		r = -EBUSY;
		goto err_kfree;
	}
	c->onenand.base = ioremap(c->phys_base, c->mem_size);
	if (c->onenand.base == NULL) {
		r = -ENOMEM;
		goto err_release_mem_region;
	}

	if (pdata->onenand_setup != NULL) {
		r = pdata->onenand_setup(c->onenand.base, &c->freq);
		if (r < 0) {
			dev_err(&pdev->dev, "Onenand platform setup failed: "
				"%d\n", r);
			goto err_iounmap;
		}
		c->setup = pdata->onenand_setup;
	}

	if (c->gpio_irq) {
		if ((r = gpio_request(c->gpio_irq, "OneNAND irq")) < 0) {
			dev_err(&pdev->dev,  "Failed to request GPIO%d for "
				"OneNAND\n", c->gpio_irq);
			goto err_iounmap;
		}
		gpio_direction_input(c->gpio_irq);

		if ((r = request_irq(gpio_to_irq(c->gpio_irq),
				     omap2_onenand_interrupt, IRQF_TRIGGER_RISING,
				     pdev->dev.driver->name, c)) < 0)
			goto err_release_gpio;

		this->wait = omap2_onenand_wait;
	}

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	c->dma_chan = dma_request_channel(mask, NULL, NULL);

	dev_info(&pdev->dev, "initializing on CS%d, phys base 0x%08lx, virtual "
		 "base %p, freq %d MHz, %s mode\n", c->gpmc_cs, c->phys_base,
		 c->onenand.base, c->freq, c->dma_chan ? "DMA" : "PIO");

	c->pdev = pdev;
	c->mtd.priv = &c->onenand;

	c->mtd.dev.parent = &pdev->dev;
	mtd_set_of_node(&c->mtd, pdata->of_node);

	this = &c->onenand;
	if (c->dma_chan) {
		this->read_bufferram = omap2_onenand_read_bufferram;
		this->write_bufferram = omap2_onenand_write_bufferram;
	}

	if ((r = onenand_scan(&c->mtd, 1)) < 0)
		goto err_release_dma;

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
	if (c->gpio_irq)
		free_irq(gpio_to_irq(c->gpio_irq), c);
err_release_gpio:
	if (c->gpio_irq)
		gpio_free(c->gpio_irq);
err_iounmap:
	iounmap(c->onenand.base);
err_release_mem_region:
	release_mem_region(c->phys_base, c->mem_size);
err_kfree:
	kfree(c);

	return r;
}

static int omap2_onenand_remove(struct platform_device *pdev)
{
	struct omap2_onenand *c = dev_get_drvdata(&pdev->dev);

	onenand_release(&c->mtd);
	if (c->dma_chan)
		dma_release_channel(c->dma_chan);
	omap2_onenand_shutdown(pdev);
	if (c->gpio_irq) {
		free_irq(gpio_to_irq(c->gpio_irq), c);
		gpio_free(c->gpio_irq);
	}
	iounmap(c->onenand.base);
	release_mem_region(c->phys_base, c->mem_size);
	kfree(c);

	return 0;
}

static struct platform_driver omap2_onenand_driver = {
	.probe		= omap2_onenand_probe,
	.remove		= omap2_onenand_remove,
	.shutdown	= omap2_onenand_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
	},
};

module_platform_driver(omap2_onenand_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarkko Lavinen <jarkko.lavinen@nokia.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on OMAP2 / OMAP3");
