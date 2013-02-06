/*
 * arch/arm/mach-s5pv210/u1.h
 */

#ifndef __P10_H__
#define __P10_H__

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
extern int s3c_gpio_slp_setpull_updown(unsigned int pin, unsigned int config);

extern int brcm_wlan_init(void);

#endif
