// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Leilk Liu <leilk.liu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/platform_data/spi-mt65xx.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/dma-mapping.h>
#include <linux/pm_qos.h>

#define SPI_CFG0_REG			0x0000
#define SPI_CFG1_REG			0x0004
#define SPI_TX_SRC_REG			0x0008
#define SPI_RX_DST_REG			0x000c
#define SPI_TX_DATA_REG			0x0010
#define SPI_RX_DATA_REG			0x0014
#define SPI_CMD_REG			0x0018
#define SPI_STATUS0_REG			0x001c
#define SPI_PAD_SEL_REG			0x0024
#define SPI_CFG2_REG			0x0028
#define SPI_TX_SRC_REG_64		0x002c
#define SPI_RX_DST_REG_64		0x0030
#define SPI_CFG3_IPM_REG		0x0040

#define SPI_CFG0_SCK_HIGH_OFFSET	0
#define SPI_CFG0_SCK_LOW_OFFSET		8
#define SPI_CFG0_CS_HOLD_OFFSET		16
#define SPI_CFG0_CS_SETUP_OFFSET	24
#define SPI_ADJUST_CFG0_CS_HOLD_OFFSET	0
#define SPI_ADJUST_CFG0_CS_SETUP_OFFSET	16

#define SPI_CFG1_CS_IDLE_OFFSET		0
#define SPI_CFG1_PACKET_LOOP_OFFSET	8
#define SPI_CFG1_PACKET_LENGTH_OFFSET	16
#define SPI_CFG1_GET_TICK_DLY_OFFSET	29
#define SPI_CFG1_GET_TICK_DLY_OFFSET_V1	30

#define SPI_CFG1_GET_TICK_DLY_MASK	0xe0000000
#define SPI_CFG1_GET_TICK_DLY_MASK_V1	0xc0000000

#define SPI_CFG1_CS_IDLE_MASK		0xff
#define SPI_CFG1_PACKET_LOOP_MASK	0xff00
#define SPI_CFG1_PACKET_LENGTH_MASK	0x3ff0000
#define SPI_CFG1_IPM_PACKET_LENGTH_MASK	GENMASK(31, 16)
#define SPI_CFG2_SCK_HIGH_OFFSET	0
#define SPI_CFG2_SCK_LOW_OFFSET		16

#define SPI_CMD_ACT			BIT(0)
#define SPI_CMD_RESUME			BIT(1)
#define SPI_CMD_RST			BIT(2)
#define SPI_CMD_PAUSE_EN		BIT(4)
#define SPI_CMD_DEASSERT		BIT(5)
#define SPI_CMD_SAMPLE_SEL		BIT(6)
#define SPI_CMD_CS_POL			BIT(7)
#define SPI_CMD_CPHA			BIT(8)
#define SPI_CMD_CPOL			BIT(9)
#define SPI_CMD_RX_DMA			BIT(10)
#define SPI_CMD_TX_DMA			BIT(11)
#define SPI_CMD_TXMSBF			BIT(12)
#define SPI_CMD_RXMSBF			BIT(13)
#define SPI_CMD_RX_ENDIAN		BIT(14)
#define SPI_CMD_TX_ENDIAN		BIT(15)
#define SPI_CMD_FINISH_IE		BIT(16)
#define SPI_CMD_PAUSE_IE		BIT(17)
#define SPI_CMD_IPM_NONIDLE_MODE	BIT(19)
#define SPI_CMD_IPM_SPIM_LOOP		BIT(21)
#define SPI_CMD_IPM_GET_TICKDLY_OFFSET	22

#define SPI_CMD_IPM_GET_TICKDLY_MASK	GENMASK(24, 22)

#define PIN_MODE_CFG(x)	((x) / 2)

#define SPI_CFG3_IPM_HALF_DUPLEX_DIR	BIT(2)
#define SPI_CFG3_IPM_HALF_DUPLEX_EN	BIT(3)
#define SPI_CFG3_IPM_XMODE_EN		BIT(4)
#define SPI_CFG3_IPM_NODATA_FLAG	BIT(5)
#define SPI_CFG3_IPM_CMD_BYTELEN_OFFSET	8
#define SPI_CFG3_IPM_ADDR_BYTELEN_OFFSET 12

#define SPI_CFG3_IPM_CMD_PIN_MODE_MASK	GENMASK(1, 0)
#define SPI_CFG3_IPM_CMD_BYTELEN_MASK	GENMASK(11, 8)
#define SPI_CFG3_IPM_ADDR_BYTELEN_MASK	GENMASK(15, 12)

#define MT8173_SPI_MAX_PAD_SEL		3

#define MTK_SPI_PAUSE_INT_STATUS	0x2

#define MTK_SPI_MAX_FIFO_SIZE		32U
#define MTK_SPI_PACKET_SIZE		1024
#define MTK_SPI_IPM_PACKET_SIZE		SZ_64K
#define MTK_SPI_IPM_PACKET_LOOP		SZ_256

#define MTK_SPI_IDLE			0
#define MTK_SPI_PAUSED			1

#define MTK_SPI_32BITS_MASK		(0xffffffff)

#define DMA_ADDR_EXT_BITS		(36)
#define DMA_ADDR_DEF_BITS		(32)

/**
 * struct mtk_spi_compatible - device data structure
 * @need_pad_sel:	Enable pad (pins) selection in SPI controller
 * @must_tx:		Must explicitly send dummy TX bytes to do RX only transfer
 * @enhance_timing:	Enable adjusting cfg register to enhance time accuracy
 * @dma_ext:		DMA address extension supported
 * @no_need_unprepare:	Don't unprepare the SPI clk during runtime
 * @ipm_design:		Adjust/extend registers to support IPM design IP features
 */
struct mtk_spi_compatible {
	bool need_pad_sel;
	bool must_tx;
	bool enhance_timing;
	bool dma_ext;
	bool no_need_unprepare;
	bool ipm_design;
};

/**
 * struct mtk_spi - SPI driver instance
 * @base:		Start address of the SPI controller registers
 * @state:		SPI controller state
 * @pad_num:		Number of pad_sel entries
 * @pad_sel:		Groups of pins to select
 * @parent_clk:		Parent of sel_clk
 * @sel_clk:		SPI host mux clock
 * @spi_clk:		Peripheral clock
 * @spi_hclk:		AHB bus clock
 * @cur_transfer:	Currently processed SPI transfer
 * @xfer_len:		Number of bytes to transfer
 * @num_xfered:		Number of transferred bytes
 * @tx_sgl:		TX transfer scatterlist
 * @rx_sgl:		RX transfer scatterlist
 * @tx_sgl_len:		Size of TX DMA transfer
 * @rx_sgl_len:		Size of RX DMA transfer
 * @dev_comp:		Device data structure
 * @qos_request:	QoS request
 * @spi_clk_hz:		Current SPI clock in Hz
 * @spimem_done:	SPI-MEM operation completion
 * @use_spimem:		Enables SPI-MEM
 * @dev:		Device pointer
 * @tx_dma:		DMA start for SPI-MEM TX
 * @rx_dma:		DMA start for SPI-MEM RX
 */
struct mtk_spi {
	void __iomem *base;
	u32 state;
	int pad_num;
	u32 *pad_sel;
	struct clk *parent_clk, *sel_clk, *spi_clk, *spi_hclk;
	struct spi_transfer *cur_transfer;
	u32 xfer_len;
	u32 num_xfered;
	struct scatterlist *tx_sgl, *rx_sgl;
	u32 tx_sgl_len, rx_sgl_len;
	const struct mtk_spi_compatible *dev_comp;
	struct pm_qos_request qos_request;
	u32 spi_clk_hz;
	struct completion spimem_done;
	bool use_spimem;
	struct device *dev;
	dma_addr_t tx_dma;
	dma_addr_t rx_dma;
};

static const struct mtk_spi_compatible mtk_common_compat;

static const struct mtk_spi_compatible mt2712_compat = {
	.must_tx = true,
};

static const struct mtk_spi_compatible mtk_ipm_compat = {
	.enhance_timing = true,
	.dma_ext = true,
	.ipm_design = true,
};

static const struct mtk_spi_compatible mt6765_compat = {
	.need_pad_sel = true,
	.must_tx = true,
	.enhance_timing = true,
	.dma_ext = true,
};

static const struct mtk_spi_compatible mt7622_compat = {
	.must_tx = true,
	.enhance_timing = true,
};

static const struct mtk_spi_compatible mt8173_compat = {
	.need_pad_sel = true,
	.must_tx = true,
};

static const struct mtk_spi_compatible mt8183_compat = {
	.need_pad_sel = true,
	.must_tx = true,
	.enhance_timing = true,
};

static const struct mtk_spi_compatible mt6893_compat = {
	.need_pad_sel = true,
	.must_tx = true,
	.enhance_timing = true,
	.dma_ext = true,
	.no_need_unprepare = true,
};

static const struct mtk_spi_compatible mt6991_compat = {
	.need_pad_sel = true,
	.must_tx = true,
	.enhance_timing = true,
	.dma_ext = true,
	.ipm_design = true,
};

/*
 * A piece of default chip info unless the platform
 * supplies it.
 */
static const struct mtk_chip_config mtk_default_chip_info = {
	.sample_sel = 0,
	.tick_delay = 0,
};

static const struct of_device_id mtk_spi_of_match[] = {
	{ .compatible = "mediatek,spi-ipm",
		.data = (void *)&mtk_ipm_compat,
	},
	{ .compatible = "mediatek,mt2701-spi",
		.data = (void *)&mtk_common_compat,
	},
	{ .compatible = "mediatek,mt2712-spi",
		.data = (void *)&mt2712_compat,
	},
	{ .compatible = "mediatek,mt6589-spi",
		.data = (void *)&mtk_common_compat,
	},
	{ .compatible = "mediatek,mt6765-spi",
		.data = (void *)&mt6765_compat,
	},
	{ .compatible = "mediatek,mt6991-spi",
		.data = (void *)&mt6991_compat,
	},
	{ .compatible = "mediatek,mt7622-spi",
		.data = (void *)&mt7622_compat,
	},
	{ .compatible = "mediatek,mt7629-spi",
		.data = (void *)&mt7622_compat,
	},
	{ .compatible = "mediatek,mt8135-spi",
		.data = (void *)&mtk_common_compat,
	},
	{ .compatible = "mediatek,mt8173-spi",
		.data = (void *)&mt8173_compat,
	},
	{ .compatible = "mediatek,mt8183-spi",
		.data = (void *)&mt8183_compat,
	},
	{ .compatible = "mediatek,mt8192-spi",
		.data = (void *)&mt6765_compat,
	},
	{ .compatible = "mediatek,mt6893-spi",
		.data = (void *)&mt6893_compat,
	},
	{}
};
MODULE_DEVICE_TABLE(of, mtk_spi_of_match);

static void mtk_spi_reset(struct mtk_spi *mdata)
{
	u32 reg_val;

	/* set the software reset bit in SPI_CMD_REG. */
	reg_val = readl(mdata->base + SPI_CMD_REG);
	reg_val |= SPI_CMD_RST;
	writel(reg_val, mdata->base + SPI_CMD_REG);

	reg_val = readl(mdata->base + SPI_CMD_REG);
	reg_val &= ~SPI_CMD_RST;
	writel(reg_val, mdata->base + SPI_CMD_REG);
}

static int mtk_spi_set_hw_cs_timing(struct spi_device *spi)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(spi->controller);
	struct spi_delay *cs_setup = &spi->cs_setup;
	struct spi_delay *cs_hold = &spi->cs_hold;
	struct spi_delay *cs_inactive = &spi->cs_inactive;
	u32 setup, hold, inactive;
	u32 reg_val;
	int delay;

	delay = spi_delay_to_ns(cs_setup, NULL);
	if (delay < 0)
		return delay;
	setup = (delay * DIV_ROUND_UP(mdata->spi_clk_hz, 1000000)) / 1000;

	delay = spi_delay_to_ns(cs_hold, NULL);
	if (delay < 0)
		return delay;
	hold = (delay * DIV_ROUND_UP(mdata->spi_clk_hz, 1000000)) / 1000;

	delay = spi_delay_to_ns(cs_inactive, NULL);
	if (delay < 0)
		return delay;
	inactive = (delay * DIV_ROUND_UP(mdata->spi_clk_hz, 1000000)) / 1000;

	if (hold || setup) {
		reg_val = readl(mdata->base + SPI_CFG0_REG);
		if (mdata->dev_comp->enhance_timing) {
			if (hold) {
				hold = min_t(u32, hold, 0x10000);
				reg_val &= ~(0xffff << SPI_ADJUST_CFG0_CS_HOLD_OFFSET);
				reg_val |= (((hold - 1) & 0xffff)
					<< SPI_ADJUST_CFG0_CS_HOLD_OFFSET);
			}
			if (setup) {
				setup = min_t(u32, setup, 0x10000);
				reg_val &= ~(0xffff << SPI_ADJUST_CFG0_CS_SETUP_OFFSET);
				reg_val |= (((setup - 1) & 0xffff)
					<< SPI_ADJUST_CFG0_CS_SETUP_OFFSET);
			}
		} else {
			if (hold) {
				hold = min_t(u32, hold, 0x100);
				reg_val &= ~(0xff << SPI_CFG0_CS_HOLD_OFFSET);
				reg_val |= (((hold - 1) & 0xff) << SPI_CFG0_CS_HOLD_OFFSET);
			}
			if (setup) {
				setup = min_t(u32, setup, 0x100);
				reg_val &= ~(0xff << SPI_CFG0_CS_SETUP_OFFSET);
				reg_val |= (((setup - 1) & 0xff)
					<< SPI_CFG0_CS_SETUP_OFFSET);
			}
		}
		writel(reg_val, mdata->base + SPI_CFG0_REG);
	}

	if (inactive) {
		inactive = min_t(u32, inactive, 0x100);
		reg_val = readl(mdata->base + SPI_CFG1_REG);
		reg_val &= ~SPI_CFG1_CS_IDLE_MASK;
		reg_val |= (((inactive - 1) & 0xff) << SPI_CFG1_CS_IDLE_OFFSET);
		writel(reg_val, mdata->base + SPI_CFG1_REG);
	}

	return 0;
}

static int mtk_spi_hw_init(struct spi_controller *host,
			   struct spi_device *spi)
{
	u16 cpha, cpol;
	u32 reg_val;
	struct mtk_chip_config *chip_config = spi->controller_data;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	cpu_latency_qos_update_request(&mdata->qos_request, 500);
	cpha = spi->mode & SPI_CPHA ? 1 : 0;
	cpol = spi->mode & SPI_CPOL ? 1 : 0;

	reg_val = readl(mdata->base + SPI_CMD_REG);
	if (mdata->dev_comp->ipm_design) {
		/* SPI transfer without idle time until packet length done */
		reg_val |= SPI_CMD_IPM_NONIDLE_MODE;
		if (spi->mode & SPI_LOOP)
			reg_val |= SPI_CMD_IPM_SPIM_LOOP;
		else
			reg_val &= ~SPI_CMD_IPM_SPIM_LOOP;
	}

	if (cpha)
		reg_val |= SPI_CMD_CPHA;
	else
		reg_val &= ~SPI_CMD_CPHA;
	if (cpol)
		reg_val |= SPI_CMD_CPOL;
	else
		reg_val &= ~SPI_CMD_CPOL;

	/* set the mlsbx and mlsbtx */
	if (spi->mode & SPI_LSB_FIRST) {
		reg_val &= ~SPI_CMD_TXMSBF;
		reg_val &= ~SPI_CMD_RXMSBF;
	} else {
		reg_val |= SPI_CMD_TXMSBF;
		reg_val |= SPI_CMD_RXMSBF;
	}

	/* set the tx/rx endian */
#ifdef __LITTLE_ENDIAN
	reg_val &= ~SPI_CMD_TX_ENDIAN;
	reg_val &= ~SPI_CMD_RX_ENDIAN;
#else
	reg_val |= SPI_CMD_TX_ENDIAN;
	reg_val |= SPI_CMD_RX_ENDIAN;
#endif

	if (mdata->dev_comp->enhance_timing) {
		/* set CS polarity */
		if (spi->mode & SPI_CS_HIGH)
			reg_val |= SPI_CMD_CS_POL;
		else
			reg_val &= ~SPI_CMD_CS_POL;

		if (chip_config->sample_sel)
			reg_val |= SPI_CMD_SAMPLE_SEL;
		else
			reg_val &= ~SPI_CMD_SAMPLE_SEL;
	}

	/* set finish and pause interrupt always enable */
	reg_val |= SPI_CMD_FINISH_IE | SPI_CMD_PAUSE_IE;

	/* disable dma mode */
	reg_val &= ~(SPI_CMD_TX_DMA | SPI_CMD_RX_DMA);

	/* disable deassert mode */
	reg_val &= ~SPI_CMD_DEASSERT;

	writel(reg_val, mdata->base + SPI_CMD_REG);

	/* pad select */
	if (mdata->dev_comp->need_pad_sel)
		writel(mdata->pad_sel[spi_get_chipselect(spi, 0)],
		       mdata->base + SPI_PAD_SEL_REG);

	/* tick delay */
	if (mdata->dev_comp->enhance_timing) {
		if (mdata->dev_comp->ipm_design) {
			reg_val = readl(mdata->base + SPI_CMD_REG);
			reg_val &= ~SPI_CMD_IPM_GET_TICKDLY_MASK;
			reg_val |= ((chip_config->tick_delay & 0x7)
				    << SPI_CMD_IPM_GET_TICKDLY_OFFSET);
			writel(reg_val, mdata->base + SPI_CMD_REG);
		} else {
			reg_val = readl(mdata->base + SPI_CFG1_REG);
			reg_val &= ~SPI_CFG1_GET_TICK_DLY_MASK;
			reg_val |= ((chip_config->tick_delay & 0x7)
				    << SPI_CFG1_GET_TICK_DLY_OFFSET);
			writel(reg_val, mdata->base + SPI_CFG1_REG);
		}
	} else {
		reg_val = readl(mdata->base + SPI_CFG1_REG);
		reg_val &= ~SPI_CFG1_GET_TICK_DLY_MASK_V1;
		reg_val |= ((chip_config->tick_delay & 0x3)
			    << SPI_CFG1_GET_TICK_DLY_OFFSET_V1);
		writel(reg_val, mdata->base + SPI_CFG1_REG);
	}

	/* set hw cs timing */
	mtk_spi_set_hw_cs_timing(spi);
	return 0;
}

static int mtk_spi_prepare_message(struct spi_controller *host,
				   struct spi_message *msg)
{
	return mtk_spi_hw_init(host, msg->spi);
}

static int mtk_spi_unprepare_message(struct spi_controller *host,
				     struct spi_message *message)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	cpu_latency_qos_update_request(&mdata->qos_request, PM_QOS_DEFAULT_VALUE);
	return 0;
}

static void mtk_spi_set_cs(struct spi_device *spi, bool enable)
{
	u32 reg_val;
	struct mtk_spi *mdata = spi_controller_get_devdata(spi->controller);

	if (spi->mode & SPI_CS_HIGH)
		enable = !enable;

	reg_val = readl(mdata->base + SPI_CMD_REG);
	if (!enable) {
		reg_val |= SPI_CMD_PAUSE_EN;
		writel(reg_val, mdata->base + SPI_CMD_REG);
	} else {
		reg_val &= ~SPI_CMD_PAUSE_EN;
		writel(reg_val, mdata->base + SPI_CMD_REG);
		mdata->state = MTK_SPI_IDLE;
		mtk_spi_reset(mdata);
	}
}

static void mtk_spi_prepare_transfer(struct spi_controller *host,
				     u32 speed_hz)
{
	u32 div, sck_time, reg_val;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	if (speed_hz < mdata->spi_clk_hz / 2)
		div = DIV_ROUND_UP(mdata->spi_clk_hz, speed_hz);
	else
		div = 1;

	sck_time = (div + 1) / 2;

	if (mdata->dev_comp->enhance_timing) {
		reg_val = readl(mdata->base + SPI_CFG2_REG);
		reg_val &= ~(0xffff << SPI_CFG2_SCK_HIGH_OFFSET);
		reg_val |= (((sck_time - 1) & 0xffff)
			   << SPI_CFG2_SCK_HIGH_OFFSET);
		reg_val &= ~(0xffff << SPI_CFG2_SCK_LOW_OFFSET);
		reg_val |= (((sck_time - 1) & 0xffff)
			   << SPI_CFG2_SCK_LOW_OFFSET);
		writel(reg_val, mdata->base + SPI_CFG2_REG);
	} else {
		reg_val = readl(mdata->base + SPI_CFG0_REG);
		reg_val &= ~(0xff << SPI_CFG0_SCK_HIGH_OFFSET);
		reg_val |= (((sck_time - 1) & 0xff)
			   << SPI_CFG0_SCK_HIGH_OFFSET);
		reg_val &= ~(0xff << SPI_CFG0_SCK_LOW_OFFSET);
		reg_val |= (((sck_time - 1) & 0xff) << SPI_CFG0_SCK_LOW_OFFSET);
		writel(reg_val, mdata->base + SPI_CFG0_REG);
	}
}

static void mtk_spi_setup_packet(struct spi_controller *host)
{
	u32 packet_size, packet_loop, reg_val;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	if (mdata->dev_comp->ipm_design)
		packet_size = min_t(u32,
				    mdata->xfer_len,
				    MTK_SPI_IPM_PACKET_SIZE);
	else
		packet_size = min_t(u32,
				    mdata->xfer_len,
				    MTK_SPI_PACKET_SIZE);

	packet_loop = mdata->xfer_len / packet_size;

	reg_val = readl(mdata->base + SPI_CFG1_REG);
	if (mdata->dev_comp->ipm_design)
		reg_val &= ~SPI_CFG1_IPM_PACKET_LENGTH_MASK;
	else
		reg_val &= ~SPI_CFG1_PACKET_LENGTH_MASK;
	reg_val |= (packet_size - 1) << SPI_CFG1_PACKET_LENGTH_OFFSET;
	reg_val &= ~SPI_CFG1_PACKET_LOOP_MASK;
	reg_val |= (packet_loop - 1) << SPI_CFG1_PACKET_LOOP_OFFSET;
	writel(reg_val, mdata->base + SPI_CFG1_REG);
}

inline u32 mtk_spi_set_nbit(u32 nbit)
{
	switch (nbit) {
	default:
		pr_warn_once("unknown nbit mode %u. Falling back to single mode\n",
			     nbit);
		fallthrough;
	case SPI_NBITS_SINGLE:
		return 0x0;
	case SPI_NBITS_DUAL:
		return 0x1;
	case SPI_NBITS_QUAD:
		return 0x2;
	}
}

static void mtk_spi_enable_transfer(struct spi_controller *host)
{
	u32 cmd;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	cmd = readl(mdata->base + SPI_CMD_REG);
	if (mdata->state == MTK_SPI_IDLE)
		cmd |= SPI_CMD_ACT;
	else
		cmd |= SPI_CMD_RESUME;
	writel(cmd, mdata->base + SPI_CMD_REG);
}

static int mtk_spi_get_mult_delta(struct mtk_spi *mdata, u32 xfer_len)
{
	u32 mult_delta = 0;

	if (mdata->dev_comp->ipm_design) {
		if (xfer_len > MTK_SPI_IPM_PACKET_SIZE)
			mult_delta = xfer_len % MTK_SPI_IPM_PACKET_SIZE;
	} else {
		if (xfer_len > MTK_SPI_PACKET_SIZE)
			mult_delta = xfer_len % MTK_SPI_PACKET_SIZE;
	}

	return mult_delta;
}

static void mtk_spi_update_mdata_len(struct spi_controller *host)
{
	int mult_delta;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	if (mdata->tx_sgl_len && mdata->rx_sgl_len) {
		if (mdata->tx_sgl_len > mdata->rx_sgl_len) {
			mult_delta = mtk_spi_get_mult_delta(mdata, mdata->rx_sgl_len);
			mdata->xfer_len = mdata->rx_sgl_len - mult_delta;
			mdata->rx_sgl_len = mult_delta;
			mdata->tx_sgl_len -= mdata->xfer_len;
		} else {
			mult_delta = mtk_spi_get_mult_delta(mdata, mdata->tx_sgl_len);
			mdata->xfer_len = mdata->tx_sgl_len - mult_delta;
			mdata->tx_sgl_len = mult_delta;
			mdata->rx_sgl_len -= mdata->xfer_len;
		}
	} else if (mdata->tx_sgl_len) {
		mult_delta = mtk_spi_get_mult_delta(mdata, mdata->tx_sgl_len);
		mdata->xfer_len = mdata->tx_sgl_len - mult_delta;
		mdata->tx_sgl_len = mult_delta;
	} else if (mdata->rx_sgl_len) {
		mult_delta = mtk_spi_get_mult_delta(mdata, mdata->rx_sgl_len);
		mdata->xfer_len = mdata->rx_sgl_len - mult_delta;
		mdata->rx_sgl_len = mult_delta;
	}
}

static void mtk_spi_setup_dma_addr(struct spi_controller *host,
				   struct spi_transfer *xfer)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	if (mdata->tx_sgl) {
		writel((u32)(xfer->tx_dma & MTK_SPI_32BITS_MASK),
		       mdata->base + SPI_TX_SRC_REG);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		if (mdata->dev_comp->dma_ext)
			writel((u32)(xfer->tx_dma >> 32),
			       mdata->base + SPI_TX_SRC_REG_64);
#endif
	}

	if (mdata->rx_sgl) {
		writel((u32)(xfer->rx_dma & MTK_SPI_32BITS_MASK),
		       mdata->base + SPI_RX_DST_REG);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		if (mdata->dev_comp->dma_ext)
			writel((u32)(xfer->rx_dma >> 32),
			       mdata->base + SPI_RX_DST_REG_64);
#endif
	}
}

static int mtk_spi_fifo_transfer(struct spi_controller *host,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	int cnt, remainder;
	u32 reg_val;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	mdata->cur_transfer = xfer;
	mdata->xfer_len = min(MTK_SPI_MAX_FIFO_SIZE, xfer->len);
	mdata->num_xfered = 0;
	mtk_spi_prepare_transfer(host, xfer->speed_hz);
	mtk_spi_setup_packet(host);

	if (xfer->tx_buf) {
		cnt = xfer->len / 4;
		iowrite32_rep(mdata->base + SPI_TX_DATA_REG, xfer->tx_buf, cnt);
		remainder = xfer->len % 4;
		if (remainder > 0) {
			reg_val = 0;
			memcpy(&reg_val, xfer->tx_buf + (cnt * 4), remainder);
			writel(reg_val, mdata->base + SPI_TX_DATA_REG);
		}
	}

	mtk_spi_enable_transfer(host);

	return 1;
}

static int mtk_spi_dma_transfer(struct spi_controller *host,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	int cmd;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	mdata->tx_sgl = NULL;
	mdata->rx_sgl = NULL;
	mdata->tx_sgl_len = 0;
	mdata->rx_sgl_len = 0;
	mdata->cur_transfer = xfer;
	mdata->num_xfered = 0;

	mtk_spi_prepare_transfer(host, xfer->speed_hz);

	cmd = readl(mdata->base + SPI_CMD_REG);
	if (xfer->tx_buf)
		cmd |= SPI_CMD_TX_DMA;
	if (xfer->rx_buf)
		cmd |= SPI_CMD_RX_DMA;
	writel(cmd, mdata->base + SPI_CMD_REG);

	if (xfer->tx_buf)
		mdata->tx_sgl = xfer->tx_sg.sgl;
	if (xfer->rx_buf)
		mdata->rx_sgl = xfer->rx_sg.sgl;

	if (mdata->tx_sgl) {
		xfer->tx_dma = sg_dma_address(mdata->tx_sgl);
		mdata->tx_sgl_len = sg_dma_len(mdata->tx_sgl);
	}
	if (mdata->rx_sgl) {
		xfer->rx_dma = sg_dma_address(mdata->rx_sgl);
		mdata->rx_sgl_len = sg_dma_len(mdata->rx_sgl);
	}

	mtk_spi_update_mdata_len(host);
	mtk_spi_setup_packet(host);
	mtk_spi_setup_dma_addr(host, xfer);
	mtk_spi_enable_transfer(host);

	return 1;
}

static int mtk_spi_transfer_one(struct spi_controller *host,
				struct spi_device *spi,
				struct spi_transfer *xfer)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(spi->controller);
	u32 reg_val = 0;

	/* prepare xfer direction and duplex mode */
	if (mdata->dev_comp->ipm_design) {
		if (xfer->tx_buf && xfer->rx_buf) {
			reg_val &= ~SPI_CFG3_IPM_HALF_DUPLEX_EN;
		} else if (xfer->tx_buf) {
			reg_val |= SPI_CFG3_IPM_HALF_DUPLEX_EN;
			reg_val &= ~SPI_CFG3_IPM_HALF_DUPLEX_DIR;
			reg_val |= mtk_spi_set_nbit(xfer->tx_nbits);
		} else {
			reg_val |= SPI_CFG3_IPM_HALF_DUPLEX_EN;
			reg_val |= SPI_CFG3_IPM_HALF_DUPLEX_DIR;
			reg_val |= mtk_spi_set_nbit(xfer->rx_nbits);
		}
		writel(reg_val, mdata->base + SPI_CFG3_IPM_REG);
	}

	if (host->can_dma(host, spi, xfer))
		return mtk_spi_dma_transfer(host, spi, xfer);
	else
		return mtk_spi_fifo_transfer(host, spi, xfer);
}

static bool mtk_spi_can_dma(struct spi_controller *host,
			    struct spi_device *spi,
			    struct spi_transfer *xfer)
{
	/* Buffers for DMA transactions must be 4-byte aligned */
	return (xfer->len > MTK_SPI_MAX_FIFO_SIZE &&
		(unsigned long)xfer->tx_buf % 4 == 0 &&
		(unsigned long)xfer->rx_buf % 4 == 0);
}

static int mtk_spi_setup(struct spi_device *spi)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(spi->controller);

	if (!spi->controller_data)
		spi->controller_data = (void *)&mtk_default_chip_info;

	if (mdata->dev_comp->need_pad_sel && spi_get_csgpiod(spi, 0))
		/* CS de-asserted, gpiolib will handle inversion */
		gpiod_direction_output(spi_get_csgpiod(spi, 0), 0);

	return 0;
}

static irqreturn_t mtk_spi_interrupt_thread(int irq, void *dev_id)
{
	u32 cmd, reg_val, cnt, remainder, len;
	struct spi_controller *host = dev_id;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);
	struct spi_transfer *xfer = mdata->cur_transfer;

	if (!host->can_dma(host, NULL, xfer)) {
		if (xfer->rx_buf) {
			cnt = mdata->xfer_len / 4;
			ioread32_rep(mdata->base + SPI_RX_DATA_REG,
				     xfer->rx_buf + mdata->num_xfered, cnt);
			remainder = mdata->xfer_len % 4;
			if (remainder > 0) {
				reg_val = readl(mdata->base + SPI_RX_DATA_REG);
				memcpy(xfer->rx_buf + (cnt * 4) + mdata->num_xfered,
					&reg_val,
					remainder);
			}
		}

		mdata->num_xfered += mdata->xfer_len;
		if (mdata->num_xfered == xfer->len) {
			spi_finalize_current_transfer(host);
			return IRQ_HANDLED;
		}

		len = xfer->len - mdata->num_xfered;
		mdata->xfer_len = min(MTK_SPI_MAX_FIFO_SIZE, len);
		mtk_spi_setup_packet(host);

		if (xfer->tx_buf) {
			cnt = mdata->xfer_len / 4;
			iowrite32_rep(mdata->base + SPI_TX_DATA_REG,
					xfer->tx_buf + mdata->num_xfered, cnt);

			remainder = mdata->xfer_len % 4;
			if (remainder > 0) {
				reg_val = 0;
				memcpy(&reg_val,
					xfer->tx_buf + (cnt * 4) + mdata->num_xfered,
					remainder);
				writel(reg_val, mdata->base + SPI_TX_DATA_REG);
			}
		}

		mtk_spi_enable_transfer(host);

		return IRQ_HANDLED;
	}

	if (mdata->tx_sgl)
		xfer->tx_dma += mdata->xfer_len;
	if (mdata->rx_sgl)
		xfer->rx_dma += mdata->xfer_len;

	if (mdata->tx_sgl && (mdata->tx_sgl_len == 0)) {
		mdata->tx_sgl = sg_next(mdata->tx_sgl);
		if (mdata->tx_sgl) {
			xfer->tx_dma = sg_dma_address(mdata->tx_sgl);
			mdata->tx_sgl_len = sg_dma_len(mdata->tx_sgl);
		}
	}
	if (mdata->rx_sgl && (mdata->rx_sgl_len == 0)) {
		mdata->rx_sgl = sg_next(mdata->rx_sgl);
		if (mdata->rx_sgl) {
			xfer->rx_dma = sg_dma_address(mdata->rx_sgl);
			mdata->rx_sgl_len = sg_dma_len(mdata->rx_sgl);
		}
	}

	if (!mdata->tx_sgl && !mdata->rx_sgl) {
		/* spi disable dma */
		cmd = readl(mdata->base + SPI_CMD_REG);
		cmd &= ~SPI_CMD_TX_DMA;
		cmd &= ~SPI_CMD_RX_DMA;
		writel(cmd, mdata->base + SPI_CMD_REG);

		spi_finalize_current_transfer(host);
		return IRQ_HANDLED;
	}

	mtk_spi_update_mdata_len(host);
	mtk_spi_setup_packet(host);
	mtk_spi_setup_dma_addr(host, xfer);
	mtk_spi_enable_transfer(host);

	return IRQ_HANDLED;
}

static irqreturn_t mtk_spi_interrupt(int irq, void *dev_id)
{
	struct spi_controller *host = dev_id;
	struct mtk_spi *mdata = spi_controller_get_devdata(host);
	u32 reg_val;

	reg_val = readl(mdata->base + SPI_STATUS0_REG);
	if (reg_val & MTK_SPI_PAUSE_INT_STATUS)
		mdata->state = MTK_SPI_PAUSED;
	else
		mdata->state = MTK_SPI_IDLE;

	/* SPI-MEM ops */
	if (mdata->use_spimem) {
		complete(&mdata->spimem_done);
		return IRQ_HANDLED;
	}

	return IRQ_WAKE_THREAD;
}

static int mtk_spi_mem_adjust_op_size(struct spi_mem *mem,
				      struct spi_mem_op *op)
{
	int opcode_len;

	if (op->data.dir != SPI_MEM_NO_DATA) {
		opcode_len = 1 + op->addr.nbytes + op->dummy.nbytes;
		if (opcode_len + op->data.nbytes > MTK_SPI_IPM_PACKET_SIZE) {
			op->data.nbytes = MTK_SPI_IPM_PACKET_SIZE - opcode_len;
			/* force data buffer dma-aligned. */
			op->data.nbytes -= op->data.nbytes % 4;
		}
	}

	return 0;
}

static bool mtk_spi_mem_supports_op(struct spi_mem *mem,
				    const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	if (op->addr.nbytes && op->dummy.nbytes &&
	    op->addr.buswidth != op->dummy.buswidth)
		return false;

	if (op->addr.nbytes + op->dummy.nbytes > 16)
		return false;

	if (op->data.nbytes > MTK_SPI_IPM_PACKET_SIZE) {
		if (op->data.nbytes / MTK_SPI_IPM_PACKET_SIZE >
		    MTK_SPI_IPM_PACKET_LOOP ||
		    op->data.nbytes % MTK_SPI_IPM_PACKET_SIZE != 0)
			return false;
	}

	return true;
}

static void mtk_spi_mem_setup_dma_xfer(struct spi_controller *host,
				       const struct spi_mem_op *op)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	writel((u32)(mdata->tx_dma & MTK_SPI_32BITS_MASK),
	       mdata->base + SPI_TX_SRC_REG);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
	if (mdata->dev_comp->dma_ext)
		writel((u32)(mdata->tx_dma >> 32),
		       mdata->base + SPI_TX_SRC_REG_64);
#endif

	if (op->data.dir == SPI_MEM_DATA_IN) {
		writel((u32)(mdata->rx_dma & MTK_SPI_32BITS_MASK),
		       mdata->base + SPI_RX_DST_REG);
#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
		if (mdata->dev_comp->dma_ext)
			writel((u32)(mdata->rx_dma >> 32),
			       mdata->base + SPI_RX_DST_REG_64);
#endif
	}
}

static int mtk_spi_transfer_wait(struct spi_mem *mem,
				 const struct spi_mem_op *op)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(mem->spi->controller);
	/*
	 * For each byte we wait for 8 cycles of the SPI clock.
	 * Since speed is defined in Hz and we want milliseconds,
	 * so it should be 8 * 1000.
	 */
	u64 ms = 8000LL;

	if (op->data.dir == SPI_MEM_NO_DATA)
		ms *= 32; /* prevent we may get 0 for short transfers. */
	else
		ms *= op->data.nbytes;
	ms = div_u64(ms, mem->spi->max_speed_hz);
	ms += ms + 1000; /* 1s tolerance */

	if (ms > UINT_MAX)
		ms = UINT_MAX;

	if (!wait_for_completion_timeout(&mdata->spimem_done,
					 msecs_to_jiffies(ms))) {
		dev_err(mdata->dev, "spi-mem transfer timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int mtk_spi_mem_exec_op(struct spi_mem *mem,
			       const struct spi_mem_op *op)
{
	struct mtk_spi *mdata = spi_controller_get_devdata(mem->spi->controller);
	u32 reg_val, nio, tx_size;
	char *tx_tmp_buf, *rx_tmp_buf;
	int ret = 0;

	mdata->use_spimem = true;
	reinit_completion(&mdata->spimem_done);

	mtk_spi_reset(mdata);
	mtk_spi_hw_init(mem->spi->controller, mem->spi);
	mtk_spi_prepare_transfer(mem->spi->controller, op->max_freq);

	reg_val = readl(mdata->base + SPI_CFG3_IPM_REG);
	/* opcode byte len */
	reg_val &= ~SPI_CFG3_IPM_CMD_BYTELEN_MASK;
	reg_val |= 1 << SPI_CFG3_IPM_CMD_BYTELEN_OFFSET;

	/* addr & dummy byte len */
	reg_val &= ~SPI_CFG3_IPM_ADDR_BYTELEN_MASK;
	if (op->addr.nbytes || op->dummy.nbytes)
		reg_val |= (op->addr.nbytes + op->dummy.nbytes) <<
			    SPI_CFG3_IPM_ADDR_BYTELEN_OFFSET;

	/* data byte len */
	if (op->data.dir == SPI_MEM_NO_DATA) {
		reg_val |= SPI_CFG3_IPM_NODATA_FLAG;
		writel(0, mdata->base + SPI_CFG1_REG);
	} else {
		reg_val &= ~SPI_CFG3_IPM_NODATA_FLAG;
		mdata->xfer_len = op->data.nbytes;
		mtk_spi_setup_packet(mem->spi->controller);
	}

	if (op->addr.nbytes || op->dummy.nbytes) {
		if (op->addr.buswidth == 1 || op->dummy.buswidth == 1)
			reg_val |= SPI_CFG3_IPM_XMODE_EN;
		else
			reg_val &= ~SPI_CFG3_IPM_XMODE_EN;
	}

	if (op->addr.buswidth == 2 ||
	    op->dummy.buswidth == 2 ||
	    op->data.buswidth == 2)
		nio = 2;
	else if (op->addr.buswidth == 4 ||
		 op->dummy.buswidth == 4 ||
		 op->data.buswidth == 4)
		nio = 4;
	else
		nio = 1;

	reg_val &= ~SPI_CFG3_IPM_CMD_PIN_MODE_MASK;
	reg_val |= PIN_MODE_CFG(nio);

	reg_val |= SPI_CFG3_IPM_HALF_DUPLEX_EN;
	if (op->data.dir == SPI_MEM_DATA_IN)
		reg_val |= SPI_CFG3_IPM_HALF_DUPLEX_DIR;
	else
		reg_val &= ~SPI_CFG3_IPM_HALF_DUPLEX_DIR;
	writel(reg_val, mdata->base + SPI_CFG3_IPM_REG);

	tx_size = 1 + op->addr.nbytes + op->dummy.nbytes;
	if (op->data.dir == SPI_MEM_DATA_OUT)
		tx_size += op->data.nbytes;

	tx_size = max_t(u32, tx_size, 32);

	tx_tmp_buf = kzalloc(tx_size, GFP_KERNEL | GFP_DMA);
	if (!tx_tmp_buf) {
		mdata->use_spimem = false;
		return -ENOMEM;
	}

	tx_tmp_buf[0] = op->cmd.opcode;

	if (op->addr.nbytes) {
		int i;

		for (i = 0; i < op->addr.nbytes; i++)
			tx_tmp_buf[i + 1] = op->addr.val >>
					(8 * (op->addr.nbytes - i - 1));
	}

	if (op->dummy.nbytes)
		memset(tx_tmp_buf + op->addr.nbytes + 1,
		       0xff,
		       op->dummy.nbytes);

	if (op->data.nbytes && op->data.dir == SPI_MEM_DATA_OUT)
		memcpy(tx_tmp_buf + op->dummy.nbytes + op->addr.nbytes + 1,
		       op->data.buf.out,
		       op->data.nbytes);

	mdata->tx_dma = dma_map_single(mdata->dev, tx_tmp_buf,
				       tx_size, DMA_TO_DEVICE);
	if (dma_mapping_error(mdata->dev, mdata->tx_dma)) {
		ret = -ENOMEM;
		goto err_exit;
	}

	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (!IS_ALIGNED((size_t)op->data.buf.in, 4)) {
			rx_tmp_buf = kzalloc(op->data.nbytes,
					     GFP_KERNEL | GFP_DMA);
			if (!rx_tmp_buf) {
				ret = -ENOMEM;
				goto unmap_tx_dma;
			}
		} else {
			rx_tmp_buf = op->data.buf.in;
		}

		mdata->rx_dma = dma_map_single(mdata->dev,
					       rx_tmp_buf,
					       op->data.nbytes,
					       DMA_FROM_DEVICE);
		if (dma_mapping_error(mdata->dev, mdata->rx_dma)) {
			ret = -ENOMEM;
			goto kfree_rx_tmp_buf;
		}
	}

	reg_val = readl(mdata->base + SPI_CMD_REG);
	reg_val |= SPI_CMD_TX_DMA;
	if (op->data.dir == SPI_MEM_DATA_IN)
		reg_val |= SPI_CMD_RX_DMA;
	writel(reg_val, mdata->base + SPI_CMD_REG);

	mtk_spi_mem_setup_dma_xfer(mem->spi->controller, op);

	mtk_spi_enable_transfer(mem->spi->controller);

	/* Wait for the interrupt. */
	ret = mtk_spi_transfer_wait(mem, op);
	if (ret)
		goto unmap_rx_dma;

	/* spi disable dma */
	reg_val = readl(mdata->base + SPI_CMD_REG);
	reg_val &= ~SPI_CMD_TX_DMA;
	if (op->data.dir == SPI_MEM_DATA_IN)
		reg_val &= ~SPI_CMD_RX_DMA;
	writel(reg_val, mdata->base + SPI_CMD_REG);

unmap_rx_dma:
	if (op->data.dir == SPI_MEM_DATA_IN) {
		dma_unmap_single(mdata->dev, mdata->rx_dma,
				 op->data.nbytes, DMA_FROM_DEVICE);
		if (!IS_ALIGNED((size_t)op->data.buf.in, 4))
			memcpy(op->data.buf.in, rx_tmp_buf, op->data.nbytes);
	}
kfree_rx_tmp_buf:
	if (op->data.dir == SPI_MEM_DATA_IN &&
	    !IS_ALIGNED((size_t)op->data.buf.in, 4))
		kfree(rx_tmp_buf);
unmap_tx_dma:
	dma_unmap_single(mdata->dev, mdata->tx_dma,
			 tx_size, DMA_TO_DEVICE);
err_exit:
	kfree(tx_tmp_buf);
	mdata->use_spimem = false;

	return ret;
}

static const struct spi_controller_mem_ops mtk_spi_mem_ops = {
	.adjust_op_size = mtk_spi_mem_adjust_op_size,
	.supports_op = mtk_spi_mem_supports_op,
	.exec_op = mtk_spi_mem_exec_op,
};

static const struct spi_controller_mem_caps mtk_spi_mem_caps = {
	.per_op_freq = true,
};

static int mtk_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct mtk_spi *mdata;
	int i, irq, ret, addr_bits;

	host = devm_spi_alloc_host(dev, sizeof(*mdata));
	if (!host)
		return -ENOMEM;

	host->auto_runtime_pm = true;
	host->dev.of_node = dev->of_node;
	host->mode_bits = SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST;

	host->set_cs = mtk_spi_set_cs;
	host->prepare_message = mtk_spi_prepare_message;
	host->unprepare_message = mtk_spi_unprepare_message;
	host->transfer_one = mtk_spi_transfer_one;
	host->can_dma = mtk_spi_can_dma;
	host->setup = mtk_spi_setup;
	host->set_cs_timing = mtk_spi_set_hw_cs_timing;
	host->use_gpio_descriptors = true;

	mdata = spi_controller_get_devdata(host);
	mdata->dev_comp = device_get_match_data(dev);

	if (mdata->dev_comp->enhance_timing)
		host->mode_bits |= SPI_CS_HIGH;

	if (mdata->dev_comp->must_tx)
		host->flags = SPI_CONTROLLER_MUST_TX;
	if (mdata->dev_comp->ipm_design)
		host->mode_bits |= SPI_LOOP | SPI_RX_DUAL | SPI_TX_DUAL |
				   SPI_RX_QUAD | SPI_TX_QUAD;

	if (mdata->dev_comp->ipm_design) {
		mdata->dev = dev;
		host->mem_ops = &mtk_spi_mem_ops;
		host->mem_caps = &mtk_spi_mem_caps;
		init_completion(&mdata->spimem_done);
	}

	if (mdata->dev_comp->need_pad_sel) {
		mdata->pad_num = of_property_count_u32_elems(dev->of_node,
			"mediatek,pad-select");
		if (mdata->pad_num < 0)
			return dev_err_probe(dev, -EINVAL,
				"No 'mediatek,pad-select' property\n");

		mdata->pad_sel = devm_kmalloc_array(dev, mdata->pad_num,
						    sizeof(u32), GFP_KERNEL);
		if (!mdata->pad_sel)
			return -ENOMEM;

		for (i = 0; i < mdata->pad_num; i++) {
			of_property_read_u32_index(dev->of_node,
						   "mediatek,pad-select",
						   i, &mdata->pad_sel[i]);
			if (mdata->pad_sel[i] > MT8173_SPI_MAX_PAD_SEL)
				return dev_err_probe(dev, -EINVAL,
						     "wrong pad-sel[%d]: %u\n",
						     i, mdata->pad_sel[i]);
		}
	}

	platform_set_drvdata(pdev, host);
	mdata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(mdata->base))
		return PTR_ERR(mdata->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	if (!dev->dma_mask)
		dev->dma_mask = &dev->coherent_dma_mask;

	if (mdata->dev_comp->ipm_design)
		dma_set_max_seg_size(dev, SZ_16M);
	else
		dma_set_max_seg_size(dev, SZ_256K);

	mdata->parent_clk = devm_clk_get(dev, "parent-clk");
	if (IS_ERR(mdata->parent_clk))
		return dev_err_probe(dev, PTR_ERR(mdata->parent_clk),
				     "failed to get parent-clk\n");

	mdata->sel_clk = devm_clk_get(dev, "sel-clk");
	if (IS_ERR(mdata->sel_clk))
		return dev_err_probe(dev, PTR_ERR(mdata->sel_clk), "failed to get sel-clk\n");

	mdata->spi_clk = devm_clk_get(dev, "spi-clk");
	if (IS_ERR(mdata->spi_clk))
		return dev_err_probe(dev, PTR_ERR(mdata->spi_clk), "failed to get spi-clk\n");

	mdata->spi_hclk = devm_clk_get_optional(dev, "hclk");
	if (IS_ERR(mdata->spi_hclk))
		return dev_err_probe(dev, PTR_ERR(mdata->spi_hclk), "failed to get hclk\n");

	ret = clk_set_parent(mdata->sel_clk, mdata->parent_clk);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to clk_set_parent\n");

	ret = clk_prepare_enable(mdata->spi_hclk);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to enable hclk\n");

	ret = clk_prepare_enable(mdata->spi_clk);
	if (ret < 0) {
		clk_disable_unprepare(mdata->spi_hclk);
		return dev_err_probe(dev, ret, "failed to enable spi_clk\n");
	}

	mdata->spi_clk_hz = clk_get_rate(mdata->spi_clk);

	if (mdata->dev_comp->no_need_unprepare) {
		clk_disable(mdata->spi_clk);
		clk_disable(mdata->spi_hclk);
	} else {
		clk_disable_unprepare(mdata->spi_clk);
		clk_disable_unprepare(mdata->spi_hclk);
	}

	cpu_latency_qos_add_request(&mdata->qos_request, PM_QOS_DEFAULT_VALUE);

	if (mdata->dev_comp->need_pad_sel) {
		if (mdata->pad_num != host->num_chipselect)
			return dev_err_probe(dev, -EINVAL,
				"pad_num does not match num_chipselect(%d != %d)\n",
				mdata->pad_num, host->num_chipselect);

		if (!host->cs_gpiods && host->num_chipselect > 1)
			return dev_err_probe(dev, -EINVAL,
				"cs_gpios not specified and num_chipselect > 1\n");
	}

	if (mdata->dev_comp->dma_ext)
		addr_bits = DMA_ADDR_EXT_BITS;
	else
		addr_bits = DMA_ADDR_DEF_BITS;
	ret = dma_set_mask(dev, DMA_BIT_MASK(addr_bits));
	if (ret)
		dev_notice(dev, "SPI dma_set_mask(%d) failed, ret:%d\n",
			   addr_bits, ret);

	ret = devm_request_threaded_irq(dev, irq, mtk_spi_interrupt,
					mtk_spi_interrupt_thread,
					IRQF_TRIGGER_NONE, dev_name(dev), host);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register irq\n");

	pm_runtime_enable(dev);

	ret = devm_spi_register_controller(dev, host);
	if (ret) {
		pm_runtime_disable(dev);
		return dev_err_probe(dev, ret, "failed to register host\n");
	}

	return 0;
}

static void mtk_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host = platform_get_drvdata(pdev);
	struct mtk_spi *mdata = spi_controller_get_devdata(host);
	int ret;

	cpu_latency_qos_remove_request(&mdata->qos_request);
	if (mdata->use_spimem && !completion_done(&mdata->spimem_done))
		complete(&mdata->spimem_done);

	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_warn(&pdev->dev, "Failed to resume hardware (%pe)\n", ERR_PTR(ret));
	} else {
		/*
		 * If pm runtime resume failed, clks are disabled and
		 * unprepared. So don't access the hardware and skip clk
		 * unpreparing.
		 */
		mtk_spi_reset(mdata);

		if (mdata->dev_comp->no_need_unprepare) {
			clk_unprepare(mdata->spi_clk);
			clk_unprepare(mdata->spi_hclk);
		}
	}

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int mtk_spi_suspend(struct device *dev)
{
	int ret;
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	ret = spi_controller_suspend(host);
	if (ret)
		return ret;

	if (!pm_runtime_suspended(dev)) {
		clk_disable_unprepare(mdata->spi_clk);
		clk_disable_unprepare(mdata->spi_hclk);
	}

	pinctrl_pm_select_sleep_state(dev);

	return 0;
}

static int mtk_spi_resume(struct device *dev)
{
	int ret;
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	pinctrl_pm_select_default_state(dev);

	if (!pm_runtime_suspended(dev)) {
		ret = clk_prepare_enable(mdata->spi_clk);
		if (ret < 0) {
			dev_err(dev, "failed to enable spi_clk (%d)\n", ret);
			return ret;
		}

		ret = clk_prepare_enable(mdata->spi_hclk);
		if (ret < 0) {
			dev_err(dev, "failed to enable spi_hclk (%d)\n", ret);
			clk_disable_unprepare(mdata->spi_clk);
			return ret;
		}
	}

	ret = spi_controller_resume(host);
	if (ret < 0) {
		clk_disable_unprepare(mdata->spi_clk);
		clk_disable_unprepare(mdata->spi_hclk);
	}

	return ret;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM
static int mtk_spi_runtime_suspend(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mtk_spi *mdata = spi_controller_get_devdata(host);

	if (mdata->dev_comp->no_need_unprepare) {
		clk_disable(mdata->spi_clk);
		clk_disable(mdata->spi_hclk);
	} else {
		clk_disable_unprepare(mdata->spi_clk);
		clk_disable_unprepare(mdata->spi_hclk);
	}

	return 0;
}

static int mtk_spi_runtime_resume(struct device *dev)
{
	struct spi_controller *host = dev_get_drvdata(dev);
	struct mtk_spi *mdata = spi_controller_get_devdata(host);
	int ret;

	if (mdata->dev_comp->no_need_unprepare) {
		ret = clk_enable(mdata->spi_clk);
		if (ret < 0) {
			dev_err(dev, "failed to enable spi_clk (%d)\n", ret);
			return ret;
		}
		ret = clk_enable(mdata->spi_hclk);
		if (ret < 0) {
			dev_err(dev, "failed to enable spi_hclk (%d)\n", ret);
			clk_disable(mdata->spi_clk);
			return ret;
		}
	} else {
		ret = clk_prepare_enable(mdata->spi_clk);
		if (ret < 0) {
			dev_err(dev, "failed to prepare_enable spi_clk (%d)\n", ret);
			return ret;
		}

		ret = clk_prepare_enable(mdata->spi_hclk);
		if (ret < 0) {
			dev_err(dev, "failed to prepare_enable spi_hclk (%d)\n", ret);
			clk_disable_unprepare(mdata->spi_clk);
			return ret;
		}
	}

	return 0;
}
#endif /* CONFIG_PM */

static const struct dev_pm_ops mtk_spi_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(mtk_spi_suspend, mtk_spi_resume)
	SET_RUNTIME_PM_OPS(mtk_spi_runtime_suspend,
			   mtk_spi_runtime_resume, NULL)
};

static struct platform_driver mtk_spi_driver = {
	.driver = {
		.name = "mtk-spi",
		.pm	= &mtk_spi_pm,
		.of_match_table = mtk_spi_of_match,
	},
	.probe = mtk_spi_probe,
	.remove = mtk_spi_remove,
};

module_platform_driver(mtk_spi_driver);

MODULE_DESCRIPTION("MTK SPI Controller driver");
MODULE_AUTHOR("Leilk Liu <leilk.liu@mediatek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mtk-spi");
