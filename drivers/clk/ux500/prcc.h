/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PRCC_H
#define __PRCC_H

#define PRCC_NUM_PERIPH_CLUSTERS 6
#define PRCC_PERIPHS_PER_CLUSTER 32

/* CLKRST4 is missing making it hard to index things */
enum clkrst_index {
	CLKRST1_INDEX = 0,
	CLKRST2_INDEX,
	CLKRST3_INDEX,
	CLKRST5_INDEX,
	CLKRST6_INDEX,
	CLKRST_MAX,
};

#endif
