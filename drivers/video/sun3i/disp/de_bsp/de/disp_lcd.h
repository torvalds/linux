
#ifndef __DISP_LCD_H__
#define __DISP_LCD_H__

#include "disp_display_i.h"

__s32 Disp_lcdc_init(__u32 sel);
__s32 Disp_lcdc_exit(__u32 sel);
#ifndef __LINUX_OSAL__
__s32 Disp_lcdc_event_proc(void *parg);
#else
__s32 Disp_lcdc_event_proc(__s32 irq, void *parg);
#endif
__s32 Disp_lcdc_pin_cfg(__u32 sel, __disp_output_type_t out_type, __u32 bon);


#endif
