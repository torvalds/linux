#ifndef __DRV_DISP_H__
#define __DRV_DISP_H__

#include "drv_disp_i.h"


typedef struct
{
	struct device		*dev;
	struct resource		*mem[DISP_IO_NUM];
	void __iomem		*io[DISP_IO_NUM];

	__u32 base_ccmu;
	__u32 base_sdram;
	__u32 base_pio;

	unsigned fb_screen_id[FB_MAX];
    unsigned layer_hdl[FB_MAX];
	void * fbinfo[FB_MAX];
	__u8 fb_num;
}fb_info_t;

#define MAX_EVENT_SEM 20
typedef struct
{
    __u32         	mid;
    __u32         	used;
    __u32         	status;
    __u32    		exit_mode;//0:clean all  1:disable interrupt
    struct semaphore *scaler_finished_sem[2];
    struct semaphore *event_sem[2][MAX_EVENT_SEM];
    __bool			de_cfg_rdy[2][MAX_EVENT_SEM];
    __bool			event_used[2][MAX_EVENT_SEM];
    __bool          b_cache[2];

	__bool			b_lcd_open[2];
}__disp_drv_t;

extern __s32 DRV_lcd_open(__u32 sel);
extern __s32 DRV_lcd_close(__u32 sel);
extern __s32 Fb_Init(void);
extern __s32 Fb_Exit(void);
#endif

