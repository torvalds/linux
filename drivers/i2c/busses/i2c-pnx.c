/*
 * Provides I2C support for Philips PNX010x/PNX4008 boards.
 *
 * Authors: Dennis Kovalev <dkovalev@ru.mvista.com>
 *	    Vitaly Wool <vwool@ru.mvista.com>
 *
 * 2004-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/i2c-pnx.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#define I2C_PNX_TIMEOUT		10 /* msec */
#define I2C_PNX_SPEED_KHZ	100
#define I2C_PNX_REGION_SIZE	0x100
#define PNX_DEFAULT_FREQ	13 /* MHz */

static inline int wait_timeout(long timeout, struct i2c_pnx_algo_data *data)
{
	while (timeout > 0 &&
			(ioread32(I2C_REG_STS(data)) & mstatus_active)) {
		mdelay(1);
		timeout--;
	}
	return (timeout <= 0);
}

static inline int wait_reset(long timeout, struct i2c_pnx_algo_data *data)
{
	while (timeout > 0 &&
			(ioread32(I2C_REG_CTL(data)) & mcntrl_reset)) {
		mdelay(1);
		timeout--;
	}
	return (timeout <= 0);
}

static inline void i2c_pnx_arm_timer(struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *data = adap->algo_data;
	struct timer_list *timer = &data->mif.timer;
	int expires = I2C_PNX_TIMEOUT / (1000 / HZ);

	del_timer_sync(timer);

	dev_dbg(&adap->dev, "Timer armed at %lu plus %u jiffies.\n",
		jiffies, expires);

	timer->expires = jiffies + expires;
	timer->data = (unsigned long)adap;

	add_timer(timer);
}

/**
 * i2c_pnx_start - start a device
 * @slave_addr:		slave address
 * @adap:		pointer to adapter structure
 *
 * Generate a START signal in the desired mode.
 */
static int i2c_pnx_start(unsigned char slave_addr, struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;

	dev_dbg(&adap->dev, "%s(): addr 0x%x mode %d\n", __func__,
		slave_addr, alg_data->mif.mode);

	/* Check for 7 bit slave addresses only */
	if (slave_addr & ~0x7f) {
		dev_err(&adap->dev, "%s: Invalid slave address %x. "
		       "Only 7-bit addresses are supported\n",
		       adap->name, slave_addr);
		return -EINVAL;
	}

	/* First, make sure bus is idle */
	if (wait_timeout(I2C_PNX_TIMEOUT, alg_data)) {
		/* Somebody else is monopolizing the bus */
		dev_err(&adap->dev, "%s: Bus busy. Slave addr = %02x, "
		       "cntrl = %x, stat = %x\n",
		       adap->name, slave_addr,
		       ioread32(I2C_REG_CTL(alg_data)),
		       ioread32(I2C_REG_STS(alg_data)));
		return -EBUSY;
	} else if (ioread32(I2C_REG_STS(alg_data)) & mstatus_afi) {
		/* Sorry, we lost the bus */
		dev_err(&adap->dev, "%s: Arbitration failure. "
		       "Slave addr = %02x\n", adap->name, slave_addr);
		return -EIO;
	}

	/*
	 * OK, I2C is enabled and we have the bus.
	 * Clear the current TDI and AFI status flags.
	 */
	iowrite32(ioread32(I2C_REG_STS(alg_data)) | mstatus_tdi | mstatus_afi,
		  I2C_REG_STS(alg_data));

	dev_dbg(&adap->dev, "%s(): sending %#x\n", __func__,
		(slave_addr << 1) | start_bit | alg_data->mif.mode);

	/* Write the slave address, START bit and R/W bit */
	iowrite32((slave_addr << 1) | start_bit | alg_data->mif.mode,
		  I2C_REG_TX(alg_data));

	dev_dbg(&adap->dev, "%s(): exit\n", __func__);

	return 0;
}

/**
 * i2c_pnx_stop - stop a device
 * @adap:		pointer to I2C adapter structure
 *
 * Generate a STOP signal to terminate the master transaction.
 */
static void i2c_pnx_stop(struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	/* Only 1 msec max timeout due to interrupt context */
	long timeout = 1000;

	dev_dbg(&adap->dev, "%s(): entering: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	/* Write a STOP bit to TX FIFO */
	iowrite32(0xff | stop_bit, I2C_REG_TX(alg_data));

	/* Wait until the STOP is seen. */
	while (timeout > 0 &&
	       (ioread32(I2C_REG_STS(alg_data)) & mstatus_active)) {
		/* may be called from interrupt context */
		udelay(1);
		timeout--;
	}

	dev_dbg(&adap->dev, "%s(): exiting: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));
}

/**
 * i2c_pnx_master_xmit - transmit data to slave
 * @adap:		pointer to I2C adapter structure
 *
 * Sends one byte of data to the slave
 */
static int i2c_pnx_master_xmit(struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	u32 val;

	dev_dbg(&adap->dev, "%s(): entering: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	if (alg_data->mif.len > 0) {
		/* We still have something to talk about... */
		val = *alg_data->mif.buf++;

		if (alg_data->mif.len == 1) {
			val |= stop_bit;
			if (!alg_data->last)
				val |= start_bit;
		}

		alg_data->mif.len--;
		iowrite32(val, I2C_REG_TX(alg_data));

		dev_dbg(&adap->dev, "%s(): xmit %#x [%d]\n", __func__,
			val, alg_data->mif.len + 1);

		if (alg_data->mif.len == 0) {
			if (alg_data->last) {
				/* Wait until the STOP is seen. */
				if (wait_timeout(I2C_PNX_TIMEOUT, alg_data))
					dev_err(&adap->dev, "The bus is still "
						"active after timeout\n");
			}
			/* Disable master interrupts */
			iowrite32(ioread32(I2C_REG_CTL(alg_data)) &
				~(mcntrl_afie | mcntrl_naie | mcntrl_drmie),
				  I2C_REG_CTL(alg_data));

			del_timer_sync(&alg_data->mif.timer);

			dev_dbg(&adap->dev, "%s(): Waking up xfer routine.\n",
				__func__);

			complete(&alg_data->mif.complete);
		}
	} else if (alg_data->mif.len == 0) {
		/* zero-sized transfer */
		i2c_pnx_stop(adap);

		/* Disable master interrupts. */
		iowrite32(ioread32(I2C_REG_CTL(alg_data)) &
			~(mcntrl_afie | mcntrl_naie | mcntrl_drmie),
			  I2C_REG_CTL(alg_data));

		/* Stop timer. */
		del_timer_sync(&alg_data->mif.timer);
		dev_dbg(&adap->dev, "%s(): Waking up xfer routine after "
			"zero-xfer.\n", __func__);

		complete(&alg_data->mif.complete);
	}

	dev_dbg(&adap->dev, "%s(): exiting: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	return 0;
}

/**
 * i2c_pnx_master_rcv - receive data from slave
 * @adap:		pointer to I2C adapter structure
 *
 * Reads one byte data from the slave
 */
static int i2c_pnx_master_rcv(struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	unsigned int val = 0;
	u32 ctl = 0;

	dev_dbg(&adap->dev, "%s(): entering: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	/* Check, whether there is already data,
	 * or we didn't 'ask' for it yet.
	 */
	if (ioread32(I2C_REG_STS(alg_data)) & mstatus_rfe) {
		dev_dbg(&adap->dev, "%s(): Write dummy data to fill "
			"Rx-fifo...\n", __func__);

		if (alg_data->mif.len == 1) {
			/* Last byte, do not acknowledge next rcv. */
			val |= stop_bit;
			if (!alg_data->last)
				val |= start_bit;

			/*
			 * Enable interrupt RFDAIE (data in Rx fifo),
			 * and disable DRMIE (need data for Tx)
			 */
			ctl = ioread32(I2C_REG_CTL(alg_data));
			ctl |= mcntrl_rffie | mcntrl_daie;
			ctl &= ~mcntrl_drmie;
			iowrite32(ctl, I2C_REG_CTL(alg_data));
		}

		/*
		 * Now we'll 'ask' for data:
		 * For each byte we want to receive, we must
		 * write a (dummy) byte to the Tx-FIFO.
		 */
		iowrite32(val, I2C_REG_TX(alg_data));

		return 0;
	}

	/* Handle data. */
	if (alg_data->mif.len > 0) {
		val = ioread32(I2C_REG_RX(alg_data));
		*alg_data->mif.buf++ = (u8) (val & 0xff);
		dev_dbg(&adap->dev, "%s(): rcv 0x%x [%d]\n", __func__, val,
			alg_data->mif.len);

		alg_data->mif.len--;
		if (alg_data->mif.len == 0) {
			if (alg_data->last)
				/* Wait until the STOP is seen. */
				if (wait_timeout(I2C_PNX_TIMEOUT, alg_data))
					dev_err(&adap->dev, "The bus is still "
						"active after timeout\n");

			/* Disable master interrupts */
			ctl = ioread32(I2C_REG_CTL(alg_data));
			ctl &= ~(mcntrl_afie | mcntrl_naie | mcntrl_rffie |
				 mcntrl_drmie | mcntrl_daie);
			iowrite32(ctl, I2C_REG_CTL(alg_data));

			/* Kill timer. */
			del_timer_sync(&alg_data->mif.timer);
			complete(&alg_data->mif.complete);
		}
	}

	dev_dbg(&adap->dev, "%s(): exiting: stat = %04x.\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	return 0;
}

static irqreturn_t i2c_pnx_interrupt(int irq, void *dev_id)
{
	u32 stat, ctl;
	struct i2c_adapter *adap = dev_id;
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;

	dev_dbg(&adap->dev, "%s(): mstat = %x mctrl = %x, mode = %d\n",
		__func__,
		ioread32(I2C_REG_STS(alg_data)),
		ioread32(I2C_REG_CTL(alg_data)),
		alg_data->mif.mode);
	stat = ioread32(I2C_REG_STS(alg_data));

	/* let's see what kind of event this is */
	if (stat & mstatus_afi) {
		/* We lost arbitration in the midst of a transfer */
		alg_data->mif.ret = -EIO;

		/* Disable master interrupts. */
		ctl = ioread32(I2C_REG_CTL(alg_data));
		ctl &= ~(mcntrl_afie | mcntrl_naie | mcntrl_rffie |
			 mcntrl_drmie);
		iowrite32(ctl, I2C_REG_CTL(alg_data));

		/* Stop timer, to prevent timeout. */
		del_timer_sync(&alg_data->mif.timer);
		complete(&alg_data->mif.complete);
	} else if (stat & mstatus_nai) {
		/* Slave did not acknowledge, generate a STOP */
		dev_dbg(&adap->dev, "%s(): "
			"Slave did not acknowledge, generating a STOP.\n",
			__func__);
		i2c_pnx_stop(adap);

		/* Disable master interrupts. */
		ctl = ioread32(I2C_REG_CTL(alg_data));
		ctl &= ~(mcntrl_afie | mcntrl_naie | mcntrl_rffie |
			 mcntrl_drmie);
		iowrite32(ctl, I2C_REG_CTL(alg_data));

		/* Our return value. */
		alg_data->mif.ret = -EIO;

		/* Stop timer, to prevent timeout. */
		del_timer_sync(&alg_data->mif.timer);
		complete(&alg_data->mif.complete);
	} else {
		/*
		 * Two options:
		 * - Master Tx needs data.
		 * - There is data in the Rx-fifo
		 * The latter is only the case if we have requested for data,
		 * via a dummy write. (See 'i2c_pnx_master_rcv'.)
		 * We therefore check, as a sanity check, whether that interrupt
		 * has been enabled.
		 */
		if ((stat & mstatus_drmi) || !(stat & mstatus_rfe)) {
			if (alg_data->mif.mode == I2C_SMBUS_WRITE) {
				i2c_pnx_master_xmit(adap);
			} else if (alg_data->mif.mode == I2C_SMBUS_READ) {
				i2c_pnx_master_rcv(adap);
			}
		}
	}

	/* Clear TDI and AFI bits */
	stat = ioread32(I2C_REG_STS(alg_data));
	iowrite32(stat | mstatus_tdi | mstatus_afi, I2C_REG_STS(alg_data));

	dev_dbg(&adap->dev, "%s(): exiting, stat = %x ctrl = %x.\n",
		 __func__, ioread32(I2C_REG_STS(alg_data)),
		 ioread32(I2C_REG_CTL(alg_data)));

	return IRQ_HANDLED;
}

static void i2c_pnx_timeout(unsigned long data)
{
	struct i2c_adapter *adap = (struct i2c_adapter *)data;
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	u32 ctl;

	dev_err(&adap->dev, "Master timed out. stat = %04x, cntrl = %04x. "
	       "Resetting master...\n",
	       ioread32(I2C_REG_STS(alg_data)),
	       ioread32(I2C_REG_CTL(alg_data)));

	/* Reset master and disable interrupts */
	ctl = ioread32(I2C_REG_CTL(alg_data));
	ctl &= ~(mcntrl_afie | mcntrl_naie | mcntrl_rffie | mcntrl_drmie);
	iowrite32(ctl, I2C_REG_CTL(alg_data));

	ctl |= mcntrl_reset;
	iowrite32(ctl, I2C_REG_CTL(alg_data));
	wait_reset(I2C_PNX_TIMEOUT, alg_data);
	alg_data->mif.ret = -EIO;
	complete(&alg_data->mif.complete);
}

static inline void bus_reset_if_active(struct i2c_adapter *adap)
{
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	u32 stat;

	if ((stat = ioread32(I2C_REG_STS(alg_data))) & mstatus_active) {
		dev_err(&adap->dev,
			"%s: Bus is still active after xfer. Reset it...\n",
		       adap->name);
		iowrite32(ioread32(I2C_REG_CTL(alg_data)) | mcntrl_reset,
			  I2C_REG_CTL(alg_data));
		wait_reset(I2C_PNX_TIMEOUT, alg_data);
	} else if (!(stat & mstatus_rfe) || !(stat & mstatus_tfe)) {
		/* If there is data in the fifo's after transfer,
		 * flush fifo's by reset.
		 */
		iowrite32(ioread32(I2C_REG_CTL(alg_data)) | mcntrl_reset,
			  I2C_REG_CTL(alg_data));
		wait_reset(I2C_PNX_TIMEOUT, alg_data);
	} else if (stat & mstatus_nai) {
		iowrite32(ioread32(I2C_REG_CTL(alg_data)) | mcntrl_reset,
			  I2C_REG_CTL(alg_data));
		wait_reset(I2C_PNX_TIMEOUT, alg_data);
	}
}

/**
 * i2c_pnx_xfer - generic transfer entry point
 * @adap:		pointer to I2C adapter structure
 * @msgs:		array of messages
 * @num:		number of messages
 *
 * Initiates the transfer
 */
static int
i2c_pnx_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct i2c_msg *pmsg;
	int rc = 0, completed = 0, i;
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;
	u32 stat = ioread32(I2C_REG_STS(alg_data));

	dev_dbg(&adap->dev, "%s(): entering: %d messages, stat = %04x.\n",
		__func__, num, ioread32(I2C_REG_STS(alg_data)));

	bus_reset_if_active(adap);

	/* Process transactions in a loop. */
	for (i = 0; rc >= 0 && i < num; i++) {
		u8 addr;

		pmsg = &msgs[i];
		addr = pmsg->addr;

		if (pmsg->flags & I2C_M_TEN) {
			dev_err(&adap->dev,
				"%s: 10 bits addr not supported!\n",
				adap->name);
			rc = -EINVAL;
			break;
		}

		alg_data->mif.buf = pmsg->buf;
		alg_data->mif.len = pmsg->len;
		alg_data->mif.mode = (pmsg->flags & I2C_M_RD) ?
			I2C_SMBUS_READ : I2C_SMBUS_WRITE;
		alg_data->mif.ret = 0;
		alg_data->last = (i == num - 1);

		dev_dbg(&adap->dev, "%s(): mode %d, %d bytes\n", __func__,
			alg_data->mif.mode,
			alg_data->mif.len);

		i2c_pnx_arm_timer(adap);

		/* initialize the completion var */
		init_completion(&alg_data->mif.complete);

		/* Enable master interrupt */
		iowrite32(ioread32(I2C_REG_CTL(alg_data)) | mcntrl_afie |
				mcntrl_naie | mcntrl_drmie,
			  I2C_REG_CTL(alg_data));

		/* Put start-code and slave-address on the bus. */
		rc = i2c_pnx_start(addr, adap);
		if (rc < 0)
			break;

		/* Wait for completion */
		wait_for_completion(&alg_data->mif.complete);

		if (!(rc = alg_data->mif.ret))
			completed++;
		dev_dbg(&adap->dev, "%s(): Complete, return code = %d.\n",
			__func__, rc);

		/* Clear TDI and AFI bits in case they are set. */
		if ((stat = ioread32(I2C_REG_STS(alg_data))) & mstatus_tdi) {
			dev_dbg(&adap->dev,
				"%s: TDI still set... clearing now.\n",
			       adap->name);
			iowrite32(stat, I2C_REG_STS(alg_data));
		}
		if ((stat = ioread32(I2C_REG_STS(alg_data))) & mstatus_afi) {
			dev_dbg(&adap->dev,
				"%s: AFI still set... clearing now.\n",
			       adap->name);
			iowrite32(stat, I2C_REG_STS(alg_data));
		}
	}

	bus_reset_if_active(adap);

	/* Cleanup to be sure... */
	alg_data->mif.buf = NULL;
	alg_data->mif.len = 0;

	dev_dbg(&adap->dev, "%s(): exiting, stat = %x\n",
		__func__, ioread32(I2C_REG_STS(alg_data)));

	if (completed != num)
		return ((rc < 0) ? rc : -EREMOTEIO);

	return num;
}

static u32 i2c_pnx_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm pnx_algorithm = {
	.master_xfer = i2c_pnx_xfer,
	.functionality = i2c_pnx_func,
};

static int i2c_pnx_controller_suspend(struct platform_device *pdev,
				      pm_message_t state)
{
	struct i2c_pnx_data *i2c_pnx = platform_get_drvdata(pdev);
	return i2c_pnx->suspend(pdev, state);
}

static int i2c_pnx_controller_resume(struct platform_device *pdev)
{
	struct i2c_pnx_data *i2c_pnx = platform_get_drvdata(pdev);
	return i2c_pnx->resume(pdev);
}

static int __devinit i2c_pnx_probe(struct platform_device *pdev)
{
	unsigned long tmp;
	int ret = 0;
	struct i2c_pnx_algo_data *alg_data;
	int freq_mhz;
	struct i2c_pnx_data *i2c_pnx = pdev->dev.platform_data;

	if (!i2c_pnx || !i2c_pnx->adapter) {
		dev_err(&pdev->dev, "%s: no platform data supplied\n",
		       __func__);
		ret = -EINVAL;
		goto out;
	}

	platform_set_drvdata(pdev, i2c_pnx);

	if (i2c_pnx->calculate_input_freq)
		freq_mhz = i2c_pnx->calculate_input_freq(pdev);
	else {
		freq_mhz = PNX_DEFAULT_FREQ;
		dev_info(&pdev->dev, "Setting bus frequency to default value: "
		       "%d MHz\n", freq_mhz);
	}

	i2c_pnx->adapter->algo = &pnx_algorithm;

	alg_data = i2c_pnx->adapter->algo_data;
	init_timer(&alg_data->mif.timer);
	alg_data->mif.timer.function = i2c_pnx_timeout;
	alg_data->mif.timer.data = (unsigned long)i2c_pnx->adapter;

	/* Register I/O resource */
	if (!request_region(alg_data->base, I2C_PNX_REGION_SIZE, pdev->name)) {
		dev_err(&pdev->dev,
		       "I/O region 0x%08x for I2C already in use.\n",
		       alg_data->base);
		ret = -ENODEV;
		goto out_drvdata;
	}

	if (!(alg_data->ioaddr =
			(u32)ioremap(alg_data->base, I2C_PNX_REGION_SIZE))) {
		dev_err(&pdev->dev, "Couldn't ioremap I2C I/O region\n");
		ret = -ENOMEM;
		goto out_release;
	}

	i2c_pnx->set_clock_run(pdev);

	/*
	 * Clock Divisor High This value is the number of system clocks
	 * the serial clock (SCL) will be high.
	 * For example, if the system clock period is 50 ns and the maximum
	 * desired serial period is 10000 ns (100 kHz), then CLKHI would be
	 * set to 0.5*(f_sys/f_i2c)-2=0.5*(20e6/100e3)-2=98. The actual value
	 * programmed into CLKHI will vary from this slightly due to
	 * variations in the output pad's rise and fall times as well as
	 * the deglitching filter length.
	 */

	tmp = ((freq_mhz * 1000) / I2C_PNX_SPEED_KHZ) / 2 - 2;
	iowrite32(tmp, I2C_REG_CKH(alg_data));
	iowrite32(tmp, I2C_REG_CKL(alg_data));

	iowrite32(mcntrl_reset, I2C_REG_CTL(alg_data));
	if (wait_reset(I2C_PNX_TIMEOUT, alg_data)) {
		ret = -ENODEV;
		goto out_unmap;
	}
	init_completion(&alg_data->mif.complete);

	ret = request_irq(alg_data->irq, i2c_pnx_interrupt,
			0, pdev->name, i2c_pnx->adapter);
	if (ret)
		goto out_clock;

	/* Register this adapter with the I2C subsystem */
	i2c_pnx->adapter->dev.parent = &pdev->dev;
	ret = i2c_add_adapter(i2c_pnx->adapter);
	if (ret < 0) {
		dev_err(&pdev->dev, "I2C: Failed to add bus\n");
		goto out_irq;
	}

	dev_dbg(&pdev->dev, "%s: Master at %#8x, irq %d.\n",
	       i2c_pnx->adapter->name, alg_data->base, alg_data->irq);

	return 0;

out_irq:
	free_irq(alg_data->irq, alg_data);
out_clock:
	i2c_pnx->set_clock_stop(pdev);
out_unmap:
	iounmap((void *)alg_data->ioaddr);
out_release:
	release_region(alg_data->base, I2C_PNX_REGION_SIZE);
out_drvdata:
	platform_set_drvdata(pdev, NULL);
out:
	return ret;
}

static int __devexit i2c_pnx_remove(struct platform_device *pdev)
{
	struct i2c_pnx_data *i2c_pnx = platform_get_drvdata(pdev);
	struct i2c_adapter *adap = i2c_pnx->adapter;
	struct i2c_pnx_algo_data *alg_data = adap->algo_data;

	free_irq(alg_data->irq, alg_data);
	i2c_del_adapter(adap);
	i2c_pnx->set_clock_stop(pdev);
	iounmap((void *)alg_data->ioaddr);
	release_region(alg_data->base, I2C_PNX_REGION_SIZE);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver i2c_pnx_driver = {
	.driver = {
		.name = "pnx-i2c",
		.owner = THIS_MODULE,
	},
	.probe = i2c_pnx_probe,
	.remove = __devexit_p(i2c_pnx_remove),
	.suspend = i2c_pnx_controller_suspend,
	.resume = i2c_pnx_controller_resume,
};

static int __init i2c_adap_pnx_init(void)
{
	return platform_driver_register(&i2c_pnx_driver);
}

static void __exit i2c_adap_pnx_exit(void)
{
	platform_driver_unregister(&i2c_pnx_driver);
}

MODULE_AUTHOR("Vitaly Wool, Dennis Kovalev <source@mvista.com>");
MODULE_DESCRIPTION("I2C driver for Philips IP3204-based I2C busses");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pnx-i2c");

/* We need to make sure I2C is initialized before USB */
subsys_initcall(i2c_adap_pnx_init);
module_exit(i2c_adap_pnx_exit);
