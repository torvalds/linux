#ifndef __BCM63XX_FLASH_H
#define __BCM63XX_FLASH_H

enum {
	BCM63XX_FLASH_TYPE_PARALLEL,
	BCM63XX_FLASH_TYPE_SERIAL,
	BCM63XX_FLASH_TYPE_NAND,
};

int __init bcm63xx_flash_register(void);

#endif /* __BCM63XX_FLASH_H */
