#include <linux/mmc/host.h>
#include <linux/platform_data/mmc-omap.h>

#define OMAP24XX_NR_MMC		2
#define OMAP2420_MMC_SIZE	OMAP1_MMC_SIZE
#define OMAP2_MMC1_BASE		0x4809c000

#define OMAP4_MMC_REG_OFFSET	0x100

#if defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE)
void omap242x_init_mmc(struct omap_mmc_platform_data **mmc_data);
#else
static inline void omap242x_init_mmc(struct omap_mmc_platform_data **mmc_data)
{
}
#endif

struct omap_hwmod;
int omap_msdi_reset(struct omap_hwmod *oh);

/* called from board-specific card detection service routine */
extern void omap_mmc_notify_cover_event(struct device *dev, int slot,
					int is_closed);
