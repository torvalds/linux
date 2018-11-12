// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Ludovic Barre <ludovic.barre@st.com> for STMicroelectronics.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_device.h>
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
#define CR_FTHRES_MASK		GENMASK(12, 8)
#define CR_TEIE			BIT(16)
#define CR_TCIE			BIT(17)
#define CR_FTIE			BIT(18)
#define CR_SMIE			BIT(19)
#define CR_TOIE			BIT(20)
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
#define LPTR_DFT_TIMEOUT	0x10

#define STM32_QSPI_MAX_MMAP_SZ	SZ_256M
#define STM32_QSPI_MAX_NORCHIP	2

#define STM32_FIFO_TIMEOUT_US 30000
#define STM32_BUSY_TIMEOUT_US 100000
#define STM32_ABT_TIMEOUT_US 100000

struct stm32_qspi_flash {
	struct stm32_qspi *qspi;
	u32 cs;
	u32 presc;
};

struct stm32_qspi {
	struct device *dev;
	void __iomem *io_base;
	void __iomem *mm_base;
	resource_size_t mm_size;
	struct clk *clk;
	u32 clk_rate;
	struct stm32_qspi_flash flash[STM32_QSPI_MAX_NORCHIP];
	struct completion data_completion;
	u32 fmode;

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

	sr = readl_relaxed(qspi->io_base + QSPI_SR);

	if (sr & (SR_TEF | SR_TCF)) {
		/* disable irq */
		cr = readl_relaxed(qspi->io_base + QSPI_CR);
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

static int stm32_qspi_tx(struct stm32_qspi *qspi, const struct spi_mem_op *op)
{
	if (!op->data.nbytes)
		return 0;

	if (qspi->fmode == CCR_FMODE_MM)
		return stm32_qspi_tx_mm(qspi, op);

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
		return stm32_qspi_wait_nobusy(qspi);

	if (readl_relaxed(qspi->io_base + QSPI_SR) & SR_TCF)
		goto out;

	reinit_completion(&qspi->data_completion);
	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	writel_relaxed(cr | CR_TCIE | CR_TEIE, qspi->io_base + QSPI_CR);

	if (!wait_for_completion_interruptible_timeout(&qspi->data_completion,
						msecs_to_jiffies(1000))) {
		err = -ETIMEDOUT;
	} else {
		sr = readl_relaxed(qspi->io_base + QSPI_SR);
		if (sr & SR_TEF)
			err = -EIO;
	}

out:
	/* clear flags */
	writel_relaxed(FCR_CTCF | FCR_CTEF, qspi->io_base + QSPI_FCR);

	return err;
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
	u32 ccr, cr, addr_max;
	int timeout, err = 0;

	dev_dbg(qspi->dev, "cmd:%#x mode:%d.%d.%d.%d addr:%#llx len:%#x\n",
		op->cmd.opcode, op->cmd.buswidth, op->addr.buswidth,
		op->dummy.buswidth, op->data.buswidth,
		op->addr.val, op->data.nbytes);

	err = stm32_qspi_wait_nobusy(qspi);
	if (err)
		goto abort;

	addr_max = op->addr.val + op->data.nbytes + 1;

	if (op->data.dir == SPI_MEM_DATA_IN) {
		if (addr_max < qspi->mm_size &&
		    op->addr.buswidth)
			qspi->fmode = CCR_FMODE_MM;
		else
			qspi->fmode = CCR_FMODE_INDR;
	} else {
		qspi->fmode = CCR_FMODE_INDW;
	}

	cr = readl_relaxed(qspi->io_base + QSPI_CR);
	cr &= ~CR_PRESC_MASK & ~CR_FSEL;
	cr |= FIELD_PREP(CR_PRESC_MASK, flash->presc);
	cr |= FIELD_PREP(CR_FSEL, flash->cs);
	writel_relaxed(cr, qspi->io_base + QSPI_CR);

	if (op->data.nbytes)
		writel_relaxed(op->data.nbytes - 1,
			       qspi->io_base + QSPI_DLR);
	else
		qspi->fmode = CCR_FMODE_INDW;

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

	err = stm32_qspi_tx(qspi, op);

	/*
	 * Abort in:
	 * -error case
	 * -read memory map: prefetching must be stopped if we read the last
	 *  byte of device (device size - fifo size). like device size is not
	 *  knows, the prefetching is always stop.
	 */
	if (err || qspi->fmode == CCR_FMODE_MM)
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

	writel_relaxed(FCR_CTCF, qspi->io_base + QSPI_FCR);

	if (err || timeout)
		dev_err(qspi->dev, "%s err:%d abort timeout:%d\n",
			__func__, err, timeout);

	return err;
}

static int stm32_qspi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct stm32_qspi *qspi = spi_controller_get_devdata(mem->spi->master);
	int ret;

	mutex_lock(&qspi->lock);
	ret = stm32_qspi_send(mem, op);
	mutex_unlock(&qspi->lock);

	return ret;
}

static int stm32_qspi_setup(struct spi_device *spi)
{
	struct spi_controller *ctrl = spi->master;
	struct stm32_qspi *qspi = spi_controller_get_devdata(ctrl);
	struct stm32_qspi_flash *flash;
	u32 cr, presc;

	if (ctrl->busy)
		return -EBUSY;

	if (!spi->max_speed_hz)
		return -EINVAL;

	presc = DIV_ROUND_UP(qspi->clk_rate, spi->max_speed_hz) - 1;

	flash = &qspi->flash[spi->chip_select];
	flash->qspi = qspi;
	flash->cs = spi->chip_select;
	flash->presc = presc;

	mutex_lock(&qspi->lock);
	writel_relaxed(LPTR_DFT_TIMEOUT, qspi->io_base + QSPI_LPTR);
	cr = FIELD_PREP(CR_FTHRES_MASK, 3) | CR_TCEN | CR_SSHIFT | CR_EN;
	writel_relaxed(cr, qspi->io_base + QSPI_CR);

	/* set dcr fsize to max address */
	writel_relaxed(DCR_FSIZE_MASK, qspi->io_base + QSPI_DCR);
	mutex_unlock(&qspi->lock);

	return 0;
}

/*
 * no special host constraint, so use default spi_mem_default_supports_op
 * to check supported mode.
 */
static const struct spi_controller_mem_ops stm32_qspi_mem_ops = {
	.exec_op = stm32_qspi_exec_op,
};

static void stm32_qspi_release(struct stm32_qspi *qspi)
{
	/* disable qspi */
	writel_relaxed(0, qspi->io_base + QSPI_CR);
	mutex_destroy(&qspi->lock);
	clk_disable_unprepare(qspi->clk);
}

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

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi");
	qspi->io_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->io_base))
		return PTR_ERR(qspi->io_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "qspi_mm");
	qspi->mm_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(qspi->mm_base))
		return PTR_ERR(qspi->mm_base);

	qspi->mm_size = resource_size(res);
	if (qspi->mm_size > STM32_QSPI_MAX_MMAP_SZ)
		return -EINVAL;

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, stm32_qspi_irq, 0,
			       dev_name(dev), qspi);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		return ret;
	}

	init_completion(&qspi->data_completion);

	qspi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(qspi->clk))
		return PTR_ERR(qspi->clk);

	qspi->clk_rate = clk_get_rate(qspi->clk);
	if (!qspi->clk_rate)
		return -EINVAL;

	ret = clk_prepare_enable(qspi->clk);
	if (ret) {
		dev_err(dev, "can not enable the clock\n");
		return ret;
	}

	rstc = devm_reset_control_get_exclusive(dev, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		udelay(2);
		reset_control_deassert(rstc);
	}

	qspi->dev = dev;
	platform_set_drvdata(pdev, qspi);
	mutex_init(&qspi->lock);

	ctrl->mode_bits = SPI_RX_DUAL | SPI_RX_QUAD
		| SPI_TX_DUAL | SPI_TX_QUAD;
	ctrl->setup = stm32_qspi_setup;
	ctrl->bus_num = -1;
	ctrl->mem_ops = &stm32_qspi_mem_ops;
	ctrl->num_chipselect = STM32_QSPI_MAX_NORCHIP;
	ctrl->dev.of_node = dev->of_node;

	ret = devm_spi_register_master(dev, ctrl);
	if (ret)
		goto err_spi_register;

	return 0;

err_spi_register:
	stm32_qspi_release(qspi);

	return ret;
}

static int stm32_qspi_remove(struct platform_device *pdev)
{
	struct stm32_qspi *qspi = platform_get_drvdata(pdev);

	stm32_qspi_release(qspi);
	return 0;
}

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
	},
};
module_platform_driver(stm32_qspi_driver);

MODULE_AUTHOR("Ludovic Barre <ludovic.barre@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 quad spi driver");
MODULE_LICENSE("GPL v2");
