
#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk29_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"


void set_hdmi_info(struct rk29fb_screen *screen)
{
    memset(screen, 0, sizeof(struct rk29fb_screen));
    screen->face = OUT_P666;
}
