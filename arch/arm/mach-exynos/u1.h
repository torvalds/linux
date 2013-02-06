/*
 * arch/arm/mach-s5pv210/u1.h
 */

#ifndef __U1_H__
#define __U1_H__

extern struct ld9040_panel_data u1_panel_data;
extern struct ld9040_panel_data u1_panel_data_a2;
extern struct ld9040_panel_data u1_panel_data_m2;

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
extern int s3c_gpio_slp_setpull_updown(unsigned int pin, unsigned int config);

extern void u1_config_gpio_table(void);
extern void u1_config_sleep_gpio_table(void);

extern int brcm_wlan_init(void);
extern void set_gps_uart_op(int onoff);

#ifdef CONFIG_TARGET_LOCALE_KOR
extern int u1_switch_get_usb_lock_state(void);
#endif

#ifdef CONFIG_WIMAX_CMC
extern struct platform_device s3c_device_cmc732;
#endif

#endif
