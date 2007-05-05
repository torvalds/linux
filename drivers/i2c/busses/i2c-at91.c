/*
    i2c Support for Atmel's AT91 Two-Wire Interface (TWI)

    Copyright (C) 2004 Rick Bronson
    Converted to 2.6 by Andrew Victor <andrew@sanpeople.com>

    Borrowed heavily from original work by:
    Copyright (C) 2000 Philip Edelbrock <phil@stimpy.netroedge.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <asm/io.h>

#include <asm/arch/at91_twi.h>
#include <asm/arch/board.h>
#include <asm/arch/cpu.h>

#define TWI_CLOCK		100000		/* Hz. max 400 Kbits/sec */


static struct clk *twi_clk;
static void __iomem *twi_base;

#define at91_twi_read(reg)		__raw_readl(twi_base + (reg))
#define at91_twi_write(reg, val)	__raw_writel((val), twi_base + (reg))


/*
 * Initialize the TWI hardware registers.
 */
static void __devinit at91_twi_hwinit(void)
{
	unsigned long cdiv, ckdiv;

	at91_twi_write(AT91_TWI_IDR, 0xffffffff);	/* Disable all interrupts */
	at91_twi_write(AT91_TWI_CR, AT91_TWI_SWRST);	/* Reset peripheral */
	at91_twi_write(AT91_TWI_CR, AT91_TWI_MSEN);	/* Set Master mode */

	/* Calcuate clock dividers */
	cdiv = (clk_get_rate(twi_clk) / (2 * TWI_CLOCK)) - 3;
	cdiv = cdiv + 1;	/* round up */
	ckdiv = 0;
	while (cdiv > 255) {
		ckdiv++;
		cdiv = cdiv >> 1;
	}

	if (cpu_is_at91rm9200()) {			/* AT91RM9200 Errata #22 */
		if (ckdiv > 5) {
			printk(KERN_ERR "AT91 I2C: Invalid TWI_CLOCK value!\n");
			ckdiv = 5;
		}
	}

	at91_twi_write(AT91_TWI_CWGR, (ckdiv << 16) | (cdiv << 8) | cdiv);
}

/*
 * Poll the i2c status register until the specified bit is set.
 * Returns 0 if timed out (100 msec).
 */
static short at91_poll_status(unsigned long bit)
{
	int loop_cntr = 10000;

	do {
		udelay(10);
	} while (!(at91_twi_read(AT91_TWI_SR) & bit) && (--loop_cntr > 0));

	return (loop_cntr > 0);
}

static int xfer_read(struct i2c_adapter *adap, unsigned char *buf, int length)
{
	/* Send Start */
	at91_twi_write(AT91_TWI_CR, AT91_TWI_START);

	/* Read data */
	while (length--) {
		if (!length)	/* need to send Stop before reading last byte */
			at91_twi_write(AT91_TWI_CR, AT91_TWI_STOP);
		if (!at91_poll_status(AT91_TWI_RXRDY)) {
			dev_dbg(&adap->dev, "RXRDY timeout\n");
			return -ETIMEDOUT;
		}
		*buf++ = (at91_twi_read(AT91_TWI_RHR) & 0xff);
	}

	return 0;
}

static int xfer_write(struct i2c_adapter *adap, unsigned char *buf, int length)
{
	/* Load first byte into transmitter */
	at91_twi_write(AT91_TWI_THR, *buf++);

	/* Send Start */
	at91_twi_write(AT91_TWI_CR, AT91_TWI_START);

	do {
		if (!at91_poll_status(AT91_TWI_TXRDY)) {
			dev_dbg(&adap->dev, "TXRDY timeout\n");
			return -ETIMEDOUT;
		}

		length--;	/* byte was transmitted */

		if (length > 0)		/* more data to send? */
			at91_twi_write(AT91_TWI_THR, *buf++);
	} while (length);

	/* Send Stop */
	at91_twi_write(AT91_TWI_CR, AT91_TWI_STOP);

	return 0;
}

/*
 * Generic i2c master transfer entrypoint.
 *
 * Note: We do not use Atmel's feature of storing the "internal device address".
 * Instead the "internal device address" has to be written using a seperate
 * i2c message.
 * http://lists.arm.linux.org.uk/pipermail/linux-arm-kernel/2004-September/024411.html
 */
static int at91_xfer(struct i2c_adapter *adap, struct i2c_msg *pmsg, int num)
{
	int i, ret;

	dev_dbg(&adap->dev, "at91_xfer: processing %d messages:\n", num);

	for (i = 0; i < num; i++) {
		dev_dbg(&adap->dev, " #%d: %sing %d byte%s %s 0x%02x\n", i,
			pmsg->flags & I2C_M_RD ? "read" : "writ",
			pmsg->len, pmsg->len > 1 ? "s" : "",
			pmsg->flags & I2C_M_RD ? "from" : "to",	pmsg->addr);

		at91_twi_write(AT91_TWI_MMR, (pmsg->addr << 16)
			| ((pmsg->flags & I2C_M_RD) ? AT91_TWI_MREAD : 0));

		if (pmsg->len && pmsg->buf) {	/* sanity check */
			if (pmsg->flags & I2C_M_RD)
				ret = xfer_read(adap, pmsg->buf, pmsg->len);
			else
				ret = xfer_write(adap, pmsg->buf, pmsg->len);

			if (ret)
				return ret;

			/* Wait until transfer is finished */
			if (!at91_poll_status(AT91_TWI_TXCOMP)) {
				dev_dbg(&adap->dev, "TXCOMP timeout\n");
				return -ETIMEDOUT;
			}
		}
		dev_dbg(&adap->dev, "transfer complete\n");
		pmsg++;		/* next message */
	}
	return i;
}

/*
 * Return list of supported functionality.
 */
static u32 at91_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm at91_algorithm = {
	.master_xfer	= at91_xfer,
	.functionality	= at91_func,
};

/*
 * Main initialization routine.
 */
static int __devinit at91_i2c_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adapter;
	struct resource *res;
	int rc;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!request_mem_region(res->start, res->end - res->start + 1, "at91_i2c"))
		return -EBUSY;

	twi_base = ioremap(res->start, res->end - res->start + 1);
	if (!twi_base) {
		rc = -ENOMEM;
		goto fail0;
	}

	twi_clk = clk_get(NULL, "twi_clk");
	if (IS_ERR(twi_clk)) {
		dev_err(&pdev->dev, "no clock defined\n");
		rc = -ENODEV;
		goto fail1;
	}

	adapter = kzalloc(sizeof(struct i2c_adapter), GFP_KERNEL);
	if (adapter == NULL) {
		dev_err(&pdev->dev, "can't allocate inteface!\n");
		rc = -ENOMEM;
		goto fail2;
	}
	sprintf(adapter->name, "AT91");
	adapter->algo = &at91_algorithm;
	adapter->class = I2C_CLASS_HWMON;
	adapter->dev.parent = &pdev->dev;

	platform_set_drvdata(pdev, adapter);

	clk_enable(twi_clk);		/* enable peripheral clock */
	at91_twi_hwinit();		/* initialize TWI controller */

	rc = i2c_add_adapter(adapter);
	if (rc) {
		dev_err(&pdev->dev, "Adapter %s registration failed\n",
				adapter->name);
		goto fail3;
	}

	dev_info(&pdev->dev, "AT91 i2c bus driver.\n");
	return 0;

fail3:
	platform_set_drvdata(pdev, NULL);
	kfree(adapter);
	clk_disable(twi_clk);
fail2:
	clk_put(twi_clk);
fail1:
	iounmap(twi_base);
fail0:
	release_mem_region(res->start, res->end - res->start + 1);

	return rc;
}

static int __devexit at91_i2c_remove(struct platform_device *pdev)
{
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);
	struct resource *res;
	int rc;

	rc = i2c_del_adapter(adapter);
	platform_set_drvdata(pdev, NULL);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iounmap(twi_base);
	release_mem_region(res->start, res->end - res->start + 1);

	clk_disable(twi_clk);		/* disable peripheral clock */
	clk_put(twi_clk);

	return rc;
}

#ifdef CONFIG_PM

/* NOTE: could save a few mA by keeping clock off outside of at91_xfer... */

static int at91_i2c_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	clk_disable(twi_clk);
	return 0;
}

static int at91_i2c_resume(struct platform_device *pdev)
{
	return clk_enable(twi_clk);
}

#else
#define at91_i2c_suspend	NULL
#define at91_i2c_resume		NULL
#endif

static struct platform_driver at91_i2c_driver = {
	.probe		= at91_i2c_probe,
	.remove		= __devexit_p(at91_i2c_remove),
	.suspend	= at91_i2c_suspend,
	.resume		= at91_i2c_resume,
	.driver		= {
		.name	= "at91_i2c",
		.owner	= THIS_MODULE,
	},
};

static int __init at91_i2c_init(void)
{
	return platform_driver_register(&at91_i2c_driver);
}

static void __exit at91_i2c_exit(void)
{
	platform_driver_unregister(&at91_i2c_driver);
}

module_init(at91_i2c_init);
module_exit(at91_i2c_exit);

MODULE_AUTHOR("Rick Bronson");
MODULE_DESCRIPTION("I2C (TWI) driver for Atmel AT91");
MODULE_LICENSE("GPL");
