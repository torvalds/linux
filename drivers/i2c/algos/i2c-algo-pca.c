// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  i2c-algo-pca.c i2c driver algorithms for PCA9564 adapters
 *    Copyright (C) 2004 Arcom Control Systems
 *    Copyright (C) 2008 Pengutronix
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-pca.h>

#define DEB1(fmt, args...) do { if (i2c_debug >= 1)			\
				 printk(KERN_DEBUG fmt, ## args); } while (0)
#define DEB2(fmt, args...) do { if (i2c_debug >= 2)			\
				 printk(KERN_DEBUG fmt, ## args); } while (0)
#define DEB3(fmt, args...) do { if (i2c_debug >= 3)			\
				 printk(KERN_DEBUG fmt, ## args); } while (0)

static int i2c_debug;

#define pca_outw(adap, reg, val) adap->write_byte(adap->data, reg, val)
#define pca_inw(adap, reg) adap->read_byte(adap->data, reg)

#define pca_status(adap) pca_inw(adap, I2C_PCA_STA)
#define pca_clock(adap) adap->i2c_clock
#define pca_set_con(adap, val) pca_outw(adap, I2C_PCA_CON, val)
#define pca_get_con(adap) pca_inw(adap, I2C_PCA_CON)
#define pca_wait(adap) adap->wait_for_completion(adap->data)

static void pca_reset(struct i2c_algo_pca_data *adap)
{
	if (adap->chip == I2C_PCA_CHIP_9665) {
		/* Ignore the reset function from the module,
		 * we can use the parallel bus reset.
		 */
		pca_outw(adap, I2C_PCA_INDPTR, I2C_PCA_IPRESET);
		pca_outw(adap, I2C_PCA_IND, 0xA5);
		pca_outw(adap, I2C_PCA_IND, 0x5A);
	} else {
		adap->reset_chip(adap->data);
	}
}

/*
 * Generate a start condition on the i2c bus.
 *
 * returns after the start condition has occurred
 */
static int pca_start(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== START\n");
	sta |= I2C_PCA_CON_STA;
	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
	return pca_wait(adap);
}

/*
 * Generate a repeated start condition on the i2c bus
 *
 * return after the repeated start condition has occurred
 */
static int pca_repeated_start(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== REPEATED START\n");
	sta |= I2C_PCA_CON_STA;
	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
	return pca_wait(adap);
}

/*
 * Generate a stop condition on the i2c bus
 *
 * returns after the stop condition has been generated
 *
 * STOPs do not generate an interrupt or set the SI flag, since the
 * part returns the idle state (0xf8). Hence we don't need to
 * pca_wait here.
 */
static void pca_stop(struct i2c_algo_pca_data *adap)
{
	int sta = pca_get_con(adap);
	DEB2("=== STOP\n");
	sta |= I2C_PCA_CON_STO;
	sta &= ~(I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);
}

/*
 * Send the slave address and R/W bit
 *
 * returns after the address has been sent
 */
static int pca_address(struct i2c_algo_pca_data *adap,
		       struct i2c_msg *msg)
{
	int sta = pca_get_con(adap);
	int addr = i2c_8bit_addr_from_msg(msg);

	DEB2("=== SLAVE ADDRESS %#04x+%c=%#04x\n",
	     msg->addr, msg->flags & I2C_M_RD ? 'R' : 'W', addr);

	pca_outw(adap, I2C_PCA_DAT, addr);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);

	return pca_wait(adap);
}

/*
 * Transmit a byte.
 *
 * Returns after the byte has been transmitted
 */
static int pca_tx_byte(struct i2c_algo_pca_data *adap,
		       __u8 b)
{
	int sta = pca_get_con(adap);
	DEB2("=== WRITE %#04x\n", b);
	pca_outw(adap, I2C_PCA_DAT, b);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI);
	pca_set_con(adap, sta);

	return pca_wait(adap);
}

/*
 * Receive a byte
 *
 * returns immediately.
 */
static void pca_rx_byte(struct i2c_algo_pca_data *adap,
			__u8 *b, int ack)
{
	*b = pca_inw(adap, I2C_PCA_DAT);
	DEB2("=== READ %#04x %s\n", *b, ack ? "ACK" : "NACK");
}

/*
 * Setup ACK or NACK for next received byte and wait for it to arrive.
 *
 * Returns after next byte has arrived.
 */
static int pca_rx_ack(struct i2c_algo_pca_data *adap,
		      int ack)
{
	int sta = pca_get_con(adap);

	sta &= ~(I2C_PCA_CON_STO|I2C_PCA_CON_STA|I2C_PCA_CON_SI|I2C_PCA_CON_AA);

	if (ack)
		sta |= I2C_PCA_CON_AA;

	pca_set_con(adap, sta);
	return pca_wait(adap);
}

static int pca_xfer(struct i2c_adapter *i2c_adap,
		    struct i2c_msg *msgs,
		    int num)
{
	struct i2c_algo_pca_data *adap = i2c_adap->algo_data;
	struct i2c_msg *msg = NULL;
	int curmsg;
	int numbytes = 0;
	int state;
	int ret;
	int completed = 1;
	unsigned long timeout = jiffies + i2c_adap->timeout;

	while ((state = pca_status(adap)) != 0xf8) {
		if (time_before(jiffies, timeout)) {
			msleep(10);
		} else {
			dev_dbg(&i2c_adap->dev, "bus is not idle. status is "
				"%#04x\n", state);
			return -EBUSY;
		}
	}

	DEB1("{{{ XFER %d messages\n", num);

	if (i2c_debug >= 2) {
		for (curmsg = 0; curmsg < num; curmsg++) {
			int addr, i;
			msg = &msgs[curmsg];

			addr = (0x7f & msg->addr) ;

			if (msg->flags & I2C_M_RD)
				printk(KERN_INFO "    [%02d] RD %d bytes from %#02x [%#02x, ...]\n",
				       curmsg, msg->len, addr, (addr << 1) | 1);
			else {
				printk(KERN_INFO "    [%02d] WR %d bytes to %#02x [%#02x%s",
				       curmsg, msg->len, addr, addr << 1,
				       msg->len == 0 ? "" : ", ");
				for (i = 0; i < msg->len; i++)
					printk("%#04x%s", msg->buf[i], i == msg->len - 1 ? "" : ", ");
				printk("]\n");
			}
		}
	}

	curmsg = 0;
	ret = -EIO;
	while (curmsg < num) {
		state = pca_status(adap);

		DEB3("STATE is 0x%02x\n", state);
		msg = &msgs[curmsg];

		switch (state) {
		case 0xf8: /* On reset or stop the bus is idle */
			completed = pca_start(adap);
			break;

		case 0x08: /* A START condition has been transmitted */
		case 0x10: /* A repeated start condition has been transmitted */
			completed = pca_address(adap, msg);
			break;

		case 0x18: /* SLA+W has been transmitted; ACK has been received */
		case 0x28: /* Data byte in I2CDAT has been transmitted; ACK has been received */
			if (numbytes < msg->len) {
				completed = pca_tx_byte(adap,
							msg->buf[numbytes]);
				numbytes++;
				break;
			}
			curmsg++; numbytes = 0;
			if (curmsg == num)
				pca_stop(adap);
			else
				completed = pca_repeated_start(adap);
			break;

		case 0x20: /* SLA+W has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after SLA+W\n");
			pca_stop(adap);
			ret = -ENXIO;
			goto out;

		case 0x40: /* SLA+R has been transmitted; ACK has been received */
			completed = pca_rx_ack(adap, msg->len > 1);
			break;

		case 0x50: /* Data bytes has been received; ACK has been returned */
			if (numbytes < msg->len) {
				pca_rx_byte(adap, &msg->buf[numbytes], 1);
				numbytes++;
				completed = pca_rx_ack(adap,
						       numbytes < msg->len - 1);
				break;
			}
			curmsg++; numbytes = 0;
			if (curmsg == num)
				pca_stop(adap);
			else
				completed = pca_repeated_start(adap);
			break;

		case 0x48: /* SLA+R has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after SLA+R\n");
			pca_stop(adap);
			ret = -ENXIO;
			goto out;

		case 0x30: /* Data byte in I2CDAT has been transmitted; NOT ACK has been received */
			DEB2("NOT ACK received after data byte\n");
			pca_stop(adap);
			goto out;

		case 0x38: /* Arbitration lost during SLA+W, SLA+R or data bytes */
			DEB2("Arbitration lost\n");
			/*
			 * The PCA9564 data sheet (2006-09-01) says "A
			 * START condition will be transmitted when the
			 * bus becomes free (STOP or SCL and SDA high)"
			 * when the STA bit is set (p. 11).
			 *
			 * In case this won't work, try pca_reset()
			 * instead.
			 */
			pca_start(adap);
			goto out;

		case 0x58: /* Data byte has been received; NOT ACK has been returned */
			if (numbytes == msg->len - 1) {
				pca_rx_byte(adap, &msg->buf[numbytes], 0);
				curmsg++; numbytes = 0;
				if (curmsg == num)
					pca_stop(adap);
				else
					completed = pca_repeated_start(adap);
			} else {
				DEB2("NOT ACK sent after data byte received. "
				     "Not final byte. numbytes %d. len %d\n",
				     numbytes, msg->len);
				pca_stop(adap);
				goto out;
			}
			break;
		case 0x70: /* Bus error - SDA stuck low */
			DEB2("BUS ERROR - SDA Stuck low\n");
			pca_reset(adap);
			goto out;
		case 0x90: /* Bus error - SCL stuck low */
			DEB2("BUS ERROR - SCL Stuck low\n");
			pca_reset(adap);
			goto out;
		case 0x00: /* Bus error during master or slave mode due to illegal START or STOP condition */
			DEB2("BUS ERROR - Illegal START or STOP\n");
			pca_reset(adap);
			goto out;
		default:
			dev_err(&i2c_adap->dev, "unhandled SIO state 0x%02x\n", state);
			break;
		}

		if (!completed)
			goto out;
	}

	ret = curmsg;
 out:
	DEB1("}}} transferred %d/%d messages. "
	     "status is %#04x. control is %#04x\n",
	     curmsg, num, pca_status(adap),
	     pca_get_con(adap));
	return ret;
}

static u32 pca_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm pca_algo = {
	.master_xfer	= pca_xfer,
	.functionality	= pca_func,
};

static unsigned int pca_probe_chip(struct i2c_adapter *adap)
{
	struct i2c_algo_pca_data *pca_data = adap->algo_data;
	/* The trick here is to check if there is an indirect register
	 * available. If there is one, we will read the value we first
	 * wrote on I2C_PCA_IADR. Otherwise, we will read the last value
	 * we wrote on I2C_PCA_ADR
	 */
	pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_IADR);
	pca_outw(pca_data, I2C_PCA_IND, 0xAA);
	pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_ITO);
	pca_outw(pca_data, I2C_PCA_IND, 0x00);
	pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_IADR);
	if (pca_inw(pca_data, I2C_PCA_IND) == 0xAA) {
		printk(KERN_INFO "%s: PCA9665 detected.\n", adap->name);
		pca_data->chip = I2C_PCA_CHIP_9665;
	} else {
		printk(KERN_INFO "%s: PCA9564 detected.\n", adap->name);
		pca_data->chip = I2C_PCA_CHIP_9564;
	}
	return pca_data->chip;
}

static int pca_init(struct i2c_adapter *adap)
{
	struct i2c_algo_pca_data *pca_data = adap->algo_data;

	adap->algo = &pca_algo;

	if (pca_probe_chip(adap) == I2C_PCA_CHIP_9564) {
		static int freqs[] = {330, 288, 217, 146, 88, 59, 44, 36};
		int clock;

		if (pca_data->i2c_clock > 7) {
			switch (pca_data->i2c_clock) {
			case 330000:
				pca_data->i2c_clock = I2C_PCA_CON_330kHz;
				break;
			case 288000:
				pca_data->i2c_clock = I2C_PCA_CON_288kHz;
				break;
			case 217000:
				pca_data->i2c_clock = I2C_PCA_CON_217kHz;
				break;
			case 146000:
				pca_data->i2c_clock = I2C_PCA_CON_146kHz;
				break;
			case 88000:
				pca_data->i2c_clock = I2C_PCA_CON_88kHz;
				break;
			case 59000:
				pca_data->i2c_clock = I2C_PCA_CON_59kHz;
				break;
			case 44000:
				pca_data->i2c_clock = I2C_PCA_CON_44kHz;
				break;
			case 36000:
				pca_data->i2c_clock = I2C_PCA_CON_36kHz;
				break;
			default:
				printk(KERN_WARNING
					"%s: Invalid I2C clock speed selected."
					" Using default 59kHz.\n", adap->name);
			pca_data->i2c_clock = I2C_PCA_CON_59kHz;
			}
		} else {
			printk(KERN_WARNING "%s: "
				"Choosing the clock frequency based on "
				"index is deprecated."
				" Use the nominal frequency.\n", adap->name);
		}

		pca_reset(pca_data);

		clock = pca_clock(pca_data);
		printk(KERN_INFO "%s: Clock frequency is %dkHz\n",
		     adap->name, freqs[clock]);

		pca_set_con(pca_data, I2C_PCA_CON_ENSIO | clock);
	} else {
		int clock;
		int mode;
		int tlow, thi;
		/* Values can be found on PCA9665 datasheet section 7.3.2.6 */
		int min_tlow, min_thi;
		/* These values are the maximum raise and fall values allowed
		 * by the I2C operation mode (Standard, Fast or Fast+)
		 * They are used (added) below to calculate the clock dividers
		 * of PCA9665. Note that they are slightly different of the
		 * real maximum, to allow the change on mode exactly on the
		 * maximum clock rate for each mode
		 */
		int raise_fall_time;

		if (pca_data->i2c_clock > 1265800) {
			printk(KERN_WARNING "%s: I2C clock speed too high."
				" Using 1265.8kHz.\n", adap->name);
			pca_data->i2c_clock = 1265800;
		}

		if (pca_data->i2c_clock < 60300) {
			printk(KERN_WARNING "%s: I2C clock speed too low."
				" Using 60.3kHz.\n", adap->name);
			pca_data->i2c_clock = 60300;
		}

		/* To avoid integer overflow, use clock/100 for calculations */
		clock = pca_clock(pca_data) / 100;

		if (pca_data->i2c_clock > I2C_MAX_FAST_MODE_PLUS_FREQ) {
			mode = I2C_PCA_MODE_TURBO;
			min_tlow = 14;
			min_thi  = 5;
			raise_fall_time = 22; /* Raise 11e-8s, Fall 11e-8s */
		} else if (pca_data->i2c_clock > I2C_MAX_FAST_MODE_FREQ) {
			mode = I2C_PCA_MODE_FASTP;
			min_tlow = 17;
			min_thi  = 9;
			raise_fall_time = 22; /* Raise 11e-8s, Fall 11e-8s */
		} else if (pca_data->i2c_clock > I2C_MAX_STANDARD_MODE_FREQ) {
			mode = I2C_PCA_MODE_FAST;
			min_tlow = 44;
			min_thi  = 20;
			raise_fall_time = 58; /* Raise 29e-8s, Fall 29e-8s */
		} else {
			mode = I2C_PCA_MODE_STD;
			min_tlow = 157;
			min_thi  = 134;
			raise_fall_time = 127; /* Raise 29e-8s, Fall 98e-8s */
		}

		/* The minimum clock that respects the thi/tlow = 134/157 is
		 * 64800 Hz. Below that, we have to fix the tlow to 255 and
		 * calculate the thi factor.
		 */
		if (clock < 648) {
			tlow = 255;
			thi = 1000000 - clock * raise_fall_time;
			thi /= (I2C_PCA_OSC_PER * clock) - tlow;
		} else {
			tlow = (1000000 - clock * raise_fall_time) * min_tlow;
			tlow /= I2C_PCA_OSC_PER * clock * (min_thi + min_tlow);
			thi = tlow * min_thi / min_tlow;
		}

		pca_reset(pca_data);

		printk(KERN_INFO
		     "%s: Clock frequency is %dHz\n", adap->name, clock * 100);

		pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_IMODE);
		pca_outw(pca_data, I2C_PCA_IND, mode);
		pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_ISCLL);
		pca_outw(pca_data, I2C_PCA_IND, tlow);
		pca_outw(pca_data, I2C_PCA_INDPTR, I2C_PCA_ISCLH);
		pca_outw(pca_data, I2C_PCA_IND, thi);

		pca_set_con(pca_data, I2C_PCA_CON_ENSIO);
	}
	udelay(500); /* 500 us for oscillator to stabilise */

	return 0;
}

/*
 * registering functions to load algorithms at runtime
 */
int i2c_pca_add_bus(struct i2c_adapter *adap)
{
	int rval;

	rval = pca_init(adap);
	if (rval)
		return rval;

	return i2c_add_adapter(adap);
}
EXPORT_SYMBOL(i2c_pca_add_bus);

int i2c_pca_add_numbered_bus(struct i2c_adapter *adap)
{
	int rval;

	rval = pca_init(adap);
	if (rval)
		return rval;

	return i2c_add_numbered_adapter(adap);
}
EXPORT_SYMBOL(i2c_pca_add_numbered_bus);

MODULE_AUTHOR("Ian Campbell <icampbell@arcom.com>, "
	"Wolfram Sang <kernel@pengutronix.de>");
MODULE_DESCRIPTION("I2C-Bus PCA9564/PCA9665 algorithm");
MODULE_LICENSE("GPL");

module_param(i2c_debug, int, 0);
