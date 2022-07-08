// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 pureLiFi
 */

#include <linux/kernel.h>
#include <linux/errno.h>

#include "chip.h"
#include "mac.h"
#include "usb.h"

void plfxlc_chip_init(struct plfxlc_chip *chip,
		      struct ieee80211_hw *hw,
		      struct usb_interface *intf)
{
	memset(chip, 0, sizeof(*chip));
	mutex_init(&chip->mutex);
	plfxlc_usb_init(&chip->usb, hw, intf);
}

void plfxlc_chip_release(struct plfxlc_chip *chip)
{
	plfxlc_usb_release(&chip->usb);
	mutex_destroy(&chip->mutex);
}

int plfxlc_set_beacon_interval(struct plfxlc_chip *chip, u16 interval,
			       u8 dtim_period, int type)
{
	if (!interval ||
	    (chip->beacon_set && chip->beacon_interval == interval))
		return 0;

	chip->beacon_interval = interval;
	chip->beacon_set = true;
	return plfxlc_usb_wreq(chip->usb.ez_usb,
			       &chip->beacon_interval,
			       sizeof(chip->beacon_interval),
			       USB_REQ_BEACON_INTERVAL_WR);
}

int plfxlc_chip_init_hw(struct plfxlc_chip *chip)
{
	unsigned char *addr = plfxlc_mac_get_perm_addr(plfxlc_chip_to_mac(chip));
	struct usb_device *udev = interface_to_usbdev(chip->usb.intf);

	pr_info("plfxlc chip %04x:%04x v%02x %pM %s\n",
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		le16_to_cpu(udev->descriptor.bcdDevice),
		addr,
		plfxlc_speed(udev->speed));

	return plfxlc_set_beacon_interval(chip, 100, 0, 0);
}

int plfxlc_chip_switch_radio(struct plfxlc_chip *chip, u16 value)
{
	int r;
	__le16 radio_on = cpu_to_le16(value);

	r = plfxlc_usb_wreq(chip->usb.ez_usb, &radio_on,
			    sizeof(value), USB_REQ_POWER_WR);
	if (r)
		dev_err(plfxlc_chip_dev(chip), "POWER_WR failed (%d)\n", r);
	return r;
}

int plfxlc_chip_enable_rxtx(struct plfxlc_chip *chip)
{
	plfxlc_usb_enable_tx(&chip->usb);
	return plfxlc_usb_enable_rx(&chip->usb);
}

void plfxlc_chip_disable_rxtx(struct plfxlc_chip *chip)
{
	u8 value = 0;

	plfxlc_usb_wreq(chip->usb.ez_usb,
			&value, sizeof(value), USB_REQ_RXTX_WR);
	plfxlc_usb_disable_rx(&chip->usb);
	plfxlc_usb_disable_tx(&chip->usb);
}

int plfxlc_chip_set_rate(struct plfxlc_chip *chip, u8 rate)
{
	int r;

	if (!chip)
		return -EINVAL;

	r = plfxlc_usb_wreq(chip->usb.ez_usb,
			    &rate, sizeof(rate), USB_REQ_RATE_WR);
	if (r)
		dev_err(plfxlc_chip_dev(chip), "RATE_WR failed (%d)\n", r);
	return r;
}
