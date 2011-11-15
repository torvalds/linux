
#ifndef __DISP_TV_H__
#define __DISP_TV_H__

#include "disp_display_i.h"

__s32 Disp_TVEC_Init(__u32 sel);
__s32 Disp_TVEC_Exit(__u32 sel);
__s32 Disp_TVEC_Open(__u32 sel);
__s32 Disp_TVEC_Close(__u32 sel);
__s32 Disp_Switch_Dram_Mode(__u32 type, __u8 tv_mod);
__s32 Disp_TVEC_Event_Proc(void *parg);

#endif
