/*
 * drivers/char/sunxi_g2d/g2d.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "g2d.h"
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <mach/clock.h>
#include "g2d_driver_i.h"

struct clk *g2d_ahbclk,*g2d_dramclk,*g2d_mclk,*g2d_src;
extern __g2d_drv_t	 g2d_ext_hd;

/* Arbitrarily pick 240MHz (TODO: confirm what is the real limit) */
#define G2D_CLOCK_SPEED_LIMIT 240000000

int g2d_openclk(void)
{
	__u32 ret, g2d_div;

	/* ahb g2d gating */
	g2d_ahbclk = clk_get(NULL,"ahb_de_mix");

	/* sdram g2d gating */
	g2d_dramclk = clk_get(NULL,"sdram_de_mix");

	/* g2d gating */
	g2d_mclk = clk_get(NULL,"de_mix");

	/*disable mp clk reset*/
	clk_reset(g2d_mclk,0);

	/* set g2d clk value */
	g2d_src = clk_get(NULL,"sdram_pll_p");//video_pll0
	ret = clk_set_parent(g2d_mclk, g2d_src);
	clk_put(g2d_src);

	ret = clk_get_rate(g2d_src);
	g2d_div = DIV_ROUND_UP(ret, G2D_CLOCK_SPEED_LIMIT);
	clk_set_rate(g2d_mclk, ret / g2d_div);

	return 0;
}

int g2d_closeclk(void)/* used once when g2d driver exit */
{
	clk_disable(g2d_ahbclk);
	clk_disable(g2d_dramclk);
	clk_disable(g2d_mclk);

	clk_put(g2d_ahbclk);
	clk_put(g2d_dramclk);
	clk_put(g2d_mclk);

	return 0;
}

int g2d_clk_on(void)/* used in request */
{
	clk_enable(g2d_ahbclk);
	clk_enable(g2d_dramclk);
	clk_enable(g2d_mclk);

	return  0;
}

int g2d_clk_off(void)/* used in release */
{
	clk_disable(g2d_ahbclk);
	clk_disable(g2d_dramclk);
	clk_disable(g2d_mclk);

	return  0;
}

irqreturn_t g2d_handle_irq(int irq, void *dev_id)
{
    __u32 irq_flag;

    irq_flag = mixer_get_irq();
    if(irq_flag & G2D_FINISH_IRQ)
    {
		mixer_clear_init();
		g2d_ext_hd.finish_flag = 1;
		wake_up(&g2d_ext_hd.queue);
    }

    return IRQ_HANDLED;
}

int g2d_init(g2d_init_para *para)
{
	mixer_set_reg_base(para->g2d_base);

	return 0;
}

int g2d_exit(void)
{
	__u8 err = 0;
	g2d_closeclk();

	return err;
}

int g2d_wait_cmd_finish(void)
{
	long timeout = 50; /* 30ms */

	timeout = wait_event_timeout(g2d_ext_hd.queue, g2d_ext_hd.finish_flag == 1, msecs_to_jiffies(timeout));
	if(timeout == 0)
	{
		mixer_clear_init();
		printk("wait g2d irq pending flag timeout\n");
		g2d_ext_hd.finish_flag = 1;
		wake_up(&g2d_ext_hd.queue);
		return -1;
	}
	return 0;
}

int g2d_blit(g2d_blt * para)
{
	__s32 err = 0;

	/* check the parameter valid */
    if(para->src_rect.w == 0 || para->src_rect.h == 0 ||
       ((para->src_rect.x < 0)&&((-para->src_rect.x) > para->src_rect.w)) ||
       ((para->src_rect.y < 0)&&((-para->src_rect.y) > para->src_rect.h)) ||
       ((para->dst_x < 0)&&((-para->dst_x) > para->src_rect.w)) ||
       ((para->dst_y < 0)&&((-para->dst_y) > para->src_rect.h)) ||
       ((para->src_rect.x > 0)&&(para->src_rect.x > para->src_image.w - 1)) ||
       ((para->src_rect.y > 0)&&(para->src_rect.y > para->src_image.h - 1)) ||
       ((para->dst_x > 0)&&(para->dst_x > para->dst_image.w - 1)) ||
       ((para->dst_y > 0)&&(para->dst_y > para->dst_image.h - 1)))
	{
		printk("invalid blit parameter setting");
		return -EINVAL;
	}
	else
	{
		if(((para->src_rect.x < 0)&&((-para->src_rect.x) < para->src_rect.w)))
		{
			para->src_rect.w = para->src_rect.w + para->src_rect.x;
			para->src_rect.x = 0;
		}
		else if((para->src_rect.x + para->src_rect.w) > para->src_image.w)
		{
			para->src_rect.w = para->src_image.w - para->src_rect.x;
		}
		if(((para->src_rect.y < 0)&&((-para->src_rect.y) < para->src_rect.h)))
		{
			para->src_rect.h = para->src_rect.h + para->src_rect.y;
			para->src_rect.y = 0;
		}
		else if((para->src_rect.y + para->src_rect.h) > para->src_image.h)
		{
			para->src_rect.h = para->src_image.h - para->src_rect.y;
		}

		if(((para->dst_x < 0)&&((-para->dst_x) < para->src_rect.w)))
		{
			para->src_rect.w = para->src_rect.w + para->dst_x;
			para->src_rect.x = (-para->dst_x);
			para->dst_x = 0;
		}
		else if((para->dst_x + para->src_rect.w) > para->dst_image.w)
		{
			para->src_rect.w = para->dst_image.w - para->dst_x;
		}
		if(((para->dst_y < 0)&&((-para->dst_y) < para->src_rect.h)))
		{
			para->src_rect.h = para->src_rect.h + para->dst_y;
			para->src_rect.y = (-para->dst_y);
			para->dst_y = 0;
		}
		else if((para->dst_y + para->src_rect.h) > para->dst_image.h)
		{
			para->src_rect.h = para->dst_image.h - para->dst_y;
		}
	}

	g2d_ext_hd.finish_flag = 0;
	err = mixer_blt(para);

	return err;
}

int g2d_fill(g2d_fillrect * para)
{
	__s32 err = 0;

	/* check the parameter valid */
	if(para->dst_rect.w == 0 || para->dst_rect.h == 0 ||
	   ((para->dst_rect.x < 0)&&((-para->dst_rect.x)>para->dst_rect.w)) ||
	   ((para->dst_rect.y < 0)&&((-para->dst_rect.y)>para->dst_rect.h)) ||
	   ((para->dst_rect.x > 0)&&(para->dst_rect.x > para->dst_image.w - 1)) ||
	   ((para->dst_rect.y > 0)&&(para->dst_rect.y > para->dst_image.h - 1)))
	{
		printk("invalid fillrect parameter setting");
		return -EINVAL;
	}
	else
	{
		if(((para->dst_rect.x < 0)&&((-para->dst_rect.x) < para->dst_rect.w)))
		{
			para->dst_rect.w = para->dst_rect.w + para->dst_rect.x;
			para->dst_rect.x = 0;
		}
		else if((para->dst_rect.x + para->dst_rect.w) > para->dst_image.w)
		{
			para->dst_rect.w = para->dst_image.w - para->dst_rect.x;
		}
		if(((para->dst_rect.y < 0)&&((-para->dst_rect.y) < para->dst_rect.h)))
		{
			para->dst_rect.h = para->dst_rect.h + para->dst_rect.y;
			para->dst_rect.y = 0;
		}
		else if((para->dst_rect.y + para->dst_rect.h) > para->dst_image.h)
		{
			para->dst_rect.h = para->dst_image.h - para->dst_rect.y;
		}
	}

	g2d_ext_hd.finish_flag = 0;
	err = mixer_fillrectangle(para);

	return err;
}

int g2d_stretchblit(g2d_stretchblt * para)
{
	__s32 err = 0;

	/* check the parameter valid */
    if(para->src_rect.w == 0 || para->src_rect.h == 0 ||
       para->dst_rect.w == 0 || para->dst_rect.h == 0 ||
       ((para->src_rect.x < 0)&&((-para->src_rect.x) > para->src_rect.w)) ||
       ((para->src_rect.y < 0)&&((-para->src_rect.y) > para->src_rect.h)) ||
       ((para->dst_rect.x < 0)&&((-para->dst_rect.x) > para->dst_rect.w)) ||
       ((para->dst_rect.y < 0)&&((-para->dst_rect.y) > para->dst_rect.h)) ||
       ((para->src_rect.x > 0)&&(para->src_rect.x > para->src_image.w - 1)) ||
       ((para->src_rect.y > 0)&&(para->src_rect.y > para->src_image.h - 1)) ||
       ((para->dst_rect.x > 0)&&(para->dst_rect.x > para->dst_image.w - 1)) ||
       ((para->dst_rect.y > 0)&&(para->dst_rect.y > para->dst_image.h - 1)))
	{
		printk("invalid stretchblit parameter setting");
		return -EINVAL;
	}
	else
	{
		if(((para->src_rect.x < 0)&&((-para->src_rect.x) < para->src_rect.w)))
		{
			para->src_rect.w = para->src_rect.w + para->src_rect.x;
			para->src_rect.x = 0;
		}
		else if((para->src_rect.x + para->src_rect.w) > para->src_image.w)
		{
			para->src_rect.w = para->src_image.w - para->src_rect.x;
		}
		if(((para->src_rect.y < 0)&&((-para->src_rect.y) < para->src_rect.h)))
		{
			para->src_rect.h = para->src_rect.h + para->src_rect.y;
			para->src_rect.y = 0;
		}
		else if((para->src_rect.y + para->src_rect.h) > para->src_image.h)
		{
			para->src_rect.h = para->src_image.h - para->src_rect.y;
		}

		if(((para->dst_rect.x < 0)&&((-para->dst_rect.x) < para->dst_rect.w)))
		{
			para->dst_rect.w = para->dst_rect.w + para->dst_rect.x;
			para->dst_rect.x = 0;
		}
		else if((para->dst_rect.x + para->dst_rect.w) > para->dst_image.w)
		{
			para->dst_rect.w = para->dst_image.w - para->dst_rect.x;
		}
		if(((para->dst_rect.y < 0)&&((-para->dst_rect.y) < para->dst_rect.h)))
		{
			para->dst_rect.h = para->dst_rect.h + para->dst_rect.y;
			para->dst_rect.y = 0;
		}
		else if((para->dst_rect.y + para->dst_rect.h) > para->dst_image.h)
		{
			para->dst_rect.h = para->dst_image.h - para->dst_rect.y;
		}
	}

	g2d_ext_hd.finish_flag = 0;
	err = mixer_stretchblt(para);

	return err;
}

int g2d_set_palette_table(g2d_palette *para)
{

    if((para->pbuffer == NULL) || (para->size < 0) || (para->size>1024))
    {
        printk("para invalid in mixer_set_palette\n");
        return -1;
    }

	mixer_set_palette(para);

	return 0;
}

