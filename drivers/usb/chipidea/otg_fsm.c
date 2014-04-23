/*
 * otg_fsm.c - ChipIdea USB IP core OTG FSM driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Jun Li
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This file mainly handles OTG fsm, it includes OTG fsm operations
 * for HNP and SRP.
 */

#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/chipidea.h>

#include "ci.h"
#include "bits.h"
#include "otg.h"
#include "otg_fsm.h"

int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci)
{
	struct usb_otg *otg;

	otg = devm_kzalloc(ci->dev,
			sizeof(struct usb_otg), GFP_KERNEL);
	if (!otg) {
		dev_err(ci->dev,
		"Failed to allocate usb_otg structure for ci hdrc otg!\n");
		return -ENOMEM;
	}

	otg->phy = ci->transceiver;
	otg->gadget = &ci->gadget;
	ci->fsm.otg = otg;
	ci->transceiver->otg = ci->fsm.otg;
	ci->fsm.power_up = 1;
	ci->fsm.id = hw_read_otgsc(ci, OTGSC_ID) ? 1 : 0;
	ci->transceiver->state = OTG_STATE_UNDEFINED;

	mutex_init(&ci->fsm.lock);

	/* Enable A vbus valid irq */
	hw_write_otgsc(ci, OTGSC_AVVIE, OTGSC_AVVIE);

	if (ci->fsm.id) {
		ci->fsm.b_ssend_srp =
			hw_read_otgsc(ci, OTGSC_BSV) ? 0 : 1;
		ci->fsm.b_sess_vld =
			hw_read_otgsc(ci, OTGSC_BSV) ? 1 : 0;
		/* Enable BSV irq */
		hw_write_otgsc(ci, OTGSC_BSVIE, OTGSC_BSVIE);
	}

	return 0;
}
