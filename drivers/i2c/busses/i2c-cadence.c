/*
 * I2C bus driver for the Cadence I2C controller.
 *
 * Copyright (C) 2009 - 2014 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any
 * later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>

/* Register offsets for the I2C device. */
#define CDNS_I2C_CR_OFFSET		0x00 /* Control Register, RW */
#define CDNS_I2C_SR_OFFSET		0x04 /* Status Register, RO */
#define CDNS_I2C_ADDR_OFFSET		0x08 /* I2C Address Register, RW */
#define CDNS_I2C_DATA_OFFSET		0x0C /* I2C Data Register, RW */
#define CDNS_I2C_ISR_OFFSET		0x10 /* IRQ Status Register, RW */
#define CDNS_I2C_XFER_SIZE_OFFSET	0x14 /* Transfer Size Register, RW */
#define CDNS_I2C_TIME_OUT_OFFSET	0x1C /* Time Out Register, RW */
#define CDNS_I2C_IER_OFFSET		0x24 /* IRQ Enable Register, WO */
#define CDNS_I2C_IDR_OFFSET		0x28 /* IRQ Disable Register, WO */

/* Control Register Bit mask definitions */
#define CDNS_I2C_CR_HOLD		BIT(4) /* Hold Bus bit */
#define CDNS_I2C_CR_ACK_EN		BIT(3)
#define CDNS_I2C_CR_NEA			BIT(2)
#define CDNS_I2C_CR_MS			BIT(1)
/* Read or Write Master transfer 0 = Transmitter, 1 = Receiver */
#define CDNS_I2C_CR_RW			BIT(0)
/* 1 = Auto init FIFO to zeroes */
#define CDNS_I2C_CR_CLR_FIFO		BIT(6)
#define CDNS_I2C_CR_DIVA_SHIFT		14
#define CDNS_I2C_CR_DIVA_MASK		(3 << CDNS_I2C_CR_DIVA_SHIFT)
#define CDNS_I2C_CR_DIVB_SHIFT		8
#define CDNS_I2C_CR_DIVB_MASK		(0x3f << CDNS_I2C_CR_DIVB_SHIFT)

/* Status Register Bit mask definitions */
#define CDNS_I2C_SR_BA		BIT(8)
#define CDNS_I2C_SR_RXDV	BIT(5)

/*
 * I2C Address Register Bit mask definitions
 * Normal addressing mode uses [6:0] bits. Extended addressing mode uses [9:0]
 * bits. A write access to this register always initiates a transfer if the I2C
 * is in master mode.
 */
#define CDNS_I2C_ADDR_MASK	0x000003FF /* I2C Address Mask */

/*
 * I2C Interrupt Registers Bit mask definitions
 * All the four interrupt registers (Status/Mask/Enable/Disable) have the same
 * bit definitions.
 */
#define CDNS_I2C_IXR_ARB_LOST		BIT(9)
#define CDNS_I2C_IXR_RX_UNF		BIT(7)
#define CDNS_I2C_IXR_TX_OVF		BIT(6)
#define CDNS_I2C_IXR_RX_OVF		BIT(5)
#define CDNS_I2C_IXR_SLV_RDY		BIT(4)
#define CDNS_I2C_IXR_TO			BIT(3)
#define CDNS_I2C_IXR_NACK		BIT(2)
#define CDNS_I2C_IXR_DATA		BIT(1)
#define CDNS_I2C_IXR_COMP		BIT(0)

#define CDNS_I2C_IXR_ALL_INTR_MASK	(CDNS_I2C_IXR_ARB_LOST | \
					 CDNS_I2C_IXR_RX_UNF | \
					 CDNS_I2C_IXR_TX_OVF | \
					 CDNS_I2C_IXR_RX_OVF | \
					 CDNS_I2C_IXR_SLV_RDY | \
					 CDNS_I2C_IXR_TO | \
					 CDNS_I2C_IXR_NACK | \
					 CDNS_I2C_IXR_DATA | \
					 CDNS_I2C_IXR_COMP)

#define CDNS_I2C_IXR_ERR_INTR_MASK	(CDNS_I2C_IXR_ARB_LOST | \
					 CDNS_I2C_IXR_RX_UNF | \
					 CDNS_I2C_IXR_TX_OVF | \
					 CDNS_I2C_IXR_RX_OVF | \
					 CDNS_I2C_IXR_NACK)

#define CDNS_I2C_ENABLED_INTR_MASK	(CDNS_I2C_IXR_ARB_LOST | \
					 CDNS_I2C_IXR_RX_UNF | \
					 CDNS_I2C_IXR_TX_OVF | \
					 CDNS_I2C_IXR_RX_OVF | \
					 CDNS_I2C_IXR_NACK | \
					 CDNS_I2C_IXR_DATA | \
					 CDNS_I2C_IXR_COMP)

#define CDNS_I2C_TIMEOUT		msecs_to_jiffies(1000)
/* timeout for pm runtime autosuspend */
#define CNDS_I2C_PM_TIMEOUT		1000	/* ms */

#define CDNS_I2C_FIFO_DEPTH		16
/* FIFO depth at which the DATA interrupt occurs */
#define CDNS_I2C_DATA_INTR_DEPTH	(CDNS_I2C_FIFO_DEPTH - 2)
#define CDNS_I2C_MAX_TRANSFER_SIZE	255
/* Transfer size in multiples of data interrupt depth */
#define CDNS_I2C_TRANSFER_SIZE	(CDNS_I2C_MAX_TRANSFER_SIZE - 3)

#define DRIVER_NAME		"cdns-i2c"

#define CDNS_I2C_SPEED_MAX	400000
#define CDNS_I2C_SPEED_DEFAULT	100000

#define CDNS_I2C_DIVA_MAX	4
#define CDNS_I2C_DIVB_MAX	64

#define CDNS_I2C_TIMEOUT_MAX	0xFF

#define CDNS_I2C_BROKEN_HOLD_BIT	BIT(0)

#define cdns_i2c_readreg(offset)       readl_relaxed(id->membase + offset)
#define cdns_i2c_writereg(val, offset) writel_relaxed(val, id->membase + offset)

/**
 * struct cdns_i2c - I2C device private data structure
 *
 * @dev:		Pointer to device structure
 * @membase:		Base address of the I2C device
 * @adap:		I2C adapter instance
 * @p_msg:		Message pointer
 * @err_status:		Error status in Interrupt Status Register
 * @xfer_done:		Transfer complete status
 * @p_send_buf:		Pointer to transmit buffer
 * @p_recv_buf:		Pointer to receive buffer
 * @send_count:		Number of bytes still expected to send
 * @recv_count:		Number of bytes still expected to receive
 * @curr_recv_count:	Number of bytes to be received in current transfer
 * @irq:		IRQ number
 * @input_clk:		Input clock to I2C controller
 * @i2c_clk:		Maximum I2C clock speed
 * @bus_hold_flag:	Flag used in repeated start for clearing HOLD bit
 * @clk:		Pointer to struct clk
 * @clk_rate_change_nb:	Notifier block for clock rate changes
 * @quirks:		flag for broken hold bit usage in r1p10
 */
struct cdns_i2c {
	struct device		*dev;
	void __iomem *membase;
	struct i2c_adapter adap;
	struct i2c_msg *p_msg;
	int err_status;
	struct completion xfer_done;
	unsigned char *p_send_buf;
	unsigned char *p_recv_buf;
	unsigned int send_count;
	unsigned int recv_count;
	unsigned int curr_recv_count;
	int irq;
	unsigned long input_clk;
	unsigned int i2c_clk;
	unsigned int bus_hold_flag;
	struct clk *clk;
	struct notifier_block clk_rate_change_nb;
	u32 quirks;
};

struct cdns_platform_data {
	u32 quirks;
};

#define to_cdns_i2c(_nb)	container_of(_nb, struct cdns_i2c, \
					     clk_rate_change_nb)

/**
 * cdns_i2c_clear_bus_hold - Clear bus hold bit
 * @id:	Pointer to driver data struct
 *
 * Helper to clear the controller's bus hold bit.
 */
static void cdns_i2c_clear_bus_hold(struct cdns_i2c *id)
{
	u32 reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	if (reg & CDNS_I2C_CR_HOLD)
		cdns_i2c_writereg(reg & ~CDNS_I2C_CR_HOLD, CDNS_I2C_CR_OFFSET);
}

static inline bool cdns_is_holdquirk(struct cdns_i2c *id, bool hold_wrkaround)
{
	return (hold_wrkaround &&
		(id->curr_recv_count == CDNS_I2C_FIFO_DEPTH + 1));
}

/**
 * cdns_i2c_isr - Interrupt handler for the I2C device
 * @irq:	irq number for the I2C device
 * @ptr:	void pointer to cdns_i2c structure
 *
 * This function handles the data interrupt, transfer complete interrupt and
 * the error interrupts of the I2C device.
 *
 * Return: IRQ_HANDLED always
 */
static irqreturn_t cdns_i2c_isr(int irq, void *ptr)
{
	unsigned int isr_status, avail_bytes, updatetx;
	unsigned int bytes_to_send;
	bool hold_quirk;
	struct cdns_i2c *id = ptr;
	/* Signal completion only after everything is updated */
	int done_flag = 0;
	irqreturn_t status = IRQ_NONE;

	isr_status = cdns_i2c_readreg(CDNS_I2C_ISR_OFFSET);
	cdns_i2c_writereg(isr_status, CDNS_I2C_ISR_OFFSET);

	/* Handling nack and arbitration lost interrupt */
	if (isr_status & (CDNS_I2C_IXR_NACK | CDNS_I2C_IXR_ARB_LOST)) {
		done_flag = 1;
		status = IRQ_HANDLED;
	}

	/*
	 * Check if transfer size register needs to be updated again for a
	 * large data receive operation.
	 */
	updatetx = 0;
	if (id->recv_count > id->curr_recv_count)
		updatetx = 1;

	hold_quirk = (id->quirks & CDNS_I2C_BROKEN_HOLD_BIT) && updatetx;

	/* When receiving, handle data interrupt and completion interrupt */
	if (id->p_recv_buf &&
	    ((isr_status & CDNS_I2C_IXR_COMP) ||
	     (isr_status & CDNS_I2C_IXR_DATA))) {
		/* Read data if receive data valid is set */
		while (cdns_i2c_readreg(CDNS_I2C_SR_OFFSET) &
		       CDNS_I2C_SR_RXDV) {
			/*
			 * Clear hold bit that was set for FIFO control if
			 * RX data left is less than FIFO depth, unless
			 * repeated start is selected.
			 */
			if ((id->recv_count < CDNS_I2C_FIFO_DEPTH) &&
			    !id->bus_hold_flag)
				cdns_i2c_clear_bus_hold(id);

			*(id->p_recv_buf)++ =
				cdns_i2c_readreg(CDNS_I2C_DATA_OFFSET);
			id->recv_count--;
			id->curr_recv_count--;

			if (cdns_is_holdquirk(id, hold_quirk))
				break;
		}

		/*
		 * The controller sends NACK to the slave when transfer size
		 * register reaches zero without considering the HOLD bit.
		 * This workaround is implemented for large data transfers to
		 * maintain transfer size non-zero while performing a large
		 * receive operation.
		 */
		if (cdns_is_holdquirk(id, hold_quirk)) {
			/* wait while fifo is full */
			while (cdns_i2c_readreg(CDNS_I2C_XFER_SIZE_OFFSET) !=
			       (id->curr_recv_count - CDNS_I2C_FIFO_DEPTH))
				;

			/*
			 * Check number of bytes to be received against maximum
			 * transfer size and update register accordingly.
			 */
			if (((int)(id->recv_count) - CDNS_I2C_FIFO_DEPTH) >
			    CDNS_I2C_TRANSFER_SIZE) {
				cdns_i2c_writereg(CDNS_I2C_TRANSFER_SIZE,
						  CDNS_I2C_XFER_SIZE_OFFSET);
				id->curr_recv_count = CDNS_I2C_TRANSFER_SIZE +
						      CDNS_I2C_FIFO_DEPTH;
			} else {
				cdns_i2c_writereg(id->recv_count -
						  CDNS_I2C_FIFO_DEPTH,
						  CDNS_I2C_XFER_SIZE_OFFSET);
				id->curr_recv_count = id->recv_count;
			}
		} else if (id->recv_count && !hold_quirk &&
						!id->curr_recv_count) {

			/* Set the slave address in address register*/
			cdns_i2c_writereg(id->p_msg->addr & CDNS_I2C_ADDR_MASK,
						CDNS_I2C_ADDR_OFFSET);

			if (id->recv_count > CDNS_I2C_TRANSFER_SIZE) {
				cdns_i2c_writereg(CDNS_I2C_TRANSFER_SIZE,
						CDNS_I2C_XFER_SIZE_OFFSET);
				id->curr_recv_count = CDNS_I2C_TRANSFER_SIZE;
			} else {
				cdns_i2c_writereg(id->recv_count,
						CDNS_I2C_XFER_SIZE_OFFSET);
				id->curr_recv_count = id->recv_count;
			}
		}

		/* Clear hold (if not repeated start) and signal completion */
		if ((isr_status & CDNS_I2C_IXR_COMP) && !id->recv_count) {
			if (!id->bus_hold_flag)
				cdns_i2c_clear_bus_hold(id);
			done_flag = 1;
		}

		status = IRQ_HANDLED;
	}

	/* When sending, handle transfer complete interrupt */
	if ((isr_status & CDNS_I2C_IXR_COMP) && !id->p_recv_buf) {
		/*
		 * If there is more data to be sent, calculate the
		 * space available in FIFO and fill with that many bytes.
		 */
		if (id->send_count) {
			avail_bytes = CDNS_I2C_FIFO_DEPTH -
			    cdns_i2c_readreg(CDNS_I2C_XFER_SIZE_OFFSET);
			if (id->send_count > avail_bytes)
				bytes_to_send = avail_bytes;
			else
				bytes_to_send = id->send_count;

			while (bytes_to_send--) {
				cdns_i2c_writereg(
					(*(id->p_send_buf)++),
					 CDNS_I2C_DATA_OFFSET);
				id->send_count--;
			}
		} else {
			/*
			 * Signal the completion of transaction and
			 * clear the hold bus bit if there are no
			 * further messages to be processed.
			 */
			done_flag = 1;
		}
		if (!id->send_count && !id->bus_hold_flag)
			cdns_i2c_clear_bus_hold(id);

		status = IRQ_HANDLED;
	}

	/* Update the status for errors */
	id->err_status = isr_status & CDNS_I2C_IXR_ERR_INTR_MASK;
	if (id->err_status)
		status = IRQ_HANDLED;

	if (done_flag)
		complete(&id->xfer_done);

	return status;
}

/**
 * cdns_i2c_mrecv - Prepare and start a master receive operation
 * @id:		pointer to the i2c device structure
 */
static void cdns_i2c_mrecv(struct cdns_i2c *id)
{
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = id->p_msg->buf;
	id->recv_count = id->p_msg->len;

	/* Put the controller in master receive mode and clear the FIFO */
	ctrl_reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	ctrl_reg |= CDNS_I2C_CR_RW | CDNS_I2C_CR_CLR_FIFO;

	if (id->p_msg->flags & I2C_M_RECV_LEN)
		id->recv_count = I2C_SMBUS_BLOCK_MAX + 1;

	id->curr_recv_count = id->recv_count;

	/*
	 * Check for the message size against FIFO depth and set the
	 * 'hold bus' bit if it is greater than FIFO depth.
	 */
	if (id->recv_count > CDNS_I2C_FIFO_DEPTH)
		ctrl_reg |= CDNS_I2C_CR_HOLD;

	cdns_i2c_writereg(ctrl_reg, CDNS_I2C_CR_OFFSET);

	/* Clear the interrupts in interrupt status register */
	isr_status = cdns_i2c_readreg(CDNS_I2C_ISR_OFFSET);
	cdns_i2c_writereg(isr_status, CDNS_I2C_ISR_OFFSET);

	/*
	 * The no. of bytes to receive is checked against the limit of
	 * max transfer size. Set transfer size register with no of bytes
	 * receive if it is less than transfer size and transfer size if
	 * it is more. Enable the interrupts.
	 */
	if (id->recv_count > CDNS_I2C_TRANSFER_SIZE) {
		cdns_i2c_writereg(CDNS_I2C_TRANSFER_SIZE,
				  CDNS_I2C_XFER_SIZE_OFFSET);
		id->curr_recv_count = CDNS_I2C_TRANSFER_SIZE;
	} else {
		cdns_i2c_writereg(id->recv_count, CDNS_I2C_XFER_SIZE_OFFSET);
	}

	/* Clear the bus hold flag if bytes to receive is less than FIFO size */
	if (!id->bus_hold_flag &&
		((id->p_msg->flags & I2C_M_RECV_LEN) != I2C_M_RECV_LEN) &&
		(id->recv_count <= CDNS_I2C_FIFO_DEPTH))
			cdns_i2c_clear_bus_hold(id);
	/* Set the slave address in address register - triggers operation */
	cdns_i2c_writereg(id->p_msg->addr & CDNS_I2C_ADDR_MASK,
						CDNS_I2C_ADDR_OFFSET);
	cdns_i2c_writereg(CDNS_I2C_ENABLED_INTR_MASK, CDNS_I2C_IER_OFFSET);
}

/**
 * cdns_i2c_msend - Prepare and start a master send operation
 * @id:		pointer to the i2c device
 */
static void cdns_i2c_msend(struct cdns_i2c *id)
{
	unsigned int avail_bytes;
	unsigned int bytes_to_send;
	unsigned int ctrl_reg;
	unsigned int isr_status;

	id->p_recv_buf = NULL;
	id->p_send_buf = id->p_msg->buf;
	id->send_count = id->p_msg->len;

	/* Set the controller in Master transmit mode and clear the FIFO. */
	ctrl_reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	ctrl_reg &= ~CDNS_I2C_CR_RW;
	ctrl_reg |= CDNS_I2C_CR_CLR_FIFO;

	/*
	 * Check for the message size against FIFO depth and set the
	 * 'hold bus' bit if it is greater than FIFO depth.
	 */
	if (id->send_count > CDNS_I2C_FIFO_DEPTH)
		ctrl_reg |= CDNS_I2C_CR_HOLD;
	cdns_i2c_writereg(ctrl_reg, CDNS_I2C_CR_OFFSET);

	/* Clear the interrupts in interrupt status register. */
	isr_status = cdns_i2c_readreg(CDNS_I2C_ISR_OFFSET);
	cdns_i2c_writereg(isr_status, CDNS_I2C_ISR_OFFSET);

	/*
	 * Calculate the space available in FIFO. Check the message length
	 * against the space available, and fill the FIFO accordingly.
	 * Enable the interrupts.
	 */
	avail_bytes = CDNS_I2C_FIFO_DEPTH -
				cdns_i2c_readreg(CDNS_I2C_XFER_SIZE_OFFSET);

	if (id->send_count > avail_bytes)
		bytes_to_send = avail_bytes;
	else
		bytes_to_send = id->send_count;

	while (bytes_to_send--) {
		cdns_i2c_writereg((*(id->p_send_buf)++), CDNS_I2C_DATA_OFFSET);
		id->send_count--;
	}

	/*
	 * Clear the bus hold flag if there is no more data
	 * and if it is the last message.
	 */
	if (!id->bus_hold_flag && !id->send_count)
		cdns_i2c_clear_bus_hold(id);
	/* Set the slave address in address register - triggers operation. */
	cdns_i2c_writereg(id->p_msg->addr & CDNS_I2C_ADDR_MASK,
						CDNS_I2C_ADDR_OFFSET);

	cdns_i2c_writereg(CDNS_I2C_ENABLED_INTR_MASK, CDNS_I2C_IER_OFFSET);
}

/**
 * cdns_i2c_master_reset - Reset the interface
 * @adap:	pointer to the i2c adapter driver instance
 *
 * This function cleanup the fifos, clear the hold bit and status
 * and disable the interrupts.
 */
static void cdns_i2c_master_reset(struct i2c_adapter *adap)
{
	struct cdns_i2c *id = adap->algo_data;
	u32 regval;

	/* Disable the interrupts */
	cdns_i2c_writereg(CDNS_I2C_IXR_ALL_INTR_MASK, CDNS_I2C_IDR_OFFSET);
	/* Clear the hold bit and fifos */
	regval = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	regval &= ~CDNS_I2C_CR_HOLD;
	regval |= CDNS_I2C_CR_CLR_FIFO;
	cdns_i2c_writereg(regval, CDNS_I2C_CR_OFFSET);
	/* Update the transfercount register to zero */
	cdns_i2c_writereg(0, CDNS_I2C_XFER_SIZE_OFFSET);
	/* Clear the interupt status register */
	regval = cdns_i2c_readreg(CDNS_I2C_ISR_OFFSET);
	cdns_i2c_writereg(regval, CDNS_I2C_ISR_OFFSET);
	/* Clear the status register */
	regval = cdns_i2c_readreg(CDNS_I2C_SR_OFFSET);
	cdns_i2c_writereg(regval, CDNS_I2C_SR_OFFSET);
}

static int cdns_i2c_process_msg(struct cdns_i2c *id, struct i2c_msg *msg,
		struct i2c_adapter *adap)
{
	unsigned long time_left;
	u32 reg;

	id->p_msg = msg;
	id->err_status = 0;
	reinit_completion(&id->xfer_done);

	/* Check for the TEN Bit mode on each msg */
	reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	if (msg->flags & I2C_M_TEN) {
		if (reg & CDNS_I2C_CR_NEA)
			cdns_i2c_writereg(reg & ~CDNS_I2C_CR_NEA,
					CDNS_I2C_CR_OFFSET);
	} else {
		if (!(reg & CDNS_I2C_CR_NEA))
			cdns_i2c_writereg(reg | CDNS_I2C_CR_NEA,
					CDNS_I2C_CR_OFFSET);
	}

	/* Check for the R/W flag on each msg */
	if (msg->flags & I2C_M_RD)
		cdns_i2c_mrecv(id);
	else
		cdns_i2c_msend(id);

	/* Wait for the signal of completion */
	time_left = wait_for_completion_timeout(&id->xfer_done, adap->timeout);
	if (time_left == 0) {
		cdns_i2c_master_reset(adap);
		dev_err(id->adap.dev.parent,
				"timeout waiting on completion\n");
		return -ETIMEDOUT;
	}

	cdns_i2c_writereg(CDNS_I2C_IXR_ALL_INTR_MASK,
			  CDNS_I2C_IDR_OFFSET);

	/* If it is bus arbitration error, try again */
	if (id->err_status & CDNS_I2C_IXR_ARB_LOST)
		return -EAGAIN;

	return 0;
}

/**
 * cdns_i2c_master_xfer - The main i2c transfer function
 * @adap:	pointer to the i2c adapter driver instance
 * @msgs:	pointer to the i2c message structure
 * @num:	the number of messages to transfer
 *
 * Initiates the send/recv activity based on the transfer message received.
 *
 * Return: number of msgs processed on success, negative error otherwise
 */
static int cdns_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
				int num)
{
	int ret, count;
	u32 reg;
	struct cdns_i2c *id = adap->algo_data;
	bool hold_quirk;

	ret = pm_runtime_get_sync(id->dev);
	if (ret < 0)
		return ret;
	/* Check if the bus is free */
	if (cdns_i2c_readreg(CDNS_I2C_SR_OFFSET) & CDNS_I2C_SR_BA) {
		ret = -EAGAIN;
		goto out;
	}

	hold_quirk = !!(id->quirks & CDNS_I2C_BROKEN_HOLD_BIT);
	/*
	 * Set the flag to one when multiple messages are to be
	 * processed with a repeated start.
	 */
	if (num > 1) {
		/*
		 * This controller does not give completion interrupt after a
		 * master receive message if HOLD bit is set (repeated start),
		 * resulting in SW timeout. Hence, if a receive message is
		 * followed by any other message, an error is returned
		 * indicating that this sequence is not supported.
		 */
		for (count = 0; (count < num - 1 && hold_quirk); count++) {
			if (msgs[count].flags & I2C_M_RD) {
				dev_warn(adap->dev.parent,
					 "Can't do repeated start after a receive message\n");
				ret = -EOPNOTSUPP;
				goto out;
			}
		}
		id->bus_hold_flag = 1;
		reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
		reg |= CDNS_I2C_CR_HOLD;
		cdns_i2c_writereg(reg, CDNS_I2C_CR_OFFSET);
	} else {
		id->bus_hold_flag = 0;
	}

	/* Process the msg one by one */
	for (count = 0; count < num; count++, msgs++) {
		if (count == (num - 1))
			id->bus_hold_flag = 0;

		ret = cdns_i2c_process_msg(id, msgs, adap);
		if (ret)
			goto out;

		/* Report the other error interrupts to application */
		if (id->err_status) {
			cdns_i2c_master_reset(adap);

			if (id->err_status & CDNS_I2C_IXR_NACK) {
				ret = -ENXIO;
				goto out;
			}
			ret = -EIO;
			goto out;
		}
	}

	ret = num;
out:
	pm_runtime_mark_last_busy(id->dev);
	pm_runtime_put_autosuspend(id->dev);
	return ret;
}

/**
 * cdns_i2c_func - Returns the supported features of the I2C driver
 * @adap:	pointer to the i2c adapter structure
 *
 * Return: 32 bit value, each bit corresponding to a feature
 */
static u32 cdns_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR |
		(I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
		I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm cdns_i2c_algo = {
	.master_xfer	= cdns_i2c_master_xfer,
	.functionality	= cdns_i2c_func,
};

/**
 * cdns_i2c_calc_divs - Calculate clock dividers
 * @f:		I2C clock frequency
 * @input_clk:	Input clock frequency
 * @a:		First divider (return value)
 * @b:		Second divider (return value)
 *
 * f is used as input and output variable. As input it is used as target I2C
 * frequency. On function exit f holds the actually resulting I2C frequency.
 *
 * Return: 0 on success, negative errno otherwise.
 */
static int cdns_i2c_calc_divs(unsigned long *f, unsigned long input_clk,
		unsigned int *a, unsigned int *b)
{
	unsigned long fscl = *f, best_fscl = *f, actual_fscl, temp;
	unsigned int div_a, div_b, calc_div_a = 0, calc_div_b = 0;
	unsigned int last_error, current_error;

	/* calculate (divisor_a+1) x (divisor_b+1) */
	temp = input_clk / (22 * fscl);

	/*
	 * If the calculated value is negative or 0, the fscl input is out of
	 * range. Return error.
	 */
	if (!temp || (temp > (CDNS_I2C_DIVA_MAX * CDNS_I2C_DIVB_MAX)))
		return -EINVAL;

	last_error = -1;
	for (div_a = 0; div_a < CDNS_I2C_DIVA_MAX; div_a++) {
		div_b = DIV_ROUND_UP(input_clk, 22 * fscl * (div_a + 1));

		if ((div_b < 1) || (div_b > CDNS_I2C_DIVB_MAX))
			continue;
		div_b--;

		actual_fscl = input_clk / (22 * (div_a + 1) * (div_b + 1));

		if (actual_fscl > fscl)
			continue;

		current_error = ((actual_fscl > fscl) ? (actual_fscl - fscl) :
							(fscl - actual_fscl));

		if (last_error > current_error) {
			calc_div_a = div_a;
			calc_div_b = div_b;
			best_fscl = actual_fscl;
			last_error = current_error;
		}
	}

	*a = calc_div_a;
	*b = calc_div_b;
	*f = best_fscl;

	return 0;
}

/**
 * cdns_i2c_setclk - This function sets the serial clock rate for the I2C device
 * @clk_in:	I2C clock input frequency in Hz
 * @id:		Pointer to the I2C device structure
 *
 * The device must be idle rather than busy transferring data before setting
 * these device options.
 * The data rate is set by values in the control register.
 * The formula for determining the correct register values is
 *	Fscl = Fpclk/(22 x (divisor_a+1) x (divisor_b+1))
 * See the hardware data sheet for a full explanation of setting the serial
 * clock rate. The clock can not be faster than the input clock divide by 22.
 * The two most common clock rates are 100KHz and 400KHz.
 *
 * Return: 0 on success, negative error otherwise
 */
static int cdns_i2c_setclk(unsigned long clk_in, struct cdns_i2c *id)
{
	unsigned int div_a, div_b;
	unsigned int ctrl_reg;
	int ret = 0;
	unsigned long fscl = id->i2c_clk;

	ret = cdns_i2c_calc_divs(&fscl, clk_in, &div_a, &div_b);
	if (ret)
		return ret;

	ctrl_reg = cdns_i2c_readreg(CDNS_I2C_CR_OFFSET);
	ctrl_reg &= ~(CDNS_I2C_CR_DIVA_MASK | CDNS_I2C_CR_DIVB_MASK);
	ctrl_reg |= ((div_a << CDNS_I2C_CR_DIVA_SHIFT) |
			(div_b << CDNS_I2C_CR_DIVB_SHIFT));
	cdns_i2c_writereg(ctrl_reg, CDNS_I2C_CR_OFFSET);

	return 0;
}

/**
 * cdns_i2c_clk_notifier_cb - Clock rate change callback
 * @nb:		Pointer to notifier block
 * @event:	Notification reason
 * @data:	Pointer to notification data object
 *
 * This function is called when the cdns_i2c input clock frequency changes.
 * The callback checks whether a valid bus frequency can be generated after the
 * change. If so, the change is acknowledged, otherwise the change is aborted.
 * New dividers are written to the HW in the pre- or post change notification
 * depending on the scaling direction.
 *
 * Return:	NOTIFY_STOP if the rate change should be aborted, NOTIFY_OK
 *		to acknowedge the change, NOTIFY_DONE if the notification is
 *		considered irrelevant.
 */
static int cdns_i2c_clk_notifier_cb(struct notifier_block *nb, unsigned long
		event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct cdns_i2c *id = to_cdns_i2c(nb);

	if (pm_runtime_suspended(id->dev))
		return NOTIFY_OK;

	switch (event) {
	case PRE_RATE_CHANGE:
	{
		unsigned long input_clk = ndata->new_rate;
		unsigned long fscl = id->i2c_clk;
		unsigned int div_a, div_b;
		int ret;

		ret = cdns_i2c_calc_divs(&fscl, input_clk, &div_a, &div_b);
		if (ret) {
			dev_warn(id->adap.dev.parent,
					"clock rate change rejected\n");
			return NOTIFY_STOP;
		}

		/* scale up */
		if (ndata->new_rate > ndata->old_rate)
			cdns_i2c_setclk(ndata->new_rate, id);

		return NOTIFY_OK;
	}
	case POST_RATE_CHANGE:
		id->input_clk = ndata->new_rate;
		/* scale down */
		if (ndata->new_rate < ndata->old_rate)
			cdns_i2c_setclk(ndata->new_rate, id);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
		/* scale up */
		if (ndata->new_rate > ndata->old_rate)
			cdns_i2c_setclk(ndata->old_rate, id);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

/**
 * cdns_i2c_runtime_suspend -  Runtime suspend method for the driver
 * @dev:	Address of the platform_device structure
 *
 * Put the driver into low power mode.
 *
 * Return: 0 always
 */
static int __maybe_unused cdns_i2c_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cdns_i2c *xi2c = platform_get_drvdata(pdev);

	clk_disable(xi2c->clk);

	return 0;
}

/**
 * cdns_i2c_runtime_resume - Runtime resume
 * @dev:	Address of the platform_device structure
 *
 * Runtime resume callback.
 *
 * Return: 0 on success and error value on error
 */
static int __maybe_unused cdns_i2c_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct cdns_i2c *xi2c = platform_get_drvdata(pdev);
	int ret;

	ret = clk_enable(xi2c->clk);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops cdns_i2c_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(cdns_i2c_runtime_suspend,
			   cdns_i2c_runtime_resume, NULL)
};

static const struct cdns_platform_data r1p10_i2c_def = {
	.quirks = CDNS_I2C_BROKEN_HOLD_BIT,
};

static const struct of_device_id cdns_i2c_of_match[] = {
	{ .compatible = "cdns,i2c-r1p10", .data = &r1p10_i2c_def },
	{ .compatible = "cdns,i2c-r1p14",},
	{ /* end of table */ }
};
MODULE_DEVICE_TABLE(of, cdns_i2c_of_match);

/**
 * cdns_i2c_probe - Platform registration call
 * @pdev:	Handle to the platform device structure
 *
 * This function does all the memory allocation and registration for the i2c
 * device. User can modify the address mode to 10 bit address mode using the
 * ioctl call with option I2C_TENBIT.
 *
 * Return: 0 on success, negative error otherwise
 */
static int cdns_i2c_probe(struct platform_device *pdev)
{
	struct resource *r_mem;
	struct cdns_i2c *id;
	int ret;
	const struct of_device_id *match;

	id = devm_kzalloc(&pdev->dev, sizeof(*id), GFP_KERNEL);
	if (!id)
		return -ENOMEM;

	id->dev = &pdev->dev;
	platform_set_drvdata(pdev, id);

	match = of_match_node(cdns_i2c_of_match, pdev->dev.of_node);
	if (match && match->data) {
		const struct cdns_platform_data *data = match->data;
		id->quirks = data->quirks;
	}

	r_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	id->membase = devm_ioremap_resource(&pdev->dev, r_mem);
	if (IS_ERR(id->membase))
		return PTR_ERR(id->membase);

	id->irq = platform_get_irq(pdev, 0);

	id->adap.owner = THIS_MODULE;
	id->adap.dev.of_node = pdev->dev.of_node;
	id->adap.algo = &cdns_i2c_algo;
	id->adap.timeout = CDNS_I2C_TIMEOUT;
	id->adap.retries = 3;		/* Default retry value. */
	id->adap.algo_data = id;
	id->adap.dev.parent = &pdev->dev;
	init_completion(&id->xfer_done);
	snprintf(id->adap.name, sizeof(id->adap.name),
		 "Cadence I2C at %08lx", (unsigned long)r_mem->start);

	id->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(id->clk)) {
		dev_err(&pdev->dev, "input clock not found.\n");
		return PTR_ERR(id->clk);
	}
	ret = clk_prepare_enable(id->clk);
	if (ret)
		dev_err(&pdev->dev, "Unable to enable clock.\n");

	pm_runtime_enable(id->dev);
	pm_runtime_set_autosuspend_delay(id->dev, CNDS_I2C_PM_TIMEOUT);
	pm_runtime_use_autosuspend(id->dev);
	pm_runtime_set_active(id->dev);

	id->clk_rate_change_nb.notifier_call = cdns_i2c_clk_notifier_cb;
	if (clk_notifier_register(id->clk, &id->clk_rate_change_nb))
		dev_warn(&pdev->dev, "Unable to register clock notifier.\n");
	id->input_clk = clk_get_rate(id->clk);

	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency",
			&id->i2c_clk);
	if (ret || (id->i2c_clk > CDNS_I2C_SPEED_MAX))
		id->i2c_clk = CDNS_I2C_SPEED_DEFAULT;

	cdns_i2c_writereg(CDNS_I2C_CR_ACK_EN | CDNS_I2C_CR_NEA | CDNS_I2C_CR_MS,
			  CDNS_I2C_CR_OFFSET);

	ret = cdns_i2c_setclk(id->input_clk, id);
	if (ret) {
		dev_err(&pdev->dev, "invalid SCL clock: %u Hz\n", id->i2c_clk);
		ret = -EINVAL;
		goto err_clk_dis;
	}

	ret = devm_request_irq(&pdev->dev, id->irq, cdns_i2c_isr, 0,
				 DRIVER_NAME, id);
	if (ret) {
		dev_err(&pdev->dev, "cannot get irq %d\n", id->irq);
		goto err_clk_dis;
	}

	ret = i2c_add_adapter(&id->adap);
	if (ret < 0)
		goto err_clk_dis;

	/*
	 * Cadence I2C controller has a bug wherein it generates
	 * invalid read transaction after HW timeout in master receiver mode.
	 * HW timeout is not used by this driver and the interrupt is disabled.
	 * But the feature itself cannot be disabled. Hence maximum value
	 * is written to this register to reduce the chances of error.
	 */
	cdns_i2c_writereg(CDNS_I2C_TIMEOUT_MAX, CDNS_I2C_TIME_OUT_OFFSET);

	dev_info(&pdev->dev, "%u kHz mmio %08lx irq %d\n",
		 id->i2c_clk / 1000, (unsigned long)r_mem->start, id->irq);

	return 0;

err_clk_dis:
	clk_disable_unprepare(id->clk);
	pm_runtime_set_suspended(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return ret;
}

/**
 * cdns_i2c_remove - Unregister the device after releasing the resources
 * @pdev:	Handle to the platform device structure
 *
 * This function frees all the resources allocated to the device.
 *
 * Return: 0 always
 */
static int cdns_i2c_remove(struct platform_device *pdev)
{
	struct cdns_i2c *id = platform_get_drvdata(pdev);

	i2c_del_adapter(&id->adap);
	clk_notifier_unregister(id->clk, &id->clk_rate_change_nb);
	clk_disable_unprepare(id->clk);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver cdns_i2c_drv = {
	.driver = {
		.name  = DRIVER_NAME,
		.of_match_table = cdns_i2c_of_match,
		.pm = &cdns_i2c_dev_pm_ops,
	},
	.probe  = cdns_i2c_probe,
	.remove = cdns_i2c_remove,
};

module_platform_driver(cdns_i2c_drv);

MODULE_AUTHOR("Xilinx Inc.");
MODULE_DESCRIPTION("Cadence I2C bus driver");
MODULE_LICENSE("GPL");
