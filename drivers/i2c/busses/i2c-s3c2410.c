/* linux/drivers/i2c/busses/i2c-s3c2410.c
 *
 * Copyright (C) 2004,2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 I2C Controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/arch/regs-gpio.h>
#include <asm/plat-s3c/regs-iic.h>
#include <asm/plat-s3c/iic.h>

/* i2c controller state */

enum s3c24xx_i2c_state {
	STATE_IDLE,
	STATE_START,
	STATE_READ,
	STATE_WRITE,
	STATE_STOP
};

struct s3c24xx_i2c {
	spinlock_t		lock;
	wait_queue_head_t	wait;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;

	unsigned int		tx_setup;

	enum s3c24xx_i2c_state	state;

	void __iomem		*regs;
	struct clk		*clk;
	struct device		*dev;
	struct resource		*irq;
	struct resource		*ioarea;
	struct i2c_adapter	adap;
};

/* default platform data to use if not supplied in the platform_device
*/

static struct s3c2410_platform_i2c s3c24xx_i2c_default_platform = {
	.flags		= 0,
	.slave_addr	= 0x10,
	.bus_freq	= 100*1000,
	.max_freq	= 400*1000,
	.sda_delay	= S3C2410_IICLC_SDA_DELAY5 | S3C2410_IICLC_FILTER_ON,
};

/* s3c24xx_i2c_is2440()
 *
 * return true is this is an s3c2440
*/

static inline int s3c24xx_i2c_is2440(struct s3c24xx_i2c *i2c)
{
	struct platform_device *pdev = to_platform_device(i2c->dev);

	return !strcmp(pdev->name, "s3c2440-i2c");
}


/* s3c24xx_i2c_get_platformdata
 *
 * get the platform data associated with the given device, or return
 * the default if there is none
*/

static inline struct s3c2410_platform_i2c *s3c24xx_i2c_get_platformdata(struct device *dev)
{
	if (dev->platform_data != NULL)
		return (struct s3c2410_platform_i2c *)dev->platform_data;

	return &s3c24xx_i2c_default_platform;
}

/* s3c24xx_i2c_master_complete
 *
 * complete the message and wake up the caller, using the given return code,
 * or zero to mean ok.
*/

static inline void s3c24xx_i2c_master_complete(struct s3c24xx_i2c *i2c, int ret)
{
	dev_dbg(i2c->dev, "master_complete %d\n", ret);

	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx ++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;

	wake_up(&i2c->wait);
}

static inline void s3c24xx_i2c_disable_ack(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;
	
	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp & ~S3C2410_IICCON_ACKEN, i2c->regs + S3C2410_IICCON);

}

static inline void s3c24xx_i2c_enable_ack(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;
	
	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp | S3C2410_IICCON_ACKEN, i2c->regs + S3C2410_IICCON);

}

/* irq enable/disable functions */

static inline void s3c24xx_i2c_disable_irq(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;
	
	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp & ~S3C2410_IICCON_IRQEN, i2c->regs + S3C2410_IICCON);
}

static inline void s3c24xx_i2c_enable_irq(struct s3c24xx_i2c *i2c)
{
	unsigned long tmp;
	
	tmp = readl(i2c->regs + S3C2410_IICCON);
	writel(tmp | S3C2410_IICCON_IRQEN, i2c->regs + S3C2410_IICCON);
}


/* s3c24xx_i2c_message_start
 *
 * put the start of a message onto the bus 
*/

static void s3c24xx_i2c_message_start(struct s3c24xx_i2c *i2c, 
				      struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;
	unsigned long stat;
	unsigned long iiccon;

	stat = 0;
	stat |=  S3C2410_IICSTAT_TXRXEN;

	if (msg->flags & I2C_M_RD) {
		stat |= S3C2410_IICSTAT_MASTER_RX;
		addr |= 1;
	} else
		stat |= S3C2410_IICSTAT_MASTER_TX;

	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	// todo - check for wether ack wanted or not
	s3c24xx_i2c_enable_ack(i2c);

	iiccon = readl(i2c->regs + S3C2410_IICCON);
	writel(stat, i2c->regs + S3C2410_IICSTAT);
	
	dev_dbg(i2c->dev, "START: %08lx to IICSTAT, %02x to DS\n", stat, addr);
	writeb(addr, i2c->regs + S3C2410_IICDS);
	
	/* delay here to ensure the data byte has gotten onto the bus
	 * before the transaction is started */

	ndelay(i2c->tx_setup);

	dev_dbg(i2c->dev, "iiccon, %08lx\n", iiccon);
	writel(iiccon, i2c->regs + S3C2410_IICCON);
	
	stat |=  S3C2410_IICSTAT_START;
	writel(stat, i2c->regs + S3C2410_IICSTAT);
}

static inline void s3c24xx_i2c_stop(struct s3c24xx_i2c *i2c, int ret)
{
	unsigned long iicstat = readl(i2c->regs + S3C2410_IICSTAT);

	dev_dbg(i2c->dev, "STOP\n");

	/* stop the transfer */
	iicstat &= ~ S3C2410_IICSTAT_START;
	writel(iicstat, i2c->regs + S3C2410_IICSTAT);
	
	i2c->state = STATE_STOP;
	
	s3c24xx_i2c_master_complete(i2c, ret);
	s3c24xx_i2c_disable_irq(i2c);
}

/* helper functions to determine the current state in the set of
 * messages we are sending */

/* is_lastmsg()
 *
 * returns TRUE if the current message is the last in the set 
*/

static inline int is_lastmsg(struct s3c24xx_i2c *i2c)
{
	return i2c->msg_idx >= (i2c->msg_num - 1);
}

/* is_msglast
 *
 * returns TRUE if we this is the last byte in the current message
*/

static inline int is_msglast(struct s3c24xx_i2c *i2c)
{
	return i2c->msg_ptr == i2c->msg->len-1;
}

/* is_msgend
 *
 * returns TRUE if we reached the end of the current message
*/

static inline int is_msgend(struct s3c24xx_i2c *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

/* i2s_s3c_irq_nextbyte
 *
 * process an interrupt and work out what to do
 */

static int i2s_s3c_irq_nextbyte(struct s3c24xx_i2c *i2c, unsigned long iicstat)
{
	unsigned long tmp;
	unsigned char byte;
	int ret = 0;

	switch (i2c->state) {

	case STATE_IDLE:
		dev_err(i2c->dev, "%s: called in STATE_IDLE\n", __FUNCTION__);
		goto out;
		break;

	case STATE_STOP:
		dev_err(i2c->dev, "%s: called in STATE_STOP\n", __FUNCTION__);
		s3c24xx_i2c_disable_irq(i2c);		
		goto out_ack;

	case STATE_START:
		/* last thing we did was send a start condition on the
		 * bus, or started a new i2c message
		 */
		
		if (iicstat  & S3C2410_IICSTAT_LASTBIT &&
		    !(i2c->msg->flags & I2C_M_IGNORE_NAK)) {
			/* ack was not received... */

			dev_dbg(i2c->dev, "ack was not received\n");
			s3c24xx_i2c_stop(i2c, -EREMOTEIO);
			goto out_ack;
		}

		if (i2c->msg->flags & I2C_M_RD)
			i2c->state = STATE_READ;
		else
			i2c->state = STATE_WRITE;

		/* terminate the transfer if there is nothing to do
		 * (used by the i2c probe to find devices */

		if (is_lastmsg(i2c) && i2c->msg->len == 0) {
			s3c24xx_i2c_stop(i2c, 0);
			goto out_ack;
		}

		if (i2c->state == STATE_READ)
			goto prepare_read;

		/* fall through to the write state, as we will need to 
		 * send a byte as well */

	case STATE_WRITE:
		/* we are writing data to the device... check for the
		 * end of the message, and if so, work out what to do
		 */

	retry_write:
		if (!is_msgend(i2c)) {
			byte = i2c->msg->buf[i2c->msg_ptr++];
			writeb(byte, i2c->regs + S3C2410_IICDS);

			/* delay after writing the byte to allow the
			 * data setup time on the bus, as writing the
			 * data to the register causes the first bit
			 * to appear on SDA, and SCL will change as
			 * soon as the interrupt is acknowledged */

			ndelay(i2c->tx_setup);

		} else if (!is_lastmsg(i2c)) {
			/* we need to go to the next i2c message */

			dev_dbg(i2c->dev, "WRITE: Next Message\n");

			i2c->msg_ptr = 0;
			i2c->msg_idx ++;
			i2c->msg++;
			
			/* check to see if we need to do another message */
			if (i2c->msg->flags & I2C_M_NOSTART) {

				if (i2c->msg->flags & I2C_M_RD) {
					/* cannot do this, the controller
					 * forces us to send a new START
					 * when we change direction */

					s3c24xx_i2c_stop(i2c, -EINVAL);
				}

				goto retry_write;
			} else {
			
				/* send the new start */
				s3c24xx_i2c_message_start(i2c, i2c->msg);
				i2c->state = STATE_START;
			}

		} else {
			/* send stop */

			s3c24xx_i2c_stop(i2c, 0);
		}
		break;

	case STATE_READ:
		/* we have a byte of data in the data register, do 
		 * something with it, and then work out wether we are
		 * going to do any more read/write
		 */

		if (!(i2c->msg->flags & I2C_M_IGNORE_NAK) &&
		    !(is_msglast(i2c) && is_lastmsg(i2c))) {

			if (iicstat & S3C2410_IICSTAT_LASTBIT) {
				dev_dbg(i2c->dev, "READ: No Ack\n");

				s3c24xx_i2c_stop(i2c, -ECONNREFUSED);
				goto out_ack;
			}
		}

		byte = readb(i2c->regs + S3C2410_IICDS);
		i2c->msg->buf[i2c->msg_ptr++] = byte;

	prepare_read:
		if (is_msglast(i2c)) {
			/* last byte of buffer */

			if (is_lastmsg(i2c))
				s3c24xx_i2c_disable_ack(i2c);
			
		} else if (is_msgend(i2c)) {
			/* ok, we've read the entire buffer, see if there
			 * is anything else we need to do */

			if (is_lastmsg(i2c)) {
				/* last message, send stop and complete */
				dev_dbg(i2c->dev, "READ: Send Stop\n");

				s3c24xx_i2c_stop(i2c, 0);
			} else {
				/* go to the next transfer */
				dev_dbg(i2c->dev, "READ: Next Transfer\n");

				i2c->msg_ptr = 0;
				i2c->msg_idx++;
				i2c->msg++;
			}
		}

		break;
	}

	/* acknowlegde the IRQ and get back on with the work */

 out_ack:
	tmp = readl(i2c->regs + S3C2410_IICCON);	
	tmp &= ~S3C2410_IICCON_IRQPEND;
	writel(tmp, i2c->regs + S3C2410_IICCON);
 out:
	return ret;
}

/* s3c24xx_i2c_irq
 *
 * top level IRQ servicing routine
*/

static irqreturn_t s3c24xx_i2c_irq(int irqno, void *dev_id)
{
	struct s3c24xx_i2c *i2c = dev_id;
	unsigned long status;
	unsigned long tmp;

	status = readl(i2c->regs + S3C2410_IICSTAT);

	if (status & S3C2410_IICSTAT_ARBITR) {
		// deal with arbitration loss
		dev_err(i2c->dev, "deal with arbitration loss\n");
	}

	if (i2c->state == STATE_IDLE) {
		dev_dbg(i2c->dev, "IRQ: error i2c->state == IDLE\n");

		tmp = readl(i2c->regs + S3C2410_IICCON);	
		tmp &= ~S3C2410_IICCON_IRQPEND;
		writel(tmp, i2c->regs +  S3C2410_IICCON);
		goto out;
	}
	
	/* pretty much this leaves us with the fact that we've
	 * transmitted or received whatever byte we last sent */

	i2s_s3c_irq_nextbyte(i2c, status);

 out:
	return IRQ_HANDLED;
}


/* s3c24xx_i2c_set_master
 *
 * get the i2c bus for a master transaction
*/

static int s3c24xx_i2c_set_master(struct s3c24xx_i2c *i2c)
{
	unsigned long iicstat;
	int timeout = 400;

	while (timeout-- > 0) {
		iicstat = readl(i2c->regs + S3C2410_IICSTAT);
		
		if (!(iicstat & S3C2410_IICSTAT_BUSBUSY))
			return 0;

		msleep(1);
	}

	dev_dbg(i2c->dev, "timeout: GPEDAT is %08x\n",
		__raw_readl(S3C2410_GPEDAT));

	return -ETIMEDOUT;
}

/* s3c24xx_i2c_doxfer
 *
 * this starts an i2c transfer
*/

static int s3c24xx_i2c_doxfer(struct s3c24xx_i2c *i2c, struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	int ret;

	ret = s3c24xx_i2c_set_master(i2c);
	if (ret != 0) {
		dev_err(i2c->dev, "cannot get bus (error %d)\n", ret);
		ret = -EAGAIN;
		goto out;
	}

	spin_lock_irq(&i2c->lock);

	i2c->msg     = msgs;
	i2c->msg_num = num;
	i2c->msg_ptr = 0;
	i2c->msg_idx = 0;
	i2c->state   = STATE_START;

	s3c24xx_i2c_enable_irq(i2c);
	s3c24xx_i2c_message_start(i2c, msgs);
	spin_unlock_irq(&i2c->lock);
	
	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, HZ * 5);

	ret = i2c->msg_idx;

	/* having these next two as dev_err() makes life very 
	 * noisy when doing an i2cdetect */

	if (timeout == 0)
		dev_dbg(i2c->dev, "timeout\n");
	else if (ret != num)
		dev_dbg(i2c->dev, "incomplete xfer (%d)\n", ret);

	/* ensure the stop has been through the bus */

	msleep(1);

 out:
	return ret;
}

/* s3c24xx_i2c_xfer
 *
 * first port of call from the i2c bus code when an message needs
 * transferring across the i2c bus.
*/

static int s3c24xx_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct s3c24xx_i2c *i2c = (struct s3c24xx_i2c *)adap->algo_data;
	int retry;
	int ret;

	for (retry = 0; retry < adap->retries; retry++) {

		ret = s3c24xx_i2c_doxfer(i2c, msgs, num);

		if (ret != -EAGAIN)
			return ret;

		dev_dbg(i2c->dev, "Retrying transmission (%d)\n", retry);

		udelay(100);
	}

	return -EREMOTEIO;
}

/* declare our i2c functionality */
static u32 s3c24xx_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_PROTOCOL_MANGLING;
}

/* i2c bus registration info */

static const struct i2c_algorithm s3c24xx_i2c_algorithm = {
	.master_xfer		= s3c24xx_i2c_xfer,
	.functionality		= s3c24xx_i2c_func,
};

static struct s3c24xx_i2c s3c24xx_i2c = {
	.lock		= __SPIN_LOCK_UNLOCKED(s3c24xx_i2c.lock),
	.wait		= __WAIT_QUEUE_HEAD_INITIALIZER(s3c24xx_i2c.wait),
	.tx_setup	= 50,
	.adap		= {
		.name			= "s3c2410-i2c",
		.owner			= THIS_MODULE,
		.algo			= &s3c24xx_i2c_algorithm,
		.retries		= 2,
		.class			= I2C_CLASS_HWMON,
	},
};

/* s3c24xx_i2c_calcdivisor
 *
 * return the divisor settings for a given frequency
*/

static int s3c24xx_i2c_calcdivisor(unsigned long clkin, unsigned int wanted,
				   unsigned int *div1, unsigned int *divs)
{
	unsigned int calc_divs = clkin / wanted;
	unsigned int calc_div1;

	if (calc_divs > (16*16))
		calc_div1 = 512;
	else
		calc_div1 = 16;

	calc_divs += calc_div1-1;
	calc_divs /= calc_div1;

	if (calc_divs == 0)
		calc_divs = 1;
	if (calc_divs > 17)
		calc_divs = 17;

	*divs = calc_divs;
	*div1 = calc_div1;

	return clkin / (calc_divs * calc_div1);
}

/* freq_acceptable
 *
 * test wether a frequency is within the acceptable range of error
*/

static inline int freq_acceptable(unsigned int freq, unsigned int wanted)
{
	int diff = freq - wanted;

	return (diff >= -2 && diff <= 2);
}

/* s3c24xx_i2c_getdivisor
 *
 * work out a divisor for the user requested frequency setting,
 * either by the requested frequency, or scanning the acceptable
 * range of frequencies until something is found
*/

static int s3c24xx_i2c_getdivisor(struct s3c24xx_i2c *i2c,
				  struct s3c2410_platform_i2c *pdata,
				  unsigned long *iicon,
				  unsigned int *got)
{
	unsigned long clkin = clk_get_rate(i2c->clk);
	
	unsigned int divs, div1;
	int freq;
	int start, end;

	clkin /= 1000;		/* clkin now in KHz */
     
	dev_dbg(i2c->dev,  "pdata %p, freq %lu %lu..%lu\n",
		 pdata, pdata->bus_freq, pdata->min_freq, pdata->max_freq);

	if (pdata->bus_freq != 0) {
		freq = s3c24xx_i2c_calcdivisor(clkin, pdata->bus_freq/1000,
					       &div1, &divs);
		if (freq_acceptable(freq, pdata->bus_freq/1000))
			goto found;
	}

	/* ok, we may have to search for something suitable... */

	start = (pdata->max_freq == 0) ? pdata->bus_freq : pdata->max_freq;
	end = pdata->min_freq;

	start /= 1000;
	end /= 1000;

	/* search loop... */

	for (; start > end; start--) {
		freq = s3c24xx_i2c_calcdivisor(clkin, start, &div1, &divs);
		if (freq_acceptable(freq, start))
			goto found;
	}

	/* cannot find frequency spec */

	return -EINVAL;

 found:
	*got = freq;
	*iicon |= (divs-1);
	*iicon |= (div1 == 512) ? S3C2410_IICCON_TXDIV_512 : 0;
	return 0;
}

/* s3c24xx_i2c_init
 *
 * initialise the controller, set the IO lines and frequency 
*/

static int s3c24xx_i2c_init(struct s3c24xx_i2c *i2c)
{
	unsigned long iicon = S3C2410_IICCON_IRQEN | S3C2410_IICCON_ACKEN;
	struct s3c2410_platform_i2c *pdata;
	unsigned int freq;

	/* get the plafrom data */

	pdata = s3c24xx_i2c_get_platformdata(i2c->adap.dev.parent);

	/* inititalise the gpio */

	s3c2410_gpio_cfgpin(S3C2410_GPE15, S3C2410_GPE15_IICSDA);
	s3c2410_gpio_cfgpin(S3C2410_GPE14, S3C2410_GPE14_IICSCL);

	/* write slave address */
	
	writeb(pdata->slave_addr, i2c->regs + S3C2410_IICADD);

	dev_info(i2c->dev, "slave address 0x%02x\n", pdata->slave_addr);

	/* we need to work out the divisors for the clock... */

	if (s3c24xx_i2c_getdivisor(i2c, pdata, &iicon, &freq) != 0) {
		dev_err(i2c->dev, "cannot meet bus frequency required\n");
		return -EINVAL;
	}

	/* todo - check that the i2c lines aren't being dragged anywhere */

	dev_info(i2c->dev, "bus frequency set to %d KHz\n", freq);
	dev_dbg(i2c->dev, "S3C2410_IICCON=0x%02lx\n", iicon);
	
	writel(iicon, i2c->regs + S3C2410_IICCON);

	/* check for s3c2440 i2c controller  */

	if (s3c24xx_i2c_is2440(i2c)) {
		dev_dbg(i2c->dev, "S3C2440_IICLC=%08x\n", pdata->sda_delay);

		writel(pdata->sda_delay, i2c->regs + S3C2440_IICLC);
	}

	return 0;
}

/* s3c24xx_i2c_probe
 *
 * called by the bus driver when a suitable device is found
*/

static int s3c24xx_i2c_probe(struct platform_device *pdev)
{
	struct s3c24xx_i2c *i2c = &s3c24xx_i2c;
	struct resource *res;
	int ret;

	/* find the clock and enable it */

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	clk_enable(i2c->clk);

	/* map the registers */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}

	i2c->ioarea = request_mem_region(res->start, (res->end-res->start)+1,
					 pdev->name);

	if (i2c->ioarea == NULL) {
		dev_err(&pdev->dev, "cannot request IO\n");
		ret = -ENXIO;
		goto err_clk;
	}

	i2c->regs = ioremap(res->start, (res->end-res->start)+1);

	if (i2c->regs == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}

	dev_dbg(&pdev->dev, "registers %p (%p, %p)\n", i2c->regs, i2c->ioarea, res);

	/* setup info block for the i2c core */

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	/* initialise the i2c controller */

	ret = s3c24xx_i2c_init(i2c);
	if (ret != 0)
		goto err_iomap;

	/* find the IRQ for this unit (note, this relies on the init call to
	 * ensure no current IRQs pending 
	 */

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		ret = -ENOENT;
		goto err_iomap;
	}

	ret = request_irq(res->start, s3c24xx_i2c_irq, IRQF_DISABLED,
			  pdev->name, i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot claim IRQ\n");
		goto err_iomap;
	}

	i2c->irq = res;
		
	dev_dbg(&pdev->dev, "irq resource %p (%lu)\n", res,
		(unsigned long)res->start);

	ret = i2c_add_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_irq;
	}

	platform_set_drvdata(pdev, i2c);

	dev_info(&pdev->dev, "%s: S3C I2C adapter\n", i2c->adap.dev.bus_id);
	return 0;

 err_irq:
	free_irq(i2c->irq->start, i2c);

 err_iomap:
	iounmap(i2c->regs);

 err_ioarea:
	release_resource(i2c->ioarea);
	kfree(i2c->ioarea);

 err_clk:
	clk_disable(i2c->clk);
	clk_put(i2c->clk);

 err_noclk:
	return ret;
}

/* s3c24xx_i2c_remove
 *
 * called when device is removed from the bus
*/

static int s3c24xx_i2c_remove(struct platform_device *pdev)
{
	struct s3c24xx_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq->start, i2c);

	clk_disable(i2c->clk);
	clk_put(i2c->clk);

	iounmap(i2c->regs);

	release_resource(i2c->ioarea);
	kfree(i2c->ioarea);

	return 0;
}

#ifdef CONFIG_PM
static int s3c24xx_i2c_resume(struct platform_device *dev)
{
	struct s3c24xx_i2c *i2c = platform_get_drvdata(dev);

	if (i2c != NULL)
		s3c24xx_i2c_init(i2c);

	return 0;
}

#else
#define s3c24xx_i2c_resume NULL
#endif

/* device driver for platform bus bits */

static struct platform_driver s3c2410_i2c_driver = {
	.probe		= s3c24xx_i2c_probe,
	.remove		= s3c24xx_i2c_remove,
	.resume		= s3c24xx_i2c_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s3c2410-i2c",
	},
};

static struct platform_driver s3c2440_i2c_driver = {
	.probe		= s3c24xx_i2c_probe,
	.remove		= s3c24xx_i2c_remove,
	.resume		= s3c24xx_i2c_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "s3c2440-i2c",
	},
};

static int __init i2c_adap_s3c_init(void)
{
	int ret;

	ret = platform_driver_register(&s3c2410_i2c_driver);
	if (ret == 0) {
		ret = platform_driver_register(&s3c2440_i2c_driver);
		if (ret)
			platform_driver_unregister(&s3c2410_i2c_driver);
	}

	return ret;
}

static void __exit i2c_adap_s3c_exit(void)
{
	platform_driver_unregister(&s3c2410_i2c_driver);
	platform_driver_unregister(&s3c2440_i2c_driver);
}

module_init(i2c_adap_s3c_init);
module_exit(i2c_adap_s3c_exit);

MODULE_DESCRIPTION("S3C24XX I2C Bus driver");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL");
