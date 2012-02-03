
#ifndef __DISP_EVENT_H__
#define __DISP_EVENT_H__

#include "disp_display_i.h"
#include "disp_layer.h"


void LCD_vbi_event_proc(__u32 sel, __u32 tcon_index);
void LCD_line_event_proc(__u32 sel);
__s32 BSP_disp_cfg_start(__u32 sel);
__s32 BSP_disp_cfg_finish(__u32 sel);

#endif
