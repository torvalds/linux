// SPDX-License-Identifier: GPL-2.0+
/*
 * Freescale MXS I2C bus driver
 *
 * Copyright (C) 2012-2013 Marek Vasut <marex@denx.de>
 * Copyright (C) 2011-2012 Wolfram Sang, Pengutronix e.K.
 *
 * based on a (non-working) driver which was:
 *
 * Copyright (C) 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <linux/stmp_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/dma/mxs-dma.h>

#define DRIVER_NAME "mxs-i2c"

#define MXS_I2C_CTRL0		(0x00)
#define MXS_I2C_CTRL0_SET	(0x04)
#define MXS_I2C_CTRL0_CLR	(0x08)

#define MXS_I2C_CTRL0_SFTRST			0x80000000
#define MXS_I2C_CTRL0_RUN			0x20000000
#define MXS_I2C_CTRL0_SEND_NAK_ON_LAST		0x02000000
#define MXS_I2C_CTRL0_PIO_MODE			0x01000000
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
#define MXS_I2C_STAT_GOT_A_NAK			0x10000000
#define MXS_I2C_STAT_BUS_BUSY			0x00000800
#define MXS_I2C_STAT_CLK_GEN_BUSY		0x00000400

#define MXS_I2C_DATA(i2c)	((i2c->dev_type == MXS_I2C_V1) ? 0x60 : 0xa0)

#define MXS_I2C_DEBUG0_CLR(i2c)	((i2c->dev_type == MXS_I2C_V1) ? 0x78 : 0xb8)

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

enum mxs_i2c_devtype {
	MXS_I2C_UNKNOWN = 0,
	MXS_I2C_V1,
	MXS_I2C_V2,
};

/**
 * struct mxs_i2c_dev - per device, private MXS-I2C data
 *
 * @dev: driver model device node
 * @dev_type: distinguish i.MX23/i.MX28 features
 * @regs: IO registers pointer
 * @cmd_complete: completion object for transaction wait
 * @cmd_err: error code for last transaction
 * @adapter: i2c subsystem adapter node
 */
struct mxs_i2c_dev {
	struct device *dev;
	enum mxs_i2c_devtype dev_type;
	void __iomem *regs;
	struct completion cmd_complete;
	int cmd_err;
	struct i2c_adapter adapter;

	uint32_t timing0;
	uint32_t timing1;
	uint32_t timing2;

	/* DMA support components */
	struct dma_chan			*dmach;
	uint32_t			pio_data[2];
	uint32_t			addr_data;
	struct scatterlist		sg_io[2];
	bool				dma_read;
};

static int mxs_i2c_reset(struct mxs_i2c_dev *i2c)
{
	int ret = stmp_reset_block(i2c->regs);
	if (ret)
		return ret;

	/*
	 * Configure timing for the I2C block. The I2C TIMING2 register has to
	 * be programmed with this particular magic number. The rest is derived
	 * from the XTAL speed and requested I2C speed.
	 *
	 * For details, see i.MX233 [25.4.2 - 25.4.4] and i.MX28 [27.5.2 - 27.5.4].
	 */
	writel(i2c->timing0, i2c->regs + MXS_I2C_TIMING0);
	writel(i2c->timing1, i2c->regs + MXS_I2C_TIMING1);
	writel(i2c->timing2, i2c->regs + MXS_I2C_TIMING2);

	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_SET);

	return 0;
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

	i2c->addr_data = i2c_8bit_addr_from_msg(msg);

	if (msg->flags & I2C_M_RD) {
		i2c->dma_read = true;

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
					DMA_PREP_INTERRUPT |
					MXS_DMA_CTRL_WAIT4END);
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
					DMA_PREP_INTERRUPT |
					MXS_DMA_CTRL_WAIT4END);
		if (!desc) {
			dev_err(i2c->dev,
				"Failed to get DMA data write descriptor.\n");
			goto read_init_dma_fail;
		}
	} else {
		i2c->dma_read = false;

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
					DMA_PREP_INTERRUPT |
					MXS_DMA_CTRL_WAIT4END);
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

static int mxs_i2c_pio_wait_xfer_end(struct mxs_i2c_dev *i2c)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	while (readl(i2c->regs + MXS_I2C_CTRL0) & MXS_I2C_CTRL0_RUN) {
		if (readl(i2c->regs + MXS_I2C_CTRL1) &
				MXS_I2C_CTRL1_NO_SLAVE_ACK_IRQ)
			return -ENXIO;
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

/*
 * Start WRITE transaction on the I2C bus. By studying i.MX23 datasheet,
 * CTRL0::PIO_MODE bit description clarifies the order in which the registers
 * must be written during PIO mode operation. First, the CTRL0 register has
 * to be programmed with all the necessary bits but the RUN bit. Then the
 * payload has to be written into the DATA register. Finally, the transmission
 * is executed by setting the RUN bit in CTRL0.
 */
static void mxs_i2c_pio_trigger_write_cmd(struct mxs_i2c_dev *i2c, u32 cmd,
					  u32 data)
{
	writel(cmd, i2c->regs + MXS_I2C_CTRL0);

	if (i2c->dev_type == MXS_I2C_V1)
		writel(MXS_I2C_CTRL0_PIO_MODE, i2c->regs + MXS_I2C_CTRL0_SET);

	writel(data, i2c->regs + MXS_I2C_DATA(i2c));
	writel(MXS_I2C_CTRL0_RUN, i2c->regs + MXS_I2C_CTRL0_SET);
}

static int mxs_i2c_pio_setup_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msg, uint32_t flags)
{
	struct mxs_i2c_dev *i2c = i2c_get_adapdata(adap);
	uint32_t addr_data = i2c_8bit_addr_from_msg(msg);
	uint32_t data = 0;
	int i, ret, xlen = 0, xmit = 0;
	uint32_t start;

	/* Mute IRQs coming from this block. */
	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_CLR);

	/*
	 * MX23 idea:
	 * - Enable CTRL0::PIO_MODE (1 << 24)
	 * - Enable CTRL1::ACK_MODE (1 << 27)
	 *
	 * WARNING! The MX23 is broken in some way, even if it claims
	 * to support PIO, when we try to transfer any amount of data
	 * that is not aligned to 4 bytes, the DMA engine will have
	 * bits in DEBUG1::DMA_BYTES_ENABLES still set even after the
	 * transfer. This in turn will mess up the next transfer as
	 * the block it emit one byte write onto the bus terminated
	 * with a NAK+STOP. A possible workaround is to reset the IP
	 * block after every PIO transmission, which might just work.
	 *
	 * NOTE: The CTRL0::PIO_MODE description is important, since
	 * it outlines how the PIO mode is really supposed to work.
	 */
	if (msg->flags & I2C_M_RD) {
		/*
		 * PIO READ transfer:
		 *
		 * This transfer MUST be limited to 4 bytes maximum. It is not
		 * possible to transfer more than four bytes via PIO, since we
		 * can not in any way make sure we can read the data from the
		 * DATA register fast enough. Besides, the RX FIFO is only four
		 * bytes deep, thus we can only really read up to four bytes at
		 * time. Finally, there is no bit indicating us that new data
		 * arrived at the FIFO and can thus be fetched from the DATA
		 * register.
		 */
		BUG_ON(msg->len > 4);

		/* SELECT command. */
		mxs_i2c_pio_trigger_write_cmd(i2c, MXS_CMD_I2C_SELECT,
					      addr_data);

		ret = mxs_i2c_pio_wait_xfer_end(i2c);
		if (ret) {
			dev_dbg(i2c->dev,
				"PIO: Failed to send SELECT command!\n");
			goto cleanup;
		}

		/* READ command. */
		mxs_i2c_pio_trigger_cmd(i2c,
					MXS_CMD_I2C_READ | flags |
					MXS_I2C_CTRL0_XFER_COUNT(msg->len));

		ret = mxs_i2c_pio_wait_xfer_end(i2c);
		if (ret) {
			dev_dbg(i2c->dev,
				"PIO: Failed to send READ command!\n");
			goto cleanup;
		}

		data = readl(i2c->regs + MXS_I2C_DATA(i2c));
		for (i = 0; i < msg->len; i++) {
			msg->buf[i] = data & 0xff;
			data >>= 8;
		}
	} else {
		/*
		 * PIO WRITE transfer:
		 *
		 * The code below implements clock stretching to circumvent
		 * the possibility of kernel not being able to supply data
		 * fast enough. It is possible to transfer arbitrary amount
		 * of data using PIO write.
		 */

		/*
		 * The LSB of data buffer is the first byte blasted across
		 * the bus. Higher order bytes follow. Thus the following
		 * filling schematic.
		 */

		data = addr_data << 24;

		/* Start the transfer with START condition. */
		start = MXS_I2C_CTRL0_PRE_SEND_START;

		/* If the transfer is long, use clock stretching. */
		if (msg->len > 3)
			start |= MXS_I2C_CTRL0_RETAIN_CLOCK;

		for (i = 0; i < msg->len; i++) {
			data >>= 8;
			data |= (msg->buf[i] << 24);

			xmit = 0;

			/* This is the last transfer of the message. */
			if (i + 1 == msg->len) {
				/* Add optional STOP flag. */
				start |= flags;
				/* Remove RETAIN_CLOCK bit. */
				start &= ~MXS_I2C_CTRL0_RETAIN_CLOCK;
				xmit = 1;
			}

			/* Four bytes are ready in the "data" variable. */
			if ((i & 3) == 2)
				xmit = 1;

			/* Nothing interesting happened, continue stuffing. */
			if (!xmit)
				continue;

			/*
			 * Compute the size of the transfer and shift the
			 * data accordingly.
			 *
			 * i = (4k + 0) .... xlen = 2
			 * i = (4k + 1) .... xlen = 3
			 * i = (4k + 2) .... xlen = 4
			 * i = (4k + 3) .... xlen = 1
			 */

			if ((i % 4) == 3)
				xlen = 1;
			else
				xlen = (i % 4) + 2;

			data >>= (4 - xlen) * 8;

			dev_dbg(i2c->dev,
				"PIO: len=%i pos=%i total=%i [W%s%s%s]\n",
				xlen, i, msg->len,
				start & MXS_I2C_CTRL0_PRE_SEND_START ? "S" : "",
				start & MXS_I2C_CTRL0_POST_SEND_STOP ? "E" : "",
				start & MXS_I2C_CTRL0_RETAIN_CLOCK ? "C" : "");

			writel(MXS_I2C_DEBUG0_DMAREQ,
			       i2c->regs + MXS_I2C_DEBUG0_CLR(i2c));

			mxs_i2c_pio_trigger_write_cmd(i2c,
				start | MXS_I2C_CTRL0_MASTER_MODE |
				MXS_I2C_CTRL0_DIRECTION |
				MXS_I2C_CTRL0_XFER_COUNT(xlen), data);

			/* The START condition is sent only once. */
			start &= ~MXS_I2C_CTRL0_PRE_SEND_START;

			/* Wait for the end of the transfer. */
			ret = mxs_i2c_pio_wait_xfer_end(i2c);
			if (ret) {
				dev_dbg(i2c->dev,
					"PIO: Failed to finish WRITE cmd!\n");
				break;
			}

			/* Check NAK here. */
			ret = readl(i2c->regs + MXS_I2C_STAT) &
				    MXS_I2C_STAT_GOT_A_NAK;
			if (ret) {
				ret = -ENXIO;
				goto cleanup;
			}
		}
	}

	/* make sure we capture any occurred error into cmd_err */
	ret = mxs_i2c_pio_check_error_state(i2c);

cleanup:
	/* Clear any dangling IRQs and re-enable interrupts. */
	writel(MXS_I2C_IRQ_MASK, i2c->regs + MXS_I2C_CTRL1_CLR);
	writel(MXS_I2C_IRQ_MASK << 8, i2c->regs + MXS_I2C_CTRL1_SET);

	/* Clear the PIO_MODE on i.MX23 */
	if (i2c->dev_type == MXS_I2C_V1)
		writel(MXS_I2C_CTRL0_PIO_MODE, i2c->regs + MXS_I2C_CTRL0_CLR);

	return ret;
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
	int use_pio = 0;
	unsigned long time_left;

	flags = stop ? MXS_I2C_CTRL0_POST_SEND_STOP : 0;

	dev_dbg(i2c->dev, "addr: 0x%04x, len: %d, flags: 0x%x, stop: %d\n",
		msg->addr, msg->len, msg->flags, stop);

	/*
	 * The MX28 I2C IP block can only do PIO READ for transfer of to up
	 * 4 bytes of length. The write transfer is not limited as it can use
	 * clock stretching to avoid FIFO underruns.
	 */
	if ((msg->flags & I2C_M_RD) && (msg->len <= 4))
		use_pio = 1;
	if (!(msg->flags & I2C_M_RD) && (msg->len < 7))
		use_pio = 1;

	i2c->cmd_err = 0;
	if (use_pio) {
		ret = mxs_i2c_pio_setup_xfer(adap, msg, flags);
		/* No need to reset the block if NAK was received. */
		if (ret && (ret != -ENXIO))
			mxs_i2c_reset(i2c);
	} else {
		reinit_completion(&i2c->cmd_complete);
		ret = mxs_i2c_dma_setup_xfer(adap, msg, flags);
		if (ret)
			return ret;

		time_left = wait_for_completion_timeout(&i2c->cmd_complete,
						msecs_to_jiffies(1000));
		if (!time_left)
			goto timeout;

		ret = i2c->cmd_err;
	}

	if (ret == -ENXIO) {
		/*
		 * If the transfer fails with a NAK from the slave the
		 * controller halts until it gets told to return to idle state.
		 */
		writel(MXS_I2C_CTRL1_CLR_GOT_A_NAK,
		       i2c->regs + MXS_I2C_CTRL1_SET);
	}

	/*
	 * WARNING!
	 * The i.MX23 is strange. After each and every operation, it's I2C IP
	 * block must be reset, otherwise the IP block will misbehave. This can
	 * be observed on the bus by the block sending out one single byte onto
	 * the bus. In case such an error happens, bit 27 will be set in the
	 * DEBUG0 register. This bit is not documented in the i.MX23 datasheet
	 * and is marked as "TBD" instead. To reset this bit to a correct state,
	 * reset the whole block. Since the block reset does not take long, do
	 * reset the block after every transfer to play safe.
	 */
	if (i2c->dev_type == MXS_I2C_V1)
		mxs_i2c_reset(i2c);

	dev_dbg(i2c->dev, "Done with err=%d\n", ret);

	return ret;

timeout:
	dev_dbg(i2c->dev, "Timeout!\n");
	mxs_i2c_dma_finish(i2c);
	ret = mxs_i2c_reset(i2c);
	if (ret)
		return ret;

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

static const struct i2c_adapter_quirks mxs_i2c_quirks = {
	.flags = I2C_AQ_NO_ZERO_LEN,
};

static void mxs_i2c_derive_timing(struct mxs_i2c_dev *i2c, uint32_t speed)
{
	/* The I2C block clock runs at 24MHz */
	const uint32_t clk = 24000000;
	uint32_t divider;
	uint16_t high_count, low_count, rcv_count, xmit_count;
	uint32_t bus_free, leadin;
	struct device *dev = i2c->dev;

	divider = DIV_ROUND_UP(clk, speed);

	if (divider < 25) {
		/*
		 * limit the divider, so that min(low_count, high_count)
		 * is >= 1
		 */
		divider = 25;
		dev_warn(dev,
			"Speed too high (%u.%03u kHz), using %u.%03u kHz\n",
			speed / 1000, speed % 1000,
			clk / divider / 1000, clk / divider % 1000);
	} else if (divider > 1897) {
		/*
		 * limit the divider, so that max(low_count, high_count)
		 * cannot exceed 1023
		 */
		divider = 1897;
		dev_warn(dev,
			"Speed too low (%u.%03u kHz), using %u.%03u kHz\n",
			speed / 1000, speed % 1000,
			clk / divider / 1000, clk / divider % 1000);
	}

	/*
	 * The I2C spec specifies the following timing data:
	 *                          standard mode  fast mode Bitfield name
	 * tLOW (SCL LOW period)     4700 ns        1300 ns
	 * tHIGH (SCL HIGH period)   4000 ns         600 ns
	 * tSU;DAT (data setup time)  250 ns         100 ns
	 * tHD;STA (START hold time) 4000 ns         600 ns
	 * tBUF (bus free time)      4700 ns        1300 ns
	 *
	 * The hardware (of the i.MX28 at least) seems to add 2 additional
	 * clock cycles to the low_count and 7 cycles to the high_count.
	 * This is compensated for by subtracting the respective constants
	 * from the values written to the timing registers.
	 */
	if (speed > I2C_MAX_STANDARD_MODE_FREQ) {
		/* fast mode */
		low_count = DIV_ROUND_CLOSEST(divider * 13, (13 + 6));
		high_count = DIV_ROUND_CLOSEST(divider * 6, (13 + 6));
		leadin = DIV_ROUND_UP(600 * (clk / 1000000), 1000);
		bus_free = DIV_ROUND_UP(1300 * (clk / 1000000), 1000);
	} else {
		/* normal mode */
		low_count = DIV_ROUND_CLOSEST(divider * 47, (47 + 40));
		high_count = DIV_ROUND_CLOSEST(divider * 40, (47 + 40));
		leadin = DIV_ROUND_UP(4700 * (clk / 1000000), 1000);
		bus_free = DIV_ROUND_UP(4700 * (clk / 1000000), 1000);
	}
	rcv_count = high_count * 3 / 8;
	xmit_count = low_count * 3 / 8;

	dev_dbg(dev,
		"speed=%u(actual %u) divider=%u low=%u high=%u xmit=%u rcv=%u leadin=%u bus_free=%u\n",
		speed, clk / divider, divider, low_count, high_count,
		xmit_count, rcv_count, leadin, bus_free);

	low_count -= 2;
	high_count -= 7;
	i2c->timing0 = (high_count << 16) | rcv_count;
	i2c->timing1 = (low_count << 16) | xmit_count;
	i2c->timing2 = (bus_free << 16 | leadin);
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
		speed = I2C_MAX_STANDARD_MODE_FREQ;
	}

	mxs_i2c_derive_timing(i2c, speed);

	return 0;
}

static const struct platform_device_id mxs_i2c_devtype[] = {
	{
		.name = "imx23-i2c",
		.driver_data = MXS_I2C_V1,
	}, {
		.name = "imx28-i2c",
		.driver_data = MXS_I2C_V2,
	}, { /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, mxs_i2c_devtype);

static const struct of_device_id mxs_i2c_dt_ids[] = {
	{ .compatible = "fsl,imx23-i2c", .data = &mxs_i2c_devtype[0], },
	{ .compatible = "fsl,imx28-i2c", .data = &mxs_i2c_devtype[1], },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_i2c_dt_ids);

static int mxs_i2c_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
				of_match_device(mxs_i2c_dt_ids, &pdev->dev);
	struct device *dev = &pdev->dev;
	struct mxs_i2c_dev *i2c;
	struct i2c_adapter *adap;
	int err, irq;

	i2c = devm_kzalloc(dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	if (of_id) {
		const struct platform_device_id *device_id = of_id->data;
		i2c->dev_type = device_id->driver_data;
	}

	i2c->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

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
	i2c->dmach = dma_request_chan(dev, "rx-tx");
	if (IS_ERR(i2c->dmach)) {
		dev_err(dev, "Failed to request dma\n");
		return PTR_ERR(i2c->dmach);
	}

	platform_set_drvdata(pdev, i2c);

	/* Do reset to enforce correct startup after pinmuxing */
	err = mxs_i2c_reset(i2c);
	if (err)
		return err;

	adap = &i2c->adapter;
	strlcpy(adap->name, "MXS I2C adapter", sizeof(adap->name));
	adap->owner = THIS_MODULE;
	adap->algo = &mxs_i2c_algo;
	adap->quirks = &mxs_i2c_quirks;
	adap->dev.parent = dev;
	adap->nr = pdev->id;
	adap->dev.of_node = pdev->dev.of_node;
	i2c_set_adapdata(adap, i2c);
	err = i2c_add_numbered_adapter(adap);
	if (err) {
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

static struct platform_driver mxs_i2c_driver = {
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = mxs_i2c_dt_ids,
		   },
	.probe = mxs_i2c_probe,
	.remove = mxs_i2c_remove,
};

static int __init mxs_i2c_init(void)
{
	return platform_driver_register(&mxs_i2c_driver);
}
subsys_initcall(mxs_i2c_init);

static void __exit mxs_i2c_exit(void)
{
	platform_driver_unregister(&mxs_i2c_driver);
}
module_exit(mxs_i2c_exit);

MODULE_AUTHOR("Marek Vasut <marex@denx.de>");
MODULE_AUTHOR("Wolfram Sang <kernel@pengutronix.de>");
MODULE_DESCRIPTION("MXS I2C Bus Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
