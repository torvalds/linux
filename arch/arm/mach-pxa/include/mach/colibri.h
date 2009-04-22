#ifndef _COLIBRI_H_
#define _COLIBRI_H_

#include <net/ax88796.h>

/*
 * common settings for all modules
 */

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
extern void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin);
#else
static inline void colibri_pxa3xx_init_mmc(mfp_cfg_t *, int, int) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
extern void colibri_pxa3xx_init_lcd(int bl_pin);
#else
static inline void colibri_pxa3xx_init_lcd(int) {}
#endif

#if defined(CONFIG_AX88796)
extern void colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data);
#endif

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* definitions for Colibri PXA270 */

#define COLIBRI_PXA270_FLASH_PHYS	(PXA_CS0_PHYS)  /* Flash region */
#define COLIBRI_PXA270_ETH_PHYS		(PXA_CS2_PHYS)  /* Ethernet */
#define COLIBRI_PXA270_ETH_IRQ_GPIO	114
#define COLIBRI_PXA270_ETH_IRQ		\
	gpio_to_irq(mfp_to_gpio(COLIBRI_PXA270_ETH_IRQ_GPIO))

#endif /* _COLIBRI_H_ */

