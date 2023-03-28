/*
 * Broadcom BCM63XX High Speed SPI Controller driver
 *
 * Copyright 2000-2010 Broadcom Corporation
 * Copyright 2012-2013 Jonas Gorski <jogo@openwrt.org>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/spi/spi-mem.h>
#include <linux/mtd/spi-nor.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>

#define HSSPI_GLOBAL_CTRL_REG			0x0
#define GLOBAL_CTRL_CS_POLARITY_SHIFT		0
#define GLOBAL_CTRL_CS_POLARITY_MASK		0x000000ff
#define GLOBAL_CTRL_PLL_CLK_CTRL_SHIFT		8
#define GLOBAL_CTRL_PLL_CLK_CTRL_MASK		0x0000ff00
#define GLOBAL_CTRL_CLK_GATE_SSOFF		BIT(16)
#define GLOBAL_CTRL_CLK_POLARITY		BIT(17)
#define GLOBAL_CTRL_MOSI_IDLE			BIT(18)

#define HSSPI_GLOBAL_EXT_TRIGGER_REG		0x4

#define HSSPI_INT_STATUS_REG			0x8
#define HSSPI_INT_STATUS_MASKED_REG		0xc
#define HSSPI_INT_MASK_REG			0x10

#define HSSPI_PINGx_CMD_DONE(i)			BIT((i * 8) + 0)
#define HSSPI_PINGx_RX_OVER(i)			BIT((i * 8) + 1)
#define HSSPI_PINGx_TX_UNDER(i)			BIT((i * 8) + 2)
#define HSSPI_PINGx_POLL_TIMEOUT(i)		BIT((i * 8) + 3)
#define HSSPI_PINGx_CTRL_INVAL(i)		BIT((i * 8) + 4)

#define HSSPI_INT_CLEAR_ALL			0xff001f1f

#define HSSPI_PINGPONG_COMMAND_REG(x)		(0x80 + (x) * 0x40)
#define PINGPONG_CMD_COMMAND_MASK		0xf
#define PINGPONG_COMMAND_NOOP			0
#define PINGPONG_COMMAND_START_NOW		1
#define PINGPONG_COMMAND_START_TRIGGER		2
#define PINGPONG_COMMAND_HALT			3
#define PINGPONG_COMMAND_FLUSH			4
#define PINGPONG_CMD_PROFILE_SHIFT		8
#define PINGPONG_CMD_SS_SHIFT			12

#define HSSPI_PINGPONG_STATUS_REG(x)		(0x84 + (x) * 0x40)
#define HSSPI_PINGPONG_STATUS_SRC_BUSY		BIT(1)

#define HSSPI_PROFILE_CLK_CTRL_REG(x)		(0x100 + (x) * 0x20)
#define CLK_CTRL_FREQ_CTRL_MASK			0x0000ffff
#define CLK_CTRL_SPI_CLK_2X_SEL			BIT(14)
#define CLK_CTRL_ACCUM_RST_ON_LOOP		BIT(15)

#define HSSPI_PROFILE_SIGNAL_CTRL_REG(x)	(0x104 + (x) * 0x20)
#define SIGNAL_CTRL_LATCH_RISING		BIT(12)
#define SIGNAL_CTRL_LAUNCH_RISING		BIT(13)
#define SIGNAL_CTRL_ASYNC_INPUT_PATH		BIT(16)

#define HSSPI_PROFILE_MODE_CTRL_REG(x)		(0x108 + (x) * 0x20)
#define MODE_CTRL_MULTIDATA_RD_STRT_SHIFT	8
#define MODE_CTRL_MULTIDATA_WR_STRT_SHIFT	12
#define MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT	16
#define MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT	18
#define MODE_CTRL_MODE_3WIRE			BIT(20)
#define MODE_CTRL_PREPENDBYTE_CNT_SHIFT		24

#define HSSPI_FIFO_REG(x)			(0x200 + (x) * 0x200)


#define HSSPI_OP_MULTIBIT			BIT(11)
#define HSSPI_OP_CODE_SHIFT			13
#define HSSPI_OP_SLEEP				(0 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ_WRITE			(1 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_WRITE				(2 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_READ				(3 << HSSPI_OP_CODE_SHIFT)
#define HSSPI_OP_SETIRQ				(4 << HSSPI_OP_CODE_SHIFT)

#define HSSPI_BUFFER_LEN			512
#define HSSPI_OPCODE_LEN			2

#define HSSPI_MAX_PREPEND_LEN			15

/*
 * Some chip require 30MHz but other require 25MHz. Use smaller value to cover
 * both cases.
 */
#define HSSPI_MAX_SYNC_CLOCK			25000000

#define HSSPI_SPI_MAX_CS			8
#define HSSPI_BUS_NUM				1 /* 0 is legacy SPI */
#define HSSPI_POLL_STATUS_TIMEOUT_MS	100

#define HSSPI_WAIT_MODE_POLLING		0
#define HSSPI_WAIT_MODE_INTR		1
#define HSSPI_WAIT_MODE_MAX			HSSPI_WAIT_MODE_INTR

/*
 * Default transfer mode is auto. If the msg is prependable, use the prepend
 * mode.  If not, falls back to use the dummy cs workaround mode but limit the
 * clock to 25MHz to make sure it works in all board design.
 */
#define HSSPI_XFER_MODE_AUTO		0
#define HSSPI_XFER_MODE_PREPEND		1
#define HSSPI_XFER_MODE_DUMMYCS		2
#define HSSPI_XFER_MODE_MAX			HSSPI_XFER_MODE_DUMMYCS

#define bcm63xx_prepend_printk_on_checkfail(bs, fmt, ...)	\
do {										\
	if (bs->xfer_mode == HSSPI_XFER_MODE_AUTO)				\
		dev_dbg(&bs->pdev->dev, fmt, ##__VA_ARGS__);		\
	else if (bs->xfer_mode == HSSPI_XFER_MODE_PREPEND)		\
		dev_err(&bs->pdev->dev, fmt, ##__VA_ARGS__);		\
} while (0)

struct bcm63xx_hsspi {
	struct completion done;
	struct mutex bus_mutex;
	struct mutex msg_mutex;
	struct platform_device *pdev;
	struct clk *clk;
	struct clk *pll_clk;
	void __iomem *regs;
	u8 __iomem *fifo;

	u32 speed_hz;
	u8 cs_polarity;
	u32 wait_mode;
	u32 xfer_mode;
	u32 prepend_cnt;
	u8 *prepend_buf;
};

static ssize_t wait_mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(ctrl);

	return sprintf(buf, "%d\n", bs->wait_mode);
}

static ssize_t wait_mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(ctrl);
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > HSSPI_WAIT_MODE_MAX) {
		dev_warn(dev, "invalid wait mode %u\n", val);
		return -EINVAL;
	}

	mutex_lock(&bs->msg_mutex);
	bs->wait_mode = val;
	/* clear interrupt status to avoid spurious int on next transfer */
	if (val == HSSPI_WAIT_MODE_INTR)
		__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);
	mutex_unlock(&bs->msg_mutex);

	return count;
}

static DEVICE_ATTR_RW(wait_mode);

static ssize_t xfer_mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(ctrl);

	return sprintf(buf, "%d\n", bs->xfer_mode);
}

static ssize_t xfer_mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct spi_controller *ctrl = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(ctrl);
	u32 val;

	if (kstrtou32(buf, 10, &val))
		return -EINVAL;

	if (val > HSSPI_XFER_MODE_MAX) {
		dev_warn(dev, "invalid xfer mode %u\n", val);
		return -EINVAL;
	}

	mutex_lock(&bs->msg_mutex);
	bs->xfer_mode = val;
	mutex_unlock(&bs->msg_mutex);

	return count;
}

static DEVICE_ATTR_RW(xfer_mode);

static struct attribute *bcm63xx_hsspi_attrs[] = {
	&dev_attr_wait_mode.attr,
	&dev_attr_xfer_mode.attr,
	NULL,
};

static const struct attribute_group bcm63xx_hsspi_group = {
	.attrs = bcm63xx_hsspi_attrs,
};

static void bcm63xx_hsspi_set_clk(struct bcm63xx_hsspi *bs,
				  struct spi_device *spi, int hz);

static size_t bcm63xx_hsspi_max_message_size(struct spi_device *spi)
{
	return HSSPI_BUFFER_LEN - HSSPI_OPCODE_LEN;
}

static int bcm63xx_hsspi_wait_cmd(struct bcm63xx_hsspi *bs)
{
	unsigned long limit;
	u32 reg = 0;
	int rc = 0;

	if (bs->wait_mode == HSSPI_WAIT_MODE_INTR) {
		if (wait_for_completion_timeout(&bs->done, HZ) == 0)
			rc = 1;
	} else {
		/* polling mode checks for status busy bit */
		limit = jiffies + msecs_to_jiffies(HSSPI_POLL_STATUS_TIMEOUT_MS);

		while (!time_after(jiffies, limit)) {
			reg = __raw_readl(bs->regs + HSSPI_PINGPONG_STATUS_REG(0));
			if (reg & HSSPI_PINGPONG_STATUS_SRC_BUSY)
				cpu_relax();
			else
				break;
		}
		if (reg & HSSPI_PINGPONG_STATUS_SRC_BUSY)
			rc = 1;
	}

	if (rc)
		dev_err(&bs->pdev->dev, "transfer timed out!\n");

	return rc;
}

static bool bcm63xx_prepare_prepend_transfer(struct spi_master *master,
					  struct spi_message *msg,
					  struct spi_transfer *t_prepend)
{

	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	bool tx_only = false;
	struct spi_transfer *t;

	/*
	 * Multiple transfers within a message may be combined into one transfer
	 * to the controller using its prepend feature. A SPI message is prependable
	 * only if the following are all true:
	 *   1. One or more half duplex write transfer in single bit mode
	 *   2. Optional full duplex read/write at the end
	 *   3. No delay and cs_change between transfers
	 */
	bs->prepend_cnt = 0;
	list_for_each_entry(t, &msg->transfers, transfer_list) {
		if ((spi_delay_to_ns(&t->delay, t) > 0) || t->cs_change) {
			bcm63xx_prepend_printk_on_checkfail(bs,
				 "Delay or cs change not supported in prepend mode!\n");
			return false;
		}

		tx_only = false;
		if (t->tx_buf && !t->rx_buf) {
			tx_only = true;
			if (bs->prepend_cnt + t->len >
				(HSSPI_BUFFER_LEN - HSSPI_OPCODE_LEN)) {
				bcm63xx_prepend_printk_on_checkfail(bs,
					 "exceed max buf len, abort prepending transfers!\n");
				return false;
			}

			if (t->tx_nbits > SPI_NBITS_SINGLE &&
				!list_is_last(&t->transfer_list, &msg->transfers)) {
				bcm63xx_prepend_printk_on_checkfail(bs,
					 "multi-bit prepend buf not supported!\n");
				return false;
			}

			if (t->tx_nbits == SPI_NBITS_SINGLE) {
				memcpy(bs->prepend_buf + bs->prepend_cnt, t->tx_buf, t->len);
				bs->prepend_cnt += t->len;
			}
		} else {
			if (!list_is_last(&t->transfer_list, &msg->transfers)) {
				bcm63xx_prepend_printk_on_checkfail(bs,
					 "rx/tx_rx transfer not supported when it is not last one!\n");
				return false;
			}
		}

		if (list_is_last(&t->transfer_list, &msg->transfers)) {
			memcpy(t_prepend, t, sizeof(struct spi_transfer));

			if (tx_only && t->tx_nbits == SPI_NBITS_SINGLE) {
				/*
				 * if the last one is also a single bit tx only transfer, merge
				 * all of them into one single tx transfer
				 */
				t_prepend->len = bs->prepend_cnt;
				t_prepend->tx_buf = bs->prepend_buf;
				bs->prepend_cnt = 0;
			} else {
				/*
				 * if the last one is not a tx only transfer or dual tx xfer, all
				 * the previous transfers are sent through prepend bytes and
				 * make sure it does not exceed the max prepend len
				 */
				if (bs->prepend_cnt > HSSPI_MAX_PREPEND_LEN) {
					bcm63xx_prepend_printk_on_checkfail(bs,
						"exceed max prepend len, abort prepending transfers!\n");
					return false;
				}
			}
		}
	}

	return true;
}

static int bcm63xx_hsspi_do_prepend_txrx(struct spi_device *spi,
					 struct spi_transfer *t)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	unsigned int chip_select = spi->chip_select;
	u16 opcode = 0, val;
	const u8 *tx = t->tx_buf;
	u8 *rx = t->rx_buf;
	u32 reg = 0;

	/*
	 * shouldn't happen as we set the max_message_size in the probe.
	 * but check it again in case some driver does not honor the max size
	 */
	if (t->len + bs->prepend_cnt > (HSSPI_BUFFER_LEN - HSSPI_OPCODE_LEN)) {
		dev_warn(&bs->pdev->dev,
			 "Prepend message large than fifo size len %d prepend %d\n",
			 t->len, bs->prepend_cnt);
		return -EINVAL;
	}

	bcm63xx_hsspi_set_clk(bs, spi, t->speed_hz);

	if (tx && rx)
		opcode = HSSPI_OP_READ_WRITE;
	else if (tx)
		opcode = HSSPI_OP_WRITE;
	else if (rx)
		opcode = HSSPI_OP_READ;

	if ((opcode == HSSPI_OP_READ && t->rx_nbits == SPI_NBITS_DUAL) ||
	    (opcode == HSSPI_OP_WRITE && t->tx_nbits == SPI_NBITS_DUAL)) {
		opcode |= HSSPI_OP_MULTIBIT;

		if (t->rx_nbits == SPI_NBITS_DUAL) {
			reg |= 1 << MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT;
			reg |= bs->prepend_cnt << MODE_CTRL_MULTIDATA_RD_STRT_SHIFT;
		}
		if (t->tx_nbits == SPI_NBITS_DUAL) {
			reg |= 1 << MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT;
			reg |= bs->prepend_cnt << MODE_CTRL_MULTIDATA_WR_STRT_SHIFT;
		}
	}

	reg |= bs->prepend_cnt << MODE_CTRL_PREPENDBYTE_CNT_SHIFT;
	__raw_writel(reg | 0xff,
		     bs->regs + HSSPI_PROFILE_MODE_CTRL_REG(chip_select));

	reinit_completion(&bs->done);
	if (bs->prepend_cnt)
		memcpy_toio(bs->fifo + HSSPI_OPCODE_LEN, bs->prepend_buf,
			    bs->prepend_cnt);
	if (tx)
		memcpy_toio(bs->fifo + HSSPI_OPCODE_LEN + bs->prepend_cnt, tx,
			    t->len);

	*(__be16 *)(&val) = cpu_to_be16(opcode | t->len);
	__raw_writew(val, bs->fifo);
	/* enable interrupt */
	if (bs->wait_mode == HSSPI_WAIT_MODE_INTR)
		__raw_writel(HSSPI_PINGx_CMD_DONE(0), bs->regs + HSSPI_INT_MASK_REG);

	/* start the transfer */
	reg = chip_select << PINGPONG_CMD_SS_SHIFT |
	    chip_select << PINGPONG_CMD_PROFILE_SHIFT |
	    PINGPONG_COMMAND_START_NOW;
	__raw_writel(reg, bs->regs + HSSPI_PINGPONG_COMMAND_REG(0));

	if (bcm63xx_hsspi_wait_cmd(bs))
		return -ETIMEDOUT;

	if (rx)
		memcpy_fromio(rx, bs->fifo, t->len);

	return 0;
}

static void bcm63xx_hsspi_set_cs(struct bcm63xx_hsspi *bs, unsigned int cs,
				 bool active)
{
	u32 reg;

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);

	reg &= ~BIT(cs);
	if (active == !(bs->cs_polarity & BIT(cs)))
		reg |= BIT(cs);

	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	mutex_unlock(&bs->bus_mutex);
}

static void bcm63xx_hsspi_set_clk(struct bcm63xx_hsspi *bs,
				  struct spi_device *spi, int hz)
{
	unsigned int profile = spi->chip_select;
	u32 reg;

	reg = DIV_ROUND_UP(2048, DIV_ROUND_UP(bs->speed_hz, hz));
	__raw_writel(CLK_CTRL_ACCUM_RST_ON_LOOP | reg,
		     bs->regs + HSSPI_PROFILE_CLK_CTRL_REG(profile));

	reg = __raw_readl(bs->regs + HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));
	if (hz > HSSPI_MAX_SYNC_CLOCK)
		reg |= SIGNAL_CTRL_ASYNC_INPUT_PATH;
	else
		reg &= ~SIGNAL_CTRL_ASYNC_INPUT_PATH;
	__raw_writel(reg, bs->regs + HSSPI_PROFILE_SIGNAL_CTRL_REG(profile));

	mutex_lock(&bs->bus_mutex);
	/* setup clock polarity */
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	reg &= ~GLOBAL_CTRL_CLK_POLARITY;
	if (spi->mode & SPI_CPOL)
		reg |= GLOBAL_CTRL_CLK_POLARITY;
	__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	mutex_unlock(&bs->bus_mutex);
}

static int bcm63xx_hsspi_do_txrx(struct spi_device *spi, struct spi_transfer *t)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	unsigned int chip_select = spi->chip_select;
	u16 opcode = 0, val;
	int pending = t->len;
	int step_size = HSSPI_BUFFER_LEN;
	const u8 *tx = t->tx_buf;
	u8 *rx = t->rx_buf;
	u32 reg = 0;

	bcm63xx_hsspi_set_clk(bs, spi, t->speed_hz);
	if (!t->cs_off)
		bcm63xx_hsspi_set_cs(bs, spi->chip_select, true);

	if (tx && rx)
		opcode = HSSPI_OP_READ_WRITE;
	else if (tx)
		opcode = HSSPI_OP_WRITE;
	else if (rx)
		opcode = HSSPI_OP_READ;

	if (opcode != HSSPI_OP_READ)
		step_size -= HSSPI_OPCODE_LEN;

	if ((opcode == HSSPI_OP_READ && t->rx_nbits == SPI_NBITS_DUAL) ||
	    (opcode == HSSPI_OP_WRITE && t->tx_nbits == SPI_NBITS_DUAL)) {
		opcode |= HSSPI_OP_MULTIBIT;

		if (t->rx_nbits == SPI_NBITS_DUAL)
			reg |= 1 << MODE_CTRL_MULTIDATA_RD_SIZE_SHIFT;
		if (t->tx_nbits == SPI_NBITS_DUAL)
			reg |= 1 << MODE_CTRL_MULTIDATA_WR_SIZE_SHIFT;
	}

	__raw_writel(reg | 0xff,
		     bs->regs + HSSPI_PROFILE_MODE_CTRL_REG(chip_select));

	while (pending > 0) {
		int curr_step = min_t(int, step_size, pending);

		reinit_completion(&bs->done);
		if (tx) {
			memcpy_toio(bs->fifo + HSSPI_OPCODE_LEN, tx, curr_step);
			tx += curr_step;
		}

		*(__be16 *)(&val) = cpu_to_be16(opcode | curr_step);
		__raw_writew(val, bs->fifo);

		/* enable interrupt */
		if (bs->wait_mode == HSSPI_WAIT_MODE_INTR)
			__raw_writel(HSSPI_PINGx_CMD_DONE(0),
				     bs->regs + HSSPI_INT_MASK_REG);

		reg =  !chip_select << PINGPONG_CMD_SS_SHIFT |
			    chip_select << PINGPONG_CMD_PROFILE_SHIFT |
			    PINGPONG_COMMAND_START_NOW;
		__raw_writel(reg, bs->regs + HSSPI_PINGPONG_COMMAND_REG(0));

		if (bcm63xx_hsspi_wait_cmd(bs))
			return -ETIMEDOUT;

		if (rx) {
			memcpy_fromio(rx, bs->fifo, curr_step);
			rx += curr_step;
		}

		pending -= curr_step;
	}

	return 0;
}

static int bcm63xx_hsspi_setup(struct spi_device *spi)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	u32 reg;

	reg = __raw_readl(bs->regs +
			  HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));
	reg &= ~(SIGNAL_CTRL_LAUNCH_RISING | SIGNAL_CTRL_LATCH_RISING);
	if (spi->mode & SPI_CPHA)
		reg |= SIGNAL_CTRL_LAUNCH_RISING;
	else
		reg |= SIGNAL_CTRL_LATCH_RISING;
	__raw_writel(reg, bs->regs +
		     HSSPI_PROFILE_SIGNAL_CTRL_REG(spi->chip_select));

	mutex_lock(&bs->bus_mutex);
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);

	/* only change actual polarities if there is no transfer */
	if ((reg & GLOBAL_CTRL_CS_POLARITY_MASK) == bs->cs_polarity) {
		if (spi->mode & SPI_CS_HIGH)
			reg |= BIT(spi->chip_select);
		else
			reg &= ~BIT(spi->chip_select);
		__raw_writel(reg, bs->regs + HSSPI_GLOBAL_CTRL_REG);
	}

	if (spi->mode & SPI_CS_HIGH)
		bs->cs_polarity |= BIT(spi->chip_select);
	else
		bs->cs_polarity &= ~BIT(spi->chip_select);

	mutex_unlock(&bs->bus_mutex);

	return 0;
}

static int bcm63xx_hsspi_do_dummy_cs_txrx(struct spi_device *spi,
				      struct spi_message *msg)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(spi->master);
	int status = -EINVAL;
	int dummy_cs;
	bool keep_cs = false;
	struct spi_transfer *t;

	/*
	 * This controller does not support keeping CS active during idle.
	 * To work around this, we use the following ugly hack:
	 *
	 * a. Invert the target chip select's polarity so it will be active.
	 * b. Select a "dummy" chip select to use as the hardware target.
	 * c. Invert the dummy chip select's polarity so it will be inactive
	 *    during the actual transfers.
	 * d. Tell the hardware to send to the dummy chip select. Thanks to
	 *    the multiplexed nature of SPI the actual target will receive
	 *    the transfer and we see its response.
	 *
	 * e. At the end restore the polarities again to their default values.
	 */

	dummy_cs = !spi->chip_select;
	bcm63xx_hsspi_set_cs(bs, dummy_cs, true);

	list_for_each_entry(t, &msg->transfers, transfer_list) {
		/*
		 * We are here because one of reasons below:
		 * a. Message is not prependable and in default auto xfer mode. This mean
		 *    we fallback to dummy cs mode at maximum 25MHz safe clock rate.
		 * b. User set to use the dummy cs mode.
		 */
		if (bs->xfer_mode == HSSPI_XFER_MODE_AUTO) {
			if (t->speed_hz > HSSPI_MAX_SYNC_CLOCK) {
				t->speed_hz = HSSPI_MAX_SYNC_CLOCK;
				dev_warn_once(&bs->pdev->dev,
					"Force to dummy cs mode. Reduce the speed to %dHz",
					t->speed_hz);
			}
		}

		status = bcm63xx_hsspi_do_txrx(spi, t);
		if (status)
			break;

		msg->actual_length += t->len;

		spi_transfer_delay_exec(t);

		/* use existing cs change logic from spi_transfer_one_message */
		if (t->cs_change) {
			if (list_is_last(&t->transfer_list, &msg->transfers)) {
				keep_cs = true;
			} else {
				if (!t->cs_off)
					bcm63xx_hsspi_set_cs(bs, spi->chip_select, false);

				spi_transfer_cs_change_delay_exec(msg, t);

				if (!list_next_entry(t, transfer_list)->cs_off)
					bcm63xx_hsspi_set_cs(bs, spi->chip_select, true);
			}
		} else if (!list_is_last(&t->transfer_list, &msg->transfers) &&
			   t->cs_off != list_next_entry(t, transfer_list)->cs_off) {
			bcm63xx_hsspi_set_cs(bs, spi->chip_select, t->cs_off);
		}
	}

	bcm63xx_hsspi_set_cs(bs, dummy_cs, false);
	if (status || !keep_cs)
		bcm63xx_hsspi_set_cs(bs, spi->chip_select, false);

	return status;
}

static int bcm63xx_hsspi_transfer_one(struct spi_master *master,
				      struct spi_message *msg)
{
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	struct spi_device *spi = msg->spi;
	int status = -EINVAL;
	bool prependable = false;
	struct spi_transfer t_prepend;

	mutex_lock(&bs->msg_mutex);

	if (bs->xfer_mode != HSSPI_XFER_MODE_DUMMYCS)
		prependable = bcm63xx_prepare_prepend_transfer(master, msg, &t_prepend);

	if (prependable) {
		status = bcm63xx_hsspi_do_prepend_txrx(spi, &t_prepend);
		msg->actual_length = (t_prepend.len + bs->prepend_cnt);
	} else {
		if (bs->xfer_mode == HSSPI_XFER_MODE_PREPEND) {
			dev_err(&bs->pdev->dev,
				"User sets prepend mode but msg not prependable! Abort transfer\n");
			status = -EINVAL;
		} else
			status = bcm63xx_hsspi_do_dummy_cs_txrx(spi, msg);
	}

	mutex_unlock(&bs->msg_mutex);
	msg->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static bool bcm63xx_hsspi_mem_supports_op(struct spi_mem *mem,
			    const struct spi_mem_op *op)
{
	if (!spi_mem_default_supports_op(mem, op))
		return false;

	/* Controller doesn't support spi mem dual io mode */
	if ((op->cmd.opcode == SPINOR_OP_READ_1_2_2) ||
		(op->cmd.opcode == SPINOR_OP_READ_1_2_2_4B) ||
		(op->cmd.opcode == SPINOR_OP_READ_1_2_2_DTR) ||
		(op->cmd.opcode == SPINOR_OP_READ_1_2_2_DTR_4B))
		return false;

	return true;
}

static const struct spi_controller_mem_ops bcm63xx_hsspi_mem_ops = {
	.supports_op = bcm63xx_hsspi_mem_supports_op,
};

static irqreturn_t bcm63xx_hsspi_interrupt(int irq, void *dev_id)
{
	struct bcm63xx_hsspi *bs = (struct bcm63xx_hsspi *)dev_id;

	if (__raw_readl(bs->regs + HSSPI_INT_STATUS_MASKED_REG) == 0)
		return IRQ_NONE;

	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	complete(&bs->done);

	return IRQ_HANDLED;
}

static int bcm63xx_hsspi_probe(struct platform_device *pdev)
{
	struct spi_master *master;
	struct bcm63xx_hsspi *bs;
	void __iomem *regs;
	struct device *dev = &pdev->dev;
	struct clk *clk, *pll_clk = NULL;
	int irq, ret;
	u32 reg, rate, num_cs = HSSPI_SPI_MAX_CS;
	struct reset_control *reset;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	clk = devm_clk_get(dev, "hsspi");

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	reset = devm_reset_control_get_optional_exclusive(dev, NULL);
	if (IS_ERR(reset))
		return PTR_ERR(reset);

	ret = clk_prepare_enable(clk);
	if (ret)
		return ret;

	ret = reset_control_reset(reset);
	if (ret) {
		dev_err(dev, "unable to reset device: %d\n", ret);
		goto out_disable_clk;
	}

	rate = clk_get_rate(clk);
	if (!rate) {
		pll_clk = devm_clk_get(dev, "pll");

		if (IS_ERR(pll_clk)) {
			ret = PTR_ERR(pll_clk);
			goto out_disable_clk;
		}

		ret = clk_prepare_enable(pll_clk);
		if (ret)
			goto out_disable_clk;

		rate = clk_get_rate(pll_clk);
		if (!rate) {
			ret = -EINVAL;
			goto out_disable_pll_clk;
		}
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*bs));
	if (!master) {
		ret = -ENOMEM;
		goto out_disable_pll_clk;
	}

	bs = spi_master_get_devdata(master);
	bs->pdev = pdev;
	bs->clk = clk;
	bs->pll_clk = pll_clk;
	bs->regs = regs;
	bs->speed_hz = rate;
	bs->fifo = (u8 __iomem *)(bs->regs + HSSPI_FIFO_REG(0));
	bs->wait_mode = HSSPI_WAIT_MODE_POLLING;
	bs->prepend_buf = devm_kzalloc(dev, HSSPI_BUFFER_LEN, GFP_KERNEL);
	if (!bs->prepend_buf) {
		ret = -ENOMEM;
		goto out_put_master;
	}

	mutex_init(&bs->bus_mutex);
	mutex_init(&bs->msg_mutex);
	init_completion(&bs->done);

	master->mem_ops = &bcm63xx_hsspi_mem_ops;
	master->dev.of_node = dev->of_node;
	if (!dev->of_node)
		master->bus_num = HSSPI_BUS_NUM;

	of_property_read_u32(dev->of_node, "num-cs", &num_cs);
	if (num_cs > 8) {
		dev_warn(dev, "unsupported number of cs (%i), reducing to 8\n",
			 num_cs);
		num_cs = HSSPI_SPI_MAX_CS;
	}
	master->num_chipselect = num_cs;
	master->setup = bcm63xx_hsspi_setup;
	master->transfer_one_message = bcm63xx_hsspi_transfer_one;
	master->max_transfer_size = bcm63xx_hsspi_max_message_size;
	master->max_message_size = bcm63xx_hsspi_max_message_size;

	master->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH |
			    SPI_RX_DUAL | SPI_TX_DUAL;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->auto_runtime_pm = true;

	platform_set_drvdata(pdev, master);

	/* Initialize the hardware */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);

	/* clean up any pending interrupts */
	__raw_writel(HSSPI_INT_CLEAR_ALL, bs->regs + HSSPI_INT_STATUS_REG);

	/* read out default CS polarities */
	reg = __raw_readl(bs->regs + HSSPI_GLOBAL_CTRL_REG);
	bs->cs_polarity = reg & GLOBAL_CTRL_CS_POLARITY_MASK;
	__raw_writel(reg | GLOBAL_CTRL_CLK_GATE_SSOFF,
		     bs->regs + HSSPI_GLOBAL_CTRL_REG);

	if (irq > 0) {
		ret = devm_request_irq(dev, irq, bcm63xx_hsspi_interrupt, IRQF_SHARED,
				       pdev->name, bs);

		if (ret)
			goto out_put_master;
	}

	pm_runtime_enable(&pdev->dev);

	ret = sysfs_create_group(&pdev->dev.kobj, &bcm63xx_hsspi_group);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register sysfs group\n");
		goto out_pm_disable;
	}

	/* register and we are done */
	ret = devm_spi_register_master(dev, master);
	if (ret)
		goto out_sysgroup_disable;

	dev_info(dev, "Broadcom 63XX High Speed SPI Controller driver");

	return 0;

out_sysgroup_disable:
	sysfs_remove_group(&pdev->dev.kobj, &bcm63xx_hsspi_group);
out_pm_disable:
	pm_runtime_disable(&pdev->dev);
out_put_master:
	spi_master_put(master);
out_disable_pll_clk:
	clk_disable_unprepare(pll_clk);
out_disable_clk:
	clk_disable_unprepare(clk);
	return ret;
}


static int bcm63xx_hsspi_remove(struct platform_device *pdev)
{
	struct spi_master *master = platform_get_drvdata(pdev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	/* reset the hardware and block queue progress */
	__raw_writel(0, bs->regs + HSSPI_INT_MASK_REG);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);
	sysfs_remove_group(&pdev->dev.kobj, &bcm63xx_hsspi_group);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bcm63xx_hsspi_suspend(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);

	spi_master_suspend(master);
	clk_disable_unprepare(bs->pll_clk);
	clk_disable_unprepare(bs->clk);

	return 0;
}

static int bcm63xx_hsspi_resume(struct device *dev)
{
	struct spi_master *master = dev_get_drvdata(dev);
	struct bcm63xx_hsspi *bs = spi_master_get_devdata(master);
	int ret;

	ret = clk_prepare_enable(bs->clk);
	if (ret)
		return ret;

	if (bs->pll_clk) {
		ret = clk_prepare_enable(bs->pll_clk);
		if (ret) {
			clk_disable_unprepare(bs->clk);
			return ret;
		}
	}

	spi_master_resume(master);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(bcm63xx_hsspi_pm_ops, bcm63xx_hsspi_suspend,
			 bcm63xx_hsspi_resume);

static const struct of_device_id bcm63xx_hsspi_of_match[] = {
	{ .compatible = "brcm,bcm6328-hsspi", },
	{ .compatible = "brcm,bcmbca-hsspi-v1.0", },
	{ },
};
MODULE_DEVICE_TABLE(of, bcm63xx_hsspi_of_match);

static struct platform_driver bcm63xx_hsspi_driver = {
	.driver = {
		.name	= "bcm63xx-hsspi",
		.pm	= &bcm63xx_hsspi_pm_ops,
		.of_match_table = bcm63xx_hsspi_of_match,
	},
	.probe		= bcm63xx_hsspi_probe,
	.remove		= bcm63xx_hsspi_remove,
};

module_platform_driver(bcm63xx_hsspi_driver);

MODULE_ALIAS("platform:bcm63xx_hsspi");
MODULE_DESCRIPTION("Broadcom BCM63xx High Speed SPI Controller driver");
MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_LICENSE("GPL");
