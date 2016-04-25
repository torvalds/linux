/*
 * (C) Copyright 2009-2010
 * Nokia Siemens Networks, michael.lawnick.ext@nsn.com
 *
 * Portions Copyright (C) 2010 - 2016 Cavium, Inc.
 *
 * This is a driver for the i2c adapter in Cavium Networks' OCTEON processors.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/octeon/octeon.h>

#define DRV_NAME "i2c-octeon"

/* Register offsets */
#define SW_TWSI			0x00
#define TWSI_INT		0x10

/* Controller command patterns */
#define SW_TWSI_V		BIT_ULL(63)	/* Valid bit */
#define SW_TWSI_R		BIT_ULL(56)	/* Result or read bit */

/* Controller opcode word (bits 60:57) */
#define SW_TWSI_OP_SHIFT	57
#define SW_TWSI_OP_TWSI_CLK	(4ULL << SW_TWSI_OP_SHIFT)
#define SW_TWSI_OP_EOP		(6ULL << SW_TWSI_OP_SHIFT) /* Extended opcode */

/* Controller extended opcode word (bits 34:32) */
#define SW_TWSI_EOP_SHIFT	32
#define SW_TWSI_EOP_TWSI_DATA	(SW_TWSI_OP_EOP | 1ULL << SW_TWSI_EOP_SHIFT)
#define SW_TWSI_EOP_TWSI_CTL	(SW_TWSI_OP_EOP | 2ULL << SW_TWSI_EOP_SHIFT)
#define SW_TWSI_EOP_TWSI_CLKCTL	(SW_TWSI_OP_EOP | 3ULL << SW_TWSI_EOP_SHIFT)
#define SW_TWSI_EOP_TWSI_STAT	(SW_TWSI_OP_EOP | 3ULL << SW_TWSI_EOP_SHIFT)
#define SW_TWSI_EOP_TWSI_RST	(SW_TWSI_OP_EOP | 7ULL << SW_TWSI_EOP_SHIFT)

/* Controller command and status bits */
#define TWSI_CTL_CE		0x80
#define TWSI_CTL_ENAB		0x40	/* Bus enable */
#define TWSI_CTL_STA		0x20	/* Master-mode start, HW clears when done */
#define TWSI_CTL_STP		0x10	/* Master-mode stop, HW clears when done */
#define TWSI_CTL_IFLG		0x08	/* HW event, SW writes 0 to ACK */
#define TWSI_CTL_AAK		0x04	/* Assert ACK */

/* Status values */
#define STAT_ERROR		0x00
#define STAT_START		0x08
#define STAT_REP_START		0x10
#define STAT_TXADDR_ACK		0x18
#define STAT_TXADDR_NAK		0x20
#define STAT_TXDATA_ACK		0x28
#define STAT_TXDATA_NAK		0x30
#define STAT_LOST_ARB_38	0x38
#define STAT_RXADDR_ACK		0x40
#define STAT_RXADDR_NAK		0x48
#define STAT_RXDATA_ACK		0x50
#define STAT_RXDATA_NAK		0x58
#define STAT_SLAVE_60		0x60
#define STAT_LOST_ARB_68	0x68
#define STAT_SLAVE_70		0x70
#define STAT_LOST_ARB_78	0x78
#define STAT_SLAVE_80		0x80
#define STAT_SLAVE_88		0x88
#define STAT_GENDATA_ACK	0x90
#define STAT_GENDATA_NAK	0x98
#define STAT_SLAVE_A0		0xA0
#define STAT_SLAVE_A8		0xA8
#define STAT_LOST_ARB_B0	0xB0
#define STAT_SLAVE_LOST		0xB8
#define STAT_SLAVE_NAK		0xC0
#define STAT_SLAVE_ACK		0xC8
#define STAT_AD2W_ACK		0xD0
#define STAT_AD2W_NAK		0xD8
#define STAT_IDLE		0xF8

/* TWSI_INT values */
#define TWSI_INT_CORE_EN	BIT_ULL(6)
#define TWSI_INT_SDA_OVR	BIT_ULL(8)
#define TWSI_INT_SCL_OVR	BIT_ULL(9)
#define TWSI_INT_SDA		BIT_ULL(10)
#define TWSI_INT_SCL		BIT_ULL(11)

struct octeon_i2c {
	wait_queue_head_t queue;
	struct i2c_adapter adap;
	int irq;
	u32 twsi_freq;
	int sys_freq;
	void __iomem *twsi_base;
	struct device *dev;
};

static void octeon_i2c_writeq_flush(u64 val, void __iomem *addr)
{
	__raw_writeq(val, addr);
	__raw_readq(addr);	/* wait for write to land */
}

/**
 * octeon_i2c_reg_write - write an I2C core register
 * @i2c: The struct octeon_i2c
 * @eop_reg: Register selector
 * @data: Value to be written
 *
 * The I2C core registers are accessed indirectly via the SW_TWSI CSR.
 */
static void octeon_i2c_reg_write(struct octeon_i2c *i2c, u64 eop_reg, u8 data)
{
	u64 tmp;

	__raw_writeq(SW_TWSI_V | eop_reg | data, i2c->twsi_base + SW_TWSI);
	do {
		tmp = __raw_readq(i2c->twsi_base + SW_TWSI);
	} while ((tmp & SW_TWSI_V) != 0);
}

#define octeon_i2c_ctl_write(i2c, val)					\
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_CTL, val)
#define octeon_i2c_data_write(i2c, val)					\
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_DATA, val)

/**
 * octeon_i2c_reg_read - read lower bits of an I2C core register
 * @i2c: The struct octeon_i2c
 * @eop_reg: Register selector
 *
 * Returns the data.
 *
 * The I2C core registers are accessed indirectly via the SW_TWSI CSR.
 */
static u8 octeon_i2c_reg_read(struct octeon_i2c *i2c, u64 eop_reg)
{
	u64 tmp;

	__raw_writeq(SW_TWSI_V | eop_reg | SW_TWSI_R, i2c->twsi_base + SW_TWSI);
	do {
		tmp = __raw_readq(i2c->twsi_base + SW_TWSI);
	} while ((tmp & SW_TWSI_V) != 0);

	return tmp & 0xFF;
}

#define octeon_i2c_ctl_read(i2c)					\
	octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_CTL)
#define octeon_i2c_data_read(i2c)					\
	octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_DATA)
#define octeon_i2c_stat_read(i2c)					\
	octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_STAT)

/**
 * octeon_i2c_read_int - read the TWSI_INT register
 * @i2c: The struct octeon_i2c
 *
 * Returns the value of the register.
 */
static u64 octeon_i2c_read_int(struct octeon_i2c *i2c)
{
	return __raw_readq(i2c->twsi_base + TWSI_INT);
}

/**
 * octeon_i2c_write_int - write the TWSI_INT register
 * @i2c: The struct octeon_i2c
 * @data: Value to be written
 */
static void octeon_i2c_write_int(struct octeon_i2c *i2c, u64 data)
{
	octeon_i2c_writeq_flush(data, i2c->twsi_base + TWSI_INT);
}

/**
 * octeon_i2c_int_enable - enable the CORE interrupt
 * @i2c: The struct octeon_i2c
 *
 * The interrupt will be asserted when there is non-STAT_IDLE state in
 * the SW_TWSI_EOP_TWSI_STAT register.
 */
static void octeon_i2c_int_enable(struct octeon_i2c *i2c)
{
	octeon_i2c_write_int(i2c, TWSI_INT_CORE_EN);
}

/* disable the CORE interrupt */
static void octeon_i2c_int_disable(struct octeon_i2c *i2c)
{
	/* clear TS/ST/IFLG events */
	octeon_i2c_write_int(i2c, 0);
}

/* interrupt service routine */
static irqreturn_t octeon_i2c_isr(int irq, void *dev_id)
{
	struct octeon_i2c *i2c = dev_id;

	octeon_i2c_int_disable(i2c);
	wake_up(&i2c->queue);

	return IRQ_HANDLED;
}

static int octeon_i2c_test_iflg(struct octeon_i2c *i2c)
{
	return (octeon_i2c_ctl_read(i2c) & TWSI_CTL_IFLG);
}

/**
 * octeon_i2c_wait - wait for the IFLG to be set
 * @i2c: The struct octeon_i2c
 *
 * Returns 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_wait(struct octeon_i2c *i2c)
{
	long time_left;

	octeon_i2c_int_enable(i2c);
	time_left = wait_event_timeout(i2c->queue, octeon_i2c_test_iflg(i2c),
				       i2c->adap.timeout);
	octeon_i2c_int_disable(i2c);
	if (!time_left) {
		dev_dbg(i2c->dev, "%s: timeout\n", __func__);
		return -ETIMEDOUT;
	}

	return 0;
}

static int octeon_i2c_check_status(struct octeon_i2c *i2c, int final_read)
{
	u8 stat = octeon_i2c_stat_read(i2c);

	switch (stat) {
	/* Everything is fine */
	case STAT_IDLE:
	case STAT_AD2W_ACK:
	case STAT_RXADDR_ACK:
	case STAT_TXADDR_ACK:
	case STAT_TXDATA_ACK:
		return 0;

	/* ACK allowed on pre-terminal bytes only */
	case STAT_RXDATA_ACK:
		if (!final_read)
			return 0;
		return -EIO;

	/* NAK allowed on terminal byte only */
	case STAT_RXDATA_NAK:
		if (final_read)
			return 0;
		return -EIO;

	/* Arbitration lost */
	case STAT_LOST_ARB_38:
	case STAT_LOST_ARB_68:
	case STAT_LOST_ARB_78:
	case STAT_LOST_ARB_B0:
		return -EAGAIN;

	/* Being addressed as slave, should back off & listen */
	case STAT_SLAVE_60:
	case STAT_SLAVE_70:
	case STAT_GENDATA_ACK:
	case STAT_GENDATA_NAK:
		return -EOPNOTSUPP;

	/* Core busy as slave */
	case STAT_SLAVE_80:
	case STAT_SLAVE_88:
	case STAT_SLAVE_A0:
	case STAT_SLAVE_A8:
	case STAT_SLAVE_LOST:
	case STAT_SLAVE_NAK:
	case STAT_SLAVE_ACK:
		return -EOPNOTSUPP;

	case STAT_TXDATA_NAK:
		return -EIO;
	case STAT_TXADDR_NAK:
	case STAT_RXADDR_NAK:
	case STAT_AD2W_NAK:
		return -ENXIO;
	default:
		dev_err(i2c->dev, "unhandled state: %d\n", stat);
		return -EIO;
	}
}

/* calculate and set clock divisors */
static void octeon_i2c_set_clock(struct octeon_i2c *i2c)
{
	int tclk, thp_base, inc, thp_idx, mdiv_idx, ndiv_idx, foscl, diff;
	int thp = 0x18, mdiv = 2, ndiv = 0, delta_hz = 1000000;

	for (ndiv_idx = 0; ndiv_idx < 8 && delta_hz != 0; ndiv_idx++) {
		/*
		 * An mdiv value of less than 2 seems to not work well
		 * with ds1337 RTCs, so we constrain it to larger values.
		 */
		for (mdiv_idx = 15; mdiv_idx >= 2 && delta_hz != 0; mdiv_idx--) {
			/*
			 * For given ndiv and mdiv values check the
			 * two closest thp values.
			 */
			tclk = i2c->twsi_freq * (mdiv_idx + 1) * 10;
			tclk *= (1 << ndiv_idx);
			thp_base = (i2c->sys_freq / (tclk * 2)) - 1;

			for (inc = 0; inc <= 1; inc++) {
				thp_idx = thp_base + inc;
				if (thp_idx < 5 || thp_idx > 0xff)
					continue;

				foscl = i2c->sys_freq / (2 * (thp_idx + 1));
				foscl = foscl / (1 << ndiv_idx);
				foscl = foscl / (mdiv_idx + 1) / 10;
				diff = abs(foscl - i2c->twsi_freq);
				if (diff < delta_hz) {
					delta_hz = diff;
					thp = thp_idx;
					mdiv = mdiv_idx;
					ndiv = ndiv_idx;
				}
			}
		}
	}
	octeon_i2c_reg_write(i2c, SW_TWSI_OP_TWSI_CLK, thp);
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_CLKCTL, (mdiv << 3) | ndiv);
}

static int octeon_i2c_init_lowlevel(struct octeon_i2c *i2c)
{
	u8 status;
	int tries;

	/* disable high level controller, enable bus access */
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	/* reset controller */
	octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_RST, 0);

	for (tries = 10; tries; tries--) {
		udelay(1);
		status = octeon_i2c_stat_read(i2c);
		if (status == STAT_IDLE)
			return 0;
	}
	dev_err(i2c->dev, "%s: TWSI_RST failed! (0x%x)\n", __func__, status);
	return -EIO;
}

static int octeon_i2c_recovery(struct octeon_i2c *i2c)
{
	int ret;

	ret = i2c_recover_bus(&i2c->adap);
	if (ret)
		/* recover failed, try hardware re-init */
		ret = octeon_i2c_init_lowlevel(i2c);
	return ret;
}

/**
 * octeon_i2c_start - send START to the bus
 * @i2c: The struct octeon_i2c
 *
 * Returns 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_start(struct octeon_i2c *i2c)
{
	int ret;
	u8 stat;

	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_STA);
	ret = octeon_i2c_wait(i2c);
	if (ret)
		goto error;

	stat = octeon_i2c_stat_read(i2c);
	if (stat == STAT_START || stat == STAT_REP_START)
		/* START successful, bail out */
		return 0;

error:
	/* START failed, try to recover */
	ret = octeon_i2c_recovery(i2c);
	return (ret) ? ret : -EAGAIN;
}

/* send STOP to the bus */
static void octeon_i2c_stop(struct octeon_i2c *i2c)
{
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_STP);
}

/**
 * octeon_i2c_write - send data to the bus via low-level controller
 * @i2c: The struct octeon_i2c
 * @target: Target address
 * @data: Pointer to the data to be sent
 * @length: Length of the data
 *
 * The address is sent over the bus, then the data.
 *
 * Returns 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_write(struct octeon_i2c *i2c, int target,
			    const u8 *data, int length)
{
	int i, result;

	octeon_i2c_data_write(i2c, target << 1);
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	result = octeon_i2c_wait(i2c);
	if (result)
		return result;

	for (i = 0; i < length; i++) {
		result = octeon_i2c_check_status(i2c, false);
		if (result)
			return result;

		octeon_i2c_data_write(i2c, data[i]);
		octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

		result = octeon_i2c_wait(i2c);
		if (result)
			return result;
	}

	return 0;
}

/**
 * octeon_i2c_read - receive data from the bus via low-level controller
 * @i2c: The struct octeon_i2c
 * @target: Target address
 * @data: Pointer to the location to store the data
 * @rlength: Length of the data
 * @recv_len: flag for length byte
 *
 * The address is sent over the bus, then the data is read.
 *
 * Returns 0 on success, otherwise a negative errno.
 */
static int octeon_i2c_read(struct octeon_i2c *i2c, int target,
			   u8 *data, u16 *rlength, bool recv_len)
{
	int i, result, length = *rlength;
	bool final_read = false;

	if (length < 1)
		return -EINVAL;

	octeon_i2c_data_write(i2c, (target << 1) | 1);
	octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);

	result = octeon_i2c_wait(i2c);
	if (result)
		return result;

	/* address OK ? */
	result = octeon_i2c_check_status(i2c, false);
	if (result)
		return result;

	for (i = 0; i < length; i++) {
		/* for the last byte TWSI_CTL_AAK must not be set */
		if (i + 1 == length)
			final_read = true;

		/* clear iflg to allow next event */
		if (final_read)
			octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB);
		else
			octeon_i2c_ctl_write(i2c, TWSI_CTL_ENAB | TWSI_CTL_AAK);

		result = octeon_i2c_wait(i2c);
		if (result)
			return result;

		data[i] = octeon_i2c_data_read(i2c);
		if (recv_len && i == 0) {
			if (data[i] > I2C_SMBUS_BLOCK_MAX + 1) {
				dev_err(i2c->dev,
					"%s: read len > I2C_SMBUS_BLOCK_MAX %d\n",
					__func__, data[i]);
				return -EPROTO;
			}
			length += data[i];
		}

		result = octeon_i2c_check_status(i2c, final_read);
		if (result)
			return result;
	}
	*rlength = length;
	return 0;
}

/**
 * octeon_i2c_xfer - The driver's master_xfer function
 * @adap: Pointer to the i2c_adapter structure
 * @msgs: Pointer to the messages to be processed
 * @num: Length of the MSGS array
 *
 * Returns the number of messages processed, or a negative errno on failure.
 */
static int octeon_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	int i, ret = 0;

	for (i = 0; ret == 0 && i < num; i++) {
		struct i2c_msg *pmsg = &msgs[i];

		ret = octeon_i2c_start(i2c);
		if (ret)
			return ret;

		if (pmsg->flags & I2C_M_RD)
			ret = octeon_i2c_read(i2c, pmsg->addr, pmsg->buf,
					      &pmsg->len, pmsg->flags & I2C_M_RECV_LEN);
		else
			ret = octeon_i2c_write(i2c, pmsg->addr, pmsg->buf,
					       pmsg->len);
	}
	octeon_i2c_stop(i2c);

	return (ret != 0) ? ret : num;
}

static int octeon_i2c_get_scl(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	u64 state;

	state = octeon_i2c_read_int(i2c);
	return state & TWSI_INT_SCL;
}

static void octeon_i2c_set_scl(struct i2c_adapter *adap, int val)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	octeon_i2c_write_int(i2c, TWSI_INT_SCL_OVR);
}

static int octeon_i2c_get_sda(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);
	u64 state;

	state = octeon_i2c_read_int(i2c);
	return state & TWSI_INT_SDA;
}

static void octeon_i2c_prepare_recovery(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	/*
	 * The stop resets the state machine, does not _transmit_ STOP unless
	 * engine was active.
	 */
	octeon_i2c_stop(i2c);

	octeon_i2c_write_int(i2c, 0);
}

static void octeon_i2c_unprepare_recovery(struct i2c_adapter *adap)
{
	struct octeon_i2c *i2c = i2c_get_adapdata(adap);

	octeon_i2c_write_int(i2c, 0);
}

static struct i2c_bus_recovery_info octeon_i2c_recovery_info = {
	.recover_bus = i2c_generic_scl_recovery,
	.get_scl = octeon_i2c_get_scl,
	.set_scl = octeon_i2c_set_scl,
	.get_sda = octeon_i2c_get_sda,
	.prepare_recovery = octeon_i2c_prepare_recovery,
	.unprepare_recovery = octeon_i2c_unprepare_recovery,
};

static u32 octeon_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA | I2C_SMBUS_BLOCK_PROC_CALL;
}

static const struct i2c_algorithm octeon_i2c_algo = {
	.master_xfer = octeon_i2c_xfer,
	.functionality = octeon_i2c_functionality,
};

static struct i2c_adapter octeon_i2c_ops = {
	.owner = THIS_MODULE,
	.name = "OCTEON adapter",
	.algo = &octeon_i2c_algo,
};

static int octeon_i2c_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res_mem;
	struct octeon_i2c *i2c;
	int irq, result = 0;

	/* All adaptors have an irq.  */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c) {
		result = -ENOMEM;
		goto out;
	}
	i2c->dev = &pdev->dev;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->twsi_base = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(i2c->twsi_base)) {
		result = PTR_ERR(i2c->twsi_base);
		goto out;
	}

	/*
	 * "clock-rate" is a legacy binding, the official binding is
	 * "clock-frequency".  Try the official one first and then
	 * fall back if it doesn't exist.
	 */
	if (of_property_read_u32(node, "clock-frequency", &i2c->twsi_freq) &&
	    of_property_read_u32(node, "clock-rate", &i2c->twsi_freq)) {
		dev_err(i2c->dev,
			"no I2C 'clock-rate' or 'clock-frequency' property\n");
		result = -ENXIO;
		goto out;
	}

	i2c->sys_freq = octeon_get_io_clock_rate();

	init_waitqueue_head(&i2c->queue);

	i2c->irq = irq;

	result = devm_request_irq(&pdev->dev, i2c->irq,
				  octeon_i2c_isr, 0, DRV_NAME, i2c);
	if (result < 0) {
		dev_err(i2c->dev, "failed to attach interrupt\n");
		goto out;
	}

	result = octeon_i2c_init_lowlevel(i2c);
	if (result) {
		dev_err(i2c->dev, "init low level failed\n");
		goto  out;
	}

	octeon_i2c_set_clock(i2c);

	i2c->adap = octeon_i2c_ops;
	i2c->adap.timeout = msecs_to_jiffies(2);
	i2c->adap.retries = 5;
	i2c->adap.bus_recovery_info = &octeon_i2c_recovery_info;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.dev.of_node = node;
	i2c_set_adapdata(&i2c->adap, i2c);
	platform_set_drvdata(pdev, i2c);

	result = i2c_add_adapter(&i2c->adap);
	if (result < 0) {
		dev_err(i2c->dev, "failed to add adapter\n");
		goto out;
	}
	dev_info(i2c->dev, "probed\n");
	return 0;

out:
	return result;
};

static int octeon_i2c_remove(struct platform_device *pdev)
{
	struct octeon_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	return 0;
};

static const struct of_device_id octeon_i2c_match[] = {
	{ .compatible = "cavium,octeon-3860-twsi", },
	{},
};
MODULE_DEVICE_TABLE(of, octeon_i2c_match);

static struct platform_driver octeon_i2c_driver = {
	.probe		= octeon_i2c_probe,
	.remove		= octeon_i2c_remove,
	.driver		= {
		.name	= DRV_NAME,
		.of_match_table = octeon_i2c_match,
	},
};

module_platform_driver(octeon_i2c_driver);

MODULE_AUTHOR("Michael Lawnick <michael.lawnick.ext@nsn.com>");
MODULE_DESCRIPTION("I2C-Bus adapter for Cavium OCTEON processors");
MODULE_LICENSE("GPL");
