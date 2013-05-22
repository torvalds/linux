/*
 * SPI bus driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_NAME "sirfsoc_spi"

#define SIRFSOC_SPI_CTRL		0x0000
#define SIRFSOC_SPI_CMD			0x0004
#define SIRFSOC_SPI_TX_RX_EN		0x0008
#define SIRFSOC_SPI_INT_EN		0x000C
#define SIRFSOC_SPI_INT_STATUS		0x0010
#define SIRFSOC_SPI_TX_DMA_IO_CTRL	0x0100
#define SIRFSOC_SPI_TX_DMA_IO_LEN	0x0104
#define SIRFSOC_SPI_TXFIFO_CTRL		0x0108
#define SIRFSOC_SPI_TXFIFO_LEVEL_CHK	0x010C
#define SIRFSOC_SPI_TXFIFO_OP		0x0110
#define SIRFSOC_SPI_TXFIFO_STATUS	0x0114
#define SIRFSOC_SPI_TXFIFO_DATA		0x0118
#define SIRFSOC_SPI_RX_DMA_IO_CTRL	0x0120
#define SIRFSOC_SPI_RX_DMA_IO_LEN	0x0124
#define SIRFSOC_SPI_RXFIFO_CTRL		0x0128
#define SIRFSOC_SPI_RXFIFO_LEVEL_CHK	0x012C
#define SIRFSOC_SPI_RXFIFO_OP		0x0130
#define SIRFSOC_SPI_RXFIFO_STATUS	0x0134
#define SIRFSOC_SPI_RXFIFO_DATA		0x0138
#define SIRFSOC_SPI_DUMMY_DELAY_CTL	0x0144

/* SPI CTRL register defines */
#define SIRFSOC_SPI_SLV_MODE		BIT(16)
#define SIRFSOC_SPI_CMD_MODE		BIT(17)
#define SIRFSOC_SPI_CS_IO_OUT		BIT(18)
#define SIRFSOC_SPI_CS_IO_MODE		BIT(19)
#define SIRFSOC_SPI_CLK_IDLE_STAT	BIT(20)
#define SIRFSOC_SPI_CS_IDLE_STAT	BIT(21)
#define SIRFSOC_SPI_TRAN_MSB		BIT(22)
#define SIRFSOC_SPI_DRV_POS_EDGE	BIT(23)
#define SIRFSOC_SPI_CS_HOLD_TIME	BIT(24)
#define SIRFSOC_SPI_CLK_SAMPLE_MODE	BIT(25)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_8	(0 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_12	(1 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_16	(2 << 26)
#define SIRFSOC_SPI_TRAN_DAT_FORMAT_32	(3 << 26)
#define SIRFSOC_SPI_CMD_BYTE_NUM(x)		((x & 3) << 28)
#define SIRFSOC_SPI_ENA_AUTO_CLR		BIT(30)
#define SIRFSOC_SPI_MUL_DAT_MODE		BIT(31)

/* Interrupt Enable */
#define SIRFSOC_SPI_RX_DONE_INT_EN		BIT(0)
#define SIRFSOC_SPI_TX_DONE_INT_EN		BIT(1)
#define SIRFSOC_SPI_RX_OFLOW_INT_EN		BIT(2)
#define SIRFSOC_SPI_TX_UFLOW_INT_EN		BIT(3)
#define SIRFSOC_SPI_RX_IO_DMA_INT_EN	BIT(4)
#define SIRFSOC_SPI_TX_IO_DMA_INT_EN	BIT(5)
#define SIRFSOC_SPI_RXFIFO_FULL_INT_EN	BIT(6)
#define SIRFSOC_SPI_TXFIFO_EMPTY_INT_EN	BIT(7)
#define SIRFSOC_SPI_RXFIFO_THD_INT_EN	BIT(8)
#define SIRFSOC_SPI_TXFIFO_THD_INT_EN	BIT(9)
#define SIRFSOC_SPI_FRM_END_INT_EN	BIT(10)

#define SIRFSOC_SPI_INT_MASK_ALL		0x1FFF

/* Interrupt status */
#define SIRFSOC_SPI_RX_DONE		BIT(0)
#define SIRFSOC_SPI_TX_DONE		BIT(1)
#define SIRFSOC_SPI_RX_OFLOW		BIT(2)
#define SIRFSOC_SPI_TX_UFLOW		BIT(3)
#define SIRFSOC_SPI_RX_FIFO_FULL	BIT(6)
#define SIRFSOC_SPI_TXFIFO_EMPTY	BIT(7)
#define SIRFSOC_SPI_RXFIFO_THD_REACH	BIT(8)
#define SIRFSOC_SPI_TXFIFO_THD_REACH	BIT(9)
#define SIRFSOC_SPI_FRM_END		BIT(10)

/* TX RX enable */
#define SIRFSOC_SPI_RX_EN		BIT(0)
#define SIRFSOC_SPI_TX_EN		BIT(1)
#define SIRFSOC_SPI_CMD_TX_EN		BIT(2)

#define SIRFSOC_SPI_IO_MODE_SEL		BIT(0)
#define SIRFSOC_SPI_RX_DMA_FLUSH	BIT(2)

/* FIFO OPs */
#define SIRFSOC_SPI_FIFO_RESET		BIT(0)
#define SIRFSOC_SPI_FIFO_START		BIT(1)

/* FIFO CTRL */
#define SIRFSOC_SPI_FIFO_WIDTH_BYTE	(0 << 0)
#define SIRFSOC_SPI_FIFO_WIDTH_WORD	(1 << 0)
#define SIRFSOC_SPI_FIFO_WIDTH_DWORD	(2 << 0)

/* FIFO Status */
#define	SIRFSOC_SPI_FIFO_LEVEL_MASK	0xFF
#define SIRFSOC_SPI_FIFO_FULL		BIT(8)
#define SIRFSOC_SPI_FIFO_EMPTY		BIT(9)

/* 256 bytes rx/tx FIFO */
#define SIRFSOC_SPI_FIFO_SIZE		256
#define SIRFSOC_SPI_DAT_FRM_LEN_MAX	(64 * 1024)

#define SIRFSOC_SPI_FIFO_SC(x)		((x) & 0x3F)
#define SIRFSOC_SPI_FIFO_LC(x)		(((x) & 0x3F) << 10)
#define SIRFSOC_SPI_FIFO_HC(x)		(((x) & 0x3F) << 20)
#define SIRFSOC_SPI_FIFO_THD(x)		(((x) & 0xFF) << 2)

struct sirfsoc_spi {
	struct spi_bitbang bitbang;
	struct completion done;

	void __iomem *base;
	u32 ctrl_freq;  /* SPI controller clock speed */
	struct clk *clk;
	struct pinctrl *p;

	/* rx & tx bufs from the spi_transfer */
	const void *tx;
	void *rx;

	/* place received word into rx buffer */
	void (*rx_word) (struct sirfsoc_spi *);
	/* get word from tx buffer for sending */
	void (*tx_word) (struct sirfsoc_spi *);

	/* number of words left to be tranmitted/received */
	unsigned int left_tx_cnt;
	unsigned int left_rx_cnt;

	/* tasklet to push tx msg into FIFO */
	struct tasklet_struct tasklet_tx;

	int chipselect[0];
};

static void spi_sirfsoc_rx_word_u8(struct sirfsoc_spi *sspi)
{
	u32 data;
	u8 *rx = sspi->rx;

	data = readl(sspi->base + SIRFSOC_SPI_RXFIFO_DATA);

	if (rx) {
		*rx++ = (u8) data;
		sspi->rx = rx;
	}

	sspi->left_rx_cnt--;
}

static void spi_sirfsoc_tx_word_u8(struct sirfsoc_spi *sspi)
{
	u32 data = 0;
	const u8 *tx = sspi->tx;

	if (tx) {
		data = *tx++;
		sspi->tx = tx;
	}

	writel(data, sspi->base + SIRFSOC_SPI_TXFIFO_DATA);
	sspi->left_tx_cnt--;
}

static void spi_sirfsoc_rx_word_u16(struct sirfsoc_spi *sspi)
{
	u32 data;
	u16 *rx = sspi->rx;

	data = readl(sspi->base + SIRFSOC_SPI_RXFIFO_DATA);

	if (rx) {
		*rx++ = (u16) data;
		sspi->rx = rx;
	}

	sspi->left_rx_cnt--;
}

static void spi_sirfsoc_tx_word_u16(struct sirfsoc_spi *sspi)
{
	u32 data = 0;
	const u16 *tx = sspi->tx;

	if (tx) {
		data = *tx++;
		sspi->tx = tx;
	}

	writel(data, sspi->base + SIRFSOC_SPI_TXFIFO_DATA);
	sspi->left_tx_cnt--;
}

static void spi_sirfsoc_rx_word_u32(struct sirfsoc_spi *sspi)
{
	u32 data;
	u32 *rx = sspi->rx;

	data = readl(sspi->base + SIRFSOC_SPI_RXFIFO_DATA);

	if (rx) {
		*rx++ = (u32) data;
		sspi->rx = rx;
	}

	sspi->left_rx_cnt--;

}

static void spi_sirfsoc_tx_word_u32(struct sirfsoc_spi *sspi)
{
	u32 data = 0;
	const u32 *tx = sspi->tx;

	if (tx) {
		data = *tx++;
		sspi->tx = tx;
	}

	writel(data, sspi->base + SIRFSOC_SPI_TXFIFO_DATA);
	sspi->left_tx_cnt--;
}

static void spi_sirfsoc_tasklet_tx(unsigned long arg)
{
	struct sirfsoc_spi *sspi = (struct sirfsoc_spi *)arg;

	/* Fill Tx FIFO while there are left words to be transmitted */
	while (!((readl(sspi->base + SIRFSOC_SPI_TXFIFO_STATUS) &
			SIRFSOC_SPI_FIFO_FULL)) &&
			sspi->left_tx_cnt)
		sspi->tx_word(sspi);
}

static irqreturn_t spi_sirfsoc_irq(int irq, void *dev_id)
{
	struct sirfsoc_spi *sspi = dev_id;
	u32 spi_stat = readl(sspi->base + SIRFSOC_SPI_INT_STATUS);

	writel(spi_stat, sspi->base + SIRFSOC_SPI_INT_STATUS);

	/* Error Conditions */
	if (spi_stat & SIRFSOC_SPI_RX_OFLOW ||
			spi_stat & SIRFSOC_SPI_TX_UFLOW) {
		complete(&sspi->done);
		writel(0x0, sspi->base + SIRFSOC_SPI_INT_EN);
	}

	if (spi_stat & SIRFSOC_SPI_FRM_END) {
		while (!((readl(sspi->base + SIRFSOC_SPI_RXFIFO_STATUS)
				& SIRFSOC_SPI_FIFO_EMPTY)) &&
				sspi->left_rx_cnt)
			sspi->rx_word(sspi);

		/* Received all words */
		if ((sspi->left_rx_cnt == 0) && (sspi->left_tx_cnt == 0)) {
			complete(&sspi->done);
			writel(0x0, sspi->base + SIRFSOC_SPI_INT_EN);
		}
	}

	if (spi_stat & SIRFSOC_SPI_RXFIFO_THD_REACH ||
		spi_stat & SIRFSOC_SPI_TXFIFO_THD_REACH ||
		spi_stat & SIRFSOC_SPI_RX_FIFO_FULL ||
		spi_stat & SIRFSOC_SPI_TXFIFO_EMPTY)
		tasklet_schedule(&sspi->tasklet_tx);

	return IRQ_HANDLED;
}

static int spi_sirfsoc_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct sirfsoc_spi *sspi;
	int timeout = t->len * 10;
	sspi = spi_master_get_devdata(spi->master);

	sspi->tx = t->tx_buf;
	sspi->rx = t->rx_buf;
	sspi->left_tx_cnt = sspi->left_rx_cnt = t->len;
	INIT_COMPLETION(sspi->done);

	writel(SIRFSOC_SPI_INT_MASK_ALL, sspi->base + SIRFSOC_SPI_INT_STATUS);

	if (t->len == 1) {
		writel(readl(sspi->base + SIRFSOC_SPI_CTRL) |
			SIRFSOC_SPI_ENA_AUTO_CLR,
			sspi->base + SIRFSOC_SPI_CTRL);
		writel(0, sspi->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(0, sspi->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
	} else if ((t->len > 1) && (t->len < SIRFSOC_SPI_DAT_FRM_LEN_MAX)) {
		writel(readl(sspi->base + SIRFSOC_SPI_CTRL) |
				SIRFSOC_SPI_MUL_DAT_MODE |
				SIRFSOC_SPI_ENA_AUTO_CLR,
			sspi->base + SIRFSOC_SPI_CTRL);
		writel(t->len - 1, sspi->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(t->len - 1, sspi->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
	} else {
		writel(readl(sspi->base + SIRFSOC_SPI_CTRL),
			sspi->base + SIRFSOC_SPI_CTRL);
		writel(0, sspi->base + SIRFSOC_SPI_TX_DMA_IO_LEN);
		writel(0, sspi->base + SIRFSOC_SPI_RX_DMA_IO_LEN);
	}

	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_TXFIFO_OP);

	/* Send the first word to trigger the whole tx/rx process */
	sspi->tx_word(sspi);

	writel(SIRFSOC_SPI_RX_OFLOW_INT_EN | SIRFSOC_SPI_TX_UFLOW_INT_EN |
		SIRFSOC_SPI_RXFIFO_THD_INT_EN | SIRFSOC_SPI_TXFIFO_THD_INT_EN |
		SIRFSOC_SPI_FRM_END_INT_EN | SIRFSOC_SPI_RXFIFO_FULL_INT_EN |
		SIRFSOC_SPI_TXFIFO_EMPTY_INT_EN, sspi->base + SIRFSOC_SPI_INT_EN);
	writel(SIRFSOC_SPI_RX_EN | SIRFSOC_SPI_TX_EN, sspi->base + SIRFSOC_SPI_TX_RX_EN);

	if (wait_for_completion_timeout(&sspi->done, timeout) == 0)
		dev_err(&spi->dev, "transfer timeout\n");

	/* TX, RX FIFO stop */
	writel(0, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(0, sspi->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(0, sspi->base + SIRFSOC_SPI_TX_RX_EN);
	writel(0, sspi->base + SIRFSOC_SPI_INT_EN);

	return t->len - sspi->left_rx_cnt;
}

static void spi_sirfsoc_chipselect(struct spi_device *spi, int value)
{
	struct sirfsoc_spi *sspi = spi_master_get_devdata(spi->master);

	if (sspi->chipselect[spi->chip_select] == 0) {
		u32 regval = readl(sspi->base + SIRFSOC_SPI_CTRL);
		regval |= SIRFSOC_SPI_CS_IO_OUT;
		switch (value) {
		case BITBANG_CS_ACTIVE:
			if (spi->mode & SPI_CS_HIGH)
				regval |= SIRFSOC_SPI_CS_IO_OUT;
			else
				regval &= ~SIRFSOC_SPI_CS_IO_OUT;
			break;
		case BITBANG_CS_INACTIVE:
			if (spi->mode & SPI_CS_HIGH)
				regval &= ~SIRFSOC_SPI_CS_IO_OUT;
			else
				regval |= SIRFSOC_SPI_CS_IO_OUT;
			break;
		}
		writel(regval, sspi->base + SIRFSOC_SPI_CTRL);
	} else {
		int gpio = sspi->chipselect[spi->chip_select];
		gpio_direction_output(gpio, spi->mode & SPI_CS_HIGH ? 0 : 1);
	}
}

static int
spi_sirfsoc_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct sirfsoc_spi *sspi;
	u8 bits_per_word = 0;
	int hz = 0;
	u32 regval;
	u32 txfifo_ctrl, rxfifo_ctrl;
	u32 fifo_size = SIRFSOC_SPI_FIFO_SIZE / 4;

	sspi = spi_master_get_devdata(spi->master);

	bits_per_word = (t) ? t->bits_per_word : spi->bits_per_word;
	hz = t && t->speed_hz ? t->speed_hz : spi->max_speed_hz;

	/* Enable IO mode for RX, TX */
	writel(SIRFSOC_SPI_IO_MODE_SEL, sspi->base + SIRFSOC_SPI_TX_DMA_IO_CTRL);
	writel(SIRFSOC_SPI_IO_MODE_SEL, sspi->base + SIRFSOC_SPI_RX_DMA_IO_CTRL);
	regval = (sspi->ctrl_freq / (2 * hz)) - 1;

	if (regval > 0xFFFF || regval < 0) {
		dev_err(&spi->dev, "Speed %d not supported\n", hz);
		return -EINVAL;
	}

	switch (bits_per_word) {
	case 8:
		regval |= SIRFSOC_SPI_TRAN_DAT_FORMAT_8;
		sspi->rx_word = spi_sirfsoc_rx_word_u8;
		sspi->tx_word = spi_sirfsoc_tx_word_u8;
		txfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_BYTE;
		rxfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_BYTE;
		break;
	case 12:
	case 16:
		regval |= (bits_per_word ==  12) ? SIRFSOC_SPI_TRAN_DAT_FORMAT_12 :
			SIRFSOC_SPI_TRAN_DAT_FORMAT_16;
		sspi->rx_word = spi_sirfsoc_rx_word_u16;
		sspi->tx_word = spi_sirfsoc_tx_word_u16;
		txfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_WORD;
		rxfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_WORD;
		break;
	case 32:
		regval |= SIRFSOC_SPI_TRAN_DAT_FORMAT_32;
		sspi->rx_word = spi_sirfsoc_rx_word_u32;
		sspi->tx_word = spi_sirfsoc_tx_word_u32;
		txfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_DWORD;
		rxfifo_ctrl = SIRFSOC_SPI_FIFO_THD(SIRFSOC_SPI_FIFO_SIZE / 2) |
					SIRFSOC_SPI_FIFO_WIDTH_DWORD;
		break;
	}

	if (!(spi->mode & SPI_CS_HIGH))
		regval |= SIRFSOC_SPI_CS_IDLE_STAT;
	if (!(spi->mode & SPI_LSB_FIRST))
		regval |= SIRFSOC_SPI_TRAN_MSB;
	if (spi->mode & SPI_CPOL)
		regval |= SIRFSOC_SPI_CLK_IDLE_STAT;

	/*
	 * Data should be driven at least 1/2 cycle before the fetch edge to make
	 * sure that data gets stable at the fetch edge.
	 */
	if (((spi->mode & SPI_CPOL) && (spi->mode & SPI_CPHA)) ||
	    (!(spi->mode & SPI_CPOL) && !(spi->mode & SPI_CPHA)))
		regval &= ~SIRFSOC_SPI_DRV_POS_EDGE;
	else
		regval |= SIRFSOC_SPI_DRV_POS_EDGE;

	writel(SIRFSOC_SPI_FIFO_SC(fifo_size - 2) |
			SIRFSOC_SPI_FIFO_LC(fifo_size / 2) |
			SIRFSOC_SPI_FIFO_HC(2),
		sspi->base + SIRFSOC_SPI_TXFIFO_LEVEL_CHK);
	writel(SIRFSOC_SPI_FIFO_SC(2) |
			SIRFSOC_SPI_FIFO_LC(fifo_size / 2) |
			SIRFSOC_SPI_FIFO_HC(fifo_size - 2),
		sspi->base + SIRFSOC_SPI_RXFIFO_LEVEL_CHK);
	writel(txfifo_ctrl, sspi->base + SIRFSOC_SPI_TXFIFO_CTRL);
	writel(rxfifo_ctrl, sspi->base + SIRFSOC_SPI_RXFIFO_CTRL);

	writel(regval, sspi->base + SIRFSOC_SPI_CTRL);
	return 0;
}

static int spi_sirfsoc_setup(struct spi_device *spi)
{
	struct sirfsoc_spi *sspi;

	if (!spi->max_speed_hz)
		return -EINVAL;

	sspi = spi_master_get_devdata(spi->master);

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	return spi_sirfsoc_setup_transfer(spi, NULL);
}

static int spi_sirfsoc_probe(struct platform_device *pdev)
{
	struct sirfsoc_spi *sspi;
	struct spi_master *master;
	struct resource *mem_res;
	int num_cs, cs_gpio, irq;
	int i;
	int ret;

	ret = of_property_read_u32(pdev->dev.of_node,
			"sirf,spi-num-chipselects", &num_cs);
	if (ret < 0) {
		dev_err(&pdev->dev, "Unable to get chip select number\n");
		goto err_cs;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*sspi) + sizeof(int) * num_cs);
	if (!master) {
		dev_err(&pdev->dev, "Unable to allocate SPI master\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, master);
	sspi = spi_master_get_devdata(master);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "Unable to get IO resource\n");
		ret = -ENODEV;
		goto free_master;
	}
	master->num_chipselect = num_cs;

	for (i = 0; i < master->num_chipselect; i++) {
		cs_gpio = of_get_named_gpio(pdev->dev.of_node, "cs-gpios", i);
		if (cs_gpio < 0) {
			dev_err(&pdev->dev, "can't get cs gpio from DT\n");
			ret = -ENODEV;
			goto free_master;
		}

		sspi->chipselect[i] = cs_gpio;
		if (cs_gpio == 0)
			continue; /* use cs from spi controller */

		ret = gpio_request(cs_gpio, DRIVER_NAME);
		if (ret) {
			while (i > 0) {
				i--;
				if (sspi->chipselect[i] > 0)
					gpio_free(sspi->chipselect[i]);
			}
			dev_err(&pdev->dev, "fail to request cs gpios\n");
			goto free_master;
		}
	}

	sspi->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(sspi->base)) {
		ret = PTR_ERR(sspi->base);
		goto free_master;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto free_master;
	}
	ret = devm_request_irq(&pdev->dev, irq, spi_sirfsoc_irq, 0,
				DRIVER_NAME, sspi);
	if (ret)
		goto free_master;

	sspi->bitbang.master = spi_master_get(master);
	sspi->bitbang.chipselect = spi_sirfsoc_chipselect;
	sspi->bitbang.setup_transfer = spi_sirfsoc_setup_transfer;
	sspi->bitbang.txrx_bufs = spi_sirfsoc_transfer;
	sspi->bitbang.master->setup = spi_sirfsoc_setup;
	master->bus_num = pdev->id;
	master->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(12) |
					SPI_BPW_MASK(16) | SPI_BPW_MASK(32);
	sspi->bitbang.master->dev.of_node = pdev->dev.of_node;

	sspi->p = pinctrl_get_select_default(&pdev->dev);
	ret = IS_ERR(sspi->p);
	if (ret)
		goto free_master;

	sspi->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(sspi->clk)) {
		ret = -EINVAL;
		goto free_pin;
	}
	clk_prepare_enable(sspi->clk);
	sspi->ctrl_freq = clk_get_rate(sspi->clk);

	init_completion(&sspi->done);

	tasklet_init(&sspi->tasklet_tx, spi_sirfsoc_tasklet_tx,
		     (unsigned long)sspi);

	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_TXFIFO_OP);
	/* We are not using dummy delay between command and data */
	writel(0, sspi->base + SIRFSOC_SPI_DUMMY_DELAY_CTL);

	ret = spi_bitbang_start(&sspi->bitbang);
	if (ret)
		goto free_clk;

	dev_info(&pdev->dev, "registerred, bus number = %d\n", master->bus_num);

	return 0;

free_clk:
	clk_disable_unprepare(sspi->clk);
	clk_put(sspi->clk);
free_pin:
	pinctrl_put(sspi->p);
free_master:
	spi_master_put(master);
err_cs:
	return ret;
}

static int  spi_sirfsoc_remove(struct platform_device *pdev)
{
	struct spi_master *master;
	struct sirfsoc_spi *sspi;
	int i;

	master = platform_get_drvdata(pdev);
	sspi = spi_master_get_devdata(master);

	spi_bitbang_stop(&sspi->bitbang);
	for (i = 0; i < master->num_chipselect; i++) {
		if (sspi->chipselect[i] > 0)
			gpio_free(sspi->chipselect[i]);
	}
	clk_disable_unprepare(sspi->clk);
	clk_put(sspi->clk);
	pinctrl_put(sspi->p);
	spi_master_put(master);
	return 0;
}

#ifdef CONFIG_PM
static int spi_sirfsoc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct sirfsoc_spi *sspi = spi_master_get_devdata(master);

	clk_disable(sspi->clk);
	return 0;
}

static int spi_sirfsoc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct spi_master *master = platform_get_drvdata(pdev);
	struct sirfsoc_spi *sspi = spi_master_get_devdata(master);

	clk_enable(sspi->clk);
	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_RESET, sspi->base + SIRFSOC_SPI_TXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_RXFIFO_OP);
	writel(SIRFSOC_SPI_FIFO_START, sspi->base + SIRFSOC_SPI_TXFIFO_OP);

	return 0;
}

static const struct dev_pm_ops spi_sirfsoc_pm_ops = {
	.suspend = spi_sirfsoc_suspend,
	.resume = spi_sirfsoc_resume,
};
#endif

static const struct of_device_id spi_sirfsoc_of_match[] = {
	{ .compatible = "sirf,prima2-spi", },
	{ .compatible = "sirf,marco-spi", },
	{}
};
MODULE_DEVICE_TABLE(of, spi_sirfsoc_of_match);

static struct platform_driver spi_sirfsoc_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm     = &spi_sirfsoc_pm_ops,
#endif
		.of_match_table = spi_sirfsoc_of_match,
	},
	.probe = spi_sirfsoc_probe,
	.remove = spi_sirfsoc_remove,
};
module_platform_driver(spi_sirfsoc_driver);

MODULE_DESCRIPTION("SiRF SoC SPI master driver");
MODULE_AUTHOR("Zhiwu Song <Zhiwu.Song@csr.com>, "
		"Barry Song <Baohua.Song@csr.com>");
MODULE_LICENSE("GPL v2");
