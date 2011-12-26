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
#include "disp_hdmi.h"

__disp_dev_t gdisp;


__s32 BSP_disp_init(__disp_bsp_init_para * para)
{
    __u32 i = 0, screen_id = 0;

    memset(&gdisp,0x00,sizeof(__disp_dev_t));

    for(screen_id = 0; screen_id < 2; screen_id++)
    {
        gdisp.screen[screen_id].max_layers = 4;
        for(i = 0;i < gdisp.screen[screen_id].max_layers;i++)
        {
            gdisp.screen[screen_id].layer_manage[i].para.prio = IDLE_PRIO;
        }
        gdisp.screen[screen_id].image_output_type = IMAGE_OUTPUT_LCDC;
        
        gdisp.screen[screen_id].bright = 50;
        gdisp.screen[screen_id].contrast = 50;
        gdisp.screen[screen_id].saturation = 50;
        
        gdisp.scaler[screen_id].bright = 32;
        gdisp.scaler[screen_id].contrast = 32;
        gdisp.scaler[screen_id].saturation = 32;
        gdisp.scaler[screen_id].hue = 32;

        gdisp.screen[screen_id].lcd_bright = DISP_LCD_BRIGHT_LEVEL12;
    }
    memcpy(&gdisp.init_para,para,sizeof(__disp_bsp_init_para));
    memset(g_video,0,sizeof(g_video));

    DE_Set_Reg_Base(0, para->base_image0);
    //DE_Set_Reg_Base(1, para->base_image1);
    DE_SCAL_Set_Reg_Base(0, para->base_scaler0);
    //DE_SCAL_Set_Reg_Base(1, para->base_scaler1);
    LCDC_set_reg_base(0,para->base_lcdc0);
    //LCDC_set_reg_base(1,para->base_lcdc1);
    TVE_set_reg_base(0, para->base_tvec0);
    //TVE_set_reg_base(1, para->base_tvec1);

	disp_pll_init();

    Scaler_Init(0);
    //Scaler_Init(1);
    Image_init(0);
    //Image_init(1);
    Disp_lcdc_init(0);
    //Disp_lcdc_init(1);
    Disp_TVEC_Init(0);
    //Disp_TVEC_Init(1);
    Display_Hdmi_Init();

    return DIS_SUCCESS;
}

__s32 BSP_disp_exit(__u32 mode)
{
    if(mode == DISP_EXIT_MODE_CLEAN_ALL)
    {
        BSP_disp_close();
        
        Scaler_Exit(0);
        //Scaler_Exit(1);
        Image_exit(0);
        //Image_exit(1);
        Disp_lcdc_exit(0);
        //Disp_lcdc_exit(1);
        Disp_TVEC_Exit(0);
        //Disp_TVEC_Exit(1);
        Display_Hdmi_Exit();
    }
    else if(mode == DISP_EXIT_MODE_CLEAN_PARTLY)
    {
        OSAL_InterruptDisable(INTC_IRQNO_LCDC0);
        OSAL_UnRegISR(INTC_IRQNO_LCDC0,Disp_lcdc_event_proc,(void*)0);

        //OSAL_InterruptDisable(INTC_IRQNO_LCDC1);
        //OSAL_UnRegISR(INTC_IRQNO_LCDC1,Disp_lcdc_event_proc,(void*)0);

        OSAL_InterruptDisable(INTC_IRQNO_SCALER0);
        OSAL_UnRegISR(INTC_IRQNO_SCALER0,Scaler_event_proc,(void*)0);

        //OSAL_InterruptDisable(INTC_IRQNO_SCALER1);
        //OSAL_UnRegISR(INTC_IRQNO_SCALER1,Scaler_event_proc,(void*)0);
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
        else if(gdisp.screen[sel].status & (TV_ON | VGA_ON))
        {
        	TVE_close(sel);
        }
    }
    

    gdisp.screen[sel].status &= (IMAGE_USED_MASK & LCD_OFF & TV_OFF & VGA_OFF & HDMI_OFF);
    gdisp.screen[sel].lcdc_status &= (LCDC_TCON0_USED_MASK & LCDC_TCON1_USED_MASK);
    return DIS_SUCCESS;
}


__s32 BSP_disp_print_reg(__bool b_force_on, __u32 id)
{   
    __u32 base = 0, size = 0;
    __u32 i = 0;
    unsigned char str[20];

    switch(id)
    {
        case DISP_REG_SCALER0:
            base = gdisp.init_para.base_scaler0;
            size = 0xa18;
            sprintf(str, "scaler0:\n");
            break;
            
        case DISP_REG_SCALER1:
            base = gdisp.init_para.base_scaler1;
            size = 0xa18;
            sprintf(str, "scaler1:\n");
            break;
            
        case DISP_REG_IMAGE0:
            base = gdisp.init_para.base_image0 + 0x800;
            size = 0xdff - 0x800;
            sprintf(str, "image0:\n");
            break;
            
        case DISP_REG_IMAGE1:
            base = gdisp.init_para.base_image1 + 0x800;
            size = 0xdff - 0x800;
            sprintf(str, "image1:\n");
            break;
        case DISP_REG_LCDC0:
            base = gdisp.init_para.base_lcdc0;
            size = 0x800;
            sprintf(str, "lcdc0:\n");
            break;
            
        case DISP_REG_LCDC1:
            base = gdisp.init_para.base_lcdc1;
            size = 0x800;
            sprintf(str, "lcdc1:\n");
            break;
            
        case DISP_REG_TVEC0:
            base = gdisp.init_para.base_tvec0;
            size = 0x20c;
            sprintf(str, "tvec0:\n");
            break;
            
        case DISP_REG_TVEC1:
            base = gdisp.init_para.base_tvec1;
            size = 0x20c;
            sprintf(str, "tvec1:\n");
            break;
            
        case DISP_REG_CCMU:
            base = gdisp.init_para.base_ccmu;
            size = 0x158;
            sprintf(str, "ccmu:\n");
            break;
            
        case DISP_REG_PIOC:
            base = gdisp.init_para.base_pioc;
            size = 0x228;
            sprintf(str, "pioc:\n");
            break;
            
        case DISP_REG_PWM:
            base = gdisp.init_para.base_pwm + 0x200;
            size = 0x0c;
            sprintf(str, "pwm:\n");
            break;
            
        default:
            return DIS_FAIL;
    }

    if(b_force_on)
    {
        OSAL_PRINTF("%s", str);
    }
    else
    {
        DE_INF("%s", str);
    }
    for(i=0; i<size; i+=16)
    {
        __u32 reg[4];
        
        reg[0] = sys_get_wvalue(base + i);
        reg[1] = sys_get_wvalue(base + i + 4);
        reg[2] = sys_get_wvalue(base + i + 8);
        reg[3] = sys_get_wvalue(base + i + 12);
#ifdef __LINUX_OSAL__
        if(b_force_on)
        {
            OSAL_PRINTF("0x%08x:%08x,%08x:%08x,%08x\n", base + i, reg[0], reg[1], reg[2], reg[3]);
        }
        else
        {
            DE_INF("0x%08x:%08x,%08x:%08x,%08x\n", base + i, reg[0], reg[1], reg[2], reg[3]);
        }
#endif
#ifdef __BOOT_OSAL__
        if(b_force_on)
        {
            OSAL_PRINTF("0x%x:%x,%x,%x,%x\n", base + i, reg[0], reg[1], reg[2], reg[3]);
        }
        else
        {
            DE_INF("0x%x:%x,%x:%x,%x\n", base + i, reg[0], reg[1], reg[2], reg[3]);
        }
#endif
    }
    
    return DIS_SUCCESS;
}

