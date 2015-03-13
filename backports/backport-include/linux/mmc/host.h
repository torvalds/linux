#ifndef _BACKPORTLINUX_MMC_HOST_H
#define _BACKPORTLINUX_MMC_HOST_H
#include_next <linux/mmc/host.h>
#include <linux/version.h>
#include <linux/mmc/card.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
#define mmc_card_hs LINUX_BACKPORT(mmc_card_hs)
static inline int mmc_card_hs(struct mmc_card *card)
{
	return card->host->ios.timing == MMC_TIMING_SD_HS ||
		card->host->ios.timing == MMC_TIMING_MMC_HS;
}
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0) */

#endif /* _BACKPORTLINUX_MMC_HOST_H */
