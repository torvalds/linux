// SPDX-License-Identifier: GPL-2.0
/*
 * sgi_w1.c - w1 master driver for one wire support in SGI ASICs
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/platform_data/sgi-w1.h>

#include <linux/w1.h>

#define MCR_RD_DATA	BIT(0)
#define MCR_DONE	BIT(1)

#define MCR_PACK(pulse, sample) (((pulse) << 10) | ((sample) << 2))

struct sgi_w1_device {
	u32 __iomem *mcr;
	struct w1_bus_master bus_master;
	char dev_id[64];
};

static u8 sgi_w1_wait(u32 __iomem *mcr)
{
	u32 mcr_val;

	do {
		mcr_val = readl(mcr);
	} while (!(mcr_val & MCR_DONE));

	return (mcr_val & MCR_RD_DATA) ? 1 : 0;
}

/*
 * this is the low level routine to
 * reset the device on the One Wire interface
 * on the hardware
 */
static u8 sgi_w1_reset_bus(void *data)
{
	struct sgi_w1_device *dev = data;
	u8 ret;

	writel(MCR_PACK(520, 65), dev->mcr);
	ret = sgi_w1_wait(dev->mcr);
	udelay(500); /* recovery time */
	return ret;
}

/*
 * this is the low level routine to read/write a bit on the One Wire
 * interface on the hardware. It does write 0 if parameter bit is set
 * to 0, otherwise a write 1/read.
 */
static u8 sgi_w1_touch_bit(void *data, u8 bit)
{
	struct sgi_w1_device *dev = data;
	u8 ret;

	if (bit)
		writel(MCR_PACK(6, 13), dev->mcr);
	else
		writel(MCR_PACK(80, 30), dev->mcr);

	ret = sgi_w1_wait(dev->mcr);
	if (bit)
		udelay(100); /* recovery */
	return ret;
}

static int sgi_w1_probe(struct platform_device *pdev)
{
	struct sgi_w1_device *sdev;
	struct sgi_w1_platform_data *pdata;

	sdev = devm_kzalloc(&pdev->dev, sizeof(struct sgi_w1_device),
			    GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->mcr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sdev->mcr))
		return PTR_ERR(sdev->mcr);

	sdev->bus_master.data = sdev;
	sdev->bus_master.reset_bus = sgi_w1_reset_bus;
	sdev->bus_master.touch_bit = sgi_w1_touch_bit;

	pdata = dev_get_platdata(&pdev->dev);
	if (pdata) {
		strlcpy(sdev->dev_id, pdata->dev_id, sizeof(sdev->dev_id));
		sdev->bus_master.dev_id = sdev->dev_id;
	}

	platform_set_drvdata(pdev, sdev);

	return w1_add_master_device(&sdev->bus_master);
}

/*
 * disassociate the w1 device from the driver
 */
static int sgi_w1_remove(struct platform_device *pdev)
{
	struct sgi_w1_device *sdev = platform_get_drvdata(pdev);

	w1_remove_master_device(&sdev->bus_master);

	return 0;
}

static struct platform_driver sgi_w1_driver = {
	.driver = {
		.name = "sgi_w1",
	},
	.probe = sgi_w1_probe,
	.remove = sgi_w1_remove,
};
module_platform_driver(sgi_w1_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Bogendoerfer");
MODULE_DESCRIPTION("Driver for One-Wire IP in SGI ASICs");
