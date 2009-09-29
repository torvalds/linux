#ifndef __ASM_SH_KFR2R09_H
#define __ASM_SH_KFR2R09_H

#include <video/sh_mobile_lcdc.h>

#ifdef CONFIG_FB_SH_MOBILE_LCDC
void kfr2r09_lcd_on(void *board_data);
void kfr2r09_lcd_off(void *board_data);
int kfr2r09_lcd_setup(void *board_data, void *sys_ops_handle,
		      struct sh_mobile_lcdc_sys_bus_ops *sys_ops);
#else
static inline void kfr2r09_lcd_on(void *board_data) {}
static inline void kfr2r09_lcd_off(void *board_data) {}
static inline int kfr2r09_lcd_setup(void *board_data, void *sys_ops_handle,
				    struct sh_mobile_lcdc_sys_bus_ops *sys_ops)
{
	return -ENODEV;
}
#endif

#endif /* __ASM_SH_KFR2R09_H */
