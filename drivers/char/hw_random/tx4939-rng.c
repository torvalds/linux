/*
 * RNG driver for TX4939 Random Number Generators (RNG)
 *
 * Copyright (C) 2009 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/gfp.h>

#define TX4939_RNG_RCSR		0x00000000
#define TX4939_RNG_ROR(n)	(0x00000018 + (n) * 8)

#define TX4939_RNG_RCSR_INTE	0x00000008
#define TX4939_RNG_RCSR_RST	0x00000004
#define TX4939_RNG_RCSR_FIN	0x00000002
#define TX4939_RNG_RCSR_ST	0x00000001

struct tx4939_rng {
	struct hwrng rng;
	void __iomem *base;
	u64 databuf[3];
	unsigned int data_avail;
};

static void rng_io_start(void)
{
#ifndef CONFIG_64BIT
	/*
	 * readq is reading a 64-bit register using a 64-bit load.  On
	 * a 32-bit kernel however interrupts or any other processor
	 * exception would clobber the upper 32-bit of the processor
	 * register so interrupts need to be disabled.
	 */
	local_irq_disable();
#endif
}

static void rng_io_end(void)
{
#ifndef CONFIG_64BIT
	local_irq_enable();
#endif
}

static u64 read_rng(void __iomem *base, unsigned int offset)
{
	return ____raw_readq(base + offset);
}

static void write_rng(u64 val, void __iomem *base, unsigned int offset)
{
	return ____raw_writeq(val, base + offset);
}

static int tx4939_rng_data_present(struct hwrng *rng, int wait)
{
	struct tx4939_rng *rngdev = container_of(rng, struct tx4939_rng, rng);
	int i;

	if (rngdev->data_avail)
		return rngdev->data_avail;
	for (i = 0; i < 20; i++) {
		rng_io_start();
		if (!(read_rng(rngdev->base, TX4939_RNG_RCSR)
		      & TX4939_RNG_RCSR_ST)) {
			rngdev->databuf[0] =
				read_rng(rngdev->base, TX4939_RNG_ROR(0));
			rngdev->databuf[1] =
				read_rng(rngdev->base, TX4939_RNG_ROR(1));
			rngdev->databuf[2] =
				read_rng(rngdev->base, TX4939_RNG_ROR(2));
			rngdev->data_avail =
				sizeof(rngdev->databuf) / sizeof(u32);
			/* Start RNG */
			write_rng(TX4939_RNG_RCSR_ST,
				  rngdev->base, TX4939_RNG_RCSR);
			wait = 0;
		}
		rng_io_end();
		if (!wait)
			break;
		/* 90 bus clock cycles by default for generation */
		ndelay(90 * 5);
	}
	return rngdev->data_avail;
}

static int tx4939_rng_data_read(struct hwrng *rng, u32 *buffer)
{
	struct tx4939_rng *rngdev = container_of(rng, struct tx4939_rng, rng);

	rngdev->data_avail--;
	*buffer = *((u32 *)&rngdev->databuf + rngdev->data_avail);
	return sizeof(u32);
}

static int __init tx4939_rng_probe(struct platform_device *dev)
{
	struct tx4939_rng *rngdev;
	struct resource *r;
	int i;

	r = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!r)
		return -EBUSY;
	rngdev = devm_kzalloc(&dev->dev, sizeof(*rngdev), GFP_KERNEL);
	if (!rngdev)
		return -ENOMEM;
	rngdev->base = devm_ioremap_resource(&dev->dev, r);
	if (IS_ERR(rngdev->base))
		return PTR_ERR(rngdev->base);

	rngdev->rng.name = dev_name(&dev->dev);
	rngdev->rng.data_present = tx4939_rng_data_present;
	rngdev->rng.data_read = tx4939_rng_data_read;

	rng_io_start();
	/* Reset RNG */
	write_rng(TX4939_RNG_RCSR_RST, rngdev->base, TX4939_RNG_RCSR);
	write_rng(0, rngdev->base, TX4939_RNG_RCSR);
	/* Start RNG */
	write_rng(TX4939_RNG_RCSR_ST, rngdev->base, TX4939_RNG_RCSR);
	rng_io_end();
	/*
	 * Drop first two results.  From the datasheet:
	 * The quality of the random numbers generated immediately
	 * after reset can be insufficient.  Therefore, do not use
	 * random numbers obtained from the first and second
	 * generations; use the ones from the third or subsequent
	 * generation.
	 */
	for (i = 0; i < 2; i++) {
		rngdev->data_avail = 0;
		if (!tx4939_rng_data_present(&rngdev->rng, 1))
			return -EIO;
	}

	platform_set_drvdata(dev, rngdev);
	return hwrng_register(&rngdev->rng);
}

static int __exit tx4939_rng_remove(struct platform_device *dev)
{
	struct tx4939_rng *rngdev = platform_get_drvdata(dev);

	hwrng_unregister(&rngdev->rng);
	platform_set_drvdata(dev, NULL);
	return 0;
}

static struct platform_driver tx4939_rng_driver = {
	.driver		= {
		.name	= "tx4939-rng",
		.owner	= THIS_MODULE,
	},
	.remove = tx4939_rng_remove,
};

static int __init tx4939rng_init(void)
{
	return platform_driver_probe(&tx4939_rng_driver, tx4939_rng_probe);
}

static void __exit tx4939rng_exit(void)
{
	platform_driver_unregister(&tx4939_rng_driver);
}

module_init(tx4939rng_init);
module_exit(tx4939rng_exit);

MODULE_DESCRIPTION("H/W Random Number Generator (RNG) driver for TX4939");
MODULE_LICENSE("GPL");
