/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MMC_JZ4740_MMC
#define __LINUX_MMC_JZ4740_MMC

struct jz4740_mmc_platform_data {
	unsigned card_detect_active_low:1;
	unsigned read_only_active_low:1;

	unsigned data_1bit:1;
};

#endif
