// SPDX-License-Identifier: GPL-2.0
/*
 *  i2c slave support for Atmel's AT91 Two-Wire Interface (TWI)
 *
 *  Copyright (C) 2017 Juergen Fitschen <me@jue.yt>
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#include "i2c-at91.h"

static irqreturn_t atmel_twi_interrupt_slave(int irq, void *dev_id)
{
	struct at91_twi_dev *dev = dev_id;
	const unsigned status = at91_twi_read(dev, AT91_TWI_SR);
	const unsigned irqstatus = status & at91_twi_read(dev, AT91_TWI_IMR);
	u8 value;

	if (!irqstatus)
		return IRQ_NONE;

	/* slave address has been detected on I2C bus */
	if (irqstatus & AT91_TWI_SVACC) {
		if (status & AT91_TWI_SVREAD) {
			i2c_slave_event(dev->slave,
					I2C_SLAVE_READ_REQUESTED, &value);
			writeb_relaxed(value, dev->base + AT91_TWI_THR);
			at91_twi_write(dev, AT91_TWI_IER,
				       AT91_TWI_TXRDY | AT91_TWI_EOSACC);
		} else {
			i2c_slave_event(dev->slave,
					I2C_SLAVE_WRITE_REQUESTED, &value);
			at91_twi_write(dev, AT91_TWI_IER,
				       AT91_TWI_RXRDY | AT91_TWI_EOSACC);
		}
		at91_twi_write(dev, AT91_TWI_IDR, AT91_TWI_SVACC);
	}

	/* byte transmitted to remote master */
	if (irqstatus & AT91_TWI_TXRDY) {
		i2c_slave_event(dev->slave, I2C_SLAVE_READ_PROCESSED, &value);
		writeb_relaxed(value, dev->base + AT91_TWI_THR);
	}

	/* byte received from remote master */
	if (irqstatus & AT91_TWI_RXRDY) {
		value = readb_relaxed(dev->base + AT91_TWI_RHR);
		i2c_slave_event(dev->slave, I2C_SLAVE_WRITE_RECEIVED, &value);
	}

	/* master sent stop */
	if (irqstatus & AT91_TWI_EOSACC) {
		at91_twi_write(dev, AT91_TWI_IDR,
			       AT91_TWI_TXRDY | AT91_TWI_RXRDY | AT91_TWI_EOSACC);
		at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_SVACC);
		i2c_slave_event(dev->slave, I2C_SLAVE_STOP, &value);
	}

	return IRQ_HANDLED;
}

static int at91_reg_slave(struct i2c_client *slave)
{
	struct at91_twi_dev *dev = i2c_get_adapdata(slave->adapter);

	if (dev->slave)
		return -EBUSY;

	if (slave->flags & I2C_CLIENT_TEN)
		return -EAFNOSUPPORT;

	/* Make sure twi_clk doesn't get turned off! */
	pm_runtime_get_sync(dev->dev);

	dev->slave = slave;
	dev->smr = AT91_TWI_SMR_SADR(slave->addr);

	at91_init_twi_bus(dev);
	at91_twi_write(dev, AT91_TWI_IER, AT91_TWI_SVACC);

	dev_info(dev->dev, "entered slave mode (ADR=%d)\n", slave->addr);

	return 0;
}

static int at91_unreg_slave(struct i2c_client *slave)
{
	struct at91_twi_dev *dev = i2c_get_adapdata(slave->adapter);

	WARN_ON(!dev->slave);

	dev_info(dev->dev, "leaving slave mode\n");

	dev->slave = NULL;
	dev->smr = 0;

	at91_init_twi_bus(dev);

	pm_runtime_put(dev->dev);

	return 0;
}

static u32 at91_twi_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SLAVE;
}

static const struct i2c_algorithm at91_twi_algorithm_slave = {
	.reg_slave	= at91_reg_slave,
	.unreg_slave	= at91_unreg_slave,
	.functionality	= at91_twi_func,
};

int at91_twi_probe_slave(struct platform_device *pdev,
			 u32 phy_addr, struct at91_twi_dev *dev)
{
	int rc;

	rc = devm_request_irq(&pdev->dev, dev->irq, atmel_twi_interrupt_slave,
			      0, dev_name(dev->dev), dev);
	if (rc) {
		dev_err(dev->dev, "Cannot get irq %d: %d\n", dev->irq, rc);
		return rc;
	}

	dev->adapter.algo = &at91_twi_algorithm_slave;

	return 0;
}

void at91_init_twi_bus_slave(struct at91_twi_dev *dev)
{
	at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_MSDIS);
	if (dev->slave_detected && dev->smr) {
		at91_twi_write(dev, AT91_TWI_SMR, dev->smr);
		at91_twi_write(dev, AT91_TWI_CR, AT91_TWI_SVEN);
	}
}
