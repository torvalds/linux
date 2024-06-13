/* SPDX-License-Identifier: GPL-2.0 */

#define OMAP24XX_NR_MMC		2
#define OMAP2420_MMC_SIZE	OMAP1_MMC_SIZE
#define OMAP2_MMC1_BASE		0x4809c000

#define OMAP4_MMC_REG_OFFSET	0x100

struct omap_hwmod;

#ifdef CONFIG_SOC_OMAP2420
int omap_msdi_reset(struct omap_hwmod *oh);
#else
static inline int omap_msdi_reset(struct omap_hwmod *oh)
{
	return 0;
}
#endif
