#ifndef LINUX_BCM47XX_PRIVATE_H_
#define LINUX_BCM47XX_PRIVATE_H_

#include <linux/kernel.h>

/* prom.c */
void __init bcm47xx_prom_highmem_init(void);

/* buttons.c */
int __init bcm47xx_buttons_register(void);

/* leds.c */
void __init bcm47xx_leds_register(void);

/* workarounds.c */
void __init bcm47xx_workarounds(void);

#endif
