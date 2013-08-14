/*
 * otg.c - ChipIdea USB IP core OTG driver
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * Author: Peter Chen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This file mainly handles otgsc register, it may include OTG operation
 * in the future.
 */

#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/chipidea.h>

#include "ci.h"
#include "bits.h"
#include "otg.h"

/**
 * ci_hdrc_otg_init - initialize otgsc bits
 * ci: the controller
 */
int ci_hdrc_otg_init(struct ci_hdrc *ci)
{
	ci_enable_otg_interrupt(ci, OTGSC_IDIE);

	return 0;
}
