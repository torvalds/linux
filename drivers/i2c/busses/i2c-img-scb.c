/*
 * I2C adapter for the IMG Serial Control Bus (SCB) IP block.
 *
 * Copyright (C) 2009, 2010, 2012, 2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * There are three ways that this I2C controller can be driven:
 *
 * - Raw control of the SDA and SCK signals.
 *
 *   This corresponds to MODE_RAW, which takes control of the signals
 *   directly for a certain number of clock cycles (the INT_TIMING
 *   interrupt can be used for timing).
 *
 * - Atomic commands. A low level I2C symbol (such as generate
 *   start/stop/ack/nack bit, generate byte, receive byte, and receive
 *   ACK) is given to the hardware, with detection of completion by bits
 *   in the LINESTAT register.
 *
 *   This mode of operation is used by MODE_ATOMIC, which uses an I2C
 *   state machine in the interrupt handler to compose/react to I2C
 *   transactions using atomic mode commands, and also by MODE_SEQUENCE,
 *   which emits a simple fixed sequence of atomic mode commands.
 *
 *   Due to software control, the use of atomic commands usually results
 *   in suboptimal use of the bus, with gaps between the I2C symbols while
 *   the driver decides what to do next.
 *
 * - Automatic mode. A bus address, and whether to read/write is
 *   specified, and the hardware takes care of the I2C state machine,
 *   using a FIFO to send/receive bytes of data to an I2C slave. The
 *   driver just has to keep the FIFO drained or filled in response to the
 *   appropriate FIFO interrupts.
 *
 *   This corresponds to MODE_AUTOMATIC, which manages the FIFOs and deals
 *   with control of repeated start bits between I2C messages.
 *
 *   Use of automatic mode and the FIFO can make much more efficient use
 *   of the bus compared to individual atomic commands, with potentially
 *   no wasted time between I2C symbols or I2C messages.
 *
 * In most cases MODE_AUTOMATIC is used, however if any of the messages in
 * a transaction are zero byte writes (e.g. used by i2cdetect for probing
 * the bus), MODE_ATOMIC must be used since automatic mode is normally
 * started by the writing of data into the FIFO.
 *
 * The other modes are used in specific circumstances where MODE_ATOMIC and
 * MODE_AUTOMATIC aren't appropriate. MODE_RAW is used to implement a bus
 * recovery routine. MODE_SEQUENCE is used to reset the bus and make sure
 * it is in a sane state.
 *
 * Notice that the driver implements a timer-based timeout mechanism.
 * The reason for this mechanism is to reduce the number of interrupts
 * received in automatic mode.
 *
 * The driver would get a slave event and transaction done interrupts for
 * each atomic mode command that gets completed. However, these events are
 * not needed in automatic mode, becase those atomic mode commands are
 * managed automatically by the hardware.
 *
 * In practice, normal I2C transactions will be complete well before you
 * get the timer interrupt, as the timer is re-scheduled during FIFO
 * maintenance and disabled after the transaction is complete.
 *
 * In this way normal automatic mode operation isn't impacted by
 * unnecessary interrupts, but the exceptional abort condition can still be
 * detected (with a slight delay).
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timer.h>

/* Register offsets */

#define SCB_STATUS_REG			0x00
#define SCB_OVERRIDE_REG		0x04
#define SCB_READ_ADDR_REG		0x08
#define SCB_READ_COUNT_REG		0x0c
#define SCB_WRITE_ADDR_REG		0x10
#define SCB_READ_DATA_REG		0x14
#define SCB_WRITE_DATA_REG		0x18
#define SCB_FIFO_STATUS_REG		0x1c
#define SCB_CONTROL_SOFT_RESET		0x1f
#define SCB_CLK_SET_REG			0x3c
#define SCB_INT_STATUS_REG		0x40
#define SCB_INT_CLEAR_REG		0x44
#define SCB_INT_MASK_REG		0x48
#define SCB_CONTROL_REG			0x4c
#define SCB_TIME_TPL_REG		0x50
#define SCB_TIME_TPH_REG		0x54
#define SCB_TIME_TP2S_REG		0x58
#define SCB_TIME_TBI_REG		0x60
#define SCB_TIME_TSL_REG		0x64
#define SCB_TIME_TDL_REG		0x68
#define SCB_TIME_TSDL_REG		0x6c
#define SCB_TIME_TSDH_REG		0x70
#define SCB_READ_XADDR_REG		0x74
#define SCB_WRITE_XADDR_REG		0x78
#define SCB_WRITE_COUNT_REG		0x7c
#define SCB_CORE_REV_REG		0x80
#define SCB_TIME_TCKH_REG		0x84
#define SCB_TIME_TCKL_REG		0x88
#define SCB_FIFO_FLUSH_REG		0x8c
#define SCB_READ_FIFO_REG		0x94
#define SCB_CLEAR_REG			0x98

/* SCB_CONTROL_REG bits */

#define SCB_CONTROL_CLK_ENABLE		0x1e0
#define SCB_CONTROL_TRANSACTION_HALT	0x200

#define FIFO_READ_FULL			BIT(0)
#define FIFO_READ_EMPTY			BIT(1)
#define FIFO_WRITE_FULL			BIT(2)
#define FIFO_WRITE_EMPTY		BIT(3)

/* SCB_CLK_SET_REG bits */
#define SCB_FILT_DISABLE		BIT(31)
#define SCB_FILT_BYPASS			BIT(30)
#define SCB_FILT_INC_MASK		0x7f
#define SCB_FILT_INC_SHIFT		16
#define SCB_INC_MASK			0x7f
#define SCB_INC_SHIFT			8

/* SCB_INT_*_REG bits */

#define INT_BUS_INACTIVE		BIT(0)
#define INT_UNEXPECTED_START		BIT(1)
#define INT_SCLK_LOW_TIMEOUT		BIT(2)
#define INT_SDAT_LOW_TIMEOUT		BIT(3)
#define INT_WRITE_ACK_ERR		BIT(4)
#define INT_ADDR_ACK_ERR		BIT(5)
#define INT_FIFO_FULL			BIT(9)
#define INT_FIFO_FILLING		BIT(10)
#define INT_FIFO_EMPTY			BIT(11)
#define INT_FIFO_EMPTYING		BIT(12)
#define INT_TRANSACTION_DONE		BIT(15)
#define INT_SLAVE_EVENT			BIT(16)
#define INT_MASTER_HALTED		BIT(17)
#define INT_TIMING			BIT(18)
#define INT_STOP_DETECTED		BIT(19)

#define INT_FIFO_FULL_FILLING	(INT_FIFO_FULL  | INT_FIFO_FILLING)

/* Level interrupts need clearing after handling instead of before */
#define INT_LEVEL			0x01e00

/* Don't allow any interrupts while the clock may be off */
#define INT_ENABLE_MASK_INACTIVE	0x00000

/* Interrupt masks for the different driver modes */

#define INT_ENABLE_MASK_RAW		INT_TIMING

#define INT_ENABLE_MASK_ATOMIC		(INT_TRANSACTION_DONE | \
					 INT_SLAVE_EVENT      | \
					 INT_ADDR_ACK_ERR     | \
					 INT_WRITE_ACK_ERR)

#define INT_ENABLE_MASK_AUTOMATIC	(INT_SCLK_LOW_TIMEOUT | \
					 INT_ADDR_ACK_ERR     | \
					 INT_WRITE_ACK_ERR    | \
					 INT_FIFO_FULL        | \
					 INT_FIFO_FILLING     | \
					 INT_FIFO_EMPTY       | \
					 INT_MASTER_HALTED    | \
					 INT_STOP_DETECTED)

#define INT_ENABLE_MASK_WAITSTOP	(INT_SLAVE_EVENT      | \
					 INT_ADDR_ACK_ERR     | \
					 INT_WRITE_ACK_ERR)

/* SCB_STATUS_REG fields */

#define LINESTAT_SCLK_LINE_STATUS	BIT(0)
#define LINESTAT_SCLK_EN		BIT(1)
#define LINESTAT_SDAT_LINE_STATUS	BIT(2)
#define LINESTAT_SDAT_EN		BIT(3)
#define LINESTAT_DET_START_STATUS	BIT(4)
#define LINESTAT_DET_STOP_STATUS	BIT(5)
#define LINESTAT_DET_ACK_STATUS		BIT(6)
#define LINESTAT_DET_NACK_STATUS	BIT(7)
#define LINESTAT_BUS_IDLE		BIT(8)
#define LINESTAT_T_DONE_STATUS		BIT(9)
#define LINESTAT_SCLK_OUT_STATUS	BIT(10)
#define LINESTAT_SDAT_OUT_STATUS	BIT(11)
#define LINESTAT_GEN_LINE_MASK_STATUS	BIT(12)
#define LINESTAT_START_BIT_DET		BIT(13)
#define LINESTAT_STOP_BIT_DET		BIT(14)
#define LINESTAT_ACK_DET		BIT(15)
#define LINESTAT_NACK_DET		BIT(16)
#define LINESTAT_INPUT_HELD_V		BIT(17)
#define LINESTAT_ABORT_DET		BIT(18)
#define LINESTAT_ACK_OR_NACK_DET	(LINESTAT_ACK_DET | LINESTAT_NACK_DET)
#define LINESTAT_INPUT_DATA		0xff000000
#define LINESTAT_INPUT_DATA_SHIFT	24

#define LINESTAT_CLEAR_SHIFT		13
#define LINESTAT_LATCHED		(0x3f << LINESTAT_CLEAR_SHIFT)

/* SCB_OVERRIDE_REG fields */

#define OVERRIDE_SCLK_OVR		BIT(0)
#define OVERRIDE_SCLKEN_OVR		BIT(1)
#define OVERRIDE_SDAT_OVR		BIT(2)
#define OVERRIDE_SDATEN_OVR		BIT(3)
#define OVERRIDE_MASTER			BIT(9)
#define OVERRIDE_LINE_OVR_EN		BIT(10)
#define OVERRIDE_DIRECT			BIT(11)
#define OVERRIDE_CMD_SHIFT		4
#define OVERRIDE_CMD_MASK		0x1f
#define OVERRIDE_DATA_SHIFT		24

#define OVERRIDE_SCLK_DOWN		(OVERRIDE_LINE_OVR_EN | \
					 OVERRIDE_SCLKEN_OVR)
#define OVERRIDE_SCLK_UP		(OVERRIDE_LINE_OVR_EN | \
					 OVERRIDE_SCLKEN_OVR | \
					 OVERRIDE_SCLK_OVR)
#define OVERRIDE_SDAT_DOWN		(OVERRIDE_LINE_OVR_EN | \
					 OVERRIDE_SDATEN_OVR)
#define OVERRIDE_SDAT_UP		(OVERRIDE_LINE_OVR_EN | \
					 OVERRIDE_SDATEN_OVR | \
					 OVERRIDE_SDAT_OVR)

/* OVERRIDE_CMD values */

#define CMD_PAUSE			0x00
#define CMD_GEN_DATA			0x01
#define CMD_GEN_START			0x02
#define CMD_GEN_STOP			0x03
#define CMD_GEN_ACK			0x04
#define CMD_GEN_NACK			0x05
#define CMD_RET_DATA			0x08
#define CMD_RET_ACK			0x09

/* Fixed timing values */

#define TIMEOUT_TBI			0x0
#define TIMEOUT_TSL			0xffff
#define TIMEOUT_TDL			0x0

/* Transaction timeout */

#define IMG_I2C_TIMEOUT			(msecs_to_jiffies(1000))

/*
 * Worst incs are 1 (innacurate) and 16*256 (irregular).
 * So a sensible inc is the logarithmic mean: 64 (2^6), which is
 * in the middle of the valid range (0-127).
 */
#define SCB_OPT_INC		64

/* Setup the clock enable filtering for 25 ns */
#define SCB_FILT_GLITCH		25

/*
 * Bits to return from interrupt handler functions for different modes.
 * This delays completion until we've finished with the registers, so that the
 * function waiting for completion can safely disable the clock to save power.
 */
#define ISR_COMPLETE_M		BIT(31)
#define ISR_FATAL_M		BIT(30)
#define ISR_WAITSTOP		BIT(29)
#define ISR_STATUS_M		0x0000ffff	/* contains +ve errno */
#define ISR_COMPLETE(err)	(ISR_COMPLETE_M | (ISR_STATUS_M & (err)))
#define ISR_FATAL(err)		(ISR_COMPLETE(err) | ISR_FATAL_M)

enum img_i2c_mode {
	MODE_INACTIVE,
	MODE_RAW,
	MODE_ATOMIC,
	MODE_AUTOMATIC,
	MODE_SEQUENCE,
	MODE_FATAL,
	MODE_WAITSTOP,
	MODE_SUSPEND,
};

/* Timing parameters for i2c modes (in ns) */
struct img_i2c_timings {
	const char *name;
	unsigned int max_bitrate;
	unsigned int tckh, tckl, tsdh, tsdl;
	unsigned int tp2s, tpl, tph;
};

/* The timings array must be ordered from slower to faster */
static struct img_i2c_timings timings[] = {
	/* Standard mode */
	{
		.name = "standard",
		.max_bitrate = 100000,
		.tckh = 4000,
		.tckl = 4700,
		.tsdh = 4700,
		.tsdl = 8700,
		.tp2s = 4700,
		.tpl = 4700,
		.tph = 4000,
	},
	/* Fast mode */
	{
		.name = "fast",
		.max_bitrate = 400000,
		.tckh = 600,
		.tckl = 1300,
		.tsdh = 600,
		.tsdl = 1200,
		.tp2s = 1300,
		.tpl = 600,
		.tph = 600,
	},
};

/* Reset dance */
static u8 img_i2c_reset_seq[] = { CMD_GEN_START,
				  CMD_GEN_DATA, 0xff,
				  CMD_RET_ACK,
				  CMD_GEN_START,
				  CMD_GEN_STOP,
				  0 };
/* Just issue a stop (after an abort condition) */
static u8 img_i2c_stop_seq[] = {  CMD_GEN_STOP,
				  0 };

/* We're interested in different interrupts depending on the mode */
static unsigned int img_i2c_int_enable_by_mode[] = {
	[MODE_INACTIVE]  = INT_ENABLE_MASK_INACTIVE,
	[MODE_RAW]       = INT_ENABLE_MASK_RAW,
	[MODE_ATOMIC]    = INT_ENABLE_MASK_ATOMIC,
	[MODE_AUTOMATIC] = INT_ENABLE_MASK_AUTOMATIC,
	[MODE_SEQUENCE]  = INT_ENABLE_MASK_ATOMIC,
	[MODE_FATAL]     = 0,
	[MODE_WAITSTOP]  = INT_ENABLE_MASK_WAITSTOP,
	[MODE_SUSPEND]   = 0,
};

/* Atomic command names */
static const char * const img_i2c_atomic_cmd_names[] = {
	[CMD_PAUSE]	= "PAUSE",
	[CMD_GEN_DATA]	= "GEN_DATA",
	[CMD_GEN_START]	= "GEN_START",
	[CMD_GEN_STOP]	= "GEN_STOP",
	[CMD_GEN_ACK]	= "GEN_ACK",
	[CMD_GEN_NACK]	= "GEN_NACK",
	[CMD_RET_DATA]	= "RET_DATA",
	[CMD_RET_ACK]	= "RET_ACK",
};

struct img_i2c {
	struct i2c_adapter adap;

	void __iomem *base;

	/*
	 * The scb core clock is used to get the input frequency, and to disable
	 * it after every set of transactions to save some power.
	 */
	struct clk *scb_clk, *sys_clk;
	unsigned int bitrate;
	bool need_wr_rd_fence;

	/* state */
	struct completion msg_complete;
	spinlock_t lock;	/* lock before doing anything with the state */
	struct i2c_msg msg;

	/* After the last transaction, wait for a stop bit */
	bool last_msg;
	int msg_status;

	enum img_i2c_mode mode;
	u32 int_enable;		/* depends on mode */
	u32 line_status;	/* line status over command */

	/*
	 * To avoid slave event interrupts in automatic mode, use a timer to
	 * poll the abort condition if we don't get an interrupt for too long.
	 */
	struct timer_list check_timer;
	bool t_halt;

	/* atomic mode state */
	bool at_t_done;
	bool at_slave_event;
	int at_cur_cmd;
	u8 at_cur_data;

	/* Sequence: either reset or stop. See img_i2c_sequence. */
	u8 *seq;

	/* raw mode */
	unsigned int raw_timeout;
};

static void img_i2c_writel(struct img_i2c *i2c, u32 offset, u32 value)
{
	writel(value, i2c->base + offset);
}

static u32 img_i2c_readl(struct img_i2c *i2c, u32 offset)
{
	return readl(i2c->base + offset);
}

/*
 * The code to read from the master read fifo, and write to the master
 * write fifo, checks a bit in an SCB register before every byte to
 * ensure that the fifo is not full (write fifo) or empty (read fifo).
 * Due to clock domain crossing inside the SCB block the updated value
 * of this bit is only visible after 2 cycles.
 *
 * The scb_wr_rd_fence() function does 2 dummy writes (to the read-only
 * revision register), and it's called after reading from or writing to the
 * fifos to ensure that subsequent reads of the fifo status bits do not read
 * stale values.
 */
static void img_i2c_wr_rd_fence(struct img_i2c *i2c)
{
	if (i2c->need_wr_rd_fence) {
		img_i2c_writel(i2c, SCB_CORE_REV_REG, 0);
		img_i2c_writel(i2c, SCB_CORE_REV_REG, 0);
	}
}

static void img_i2c_switch_mode(struct img_i2c *i2c, enum img_i2c_mode mode)
{
	i2c->mode = mode;
	i2c->int_enable = img_i2c_int_enable_by_mode[mode];
	i2c->line_status = 0;
}

static void img_i2c_raw_op(struct img_i2c *i2c)
{
	i2c->raw_timeout = 0;
	img_i2c_writel(i2c, SCB_OVERRIDE_REG,
		OVERRIDE_SCLKEN_OVR |
		OVERRIDE_SDATEN_OVR |
		OVERRIDE_MASTER |
		OVERRIDE_LINE_OVR_EN |
		OVERRIDE_DIRECT |
		((i2c->at_cur_cmd & OVERRIDE_CMD_MASK) << OVERRIDE_CMD_SHIFT) |
		(i2c->at_cur_data << OVERRIDE_DATA_SHIFT));
}

static const char *img_i2c_atomic_op_name(unsigned int cmd)
{
	if (unlikely(cmd >= ARRAY_SIZE(img_i2c_atomic_cmd_names)))
		return "UNKNOWN";
	return img_i2c_atomic_cmd_names[cmd];
}

/* Send a single atomic mode command to the hardware */
static void img_i2c_atomic_op(struct img_i2c *i2c, int cmd, u8 data)
{
	i2c->at_cur_cmd = cmd;
	i2c->at_cur_data = data;

	/* work around lack of data setup time when generating data */
	if (cmd == CMD_GEN_DATA && i2c->mode == MODE_ATOMIC) {
		u32 line_status = img_i2c_readl(i2c, SCB_STATUS_REG);

		if (line_status & LINESTAT_SDAT_LINE_STATUS && !(data & 0x80)) {
			/* hold the data line down for a moment */
			img_i2c_switch_mode(i2c, MODE_RAW);
			img_i2c_raw_op(i2c);
			return;
		}
	}

	dev_dbg(i2c->adap.dev.parent,
		"atomic cmd=%s (%d) data=%#x\n",
		img_i2c_atomic_op_name(cmd), cmd, data);
	i2c->at_t_done = (cmd == CMD_RET_DATA || cmd == CMD_RET_ACK);
	i2c->at_slave_event = false;
	i2c->line_status = 0;

	img_i2c_writel(i2c, SCB_OVERRIDE_REG,
		((cmd & OVERRIDE_CMD_MASK) << OVERRIDE_CMD_SHIFT) |
		OVERRIDE_MASTER |
		OVERRIDE_DIRECT |
		(data << OVERRIDE_DATA_SHIFT));
}

/* Start a transaction in atomic mode */
static void img_i2c_atomic_start(struct img_i2c *i2c)
{
	img_i2c_switch_mode(i2c, MODE_ATOMIC);
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);
	img_i2c_atomic_op(i2c, CMD_GEN_START, 0x00);
}

static void img_i2c_soft_reset(struct img_i2c *i2c)
{
	i2c->t_halt = false;
	img_i2c_writel(i2c, SCB_CONTROL_REG, 0);
	img_i2c_writel(i2c, SCB_CONTROL_REG,
		       SCB_CONTROL_CLK_ENABLE | SCB_CONTROL_SOFT_RESET);
}

/*
 * Enable or release transaction halt for control of repeated starts.
 * In version 3.3 of the IP when transaction halt is set, an interrupt
 * will be generated after each byte of a transfer instead of after
 * every transfer but before the stop bit.
 * Due to this behaviour we have to be careful that every time we
 * release the transaction halt we have to re-enable it straight away
 * so that we only process a single byte, not doing so will result in
 * all remaining bytes been processed and a stop bit being issued,
 * which will prevent us having a repeated start.
 */
static void img_i2c_transaction_halt(struct img_i2c *i2c, bool t_halt)
{
	u32 val;

	if (i2c->t_halt == t_halt)
		return;
	i2c->t_halt = t_halt;
	val = img_i2c_readl(i2c, SCB_CONTROL_REG);
	if (t_halt)
		val |= SCB_CONTROL_TRANSACTION_HALT;
	else
		val &= ~SCB_CONTROL_TRANSACTION_HALT;
	img_i2c_writel(i2c, SCB_CONTROL_REG, val);
}

/* Drain data from the FIFO into the buffer (automatic mode) */
static void img_i2c_read_fifo(struct img_i2c *i2c)
{
	while (i2c->msg.len) {
		u32 fifo_status;
		u8 data;

		img_i2c_wr_rd_fence(i2c);
		fifo_status = img_i2c_readl(i2c, SCB_FIFO_STATUS_REG);
		if (fifo_status & FIFO_READ_EMPTY)
			break;

		data = img_i2c_readl(i2c, SCB_READ_DATA_REG);
		*i2c->msg.buf = data;

		img_i2c_writel(i2c, SCB_READ_FIFO_REG, 0xff);
		i2c->msg.len--;
		i2c->msg.buf++;
	}
}

/* Fill the FIFO with data from the buffer (automatic mode) */
static void img_i2c_write_fifo(struct img_i2c *i2c)
{
	while (i2c->msg.len) {
		u32 fifo_status;

		img_i2c_wr_rd_fence(i2c);
		fifo_status = img_i2c_readl(i2c, SCB_FIFO_STATUS_REG);
		if (fifo_status & FIFO_WRITE_FULL)
			break;

		img_i2c_writel(i2c, SCB_WRITE_DATA_REG, *i2c->msg.buf);
		i2c->msg.len--;
		i2c->msg.buf++;
	}

	/* Disable fifo emptying interrupt if nothing more to write */
	if (!i2c->msg.len)
		i2c->int_enable &= ~INT_FIFO_EMPTYING;
}

/* Start a read transaction in automatic mode */
static void img_i2c_read(struct img_i2c *i2c)
{
	img_i2c_switch_mode(i2c, MODE_AUTOMATIC);
	if (!i2c->last_msg)
		i2c->int_enable |= INT_SLAVE_EVENT;

	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);
	img_i2c_writel(i2c, SCB_READ_ADDR_REG, i2c->msg.addr);
	img_i2c_writel(i2c, SCB_READ_COUNT_REG, i2c->msg.len);

	mod_timer(&i2c->check_timer, jiffies + msecs_to_jiffies(1));
}

/* Start a write transaction in automatic mode */
static void img_i2c_write(struct img_i2c *i2c)
{
	img_i2c_switch_mode(i2c, MODE_AUTOMATIC);
	if (!i2c->last_msg)
		i2c->int_enable |= INT_SLAVE_EVENT;

	img_i2c_writel(i2c, SCB_WRITE_ADDR_REG, i2c->msg.addr);
	img_i2c_writel(i2c, SCB_WRITE_COUNT_REG, i2c->msg.len);

	mod_timer(&i2c->check_timer, jiffies + msecs_to_jiffies(1));
	img_i2c_write_fifo(i2c);

	/* img_i2c_write_fifo() may modify int_enable */
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);
}

/*
 * Indicate that the transaction is complete. This is called from the
 * ISR to wake up the waiting thread, after which the ISR must not
 * access any more SCB registers.
 */
static void img_i2c_complete_transaction(struct img_i2c *i2c, int status)
{
	img_i2c_switch_mode(i2c, MODE_INACTIVE);
	if (status) {
		i2c->msg_status = status;
		img_i2c_transaction_halt(i2c, false);
	}
	complete(&i2c->msg_complete);
}

static unsigned int img_i2c_raw_atomic_delay_handler(struct img_i2c *i2c,
					u32 int_status, u32 line_status)
{
	/* Stay in raw mode for this, so we don't just loop infinitely */
	img_i2c_atomic_op(i2c, i2c->at_cur_cmd, i2c->at_cur_data);
	img_i2c_switch_mode(i2c, MODE_ATOMIC);
	return 0;
}

static unsigned int img_i2c_raw(struct img_i2c *i2c, u32 int_status,
				u32 line_status)
{
	if (int_status & INT_TIMING) {
		if (i2c->raw_timeout == 0)
			return img_i2c_raw_atomic_delay_handler(i2c,
				int_status, line_status);
		--i2c->raw_timeout;
	}
	return 0;
}

static unsigned int img_i2c_sequence(struct img_i2c *i2c, u32 int_status)
{
	static const unsigned int continue_bits[] = {
		[CMD_GEN_START] = LINESTAT_START_BIT_DET,
		[CMD_GEN_DATA]  = LINESTAT_INPUT_HELD_V,
		[CMD_RET_ACK]   = LINESTAT_ACK_DET | LINESTAT_NACK_DET,
		[CMD_RET_DATA]  = LINESTAT_INPUT_HELD_V,
		[CMD_GEN_STOP]  = LINESTAT_STOP_BIT_DET,
	};
	int next_cmd = -1;
	u8 next_data = 0x00;

	if (int_status & INT_SLAVE_EVENT)
		i2c->at_slave_event = true;
	if (int_status & INT_TRANSACTION_DONE)
		i2c->at_t_done = true;

	if (!i2c->at_slave_event || !i2c->at_t_done)
		return 0;

	/* wait if no continue bits are set */
	if (i2c->at_cur_cmd >= 0 &&
	    i2c->at_cur_cmd < ARRAY_SIZE(continue_bits)) {
		unsigned int cont_bits = continue_bits[i2c->at_cur_cmd];

		if (cont_bits) {
			cont_bits |= LINESTAT_ABORT_DET;
			if (!(i2c->line_status & cont_bits))
				return 0;
		}
	}

	/* follow the sequence of commands in i2c->seq */
	next_cmd = *i2c->seq;
	/* stop on a nil */
	if (!next_cmd) {
		img_i2c_writel(i2c, SCB_OVERRIDE_REG, 0);
		return ISR_COMPLETE(0);
	}
	/* when generating data, the next byte is the data */
	if (next_cmd == CMD_GEN_DATA) {
		++i2c->seq;
		next_data = *i2c->seq;
	}
	++i2c->seq;
	img_i2c_atomic_op(i2c, next_cmd, next_data);

	return 0;
}

static void img_i2c_reset_start(struct img_i2c *i2c)
{
	/* Initiate the magic dance */
	img_i2c_switch_mode(i2c, MODE_SEQUENCE);
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);
	i2c->seq = img_i2c_reset_seq;
	i2c->at_slave_event = true;
	i2c->at_t_done = true;
	i2c->at_cur_cmd = -1;

	/* img_i2c_reset_seq isn't empty so the following won't fail */
	img_i2c_sequence(i2c, 0);
}

static void img_i2c_stop_start(struct img_i2c *i2c)
{
	/* Initiate a stop bit sequence */
	img_i2c_switch_mode(i2c, MODE_SEQUENCE);
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);
	i2c->seq = img_i2c_stop_seq;
	i2c->at_slave_event = true;
	i2c->at_t_done = true;
	i2c->at_cur_cmd = -1;

	/* img_i2c_stop_seq isn't empty so the following won't fail */
	img_i2c_sequence(i2c, 0);
}

static unsigned int img_i2c_atomic(struct img_i2c *i2c,
				   u32 int_status,
				   u32 line_status)
{
	int next_cmd = -1;
	u8 next_data = 0x00;

	if (int_status & INT_SLAVE_EVENT)
		i2c->at_slave_event = true;
	if (int_status & INT_TRANSACTION_DONE)
		i2c->at_t_done = true;

	if (!i2c->at_slave_event || !i2c->at_t_done)
		goto next_atomic_cmd;
	if (i2c->line_status & LINESTAT_ABORT_DET) {
		dev_dbg(i2c->adap.dev.parent, "abort condition detected\n");
		next_cmd = CMD_GEN_STOP;
		i2c->msg_status = -EIO;
		goto next_atomic_cmd;
	}

	/* i2c->at_cur_cmd may have completed */
	switch (i2c->at_cur_cmd) {
	case CMD_GEN_START:
		next_cmd = CMD_GEN_DATA;
		next_data = i2c_8bit_addr_from_msg(&i2c->msg);
		break;
	case CMD_GEN_DATA:
		if (i2c->line_status & LINESTAT_INPUT_HELD_V)
			next_cmd = CMD_RET_ACK;
		break;
	case CMD_RET_ACK:
		if (i2c->line_status & LINESTAT_ACK_DET ||
		    (i2c->line_status & LINESTAT_NACK_DET &&
		    i2c->msg.flags & I2C_M_IGNORE_NAK)) {
			if (i2c->msg.len == 0) {
				next_cmd = CMD_GEN_STOP;
			} else if (i2c->msg.flags & I2C_M_RD) {
				next_cmd = CMD_RET_DATA;
			} else {
				next_cmd = CMD_GEN_DATA;
				next_data = *i2c->msg.buf;
				--i2c->msg.len;
				++i2c->msg.buf;
			}
		} else if (i2c->line_status & LINESTAT_NACK_DET) {
			i2c->msg_status = -EIO;
			next_cmd = CMD_GEN_STOP;
		}
		break;
	case CMD_RET_DATA:
		if (i2c->line_status & LINESTAT_INPUT_HELD_V) {
			*i2c->msg.buf = (i2c->line_status &
						LINESTAT_INPUT_DATA)
					>> LINESTAT_INPUT_DATA_SHIFT;
			--i2c->msg.len;
			++i2c->msg.buf;
			if (i2c->msg.len)
				next_cmd = CMD_GEN_ACK;
			else
				next_cmd = CMD_GEN_NACK;
		}
		break;
	case CMD_GEN_ACK:
		if (i2c->line_status & LINESTAT_ACK_DET) {
			next_cmd = CMD_RET_DATA;
		} else {
			i2c->msg_status = -EIO;
			next_cmd = CMD_GEN_STOP;
		}
		break;
	case CMD_GEN_NACK:
		next_cmd = CMD_GEN_STOP;
		break;
	case CMD_GEN_STOP:
		img_i2c_writel(i2c, SCB_OVERRIDE_REG, 0);
		return ISR_COMPLETE(0);
	default:
		dev_err(i2c->adap.dev.parent, "bad atomic command %d\n",
			i2c->at_cur_cmd);
		i2c->msg_status = -EIO;
		next_cmd = CMD_GEN_STOP;
		break;
	}

next_atomic_cmd:
	if (next_cmd != -1) {
		/* don't actually stop unless we're the last transaction */
		if (next_cmd == CMD_GEN_STOP && !i2c->msg_status &&
						!i2c->last_msg)
			return ISR_COMPLETE(0);
		img_i2c_atomic_op(i2c, next_cmd, next_data);
	}
	return 0;
}

/*
 * Timer function to check if something has gone wrong in automatic mode (so we
 * don't have to handle so many interrupts just to catch an exception).
 */
static void img_i2c_check_timer(unsigned long arg)
{
	struct img_i2c *i2c = (struct img_i2c *)arg;
	unsigned long flags;
	unsigned int line_status;

	spin_lock_irqsave(&i2c->lock, flags);
	line_status = img_i2c_readl(i2c, SCB_STATUS_REG);

	/* check for an abort condition */
	if (line_status & LINESTAT_ABORT_DET) {
		dev_dbg(i2c->adap.dev.parent,
			"abort condition detected by check timer\n");
		/* enable slave event interrupt mask to trigger irq */
		img_i2c_writel(i2c, SCB_INT_MASK_REG,
			       i2c->int_enable | INT_SLAVE_EVENT);
	}

	spin_unlock_irqrestore(&i2c->lock, flags);
}

static unsigned int img_i2c_auto(struct img_i2c *i2c,
				 unsigned int int_status,
				 unsigned int line_status)
{
	if (int_status & (INT_WRITE_ACK_ERR | INT_ADDR_ACK_ERR))
		return ISR_COMPLETE(EIO);

	if (line_status & LINESTAT_ABORT_DET) {
		dev_dbg(i2c->adap.dev.parent, "abort condition detected\n");
		/* empty the read fifo */
		if ((i2c->msg.flags & I2C_M_RD) &&
		    (int_status & INT_FIFO_FULL_FILLING))
			img_i2c_read_fifo(i2c);
		/* use atomic mode and try to force a stop bit */
		i2c->msg_status = -EIO;
		img_i2c_stop_start(i2c);
		return 0;
	}

	/* Enable transaction halt on start bit */
	if (!i2c->last_msg && line_status & LINESTAT_START_BIT_DET) {
		img_i2c_transaction_halt(i2c, !i2c->last_msg);
		/* we're no longer interested in the slave event */
		i2c->int_enable &= ~INT_SLAVE_EVENT;
	}

	mod_timer(&i2c->check_timer, jiffies + msecs_to_jiffies(1));

	if (int_status & INT_STOP_DETECTED) {
		/* Drain remaining data in FIFO and complete transaction */
		if (i2c->msg.flags & I2C_M_RD)
			img_i2c_read_fifo(i2c);
		return ISR_COMPLETE(0);
	}

	if (i2c->msg.flags & I2C_M_RD) {
		if (int_status & (INT_FIFO_FULL_FILLING | INT_MASTER_HALTED)) {
			img_i2c_read_fifo(i2c);
			if (i2c->msg.len == 0)
				return ISR_WAITSTOP;
		}
	} else {
		if (int_status & (INT_FIFO_EMPTY | INT_MASTER_HALTED)) {
			if ((int_status & INT_FIFO_EMPTY) &&
			    i2c->msg.len == 0)
				return ISR_WAITSTOP;
			img_i2c_write_fifo(i2c);
		}
	}
	if (int_status & INT_MASTER_HALTED) {
		/*
		 * Release and then enable transaction halt, to
		 * allow only a single byte to proceed.
		 */
		img_i2c_transaction_halt(i2c, false);
		img_i2c_transaction_halt(i2c, !i2c->last_msg);
	}

	return 0;
}

static irqreturn_t img_i2c_isr(int irq, void *dev_id)
{
	struct img_i2c *i2c = (struct img_i2c *)dev_id;
	u32 int_status, line_status;
	/* We handle transaction completion AFTER accessing registers */
	unsigned int hret;

	/* Read interrupt status register. */
	int_status = img_i2c_readl(i2c, SCB_INT_STATUS_REG);
	/* Clear detected interrupts. */
	img_i2c_writel(i2c, SCB_INT_CLEAR_REG, int_status);

	/*
	 * Read line status and clear it until it actually is clear.  We have
	 * to be careful not to lose any line status bits that get latched.
	 */
	line_status = img_i2c_readl(i2c, SCB_STATUS_REG);
	if (line_status & LINESTAT_LATCHED) {
		img_i2c_writel(i2c, SCB_CLEAR_REG,
			      (line_status & LINESTAT_LATCHED)
				>> LINESTAT_CLEAR_SHIFT);
		img_i2c_wr_rd_fence(i2c);
	}

	spin_lock(&i2c->lock);

	/* Keep track of line status bits received */
	i2c->line_status &= ~LINESTAT_INPUT_DATA;
	i2c->line_status |= line_status;

	/*
	 * Certain interrupts indicate that sclk low timeout is not
	 * a problem. If any of these are set, just continue.
	 */
	if ((int_status & INT_SCLK_LOW_TIMEOUT) &&
	    !(int_status & (INT_SLAVE_EVENT |
			    INT_FIFO_EMPTY |
			    INT_FIFO_FULL))) {
		dev_crit(i2c->adap.dev.parent,
			 "fatal: clock low timeout occurred %s addr 0x%02x\n",
			 (i2c->msg.flags & I2C_M_RD) ? "reading" : "writing",
			 i2c->msg.addr);
		hret = ISR_FATAL(EIO);
		goto out;
	}

	if (i2c->mode == MODE_ATOMIC)
		hret = img_i2c_atomic(i2c, int_status, line_status);
	else if (i2c->mode == MODE_AUTOMATIC)
		hret = img_i2c_auto(i2c, int_status, line_status);
	else if (i2c->mode == MODE_SEQUENCE)
		hret = img_i2c_sequence(i2c, int_status);
	else if (i2c->mode == MODE_WAITSTOP && (int_status & INT_SLAVE_EVENT) &&
			 (line_status & LINESTAT_STOP_BIT_DET))
		hret = ISR_COMPLETE(0);
	else if (i2c->mode == MODE_RAW)
		hret = img_i2c_raw(i2c, int_status, line_status);
	else
		hret = 0;

	/* Clear detected level interrupts. */
	img_i2c_writel(i2c, SCB_INT_CLEAR_REG, int_status & INT_LEVEL);

out:
	if (hret & ISR_WAITSTOP) {
		/*
		 * Only wait for stop on last message.
		 * Also we may already have detected the stop bit.
		 */
		if (!i2c->last_msg || i2c->line_status & LINESTAT_STOP_BIT_DET)
			hret = ISR_COMPLETE(0);
		else
			img_i2c_switch_mode(i2c, MODE_WAITSTOP);
	}

	/* now we've finished using regs, handle transaction completion */
	if (hret & ISR_COMPLETE_M) {
		int status = -(hret & ISR_STATUS_M);

		img_i2c_complete_transaction(i2c, status);
		if (hret & ISR_FATAL_M)
			img_i2c_switch_mode(i2c, MODE_FATAL);
	}

	/* Enable interrupts (int_enable may be altered by changing mode) */
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);

	spin_unlock(&i2c->lock);

	return IRQ_HANDLED;
}

/* Force a bus reset sequence and wait for it to complete */
static int img_i2c_reset_bus(struct img_i2c *i2c)
{
	unsigned long flags;
	unsigned long time_left;

	spin_lock_irqsave(&i2c->lock, flags);
	reinit_completion(&i2c->msg_complete);
	img_i2c_reset_start(i2c);
	spin_unlock_irqrestore(&i2c->lock, flags);

	time_left = wait_for_completion_timeout(&i2c->msg_complete,
					      IMG_I2C_TIMEOUT);
	if (time_left == 0)
		return -ETIMEDOUT;
	return 0;
}

static int img_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			int num)
{
	struct img_i2c *i2c = i2c_get_adapdata(adap);
	bool atomic = false;
	int i, ret;
	unsigned long time_left;

	if (i2c->mode == MODE_SUSPEND) {
		WARN(1, "refusing to service transaction in suspended state\n");
		return -EIO;
	}

	if (i2c->mode == MODE_FATAL)
		return -EIO;

	for (i = 0; i < num; i++) {
		/*
		 * 0 byte reads are not possible because the slave could try
		 * and pull the data line low, preventing a stop bit.
		 */
		if (!msgs[i].len && msgs[i].flags & I2C_M_RD)
			return -EIO;
		/*
		 * 0 byte writes are possible and used for probing, but we
		 * cannot do them in automatic mode, so use atomic mode
		 * instead.
		 *
		 * Also, the I2C_M_IGNORE_NAK mode can only be implemented
		 * in atomic mode.
		 */
		if (!msgs[i].len ||
		    (msgs[i].flags & I2C_M_IGNORE_NAK))
			atomic = true;
	}

	ret = clk_prepare_enable(i2c->scb_clk);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		struct i2c_msg *msg = &msgs[i];
		unsigned long flags;

		spin_lock_irqsave(&i2c->lock, flags);

		/*
		 * Make a copy of the message struct. We mustn't modify the
		 * original or we'll confuse drivers and i2c-dev.
		 */
		i2c->msg = *msg;
		i2c->msg_status = 0;

		/*
		 * After the last message we must have waited for a stop bit.
		 * Not waiting can cause problems when the clock is disabled
		 * before the stop bit is sent, and the linux I2C interface
		 * requires separate transfers not to joined with repeated
		 * start.
		 */
		i2c->last_msg = (i == num - 1);
		reinit_completion(&i2c->msg_complete);

		/*
		 * Clear line status and all interrupts before starting a
		 * transfer, as we may have unserviced interrupts from
		 * previous transfers that might be handled in the context
		 * of the new transfer.
		 */
		img_i2c_writel(i2c, SCB_INT_CLEAR_REG, ~0);
		img_i2c_writel(i2c, SCB_CLEAR_REG, ~0);

		if (atomic) {
			img_i2c_atomic_start(i2c);
		} else {
			/*
			 * Enable transaction halt if not the last message in
			 * the queue so that we can control repeated starts.
			 */
			img_i2c_transaction_halt(i2c, !i2c->last_msg);

			if (msg->flags & I2C_M_RD)
				img_i2c_read(i2c);
			else
				img_i2c_write(i2c);

			/*
			 * Release and then enable transaction halt, to
			 * allow only a single byte to proceed.
			 * This doesn't have an effect on the initial transfer
			 * but will allow the following transfers to start
			 * processing if the previous transfer was marked as
			 * complete while the i2c block was halted.
			 */
			img_i2c_transaction_halt(i2c, false);
			img_i2c_transaction_halt(i2c, !i2c->last_msg);
		}
		spin_unlock_irqrestore(&i2c->lock, flags);

		time_left = wait_for_completion_timeout(&i2c->msg_complete,
						      IMG_I2C_TIMEOUT);
		del_timer_sync(&i2c->check_timer);

		if (time_left == 0) {
			dev_err(adap->dev.parent, "i2c transfer timed out\n");
			i2c->msg_status = -ETIMEDOUT;
			break;
		}

		if (i2c->msg_status)
			break;
	}

	clk_disable_unprepare(i2c->scb_clk);

	return i2c->msg_status ? i2c->msg_status : num;
}

static u32 img_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm img_i2c_algo = {
	.master_xfer = img_i2c_xfer,
	.functionality = img_i2c_func,
};

static int img_i2c_init(struct img_i2c *i2c)
{
	unsigned int clk_khz, bitrate_khz, clk_period, tckh, tckl, tsdh;
	unsigned int i, ret, data, prescale, inc, int_bitrate, filt;
	struct img_i2c_timings timing;
	u32 rev;

	ret = clk_prepare_enable(i2c->scb_clk);
	if (ret)
		return ret;

	rev = img_i2c_readl(i2c, SCB_CORE_REV_REG);
	if ((rev & 0x00ffffff) < 0x00020200) {
		dev_info(i2c->adap.dev.parent,
			 "Unknown hardware revision (%d.%d.%d.%d)\n",
			 (rev >> 24) & 0xff, (rev >> 16) & 0xff,
			 (rev >> 8) & 0xff, rev & 0xff);
		clk_disable_unprepare(i2c->scb_clk);
		return -EINVAL;
	}

	/* Fencing enabled by default. */
	i2c->need_wr_rd_fence = true;

	/* Determine what mode we're in from the bitrate */
	timing = timings[0];
	for (i = 0; i < ARRAY_SIZE(timings); i++) {
		if (i2c->bitrate <= timings[i].max_bitrate) {
			timing = timings[i];
			break;
		}
	}
	if (i2c->bitrate > timings[ARRAY_SIZE(timings) - 1].max_bitrate) {
		dev_warn(i2c->adap.dev.parent,
			 "requested bitrate (%u) is higher than the max bitrate supported (%u)\n",
			 i2c->bitrate,
			 timings[ARRAY_SIZE(timings) - 1].max_bitrate);
		timing = timings[ARRAY_SIZE(timings) - 1];
		i2c->bitrate = timing.max_bitrate;
	}

	bitrate_khz = i2c->bitrate / 1000;
	clk_khz = clk_get_rate(i2c->scb_clk) / 1000;

	/* Find the prescale that would give us that inc (approx delay = 0) */
	prescale = SCB_OPT_INC * clk_khz / (256 * 16 * bitrate_khz);
	prescale = clamp_t(unsigned int, prescale, 1, 8);
	clk_khz /= prescale;

	/* Setup the clock increment value */
	inc = (256 * 16 * bitrate_khz) / clk_khz;

	/*
	 * The clock generation logic allows to filter glitches on the bus.
	 * This filter is able to remove bus glitches shorter than 50ns.
	 * If the clock enable rate is greater than 20 MHz, no filtering
	 * is required, so we need to disable it.
	 * If it's between the 20-40 MHz range, there's no need to divide
	 * the clock to get a filter.
	 */
	if (clk_khz < 20000) {
		filt = SCB_FILT_DISABLE;
	} else if (clk_khz < 40000) {
		filt = SCB_FILT_BYPASS;
	} else {
		/* Calculate filter clock */
		filt = (64000 / ((clk_khz / 1000) * SCB_FILT_GLITCH));

		/* Scale up if needed */
		if (64000 % ((clk_khz / 1000) * SCB_FILT_GLITCH))
			inc++;

		if (filt > SCB_FILT_INC_MASK)
			filt = SCB_FILT_INC_MASK;

		filt = (filt & SCB_FILT_INC_MASK) << SCB_FILT_INC_SHIFT;
	}
	data = filt | ((inc & SCB_INC_MASK) << SCB_INC_SHIFT) | (prescale - 1);
	img_i2c_writel(i2c, SCB_CLK_SET_REG, data);

	/* Obtain the clock period of the fx16 clock in ns */
	clk_period = (256 * 1000000) / (clk_khz * inc);

	/* Calculate the bitrate in terms of internal clock pulses */
	int_bitrate = 1000000 / (bitrate_khz * clk_period);
	if ((1000000 % (bitrate_khz * clk_period)) >=
	    ((bitrate_khz * clk_period) / 2))
		int_bitrate++;

	/*
	 * Setup clock duty cycle, start with 50% and adjust TCKH and TCKL
	 * values from there if they don't meet minimum timing requirements
	 */
	tckh = int_bitrate / 2;
	tckl = int_bitrate - tckh;

	/* Adjust TCKH and TCKL values */
	data = DIV_ROUND_UP(timing.tckl, clk_period);

	if (tckl < data) {
		tckl = data;
		tckh = int_bitrate - tckl;
	}

	if (tckh > 0)
		--tckh;

	if (tckl > 0)
		--tckl;

	img_i2c_writel(i2c, SCB_TIME_TCKH_REG, tckh);
	img_i2c_writel(i2c, SCB_TIME_TCKL_REG, tckl);

	/* Setup TSDH value */
	tsdh = DIV_ROUND_UP(timing.tsdh, clk_period);

	if (tsdh > 1)
		data = tsdh - 1;
	else
		data = 0x01;
	img_i2c_writel(i2c, SCB_TIME_TSDH_REG, data);

	/* This value is used later */
	tsdh = data;

	/* Setup TPL value */
	data = timing.tpl / clk_period;
	if (data > 0)
		--data;
	img_i2c_writel(i2c, SCB_TIME_TPL_REG, data);

	/* Setup TPH value */
	data = timing.tph / clk_period;
	if (data > 0)
		--data;
	img_i2c_writel(i2c, SCB_TIME_TPH_REG, data);

	/* Setup TSDL value to TPL + TSDH + 2 */
	img_i2c_writel(i2c, SCB_TIME_TSDL_REG, data + tsdh + 2);

	/* Setup TP2S value */
	data = timing.tp2s / clk_period;
	if (data > 0)
		--data;
	img_i2c_writel(i2c, SCB_TIME_TP2S_REG, data);

	img_i2c_writel(i2c, SCB_TIME_TBI_REG, TIMEOUT_TBI);
	img_i2c_writel(i2c, SCB_TIME_TSL_REG, TIMEOUT_TSL);
	img_i2c_writel(i2c, SCB_TIME_TDL_REG, TIMEOUT_TDL);

	/* Take module out of soft reset and enable clocks */
	img_i2c_soft_reset(i2c);

	/* Disable all interrupts */
	img_i2c_writel(i2c, SCB_INT_MASK_REG, 0);

	/* Clear all interrupts */
	img_i2c_writel(i2c, SCB_INT_CLEAR_REG, ~0);

	/* Clear the scb_line_status events */
	img_i2c_writel(i2c, SCB_CLEAR_REG, ~0);

	/* Enable interrupts */
	img_i2c_writel(i2c, SCB_INT_MASK_REG, i2c->int_enable);

	/* Perform a synchronous sequence to reset the bus */
	ret = img_i2c_reset_bus(i2c);

	clk_disable_unprepare(i2c->scb_clk);

	return ret;
}

static int img_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct img_i2c *i2c;
	struct resource *res;
	int irq, ret;
	u32 val;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct img_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "can't get irq number\n");
		return irq;
	}

	i2c->sys_clk = devm_clk_get(&pdev->dev, "sys");
	if (IS_ERR(i2c->sys_clk)) {
		dev_err(&pdev->dev, "can't get system clock\n");
		return PTR_ERR(i2c->sys_clk);
	}

	i2c->scb_clk = devm_clk_get(&pdev->dev, "scb");
	if (IS_ERR(i2c->scb_clk)) {
		dev_err(&pdev->dev, "can't get core clock\n");
		return PTR_ERR(i2c->scb_clk);
	}

	ret = devm_request_irq(&pdev->dev, irq, img_i2c_isr, 0,
			       pdev->name, i2c);
	if (ret) {
		dev_err(&pdev->dev, "can't request irq %d\n", irq);
		return ret;
	}

	/* Set up the exception check timer */
	setup_timer(&i2c->check_timer, img_i2c_check_timer,
		    (unsigned long)i2c);

	i2c->bitrate = timings[0].max_bitrate;
	if (!of_property_read_u32(node, "clock-frequency", &val))
		i2c->bitrate = val;

	i2c_set_adapdata(&i2c->adap, i2c);
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = node;
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &img_i2c_algo;
	i2c->adap.retries = 5;
	i2c->adap.nr = pdev->id;
	snprintf(i2c->adap.name, sizeof(i2c->adap.name), "IMG SCB I2C");

	img_i2c_switch_mode(i2c, MODE_INACTIVE);
	spin_lock_init(&i2c->lock);
	init_completion(&i2c->msg_complete);

	platform_set_drvdata(pdev, i2c);

	ret = clk_prepare_enable(i2c->sys_clk);
	if (ret)
		return ret;

	ret = img_i2c_init(i2c);
	if (ret)
		goto disable_clk;

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0)
		goto disable_clk;

	return 0;

disable_clk:
	clk_disable_unprepare(i2c->sys_clk);
	return ret;
}

static int img_i2c_remove(struct platform_device *dev)
{
	struct img_i2c *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);
	clk_disable_unprepare(i2c->sys_clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int img_i2c_suspend(struct device *dev)
{
	struct img_i2c *i2c = dev_get_drvdata(dev);

	img_i2c_switch_mode(i2c, MODE_SUSPEND);

	clk_disable_unprepare(i2c->sys_clk);

	return 0;
}

static int img_i2c_resume(struct device *dev)
{
	struct img_i2c *i2c = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2c->sys_clk);
	if (ret)
		return ret;

	img_i2c_init(i2c);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(img_i2c_pm, img_i2c_suspend, img_i2c_resume);

static const struct of_device_id img_scb_i2c_match[] = {
	{ .compatible = "img,scb-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(of, img_scb_i2c_match);

static struct platform_driver img_scb_i2c_driver = {
	.driver = {
		.name		= "img-i2c-scb",
		.of_match_table	= img_scb_i2c_match,
		.pm		= &img_i2c_pm,
	},
	.probe = img_i2c_probe,
	.remove = img_i2c_remove,
};
module_platform_driver(img_scb_i2c_driver);

MODULE_AUTHOR("James Hogan <jhogan@kernel.org>");
MODULE_DESCRIPTION("IMG host I2C driver");
MODULE_LICENSE("GPL v2");
