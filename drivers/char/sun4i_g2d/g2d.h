
#ifndef __G2D_H__
#define __G2D_H__

#include"g2d_bsp.h"

/* Mixer status select */
#define G2D_FINISH_IRQ		(1<<8)
#define G2D_ERROR_IRQ		(1<<9)

typedef struct
{
	g2d_init_para init_para;
	
}g2d_dev_t;

int g2d_openclk(void);
int g2d_closeclk(void);
int g2d_clk_on(void);
int g2d_clk_off(void);
irqreturn_t g2d_handle_irq(int irq, void *dev_id);
int g2d_init(g2d_init_para *para);
int g2d_blit(g2d_blt * para);
int g2d_fill(g2d_fillrect * para);
int g2d_stretchblit(g2d_stretchblt * para);
int g2d_set_palette_table(g2d_palette *para);
int g2d_wait_cmd_finish(void);

#endif/* __G2D_H__ */