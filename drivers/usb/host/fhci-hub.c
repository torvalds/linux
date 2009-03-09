/*
 * Freescale QUICC Engine USB Host Controller Driver
 *
 * Copyright (c) Freescale Semicondutor, Inc. 2006.
 *               Shlomi Gridish <gridish@freescale.com>
 *               Jerry Huang <Chang-Ming.Huang@freescale.com>
 * Copyright (c) Logic Product Development, Inc. 2007
 *               Peter Barada <peterb@logicpd.com>
 * Copyright (c) MontaVista Software, Inc. 2008.
 *               Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/usb.h>
#include <linux/gpio.h>
#include <asm/qe.h>
#include "../core/hcd.h"
#include "fhci.h"

/* virtual root hub specific descriptor */
static u8 root_hub_des[] = {
	0x09, /* blength */
	0x29, /* bDescriptorType;hub-descriptor */
	0x01, /* bNbrPorts */
	0x00, /* wHubCharacteristics */
	0x00,
	0x01, /* bPwrOn2pwrGood;2ms */
	0x00, /* bHubContrCurrent;0mA */
	0x00, /* DeviceRemoveable */
	0xff, /* PortPwrCtrlMask */
};

static void fhci_gpio_set_value(struct fhci_hcd *fhci, int gpio_nr, bool on)
{
	int gpio = fhci->gpios[gpio_nr];
	bool alow = fhci->alow_gpios[gpio_nr];

	if (!gpio_is_valid(gpio))
		return;

	gpio_set_value(gpio, on ^ alow);
	mdelay(5);
}

void fhci_config_transceiver(struct fhci_hcd *fhci,
			     enum fhci_port_status status)
{
	fhci_dbg(fhci, "-> %s: %d\n", __func__, status);

	switch (status) {
	case FHCI_PORT_POWER_OFF:
		fhci_gpio_set_value(fhci, GPIO_POWER, false);
		break;
	case FHCI_PORT_DISABLED:
	case FHCI_PORT_WAITING:
		fhci_gpio_set_value(fhci, GPIO_POWER, true);
		break;
	case FHCI_PORT_LOW:
		fhci_gpio_set_value(fhci, GPIO_SPEED, false);
		break;
	case FHCI_PORT_FULL:
		fhci_gpio_set_value(fhci, GPIO_SPEED, true);
		break;
	default:
		WARN_ON(1);
		break;
	}

	fhci_dbg(fhci, "<- %s: %d\n", __func__, status);
}

/* disable the USB port by clearing the EN bit in the USBMOD register */
void fhci_port_disable(struct fhci_hcd *fhci)
{
	struct fhci_usb *usb = (struct fhci_usb *)fhci->usb_lld;
	enum fhci_port_status port_status;

	fhci_dbg(fhci, "-> %s\n", __func__);

	fhci_stop_sof_timer(fhci);

	fhci_flush_all_transmissions(usb);

	fhci_usb_disable_interrupt((struct fhci_usb *)fhci->usb_lld);
	port_status = usb->port_status;
	usb->port_status = FHCI_PORT_DISABLED;

	/* Enable IDLE since we want to know if something comes along */
	usb->saved_msk |= USB_E_IDLE_MASK;
	out_be16(&usb->fhci->regs->usb_mask, usb->saved_msk);

	/* check if during the disconnection process attached new device */
	if (port_status == FHCI_PORT_WAITING)
		fhci_device_connected_interrupt(fhci);
	usb->vroot_hub->port.wPortStatus &= ~USB_PORT_STAT_ENABLE;
	usb->vroot_hub->port.wPortChange |= USB_PORT_STAT_C_ENABLE;
	fhci_usb_enable_interrupt((struct fhci_usb *)fhci->usb_lld);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

/* enable the USB port by setting the EN bit in the USBMOD register */
void fhci_port_enable(void *lld)
{
	struct fhci_usb *usb = (struct fhci_usb *)lld;
	struct fhci_hcd *fhci = usb->fhci;

	fhci_dbg(fhci, "-> %s\n", __func__);

	fhci_config_transceiver(fhci, usb->port_status);

	if ((usb->port_status != FHCI_PORT_FULL) &&
			(usb->port_status != FHCI_PORT_LOW))
		fhci_start_sof_timer(fhci);

	usb->vroot_hub->port.wPortStatus |= USB_PORT_STAT_ENABLE;
	usb->vroot_hub->port.wPortChange |= USB_PORT_STAT_C_ENABLE;

	fhci_dbg(fhci, "<- %s\n", __func__);
}

void fhci_io_port_generate_reset(struct fhci_hcd *fhci)
{
	fhci_dbg(fhci, "-> %s\n", __func__);

	gpio_direction_output(fhci->gpios[GPIO_USBOE], 0);
	gpio_direction_output(fhci->gpios[GPIO_USBTP], 0);
	gpio_direction_output(fhci->gpios[GPIO_USBTN], 0);

	mdelay(5);

	qe_pin_set_dedicated(fhci->pins[PIN_USBOE]);
	qe_pin_set_dedicated(fhci->pins[PIN_USBTP]);
	qe_pin_set_dedicated(fhci->pins[PIN_USBTN]);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

/* generate the RESET condition on the bus */
void fhci_port_reset(void *lld)
{
	struct fhci_usb *usb = (struct fhci_usb *)lld;
	struct fhci_hcd *fhci = usb->fhci;
	u8 mode;
	u16 mask;

	fhci_dbg(fhci, "-> %s\n", __func__);

	fhci_stop_sof_timer(fhci);
	/* disable the USB controller */
	mode = in_8(&fhci->regs->usb_mod);
	out_8(&fhci->regs->usb_mod, mode & (~USB_MODE_EN));

	/* disable idle interrupts */
	mask = in_be16(&fhci->regs->usb_mask);
	out_be16(&fhci->regs->usb_mask, mask & (~USB_E_IDLE_MASK));

	fhci_io_port_generate_reset(fhci);

	/* enable interrupt on this endpoint */
	out_be16(&fhci->regs->usb_mask, mask);

	/* enable the USB controller */
	mode = in_8(&fhci->regs->usb_mod);
	out_8(&fhci->regs->usb_mod, mode | USB_MODE_EN);
	fhci_start_sof_timer(fhci);

	fhci_dbg(fhci, "<- %s\n", __func__);
}

int fhci_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	int ret = 0;
	unsigned long flags;

	fhci_dbg(fhci, "-> %s\n", __func__);

	spin_lock_irqsave(&fhci->lock, flags);

	if (fhci->vroot_hub->port.wPortChange & (USB_PORT_STAT_C_CONNECTION |
			USB_PORT_STAT_C_ENABLE | USB_PORT_STAT_C_SUSPEND |
			USB_PORT_STAT_C_RESET | USB_PORT_STAT_C_OVERCURRENT)) {
		*buf = 1 << 1;
		ret = 1;
		fhci_dbg(fhci, "-- %s\n", __func__);
	}

	spin_unlock_irqrestore(&fhci->lock, flags);

	fhci_dbg(fhci, "<- %s\n", __func__);

	return ret;
}

int fhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			    u16 wIndex, char *buf, u16 wLength)
{
	struct fhci_hcd *fhci = hcd_to_fhci(hcd);
	int retval = 0;
	int len = 0;
	struct usb_hub_status *hub_status;
	struct usb_port_status *port_status;
	unsigned long flags;

	spin_lock_irqsave(&fhci->lock, flags);

	fhci_dbg(fhci, "-> %s\n", __func__);

	switch (typeReq) {
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_LOCAL_POWER:
		case C_HUB_OVER_CURRENT:
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		fhci->vroot_hub->feature &= (1 << wValue);

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			fhci->vroot_hub->port.wPortStatus &=
			    ~USB_PORT_STAT_ENABLE;
			fhci_port_disable(fhci);
			break;
		case USB_PORT_FEAT_C_ENABLE:
			fhci->vroot_hub->port.wPortChange &=
			    ~USB_PORT_STAT_C_ENABLE;
			break;
		case USB_PORT_FEAT_SUSPEND:
			fhci->vroot_hub->port.wPortStatus &=
			    ~USB_PORT_STAT_SUSPEND;
			fhci_stop_sof_timer(fhci);
			break;
		case USB_PORT_FEAT_C_SUSPEND:
			fhci->vroot_hub->port.wPortChange &=
			    ~USB_PORT_STAT_C_SUSPEND;
			break;
		case USB_PORT_FEAT_POWER:
			fhci->vroot_hub->port.wPortStatus &=
			    ~USB_PORT_STAT_POWER;
			fhci_config_transceiver(fhci, FHCI_PORT_POWER_OFF);
			break;
		case USB_PORT_FEAT_C_CONNECTION:
			fhci->vroot_hub->port.wPortChange &=
			    ~USB_PORT_STAT_C_CONNECTION;
			break;
		case USB_PORT_FEAT_C_OVER_CURRENT:
			fhci->vroot_hub->port.wPortChange &=
			    ~USB_PORT_STAT_C_OVERCURRENT;
			break;
		case USB_PORT_FEAT_C_RESET:
			fhci->vroot_hub->port.wPortChange &=
			    ~USB_PORT_STAT_C_RESET;
			break;
		default:
			goto error;
		}
		break;
	case GetHubDescriptor:
		memcpy(buf, root_hub_des, sizeof(root_hub_des));
		buf[3] = 0x11; /* per-port power, no ovrcrnt */
		len = (buf[0] < wLength) ? buf[0] : wLength;
		break;
	case GetHubStatus:
		hub_status = (struct usb_hub_status *)buf;
		hub_status->wHubStatus =
		    cpu_to_le16(fhci->vroot_hub->hub.wHubStatus);
		hub_status->wHubChange =
		    cpu_to_le16(fhci->vroot_hub->hub.wHubChange);
		len = 4;
		break;
	case GetPortStatus:
		port_status = (struct usb_port_status *)buf;
		port_status->wPortStatus =
		    cpu_to_le16(fhci->vroot_hub->port.wPortStatus);
		port_status->wPortChange =
		    cpu_to_le16(fhci->vroot_hub->port.wPortChange);
		len = 4;
		break;
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case SetPortFeature:
		fhci->vroot_hub->feature |= (1 << wValue);

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			fhci->vroot_hub->port.wPortStatus |=
			    USB_PORT_STAT_ENABLE;
			fhci_port_enable(fhci->usb_lld);
			break;
		case USB_PORT_FEAT_SUSPEND:
			fhci->vroot_hub->port.wPortStatus |=
			    USB_PORT_STAT_SUSPEND;
			fhci_stop_sof_timer(fhci);
			break;
		case USB_PORT_FEAT_RESET:
			fhci->vroot_hub->port.wPortStatus |=
			    USB_PORT_STAT_RESET;
			fhci_port_reset(fhci->usb_lld);
			fhci->vroot_hub->port.wPortStatus |=
			    USB_PORT_STAT_ENABLE;
			fhci->vroot_hub->port.wPortStatus &=
			    ~USB_PORT_STAT_RESET;
			break;
		case USB_PORT_FEAT_POWER:
			fhci->vroot_hub->port.wPortStatus |=
			    USB_PORT_STAT_POWER;
			fhci_config_transceiver(fhci, FHCI_PORT_WAITING);
			break;
		default:
			goto error;
		}
		break;
	default:
error:
		retval = -EPIPE;
	}

	fhci_dbg(fhci, "<- %s\n", __func__);

	spin_unlock_irqrestore(&fhci->lock, flags);

	return retval;
}
