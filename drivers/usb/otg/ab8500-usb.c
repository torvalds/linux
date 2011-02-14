/*
 * drivers/usb/otg/ab8500_usb.c
 *
 * USB transceiver driver for AB8500 chip
 *
 * Copyright (C) 2010 ST-Ericsson AB
 * Mian Yousaf Kaukab <mian.yousaf.kaukab@stericsson.com>
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
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/usb/otg.h>
#include <linux/slab.h>
#include <linux/notifier.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mfd/abx500.h>
#include <linux/mfd/ab8500.h>

#define AB8500_MAIN_WD_CTRL_REG 0x01
#define AB8500_USB_LINE_STAT_REG 0x80
#define AB8500_USB_PHY_CTRL_REG 0x8A

#define AB8500_BIT_OTG_STAT_ID (1 << 0)
#define AB8500_BIT_PHY_CTRL_HOST_EN (1 << 0)
#define AB8500_BIT_PHY_CTRL_DEVICE_EN (1 << 1)
#define AB8500_BIT_WD_CTRL_ENABLE (1 << 0)
#define AB8500_BIT_WD_CTRL_KICK (1 << 1)

#define AB8500_V1x_LINK_STAT_WAIT (HZ/10)
#define AB8500_WD_KICK_DELAY_US 100 /* usec */
#define AB8500_WD_V11_DISABLE_DELAY_US 100 /* usec */
#define AB8500_WD_V10_DISABLE_DELAY_MS 100 /* ms */

/* Usb line status register */
enum ab8500_usb_link_status {
	USB_LINK_NOT_CONFIGURED = 0,
	USB_LINK_STD_HOST_NC,
	USB_LINK_STD_HOST_C_NS,
	USB_LINK_STD_HOST_C_S,
	USB_LINK_HOST_CHG_NM,
	USB_LINK_HOST_CHG_HS,
	USB_LINK_HOST_CHG_HS_CHIRP,
	USB_LINK_DEDICATED_CHG,
	USB_LINK_ACA_RID_A,
	USB_LINK_ACA_RID_B,
	USB_LINK_ACA_RID_C_NM,
	USB_LINK_ACA_RID_C_HS,
	USB_LINK_ACA_RID_C_HS_CHIRP,
	USB_LINK_HM_IDGND,
	USB_LINK_RESERVED,
	USB_LINK_NOT_VALID_LINK
};

struct ab8500_usb {
	struct otg_transceiver otg;
	struct device *dev;
	int irq_num_id_rise;
	int irq_num_id_fall;
	int irq_num_vbus_rise;
	int irq_num_vbus_fall;
	int irq_num_link_status;
	unsigned vbus_draw;
	struct delayed_work dwork;
	struct work_struct phy_dis_work;
	unsigned long link_status_wait;
	int rev;
};

static inline struct ab8500_usb *xceiv_to_ab(struct otg_transceiver *x)
{
	return container_of(x, struct ab8500_usb, otg);
}

static void ab8500_usb_wd_workaround(struct ab8500_usb *ab)
{
	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		AB8500_BIT_WD_CTRL_ENABLE);

	udelay(AB8500_WD_KICK_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		(AB8500_BIT_WD_CTRL_ENABLE
		| AB8500_BIT_WD_CTRL_KICK));

	if (ab->rev > 0x10) /* v1.1 v2.0 */
		udelay(AB8500_WD_V11_DISABLE_DELAY_US);
	else /* v1.0 */
		msleep(AB8500_WD_V10_DISABLE_DELAY_MS);

	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		0);
}

static void ab8500_usb_phy_ctrl(struct ab8500_usb *ab, bool sel_host,
					bool enable)
{
	u8 ctrl_reg;
	abx500_get_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				&ctrl_reg);
	if (sel_host) {
		if (enable)
			ctrl_reg |= AB8500_BIT_PHY_CTRL_HOST_EN;
		else
			ctrl_reg &= ~AB8500_BIT_PHY_CTRL_HOST_EN;
	} else {
		if (enable)
			ctrl_reg |= AB8500_BIT_PHY_CTRL_DEVICE_EN;
		else
			ctrl_reg &= ~AB8500_BIT_PHY_CTRL_DEVICE_EN;
	}

	abx500_set_register_interruptible(ab->dev,
				AB8500_USB,
				AB8500_USB_PHY_CTRL_REG,
				ctrl_reg);

	/* Needed to enable the phy.*/
	if (enable)
		ab8500_usb_wd_workaround(ab);
}

#define ab8500_usb_host_phy_en(ab)	ab8500_usb_phy_ctrl(ab, true, true)
#define ab8500_usb_host_phy_dis(ab)	ab8500_usb_phy_ctrl(ab, true, false)
#define ab8500_usb_peri_phy_en(ab)	ab8500_usb_phy_ctrl(ab, false, true)
#define ab8500_usb_peri_phy_dis(ab)	ab8500_usb_phy_ctrl(ab, false, false)

static int ab8500_usb_link_status_update(struct ab8500_usb *ab)
{
	u8 reg;
	enum ab8500_usb_link_status lsts;
	void *v = NULL;
	enum usb_xceiv_events event;

	abx500_get_register_interruptible(ab->dev,
			AB8500_USB,
			AB8500_USB_LINE_STAT_REG,
			&reg);

	lsts = (reg >> 3) & 0x0F;

	switch (lsts) {
	case USB_LINK_NOT_CONFIGURED:
	case USB_LINK_RESERVED:
	case USB_LINK_NOT_VALID_LINK:
		/* TODO: Disable regulators. */
		ab8500_usb_host_phy_dis(ab);
		ab8500_usb_peri_phy_dis(ab);
		ab->otg.state = OTG_STATE_B_IDLE;
		ab->otg.default_a = false;
		ab->vbus_draw = 0;
		event = USB_EVENT_NONE;
		break;

	case USB_LINK_STD_HOST_NC:
	case USB_LINK_STD_HOST_C_NS:
	case USB_LINK_STD_HOST_C_S:
	case USB_LINK_HOST_CHG_NM:
	case USB_LINK_HOST_CHG_HS:
	case USB_LINK_HOST_CHG_HS_CHIRP:
		if (ab->otg.gadget) {
			/* TODO: Enable regulators. */
			ab8500_usb_peri_phy_en(ab);
			v = ab->otg.gadget;
		}
		event = USB_EVENT_VBUS;
		break;

	case USB_LINK_HM_IDGND:
		if (ab->otg.host) {
			/* TODO: Enable regulators. */
			ab8500_usb_host_phy_en(ab);
			v = ab->otg.host;
		}
		ab->otg.state = OTG_STATE_A_IDLE;
		ab->otg.default_a = true;
		event = USB_EVENT_ID;
		break;

	case USB_LINK_ACA_RID_A:
	case USB_LINK_ACA_RID_B:
		/* TODO */
	case USB_LINK_ACA_RID_C_NM:
	case USB_LINK_ACA_RID_C_HS:
	case USB_LINK_ACA_RID_C_HS_CHIRP:
	case USB_LINK_DEDICATED_CHG:
		/* TODO: vbus_draw */
		event = USB_EVENT_CHARGER;
		break;
	}

	blocking_notifier_call_chain(&ab->otg.notifier, event, v);

	return 0;
}

static void ab8500_usb_delayed_work(struct work_struct *work)
{
	struct ab8500_usb *ab = container_of(work, struct ab8500_usb,
						dwork.work);

	ab8500_usb_link_status_update(ab);
}

static irqreturn_t ab8500_usb_v1x_common_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;

	/* Wait for link status to become stable. */
	schedule_delayed_work(&ab->dwork, ab->link_status_wait);

	return IRQ_HANDLED;
}

static irqreturn_t ab8500_usb_v1x_vbus_fall_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;

	/* Link status will not be updated till phy is disabled. */
	ab8500_usb_peri_phy_dis(ab);

	/* Wait for link status to become stable. */
	schedule_delayed_work(&ab->dwork, ab->link_status_wait);

	return IRQ_HANDLED;
}

static irqreturn_t ab8500_usb_v20_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;

	ab8500_usb_link_status_update(ab);

	return IRQ_HANDLED;
}

static void ab8500_usb_phy_disable_work(struct work_struct *work)
{
	struct ab8500_usb *ab = container_of(work, struct ab8500_usb,
						phy_dis_work);

	if (!ab->otg.host)
		ab8500_usb_host_phy_dis(ab);

	if (!ab->otg.gadget)
		ab8500_usb_peri_phy_dis(ab);
}

static int ab8500_usb_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	ab->vbus_draw = mA;

	if (mA)
		blocking_notifier_call_chain(&ab->otg.notifier,
				USB_EVENT_ENUMERATED, ab->otg.gadget);
	return 0;
}

/* TODO: Implement some way for charging or other drivers to read
 * ab->vbus_draw.
 */

static int ab8500_usb_set_suspend(struct otg_transceiver *x, int suspend)
{
	/* TODO */
	return 0;
}

static int ab8500_usb_set_peripheral(struct otg_transceiver *otg,
		struct usb_gadget *gadget)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */

	if (!gadget) {
		/* TODO: Disable regulators. */
		ab->otg.gadget = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		ab->otg.gadget = gadget;
		ab->otg.state = OTG_STATE_B_IDLE;

		/* Phy will not be enabled if cable is already
		 * plugged-in. Schedule to enable phy.
		 * Use same delay to avoid any race condition.
		 */
		schedule_delayed_work(&ab->dwork, ab->link_status_wait);
	}

	return 0;
}

static int ab8500_usb_set_host(struct otg_transceiver *otg,
					struct usb_bus *host)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = xceiv_to_ab(otg);

	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */

	if (!host) {
		/* TODO: Disable regulators. */
		ab->otg.host = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		ab->otg.host = host;
		/* Phy will not be enabled if cable is already
		 * plugged-in. Schedule to enable phy.
		 * Use same delay to avoid any race condition.
		 */
		schedule_delayed_work(&ab->dwork, ab->link_status_wait);
	}

	return 0;
}

static void ab8500_usb_irq_free(struct ab8500_usb *ab)
{
	if (ab->rev < 0x20) {
		free_irq(ab->irq_num_id_rise, ab);
		free_irq(ab->irq_num_id_fall, ab);
		free_irq(ab->irq_num_vbus_rise, ab);
		free_irq(ab->irq_num_vbus_fall, ab);
	} else {
		free_irq(ab->irq_num_link_status, ab);
	}
}

static int ab8500_usb_v1x_res_setup(struct platform_device *pdev,
				struct ab8500_usb *ab)
{
	int err;

	ab->irq_num_id_rise = platform_get_irq_byname(pdev, "ID_WAKEUP_R");
	if (ab->irq_num_id_rise < 0) {
		dev_err(&pdev->dev, "ID rise irq not found\n");
		return ab->irq_num_id_rise;
	}
	err = request_threaded_irq(ab->irq_num_id_rise, NULL,
		ab8500_usb_v1x_common_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-id-rise", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for ID rise irq\n");
		goto fail0;
	}

	ab->irq_num_id_fall = platform_get_irq_byname(pdev, "ID_WAKEUP_F");
	if (ab->irq_num_id_fall < 0) {
		dev_err(&pdev->dev, "ID fall irq not found\n");
		return ab->irq_num_id_fall;
	}
	err = request_threaded_irq(ab->irq_num_id_fall, NULL,
		ab8500_usb_v1x_common_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-id-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for ID fall irq\n");
		goto fail1;
	}

	ab->irq_num_vbus_rise = platform_get_irq_byname(pdev, "VBUS_DET_R");
	if (ab->irq_num_vbus_rise < 0) {
		dev_err(&pdev->dev, "VBUS rise irq not found\n");
		return ab->irq_num_vbus_rise;
	}
	err = request_threaded_irq(ab->irq_num_vbus_rise, NULL,
		ab8500_usb_v1x_common_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-vbus-rise", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for Vbus rise irq\n");
		goto fail2;
	}

	ab->irq_num_vbus_fall = platform_get_irq_byname(pdev, "VBUS_DET_F");
	if (ab->irq_num_vbus_fall < 0) {
		dev_err(&pdev->dev, "VBUS fall irq not found\n");
		return ab->irq_num_vbus_fall;
	}
	err = request_threaded_irq(ab->irq_num_vbus_fall, NULL,
		ab8500_usb_v1x_vbus_fall_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-vbus-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for Vbus fall irq\n");
		goto fail3;
	}

	return 0;
fail3:
	free_irq(ab->irq_num_vbus_rise, ab);
fail2:
	free_irq(ab->irq_num_id_fall, ab);
fail1:
	free_irq(ab->irq_num_id_rise, ab);
fail0:
	return err;
}

static int ab8500_usb_v2_res_setup(struct platform_device *pdev,
				struct ab8500_usb *ab)
{
	int err;

	ab->irq_num_link_status = platform_get_irq_byname(pdev,
						"USB_LINK_STATUS");
	if (ab->irq_num_link_status < 0) {
		dev_err(&pdev->dev, "Link status irq not found\n");
		return ab->irq_num_link_status;
	}

	err = request_threaded_irq(ab->irq_num_link_status, NULL,
		ab8500_usb_v20_irq,
		IRQF_NO_SUSPEND | IRQF_SHARED,
		"usb-link-status", ab);
	if (err < 0) {
		dev_err(ab->dev,
			"request_irq failed for link status irq\n");
		return err;
	}

	return 0;
}

static int __devinit ab8500_usb_probe(struct platform_device *pdev)
{
	struct ab8500_usb	*ab;
	int err;
	int rev;

	rev = abx500_get_chip_id(&pdev->dev);
	if (rev < 0) {
		dev_err(&pdev->dev, "Chip id read failed\n");
		return rev;
	} else if (rev < 0x10) {
		dev_err(&pdev->dev, "Unsupported AB8500 chip\n");
		return -ENODEV;
	}

	ab = kzalloc(sizeof *ab, GFP_KERNEL);
	if (!ab)
		return -ENOMEM;

	ab->dev			= &pdev->dev;
	ab->rev			= rev;
	ab->otg.dev		= ab->dev;
	ab->otg.label		= "ab8500";
	ab->otg.state		= OTG_STATE_UNDEFINED;
	ab->otg.set_host	= ab8500_usb_set_host;
	ab->otg.set_peripheral	= ab8500_usb_set_peripheral;
	ab->otg.set_suspend	= ab8500_usb_set_suspend;
	ab->otg.set_power	= ab8500_usb_set_power;

	platform_set_drvdata(pdev, ab);

	BLOCKING_INIT_NOTIFIER_HEAD(&ab->otg.notifier);

	/* v1: Wait for link status to become stable.
	 * all: Updates form set_host and set_peripheral as they are atomic.
	 */
	INIT_DELAYED_WORK(&ab->dwork, ab8500_usb_delayed_work);

	/* all: Disable phy when called from set_host and set_peripheral */
	INIT_WORK(&ab->phy_dis_work, ab8500_usb_phy_disable_work);

	if (ab->rev < 0x20) {
		err = ab8500_usb_v1x_res_setup(pdev, ab);
		ab->link_status_wait = AB8500_V1x_LINK_STAT_WAIT;
	} else {
		err = ab8500_usb_v2_res_setup(pdev, ab);
	}

	if (err < 0)
		goto fail0;

	err = otg_set_transceiver(&ab->otg);
	if (err) {
		dev_err(&pdev->dev, "Can't register transceiver\n");
		goto fail1;
	}

	dev_info(&pdev->dev, "AB8500 usb driver initialized\n");

	return 0;
fail1:
	ab8500_usb_irq_free(ab);
fail0:
	kfree(ab);
	return err;
}

static int __devexit ab8500_usb_remove(struct platform_device *pdev)
{
	struct ab8500_usb *ab = platform_get_drvdata(pdev);

	ab8500_usb_irq_free(ab);

	cancel_delayed_work_sync(&ab->dwork);

	cancel_work_sync(&ab->phy_dis_work);

	otg_set_transceiver(NULL);

	ab8500_usb_host_phy_dis(ab);
	ab8500_usb_peri_phy_dis(ab);

	platform_set_drvdata(pdev, NULL);

	kfree(ab);

	return 0;
}

static struct platform_driver ab8500_usb_driver = {
	.probe		= ab8500_usb_probe,
	.remove		= __devexit_p(ab8500_usb_remove),
	.driver		= {
		.name	= "ab8500-usb",
		.owner	= THIS_MODULE,
	},
};

static int __init ab8500_usb_init(void)
{
	return platform_driver_register(&ab8500_usb_driver);
}
subsys_initcall(ab8500_usb_init);

static void __exit ab8500_usb_exit(void)
{
	platform_driver_unregister(&ab8500_usb_driver);
}
module_exit(ab8500_usb_exit);

MODULE_ALIAS("platform:ab8500_usb");
MODULE_AUTHOR("ST-Ericsson AB");
MODULE_DESCRIPTION("AB8500 usb transceiver driver");
MODULE_LICENSE("GPL");
