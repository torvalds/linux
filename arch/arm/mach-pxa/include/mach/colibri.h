#ifndef _COLIBRI_H_
#define _COLIBRI_H_
/*
 * common settings for all modules
 */

/* physical memory regions */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* definitions for Colibri PXA270 */

#define COLIBRI_PXA270_FLASH_PHYS	(PXA_CS0_PHYS)  /* Flash region */
#define COLIBRI_PXA270_ETH_PHYS		(PXA_CS2_PHYS)  /* Ethernet */
#define COLIBRI_PXA270_ETH_IRQ_GPIO	114
#define COLIBRI_PXA270_ETH_IRQ		\
	gpio_to_irq(mfp_to_gpio(COLIBRI_PXA270_ETH_IRQ_GPIO))

/* definitions for Colibri PXA300 */

#define COLIBRI_PXA300_ETH_IRQ_GPIO     26
#define COLIBRI_PXA300_ETH_IRQ          \
	gpio_to_irq(mfp_to_gpio(COLIBRI_PXA300_ETH_IRQ_GPIO))

/* definitions for Colibri PXA320 */

#define COLIBRI_PXA320_ETH_IRQ_GPIO     36
#define COLIBRI_PXA320_ETH_IRQ          \
	gpio_to_irq(mfp_to_gpio(COLIBRI_PXA320_ETH_IRQ_GPIO))

#endif /* _COLIBRI_H_ */

