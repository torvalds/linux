/*
 * arch/arm/mach-u300/dummyspichip.c
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * This is a dummy loopback SPI "chip" used for testing SPI.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
/*
 * WARNING! Do not include this pl022-specific controller header
 * for any generic driver. It is only done in this dummy chip
 * because we alter the chip configuration in order to test some
 * different settings on the loopback device. Normal chip configs
 * shall be STATIC and not altered by the driver!
 */
#include <linux/amba/pl022.h>

struct dummy {
	struct device *dev;
	struct mutex lock;
};

#define DMA_TEST_SIZE 2048

/* When we cat /sys/bus/spi/devices/spi0.0/looptest this will be triggered */
static ssize_t dummy_looptest(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	struct dummy *p_dummy = dev_get_drvdata(&spi->dev);

	/*
	 * WARNING! Do not dereference the chip-specific data in any normal
	 * driver for a chip. It is usually STATIC and shall not be read
	 * or written to. Your chip driver should NOT depend on fields in this
	 * struct, this is just used here to alter the behaviour of the chip
	 * in order to perform tests.
	 */
	int status;
	u8 txbuf[14] = {0xDE, 0xAD, 0xBE, 0xEF, 0x2B, 0xAD,
			0xCA, 0xFE, 0xBA, 0xBE, 0xB1, 0x05,
			0xF0, 0x0D};
	u8 rxbuf[14];
	u8 *bigtxbuf_virtual;
	u8 *bigrxbuf_virtual;

	if (mutex_lock_interruptible(&p_dummy->lock))
		return -ERESTARTSYS;

	bigtxbuf_virtual = kmalloc(DMA_TEST_SIZE, GFP_KERNEL);
	if (bigtxbuf_virtual == NULL) {
		status = -ENOMEM;
		goto out;
	}
	bigrxbuf_virtual = kmalloc(DMA_TEST_SIZE, GFP_KERNEL);

	/* Fill TXBUF with some happy pattern */
	memset(bigtxbuf_virtual, 0xAA, DMA_TEST_SIZE);

	/*
	 * Force chip to 8 bit mode
	 * WARNING: NEVER DO THIS IN REAL DRIVER CODE, THIS SHOULD BE STATIC!
	 */
	spi->bits_per_word = 8;
	/* You should NOT DO THIS EITHER */
	spi->master->setup(spi);

	/* Now run the tests for 8bit mode */
	pr_info("Simple test 1: write 0xAA byte, read back garbage byte "
		"in 8bit mode\n");
	status = spi_w8r8(spi, 0xAA);
	if (status < 0)
		pr_warning("Siple test 1: FAILURE: spi_write_then_read "
			   "failed with status %d\n", status);
	else
		pr_info("Simple test 1: SUCCESS!\n");

	pr_info("Simple test 2: write 8 bytes, read back 8 bytes garbage "
		"in 8bit mode (full FIFO)\n");
	status = spi_write_then_read(spi, &txbuf[0], 8, &rxbuf[0], 8);
	if (status < 0)
		pr_warning("Simple test 2: FAILURE: spi_write_then_read() "
			   "failed with status %d\n", status);
	else
		pr_info("Simple test 2: SUCCESS!\n");

	pr_info("Simple test 3: write 14 bytes, read back 14 bytes garbage "
		"in 8bit mode (see if we overflow FIFO)\n");
	status = spi_write_then_read(spi, &txbuf[0], 14, &rxbuf[0], 14);
	if (status < 0)
		pr_warning("Simple test 3: FAILURE: failed with status %d "
			   "(probably FIFO overrun)\n", status);
	else
		pr_info("Simple test 3: SUCCESS!\n");

	pr_info("Simple test 4: write 8 bytes with spi_write(), read 8 "
		"bytes garbage with spi_read() in 8bit mode\n");
	status = spi_write(spi, &txbuf[0], 8);
	if (status < 0)
		pr_warning("Simple test 4 step 1: FAILURE: spi_write() "
			   "failed with status %d\n", status);
	else
		pr_info("Simple test 4 step 1: SUCCESS!\n");
	status = spi_read(spi, &rxbuf[0], 8);
	if (status < 0)
		pr_warning("Simple test 4 step 2: FAILURE: spi_read() "
			   "failed with status %d\n", status);
	else
		pr_info("Simple test 4 step 2: SUCCESS!\n");

	pr_info("Simple test 5: write 14 bytes with spi_write(), read "
		"14 bytes garbage with spi_read() in 8bit mode\n");
	status = spi_write(spi, &txbuf[0], 14);
	if (status < 0)
		pr_warning("Simple test 5 step 1: FAILURE: spi_write() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 5 step 1: SUCCESS!\n");
	status = spi_read(spi, &rxbuf[0], 14);
	if (status < 0)
		pr_warning("Simple test 5 step 2: FAILURE: spi_read() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 5: SUCCESS!\n");

	pr_info("Simple test 6: write %d bytes with spi_write(), "
		"read %d bytes garbage with spi_read() in 8bit mode\n",
		DMA_TEST_SIZE, DMA_TEST_SIZE);
	status = spi_write(spi, &bigtxbuf_virtual[0], DMA_TEST_SIZE);
	if (status < 0)
		pr_warning("Simple test 6 step 1: FAILURE: spi_write() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 6 step 1: SUCCESS!\n");
	status = spi_read(spi, &bigrxbuf_virtual[0], DMA_TEST_SIZE);
	if (status < 0)
		pr_warning("Simple test 6 step 2: FAILURE: spi_read() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 6: SUCCESS!\n");


	/*
	 * Force chip to 16 bit mode
	 * WARNING: NEVER DO THIS IN REAL DRIVER CODE, THIS SHOULD BE STATIC!
	 */
	spi->bits_per_word = 16;
	/* You should NOT DO THIS EITHER */
	spi->master->setup(spi);

	pr_info("Simple test 7: write 0xAA byte, read back garbage byte "
		"in 16bit bus mode\n");
	status = spi_w8r8(spi, 0xAA);
	if (status == -EIO)
		pr_info("Simple test 7: SUCCESS! (expected failure with "
			"status EIO)\n");
	else if (status < 0)
		pr_warning("Siple test 7: FAILURE: spi_write_then_read "
			   "failed with status %d\n", status);
	else
		pr_warning("Siple test 7: FAILURE: spi_write_then_read "
			   "succeeded but it was expected to fail!\n");

	pr_info("Simple test 8: write 8 bytes, read back 8 bytes garbage "
		"in 16bit mode (full FIFO)\n");
	status = spi_write_then_read(spi, &txbuf[0], 8, &rxbuf[0], 8);
	if (status < 0)
		pr_warning("Simple test 8: FAILURE: spi_write_then_read() "
			   "failed with status %d\n", status);
	else
		pr_info("Simple test 8: SUCCESS!\n");

	pr_info("Simple test 9: write 14 bytes, read back 14 bytes garbage "
		"in 16bit mode (see if we overflow FIFO)\n");
	status = spi_write_then_read(spi, &txbuf[0], 14, &rxbuf[0], 14);
	if (status < 0)
		pr_warning("Simple test 9: FAILURE: failed with status %d "
			   "(probably FIFO overrun)\n", status);
	else
		pr_info("Simple test 9: SUCCESS!\n");

	pr_info("Simple test 10: write %d bytes with spi_write(), "
	       "read %d bytes garbage with spi_read() in 16bit mode\n",
	       DMA_TEST_SIZE, DMA_TEST_SIZE);
	status = spi_write(spi, &bigtxbuf_virtual[0], DMA_TEST_SIZE);
	if (status < 0)
		pr_warning("Simple test 10 step 1: FAILURE: spi_write() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 10 step 1: SUCCESS!\n");

	status = spi_read(spi, &bigrxbuf_virtual[0], DMA_TEST_SIZE);
	if (status < 0)
		pr_warning("Simple test 10 step 2: FAILURE: spi_read() "
			   "failed with status %d (probably FIFO overrun)\n",
			   status);
	else
		pr_info("Simple test 10: SUCCESS!\n");

	status = sprintf(buf, "loop test complete\n");
	kfree(bigrxbuf_virtual);
	kfree(bigtxbuf_virtual);
 out:
	mutex_unlock(&p_dummy->lock);
	return status;
}

static DEVICE_ATTR(looptest, S_IRUGO, dummy_looptest, NULL);

static int pl022_dummy_probe(struct spi_device *spi)
{
	struct dummy *p_dummy;
	int status;

	dev_info(&spi->dev, "probing dummy SPI device\n");

	p_dummy = kzalloc(sizeof *p_dummy, GFP_KERNEL);
	if (!p_dummy)
		return -ENOMEM;

	dev_set_drvdata(&spi->dev, p_dummy);
	mutex_init(&p_dummy->lock);

	/* sysfs hook */
	status = device_create_file(&spi->dev, &dev_attr_looptest);
	if (status) {
		dev_dbg(&spi->dev, "device_create_file looptest failure.\n");
		goto out_dev_create_looptest_failed;
	}

	return 0;

out_dev_create_looptest_failed:
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_dummy);
	return status;
}

static int pl022_dummy_remove(struct spi_device *spi)
{
	struct dummy *p_dummy = dev_get_drvdata(&spi->dev);

	dev_info(&spi->dev, "removing dummy SPI device\n");
	device_remove_file(&spi->dev, &dev_attr_looptest);
	dev_set_drvdata(&spi->dev, NULL);
	kfree(p_dummy);

	return 0;
}

static struct spi_driver pl022_dummy_driver = {
	.driver = {
		.name	= "spi-dummy",
		.owner	= THIS_MODULE,
	},
	.probe	= pl022_dummy_probe,
	.remove	= pl022_dummy_remove,
};

static int __init pl022_init_dummy(void)
{
	return spi_register_driver(&pl022_dummy_driver);
}

static void __exit pl022_exit_dummy(void)
{
	spi_unregister_driver(&pl022_dummy_driver);
}

module_init(pl022_init_dummy);
module_exit(pl022_exit_dummy);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");
MODULE_DESCRIPTION("PL022 SSP/SPI DUMMY Linux driver");
MODULE_LICENSE("GPL");
