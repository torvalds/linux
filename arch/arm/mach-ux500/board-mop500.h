/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_MOP500_H
#define __BOARD_MOP500_H

#include <linux/platform_data/asoc-ux500-msp.h>
#include <linux/amba/mmci.h>

extern struct mmci_platform_data mop500_sdi0_data;
extern struct mmci_platform_data mop500_sdi1_data;
extern struct mmci_platform_data mop500_sdi2_data;
extern struct mmci_platform_data mop500_sdi4_data;
extern struct msp_i2s_platform_data msp0_platform_data;
extern struct msp_i2s_platform_data msp1_platform_data;
extern struct msp_i2s_platform_data msp2_platform_data;
extern struct msp_i2s_platform_data msp3_platform_data;

#endif
