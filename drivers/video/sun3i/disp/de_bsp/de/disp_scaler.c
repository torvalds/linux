
#include "disp_scaler.h"
#include "disp_display.h"
#include "disp_event.h"
#include "disp_layer.h"
#include "disp_clk.h"


// 0:scaler input pixel format
// 1:scaler input yuv mode
// 2:scaler input pixel sequence
// 3:scaler output format
__s32  Scaler_sw_para_to_reg(__u8 type, __u8 value)
{
	if(type == 0)//scaler input  pixel format
	{
	    if(value == DISP_FORMAT_YUV444)
	    {
	        return DE_SCAL_INYUV444;
	    }
	    else if(value == DISP_FORMAT_YUV420)
	    {
	        return DE_SCAL_INYUV420;
	    }
	    else if(value == DISP_FORMAT_YUV422)
	    {
	        return DE_SCAL_INYUV422;
	    }
	    else if(value == DISP_FORMAT_YUV411)
	    {
	        return DE_SCAL_INYUV411;
	    }
	    else if(value == DISP_FORMAT_CSIRGB)
	    {
	        return DE_SCAL_INCSIRGB;
	    }
	    else if(value == DISP_FORMAT_ARGB8888)
	    {
	        return DE_SCAL_INRGB888;
	    }
	    else if(value == DISP_FORMAT_RGB888)
	    {
	        return DE_SCAL_INRGB888;
	    }
	    else
	    {
	        DE_WRN("not supported scaler input pixel format:%d in Scaler_sw_para_to_reg\n",value);
	    }
    }
    else if(type == 1)//scaler input mode
    {
	    if(value == DISP_MOD_INTERLEAVED)
	    {
	        return DE_SCAL_INTER_LEAVED;
	    }
	    else if(value == DISP_MOD_MB_PLANAR)
	    {
	        return DE_SCAL_PLANNARMB;
	    }
	    else if(value == DISP_MOD_NON_MB_PLANAR)
	    {
	        return DE_SCAL_PLANNAR;
	    }
	    else if(value == DISP_MOD_NON_MB_UV_COMBINED)
	    {
	        return DE_SCAL_UVCOMBINED;
	    }
	    else if(value == DISP_MOD_MB_UV_COMBINED)
	    {
	        return DE_SCAL_UVCOMBINEDMB;
	    }
	    else
	    {
	        DE_WRN("not supported scaler input mode:%d in Scaler_sw_para_to_reg\n",value);
	    }
    }
    else if(type == 2)//scaler input pixel sequence
    {
	    if(value == DISP_SEQ_UYVY)
	    {
	        return DE_SCAL_UYVY;
	    }
	    else if(value == DISP_SEQ_YUYV)
	    {
	        return DE_SCAL_YUYV;
	    }
	    else if(value == DISP_SEQ_VYUY)
	    {
	        return DE_SCAL_VYUY;
	    }
	    else if(value == DISP_SEQ_YVYU)
	    {
	        return DE_SCAL_YVYU;
	    }
	    else if(value == DISP_SEQ_AYUV)
	    {
	        return DE_SCAL_AYUV;
	    }
	    else if(value == DISP_SEQ_UVUV)
	    {
	        return DE_SCAL_UVUV;
	    }
	    else if(value == DISP_SEQ_VUVU)
	    {
	        return DE_SCAL_VUVU;
	    }
	    else if(value == DISP_SEQ_ARGB)
	    {
	        return DE_SCAL_ARGB;
	    }
	    else if(value == DISP_SEQ_BGRA)
	    {
	        return DE_SCAL_BGRA;
	    }
	    else if(value == DISP_SEQ_P3210)
	    {
	        return 0;
	    }
	    else
	    {
	        DE_WRN("not supported scaler input pixel sequence:%d in Scaler_sw_para_to_reg\n",value);
	    }

    }
    else if(type == 3)//scaler output value
    {
		if(value == DISP_FORMAT_YUV444)
		{
			return DE_SCAL_OUTPYUV444;
		}
		else if(value == DISP_FORMAT_YUV422)
		{
			return DE_SCAL_OUTPYUV422;
		}
		else if(value == DISP_FORMAT_YUV420)
		{
			return DE_SCAL_OUTPYUV420;
		}
		else if(value == DISP_FORMAT_YUV411)
		{
			return DE_SCAL_OUTPYUV411;
		}
		else if(value == DISP_FORMAT_ARGB8888)
	    {
	        return DE_SCAL_OUTI0RGB888;
	    }
		else if(value == DISP_FORMAT_RGB888)
	    {
	        return DE_SCAL_OUTPRGB888;
	    }
	    else
	    {
	        DE_WRN("not supported scaler output value:%d in Scaler_sw_para_to_reg\n", value);
	    }
    }
    DE_WRN("not supported type:%d in Scaler_sw_para_to_reg\n", type);
    return DIS_FAIL;
}

#ifndef __LINUX_OSAL__
__s32 Scaler_event_proc(void *parg)
#else
__s32 Scaler_event_proc(__s32 irq, void *parg)
#endif
{
    __u8 fe_intflags;
    __u32 sel = (__u32)parg;

    fe_intflags = DE_SCAL_QueryINT(sel);
    if(fe_intflags & DE_WB_END_IE)
    {
		DE_SCAL_ClearINT(sel,DE_WB_END_IE);
		DE_SCAL_DisableINT(sel,DE_FE_INTEN_ALL);
		gdisp.init_para.scaler_finish(sel);
    }

    return OSAL_IRQ_RETURN;
}

__s32 Scaler_Init(__u32 sel)
{
    scaler_clk_init(sel);
    DE_SCAL_EnableINT(sel,DE_WB_END_IE);

    if(sel == 0)
    {
        OSAL_RegISR(INTC_IRQNO_SCALER0,0,Scaler_event_proc, (void *)sel,0,0);
        //OSAL_InterruptEnable(INTC_IRQNO_SCALER0);
    }
    else if(sel == 1)
    {
        OSAL_RegISR(INTC_IRQNO_SCALER1,0,Scaler_event_proc, (void *)sel,0,0);
        //OSAL_InterruptEnable(INTC_IRQNO_SCALER1);
    }

   	return DIS_SUCCESS;
}

__s32 Scaler_Exit(__u32 sel)
{
    if(sel == 0)
    {
        OSAL_InterruptDisable(INTC_IRQNO_SCALER0);
        OSAL_UnRegISR(INTC_IRQNO_SCALER0,Scaler_event_proc,(void*)sel);
    }
    else if(sel == 1)
    {
        OSAL_InterruptDisable(INTC_IRQNO_SCALER1);
        OSAL_UnRegISR(INTC_IRQNO_SCALER1,Scaler_event_proc,(void*)sel);
    }

    DE_SCAL_DisableINT(sel,DE_WB_END_IE);
    DE_SCAL_Reset(sel);
    DE_SCAL_Disable(sel);
    scaler_clk_off(sel);
    return DIS_SUCCESS;
}

__s32 Scaler_open(__u32 sel)
{
    scaler_clk_on(sel);
    DE_SCAL_Reset(sel);
    DE_SCAL_Enable(sel);

    return DIS_SUCCESS;
}

__s32 Scaler_close(__u32 sel)
{
    DE_SCAL_Reset(sel);
    DE_SCAL_Disable(sel);
    scaler_clk_off(sel);

    gdisp.scaler[sel].status &= SCALER_USED_MASK;
    return DIS_SUCCESS;
}

__s32 Scaler_Request(__u32 sel)
{
    __s32 ret = DIS_NO_RES;

    if(sel == 0)//request scaler0
    {
        if(!(gdisp.scaler[0].status & SCALER_USED))
        {
            ret = 0;
        }
    }
    else if(sel == 1)//request scaler1
    {
        if(!(gdisp.scaler[1].status & SCALER_USED))
        {
            ret = 1;
        }
    }
    else//request any scaler
    {
        if(!(gdisp.scaler[1].status & SCALER_USED))
        {
            ret = 1;
        }
        else if(!(gdisp.scaler[0].status & SCALER_USED))
        {
            ret = 0;
        }
    }

    if(ret == 0 || ret == 1)
    {
        Scaler_open(ret);
        gdisp.scaler[ret].b_close = FALSE;
        gdisp.scaler[ret].status |= SCALER_USED;
    }
    else
    {
        DE_WRN("request scaler fail\n");
    }
    return ret;
}



__s32 Scaler_Release(__u32 sel, __bool b_display)
{
    DE_SCAL_Set_Di_Ctrl(sel, 0, 0, 0, 0);
    if(b_display == FALSE)
    {
        Scaler_close(sel);
    }
    else
    {
        gdisp.scaler[sel].b_close = TRUE;
    }
    memset(&gdisp.scaler[sel], 0, sizeof(__disp_scaler_t));
    gdisp.scaler[sel].bright = 32;
    gdisp.scaler[sel].contrast = 32;
    gdisp.scaler[sel].saturation = 32;
    gdisp.scaler[sel].hue = 32;
    return DIS_SUCCESS;
}


__s32 Scaler_Set_Framebuffer(__u32 sel, __disp_fb_t *pfb)//keep the source window
{
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
    __disp_scaler_t * scaler;
    __u32 cpu_sr;

    scaler = &(gdisp.scaler[sel]);

	OSAL_IrqLock(&cpu_sr);
	memcpy(&scaler->in_fb, pfb, sizeof(__disp_fb_t));
	OSAL_IrqUnLock(cpu_sr);

	in_type.fmt= Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.mod= Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.ps= Scaler_sw_para_to_reg(2,(__u8)scaler->in_fb.seq);
	in_type.byte_seq = 0;

	scal_addr.ch0_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[0]));
	scal_addr.ch1_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[1]));
	scal_addr.ch2_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[2]));

	in_size.src_width = scaler->in_fb.size.width;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_width = scaler->src_win.width;
	in_size.scal_height = scaler->src_win.height;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = scaler->out_scan_mode;
	if(scaler->in_fb.cs_mode > DISP_VXYCC)
	{
		scaler->in_fb.cs_mode = DISP_BT601;
	}

	DE_SCAL_Config_Src(sel,&scal_addr,&in_size,&in_type,FALSE,FALSE);
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
    if(scaler->enhance_en == TRUE)
    {
        Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast, scaler->saturation, scaler->hue);
    }
    else
    {
	    DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601, get_fb_type(scaler->in_fb.format), DISP_FB_TYPE_RGB);
	}
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, scaler->smooth_mode);
	return DIS_SUCCESS;
}



__s32 Scaler_Get_Framebuffer(__u32 sel, __disp_fb_t *pfb)
{
    __disp_scaler_t * scaler;

    if(pfb==NULL)
    {
        return  DIS_PARA_FAILED;
    }

    scaler = &(gdisp.scaler[sel]);
    if(scaler->status & SCALER_USED)
    {
        memcpy(pfb,&scaler->in_fb, sizeof(__disp_fb_t));
    }
    else
    {
        return DIS_PARA_FAILED;
    }

    return DIS_SUCCESS;
}

__s32 Scaler_Set_Output_Size(__u32 sel, __disp_rectsz_t *size)
{
    __disp_scaler_t * scaler;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;

    scaler = &(gdisp.scaler[sel]);

	scaler->out_size.height = size->height;
	scaler->out_size.width = size->width;

	in_type.mod = Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2,(__u8)scaler->in_fb.seq);
	in_type.byte_seq = 0;

	in_size.src_width = scaler->src_win.width;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height= scaler->src_win.height;
	in_size.scal_width= scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = scaler->out_scan_mode;

	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
	if(scaler->enhance_en == TRUE)
    {
        Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast, scaler->saturation, scaler->hue);
    }
    else
    {
	    DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601, get_fb_type(scaler->in_fb.format), DISP_FB_TYPE_RGB);
	}
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, scaler->smooth_mode);
	DE_SCAL_Set_Out_Size(sel, &out_scan, &out_type, &out_size);

	return DIS_SUCCESS;
}

__s32 Scaler_Set_SclRegn(__u32 sel, __disp_rect_t *scl_rect)
{
    __disp_scaler_t * scaler;
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;

    scaler = &(gdisp.scaler[sel]);

	scaler->src_win.x         = scl_rect->x;
	scaler->src_win.y         = scl_rect->y;
	scaler->src_win.height    = scl_rect->height;
	scaler->src_win.width     = scl_rect->width;

	in_type.mod = Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2,(__u8)scaler->in_fb.seq);
	in_type.byte_seq = 0;

	scal_addr.ch0_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[0]));
	scal_addr.ch1_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[1]));
	scal_addr.ch2_addr= (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[2]));

	in_size.src_width = scaler->in_fb.size.width;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_width = scaler->src_win.width;
	in_size.scal_height = scaler->src_win.height;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = scaler->out_scan_mode;

	if(scaler->in_fb.cs_mode > DISP_VXYCC)
	{
		scaler->in_fb.cs_mode = DISP_BT601;
	}

	DE_SCAL_Config_Src(sel,&scal_addr,&in_size,&in_type,FALSE,FALSE);
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, scaler->smooth_mode);

	return DIS_SUCCESS;
}


__s32 Scaler_Get_SclRegn(__u32 sel, __disp_rect_t *scl_rect)
{
    __disp_scaler_t * scaler;

    if(scl_rect == NULL)
    {
        return  DIS_PARA_FAILED;
    }

    scaler = &(gdisp.scaler[sel]);
    if(scaler->status & SCALER_USED)
    {
        scl_rect->x = scaler->src_win.x;
        scl_rect->y = scaler->src_win.y;
        scl_rect->width = scaler->src_win.width;
        scl_rect->height = scaler->src_win.height;
    }
    else
    {
        return DIS_PARA_FAILED;
    }

    return DIS_SUCCESS;
}

__s32 Scaler_Set_Para(__u32 sel, __disp_scaler_t *scl)
{
    __disp_scaler_t * scaler;
	__scal_buf_addr_t scal_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;

    scaler = &(gdisp.scaler[sel]);

	memcpy(&(scaler->in_fb), &(scl->in_fb), sizeof(__disp_fb_t));
	memcpy(&(scaler->src_win), &(scl->src_win), sizeof(__disp_rect_t));
	memcpy(&(scaler->out_size), &(scl->out_size), sizeof(__disp_rectsz_t));

	in_type.mod = Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2,(__u8)scaler->in_fb.seq);
	in_type.byte_seq = 0;

	scal_addr.ch0_addr = (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[0]));
	scal_addr.ch1_addr = (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[1]));
	scal_addr.ch2_addr = (__u32)OSAL_VAtoPA((void*)(scaler->in_fb.addr[2]));

	in_size.src_width = scaler->in_fb.size.width;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height= scaler->src_win.height;
	in_size.scal_width= scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = scaler->out_scan_mode;

	if(scaler->in_fb.cs_mode > DISP_VXYCC)
	{
		scaler->in_fb.cs_mode = DISP_BT601;
	}

	DE_SCAL_Config_Src(sel,&scal_addr,&in_size,&in_type,FALSE,FALSE);
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
	DE_SCAL_Set_Init_Phase(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, FALSE);
	if(scaler->enhance_en == TRUE)
    {
        Scaler_Set_Enhance(sel, scaler->bright, scaler->contrast, scaler->saturation, scaler->hue);
    }
    else
    {
	    DE_SCAL_Set_CSC_Coef(sel, scaler->in_fb.cs_mode, DISP_BT601, get_fb_type(scaler->in_fb.format), DISP_FB_TYPE_RGB);
	}
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, scaler->smooth_mode);
	DE_SCAL_Set_Out_Format(sel, &out_type);
	DE_SCAL_Set_Out_Size(sel, &out_scan,&out_type, &out_size);

	return 0;
}

__s32 BSP_disp_scaler_set_smooth(__u32 sel, __disp_video_smooth_t  mode)
{
    __disp_scaler_t * scaler;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;

    scaler = &(gdisp.scaler[sel]);
	scaler->smooth_mode = mode;

	in_type.mod = Scaler_sw_para_to_reg(1,scaler->in_fb.mode);
	in_type.fmt = Scaler_sw_para_to_reg(0,scaler->in_fb.format);
	in_type.ps = Scaler_sw_para_to_reg(2,(__u8)scaler->in_fb.seq);
    in_type.byte_seq = 0;

	in_size.src_width = scaler->in_fb.size.width;
	in_size.x_off = scaler->src_win.x;
	in_size.y_off = scaler->src_win.y;
	in_size.scal_height= scaler->src_win.height;
	in_size.scal_width= scaler->src_win.width;

	out_type.byte_seq = scaler->out_fb.seq;
	out_type.fmt = scaler->out_fb.format;

	out_size.width = scaler->out_size.width;
	out_size.height = scaler->out_size.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = scaler->out_scan_mode;

	DE_SCAL_Set_Scaling_Coef(0, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, scaler->smooth_mode);
    scaler->b_reg_change = TRUE;

	return DIS_SUCCESS;
}

__s32 BSP_disp_scaler_get_smooth(__u32 sel)
{
	return gdisp.scaler[sel].smooth_mode;
}


__s32 BSP_disp_scaler_request(void)
{
    __s32 sel = 0;
    sel =  Scaler_Request(0xff);
	if(sel < 0)
		return sel;
	else
   	 	return SCALER_IDTOHAND(sel);
}

__s32 BSP_disp_scaler_release(__u32 handle)
{
    __u32 sel = 0;

    sel = SCALER_HANDTOID(handle);
    return Scaler_Release(sel, FALSE);
}

__s32 BSP_disp_scaler_start(__u32 handle,__disp_scaler_para_t *para)
{
	__scal_buf_addr_t in_addr;
	__scal_buf_addr_t out_addr;
	__scal_src_size_t in_size;
	__scal_out_size_t out_size;
	__scal_src_type_t in_type;
	__scal_out_type_t out_type;
	__scal_scan_mod_t in_scan;
	__scal_scan_mod_t out_scan;
	__u32 size = 0;
	__u32 sel = 0;
	__s32 ret = 0;

	if(para==NULL)
	{
		DE_WRN("input parameter can't be null!\n");
		return DIS_FAIL;
	}

	sel = SCALER_HANDTOID(handle);

	in_type.mod= Scaler_sw_para_to_reg(1,para->input_fb.mode);
	in_type.fmt= Scaler_sw_para_to_reg(0,para->input_fb.format);
	in_type.ps= Scaler_sw_para_to_reg(2,(__u8)para->input_fb.seq);
	in_type.byte_seq = 0;

	if(get_fb_type(para->output_fb.format) == DISP_FB_TYPE_YUV)
	{
		if(para->output_fb.mode == DISP_MOD_NON_MB_PLANAR)
		{
			out_type.fmt = Scaler_sw_para_to_reg(3, para->output_fb.format);
		}
		else
		{
			DE_WRN("output mode:%d invalid in Display_Scaler_Start\n",para->output_fb.mode);
			return DIS_FAIL;
		}
	}
	else
	{
		if(para->output_fb.mode == DISP_MOD_NON_MB_PLANAR && para->output_fb.format == DISP_FORMAT_RGB888)
		{
			out_type.fmt = DE_SCAL_OUTPRGB888;
		}
		else if(para->output_fb.mode == DISP_MOD_INTERLEAVED && para->output_fb.format == DISP_FORMAT_ARGB8888)
		{
			out_type.fmt = DE_SCAL_OUTI0RGB888;
		}
		else
		{
			DE_WRN("output para invalid in Display_Scaler_Start,mode:%d,format:%d\n",para->output_fb.mode, para->output_fb.format);
			return DIS_FAIL;
		}
		para->output_fb.br_swap= FALSE;
	}
	out_type.byte_seq = Scaler_sw_para_to_reg(2,para->output_fb.seq);

	out_size.width     = para->output_fb.size.width;
	out_size.height = para->output_fb.size.height;

	in_addr.ch0_addr = (__u32)OSAL_VAtoPA((void*)(para->input_fb.addr[0]));
	in_addr.ch1_addr = (__u32)OSAL_VAtoPA((void*)(para->input_fb.addr[1]));
	in_addr.ch2_addr = (__u32)OSAL_VAtoPA((void*)(para->input_fb.addr[2]));

	in_size.src_width = para->input_fb.size.width;
	in_size.x_off = para->source_regn.x;
	in_size.y_off = para->source_regn.y;
	in_size.scal_width= para->source_regn.width;
	in_size.scal_height= para->source_regn.height;

	in_scan.field = FALSE;
	in_scan.bottom = FALSE;

	out_scan.field = FALSE;	//when use scaler as writeback, won't be outinterlaced for any display device
	out_scan.bottom = FALSE;

	out_addr.ch0_addr = (__u32)OSAL_VAtoPA((void*)(para->output_fb.addr[0]));
	out_addr.ch1_addr = (__u32)OSAL_VAtoPA((void*)(para->output_fb.addr[1]));
	out_addr.ch2_addr = (__u32)OSAL_VAtoPA((void*)(para->output_fb.addr[2]));

	size = (para->input_fb.size.width * para->input_fb.size.height * de_format_to_bpp(para->input_fb.format) + 7)/8;
    OSAL_CacheRangeFlush((void *)para->input_fb.addr[0],size ,CACHE_CLEAN_FLUSH_D_CACHE_REGION);

	size = (para->output_fb.size.width * para->output_fb.size.height * de_format_to_bpp(para->output_fb.format) + 7)/8;
    OSAL_CacheRangeFlush((void *)para->output_fb.addr[0],size ,CACHE_FLUSH_D_CACHE_REGION);

	DE_SCAL_Config_Src(sel,&in_addr,&in_size,&in_type,FALSE,FALSE);
	DE_SCAL_Set_Scaling_Factor(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type);
	DE_SCAL_Set_Init_Phase(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, FALSE);
	DE_SCAL_Set_CSC_Coef(sel, para->input_fb.cs_mode, para->output_fb.cs_mode, get_fb_type(para->input_fb.format), get_fb_type(para->output_fb.format));
	DE_SCAL_Set_Scaling_Coef(sel, &in_scan, &in_size, &in_type, &out_scan, &out_size, &out_type, DISP_VIDEO_NATUAL);
	DE_SCAL_Set_Out_Format(sel, &out_type);
	DE_SCAL_Set_Out_Size(sel, &out_scan,&out_type, &out_size);
	DE_SCAL_Set_Writeback_Addr(sel,&out_addr);
    DE_SCAL_Output_Select(sel, 3);
	DE_SCAL_EnableINT(sel,DE_WB_END_IE);
	DE_SCAL_Start(sel);
	DE_SCAL_Set_Reg_Rdy(sel);
	DE_SCAL_Writeback_Enable(sel);

	ret = gdisp.init_para.scaler_begin(sel);


	DE_SCAL_Reset(sel);
	DE_SCAL_Writeback_Disable(sel);

	return ret;

}

__s32 Scaler_Set_Enhance(__u32 sel, __u32 bright, __u32 contrast, __u32 saturation, __u32 hue)
{
    __u32 b_yuv_in,b_yuv_out;
    __disp_scaler_t * scaler;

    scaler = &(gdisp.scaler[sel]);

    b_yuv_in = (get_fb_type(scaler->in_fb.format) == DISP_FB_TYPE_YUV)?1:0;
    b_yuv_out = (get_fb_type(scaler->out_fb.format) == DISP_FB_TYPE_YUV)?1:0;
    DE_SCAL_Set_CSC_Coef_Enhance(sel, scaler->in_fb.cs_mode, scaler->out_fb.cs_mode, b_yuv_in, b_yuv_out, bright, contrast, saturation, hue);
    scaler->b_reg_change = TRUE;

    return DIS_SUCCESS;
}

__s32 Scaler_Set_Outinterlace(__u32 sel)
{
	__disp_screen_t *screen;
	__disp_scaler_t *scaler;
	__u32 screen_index;

	scaler = &(gdisp.scaler[sel]);
	screen_index = scaler->screen_index;

	screen = &(gdisp.screen[screen_index]);

	if(screen->output_type == DISP_OUTPUT_TYPE_TV)
	{
		switch(screen->tv_mode)
		{
			case DISP_TV_MOD_480I:
			case DISP_TV_MOD_NTSC:
			case DISP_TV_MOD_NTSC_SVIDEO:
			case DISP_TV_MOD_NTSC_CVBS_SVIDEO:
			case DISP_TV_MOD_PAL_M:
			case DISP_TV_MOD_PAL_M_SVIDEO:
			case DISP_TV_MOD_PAL_M_CVBS_SVIDEO:
			case DISP_TV_MOD_576I:
			case DISP_TV_MOD_PAL:
			case DISP_TV_MOD_PAL_SVIDEO:
			case DISP_TV_MOD_PAL_CVBS_SVIDEO:
			case DISP_TV_MOD_PAL_NC:
			case DISP_TV_MOD_PAL_NC_SVIDEO:
			case DISP_TV_MOD_PAL_NC_CVBS_SVIDEO:
			case DISP_TV_MOD_1080I_50HZ:
			case DISP_TV_MOD_1080I_60HZ:
			scaler->out_scan_mode = TRUE;
			break;

			default:
			scaler->out_scan_mode = FALSE;
			break;
		}
	}
	else if(screen->output_type == DISP_OUTPUT_TYPE_HDMI)
	{
		switch(screen->hdmi_mode)
		{
			case DISP_TV_MOD_480I:
			case DISP_TV_MOD_576I:
			case DISP_TV_MOD_1080I_50HZ:
			case DISP_TV_MOD_1080I_60HZ:
			scaler->out_scan_mode = TRUE;
			break;

			default:
			scaler->out_scan_mode = FALSE;
			break;
		}
	}
	else	//lcd/ vga/ other progress hdmi and tv devices
	{
			scaler->out_scan_mode = FALSE;
	}
	return DIS_SUCCESS;
}

