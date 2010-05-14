
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/arch/lcdcon.h>
#include <asm/arch/rk28_i2c.h>
#include <asm/arch/rk28_fb.h>
#include <asm/arch/gpio.h>
#include <asm/arch/iomux.h>
#include "screen.h"


void set_lcd_info(struct rk28fb_screen *screen)
{
    memset(screen, 0, sizeof(struct rk28fb_screen));
    screen->face = OUT_P666;
}
