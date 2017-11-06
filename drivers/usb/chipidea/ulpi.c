// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 Linaro Ltd.
 */

#include <linux/device.h>
#include <linux/usb/chipidea.h>
#include <linux/ulpi/interface.h>

#include "ci.h"

#define ULPI_WAKEUP		BIT(31)
#define ULPI_RUN		BIT(30)
#define ULPI_WRITE		BIT(29)
#define ULPI_SYNC_STATE		BIT(27)
#define ULPI_ADDR(n)		((n) << 16)
#define ULPI_DATA(n)		(n)

static int ci_ulpi_wait(struct ci_hdrc *ci, u32 mask)
{
	unsigned long usec = 10000;

	while (usec--) {
		if (!hw_read(ci, OP_ULPI_VIEWPORT, mask))
			return 0;

		udelay(1);
	}

	return -ETIMEDOUT;
}

static int ci_ulpi_read(struct device *dev, u8 addr)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret;

	hw_write(ci, OP_ULPI_VIEWPORT, 0xffffffff, ULPI_WRITE | ULPI_WAKEUP);
	ret = ci_ulpi_wait(ci, ULPI_WAKEUP);
	if (ret)
		return ret;

	hw_write(ci, OP_ULPI_VIEWPORT, 0xffffffff, ULPI_RUN | ULPI_ADDR(addr));
	ret = ci_ulpi_wait(ci, ULPI_RUN);
	if (ret)
		return ret;

	return hw_read(ci, OP_ULPI_VIEWPORT, GENMASK(15, 8)) >> 8;
}

static int ci_ulpi_write(struct device *dev, u8 addr, u8 val)
{
	struct ci_hdrc *ci = dev_get_drvdata(dev);
	int ret;

	hw_write(ci, OP_ULPI_VIEWPORT, 0xffffffff, ULPI_WRITE | ULPI_WAKEUP);
	ret = ci_ulpi_wait(ci, ULPI_WAKEUP);
	if (ret)
		return ret;

	hw_write(ci, OP_ULPI_VIEWPORT, 0xffffffff,
		 ULPI_RUN | ULPI_WRITE | ULPI_ADDR(addr) | val);
	return ci_ulpi_wait(ci, ULPI_RUN);
}

int ci_ulpi_init(struct ci_hdrc *ci)
{
	if (ci->platdata->phy_mode != USBPHY_INTERFACE_MODE_ULPI)
		return 0;

	/*
	 * Set PORTSC correctly so we can read/write ULPI registers for
	 * identification purposes
	 */
	hw_phymode_configure(ci);

	ci->ulpi_ops.read = ci_ulpi_read;
	ci->ulpi_ops.write = ci_ulpi_write;
	ci->ulpi = ulpi_register_interface(ci->dev, &ci->ulpi_ops);
	if (IS_ERR(ci->ulpi))
		dev_err(ci->dev, "failed to register ULPI interface");

	return PTR_ERR_OR_ZERO(ci->ulpi);
}

void ci_ulpi_exit(struct ci_hdrc *ci)
{
	if (ci->ulpi) {
		ulpi_unregister_interface(ci->ulpi);
		ci->ulpi = NULL;
	}
}

int ci_ulpi_resume(struct ci_hdrc *ci)
{
	int cnt = 100000;

	while (cnt-- > 0) {
		if (hw_read(ci, OP_ULPI_VIEWPORT, ULPI_SYNC_STATE))
			return 0;
		udelay(1);
	}

	return -ETIMEDOUT;
}
