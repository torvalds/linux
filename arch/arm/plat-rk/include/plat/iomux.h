#ifndef __PLAT_IOMUX_H
#define __PLAT_IOMUX_H

#define INVALID_MODE	0xffffffff

#define iomux_switch_gpio_mode(m)	((m) & (~0x03))

int iomux_gpio_to_mode(int gpio);
int iomux_mode_to_gpio(unsigned int mode);
void iomux_set_gpio_mode(int gpio);
void iomux_set(unsigned int mode);
void __init iomux_init(void);

#endif
