// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Ludovic Barre <ludovic.barre@st.com> for STMicroelectronics.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sizes.h>
#include <linux/spi/spi-mem.h>

#define QSPI_CR			0x00
#define CR_EN			BIT(0)
#define CR_ABORT		BIT(1)
#define CR_DMAEN		BIT(2)
#define CR_TCEN			BIT(3)
#define CR_SSHIFT		BIT(4)
#define CR_DFM			BIT(6)
#define CR_FSEL			BIT(7)
#define CR_FTHRES_SHIFT		8
#define CR_TEIE			BIT(16)
#define CR_TCIE			BIT(17)
#define CR_FTIE			BIT(18)
#define CR_SMIE			BIT(19)
#define CR_TOIE			BIT(20)
#define CR_APMS			BIT(22)
#define CR_PRESC_MASK		GENMASK(31, 24)

#define QSPI_DCR		0x04
#define DCR_FSIZE_MASK		GENMASK(20, 16)

#define QSPI_SR			0x08
#define SR_TEF			BIT(0)
#define SR_TCF			BIT(1)
#define SR_FTF			BIT(2)
#define SR_SMF			BIT(3)
#define SR_TOF			BIT(4)
#define SR_BUSY			BIT(5)
#define SR_FLEVEL_MASK		GENMASK(13, 8)

#define QSPI_FCR		0x0c
#define FCR_CTEF		BIT(0)
#define FCR_CTCF		BIT(1)
#define FCR_CSMF		BIT(3)

#define QSPI_DLR		0x10

#define QSPI_CCR		0x14
#define CCR_INST_MASK		GENMASK(7, 0)
#define CCR_IMODE_MASK		GENMASK(9, 8)
#define CCR_ADMODE_MASK		GENMASK(11, 10)
#define CCR_ADSIZE_MASK		GENMASK(13, 12)
#define CCR_DCYC_MASK		GENMASK(22, 18)
#define CCR_DMODE_MASK		GENMASK(25, 24)
#define CCR_FMODE_MASK		GENMASK(27, 26)
#define CCR_FMODE_INDW		(0U << 26)
#define CCR_FMODE_INDR		(1U << 26)
#define CCR_FMODE_APM		(2U << 26)
#define CCR_FMODE_MM		(3U << 26)
#define CCR_BUSWIDTH_0		0x0
#define CCR_BUSWIDTH_1		0x1
#define CCR_BUSWIDTH_2		0x2
#define CCR_BUSWIDTH_4		0x3

#define QSPI_AR			0x18
#define QSPI_ABR		0x1c
#define QSPI_DR			0x20
#define QSPI_PSMKR		0x24
#define QSPI_PSMAR		0x28
#define QSPI_PIR		0x2c
#define QSPI_LPTR		0x30

#define STM32_QSPI_MAX_MMAP_SZ	SZ_256M
#define STM32_QSPI_MAX_NORCHIP	2

#define STM32_FIFO_TIMEOUT_US 30000
#define STM32_BUSY_TIMEOUT_US 100000
#define STM32_ABT_TIMEOUT_US 100000
#define STM32_COMP_TIMEOUT_MS 1000
#define STM32_AUTOSUSPEND_DELAY -1

struct stm32_qspi_flash {
	u32 cs;
	u32 presc;
};

struct stm32_qspi {
	struct device *dev;
	struct spi_controller *ctrl;
	phys_addr_t phys_base;
	void __iomem *io_base;
	void __iomem *mm_base;
	resource_size_t mm_size;
	struct clk *clk;
	u32 clk_rate;
	struct stm32_qspi_flash flash[STM32_QSPI_MAX_NORCHIP];
	struct completion data_completion;
	struct completion match_completion;
	u32 fmode;

	struct dma_chan *dma_chtx;
	struct dma_chan *dma_chrx;
	struct completion dma_completion;

	u32 cr_reg;
	u32 dcr_reg;
	unsigned long status_timeout;

	/*
	 * to protect device configuration, could be different between
	 * 2 flash access (bk1, bk2)
	 */
	struct mutex lock;
};

static irqreturn_t stm32_qspi_irq(int irq, void *dev_id)
{
	struct stm32_qspi *qspi = (struct stm32_qspi *)dev_id;
	u32 cr, sr;

	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	sr = readl_relaxed(qspi->io_base + QSPI_SR);

	if (cr & CR_SMIE && sr & SR_SMF) {
		/* disable irq */
		cr &= ~CR_SMIE;
		writel_relaxed(cr, qspi->io_base + QSPI_CR);
		complete(&qspi->match_completion);

		return IRQ_HANDLED;
	}

	if (sr & (SR_TEF | SR_TCF)) {
		/* disable irq */
		cr &= ~CR_TCIE & ~CR_TEIE;
		writel_relaxed(cr, qspi->io_base + QSPI_CR);
		complete(&qspi->data_completion);
	}

	return IRQ_HANDLED;
}

static void stm32_qspi_read_fifo(u8 *val, void __iomem *addr)
{
	*val = readb_relaxed(addr);
}

static void stm32_qspi_write_fifo(u8 *val, void __iomem *addr)
{
	writeb_relaxed(*val, addr);
}

static int stm32_qspi_tx_poll(struct stm32_qspi *qspi,
			      const struct spi_mem_op *op)
{
	void (*tx_fifo)(u8 *val, void __iomem *addr);
	u32 len = op->data.nbytes, sr;
	u8 *buf;
	int ret;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		tx_fifo = stm32_qspi_read_fifo;
		buf = op->data.buf.in;

	} else {
		tx_fifo = stm32_qspi_write_fifo;
		buf = (u8 *)op->data.buf.out;
	}

	while (len--) {
		ret = readl_relaxed_poll_timeout_atomic(qspi->io_base + QSPI_SR,
							sr, (sr & SR_FTF), 1,
							STM32_FIFO_TIMEOUT_US);
		if (ret) {
			dev_err(qspi->dev, "fifo timeout (len:%d stat:%#x)\n",
				len, sr);
			return ret;
		}
		tx_fifo(buf++, qspi->io_base + QSPI_DR);
	}

	return 0;
}

static int stm32_qspi_tx_mm(struct stm32_qspi *qspi,
			    const struct spi_mem_op *op)
{
	memcpy_fromio(op->data.buf.in, qspi->mm_base + op->addr.val,
		      op->data.nbytes);
	return 0;
}

static void stm32_qspi_dma_callback(void *arg)
{
	struct completion *dma_completion = arg;

	complete(dma_completion);
}

static int stm32_qspi_tx_dma(struct stm32_qspi *qspi,
			     const struct spi_mem_op *op)
{
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction dma_dir;
	struct dma_chan *dma_ch;
	struct sg_table sgt;
	dma_cookie_t cookie;
	u32 cr, t_out;
	int err;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		dma_dir = DMA_DEV_TO_MEM;
		dma_ch = qspi->dma_chrx;
	} else {
		dma_dir = DMA_MEM_TO_DEV;
		dma_ch = qspi->dma_chtx;
	}

	/*
	 * spi_map_buf return -EINVAL if the buffer is not DMA-able
	 * (DMA-able: in vmalloc | kmap | virt_addr_valid)
	 */
	err = spi_controller_dma_map_mem_op_data(qspi->ctrl, op, &sgt);
	if (err)
		return err;

	desc = dmaengine_prep_slave_sg(dma_ch, sgt.sgl, sgt.nents,
				       dma_dir, DMA_PREP_INTERRUPT);
	if (!desc) {
		err = -ENOMEM;
		goto out_unmap;
	}

	cr = readl_relaxed(qspi->io_base + QSPI_CR);

	reinit_completion(&qspi->dma_completion);
	desc->callback = stm32_qspi_dma_callback;
	desc->callback_param = &qspi->dma_completion;
	cookie = dmaengine_submit(desc);
	err = dma_submit_error(cookie);
	if (err)
		goto out;

	dma_async_issue_pending(dma_ch);

	writel_relaxed(cr | CR_DMAEN, qspi->io_base + QSPI_CR);

	t_out = sgt.nents * STM32_COMP_TIMEOUT_MS;
	if (!wait_for_completion_timeout(&qspi->dma_completion,
					 msecs_to_jiffies(t_out)))
		err = -ETIMEDOUT;

	if (err)
		dmaengine_terminate_all(dma_ch);

out:
	writel_relaxed(cr & ~CR_DMAEN, qspi->io_base + QSPI_CR);
out_unmap:
	spi_controller_dma_unmap_mem_op_data(qspi->ctrl, op, &sgt);

	return err;
}

static int stm32_qspi_tx(struct stm32_qspi *qspi, const struct spi_mem_op *op)
{
	if (!op->data.nbytes)
		return 0;

	if (qspi->fmode == CCR_FMODE_MM)
		return stm32_qspi_tx_mm(qspi, op);
	else if (((op->data.dir == SPI_MEM_DATA_IN && qspi->dma_chrx) ||
		 (op->data.dir == SPI_MEM_DATA_OUT && qspi->dma_chtx)) &&
		  op->data.nbytes > 4)
		if (!stm32_qspi_tx_dma(qspi, op))
			return 0;

	return stm32_qspi_tx_poll(qspi, op);
}

static int stm32_qspi_wait_nobusy(struct stm32_qspi *qspi)
{
	u32 sr;

	return readl_relaxed_poll_timeout_atomic(qspi->io_base + QSPI_SR, sr,
						 !(sr & SR_BUSY), 1,
						 STM32_BUSY_TIMEOUT_US);
}

static int stm32_qspi_wait_cmd(struct stm32_qspi *qspi,
			       const struct spi_mem_op *op)
{
	u32 cr, sr;
	int err = 0;

	if (!op->data.nbytes)
		goto wait_nobusy;

	if (readl_relaxed(qspi->io_base + QSPI_SR) & SR_TCF)
		goto out;

	reinit_completion(&qspi->data_completion);
	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	writel_relaxed(cr | CR_TCIE | CR_TEIE, qspi->io_base + QSPI_CR);

	if (!wait_for_completion_timeout(&qspi->data_completion,
				msecs_to_jiffies(STM32_COMP_TIMEOUT_MS))) {
		err = -ETIMEDOUT;
	} else {
		sr = readl_relaxed(qspi->io_base + QSPI_SR);
		if (sr & SR_TEF)
			err = -EIO;
	}

out:
	/* clear flags */
	writel_relaxed(FCR_CTCF | FCR_CTEF, qspi->io_base + QSPI_FCR);
wait_nobusy:
	if (!err)
		err = stm32_qspi_wait_nobusy(qspi);

	return err;
}

static int stm32_qspi_wait_poll_status(struct stm32_qspi *qspi,
				       const struct spi_mem_op *op)
{
	u32 cr;

	reinit_completion(&qspi->match_completion);
	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	writel_relaxed(cr | CR_SMIE, qspi->io_base + QSPI_CR);

	if (!wait_for_completion_timeout(&qspi->match_completion,
				msecs_to_jiffies(qspi->status_timeout)))
		return -ETIMEDOUT;

	writel_relaxed(FCR_CSMF, qspi->io_base + QSPI_FCR);

	return 0;
}

static int stm32_qspi_get_mode(struct stm32_qspi *qspi, u8 buswidth)
{
	if (buswidth == 4)
		return CCR_BUSWIDTH_4;

	return buswidth;
}

static int stm32_qspi_send(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(mem->spi->master);
	struct stm32_qspi_flash *flash = &qspi->flash[mem->spi->chip_select];
	u32 ccr, cr;
	int timeout, err = 0, err_poll_status = 0;

	dev_dbg(qspi->dev, "cmd:%#x mode:%d.%d.%d.%d addr:%#llx len:%#x\n",
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.val, op->data.nbytes);

	err = stm32_qspi_wait_nobusy(qspi);
	if (err)
		goto abort;

	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	cr &= ~CR_PRESC_MASK & ~CR_FSEL;
	cr |= FIELD_PREP(CR_PRESC_MASK, flash->presc);
	cr |= FIELD_PREP(CR_FSEL, flash->cs);
	writel_relaxed(cr, qspi->io_base + QSPI_CR);

	if (op->data.nbytes)
		writel_relaxed(op->data.nbytes - 1,
			       qspi->io_base + QSPI_DLR);

	ccr = qspi->fmode;
	ccr |= FIELD_PREP(CCR_INST_MASK, op->cmd.opcode);
	ccr |= FIELD_PREP(CCR_IMODE_MASK,
			  stm32_qspi_get_mode(qspi, op->cmd.buswidth));

	if (op->addr.nbytes) {
		ccr |= FIELD_PREP(CCR_ADMODE_MASK,
				  stm32_qspi_get_mode(qspi, op->addr.buswidth));
		ccr |= FIELD_PREP(CCR_ADSIZE_MASK, op->addr.nbytes - 1);
	}

	if (op->dummy.buswidth && op->dummy.nbytes)
		ccr |= FIELD_PREP(CCR_DCYC_MASK,
				  op->dummy.nbytes * 8 / op->dummy.buswidth);

	if (op->data.nbytes) {
		ccr |= FIELD_PREP(CCR_DMODE_MASK,
				  stm32_qspi_get_mode(qspi, op->data.buswidth));
	}

	writel_relaxed(ccr, qspi->io_base + QSPI_CCR);

	if (op->addr.nbytes && qspi->fmode != CCR_FMODE_MM)
		writel_relaxed(op->addr.val, qspi->io_base + QSPI_AR);

	if (qspi->fmode == CCR_FMODE_APM)
		err_poll_status = stm32_qspi_wait_poll_status(qspi, op);

	err = stm32_qspi_tx(qspi, op);

	/*
	 * Abort in:
	 * -error case
	 * -read memory map: prefetching must be stopped if we read the last
	 *  byte of device (device size - fifo size). like device size is not
	 *  knows, the prefetching is always stop.
	 */
	if (err || err_poll_status || qspi->fmode == CCR_FMODE_MM)
		goto abort;

	/* wait end of tx in indirect mode */
	err = stm32_qspi_wait_cmd(qspi, op);
	if (err)
		goto abort;

	return 0;

abort:
	cr = readl_relaxed(qspi->io_base + QSPI_CR) | CR_ABORT;
	writel_relaxed(cr, qspi->io_base + QSPI_CR);

	/* wait clear of abort bit by hw */
	timeout = readl_relaxed_poll_timeout_atomic(qspi->io_base + QSPI_CR,
						    cr, !(cr & CR_ABORT), 1,
						    STM32_ABT_TIMEOUT_US);

	writel_relaxed(FCR_CTCF | FCR_CSMF, qspi->io_base + QSPI_FCR);

	if (err || err_poll_status || timeout)
		dev_err(qspi->dev, "%s err:%d err_poll_status:%d abort timeout:%d\n",
			__func__, err, err_poll_status, timeout);

	return err;
}

static int stm32_qspi_poll_status(struct spi_mem *mem, const struct spi_mem_op *op,
				  u16 mask, u16 match,
				  unsigned long initial_delay_us,
				  unsigned long polling_rate_us,
				  unsigned long timeout_ms)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(mem->spi->master);
	int ret;

	if (!spi_mem_supports_op(mem, op))
		return -EOPNOTSUPP;

	ret = pm_runtime_get_sync(qspi->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(qspi->dev);
		return ret;
	}

	mutex_lock(&qspi->lock);

	writel_relaxed(mask, qspi->io_base + QSPI_PSMKR);
	writel_relaxed(match, qspi->io_base + QSPI_PSMAR);
	qspi->fmode = CCR_FMODE_APM;
	qspi->status_timeout = timeout_ms;

	ret = stm32_qspi_send(mem, op);
	mutex_unlock(&qspi->lock);

	pm_runtime_mark_last_busy(qspi->dev);
	pm_runtime_put_autosuspend(qspi->dev);

	return ret;
}

static int stm32_qspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(mem->spi->master);
	int ret;

	ret = pm_runtime_get_sync(qspi->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(qspi->dev);
		return ret;
	}

	mutex_lock(&qspi->lock);
	if (op->data.dir == SPI_MEM_DATA_IN && op->data.nbytes)
		qspi->fmode = CCR_FMODE_INDR;
	else
		qspi->fmode = CCR_FMODE_INDW;

	ret = stm32_qspi_send(mem, op);
	mutex_unlock(&qspi->lock);

	pm_runtime_mark_last_busy(qspi->dev);
	pm_runtime_put_autosuspend(qspi->dev);

	return ret;
}

static int stm32_qspi_dirmap_create(struct spi_mem_dirmap_desc *desc)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(desc->mem->spi->master);

	if (desc->info.op_tmpl.data.dir == SPI_MEM_DATA_OUT)
		return -EOPNOTSUPP;

	/* should never happen, as mm_base == null is an error probe exit condition */
	if (!qspi->mm_base && desc->info.op_tmpl.data.dir == SPI_MEM_DATA_IN)
		return -EOPNOTSUPP;

	if (!qspi->mm_size)
		return -EOPNOTSUPP;

	return 0;
}

static ssize_t stm32_qspi_dirmap_read(struct spi_mem_dirmap_desc *desc,
				      u64 offs, size_t len, void *buf)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(desc->mem->spi->master);
	struct spi_mem_op op;
	u32 addr_max;
	int ret;

	ret = pm_runtime_get_sync(qspi->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(qspi->dev);
		return ret;
	}

	mutex_lock(&qspi->lock);
	/* make a local copy of desc op_tmpl and complete dirmap rdesc
	 * spi_mem_op template with offs, len and *buf in  order to get
	 * all needed transfer information into struct spi_mem_op
	 */
	memcpy(&op, &desc->info.op_tmpl, sizeof(struct spi_mem_op));
	dev_dbg(qspi->dev, "%s len = 0x%zx offs = 0x%llx buf = 0x%p\n", __func__, len, offs, buf);

	op.data.nbytes = len;
	op.addr.val = desc->info.offset + offs;
	op.data.buf.in = buf;

	addr_max = op.addr.val + op.data.nbytes + 1;
	if (addr_max < qspi->mm_size && op.addr.buswidth)
		qspi->fmode = CCR_FMODE_MM;
	else
		qspi->fmode = CCR_FMODE_INDR;

	ret = stm32_qspi_send(desc->mem, &op);
	mutex_unlock(&qspi->lock);

	pm_runtime_mark_last_busy(qspi->dev);
	pm_runtime_put_autosuspend(qspi->dev);

	return ret ?: len;
}

static int stm32_qspi_setup(struct spi_device *spi)
{
	struct spi_controller *ctrl = spi->master;
	struct stm32_qspi *qspi = spi_controller_get_devdata(ctrl);
	struct stm32_qspi_flash *flash;
	u32 presc;
	int ret;

	if (ctrl->busy)
		return -EBUSY;

	if (!spi->max_speed_hz)
		return -EINVAL;

	ret = pm_runtime_get_sync(qspi->dev);
	if (ret < 0) {
		pm_runtime_put_noidle(qspi->dev);
		return ret;
	}

	presc = DIV_ROUND_UP(qspi->clk_rate, spi->max_speed_hz) - 1;

	flash = &qspi->flash[spi->chip_select];
	flash->cs = spi->chip_select;
	flash->presc = presc;

	mutex_lock(&qspi->lock);
	qspi->cr_reg = CR_APMS | 3 << CR_FTHRES_SHIFT | CR_SSHIFT | CR_EN;
	writel_relaxed(qspi->cr_reg, qspi->io_base + QSPI_CR);

	/* set dcr fsize to max address */
	qspi->dcr_reg = DCR_FSIZE_MASK;
	writel_relaxed(qspi->dcr_reg, qspi->io_base + QSPI_DCR);
	mutex_unlock(&qspi->lock);

	pm_runtime_mark_last_busy(qspi->dev);
	pm_runtime_put_autosuspend(qspi->dev);

	return 0;
}

static int stm32_qspi_dma_setup(struct stm32_qspi *qspi)
{
	struct dma_slave_config dma_cfg;
	struct device *dev = qspi->dev;
	int ret = 0;

	memset(&dma_cfg, 0, sizeof(dma_cfg));

	dma_cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
	dma_cfg.src_addr = qspi->phys_base + QSPI_DR;
	dma_cfg.dst_addr = qspi->phys_base + QSPI_DR;
	dma_cfg.src_maxburst = 4;
	dma_cfg.dst_maxburst = 4;

	qspi->dma_chrx = dma_request_chan(dev, "rx");
	if (IS_ERR(qspi->dma_chrx)) {
		ret = PTR_ERR(qspi->dma_chrx);
		qspi->dma_chrx = NULL;
		if (ret == -EPROBE_DEFER)
			goto out;
	} else {
		if (dmaengine_slave_config(qspi->dma_chrx, &dma_cfg)) {
			dev_err(dev, "dma rx config failed\n");
			dma_release_channel(qspi->dma_chrx);
			qspi->dma_chrx = NULL;
		}
	}

	qspi->dma_chtx = dma_request_chan(dev, "tx");
	if (IS_ERR(qspi->dma_chtx)) {
		ret = PTR_ERR(qspi->dma_chtx);
		qspi->dma_chtx = NULL;
	} else {
		if (dmaengine_slave_config(qspi->dma_chtx, &dma_cfg)) {
			dev_err(dev, "dma tx config failed\n");
			dma_release_channel(qspi->dma_chtx);
			qspi->dma_chtx = NULL;
		}
	}

out:
	init_completion(&qspi->dma_completion);

	if (ret != -EPROBE_DEFER)
		ret = 0;

	return ret;
}

static void stm32_qspi_dma_free(struct stm32_qspi *qspi)
{
	if (qspi->dma_chtx)
		dma_release_channel(qspi->dma_chtx);
	if (qspi->dma_chrx)
		dma_release_channel(qspi->dma_chrx);
}

/*
 * no special host constraint, so use default spi_mem_default_supports_op
 * to check supported mode.
 */
static const struct spi_controller_mem_ops stm32_qspi_mem_ops = {
	.exec_op	= stm32_qspi_exec_op,
	.dirmap_create	= stm32_qspi_dirmap_create,
	.dirmap_read	= stm32_qspi_dirmap_read,
	.poll_status	= stm32_qspi_poll_status,
};

static int stm32_qspi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *ctrl;
	struct reset_control *rstc;
	struct stm32_qspi *qspi;
	struct resource *res;
	int ret, irq;

	ctrl = spi_alloc_master(dev, sizeof(*qspi));
	if (!ctrl)
		return -ENOMEM;

	qspi = spi_controller_get_devdata(ctrl);
	qspi->ctrl = ctrl;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi");
	qspi->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->io_base)) {
		ret = PTR_ERR(qspi->io_base);
		goto err_master_put;
	}

	qspi->phys_base = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_mm");
	qspi->mm_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->mm_base)) {
		ret = PTR_ERR(qspi->mm_base);
		goto err_master_put;
	}

	qspi->mm_size = resource_size(res);
	if (qspi->mm_size > STM32_QSPI_MAX_MMAP_SZ) {
		ret = -EINVAL;
		goto err_master_put;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = irq;
		goto err_master_put;
	}

	ret = devm_request_irq(dev, irq, stm32_qspi_irq, 0,
			       dev_name(dev), qspi);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_master_put;
	}

	init_completion(&qspi->data_completion);
	init_completion(&qspi->match_completion);

	qspi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(qspi->clk)) {
		ret = PTR_ERR(qspi->clk);
		goto err_master_put;
	}

	qspi->clk_rate = clk_get_rate(qspi->clk);
	if (!qspi->clk_rate) {
		ret = -EINVAL;
		goto err_master_put;
	}

	ret = clk_prepare_enable(qspi->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		goto err_master_put;
	}

	rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		if (ret == -EPROBE_DEFER)
			goto err_clk_disable;
	} else {
		reset_control_assert(rstc);
		udelay(2);
		reset_control_deassert(rstc);
	}

	qspi->dev = dev;
	platform_set_drvdata(pdev, qspi);
	ret = stm32_qspi_dma_setup(qspi);
	if (ret)
		goto err_dma_free;

	mutex_init(&qspi->lock);

	ctrl->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD
		| SPI_TX_DUAL | SPI_TX_QUAD;
	ctrl->setup = stm32_qspi_setup;
	ctrl->bus_num = -1;
	ctrl->mem_ops = &stm32_qspi_mem_ops;
	ctrl->num_chipselect = STM32_QSPI_MAX_NORCHIP;
	ctrl->dev.of_node = dev->of_node;

	pm_runtime_set_autosuspend_delay(dev, STM32_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);

	ret = devm_spi_register_master(dev, ctrl);
	if (ret)
		goto err_pm_runtime_free;

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_pm_runtime_free:
	pm_runtime_get_sync(qspi->dev);
	/* disable qspi */
	writel_relaxed(0, qspi->io_base + QSPI_CR);
	mutex_destroy(&qspi->lock);
	pm_runtime_put_noidle(qspi->dev);
	pm_runtime_disable(qspi->dev);
	pm_runtime_set_suspended(qspi->dev);
	pm_runtime_dont_use_autosuspend(qspi->dev);
err_dma_free:
	stm32_qspi_dma_free(qspi);
err_clk_disable:
	clk_disable_unprepare(qspi->clk);
err_master_put:
	spi_master_put(qspi->ctrl);

	return ret;
}

static int stm32_qspi_remove(struct platform_device *pdev)
{
	struct stm32_qspi *qspi = platform_get_drvdata(pdev);

	pm_runtime_get_sync(qspi->dev);
	/* disable qspi */
	writel_relaxed(0, qspi->io_base + QSPI_CR);
	stm32_qspi_dma_free(qspi);
	mutex_destroy(&qspi->lock);
	pm_runtime_put_noidle(qspi->dev);
	pm_runtime_disable(qspi->dev);
	pm_runtime_set_suspended(qspi->dev);
	pm_runtime_dont_use_autosuspend(qspi->dev);
	clk_disable_unprepare(qspi->clk);

	return 0;
}

static int __maybe_unused stm32_qspi_runtime_suspend(struct device *dev)
{
	struct stm32_qspi *qspi = dev_get_drvdata(dev);

	clk_disable_unprepare(qspi->clk);

	return 0;
}

static int __maybe_unused stm32_qspi_runtime_resume(struct device *dev)
{
	struct stm32_qspi *qspi = dev_get_drvdata(dev);

	return clk_prepare_enable(qspi->clk);
}

static int __maybe_unused stm32_qspi_suspend(struct device *dev)
{
	pinctrl_pm_select_sleep_state(dev);

	return pm_runtime_force_suspend(dev);
}

static int __maybe_unused stm32_qspi_resume(struct device *dev)
{
	struct stm32_qspi *qspi = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_resume(dev);
	if (ret < 0)
		return ret;

	pinctrl_pm_select_default_state(dev);

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		pm_runtime_put_noidle(dev);
		return ret;
	}

	writel_relaxed(qspi->cr_reg, qspi->io_base + QSPI_CR);
	writel_relaxed(qspi->dcr_reg, qspi->io_base + QSPI_DCR);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}

static const struct dev_pm_ops stm32_qspi_pm_ops = {
	SET_RUNTIME_PM_OPS(stm32_qspi_runtime_suspend,
			   stm32_qspi_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(stm32_qspi_suspend, stm32_qspi_resume)
};

static const struct of_device_id stm32_qspi_match[] = {
	{.compatible = "st,stm32f469-qspi"},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_qspi_match);

static struct platform_driver stm32_qspi_driver = {
	.probe	= stm32_qspi_probe,
	.remove	= stm32_qspi_remove,
	.driver	= {
		.name = "stm32-qspi",
		.of_match_table = stm32_qspi_match,
		.pm = &stm32_qspi_pm_ops,
	},
};
module_platform_driver(stm32_qspi_driver);

MODULE_AUTHOR("Ludovic Barre <ludovic.barre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 quad spi driver");
MODULE_LICENSE("GPL v2");
