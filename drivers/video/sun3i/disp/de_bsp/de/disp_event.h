
#ifndef __DISP_EVENT_H__
#define __DISP_EVENT_H__

#include "disp_display_i.h"
#include "disp_layer.h"


__bool Is_In_Valid_Regn(__u8 mode,__u32 justd);
void LCD_vbi_event_proc(__u32 sel);
void LCD_line_event_proc(__u32 sel);
__s32 BSP_disp_cfg_start(__u32 sel);
__s32 BSP_disp_cfg_finish(__u32 sel);

#endif
