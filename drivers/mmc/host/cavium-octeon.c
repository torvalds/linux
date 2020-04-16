/*
 * Driver for MMC and SSD cards for Cavium OCTEON SOCs.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012-2017 Cavium Inc.
 */
#include <linux/dma-mapping.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <asm/octeon/octeon.h>
#include "cavium.h"

#define CVMX_MIO_BOOT_CTL CVMX_ADD_IO_SEG(0x00011800000000D0ull)

/*
 * The l2c* functions below are used for the EMMC-17978 workaround.
 *
 * Due to a bug in the design of the MMC bus hardware, the 2nd to last
 * cache block of a DMA read must be locked into the L2 Cache.
 * Otherwise, data corruption may occur.
 */
static inline void *phys_to_ptr(u64 address)
{
	return (void *)(address | (1ull << 63)); /* XKPHYS */
}

/*
 * Lock a single line into L2. The line is zeroed before locking
 * to make sure no dram accesses are made.
 */
static void l2c_lock_line(u64 addr)
{
	char *addr_ptr = phys_to_ptr(addr);

	asm volatile (
		"cache 31, %[line]"	/* Unlock the line */
		::[line] "m" (*addr_ptr));
}

/* Unlock a single line in the L2 cache. */
static void l2c_unlock_line(u64 addr)
{
	char *addr_ptr = phys_to_ptr(addr);

	asm volatile (
		"cache 23, %[line]"	/* Unlock the line */
		::[line] "m" (*addr_ptr));
}

/* Locks a memory region in the L2 cache. */
static void l2c_lock_mem_region(u64 start, u64 len)
{
	u64 end;

	/* Round start/end to cache line boundaries */
	end = ALIGN(start + len - 1, CVMX_CACHE_LINE_SIZE);
	start = ALIGN(start, CVMX_CACHE_LINE_SIZE);

	while (start <= end) {
		l2c_lock_line(start);
		start += CVMX_CACHE_LINE_SIZE;
	}
	asm volatile("sync");
}

/* Unlock a memory region in the L2 cache. */
static void l2c_unlock_mem_region(u64 start, u64 len)
{
	u64 end;

	/* Round start/end to cache line boundaries */
	end = ALIGN(start + len - 1, CVMX_CACHE_LINE_SIZE);
	start = ALIGN(start, CVMX_CACHE_LINE_SIZE);

	while (start <= end) {
		l2c_unlock_line(start);
		start += CVMX_CACHE_LINE_SIZE;
	}
}

static void octeon_mmc_acquire_bus(struct cvm_mmc_host *host)
{
	if (!host->has_ciu3) {
		down(&octeon_bootbus_sem);
		/* For CN70XX, switch the MMC controller onto the bus. */
		if (OCTEON_IS_MODEL(OCTEON_CN70XX))
			writeq(0, (void __iomem *)CVMX_MIO_BOOT_CTL);
	} else {
		down(&host->mmc_serializer);
	}
}

static void octeon_mmc_release_bus(struct cvm_mmc_host *host)
{
	if (!host->has_ciu3)
		up(&octeon_bootbus_sem);
	else
		up(&host->mmc_serializer);
}

static void octeon_mmc_int_enable(struct cvm_mmc_host *host, u64 val)
{
	writeq(val, host->base + MIO_EMM_INT(host));
	if (!host->has_ciu3)
		writeq(val, host->base + MIO_EMM_INT_EN(host));
}

static void octeon_mmc_set_shared_power(struct cvm_mmc_host *host, int dir)
{
	if (dir == 0)
		if (!atomic_dec_return(&host->shared_power_users))
			gpiod_set_value_cansleep(host->global_pwr_gpiod, 0);
	if (dir == 1)
		if (atomic_inc_return(&host->shared_power_users) == 1)
			gpiod_set_value_cansleep(host->global_pwr_gpiod, 1);
}

static void octeon_mmc_dmar_fixup(struct cvm_mmc_host *host,
				  struct mmc_command *cmd,
				  struct mmc_data *data,
				  u64 addr)
{
	if (cmd->opcode != MMC_WRITE_MULTIPLE_BLOCK)
		return;
	if (data->blksz * data->blocks <= 1024)
		return;

	host->n_minus_one = addr + (data->blksz * data->blocks) - 1024;
	l2c_lock_mem_region(host->n_minus_one, 512);
}

static void octeon_mmc_dmar_fixup_done(struct cvm_mmc_host *host)
{
	if (!host->n_minus_one)
		return;
	l2c_unlock_mem_region(host->n_minus_one, 512);
	host->n_minus_one = 0;
}

static int octeon_mmc_probe(struct platform_device *pdev)
{
	struct device_node *cn, *node = pdev->dev.of_node;
	struct cvm_mmc_host *host;
	void __iomem *base;
	int mmc_irq[9];
	int i, ret = 0;
	u64 val;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	spin_lock_init(&host->irq_handler_lock);
	sema_init(&host->mmc_serializer, 1);

	host->dev = &pdev->dev;
	host->acquire_bus = octeon_mmc_acquire_bus;
	host->release_bus = octeon_mmc_release_bus;
	host->int_enable = octeon_mmc_int_enable;
	host->set_shared_power = octeon_mmc_set_shared_power;
	if (OCTEON_IS_MODEL(OCTEON_CN6XXX) ||
	    OCTEON_IS_MODEL(OCTEON_CNF7XXX)) {
		host->dmar_fixup = octeon_mmc_dmar_fixup;
		host->dmar_fixup_done = octeon_mmc_dmar_fixup_done;
	}

	host->sys_freq = octeon_get_io_clock_rate();

	if (of_device_is_compatible(node, "cavium,octeon-7890-mmc")) {
		host->big_dma_addr = true;
		host->need_irq_handler_lock = true;
		host->has_ciu3 = true;
		host->use_sg = true;
		/*
		 * First seven are the EMM_INT bits 0..6, then two for
		 * the EMM_DMA_INT bits
		 */
		for (i = 0; i < 9; i++) {
			mmc_irq[i] = platform_get_irq(pdev, i);
			if (mmc_irq[i] < 0)
				return mmc_irq[i];

			/* work around legacy u-boot device trees */
			irq_set_irq_type(mmc_irq[i], IRQ_TYPE_EDGE_RISING);
		}
	} else {
		host->big_dma_addr = false;
		host->need_irq_handler_lock = false;
		host->has_ciu3 = false;
		/* First one is EMM second DMA */
		for (i = 0; i < 2; i++) {
			mmc_irq[i] = platform_get_irq(pdev, i);
			if (mmc_irq[i] < 0)
				return mmc_irq[i];
		}
	}

	host->last_slot = -1;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);
	host->base = base;
	host->reg_off = 0;

	base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(base))
		return PTR_ERR(base);
	host->dma_base = base;
	/*
	 * To keep the register addresses shared we intentionaly use
	 * a negative offset here, first register used on Octeon therefore
	 * starts at 0x20 (MIO_EMM_DMA_CFG).
	 */
	host->reg_off_dma = -0x20;

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	/*
	 * Clear out any pending interrupts that may be left over from
	 * bootloader.
	 */
	val = readq(host->base + MIO_EMM_INT(host));
	writeq(val, host->base + MIO_EMM_INT(host));

	if (host->has_ciu3) {
		/* Only CMD_DONE, DMA_DONE, CMD_ERR, DMA_ERR */
		for (i = 1; i <= 4; i++) {
			ret = devm_request_irq(&pdev->dev, mmc_irq[i],
					       cvm_mmc_interrupt,
					       0, cvm_mmc_irq_names[i], host);
			if (ret < 0) {
				dev_err(&pdev->dev, "Error: devm_request_irq %d\n",
					mmc_irq[i]);
				return ret;
			}
		}
	} else {
		ret = devm_request_irq(&pdev->dev, mmc_irq[0],
				       cvm_mmc_interrupt, 0, KBUILD_MODNAME,
				       host);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error: devm_request_irq %d\n",
				mmc_irq[0]);
			return ret;
		}
	}

	host->global_pwr_gpiod = devm_gpiod_get_optional(&pdev->dev,
							 "power",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(host->global_pwr_gpiod)) {
		dev_err(&pdev->dev, "Invalid power GPIO\n");
		return PTR_ERR(host->global_pwr_gpiod);
	}

	platform_set_drvdata(pdev, host);

	i = 0;
	for_each_child_of_node(node, cn) {
		host->slot_pdev[i] =
			of_platform_device_create(cn, NULL, &pdev->dev);
		if (!host->slot_pdev[i]) {
			i++;
			continue;
		}
		ret = cvm_mmc_of_slot_probe(&host->slot_pdev[i]->dev, host);
		if (ret) {
			dev_err(&pdev->dev, "Error populating slots\n");
			octeon_mmc_set_shared_power(host, 0);
			goto error;
		}
		i++;
	}
	return 0;

error:
	for (i = 0; i < CAVIUM_MAX_MMC; i++) {
		if (host->slot[i])
			cvm_mmc_of_slot_remove(host->slot[i]);
		if (host->slot_pdev[i])
			of_platform_device_destroy(&host->slot_pdev[i]->dev, NULL);
	}
	return ret;
}

static int octeon_mmc_remove(struct platform_device *pdev)
{
	struct cvm_mmc_host *host = platform_get_drvdata(pdev);
	u64 dma_cfg;
	int i;

	for (i = 0; i < CAVIUM_MAX_MMC; i++)
		if (host->slot[i])
			cvm_mmc_of_slot_remove(host->slot[i]);

	dma_cfg = readq(host->dma_base + MIO_EMM_DMA_CFG(host));
	dma_cfg &= ~MIO_EMM_DMA_CFG_EN;
	writeq(dma_cfg, host->dma_base + MIO_EMM_DMA_CFG(host));

	octeon_mmc_set_shared_power(host, 0);
	return 0;
}

static const struct of_device_id octeon_mmc_match[] = {
	{
		.compatible = "cavium,octeon-6130-mmc",
	},
	{
		.compatible = "cavium,octeon-7890-mmc",
	},
	{},
};
MODULE_DEVICE_TABLE(of, octeon_mmc_match);

static struct platform_driver octeon_mmc_driver = {
	.probe		= octeon_mmc_probe,
	.remove		= octeon_mmc_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table = octeon_mmc_match,
	},
};

module_platform_driver(octeon_mmc_driver);

MODULE_AUTHOR("Cavium Inc. <support@cavium.com>");
MODULE_DESCRIPTION("Low-level driver for Cavium OCTEON MMC/SSD card");
MODULE_LICENSE("GPL");
