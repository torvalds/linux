#include "disp_de.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_scaler.h"
#include "disp_clk.h"

__s32 cb_DE_ClockChange(__u32 cmd, __s32 aux)
{
/*
	switch(cmd)
	{
	case CLK_CMD_SCLKCHG_REQ:
		{
			if(gdisp_screen.fe_status & SCALER_USED)
			{
				DE_SCAL_Disable(0);
			}
			Event_DE_Enable(0);
			return DIS_SUCCESS;
		}

	case CLK_CMD_SCLKCHG_DONE:
		{
			__u32   getclk;

			getclk = esCLK_GetSrcFreq(CCMU_SCLK_DRAMPLL);
		    if(getclk > (180000000))
		    {
		        esCLK_SetFreq(h_debemclk, CCMU_SCLK_DRAMPLL, 2);
		    }
		    else if(getclk < (120000000))
		    {
		        esCLK_SetFreq(h_debemclk, CCMU_SCLK_DRAMPLL, 1);
		    }

			//reset de machine
        	if(gdisp_screen.fe_status & SCALER_USED)
        	{
        		DE_SCAL_Enable(0);
        		DE_SCAL_Set_Reg_Rdy(0);
        		DE_SCAL_Start(0);
        	}
        	Event_DE_Enable(1);
			return DIS_SUCCESS;
		}
	default:
		return DIS_FAIL;
	}
*/
    return DIS_SUCCESS;
}

#ifndef __LINUX_OSAL__
__s32 Image_event_proc(void *parg)
#else
__s32 Image_event_proc(int irq, void *parg)
#endif
{
    __u8 img_intflags;
    __u32 sel = (__u32)parg;

    img_intflags = DE_BE_QueryINT(sel);
    if(img_intflags & DE_IMG_IRDY_IE)
    {
		DE_BE_ClearINT(sel,DE_IMG_IRDY_IE);
		//if(gdisp.scaler[sel].b_cfg_reg)
		//{
		//    gdisp.init_para.disp_int_process(sel);
		//}
    }

    return OSAL_IRQ_RETURN;
}

__s32 Image_init(__u32 sel)
{

    image_clk_init(sel);
	image_clk_on(sel);	//when access image registers, must open MODULE CLOCK of image
	DE_BE_Reg_Init(sel);

    if(sel == 0)
    {
        BSP_disp_sprite_init(sel);
    }
    //DE_BE_Ready_Enable(sel, TRUE);
    Image_open(sel);

	if(sel == 0)
	{
    	DE_BE_EnableINT(sel, DE_IMG_IRDY_IE);
	}//DE_BE_EnableINT(sel , DE_IMG_IRDY_IE);	//when sel == 1, can't process when image0 module clk close
	//image_clk_off(sel);	//close MODULE CLOCK of image

    if(sel == 0)
    {
        OSAL_RegISR(INTC_IRQNO_IMAGE0,0,Image_event_proc, (void *)sel,0,0);
        //OSAL_InterruptEnable(INTC_IRQNO_IMAGE0);
    }
    else if(sel == 1)
    {
        OSAL_RegISR(INTC_IRQNO_IMAGE1,0,Image_event_proc, (void *)sel,0,0);
        //OSAL_InterruptEnable(INTC_IRQNO_IMAGE1);
    }

    return DIS_SUCCESS;
}

__s32 Image_exit(__u32 sel)
{
    DE_BE_DisableINT(sel, DE_IMG_IRDY_IE);
    if(sel == 0)
    {
        BSP_disp_sprite_exit(sel);
    }
    image_clk_exit(sel);

    return DIS_SUCCESS;
}

__s32 Image_open(__u32  sel)
{
   DE_BE_Enable(sel);

   return DIS_SUCCESS;
}


__s32 Image_close(__u32 sel)
{
   DE_BE_Disable(sel);

   gdisp.screen[sel].status &= IMAGE_USED_MASK;

   return DIS_SUCCESS;
}


__s32 BSP_disp_set_bright(__u32 sel, __u32 bright)
{
    if(sel == 0)
    {
        gdisp.screen[sel].bright = bright;
        Be_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);
        return DIS_SUCCESS;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_get_bright(__u32 sel)
{
    if(sel == 0)
    {
        return gdisp.screen[sel].bright;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_set_contrast(__u32 sel, __u32 contrast)
{
    if(sel == 0)
    {
        gdisp.screen[sel].contrast = contrast;
        Be_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);
        return DIS_SUCCESS;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_get_contrast(__u32 sel)
{
    if(sel == 0)
    {
        return gdisp.screen[sel].contrast;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_set_saturation(__u32 sel, __u32 saturation)
{
    if(sel == 0)
    {
        gdisp.screen[sel].saturation = saturation;
        Be_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);
        return DIS_SUCCESS;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_get_saturation(__u32 sel)
{
    if(sel == 0)
    {
        return gdisp.screen[sel].saturation;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_enhance_enable(__u32 sel, __bool enable)
{
    if(sel == 0)
    {
        Be_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);
        DE_BE_enhance_enable(sel, enable);
        gdisp.screen[sel].enhance_en = enable;
        return DIS_SUCCESS;
    }
    return DIS_NOT_SUPPORT;
}

__s32 BSP_disp_get_enhance_enable(__u32 sel)
{
    if(sel == 0)
    {
        return gdisp.screen[sel].enhance_en;
    }
    return DIS_NOT_SUPPORT;
}

