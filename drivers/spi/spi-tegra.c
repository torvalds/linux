/*
 * Driver for Nvidia TEGRA spi controller.
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *     Erik Gilling <konkers@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/spi/spi.h>

#include <mach/dma.h>

#define SLINK_COMMAND		0x000
#define   SLINK_BIT_LENGTH(x)		(((x) & 0x1f) << 0)
#define   SLINK_WORD_SIZE(x)		(((x) & 0x1f) << 5)
#define   SLINK_BOTH_EN			(1 << 10)
#define   SLINK_CS_SW			(1 << 11)
#define   SLINK_CS_VALUE		(1 << 12)
#define   SLINK_CS_POLARITY		(1 << 13)
#define   SLINK_IDLE_SDA_DRIVE_LOW	(0 << 16)
#define   SLINK_IDLE_SDA_DRIVE_HIGH	(1 << 16)
#define   SLINK_IDLE_SDA_PULL_LOW	(2 << 16)
#define   SLINK_IDLE_SDA_PULL_HIGH	(3 << 16)
#define   SLINK_IDLE_SDA_MASK		(3 << 16)
#define   SLINK_CS_POLARITY1		(1 << 20)
#define   SLINK_CK_SDA			(1 << 21)
#define   SLINK_CS_POLARITY2		(1 << 22)
#define   SLINK_CS_POLARITY3		(1 << 23)
#define   SLINK_IDLE_SCLK_DRIVE_LOW	(0 << 24)
#define   SLINK_IDLE_SCLK_DRIVE_HIGH	(1 << 24)
#define   SLINK_IDLE_SCLK_PULL_LOW	(2 << 24)
#define   SLINK_IDLE_SCLK_PULL_HIGH	(3 << 24)
#define   SLINK_IDLE_SCLK_MASK		(3 << 24)
#define   SLINK_M_S			(1 << 28)
#define   SLINK_WAIT			(1 << 29)
#define   SLINK_GO			(1 << 30)
#define   SLINK_ENB			(1 << 31)

#define SLINK_COMMAND2		0x004
#define   SLINK_LSBFE			(1 << 0)
#define   SLINK_SSOE			(1 << 1)
#define   SLINK_SPIE			(1 << 4)
#define   SLINK_BIDIROE			(1 << 6)
#define   SLINK_MODFEN			(1 << 7)
#define   SLINK_INT_SIZE(x)		(((x) & 0x1f) << 8)
#define   SLINK_CS_ACTIVE_BETWEEN	(1 << 17)
#define   SLINK_SS_EN_CS(x)		(((x) & 0x3) << 18)
#define   SLINK_SS_SETUP(x)		(((x) & 0x3) << 20)
#define   SLINK_FIFO_REFILLS_0		(0 << 22)
#define   SLINK_FIFO_REFILLS_1		(1 << 22)
#define   SLINK_FIFO_REFILLS_2		(2 << 22)
#define   SLINK_FIFO_REFILLS_3		(3 << 22)
#define   SLINK_FIFO_REFILLS_MASK	(3 << 22)
#define   SLINK_WAIT_PACK_INT(x)	(((x) & 0x7) << 26)
#define   SLINK_SPC0			(1 << 29)
#define   SLINK_TXEN			(1 << 30)
#define   SLINK_RXEN			(1 << 31)

#define SLINK_STATUS		0x008
#define   SLINK_COUNT(val)		(((val) >> 0) & 0x1f)
#define   SLINK_WORD(val)		(((val) >> 5) & 0x1f)
#define   SLINK_BLK_CNT(val)		(((val) >> 0) & 0xffff)
#define   SLINK_MODF			(1 << 16)
#define   SLINK_RX_UNF			(1 << 18)
#define   SLINK_TX_OVF			(1 << 19)
#define   SLINK_TX_FULL			(1 << 20)
#define   SLINK_TX_EMPTY		(1 << 21)
#define   SLINK_RX_FULL			(1 << 22)
#define   SLINK_RX_EMPTY		(1 << 23)
#define   SLINK_TX_UNF			(1 << 24)
#define   SLINK_RX_OVF			(1 << 25)
#define   SLINK_TX_FLUSH		(1 << 26)
#define   SLINK_RX_FLUSH		(1 << 27)
#define   SLINK_SCLK			(1 << 28)
#define   SLINK_ERR			(1 << 29)
#define   SLINK_RDY			(1 << 30)
#define   SLINK_BSY			(1 << 31)

#define SLINK_MAS_DATA		0x010
#define SLINK_SLAVE_DATA	0x014

#define SLINK_DMA_CTL		0x018
#define   SLINK_DMA_BLOCK_SIZE(x)	(((x) & 0xffff) << 0)
#define   SLINK_TX_TRIG_1		(0 << 16)
#define   SLINK_TX_TRIG_4		(1 << 16)
#define   SLINK_TX_TRIG_8		(2 << 16)
#define   SLINK_TX_TRIG_16		(3 << 16)
#define   SLINK_TX_TRIG_MASK		(3 << 16)
#define   SLINK_RX_TRIG_1		(0 << 18)
#define   SLINK_RX_TRIG_4		(1 << 18)
#define   SLINK_RX_TRIG_8		(2 << 18)
#define   SLINK_RX_TRIG_16		(3 << 18)
#define   SLINK_RX_TRIG_MASK		(3 << 18)
#define   SLINK_PACKED			(1 << 20)
#define   SLINK_PACK_SIZE_4		(0 << 21)
#define   SLINK_PACK_SIZE_8		(1 << 21)
#define   SLINK_PACK_SIZE_16		(2 << 21)
#define   SLINK_PACK_SIZE_32		(3 << 21)
#define   SLINK_PACK_SIZE_MASK		(3 << 21)
#define   SLINK_IE_TXC			(1 << 26)
#define   SLINK_IE_RXC			(1 << 27)
#define   SLINK_DMA_EN			(1 << 31)

#define SLINK_STATUS2		0x01c
#define   SLINK_TX_FIFO_EMPTY_COUNT(val)	(((val) & 0x3f) >> 0)
#define   SLINK_RX_FIFO_FULL_COUNT(val)		(((val) & 0x3f) >> 16)

#define SLINK_TX_FIFO		0x100
#define SLINK_RX_FIFO		0x180

static const unsigned long spi_tegra_req_sels[] = {
	TEGRA_DMA_REQ_SEL_SL2B1,
	TEGRA_DMA_REQ_SEL_SL2B2,
	TEGRA_DMA_REQ_SEL_SL2B3,
	TEGRA_DMA_REQ_SEL_SL2B4,
};

#define BB_LEN			32

struct spi_tegra_data {
	struct spi_master	*master;
	struct platform_device	*pdev;
	spinlock_t		lock;

	struct clk		*clk;
	void __iomem		*base;
	unsigned long		phys;

	u32			cur_speed;

	struct list_head	queue;
	struct spi_transfer	*cur;
	unsigned		cur_pos;
	unsigned		cur_len;
	unsigned		cur_bytes_per_word;

	/* The tegra spi controller has a bug which causes the first word
	 * in PIO transactions to be garbage.  Since packed DMA transactions
	 * require transfers to be 4 byte aligned we need a bounce buffer
	 * for the generic case.
	 */
	struct tegra_dma_req	rx_dma_req;
	struct tegra_dma_channel *rx_dma;
	u32			*rx_bb;
	dma_addr_t		rx_bb_phys;
};


static inline unsigned long spi_tegra_readl(struct spi_tegra_data *tspi,
					    unsigned long reg)
{
	return readl(tspi->base + reg);
}

static inline void spi_tegra_writel(struct spi_tegra_data *tspi,
				    unsigned long val,
				    unsigned long reg)
{
	writel(val, tspi->base + reg);
}

static void spi_tegra_go(struct spi_tegra_data *tspi)
{
	unsigned long val;

	wmb();

	val = spi_tegra_readl(tspi, SLINK_DMA_CTL);
	val &= ~SLINK_DMA_BLOCK_SIZE(~0) & ~SLINK_DMA_EN;
	val |= SLINK_DMA_BLOCK_SIZE(tspi->rx_dma_req.size / 4 - 1);
	spi_tegra_writel(tspi, val, SLINK_DMA_CTL);

	tegra_dma_enqueue_req(tspi->rx_dma, &tspi->rx_dma_req);

	val |= SLINK_DMA_EN;
	spi_tegra_writel(tspi, val, SLINK_DMA_CTL);
}

static unsigned spi_tegra_fill_tx_fifo(struct spi_tegra_data *tspi,
				  struct spi_transfer *t)
{
	unsigned len = min(t->len - tspi->cur_pos, BB_LEN *
			   tspi->cur_bytes_per_word);
	u8 *tx_buf = (u8 *)t->tx_buf + tspi->cur_pos;
	int i, j;
	unsigned long val;

	val = spi_tegra_readl(tspi, SLINK_COMMAND);
	val &= ~SLINK_WORD_SIZE(~0);
	val |= SLINK_WORD_SIZE(len / tspi->cur_bytes_per_word - 1);
	spi_tegra_writel(tspi, val, SLINK_COMMAND);

	for (i = 0; i < len; i += tspi->cur_bytes_per_word) {
		val = 0;
		for (j = 0; j < tspi->cur_bytes_per_word; j++)
			val |= tx_buf[i + j] << j * 8;

		spi_tegra_writel(tspi, val, SLINK_TX_FIFO);
	}

	tspi->rx_dma_req.size = len / tspi->cur_bytes_per_word * 4;

	return len;
}

static unsigned spi_tegra_drain_rx_fifo(struct spi_tegra_data *tspi,
				  struct spi_transfer *t)
{
	unsigned len = tspi->cur_len;
	u8 *rx_buf = (u8 *)t->rx_buf + tspi->cur_pos;
	int i, j;
	unsigned long val;

	for (i = 0; i < len; i += tspi->cur_bytes_per_word) {
		val = tspi->rx_bb[i / tspi->cur_bytes_per_word];
		for (j = 0; j < tspi->cur_bytes_per_word; j++)
			rx_buf[i + j] = (val >> (j * 8)) & 0xff;
	}

	return len;
}

static void spi_tegra_start_transfer(struct spi_device *spi,
				    struct spi_transfer *t)
{
	struct spi_tegra_data *tspi = spi_master_get_devdata(spi->master);
	u32 speed;
	u8 bits_per_word;
	unsigned long val;

	speed = t->speed_hz ? t->speed_hz : spi->max_speed_hz;
	bits_per_word = t->bits_per_word ? t->bits_per_word  :
		spi->bits_per_word;

	tspi->cur_bytes_per_word = (bits_per_word - 1) / 8 + 1;

	if (speed != tspi->cur_speed)
		clk_set_rate(tspi->clk, speed);

	if (tspi->cur_speed == 0)
		clk_enable(tspi->clk);

	tspi->cur_speed = speed;

	val = spi_tegra_readl(tspi, SLINK_COMMAND2);
	val &= ~SLINK_SS_EN_CS(~0) | SLINK_RXEN | SLINK_TXEN;
	if (t->rx_buf)
		val |= SLINK_RXEN;
	if (t->tx_buf)
		val |= SLINK_TXEN;
	val |= SLINK_SS_EN_CS(spi->chip_select);
	val |= SLINK_SPIE;
	spi_tegra_writel(tspi, val, SLINK_COMMAND2);

	val = spi_tegra_readl(tspi, SLINK_COMMAND);
	val &= ~SLINK_BIT_LENGTH(~0);
	val |= SLINK_BIT_LENGTH(bits_per_word - 1);

	/* FIXME: should probably control CS manually so that we can be sure
	 * it does not go low between transfer and to support delay_usecs
	 * correctly.
	 */
	val &= ~SLINK_IDLE_SCLK_MASK & ~SLINK_CK_SDA & ~SLINK_CS_SW;

	if (spi->mode & SPI_CPHA)
		val |= SLINK_CK_SDA;

	if (spi->mode & SPI_CPOL)
		val |= SLINK_IDLE_SCLK_DRIVE_HIGH;
	else
		val |= SLINK_IDLE_SCLK_DRIVE_LOW;

	val |= SLINK_M_S;

	spi_tegra_writel(tspi, val, SLINK_COMMAND);

	spi_tegra_writel(tspi, SLINK_RX_FLUSH | SLINK_TX_FLUSH, SLINK_STATUS);

	tspi->cur = t;
	tspi->cur_pos = 0;
	tspi->cur_len = spi_tegra_fill_tx_fifo(tspi, t);

	spi_tegra_go(tspi);
}

static void spi_tegra_start_message(struct spi_device *spi,
				    struct spi_message *m)
{
	struct spi_transfer *t;

	m->actual_length = 0;
	m->status = 0;

	t = list_first_entry(&m->transfers, struct spi_transfer, transfer_list);
	spi_tegra_start_transfer(spi, t);
}

static void tegra_spi_rx_dma_complete(struct tegra_dma_req *req)
{
	struct spi_tegra_data *tspi = req->dev;
	unsigned long flags;
	struct spi_message *m;
	struct spi_device *spi;
	int timeout = 0;
	unsigned long val;

	/* the SPI controller may come back with both the BSY and RDY bits
	 * set.  In this case we need to wait for the BSY bit to clear so
	 * that we are sure the DMA is finished.  1000 reads was empirically
	 * determined to be long enough.
	 */
	while (timeout++ < 1000) {
		if (!(spi_tegra_readl(tspi, SLINK_STATUS) & SLINK_BSY))
			break;
	}

	spin_lock_irqsave(&tspi->lock, flags);

	val = spi_tegra_readl(tspi, SLINK_STATUS);
	val |= SLINK_RDY;
	spi_tegra_writel(tspi, val, SLINK_STATUS);

	m = list_first_entry(&tspi->queue, struct spi_message, queue);

	if (timeout >= 1000)
		m->status = -EIO;

	spi = m->state;

	tspi->cur_pos += spi_tegra_drain_rx_fifo(tspi, tspi->cur);
	m->actual_length += tspi->cur_pos;

	if (tspi->cur_pos < tspi->cur->len) {
		tspi->cur_len = spi_tegra_fill_tx_fifo(tspi, tspi->cur);
		spi_tegra_go(tspi);
	} else if (!list_is_last(&tspi->cur->transfer_list,
				 &m->transfers)) {
		tspi->cur =  list_first_entry(&tspi->cur->transfer_list,
					      struct spi_transfer,
					      transfer_list);
		spi_tegra_start_transfer(spi, tspi->cur);
	} else {
		list_del(&m->queue);

		m->complete(m->context);

		if (!list_empty(&tspi->queue)) {
			m = list_first_entry(&tspi->queue, struct spi_message,
					     queue);
			spi = m->state;
			spi_tegra_start_message(spi, m);
		} else {
			clk_disable(tspi->clk);
			tspi->cur_speed = 0;
		}
	}

	spin_unlock_irqrestore(&tspi->lock, flags);
}

static int spi_tegra_setup(struct spi_device *spi)
{
	struct spi_tegra_data *tspi = spi_master_get_devdata(spi->master);
	unsigned long cs_bit;
	unsigned long val;
	unsigned long flags;

	dev_dbg(&spi->dev, "setup %d bpw, %scpol, %scpha, %dHz\n",
		spi->bits_per_word,
		spi->mode & SPI_CPOL ? "" : "~",
		spi->mode & SPI_CPHA ? "" : "~",
		spi->max_speed_hz);


	switch (spi->chip_select) {
	case 0:
		cs_bit = SLINK_CS_POLARITY;
		break;

	case 1:
		cs_bit = SLINK_CS_POLARITY1;
		break;

	case 2:
		cs_bit = SLINK_CS_POLARITY2;
		break;

	case 4:
		cs_bit = SLINK_CS_POLARITY3;
		break;

	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&tspi->lock, flags);

	val = spi_tegra_readl(tspi, SLINK_COMMAND);
	if (spi->mode & SPI_CS_HIGH)
		val |= cs_bit;
	else
		val &= ~cs_bit;
	spi_tegra_writel(tspi, val, SLINK_COMMAND);

	spin_unlock_irqrestore(&tspi->lock, flags);

	return 0;
}

static int spi_tegra_transfer(struct spi_device *spi, struct spi_message *m)
{
	struct spi_tegra_data *tspi = spi_master_get_devdata(spi->master);
	struct spi_transfer *t;
	unsigned long flags;
	int was_empty;

	if (list_empty(&m->transfers) || !m->complete)
		return -EINVAL;

	list_for_each_entry(t, &m->transfers, transfer_list) {
		if (t->bits_per_word < 0 || t->bits_per_word > 32)
			return -EINVAL;

		if (t->len == 0)
			return -EINVAL;

		if (!t->rx_buf && !t->tx_buf)
			return -EINVAL;
	}

	m->state = spi;

	spin_lock_irqsave(&tspi->lock, flags);
	was_empty = list_empty(&tspi->queue);
	list_add_tail(&m->queue, &tspi->queue);

	if (was_empty)
		spi_tegra_start_message(spi, m);

	spin_unlock_irqrestore(&tspi->lock, flags);

	return 0;
}

static int __init spi_tegra_probe(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct spi_tegra_data	*tspi;
	struct resource		*r;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof *tspi);
	if (master == NULL) {
		dev_err(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	/* the spi->mode bits understood by this driver: */
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;

	master->bus_num = pdev->id;

	master->setup = spi_tegra_setup;
	master->transfer = spi_tegra_transfer;
	master->num_chipselect = 4;

	dev_set_drvdata(&pdev->dev, master);
	tspi = spi_master_get_devdata(master);
	tspi->master = master;
	tspi->pdev = pdev;
	spin_lock_init(&tspi->lock);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		ret = -ENODEV;
		goto err0;
	}

	if (!request_mem_region(r->start, resource_size(r),
				dev_name(&pdev->dev))) {
		ret = -EBUSY;
		goto err0;
	}

	tspi->phys = r->start;
	tspi->base = ioremap(r->start, resource_size(r));
	if (!tspi->base) {
		dev_err(&pdev->dev, "can't ioremap iomem\n");
		ret = -ENOMEM;
		goto err1;
	}

	tspi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tspi->clk)) {
		dev_err(&pdev->dev, "can not get clock\n");
		ret = PTR_ERR(tspi->clk);
		goto err2;
	}

	INIT_LIST_HEAD(&tspi->queue);

	tspi->rx_dma = tegra_dma_allocate_channel(TEGRA_DMA_MODE_ONESHOT);
	if (!tspi->rx_dma) {
		dev_err(&pdev->dev, "can not allocate rx dma channel\n");
		ret = -ENODEV;
		goto err3;
	}

	tspi->rx_bb = dma_alloc_coherent(&pdev->dev, sizeof(u32) * BB_LEN,
					 &tspi->rx_bb_phys, GFP_KERNEL);
	if (!tspi->rx_bb) {
		dev_err(&pdev->dev, "can not allocate rx bounce buffer\n");
		ret = -ENOMEM;
		goto err4;
	}

	tspi->rx_dma_req.complete = tegra_spi_rx_dma_complete;
	tspi->rx_dma_req.to_memory = 1;
	tspi->rx_dma_req.dest_addr = tspi->rx_bb_phys;
	tspi->rx_dma_req.dest_bus_width = 32;
	tspi->rx_dma_req.source_addr = tspi->phys + SLINK_RX_FIFO;
	tspi->rx_dma_req.source_bus_width = 32;
	tspi->rx_dma_req.source_wrap = 4;
	tspi->rx_dma_req.req_sel = spi_tegra_req_sels[pdev->id];
	tspi->rx_dma_req.dev = tspi;

	master->dev.of_node = pdev->dev.of_node;
	ret = spi_register_master(master);

	if (ret < 0)
		goto err5;

	return ret;

err5:
	dma_free_coherent(&pdev->dev, sizeof(u32) * BB_LEN,
			  tspi->rx_bb, tspi->rx_bb_phys);
err4:
	tegra_dma_free_channel(tspi->rx_dma);
err3:
	clk_put(tspi->clk);
err2:
	iounmap(tspi->base);
err1:
	release_mem_region(r->start, resource_size(r));
err0:
	spi_master_put(master);
	return ret;
}

static int __devexit spi_tegra_remove(struct platform_device *pdev)
{
	struct spi_master	*master;
	struct spi_tegra_data	*tspi;
	struct resource		*r;

	master = dev_get_drvdata(&pdev->dev);
	tspi = spi_master_get_devdata(master);

	spi_unregister_master(master);
	tegra_dma_free_channel(tspi->rx_dma);

	dma_free_coherent(&pdev->dev, sizeof(u32) * BB_LEN,
			  tspi->rx_bb, tspi->rx_bb_phys);

	clk_put(tspi->clk);
	iounmap(tspi->base);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, resource_size(r));

	return 0;
}

MODULE_ALIAS("platform:spi_tegra");

#ifdef CONFIG_OF
static struct of_device_id spi_tegra_of_match_table[] __devinitdata = {
	{ .compatible = "nvidia,tegra20-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, spi_tegra_of_match_table);
#else /* CONFIG_OF */
#define spi_tegra_of_match_table NULL
#endif /* CONFIG_OF */

static struct platform_driver spi_tegra_driver = {
	.driver = {
		.name =		"spi_tegra",
		.owner =	THIS_MODULE,
		.of_match_table = spi_tegra_of_match_table,
	},
	.remove =	__devexit_p(spi_tegra_remove),
};

static int __init spi_tegra_init(void)
{
	return platform_driver_probe(&spi_tegra_driver, spi_tegra_probe);
}
module_init(spi_tegra_init);

static void __exit spi_tegra_exit(void)
{
	platform_driver_unregister(&spi_tegra_driver);
}
module_exit(spi_tegra_exit);

MODULE_LICENSE("GPL");
