// SPDX-License-Identifier: GPL-2.0
// spi-uniphier.c - Socionext UniPhier SPI controller driver
// Copyright 2012      Panasonic Corporation
// Copyright 2016-2018 Socionext Inc.

#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

#include <asm/unaligned.h>

#define SSI_TIMEOUT_MS		2000
#define SSI_POLL_TIMEOUT_US	200
#define SSI_MAX_CLK_DIVIDER	254
#define SSI_MIN_CLK_DIVIDER	4

struct uniphier_spi_priv {
	void __iomem *base;
	dma_addr_t base_dma_addr;
	struct clk *clk;
	struct spi_master *master;
	struct completion xfer_done;

	int error;
	unsigned int tx_bytes;
	unsigned int rx_bytes;
	const u8 *tx_buf;
	u8 *rx_buf;
	atomic_t dma_busy;

	bool is_save_param;
	u8 bits_per_word;
	u16 mode;
	u32 speed_hz;
};

#define SSI_CTL			0x00
#define   SSI_CTL_EN		BIT(0)

#define SSI_CKS			0x04
#define   SSI_CKS_CKRAT_MASK	GENMASK(7, 0)
#define   SSI_CKS_CKPHS		BIT(14)
#define   SSI_CKS_CKINIT	BIT(13)
#define   SSI_CKS_CKDLY		BIT(12)

#define SSI_TXWDS		0x08
#define   SSI_TXWDS_WDLEN_MASK	GENMASK(13, 8)
#define   SSI_TXWDS_TDTF_MASK	GENMASK(7, 6)
#define   SSI_TXWDS_DTLEN_MASK	GENMASK(5, 0)

#define SSI_RXWDS		0x0c
#define   SSI_RXWDS_DTLEN_MASK	GENMASK(5, 0)

#define SSI_FPS			0x10
#define   SSI_FPS_FSPOL		BIT(15)
#define   SSI_FPS_FSTRT		BIT(14)

#define SSI_SR			0x14
#define   SSI_SR_BUSY		BIT(7)
#define   SSI_SR_RNE		BIT(0)

#define SSI_IE			0x18
#define   SSI_IE_TCIE		BIT(4)
#define   SSI_IE_RCIE		BIT(3)
#define   SSI_IE_TXRE		BIT(2)
#define   SSI_IE_RXRE		BIT(1)
#define   SSI_IE_RORIE		BIT(0)
#define   SSI_IE_ALL_MASK	GENMASK(4, 0)

#define SSI_IS			0x1c
#define   SSI_IS_RXRS		BIT(9)
#define   SSI_IS_RCID		BIT(3)
#define   SSI_IS_RORID		BIT(0)

#define SSI_IC			0x1c
#define   SSI_IC_TCIC		BIT(4)
#define   SSI_IC_RCIC		BIT(3)
#define   SSI_IC_RORIC		BIT(0)

#define SSI_FC			0x20
#define   SSI_FC_TXFFL		BIT(12)
#define   SSI_FC_TXFTH_MASK	GENMASK(11, 8)
#define   SSI_FC_RXFFL		BIT(4)
#define   SSI_FC_RXFTH_MASK	GENMASK(3, 0)

#define SSI_TXDR		0x24
#define SSI_RXDR		0x24

#define SSI_FIFO_DEPTH		8U
#define SSI_FIFO_BURST_NUM	1

#define SSI_DMA_RX_BUSY		BIT(1)
#define SSI_DMA_TX_BUSY		BIT(0)

static inline unsigned int bytes_per_word(unsigned int bits)
{
	return bits <= 8 ? 1 : (bits <= 16 ? 2 : 4);
}

static inline void uniphier_spi_irq_enable(struct uniphier_spi_priv *priv,
					   u32 mask)
{
	u32 val;

	val = readl(priv->base + SSI_IE);
	val |= mask;
	writel(val, priv->base + SSI_IE);
}

static inline void uniphier_spi_irq_disable(struct uniphier_spi_priv *priv,
					    u32 mask)
{
	u32 val;

	val = readl(priv->base + SSI_IE);
	val &= ~mask;
	writel(val, priv->base + SSI_IE);
}

static void uniphier_spi_set_mode(struct spi_device *spi)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(spi->master);
	u32 val1, val2;

	/*
	 * clock setting
	 * CKPHS    capture timing. 0:rising edge, 1:falling edge
	 * CKINIT   clock initial level. 0:low, 1:high
	 * CKDLY    clock delay. 0:no delay, 1:delay depending on FSTRT
	 *          (FSTRT=0: 1 clock, FSTRT=1: 0.5 clock)
	 *
	 * frame setting
	 * FSPOL    frame signal porarity. 0: low, 1: high
	 * FSTRT    start frame timing
	 *          0: rising edge of clock, 1: falling edge of clock
	 */
	switch (spi->mode & SPI_MODE_X_MASK) {
	case SPI_MODE_0:
		/* CKPHS=1, CKINIT=0, CKDLY=1, FSTRT=0 */
		val1 = SSI_CKS_CKPHS | SSI_CKS_CKDLY;
		val2 = 0;
		break;
	case SPI_MODE_1:
		/* CKPHS=0, CKINIT=0, CKDLY=0, FSTRT=1 */
		val1 = 0;
		val2 = SSI_FPS_FSTRT;
		break;
	case SPI_MODE_2:
		/* CKPHS=0, CKINIT=1, CKDLY=1, FSTRT=1 */
		val1 = SSI_CKS_CKINIT | SSI_CKS_CKDLY;
		val2 = SSI_FPS_FSTRT;
		break;
	case SPI_MODE_3:
		/* CKPHS=1, CKINIT=1, CKDLY=0, FSTRT=0 */
		val1 = SSI_CKS_CKPHS | SSI_CKS_CKINIT;
		val2 = 0;
		break;
	}

	if (!(spi->mode & SPI_CS_HIGH))
		val2 |= SSI_FPS_FSPOL;

	writel(val1, priv->base + SSI_CKS);
	writel(val2, priv->base + SSI_FPS);

	val1 = 0;
	if (spi->mode & SPI_LSB_FIRST)
		val1 |= FIELD_PREP(SSI_TXWDS_TDTF_MASK, 1);
	writel(val1, priv->base + SSI_TXWDS);
	writel(val1, priv->base + SSI_RXWDS);
}

static void uniphier_spi_set_transfer_size(struct spi_device *spi, int size)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(spi->master);
	u32 val;

	val = readl(priv->base + SSI_TXWDS);
	val &= ~(SSI_TXWDS_WDLEN_MASK | SSI_TXWDS_DTLEN_MASK);
	val |= FIELD_PREP(SSI_TXWDS_WDLEN_MASK, size);
	val |= FIELD_PREP(SSI_TXWDS_DTLEN_MASK, size);
	writel(val, priv->base + SSI_TXWDS);

	val = readl(priv->base + SSI_RXWDS);
	val &= ~SSI_RXWDS_DTLEN_MASK;
	val |= FIELD_PREP(SSI_RXWDS_DTLEN_MASK, size);
	writel(val, priv->base + SSI_RXWDS);
}

static void uniphier_spi_set_baudrate(struct spi_device *spi,
				      unsigned int speed)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(spi->master);
	u32 val, ckdiv;

	/*
	 * the supported rates are even numbers from 4 to 254. (4,6,8...254)
	 * round up as we look for equal or less speed
	 */
	ckdiv = DIV_ROUND_UP(clk_get_rate(priv->clk), speed);
	ckdiv = round_up(ckdiv, 2);

	val = readl(priv->base + SSI_CKS);
	val &= ~SSI_CKS_CKRAT_MASK;
	val |= ckdiv & SSI_CKS_CKRAT_MASK;
	writel(val, priv->base + SSI_CKS);
}

static void uniphier_spi_setup_transfer(struct spi_device *spi,
				       struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(spi->master);
	u32 val;

	priv->error = 0;
	priv->tx_buf = t->tx_buf;
	priv->rx_buf = t->rx_buf;
	priv->tx_bytes = priv->rx_bytes = t->len;

	if (!priv->is_save_param || priv->mode != spi->mode) {
		uniphier_spi_set_mode(spi);
		priv->mode = spi->mode;
		priv->is_save_param = false;
	}

	if (!priv->is_save_param || priv->bits_per_word != t->bits_per_word) {
		uniphier_spi_set_transfer_size(spi, t->bits_per_word);
		priv->bits_per_word = t->bits_per_word;
	}

	if (!priv->is_save_param || priv->speed_hz != t->speed_hz) {
		uniphier_spi_set_baudrate(spi, t->speed_hz);
		priv->speed_hz = t->speed_hz;
	}

	priv->is_save_param = true;

	/* reset FIFOs */
	val = SSI_FC_TXFFL | SSI_FC_RXFFL;
	writel(val, priv->base + SSI_FC);
}

static void uniphier_spi_send(struct uniphier_spi_priv *priv)
{
	int wsize;
	u32 val = 0;

	wsize = min(bytes_per_word(priv->bits_per_word), priv->tx_bytes);
	priv->tx_bytes -= wsize;

	if (priv->tx_buf) {
		switch (wsize) {
		case 1:
			val = *priv->tx_buf;
			break;
		case 2:
			val = get_unaligned_le16(priv->tx_buf);
			break;
		case 4:
			val = get_unaligned_le32(priv->tx_buf);
			break;
		}

		priv->tx_buf += wsize;
	}

	writel(val, priv->base + SSI_TXDR);
}

static void uniphier_spi_recv(struct uniphier_spi_priv *priv)
{
	int rsize;
	u32 val;

	rsize = min(bytes_per_word(priv->bits_per_word), priv->rx_bytes);
	priv->rx_bytes -= rsize;

	val = readl(priv->base + SSI_RXDR);

	if (priv->rx_buf) {
		switch (rsize) {
		case 1:
			*priv->rx_buf = val;
			break;
		case 2:
			put_unaligned_le16(val, priv->rx_buf);
			break;
		case 4:
			put_unaligned_le32(val, priv->rx_buf);
			break;
		}

		priv->rx_buf += rsize;
	}
}

static void uniphier_spi_set_fifo_threshold(struct uniphier_spi_priv *priv,
					    unsigned int threshold)
{
	u32 val;

	val = readl(priv->base + SSI_FC);
	val &= ~(SSI_FC_TXFTH_MASK | SSI_FC_RXFTH_MASK);
	val |= FIELD_PREP(SSI_FC_TXFTH_MASK, SSI_FIFO_DEPTH - threshold);
	val |= FIELD_PREP(SSI_FC_RXFTH_MASK, threshold);
	writel(val, priv->base + SSI_FC);
}

static void uniphier_spi_fill_tx_fifo(struct uniphier_spi_priv *priv)
{
	unsigned int fifo_threshold, fill_words;
	unsigned int bpw = bytes_per_word(priv->bits_per_word);

	fifo_threshold = DIV_ROUND_UP(priv->rx_bytes, bpw);
	fifo_threshold = min(fifo_threshold, SSI_FIFO_DEPTH);

	uniphier_spi_set_fifo_threshold(priv, fifo_threshold);

	fill_words = fifo_threshold -
		DIV_ROUND_UP(priv->rx_bytes - priv->tx_bytes, bpw);

	while (fill_words--)
		uniphier_spi_send(priv);
}

static void uniphier_spi_set_cs(struct spi_device *spi, bool enable)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(spi->master);
	u32 val;

	val = readl(priv->base + SSI_FPS);

	if (enable)
		val |= SSI_FPS_FSPOL;
	else
		val &= ~SSI_FPS_FSPOL;

	writel(val, priv->base + SSI_FPS);
}

static bool uniphier_spi_can_dma(struct spi_master *master,
				 struct spi_device *spi,
				 struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	unsigned int bpw = bytes_per_word(priv->bits_per_word);

	if ((!master->dma_tx && !master->dma_rx)
	    || (!master->dma_tx && t->tx_buf)
	    || (!master->dma_rx && t->rx_buf))
		return false;

	return DIV_ROUND_UP(t->len, bpw) > SSI_FIFO_DEPTH;
}

static void uniphier_spi_dma_rxcb(void *data)
{
	struct spi_master *master = data;
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	int state = atomic_fetch_andnot(SSI_DMA_RX_BUSY, &priv->dma_busy);

	uniphier_spi_irq_disable(priv, SSI_IE_RXRE);

	if (!(state & SSI_DMA_TX_BUSY))
		spi_finalize_current_transfer(master);
}

static void uniphier_spi_dma_txcb(void *data)
{
	struct spi_master *master = data;
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	int state = atomic_fetch_andnot(SSI_DMA_TX_BUSY, &priv->dma_busy);

	uniphier_spi_irq_disable(priv, SSI_IE_TXRE);

	if (!(state & SSI_DMA_RX_BUSY))
		spi_finalize_current_transfer(master);
}

static int uniphier_spi_transfer_one_dma(struct spi_master *master,
					 struct spi_device *spi,
					 struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	struct dma_async_tx_descriptor *rxdesc = NULL, *txdesc = NULL;
	int buswidth;

	atomic_set(&priv->dma_busy, 0);

	uniphier_spi_set_fifo_threshold(priv, SSI_FIFO_BURST_NUM);

	if (priv->bits_per_word <= 8)
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
	else if (priv->bits_per_word <= 16)
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
	else
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;

	if (priv->rx_buf) {
		struct dma_slave_config rxconf = {
			.direction = DMA_DEV_TO_MEM,
			.src_addr = priv->base_dma_addr + SSI_RXDR,
			.src_addr_width = buswidth,
			.src_maxburst = SSI_FIFO_BURST_NUM,
		};

		dmaengine_slave_config(master->dma_rx, &rxconf);

		rxdesc = dmaengine_prep_slave_sg(
			master->dma_rx,
			t->rx_sg.sgl, t->rx_sg.nents,
			DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!rxdesc)
			goto out_err_prep;

		rxdesc->callback = uniphier_spi_dma_rxcb;
		rxdesc->callback_param = master;

		uniphier_spi_irq_enable(priv, SSI_IE_RXRE);
		atomic_or(SSI_DMA_RX_BUSY, &priv->dma_busy);

		dmaengine_submit(rxdesc);
		dma_async_issue_pending(master->dma_rx);
	}

	if (priv->tx_buf) {
		struct dma_slave_config txconf = {
			.direction = DMA_MEM_TO_DEV,
			.dst_addr = priv->base_dma_addr + SSI_TXDR,
			.dst_addr_width = buswidth,
			.dst_maxburst = SSI_FIFO_BURST_NUM,
		};

		dmaengine_slave_config(master->dma_tx, &txconf);

		txdesc = dmaengine_prep_slave_sg(
			master->dma_tx,
			t->tx_sg.sgl, t->tx_sg.nents,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!txdesc)
			goto out_err_prep;

		txdesc->callback = uniphier_spi_dma_txcb;
		txdesc->callback_param = master;

		uniphier_spi_irq_enable(priv, SSI_IE_TXRE);
		atomic_or(SSI_DMA_TX_BUSY, &priv->dma_busy);

		dmaengine_submit(txdesc);
		dma_async_issue_pending(master->dma_tx);
	}

	/* signal that we need to wait for completion */
	return (priv->tx_buf || priv->rx_buf);

out_err_prep:
	if (rxdesc)
		dmaengine_terminate_sync(master->dma_rx);

	return -EINVAL;
}

static int uniphier_spi_transfer_one_irq(struct spi_master *master,
					 struct spi_device *spi,
					 struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	struct device *dev = master->dev.parent;
	unsigned long time_left;

	reinit_completion(&priv->xfer_done);

	uniphier_spi_fill_tx_fifo(priv);

	uniphier_spi_irq_enable(priv, SSI_IE_RCIE | SSI_IE_RORIE);

	time_left = wait_for_completion_timeout(&priv->xfer_done,
					msecs_to_jiffies(SSI_TIMEOUT_MS));

	uniphier_spi_irq_disable(priv, SSI_IE_RCIE | SSI_IE_RORIE);

	if (!time_left) {
		dev_err(dev, "transfer timeout.\n");
		return -ETIMEDOUT;
	}

	return priv->error;
}

static int uniphier_spi_transfer_one_poll(struct spi_master *master,
					  struct spi_device *spi,
					  struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	int loop = SSI_POLL_TIMEOUT_US * 10;

	while (priv->tx_bytes) {
		uniphier_spi_fill_tx_fifo(priv);

		while ((priv->rx_bytes - priv->tx_bytes) > 0) {
			while (!(readl(priv->base + SSI_SR) & SSI_SR_RNE)
								&& loop--)
				ndelay(100);

			if (loop == -1)
				goto irq_transfer;

			uniphier_spi_recv(priv);
		}
	}

	return 0;

irq_transfer:
	return uniphier_spi_transfer_one_irq(master, spi, t);
}

static int uniphier_spi_transfer_one(struct spi_master *master,
				     struct spi_device *spi,
				     struct spi_transfer *t)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	unsigned long threshold;
	bool use_dma;

	/* Terminate and return success for 0 byte length transfer */
	if (!t->len)
		return 0;

	uniphier_spi_setup_transfer(spi, t);

	use_dma = master->can_dma ? master->can_dma(master, spi, t) : false;
	if (use_dma)
		return uniphier_spi_transfer_one_dma(master, spi, t);

	/*
	 * If the transfer operation will take longer than
	 * SSI_POLL_TIMEOUT_US, it should use irq.
	 */
	threshold = DIV_ROUND_UP(SSI_POLL_TIMEOUT_US * priv->speed_hz,
					USEC_PER_SEC * BITS_PER_BYTE);
	if (t->len > threshold)
		return uniphier_spi_transfer_one_irq(master, spi, t);
	else
		return uniphier_spi_transfer_one_poll(master, spi, t);
}

static int uniphier_spi_prepare_transfer_hardware(struct spi_master *master)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);

	writel(SSI_CTL_EN, priv->base + SSI_CTL);

	return 0;
}

static int uniphier_spi_unprepare_transfer_hardware(struct spi_master *master)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);

	writel(0, priv->base + SSI_CTL);

	return 0;
}

static void uniphier_spi_handle_err(struct spi_master *master,
				    struct spi_message *msg)
{
	struct uniphier_spi_priv *priv = spi_master_get_devdata(master);
	u32 val;

	/* stop running spi transfer */
	writel(0, priv->base + SSI_CTL);

	/* reset FIFOs */
	val = SSI_FC_TXFFL | SSI_FC_RXFFL;
	writel(val, priv->base + SSI_FC);

	uniphier_spi_irq_disable(priv, SSI_IE_ALL_MASK);

	if (atomic_read(&priv->dma_busy) & SSI_DMA_TX_BUSY) {
		dmaengine_terminate_async(master->dma_tx);
		atomic_andnot(SSI_DMA_TX_BUSY, &priv->dma_busy);
	}

	if (atomic_read(&priv->dma_busy) & SSI_DMA_RX_BUSY) {
		dmaengine_terminate_async(master->dma_rx);
		atomic_andnot(SSI_DMA_RX_BUSY, &priv->dma_busy);
	}
}

static irqreturn_t uniphier_spi_handler(int irq, void *dev_id)
{
	struct uniphier_spi_priv *priv = dev_id;
	u32 val, stat;

	stat = readl(priv->base + SSI_IS);
	val = SSI_IC_TCIC | SSI_IC_RCIC | SSI_IC_RORIC;
	writel(val, priv->base + SSI_IC);

	/* rx fifo overrun */
	if (stat & SSI_IS_RORID) {
		priv->error = -EIO;
		goto done;
	}

	/* rx complete */
	if ((stat & SSI_IS_RCID) && (stat & SSI_IS_RXRS)) {
		while ((readl(priv->base + SSI_SR) & SSI_SR_RNE) &&
				(priv->rx_bytes - priv->tx_bytes) > 0)
			uniphier_spi_recv(priv);

		if ((readl(priv->base + SSI_SR) & SSI_SR_RNE) ||
				(priv->rx_bytes != priv->tx_bytes)) {
			priv->error = -EIO;
			goto done;
		} else if (priv->rx_bytes == 0)
			goto done;

		/* next tx transfer */
		uniphier_spi_fill_tx_fifo(priv);

		return IRQ_HANDLED;
	}

	return IRQ_NONE;

done:
	complete(&priv->xfer_done);
	return IRQ_HANDLED;
}

static int uniphier_spi_probe(struct platform_device *pdev)
{
	struct uniphier_spi_priv *priv;
	struct spi_master *master;
	struct resource *res;
	struct dma_slave_caps caps;
	u32 dma_tx_burst = 0, dma_rx_burst = 0;
	unsigned long clk_rate;
	int irq;
	int ret;

	master = spi_alloc_master(&pdev->dev, sizeof(*priv));
	if (!master)
		return -ENOMEM;

	platform_set_drvdata(pdev, master);

	priv = spi_master_get_devdata(master);
	priv->master = master;
	priv->is_save_param = false;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out_master_put;
	}
	priv->base_dma_addr = res->start;

	priv->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "failed to get clock\n");
		ret = PTR_ERR(priv->clk);
		goto out_master_put;
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret)
		goto out_master_put;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto out_disable_clk;
	}

	ret = devm_request_irq(&pdev->dev, irq, uniphier_spi_handler,
			       0, "uniphier-spi", priv);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto out_disable_clk;
	}

	init_completion(&priv->xfer_done);

	clk_rate = clk_get_rate(priv->clk);

	master->max_speed_hz = DIV_ROUND_UP(clk_rate, SSI_MIN_CLK_DIVIDER);
	master->min_speed_hz = DIV_ROUND_UP(clk_rate, SSI_MAX_CLK_DIVIDER);
	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
	master->dev.of_node = pdev->dev.of_node;
	master->bus_num = pdev->id;
	master->bits_per_word_mask = SPI_BPW_RANGE_MASK(1, 32);

	master->set_cs = uniphier_spi_set_cs;
	master->transfer_one = uniphier_spi_transfer_one;
	master->prepare_transfer_hardware
				= uniphier_spi_prepare_transfer_hardware;
	master->unprepare_transfer_hardware
				= uniphier_spi_unprepare_transfer_hardware;
	master->handle_err = uniphier_spi_handle_err;
	master->can_dma = uniphier_spi_can_dma;

	master->num_chipselect = 1;
	master->flags = SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX;

	master->dma_tx = dma_request_chan(&pdev->dev, "tx");
	if (IS_ERR_OR_NULL(master->dma_tx)) {
		if (PTR_ERR(master->dma_tx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto out_disable_clk;
		}
		master->dma_tx = NULL;
		dma_tx_burst = INT_MAX;
	} else {
		ret = dma_get_slave_caps(master->dma_tx, &caps);
		if (ret) {
			dev_err(&pdev->dev, "failed to get TX DMA capacities: %d\n",
				ret);
			goto out_disable_clk;
		}
		dma_tx_burst = caps.max_burst;
	}

	master->dma_rx = dma_request_chan(&pdev->dev, "rx");
	if (IS_ERR_OR_NULL(master->dma_rx)) {
		if (PTR_ERR(master->dma_rx) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			goto out_disable_clk;
		}
		master->dma_rx = NULL;
		dma_rx_burst = INT_MAX;
	} else {
		ret = dma_get_slave_caps(master->dma_rx, &caps);
		if (ret) {
			dev_err(&pdev->dev, "failed to get RX DMA capacities: %d\n",
				ret);
			goto out_disable_clk;
		}
		dma_rx_burst = caps.max_burst;
	}

	master->max_dma_len = min(dma_tx_burst, dma_rx_burst);

	ret = devm_spi_register_master(&pdev->dev, master);
	if (ret)
		goto out_disable_clk;

	return 0;

out_disable_clk:
	clk_disable_unprepare(priv->clk);

out_master_put:
	spi_master_put(master);
	return ret;
}

static int uniphier_spi_remove(struct platform_device *pdev)
{
	struct uniphier_spi_priv *priv = platform_get_drvdata(pdev);

	if (priv->master->dma_tx)
		dma_release_channel(priv->master->dma_tx);
	if (priv->master->dma_rx)
		dma_release_channel(priv->master->dma_rx);

	clk_disable_unprepare(priv->clk);

	return 0;
}

static const struct of_device_id uniphier_spi_match[] = {
	{ .compatible = "socionext,uniphier-scssi" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, uniphier_spi_match);

static struct platform_driver uniphier_spi_driver = {
	.probe = uniphier_spi_probe,
	.remove = uniphier_spi_remove,
	.driver = {
		.name = "uniphier-spi",
		.of_match_table = uniphier_spi_match,
	},
};
module_platform_driver(uniphier_spi_driver);

MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_AUTHOR("Keiji Hayashibara <hayashibara.keiji@socionext.com>");
MODULE_DESCRIPTION("Socionext UniPhier SPI controller driver");
MODULE_LICENSE("GPL v2");
