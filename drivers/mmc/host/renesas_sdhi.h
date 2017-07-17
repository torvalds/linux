/*
 * Renesas Mobile SDHI
 *
 * Copyright (C) 2017 Horms Solutions Ltd., Simon Horman
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RENESAS_SDHI_H
#define RENESAS_SDHI_H

#include <linux/platform_device.h>
#include "tmio_mmc.h"

struct renesas_sdhi_scc {
	unsigned long clk_rate;	/* clock rate for SDR104 */
	u32 tap;		/* sampling clock position for SDR104 */
};

struct renesas_sdhi_of_data {
	unsigned long tmio_flags;
	u32	      tmio_ocr_mask;
	unsigned long capabilities;
	unsigned long capabilities2;
	enum dma_slave_buswidth dma_buswidth;
	dma_addr_t dma_rx_offset;
	unsigned int bus_shift;
	int scc_offset;
	struct renesas_sdhi_scc *taps;
	int taps_num;
};

int renesas_sdhi_probe(struct platform_device *pdev,
		       const struct tmio_mmc_dma_ops *dma_ops);
int renesas_sdhi_remove(struct platform_device *pdev);
#endif
