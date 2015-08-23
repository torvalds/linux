#ifndef __ASM_MACH_ADDR_MAP_H
#define __ASM_MACH_ADDR_MAP_H

/*
 * Chip Selects
 */
#define PXA_CS0_PHYS		0x00000000
#define PXA_CS1_PHYS		0x04000000
#define PXA_CS2_PHYS		0x08000000
#define PXA_CS3_PHYS		0x0C000000
#define PXA_CS4_PHYS		0x10000000
#define PXA_CS5_PHYS		0x14000000

#define PXA300_CS0_PHYS		0x00000000	/* PXA300/PXA310 _only_ */
#define PXA300_CS1_PHYS		0x30000000	/* PXA300/PXA310 _only_ */
#define PXA3xx_CS2_PHYS		0x10000000
#define PXA3xx_CS3_PHYS		0x14000000

/*
 * Peripheral Bus
 */
#define PERIPH_PHYS		0x40000000
#define PERIPH_VIRT		IOMEM(0xf2000000)
#define PERIPH_SIZE		0x02000000

/*
 * Static Memory Controller (w/ SDRAM controls on PXA25x/PXA27x)
 */
#define PXA2XX_SMEMC_PHYS	0x48000000
#define PXA3XX_SMEMC_PHYS	0x4a000000
#define SMEMC_VIRT		IOMEM(0xf6000000)
#define SMEMC_SIZE		0x00100000

/*
 * Dynamic Memory Controller (only on PXA3xx)
 */
#define DMEMC_PHYS		0x48100000
#define DMEMC_VIRT		IOMEM(0xf6100000)
#define DMEMC_SIZE		0x00100000

/*
 * Reserved space for low level debug virtual addresses within
 * 0xf6200000..0xf6201000
 */

/*
 * DFI Bus for NAND, PXA3xx only
 */
#define NAND_PHYS		0x43100000
#define NAND_VIRT		IOMEM(0xf6300000)
#define NAND_SIZE		0x00100000

/*
 * Internal Memory Controller (PXA27x and later)
 */
#define IMEMC_PHYS		0x58000000
#define IMEMC_VIRT		IOMEM(0xfe000000)
#define IMEMC_SIZE		0x00100000

#endif /* __ASM_MACH_ADDR_MAP_H */
