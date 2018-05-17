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
	unsigned int max_blk_count;
	unsigned short max_segs;
};

struct tmio_mmc_dma {
	enum dma_slave_buswidth dma_buswidth;
	bool (*filter)(struct dma_chan *chan, void *arg);
	void (*enable)(struct tmio_mmc_host *host, bool enable);
	struct completion	dma_dataend;
	struct tasklet_struct	dma_complete;
};

struct renesas_sdhi {
	struct clk *clk;
	struct clk *clk_cd;
	struct tmio_mmc_data mmc_data;
	struct tmio_mmc_dma dma_priv;
	struct pinctrl *pinctrl;
	struct pinctrl_state *pins_default, *pins_uhs;
	void __iomem *scc_ctl;
	u32 scc_tappos;
};

#define host_to_priv(host) \
	container_of((host)->pdata, struct renesas_sdhi, mmc_data)

int renesas_sdhi_probe(struct platform_device *pdev,
		       const struct tmio_mmc_dma_ops *dma_ops);
int renesas_sdhi_remove(struct platform_device *pdev);
#endif
