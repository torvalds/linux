/*
 * Copyright (C) ST-Ericsson SA 2011
 *
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */
#ifndef _CRYPTO_UX500_H
#include <linux/dmaengine.h>
#include <plat/ste_dma40.h>

struct cryp_platform_data {
	struct stedma40_chan_cfg mem_to_engine;
	struct stedma40_chan_cfg engine_to_mem;
};

#endif
