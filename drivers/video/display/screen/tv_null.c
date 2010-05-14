
#include <linux/fb.h>
#include <linux/delay.h>
#include "../../rk2818_fb.h"
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "screen.h"



void set_tv_info(struct rk28fb_screen *screen)
{
    memset(screen, 0, sizeof(struct rk28fb_screen));
    screen->face = OUT_P666;
}
