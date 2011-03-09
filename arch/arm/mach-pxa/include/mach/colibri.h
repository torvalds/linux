#ifndef _COLIBRI_H_
#define _COLIBRI_H_

#include <net/ax88796.h>
#include <mach/mfp.h>

/*
 * base board glue for PXA270 module
 */

enum {
	COLIBRI_EVALBOARD = 0,
	COLIBRI_PXA270_INCOME,
};

#if defined(CONFIG_MACH_COLIBRI_EVALBOARD)
extern void colibri_evalboard_init(void);
#else
static inline void colibri_evalboard_init(void) {}
#endif

#if defined(CONFIG_MACH_COLIBRI_PXA270_INCOME)
extern void colibri_pxa270_income_boardinit(void);
#else
static inline void colibri_pxa270_income_boardinit(void) {}
#endif

/*
 * common settings for all modules
 */

#if defined(CONFIG_MMC_PXA) || defined(CONFIG_MMC_PXA_MODULE)
extern void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin);
#else
static inline void colibri_pxa3xx_init_mmc(mfp_cfg_t *pins, int len, int detect_pin) {}
#endif

#if defined(CONFIG_FB_PXA) || defined(CONFIG_FB_PXA_MODULE)
extern void colibri_pxa3xx_init_lcd(int bl_pin);
#else
static inline void colibri_pxa3xx_init_lcd(int bl_pin) {}
#endif

#if defined(CONFIG_AX88796)
extern void colibri_pxa3xx_init_eth(struct ax_plat_data *plat_data);
#endif

#if defined(CONFIG_MTD_NAND_PXA3xx) || defined(CONFIG_MTD_NAND_PXA3xx_MODULE)
extern void colibri_pxa3xx_init_nand(void);
#else
static inline void colibri_pxa3xx_init_nand(void) {}
#endif

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* GPIO definitions for Colibri PXA270 */
#define GPIO114_COLIBRI_PXA270_ETH_IRQ	114
#define GPIO0_COLIBRI_PXA270_SD_DETECT	0
#define GPIO113_COLIBRI_PXA270_TS_IRQ	113

/* GPIO definitions for Colibri PXA300/310 */
#define GPIO39_COLIBRI_PXA300_SD_DETECT	39

/* GPIO definitions for Colibri PXA320 */
#define GPIO28_COLIBRI_PXA320_SD_DETECT	28

#endif /* _COLIBRI_H_ */

