/*
 * Watchdog Device Driver for Xilinx axi/xps_timebase_wdt
 *
 * (C) Copyright 2011 (Alejandro Cabrera <aldaya@gmail.com>)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/watchdog.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

/* Register offsets for the Wdt device */
#define XWT_TWCSR0_OFFSET   0x0 /* Control/Status Register0 */
#define XWT_TWCSR1_OFFSET   0x4 /* Control/Status Register1 */
#define XWT_TBR_OFFSET      0x8 /* Timebase Register Offset */

/* Control/Status Register Masks  */
#define XWT_CSR0_WRS_MASK   0x00000008 /* Reset status */
#define XWT_CSR0_WDS_MASK   0x00000004 /* Timer state  */
#define XWT_CSR0_EWDT1_MASK 0x00000002 /* Enable bit 1 */

/* Control/Status Register 0/1 bits  */
#define XWT_CSRX_EWDT2_MASK 0x00000001 /* Enable bit 2 */

/* SelfTest constants */
#define XWT_MAX_SELFTEST_LOOP_COUNT 0x00010000
#define XWT_TIMER_FAILED            0xFFFFFFFF

#define WATCHDOG_NAME     "Xilinx Watchdog"
#define PFX WATCHDOG_NAME ": "

struct xwdt_device {
	struct resource  res;
	void __iomem *base;
	u32 nowayout;
	u32 wdt_interval;
	u32 boot_status;
};

static struct xwdt_device xdev;

static  u32 timeout;
static  u32 control_status_reg;
static  u8  expect_close;
static  u8  no_timeout;
static unsigned long driver_open;

static  DEFINE_SPINLOCK(spinlock);

static void xwdt_start(void)
{
	spin_lock(&spinlock);

	/* Clean previous status and enable the watchdog timer */
	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);

	iowrite32((control_status_reg | XWT_CSR0_EWDT1_MASK),
				xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32(XWT_CSRX_EWDT2_MASK, xdev.base + XWT_TWCSR1_OFFSET);

	spin_unlock(&spinlock);
}

static void xwdt_stop(void)
{
	spin_lock(&spinlock);

	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32((control_status_reg & ~XWT_CSR0_EWDT1_MASK),
				xdev.base + XWT_TWCSR0_OFFSET);

	iowrite32(0, xdev.base + XWT_TWCSR1_OFFSET);

	spin_unlock(&spinlock);
	pr_info("Stopped!\n");
}

static void xwdt_keepalive(void)
{
	spin_lock(&spinlock);

	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);
	control_status_reg |= (XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK);
	iowrite32(control_status_reg, xdev.base + XWT_TWCSR0_OFFSET);

	spin_unlock(&spinlock);
}

static void xwdt_get_status(int *status)
{
	int new_status;

	spin_lock(&spinlock);

	control_status_reg = ioread32(xdev.base + XWT_TWCSR0_OFFSET);
	new_status = ((control_status_reg &
			(XWT_CSR0_WRS_MASK | XWT_CSR0_WDS_MASK)) != 0);
	spin_unlock(&spinlock);

	*status = 0;
	if (new_status & 1)
		*status |= WDIOF_CARDRESET;
}

static u32 xwdt_selftest(void)
{
	int i;
	u32 timer_value1;
	u32 timer_value2;

	spin_lock(&spinlock);

	timer_value1 = ioread32(xdev.base + XWT_TBR_OFFSET);
	timer_value2 = ioread32(xdev.base + XWT_TBR_OFFSET);

	for (i = 0;
		((i <= XWT_MAX_SELFTEST_LOOP_COUNT) &&
			(timer_value2 == timer_value1)); i++) {
		timer_value2 = ioread32(xdev.base + XWT_TBR_OFFSET);
	}

	spin_unlock(&spinlock);

	if (timer_value2 != timer_value1)
		return ~XWT_TIMER_FAILED;
	else
		return XWT_TIMER_FAILED;
}

static int xwdt_open(struct inode *inode, struct file *file)
{
	/* Only one process can handle the wdt at a time */
	if (test_and_set_bit(0, &driver_open))
		return -EBUSY;

	/* Make sure that the module are always loaded...*/
	if (xdev.nowayout)
		__module_get(THIS_MODULE);

	xwdt_start();
	pr_info("Started...\n");

	return nonseekable_open(inode, file);
}

static int xwdt_release(struct inode *inode, struct file *file)
{
	if (expect_close == 42) {
		xwdt_stop();
	} else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		xwdt_keepalive();
	}

	clear_bit(0, &driver_open);
	expect_close = 0;
	return 0;
}

/*
 *      xwdt_write:
 *      @file: file handle to the watchdog
 *      @buf: buffer to write (unused as data does not matter here
 *      @count: count of bytes
 *      @ppos: pointer to the position to write. No seeks allowed
 *
 *      A write to a watchdog device is defined as a keepalive signal. Any
 *      write of data will do, as we don't define content meaning.
 */
static ssize_t xwdt_write(struct file *file, const char __user *buf,
						size_t len, loff_t *ppos)
{
	if (len) {
		if (!xdev.nowayout) {
			size_t i;

			/* In case it was set long ago */
			expect_close = 0;

			for (i = 0; i != len; i++) {
				char c;

				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					expect_close = 42;
			}
		}
		xwdt_keepalive();
	}
	return len;
}

static const struct watchdog_info ident = {
	.options =  WDIOF_MAGICCLOSE |
		    WDIOF_KEEPALIVEPING,
	.firmware_version =	1,
	.identity =	WATCHDOG_NAME,
};

/*
 *      xwdt_ioctl:
 *      @file: file handle to the device
 *      @cmd: watchdog command
 *      @arg: argument pointer
 *
 *      The watchdog API defines a common set of functions for all watchdogs
 *      according to their available features.
 */
static long xwdt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int status;

	union {
		struct watchdog_info __user *ident;
		int __user *i;
	} uarg;

	uarg.i = (int __user *)arg;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(uarg.ident, &ident,
					sizeof(ident)) ? -EFAULT : 0;

	case WDIOC_GETBOOTSTATUS:
		return put_user(xdev.boot_status, uarg.i);

	case WDIOC_GETSTATUS:
		xwdt_get_status(&status);
		return put_user(status, uarg.i);

	case WDIOC_KEEPALIVE:
		xwdt_keepalive();
		return 0;

	case WDIOC_GETTIMEOUT:
		if (no_timeout)
			return -ENOTTY;
		else
			return put_user(timeout, uarg.i);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations xwdt_fops = {
	.owner      = THIS_MODULE,
	.llseek     = no_llseek,
	.write      = xwdt_write,
	.open       = xwdt_open,
	.release    = xwdt_release,
	.unlocked_ioctl = xwdt_ioctl,
};

static struct miscdevice xwdt_miscdev = {
	.minor      = WATCHDOG_MINOR,
	.name       = "watchdog",
	.fops       = &xwdt_fops,
};

static int xwdt_probe(struct platform_device *pdev)
{
	int rc;
	u32 *tmptr;
	u32 *pfreq;

	no_timeout = 0;

	pfreq = (u32 *)of_get_property(pdev->dev.of_node,
					"clock-frequency", NULL);

	if (pfreq == NULL) {
		pr_warn("The watchdog clock frequency cannot be obtained!\n");
		no_timeout = 1;
	}

	rc = of_address_to_resource(pdev->dev.of_node, 0, &xdev.res);
	if (rc) {
		pr_warn("invalid address!\n");
		return rc;
	}

	tmptr = (u32 *)of_get_property(pdev->dev.of_node,
					"xlnx,wdt-interval", NULL);
	if (tmptr == NULL) {
		pr_warn("Parameter \"xlnx,wdt-interval\" not found in device tree!\n");
		no_timeout = 1;
	} else {
		xdev.wdt_interval = *tmptr;
	}

	tmptr = (u32 *)of_get_property(pdev->dev.of_node,
					"xlnx,wdt-enable-once", NULL);
	if (tmptr == NULL) {
		pr_warn("Parameter \"xlnx,wdt-enable-once\" not found in device tree!\n");
		xdev.nowayout = WATCHDOG_NOWAYOUT;
	}

/*
 *  Twice of the 2^wdt_interval / freq  because the first wdt overflow is
 *  ignored (interrupt), reset is only generated at second wdt overflow
 */
	if (!no_timeout)
		timeout = 2 * ((1<<xdev.wdt_interval) / *pfreq);

	if (!request_mem_region(xdev.res.start,
			xdev.res.end - xdev.res.start + 1, WATCHDOG_NAME)) {
		rc = -ENXIO;
		pr_err("memory request failure!\n");
		goto err_out;
	}

	xdev.base = ioremap(xdev.res.start, xdev.res.end - xdev.res.start + 1);
	if (xdev.base == NULL) {
		rc = -ENOMEM;
		pr_err("ioremap failure!\n");
		goto release_mem;
	}

	rc = xwdt_selftest();
	if (rc == XWT_TIMER_FAILED) {
		pr_err("SelfTest routine error!\n");
		goto unmap_io;
	}

	xwdt_get_status(&xdev.boot_status);

	rc = misc_register(&xwdt_miscdev);
	if (rc) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       xwdt_miscdev.minor, rc);
		goto unmap_io;
	}

	if (no_timeout)
		pr_info("driver loaded (timeout=? sec, nowayout=%d)\n",
			xdev.nowayout);
	else
		pr_info("driver loaded (timeout=%d sec, nowayout=%d)\n",
			timeout, xdev.nowayout);

	expect_close = 0;
	clear_bit(0, &driver_open);

	return 0;

unmap_io:
	iounmap(xdev.base);
release_mem:
	release_mem_region(xdev.res.start, resource_size(&xdev.res));
err_out:
	return rc;
}

static int xwdt_remove(struct platform_device *dev)
{
	misc_deregister(&xwdt_miscdev);
	iounmap(xdev.base);
	release_mem_region(xdev.res.start, resource_size(&xdev.res));

	return 0;
}

/* Match table for of_platform binding */
static struct of_device_id xwdt_of_match[] = {
	{ .compatible = "xlnx,xps-timebase-wdt-1.00.a", },
	{ .compatible = "xlnx,xps-timebase-wdt-1.01.a", },
	{},
};
MODULE_DEVICE_TABLE(of, xwdt_of_match);

static struct platform_driver xwdt_driver = {
	.probe       = xwdt_probe,
	.remove      = xwdt_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name  = WATCHDOG_NAME,
		.of_match_table = xwdt_of_match,
	},
};

module_platform_driver(xwdt_driver);

MODULE_AUTHOR("Alejandro Cabrera <aldaya@gmail.com>");
MODULE_DESCRIPTION("Xilinx Watchdog driver");
MODULE_LICENSE("GPL v2");
