#include "disp_tv.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_de.h"
#include "disp_lcd.h"
#include "disp_clk.h"

//static __u32            tv_curinter = DISP_TV_NONE;   /*tv current signal interface,initinal none status*/
__hdle           		h_tvahbclk = 0;
__hdle          		h_tv1clk = 0;
__hdle           		h_tv2clk = 0;


__s32 Disp_TV_ClockChange_cb(__u32 cmd, __s32 aux)
{
/*
    switch(cmd)
    {
        case CLK_CMD_SCLKCHG_REQ:
        {
         	return DIS_SUCCESS;
        }

		case CLK_CMD_SCLKCHG_DONE:
        {
			return DIS_SUCCESS;
	    }
        default:
            return DIS_FAIL;
    }
*/
    return DIS_SUCCESS;
}


__s32 Disp_Switch_Dram_Mode(__u32 type, __u8 tv_mod)
{
    return DIS_SUCCESS;
}

__s32 Disp_TVEC_Init(void)
{
    tve_clk_init();
    disp_clk_cfg(0,DISP_OUTPUT_TYPE_TV,DISP_TV_MOD_720P_50HZ);
    tve_clk_on();
	TVE_init();
    tve_clk_off();

    gdisp.screen[0].dac_source[0] = DISP_TV_DAC_SRC_COMPOSITE;
    gdisp.screen[0].dac_source[1] = DISP_TV_DAC_SRC_Y;
    gdisp.screen[0].dac_source[2] = DISP_TV_DAC_SRC_PB;
    gdisp.screen[0].dac_source[3] = DISP_TV_DAC_SRC_PR;
    gdisp.screen[1].dac_source[0] = DISP_TV_DAC_SRC_COMPOSITE;
    gdisp.screen[1].dac_source[1] = DISP_TV_DAC_SRC_Y;
    gdisp.screen[1].dac_source[2] = DISP_TV_DAC_SRC_PB;
    gdisp.screen[1].dac_source[3] = DISP_TV_DAC_SRC_PR;

    gdisp.screen[0].tv_mode = DISP_TV_MOD_720P_50HZ;
    gdisp.screen[1].tv_mode = DISP_TV_MOD_720P_50HZ;
    return DIS_SUCCESS;
}


__s32 Disp_TVEC_Exit(void)
{
    TVE_exit();
    tve_clk_exit();

    return DIS_SUCCESS;
}

__s32 Disp_TVEC_Open(__u32 sel)
{
	TVE_open(sel);
	return DIS_SUCCESS;
}

__s32 Disp_TVEC_Close(void)
{
	TVE_dac_disable(0);
	TVE_dac_disable(1);
	TVE_dac_disable(2);
	TVE_dac_disable(3);

	TVE_close();

	return DIS_SUCCESS;
}

static void Disp_TVEC_DacCfg(__u32 sel, __u8 mode)
{
    __u32 i = 0;

	switch(mode)
	{
	case DISP_TV_MOD_NTSC:
	case DISP_TV_MOD_PAL:
	case DISP_TV_MOD_PAL_M:
	case DISP_TV_MOD_PAL_NC:
    	{
    	    for(i=0; i<4; i++)
    	    {
    	        if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_COMPOSITE)
    	        {
    	            TVE_dac_set_source(i, DISP_TV_DAC_SRC_COMPOSITE);
    	            TVE_dac_enable(i);
    	        }
    	    }
    	}
	    break;

	case DISP_TV_MOD_NTSC_SVIDEO:
	case DISP_TV_MOD_PAL_SVIDEO:
	case DISP_TV_MOD_PAL_M_SVIDEO:
	case DISP_TV_MOD_PAL_NC_SVIDEO:
		{
		    for(i=0; i<4; i++)
		    {
		        if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_LUMA)
		        {
		            TVE_dac_set_source(i, DISP_TV_DAC_SRC_LUMA);
		            TVE_dac_enable(i);
		        }
		        else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_CHROMA)
		        {
		            TVE_dac_set_source(i, DISP_TV_DAC_SRC_CHROMA);
		            TVE_dac_enable(i);
		        }
		    }

		}
		break;

	case DISP_TV_MOD_NTSC_CVBS_SVIDEO:
	case DISP_TV_MOD_PAL_CVBS_SVIDEO:
	case DISP_TV_MOD_PAL_M_CVBS_SVIDEO:
	case DISP_TV_MOD_PAL_NC_CVBS_SVIDEO:
		{
		    for(i=0; i<4; i++)
		    {
		        if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_COMPOSITE)
		        {
		            TVE_dac_set_source(i, DISP_TV_DAC_SRC_COMPOSITE);
		            TVE_dac_enable(i);
		        }
		        else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_LUMA)
		        {
		            TVE_dac_set_source(i, DISP_TV_DAC_SRC_LUMA);
		            TVE_dac_enable(i);
		        }
		        else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_CHROMA)
		        {
		            TVE_dac_set_source(i, DISP_TV_DAC_SRC_CHROMA);
		            TVE_dac_enable(i);
		        }
		    }

		}
		break;

	case DISP_TV_MOD_480I:
	case DISP_TV_MOD_576I:
	case DISP_TV_MOD_480P:
	case DISP_TV_MOD_576P:
	case DISP_TV_MOD_720P_50HZ:
	case DISP_TV_MOD_720P_60HZ:
	case DISP_TV_MOD_1080I_50HZ:
	case DISP_TV_MOD_1080I_60HZ:
	case DISP_TV_MOD_1080P_50HZ:
	case DISP_TV_MOD_1080P_60HZ:
        {
    	    for(i=0; i<4; i++)
    	    {
    	        if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_Y)
    	        {
    	            TVE_dac_set_source(i, DISP_TV_DAC_SRC_Y);
		            TVE_dac_enable(i);
    	        }
    	        else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_PB)
    	        {
    	            TVE_dac_set_source(i, DISP_TV_DAC_SRC_PB);
		            TVE_dac_enable(i);
    	        }
    	        else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_PR)
    	        {
    	            TVE_dac_set_source(i, DISP_TV_DAC_SRC_PR);
		            TVE_dac_enable(i);
    	        }
    	    }
    	}
    	break;

	default:
		break;
	}
}

__s32 BSP_disp_tv_open(__u32 sel)
{
	if(!(gdisp.screen[sel].status & TV_ON))
	{
    	__disp_tv_mode_t     tv_mod;

    	tv_mod = gdisp.screen[sel].tv_mode;


        image_clk_on(sel);
		Image_open(sel);//set image normal channel start bit , because every de_clk_off( )will reset this bit

        disp_clk_cfg(sel,DISP_OUTPUT_TYPE_TV, tv_mod);
		tve_clk_on();
		lcdc_clk_on(sel);

        TCON1_set_tv_mode(sel,tv_mod);
		TVE_set_tv_mode(sel, tv_mod);
		Disp_TVEC_DacCfg(sel, tv_mod);

        TCON1_open(sel);
        Disp_TVEC_Open(sel);

        Disp_Switch_Dram_Mode(DISP_OUTPUT_TYPE_TV, tv_mod);

        gdisp.screen[sel].status |= TV_ON;
        gdisp.screen[sel].lcdc_status |= LCDC_TCON1_USED;
        gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_TV;
	}
	return DIS_SUCCESS;
}


__s32 BSP_disp_tv_close(__u32 sel)
{
    if(gdisp.screen[sel].status & TV_ON)
    {
        TCON1_close(sel);
        Disp_TVEC_Close();

        tve_clk_off();
        image_clk_off(sel);
        lcdc_clk_off(sel);

        gdisp.screen[sel].status &= TV_OFF;
        gdisp.screen[sel].lcdc_status &= LCDC_TCON1_USED_MASK;
        gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_NONE;
		gdisp.screen[sel].pll_use_status &= ((gdisp.screen[sel].pll_use_status == VIDEO_PLL0_USED)? VIDEO_PLL0_USED_MASK : VIDEO_PLL1_USED_MASK);
    }
    return DIS_SUCCESS;
}

__s32 BSP_disp_tv_set_mode(__u32 sel, __disp_tv_mode_t tv_mod)
{
    if(tv_mod < DISP_TV_MOD_480I ||  tv_mod > DISP_TV_MOD_PAL_NC_CVBS_SVIDEO)
    {
        DE_WRN("unsupported tv mode in BSP_disp_tv_set_mode\n");
        return DIS_FAIL;
    }

    gdisp.screen[sel].tv_mode = tv_mod;
    gdisp.screen[sel].output_type = DISP_OUTPUT_TYPE_TV;
    return DIS_SUCCESS;
}


__s32 BSP_disp_tv_get_mode(__u32 sel)
{
    return gdisp.screen[sel].tv_mode;
}


__s32 BSP_disp_tv_get_interface(__u32 sel)
{
    __u8 dac[4];
    __s32 i = 0;
	__u32  ret = DISP_TV_NONE;

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_on();
    }

    for(i=0; i<4; i++)
    {
        dac[i] = TVE_get_dac_status(i);
    }

    if(dac[0]>1 || dac[1]>1 || dac[2]>1 || dac[3]>1)
    {
        DE_WRN("shor to ground\n");
    }
    else
    {
        for(i=0; i<4; i++)
        {
            if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_COMPOSITE && dac[i] == 1)
            {
                ret |= DISP_TV_CVBS;
            }
            else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_Y && dac[i] == 1)
            {
                ret |= DISP_TV_YPBPR;
            }
            else if(gdisp.screen[sel].dac_source[i] == DISP_TV_DAC_SRC_LUMA && dac[i] == 1)
            {
                ret |= DISP_TV_SVIDEO;
            }
        }
    }

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_off();
    }

    return  ret;
}



__s32 BSP_disp_tv_get_dac_status(__u32 sel, __u32 index)
{
	__u32  ret;

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_on();
    }

	ret = TVE_get_dac_status(index);

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_off();
    }

    return  ret;
}

__s32 BSP_disp_tv_set_dac_source(__u32 sel, __u32 index, __disp_tv_dac_source source)
{
	__u32  ret;

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_on();
    }

	ret = TVE_dac_set_source(index, source);

    if(!(gdisp.screen[sel].status & TV_ON))
    {
        tve_clk_off();
    }

    gdisp.screen[sel].dac_source[index] = source;

    return  ret;
}

__s32 BSP_disp_tv_get_dac_source(__u32 sel, __u32 index)
{
    return (__s32)gdisp.screen[sel].dac_source[index];
}

__s32 BSP_disp_tv_auto_check_enable(__u32 sel)
{
    TVE_dac_autocheck_enable(0);
    TVE_dac_autocheck_enable(1);
    TVE_dac_autocheck_enable(2);
    TVE_dac_autocheck_enable(3);

    return DIS_SUCCESS;
}


__s32 BSP_disp_tv_auto_check_disable(__u32 sel)
{
    TVE_dac_autocheck_disable(0);
    TVE_dac_autocheck_disable(1);
    TVE_dac_autocheck_disable(2);
    TVE_dac_autocheck_disable(3);

    return DIS_SUCCESS;
}

__s32 BSP_disp_tv_set_src(__u32 sel, __disp_lcdc_src_t src)
{
    switch (src)
    {
        case DISP_LCDC_SRC_DE_CH1:
            TCON1_select_src(sel, SRC_DE_CH1);
            break;

        case DISP_LCDC_SRC_DE_CH2:
            TCON1_select_src(sel, SRC_DE_CH2);
            break;

        case DISP_LCDC_SRC_BLUT:
            TCON1_select_src(sel, SRC_BLUE);
            break;

        default:
            DE_WRN("not supported lcdc src:%d in BSP_disp_tv_set_src\n", src);
            return DIS_NOT_SUPPORT;
    }
    return DIS_SUCCESS;
}

