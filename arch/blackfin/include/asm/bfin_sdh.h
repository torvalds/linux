/*
 * bfin_sdh.h - Blackfin SDH definitions
 *
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_SDH_H__
#define __BFIN_SDH_H__

struct bfin_sd_host {
	int dma_chan;
	int irq_int0;
	int irq_int1;
	u16 pin_req[7];
};

#endif
