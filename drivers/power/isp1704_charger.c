/*
 * ISP1704 USB Charger Detection driver
 *
 * Copyright (C) 2010 Nokia Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/delay.h>

#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>

/* Vendor specific Power Control register */
#define ISP1704_PWR_CTRL		0x3d
#define ISP1704_PWR_CTRL_SWCTRL		(1 << 0)
#define ISP1704_PWR_CTRL_DET_COMP	(1 << 1)
#define ISP1704_PWR_CTRL_BVALID_RISE	(1 << 2)
#define ISP1704_PWR_CTRL_BVALID_FALL	(1 << 3)
#define ISP1704_PWR_CTRL_DP_WKPU_EN	(1 << 4)
#define ISP1704_PWR_CTRL_VDAT_DET	(1 << 5)
#define ISP1704_PWR_CTRL_DPVSRC_EN	(1 << 6)
#define ISP1704_PWR_CTRL_HWDETECT	(1 << 7)

#define NXP_VENDOR_ID			0x04cc

static u16 isp170x_id[] = {
	0x1704,
	0x1707,
};

struct isp1704_charger {
	struct device		*dev;
	struct power_supply	psy;
	struct otg_transceiver	*otg;
	struct notifier_block	nb;
	struct work_struct	work;

	char			model[7];
	unsigned		present:1;
};

/*
 * ISP1704 detects PS/2 adapters as charger. To make sure the detected charger
 * is actually a dedicated charger, the following steps need to be taken.
 */
static inline int isp1704_charger_verify(struct isp1704_charger *isp)
{
	int	ret = 0;
	u8	r;

	/* Reset the transceiver */
	r = otg_io_read(isp->otg, ULPI_FUNC_CTRL);
	r |= ULPI_FUNC_CTRL_RESET;
	otg_io_write(isp->otg, ULPI_FUNC_CTRL, r);
	usleep_range(1000, 2000);

	/* Set normal mode */
	r &= ~(ULPI_FUNC_CTRL_RESET | ULPI_FUNC_CTRL_OPMODE_MASK);
	otg_io_write(isp->otg, ULPI_FUNC_CTRL, r);

	/* Clear the DP and DM pull-down bits */
	r = ULPI_OTG_CTRL_DP_PULLDOWN | ULPI_OTG_CTRL_DM_PULLDOWN;
	otg_io_write(isp->otg, ULPI_CLR(ULPI_OTG_CTRL), r);

	/* Enable strong pull-up on DP (1.5K) and reset */
	r = ULPI_FUNC_CTRL_TERMSELECT | ULPI_FUNC_CTRL_RESET;
	otg_io_write(isp->otg, ULPI_SET(ULPI_FUNC_CTRL), r);
	usleep_range(1000, 2000);

	/* Read the line state */
	if (!otg_io_read(isp->otg, ULPI_DEBUG)) {
		/* Disable strong pull-up on DP (1.5K) */
		otg_io_write(isp->otg, ULPI_CLR(ULPI_FUNC_CTRL),
				ULPI_FUNC_CTRL_TERMSELECT);
		return 1;
	}

	/* Is it a charger or PS/2 connection */

	/* Enable weak pull-up resistor on DP */
	otg_io_write(isp->otg, ULPI_SET(ISP1704_PWR_CTRL),
			ISP1704_PWR_CTRL_DP_WKPU_EN);

	/* Disable strong pull-up on DP (1.5K) */
	otg_io_write(isp->otg, ULPI_CLR(ULPI_FUNC_CTRL),
			ULPI_FUNC_CTRL_TERMSELECT);

	/* Enable weak pull-down resistor on DM */
	otg_io_write(isp->otg, ULPI_SET(ULPI_OTG_CTRL),
			ULPI_OTG_CTRL_DM_PULLDOWN);

	/* It's a charger if the line states are clear */
	if (!(otg_io_read(isp->otg, ULPI_DEBUG)))
		ret = 1;

	/* Disable weak pull-up resistor on DP */
	otg_io_write(isp->otg, ULPI_CLR(ISP1704_PWR_CTRL),
			ISP1704_PWR_CTRL_DP_WKPU_EN);

	return ret;
}

static inline int isp1704_charger_detect(struct isp1704_charger *isp)
{
	unsigned long	timeout;
	u8		r;
	int		ret = 0;

	/* set SW control bit in PWR_CTRL register */
	otg_io_write(isp->otg, ISP1704_PWR_CTRL,
			ISP1704_PWR_CTRL_SWCTRL);

	/* enable manual charger detection */
	r = (ISP1704_PWR_CTRL_SWCTRL | ISP1704_PWR_CTRL_DPVSRC_EN);
	otg_io_write(isp->otg, ULPI_SET(ISP1704_PWR_CTRL), r);
	usleep_range(1000, 2000);

	timeout = jiffies + msecs_to_jiffies(300);
	do {
		/* Check if there is a charger */
		if (otg_io_read(isp->otg, ISP1704_PWR_CTRL)
				& ISP1704_PWR_CTRL_VDAT_DET) {
			ret = isp1704_charger_verify(isp);
			break;
		}
	} while (!time_after(jiffies, timeout));

	return ret;
}

static void isp1704_charger_work(struct work_struct *data)
{
	int			detect;
	struct isp1704_charger	*isp =
		container_of(data, struct isp1704_charger, work);

	/*
	 * FIXME Only supporting dedicated chargers even though isp1704 can
	 * detect HUB and HOST chargers. If the device has already been
	 * enumerated, the detection will break the connection.
	 */
	if (isp->otg->state != OTG_STATE_B_IDLE)
		return;

	/* disable data pullups */
	if (isp->otg->gadget)
		usb_gadget_disconnect(isp->otg->gadget);

	/* detect charger */
	detect = isp1704_charger_detect(isp);
	if (detect) {
		isp->present = detect;
		power_supply_changed(&isp->psy);
	}

	/* enable data pullups */
	if (isp->otg->gadget)
		usb_gadget_connect(isp->otg->gadget);
}

static int isp1704_notifier_call(struct notifier_block *nb,
		unsigned long event, void *unused)
{
	struct isp1704_charger *isp =
		container_of(nb, struct isp1704_charger, nb);

	switch (event) {
	case USB_EVENT_VBUS:
		schedule_work(&isp->work);
		break;
	case USB_EVENT_NONE:
		if (isp->present) {
			isp->present = 0;
			power_supply_changed(&isp->psy);
		}
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int isp1704_charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct isp1704_charger *isp =
		container_of(psy, struct isp1704_charger, psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = isp->present;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = isp->model;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "NXP";
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static enum power_supply_property power_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

static inline int isp1704_test_ulpi(struct isp1704_charger *isp)
{
	int vendor;
	int product;
	int i;
	int ret = -ENODEV;

	/* Test ULPI interface */
	ret = otg_io_write(isp->otg, ULPI_SCRATCH, 0xaa);
	if (ret < 0)
		return ret;

	ret = otg_io_read(isp->otg, ULPI_SCRATCH);
	if (ret < 0)
		return ret;

	if (ret != 0xaa)
		return -ENODEV;

	/* Verify the product and vendor id matches */
	vendor = otg_io_read(isp->otg, ULPI_VENDOR_ID_LOW);
	vendor |= otg_io_read(isp->otg, ULPI_VENDOR_ID_HIGH) << 8;
	if (vendor != NXP_VENDOR_ID)
		return -ENODEV;

	product = otg_io_read(isp->otg, ULPI_PRODUCT_ID_LOW);
	product |= otg_io_read(isp->otg, ULPI_PRODUCT_ID_HIGH) << 8;

	for (i = 0; i < ARRAY_SIZE(isp170x_id); i++) {
		if (product == isp170x_id[i]) {
			sprintf(isp->model, "isp%x", product);
			return product;
		}
	}

	dev_err(isp->dev, "product id %x not matching known ids", product);

	return -ENODEV;
}

static int __devinit isp1704_charger_probe(struct platform_device *pdev)
{
	struct isp1704_charger	*isp;
	int			ret = -ENODEV;

	isp = kzalloc(sizeof *isp, GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->otg = otg_get_transceiver();
	if (!isp->otg)
		goto fail0;

	ret = isp1704_test_ulpi(isp);
	if (ret < 0)
		goto fail1;

	isp->dev = &pdev->dev;
	platform_set_drvdata(pdev, isp);

	isp->psy.name		= "isp1704";
	isp->psy.type		= POWER_SUPPLY_TYPE_USB;
	isp->psy.properties	= power_props;
	isp->psy.num_properties	= ARRAY_SIZE(power_props);
	isp->psy.get_property	= isp1704_charger_get_property;

	ret = power_supply_register(isp->dev, &isp->psy);
	if (ret)
		goto fail1;

	/*
	 * REVISIT: using work in order to allow the otg notifications to be
	 * made atomically in the future.
	 */
	INIT_WORK(&isp->work, isp1704_charger_work);

	isp->nb.notifier_call = isp1704_notifier_call;

	ret = otg_register_notifier(isp->otg, &isp->nb);
	if (ret)
		goto fail2;

	dev_info(isp->dev, "registered with product id %s\n", isp->model);

	return 0;
fail2:
	power_supply_unregister(&isp->psy);
fail1:
	otg_put_transceiver(isp->otg);
fail0:
	kfree(isp);

	dev_err(&pdev->dev, "failed to register isp1704 with error %d\n", ret);

	return ret;
}

static int __devexit isp1704_charger_remove(struct platform_device *pdev)
{
	struct isp1704_charger *isp = platform_get_drvdata(pdev);

	otg_unregister_notifier(isp->otg, &isp->nb);
	power_supply_unregister(&isp->psy);
	otg_put_transceiver(isp->otg);
	kfree(isp);

	return 0;
}

static struct platform_driver isp1704_charger_driver = {
	.driver = {
		.name = "isp1704_charger",
	},
	.probe = isp1704_charger_probe,
	.remove = __devexit_p(isp1704_charger_remove),
};

static int __init isp1704_charger_init(void)
{
	return platform_driver_register(&isp1704_charger_driver);
}
module_init(isp1704_charger_init);

static void __exit isp1704_charger_exit(void)
{
	platform_driver_unregister(&isp1704_charger_driver);
}
module_exit(isp1704_charger_exit);

MODULE_ALIAS("platform:isp1704_charger");
MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("ISP170x USB Charger driver");
MODULE_LICENSE("GPL");
