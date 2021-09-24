// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for I2C adapter in Rockchip RK3xxx SoC
 *
 * Max Schwarz <max.schwarz@online.de>
 * based on the patches by Rockchip Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/math64.h>
#include <linux/reboot.h>
#include <linux/delay.h>


/* Register Map */
#define REG_CON        0x00 /* control register */
#define REG_CLKDIV     0x04 /* clock divisor register */
#define REG_MRXADDR    0x08 /* slave address for REGISTER_TX */
#define REG_MRXRADDR   0x0c /* slave register address for REGISTER_TX */
#define REG_MTXCNT     0x10 /* number of bytes to be transmitted */
#define REG_MRXCNT     0x14 /* number of bytes to be received */
#define REG_IEN        0x18 /* interrupt enable */
#define REG_IPD        0x1c /* interrupt pending */
#define REG_FCNT       0x20 /* finished count */
#define REG_CON1       0x228 /* control register1 */

/* Data buffer offsets */
#define TXBUFFER_BASE 0x100
#define RXBUFFER_BASE 0x200

/* REG_CON bits */
#define REG_CON_EN        BIT(0)
enum {
	REG_CON_MOD_TX = 0,      /* transmit data */
	REG_CON_MOD_REGISTER_TX, /* select register and restart */
	REG_CON_MOD_RX,          /* receive data */
	REG_CON_MOD_REGISTER_RX, /* broken: transmits read addr AND writes
				  * register addr */
};
#define REG_CON_MOD(mod)  ((mod) << 1)
#define REG_CON_MOD_MASK  (BIT(1) | BIT(2))
#define REG_CON_START     BIT(3)
#define REG_CON_STOP      BIT(4)
#define REG_CON_LASTACK   BIT(5) /* 1: send NACK after last received byte */
#define REG_CON_ACTACK    BIT(6) /* 1: stop if NACK is received */

#define REG_CON_TUNING_MASK GENMASK_ULL(15, 8)

#define REG_CON_SDA_CFG(cfg) ((cfg) << 8)
#define REG_CON_STA_CFG(cfg) ((cfg) << 12)
#define REG_CON_STO_CFG(cfg) ((cfg) << 14)

enum {
	RK_I2C_VERSION0 = 0,
	RK_I2C_VERSION1,
	RK_I2C_VERSION5 = 5,
};

#define REG_CON_VERSION GENMASK_ULL(24, 16)
#define REG_CON_VERSION_SHIFT 16

/* REG_MRXADDR bits */
#define REG_MRXADDR_VALID(x) BIT(24 + (x)) /* [x*8+7:x*8] of MRX[R]ADDR valid */

/* REG_IEN/REG_IPD bits */
#define REG_INT_BTF       BIT(0) /* a byte was transmitted */
#define REG_INT_BRF       BIT(1) /* a byte was received */
#define REG_INT_MBTF      BIT(2) /* master data transmit finished */
#define REG_INT_MBRF      BIT(3) /* master data receive finished */
#define REG_INT_START     BIT(4) /* START condition generated */
#define REG_INT_STOP      BIT(5) /* STOP condition generated */
#define REG_INT_NAKRCV    BIT(6) /* NACK received */
#define REG_INT_ALL       0xff

/* Disable i2c all irqs */
#define IEN_ALL_DISABLE   0

#define REG_CON1_AUTO_STOP BIT(0)
#define REG_CON1_TRANSFER_AUTO_STOP BIT(1)
#define REG_CON1_NACK_AUTO_STOP BIT(2)

/* Constants */
#define WAIT_TIMEOUT      1000 /* ms */
#define DEFAULT_SCL_RATE  (100 * 1000) /* Hz */

/**
 * struct i2c_spec_values:
 * @min_hold_start_ns: min hold time (repeated) START condition
 * @min_low_ns: min LOW period of the SCL clock
 * @min_high_ns: min HIGH period of the SCL cloc
 * @min_setup_start_ns: min set-up time for a repeated START conditio
 * @max_data_hold_ns: max data hold time
 * @min_data_setup_ns: min data set-up time
 * @min_setup_stop_ns: min set-up time for STOP condition
 * @min_hold_buffer_ns: min bus free time between a STOP and
 * START condition
 */
struct i2c_spec_values {
	unsigned long min_hold_start_ns;
	unsigned long min_low_ns;
	unsigned long min_high_ns;
	unsigned long min_setup_start_ns;
	unsigned long max_data_hold_ns;
	unsigned long min_data_setup_ns;
	unsigned long min_setup_stop_ns;
	unsigned long min_hold_buffer_ns;
};

static const struct i2c_spec_values standard_mode_spec = {
	.min_hold_start_ns = 4000,
	.min_low_ns = 4700,
	.min_high_ns = 4000,
	.min_setup_start_ns = 4700,
	.max_data_hold_ns = 3450,
	.min_data_setup_ns = 250,
	.min_setup_stop_ns = 4000,
	.min_hold_buffer_ns = 4700,
};

static const struct i2c_spec_values fast_mode_spec = {
	.min_hold_start_ns = 600,
	.min_low_ns = 1300,
	.min_high_ns = 600,
	.min_setup_start_ns = 600,
	.max_data_hold_ns = 900,
	.min_data_setup_ns = 100,
	.min_setup_stop_ns = 600,
	.min_hold_buffer_ns = 1300,
};

static const struct i2c_spec_values fast_mode_plus_spec = {
	.min_hold_start_ns = 260,
	.min_low_ns = 500,
	.min_high_ns = 260,
	.min_setup_start_ns = 260,
	.max_data_hold_ns = 400,
	.min_data_setup_ns = 50,
	.min_setup_stop_ns = 260,
	.min_hold_buffer_ns = 500,
};

/**
 * struct rk3x_i2c_calced_timings:
 * @div_low: Divider output for low
 * @div_high: Divider output for high
 * @tuning: Used to adjust setup/hold data time,
 * setup/hold start time and setup stop time for
 * v1's calc_timings, the tuning should all be 0
 * for old hardware anyone using v0's calc_timings.
 */
struct rk3x_i2c_calced_timings {
	unsigned long div_low;
	unsigned long div_high;
	unsigned int tuning;
};

enum rk3x_i2c_state {
	STATE_IDLE,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

/**
 * struct rk3x_i2c_soc_data:
 * @grf_offset: offset inside the grf regmap for setting the i2c type
 * @calc_timings: Callback function for i2c timing information calculated
 */
struct rk3x_i2c_soc_data {
	int grf_offset;
	int (*calc_timings)(unsigned long, struct i2c_timings *,
			    struct rk3x_i2c_calced_timings *);
};

/**
 * struct rk3x_i2c - private data of the controller
 * @adap: corresponding I2C adapter
 * @dev: device for this controller
 * @soc_data: related soc data struct
 * @regs: virtual memory area
 * @clk: function clk for rk3399 or function & Bus clks for others
 * @pclk: Bus clk for rk3399
 * @clk_rate_nb: i2c clk rate change notify
 * @t: I2C known timing information
 * @lock: spinlock for the i2c bus
 * @wait: the waitqueue to wait for i2c transfer
 * @busy: the condition for the event to wait for
 * @msg: current i2c message
 * @addr: addr of i2c slave device
 * @mode: mode of i2c transfer
 * @is_last_msg: flag determines whether it is the last msg in this transfer
 * @state: state of i2c transfer
 * @processed: byte length which has been send or received
 * @error: error code for i2c transfer
 * @i2c_restart_nb: make sure the i2c transfer to be finished
 * @system_restarting: true if system is restarting
 */
struct rk3x_i2c {
	struct i2c_adapter adap;
	struct device *dev;
	const struct rk3x_i2c_soc_data *soc_data;

	/* Hardware resources */
	void __iomem *regs;
	struct clk *clk;
	struct clk *pclk;
	struct notifier_block clk_rate_nb;
	bool autostop_supported;

	/* Settings */
	struct i2c_timings t;

	/* Synchronization & notification */
	spinlock_t lock;
	wait_queue_head_t wait;
	bool busy;

	/* Current message */
	struct i2c_msg *msg;
	u8 addr;
	unsigned int mode;
	bool is_last_msg;

	/* I2C state machine */
	enum rk3x_i2c_state state;
	unsigned int processed;
	int error;
	unsigned int suspended:1;

	struct notifier_block i2c_restart_nb;
	bool system_restarting;
};

static void rk3x_i2c_prepare_read(struct rk3x_i2c *i2c);
static int rk3x_i2c_fill_transmit_buf(struct rk3x_i2c *i2c, bool sended);

static inline void rk3x_i2c_wake_up(struct rk3x_i2c *i2c)
{
	if (!i2c->system_restarting)
		wake_up(&i2c->wait);
}

static inline void i2c_writel(struct rk3x_i2c *i2c, u32 value,
			      unsigned int offset)
{
	writel(value, i2c->regs + offset);
}

static inline u32 i2c_readl(struct rk3x_i2c *i2c, unsigned int offset)
{
	return readl(i2c->regs + offset);
}

/* Reset all interrupt pending bits */
static inline void rk3x_i2c_clean_ipd(struct rk3x_i2c *i2c)
{
	i2c_writel(i2c, REG_INT_ALL, REG_IPD);
}

static inline void rk3x_i2c_disable_irq(struct rk3x_i2c *i2c)
{
	i2c_writel(i2c, IEN_ALL_DISABLE, REG_IEN);
}

static inline void rk3x_i2c_disable(struct rk3x_i2c *i2c)
{
	u32 val = i2c_readl(i2c, REG_CON) & REG_CON_TUNING_MASK;

	i2c_writel(i2c, val, REG_CON);
}

static bool rk3x_i2c_auto_stop(struct rk3x_i2c *i2c)
{
	unsigned int len, con1 = 0;

	if (!i2c->autostop_supported)
		return false;

	if (!(i2c->msg->flags & I2C_M_IGNORE_NAK))
		con1 = REG_CON1_NACK_AUTO_STOP | REG_CON1_AUTO_STOP;

	if (!i2c->is_last_msg)
		goto out;

	len = i2c->msg->len - i2c->processed;

	if (len > 32)
		goto out;

	i2c->state = STATE_STOP;

	con1 |= REG_CON1_TRANSFER_AUTO_STOP | REG_CON1_AUTO_STOP;
	i2c_writel(i2c, con1, REG_CON1);
	if (con1 & REG_CON1_NACK_AUTO_STOP)
		i2c_writel(i2c, REG_INT_STOP, REG_IEN);
	else
		i2c_writel(i2c, REG_INT_STOP | REG_INT_NAKRCV, REG_IEN);

	return true;

out:
	i2c_writel(i2c, con1, REG_CON1);
	return false;
}

/**
 * Generate a START condition, which triggers a REG_INT_START interrupt.
 */
static void rk3x_i2c_start(struct rk3x_i2c *i2c)
{
	u32 val = i2c_readl(i2c, REG_CON) & REG_CON_TUNING_MASK;
	bool auto_stop = rk3x_i2c_auto_stop(i2c);
	int length = 0;

	/* enable appropriate interrupts */
	if (i2c->mode == REG_CON_MOD_TX) {
		if (!auto_stop) {
			i2c_writel(i2c, REG_INT_MBTF | REG_INT_NAKRCV, REG_IEN);
			i2c->state = STATE_WRITE;
		}
		length = rk3x_i2c_fill_transmit_buf(i2c, false);
	} else {
		/* in any other case, we are going to be reading. */
		if (!auto_stop) {
			i2c_writel(i2c, REG_INT_MBRF | REG_INT_NAKRCV, REG_IEN);
			i2c->state = STATE_READ;
		}
	}

	/* enable adapter with correct mode, send START condition */
	val |= REG_CON_EN | REG_CON_MOD(i2c->mode) | REG_CON_START;

	/* if we want to react to NACK, set ACTACK bit */
	if (!(i2c->msg->flags & I2C_M_IGNORE_NAK))
		val |= REG_CON_ACTACK;

	i2c_writel(i2c, val, REG_CON);

	/* enable transition */
	if (i2c->mode == REG_CON_MOD_TX)
		i2c_writel(i2c, length, REG_MTXCNT);
	else
		rk3x_i2c_prepare_read(i2c);
}

/**
 * Generate a STOP condition, which triggers a REG_INT_STOP interrupt.
 *
 * @error: Error code to return in rk3x_i2c_xfer
 */
static void rk3x_i2c_stop(struct rk3x_i2c *i2c, int error)
{
	unsigned int ctrl;

	i2c->processed = 0;
	i2c->msg = NULL;
	i2c->error = error;

	if (i2c->is_last_msg) {
		/* Enable stop interrupt */
		i2c_writel(i2c, REG_INT_STOP, REG_IEN);

		i2c->state = STATE_STOP;

		ctrl = i2c_readl(i2c, REG_CON);
		ctrl |= REG_CON_STOP;
		ctrl &= ~REG_CON_START;
		i2c_writel(i2c, ctrl, REG_CON);
	} else {
		/* Signal rk3x_i2c_xfer to start the next message. */
		i2c->busy = false;
		i2c->state = STATE_IDLE;

		/*
		 * The HW is actually not capable of REPEATED START. But we can
		 * get the intended effect by resetting its internal state
		 * and issuing an ordinary START.
		 */
		ctrl = i2c_readl(i2c, REG_CON) & REG_CON_TUNING_MASK;
		i2c_writel(i2c, ctrl, REG_CON);

		/* signal that we are finished with the current msg */
		rk3x_i2c_wake_up(i2c);
	}
}

/**
 * Setup a read according to i2c->msg
 */
static void rk3x_i2c_prepare_read(struct rk3x_i2c *i2c)
{
	unsigned int len = i2c->msg->len - i2c->processed;
	u32 con;

	con = i2c_readl(i2c, REG_CON);

	/*
	 * The hw can read up to 32 bytes at a time. If we need more than one
	 * chunk, send an ACK after the last byte of the current chunk.
	 */
	if (len > 32) {
		len = 32;
		con &= ~REG_CON_LASTACK;
	} else {
		con |= REG_CON_LASTACK;
	}

	/* make sure we are in plain RX mode if we read a second chunk */
	if (i2c->processed != 0) {
		con &= ~REG_CON_MOD_MASK;
		con |= REG_CON_MOD(REG_CON_MOD_RX);
		if (con & REG_CON_START)
			con &= ~REG_CON_START;
	}

	i2c_writel(i2c, con, REG_CON);
	i2c_writel(i2c, len, REG_MRXCNT);
}

/**
 * Fill the transmit buffer with data from i2c->msg
 */
static int rk3x_i2c_fill_transmit_buf(struct rk3x_i2c *i2c, bool sendend)
{
	unsigned int i, j;
	u32 cnt = 0;
	u32 val;
	u8 byte;

	for (i = 0; i < 8; ++i) {
		val = 0;
		for (j = 0; j < 4; ++j) {
			if ((i2c->processed == i2c->msg->len) && (cnt != 0))
				break;

			if (i2c->processed == 0 && cnt == 0)
				byte = (i2c->addr & 0x7f) << 1;
			else
				byte = i2c->msg->buf[i2c->processed++];

			val |= byte << (j * 8);
			cnt++;
		}

		i2c_writel(i2c, val, TXBUFFER_BASE + 4 * i);

		if (i2c->processed == i2c->msg->len)
			break;
	}

	if (sendend)
		i2c_writel(i2c, cnt, REG_MTXCNT);

	return cnt;
}


/* IRQ handlers for individual states */

static void rk3x_i2c_handle_write(struct rk3x_i2c *i2c, unsigned int ipd)
{
	if (!(ipd & REG_INT_MBTF)) {
		rk3x_i2c_stop(i2c, -EIO);
		dev_err(i2c->dev, "unexpected irq in WRITE: 0x%x\n", ipd);
		rk3x_i2c_clean_ipd(i2c);
		return;
	}

	/* ack interrupt */
	i2c_writel(i2c, REG_INT_MBTF, REG_IPD);

	rk3x_i2c_auto_stop(i2c);
	/* are we finished? */
	if (i2c->processed == i2c->msg->len)
		rk3x_i2c_stop(i2c, i2c->error);
	else
		rk3x_i2c_fill_transmit_buf(i2c, true);
}

static void rk3x_i2c_read(struct rk3x_i2c *i2c)
{
	unsigned int i;
	unsigned int len = i2c->msg->len - i2c->processed;
	u32 val;
	u8 byte;

	/* Can only handle a maximum of 32 bytes at a time */
	if (len > 32)
		len = 32;

	/* read the data from receive buffer */
	for (i = 0; i < len; ++i) {
		if (i % 4 == 0)
			val = i2c_readl(i2c, RXBUFFER_BASE + (i / 4) * 4);

		byte = (val >> ((i % 4) * 8)) & 0xff;
		i2c->msg->buf[i2c->processed++] = byte;
	}
}

static void rk3x_i2c_handle_read(struct rk3x_i2c *i2c, unsigned int ipd)
{
	/* we only care for MBRF here. */
	if (!(ipd & REG_INT_MBRF))
		return;

	/* ack interrupt (read also produces a spurious START flag, clear it too) */
	i2c_writel(i2c, REG_INT_MBRF | REG_INT_START, REG_IPD);

	/* read the data from receive buffer */
	rk3x_i2c_read(i2c);

	rk3x_i2c_auto_stop(i2c);
	/* are we finished? */
	if (i2c->processed == i2c->msg->len)
		rk3x_i2c_stop(i2c, i2c->error);
	else
		rk3x_i2c_prepare_read(i2c);
}

static void rk3x_i2c_handle_stop(struct rk3x_i2c *i2c, unsigned int ipd)
{
	unsigned int con;

	if (!(ipd & REG_INT_STOP)) {
		rk3x_i2c_stop(i2c, -EIO);
		dev_err(i2c->dev, "unexpected irq in STOP: 0x%x\n", ipd);
		rk3x_i2c_clean_ipd(i2c);
		return;
	}

	if (i2c->autostop_supported && !i2c->error) {
		if (i2c->mode != REG_CON_MOD_TX && i2c->msg) {
			if ((i2c->msg->len - i2c->processed) > 0)
				rk3x_i2c_read(i2c);
		}

		i2c->processed = 0;
		i2c->msg = NULL;
	}

	/* ack interrupt */
	i2c_writel(i2c, REG_INT_STOP, REG_IPD);

	/* disable STOP bit */
	con = i2c_readl(i2c, REG_CON);
	con &= ~REG_CON_STOP;
	if (i2c->autostop_supported)
		con &= ~REG_CON_START;
	i2c_writel(i2c, con, REG_CON);

	i2c->busy = false;
	i2c->state = STATE_IDLE;

	/* signal rk3x_i2c_xfer that we are finished */
	rk3x_i2c_wake_up(i2c);
}

static irqreturn_t rk3x_i2c_irq(int irqno, void *dev_id)
{
	struct rk3x_i2c *i2c = dev_id;
	unsigned int ipd;

	spin_lock(&i2c->lock);

	ipd = i2c_readl(i2c, REG_IPD);
	if (i2c->state == STATE_IDLE) {
		dev_warn_ratelimited(i2c->dev,
				     "irq in STATE_IDLE, ipd = 0x%x\n",
				     ipd);
		rk3x_i2c_clean_ipd(i2c);
		goto out;
	}

	dev_dbg(i2c->dev, "IRQ: state %d, ipd: %x\n", i2c->state, ipd);

	/* Clean interrupt bits we don't care about */
	ipd &= ~(REG_INT_BRF | REG_INT_BTF);

	if (ipd & REG_INT_NAKRCV) {
		/*
		 * We got a NACK in the last operation. Depending on whether
		 * IGNORE_NAK is set, we have to stop the operation and report
		 * an error.
		 */
		i2c_writel(i2c, REG_INT_NAKRCV, REG_IPD);

		ipd &= ~REG_INT_NAKRCV;

		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			if (i2c->autostop_supported) {
				i2c->error = -ENXIO;
				i2c->state = STATE_STOP;
			} else {
				rk3x_i2c_stop(i2c, -ENXIO);
				goto out;
			}
		}
	}

	/* is there anything left to handle? */
	if ((ipd & REG_INT_ALL) == 0)
		goto out;

	switch (i2c->state) {
	case STATE_WRITE:
		rk3x_i2c_handle_write(i2c, ipd);
		break;
	case STATE_READ:
		rk3x_i2c_handle_read(i2c, ipd);
		break;
	case STATE_STOP:
		rk3x_i2c_handle_stop(i2c, ipd);
		break;
	case STATE_IDLE:
		break;
	}

out:
	spin_unlock(&i2c->lock);
	return IRQ_HANDLED;
}

/**
 * Get timing values of I2C specification
 *
 * @speed: Desired SCL frequency
 *
 * Returns: Matched i2c spec values.
 */
static const struct i2c_spec_values *rk3x_i2c_get_spec(unsigned int speed)
{
	if (speed <= I2C_MAX_STANDARD_MODE_FREQ)
		return &standard_mode_spec;
	else if (speed <= I2C_MAX_FAST_MODE_FREQ)
		return &fast_mode_spec;
	else
		return &fast_mode_plus_spec;
}

/**
 * Calculate divider values for desired SCL frequency
 *
 * @clk_rate: I2C input clock rate
 * @t: Known I2C timing information
 * @t_calc: Caculated rk3x private timings that would be written into regs
 *
 * Returns: 0 on success, -EINVAL if the goal SCL rate is too slow. In that case
 * a best-effort divider value is returned in divs. If the target rate is
 * too high, we silently use the highest possible rate.
 */
static int rk3x_i2c_v0_calc_timings(unsigned long clk_rate,
				    struct i2c_timings *t,
				    struct rk3x_i2c_calced_timings *t_calc)
{
	unsigned long min_low_ns, min_high_ns;
	unsigned long max_low_ns, min_total_ns;

	unsigned long clk_rate_khz, scl_rate_khz;

	unsigned long min_low_div, min_high_div;
	unsigned long max_low_div;

	unsigned long min_div_for_hold, min_total_div;
	unsigned long extra_div, extra_low_div, ideal_low_div;

	unsigned long data_hold_buffer_ns = 50;
	const struct i2c_spec_values *spec;
	int ret = 0;

	/* Only support standard-mode and fast-mode */
	if (WARN_ON(t->bus_freq_hz > I2C_MAX_FAST_MODE_FREQ))
		t->bus_freq_hz = I2C_MAX_FAST_MODE_FREQ;

	/* prevent scl_rate_khz from becoming 0 */
	if (WARN_ON(t->bus_freq_hz < 1000))
		t->bus_freq_hz = 1000;

	/*
	 * min_low_ns:  The minimum number of ns we need to hold low to
	 *		meet I2C specification, should include fall time.
	 * min_high_ns: The minimum number of ns we need to hold high to
	 *		meet I2C specification, should include rise time.
	 * max_low_ns:  The maximum number of ns we can hold low to meet
	 *		I2C specification.
	 *
	 * Note: max_low_ns should be (maximum data hold time * 2 - buffer)
	 *	 This is because the i2c host on Rockchip holds the data line
	 *	 for half the low time.
	 */
	spec = rk3x_i2c_get_spec(t->bus_freq_hz);
	min_high_ns = t->scl_rise_ns + spec->min_high_ns;

	/*
	 * Timings for repeated start:
	 * - controller appears to drop SDA at .875x (7/8) programmed clk high.
	 * - controller appears to keep SCL high for 2x programmed clk high.
	 *
	 * We need to account for those rules in picking our "high" time so
	 * we meet tSU;STA and tHD;STA times.
	 */
	min_high_ns = max(min_high_ns, DIV_ROUND_UP(
		(t->scl_rise_ns + spec->min_setup_start_ns) * 1000, 875));
	min_high_ns = max(min_high_ns, DIV_ROUND_UP(
		(t->scl_rise_ns + spec->min_setup_start_ns + t->sda_fall_ns +
		spec->min_high_ns), 2));

	min_low_ns = t->scl_fall_ns + spec->min_low_ns;
	max_low_ns =  spec->max_data_hold_ns * 2 - data_hold_buffer_ns;
	min_total_ns = min_low_ns + min_high_ns;

	/* Adjust to avoid overflow */
	clk_rate_khz = DIV_ROUND_UP(clk_rate, 1000);
	scl_rate_khz = t->bus_freq_hz / 1000;

	/*
	 * We need the total div to be >= this number
	 * so we don't clock too fast.
	 */
	min_total_div = DIV_ROUND_UP(clk_rate_khz, scl_rate_khz * 8);

	/* These are the min dividers needed for min hold times. */
	min_low_div = DIV_ROUND_UP(clk_rate_khz * min_low_ns, 8 * 1000000);
	min_high_div = DIV_ROUND_UP(clk_rate_khz * min_high_ns, 8 * 1000000);
	min_div_for_hold = (min_low_div + min_high_div);

	/*
	 * This is the maximum divider so we don't go over the maximum.
	 * We don't round up here (we round down) since this is a maximum.
	 */
	max_low_div = clk_rate_khz * max_low_ns / (8 * 1000000);

	if (min_low_div > max_low_div) {
		WARN_ONCE(true,
			  "Conflicting, min_low_div %lu, max_low_div %lu\n",
			  min_low_div, max_low_div);
		max_low_div = min_low_div;
	}

	if (min_div_for_hold > min_total_div) {
		/*
		 * Time needed to meet hold requirements is important.
		 * Just use that.
		 */
		t_calc->div_low = min_low_div;
		t_calc->div_high = min_high_div;
	} else {
		/*
		 * We've got to distribute some time among the low and high
		 * so we don't run too fast.
		 */
		extra_div = min_total_div - min_div_for_hold;

		/*
		 * We'll try to split things up perfectly evenly,
		 * biasing slightly towards having a higher div
		 * for low (spend more time low).
		 */
		ideal_low_div = DIV_ROUND_UP(clk_rate_khz * min_low_ns,
					     scl_rate_khz * 8 * min_total_ns);

		/* Don't allow it to go over the maximum */
		if (ideal_low_div > max_low_div)
			ideal_low_div = max_low_div;

		/*
		 * Handle when the ideal low div is going to take up
		 * more than we have.
		 */
		if (ideal_low_div > min_low_div + extra_div)
			ideal_low_div = min_low_div + extra_div;

		/* Give low the "ideal" and give high whatever extra is left */
		extra_low_div = ideal_low_div - min_low_div;
		t_calc->div_low = ideal_low_div;
		t_calc->div_high = min_high_div + (extra_div - extra_low_div);
	}

	/*
	 * Adjust to the fact that the hardware has an implicit "+1".
	 * NOTE: Above calculations always produce div_low > 0 and div_high > 0.
	 */
	t_calc->div_low--;
	t_calc->div_high--;

	/* Give the tuning value 0, that would not update con register */
	t_calc->tuning = 0;
	/* Maximum divider supported by hw is 0xffff */
	if (t_calc->div_low > 0xffff) {
		t_calc->div_low = 0xffff;
		ret = -EINVAL;
	}

	if (t_calc->div_high > 0xffff) {
		t_calc->div_high = 0xffff;
		ret = -EINVAL;
	}

	return ret;
}

/**
 * Calculate timing values for desired SCL frequency
 *
 * @clk_rate: I2C input clock rate
 * @t: Known I2C timing information
 * @t_calc: Caculated rk3x private timings that would be written into regs
 *
 * Returns: 0 on success, -EINVAL if the goal SCL rate is too slow. In that case
 * a best-effort divider value is returned in divs. If the target rate is
 * too high, we silently use the highest possible rate.
 * The following formulas are v1's method to calculate timings.
 *
 * l = divl + 1;
 * h = divh + 1;
 * s = sda_update_config + 1;
 * u = start_setup_config + 1;
 * p = stop_setup_config + 1;
 * T = Tclk_i2c;
 *
 * tHigh = 8 * h * T;
 * tLow = 8 * l * T;
 *
 * tHD;sda = (l * s + 1) * T;
 * tSU;sda = [(8 - s) * l + 1] * T;
 * tI2C = 8 * (l + h) * T;
 *
 * tSU;sta = (8h * u + 1) * T;
 * tHD;sta = [8h * (u + 1) - 1] * T;
 * tSU;sto = (8h * p + 1) * T;
 */
static int rk3x_i2c_v1_calc_timings(unsigned long clk_rate,
				    struct i2c_timings *t,
				    struct rk3x_i2c_calced_timings *t_calc)
{
	unsigned long min_low_ns, min_high_ns;
	unsigned long min_setup_start_ns, min_setup_data_ns;
	unsigned long min_setup_stop_ns, max_hold_data_ns;

	unsigned long clk_rate_khz, scl_rate_khz;

	unsigned long min_low_div, min_high_div;

	unsigned long min_div_for_hold, min_total_div;
	unsigned long extra_div, extra_low_div;
	unsigned long sda_update_cfg, stp_sta_cfg, stp_sto_cfg;

	const struct i2c_spec_values *spec;
	int ret = 0;

	/* Support standard-mode, fast-mode and fast-mode plus */
	if (WARN_ON(t->bus_freq_hz > I2C_MAX_FAST_MODE_PLUS_FREQ))
		t->bus_freq_hz = I2C_MAX_FAST_MODE_PLUS_FREQ;

	/* prevent scl_rate_khz from becoming 0 */
	if (WARN_ON(t->bus_freq_hz < 1000))
		t->bus_freq_hz = 1000;

	/*
	 * min_low_ns: The minimum number of ns we need to hold low to
	 *	       meet I2C specification, should include fall time.
	 * min_high_ns: The minimum number of ns we need to hold high to
	 *	        meet I2C specification, should include rise time.
	 */
	spec = rk3x_i2c_get_spec(t->bus_freq_hz);

	/* calculate min-divh and min-divl */
	clk_rate_khz = DIV_ROUND_UP(clk_rate, 1000);
	scl_rate_khz = t->bus_freq_hz / 1000;
	min_total_div = DIV_ROUND_UP(clk_rate_khz, scl_rate_khz * 8);

	min_high_ns = t->scl_rise_ns + spec->min_high_ns;
	min_high_div = DIV_ROUND_UP(clk_rate_khz * min_high_ns, 8 * 1000000);

	min_low_ns = t->scl_fall_ns + spec->min_low_ns;
	min_low_div = DIV_ROUND_UP(clk_rate_khz * min_low_ns, 8 * 1000000);

	/*
	 * Final divh and divl must be greater than 0, otherwise the
	 * hardware would not output the i2c clk.
	 */
	min_high_div = (min_high_div < 1) ? 2 : min_high_div;
	min_low_div = (min_low_div < 1) ? 2 : min_low_div;

	/* These are the min dividers needed for min hold times. */
	min_div_for_hold = (min_low_div + min_high_div);

	/*
	 * This is the maximum divider so we don't go over the maximum.
	 * We don't round up here (we round down) since this is a maximum.
	 */
	if (min_div_for_hold >= min_total_div) {
		/*
		 * Time needed to meet hold requirements is important.
		 * Just use that.
		 */
		t_calc->div_low = min_low_div;
		t_calc->div_high = min_high_div;
	} else {
		/*
		 * We've got to distribute some time among the low and high
		 * so we don't run too fast.
		 * We'll try to split things up by the scale of min_low_div and
		 * min_high_div, biasing slightly towards having a higher div
		 * for low (spend more time low).
		 */
		extra_div = min_total_div - min_div_for_hold;
		extra_low_div = DIV_ROUND_UP(min_low_div * extra_div,
					     min_div_for_hold);

		t_calc->div_low = min_low_div + extra_low_div;
		t_calc->div_high = min_high_div + (extra_div - extra_low_div);
	}

	/*
	 * calculate sda data hold count by the rules, data_upd_st:3
	 * is a appropriate value to reduce calculated times.
	 */
	for (sda_update_cfg = 3; sda_update_cfg > 0; sda_update_cfg--) {
		max_hold_data_ns =  DIV_ROUND_UP((sda_update_cfg
						 * (t_calc->div_low) + 1)
						 * 1000000, clk_rate_khz);
		min_setup_data_ns =  DIV_ROUND_UP(((8 - sda_update_cfg)
						 * (t_calc->div_low) + 1)
						 * 1000000, clk_rate_khz);
		if ((max_hold_data_ns < spec->max_data_hold_ns) &&
		    (min_setup_data_ns > spec->min_data_setup_ns))
			break;
	}

	/* calculate setup start config */
	min_setup_start_ns = t->scl_rise_ns + spec->min_setup_start_ns;
	stp_sta_cfg = DIV_ROUND_UP(clk_rate_khz * min_setup_start_ns
			   - 1000000, 8 * 1000000 * (t_calc->div_high));

	/* calculate setup stop config */
	min_setup_stop_ns = t->scl_rise_ns + spec->min_setup_stop_ns;
	stp_sto_cfg = DIV_ROUND_UP(clk_rate_khz * min_setup_stop_ns
			   - 1000000, 8 * 1000000 * (t_calc->div_high));

	t_calc->tuning = REG_CON_SDA_CFG(--sda_update_cfg) |
			 REG_CON_STA_CFG(--stp_sta_cfg) |
			 REG_CON_STO_CFG(--stp_sto_cfg);

	t_calc->div_low--;
	t_calc->div_high--;

	/* Maximum divider supported by hw is 0xffff */
	if (t_calc->div_low > 0xffff) {
		t_calc->div_low = 0xffff;
		ret = -EINVAL;
	}

	if (t_calc->div_high > 0xffff) {
		t_calc->div_high = 0xffff;
		ret = -EINVAL;
	}

	return ret;
}

static void rk3x_i2c_adapt_div(struct rk3x_i2c *i2c, unsigned long clk_rate)
{
	struct i2c_timings *t = &i2c->t;
	struct rk3x_i2c_calced_timings calc;
	u64 t_low_ns, t_high_ns;
	unsigned long flags;
	u32 val;
	int ret;

	ret = i2c->soc_data->calc_timings(clk_rate, t, &calc);
	WARN_ONCE(ret != 0, "Could not reach SCL freq %u", t->bus_freq_hz);

	clk_enable(i2c->pclk);

	spin_lock_irqsave(&i2c->lock, flags);
	val = i2c_readl(i2c, REG_CON);
	val &= ~REG_CON_TUNING_MASK;
	val |= calc.tuning;
	i2c_writel(i2c, val, REG_CON);
	i2c_writel(i2c, (calc.div_high << 16) | (calc.div_low & 0xffff),
		   REG_CLKDIV);
	spin_unlock_irqrestore(&i2c->lock, flags);

	clk_disable(i2c->pclk);

	t_low_ns = div_u64(((u64)calc.div_low + 1) * 8 * 1000000000, clk_rate);
	t_high_ns = div_u64(((u64)calc.div_high + 1) * 8 * 1000000000,
			    clk_rate);
	dev_dbg(i2c->dev,
		"CLK %lukhz, Req %uns, Act low %lluns high %lluns\n",
		clk_rate / 1000,
		1000000000 / t->bus_freq_hz,
		t_low_ns, t_high_ns);
}

/**
 * rk3x_i2c_clk_notifier_cb - Clock rate change callback
 * @nb:		Pointer to notifier block
 * @event:	Notification reason
 * @data:	Pointer to notification data object
 *
 * The callback checks whether a valid bus frequency can be generated after the
 * change. If so, the change is acknowledged, otherwise the change is aborted.
 * New dividers are written to the HW in the pre- or post change notification
 * depending on the scaling direction.
 *
 * Code adapted from i2c-cadence.c.
 *
 * Return:	NOTIFY_STOP if the rate change should be aborted, NOTIFY_OK
 *		to acknowledge the change, NOTIFY_DONE if the notification is
 *		considered irrelevant.
 */
static int rk3x_i2c_clk_notifier_cb(struct notifier_block *nb, unsigned long
				    event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct rk3x_i2c *i2c = container_of(nb, struct rk3x_i2c, clk_rate_nb);
	struct rk3x_i2c_calced_timings calc;

	switch (event) {
	case PRE_RATE_CHANGE:
		/*
		 * Try the calculation (but don't store the result) ahead of
		 * time to see if we need to block the clock change.  Timings
		 * shouldn't actually take effect until rk3x_i2c_adapt_div().
		 */
		if (i2c->soc_data->calc_timings(ndata->new_rate, &i2c->t,
						&calc) != 0)
			return NOTIFY_STOP;

		/* scale up */
		if (ndata->new_rate > ndata->old_rate)
			rk3x_i2c_adapt_div(i2c, ndata->new_rate);

		return NOTIFY_OK;
	case POST_RATE_CHANGE:
		/* scale down */
		if (ndata->new_rate < ndata->old_rate)
			rk3x_i2c_adapt_div(i2c, ndata->new_rate);
		return NOTIFY_OK;
	case ABORT_RATE_CHANGE:
		/* scale up */
		if (ndata->new_rate > ndata->old_rate)
			rk3x_i2c_adapt_div(i2c, ndata->old_rate);
		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

/**
 * Setup I2C registers for an I2C operation specified by msgs, num.
 *
 * Must be called with i2c->lock held.
 *
 * @msgs: I2C msgs to process
 * @num: Number of msgs
 *
 * returns: Number of I2C msgs processed or negative in case of error
 */
static int rk3x_i2c_setup(struct rk3x_i2c *i2c, struct i2c_msg *msgs, int num)
{
	u32 addr = (msgs[0].addr & 0x7f) << 1;
	int ret = 0;

	/*
	 * The I2C adapter can issue a small (len < 4) write packet before
	 * reading. This speeds up SMBus-style register reads.
	 * The MRXADDR/MRXRADDR hold the slave address and the slave register
	 * address in this case.
	 */

	if (num >= 2 && msgs[0].len < 4 &&
	    !(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD)) {
		u32 reg_addr = 0;
		int i;

		dev_dbg(i2c->dev, "Combined write/read from addr 0x%x\n",
			addr >> 1);

		/* Fill MRXRADDR with the register address(es) */
		for (i = 0; i < msgs[0].len; ++i) {
			reg_addr |= msgs[0].buf[i] << (i * 8);
			reg_addr |= REG_MRXADDR_VALID(i);
		}

		/* msgs[0] is handled by hw. */
		i2c->msg = &msgs[1];

		i2c->mode = REG_CON_MOD_REGISTER_TX;

		i2c_writel(i2c, addr | REG_MRXADDR_VALID(0), REG_MRXADDR);
		i2c_writel(i2c, reg_addr, REG_MRXRADDR);

		ret = 2;
	} else {
		/*
		 * We'll have to do it the boring way and process the msgs
		 * one-by-one.
		 */

		if (msgs[0].flags & I2C_M_RD) {
			addr |= 1; /* set read bit */

			/*
			 * We have to transmit the slave addr first. Use
			 * MOD_REGISTER_TX for that purpose.
			 */
			i2c->mode = REG_CON_MOD_REGISTER_TX;
			i2c_writel(i2c, addr | REG_MRXADDR_VALID(0),
				   REG_MRXADDR);
			i2c_writel(i2c, 0, REG_MRXRADDR);
		} else {
			i2c->mode = REG_CON_MOD_TX;
		}

		i2c->msg = &msgs[0];

		ret = 1;
	}

	i2c->addr = msgs[0].addr;
	i2c->busy = true;
	i2c->processed = 0;
	i2c->error = 0;

	rk3x_i2c_clean_ipd(i2c);
	if (i2c->autostop_supported)
		i2c_writel(i2c, 0, REG_CON1);

	return ret;
}

static int rk3x_i2c_wait_xfer_poll(struct rk3x_i2c *i2c)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), WAIT_TIMEOUT);

	while (READ_ONCE(i2c->busy) &&
	       ktime_compare(ktime_get(), timeout) < 0) {
		udelay(5);
		rk3x_i2c_irq(0, i2c);
	}

	return !i2c->busy;
}

static int rk3x_i2c_xfer_common(struct i2c_adapter *adap,
				struct i2c_msg *msgs, int num, bool polling)
{
	struct rk3x_i2c *i2c = (struct rk3x_i2c *)adap->algo_data;
	unsigned long timeout, flags;
	u32 val;
	int ret = 0;
	int i;

	if (i2c->suspended)
		return -EACCES;

	spin_lock_irqsave(&i2c->lock, flags);

	clk_enable(i2c->clk);
	clk_enable(i2c->pclk);

	i2c->is_last_msg = false;

	/*
	 * Process msgs. We can handle more than one message at once (see
	 * rk3x_i2c_setup()).
	 */
	for (i = 0; i < num; i += ret) {
		ret = rk3x_i2c_setup(i2c, msgs + i, num - i);

		if (ret < 0) {
			dev_err(i2c->dev, "rk3x_i2c_setup() failed\n");
			break;
		}

		if (i + ret >= num)
			i2c->is_last_msg = true;

		rk3x_i2c_start(i2c);

		spin_unlock_irqrestore(&i2c->lock, flags);

		if (!polling) {
			timeout = wait_event_timeout(i2c->wait, !i2c->busy,
						     msecs_to_jiffies(WAIT_TIMEOUT));
		} else {
			timeout = rk3x_i2c_wait_xfer_poll(i2c);
		}

		spin_lock_irqsave(&i2c->lock, flags);

		if (timeout == 0) {
			dev_err(i2c->dev, "timeout, ipd: 0x%02x, state: %d\n",
				i2c_readl(i2c, REG_IPD), i2c->state);

			/* Force a STOP condition without interrupt */
			rk3x_i2c_disable_irq(i2c);
			val = i2c_readl(i2c, REG_CON) & REG_CON_TUNING_MASK;
			val |= REG_CON_EN | REG_CON_STOP;
			i2c_writel(i2c, val, REG_CON);

			i2c->state = STATE_IDLE;

			ret = -ETIMEDOUT;
			break;
		}

		if (i2c->error) {
			ret = i2c->error;
			break;
		}
	}

	rk3x_i2c_disable_irq(i2c);
	rk3x_i2c_disable(i2c);

	clk_disable(i2c->pclk);
	clk_disable(i2c->clk);

	spin_unlock_irqrestore(&i2c->lock, flags);

	return ret < 0 ? ret : num;
}

static int rk3x_i2c_xfer(struct i2c_adapter *adap,
			 struct i2c_msg *msgs, int num)
{
	return rk3x_i2c_xfer_common(adap, msgs, num, false);
}

static int rk3x_i2c_xfer_polling(struct i2c_adapter *adap,
				 struct i2c_msg *msgs, int num)
{
	return rk3x_i2c_xfer_common(adap, msgs, num, true);
}

static int rk3x_i2c_restart_notify(struct notifier_block *this,
				   unsigned long mode, void *cmd)
{
	struct rk3x_i2c *i2c = container_of(this, struct rk3x_i2c,
					    i2c_restart_nb);
	int tmo = WAIT_TIMEOUT * USEC_PER_MSEC;
	u32 val;

	if (i2c->state != STATE_IDLE) {
		i2c->system_restarting = true;
		/* complete the unfinished job */
		while (tmo-- && i2c->busy) {
			udelay(1);
			rk3x_i2c_irq(0, i2c);
		}
	}

	if (tmo <= 0) {
		dev_err(i2c->dev, "restart timeout, ipd: 0x%02x, state: %d\n",
			i2c_readl(i2c, REG_IPD), i2c->state);

		/* Force a STOP condition without interrupt */
		i2c_writel(i2c, 0, REG_IEN);
		val = i2c_readl(i2c, REG_CON) & REG_CON_TUNING_MASK;
		val |= REG_CON_EN | REG_CON_STOP;
		i2c_writel(i2c, val, REG_CON);

		udelay(10);
		i2c->state = STATE_IDLE;
	}

	return NOTIFY_DONE;
}

static unsigned int rk3x_i2c_get_version(struct rk3x_i2c *i2c)
{
	unsigned int version;

	clk_enable(i2c->pclk);
	version = i2c_readl(i2c, REG_CON) & REG_CON_VERSION;
	clk_disable(i2c->pclk);
	version >>= REG_CON_VERSION_SHIFT;

	return version;
}

static __maybe_unused int rk3x_i2c_suspend_noirq(struct device *dev)
{
	struct rk3x_i2c *i2c = dev_get_drvdata(dev);

	/*
	 * Below code is needed only to ensure that there are no
	 * activities on I2C bus. if at this moment any driver
	 * is trying to use I2C bus - this may cause i2c timeout.
	 *
	 * So forbid access to I2C device using i2c->suspended flag.
	 */
	i2c_lock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);
	i2c->suspended = 1;
	i2c_unlock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);

	return 0;
}

static __maybe_unused int rk3x_i2c_resume_noirq(struct device *dev)
{
	struct rk3x_i2c *i2c = dev_get_drvdata(dev);

	rk3x_i2c_adapt_div(i2c, clk_get_rate(i2c->clk));

	/* Allow access to I2C bus */
	i2c_lock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);
	i2c->suspended = 0;
	i2c_unlock_bus(&i2c->adap, I2C_LOCK_ROOT_ADAPTER);

	return 0;
}

static u32 rk3x_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm rk3x_i2c_algorithm = {
	.master_xfer		= rk3x_i2c_xfer,
	.master_xfer_atomic	= rk3x_i2c_xfer_polling,
	.functionality		= rk3x_i2c_func,
};

static const struct rk3x_i2c_soc_data rv1108_soc_data = {
	.grf_offset = 0x408,
	.calc_timings = rk3x_i2c_v1_calc_timings,
};

static const struct rk3x_i2c_soc_data rv1126_soc_data = {
	.grf_offset = 0x118,
	.calc_timings = rk3x_i2c_v1_calc_timings,
};

static const struct rk3x_i2c_soc_data rk3066_soc_data = {
	.grf_offset = 0x154,
	.calc_timings = rk3x_i2c_v0_calc_timings,
};

static const struct rk3x_i2c_soc_data rk3188_soc_data = {
	.grf_offset = 0x0a4,
	.calc_timings = rk3x_i2c_v0_calc_timings,
};

static const struct rk3x_i2c_soc_data rk3228_soc_data = {
	.grf_offset = -1,
	.calc_timings = rk3x_i2c_v0_calc_timings,
};

static const struct rk3x_i2c_soc_data rk3288_soc_data = {
	.grf_offset = -1,
	.calc_timings = rk3x_i2c_v0_calc_timings,
};

static const struct rk3x_i2c_soc_data rk3399_soc_data = {
	.grf_offset = -1,
	.calc_timings = rk3x_i2c_v1_calc_timings,
};

static const struct of_device_id rk3x_i2c_match[] = {
	{
		.compatible = "rockchip,rv1108-i2c",
		.data = &rv1108_soc_data
	},
	{
		.compatible = "rockchip,rv1126-i2c",
		.data = &rv1126_soc_data
	},
	{
		.compatible = "rockchip,rk3066-i2c",
		.data = &rk3066_soc_data
	},
	{
		.compatible = "rockchip,rk3188-i2c",
		.data = &rk3188_soc_data
	},
	{
		.compatible = "rockchip,rk3228-i2c",
		.data = &rk3228_soc_data
	},
	{
		.compatible = "rockchip,rk3288-i2c",
		.data = &rk3288_soc_data
	},
	{
		.compatible = "rockchip,rk3399-i2c",
		.data = &rk3399_soc_data
	},
	{},
};
MODULE_DEVICE_TABLE(of, rk3x_i2c_match);

static int rk3x_i2c_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct rk3x_i2c *i2c;
	int ret = 0;
	u32 value;
	int irq;
	unsigned long clk_rate;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct rk3x_i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	match = of_match_node(rk3x_i2c_match, np);
	i2c->soc_data = match->data;

	/* use common interface to get I2C timing properties */
	i2c_parse_fw_timings(&pdev->dev, &i2c->t, true);

	strlcpy(i2c->adap.name, "rk3x-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &rk3x_i2c_algorithm;
	i2c->adap.retries = 3;
	i2c->adap.dev.of_node = np;
	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	i2c->dev = &pdev->dev;

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	i2c->i2c_restart_nb.notifier_call = rk3x_i2c_restart_notify;
	i2c->i2c_restart_nb.priority = 128;
	ret = register_pre_restart_handler(&i2c->i2c_restart_nb);
	if (ret) {
		dev_err(&pdev->dev, "failed to setup i2c restart handler.\n");
		return ret;
	}

	i2c->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c->regs))
		return PTR_ERR(i2c->regs);

	/*
	 * Switch to new interface if the SoC also offers the old one.
	 * The control bit is located in the GRF register space.
	 */
	if (i2c->soc_data->grf_offset >= 0) {
		struct regmap *grf;

		grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
		if (!IS_ERR(grf)) {
			int bus_nr;

			/* Try to set the I2C adapter number from dt */
			bus_nr = of_alias_get_id(np, "i2c");
			if (bus_nr < 0) {
				dev_err(&pdev->dev, "rk3x-i2c needs i2cX alias");
				return -EINVAL;
			}

			if (i2c->soc_data == &rv1108_soc_data && bus_nr == 2)
				/* rv1108 i2c2 set grf offset-0x408, bit-10 */
				value = BIT(26) | BIT(10);
			else if (i2c->soc_data == &rv1126_soc_data &&
				 bus_nr == 2)
				/* rv1126 i2c2 set pmugrf offset-0x118, bit-4 */
				value = BIT(20) | BIT(4);
			else
				/* rk3xxx 27+i: write mask, 11+i: value */
				value = BIT(27 + bus_nr) | BIT(11 + bus_nr);

			ret = regmap_write(grf, i2c->soc_data->grf_offset,
					   value);
			if (ret != 0) {
				dev_err(i2c->dev, "Could not write to GRF: %d\n",
					ret);
				return ret;
			}
		}
	}

	/* IRQ setup */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(&pdev->dev, irq, rk3x_i2c_irq,
			       0, dev_name(&pdev->dev), i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request IRQ\n");
		return ret;
	}

	platform_set_drvdata(pdev, i2c);

	if (i2c->soc_data->calc_timings == rk3x_i2c_v0_calc_timings) {
		/* Only one clock to use for bus clock and peripheral clock */
		i2c->clk = devm_clk_get(&pdev->dev, NULL);
		i2c->pclk = i2c->clk;
	} else {
		i2c->clk = devm_clk_get(&pdev->dev, "i2c");
		i2c->pclk = devm_clk_get(&pdev->dev, "pclk");
	}

	if (IS_ERR(i2c->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c->clk),
				     "Can't get bus clk\n");

	if (IS_ERR(i2c->pclk))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2c->pclk),
				     "Can't get periph clk\n");

	ret = clk_prepare(i2c->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can't prepare bus clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare(i2c->pclk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can't prepare periph clock: %d\n", ret);
		goto err_clk;
	}

	i2c->clk_rate_nb.notifier_call = rk3x_i2c_clk_notifier_cb;
	ret = clk_notifier_register(i2c->clk, &i2c->clk_rate_nb);
	if (ret != 0) {
		dev_err(&pdev->dev, "Unable to register clock notifier\n");
		goto err_pclk;
	}

	clk_rate = clk_get_rate(i2c->clk);
	rk3x_i2c_adapt_div(i2c, clk_rate);

	if (rk3x_i2c_get_version(i2c) >= RK_I2C_VERSION5)
		i2c->autostop_supported = true;

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0)
		goto err_clk_notifier;

	return 0;

err_clk_notifier:
	clk_notifier_unregister(i2c->clk, &i2c->clk_rate_nb);
err_pclk:
	clk_unprepare(i2c->pclk);
err_clk:
	clk_unprepare(i2c->clk);
	return ret;
}

static int rk3x_i2c_remove(struct platform_device *pdev)
{
	struct rk3x_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);

	clk_notifier_unregister(i2c->clk, &i2c->clk_rate_nb);
	unregister_pre_restart_handler(&i2c->i2c_restart_nb);
	clk_unprepare(i2c->pclk);
	clk_unprepare(i2c->clk);

	return 0;
}

static const struct dev_pm_ops rk3x_i2c_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(rk3x_i2c_suspend_noirq,
				      rk3x_i2c_resume_noirq)
};

static struct platform_driver rk3x_i2c_driver = {
	.probe   = rk3x_i2c_probe,
	.remove  = rk3x_i2c_remove,
	.driver  = {
		.name  = "rk3x-i2c",
		.of_match_table = rk3x_i2c_match,
		.pm = &rk3x_i2c_pm_ops,
	},
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
static int __init rk3x_i2c_driver_init(void)
{
	return platform_driver_register(&rk3x_i2c_driver);
}
subsys_initcall_sync(rk3x_i2c_driver_init);

static void __exit rk3x_i2c_driver_exit(void)
{
	platform_driver_unregister(&rk3x_i2c_driver);
}
module_exit(rk3x_i2c_driver_exit);
#else
module_platform_driver(rk3x_i2c_driver);
#endif

MODULE_DESCRIPTION("Rockchip RK3xxx I2C Bus driver");
MODULE_AUTHOR("Max Schwarz <max.schwarz@online.de>");
MODULE_LICENSE("GPL v2");
