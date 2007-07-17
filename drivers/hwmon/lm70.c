/*
 * lm70.c
 *
 * The LM70 is a temperature sensor chip from National Semiconductor (NS).
 * Copyright (C) 2006 Kaiwan N Billimoria <kaiwan@designergraphix.com>
 *
 * The LM70 communicates with a host processor via an SPI/Microwire Bus
 * interface. The complete datasheet is available at National's website
 * here:
 * http://www.national.com/pf/LM/LM70.html
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/hwmon.h>
#include <linux/spi/spi.h>
#include <asm/semaphore.h>

#define DRVNAME		"lm70"

struct lm70 {
	struct class_device *cdev;
	struct semaphore sem;
};

/* sysfs hook function */
static ssize_t lm70_sense_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	int status, val;
	u8 rxbuf[2];
	s16 raw=0;
	struct lm70 *p_lm70 = dev_get_drvdata(&spi->dev);

	if (down_interruptible(&p_lm70->sem))
		return -ERESTARTSYS;

	/*
	 * spi_read() requires a DMA-safe buffer; so we use
	 * spi_write_then_read(), transmitting 0 bytes.
	 */
	status = spi_write_then_read(spi, NULL, 0, &rxbuf[0], 2);
	if (status < 0) {
		printk(KERN_WARNING
		"spi_write_then_read failed with status %d\n", status);
		goto out;
	}
	dev_dbg(dev, "rxbuf[1] : 0x%x rxbuf[0] : 0x%x\n", rxbuf[1], rxbuf[0]);

	raw = (rxbuf[1] << 8) + rxbuf[0];
	dev_dbg(dev, "raw=0x%x\n", raw);

	/*
	 * The "raw" temperature read into rxbuf[] is a 16-bit signed 2's
	 * complement value. Only the MSB 11 bits (1 sign + 10 temperature
	 * bits) are meaningful; the LSB 5 bits are to be discarded.
	 * See the datasheet.
	 *
	 * Further, each bit represents 0.25 degrees Celsius; so, multiply
	 * by 0.25. Also multiply by 1000 to represent in millidegrees
	 * Celsius.
	 * So it's equivalent to multiplying by 0.25 * 1000 = 250.
	 */
	val = ((int)raw/32) * 250;
	status = sprintf(buf, "%+d\n", val); /* millidegrees Celsius */
out:
	up(&p_lm70->sem);
	return status;
}

static DEVICE_ATTR(temp1_input, S_IRUGO, lm70_sense_temp, NULL);

/*----------------------------------------------------------------------*/

static int __devinit lm70_probe(struct spi_device *spi)
{
	struct lm70 *p_lm70;
	int status;

	/* signaling is SPI_MODE_0 on a 3-wire link (shared SI/SO) */
	if ((spi->mode & (SPI_CPOL|SPI_CPHA)) || !(spi->mode & SPI_3WIRE))
		return -EINVAL;

	p_lm70 = kzalloc(sizeof *p_lm70, GFP_KERNEL);
	if (!p_lm70)
		return -ENOMEM;

	init_MUTEX(&p_lm70->sem);

	/* sysfs hook */
	p_lm70->cdev = hwmon_device_register(&spi->dev);
	if (IS_ERR(p_lm70->cdev)) {
		dev_dbg(&spi->dev, "hwmon_device_register failed.\n");
		status = PTR_ERR(p_lm70->cdev);
		goto out_dev_reg_failed;
	}
	dev_set_drvdata(&spi->dev, p_lm70);

	if ((status = device_create_file(&spi->dev, &dev_attr_temp1_input))) {
		dev_dbg(&spi->dev, "device_create_file failure.\n");
		goto out_dev_create_file_failed;
	}

	return 0;

out_dev_create_file_failed:
	hwmon_device_unregister(p_lm70->cdev);
out_dev_reg_failed:
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_lm70);
	return status;
}

static int __devexit lm70_remove(struct spi_device *spi)
{
	struct lm70 *p_lm70 = dev_get_drvdata(&spi->dev);

	device_remove_file(&spi->dev, &dev_attr_temp1_input);
	hwmon_device_unregister(p_lm70->cdev);
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_lm70);

	return 0;
}

static struct spi_driver lm70_driver = {
	.driver = {
		.name	= "lm70",
		.owner	= THIS_MODULE,
	},
	.probe	= lm70_probe,
	.remove	= __devexit_p(lm70_remove),
};

static int __init init_lm70(void)
{
	return spi_register_driver(&lm70_driver);
}

static void __exit cleanup_lm70(void)
{
	spi_unregister_driver(&lm70_driver);
}

module_init(init_lm70);
module_exit(cleanup_lm70);

MODULE_AUTHOR("Kaiwan N Billimoria");
MODULE_DESCRIPTION("National Semiconductor LM70 Linux driver");
MODULE_LICENSE("GPL");
