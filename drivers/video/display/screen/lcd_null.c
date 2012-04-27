
#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <linux/rk_screen.h>

void set_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info)
{
    memset(screen, 0, sizeof(struct rk29fb_screen));
    screen->face = OUT_P666;
}
