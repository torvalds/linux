/*
 * ADMA driver for Nvidia's Tegra210 ADMA controller.
 *
 * Copyright (c) 2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_dma.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/clk/tegra.h>

#include "dmaengine.h"
#include "virt-dma.h"

#define ADMA_CH_CMD					0x00
#define ADMA_CH_STATUS					0x0c
#define ADMA_CH_STATUS_XFER_EN				BIT(0)

#define ADMA_CH_INT_STATUS				0x10
#define ADMA_CH_INT_STATUS_XFER_DONE			BIT(0)

#define ADMA_CH_INT_CLEAR				0x1c
#define ADMA_CH_CTRL					0x24
#define ADMA_CH_CTRL_TX_REQ(val)			(((val) & 0xf) << 28)
#define ADMA_CH_CTRL_TX_REQ_MAX				10
#define ADMA_CH_CTRL_RX_REQ(val)			(((val) & 0xf) << 24)
#define ADMA_CH_CTRL_RX_REQ_MAX				10
#define ADMA_CH_CTRL_DIR(val)				(((val) & 0xf) << 12)
#define ADMA_CH_CTRL_DIR_AHUB2MEM			2
#define ADMA_CH_CTRL_DIR_MEM2AHUB			4
#define ADMA_CH_CTRL_MODE_CONTINUOUS			(2 << 8)
#define ADMA_CH_CTRL_FLOWCTRL_EN			BIT(1)

#define ADMA_CH_CONFIG					0x28
#define ADMA_CH_CONFIG_SRC_BUF(val)			(((val) & 0x7) << 28)
#define ADMA_CH_CONFIG_TRG_BUF(val)			(((val) & 0x7) << 24)
#define ADMA_CH_CONFIG_BURST_SIZE(val)			(((val) & 0x7) << 20)
#define ADMA_CH_CONFIG_BURST_16				5
#define ADMA_CH_CONFIG_WEIGHT_FOR_WRR(val)		((val) & 0xf)
#define ADMA_CH_CONFIG_MAX_BUFS				8

#define ADMA_CH_FIFO_CTRL				0x2c
#define ADMA_CH_FIFO_CTRL_OVRFW_THRES(val)		(((val) & 0xf) << 24)
#define ADMA_CH_FIFO_CTRL_STARV_THRES(val)		(((val) & 0xf) << 16)
#define ADMA_CH_FIFO_CTRL_TX_SIZE(val)			(((val) & 0xf) << 8)
#define ADMA_CH_FIFO_CTRL_RX_SIZE(val)			((val) & 0xf)

#define ADMA_CH_TC_STATUS				0x30
#define ADMA_CH_LOWER_SRC_ADDR				0x34
#define ADMA_CH_LOWER_TRG_ADDR				0x3c
#define ADMA_CH_TC					0x44
#define ADMA_CH_TC_COUNT_MASK				0x3ffffffc

#define ADMA_CH_XFER_STATUS				0x54
#define ADMA_CH_XFER_STATUS_COUNT_MASK			0xffff

#define ADMA_GLOBAL_CMD					0xc00
#define ADMA_GLOBAL_SOFT_RESET				0xc04
#define ADMA_GLOBAL_INT_CLEAR				0xc20
#define ADMA_GLOBAL_CTRL				0xc24

#define ADMA_CH_REG_OFFSET(a)				(a * 0x80)

#define ADMA_CH_FIFO_CTRL_DEFAULT	(ADMA_CH_FIFO_CTRL_OVRFW_THRES(1) | \
					 ADMA_CH_FIFO_CTRL_STARV_THRES(1) | \
					 ADMA_CH_FIFO_CTRL_TX_SIZE(3)     | \
					 ADMA_CH_FIFO_CTRL_RX_SIZE(3))
struct tegra_adma;

/*
 * struct tegra_adma_chip_data - Tegra chip specific data
 * @nr_channels: Number of DMA channels available.
 */
struct tegra_adma_chip_data {
	int nr_channels;
};

/*
 * struct tegra_adma_chan_regs - Tegra ADMA channel registers
 */
struct tegra_adma_chan_regs {
	unsigned int ctrl;
	unsigned int config;
	unsigned int src_addr;
	unsigned int trg_addr;
	unsigned int fifo_ctrl;
	unsigned int tc;
};

/*
 * struct tegra_adma_desc - Tegra ADMA descriptor to manage transfer requests.
 */
struct tegra_adma_desc {
	struct virt_dma_desc		vd;
	struct tegra_adma_chan_regs	ch_regs;
	unsigned long			bytes_requested;
	unsigned long			bytes_transferred;
};

/*
 * struct tegra_adma_chan - Tegra ADMA channel information
 */
struct tegra_adma_chan {
	struct virt_dma_chan		vc;
	struct tegra_adma_desc		*desc;
	struct tegra_adma		*tdma;
	char				name[30];
	int				irq;
	void __iomem			*chan_addr;
	spinlock_t			lock;

	/* Slave channel configuration info */
	struct dma_slave_config		sconfig;
	bool				sconfig_valid;
	unsigned int			sreq_dir;
	unsigned int			sreq_index;
	bool				sreq_reserved;

	/* Transfer count and position info */
	unsigned int			tx_buf_count;
	unsigned int			tx_buf_pos;
};

/*
 * struct tegra_adma - Tegra ADMA controller information
 */
struct tegra_adma {
	struct dma_device			dma_dev;
	struct device				*dev;
	struct clk				*adma_clk;
	void __iomem				*base_addr;
	unsigned int				nr_channels;
	unsigned long				rx_requests_reserved;
	unsigned long				tx_requests_reserved;

	/* Used to store global command register state when suspending */
	unsigned int				global_cmd;

	/* Last member of the structure */
	struct tegra_adma_chan			channels[0];
};

static inline void tdma_write(struct tegra_adma *tdma, u32 reg, u32 val)
{
	writel(val, tdma->base_addr + reg);
}

static inline u32 tdma_read(struct tegra_adma *tdma, u32 reg)
{
	return readl(tdma->base_addr + reg);
}

static inline void tdma_ch_write(struct tegra_adma_chan *tdc,
		u32 reg, u32 val)
{
	writel(val, tdc->chan_addr + reg);
}

static inline u32 tdma_ch_read(struct tegra_adma_chan *tdc, u32 reg)
{
	return readl(tdc->chan_addr + reg);
}

static inline struct tegra_adma_chan *to_tegra_adma_chan(struct dma_chan *dc)
{
	return container_of(dc, struct tegra_adma_chan, vc.chan);
}

static inline struct tegra_adma_desc *to_tegra_adma_desc(
		struct dma_async_tx_descriptor *td)
{
	return container_of(td, struct tegra_adma_desc, vd.tx);
}

static inline struct device *tdc2dev(struct tegra_adma_chan *tdc)
{
	return tdc->tdma->dev;
}

static void tegra_adma_desc_free(struct virt_dma_desc *vd)
{
	kfree(container_of(vd, struct tegra_adma_desc, vd));
}

static int tegra_adma_slave_config(struct dma_chan *dc,
				   struct dma_slave_config *sconfig)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);

	memcpy(&tdc->sconfig, sconfig, sizeof(*sconfig));
	tdc->sconfig_valid = true;

	return 0;
}

static int tegra_adma_init(struct tegra_adma *tdma)
{
	u32 status;
	int ret;

	/* Clear any interrupts */
	tdma_write(tdma, ADMA_GLOBAL_INT_CLEAR, 0x1);

	/* Assert soft reset */
	tdma_write(tdma, ADMA_GLOBAL_SOFT_RESET, 0x1);

	/* Wait for reset to clear */
	ret = readx_poll_timeout(readl,
				 tdma->base_addr + ADMA_GLOBAL_SOFT_RESET,
				 status, status == 0, 20, 10000);
	if (ret)
		return ret;

	/* Enable global ADMA registers */
	tdma_write(tdma, ADMA_GLOBAL_CMD, 1);

	return 0;
}

static int tegra_adma_request_alloc(struct tegra_adma_chan *tdc,
				    unsigned int sreq_dir)
{
	struct tegra_adma *tdma = tdc->tdma;
	unsigned int sreq_index = tdc->sreq_index;

	if (tdc->sreq_reserved)
		return tdc->sreq_dir == sreq_dir ? 0 : -EINVAL;

	switch (sreq_dir) {
	case ADMA_CH_CTRL_DIR_MEM2AHUB:
		if (sreq_index > ADMA_CH_CTRL_TX_REQ_MAX) {
			dev_err(tdma->dev, "invalid DMA request\n");
			return -EINVAL;
		}

		if (test_and_set_bit(sreq_index, &tdma->tx_requests_reserved)) {
			dev_err(tdma->dev, "DMA request reserved\n");
			return -EINVAL;
		}
		break;

	case ADMA_CH_CTRL_DIR_AHUB2MEM:
		if (sreq_index > ADMA_CH_CTRL_RX_REQ_MAX) {
			dev_err(tdma->dev, "invalid DMA request\n");
			return -EINVAL;
		}

		if (test_and_set_bit(sreq_index, &tdma->rx_requests_reserved)) {
			dev_err(tdma->dev, "DMA request reserved\n");
			return -EINVAL;
		}
		break;

	default:
		dev_WARN(tdma->dev, "channel %s has invalid transfer type\n",
			 tdc->name);
		return -EINVAL;
	}

	tdc->sreq_dir = sreq_dir;
	tdc->sreq_reserved = true;

	return 0;
}

static void tegra_adma_request_free(struct tegra_adma_chan *tdc)
{
	struct tegra_adma *tdma = tdc->tdma;

	if (!tdc->sreq_reserved)
		return;

	switch (tdc->sreq_dir) {
	case ADMA_CH_CTRL_DIR_MEM2AHUB:
		clear_bit(tdc->sreq_index, &tdma->tx_requests_reserved);
		break;
	case ADMA_CH_CTRL_DIR_AHUB2MEM:
		clear_bit(tdc->sreq_index, &tdma->rx_requests_reserved);
		break;
	default:
		dev_WARN(tdma->dev, "channel %s has invalid transfer type\n",
			 tdc->name);
		return;
	}

	tdc->sreq_reserved = false;
}

static u32 tegra_adma_irq_status(struct tegra_adma_chan *tdc)
{
	u32 status = tdma_ch_read(tdc, ADMA_CH_INT_STATUS);

	return status & ADMA_CH_INT_STATUS_XFER_DONE;
}

static u32 tegra_adma_irq_clear(struct tegra_adma_chan *tdc)
{
	u32 status = tegra_adma_irq_status(tdc);

	if (status)
		tdma_ch_write(tdc, ADMA_CH_INT_CLEAR, status);

	return status;
}

static void tegra_adma_stop(struct tegra_adma_chan *tdc)
{
	unsigned int status;

	/* Disable ADMA */
	tdma_ch_write(tdc, ADMA_CH_CMD, 0);

	/* Clear interrupt status */
	tegra_adma_irq_clear(tdc);

	if (readx_poll_timeout_atomic(readl, tdc->chan_addr + ADMA_CH_STATUS,
			status, !(status & ADMA_CH_STATUS_XFER_EN),
			20, 10000)) {
		dev_err(tdc2dev(tdc), "unable to stop DMA channel\n");
		return;
	}

	tdc->desc = NULL;
}

static void tegra_adma_start(struct tegra_adma_chan *tdc)
{
	struct virt_dma_desc *vd = vchan_next_desc(&tdc->vc);
	struct tegra_adma_chan_regs *ch_regs;
	struct tegra_adma_desc *desc;

	if (!vd)
		return;

	list_del(&vd->node);

	desc = to_tegra_adma_desc(&vd->tx);

	if (!desc) {
		dev_warn(tdc2dev(tdc), "unable to start DMA, no descriptor\n");
		return;
	}

	ch_regs = &desc->ch_regs;

	tdc->tx_buf_pos = 0;
	tdc->tx_buf_count = 0;
	tdma_ch_write(tdc, ADMA_CH_TC, ch_regs->tc);
	tdma_ch_write(tdc, ADMA_CH_CTRL, ch_regs->ctrl);
	tdma_ch_write(tdc, ADMA_CH_LOWER_SRC_ADDR, ch_regs->src_addr);
	tdma_ch_write(tdc, ADMA_CH_LOWER_TRG_ADDR, ch_regs->trg_addr);
	tdma_ch_write(tdc, ADMA_CH_FIFO_CTRL, ch_regs->fifo_ctrl);
	tdma_ch_write(tdc, ADMA_CH_CONFIG, ch_regs->config);

	/* Start ADMA */
	tdma_ch_write(tdc, ADMA_CH_CMD, 1);

	tdc->desc = desc;
}

static void tegra_adma_update_position(struct tegra_adma_chan *tdc)
{
	struct tegra_adma_desc *desc = tdc->desc;
	unsigned int max = ADMA_CH_XFER_STATUS_COUNT_MASK + 1;
	unsigned int pos = tdma_ch_read(tdc, ADMA_CH_XFER_STATUS);

	/*
	 * Handle wrap around of buffer count register
	 */
	if (pos < tdc->tx_buf_pos)
		tdc->tx_buf_count += pos + (max - tdc->tx_buf_pos);
	else
		tdc->tx_buf_count += pos - tdc->tx_buf_pos;

	tdc->tx_buf_pos = pos;

	desc->bytes_transferred = tdc->tx_buf_count * desc->ch_regs.tc;

	/*
	 * If we are not currently active, then it is safe to read the
	 * remaining words from the TC_STATUS register and add the partial
	 * buffer to the total transferred.
	 */
	if (!tdc->desc)
		desc->bytes_transferred += desc->ch_regs.tc -
					   tdma_ch_read(tdc, ADMA_CH_TC_STATUS);
}

static unsigned int tegra_adma_get_residue(struct tegra_adma_desc *desc)
{
	return desc->bytes_requested - (desc->bytes_transferred %
					desc->bytes_requested);
}

static irqreturn_t tegra_adma_isr(int irq, void *dev_id)
{
	struct tegra_adma_chan *tdc = dev_id;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);

	status = tegra_adma_irq_clear(tdc);
	if (status == 0 || !tdc->desc) {
		spin_unlock_irqrestore(&tdc->lock, flags);
		return IRQ_NONE;
	}

	vchan_cyclic_callback(&tdc->desc->vd);

	spin_unlock_irqrestore(&tdc->lock, flags);

	return IRQ_HANDLED;
}

static void tegra_adma_issue_pending(struct dma_chan *dc)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);
	unsigned long flags;

	spin_lock_irqsave(&tdc->lock, flags);

	if (vchan_issue_pending(&tdc->vc)) {
		if (tdc->desc)
			dev_warn(tdc2dev(tdc), "DMA already running\n");
		else
			tegra_adma_start(tdc);
	}

	spin_unlock_irqrestore(&tdc->lock, flags);
}

static int tegra_adma_terminate_all(struct dma_chan *dc)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);
	unsigned long flags;
	LIST_HEAD(head);

	spin_lock_irqsave(&tdc->lock, flags);

	if (tdc->desc)
		tegra_adma_stop(tdc);

	tegra_adma_request_free(tdc);
	vchan_get_all_descriptors(&tdc->vc, &head);
	spin_unlock_irqrestore(&tdc->lock, flags);
	vchan_dma_desc_free_list(&tdc->vc, &head);

	return 0;
}

static enum dma_status tegra_adma_tx_status(struct dma_chan *dc,
					    dma_cookie_t cookie,
					    struct dma_tx_state *txstate)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);
	struct tegra_adma_desc *desc;
	struct virt_dma_desc *vd;
	enum dma_status ret;
	unsigned long flags;
	unsigned int residual;

	spin_lock_irqsave(&tdc->lock, flags);

	ret = dma_cookie_status(dc, cookie, txstate);
	if (ret == DMA_COMPLETE || !txstate) {
		spin_unlock_irqrestore(&tdc->lock, flags);
		return ret;
	}

	vd = vchan_find_desc(&tdc->vc, cookie);
	if (vd) {
		desc = to_tegra_adma_desc(&vd->tx);
		residual = desc->ch_regs.tc;
	} else if (tdc->desc && tdc->desc->vd.tx.cookie == cookie) {
		tegra_adma_update_position(tdc);
		residual = tegra_adma_get_residue(tdc->desc);
	} else {
		residual = 0;
	}

	dma_set_residue(txstate, residual);

	spin_unlock_irqrestore(&tdc->lock, flags);

	return ret;
}

static int tegra_adma_set_xfer_params(struct tegra_adma_chan *tdc,
				      struct tegra_adma_desc *desc,
				      dma_addr_t buf_addr, size_t buf_len,
				      size_t period_len,
				      enum dma_transfer_direction direction)
{
	struct tegra_adma_chan_regs *ch_regs = &desc->ch_regs;
	unsigned int burst_size, num_bufs, sreq_dir;

	num_bufs = buf_len / period_len;

	if (num_bufs > ADMA_CH_CONFIG_MAX_BUFS)
		return -EINVAL;

	switch (direction) {
	case DMA_MEM_TO_DEV:
		sreq_dir = ADMA_CH_CTRL_DIR_MEM2AHUB;
		burst_size = fls(tdc->sconfig.dst_maxburst);
		ch_regs->config = ADMA_CH_CONFIG_SRC_BUF(num_bufs - 1);
		ch_regs->ctrl = ADMA_CH_CTRL_TX_REQ(tdc->sreq_index);
		ch_regs->src_addr = buf_addr;
		break;

	case DMA_DEV_TO_MEM:
		sreq_dir = ADMA_CH_CTRL_DIR_AHUB2MEM;
		burst_size = fls(tdc->sconfig.src_maxburst);
		ch_regs->config = ADMA_CH_CONFIG_TRG_BUF(num_bufs - 1);
		ch_regs->ctrl = ADMA_CH_CTRL_RX_REQ(tdc->sreq_index);
		ch_regs->trg_addr = buf_addr;
		break;

	default:
		dev_err(tdc2dev(tdc), "DMA direction is not supported\n");
		return -EINVAL;
	}

	if (!burst_size || burst_size > ADMA_CH_CONFIG_BURST_16)
		burst_size = ADMA_CH_CONFIG_BURST_16;

	ch_regs->ctrl |= ADMA_CH_CTRL_DIR(sreq_dir) |
			 ADMA_CH_CTRL_MODE_CONTINUOUS |
			 ADMA_CH_CTRL_FLOWCTRL_EN;
	ch_regs->config |= ADMA_CH_CONFIG_BURST_SIZE(burst_size);
	ch_regs->config |= ADMA_CH_CONFIG_WEIGHT_FOR_WRR(1);
	ch_regs->fifo_ctrl = ADMA_CH_FIFO_CTRL_DEFAULT;
	ch_regs->tc = period_len & ADMA_CH_TC_COUNT_MASK;

	return tegra_adma_request_alloc(tdc, sreq_dir);
}

static struct dma_async_tx_descriptor *tegra_adma_prep_slave_sg(
	struct dma_chan *dc, struct scatterlist *sgl, unsigned int sg_len,
	enum dma_transfer_direction direction, unsigned long flags,
	void *context)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);

	dev_warn(tdc2dev(tdc), "scatter-gather transfers are not supported\n");

	return NULL;
}

static struct dma_async_tx_descriptor *tegra_adma_prep_dma_cyclic(
	struct dma_chan *dc, dma_addr_t buf_addr, size_t buf_len,
	size_t period_len, enum dma_transfer_direction direction,
	unsigned long flags)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);
	struct tegra_adma_desc *desc = NULL;

	if (!tdc->sconfig_valid) {
		dev_err(tdc2dev(tdc), "ADMA slave configuration not set\n");
		return NULL;
	}

	if (!buf_len || !period_len || period_len > ADMA_CH_TC_COUNT_MASK) {
		dev_err(tdc2dev(tdc), "invalid buffer/period len\n");
		return NULL;
	}

	if (buf_len % period_len) {
		dev_err(tdc2dev(tdc), "buf_len not a multiple of period_len\n");
		return NULL;
	}

	if (!IS_ALIGNED(buf_addr, 4)) {
		dev_err(tdc2dev(tdc), "invalid buffer alignment\n");
		return NULL;
	}

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	desc->bytes_transferred = 0;
	desc->bytes_requested = buf_len;

	if (tegra_adma_set_xfer_params(tdc, desc, buf_addr, buf_len, period_len,
				       direction)) {
		kfree(desc);
		return NULL;
	}

	return vchan_tx_prep(&tdc->vc, &desc->vd, flags);
}

static int tegra_adma_alloc_chan_resources(struct dma_chan *dc)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);
	int ret;

	ret = pm_runtime_get_sync(tdc2dev(tdc));
	if (ret)
		return ret;

	dma_cookie_init(&tdc->vc.chan);
	tdc->sconfig_valid = false;

	return 0;
}

static void tegra_adma_free_chan_resources(struct dma_chan *dc)
{
	struct tegra_adma_chan *tdc = to_tegra_adma_chan(dc);

	if (tdc->desc)
		tegra_adma_terminate_all(dc);

	tdc->sconfig_valid = false;
	vchan_free_chan_resources(&tdc->vc);

	pm_runtime_put(tdc2dev(tdc));

	tegra_adma_request_free(tdc);

	tdc->sreq_index = 0;
	tdc->sreq_dir = 0;
}

static struct dma_chan *tegra_dma_of_xlate(struct of_phandle_args *dma_spec,
					   struct of_dma *ofdma)
{
	struct tegra_adma *tdma = ofdma->of_dma_data;
	struct tegra_adma_chan *tdc;
	struct dma_chan *chan;
	unsigned int sreq_index;

	if (dma_spec->args_count != 1)
		return NULL;

	sreq_index = dma_spec->args[0];

	if (sreq_index == 0) {
		dev_err(tdma->dev, "DMA request must not be 0\n");
		return NULL;
	}

	chan = dma_get_any_slave_channel(&tdma->dma_dev);
	if (!chan)
		return NULL;

	tdc = to_tegra_adma_chan(chan);
	tdc->sreq_index = sreq_index;

	return chan;
}

static int tegra_adma_runtime_suspend(struct device *dev)
{
	struct tegra_adma *tdma = dev_get_drvdata(dev);

	tdma->global_cmd = tdma_read(tdma, ADMA_GLOBAL_CMD);

	clk_disable_unprepare(tdma->adma_clk);

	return 0;
}

static int tegra_adma_runtime_resume(struct device *dev)
{
	struct tegra_adma *tdma = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(tdma->adma_clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable ADMA clock: %d\n", ret);
		return ret;
	}

	tdma_write(tdma, ADMA_GLOBAL_CMD, tdma->global_cmd);

	return 0;
}

static const struct tegra_adma_chip_data tegra210_chip_data = {
	.nr_channels = 22,
};

static const struct of_device_id tegra_adma_of_match[] = {
	{ .compatible = "nvidia,tegra210-adma", .data = &tegra210_chip_data },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_adma_of_match);

static int tegra_adma_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	const struct tegra_adma_chip_data *cdata;
	struct tegra_adma *tdma;
	struct resource	*res;
	int ret, i;

	if (!pdev->dev.of_node) {
		dev_err(&pdev->dev, "no device tree node for ADMA\n");
		return -ENODEV;
	}

	match = of_match_device(tegra_adma_of_match, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev, "no device match found\n");
		return -ENODEV;
	}
	cdata = match->data;

	tdma = devm_kzalloc(&pdev->dev, sizeof(*tdma) + cdata->nr_channels *
			    sizeof(struct tegra_adma_chan), GFP_KERNEL);
	if (!tdma)
		return -ENOMEM;

	tdma->dev = &pdev->dev;
	tdma->nr_channels = cdata->nr_channels;
	platform_set_drvdata(pdev, tdma);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tdma->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(tdma->base_addr))
		return PTR_ERR(tdma->base_addr);

	tdma->adma_clk = devm_clk_get(&pdev->dev, "adma_ape");
	if (IS_ERR(tdma->adma_clk)) {
		dev_err(&pdev->dev, "ADMA clock not found\n");
		return PTR_ERR(tdma->adma_clk);
	}

	pm_runtime_enable(&pdev->dev);
	if (pm_runtime_enabled(&pdev->dev))
		ret = pm_runtime_get_sync(&pdev->dev);
	else
		ret = tegra_adma_runtime_resume(&pdev->dev);

	if (ret) {
		pm_runtime_disable(&pdev->dev);
		return ret;
	}

	ret = tegra_adma_init(tdma);
	if (ret)
		goto err_pm_disable;

	INIT_LIST_HEAD(&tdma->dma_dev.channels);
	for (i = 0; i < tdma->nr_channels; i++) {
		struct tegra_adma_chan *tdc = &tdma->channels[i];

		tdc->chan_addr = tdma->base_addr + ADMA_CH_REG_OFFSET(i);

		snprintf(tdc->name, sizeof(tdc->name), "adma.%d", i);

		tdc->irq = platform_get_irq(pdev, i);
		if (tdc->irq < 0) {
			ret = -EPROBE_DEFER;
			goto err_irq;
		}

		ret = devm_request_irq(&pdev->dev, tdc->irq, tegra_adma_isr, 0,
				       tdc->name, tdc);
		if (ret) {
			dev_err(&pdev->dev,
				"failed to get interrupt for channel %d\n", i);
			goto err_irq;
		}

		spin_lock_init(&tdc->lock);
		vchan_init(&tdc->vc, &tdma->dma_dev);
		tdc->vc.desc_free = tegra_adma_desc_free;
		tdc->tdma = tdma;
	}

	dma_cap_set(DMA_SLAVE, tdma->dma_dev.cap_mask);
	dma_cap_set(DMA_PRIVATE, tdma->dma_dev.cap_mask);
	dma_cap_set(DMA_CYCLIC, tdma->dma_dev.cap_mask);

	tdma->dma_dev.dev = &pdev->dev;
	tdma->dma_dev.device_alloc_chan_resources =
					tegra_adma_alloc_chan_resources;
	tdma->dma_dev.device_free_chan_resources =
					tegra_adma_free_chan_resources;
	tdma->dma_dev.device_issue_pending = tegra_adma_issue_pending;
	tdma->dma_dev.device_prep_slave_sg = tegra_adma_prep_slave_sg;
	tdma->dma_dev.device_prep_dma_cyclic = tegra_adma_prep_dma_cyclic;
	tdma->dma_dev.device_config = tegra_adma_slave_config;
	tdma->dma_dev.device_tx_status = tegra_adma_tx_status;
	tdma->dma_dev.device_terminate_all = tegra_adma_terminate_all;
	tdma->dma_dev.src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	tdma->dma_dev.dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);

	ret = dma_async_device_register(&tdma->dma_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "ADMA registration failed: %d\n", ret);
		goto err_irq;
	}

	ret = of_dma_controller_register(pdev->dev.of_node,
					 tegra_dma_of_xlate, tdma);
	if (ret < 0) {
		dev_err(&pdev->dev, "ADMA OF registration failed %d\n", ret);
		goto err_unregister_dma_dev;
	}

	pm_runtime_put(&pdev->dev);

	dev_info(&pdev->dev, "Tegra210 ADMA driver registered %d channels\n",
		 tdma->nr_channels);

	return 0;

err_unregister_dma_dev:
	dma_async_device_unregister(&tdma->dma_dev);
err_irq:
	while (--i >= 0) {
		struct tegra_adma_chan *tdc = &tdma->channels[i];

		tasklet_kill(&tdc->vc.task);
	}
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_adma_runtime_suspend(&pdev->dev);

	return ret;
}

static int tegra_adma_remove(struct platform_device *pdev)
{
	struct tegra_adma *tdma = platform_get_drvdata(pdev);
	struct tegra_adma_chan *tdc;
	int i;

	dma_async_device_unregister(&tdma->dma_dev);

	for (i = 0; i < tdma->nr_channels; ++i) {
		tdc = &tdma->channels[i];
		disable_irq(tdc->irq);
		tasklet_kill(&tdc->vc.task);
	}

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		tegra_adma_runtime_suspend(&pdev->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int tegra_adma_pm_suspend(struct device *dev)
{
	return pm_runtime_suspended(dev);
}
#endif

static const struct dev_pm_ops tegra_adma_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_adma_runtime_suspend,
			   tegra_adma_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(tegra_adma_pm_suspend, NULL)
};

static struct platform_driver tegra_admac_driver = {
	.driver = {
		.name	= "tegra-adma",
		.pm	= &tegra_adma_dev_pm_ops,
		.of_match_table = tegra_adma_of_match,
	},
	.probe		= tegra_adma_probe,
	.remove		= tegra_adma_remove,
};

module_platform_driver(tegra_admac_driver);

MODULE_ALIAS("platform:tegra210-adma");
MODULE_DESCRIPTION("NVIDIA Tegra ADMA driver");
MODULE_AUTHOR("Dara Ramesh <dramesh@nvidia.com>");
MODULE_AUTHOR("Jon Hunter <jonathanh@nvidia.com>");
MODULE_LICENSE("GPL v2");
