// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 ST-Ericsson SA
 * Copyright (C) 2009 STMicroelectronics
 *
 * I2C master mode controller driver, used in Nomadik 8815
 * and Ux500 platforms.
 *
 * The Mobileye EyeQ5 and EyeQ6H platforms are also supported; they use
 * the same Ux500/DB8500 IP block with two quirks:
 *  - The memory bus only supports 32-bit accesses.
 *  - (only EyeQ5) A register must be configured for the I2C speed mode;
 *    it is located in a shared register region called OLB.
 *
 * Author: Srinidhi Kasagar <srinidhi.kasagar@stericsson.com>
 * Author: Sachin Verma <sachin.verma@st.com>
 */
#include <linux/amba/bus.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>

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
#define I2C_CR_PE		BIT(0)		/* Peripheral Enable */
#define I2C_CR_OM		GENMASK(2, 1)	/* Operating mode */
#define I2C_CR_SAM		BIT(3)		/* Slave addressing mode */
#define I2C_CR_SM		GENMASK(5, 4)	/* Speed mode */
#define I2C_CR_SGCM		BIT(6)		/* Slave general call mode */
#define I2C_CR_FTX		BIT(7)		/* Flush Transmit */
#define I2C_CR_FRX		BIT(8)		/* Flush Receive */
#define I2C_CR_DMA_TX_EN	BIT(9)		/* DMA Tx enable */
#define I2C_CR_DMA_RX_EN	BIT(10)		/* DMA Rx Enable */
#define I2C_CR_DMA_SLE		BIT(11)		/* DMA sync. logic enable */
#define I2C_CR_LM		BIT(12)		/* Loopback mode */
#define I2C_CR_FON		GENMASK(14, 13)	/* Filtering on */
#define I2C_CR_FS		GENMASK(16, 15)	/* Force stop enable */

/* Slave control register (SCR) */
#define I2C_SCR_SLSU		GENMASK(31, 16)	/* Slave data setup time */

/* Master controller (MCR) register */
#define I2C_MCR_OP		BIT(0)		/* Operation */
#define I2C_MCR_A7		GENMASK(7, 1)	/* 7-bit address */
#define I2C_MCR_EA10		GENMASK(10, 8)	/* 10-bit Extended address */
#define I2C_MCR_SB		BIT(11)		/* Extended address */
#define I2C_MCR_AM		GENMASK(13, 12)	/* Address type */
#define I2C_MCR_STOP		BIT(14)		/* Stop condition */
#define I2C_MCR_LENGTH		GENMASK(25, 15)	/* Transaction length */

/* Status register (SR) */
#define I2C_SR_OP		GENMASK(1, 0)	/* Operation */
#define I2C_SR_STATUS		GENMASK(3, 2)	/* controller status */
#define I2C_SR_CAUSE		GENMASK(6, 4)	/* Abort cause */
#define I2C_SR_TYPE		GENMASK(8, 7)	/* Receive type */
#define I2C_SR_LENGTH		GENMASK(19, 9)	/* Transfer length */

/* Baud-rate counter register (BRCR) */
#define I2C_BRCR_BRCNT1		GENMASK(31, 16)	/* Baud-rate counter 1 */
#define I2C_BRCR_BRCNT2		GENMASK(15, 0)	/* Baud-rate counter 2 */

/* Interrupt mask set/clear (IMSCR) bits */
#define I2C_IT_TXFE		BIT(0)
#define I2C_IT_TXFNE		BIT(1)
#define I2C_IT_TXFF		BIT(2)
#define I2C_IT_TXFOVR		BIT(3)
#define I2C_IT_RXFE		BIT(4)
#define I2C_IT_RXFNF		BIT(5)
#define I2C_IT_RXFF		BIT(6)
#define I2C_IT_RFSR		BIT(16)
#define I2C_IT_RFSE		BIT(17)
#define I2C_IT_WTSR		BIT(18)
#define I2C_IT_MTD		BIT(19)
#define I2C_IT_STD		BIT(20)
#define I2C_IT_MAL		BIT(24)
#define I2C_IT_BERR		BIT(25)
#define I2C_IT_MTDWS		BIT(28)

/* some bits in ICR are reserved */
#define I2C_CLEAR_ALL_INTS	0x131f007f

/* maximum threshold value */
#define MAX_I2C_FIFO_THRESHOLD	15

enum i2c_freq_mode {
	I2C_FREQ_MODE_STANDARD,		/* up to 100 Kb/s */
	I2C_FREQ_MODE_FAST,		/* up to 400 Kb/s */
	I2C_FREQ_MODE_HIGH_SPEED,	/* up to 3.4 Mb/s */
	I2C_FREQ_MODE_FAST_PLUS,	/* up to 1 Mb/s */
};

/* Mobileye EyeQ5 offset into a shared register region (called OLB) */
#define NMK_I2C_EYEQ5_OLB_IOCR2			0x0B8

enum i2c_eyeq5_speed {
	I2C_EYEQ5_SPEED_FAST,
	I2C_EYEQ5_SPEED_FAST_PLUS,
	I2C_EYEQ5_SPEED_HIGH_SPEED,
};

/**
 * struct i2c_vendor_data - per-vendor variations
 * @has_mtdws: variant has the MTDWS bit
 * @fifodepth: variant FIFO depth
 */
struct i2c_vendor_data {
	bool has_mtdws;
	u32 fifodepth;
};

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

enum i2c_operating_mode {
	I2C_OM_SLAVE,
	I2C_OM_MASTER,
	I2C_OM_MASTER_OR_SLAVE,
};

/**
 * struct i2c_nmk_client - client specific data
 * @slave_adr: 7-bit slave address
 * @count: no. bytes to be transferred
 * @buffer: client data buffer
 * @xfer_bytes: bytes transferred till now
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
 * struct nmk_i2c_dev - private data structure of the controller.
 * @vendor: vendor data for this variant.
 * @adev: parent amba device.
 * @adap: corresponding I2C adapter.
 * @irq: interrupt line for the controller.
 * @virtbase: virtual io memory area.
 * @clk: hardware i2c block clock.
 * @cli: holder of client specific data.
 * @clk_freq: clock frequency for the operation mode
 * @tft: Tx FIFO Threshold in bytes
 * @rft: Rx FIFO Threshold in bytes
 * @timeout_usecs: Slave response timeout
 * @sm: speed mode
 * @stop: stop condition.
 * @xfer_wq: xfer done wait queue.
 * @xfer_done: xfer done boolean.
 * @result: controller propogated result.
 * @has_32b_bus: controller is on a bus that only supports 32-bit accesses.
 */
struct nmk_i2c_dev {
	struct i2c_vendor_data		*vendor;
	struct amba_device		*adev;
	struct i2c_adapter		adap;
	int				irq;
	void __iomem			*virtbase;
	struct clk			*clk;
	struct i2c_nmk_client		cli;
	u32				clk_freq;
	unsigned char			tft;
	unsigned char			rft;
	u32				timeout_usecs;
	enum i2c_freq_mode		sm;
	int				stop;
	struct wait_queue_head		xfer_wq;
	bool				xfer_done;
	int				result;
	bool				has_32b_bus;
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

static inline u8 nmk_i2c_readb(const struct nmk_i2c_dev *priv,
			       unsigned long reg)
{
	if (priv->has_32b_bus)
		return readl(priv->virtbase + reg);
	else
		return readb(priv->virtbase + reg);
}

static inline void nmk_i2c_writeb(const struct nmk_i2c_dev *priv, u32 val,
				  unsigned long reg)
{
	if (priv->has_32b_bus)
		writel(val, priv->virtbase + reg);
	else
		writeb(val, priv->virtbase + reg);
}

/**
 * flush_i2c_fifo() - This function flushes the I2C FIFO
 * @priv: private data of I2C Driver
 *
 * This function flushes the I2C Tx and Rx FIFOs. It returns
 * 0 on successful flushing of FIFO
 */
static int flush_i2c_fifo(struct nmk_i2c_dev *priv)
{
#define LOOP_ATTEMPTS 10
	ktime_t timeout;
	int i;

	/*
	 * flush the transmit and receive FIFO. The flushing
	 * operation takes several cycles before to be completed.
	 * On the completion, the I2C internal logic clears these
	 * bits, until then no one must access Tx, Rx FIFO and
	 * should poll on these bits waiting for the completion.
	 */
	writel((I2C_CR_FTX | I2C_CR_FRX), priv->virtbase + I2C_CR);

	for (i = 0; i < LOOP_ATTEMPTS; i++) {
		timeout = ktime_add_us(ktime_get(), priv->timeout_usecs);

		while (ktime_after(timeout, ktime_get())) {
			if ((readl(priv->virtbase + I2C_CR) &
				(I2C_CR_FTX | I2C_CR_FRX)) == 0)
				return 0;
		}
	}

	dev_err(&priv->adev->dev,
		"flushing operation timed out giving up after %d attempts",
		LOOP_ATTEMPTS);

	return -ETIMEDOUT;
}

/**
 * disable_all_interrupts() - Disable all interrupts of this I2c Bus
 * @priv: private data of I2C Driver
 */
static void disable_all_interrupts(struct nmk_i2c_dev *priv)
{
	writel(0, priv->virtbase + I2C_IMSCR);
}

/**
 * clear_all_interrupts() - Clear all interrupts of I2C Controller
 * @priv: private data of I2C Driver
 */
static void clear_all_interrupts(struct nmk_i2c_dev *priv)
{
	writel(I2C_CLEAR_ALL_INTS, priv->virtbase + I2C_ICR);
}

/**
 * init_hw() - initialize the I2C hardware
 * @priv: private data of I2C Driver
 */
static int init_hw(struct nmk_i2c_dev *priv)
{
	int stat;

	stat = flush_i2c_fifo(priv);
	if (stat)
		goto exit;

	/* disable the controller */
	i2c_clr_bit(priv->virtbase + I2C_CR, I2C_CR_PE);

	disable_all_interrupts(priv);

	clear_all_interrupts(priv);

	priv->cli.operation = I2C_NO_OPERATION;

exit:
	return stat;
}

/* enable peripheral, master mode operation */
#define DEFAULT_I2C_REG_CR	(FIELD_PREP(I2C_CR_OM, I2C_OM_MASTER) | I2C_CR_PE)

/* grab top three bits from extended I2C addresses */
#define ADR_3MSB_BITS		GENMASK(9, 7)

/**
 * load_i2c_mcr_reg() - load the MCR register
 * @priv: private data of controller
 * @flags: message flags
 */
static u32 load_i2c_mcr_reg(struct nmk_i2c_dev *priv, u16 flags)
{
	u32 mcr = 0;
	unsigned short slave_adr_3msb_bits;

	mcr |= FIELD_PREP(I2C_MCR_A7, priv->cli.slave_adr);

	if (unlikely(flags & I2C_M_TEN)) {
		/* 10-bit address transaction */
		mcr |= FIELD_PREP(I2C_MCR_AM, 2);
		/*
		 * Get the top 3 bits.
		 * EA10 represents extended address in MCR. This includes
		 * the extension (MSB bits) of the 7 bit address loaded
		 * in A7
		 */
		slave_adr_3msb_bits = FIELD_GET(ADR_3MSB_BITS,
						priv->cli.slave_adr);

		mcr |= FIELD_PREP(I2C_MCR_EA10, slave_adr_3msb_bits);
	} else {
		/* 7-bit address transaction */
		mcr |= FIELD_PREP(I2C_MCR_AM, 1);
	}

	/* start byte procedure not applied */
	mcr |= FIELD_PREP(I2C_MCR_SB, 0);

	/* check the operation, master read/write? */
	if (priv->cli.operation == I2C_WRITE)
		mcr |= FIELD_PREP(I2C_MCR_OP, I2C_WRITE);
	else
		mcr |= FIELD_PREP(I2C_MCR_OP, I2C_READ);

	/* stop or repeated start? */
	if (priv->stop)
		mcr |= FIELD_PREP(I2C_MCR_STOP, 1);
	else
		mcr &= ~FIELD_PREP(I2C_MCR_STOP, 1);

	mcr |= FIELD_PREP(I2C_MCR_LENGTH, priv->cli.count);

	return mcr;
}

/**
 * setup_i2c_controller() - setup the controller
 * @priv: private data of controller
 */
static void setup_i2c_controller(struct nmk_i2c_dev *priv)
{
	u32 brcr;
	u32 i2c_clk, div;
	u32 ns;
	u16 slsu;

	writel(0x0, priv->virtbase + I2C_CR);
	writel(0x0, priv->virtbase + I2C_HSMCR);
	writel(0x0, priv->virtbase + I2C_TFTR);
	writel(0x0, priv->virtbase + I2C_RFTR);
	writel(0x0, priv->virtbase + I2C_DMAR);

	i2c_clk = clk_get_rate(priv->clk);

	/*
	 * set the slsu:
	 *
	 * slsu defines the data setup time after SCL clock
	 * stretching in terms of i2c clk cycles + 1 (zero means
	 * "wait one cycle"), the needed setup time for the three
	 * modes are 250ns, 100ns, 10ns respectively.
	 *
	 * As the time for one cycle T in nanoseconds is
	 * T = (1/f) * 1000000000 =>
	 * slsu = cycles / (1000000000 / f) + 1
	 */
	ns = DIV_ROUND_UP_ULL(1000000000ULL, i2c_clk);
	switch (priv->sm) {
	case I2C_FREQ_MODE_FAST:
	case I2C_FREQ_MODE_FAST_PLUS:
		slsu = DIV_ROUND_UP(100, ns); /* Fast */
		break;
	case I2C_FREQ_MODE_HIGH_SPEED:
		slsu = DIV_ROUND_UP(10, ns); /* High */
		break;
	case I2C_FREQ_MODE_STANDARD:
	default:
		slsu = DIV_ROUND_UP(250, ns); /* Standard */
		break;
	}
	slsu += 1;

	dev_dbg(&priv->adev->dev, "calculated SLSU = %04x\n", slsu);
	writel(FIELD_PREP(I2C_SCR_SLSU, slsu), priv->virtbase + I2C_SCR);

	/*
	 * The spec says, in case of std. mode the divider is
	 * 2 whereas it is 3 for fast and fastplus mode of
	 * operation.
	 */
	div = (priv->clk_freq > I2C_MAX_STANDARD_MODE_FREQ) ? 3 : 2;

	/*
	 * generate the mask for baud rate counters. The controller
	 * has two baud rate counters. One is used for High speed
	 * operation, and the other is for std, fast mode, fast mode
	 * plus operation.
	 *
	 * BRCR is a clock divider amount. Pick highest value that
	 * leads to rate strictly below target. Eg when asking for
	 * 400kHz you want a bus rate <=400kHz (and not >=400kHz).
	 */
	brcr = DIV_ROUND_UP(i2c_clk, priv->clk_freq * div);

	if (priv->sm == I2C_FREQ_MODE_HIGH_SPEED)
		brcr = FIELD_PREP(I2C_BRCR_BRCNT1, brcr);
	else
		brcr = FIELD_PREP(I2C_BRCR_BRCNT2, brcr);

	/* set the baud rate counter register */
	writel(brcr, priv->virtbase + I2C_BRCR);

	/* set the speed mode */
	writel(FIELD_PREP(I2C_CR_SM, priv->sm), priv->virtbase + I2C_CR);

	/* set the Tx and Rx FIFO threshold */
	writel(priv->tft, priv->virtbase + I2C_TFTR);
	writel(priv->rft, priv->virtbase + I2C_RFTR);
}

static bool nmk_i2c_wait_xfer_done(struct nmk_i2c_dev *priv)
{
	if (priv->timeout_usecs < jiffies_to_usecs(1)) {
		unsigned long timeout_usecs = priv->timeout_usecs;
		ktime_t timeout = ktime_set(0, timeout_usecs * NSEC_PER_USEC);

		wait_event_hrtimeout(priv->xfer_wq, priv->xfer_done, timeout);
	} else {
		unsigned long timeout = usecs_to_jiffies(priv->timeout_usecs);

		wait_event_timeout(priv->xfer_wq, priv->xfer_done, timeout);
	}

	return priv->xfer_done;
}

/**
 * read_i2c() - Read from I2C client device
 * @priv: private data of I2C Driver
 * @flags: message flags
 *
 * This function reads from i2c client device when controller is in
 * master mode. There is a completion timeout. If there is no transfer
 * before timeout error is returned.
 */
static int read_i2c(struct nmk_i2c_dev *priv, u16 flags)
{
	u32 mcr, irq_mask;
	int status = 0;
	bool xfer_done;

	mcr = load_i2c_mcr_reg(priv, flags);
	writel(mcr, priv->virtbase + I2C_MCR);

	/* load the current CR value */
	writel(readl(priv->virtbase + I2C_CR) | DEFAULT_I2C_REG_CR,
	       priv->virtbase + I2C_CR);

	/* enable the controller */
	i2c_set_bit(priv->virtbase + I2C_CR, I2C_CR_PE);

	init_waitqueue_head(&priv->xfer_wq);
	priv->xfer_done = false;

	/* enable interrupts by setting the mask */
	irq_mask = (I2C_IT_RXFNF | I2C_IT_RXFF |
			I2C_IT_MAL | I2C_IT_BERR);

	if (priv->stop || !priv->vendor->has_mtdws)
		irq_mask |= I2C_IT_MTD;
	else
		irq_mask |= I2C_IT_MTDWS;

	irq_mask &= I2C_CLEAR_ALL_INTS;

	writel(readl(priv->virtbase + I2C_IMSCR) | irq_mask,
	       priv->virtbase + I2C_IMSCR);

	xfer_done = nmk_i2c_wait_xfer_done(priv);

	if (!xfer_done)
		status = -ETIMEDOUT;

	return status;
}

static void fill_tx_fifo(struct nmk_i2c_dev *priv, int no_bytes)
{
	int count;

	for (count = (no_bytes - 2);
			(count > 0) &&
			(priv->cli.count != 0);
			count--) {
		/* write to the Tx FIFO */
		nmk_i2c_writeb(priv, *priv->cli.buffer, I2C_TFR);
		priv->cli.buffer++;
		priv->cli.count--;
		priv->cli.xfer_bytes++;
	}

}

/**
 * write_i2c() - Write data to I2C client.
 * @priv: private data of I2C Driver
 * @flags: message flags
 *
 * This function writes data to I2C client
 */
static int write_i2c(struct nmk_i2c_dev *priv, u16 flags)
{
	u32 mcr, irq_mask;
	u32 status = 0;
	bool xfer_done;

	mcr = load_i2c_mcr_reg(priv, flags);

	writel(mcr, priv->virtbase + I2C_MCR);

	/* load the current CR value */
	writel(readl(priv->virtbase + I2C_CR) | DEFAULT_I2C_REG_CR,
	       priv->virtbase + I2C_CR);

	/* enable the controller */
	i2c_set_bit(priv->virtbase + I2C_CR, I2C_CR_PE);

	init_waitqueue_head(&priv->xfer_wq);
	priv->xfer_done = false;

	/* enable interrupts by settings the masks */
	irq_mask = (I2C_IT_TXFOVR | I2C_IT_MAL | I2C_IT_BERR);

	/* Fill the TX FIFO with transmit data */
	fill_tx_fifo(priv, MAX_I2C_FIFO_THRESHOLD);

	if (priv->cli.count != 0)
		irq_mask |= I2C_IT_TXFNE;

	/*
	 * check if we want to transfer a single or multiple bytes, if so
	 * set the MTDWS bit (Master Transaction Done Without Stop)
	 * to start repeated start operation
	 */
	if (priv->stop || !priv->vendor->has_mtdws)
		irq_mask |= I2C_IT_MTD;
	else
		irq_mask |= I2C_IT_MTDWS;

	irq_mask &= I2C_CLEAR_ALL_INTS;

	writel(readl(priv->virtbase + I2C_IMSCR) | irq_mask,
	       priv->virtbase + I2C_IMSCR);

	xfer_done = nmk_i2c_wait_xfer_done(priv);

	if (!xfer_done) {
		/* Controller timed out */
		dev_err(&priv->adev->dev, "write to slave 0x%x timed out\n",
			priv->cli.slave_adr);
		status = -ETIMEDOUT;
	}

	return status;
}

/**
 * nmk_i2c_xfer_one() - transmit a single I2C message
 * @priv: device with a message encoded into it
 * @flags: message flags
 */
static int nmk_i2c_xfer_one(struct nmk_i2c_dev *priv, u16 flags)
{
	int status;

	if (flags & I2C_M_RD) {
		/* read operation */
		priv->cli.operation = I2C_READ;
		status = read_i2c(priv, flags);
	} else {
		/* write operation */
		priv->cli.operation = I2C_WRITE;
		status = write_i2c(priv, flags);
	}

	if (status || priv->result) {
		u32 i2c_sr;
		u32 cause;

		i2c_sr = readl(priv->virtbase + I2C_SR);
		if (FIELD_GET(I2C_SR_STATUS, i2c_sr) == I2C_ABORT) {
			cause = FIELD_GET(I2C_SR_CAUSE, i2c_sr);
			dev_err(&priv->adev->dev, "%s\n",
				cause >= ARRAY_SIZE(abort_causes) ?
				"unknown reason" :
				abort_causes[cause]);
		}

		init_hw(priv);

		status = status ? status : priv->result;
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
 *		index message for any read transaction.
 *		- a no index is coded as '0',
 *		- 2byte big endian index is coded as '3'
 *		!!! msg[0].buf holds the actual index.
 *		This is compatible with generic messages of smbus emulator
 *		that send a one byte index.
 *		eg. a I2C transation to read 2 bytes from index 0
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
	int status = 0;
	int i;
	struct nmk_i2c_dev *priv = i2c_get_adapdata(i2c_adap);
	int j;

	pm_runtime_get_sync(&priv->adev->dev);

	/* Attempt three times to send the message queue */
	for (j = 0; j < 3; j++) {
		/* setup the i2c controller */
		setup_i2c_controller(priv);

		for (i = 0; i < num_msgs; i++) {
			priv->cli.slave_adr	= msgs[i].addr;
			priv->cli.buffer		= msgs[i].buf;
			priv->cli.count		= msgs[i].len;
			priv->stop = (i < (num_msgs - 1)) ? 0 : 1;
			priv->result = 0;

			status = nmk_i2c_xfer_one(priv, msgs[i].flags);
			if (status != 0)
				break;
		}
		if (status == 0)
			break;
	}

	pm_runtime_put_sync(&priv->adev->dev);

	/* return the no. messages processed */
	if (status)
		return status;
	else
		return num_msgs;
}

/**
 * disable_interrupts() - disable the interrupts
 * @priv: private data of controller
 * @irq: interrupt number
 */
static int disable_interrupts(struct nmk_i2c_dev *priv, u32 irq)
{
	irq &= I2C_CLEAR_ALL_INTS;
	writel(readl(priv->virtbase + I2C_IMSCR) & ~irq,
	       priv->virtbase + I2C_IMSCR);
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
	struct nmk_i2c_dev *priv = arg;
	struct device *dev = &priv->adev->dev;
	u32 tft, rft;
	u32 count;
	u32 misr, src;

	/* load Tx FIFO and Rx FIFO threshold values */
	tft = readl(priv->virtbase + I2C_TFTR);
	rft = readl(priv->virtbase + I2C_RFTR);

	/* read interrupt status register */
	misr = readl(priv->virtbase + I2C_MISR);

	src = __ffs(misr);
	switch (BIT(src)) {

	/* Transmit FIFO nearly empty interrupt */
	case I2C_IT_TXFNE:
	{
		if (priv->cli.operation == I2C_READ) {
			/*
			 * in read operation why do we care for writing?
			 * so disable the Transmit FIFO interrupt
			 */
			disable_interrupts(priv, I2C_IT_TXFNE);
		} else {
			fill_tx_fifo(priv, (MAX_I2C_FIFO_THRESHOLD - tft));
			/*
			 * if done, close the transfer by disabling the
			 * corresponding TXFNE interrupt
			 */
			if (priv->cli.count == 0)
				disable_interrupts(priv,	I2C_IT_TXFNE);
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
			*priv->cli.buffer = nmk_i2c_readb(priv, I2C_RFR);
			priv->cli.buffer++;
		}
		priv->cli.count -= rft;
		priv->cli.xfer_bytes += rft;
		break;

	/* Rx FIFO full */
	case I2C_IT_RXFF:
		for (count = MAX_I2C_FIFO_THRESHOLD; count > 0; count--) {
			*priv->cli.buffer = nmk_i2c_readb(priv, I2C_RFR);
			priv->cli.buffer++;
		}
		priv->cli.count -= MAX_I2C_FIFO_THRESHOLD;
		priv->cli.xfer_bytes += MAX_I2C_FIFO_THRESHOLD;
		break;

	/* Master Transaction Done with/without stop */
	case I2C_IT_MTD:
	case I2C_IT_MTDWS:
		if (priv->cli.operation == I2C_READ) {
			while (!(readl(priv->virtbase + I2C_RISR)
				 & I2C_IT_RXFE)) {
				if (priv->cli.count == 0)
					break;
				*priv->cli.buffer =
					nmk_i2c_readb(priv, I2C_RFR);
				priv->cli.buffer++;
				priv->cli.count--;
				priv->cli.xfer_bytes++;
			}
		}

		disable_all_interrupts(priv);
		clear_all_interrupts(priv);

		if (priv->cli.count) {
			priv->result = -EIO;
			dev_err(dev, "%lu bytes still remain to be xfered\n",
				priv->cli.count);
			init_hw(priv);
		}
		priv->xfer_done = true;
		wake_up(&priv->xfer_wq);


		break;

	/* Master Arbitration lost interrupt */
	case I2C_IT_MAL:
		priv->result = -EIO;
		init_hw(priv);

		i2c_set_bit(priv->virtbase + I2C_ICR, I2C_IT_MAL);
		priv->xfer_done = true;
		wake_up(&priv->xfer_wq);


		break;

	/*
	 * Bus Error interrupt.
	 * This happens when an unexpected start/stop condition occurs
	 * during the transaction.
	 */
	case I2C_IT_BERR:
	{
		u32 sr;

		sr = readl(priv->virtbase + I2C_SR);
		priv->result = -EIO;
		if (FIELD_GET(I2C_SR_STATUS, sr) == I2C_ABORT)
			init_hw(priv);

		i2c_set_bit(priv->virtbase + I2C_ICR, I2C_IT_BERR);
		priv->xfer_done = true;
		wake_up(&priv->xfer_wq);

	}
	break;

	/*
	 * Tx FIFO overrun interrupt.
	 * This is set when a write operation in Tx FIFO is performed and
	 * the Tx FIFO is full.
	 */
	case I2C_IT_TXFOVR:
		priv->result = -EIO;
		init_hw(priv);

		dev_err(dev, "Tx Fifo Over run\n");
		priv->xfer_done = true;
		wake_up(&priv->xfer_wq);


		break;

	/* unhandled interrupts by this driver - TODO*/
	case I2C_IT_TXFE:
	case I2C_IT_TXFF:
	case I2C_IT_RXFE:
	case I2C_IT_RFSR:
	case I2C_IT_RFSE:
	case I2C_IT_WTSR:
	case I2C_IT_STD:
		dev_err(dev, "unhandled Interrupt\n");
		break;
	default:
		dev_err(dev, "spurious Interrupt..\n");
		break;
	}

	return IRQ_HANDLED;
}

static int nmk_i2c_suspend_late(struct device *dev)
{
	int ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	pinctrl_pm_select_sleep_state(dev);
	return 0;
}

static int nmk_i2c_resume_early(struct device *dev)
{
	return pm_runtime_force_resume(dev);
}

static int nmk_i2c_runtime_suspend(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct nmk_i2c_dev *priv = amba_get_drvdata(adev);

	clk_disable_unprepare(priv->clk);
	pinctrl_pm_select_idle_state(dev);
	return 0;
}

static int nmk_i2c_runtime_resume(struct device *dev)
{
	struct amba_device *adev = to_amba_device(dev);
	struct nmk_i2c_dev *priv = amba_get_drvdata(adev);
	int ret;

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "can't prepare_enable clock\n");
		return ret;
	}

	pinctrl_pm_select_default_state(dev);

	ret = init_hw(priv);
	if (ret) {
		clk_disable_unprepare(priv->clk);
		pinctrl_pm_select_idle_state(dev);
	}

	return ret;
}

static const struct dev_pm_ops nmk_i2c_pm = {
	LATE_SYSTEM_SLEEP_PM_OPS(nmk_i2c_suspend_late, nmk_i2c_resume_early)
	RUNTIME_PM_OPS(nmk_i2c_runtime_suspend, nmk_i2c_runtime_resume, NULL)
};

static unsigned int nmk_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm nmk_i2c_algo = {
	.xfer = nmk_i2c_xfer,
	.functionality = nmk_i2c_functionality
};

static void nmk_i2c_of_probe(struct device_node *np,
			     struct nmk_i2c_dev *priv)
{
	u32 timeout_usecs;

	/* Default to 100 kHz if no frequency is given in the node */
	if (of_property_read_u32(np, "clock-frequency", &priv->clk_freq))
		priv->clk_freq = I2C_MAX_STANDARD_MODE_FREQ;

	if (priv->clk_freq <= I2C_MAX_STANDARD_MODE_FREQ)
		priv->sm = I2C_FREQ_MODE_STANDARD;
	else if (priv->clk_freq <= I2C_MAX_FAST_MODE_FREQ)
		priv->sm = I2C_FREQ_MODE_FAST;
	else if (priv->clk_freq <= I2C_MAX_FAST_MODE_PLUS_FREQ)
		priv->sm = I2C_FREQ_MODE_FAST_PLUS;
	else
		priv->sm = I2C_FREQ_MODE_HIGH_SPEED;
	priv->tft = 1; /* Tx FIFO threshold */
	priv->rft = 8; /* Rx FIFO threshold */

	/* Slave response timeout */
	if (!of_property_read_u32(np, "i2c-transfer-timeout-us", &timeout_usecs))
		priv->timeout_usecs = timeout_usecs;
	else
		priv->timeout_usecs = 200 * USEC_PER_MSEC;
}

static const unsigned int nmk_i2c_eyeq5_masks[] = {
	GENMASK(5, 4),
	GENMASK(7, 6),
	GENMASK(9, 8),
	GENMASK(11, 10),
	GENMASK(13, 12),
};

static int nmk_i2c_eyeq5_probe(struct nmk_i2c_dev *priv)
{
	struct device *dev = &priv->adev->dev;
	struct device_node *np = dev->of_node;
	unsigned int mask, speed_mode;
	struct regmap *olb;
	unsigned int id;

	olb = syscon_regmap_lookup_by_phandle_args(np, "mobileye,olb", 1, &id);
	if (IS_ERR(olb))
		return PTR_ERR(olb);
	if (id >= ARRAY_SIZE(nmk_i2c_eyeq5_masks))
		return -ENOENT;

	if (priv->clk_freq <= 400000)
		speed_mode = I2C_EYEQ5_SPEED_FAST;
	else if (priv->clk_freq <= 1000000)
		speed_mode = I2C_EYEQ5_SPEED_FAST_PLUS;
	else
		speed_mode = I2C_EYEQ5_SPEED_HIGH_SPEED;

	mask = nmk_i2c_eyeq5_masks[id];
	regmap_update_bits(olb, NMK_I2C_EYEQ5_OLB_IOCR2,
			   mask, speed_mode << __fls(mask));

	return 0;
}

#define NMK_I2C_EYEQ_FLAG_32B_BUS	BIT(0)
#define NMK_I2C_EYEQ_FLAG_IS_EYEQ5	BIT(1)

static const struct of_device_id nmk_i2c_eyeq_match_table[] = {
	{
		.compatible = "mobileye,eyeq5-i2c",
		.data = (void *)(NMK_I2C_EYEQ_FLAG_32B_BUS | NMK_I2C_EYEQ_FLAG_IS_EYEQ5),
	},
	{
		.compatible = "mobileye,eyeq6h-i2c",
		.data = (void *)NMK_I2C_EYEQ_FLAG_32B_BUS,
	},
	{ /* sentinel */ }
};

static int nmk_i2c_probe(struct amba_device *adev, const struct amba_id *id)
{
	struct i2c_vendor_data *vendor = id->data;
	u32 max_fifo_threshold = (vendor->fifodepth / 2) - 1;
	struct device_node *np = adev->dev.of_node;
	const struct of_device_id *match;
	struct device *dev = &adev->dev;
	unsigned long match_flags = 0;
	struct nmk_i2c_dev *priv;
	struct i2c_adapter *adap;
	int ret = 0;

	/*
	 * We do not want to attach a .of_match_table to our amba driver.
	 * Do not convert to device_get_match_data().
	 */
	match = of_match_device(nmk_i2c_eyeq_match_table, dev);
	if (match)
		match_flags = (unsigned long)match->data;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->vendor = vendor;
	priv->adev = adev;
	priv->has_32b_bus = match_flags & NMK_I2C_EYEQ_FLAG_32B_BUS;
	nmk_i2c_of_probe(np, priv);

	if (match_flags & NMK_I2C_EYEQ_FLAG_IS_EYEQ5) {
		ret = nmk_i2c_eyeq5_probe(priv);
		if (ret)
			return dev_err_probe(dev, ret, "failed OLB lookup\n");
	}

	if (priv->tft > max_fifo_threshold) {
		dev_warn(dev, "requested TX FIFO threshold %u, adjusted down to %u\n",
			 priv->tft, max_fifo_threshold);
		priv->tft = max_fifo_threshold;
	}

	if (priv->rft > max_fifo_threshold) {
		dev_warn(dev, "requested RX FIFO threshold %u, adjusted down to %u\n",
			 priv->rft, max_fifo_threshold);
		priv->rft = max_fifo_threshold;
	}

	amba_set_drvdata(adev, priv);

	priv->virtbase = devm_ioremap(dev, adev->res.start,
				      resource_size(&adev->res));
	if (!priv->virtbase)
		return -ENOMEM;

	priv->irq = adev->irq[0];
	ret = devm_request_irq(dev, priv->irq, i2c_irq_handler, 0,
			       DRIVER_NAME, priv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "cannot claim the irq %d\n", priv->irq);

	priv->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "could enable i2c clock\n");

	init_hw(priv);

	adap = &priv->adap;
	adap->dev.of_node = np;
	adap->dev.parent = dev;
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_DEPRECATED;
	adap->algo = &nmk_i2c_algo;
	adap->timeout = usecs_to_jiffies(priv->timeout_usecs);
	snprintf(adap->name, sizeof(adap->name),
		 "Nomadik I2C at %pR", &adev->res);

	i2c_set_adapdata(adap, priv);

	dev_info(dev,
		 "initialize %s on virtual base %p\n",
		 adap->name, priv->virtbase);

	ret = i2c_add_adapter(adap);
	if (ret)
		return ret;

	pm_runtime_put(dev);

	return 0;
}

static void nmk_i2c_remove(struct amba_device *adev)
{
	struct nmk_i2c_dev *priv = amba_get_drvdata(adev);

	i2c_del_adapter(&priv->adap);
	flush_i2c_fifo(priv);
	disable_all_interrupts(priv);
	clear_all_interrupts(priv);
	/* disable the controller */
	i2c_clr_bit(priv->virtbase + I2C_CR, I2C_CR_PE);
}

static struct i2c_vendor_data vendor_stn8815 = {
	.has_mtdws = false,
	.fifodepth = 16, /* Guessed from TFTR/RFTR = 7 */
};

static struct i2c_vendor_data vendor_db8500 = {
	.has_mtdws = true,
	.fifodepth = 32, /* Guessed from TFTR/RFTR = 15 */
};

static const struct amba_id nmk_i2c_ids[] = {
	{
		.id	= 0x00180024,
		.mask	= 0x00ffffff,
		.data	= &vendor_stn8815,
	},
	{
		.id	= 0x00380024,
		.mask	= 0x00ffffff,
		.data	= &vendor_db8500,
	},
	{},
};

MODULE_DEVICE_TABLE(amba, nmk_i2c_ids);

static struct amba_driver nmk_i2c_driver = {
	.drv = {
		.name = DRIVER_NAME,
		.pm = pm_ptr(&nmk_i2c_pm),
	},
	.id_table = nmk_i2c_ids,
	.probe = nmk_i2c_probe,
	.remove = nmk_i2c_remove,
};

static int __init nmk_i2c_init(void)
{
	return amba_driver_register(&nmk_i2c_driver);
}

static void __exit nmk_i2c_exit(void)
{
	amba_driver_unregister(&nmk_i2c_driver);
}

subsys_initcall(nmk_i2c_init);
module_exit(nmk_i2c_exit);

MODULE_AUTHOR("Sachin Verma");
MODULE_AUTHOR("Srinidhi KASAGAR");
MODULE_DESCRIPTION("Nomadik/Ux500 I2C driver");
MODULE_LICENSE("GPL");
