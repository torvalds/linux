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
#include <linux/mfd/abx500/ab8500.h>
#include <linux/usb/musb-ux500.h>

#define AB8500_MAIN_WD_CTRL_REG 0x01
#define AB8500_USB_LINE_STAT_REG 0x80
#define AB8505_USB_LINE_STAT_REG 0x94
#define AB8500_USB_PHY_CTRL_REG 0x8A

#define AB8500_BIT_OTG_STAT_ID (1 << 0)
#define AB8500_BIT_PHY_CTRL_HOST_EN (1 << 0)
#define AB8500_BIT_PHY_CTRL_DEVICE_EN (1 << 1)
#define AB8500_BIT_WD_CTRL_ENABLE (1 << 0)
#define AB8500_BIT_WD_CTRL_KICK (1 << 1)

#define AB8500_WD_KICK_DELAY_US 100 /* usec */
#define AB8500_WD_V11_DISABLE_DELAY_US 100 /* usec */
#define AB8500_V20_31952_DISABLE_DELAY_US 100 /* usec */

/* Usb line status register */
enum ab8500_usb_link_status {
	USB_LINK_NOT_CONFIGURED_8500 = 0,
	USB_LINK_STD_HOST_NC_8500,
	USB_LINK_STD_HOST_C_NS_8500,
	USB_LINK_STD_HOST_C_S_8500,
	USB_LINK_HOST_CHG_NM_8500,
	USB_LINK_HOST_CHG_HS_8500,
	USB_LINK_HOST_CHG_HS_CHIRP_8500,
	USB_LINK_DEDICATED_CHG_8500,
	USB_LINK_ACA_RID_A_8500,
	USB_LINK_ACA_RID_B_8500,
	USB_LINK_ACA_RID_C_NM_8500,
	USB_LINK_ACA_RID_C_HS_8500,
	USB_LINK_ACA_RID_C_HS_CHIRP_8500,
	USB_LINK_HM_IDGND_8500,
	USB_LINK_RESERVED_8500,
	USB_LINK_NOT_VALID_LINK_8500,
};

enum ab8505_usb_link_status {
	USB_LINK_NOT_CONFIGURED_8505 = 0,
	USB_LINK_STD_HOST_NC_8505,
	USB_LINK_STD_HOST_C_NS_8505,
	USB_LINK_STD_HOST_C_S_8505,
	USB_LINK_CDP_8505,
	USB_LINK_RESERVED0_8505,
	USB_LINK_RESERVED1_8505,
	USB_LINK_DEDICATED_CHG_8505,
	USB_LINK_ACA_RID_A_8505,
	USB_LINK_ACA_RID_B_8505,
	USB_LINK_ACA_RID_C_NM_8505,
	USB_LINK_RESERVED2_8505,
	USB_LINK_RESERVED3_8505,
	USB_LINK_HM_IDGND_8505,
	USB_LINK_CHARGERPORT_NOT_OK_8505,
	USB_LINK_CHARGER_DM_HIGH_8505,
	USB_LINK_PHYEN_NO_VBUS_NO_IDGND_8505,
	USB_LINK_STD_UPSTREAM_NO_IDGNG_NO_VBUS_8505,
	USB_LINK_STD_UPSTREAM_8505,
	USB_LINK_CHARGER_SE1_8505,
	USB_LINK_CARKIT_CHGR_1_8505,
	USB_LINK_CARKIT_CHGR_2_8505,
	USB_LINK_ACA_DOCK_CHGR_8505,
	USB_LINK_SAMSUNG_BOOT_CBL_PHY_EN_8505,
	USB_LINK_SAMSUNG_BOOT_CBL_PHY_DISB_8505,
	USB_LINK_SAMSUNG_UART_CBL_PHY_EN_8505,
	USB_LINK_SAMSUNG_UART_CBL_PHY_DISB_8505,
	USB_LINK_MOTOROLA_FACTORY_CBL_PHY_EN_8505,
};

enum ab8500_usb_mode {
	USB_IDLE = 0,
	USB_PERIPHERAL,
	USB_HOST,
	USB_DEDICATED_CHG
};

struct ab8500_usb {
	struct usb_phy phy;
	struct device *dev;
	struct ab8500 *ab8500;
	unsigned vbus_draw;
	struct delayed_work dwork;
	struct work_struct phy_dis_work;
	unsigned long link_status_wait;
	enum ab8500_usb_mode mode;
	int previous_link_status_state;
};

static inline struct ab8500_usb *phy_to_ab(struct usb_phy *x)
{
	return container_of(x, struct ab8500_usb, phy);
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

	udelay(AB8500_WD_V11_DISABLE_DELAY_US);

	abx500_set_register_interruptible(ab->dev,
		AB8500_SYS_CTRL2_BLOCK,
		AB8500_MAIN_WD_CTRL_REG,
		0);
}

static void ab8500_usb_wd_linkstatus(struct ab8500_usb *ab, u8 bit)
{
	/* Workaround for v2.0 bug # 31952 */
	if (is_ab8500_2p0(ab->ab8500)) {
		abx500_mask_and_set_register_interruptible(ab->dev,
				AB8500_USB, AB8500_USB_PHY_CTRL_REG,
				bit, bit);
		udelay(AB8500_V20_31952_DISABLE_DELAY_US);
	}
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

static int ab8505_usb_link_status_update(struct ab8500_usb *ab,
		enum ab8505_usb_link_status lsts)
{
	enum ux500_musb_vbus_id_status event = 0;

	dev_dbg(ab->dev, "ab8505_usb_link_status_update %d\n", lsts);

	/*
	 * Spurious link_status interrupts are seen at the time of
	 * disconnection of a device in RIDA state
	 */
	if (ab->previous_link_status_state == USB_LINK_ACA_RID_A_8505 &&
			(lsts == USB_LINK_STD_HOST_NC_8505))
		return 0;

	ab->previous_link_status_state = lsts;

	switch (lsts) {
	case USB_LINK_ACA_RID_B_8505:
		event = UX500_MUSB_RIDB;
	case USB_LINK_NOT_CONFIGURED_8505:
	case USB_LINK_RESERVED0_8505:
	case USB_LINK_RESERVED1_8505:
	case USB_LINK_RESERVED2_8505:
	case USB_LINK_RESERVED3_8505:
		ab->mode = USB_IDLE;
		ab->phy.otg->default_a = false;
		ab->vbus_draw = 0;
		if (event != UX500_MUSB_RIDB)
			event = UX500_MUSB_NONE;
		/*
		 * Fallback to default B_IDLE as nothing
		 * is connected
		 */
		ab->phy.state = OTG_STATE_B_IDLE;
		break;

	case USB_LINK_ACA_RID_C_NM_8505:
		event = UX500_MUSB_RIDC;
	case USB_LINK_STD_HOST_NC_8505:
	case USB_LINK_STD_HOST_C_NS_8505:
	case USB_LINK_STD_HOST_C_S_8505:
	case USB_LINK_CDP_8505:
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->phy.notifier,
					UX500_MUSB_PREPARE, &ab->vbus_draw);
		}
		if (event != UX500_MUSB_RIDC)
			event = UX500_MUSB_VBUS;
		break;

	case USB_LINK_ACA_RID_A_8505:
	case USB_LINK_ACA_DOCK_CHGR_8505:
		event = UX500_MUSB_RIDA;
	case USB_LINK_HM_IDGND_8505:
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_HOST;
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->phy.notifier,
					UX500_MUSB_PREPARE, &ab->vbus_draw);
		}
		ab->phy.otg->default_a = true;
		if (event != UX500_MUSB_RIDA)
			event = UX500_MUSB_ID;
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		break;

	case USB_LINK_DEDICATED_CHG_8505:
		ab->mode = USB_DEDICATED_CHG;
		event = UX500_MUSB_CHARGER;
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		break;

	default:
		break;
	}

	return 0;
}

static int ab8500_usb_link_status_update(struct ab8500_usb *ab,
		enum ab8500_usb_link_status lsts)
{
	enum ux500_musb_vbus_id_status event = 0;

	dev_dbg(ab->dev, "ab8500_usb_link_status_update %d\n", lsts);

	/*
	 * Spurious link_status interrupts are seen in case of a
	 * disconnection of a device in IDGND and RIDA stage
	 */
	if (ab->previous_link_status_state == USB_LINK_HM_IDGND_8500 &&
			(lsts == USB_LINK_STD_HOST_C_NS_8500 ||
			 lsts == USB_LINK_STD_HOST_NC_8500))
		return 0;

	if (ab->previous_link_status_state == USB_LINK_ACA_RID_A_8500 &&
			lsts == USB_LINK_STD_HOST_NC_8500)
		return 0;

	ab->previous_link_status_state = lsts;

	switch (lsts) {
	case USB_LINK_ACA_RID_B_8500:
		event = UX500_MUSB_RIDB;
	case USB_LINK_NOT_CONFIGURED_8500:
	case USB_LINK_NOT_VALID_LINK_8500:
		ab->mode = USB_IDLE;
		ab->phy.otg->default_a = false;
		ab->vbus_draw = 0;
		if (event != UX500_MUSB_RIDB)
			event = UX500_MUSB_NONE;
		/* Fallback to default B_IDLE as nothing is connected */
		ab->phy.state = OTG_STATE_B_IDLE;
		break;

	case USB_LINK_ACA_RID_C_NM_8500:
	case USB_LINK_ACA_RID_C_HS_8500:
	case USB_LINK_ACA_RID_C_HS_CHIRP_8500:
		event = UX500_MUSB_RIDC;
	case USB_LINK_STD_HOST_NC_8500:
	case USB_LINK_STD_HOST_C_NS_8500:
	case USB_LINK_STD_HOST_C_S_8500:
	case USB_LINK_HOST_CHG_NM_8500:
	case USB_LINK_HOST_CHG_HS_8500:
	case USB_LINK_HOST_CHG_HS_CHIRP_8500:
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_PERIPHERAL;
			ab8500_usb_peri_phy_en(ab);
			atomic_notifier_call_chain(&ab->phy.notifier,
					UX500_MUSB_PREPARE, &ab->vbus_draw);
		}
		if (event != UX500_MUSB_RIDC)
			event = UX500_MUSB_VBUS;
		break;

	case USB_LINK_ACA_RID_A_8500:
		event = UX500_MUSB_RIDA;
	case USB_LINK_HM_IDGND_8500:
		if (ab->mode == USB_IDLE) {
			ab->mode = USB_HOST;
			ab8500_usb_host_phy_en(ab);
			atomic_notifier_call_chain(&ab->phy.notifier,
					UX500_MUSB_PREPARE, &ab->vbus_draw);
		}
		ab->phy.otg->default_a = true;
		if (event != UX500_MUSB_RIDA)
			event = UX500_MUSB_ID;
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		break;

	case USB_LINK_DEDICATED_CHG_8500:
		ab->mode = USB_DEDICATED_CHG;
		event = UX500_MUSB_CHARGER;
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		break;

	case USB_LINK_RESERVED_8500:
		break;
	}

	return 0;
}

/*
 * Connection Sequence:
 *   1. Link Status Interrupt
 *   2. Enable AB clock
 *   3. Enable AB regulators
 *   4. Enable USB phy
 *   5. Reset the musb controller
 *   6. Switch the ULPI GPIO pins to fucntion mode
 *   7. Enable the musb Peripheral5 clock
 *   8. Restore MUSB context
 */
static int abx500_usb_link_status_update(struct ab8500_usb *ab)
{
	u8 reg;
	int ret = 0;

	if (is_ab8500(ab->ab8500)) {
		enum ab8500_usb_link_status lsts;

		abx500_get_register_interruptible(ab->dev,
				AB8500_USB, AB8500_USB_LINE_STAT_REG, &reg);
		lsts = (reg >> 3) & 0x0F;
		ret = ab8500_usb_link_status_update(ab, lsts);
	} else if (is_ab8505(ab->ab8500)) {
		enum ab8505_usb_link_status lsts;

		abx500_get_register_interruptible(ab->dev,
				AB8500_USB, AB8505_USB_LINE_STAT_REG, &reg);
		lsts = (reg >> 3) & 0x1F;
		ret = ab8505_usb_link_status_update(ab, lsts);
	}

	return ret;
}

/*
 * Disconnection Sequence:
 *   1. Disconect Interrupt
 *   2. Disable regulators
 *   3. Disable AB clock
 *   4. Disable the Phy
 *   5. Link Status Interrupt
 *   6. Disable Musb Clock
 */
static irqreturn_t ab8500_usb_disconnect_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;
	enum usb_phy_events event = UX500_MUSB_NONE;

	/* Link status will not be updated till phy is disabled. */
	if (ab->mode == USB_HOST) {
		ab->phy.otg->default_a = false;
		ab->vbus_draw = 0;
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		ab8500_usb_host_phy_dis(ab);
		ab->mode = USB_IDLE;
	}

	if (ab->mode == USB_PERIPHERAL) {
		atomic_notifier_call_chain(&ab->phy.notifier,
				event, &ab->vbus_draw);
		ab8500_usb_peri_phy_dis(ab);
		atomic_notifier_call_chain(&ab->phy.notifier,
				UX500_MUSB_CLEAN, &ab->vbus_draw);
		ab->mode = USB_IDLE;
		ab->phy.otg->default_a = false;
		ab->vbus_draw = 0;
	}

	if (is_ab8500_2p0(ab->ab8500)) {
		if (ab->mode == USB_DEDICATED_CHG) {
			ab8500_usb_wd_linkstatus(ab,
					AB8500_BIT_PHY_CTRL_DEVICE_EN);
			abx500_mask_and_set_register_interruptible(ab->dev,
					AB8500_USB, AB8500_USB_PHY_CTRL_REG,
					AB8500_BIT_PHY_CTRL_DEVICE_EN, 0);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t ab8500_usb_link_status_irq(int irq, void *data)
{
	struct ab8500_usb *ab = (struct ab8500_usb *) data;

	abx500_usb_link_status_update(ab);

	return IRQ_HANDLED;
}

static void ab8500_usb_delayed_work(struct work_struct *work)
{
	struct ab8500_usb *ab = container_of(work, struct ab8500_usb,
						dwork.work);

	abx500_usb_link_status_update(ab);
}

static void ab8500_usb_phy_disable_work(struct work_struct *work)
{
	struct ab8500_usb *ab = container_of(work, struct ab8500_usb,
						phy_dis_work);

	if (!ab->phy.otg->host)
		ab8500_usb_host_phy_dis(ab);

	if (!ab->phy.otg->gadget)
		ab8500_usb_peri_phy_dis(ab);
}

static int ab8500_usb_set_power(struct usb_phy *phy, unsigned mA)
{
	struct ab8500_usb *ab;

	if (!phy)
		return -ENODEV;

	ab = phy_to_ab(phy);

	ab->vbus_draw = mA;

	if (mA)
		atomic_notifier_call_chain(&ab->phy.notifier,
				UX500_MUSB_ENUMERATED, ab->phy.otg->gadget);
	return 0;
}

/* TODO: Implement some way for charging or other drivers to read
 * ab->vbus_draw.
 */

static int ab8500_usb_set_suspend(struct usb_phy *x, int suspend)
{
	/* TODO */
	return 0;
}

static int ab8500_usb_set_peripheral(struct usb_otg *otg,
					struct usb_gadget *gadget)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = phy_to_ab(otg->phy);

	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */

	if (!gadget) {
		/* TODO: Disable regulators. */
		otg->gadget = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		otg->gadget = gadget;
		otg->phy->state = OTG_STATE_B_IDLE;

		/* Phy will not be enabled if cable is already
		 * plugged-in. Schedule to enable phy.
		 * Use same delay to avoid any race condition.
		 */
		schedule_delayed_work(&ab->dwork, ab->link_status_wait);
	}

	return 0;
}

static int ab8500_usb_set_host(struct usb_otg *otg, struct usb_bus *host)
{
	struct ab8500_usb *ab;

	if (!otg)
		return -ENODEV;

	ab = phy_to_ab(otg->phy);

	/* Some drivers call this function in atomic context.
	 * Do not update ab8500 registers directly till this
	 * is fixed.
	 */

	if (!host) {
		/* TODO: Disable regulators. */
		otg->host = NULL;
		schedule_work(&ab->phy_dis_work);
	} else {
		otg->host = host;
		/* Phy will not be enabled if cable is already
		 * plugged-in. Schedule to enable phy.
		 * Use same delay to avoid any race condition.
		 */
		schedule_delayed_work(&ab->dwork, ab->link_status_wait);
	}

	return 0;
}

static int ab8500_usb_irq_setup(struct platform_device *pdev,
		struct ab8500_usb *ab)
{
	int err;
	int irq;

	irq = platform_get_irq_byname(pdev, "USB_LINK_STATUS");
	if (irq < 0) {
		dev_err(&pdev->dev, "Link status irq not found\n");
		return irq;
	}
	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
			ab8500_usb_link_status_irq,
			IRQF_NO_SUSPEND | IRQF_SHARED, "usb-link-status", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for link status irq\n");
		return err;
	}

	irq = platform_get_irq_byname(pdev, "ID_WAKEUP_F");
	if (irq < 0) {
		dev_err(&pdev->dev, "ID fall irq not found\n");
		return irq;
	}
	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
			ab8500_usb_disconnect_irq,
			IRQF_NO_SUSPEND | IRQF_SHARED, "usb-id-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for ID fall irq\n");
		return err;
	}

	irq = platform_get_irq_byname(pdev, "VBUS_DET_F");
	if (irq < 0) {
		dev_err(&pdev->dev, "VBUS fall irq not found\n");
		return irq;
	}
	err = devm_request_threaded_irq(&pdev->dev, irq, NULL,
			ab8500_usb_disconnect_irq,
			IRQF_NO_SUSPEND | IRQF_SHARED, "usb-vbus-fall", ab);
	if (err < 0) {
		dev_err(ab->dev, "request_irq failed for Vbus fall irq\n");
		return err;
	}

	return 0;
}

static int ab8500_usb_probe(struct platform_device *pdev)
{
	struct ab8500_usb	*ab;
	struct ab8500		*ab8500;
	struct usb_otg		*otg;
	int err;
	int rev;

	ab8500 = dev_get_drvdata(pdev->dev.parent);
	rev = abx500_get_chip_id(&pdev->dev);

	if (is_ab8500_1p1_or_earlier(ab8500)) {
		dev_err(&pdev->dev, "Unsupported AB8500 chip rev=%d\n", rev);
		return -ENODEV;
	}

	ab = kzalloc(sizeof *ab, GFP_KERNEL);
	if (!ab)
		return -ENOMEM;

	otg = kzalloc(sizeof *otg, GFP_KERNEL);
	if (!otg) {
		kfree(ab);
		return -ENOMEM;
	}

	ab->dev			= &pdev->dev;
	ab->ab8500		= ab8500;
	ab->phy.dev		= ab->dev;
	ab->phy.otg		= otg;
	ab->phy.label		= "ab8500";
	ab->phy.set_suspend	= ab8500_usb_set_suspend;
	ab->phy.set_power	= ab8500_usb_set_power;
	ab->phy.state		= OTG_STATE_UNDEFINED;

	otg->phy		= &ab->phy;
	otg->set_host		= ab8500_usb_set_host;
	otg->set_peripheral	= ab8500_usb_set_peripheral;

	platform_set_drvdata(pdev, ab);

	ATOMIC_INIT_NOTIFIER_HEAD(&ab->phy.notifier);

	/* v1: Wait for link status to become stable.
	 * all: Updates form set_host and set_peripheral as they are atomic.
	 */
	INIT_DELAYED_WORK(&ab->dwork, ab8500_usb_delayed_work);

	/* all: Disable phy when called from set_host and set_peripheral */
	INIT_WORK(&ab->phy_dis_work, ab8500_usb_phy_disable_work);

	err = ab8500_usb_irq_setup(pdev, ab);
	if (err < 0)
		goto fail;

	err = usb_add_phy(&ab->phy, USB_PHY_TYPE_USB2);
	if (err) {
		dev_err(&pdev->dev, "Can't register transceiver\n");
		goto fail;
	}

	/* Needed to enable ID detection. */
	ab8500_usb_wd_workaround(ab);

	dev_info(&pdev->dev, "revision 0x%2x driver initialized\n", rev);

	return 0;
fail:
	kfree(otg);
	kfree(ab);
	return err;
}

static int ab8500_usb_remove(struct platform_device *pdev)
{
	struct ab8500_usb *ab = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&ab->dwork);

	cancel_work_sync(&ab->phy_dis_work);

	usb_remove_phy(&ab->phy);

	ab8500_usb_host_phy_dis(ab);
	ab8500_usb_peri_phy_dis(ab);

	platform_set_drvdata(pdev, NULL);

	kfree(ab->phy.otg);
	kfree(ab);

	return 0;
}

static struct platform_driver ab8500_usb_driver = {
	.probe		= ab8500_usb_probe,
	.remove		= ab8500_usb_remove,
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
