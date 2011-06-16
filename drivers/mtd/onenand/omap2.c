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
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/onenand.h>
#include <linux/mtd/partitions.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <asm/mach/flash.h>
#include <mach/gpmc.h>
#include <mach/onenand.h>
#include <mach/gpio.h>

#include <mach/dma.h>

#include <mach/board.h>

#define DRIVER_NAME "omap2-onenand"

#define ONENAND_IO_SIZE		SZ_128K
#define ONENAND_BUFRAM_SIZE	(1024 * 5)

struct omap2_onenand {
	struct platform_device *pdev;
	int gpmc_cs;
	unsigned long phys_base;
	int gpio_irq;
	struct mtd_info mtd;
	struct mtd_partition *parts;
	struct onenand_chip onenand;
	struct completion irq_done;
	struct completion dma_done;
	int dma_channel;
	int freq;
	int (*setup)(void __iomem *base, int freq);
};

static void omap2_onenand_dma_cb(int lch, u16 ch_status, void *data)
{
	struct omap2_onenand *c = data;

	complete(&c->dma_done);
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
	unsigned int intr = 0;
	unsigned int ctrl;
	unsigned long timeout;
	u32 syscfg;

	if (state == FL_RESETING) {
		int i;

		for (i = 0; i < 20; i++) {
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
		if (!(intr & ONENAND_INT_RESET)) {
			wait_err("timeout", state, ctrl, intr);
			return -EIO;
		}
		return 0;
	}

	if (state != FL_READING) {
		int result;

		/* Turn interrupts on */
		syscfg = read_reg(c, ONENAND_REG_SYS_CFG1);
		if (!(syscfg & ONENAND_SYS_CFG1_IOBE)) {
			syscfg |= ONENAND_SYS_CFG1_IOBE;
			write_reg(c, syscfg, ONENAND_REG_SYS_CFG1);
			if (cpu_is_omap34xx())
				/* Add a delay to let GPIO settle */
				syscfg = read_reg(c, ONENAND_REG_SYS_CFG1);
		}

		INIT_COMPLETION(c->irq_done);
		if (c->gpio_irq) {
			result = gpio_get_value(c->gpio_irq);
			if (result == -1) {
				ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
				intr = read_reg(c, ONENAND_REG_INTERRUPT);
				wait_err("gpio error", state, ctrl, intr);
				return -EIO;
			}
		} else
			result = 0;
		if (result == 0) {
			int retry_cnt = 0;
retry:
			result = wait_for_completion_timeout(&c->irq_done,
						    msecs_to_jiffies(20));
			if (result == 0) {
				/* Timeout after 20ms */
				ctrl = read_reg(c, ONENAND_REG_CTRL_STATUS);
				if (ctrl & ONENAND_CTRL_ONGO) {
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

	if (ctrl & 0xFE9F)
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

#if defined(CONFIG_ARCH_OMAP3) || defined(MULTI_OMAP2)

static int omap3_onenand_read_bufferram(struct mtd_info *mtd, int area,
					unsigned char *buffer, int offset,
					size_t count)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	dma_addr_t dma_src, dma_dst;
	int bram_offset;
	unsigned long timeout;
	void *buf = (void *)buffer;
	size_t xtra;
	volatile unsigned *done;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	if (bram_offset & 3 || (size_t)buf & 3 || count < 384)
		goto out_copy;

	/* panic_write() may be in an interrupt context */
	if (in_interrupt())
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

	omap_set_dma_transfer_params(c->dma_channel, OMAP_DMA_DATA_TYPE_S32,
				     count >> 2, 1, 0, 0, 0);
	omap_set_dma_src_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				dma_src, 0, 0);
	omap_set_dma_dest_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				 dma_dst, 0, 0);

	INIT_COMPLETION(c->dma_done);
	omap_start_dma(c->dma_channel);

	timeout = jiffies + msecs_to_jiffies(20);
	done = &c->dma_done.done;
	while (time_before(jiffies, timeout))
		if (*done)
			break;

	dma_unmap_single(&c->pdev->dev, dma_dst, count, DMA_FROM_DEVICE);

	if (!*done) {
		dev_err(&c->pdev->dev, "timeout waiting for DMA\n");
		goto out_copy;
	}

	return 0;

out_copy:
	memcpy(buf, this->base + bram_offset, count);
	return 0;
}

static int omap3_onenand_write_bufferram(struct mtd_info *mtd, int area,
					 const unsigned char *buffer,
					 int offset, size_t count)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	dma_addr_t dma_src, dma_dst;
	int bram_offset;
	unsigned long timeout;
	void *buf = (void *)buffer;
	volatile unsigned *done;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	if (bram_offset & 3 || (size_t)buf & 3 || count < 384)
		goto out_copy;

	/* panic_write() may be in an interrupt context */
	if (in_interrupt())
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
	if (dma_mapping_error(&c->pdev->dev, dma_dst)) {
		dev_err(&c->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n",
			count);
		return -1;
	}

	omap_set_dma_transfer_params(c->dma_channel, OMAP_DMA_DATA_TYPE_S32,
				     count >> 2, 1, 0, 0, 0);
	omap_set_dma_src_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				dma_src, 0, 0);
	omap_set_dma_dest_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				 dma_dst, 0, 0);

	INIT_COMPLETION(c->dma_done);
	omap_start_dma(c->dma_channel);

	timeout = jiffies + msecs_to_jiffies(20);
	done = &c->dma_done.done;
	while (time_before(jiffies, timeout))
		if (*done)
			break;

	dma_unmap_single(&c->pdev->dev, dma_dst, count, DMA_TO_DEVICE);

	if (!*done) {
		dev_err(&c->pdev->dev, "timeout waiting for DMA\n");
		goto out_copy;
	}

	return 0;

out_copy:
	memcpy(this->base + bram_offset, buf, count);
	return 0;
}

#else

int omap3_onenand_read_bufferram(struct mtd_info *mtd, int area,
				 unsigned char *buffer, int offset,
				 size_t count);

int omap3_onenand_write_bufferram(struct mtd_info *mtd, int area,
				  const unsigned char *buffer,
				  int offset, size_t count);

#endif

#if defined(CONFIG_ARCH_OMAP2) || defined(MULTI_OMAP2)

static int omap2_onenand_read_bufferram(struct mtd_info *mtd, int area,
					unsigned char *buffer, int offset,
					size_t count)
{
	struct omap2_onenand *c = container_of(mtd, struct omap2_onenand, mtd);
	struct onenand_chip *this = mtd->priv;
	dma_addr_t dma_src, dma_dst;
	int bram_offset;

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	/* DMA is not used.  Revisit PM requirements before enabling it. */
	if (1 || (c->dma_channel < 0) ||
	    ((void *) buffer >= (void *) high_memory) || (bram_offset & 3) ||
	    (((unsigned int) buffer) & 3) || (count < 1024) || (count & 3)) {
		memcpy(buffer, (__force void *)(this->base + bram_offset),
		       count);
		return 0;
	}

	dma_src = c->phys_base + bram_offset;
	dma_dst = dma_map_single(&c->pdev->dev, buffer, count,
				 DMA_FROM_DEVICE);
	if (dma_mapping_error(&c->pdev->dev, dma_dst)) {
		dev_err(&c->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n",
			count);
		return -1;
	}

	omap_set_dma_transfer_params(c->dma_channel, OMAP_DMA_DATA_TYPE_S32,
				     count / 4, 1, 0, 0, 0);
	omap_set_dma_src_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				dma_src, 0, 0);
	omap_set_dma_dest_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				 dma_dst, 0, 0);

	INIT_COMPLETION(c->dma_done);
	omap_start_dma(c->dma_channel);
	wait_for_completion(&c->dma_done);

	dma_unmap_single(&c->pdev->dev, dma_dst, count, DMA_FROM_DEVICE);

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

	bram_offset = omap2_onenand_bufferram_offset(mtd, area) + area + offset;
	/* DMA is not used.  Revisit PM requirements before enabling it. */
	if (1 || (c->dma_channel < 0) ||
	    ((void *) buffer >= (void *) high_memory) || (bram_offset & 3) ||
	    (((unsigned int) buffer) & 3) || (count < 1024) || (count & 3)) {
		memcpy((__force void *)(this->base + bram_offset), buffer,
		       count);
		return 0;
	}

	dma_src = dma_map_single(&c->pdev->dev, (void *) buffer, count,
				 DMA_TO_DEVICE);
	dma_dst = c->phys_base + bram_offset;
	if (dma_mapping_error(&c->pdev->dev, dma_dst)) {
		dev_err(&c->pdev->dev,
			"Couldn't DMA map a %d byte buffer\n",
			count);
		return -1;
	}

	omap_set_dma_transfer_params(c->dma_channel, OMAP_DMA_DATA_TYPE_S16,
				     count / 2, 1, 0, 0, 0);
	omap_set_dma_src_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				dma_src, 0, 0);
	omap_set_dma_dest_params(c->dma_channel, 0, OMAP_DMA_AMODE_POST_INC,
				 dma_dst, 0, 0);

	INIT_COMPLETION(c->dma_done);
	omap_start_dma(c->dma_channel);
	wait_for_completion(&c->dma_done);

	dma_unmap_single(&c->pdev->dev, dma_dst, count, DMA_TO_DEVICE);

	return 0;
}

#else

int omap2_onenand_read_bufferram(struct mtd_info *mtd, int area,
				 unsigned char *buffer, int offset,
				 size_t count);

int omap2_onenand_write_bufferram(struct mtd_info *mtd, int area,
				  const unsigned char *buffer,
				  int offset, size_t count);

#endif

static struct platform_driver omap2_onenand_driver;

static int __adjust_timing(struct device *dev, void *data)
{
	int ret = 0;
	struct omap2_onenand *c;

	c = dev_get_drvdata(dev);

	BUG_ON(c->setup == NULL);

	/* DMA is not in use so this is all that is needed */
	/* Revisit for OMAP3! */
	ret = c->setup(c->onenand.base, c->freq);

	return ret;
}

int omap2_onenand_rephase(void)
{
	return driver_for_each_device(&omap2_onenand_driver.driver, NULL,
				      NULL, __adjust_timing);
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

static int __devinit omap2_onenand_probe(struct platform_device *pdev)
{
	struct omap_onenand_platform_data *pdata;
	struct omap2_onenand *c;
	int r;

	pdata = pdev->dev.platform_data;
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
	c->dma_channel = pdata->dma_channel;
	if (c->dma_channel < 0) {
		/* if -1, don't use DMA */
		c->gpio_irq = 0;
	}

	r = gpmc_cs_request(c->gpmc_cs, ONENAND_IO_SIZE, &c->phys_base);
	if (r < 0) {
		dev_err(&pdev->dev, "Cannot request GPMC CS\n");
		goto err_kfree;
	}

	if (request_mem_region(c->phys_base, ONENAND_IO_SIZE,
			       pdev->dev.driver->name) == NULL) {
		dev_err(&pdev->dev, "Cannot reserve memory region at 0x%08lx, "
			"size: 0x%x\n",	c->phys_base, ONENAND_IO_SIZE);
		r = -EBUSY;
		goto err_free_cs;
	}
	c->onenand.base = ioremap(c->phys_base, ONENAND_IO_SIZE);
	if (c->onenand.base == NULL) {
		r = -ENOMEM;
		goto err_release_mem_region;
	}

	if (pdata->onenand_setup != NULL) {
		r = pdata->onenand_setup(c->onenand.base, c->freq);
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
	}

	if (c->dma_channel >= 0) {
		r = omap_request_dma(0, pdev->dev.driver->name,
				     omap2_onenand_dma_cb, (void *) c,
				     &c->dma_channel);
		if (r == 0) {
			omap_set_dma_write_mode(c->dma_channel,
						OMAP_DMA_WRITE_NON_POSTED);
			omap_set_dma_src_data_pack(c->dma_channel, 1);
			omap_set_dma_src_burst_mode(c->dma_channel,
						    OMAP_DMA_DATA_BURST_8);
			omap_set_dma_dest_data_pack(c->dma_channel, 1);
			omap_set_dma_dest_burst_mode(c->dma_channel,
						     OMAP_DMA_DATA_BURST_8);
		} else {
			dev_info(&pdev->dev,
				 "failed to allocate DMA for OneNAND, "
				 "using PIO instead\n");
			c->dma_channel = -1;
		}
	}

	dev_info(&pdev->dev, "initializing on CS%d, phys base 0x%08lx, virtual "
		 "base %p\n", c->gpmc_cs, c->phys_base,
		 c->onenand.base);

	c->pdev = pdev;
	c->mtd.name = dev_name(&pdev->dev);
	c->mtd.priv = &c->onenand;
	c->mtd.owner = THIS_MODULE;

	c->mtd.dev.parent = &pdev->dev;

	if (c->dma_channel >= 0) {
		struct onenand_chip *this = &c->onenand;

		this->wait = omap2_onenand_wait;
		if (cpu_is_omap34xx()) {
			this->read_bufferram = omap3_onenand_read_bufferram;
			this->write_bufferram = omap3_onenand_write_bufferram;
		} else {
			this->read_bufferram = omap2_onenand_read_bufferram;
			this->write_bufferram = omap2_onenand_write_bufferram;
		}
	}

	if ((r = onenand_scan(&c->mtd, 1)) < 0)
		goto err_release_dma;

	switch ((c->onenand.version_id >> 4) & 0xf) {
	case 0:
		c->freq = 40;
		break;
	case 1:
		c->freq = 54;
		break;
	case 2:
		c->freq = 66;
		break;
	case 3:
		c->freq = 83;
		break;
	}

#ifdef CONFIG_MTD_PARTITIONS
	if (pdata->parts != NULL)
		r = add_mtd_partitions(&c->mtd, pdata->parts,
				       pdata->nr_parts);
	else
#endif
		r = add_mtd_device(&c->mtd);
	if (r < 0)
		goto err_release_onenand;

	platform_set_drvdata(pdev, c);

	return 0;

err_release_onenand:
	onenand_release(&c->mtd);
err_release_dma:
	if (c->dma_channel != -1)
		omap_free_dma(c->dma_channel);
	if (c->gpio_irq)
		free_irq(gpio_to_irq(c->gpio_irq), c);
err_release_gpio:
	if (c->gpio_irq)
		gpio_free(c->gpio_irq);
err_iounmap:
	iounmap(c->onenand.base);
err_release_mem_region:
	release_mem_region(c->phys_base, ONENAND_IO_SIZE);
err_free_cs:
	gpmc_cs_free(c->gpmc_cs);
err_kfree:
	kfree(c);

	return r;
}

static int __devexit omap2_onenand_remove(struct platform_device *pdev)
{
	struct omap2_onenand *c = dev_get_drvdata(&pdev->dev);

	BUG_ON(c == NULL);

#ifdef CONFIG_MTD_PARTITIONS
	if (c->parts)
		del_mtd_partitions(&c->mtd);
	else
		del_mtd_device(&c->mtd);
#else
	del_mtd_device(&c->mtd);
#endif

	onenand_release(&c->mtd);
	if (c->dma_channel != -1)
		omap_free_dma(c->dma_channel);
	omap2_onenand_shutdown(pdev);
	platform_set_drvdata(pdev, NULL);
	if (c->gpio_irq) {
		free_irq(gpio_to_irq(c->gpio_irq), c);
		gpio_free(c->gpio_irq);
	}
	iounmap(c->onenand.base);
	release_mem_region(c->phys_base, ONENAND_IO_SIZE);
	gpmc_cs_free(c->gpmc_cs);
	kfree(c);

	return 0;
}

static struct platform_driver omap2_onenand_driver = {
	.probe		= omap2_onenand_probe,
	.remove		= __devexit_p(omap2_onenand_remove),
	.shutdown	= omap2_onenand_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner  = THIS_MODULE,
	},
};

static int __init omap2_onenand_init(void)
{
	printk(KERN_INFO "OneNAND driver initializing\n");
	return platform_driver_register(&omap2_onenand_driver);
}

static void __exit omap2_onenand_exit(void)
{
	platform_driver_unregister(&omap2_onenand_driver);
}

module_init(omap2_onenand_init);
module_exit(omap2_onenand_exit);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarkko Lavinen <jarkko.lavinen@nokia.com>");
MODULE_DESCRIPTION("Glue layer for OneNAND flash on OMAP2 / OMAP3");
