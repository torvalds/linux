#ifndef _COLIBRI_H_
#define _COLIBRI_H_

/* physical memory regions */
#define COLIBRI_FLASH_PHYS	(PXA_CS0_PHYS)  /* Flash region */
#define COLIBRI_ETH_PHYS	(PXA_CS2_PHYS)  /* Ethernet DM9000 region */
#define COLIBRI_SDRAM_BASE	0xa0000000      /* SDRAM region */

/* virtual memory regions */
#define COLIBRI_DISK_VIRT	0xF0000000	/* Disk On Chip region */

/* size of flash */
#define COLIBRI_FLASH_SIZE	0x02000000	/* Flash size 32 MB */

/* Ethernet Controller Davicom DM9000 */
#define GPIO_DM9000		114
#define COLIBRI_ETH_IRQ	IRQ_GPIO(GPIO_DM9000)

#endif /* _COLIBRI_H_ */
