/*
 * Exported ksyms for the SSI FIQ handler
 *
 * Copyright (C) 2009, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <mach/ssi.h>

EXPORT_SYMBOL(imx_ssi_fiq_tx_buffer);
EXPORT_SYMBOL(imx_ssi_fiq_rx_buffer);
EXPORT_SYMBOL(imx_ssi_fiq_start);
EXPORT_SYMBOL(imx_ssi_fiq_end);
EXPORT_SYMBOL(imx_ssi_fiq_base);

