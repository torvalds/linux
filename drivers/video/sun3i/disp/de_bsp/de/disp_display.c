#include "disp_display.h"
#include "disp_de.h"
#include "disp_lcd.h"
#include "disp_tv.h"
#include "disp_event.h"
#include "disp_sprite.h"
#include "disp_combined.h"
#include "disp_scaler.h"
#include "disp_video.h"
#include "disp_clk.h"

__disp_dev_t gdisp;


__s32 BSP_disp_init(__disp_bsp_init_para * para)
{
    __u32 i = 0;

    memset(&gdisp,0x00,sizeof(__disp_dev_t));
    gdisp.screen[0].max_layers = 4;
    gdisp.screen[1].max_layers = 2;
    for(i = 0;i < gdisp.screen[0].max_layers;i++)
    {
        gdisp.screen[0].layer_manage[i].para.prio = IDLE_PRIO;
    }
    for(i = 0;i < gdisp.screen[1].max_layers;i++)
    {
        gdisp.screen[1].layer_manage[i].para.prio = IDLE_PRIO;
    }
    gdisp.screen[0].bright = 50;
    gdisp.screen[0].contrast = 50;
    gdisp.screen[0].saturation = 50;
    gdisp.screen[1].bright = 50;
    gdisp.screen[1].contrast = 50;
    gdisp.screen[1].saturation = 50;

    gdisp.scaler[0].bright = 32;
    gdisp.scaler[0].contrast = 32;
    gdisp.scaler[0].saturation = 32;
    gdisp.scaler[0].hue = 32;
    gdisp.scaler[1].bright = 32;
    gdisp.scaler[1].contrast = 32;
    gdisp.scaler[1].saturation = 32;
    gdisp.scaler[1].hue = 32;
    memcpy(&gdisp.init_para,para,sizeof(__disp_bsp_init_para));
    memset(g_video,0,sizeof(g_video));

    DE_Set_Reg_Base(0, para->base_image0);
    DE_Set_Reg_Base(1, para->base_image1);
    DE_SCAL_Set_Reg_Base(0, para->base_scaler0);
    DE_SCAL_Set_Reg_Base(1, para->base_scaler1);
    LCDC_set_reg_base(0,para->base_lcdc0);
    LCDC_set_reg_base(1,para->base_lcdc1);
    TVE_set_reg_base(para->base_tvec);

	disp_pll_init();
	disp_clk_init();

    Scaler_Init(0);
    Scaler_Init(1);
    Image_init(0);
    Image_init(1);
    Disp_lcdc_init(0);
    Disp_lcdc_init(1);
    Disp_TVEC_Init();

    return DIS_SUCCESS;
}


__s32 BSP_disp_exit(__u32 mode)
{
    if(mode == DISP_EXIT_MODE_CLEAN_ALL)
    {
        BSP_disp_close();

        Scaler_Exit(0);
        Scaler_Exit(1);
        Image_exit(0);
        Image_exit(1);
        Disp_lcdc_exit(0);
        Disp_lcdc_exit(1);
        Disp_TVEC_Exit();
    }
    else if(mode == DISP_EXIT_MODE_CLEAN_PARTLY)
    {
        OSAL_InterruptDisable(INTC_IRQNO_LCDC0);
        OSAL_UnRegISR(INTC_IRQNO_LCDC0,Disp_lcdc_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_LCDC1);
        OSAL_UnRegISR(INTC_IRQNO_LCDC1,Disp_lcdc_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_SCALER0);
        OSAL_UnRegISR(INTC_IRQNO_SCALER0,Scaler_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_SCALER1);
        OSAL_UnRegISR(INTC_IRQNO_SCALER1,Scaler_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_IMAGE0);
        OSAL_UnRegISR(INTC_IRQNO_IMAGE0,Image_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_IMAGE1);
        OSAL_UnRegISR(INTC_IRQNO_IMAGE1,Image_event_proc,(void*)0);
    }

    return DIS_SUCCESS;
}

__s32 BSP_disp_open(void)
{
    return DIS_SUCCESS;
}

__s32 BSP_disp_close(void)
{
    __u32 sel = 0;

    for(sel = 0; sel<2; sel++)
    {
       Image_close(sel);
        if(gdisp.scaler[sel].status & SCALER_USED)
        {
            Scaler_close(sel);
        }
        if(gdisp.screen[sel].lcdc_status & LCDC_TCON0_USED)
        {
            TCON0_close(sel);
            LCDC_close(sel);
        }
        else if(gdisp.screen[sel].lcdc_status & LCDC_TCON1_USED)
        {
    	    TCON1_close(sel);
    	    LCDC_close(sel);
        }
    }
    if(gdisp.screen[sel].status & (TV_ON | VGA_ON))
    {
    	TVE_close();
    }

    gdisp.screen[sel].status &= (IMAGE_USED_MASK & LCD_OFF & TV_OFF & VGA_OFF & HDMI_OFF);
    gdisp.screen[sel].lcdc_status &= (LCDC_TCON0_USED_MASK & LCDC_TCON1_USED_MASK);
    return DIS_SUCCESS;
}

