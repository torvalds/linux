// SPDX-License-Identifier: GPL-2.0-only
/*
 * PIC32 Quad SPI controller driver.
 *
 * Purna Chandra Mandal <purna.mandal@microchip.com>
 * Copyright (c) 2016, Microchip Technology Inc.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

/* SQI registers */
#define PESQI_XIP_CONF1_REG	0x00
#define PESQI_XIP_CONF2_REG	0x04
#define PESQI_CONF_REG		0x08
#define PESQI_CTRL_REG		0x0C
#define PESQI_CLK_CTRL_REG	0x10
#define PESQI_CMD_THRES_REG	0x14
#define PESQI_INT_THRES_REG	0x18
#define PESQI_INT_ENABLE_REG	0x1C
#define PESQI_INT_STAT_REG	0x20
#define PESQI_TX_DATA_REG	0x24
#define PESQI_RX_DATA_REG	0x28
#define PESQI_STAT1_REG		0x2C
#define PESQI_STAT2_REG		0x30
#define PESQI_BD_CTRL_REG	0x34
#define PESQI_BD_CUR_ADDR_REG	0x38
#define PESQI_BD_BASE_ADDR_REG	0x40
#define PESQI_BD_STAT_REG	0x44
#define PESQI_BD_POLL_CTRL_REG	0x48
#define PESQI_BD_TX_DMA_STAT_REG	0x4C
#define PESQI_BD_RX_DMA_STAT_REG	0x50
#define PESQI_THRES_REG		0x54
#define PESQI_INT_SIGEN_REG	0x58

/* PESQI_CONF_REG fields */
#define PESQI_MODE		0x7
#define  PESQI_MODE_BOOT	0
#define  PESQI_MODE_PIO		1
#define  PESQI_MODE_DMA		2
#define  PESQI_MODE_XIP		3
#define PESQI_MODE_SHIFT	0
#define PESQI_CPHA		BIT(3)
#define PESQI_CPOL		BIT(4)
#define PESQI_LSBF		BIT(5)
#define PESQI_RXLATCH		BIT(7)
#define PESQI_SERMODE		BIT(8)
#define PESQI_WP_EN		BIT(9)
#define PESQI_HOLD_EN		BIT(10)
#define PESQI_BURST_EN		BIT(12)
#define PESQI_CS_CTRL_HW	BIT(15)
#define PESQI_SOFT_RESET	BIT(16)
#define PESQI_LANES_SHIFT	20
#define  PESQI_SINGLE_LANE	0
#define  PESQI_DUAL_LANE	1
#define  PESQI_QUAD_LANE	2
#define PESQI_CSEN_SHIFT	24
#define PESQI_EN		BIT(23)

/* PESQI_CLK_CTRL_REG fields */
#define PESQI_CLK_EN		BIT(0)
#define PESQI_CLK_STABLE	BIT(1)
#define PESQI_CLKDIV_SHIFT	8
#define PESQI_CLKDIV		0xff

/* PESQI_INT_THR/CMD_THR_REG */
#define PESQI_TXTHR_MASK	0x1f
#define PESQI_TXTHR_SHIFT	8
#define PESQI_RXTHR_MASK	0x1f
#define PESQI_RXTHR_SHIFT	0

/* PESQI_INT_EN/INT_STAT/INT_SIG_EN_REG */
#define PESQI_TXEMPTY		BIT(0)
#define PESQI_TXFULL		BIT(1)
#define PESQI_TXTHR		BIT(2)
#define PESQI_RXEMPTY		BIT(3)
#define PESQI_RXFULL		BIT(4)
#define PESQI_RXTHR		BIT(5)
#define PESQI_BDDONE		BIT(9)  /* BD processing complete */
#define PESQI_PKTCOMP		BIT(10) /* packet processing complete */
#define PESQI_DMAERR		BIT(11) /* error */

/* PESQI_BD_CTRL_REG */
#define PESQI_DMA_EN		BIT(0) /* enable DMA engine */
#define PESQI_POLL_EN		BIT(1) /* enable polling */
#define PESQI_BDP_START		BIT(2) /* start BD processor */

/* PESQI controller buffer descriptor */
struct buf_desc {
	u32 bd_ctrl;	/* control */
	u32 bd_status;	/* reserved */
	u32 bd_addr;	/* DMA buffer addr */
	u32 bd_nextp;	/* next item in chain */
};

/* bd_ctrl */
#define BD_BUFLEN		0x1ff
#define BD_CBD_INT_EN		BIT(16)	/* Current BD is processed */
#define BD_PKT_INT_EN		BIT(17) /* All BDs of PKT processed */
#define BD_LIFM			BIT(18) /* last data of pkt */
#define BD_LAST			BIT(19) /* end of list */
#define BD_DATA_RECV		BIT(20) /* receive data */
#define BD_DDR			BIT(21) /* DDR mode */
#define BD_DUAL			BIT(22)	/* Dual SPI */
#define BD_QUAD			BIT(23) /* Quad SPI */
#define BD_LSBF			BIT(25)	/* LSB First */
#define BD_STAT_CHECK		BIT(27) /* Status poll */
#define BD_DEVSEL_SHIFT		28	/* CS */
#define BD_CS_DEASSERT		BIT(30) /* de-assert CS after current BD */
#define BD_EN			BIT(31) /* BD owned by H/W */

/**
 * struct ring_desc - Representation of SQI ring descriptor
 * @list:	list element to add to free or used list.
 * @bd:		PESQI controller buffer descriptor
 * @bd_dma:	DMA address of PESQI controller buffer descriptor
 * @xfer_len:	transfer length
 */
struct ring_desc {
	struct list_head list;
	struct buf_desc *bd;
	dma_addr_t bd_dma;
	u32 xfer_len;
};

/* Global constants */
#define PESQI_BD_BUF_LEN_MAX	256
#define PESQI_BD_COUNT		256 /* max 64KB data per spi message */

struct pic32_sqi {
	void __iomem		*regs;
	struct clk		*sys_clk;
	struct clk		*base_clk; /* drives spi clock */
	struct spi_controller	*host;
	int			irq;
	struct completion	xfer_done;
	struct ring_desc	*ring;
	void			*bd;
	dma_addr_t		bd_dma;
	struct list_head	bd_list_free; /* free */
	struct list_head	bd_list_used; /* allocated */
	struct spi_device	*cur_spi;
	u32			cur_speed;
	u8			cur_mode;
};

static inline void pic32_setbits(void __iomem *reg, u32 set)
{
	writel(readl(reg) | set, reg);
}

static inline void pic32_clrbits(void __iomem *reg, u32 clr)
{
	writel(readl(reg) & ~clr, reg);
}

static int pic32_sqi_set_clk_rate(struct pic32_sqi *sqi, u32 sck)
{
	u32 val, div;

	/* div = base_clk / (2 * spi_clk) */
	div = clk_get_rate(sqi->base_clk) / (2 * sck);
	div &= PESQI_CLKDIV;

	val = readl(sqi->regs + PESQI_CLK_CTRL_REG);
	/* apply new divider */
	val &= ~(PESQI_CLK_STABLE | (PESQI_CLKDIV << PESQI_CLKDIV_SHIFT));
	val |= div << PESQI_CLKDIV_SHIFT;
	writel(val, sqi->regs + PESQI_CLK_CTRL_REG);

	/* wait for stability */
	return readl_poll_timeout(sqi->regs + PESQI_CLK_CTRL_REG, val,
				  val & PESQI_CLK_STABLE, 1, 5000);
}

static inline void pic32_sqi_enable_int(struct pic32_sqi *sqi)
{
	u32 mask = PESQI_DMAERR | PESQI_BDDONE | PESQI_PKTCOMP;

	writel(mask, sqi->regs + PESQI_INT_ENABLE_REG);
	/* INT_SIGEN works as interrupt-gate to INTR line */
	writel(mask, sqi->regs + PESQI_INT_SIGEN_REG);
}

static inline void pic32_sqi_disable_int(struct pic32_sqi *sqi)
{
	writel(0, sqi->regs + PESQI_INT_ENABLE_REG);
	writel(0, sqi->regs + PESQI_INT_SIGEN_REG);
}

static irqreturn_t pic32_sqi_isr(int irq, void *dev_id)
{
	struct pic32_sqi *sqi = dev_id;
	u32 enable, status;

	enable = readl(sqi->regs + PESQI_INT_ENABLE_REG);
	status = readl(sqi->regs + PESQI_INT_STAT_REG);

	/* check spurious interrupt */
	if (!status)
		return IRQ_NONE;

	if (status & PESQI_DMAERR) {
		enable = 0;
		goto irq_done;
	}

	if (status & PESQI_TXTHR)
		enable &= ~(PESQI_TXTHR | PESQI_TXFULL | PESQI_TXEMPTY);

	if (status & PESQI_RXTHR)
		enable &= ~(PESQI_RXTHR | PESQI_RXFULL | PESQI_RXEMPTY);

	if (status & PESQI_BDDONE)
		enable &= ~PESQI_BDDONE;

	/* packet processing completed */
	if (status & PESQI_PKTCOMP) {
		/* mask all interrupts */
		enable = 0;
		/* complete transaction */
		complete(&sqi->xfer_done);
	}

irq_done:
	/* interrupts are sticky, so mask when handled */
	writel(enable, sqi->regs + PESQI_INT_ENABLE_REG);

	return IRQ_HANDLED;
}

static struct ring_desc *ring_desc_get(struct pic32_sqi *sqi)
{
	struct ring_desc *rdesc;

	if (list_empty(&sqi->bd_list_free))
		return NULL;

	rdesc = list_first_entry(&sqi->bd_list_free, struct ring_desc, list);
	list_move_tail(&rdesc->list, &sqi->bd_list_used);
	return rdesc;
}

static void ring_desc_put(struct pic32_sqi *sqi, struct ring_desc *rdesc)
{
	list_move(&rdesc->list, &sqi->bd_list_free);
}

static int pic32_sqi_one_transfer(struct pic32_sqi *sqi,
				  struct spi_message *mesg,
				  struct spi_transfer *xfer)
{
	struct spi_device *spi = mesg->spi;
	struct scatterlist *sg, *sgl;
	struct ring_desc *rdesc;
	struct buf_desc *bd;
	int nents, i;
	u32 bd_ctrl;
	u32 nbits;

	/* Device selection */
	bd_ctrl = spi_get_chipselect(spi, 0) << BD_DEVSEL_SHIFT;

	/* half-duplex: select transfer buffer, direction and lane */
	if (xfer->rx_buf) {
		bd_ctrl |= BD_DATA_RECV;
		nbits = xfer->rx_nbits;
		sgl = xfer->rx_sg.sgl;
		nents = xfer->rx_sg.nents;
	} else {
		nbits = xfer->tx_nbits;
		sgl = xfer->tx_sg.sgl;
		nents = xfer->tx_sg.nents;
	}

	if (nbits & SPI_NBITS_QUAD)
		bd_ctrl |= BD_QUAD;
	else if (nbits & SPI_NBITS_DUAL)
		bd_ctrl |= BD_DUAL;

	/* LSB first */
	if (spi->mode & SPI_LSB_FIRST)
		bd_ctrl |= BD_LSBF;

	/* ownership to hardware */
	bd_ctrl |= BD_EN;

	for_each_sg(sgl, sg, nents, i) {
		/* get ring descriptor */
		rdesc = ring_desc_get(sqi);
		if (!rdesc)
			break;

		bd = rdesc->bd;

		/* BD CTRL: length */
		rdesc->xfer_len = sg_dma_len(sg);
		bd->bd_ctrl = bd_ctrl;
		bd->bd_ctrl |= rdesc->xfer_len;

		/* BD STAT */
		bd->bd_status = 0;

		/* BD BUFFER ADDRESS */
		bd->bd_addr = sg->dma_address;
	}

	return 0;
}

static int pic32_sqi_prepare_hardware(struct spi_controller *host)
{
	struct pic32_sqi *sqi = spi_controller_get_devdata(host);

	/* enable spi interface */
	pic32_setbits(sqi->regs + PESQI_CONF_REG, PESQI_EN);
	/* enable spi clk */
	pic32_setbits(sqi->regs + PESQI_CLK_CTRL_REG, PESQI_CLK_EN);

	return 0;
}

static bool pic32_sqi_can_dma(struct spi_controller *host,
			      struct spi_device *spi,
			      struct spi_transfer *x)
{
	/* Do DMA irrespective of transfer size */
	return true;
}

static int pic32_sqi_one_message(struct spi_controller *host,
				 struct spi_message *msg)
{
	struct spi_device *spi = msg->spi;
	struct ring_desc *rdesc, *next;
	struct spi_transfer *xfer;
	struct pic32_sqi *sqi;
	int ret = 0, mode;
	unsigned long time_left;
	u32 val;

	sqi = spi_controller_get_devdata(host);

	reinit_completion(&sqi->xfer_done);
	msg->actual_length = 0;

	/* We can't handle spi_transfer specific "speed_hz", "bits_per_word"
	 * and "delay_usecs". But spi_device specific speed and mode change
	 * can be handled at best during spi chip-select switch.
	 */
	if (sqi->cur_spi != spi) {
		/* set spi speed */
		if (sqi->cur_speed != spi->max_speed_hz) {
			sqi->cur_speed = spi->max_speed_hz;
			ret = pic32_sqi_set_clk_rate(sqi, spi->max_speed_hz);
			if (ret)
				dev_warn(&spi->dev, "set_clk, %d\n", ret);
		}

		/* set spi mode */
		mode = spi->mode & (SPI_MODE_3 | SPI_LSB_FIRST);
		if (sqi->cur_mode != mode) {
			val = readl(sqi->regs + PESQI_CONF_REG);
			val &= ~(PESQI_CPOL | PESQI_CPHA | PESQI_LSBF);
			if (mode & SPI_CPOL)
				val |= PESQI_CPOL;
			if (mode & SPI_LSB_FIRST)
				val |= PESQI_LSBF;
			val |= PESQI_CPHA;
			writel(val, sqi->regs + PESQI_CONF_REG);

			sqi->cur_mode = mode;
		}
		sqi->cur_spi = spi;
	}

	/* prepare hardware desc-list(BD) for transfer(s) */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		ret = pic32_sqi_one_transfer(sqi, msg, xfer);
		if (ret) {
			dev_err(&spi->dev, "xfer %p err\n", xfer);
			goto xfer_out;
		}
	}

	/* BDs are prepared and chained. Now mark LAST_BD, CS_DEASSERT at last
	 * element of the list.
	 */
	rdesc = list_last_entry(&sqi->bd_list_used, struct ring_desc, list);
	rdesc->bd->bd_ctrl |= BD_LAST | BD_CS_DEASSERT |
			      BD_LIFM | BD_PKT_INT_EN;

	/* set base address BD list for DMA engine */
	rdesc = list_first_entry(&sqi->bd_list_used, struct ring_desc, list);
	writel(rdesc->bd_dma, sqi->regs + PESQI_BD_BASE_ADDR_REG);

	/* enable interrupt */
	pic32_sqi_enable_int(sqi);

	/* enable DMA engine */
	val = PESQI_DMA_EN | PESQI_POLL_EN | PESQI_BDP_START;
	writel(val, sqi->regs + PESQI_BD_CTRL_REG);

	/* wait for xfer completion */
	time_left = wait_for_completion_timeout(&sqi->xfer_done, 5 * HZ);
	if (time_left == 0) {
		dev_err(&sqi->host->dev, "wait timedout/interrupted\n");
		ret = -ETIMEDOUT;
		msg->status = ret;
	} else {
		/* success */
		msg->status = 0;
		ret = 0;
	}

	/* disable DMA */
	writel(0, sqi->regs + PESQI_BD_CTRL_REG);

	pic32_sqi_disable_int(sqi);

xfer_out:
	list_for_each_entry_safe_reverse(rdesc, next,
					 &sqi->bd_list_used, list) {
		/* Update total byte transferred */
		msg->actual_length += rdesc->xfer_len;
		/* release ring descr */
		ring_desc_put(sqi, rdesc);
	}
	spi_finalize_current_message(spi->controller);

	return ret;
}

static int pic32_sqi_unprepare_hardware(struct spi_controller *host)
{
	struct pic32_sqi *sqi = spi_controller_get_devdata(host);

	/* disable clk */
	pic32_clrbits(sqi->regs + PESQI_CLK_CTRL_REG, PESQI_CLK_EN);
	/* disable spi */
	pic32_clrbits(sqi->regs + PESQI_CONF_REG, PESQI_EN);

	return 0;
}

static int ring_desc_ring_alloc(struct pic32_sqi *sqi)
{
	struct ring_desc *rdesc;
	struct buf_desc *bd;
	int i;

	/* allocate coherent DMAable memory for hardware buffer descriptors. */
	sqi->bd = dma_alloc_coherent(&sqi->host->dev,
				     sizeof(*bd) * PESQI_BD_COUNT,
				     &sqi->bd_dma, GFP_KERNEL);
	if (!sqi->bd) {
		dev_err(&sqi->host->dev, "failed allocating dma buffer\n");
		return -ENOMEM;
	}

	/* allocate software ring descriptors */
	sqi->ring = kcalloc(PESQI_BD_COUNT, sizeof(*rdesc), GFP_KERNEL);
	if (!sqi->ring) {
		dma_free_coherent(&sqi->host->dev,
				  sizeof(*bd) * PESQI_BD_COUNT,
				  sqi->bd, sqi->bd_dma);
		return -ENOMEM;
	}

	bd = (struct buf_desc *)sqi->bd;

	INIT_LIST_HEAD(&sqi->bd_list_free);
	INIT_LIST_HEAD(&sqi->bd_list_used);

	/* initialize ring-desc */
	for (i = 0, rdesc = sqi->ring; i < PESQI_BD_COUNT; i++, rdesc++) {
		INIT_LIST_HEAD(&rdesc->list);
		rdesc->bd = &bd[i];
		rdesc->bd_dma = sqi->bd_dma + (void *)&bd[i] - (void *)bd;
		list_add_tail(&rdesc->list, &sqi->bd_list_free);
	}

	/* Prepare BD: chain to next BD(s) */
	for (i = 0, rdesc = sqi->ring; i < PESQI_BD_COUNT - 1; i++)
		bd[i].bd_nextp = rdesc[i + 1].bd_dma;
	bd[PESQI_BD_COUNT - 1].bd_nextp = 0;

	return 0;
}

static void ring_desc_ring_free(struct pic32_sqi *sqi)
{
	dma_free_coherent(&sqi->host->dev,
			  sizeof(struct buf_desc) * PESQI_BD_COUNT,
			  sqi->bd, sqi->bd_dma);
	kfree(sqi->ring);
}

static void pic32_sqi_hw_init(struct pic32_sqi *sqi)
{
	unsigned long flags;
	u32 val;

	/* Soft-reset of PESQI controller triggers interrupt.
	 * We are not yet ready to handle them so disable CPU
	 * interrupt for the time being.
	 */
	local_irq_save(flags);

	/* assert soft-reset */
	writel(PESQI_SOFT_RESET, sqi->regs + PESQI_CONF_REG);

	/* wait until clear */
	readl_poll_timeout_atomic(sqi->regs + PESQI_CONF_REG, val,
				  !(val & PESQI_SOFT_RESET), 1, 5000);

	/* disable all interrupts */
	pic32_sqi_disable_int(sqi);

	/* Now it is safe to enable back CPU interrupt */
	local_irq_restore(flags);

	/* tx and rx fifo interrupt threshold */
	val = readl(sqi->regs + PESQI_CMD_THRES_REG);
	val &= ~(PESQI_TXTHR_MASK << PESQI_TXTHR_SHIFT);
	val &= ~(PESQI_RXTHR_MASK << PESQI_RXTHR_SHIFT);
	val |= (1U << PESQI_TXTHR_SHIFT) | (1U << PESQI_RXTHR_SHIFT);
	writel(val, sqi->regs + PESQI_CMD_THRES_REG);

	val = readl(sqi->regs + PESQI_INT_THRES_REG);
	val &= ~(PESQI_TXTHR_MASK << PESQI_TXTHR_SHIFT);
	val &= ~(PESQI_RXTHR_MASK << PESQI_RXTHR_SHIFT);
	val |= (1U << PESQI_TXTHR_SHIFT) | (1U << PESQI_RXTHR_SHIFT);
	writel(val, sqi->regs + PESQI_INT_THRES_REG);

	/* default configuration */
	val = readl(sqi->regs + PESQI_CONF_REG);

	/* set mode: DMA */
	val &= ~PESQI_MODE;
	val |= PESQI_MODE_DMA << PESQI_MODE_SHIFT;
	writel(val, sqi->regs + PESQI_CONF_REG);

	/* DATAEN - SQIID0-ID3 */
	val |= PESQI_QUAD_LANE << PESQI_LANES_SHIFT;

	/* burst/INCR4 enable */
	val |= PESQI_BURST_EN;

	/* CSEN - all CS */
	val |= 3U << PESQI_CSEN_SHIFT;
	writel(val, sqi->regs + PESQI_CONF_REG);

	/* write poll count */
	writel(0, sqi->regs + PESQI_BD_POLL_CTRL_REG);

	sqi->cur_speed = 0;
	sqi->cur_mode = -1;
}

static int pic32_sqi_probe(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct pic32_sqi *sqi;
	int ret;

	host = spi_alloc_host(&pdev->dev, sizeof(*sqi));
	if (!host)
		return -ENOMEM;

	sqi = spi_controller_get_devdata(host);
	sqi->host = host;

	sqi->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sqi->regs)) {
		ret = PTR_ERR(sqi->regs);
		goto err_free_host;
	}

	/* irq */
	sqi->irq = platform_get_irq(pdev, 0);
	if (sqi->irq < 0) {
		ret = sqi->irq;
		goto err_free_host;
	}

	/* clocks */
	sqi->sys_clk = devm_clk_get_enabled(&pdev->dev, "reg_ck");
	if (IS_ERR(sqi->sys_clk)) {
		ret = PTR_ERR(sqi->sys_clk);
		dev_err(&pdev->dev, "no sys_clk ?\n");
		goto err_free_host;
	}

	sqi->base_clk = devm_clk_get_enabled(&pdev->dev, "spi_ck");
	if (IS_ERR(sqi->base_clk)) {
		ret = PTR_ERR(sqi->base_clk);
		dev_err(&pdev->dev, "no base clk ?\n");
		goto err_free_host;
	}

	init_completion(&sqi->xfer_done);

	/* initialize hardware */
	pic32_sqi_hw_init(sqi);

	/* allocate buffers & descriptors */
	ret = ring_desc_ring_alloc(sqi);
	if (ret) {
		dev_err(&pdev->dev, "ring alloc failed\n");
		goto err_free_host;
	}

	/* install irq handlers */
	ret = request_irq(sqi->irq, pic32_sqi_isr, 0,
			  dev_name(&pdev->dev), sqi);
	if (ret < 0) {
		dev_err(&pdev->dev, "request_irq(%d), failed\n", sqi->irq);
		goto err_free_ring;
	}

	/* register host */
	host->num_chipselect	= 2;
	host->max_speed_hz	= clk_get_rate(sqi->base_clk);
	host->dma_alignment	= 32;
	host->max_dma_len	= PESQI_BD_BUF_LEN_MAX;
	host->dev.of_node	= pdev->dev.of_node;
	host->mode_bits		= SPI_MODE_3 | SPI_MODE_0 | SPI_TX_DUAL |
				  SPI_RX_DUAL | SPI_TX_QUAD | SPI_RX_QUAD;
	host->flags		= SPI_CONTROLLER_HALF_DUPLEX;
	host->can_dma		= pic32_sqi_can_dma;
	host->bits_per_word_mask	= SPI_BPW_RANGE_MASK(8, 32);
	host->transfer_one_message	= pic32_sqi_one_message;
	host->prepare_transfer_hardware	= pic32_sqi_prepare_hardware;
	host->unprepare_transfer_hardware	= pic32_sqi_unprepare_hardware;

	ret = devm_spi_register_controller(&pdev->dev, host);
	if (ret) {
		dev_err(&host->dev, "failed registering spi host\n");
		free_irq(sqi->irq, sqi);
		goto err_free_ring;
	}

	platform_set_drvdata(pdev, sqi);

	return 0;

err_free_ring:
	ring_desc_ring_free(sqi);

err_free_host:
	spi_controller_put(host);
	return ret;
}

static void pic32_sqi_remove(struct platform_device *pdev)
{
	struct pic32_sqi *sqi = platform_get_drvdata(pdev);

	/* release resources */
	free_irq(sqi->irq, sqi);
	ring_desc_ring_free(sqi);
}

static const struct of_device_id pic32_sqi_of_ids[] = {
	{.compatible = "microchip,pic32mzda-sqi",},
	{},
};
MODULE_DEVICE_TABLE(of, pic32_sqi_of_ids);

static struct platform_driver pic32_sqi_driver = {
	.driver = {
		.name = "sqi-pic32",
		.of_match_table = of_match_ptr(pic32_sqi_of_ids),
	},
	.probe = pic32_sqi_probe,
	.remove = pic32_sqi_remove,
};

module_platform_driver(pic32_sqi_driver);

MODULE_AUTHOR("Purna Chandra Mandal <purna.mandal@microchip.com>");
MODULE_DESCRIPTION("Microchip SPI driver for PIC32 SQI controller.");
MODULE_LICENSE("GPL v2");
