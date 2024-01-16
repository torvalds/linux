// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exported ksyms for the SSI FIQ handler
 *
 * Copyright (C) 2009, Sascha Hauer <s.hauer@pengutronix.de>
 */

#include <linux/module.h>

#include <linux/platform_data/asoc-imx-ssi.h>

EXPORT_SYMBOL(imx_ssi_fiq_tx_buffer);
EXPORT_SYMBOL(imx_ssi_fiq_rx_buffer);
EXPORT_SYMBOL(imx_ssi_fiq_start);
EXPORT_SYMBOL(imx_ssi_fiq_end);
EXPORT_SYMBOL(imx_ssi_fiq_base);

