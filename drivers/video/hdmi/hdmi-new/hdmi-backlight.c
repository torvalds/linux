#include <linux/hdmi-new.h>

extern void rk29_backlight_set(bool on);
void hdmi_set_backlight(int on)
{
	rk29_backlight_set(on);
}