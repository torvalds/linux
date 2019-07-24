/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * MMC definitions for OMAP2
 */

struct mmc_card;

struct omap2_hsmmc_info {
	u8	mmc;		/* controller 1/2/3 */
	u32	caps;		/* 4/8 wires and any additional host
				 * capabilities OR'd (ref. linux/mmc/host.h) */
	struct platform_device *pdev;	/* mmc controller instance */
	/* init some special card */
	void (*init_card)(struct mmc_card *card);
};

#if IS_ENABLED(CONFIG_MMC_OMAP_HS)

void omap_hsmmc_init(struct omap2_hsmmc_info *);
void omap_hsmmc_late_init(struct omap2_hsmmc_info *);

#else

static inline void omap_hsmmc_init(struct omap2_hsmmc_info *info)
{
}

static inline void omap_hsmmc_late_init(struct omap2_hsmmc_info *info)
{
}

#endif
