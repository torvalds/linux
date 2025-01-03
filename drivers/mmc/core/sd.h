/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _MMC_CORE_SD_H
#define _MMC_CORE_SD_H

#include <linux/types.h>

extern const struct device_type sd_type;

struct mmc_host;
struct mmc_card;

int mmc_sd_get_cid(struct mmc_host *host, u32 ocr, u32 *cid, u32 *rocr);
int mmc_sd_get_csd(struct mmc_card *card, bool is_sduc);
int mmc_decode_scr(struct mmc_card *card);
int mmc_sd_get_ro(struct mmc_host *host);
void mmc_decode_cid(struct mmc_card *card);
int mmc_sd_setup_card(struct mmc_host *host, struct mmc_card *card,
	bool reinit);
unsigned mmc_sd_get_max_clock(struct mmc_card *card);
int mmc_sd_switch_hs(struct mmc_card *card);

#endif
