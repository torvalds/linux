#include "disp_de.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_scaler.h"
#include "disp_clk.h"
#include "disp_lcd.h"

__s32 Image_init(__u32 sel)
{
    
    image_clk_init(sel);
	image_clk_on(sel);	//when access image registers, must open MODULE CLOCK of image
	DE_BE_Reg_Init(sel);
	
    BSP_disp_sprite_init(sel);
    BSP_disp_set_output_csc(sel, DISP_OUTPUT_TYPE_LCD);
    
    Image_open(sel);
	
    return DIS_SUCCESS;
}
      
__s32 Image_exit(__u32 sel)
{    
    DE_BE_DisableINT(sel, DE_IMG_IRDY_IE);
    BSP_disp_sprite_exit(sel);
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
    gdisp.screen[sel].bright = bright;
    DE_BE_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_bright(__u32 sel)
{
    return gdisp.screen[sel].bright;
}

__s32 BSP_disp_set_contrast(__u32 sel, __u32 contrast)
{
    gdisp.screen[sel].contrast = contrast;
    DE_BE_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_contrast(__u32 sel)
{
    return gdisp.screen[sel].contrast;
}

__s32 BSP_disp_set_saturation(__u32 sel, __u32 saturation)
{
    gdisp.screen[sel].saturation = saturation;
    DE_BE_Set_Enhance(sel, gdisp.screen[sel].bright, gdisp.screen[sel].contrast, gdisp.screen[sel].saturation);

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_saturation(__u32 sel)
{
    return gdisp.screen[sel].saturation;
}

__s32 BSP_disp_enhance_enable(__u32 sel, __bool enable)
{
    DE_BE_enhance_enable(sel, enable);
    gdisp.screen[sel].enhance_en = enable;

    return DIS_SUCCESS;
}

__s32 BSP_disp_get_enhance_enable(__u32 sel)
{
    return gdisp.screen[sel].enhance_en;
}


__s32 BSP_disp_set_screen_size(__u32 sel, __disp_rectsz_t * size)
{    
    DE_BE_set_display_size(sel, size->width, size->height);

    gdisp.screen[sel].screen_width = size->width;
    gdisp.screen[sel].screen_height= size->height;

    return DIS_SUCCESS;
}

__s32 BSP_disp_set_output_csc(__u32 sel, __disp_output_type_t type)
{
    __disp_color_range_t out_color_range = DISP_COLOR_RANGE_0_255;
    __bool bout_yuv = FALSE;

    if(type == DISP_OUTPUT_TYPE_HDMI)
    {
        __s32 ret = 0;
        __s32 value = 0;
        
        out_color_range = DISP_COLOR_RANGE_16_255;

        ret = OSAL_Script_FetchParser_Data("disp_init", "screen0_out_color_range", &value, 1);
        if(ret < 0)
        {
            DE_INF("fetch script data disp_init.screen0_out_color_range fail\n");
        }
        else
        {
            out_color_range = value;
            DE_INF("screen0_out_color_range = %d\n", value);
        }
    }
    else if(type == DISP_OUTPUT_TYPE_TV)
    {
        bout_yuv = TRUE;
    }
   
    DE_BE_Output_Cfg_Csc_Coeff(sel, bout_yuv, out_color_range);

    gdisp.screen[sel].bout_yuv = bout_yuv;

    return DIS_SUCCESS;
}

__s32 BSP_disp_de_flicker_enable(__u32 sel, __bool b_en)
{   
	if(b_en)
	{
		gdisp.screen[sel].de_flicker_status |= DE_FLICKER_REQUIRED;
	}
	else
	{
		gdisp.screen[sel].de_flicker_status &= DE_FLICKER_REQUIRED_MASK;
	}
	Disp_de_flicker_enable(sel, b_en);
	return DIS_SUCCESS;
}

__s32 Disp_de_flicker_enable(__u32 sel, __u32 enable )
{
	__disp_tv_mode_t tv_mode;
	__u32 scan_mode;
	__u32 i;
	__u32 scaler_index;
	
	tv_mode = gdisp.screen[sel].tv_mode;
	scan_mode = Disp_get_screen_scan_mode(tv_mode);
			
	if(enable)
	{
		if((gdisp.screen[sel].de_flicker_status & DE_FLICKER_REQUIRED) && (scan_mode == 1))	//when output device is ntsc/pal/480i/576i
		{
			for(i = 0; i < gdisp.screen[sel].max_layers; i++)
			{
				if((gdisp.screen[sel].layer_manage[i].para.mode == DISP_LAYER_WORK_MODE_SCALER) && 	//when a layer using scaler layer
					(gdisp.screen[sel].layer_manage[i].scaler_index == sel) && 						//when this scaler is the same channel with be
					(g_video[sel][i].dit_enable == TRUE))	//when this scaler is using de-interlaced
				{
					DE_INF("de: CANNOT OPEN de-flicker due to scaler de-interlaced using!\n");
					DE_INF("de: Will OPEN de-flicker when scaler de-interlaced disable automatic!\n");
					break;
				}
			}
			if(i == gdisp.screen[sel].max_layers)//no scaler using de-interlaced
			{
				BSP_disp_cfg_start(sel);
				
				DE_BE_deflicker_enable(sel, TRUE);

				//config scaler to fit de-flicker
				for(i = 0; i < gdisp.screen[sel].max_layers; i++)
				{
					if((gdisp.screen[sel].layer_manage[i].para.mode == DISP_LAYER_WORK_MODE_SCALER) && 
						 ((scaler_index = gdisp.screen[sel].layer_manage[i].scaler_index) == sel))
					{
						Scaler_Set_Outitl(scaler_index, FALSE);
    					gdisp.scaler[scaler_index].b_reg_change = TRUE;
					}
				}
				gdisp.screen[sel].de_flicker_status |= DE_FLICKER_USED;

				BSP_disp_cfg_finish(sel);
			}
		}
		else
		{
			DE_INF("de: Will OPEN de-flicker when output to interlaced device !\n");
		}
		
	}
	else
	{
		BSP_disp_cfg_start(sel);

		for(i = 0; i < gdisp.screen[sel].max_layers; i++)
		{
			if((gdisp.screen[sel].layer_manage[i].para.mode == DISP_LAYER_WORK_MODE_SCALER) && 
					((scaler_index = gdisp.screen[sel].layer_manage[i].scaler_index) == sel))
			{
				Scaler_Set_Outitl(scaler_index, TRUE);
				gdisp.scaler[scaler_index].b_reg_change = TRUE;
			}
		}
		DE_BE_deflicker_enable(sel, FALSE);
		gdisp.screen[sel].de_flicker_status &= DE_FLICKER_USED_MASK;

		BSP_disp_cfg_finish(sel);
	}
	
	return DIS_SUCCESS;
}
