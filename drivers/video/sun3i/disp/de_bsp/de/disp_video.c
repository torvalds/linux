
#include "disp_video.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_scaler.h"

frame_para_t g_video[2];

#if 0
static tv_mode_info_t tv_info[23]=
{
    {DISP_TV_MOD_480I,				720,	480,	1,	30000,	263}, //480i
    {DISP_TV_MOD_576I,				720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_480P,				720,	480,	0,	60000,	525}, //480p
    {DISP_TV_MOD_576P,				720,	576,	0,	50000,	621}, //576p
    {DISP_TV_MOD_720P_50HZ,			1280,	720,	0,	50000,	750}, //720p50
    {DISP_TV_MOD_720P_60HZ,			1280,	720,	0,	60000,	750}, //720p60
    {DISP_TV_MOD_1080I_50HZ,		1920,	1080,	1,	25000,	561}, //1080i25
    {DISP_TV_MOD_1080I_60HZ,		1920,	1080,	1,	30000,	561}, //1080i30
    {DISP_TV_MOD_1080P_24HZ,		1920,	1080,	0,	24000,	1125}, //1080p24
    {DISP_TV_MOD_1080P_50HZ,		1920,	1080,	0,	50000,	1125}, //1080p50
    {DISP_TV_MOD_1080P_60HZ,		1920,	1080,	0,	60000,	1125}, //1080p60
    {DISP_TV_MOD_PAL,				720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_PAL_SVIDEO,		720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_PAL_CVBS_SVIDEO,	720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_NTSC,				720,	480,	1,	30000,	263}, //480i
    {DISP_TV_MOD_NTSC_SVIDEO,		720,	480,	1,	30000,	263}, //480i
    {DISP_TV_MOD_NTSC_CVBS_SVIDEO,	720,	480,	1,	30000,	263}, //480i
    {DISP_TV_MOD_PAL_M,				720,	480,	1,	30000,	263}, //pal-m
    {DISP_TV_MOD_PAL_M_SVIDEO,		720,	480,	1,	30000,	263}, //pal-m
    {DISP_TV_MOD_PAL_M_CVBS_SVIDEO,	720,	480,	1,	30000,	263}, //pal-m
    {DISP_TV_MOD_PAL_NC,			720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_PAL_NC_SVIDEO,		720,	576,	1,	25000,	311}, //576i
    {DISP_TV_MOD_PAL_NC_CVBS_SVIDEO,720,	576,	1,	25000,	311}, //576i
};

static __inline __bool Get_Output_Interlace(__u32 sel)
{
	__disp_tv_mode_t out_tv_mode;

	if(gdisp.screen[sel].status & TV_ON)
	{
		out_tv_mode = gdisp.screen[sel].tv_mode;
	}
	else if(gdisp.screen[sel].status & HDMI_ON)
	{
		out_tv_mode = gdisp.screen[sel].hdmi_mode;
	}
	else
	{
		return FALSE;
	}

	return tv_info[out_tv_mode].interlace;
}

static __inline __s32 Get_Case_Num(__u32 sel, frame_para_t * in_para)
{
	__disp_tv_mode_t out_tv_mode;
	tv_mode_info_t *cur_tv = NULL;
	__s32 in_fps = 0;
	__s32 out_fps = 0;

	if(in_para->video_cur.interlace == FALSE)
	{
		return CASE_P_SOURCE;
	}

	if(gdisp.screen[sel].status & TV_ON)
	{
		out_tv_mode = gdisp.screen[sel].tv_mode;
	}
	else if(gdisp.screen[sel].status & HDMI_ON)
	{
		out_tv_mode = gdisp.screen[sel].hdmi_mode;
	}
	else
	{
		return CASE_I_DIFF_FRAME_RATE;
	}
	cur_tv = &tv_info[out_tv_mode];

	in_fps = (in_para->video_cur.frame_rate+100)/1000;
	out_fps = ((cur_tv->frame_rate+100)/1000)/(2-cur_tv->interlace);
	if(in_fps==out_fps)
	{
		return CASE_I_SAME_FRAME_RATE;
	}
	else
	{
		return CASE_I_DIFF_FRAME_RATE;
	}
}
#endif

static __inline __s32 Hal_Set_Frame(__u32 sel, __u32 scaler_index)
{
	__scal_buf_addr_t scal_addr;
    __scal_src_size_t in_size;
    __scal_out_size_t out_size;
    __scal_src_type_t in_type;
    __scal_out_type_t out_type;
    __scal_scan_mod_t in_scan;
    __scal_scan_mod_t out_scan;
    __disp_scaler_t * scaler;
    __disp_video_fb_t * video_fb;
    __u32 pre_frame_addr;
    __u32 maf_flag_addr;
    __u32 maf_linestride;

	if(Is_In_Valid_Regn(sel, LCDC_get_start_delay(0) -5) == FALSE)
	{
		return DIS_FAIL;
	}

    memcpy(&g_video[scaler_index].video_cur, &g_video[scaler_index].video_new, sizeof(__disp_video_fb_t));
    scaler = &(gdisp.scaler[scaler_index]);
    video_fb = &(g_video[scaler_index].video_cur);

	if(g_video[scaler_index].dit_enable == FALSE)
	{
	    g_video[scaler_index].fetch_field = FALSE;
	    g_video[scaler_index].fetch_bot = FALSE;
	    g_video[scaler_index].dit_enable = FALSE;
	    g_video[scaler_index].dit_mode = DIT_MODE_WEAVE;
	    g_video[scaler_index].tempdiff_en = FALSE;
	    g_video[scaler_index].diagintp_en = FALSE;
    }
    else
    {
    	g_video[scaler_index].diagintp_en = TRUE;

    	if(g_video[scaler_index].field_cnt != 0)
    	{
    		g_video[scaler_index].fetch_bot = (g_video[scaler_index].fetch_bot == 0)?1:0;
    		g_video[scaler_index].field_cnt++;
    	}

    	pre_frame_addr = (__u32)OSAL_VAtoPA((void*)g_video[scaler_index].pre_frame_addr0);
    	maf_flag_addr = (__u32)OSAL_VAtoPA((void*)g_video[scaler_index].video_cur.flag_addr);

		maf_linestride =  g_video[scaler_index].video_cur.flag_stride;
    }
    //if(g_video[scaler_index].video_cur.interlace)
    //{
    //    g_video[scaler_index].dit_enable = TRUE;
    //    g_video[scaler_index].dit_mode = DIT_MODE_MAF_BOB;
    //}

	in_type.fmt= Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.mod= Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.ps= Scaler_sw_para_to_reg(2,scaler->in_fb.seq);
	in_type.byte_seq = 0;

	scal_addr.ch0_addr= (__u32)OSAL_VAtoPA((void*)(video_fb->addr[0]));
	scal_addr.ch1_addr= (__u32)OSAL_VAtoPA((void*)(video_fb->addr[1]));
	scal_addr.ch2_addr= (__u32)OSAL_VAtoPA((void*)(video_fb->addr[2]));

	in_size.src_width = scaler->in_fb.size.width;
	in_size.x_off =  scaler->src_win.x;
	in_size.y_off =  scaler->src_win.y;
	in_size.scal_height=  scaler->src_win.height;
	in_size.scal_width=  scaler->src_win.width;

	out_type.byte_seq =  scaler->out_fb.seq;
	out_type.fmt =  scaler->out_fb.format;

	out_size.width =  scaler->out_size.width;
	out_size.height =  scaler->out_size.height;

	in_scan.field = g_video[scaler_index].fetch_field;
	in_scan.bottom = g_video[scaler_index].fetch_bot;

	out_scan.field = scaler->out_scan_mode;

	if(scaler->out_fb.cs_mode > DISP_VXYCC)
	{
		scaler->out_fb.cs_mode = DISP_BT601;
	}

	DE_SCAL_Config_Src(scaler_index,&scal_addr,&in_size,&in_type,g_video[scaler_index].fetch_field,FALSE);
	DE_SCAL_Set_Init_Phase(scaler_index, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, FALSE);
	DE_SCAL_Set_Scaling_Factor(scaler_index, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
	DE_SCAL_Set_Scaling_Coef(scaler_index, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type,  scaler->smooth_mode);
	DE_SCAL_Set_Di_Ctrl(scaler_index,g_video[scaler_index].dit_enable,g_video[scaler_index].dit_mode,g_video[scaler_index].diagintp_en,g_video[scaler_index].tempdiff_en);
	DE_SCAL_Set_Di_PreFrame_Addr(scaler_index, pre_frame_addr);
	DE_SCAL_Set_Di_MafFlag_Src(scaler_index, maf_flag_addr, maf_linestride);
    DE_SCAL_Set_Out_Size(scaler_index, &out_scan,&out_type, &out_size);

    gdisp.scaler[scaler_index].b_reg_change = TRUE;

	return DIS_SUCCESS;
}


__s32 Video_Operation_In_Vblanking(__u32 sel)
{
    __u32 scaler_index = 0;

    for(scaler_index = 0; scaler_index<2; scaler_index++)
    {
        if((gdisp.scaler[scaler_index].status & SCALER_USED) &&
            (g_video[scaler_index].enable == TRUE) &&
            (g_video[scaler_index].have_got_frame == TRUE) &&
            (gdisp.scaler[scaler_index].screen_index == sel))
        {
    		Hal_Set_Frame(sel, scaler_index);
    	}
    }

	return DIS_SUCCESS;
}

__s32 BSP_disp_video_set_fb(__u32 sel, __u32 hid, __disp_video_fb_t *in_addr)
{
    hid = HANDTOID(hid);
    HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

    if(gdisp.screen[sel].layer_manage[hid].status & LAYER_USED &&
        gdisp.screen[sel].layer_manage[hid].para.mode == DISP_LAYER_WORK_MODE_SCALER)
    {
        __u32 scaler_index = 0;

        scaler_index = gdisp.screen[sel].layer_manage[hid].scaler_index;
    	memcpy(&g_video[scaler_index].video_new, in_addr, sizeof(__disp_video_fb_t));
    	g_video[scaler_index].have_got_frame = TRUE;
    	g_video[scaler_index].case_num = CASE_P_SOURCE;

		if(g_video[scaler_index].video_new.interlace == TRUE)
		{
			g_video[scaler_index].dit_enable = FALSE;	//FOR RELEASE VERSION, TURE for debug ver

			if(g_video[scaler_index].video_new.maf_valid == TRUE)
			{
				g_video[scaler_index].dit_mode = DIT_MODE_WEAVE;//FOR RELEASE VERSION, DIT_MODE_MAF for debug ver
			}
			else
			{
				g_video[scaler_index].dit_mode = DIT_MODE_WEAVE;//FOR RELEASE VERSION, DIT_MODE_MAF_BOB for debug ver
			}

			if(g_video[scaler_index].video_new.pre_frame_valid == TRUE)
			{
				g_video[scaler_index].tempdiff_en = FALSE;	//FOR RELEASE VERSION, TURE for debug ver
			}
			else
			{
				g_video[scaler_index].tempdiff_en = FALSE;
			}

			if(g_video[scaler_index].video_new.top_field_first == TRUE)
			{
				g_video[scaler_index].fetch_bot = FALSE;
			}
			else
			{
				g_video[scaler_index].fetch_bot = TRUE;
			}
			g_video[scaler_index].field_cnt = 0;

		}
		else
		{
			g_video[scaler_index].dit_enable = FALSE;
		}

		g_video[scaler_index].pre_frame_addr0 = g_video[scaler_index].video_cur.addr[0];
    	return DIS_SUCCESS;
    }
    else
    {
        return DIS_FAIL;
    }
}


__s32 BSP_disp_video_get_frame_id(__u32 sel, __u32 hid)//get the current displaying frame id
{
    hid = HANDTOID(hid);
    HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

    if(gdisp.screen[sel].layer_manage[hid].status & LAYER_USED &&
        gdisp.screen[sel].layer_manage[hid].para.mode == DISP_LAYER_WORK_MODE_SCALER)
    {
        __u32 scaler_index = 0;

        scaler_index = gdisp.screen[sel].layer_manage[hid].scaler_index;
        if(g_video[scaler_index].have_got_frame == TRUE)
        {
            return g_video[scaler_index].video_cur.id;
        }
        else
        {
            return DIS_FAIL;
        }
    }
    else
    {
        return DIS_FAIL;
    }
}

__s32 BSP_disp_video_get_dit_info(__u32 sel, __u32 hid, __disp_dit_info_t * dit_info)
{
    hid = HANDTOID(hid);
    HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

    if((gdisp.screen[sel].layer_manage[hid].status & LAYER_USED) &&
        (gdisp.screen[sel].layer_manage[hid].para.mode == DISP_LAYER_WORK_MODE_SCALER))
    {
        __u32 scaler_index = 0;

    	dit_info->maf_enable = FALSE;
    	dit_info->pre_frame_enable = FALSE;

    	if(g_video[scaler_index].dit_enable)
    	{
    		if(g_video[scaler_index].dit_mode == DIT_MODE_MAF)
    		{
    			dit_info->maf_enable = TRUE;
    		}
    		if(g_video[scaler_index].tempdiff_en)
    		{
    			dit_info->pre_frame_enable = TRUE;
    		}
    	}
	}
	return DIS_SUCCESS;
}
__s32 BSP_disp_video_start(__u32 sel, __u32 hid)
{
    hid = HANDTOID(hid);
    HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

    if((gdisp.screen[sel].layer_manage[hid].status & LAYER_USED) &&
        (gdisp.screen[sel].layer_manage[hid].para.mode == DISP_LAYER_WORK_MODE_SCALER))
    {
        __u32 scaler_index = 0;

        scaler_index = gdisp.screen[sel].layer_manage[hid].scaler_index;
        memset(&g_video[scaler_index], 0, sizeof(frame_para_t));
        g_video[scaler_index].video_cur.id = -1;
        g_video[scaler_index].enable = TRUE;

    	return DIS_SUCCESS;
    }
    else
    {
        return DIS_FAIL;
    }
}

__s32 BSP_disp_video_stop(__u32 sel, __u32 hid)
{
    hid = HANDTOID(hid);
    HLID_ASSERT(hid, gdisp.screen[sel].max_layers);

    if(gdisp.screen[sel].layer_manage[hid].status & LAYER_USED &&
        gdisp.screen[sel].layer_manage[hid].para.mode == DISP_LAYER_WORK_MODE_SCALER)
    {
        g_video[sel].enable = FALSE;
        memset(&g_video[sel], 0, sizeof(frame_para_t));

    	return DIS_SUCCESS;
    }
    else
    {
        return DIS_FAIL;
    }

}

