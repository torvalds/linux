/*
 * bdc_udc.c - BRCM BDC USB3.0 device controller gagdet ops
 *
 * Copyright (C) 2014 Broadcom Corporation
 *
 * Author: Ashwini Pahuja
 *
 * Based on drivers under drivers/usb/gadget/udc/
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/pm.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <asm/unaligned.h>
#include <linux/platform_device.h>

#include "bdc.h"
#include "bdc_ep.h"
#include "bdc_cmd.h"
#include "bdc_dbg.h"

static const struct usb_gadget_ops bdc_gadget_ops;

static const char * const conn_speed_str[] =  {
	"Not connected",
	"Full Speed",
	"Low Speed",
	"High Speed",
	"Super Speed",
};

/* EP0 initial descripror */
static struct usb_endpoint_descriptor bdc_gadget_ep0_desc = {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bmAttributes = USB_ENDPOINT_XFER_CONTROL,
	.bEndpointAddress = 0,
	.wMaxPacketSize	= cpu_to_le16(EP0_MAX_PKT_SIZE),
};

/* Advance the srr dqp maintained by SW */
static void srr_dqp_index_advc(struct bdc *bdc, u32 srr_num)
{
	struct srr *srr;

	srr = &bdc->srr;
	dev_dbg_ratelimited(bdc->dev, "srr->dqp_index:%d\n", srr->dqp_index);
	srr->dqp_index++;
	/* rollback to 0 if we are past the last */
	if (srr->dqp_index == NUM_SR_ENTRIES)
		srr->dqp_index = 0;
}

/* connect sr */
static void bdc_uspc_connected(struct bdc *bdc)
{
	u32 speed, temp;
	u32 usppms;
	int ret;

	temp = bdc_readl(bdc->regs, BDC_USPC);
	speed = BDC_PSP(temp);
	dev_dbg(bdc->dev, "%s speed=%x\n", __func__, speed);
	switch (speed) {
	case BDC_SPEED_SS:
		bdc_gadget_ep0_desc.wMaxPacketSize =
						cpu_to_le16(EP0_MAX_PKT_SIZE);
		bdc->gadget.ep0->maxpacket = EP0_MAX_PKT_SIZE;
		bdc->gadget.speed = USB_SPEED_SUPER;
		/* Enable U1T in SS mode */
		usppms =  bdc_readl(bdc->regs, BDC_USPPMS);
		usppms &= ~BDC_U1T(0xff);
		usppms |= BDC_U1T(U1_TIMEOUT);
		usppms |= BDC_PORT_W1S;
		bdc_writel(bdc->regs, BDC_USPPMS, usppms);
		break;

	case BDC_SPEED_HS:
		bdc_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(64);
		bdc->gadget.ep0->maxpacket = 64;
		bdc->gadget.speed = USB_SPEED_HIGH;
		break;

	case BDC_SPEED_FS:
		bdc_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(64);
		bdc->gadget.ep0->maxpacket = 64;
		bdc->gadget.speed = USB_SPEED_FULL;
		break;

	case BDC_SPEED_LS:
		bdc_gadget_ep0_desc.wMaxPacketSize = cpu_to_le16(8);
		bdc->gadget.ep0->maxpacket = 8;
		bdc->gadget.speed = USB_SPEED_LOW;
		break;
	default:
		dev_err(bdc->dev, "UNDEFINED SPEED\n");
		return;
	}
	dev_dbg(bdc->dev, "connected at %s\n", conn_speed_str[speed]);
	/* Now we know the speed, configure ep0 */
	bdc->bdc_ep_array[1]->desc = &bdc_gadget_ep0_desc;
	ret = bdc_config_ep(bdc, bdc->bdc_ep_array[1]);
	if (ret)
		dev_err(bdc->dev, "EP0 config failed\n");
	bdc->bdc_ep_array[1]->usb_ep.desc = &bdc_gadget_ep0_desc;
	bdc->bdc_ep_array[1]->flags |= BDC_EP_ENABLED;
	usb_gadget_set_state(&bdc->gadget, USB_STATE_DEFAULT);
}

/* device got disconnected */
static void bdc_uspc_disconnected(struct bdc *bdc, bool reinit)
{
	struct bdc_ep *ep;

	dev_dbg(bdc->dev, "%s\n", __func__);
	/*
	 * Only stop ep0 from here, rest of the endpoints will be disabled
	 * from gadget_disconnect
	 */
	ep = bdc->bdc_ep_array[1];
	if (ep && (ep->flags & BDC_EP_ENABLED))
		/* if enabled then stop and remove requests */
		bdc_ep_disable(ep);

	if (bdc->gadget_driver && bdc->gadget_driver->disconnect) {
		spin_unlock(&bdc->lock);
		bdc->gadget_driver->disconnect(&bdc->gadget);
		spin_lock(&bdc->lock);
	}
	/* Set Unknown speed */
	bdc->gadget.speed = USB_SPEED_UNKNOWN;
	bdc->devstatus &= DEVSTATUS_CLEAR;
	bdc->delayed_status = false;
	bdc->reinit = reinit;
	bdc->test_mode = false;
}

/* TNotify wkaeup timer */
static void bdc_func_wake_timer(struct work_struct *work)
{
	struct bdc *bdc = container_of(work, struct bdc, func_wake_notify.work);
	unsigned long flags;

	dev_dbg(bdc->dev, "%s\n", __func__);
	spin_lock_irqsave(&bdc->lock, flags);
	/*
	 * Check if host has started transferring on endpoints
	 * FUNC_WAKE_ISSUED is cleared when transfer has started after resume
	*/
	if (bdc->devstatus & FUNC_WAKE_ISSUED) {
		dev_dbg(bdc->dev, "FUNC_WAKE_ISSUED FLAG IS STILL SET\n");
		/* flag is still set, so again send func wake */
		bdc_function_wake_fh(bdc, 0);
		schedule_delayed_work(&bdc->func_wake_notify,
						msecs_to_jiffies(BDC_TNOTIFY));
	}
	spin_unlock_irqrestore(&bdc->lock, flags);
}

/* handler for Link state change condition */
static void handle_link_state_change(struct bdc *bdc, u32 uspc)
{
	u32 link_state;

	dev_dbg(bdc->dev, "Link state change");
	link_state = BDC_PST(uspc);
	switch (link_state) {
	case BDC_LINK_STATE_U3:
		if ((bdc->gadget.speed != USB_SPEED_UNKNOWN) &&
						bdc->gadget_driver->suspend) {
			dev_dbg(bdc->dev, "Entered Suspend mode\n");
			spin_unlock(&bdc->lock);
			bdc->devstatus |= DEVICE_SUSPENDED;
			bdc->gadget_driver->suspend(&bdc->gadget);
			spin_lock(&bdc->lock);
		}
		break;
	case BDC_LINK_STATE_U0:
		if (bdc->devstatus & REMOTE_WAKEUP_ISSUED) {
					bdc->devstatus &= ~REMOTE_WAKEUP_ISSUED;
			if (bdc->gadget.speed == USB_SPEED_SUPER) {
				bdc_function_wake_fh(bdc, 0);
				bdc->devstatus |= FUNC_WAKE_ISSUED;
				/*
				 * Start a Notification timer and check if the
				 * Host transferred anything on any of the EPs,
				 * if not then send function wake again every
				 * TNotification secs until host initiates
				 * transfer to BDC, USB3 spec Table 8.13
				*/
				schedule_delayed_work(
						&bdc->func_wake_notify,
						msecs_to_jiffies(BDC_TNOTIFY));
				dev_dbg(bdc->dev, "sched func_wake_notify\n");
			}
		}
		break;

	case BDC_LINK_STATE_RESUME:
		dev_dbg(bdc->dev, "Resumed from Suspend\n");
		if (bdc->devstatus & DEVICE_SUSPENDED) {
			bdc->gadget_driver->resume(&bdc->gadget);
			bdc->devstatus &= ~DEVICE_SUSPENDED;
		}
		break;
	default:
		dev_dbg(bdc->dev, "link state:%d\n", link_state);
	}
}

/* something changes on upstream port, handle it here */
void bdc_sr_uspc(struct bdc *bdc, struct bdc_sr *sreport)
{
	u32 clear_flags = 0;
	u32 uspc;
	bool connected = false;
	bool disconn = false;

	uspc = bdc_readl(bdc->regs, BDC_USPC);
	dev_dbg(bdc->dev, "%s uspc=0x%08x\n", __func__, uspc);

	/* Port connect changed */
	if (uspc & BDC_PCC) {
		/* Vbus not present, and not connected to Downstream port */
		if ((uspc & BDC_VBC) && !(uspc & BDC_VBS) && !(uspc & BDC_PCS))
			disconn = true;
		else if ((uspc & BDC_PCS) && !BDC_PST(uspc))
			connected = true;
	}

	/* Change in VBus and VBus is present */
	if ((uspc & BDC_VBC) && (uspc & BDC_VBS)) {
		if (bdc->pullup) {
			dev_dbg(bdc->dev, "Do a softconnect\n");
			/* Attached state, do a softconnect */
			bdc_softconn(bdc);
			usb_gadget_set_state(&bdc->gadget, USB_STATE_POWERED);
		}
		clear_flags = BDC_VBC;
	} else if ((uspc & BDC_PRS) || (uspc & BDC_PRC) || disconn) {
		/* Hot reset, warm reset, 2.0 bus reset or disconn */
		dev_dbg(bdc->dev, "Port reset or disconn\n");
		bdc_uspc_disconnected(bdc, disconn);
		clear_flags = BDC_PCC|BDC_PCS|BDC_PRS|BDC_PRC;
	} else if ((uspc & BDC_PSC) && (uspc & BDC_PCS)) {
		/* Change in Link state */
		handle_link_state_change(bdc, uspc);
		clear_flags = BDC_PSC|BDC_PCS;
	}

	/*
	 * In SS we might not have PRC bit set before connection, but in 2.0
	 * the PRC bit is set before connection, so moving this condition out
	 * of bus reset to handle both SS/2.0 speeds.
	 */
	if (connected) {
		/* This is the connect event for U0/L0 */
		dev_dbg(bdc->dev, "Connected\n");
		bdc_uspc_connected(bdc);
		bdc->devstatus &= ~(DEVICE_SUSPENDED);
	}
	uspc = bdc_readl(bdc->regs, BDC_USPC);
	uspc &= (~BDC_USPSC_RW);
	dev_dbg(bdc->dev, "uspc=%x\n", uspc);
	bdc_writel(bdc->regs, BDC_USPC, clear_flags);
}

/* Main interrupt handler for bdc */
static irqreturn_t bdc_udc_interrupt(int irq, void *_bdc)
{
	u32 eqp_index, dqp_index, sr_type, srr_int;
	struct bdc_sr *sreport;
	struct bdc *bdc = _bdc;
	u32 status;
	int ret;

	spin_lock(&bdc->lock);
	status = bdc_readl(bdc->regs, BDC_BDCSC);
	if (!(status & BDC_GIP)) {
		spin_unlock(&bdc->lock);
		return IRQ_NONE;
	}
	srr_int = bdc_readl(bdc->regs, BDC_SRRINT(0));
	/* Check if the SRR IP bit it set? */
	if (!(srr_int & BDC_SRR_IP)) {
		dev_warn(bdc->dev, "Global irq pending but SRR IP is 0\n");
		spin_unlock(&bdc->lock);
		return IRQ_NONE;
	}
	eqp_index = BDC_SRR_EPI(srr_int);
	dqp_index = BDC_SRR_DPI(srr_int);
	dev_dbg(bdc->dev,
			"%s eqp_index=%d dqp_index=%d  srr.dqp_index=%d\n\n",
			 __func__, eqp_index, dqp_index, bdc->srr.dqp_index);

	/* check for ring empty condition */
	if (eqp_index == dqp_index) {
		dev_dbg(bdc->dev, "SRR empty?\n");
		spin_unlock(&bdc->lock);
		return IRQ_HANDLED;
	}

	while (bdc->srr.dqp_index != eqp_index) {
		sreport = &bdc->srr.sr_bds[bdc->srr.dqp_index];
		/* sreport is read before using it */
		rmb();
		sr_type = le32_to_cpu(sreport->offset[3]) & BD_TYPE_BITMASK;
		dev_dbg_ratelimited(bdc->dev, "sr_type=%d\n", sr_type);
		switch (sr_type) {
		case SR_XSF:
			bdc->sr_handler[0](bdc, sreport);
			break;

		case SR_USPC:
			bdc->sr_handler[1](bdc, sreport);
			break;
		default:
			dev_warn(bdc->dev, "SR:%d not handled\n", sr_type);
		}
		/* Advance the srr dqp index */
		srr_dqp_index_advc(bdc, 0);
	}
	/* update the hw dequeue pointer */
	srr_int = bdc_readl(bdc->regs, BDC_SRRINT(0));
	srr_int &= ~BDC_SRR_DPI_MASK;
	srr_int &= ~(BDC_SRR_RWS|BDC_SRR_RST|BDC_SRR_ISR);
	srr_int |= ((bdc->srr.dqp_index) << 16);
	srr_int |= BDC_SRR_IP;
	bdc_writel(bdc->regs, BDC_SRRINT(0), srr_int);
	srr_int = bdc_readl(bdc->regs, BDC_SRRINT(0));
	if (bdc->reinit) {
		ret = bdc_reinit(bdc);
		if (ret)
			dev_err(bdc->dev, "err in bdc reinit\n");
	}

	spin_unlock(&bdc->lock);

	return IRQ_HANDLED;
}

/* Gadget ops */
static int bdc_udc_start(struct usb_gadget *gadget,
				struct usb_gadget_driver *driver)
{
	struct bdc *bdc = gadget_to_bdc(gadget);
	unsigned long flags;
	int ret = 0;

	dev_dbg(bdc->dev, "%s()\n", __func__);
	spin_lock_irqsave(&bdc->lock, flags);
	if (bdc->gadget_driver) {
		dev_err(bdc->dev, "%s is already bound to %s\n",
			bdc->gadget.name,
			bdc->gadget_driver->driver.name);
		ret = -EBUSY;
		goto err;
	}
	/*
	 * Run the controller from here and when BDC is connected to
	 * Host then driver will receive a USPC SR with VBUS present
	 * and then driver will do a softconnect.
	*/
	ret = bdc_run(bdc);
	if (ret) {
		dev_err(bdc->dev, "%s bdc run fail\n", __func__);
		goto err;
	}
	bdc->gadget_driver = driver;
	bdc->gadget.dev.driver = &driver->driver;
err:
	spin_unlock_irqrestore(&bdc->lock, flags);

	return ret;
}

static int bdc_udc_stop(struct usb_gadget *gadget)
{
	struct bdc *bdc = gadget_to_bdc(gadget);
	unsigned long flags;

	dev_dbg(bdc->dev, "%s()\n", __func__);
	spin_lock_irqsave(&bdc->lock, flags);
	bdc_stop(bdc);
	bdc->gadget_driver = NULL;
	bdc->gadget.dev.driver = NULL;
	spin_unlock_irqrestore(&bdc->lock, flags);

	return 0;
}

static int bdc_udc_pullup(struct usb_gadget *gadget, int is_on)
{
	struct bdc *bdc = gadget_to_bdc(gadget);
	unsigned long flags;
	u32 uspc;

	dev_dbg(bdc->dev, "%s() is_on:%d\n", __func__, is_on);
	if (!gadget)
		return -EINVAL;

	spin_lock_irqsave(&bdc->lock, flags);
	if (!is_on) {
		bdc_softdisconn(bdc);
		bdc->pullup = false;
	} else {
		/*
		 * For a self powered device, we need to wait till we receive
		 * a VBUS change and Vbus present event, then if pullup flag
		 * is set, then only we present the Termintation.
		 */
		bdc->pullup = true;
		/*
		 * Check if BDC is already connected to Host i.e Vbus=1,
		 * if yes, then present TERM now, this is typical for bus
		 * powered devices.
		 */
		uspc = bdc_readl(bdc->regs, BDC_USPC);
		if (uspc & BDC_VBS)
			bdc_softconn(bdc);
	}
	spin_unlock_irqrestore(&bdc->lock, flags);

	return 0;
}

static int bdc_udc_set_selfpowered(struct usb_gadget *gadget,
		int is_self)
{
	struct bdc		*bdc = gadget_to_bdc(gadget);
	unsigned long           flags;

	dev_dbg(bdc->dev, "%s()\n", __func__);
	spin_lock_irqsave(&bdc->lock, flags);
	if (!is_self)
		bdc->devstatus |= 1 << USB_DEVICE_SELF_POWERED;
	else
		bdc->devstatus &= ~(1 << USB_DEVICE_SELF_POWERED);

	spin_unlock_irqrestore(&bdc->lock, flags);

	return 0;
}

static int bdc_udc_wakeup(struct usb_gadget *gadget)
{
	struct bdc *bdc = gadget_to_bdc(gadget);
	unsigned long		flags;
	u8	link_state;
	u32	uspc;
	int ret = 0;

	dev_dbg(bdc->dev,
		"%s() bdc->devstatus=%08x\n",
		__func__, bdc->devstatus);

	if (!(bdc->devstatus & REMOTE_WAKE_ENABLE))
		return  -EOPNOTSUPP;

	spin_lock_irqsave(&bdc->lock, flags);
	uspc = bdc_readl(bdc->regs, BDC_USPC);
	link_state = BDC_PST(uspc);
	dev_dbg(bdc->dev, "link_state =%d portsc=%x", link_state, uspc);
	if (link_state != BDC_LINK_STATE_U3) {
		dev_warn(bdc->dev,
			"can't wakeup from link state %d\n",
			link_state);
		ret = -EINVAL;
		goto out;
	}
	if (bdc->gadget.speed == USB_SPEED_SUPER)
		bdc->devstatus |= REMOTE_WAKEUP_ISSUED;

	uspc &= ~BDC_PST_MASK;
	uspc &= (~BDC_USPSC_RW);
	uspc |=  BDC_PST(BDC_LINK_STATE_U0);
	uspc |=  BDC_SWS;
	bdc_writel(bdc->regs, BDC_USPC, uspc);
	uspc = bdc_readl(bdc->regs, BDC_USPC);
	link_state = BDC_PST(uspc);
	dev_dbg(bdc->dev, "link_state =%d portsc=%x", link_state, uspc);
out:
	spin_unlock_irqrestore(&bdc->lock, flags);

	return ret;
}

static const struct usb_gadget_ops bdc_gadget_ops = {
	.wakeup = bdc_udc_wakeup,
	.set_selfpowered = bdc_udc_set_selfpowered,
	.pullup = bdc_udc_pullup,
	.udc_start = bdc_udc_start,
	.udc_stop = bdc_udc_stop,
};

/* Init the gadget interface and register the udc */
int bdc_udc_init(struct bdc *bdc)
{
	u32 temp;
	int ret;

	dev_dbg(bdc->dev, "%s()\n", __func__);
	bdc->gadget.ops = &bdc_gadget_ops;
	bdc->gadget.max_speed = USB_SPEED_SUPER;
	bdc->gadget.speed = USB_SPEED_UNKNOWN;
	bdc->gadget.dev.parent = bdc->dev;

	bdc->gadget.sg_supported = false;


	bdc->gadget.name = BRCM_BDC_NAME;
	ret = devm_request_irq(bdc->dev, bdc->irq, bdc_udc_interrupt,
				IRQF_SHARED , BRCM_BDC_NAME, bdc);
	if (ret) {
		dev_err(bdc->dev,
			"failed to request irq #%d %d\n",
			bdc->irq, ret);
		return ret;
	}

	ret = bdc_init_ep(bdc);
	if (ret) {
		dev_err(bdc->dev, "bdc init ep fail: %d\n", ret);
		return ret;
	}

	ret = usb_add_gadget_udc(bdc->dev, &bdc->gadget);
	if (ret) {
		dev_err(bdc->dev, "failed to register udc\n");
		goto err0;
	}
	usb_gadget_set_state(&bdc->gadget, USB_STATE_NOTATTACHED);
	bdc->bdc_ep_array[1]->desc = &bdc_gadget_ep0_desc;
	/*
	 * Allocate bd list for ep0 only, ep0 will be enabled on connect
	 * status report when the speed is known
	 */
	ret = bdc_ep_enable(bdc->bdc_ep_array[1]);
	if (ret) {
		dev_err(bdc->dev, "fail to enable %s\n",
						bdc->bdc_ep_array[1]->name);
		goto err1;
	}
	INIT_DELAYED_WORK(&bdc->func_wake_notify, bdc_func_wake_timer);
	/* Enable Interrupts */
	temp = bdc_readl(bdc->regs, BDC_BDCSC);
	temp |= BDC_GIE;
	bdc_writel(bdc->regs, BDC_BDCSC, temp);
	return 0;
err1:
	usb_del_gadget_udc(&bdc->gadget);
err0:
	bdc_free_ep(bdc);

	return ret;
}

void bdc_udc_exit(struct bdc *bdc)
{
	dev_dbg(bdc->dev, "%s()\n", __func__);
	bdc_ep_disable(bdc->bdc_ep_array[1]);
	usb_del_gadget_udc(&bdc->gadget);
	bdc_free_ep(bdc);
}
