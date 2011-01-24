/*
 * Copyright (C) 2009 ST-Ericsson SA
 * Copyright (C) 2009 STMicroelectronics
 *
 * I2C master mode controller driver, used in Nomadik 8815
 * and Ux500 platforms.
 *
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 * Author: Sachin Verma <sachin.verma@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <plat/i2c.h>

#define DRIVER_NAME "nmk-i2c"

/* I2C Controller register offsets */
#define I2C_CR		(0x000)
#define I2C_SCR		(0x004)
#define I2C_HSMCR	(0x008)
#define I2C_MCR		(0x00C)
#define I2C_TFR		(0x010)
#define I2C_SR		(0x014)
#define I2C_RFR		(0x018)
#define I2C_TFTR	(0x01C)
#define I2C_RFTR	(0x020)
#define I2C_DMAR	(0x024)
#define I2C_BRCR	(0x028)
#define I2C_IMSCR	(0x02C)
#define I2C_RISR	(0x030)
#define I2C_MISR	(0x034)
#define I2C_ICR		(0x038)

/* Control registers */
#define I2C_CR_PE		(0x1 << 0)	/* Peripheral Enable */
#define I2C_CR_OM		(0x3 << 1)	/* Operating mode */
#define I2C_CR_SAM		(0x1 << 3)	/* Slave addressing mode */
#define I2C_CR_SM		(0x3 << 4)	/* Speed mode */
#define I2C_CR_SGCM		(0x1 << 6)	/* Slave general call mode */
#define I2C_CR_FTX		(0x1 << 7)	/* Flush Transmit */
#define I2C_CR_FRX		(0x1 << 8)	/* Flush Receive */
#define I2C_CR_DMA_TX_EN	(0x1 << 9)	/* DMA Tx enable */
#define I2C_CR_DMA_RX_EN	(0x1 << 10)	/* DMA Rx Enable */
#define I2C_CR_DMA_SLE		(0x1 << 11)	/* DMA sync. logic enable */
#define I2C_CR_LM		(0x1 << 12)	/* Loopback mode */
#define I2C_CR_FON		(0x3 << 13)	/* Filtering on */
#define I2C_CR_FS		(0x3 << 15)	/* Force stop enable */

/* Master controller (MCR) register */
#define I2C_MCR_OP		(0x1 << 0)	/* Operation */
#define I2C_MCR_A7		(0x7f << 1)	/* 7-bit address */
#define I2C_MCR_EA10		(0x7 << 8) 	/* 10-bit Extended address */
#define I2C_MCR_SB		(0x1 << 11)	/* Extended address */
#define I2C_MCR_AM		(0x3 << 12)	/* Address type */
#define I2C_MCR_STOP		(0x1 << 14) 	/* Stop condition */
#define I2C_MCR_LENGTH		(0x7ff << 15) 	/* Transaction length */

/* Status register (SR) */
#define I2C_SR_OP		(0x3 << 0)	/* Operation */
#define I2C_SR_STATUS		(0x3 << 2)	/* controller status */
#define I2C_SR_CAUSE		(0x7 << 4)	/* Abort cause */
#define I2C_SR_TYPE		(0x3 << 7)	/* Receive type */
#define I2C_SR_LENGTH		(0x7ff << 9)	/* Transfer length */

/* Interrupt mask set/clear (IMSCR) bits */
#define I2C_IT_TXFE 		(0x1 << 0)
#define I2C_IT_TXFNE		(0x1 << 1)
#define I2C_IT_TXFF		(0x1 << 2)
#define I2C_IT_TXFOVR		(0x1 << 3)
#define I2C_IT_RXFE		(0x1 << 4)
#define I2C_IT_RXFNF		(0x1 << 5)
#define I2C_IT_RXFF		(0x1 << 6)
#define I2C_IT_RFSR		(0x1 << 16)
#define I2C_IT_RFSE		(0x1 << 17)
#define I2C_IT_WTSR		(0x1 << 18)
#define I2C_IT_MTD		(0x1 << 19)
#define I2C_IT_STD		(0x1 << 20)
#define I2C_IT_MAL		(0x1 << 24)
#define I2C_IT_BERR		(0x1 << 25)
#define I2C_IT_MTDWS		(0x1 << 28)

#define GEN_MASK(val, mask, sb)  (((val) << (sb)) & (mask))

/* some bits in ICR are reserved */
#define I2C_CLEAR_ALL_INTS	0x131f007f

/* first three msb bits are reserved */
#define IRQ_MASK(mask)		(mask & 0x1fffffff)

/* maximum threshold value */
#define MAX_I2C_FIFO_THRESHOLD	15

/* per-transfer delay, required for the hardware to stabilize */
#define I2C_DELAY		150

enum i2c_status {
	I2C_NOP,
	I2C_ON_GOING,
	I2C_OK,
	I2C_ABORT
};

/* operation */
enum i2c_operation {
	I2C_NO_OPERATION = 0xff,
	I2C_WRITE = 0x00,
	I2C_READ = 0x01
};

/* controller response timeout in ms */
#define I2C_TIMEOUT_MS	2000

/**
 * struct i2c_nmk_client - client specific data
 * @slave_adr: 7-bit slave address
 * @count: no. bytes to be transfered
 * @buffer: client data buffer
 * @xfer_bytes: bytes transfered till now
 * @operation: current I2C operation
 */
struct i2c_nmk_client {
	unsigned short		slave_adr;
	unsigned long		count;
	unsigned char		*buffer;
	unsigned long		xfer_bytes;
	enum i2c_operation	operation;
};

/**
 * struct nmk_i2c_dev - private data structure of the controller
 * @pdev: parent platform device
 * @adap: corresponding I2C adapter
 * @irq: interrupt line for the controller
 * @virtbase: virtual io memory area
 * @clk: hardware i2c block clock
 * @cfg: machine provided controller configuration
 * @cli: holder of client specific data
 * @stop: stop condition
 * @xfer_complete: acknowledge completion for a I2C message
 * @result: controller propogated result
 */
struct nmk_i2c_dev {
	struct platform_device		*pdev;
	struct i2c_adapter 		adap;
	int 				irq;
	void __iomem			*virtbase;
	struct clk			*clk;
	struct nmk_i2c_controller	cfg;
	struct i2c_nmk_client		cli;
	int 				stop;
	struct completion		xfer_complete;
	int 				result;
};

/* controller's abort causes */
static const char *abort_causes[] = {
	"no ack received after address transmission",
	"no ack received during data phase",
	"ack received after xmission of master code",
	"master lost arbitration",
	"slave restarts",
	"slave reset",
	"overflow, maxsize is 2047 bytes",
};

static inline void i2c_set_bit(void __iomem *reg, u32 mask)
{
	writel(readl(reg) | mask, reg);
}

static inline void i2c_clr_bit(void __iomem *reg, u32 mask)
{
	writel(readl(reg) & ~mask, reg);
}

/**
 * flush_i2c_fifo() - This function flushes the I2C FIFO
 * @dev: private data of I2C Driver
 *
 * This function flushes the I2C Tx and Rx FIFOs. It returns
 * 0 on successful flushing of FIFO
 */
static int flush_i2c_fifo(struct nmk_i2c_dev *dev)
{
#define LOOP_ATTEMPTS 10
	int i;
	unsigned long timeout;

	/*
	 * flush the transmit and receive FIFO. The flushing
	 * operation takes several cycles before to be completed.
	 * On the completion, the I2C internal logic clears these
	 * bits, until then no one must access Tx, Rx FIFO and
	 * should poll on these bits waiting for the completion.
	 */
	writel((I2C_CR_FTX | I2C_CR_FRX), dev->virtbase + I2C_CR);

	for (i = 0; i < LOOP_ATTEMPTS; i++) {
		timeout = jiffies + msecs_to_jiffies(I2C_TIMEOUT_MS);

		while (!time_after(jiffies, timeout)) {
			if ((readl(dev->virtbase + I2C_CR) &
				(I2C_CR_FTX | I2C_CR_FRX)) == 0)
					return 0;
		}
	}

	dev_err(&dev->pdev->dev, "flushing operation timed out "
		"giving up after %d attempts", LOOP_ATTEMPTS);

	return -ETIMEDOUT;
}

/**
 * disable_all_interrupts() - Disable all interrupts of this I2c Bus
 * @dev: private data of I2C Driver
 */
static void disable_all_interrupts(struct nmk_i2c_dev *dev)
{
	u32 mask = IRQ_MASK(0);
	writel(mask, dev->virtbase + I2C_IMSCR);
}

/**
 * clear_all_interrupts() - Clear all interrupts of I2C Controller
 * @dev: private data of I2C Driver
 */
static void clear_all_interrupts(struct nmk_i2c_dev *dev)
{
	u32 mask;
	mask = IRQ_MASK(I2C_CLEAR_ALL_INTS);
	writel(mask, dev->virtbase + I2C_ICR);
}

/**
 * init_hw() - initialize the I2C hardware
 * @dev: private data of I2C Driver
 */
static int init_hw(struct nmk_i2c_dev *dev)
{
	int stat;

	clk_enable(dev->clk);

	stat = flush_i2c_fifo(dev);
	if (stat)
		return stat;

	/* disable the controller */
	i2c_clr_bit(dev->virtbase + I2C_CR , I2C_CR_PE);

	disable_all_interrupts(dev);

	clear_all_interrupts(dev);

	dev->cli.operation = I2C_NO_OPERATION;

	clk_disable(dev->clk);

	udelay(I2C_DELAY);
	return 0;
}

/* enable peripheral, master mode operation */
#define DEFAULT_I2C_REG_CR 	((1 << 1) | I2C_CR_PE)

/**
 * load_i2c_mcr_reg() - load the MCR register
 * @dev: private data of controller
 */
static u32 load_i2c_mcr_reg(struct nmk_i2c_dev *dev)
{
	u32 mcr = 0;

	/* 7-bit address transaction */
	mcr |= GEN_MASK(1, I2C_MCR_AM, 12);
	mcr |= GEN_MASK(dev->cli.slave_adr, I2C_MCR_A7, 1);

	/* start byte procedure not applied */
	mcr |= GEN_MASK(0, I2C_MCR_SB, 11);

	/* check the operation, master read/write? */
	if (dev->cli.operation == I2C_WRITE)
		mcr |= GEN_MASK(I2C_WRITE, I2C_MCR_OP, 0);
	else
		mcr |= GEN_MASK(I2C_READ, I2C_MCR_OP, 0);

	/* stop or repeated start? */
	if (dev->stop)
		mcr |= GEN_MASK(1, I2C_MCR_STOP, 14);
	else
		mcr &= ~(GEN_MASK(1, I2C_MCR_STOP, 14));

	mcr |= GEN_MASK(dev->cli.count, I2C_MCR_LENGTH, 15);

	return mcr;
}

/**
 * setup_i2c_controller() - setup the controller
 * @dev: private data of controller
 */
static void setup_i2c_controller(struct nmk_i2c_dev *dev)
{
	u32 brcr1, brcr2;
	u32 i2c_clk, div;

	writel(0x0, dev->virtbase + I2C_CR);
	writel(0x0, dev->virtbase + I2C_HSMCR);
	writel(0x0, dev->virtbase + I2C_TFTR);
	writel(0x0, dev->virtbase + I2C_RFTR);
	writel(0x0, dev->virtbase + I2C_DMAR);

	/*
	 * set the slsu:
	 *
	 * slsu defines the data setup time after SCL clock
	 * stretching in terms of i2c clk cycles. The
	 * needed setup time for the three modes are 250ns,
	 * 100ns, 10ns repectively thus leading to the values
	 * of 14, 6, 2 for a 48 MHz i2c clk.
	 */
	writel(dev->cfg.slsu << 16, dev->virtbase + I2C_SCR);

	i2c_clk = clk_get_rate(dev->clk);

	/* fallback to std. mode if machine has not provided it */
	if (dev->cfg.clk_freq == 0)
		dev->cfg.clk_freq = 100000;

	/*
	 * The spec says, in case of std. mode the divider is
	 * 2 whereas it is 3 for fast and fastplus mode of
	 * operation. TODO - high speed support.
	 */
	div = (dev->cfg.clk_freq > 100000) ? 3 : 2;

	/*
	 * generate the mask for baud rate counters. The controller
	 * has two baud rate counters. One is used for High speed
	 * operation, and the other is for std, fast mode, fast mode
	 * plus operation. Currently we do not supprt high speed mode
	 * so set brcr1 to 0.
	 */
	brcr1 = 0 << 16;
	brcr2 = (i2c_clk/(dev->cfg.clk_freq * div)) & 0xffff;

	/* set the baud rate counter register */
	writel((brcr1 | brcr2), dev->virtbase + I2C_BRCR);

	/*
	 * set the speed mode. Currently we support
	 * only standard and fast mode of operation
	 * TODO - support for fast mode plus (upto 1Mb/s)
	 * and high speed (up to 3.4 Mb/s)
	 */
	if (dev->cfg.sm > I2C_FREQ_MODE_FAST) {
		dev_err(&dev->pdev->dev, "do not support this mode "
			"defaulting to std. mode\n");
		brcr2 = i2c_clk/(100000 * 2) & 0xffff;
		writel((brcr1 | brcr2), dev->virtbase + I2C_BRCR);
		writel(I2C_FREQ_MODE_STANDARD << 4,
				dev->virtbase + I2C_CR);
	}
	writel(dev->cfg.sm << 4, dev->virtbase + I2C_CR);

	/* set the Tx and Rx FIFO threshold */
	writel(dev->cfg.tft, dev->virtbase + I2C_TFTR);
	writel(dev->cfg.rft, dev->virtbase + I2C_RFTR);
}

/**
 * read_i2c() - Read from I2C client device
 * @dev: private data of I2C Driver
 *
 * This function reads from i2c client device when controller is in
 * master mode. There is a completion timeout. If there is no transfer
 * before timeout error is returned.
 */
static int read_i2c(struct nmk_i2c_dev *dev)
{
	u32 status = 0;
	u32 mcr;
	u32 irq_mask = 0;
	int timeout;

	mcr = load_i2c_mcr_reg(dev);
	writel(mcr, dev->virtbase + I2C_MCR);

	/* load the current CR value */
	writel(readl(dev->virtbase + I2C_CR) | DEFAULT_I2C_REG_CR,
			dev->virtbase + I2C_CR);

	/* enable the controller */
	i2c_set_bit(dev->virtbase + I2C_CR, I2C_CR_PE);

	init_completion(&dev->xfer_complete);

	/* enable interrupts by setting the mask */
	irq_mask = (I2C_IT_RXFNF | I2C_IT_RXFF |
			I2C_IT_MAL | I2C_IT_BERR);

	if (dev->stop)
		irq_mask |= I2C_IT_MTD;
	else
		irq_mask |= I2C_IT_MTDWS;

	irq_mask = I2C_CLEAR_ALL_INTS & IRQ_MASK(irq_mask);

	writel(readl(dev->virtbase + I2C_IMSCR) | irq_mask,
			dev->virtbase + I2C_IMSCR);

	timeout = wait_for_completion_interruptible_timeout(
		&dev->xfer_complete, msecs_to_jiffies(I2C_TIMEOUT_MS));

	if (timeout < 0) {
		dev_err(&dev->pdev->dev,
			"wait_for_completion_interruptible_timeout"
			"returned %d waiting for event\n", timeout);
		status = timeout;
	}

	if (timeout == 0) {
		/* controller has timedout, re-init the h/w */
		dev_err(&dev->pdev->dev, "controller timed out, re-init h/w\n");
		(void) init_hw(dev);
		status = -ETIMEDOUT;
	}
	return status;
}

/**
 * write_i2c() - Write data to I2C client.
 * @dev: private data of I2C Driver
 *
 * This function writes data to I2C client
 */
static int write_i2c(struct nmk_i2c_dev *dev)
{
	u32 status = 0;
	u32 mcr;
	u32 irq_mask = 0;
	int timeout;

	mcr = load_i2c_mcr_reg(dev);

	writel(mcr, dev->virtbase + I2C_MCR);

	/* load the current CR value */
	writel(readl(dev->virtbase + I2C_CR) | DEFAULT_I2C_REG_CR,
			dev->virtbase + I2C_CR);

	/* enable the controller */
	i2c_set_bit(dev->virtbase + I2C_CR , I2C_CR_PE);

	init_completion(&dev->xfer_complete);

	/* enable interrupts by settings the masks */
	irq_mask = (I2C_IT_TXFNE | I2C_IT_TXFOVR |
			I2C_IT_MAL | I2C_IT_BERR);

	/*
	 * check if we want to transfer a single or multiple bytes, if so
	 * set the MTDWS bit (Master Transaction Done Without Stop)
	 * to start repeated start operation
	 */
	if (dev->stop)
		irq_mask |= I2C_IT_MTD;
	else
		irq_mask |= I2C_IT_MTDWS;

	irq_mask = I2C_CLEAR_ALL_INTS & IRQ_MASK(irq_mask);

	writel(readl(dev->virtbase + I2C_IMSCR) | irq_mask,
			dev->virtbase + I2C_IMSCR);

	timeout = wait_for_completion_interruptible_timeout(
		&dev->xfer_complete, msecs_to_jiffies(I2C_TIMEOUT_MS));

	if (timeout < 0) {
		dev_err(&dev->pdev->dev,
			"wait_for_completion_interruptible_timeout"
			"returned %d waiting for event\n", timeout);
		status = timeout;
	}

	if (timeout == 0) {
		/* controller has timedout, re-init the h/w */
		dev_err(&dev->pdev->dev, "controller timed out, re-init h/w\n");
		(void) init_hw(dev);
		status = -ETIMEDOUT;
	}

	return status;
}

/**
 * nmk_i2c_xfer() - I2C transfer function used by kernel framework
 * @i2c_adap: Adapter pointer to the controller
 * @msgs: Pointer to data to be written.
 * @num_msgs: Number of messages to be executed
 *
 * This is the function called by the generic kernel i2c_transfer()
 * or i2c_smbus...() API calls. Note that this code is protected by the
 * semaphore set in the kernel i2c_transfer() function.
 *
 * NOTE:
 * READ TRANSFER : We impose a restriction of the first message to be the
 * 		index message for any read transaction.
 * 		- a no index is coded as '0',
 * 		- 2byte big endian index is coded as '3'
 * 		!!! msg[0].buf holds the actual index.
 * 		This is compatible with generic messages of smbus emulator
 * 		that send a one byte index.
 * 		eg. a I2C transation to read 2 bytes from index 0
 *			idx = 0;
 *			msg[0].addr = client->addr;
 *			msg[0].flags = 0x0;
 *			msg[0].len = 1;
 *			msg[0].buf = &idx;
 *
 *			msg[1].addr = client->addr;
 *			msg[1].flags = I2C_M_RD;
 *			msg[1].len = 2;
 *			msg[1].buf = rd_buff
 *			i2c_transfer(adap, msg, 2);
 *
 * WRITE TRANSFER : The I2C standard interface interprets all data as payload.
 *		If you want to emulate an SMBUS write transaction put the
 *		index as first byte(or first and second) in the payload.
 *		eg. a I2C transation to write 2 bytes from index 1
 *			wr_buff[0] = 0x1;
 *			wr_buff[1] = 0x23;
 *			wr_buff[2] = 0x46;
 *			msg[0].flags = 0x0;
 *			msg[0].len = 3;
 *			msg[0].buf = wr_buff;
 *			i2c_transfer(adap, msg, 1);
 *
 * To read or write a block of data (multiple bytes) using SMBUS emulation
 * please use the i2c_smbus_read_i2c_block_data()
 * or i2c_smbus_write_i2c_block_data() API
 */
static int nmk_i2c_xfer(struct i2c_adapter *i2c_adap,
		struct i2c_msg msgs[], int num_msgs)
{
	int status;
	int i;
	u32 cause;
	struct nmk_i2c_dev *dev = i2c_get_adapdata(i2c_adap);

	status = init_hw(dev);
	if (status)
		return status;

	clk_enable(dev->clk);

	/* setup the i2c controller */
	setup_i2c_controller(dev);

	for (i = 0; i < num_msgs; i++) {
		if (unlikely(msgs[i].flags & I2C_M_TEN)) {
			dev_err(&dev->pdev->dev, "10 bit addressing"
					"not supported\n");
			return -EINVAL;
		}
		dev->cli.slave_adr	= msgs[i].addr;
		dev->cli.buffer		= msgs[i].buf;
		dev->cli.count		= msgs[i].len;
		dev->stop = (i < (num_msgs - 1)) ? 0 : 1;
		dev->result = 0;

		if (msgs[i].flags & I2C_M_RD) {
			/* it is a read operation */
			dev->cli.operation = I2C_READ;
			status = read_i2c(dev);
		} else {
			/* write operation */
			dev->cli.operation = I2C_WRITE;
			status = write_i2c(dev);
		}
		if (status || (dev->result)) {
			/* get the abort cause */
			cause =	(readl(dev->virtbase + I2C_SR) >> 4) & 0x7;
			dev_err(&dev->pdev->dev, "error during I2C"
					"message xfer: %d\n", cause);
			dev_err(&dev->pdev->dev, "%s\n",
				cause >= ARRAY_SIZE(abort_causes)
				? "unknown reason" : abort_causes[cause]);
			clk_disable(dev->clk);
			return status;
		}
		udelay(I2C_DELAY);
	}
	clk_disable(dev->clk);

	/* return the no. messages processed */
	if (status)
		return status;
	else
		return num_msgs;
}

/**
 * disable_interrupts() - disable the interrupts
 * @dev: private data of controller
 * @irq: interrupt number
 */
static int disable_interrupts(struct nmk_i2c_dev *dev, u32 irq)
{
	irq = IRQ_MASK(irq);
	writel(readl(dev->virtbase + I2C_IMSCR) & ~(I2C_CLEAR_ALL_INTS & irq),
			dev->virtbase + I2C_IMSCR);
	return 0;
}

/**
 * i2c_irq_handler() - interrupt routine
 * @irq: interrupt number
 * @arg: data passed to the handler
 *
 * This is the interrupt handler for the i2c driver. Currently
 * it handles the major interrupts like Rx & Tx FIFO management
 * interrupts, master transaction interrupts, arbitration and
 * bus error interrupts. The rest of the interrupts are treated as
 * unhandled.
 */
static irqreturn_t i2c_irq_handler(int irq, void *arg)
{
	struct nmk_i2c_dev *dev = arg;
	u32 tft, rft;
	u32 count;
	u32 misr;
	u32 src = 0;

	/* load Tx FIFO and Rx FIFO threshold values */
	tft = readl(dev->virtbase + I2C_TFTR);
	rft = readl(dev->virtbase + I2C_RFTR);

	/* read interrupt status register */
	misr = readl(dev->virtbase + I2C_MISR);

	src = __ffs(misr);
	switch ((1 << src)) {

	/* Transmit FIFO nearly empty interrupt */
	case I2C_IT_TXFNE:
	{
		if (dev->cli.operation == I2C_READ) {
			/*
			 * in read operation why do we care for writing?
			 * so disable the Transmit FIFO interrupt
			 */
			disable_interrupts(dev, I2C_IT_TXFNE);
		} else {
			for (count = (MAX_I2C_FIFO_THRESHOLD - tft - 2);
					(count > 0) &&
					(dev->cli.count != 0);
					count--) {
				/* write to the Tx FIFO */
				writeb(*dev->cli.buffer,
					dev->virtbase + I2C_TFR);
				dev->cli.buffer++;
				dev->cli.count--;
				dev->cli.xfer_bytes++;
			}
			/*
			 * if done, close the transfer by disabling the
			 * corresponding TXFNE interrupt
			 */
			if (dev->cli.count == 0)
				disable_interrupts(dev,	I2C_IT_TXFNE);
		}
	}
	break;

	/*
	 * Rx FIFO nearly full interrupt.
	 * This is set when the numer of entries in Rx FIFO is
	 * greater or equal than the threshold value programmed
	 * in RFT
	 */
	case I2C_IT_RXFNF:
		for (count = rft; count > 0; count--) {
			/* Read the Rx FIFO */
			*dev->cli.buffer = readb(dev->virtbase + I2C_RFR);
			dev->cli.buffer++;
		}
		dev->cli.count -= rft;
		dev->cli.xfer_bytes += rft;
		break;

	/* Rx FIFO full */
	case I2C_IT_RXFF:
		for (count = MAX_I2C_FIFO_THRESHOLD; count > 0; count--) {
			*dev->cli.buffer = readb(dev->virtbase + I2C_RFR);
			dev->cli.buffer++;
		}
		dev->cli.count -= MAX_I2C_FIFO_THRESHOLD;
		dev->cli.xfer_bytes += MAX_I2C_FIFO_THRESHOLD;
		break;

	/* Master Transaction Done with/without stop */
	case I2C_IT_MTD:
	case I2C_IT_MTDWS:
		if (dev->cli.operation == I2C_READ) {
			while (!(readl(dev->virtbase + I2C_RISR)
				 & I2C_IT_RXFE)) {
				if (dev->cli.count == 0)
					break;
				*dev->cli.buffer =
					readb(dev->virtbase + I2C_RFR);
				dev->cli.buffer++;
				dev->cli.count--;
				dev->cli.xfer_bytes++;
			}
		}

		i2c_set_bit(dev->virtbase + I2C_ICR, I2C_IT_MTD);
		i2c_set_bit(dev->virtbase + I2C_ICR, I2C_IT_MTDWS);

		disable_interrupts(dev,
				(I2C_IT_TXFNE | I2C_IT_TXFE | I2C_IT_TXFF
					| I2C_IT_TXFOVR | I2C_IT_RXFNF
					| I2C_IT_RXFF | I2C_IT_RXFE));

		if (dev->cli.count) {
			dev->result = -1;
			dev_err(&dev->pdev->dev, "%lu bytes still remain to be"
					"xfered\n", dev->cli.count);
			(void) init_hw(dev);
		}
		complete(&dev->xfer_complete);

		break;

	/* Master Arbitration lost interrupt */
	case I2C_IT_MAL:
		dev->result = -1;
		(void) init_hw(dev);

		i2c_set_bit(dev->virtbase + I2C_ICR, I2C_IT_MAL);
		complete(&dev->xfer_complete);

		break;

	/*
	 * Bus Error interrupt.
	 * This happens when an unexpected start/stop condition occurs
	 * during the transaction.
	 */
	case I2C_IT_BERR:
		dev->result = -1;
		/* get the status */
		if (((readl(dev->virtbase + I2C_SR) >> 2) & 0x3) == I2C_ABORT)
			(void) init_hw(dev);

		i2c_set_bit(dev->virtbase + I2C_ICR, I2C_IT_BERR);
		complete(&dev->xfer_complete);

		break;

	/*
	 * Tx FIFO overrun interrupt.
	 * This is set when a write operation in Tx FIFO is performed and
	 * the Tx FIFO is full.
	 */
	case I2C_IT_TXFOVR:
		dev->result = -1;
		(void) init_hw(dev);

		dev_err(&dev->pdev->dev, "Tx Fifo Over run\n");
		complete(&dev->xfer_complete);

		break;

	/* unhandled interrupts by this driver - TODO*/
	case I2C_IT_TXFE:
	case I2C_IT_TXFF:
	case I2C_IT_RXFE:
	case I2C_IT_RFSR:
	case I2C_IT_RFSE:
	case I2C_IT_WTSR:
	case I2C_IT_STD:
		dev_err(&dev->pdev->dev, "unhandled Interrupt\n");
		break;
	default:
		dev_err(&dev->pdev->dev, "spurious Interrupt..\n");
		break;
	}

	return IRQ_HANDLED;
}

static unsigned int nmk_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm nmk_i2c_algo = {
	.master_xfer	= nmk_i2c_xfer,
	.functionality	= nmk_i2c_functionality
};

static int __devinit nmk_i2c_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	struct nmk_i2c_controller *pdata =
			pdev->dev.platform_data;
	struct nmk_i2c_dev	*dev;
	struct i2c_adapter *adap;

	dev = kzalloc(sizeof(struct nmk_i2c_dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "cannot allocate memory\n");
		ret = -ENOMEM;
		goto err_no_mem;
	}

	dev->pdev = pdev;
	platform_set_drvdata(pdev, dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENOENT;
		goto err_no_resource;
	}

	if (request_mem_region(res->start, resource_size(res),
		DRIVER_NAME "I/O region") == 	NULL)	{
		ret = -EBUSY;
		goto err_no_region;
	}

	dev->virtbase = ioremap(res->start, resource_size(res));
	if (!dev->virtbase) {
		ret = -ENOMEM;
		goto err_no_ioremap;
	}

	dev->irq = platform_get_irq(pdev, 0);
	ret = request_irq(dev->irq, i2c_irq_handler, IRQF_DISABLED,
				DRIVER_NAME, dev);
	if (ret) {
		dev_err(&pdev->dev, "cannot claim the irq %d\n", dev->irq);
		goto err_irq;
	}

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		dev_err(&pdev->dev, "could not get i2c clock\n");
		ret = PTR_ERR(dev->clk);
		goto err_no_clk;
	}

	adap = &dev->adap;
	adap->dev.parent = &pdev->dev;
	adap->owner	= THIS_MODULE;
	adap->class	= I2C_CLASS_HWMON | I2C_CLASS_SPD;
	adap->algo	= &nmk_i2c_algo;
	snprintf(adap->name, sizeof(adap->name),
		 "Nomadik I2C%d at %lx", pdev->id, (unsigned long)res->start);

	/* fetch the controller id */
	adap->nr	= pdev->id;

	/* fetch the controller configuration from machine */
	dev->cfg.clk_freq = pdata->clk_freq;
	dev->cfg.slsu	= pdata->slsu;
	dev->cfg.tft	= pdata->tft;
	dev->cfg.rft	= pdata->rft;
	dev->cfg.sm	= pdata->sm;

	i2c_set_adapdata(adap, dev);

	ret = init_hw(dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "error in initializing i2c hardware\n");
		goto err_init_hw;
	}

	dev_info(&pdev->dev, "initialize %s on virtual "
		"base %p\n", adap->name, dev->virtbase);

	ret = i2c_add_numbered_adapter(adap);
	if (ret) {
		dev_err(&pdev->dev, "failed to add adapter\n");
		goto err_add_adap;
	}

	return 0;

 err_init_hw:
 err_add_adap:
	clk_put(dev->clk);
 err_no_clk:
	free_irq(dev->irq, dev);
 err_irq:
	iounmap(dev->virtbase);
 err_no_ioremap:
	release_mem_region(res->start, resource_size(res));
 err_no_region:
	platform_set_drvdata(pdev, NULL);
 err_no_resource:
	kfree(dev);
 err_no_mem:

	return ret;
}

static int __devexit nmk_i2c_remove(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct nmk_i2c_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adap);
	flush_i2c_fifo(dev);
	disable_all_interrupts(dev);
	clear_all_interrupts(dev);
	/* disable the controller */
	i2c_clr_bit(dev->virtbase + I2C_CR, I2C_CR_PE);
	free_irq(dev->irq, dev);
	iounmap(dev->virtbase);
	if (res)
		release_mem_region(res->start, resource_size(res));
	clk_put(dev->clk);
	platform_set_drvdata(pdev, NULL);
	kfree(dev);

	return 0;
}

static struct platform_driver nmk_i2c_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = DRIVER_NAME,
	},
	.probe = nmk_i2c_probe,
	.remove = __devexit_p(nmk_i2c_remove),
};

static int __init nmk_i2c_init(void)
{
	return platform_driver_register(&nmk_i2c_driver);
}

static void __exit nmk_i2c_exit(void)
{
	platform_driver_unregister(&nmk_i2c_driver);
}

subsys_initcall(nmk_i2c_init);
module_exit(nmk_i2c_exit);

MODULE_AUTHOR("Sachin Verma, Srinidhi KASAGAR");
MODULE_DESCRIPTION("Nomadik/Ux500 I2C driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
