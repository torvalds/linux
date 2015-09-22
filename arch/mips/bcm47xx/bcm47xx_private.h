#ifndef LINUX_BCM47XX_PRIVATE_H_
#define LINUX_BCM47XX_PRIVATE_H_

#ifndef pr_fmt
#define pr_fmt(fmt)		"bcm47xx: " fmt
#endif

#include <linux/kernel.h>

/* prom.c */
void __init bcm47xx_prom_highmem_init(void);

/* sprom.c */
void bcm47xx_sprom_register_fallbacks(void);

/* buttons.c */
int __init bcm47xx_buttons_register(void);

/* leds.c */
void __init bcm47xx_leds_register(void);

/* setup.c */
void __init bcm47xx_bus_setup(void);

/* workarounds.c */
void __init bcm47xx_workarounds(void);

#endif
