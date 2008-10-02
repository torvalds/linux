#ifndef __ASM_SH_RENESAS_SH7785LCR_H
#define __ASM_SH_RENESAS_SH7785LCR_H

/*
 * This board has 2 physical memory maps.
 * It can be changed with DIP switch(S2-5).
 *
 * phys address			| S2-5 = OFF	| S2-5 = ON
 * -----------------------------+---------------+---------------
 * 0x00000000 - 0x03ffffff(CS0)	| NOR Flash	| NOR Flash
 * 0x04000000 - 0x05ffffff(CS1)	| PLD		| PLD
 * 0x06000000 - 0x07ffffff(CS1)	| reserved	| I2C
 * 0x08000000 - 0x0bffffff(CS2)	| USB		| DDR SDRAM
 * 0x0c000000 - 0x0fffffff(CS3)	| SD		| DDR SDRAM
 * 0x10000000 - 0x13ffffff(CS4)	| SM107		| SM107
 * 0x14000000 - 0x17ffffff(CS5)	| I2C		| USB
 * 0x18000000 - 0x1bffffff(CS6)	| reserved	| SD
 * 0x40000000 - 0x5fffffff	| DDR SDRAM	| (cannot use)
 *
 */

#define NOR_FLASH_ADDR		0x00000000
#define NOR_FLASH_SIZE		0x04000000

#define PLD_BASE_ADDR		0x04000000
#define PLD_PCICR		(PLD_BASE_ADDR + 0x00)
#define PLD_LCD_BK_CONTR	(PLD_BASE_ADDR + 0x02)
#define PLD_LOCALCR		(PLD_BASE_ADDR + 0x04)
#define PLD_POFCR		(PLD_BASE_ADDR + 0x06)
#define PLD_LEDCR		(PLD_BASE_ADDR + 0x08)
#define PLD_SWSR		(PLD_BASE_ADDR + 0x0a)
#define PLD_VERSR		(PLD_BASE_ADDR + 0x0c)
#define PLD_MMSR		(PLD_BASE_ADDR + 0x0e)

#define SM107_MEM_ADDR		0x10000000
#define SM107_MEM_SIZE		0x00e00000
#define SM107_REG_ADDR		0x13e00000
#define SM107_REG_SIZE		0x00200000

#if defined(CONFIG_SH_SH7785LCR_29BIT_PHYSMAPS)
#define R8A66597_ADDR		0x14000000	/* USB */
#define CG200_ADDR		0x18000000	/* SD */
#define PCA9564_ADDR		0x06000000	/* I2C */
#else
#define R8A66597_ADDR		0x08000000
#define CG200_ADDR		0x0c000000
#define PCA9564_ADDR		0x14000000
#endif

#define R8A66597_SIZE		0x00000100
#define CG200_SIZE		0x00010000
#define PCA9564_SIZE		0x00000100

#endif  /* __ASM_SH_RENESAS_SH7785LCR_H */

