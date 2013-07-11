/*
 * Freescale MXS I2C bus driver
 *
 * Copyright (C) 2011-2012 Wolfram Sang, Pengutronix e.K.
 *
 * based on a (non-working) driver which was:
 *
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/pinctrl/consumer.h>
#include <linux/stmp_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>

#define DRIVER_NAME "mxs-i2c"

#define MXS_I2C_CTRL0		(0x00)
#define MXS_I2C_CTRL0_SET	(0x04)

#define MXS_I2C_CTRL0_SFTRST			0x80000000
#define MXS_I2C_CTRL0_RUN			0x20000000
#define MXS_I2C_CTRL0_SEND_NAK_ON_LAST		0x02000000
#define MXS_I2C_CTRL0_RETAIN_CLOCK		0x00200000
#define MXS_I2C_CTRL0_POST_SEND_STOP		0x00100000
#define MXS_I2C_CTRL0_PRE_SEND_START		0x00080000
#define MXS_I2C_CTRL0_MASTER_MODE		0x00020000
#define MXS_I2C_CTRL0_DIRECTION			0x00010000
#define MXS_I2C_CTRL0_XFER_COUNT(v)		((v) & 0x0000FFFF)

#define MXS_I2C_TIMING0		(0x10)
#define MXS_I2C_TIMING1		(0x20)
#define MXS_I2C_TIMING2		(0x30)

#define MXS_I2C_CTRL1		(0x40)
#define MXS_I2C_CTRL1_SET	(0x44)
#define MXS_I2C_CTRL1_CLR	(0x48)

#define MXS_I2C_CTRL1_CLR_GOT_A_NAK		0x10000000
#define MXS_I2C_CTRL1_BUS_FREE_IRQ		0x80
#define MXS_I2C_CTRL1_DATA_ENGINE_CMPLT_IRQ	0x40
#define MXS_I2C_CTRL1_NO_SLAVE_ACK_IRQ		0x20
#define MXS_I2C_CTRL1_OVERSIZE_XFER_TERM_IRQ	0x10
#define MXS_I2C_CTRL1_EARLY_TERM_IRQ		0x08
#define MXS_I2C_CTRL1_MASTER_LOSS_IRQ		0x04
#define MXS_I2C_CTRL1_SLAVE_STOP_IRQ		0x02
#define MXS_I2C_CTRL1_SLAVE_IRQ			0x01

#define MXS_I2C_STAT		(0x50)
#define MXS_I2C_STAT_BUS_BUSY			0x00000800
#define MXS_I2C_STAT_CLK_GEN_BUSY		0x00000400

#define MXS_I2C_DATA		(0xa0)

#define MXS_I2C_DEBUG0		(0xb0)
#define MXS_I2C_DEBUG0_CLR	(0xb8)

#define MXS_I2C_DEBUG0_DMAREQ	0x80000000

#define MXS_I2C_IRQ_MASK	(MXS_I2C_CTRL1_DATA_ENGINE_CMPLT_IRQ | \
				 MXS_I2C_CTRL1_NO_SLAVE_ACK_IRQ | \
				 MXS_I2C_CTRL1_EARLY_TERM_IRQ | \
				 MXS_I2C_CTRL1_MASTER_LOSS_IRQ | \
				 MXS_I2C_CTRL1_SLAVE_STOP_IRQ | \
				 MXS_I2C_CTRL1_SLAVE_IRQ)


#define MXS_CMD_I2C_SELECT	(MXS_I2C_CTRL0_RETAIN_CLOCK |	\
				 MXS_I2C_CTRL0_PRE_SEND_START |	\
				 MXS_I2C_CTRL0_MASTER_MODE |	\
				 MXS_I2C_CTRL0_DIRECTION |	\
				 MXS_I2C_CTRL0_XFER_COUNT(1))

#define MXS_CMD_I2C_WRITE	(MXS_I2C_CTRL0_PRE_SEND_START |	\
				 MXS_I2C_CTRL0_MASTER_MODE |	\
				 MXS_I2C_CTRL0_DIRECTION)

#define MXS_CMD_I2C_READ	(MXS_I2C_CTRL0_SEND_NAK_ON_LAST | \
				 MXS_I2C_CTRL0_MASTER_MODE)

/**
 * struct mxs_i2c_dev - per device, private MXS-I2C data
 *
 * @dev: driver model device node
 * @regs: IO registers pointer
 * @cmd_complete: completion object for transaction wait
 * @cmd_err: error code for last transaction
 * @adapter: i2c subsystem adapter node
 */
struct mxs_i2c_dev {
	struct device *dev;
	void __iomem *regs;
	struct completion cmd_complete;
	int cmd_err;
	struct i2c_adapter adapter;

	uint32_t timing0;
	uint32_t timing1;

	/* DMA support components */
	struct dma_chan         	*dmach;
	uint32_t			pio_data[2];
	uint32_t			addr_data;
	struct scatterlist		sg_io[2];
	bool				dma_read;
};

static void mxs_i2c_reset(struct mxs_i2c_dev *i2c)
{
	stmp_reset_block(i2c->regs);

	/*
	 * Configure timing for the I2C block. The I2C TIMING2 register has to
	 * be programmed with this particular magic number. The rest is derived
	 * from the XTAL speed and requested I2C speed.
	 *
	 * For details, see i.MX233 [25.4.2 - 25.4.4] and i.MX28 [27.5.2 - 27.5.4].
	 */
	writel(i2c->timing0, i2c->regs + MXS_I2C_TIMING0);
	writel(i2c->timing1, i2c->regs + MXS_I2C_TIMING1);
	writel(0x00300030, i2c->regs + MXS_I2C_TIMING2);

	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_SET);
}

static void mxs_i2c_dma_finish(struct mxs_i2c_dev *i2c)
{
	if (i2c->dma_read) {
		dma_unmap_sg(i2c->dev, &i2c->sg_io[0], 1, DMA_TO_DEVICE);
		dma_unmap_sg(i2c->dev, &i2c->sg_io[1], 1, DMA_FROM_DEVICE);
	} else {
		dma_unmap_sg(i2c->dev, i2c->sg_io, 2, DMA_TO_DEVICE);
	}
}

static void mxs_i2c_dma_irq_callback(void *param)
{
	struct mxs_i2c_dev *i2c = param;

	complete(&i2c->cmd_complete);
	mxs_i2c_dma_finish(i2c);
}

static int mxs_i2c_dma_setup_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msg, uint32_t flags)
{
	struct dma_async_tx_descriptor *desc;
	struct mxs_i2c_dev *i2c = i2c_get_adapdata(adap);

	if (msg->flags & I2C_M_RD) {
		i2c->dma_read = 1;
		i2c->addr_data = (msg->addr << 1) | I2C_SMBUS_READ;

		/*
		 * SELECT command.
		 */

		/* Queue the PIO register write transfer. */
		i2c->pio_data[0] = MXS_CMD_I2C_SELECT;
		desc = dmaengine_prep_slave_sg(i2c->dmach,
					(struct scatterlist *)&i2c->pio_data[0],
					1, DMA_TRANS_NONE, 0);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get PIO reg. write descriptor.\n");
			goto select_init_pio_fail;
		}

		/* Queue the DMA data transfer. */
		sg_init_one(&i2c->sg_io[0], &i2c->addr_data, 1);
		dma_map_sg(i2c->dev, &i2c->sg_io[0], 1, DMA_TO_DEVICE);
		desc = dmaengine_prep_slave_sg(i2c->dmach, &i2c->sg_io[0], 1,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get DMA data write descriptor.\n");
			goto select_init_dma_fail;
		}

		/*
		 * READ command.
		 */

		/* Queue the PIO register write transfer. */
		i2c->pio_data[1] = flags | MXS_CMD_I2C_READ |
				MXS_I2C_CTRL0_XFER_COUNT(msg->len);
		desc = dmaengine_prep_slave_sg(i2c->dmach,
					(struct scatterlist *)&i2c->pio_data[1],
					1, DMA_TRANS_NONE, DMA_PREP_INTERRUPT);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get PIO reg. write descriptor.\n");
			goto select_init_dma_fail;
		}

		/* Queue the DMA data transfer. */
		sg_init_one(&i2c->sg_io[1], msg->buf, msg->len);
		dma_map_sg(i2c->dev, &i2c->sg_io[1], 1, DMA_FROM_DEVICE);
		desc = dmaengine_prep_slave_sg(i2c->dmach, &i2c->sg_io[1], 1,
					DMA_DEV_TO_MEM,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get DMA data write descriptor.\n");
			goto read_init_dma_fail;
		}
	} else {
		i2c->dma_read = 0;
		i2c->addr_data = (msg->addr << 1) | I2C_SMBUS_WRITE;

		/*
		 * WRITE command.
		 */

		/* Queue the PIO register write transfer. */
		i2c->pio_data[0] = flags | MXS_CMD_I2C_WRITE |
				MXS_I2C_CTRL0_XFER_COUNT(msg->len + 1);
		desc = dmaengine_prep_slave_sg(i2c->dmach,
					(struct scatterlist *)&i2c->pio_data[0],
					1, DMA_TRANS_NONE, 0);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get PIO reg. write descriptor.\n");
			goto write_init_pio_fail;
		}

		/* Queue the DMA data transfer. */
		sg_init_table(i2c->sg_io, 2);
		sg_set_buf(&i2c->sg_io[0], &i2c->addr_data, 1);
		sg_set_buf(&i2c->sg_io[1], msg->buf, msg->len);
		dma_map_sg(i2c->dev, i2c->sg_io, 2, DMA_TO_DEVICE);
		desc = dmaengine_prep_slave_sg(i2c->dmach, i2c->sg_io, 2,
					DMA_MEM_TO_DEV,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get DMA data write descriptor.\n");
			goto write_init_dma_fail;
		}
	}

	/*
	 * The last descriptor must have this callback,
	 * to finish the DMA transaction.
	 */
	desc->callback = mxs_i2c_dma_irq_callback;
	desc->callback_param = i2c;

	/* Start the transfer. */
	dmaengine_submit(desc);
	dma_async_issue_pending(i2c->dmach);
	return 0;

/* Read failpath. */
read_init_dma_fail:
	dma_unmap_sg(i2c->dev, &i2c->sg_io[1], 1, DMA_FROM_DEVICE);
select_init_dma_fail:
	dma_unmap_sg(i2c->dev, &i2c->sg_io[0], 1, DMA_TO_DEVICE);
select_init_pio_fail:
	dmaengine_terminate_all(i2c->dmach);
	return -EINVAL;

/* Write failpath. */
write_init_dma_fail:
	dma_unmap_sg(i2c->dev, i2c->sg_io, 2, DMA_TO_DEVICE);
write_init_pio_fail:
	dmaengine_terminate_all(i2c->dmach);
	return -EINVAL;
}

static int mxs_i2c_pio_wait_dmareq(struct mxs_i2c_dev *i2c)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	while (!(readl(i2c->regs + MXS_I2C_DEBUG0) &
		MXS_I2C_DEBUG0_DMAREQ)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cond_resched();
	}

	return 0;
}

static int mxs_i2c_pio_wait_cplt(struct mxs_i2c_dev *i2c, int last)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/*
	 * We do not use interrupts in the PIO mode. Due to the
	 * maximum transfer length being 8 bytes in PIO mode, the
	 * overhead of interrupt would be too large and this would
	 * neglect the gain from using the PIO mode.
	 */

	while (!(readl(i2c->regs + MXS_I2C_CTRL1) &
		MXS_I2C_CTRL1_DATA_ENGINE_CMPLT_IRQ)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cond_resched();
	}

	writel(MXS_I2C_CTRL1_DATA_ENGINE_CMPLT_IRQ,
		i2c->regs + MXS_I2C_CTRL1_CLR);

	/*
	 * When ending a transfer with a stop, we have to wait for the bus to
	 * go idle before we report the transfer as completed. Otherwise the
	 * start of the next transfer may race with the end of the current one.
	 */
	while (last && (readl(i2c->regs + MXS_I2C_STAT) &
			(MXS_I2C_STAT_BUS_BUSY | MXS_I2C_STAT_CLK_GEN_BUSY))) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cond_resched();
	}

	return 0;
}

static int mxs_i2c_pio_check_error_state(struct mxs_i2c_dev *i2c)
{
	u32 state;

	state = readl(i2c->regs + MXS_I2C_CTRL1_CLR) & MXS_I2C_IRQ_MASK;

	if (state & MXS_I2C_CTRL1_NO_SLAVE_ACK_IRQ)
		i2c->cmd_err = -ENXIO;
	else if (state & (MXS_I2C_CTRL1_EARLY_TERM_IRQ |
			  MXS_I2C_CTRL1_MASTER_LOSS_IRQ |
			  MXS_I2C_CTRL1_SLAVE_STOP_IRQ |
			  MXS_I2C_CTRL1_SLAVE_IRQ))
		i2c->cmd_err = -EIO;

	return i2c->cmd_err;
}

static void mxs_i2c_pio_trigger_cmd(struct mxs_i2c_dev *i2c, u32 cmd)
{
	u32 reg;

	writel(cmd, i2c->regs + MXS_I2C_CTRL0);

	/* readback makes sure the write is latched into hardware */
	reg = readl(i2c->regs + MXS_I2C_CTRL0);
	reg |= MXS_I2C_CTRL0_RUN;
	writel(reg, i2c->regs + MXS_I2C_CTRL0);
}

static int mxs_i2c_pio_setup_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msg, uint32_t flags)
{
	struct mxs_i2c_dev *i2c = i2c_get_adapdata(adap);
	uint32_t addr_data = msg->addr << 1;
	uint32_t data = 0;
	int i, shifts_left, ret;

	/* Mute IRQs coming from this block. */
	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_CLR);

	if (msg->flags & I2C_M_RD) {
		addr_data |= I2C_SMBUS_READ;

		/* SELECT command. */
		mxs_i2c_pio_trigger_cmd(i2c, MXS_CMD_I2C_SELECT);

		ret = mxs_i2c_pio_wait_dmareq(i2c);
		if (ret)
			return ret;

		writel(addr_data, i2c->regs + MXS_I2C_DATA);
		writel(MXS_I2C_DEBUG0_DMAREQ, i2c->regs + MXS_I2C_DEBUG0_CLR);

		ret = mxs_i2c_pio_wait_cplt(i2c, 0);
		if (ret)
			return ret;

		if (mxs_i2c_pio_check_error_state(i2c))
			goto cleanup;

		/* READ command. */
		mxs_i2c_pio_trigger_cmd(i2c,
					MXS_CMD_I2C_READ | flags |
					MXS_I2C_CTRL0_XFER_COUNT(msg->len));

		for (i = 0; i < msg->len; i++) {
			if ((i & 3) == 0) {
				ret = mxs_i2c_pio_wait_dmareq(i2c);
				if (ret)
					return ret;
				data = readl(i2c->regs + MXS_I2C_DATA);
				writel(MXS_I2C_DEBUG0_DMAREQ,
				       i2c->regs + MXS_I2C_DEBUG0_CLR);
			}
			msg->buf[i] = data & 0xff;
			data >>= 8;
		}
	} else {
		addr_data |= I2C_SMBUS_WRITE;

		/* WRITE command. */
		mxs_i2c_pio_trigger_cmd(i2c,
					MXS_CMD_I2C_WRITE | flags |
					MXS_I2C_CTRL0_XFER_COUNT(msg->len + 1));

		/*
		 * The LSB of data buffer is the first byte blasted across
		 * the bus. Higher order bytes follow. Thus the following
		 * filling schematic.
		 */
		data = addr_data << 24;
		for (i = 0; i < msg->len; i++) {
			data >>= 8;
			data |= (msg->buf[i] << 24);
			if ((i & 3) == 2) {
				ret = mxs_i2c_pio_wait_dmareq(i2c);
				if (ret)
					return ret;
				writel(data, i2c->regs + MXS_I2C_DATA);
				writel(MXS_I2C_DEBUG0_DMAREQ,
				       i2c->regs + MXS_I2C_DEBUG0_CLR);
			}
		}

		shifts_left = 24 - (i & 3) * 8;
		if (shifts_left) {
			data >>= shifts_left;
			ret = mxs_i2c_pio_wait_dmareq(i2c);
			if (ret)
				return ret;
			writel(data, i2c->regs + MXS_I2C_DATA);
			writel(MXS_I2C_DEBUG0_DMAREQ,
			       i2c->regs + MXS_I2C_DEBUG0_CLR);
		}
	}

	ret = mxs_i2c_pio_wait_cplt(i2c, flags & MXS_I2C_CTRL0_POST_SEND_STOP);
	if (ret)
		return ret;

	/* make sure we capture any occurred error into cmd_err */
	mxs_i2c_pio_check_error_state(i2c);

cleanup:
	/* Clear any dangling IRQs and re-enable interrupts. */
	writel(MXS_I2C_IRQ_MASK, i2c->regs + MXS_I2C_CTRL1_CLR);
	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_SET);

	return 0;
}

/*
 * Low level master read/write transaction.
 */
static int mxs_i2c_xfer_msg(struct i2c_adapter *adap, struct i2c_msg *msg,
				int stop)
{
	struct mxs_i2c_dev *i2c = i2c_get_adapdata(adap);
	int ret;
	int flags;

	flags = stop ? MXS_I2C_CTRL0_POST_SEND_STOP : 0;

	dev_dbg(i2c->dev, "addr: 0x%04x, len: %d, flags: 0x%x, stop: %d\n",
		msg->addr, msg->len, msg->flags, stop);

	if (msg->len == 0)
		return -EINVAL;

	/*
	 * The current boundary to select between PIO/DMA transfer method
	 * is set to 8 bytes, transfers shorter than 8 bytes are transfered
	 * using PIO mode while longer transfers use DMA. The 8 byte border is
	 * based on this empirical measurement and a lot of previous frobbing.
	 */
	i2c->cmd_err = 0;
	if (0) {	/* disable PIO mode until a proper fix is made */
		ret = mxs_i2c_pio_setup_xfer(adap, msg, flags);
		if (ret)
			mxs_i2c_reset(i2c);
	} else {
		INIT_COMPLETION(i2c->cmd_complete);
		ret = mxs_i2c_dma_setup_xfer(adap, msg, flags);
		if (ret)
			return ret;

		ret = wait_for_completion_timeout(&i2c->cmd_complete,
						msecs_to_jiffies(1000));
		if (ret == 0)
			goto timeout;
	}

	if (i2c->cmd_err == -ENXIO) {
		/*
		 * If the transfer fails with a NAK from the slave the
		 * controller halts until it gets told to return to idle state.
		 */
		writel(MXS_I2C_CTRL1_CLR_GOT_A_NAK,
		       i2c->regs + MXS_I2C_CTRL1_SET);
	}

	ret = i2c->cmd_err;

	dev_dbg(i2c->dev, "Done with err=%d\n", ret);

	return ret;

timeout:
	dev_dbg(i2c->dev, "Timeout!\n");
	mxs_i2c_dma_finish(i2c);
	mxs_i2c_reset(i2c);
	return -ETIMEDOUT;
}

static int mxs_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[],
			int num)
{
	int i;
	int err;

	for (i = 0; i < num; i++) {
		err = mxs_i2c_xfer_msg(adap, &msgs[i], i == (num - 1));
		if (err)
			return err;
	}

	return num;
}

static u32 mxs_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static irqreturn_t mxs_i2c_isr(int this_irq, void *dev_id)
{
	struct mxs_i2c_dev *i2c = dev_id;
	u32 stat = readl(i2c->regs + MXS_I2C_CTRL1) & MXS_I2C_IRQ_MASK;

	if (!stat)
		return IRQ_NONE;

	if (stat & MXS_I2C_CTRL1_NO_SLAVE_ACK_IRQ)
		i2c->cmd_err = -ENXIO;
	else if (stat & (MXS_I2C_CTRL1_EARLY_TERM_IRQ |
		    MXS_I2C_CTRL1_MASTER_LOSS_IRQ |
		    MXS_I2C_CTRL1_SLAVE_STOP_IRQ | MXS_I2C_CTRL1_SLAVE_IRQ))
		/* MXS_I2C_CTRL1_OVERSIZE_XFER_TERM_IRQ is only for slaves */
		i2c->cmd_err = -EIO;

	writel(stat, i2c->regs + MXS_I2C_CTRL1_CLR);

	return IRQ_HANDLED;
}

static const struct i2c_algorithm mxs_i2c_algo = {
	.master_xfer = mxs_i2c_xfer,
	.functionality = mxs_i2c_func,
};

static void mxs_i2c_derive_timing(struct mxs_i2c_dev *i2c, int speed)
{
	/* The I2C block clock run at 24MHz */
	const uint32_t clk = 24000000;
	uint32_t base;
	uint16_t high_count, low_count, rcv_count, xmit_count;
	struct device *dev = i2c->dev;

	if (speed > 540000) {
		dev_warn(dev, "Speed too high (%d Hz), using 540 kHz\n", speed);
		speed = 540000;
	} else if (speed < 12000) {
		dev_warn(dev, "Speed too low (%d Hz), using 12 kHz\n", speed);
		speed = 12000;
	}

	/*
	 * The timing derivation algorithm. There is no documentation for this
	 * algorithm available, it was derived by using the scope and fiddling
	 * with constants until the result observed on the scope was good enough
	 * for 20kHz, 50kHz, 100kHz, 200kHz, 300kHz and 400kHz. It should be
	 * possible to assume the algorithm works for other frequencies as well.
	 *
	 * Note it was necessary to cap the frequency on both ends as it's not
	 * possible to configure completely arbitrary frequency for the I2C bus
	 * clock.
	 */
	base = ((clk / speed) - 38) / 2;
	high_count = base + 3;
	low_count = base - 3;
	rcv_count = (high_count * 3) / 4;
	xmit_count = low_count / 4;

	i2c->timing0 = (high_count << 16) | rcv_count;
	i2c->timing1 = (low_count << 16) | xmit_count;
}

static int mxs_i2c_get_ofdata(struct mxs_i2c_dev *i2c)
{
	uint32_t speed;
	struct device *dev = i2c->dev;
	struct device_node *node = dev->of_node;
	int ret;

	ret = of_property_read_u32(node, "clock-frequency", &speed);
	if (ret) {
		dev_warn(dev, "No I2C speed selected, using 100kHz\n");
		speed = 100000;
	}

	mxs_i2c_derive_timing(i2c, speed);

	return 0;
}

static int mxs_i2c_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mxs_i2c_dev *i2c;
	struct i2c_adapter *adap;
	struct pinctrl *pinctrl;
	struct resource *res;
	resource_size_t res_size;
	int err, irq;

	pinctrl = devm_pinctrl_get_select_default(dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);

	i2c = devm_kzalloc(dev, sizeof(struct mxs_i2c_dev), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);

	if (!res || irq < 0)
		return -ENOENT;

	res_size = resource_size(res);
	if (!devm_request_mem_region(dev, res->start, res_size, res->name))
		return -EBUSY;

	i2c->regs = devm_ioremap_nocache(dev, res->start, res_size);
	if (!i2c->regs)
		return -EBUSY;

	err = devm_request_irq(dev, irq, mxs_i2c_isr, 0, dev_name(dev), i2c);
	if (err)
		return err;

	i2c->dev = dev;

	init_completion(&i2c->cmd_complete);

	if (dev->of_node) {
		err = mxs_i2c_get_ofdata(i2c);
		if (err)
			return err;
	}

	/* Setup the DMA */
	i2c->dmach = dma_request_slave_channel(dev, "rx-tx");
	if (!i2c->dmach) {
		dev_err(dev, "Failed to request dma\n");
		return -ENODEV;
	}

	platform_set_drvdata(pdev, i2c);

	/* Do reset to enforce correct startup after pinmuxing */
	mxs_i2c_reset(i2c);

	adap = &i2c->adapter;
	strlcpy(adap->name, "MXS I2C adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &mxs_i2c_algo;
	adap->dev.parent = dev;
	adap->nr = pdev->id;
	adap->dev.of_node = pdev->dev.of_node;
	i2c_set_adapdata(adap, i2c);
	err = i2c_add_numbered_adapter(adap);
	if (err) {
		dev_err(dev, "Failed to add adapter (%d)\n", err);
		writel(MXS_I2C_CTRL0_SFTRST,
				i2c->regs + MXS_I2C_CTRL0_SET);
		return err;
	}

	return 0;
}

static int mxs_i2c_remove(struct platform_device *pdev)
{
	struct mxs_i2c_dev *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adapter);

	if (i2c->dmach)
		dma_release_channel(i2c->dmach);

	writel(MXS_I2C_CTRL0_SFTRST, i2c->regs + MXS_I2C_CTRL0_SET);

	return 0;
}

static const struct of_device_id mxs_i2c_dt_ids[] = {
	{ .compatible = "fsl,imx28-i2c", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_i2c_dt_ids);

static struct platform_driver mxs_i2c_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = mxs_i2c_dt_ids,
		   },
	.remove = mxs_i2c_remove,
};

static int __init mxs_i2c_init(void)
{
	return platform_driver_probe(&mxs_i2c_driver, mxs_i2c_probe);
}
subsys_initcall(mxs_i2c_init);

static void __exit mxs_i2c_exit(void)
{
	platform_driver_unregister(&mxs_i2c_driver);
}
module_exit(mxs_i2c_exit);

MODULE_AUTHOR("Wolfram Sang <w.sang@pengutronix.de>");
MODULE_DESCRIPTION("MXS I2C Bus Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
