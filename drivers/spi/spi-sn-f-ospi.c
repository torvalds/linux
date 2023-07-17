// SPDX-License-Identifier: GPL-2.0-only
/*
 * Socionext SPI flash controller F_OSPI driver
 * Copyright (C) 2021 Socionext Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/* Registers */
#define OSPI_PROT_CTL_INDIR			0x00
#define   OSPI_PROT_MODE_DATA_MASK		GENMASK(31, 30)
#define   OSPI_PROT_MODE_ALT_MASK		GENMASK(29, 28)
#define   OSPI_PROT_MODE_ADDR_MASK		GENMASK(27, 26)
#define   OSPI_PROT_MODE_CODE_MASK		GENMASK(25, 24)
#define     OSPI_PROT_MODE_SINGLE		0
#define     OSPI_PROT_MODE_DUAL			1
#define     OSPI_PROT_MODE_QUAD			2
#define     OSPI_PROT_MODE_OCTAL		3
#define   OSPI_PROT_DATA_RATE_DATA		BIT(23)
#define   OSPI_PROT_DATA_RATE_ALT		BIT(22)
#define   OSPI_PROT_DATA_RATE_ADDR		BIT(21)
#define   OSPI_PROT_DATA_RATE_CODE		BIT(20)
#define     OSPI_PROT_SDR			0
#define     OSPI_PROT_DDR			1
#define   OSPI_PROT_BIT_POS_DATA		BIT(19)
#define   OSPI_PROT_BIT_POS_ALT			BIT(18)
#define   OSPI_PROT_BIT_POS_ADDR		BIT(17)
#define   OSPI_PROT_BIT_POS_CODE		BIT(16)
#define   OSPI_PROT_SAMP_EDGE			BIT(12)
#define   OSPI_PROT_DATA_UNIT_MASK		GENMASK(11, 10)
#define     OSPI_PROT_DATA_UNIT_1B		0
#define     OSPI_PROT_DATA_UNIT_2B		1
#define     OSPI_PROT_DATA_UNIT_4B		3
#define   OSPI_PROT_TRANS_DIR_WRITE		BIT(9)
#define   OSPI_PROT_DATA_EN			BIT(8)
#define   OSPI_PROT_ALT_SIZE_MASK		GENMASK(7, 5)
#define   OSPI_PROT_ADDR_SIZE_MASK		GENMASK(4, 2)
#define   OSPI_PROT_CODE_SIZE_MASK		GENMASK(1, 0)

#define OSPI_CLK_CTL				0x10
#define   OSPI_CLK_CTL_BOOT_INT_CLK_EN		BIT(16)
#define   OSPI_CLK_CTL_PHA			BIT(12)
#define     OSPI_CLK_CTL_PHA_180		0
#define     OSPI_CLK_CTL_PHA_90			1
#define   OSPI_CLK_CTL_DIV			GENMASK(9, 8)
#define     OSPI_CLK_CTL_DIV_1			0
#define     OSPI_CLK_CTL_DIV_2			1
#define     OSPI_CLK_CTL_DIV_4			2
#define     OSPI_CLK_CTL_DIV_8			3
#define   OSPI_CLK_CTL_INT_CLK_EN		BIT(0)

#define OSPI_CS_CTL1				0x14
#define OSPI_CS_CTL2				0x18
#define OSPI_SSEL				0x20
#define OSPI_CMD_IDX_INDIR			0x40
#define OSPI_ADDR				0x50
#define OSPI_ALT_INDIR				0x60
#define OSPI_DMY_INDIR				0x70
#define OSPI_DAT				0x80
#define OSPI_DAT_SWP_INDIR			0x90

#define OSPI_DAT_SIZE_INDIR			0xA0
#define   OSPI_DAT_SIZE_EN			BIT(15)
#define   OSPI_DAT_SIZE_MASK			GENMASK(10, 0)
#define   OSPI_DAT_SIZE_MAX			(OSPI_DAT_SIZE_MASK + 1)

#define OSPI_TRANS_CTL				0xC0
#define   OSPI_TRANS_CTL_STOP_REQ		BIT(1)	/* RW1AC */
#define   OSPI_TRANS_CTL_START_REQ		BIT(0)	/* RW1AC */

#define OSPI_ACC_MODE				0xC4
#define   OSPI_ACC_MODE_BOOT_DISABLE		BIT(0)

#define OSPI_SWRST				0xD0
#define   OSPI_SWRST_INDIR_WRITE_FIFO		BIT(9)	/* RW1AC */
#define   OSPI_SWRST_INDIR_READ_FIFO		BIT(8)	/* RW1AC */

#define OSPI_STAT				0xE0
#define   OSPI_STAT_IS_AXI_WRITING		BIT(10)
#define   OSPI_STAT_IS_AXI_READING		BIT(9)
#define   OSPI_STAT_IS_SPI_INT_CLK_STOP		BIT(4)
#define   OSPI_STAT_IS_SPI_IDLE			BIT(3)

#define OSPI_IRQ				0xF0
#define   OSPI_IRQ_CS_DEASSERT			BIT(8)
#define   OSPI_IRQ_WRITE_BUF_READY		BIT(2)
#define   OSPI_IRQ_READ_BUF_READY		BIT(1)
#define   OSPI_IRQ_CS_TRANS_COMP		BIT(0)
#define   OSPI_IRQ_ALL				\
		(OSPI_IRQ_CS_DEASSERT | OSPI_IRQ_WRITE_BUF_READY \
		 | OSPI_IRQ_READ_BUF_READY | OSPI_IRQ_CS_TRANS_COMP)

#define OSPI_IRQ_STAT_EN			0xF4
#define OSPI_IRQ_SIG_EN				0xF8

/* Parameters */
#define OSPI_NUM_CS				4
#define OSPI_DUMMY_CYCLE_MAX			255
#define OSPI_WAIT_MAX_MSEC			100

struct f_ospi {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
	struct mutex mlock;
};

static u32 f_ospi_get_dummy_cycle(const struct spi_mem_op *op)
{
	return (op->dummy.nbytes * 8) / op->dummy.buswidth;
}

static void f_ospi_clear_irq(struct f_ospi *ospi)
{
	writel(OSPI_IRQ_CS_DEASSERT | OSPI_IRQ_CS_TRANS_COMP,
	       ospi->base + OSPI_IRQ);
}

static void f_ospi_enable_irq_status(struct f_ospi *ospi, u32 irq_bits)
{
	u32 val;

	val = readl(ospi->base + OSPI_IRQ_STAT_EN);
	val |= irq_bits;
	writel(val, ospi->base + OSPI_IRQ_STAT_EN);
}

static void f_ospi_disable_irq_status(struct f_ospi *ospi, u32 irq_bits)
{
	u32 val;

	val = readl(ospi->base + OSPI_IRQ_STAT_EN);
	val &= ~irq_bits;
	writel(val, ospi->base + OSPI_IRQ_STAT_EN);
}

static void f_ospi_disable_irq_output(struct f_ospi *ospi, u32 irq_bits)
{
	u32 val;

	val = readl(ospi->base + OSPI_IRQ_SIG_EN);
	val &= ~irq_bits;
	writel(val, ospi->base + OSPI_IRQ_SIG_EN);
}

static int f_ospi_prepare_config(struct f_ospi *ospi)
{
	u32 val, stat0, stat1;

	/* G4: Disable internal clock */
	val = readl(ospi->base + OSPI_CLK_CTL);
	val &= ~(OSPI_CLK_CTL_BOOT_INT_CLK_EN | OSPI_CLK_CTL_INT_CLK_EN);
	writel(val, ospi->base + OSPI_CLK_CTL);

	/* G5: Wait for stop */
	stat0 = OSPI_STAT_IS_AXI_WRITING | OSPI_STAT_IS_AXI_READING;
	stat1 = OSPI_STAT_IS_SPI_IDLE | OSPI_STAT_IS_SPI_INT_CLK_STOP;

	return readl_poll_timeout(ospi->base + OSPI_STAT,
				  val, (val & (stat0 | stat1)) == stat1,
				  0, OSPI_WAIT_MAX_MSEC);
}

static int f_ospi_unprepare_config(struct f_ospi *ospi)
{
	u32 val;

	/* G11: Enable internal clock */
	val = readl(ospi->base + OSPI_CLK_CTL);
	val |= OSPI_CLK_CTL_BOOT_INT_CLK_EN | OSPI_CLK_CTL_INT_CLK_EN;
	writel(val, ospi->base + OSPI_CLK_CTL);

	/* G12: Wait for clock to start */
	return readl_poll_timeout(ospi->base + OSPI_STAT,
				  val, !(val & OSPI_STAT_IS_SPI_INT_CLK_STOP),
				  0, OSPI_WAIT_MAX_MSEC);
}

static void f_ospi_config_clk(struct f_ospi *ospi, u32 device_hz)
{
	long rate_hz = clk_get_rate(ospi->clk);
	u32 div = DIV_ROUND_UP(rate_hz, device_hz);
	u32 div_reg;
	u32 val;

	if (rate_hz < device_hz) {
		dev_warn(ospi->dev, "Device frequency too large: %d\n",
			 device_hz);
		div_reg = OSPI_CLK_CTL_DIV_1;
	} else {
		if (div == 1) {
			div_reg = OSPI_CLK_CTL_DIV_1;
		} else if (div == 2) {
			div_reg = OSPI_CLK_CTL_DIV_2;
		} else if (div <= 4) {
			div_reg = OSPI_CLK_CTL_DIV_4;
		} else if (div <= 8) {
			div_reg = OSPI_CLK_CTL_DIV_8;
		} else {
			dev_warn(ospi->dev, "Device frequency too small: %d\n",
				 device_hz);
			div_reg = OSPI_CLK_CTL_DIV_8;
		}
	}

	/*
	 * G7: Set clock mode
	 * clock phase is fixed at 180 degrees and configure edge direction
	 * instead.
	 */
	val = readl(ospi->base + OSPI_CLK_CTL);

	val &= ~(OSPI_CLK_CTL_PHA | OSPI_CLK_CTL_DIV);
	val |= FIELD_PREP(OSPI_CLK_CTL_PHA, OSPI_CLK_CTL_PHA_180)
	     | FIELD_PREP(OSPI_CLK_CTL_DIV, div_reg);

	writel(val, ospi->base + OSPI_CLK_CTL);
}

static void f_ospi_config_dll(struct f_ospi *ospi)
{
	/* G8: Configure DLL, nothing */
}

static u8 f_ospi_get_mode(struct f_ospi *ospi, int width, int data_size)
{
	u8 mode = OSPI_PROT_MODE_SINGLE;

	switch (width) {
	case 1:
		mode = OSPI_PROT_MODE_SINGLE;
		break;
	case 2:
		mode = OSPI_PROT_MODE_DUAL;
		break;
	case 4:
		mode = OSPI_PROT_MODE_QUAD;
		break;
	case 8:
		mode = OSPI_PROT_MODE_OCTAL;
		break;
	default:
		if (data_size)
			dev_err(ospi->dev, "Invalid buswidth: %d\n", width);
		break;
	}

	return mode;
}

static void f_ospi_config_indir_protocol(struct f_ospi *ospi,
					 struct spi_mem *mem,
					 const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	u8 mode;
	u32 prot = 0, val;
	int unit;

	/* Set one chip select */
	writel(BIT(spi_get_chipselect(spi, 0)), ospi->base + OSPI_SSEL);

	mode = f_ospi_get_mode(ospi, op->cmd.buswidth, 1);
	prot |= FIELD_PREP(OSPI_PROT_MODE_CODE_MASK, mode);

	mode = f_ospi_get_mode(ospi, op->addr.buswidth, op->addr.nbytes);
	prot |= FIELD_PREP(OSPI_PROT_MODE_ADDR_MASK, mode);

	mode = f_ospi_get_mode(ospi, op->data.buswidth, op->data.nbytes);
	prot |= FIELD_PREP(OSPI_PROT_MODE_DATA_MASK, mode);

	prot |= FIELD_PREP(OSPI_PROT_DATA_RATE_DATA, OSPI_PROT_SDR);
	prot |= FIELD_PREP(OSPI_PROT_DATA_RATE_ALT,  OSPI_PROT_SDR);
	prot |= FIELD_PREP(OSPI_PROT_DATA_RATE_ADDR, OSPI_PROT_SDR);
	prot |= FIELD_PREP(OSPI_PROT_DATA_RATE_CODE, OSPI_PROT_SDR);

	if (spi->mode & SPI_LSB_FIRST)
		prot |= OSPI_PROT_BIT_POS_DATA | OSPI_PROT_BIT_POS_ALT
		      | OSPI_PROT_BIT_POS_ADDR | OSPI_PROT_BIT_POS_CODE;

	if (spi->mode & SPI_CPHA)
		prot |= OSPI_PROT_SAMP_EDGE;

	/* Examine nbytes % 4 */
	switch (op->data.nbytes & 0x3) {
	case 0:
		unit = OSPI_PROT_DATA_UNIT_4B;
		val = 0;
		break;
	case 2:
		unit = OSPI_PROT_DATA_UNIT_2B;
		val = OSPI_DAT_SIZE_EN | (op->data.nbytes - 1);
		break;
	default:
		unit = OSPI_PROT_DATA_UNIT_1B;
		val = OSPI_DAT_SIZE_EN | (op->data.nbytes - 1);
		break;
	}
	prot |= FIELD_PREP(OSPI_PROT_DATA_UNIT_MASK, unit);

	switch (op->data.dir) {
	case SPI_MEM_DATA_IN:
		prot |= OSPI_PROT_DATA_EN;
		break;

	case SPI_MEM_DATA_OUT:
		prot |= OSPI_PROT_TRANS_DIR_WRITE | OSPI_PROT_DATA_EN;
		break;

	case SPI_MEM_NO_DATA:
		prot |= OSPI_PROT_TRANS_DIR_WRITE;
		break;

	default:
		dev_warn(ospi->dev, "Unsupported direction");
		break;
	}

	prot |= FIELD_PREP(OSPI_PROT_ADDR_SIZE_MASK, op->addr.nbytes);
	prot |= FIELD_PREP(OSPI_PROT_CODE_SIZE_MASK, 1);	/* 1byte */

	writel(prot, ospi->base + OSPI_PROT_CTL_INDIR);
	writel(val, ospi->base + OSPI_DAT_SIZE_INDIR);
}

static int f_ospi_indir_prepare_op(struct f_ospi *ospi, struct spi_mem *mem,
				   const struct spi_mem_op *op)
{
	struct spi_device *spi = mem->spi;
	u32 irq_stat_en;
	int ret;

	ret = f_ospi_prepare_config(ospi);
	if (ret)
		return ret;

	f_ospi_config_clk(ospi, spi->max_speed_hz);

	f_ospi_config_indir_protocol(ospi, mem, op);

	writel(f_ospi_get_dummy_cycle(op), ospi->base + OSPI_DMY_INDIR);
	writel(op->addr.val, ospi->base + OSPI_ADDR);
	writel(op->cmd.opcode, ospi->base + OSPI_CMD_IDX_INDIR);

	f_ospi_clear_irq(ospi);

	switch (op->data.dir) {
	case SPI_MEM_DATA_IN:
		irq_stat_en = OSPI_IRQ_READ_BUF_READY | OSPI_IRQ_CS_TRANS_COMP;
		break;

	case SPI_MEM_DATA_OUT:
		irq_stat_en = OSPI_IRQ_WRITE_BUF_READY | OSPI_IRQ_CS_TRANS_COMP;
		break;

	case SPI_MEM_NO_DATA:
		irq_stat_en = OSPI_IRQ_CS_TRANS_COMP;
		break;

	default:
		dev_warn(ospi->dev, "Unsupported direction");
		irq_stat_en = 0;
	}

	f_ospi_disable_irq_status(ospi, ~irq_stat_en);
	f_ospi_enable_irq_status(ospi, irq_stat_en);

	return f_ospi_unprepare_config(ospi);
}

static void f_ospi_indir_start_xfer(struct f_ospi *ospi)
{
	/* Write only 1, auto cleared */
	writel(OSPI_TRANS_CTL_START_REQ, ospi->base + OSPI_TRANS_CTL);
}

static void f_ospi_indir_stop_xfer(struct f_ospi *ospi)
{
	/* Write only 1, auto cleared */
	writel(OSPI_TRANS_CTL_STOP_REQ, ospi->base + OSPI_TRANS_CTL);
}

static int f_ospi_indir_wait_xfer_complete(struct f_ospi *ospi)
{
	u32 val;

	return readl_poll_timeout(ospi->base + OSPI_IRQ, val,
				  val & OSPI_IRQ_CS_TRANS_COMP,
				  0, OSPI_WAIT_MAX_MSEC);
}

static int f_ospi_indir_read(struct f_ospi *ospi, struct spi_mem *mem,
			     const struct spi_mem_op *op)
{
	u8 *buf = op->data.buf.in;
	u32 val;
	int i, ret;

	mutex_lock(&ospi->mlock);

	/* E1-2: Prepare transfer operation */
	ret = f_ospi_indir_prepare_op(ospi, mem, op);
	if (ret)
		goto out;

	f_ospi_indir_start_xfer(ospi);

	/* E3-4: Wait for ready and read data */
	for (i = 0; i < op->data.nbytes; i++) {
		ret = readl_poll_timeout(ospi->base + OSPI_IRQ, val,
					 val & OSPI_IRQ_READ_BUF_READY,
					 0, OSPI_WAIT_MAX_MSEC);
		if (ret)
			goto out;

		buf[i] = readl(ospi->base + OSPI_DAT) & 0xFF;
	}

	/* E5-6: Stop transfer if data size is nothing */
	if (!(readl(ospi->base + OSPI_DAT_SIZE_INDIR) & OSPI_DAT_SIZE_EN))
		f_ospi_indir_stop_xfer(ospi);

	/* E7-8: Wait for completion and clear */
	ret = f_ospi_indir_wait_xfer_complete(ospi);
	if (ret)
		goto out;

	writel(OSPI_IRQ_CS_TRANS_COMP, ospi->base + OSPI_IRQ);

	/* E9: Do nothing if data size is valid */
	if (readl(ospi->base + OSPI_DAT_SIZE_INDIR) & OSPI_DAT_SIZE_EN)
		goto out;

	/* E10-11: Reset and check read fifo */
	writel(OSPI_SWRST_INDIR_READ_FIFO, ospi->base + OSPI_SWRST);

	ret = readl_poll_timeout(ospi->base + OSPI_SWRST, val,
				 !(val & OSPI_SWRST_INDIR_READ_FIFO),
				 0, OSPI_WAIT_MAX_MSEC);
out:
	mutex_unlock(&ospi->mlock);

	return ret;
}

static int f_ospi_indir_write(struct f_ospi *ospi, struct spi_mem *mem,
			      const struct spi_mem_op *op)
{
	u8 *buf = (u8 *)op->data.buf.out;
	u32 val;
	int i, ret;

	mutex_lock(&ospi->mlock);

	/* F1-3: Prepare transfer operation */
	ret = f_ospi_indir_prepare_op(ospi, mem, op);
	if (ret)
		goto out;

	f_ospi_indir_start_xfer(ospi);

	if (!(readl(ospi->base + OSPI_PROT_CTL_INDIR) & OSPI_PROT_DATA_EN))
		goto nodata;

	/* F4-5: Wait for buffer ready and write data */
	for (i = 0; i < op->data.nbytes; i++) {
		ret = readl_poll_timeout(ospi->base + OSPI_IRQ, val,
					 val & OSPI_IRQ_WRITE_BUF_READY,
					 0, OSPI_WAIT_MAX_MSEC);
		if (ret)
			goto out;

		writel(buf[i], ospi->base + OSPI_DAT);
	}

	/* F6-7: Stop transfer if data size is nothing */
	if (!(readl(ospi->base + OSPI_DAT_SIZE_INDIR) & OSPI_DAT_SIZE_EN))
		f_ospi_indir_stop_xfer(ospi);

nodata:
	/* F8-9: Wait for completion and clear */
	ret = f_ospi_indir_wait_xfer_complete(ospi);
	if (ret)
		goto out;

	writel(OSPI_IRQ_CS_TRANS_COMP, ospi->base + OSPI_IRQ);
out:
	mutex_unlock(&ospi->mlock);

	return ret;
}

static int f_ospi_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct f_ospi *ospi = spi_controller_get_devdata(mem->spi->master);
	int err = 0;

	switch (op->data.dir) {
	case SPI_MEM_DATA_IN:
		err = f_ospi_indir_read(ospi, mem, op);
		break;

	case SPI_MEM_DATA_OUT:
		fallthrough;
	case SPI_MEM_NO_DATA:
		err = f_ospi_indir_write(ospi, mem, op);
		break;

	default:
		dev_warn(ospi->dev, "Unsupported direction");
		err = -EOPNOTSUPP;
	}

	return err;
}

static bool f_ospi_supports_op_width(struct spi_mem *mem,
				     const struct spi_mem_op *op)
{
	u8 width_available[] = { 0, 1, 2, 4, 8 };
	u8 width_op[] = { op->cmd.buswidth, op->addr.buswidth,
			  op->dummy.buswidth, op->data.buswidth };
	bool is_match_found;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(width_op); i++) {
		is_match_found = false;

		for (j = 0; j < ARRAY_SIZE(width_available); j++) {
			if (width_op[i] == width_available[j]) {
				is_match_found = true;
				break;
			}
		}

		if (!is_match_found)
			return false;
	}

	return true;
}

static bool f_ospi_supports_op(struct spi_mem *mem,
			       const struct spi_mem_op *op)
{
	if (f_ospi_get_dummy_cycle(op) > OSPI_DUMMY_CYCLE_MAX)
		return false;

	if (op->addr.nbytes > 4)
		return false;

	if (!f_ospi_supports_op_width(mem, op))
		return false;

	return spi_mem_default_supports_op(mem, op);
}

static int f_ospi_adjust_op_size(struct spi_mem *mem, struct spi_mem_op *op)
{
	op->data.nbytes = min((int)op->data.nbytes, (int)(OSPI_DAT_SIZE_MAX));

	return 0;
}

static const struct spi_controller_mem_ops f_ospi_mem_ops = {
	.adjust_op_size = f_ospi_adjust_op_size,
	.supports_op = f_ospi_supports_op,
	.exec_op = f_ospi_exec_op,
};

static int f_ospi_init(struct f_ospi *ospi)
{
	int ret;

	ret = f_ospi_prepare_config(ospi);
	if (ret)
		return ret;

	/* Disable boot signal */
	writel(OSPI_ACC_MODE_BOOT_DISABLE, ospi->base + OSPI_ACC_MODE);

	f_ospi_config_dll(ospi);

	/* Disable IRQ */
	f_ospi_clear_irq(ospi);
	f_ospi_disable_irq_status(ospi, OSPI_IRQ_ALL);
	f_ospi_disable_irq_output(ospi, OSPI_IRQ_ALL);

	return f_ospi_unprepare_config(ospi);
}

static int f_ospi_probe(struct platform_device *pdev)
{
	struct spi_controller *ctlr;
	struct device *dev = &pdev->dev;
	struct f_ospi *ospi;
	u32 num_cs = OSPI_NUM_CS;
	int ret;

	ctlr = spi_alloc_master(dev, sizeof(*ospi));
	if (!ctlr)
		return -ENOMEM;

	ctlr->mode_bits = SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL
		| SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL
		| SPI_MODE_0 | SPI_MODE_1 | SPI_LSB_FIRST;
	ctlr->mem_ops = &f_ospi_mem_ops;
	ctlr->bus_num = -1;
	of_property_read_u32(dev->of_node, "num-cs", &num_cs);
	if (num_cs > OSPI_NUM_CS) {
		dev_err(dev, "num-cs too large: %d\n", num_cs);
		return -ENOMEM;
	}
	ctlr->num_chipselect = num_cs;
	ctlr->dev.of_node = dev->of_node;

	ospi = spi_controller_get_devdata(ctlr);
	ospi->dev = dev;

	platform_set_drvdata(pdev, ospi);

	ospi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ospi->base)) {
		ret = PTR_ERR(ospi->base);
		goto err_put_ctlr;
	}

	ospi->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ospi->clk)) {
		ret = PTR_ERR(ospi->clk);
		goto err_put_ctlr;
	}

	ret = clk_prepare_enable(ospi->clk);
	if (ret) {
		dev_err(dev, "Failed to enable the clock\n");
		goto err_disable_clk;
	}

	mutex_init(&ospi->mlock);

	ret = f_ospi_init(ospi);
	if (ret)
		goto err_destroy_mutex;

	ret = devm_spi_register_controller(dev, ctlr);
	if (ret)
		goto err_destroy_mutex;

	return 0;

err_destroy_mutex:
	mutex_destroy(&ospi->mlock);

err_disable_clk:
	clk_disable_unprepare(ospi->clk);

err_put_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

static void f_ospi_remove(struct platform_device *pdev)
{
	struct f_ospi *ospi = platform_get_drvdata(pdev);

	clk_disable_unprepare(ospi->clk);

	mutex_destroy(&ospi->mlock);
}

static const struct of_device_id f_ospi_dt_ids[] = {
	{ .compatible = "socionext,f-ospi" },
	{}
};
MODULE_DEVICE_TABLE(of, f_ospi_dt_ids);

static struct platform_driver f_ospi_driver = {
	.driver = {
		.name = "socionext,f-ospi",
		.of_match_table = f_ospi_dt_ids,
	},
	.probe = f_ospi_probe,
	.remove_new = f_ospi_remove,
};
module_platform_driver(f_ospi_driver);

MODULE_DESCRIPTION("Socionext F_OSPI controller driver");
MODULE_AUTHOR("Socionext Inc.");
MODULE_AUTHOR("Kunihiko Hayashi <hayashi.kunihiko@socionext.com>");
MODULE_LICENSE("GPL");
