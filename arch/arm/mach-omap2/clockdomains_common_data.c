/*
 * OMAP2+-common clockdomain data
 *
 * Copyright (C) 2008-2012 Texas Instruments, Inc.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Paul Walmsley, Jouni Högander
 */

#include <linux/kernel.h>
#include <linux/io.h>

#include "clockdomain.h"

/* These are implicit clockdomains - they are never defined as such in TRM */
struct clockdomain prm_common_clkdm = {
	.name		= "prm_clkdm",
	.pwrdm		= { .name = "wkup_pwrdm" },
};

struct clockdomain cm_common_clkdm = {
	.name		= "cm_clkdm",
	.pwrdm		= { .name = "core_pwrdm" },
};
