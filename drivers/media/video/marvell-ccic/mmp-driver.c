/*
 * Support for the camera device found on Marvell MMP processors; known
 * to work with the Armada 610 as used in the OLPC 1.75 system.
 *
 * Copyright 2011 Jonathan Corbet <corbet@lwn.net>
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/mmp-camera.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/list.h>

#include "mcam-core.h"

MODULE_AUTHOR("Jonathan Corbet <corbet@lwn.net>");
MODULE_LICENSE("GPL");

struct mmp_camera {
	void *power_regs;
	struct platform_device *pdev;
	struct mcam_camera mcam;
	struct list_head devlist;
	int irq;
};

static inline struct mmp_camera *mcam_to_cam(struct mcam_camera *mcam)
{
	return container_of(mcam, struct mmp_camera, mcam);
}

/*
 * A silly little infrastructure so we can keep track of our devices.
 * Chances are that we will never have more than one of them, but
 * the Armada 610 *does* have two controllers...
 */

static LIST_HEAD(mmpcam_devices);
static struct mutex mmpcam_devices_lock;

static void mmpcam_add_device(struct mmp_camera *cam)
{
	mutex_lock(&mmpcam_devices_lock);
	list_add(&cam->devlist, &mmpcam_devices);
	mutex_unlock(&mmpcam_devices_lock);
}

static void mmpcam_remove_device(struct mmp_camera *cam)
{
	mutex_lock(&mmpcam_devices_lock);
	list_del(&cam->devlist);
	mutex_unlock(&mmpcam_devices_lock);
}

/*
 * Platform dev remove passes us a platform_device, and there's
 * no handy unused drvdata to stash a backpointer in.  So just
 * dig it out of our list.
 */
static struct mmp_camera *mmpcam_find_device(struct platform_device *pdev)
{
	struct mmp_camera *cam;

	mutex_lock(&mmpcam_devices_lock);
	list_for_each_entry(cam, &mmpcam_devices, devlist) {
		if (cam->pdev == pdev) {
			mutex_unlock(&mmpcam_devices_lock);
			return cam;
		}
	}
	mutex_unlock(&mmpcam_devices_lock);
	return NULL;
}




/*
 * Power-related registers; this almost certainly belongs
 * somewhere else.
 *
 * ARMADA 610 register manual, sec 7.2.1, p1842.
 */
#define CPU_SUBSYS_PMU_BASE	0xd4282800
#define REG_CCIC_DCGCR		0x28	/* CCIC dyn clock gate ctrl reg */
#define REG_CCIC_CRCR		0x50	/* CCIC clk reset ctrl reg	*/

/*
 * Power control.
 */
static void mmpcam_power_up(struct mcam_camera *mcam)
{
	struct mmp_camera *cam = mcam_to_cam(mcam);
	struct mmp_camera_platform_data *pdata;
/*
 * Turn on power and clocks to the controller.
 */
	iowrite32(0x3f, cam->power_regs + REG_CCIC_DCGCR);
	iowrite32(0x3805b, cam->power_regs + REG_CCIC_CRCR);
	mdelay(1);
/*
 * Provide power to the sensor.
 */
	mcam_reg_write(mcam, REG_CLKCTRL, 0x60000002);
	pdata = cam->pdev->dev.platform_data;
	gpio_set_value(pdata->sensor_power_gpio, 1);
	mdelay(5);
	mcam_reg_clear_bit(mcam, REG_CTRL1, 0x10000000);
	gpio_set_value(pdata->sensor_reset_gpio, 0); /* reset is active low */
	mdelay(5);
	gpio_set_value(pdata->sensor_reset_gpio, 1); /* reset is active low */
	mdelay(5);
}

static void mmpcam_power_down(struct mcam_camera *mcam)
{
	struct mmp_camera *cam = mcam_to_cam(mcam);
	struct mmp_camera_platform_data *pdata;
/*
 * Turn off clocks and set reset lines
 */
	iowrite32(0, cam->power_regs + REG_CCIC_DCGCR);
	iowrite32(0, cam->power_regs + REG_CCIC_CRCR);
/*
 * Shut down the sensor.
 */
	pdata = cam->pdev->dev.platform_data;
	gpio_set_value(pdata->sensor_power_gpio, 0);
	gpio_set_value(pdata->sensor_reset_gpio, 0);
}


static irqreturn_t mmpcam_irq(int irq, void *data)
{
	struct mcam_camera *mcam = data;
	unsigned int irqs, handled;

	spin_lock(&mcam->dev_lock);
	irqs = mcam_reg_read(mcam, REG_IRQSTAT);
	handled = mccic_irq(mcam, irqs);
	spin_unlock(&mcam->dev_lock);
	return IRQ_RETVAL(handled);
}


static int mmpcam_probe(struct platform_device *pdev)
{
	struct mmp_camera *cam;
	struct mcam_camera *mcam;
	struct resource *res;
	struct mmp_camera_platform_data *pdata;
	int ret;

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (cam == NULL)
		return -ENOMEM;
	cam->pdev = pdev;
	INIT_LIST_HEAD(&cam->devlist);

	mcam = &cam->mcam;
	mcam->platform = MHP_Armada610;
	mcam->plat_power_up = mmpcam_power_up;
	mcam->plat_power_down = mmpcam_power_down;
	mcam->dev = &pdev->dev;
	mcam->use_smbus = 0;
	mcam->chip_id = V4L2_IDENT_ARMADA610;
	mcam->buffer_mode = B_DMA_sg;
	spin_lock_init(&mcam->dev_lock);
	/*
	 * Get our I/O memory.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no iomem resource!\n");
		ret = -ENODEV;
		goto out_free;
	}
	mcam->regs = ioremap(res->start, resource_size(res));
	if (mcam->regs == NULL) {
		dev_err(&pdev->dev, "MMIO ioremap fail\n");
		ret = -ENODEV;
		goto out_free;
	}
	/*
	 * Power/clock memory is elsewhere; get it too.  Perhaps this
	 * should really be managed outside of this driver?
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res == NULL) {
		dev_err(&pdev->dev, "no power resource!\n");
		ret = -ENODEV;
		goto out_unmap1;
	}
	cam->power_regs = ioremap(res->start, resource_size(res));
	if (cam->power_regs == NULL) {
		dev_err(&pdev->dev, "power MMIO ioremap fail\n");
		ret = -ENODEV;
		goto out_unmap1;
	}
	/*
	 * Find the i2c adapter.  This assumes, of course, that the
	 * i2c bus is already up and functioning.
	 */
	pdata = pdev->dev.platform_data;
	mcam->i2c_adapter = platform_get_drvdata(pdata->i2c_device);
	if (mcam->i2c_adapter == NULL) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "No i2c adapter\n");
		goto out_unmap2;
	}
	/*
	 * Sensor GPIO pins.
	 */
	ret = gpio_request(pdata->sensor_power_gpio, "cam-power");
	if (ret) {
		dev_err(&pdev->dev, "Can't get sensor power gpio %d",
				pdata->sensor_power_gpio);
		goto out_unmap2;
	}
	gpio_direction_output(pdata->sensor_power_gpio, 0);
	ret = gpio_request(pdata->sensor_reset_gpio, "cam-reset");
	if (ret) {
		dev_err(&pdev->dev, "Can't get sensor reset gpio %d",
				pdata->sensor_reset_gpio);
		goto out_gpio;
	}
	gpio_direction_output(pdata->sensor_reset_gpio, 0);
	/*
	 * Power the device up and hand it off to the core.
	 */
	mmpcam_power_up(mcam);
	ret = mccic_register(mcam);
	if (ret)
		goto out_gpio2;
	/*
	 * Finally, set up our IRQ now that the core is ready to
	 * deal with it.
	 */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		ret = -ENODEV;
		goto out_unregister;
	}
	cam->irq = res->start;
	ret = request_irq(cam->irq, mmpcam_irq, IRQF_SHARED,
			"mmp-camera", mcam);
	if (ret == 0) {
		mmpcam_add_device(cam);
		return 0;
	}

out_unregister:
	mccic_shutdown(mcam);
out_gpio2:
	mmpcam_power_down(mcam);
	gpio_free(pdata->sensor_reset_gpio);
out_gpio:
	gpio_free(pdata->sensor_power_gpio);
out_unmap2:
	iounmap(cam->power_regs);
out_unmap1:
	iounmap(mcam->regs);
out_free:
	kfree(cam);
	return ret;
}


static int mmpcam_remove(struct mmp_camera *cam)
{
	struct mcam_camera *mcam = &cam->mcam;
	struct mmp_camera_platform_data *pdata;

	mmpcam_remove_device(cam);
	free_irq(cam->irq, mcam);
	mccic_shutdown(mcam);
	mmpcam_power_down(mcam);
	pdata = cam->pdev->dev.platform_data;
	gpio_free(pdata->sensor_reset_gpio);
	gpio_free(pdata->sensor_power_gpio);
	iounmap(cam->power_regs);
	iounmap(mcam->regs);
	kfree(cam);
	return 0;
}

static int mmpcam_platform_remove(struct platform_device *pdev)
{
	struct mmp_camera *cam = mmpcam_find_device(pdev);

	if (cam == NULL)
		return -ENODEV;
	return mmpcam_remove(cam);
}


static struct platform_driver mmpcam_driver = {
	.probe		= mmpcam_probe,
	.remove		= mmpcam_platform_remove,
	.driver = {
		.name	= "mmp-camera",
		.owner	= THIS_MODULE
	}
};


static int __init mmpcam_init_module(void)
{
	mutex_init(&mmpcam_devices_lock);
	return platform_driver_register(&mmpcam_driver);
}

static void __exit mmpcam_exit_module(void)
{
	platform_driver_unregister(&mmpcam_driver);
	/*
	 * platform_driver_unregister() should have emptied the list
	 */
	if (!list_empty(&mmpcam_devices))
		printk(KERN_ERR "mmp_camera leaving devices behind\n");
}

module_init(mmpcam_init_module);
module_exit(mmpcam_exit_module);
