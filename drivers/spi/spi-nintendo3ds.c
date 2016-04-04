/*
 *  spi-nintendo3ds.c
 *
 *  Copyright (C) 2016 Sergi Granell (xerpi)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#define NINTENDO3DS_SPI_NAME "nintendo3ds_spi"

struct nintendo3ds_spi {
	struct spi_master	*master;
	void __iomem		*base_addr;
	u32			current_cs;
};

/* SPI registers */

#define SPI_REG_NEW_CNT_OFFSET		0x800
#define SPI_REG_NEW_DONE_OFFSET		0x804
#define SPI_REG_NEW_BLKLEN_OFFSET	0x808
#define SPI_REG_NEW_FIFO_OFFSET		0x80C
#define SPI_REG_NEW_STATUS_OFFSET	0x810

#define SPI_NEW_CNT_SELECT_DEVICE(n)	(n << 6)
#define SPI_NEW_CNT_TRANSFER_IN		(0 << 13)
#define SPI_NEW_CNT_TRANSFER_OUT	(1 << 13)
#define SPI_NEW_CNT_BUSY		BIT(15)
#define SPI_NEW_CNT_ENABLE		BIT(15)

#define SPI_NEW_STATUS_FIFO_BUSY	BIT(0)

static inline u32 spi_reg_new_cnt_read(void __iomem *base)
{
	return readl(base + SPI_REG_NEW_CNT_OFFSET);
}
static inline void spi_reg_new_cnt_write(void __iomem *base, u32 val)
{
	writel(val, base + SPI_REG_NEW_CNT_OFFSET);
}

static inline u32 spi_reg_new_done_read(void __iomem *base)
{
	return readl(base + SPI_REG_NEW_DONE_OFFSET);
}
static inline void spi_reg_new_done_write(void __iomem *base, u32 val)
{
	writel(val, base + SPI_REG_NEW_DONE_OFFSET);
}

static inline u32 spi_reg_new_blklen_read(void __iomem *base)
{
	return readl(base + SPI_REG_NEW_BLKLEN_OFFSET);
}
static inline void spi_reg_new_blklen_write(void __iomem *base, u32 val)
{
	writel(val, base + SPI_REG_NEW_BLKLEN_OFFSET);
}

static inline u32 spi_reg_new_fifo_read(void __iomem *base)
{
	return readl(base + SPI_REG_NEW_FIFO_OFFSET);
}
static inline void spi_reg_new_fifo_write(void __iomem *base, u32 val)
{
	writel(val, base + SPI_REG_NEW_FIFO_OFFSET);
}

static inline u32 spi_reg_new_status_read(void __iomem *base)
{
	return readl(base + SPI_REG_NEW_STATUS_OFFSET);
}
static inline void spi_reg_new_status_write(void __iomem *base, u32 val)
{
	writel(val, base + SPI_REG_NEW_STATUS_OFFSET);
}

static inline void spi_wait_new_cnt_busy(void __iomem *base)
{
	while (spi_reg_new_cnt_read(base) & SPI_NEW_CNT_BUSY)
		;
}

static inline void spi_wait_new_fifo_busy(void __iomem *base)
{
	while (spi_reg_new_status_read(base) & SPI_NEW_STATUS_FIFO_BUSY)
		;
}

static u8 spi_get_device_bits(u8 device_id)
{
	if (device_id < 6) {
		if (device_id == 0 || device_id == 3) {
			return 0;
		} else if (device_id == 1 || device_id == 4) {
			return 0x40;
		} else if (device_id == 2 || device_id == 5) {
			return 0x80;
		}
	}
	return 0;
}

static u8 spi_get_baudrate_for_freq(u32 freq)
{
	switch (freq) {
	case 4000000:
		return 5;
	case 2000000:
		return 4;
	case 1000000:
		return 3;
	case 512000:
		return 2;
	case 256000:
		return 1;
	case 128000:
	default:
		return 0;
	}
}

/* Not needed for now
static u32 spi_get_ns_delay_for_baudrate(u8 baudrate)
{
	switch (baudrate) {
	default:
	case 0:
		return 0x83400;
	case 1:
		return 0x41A00;
	case 2:
		return 0x20D00;
	case 3:
		return 0x10680;
	case 4:
		return 0x8340;
	case 5:
		return 0x41A0;
	}
}*/

struct nintendo3ds_spi_msg {
	u32 baudrate;
	void *buffer;
	u32 size;
	u8  device;
};

static void nintendo3ds_spi_write_msg(void __iomem *base, const struct nintendo3ds_spi_msg *msg)
{
	u32 device_bits;
	u32 count;
	u32 buffer_idx;

	device_bits = spi_get_device_bits(msg->device);

	spi_wait_new_cnt_busy(base);

	spi_reg_new_blklen_write(base, msg->size);
	spi_reg_new_cnt_write(base, msg->baudrate | device_bits
		| SPI_NEW_CNT_ENABLE | SPI_NEW_CNT_TRANSFER_OUT);

	if (msg->size > 0) {
		count = 0;
		do {
			if ((count & 0x1F) == 0) {
				spi_wait_new_fifo_busy(base);
			}
			buffer_idx = count & ~0b11;
			count = count + 4;
			spi_reg_new_fifo_write(base, *(u32 *)
				(msg->buffer + buffer_idx));
		} while (count < msg->size);
	}

	spi_wait_new_cnt_busy(base);
}

static void nintendo3ds_spi_read_msg(void __iomem *base, struct nintendo3ds_spi_msg *msg)
{
	u32 device_bits;
	u32 count;
	u32 buffer_idx;

	device_bits = spi_get_device_bits(msg->device);

	spi_wait_new_cnt_busy(base);

	spi_reg_new_blklen_write(base, msg->size);
	spi_reg_new_cnt_write(base, msg->baudrate | device_bits
		| SPI_NEW_CNT_ENABLE | SPI_NEW_CNT_TRANSFER_IN);

	if (msg->size > 0) {
		count = 0;
		do {
			if ((count & 0x1F) == 0) {
				spi_wait_new_fifo_busy(base);
			}
			buffer_idx = count & ~0b11;
			count = count + 4;
			*(u32 *)(msg->buffer + buffer_idx) =
				spi_reg_new_fifo_read(base);
		} while (count < msg->size);
	}

	spi_wait_new_cnt_busy(base);
}

static inline void nintendo3ds_spi_msg_done(void __iomem *base)
{
	spi_reg_new_done_write(base, 0);
}

static void nintendo3ds_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct nintendo3ds_spi *n3ds_spi = spi_master_get_devdata(spi->master);

	n3ds_spi->current_cs = spi->chip_select;
}

static int nintendo3ds_spi_transfer_one(struct spi_master *master,
				  struct spi_device *spi,
				  struct spi_transfer *tfr)
{

	struct nintendo3ds_spi_msg n3ds_msg;
	struct nintendo3ds_spi *n3ds_spi = spi_master_get_devdata(master);

	n3ds_msg.size = tfr->len;
	n3ds_msg.baudrate = spi_get_baudrate_for_freq(spi->max_speed_hz);
	n3ds_msg.device = n3ds_spi->current_cs;

	if (tfr->tx_buf) {
		n3ds_msg.buffer = (void *)tfr->tx_buf;
		nintendo3ds_spi_write_msg(n3ds_spi->base_addr,
			&n3ds_msg);
	} else if (tfr->rx_buf) {
		n3ds_msg.buffer = tfr->rx_buf;
		nintendo3ds_spi_read_msg(n3ds_spi->base_addr,
			&n3ds_msg);
	} else {
		dev_err(&spi->dev, "%s: null SPI transfer\n", __func__);
		return -EINVAL;
	}

	if (spi_transfer_is_last(master, tfr)) {
		nintendo3ds_spi_msg_done(n3ds_spi->base_addr);
	}

	spi_finalize_current_transfer(master);

	return 0;
}

static int nintendo3ds_spi_probe(struct platform_device *pdev)
{
	struct nintendo3ds_spi *n3ds_spi;
	struct resource *mem;
	struct spi_master *master;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(struct nintendo3ds_spi));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);
	n3ds_spi = spi_master_get_devdata(master);

	n3ds_spi->master = master;
	master->bus_num = pdev->id;
	master->set_cs = nintendo3ds_spi_set_cs;
	master->transfer_one = nintendo3ds_spi_transfer_one;
	master->num_chipselect = 6;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->flags = SPI_MASTER_HALF_DUPLEX;
	master->dev.of_node = pdev->dev.of_node;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err;
	}

	n3ds_spi->base_addr = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(n3ds_spi->base_addr)) {
		ret = PTR_ERR(n3ds_spi->base_addr);
		goto err;
	}

	pr_info(NINTENDO3DS_SPI_NAME " %s registered, mapped to: %p\n",
		pdev->name, n3ds_spi->base_addr);

	/* Stop any possible running transfer */
	spi_reg_new_cnt_write(n3ds_spi->base_addr, 0);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret) {
		dev_err(&pdev->dev, "cannot register SPI master\n");
		goto err;
	}

	return 0;
err:
	spi_master_put(master);
	return ret;
}

static int nintendo3ds_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	/*struct nintendo3ds_spi *n3ds_spi = spi_master_get_devdata(master);*/

	spi_master_put(master);

	return 0;
}


MODULE_ALIAS("platform:" NINTENDO3DS_SPI_NAME);

#ifdef CONFIG_OF
static const struct of_device_id nintendo3ds_spi_of_match[] = {
	{ .compatible = "nintendo3ds,nintendo3ds-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, nintendo3ds_spi_of_match);
#endif

static struct platform_driver nintendo3ds_spi_driver = {
	.probe = nintendo3ds_spi_probe,
	.remove = nintendo3ds_spi_remove,
	.driver = {
		.name = NINTENDO3DS_SPI_NAME,
		.of_match_table = of_match_ptr(nintendo3ds_spi_of_match),
	},
};
module_platform_driver(nintendo3ds_spi_driver);

MODULE_AUTHOR("Sergi Granell <xerpi.g.12@gmail.com>");
MODULE_DESCRIPTION("Nintendo 3DS SPI driver");
MODULE_LICENSE("GPL");
