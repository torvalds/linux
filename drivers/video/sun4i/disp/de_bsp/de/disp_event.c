#include "disp_event.h"
#include "disp_display.h"
#include "disp_de.h"
#include "disp_video.h"
#include "disp_scaler.h"

__s32 BSP_disp_cmd_cache(__u32 sel)
{
    gdisp.screen[sel].cache_flag = TRUE;
    return DIS_SUCCESS;
}

__s32 BSP_disp_cmd_submit(__u32 sel)
{
    gdisp.screen[sel].cache_flag = FALSE;

    return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_start(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt++;
	
	return DIS_SUCCESS;
}

__s32 BSP_disp_cfg_finish(__u32 sel)
{
	gdisp.screen[sel].cfg_cnt--;
	
	return DIS_SUCCESS;
}

void LCD_vbi_event_proc(__u32 sel, __u32 tcon_index)
{    
    __u32 cur_line = 0, start_delay = 0;
    __u32 i = 0;
    
	Video_Operation_In_Vblanking(sel, tcon_index);

    cur_line = LCDC_get_cur_line(sel, tcon_index);
    start_delay = LCDC_get_start_delay(sel, tcon_index);
    if(cur_line > start_delay-3)
	{
	      //DE_INF("cur_line(%d) >= start_delay(%d)-3 in LCD_vbi_event_proc\n", cur_line, start_delay);
		return ;
	}

    if(gdisp.screen[sel].LCD_CPUIF_ISR)
    {
    	(*gdisp.screen[sel].LCD_CPUIF_ISR)();
    }

    if(gdisp.screen[sel].cache_flag == FALSE && gdisp.screen[sel].cfg_cnt == 0)
    {
        for(i=0; i<2; i++)
        {
            if((gdisp.scaler[i].status & SCALER_USED) && (gdisp.scaler[i].screen_index == sel))
            {
                DE_SCAL_Set_Reg_Rdy(i);
                //DE_SCAL_Reset(i);
                //DE_SCAL_Start(i);
                gdisp.scaler[i].b_reg_change = FALSE;
            }
            if(gdisp.scaler[i].b_close == TRUE)
            {
                Scaler_close(i);
                gdisp.scaler[i].b_close = FALSE;
            }
        }
        DE_BE_Cfg_Ready(sel);
		gdisp.screen[sel].have_cfg_reg = TRUE;
    }

#if 0
    cur_line = LCDC_get_cur_line(sel, tcon_index);
    
	if(cur_line > 5)
	{
    	DE_INF("%d\n", cur_line);
    }
#endif

    return ;
}

void LCD_line_event_proc(__u32 sel, __u32 tcon_index)
{    
	if(gdisp.screen[sel].have_cfg_reg)
	{   
	    gdisp.init_para.disp_int_process(sel);
	    gdisp.screen[sel].have_cfg_reg = FALSE;
	}
}
