#include <linux/hdmi.h>
#ifdef CONFIG_BACKLIGHT_RK29_BL
/* drivers/video/backlight/rk29_backlight.c */
extern void rk29_backlight_set(bool on);
#else
void rk29_backlight_set(bool on)
{
	/* please add backlight switching-related code here or on your backlight driver
	   parameter: on=1 ==> open spk 
	   			  on=0 ==> close spk
	*/
}
#endif
#ifdef CONFIG_FB_RK29 && CONFIG_HDMI_DUAL_DISP
extern void rk29_lcd_set(bool on);
#else
void rk29_lcd_set(bool on){}
#endif
void hdmi_set_backlight(int on)
{
	rk29_backlight_set(on);
	rk29_lcd_set(on);
}
