#include <linux/amlogic/ge2d/ge2d.h>

static inline void _set_src1_format(ge2d_src1_data_t *src1_data_cfg,
                                     ge2d_src1_gen_t *src1_gen_cfg,
                                     ge2d_dp_gen_t *dp_gen_cfg,
                                     unsigned format_src,
                                     unsigned format_dst)
{
    src1_data_cfg->format_all  = format_src;

    src1_data_cfg->format      = (format_src >> 8) & 3;
    src1_data_cfg->mode_8b_sel = (format_src >> 6) & 3;
    src1_data_cfg->lut_en      = (format_src >> 5) & 1;
    src1_data_cfg->sep_en      = (format_src >> 2) & 1;

	src1_data_cfg->endian      = (format_src & GE2D_ENDIAN_MASK) >> GE2D_ENDIAN_SHIFT;
	src1_data_cfg->color_map   = (format_src & GE2D_COLOR_MAP_MASK) >> GE2D_COLOR_MAP_SHIFT;
   

    src1_gen_cfg->pic_struct   = (format_src >> 3) & 3;
    src1_data_cfg->x_yc_ratio  = (format_src >> 1) & 1;
    src1_data_cfg->y_yc_ratio  = (format_src >> 0) & 1;
    
    if ((format_src & GE2D_FORMAT_YUV) &&
        ((format_dst & GE2D_FORMAT_YUV) == 0)) {
        dp_gen_cfg->use_matrix_default = (format_src & GE2D_FORMAT_COMP_RANGE) ? MATRIX_FULL_RANGE_YCC_TO_RGB : MATRIX_YCC_TO_RGB;
        dp_gen_cfg->conv_matrix_en = 1;
    }
    else if (((format_src & GE2D_FORMAT_YUV) == 0) &&
             (format_dst & GE2D_FORMAT_YUV)) {
        dp_gen_cfg->use_matrix_default = MATRIX_RGB_TO_YCC;
        dp_gen_cfg->conv_matrix_en = 1;
    }
    else {
        dp_gen_cfg->conv_matrix_en = 0;
    }
}

static inline void _set_src2_format(ge2d_src2_dst_data_t *src2_dst_data_cfg,
                             ge2d_src2_dst_gen_t *src2_dst_gen_cfg,
                             unsigned format)
{
    src2_dst_data_cfg->src2_format_all  = format;

    src2_dst_data_cfg->src2_format      = (format >> 8) & 3;
	src2_dst_data_cfg->src2_endian      = (format & GE2D_ENDIAN_MASK) >> GE2D_ENDIAN_SHIFT;
	src2_dst_data_cfg->src2_color_map   = (format & GE2D_COLOR_MAP_MASK) >> GE2D_COLOR_MAP_SHIFT;
   
    src2_dst_data_cfg->src2_mode_8b_sel = (format >> 6) & 3;

    src2_dst_gen_cfg->src2_pic_struct   = (format >> 3) & 3;
}

static inline void _set_dst_format(ge2d_src2_dst_data_t *src2_dst_data_cfg,
                             ge2d_src2_dst_gen_t *src2_dst_gen_cfg,
                             ge2d_dp_gen_t *dp_gen_cfg,
                             unsigned format_src,
                             unsigned format_dst)
{
	src2_dst_data_cfg->dst_format_all  = format_dst;

	src2_dst_data_cfg->dst_format      = (format_dst >> 8) & 3;

	src2_dst_data_cfg->dst_endian      = (format_dst & GE2D_ENDIAN_MASK) >> GE2D_ENDIAN_SHIFT;
	src2_dst_data_cfg->dst_color_map   = (format_dst & GE2D_COLOR_MAP_MASK) >> GE2D_COLOR_MAP_SHIFT;

	src2_dst_data_cfg->dst_mode_8b_sel = (format_dst >> 6) & 3;

	src2_dst_gen_cfg->dst_pic_struct   = (format_dst >> 3) & 3;

	if ((format_src & GE2D_FORMAT_YUV) && 
				((format_dst & GE2D_FORMAT_YUV) == 0)) {
		dp_gen_cfg->use_matrix_default = (format_src & GE2D_FORMAT_COMP_RANGE) ? MATRIX_FULL_RANGE_YCC_TO_RGB : MATRIX_YCC_TO_RGB;
		dp_gen_cfg->conv_matrix_en = 1;
	}
	else if (((format_src & GE2D_FORMAT_YUV) == 0) &&
			(format_dst & GE2D_FORMAT_YUV)) {
		dp_gen_cfg->use_matrix_default = MATRIX_RGB_TO_YCC;
		dp_gen_cfg->conv_matrix_en = 1;
	}
	else {
		dp_gen_cfg->conv_matrix_en = 0;
	}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	/* for dest is nv21 or nv12 in m6. */
	if((format_dst & GE2D_FORMAT_YUV) && ((src2_dst_data_cfg->dst_color_map|1)==15)) {
		src2_dst_data_cfg->dst_format = 0;
		src2_dst_data_cfg->dst_mode_8b_sel = 0;
		src2_dst_data_cfg->dst2_pixel_byte_width = 1;
		src2_dst_data_cfg->dst2_discard_mode = 0xf;
		src2_dst_data_cfg->dst2_enable = 1;
		src2_dst_data_cfg->dst2_color_map = src2_dst_data_cfg->dst_color_map - 5;
	} else 
		src2_dst_data_cfg->dst2_enable = 0;
#endif
}
void ge2dgen_src(ge2d_context_t *wq,
                 unsigned canvas_addr,
                 unsigned format)
{
    ge2d_src1_data_t *src1_data_cfg = ge2d_wq_get_src_data(wq);
    ge2d_src1_gen_t *src1_gen_cfg = ge2d_wq_get_src_gen(wq);
    ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);
    ge2d_src2_dst_data_t *src2_dst_data_cfg = ge2d_wq_get_dst_data(wq);

    if ((format != src1_data_cfg->format_all) ||
        (canvas_addr != src1_data_cfg->canaddr)) {
        src1_data_cfg->canaddr = canvas_addr;

        _set_src1_format(src1_data_cfg, src1_gen_cfg, dp_gen_cfg,
            format, src2_dst_data_cfg->dst_format_all);
	  wq->config.update_flag |= UPDATE_SRC_DATA;
	  wq->config.update_flag |= UPDATE_SRC_GEN;
	  wq->config.update_flag |= UPDATE_DP_GEN;
    }
}

void ge2dgen_antiflicker(ge2d_context_t *wq,unsigned long enable)
{
	ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);

	enable = enable?1:0;  

	if(dp_gen_cfg->antiflick_en != enable)
	{
		dp_gen_cfg->antiflick_en=enable;
		wq->config.update_flag |= UPDATE_DP_GEN;
	}
}
void ge2dgen_post_release_src1buf(ge2d_context_t *wq, unsigned buffer)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->src1_buffer = buffer;
    ge2d_cmd_cfg->release_flag |= RELEASE_SRC1_BUFFER;
}

void ge2dgen_post_release_src1canvas(ge2d_context_t *wq)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->release_flag |= RELEASE_SRC1_CANVAS;
}

void ge2dgen_post_release_src2buf(ge2d_context_t *wq, unsigned buffer)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->src2_buffer = buffer;
    ge2d_cmd_cfg->release_flag |= RELEASE_SRC2_BUFFER;
}

void ge2dgen_post_release_src2canvas(ge2d_context_t *wq)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->release_flag |= RELEASE_SRC2_CANVAS;
}

void ge2dgen_cb(ge2d_context_t *wq, int (*cmd_cb)(unsigned), unsigned param)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->cmd_cb = cmd_cb;
    ge2d_cmd_cfg->cmd_cb_param = param;
    ge2d_cmd_cfg->release_flag |= RELEASE_CB;
}

void ge2dgen_src2(ge2d_context_t *wq,
                  unsigned canvas_addr,
                  unsigned format)
{
    ge2d_src2_dst_data_t *src2_dst_data_cfg = ge2d_wq_get_dst_data(wq);
    ge2d_src2_dst_gen_t *src2_dst_gen_cfg = ge2d_wq_get_dst_gen(wq);

    if ((format != src2_dst_data_cfg->src2_format_all) ||
        (canvas_addr != src2_dst_data_cfg->src2_canaddr)) {

        src2_dst_data_cfg->src2_canaddr = canvas_addr;

        _set_src2_format(src2_dst_data_cfg, src2_dst_gen_cfg, format);
	 wq->config.update_flag |= UPDATE_DST_DATA;
	 wq->config.update_flag |= UPDATE_DST_GEN;
    }
}

void ge2dgen_dst(ge2d_context_t *wq,
                 unsigned canvas_addr,
                 unsigned format)
{
    ge2d_src1_data_t *src1_data_cfg = ge2d_wq_get_src_data(wq);
    ge2d_src2_dst_data_t *src2_dst_data_cfg = ge2d_wq_get_dst_data(wq);
    ge2d_src2_dst_gen_t *src2_dst_gen_cfg = ge2d_wq_get_dst_gen(wq);
    ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);

    if ((format != src2_dst_data_cfg->dst_format_all) ||
        (canvas_addr != src2_dst_data_cfg->dst_canaddr)) {
        src2_dst_data_cfg->dst_canaddr = canvas_addr;

      	_set_dst_format(src2_dst_data_cfg, src2_dst_gen_cfg, dp_gen_cfg,
            src1_data_cfg->format_all, format);
	wq->config.update_flag |= UPDATE_DST_DATA;
      	wq->config.update_flag |= UPDATE_DST_GEN;
	wq->config.update_flag |= UPDATE_DP_GEN;  
    }
}

void ge2dgen_src_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h)
{
    	ge2d_src1_gen_t *src1_gen_cfg = ge2d_wq_get_src_gen(wq);
	//adjust w->x_end h->y_end
	w=x+w-1 ;
	h=y+h-1 ;
	if(src1_gen_cfg->clipx_start != x || src1_gen_cfg->clipx_end   != w||
			src1_gen_cfg->clipy_start != y ||src1_gen_cfg->clipy_end   != h)
	{
    		src1_gen_cfg->clipx_start = x;
    		src1_gen_cfg->clipx_end   = w;
    		src1_gen_cfg->clipy_start = y;
    		src1_gen_cfg->clipy_end   = h;
		wq->config.update_flag |= UPDATE_SRC_GEN;	
	}
}

void ge2dgen_src2_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h)
{
    	ge2d_src2_dst_gen_t *src2_dst_gen_cfg = ge2d_wq_get_dst_gen(wq);

	//adjust w->x_end h->y_end
	w=x+w-1 ;
	h=y+h-1 ;
	if(src2_dst_gen_cfg->src2_clipx_start != x || src2_dst_gen_cfg->src2_clipx_end   != w ||
			src2_dst_gen_cfg->src2_clipy_start != y || src2_dst_gen_cfg->src2_clipy_end   != h)
	{
    		src2_dst_gen_cfg->src2_clipx_start = x;
    		src2_dst_gen_cfg->src2_clipx_end   = w;
    		src2_dst_gen_cfg->src2_clipy_start = y;
    		src2_dst_gen_cfg->src2_clipy_end   = h;
		wq->config.update_flag |= UPDATE_DST_GEN;
	}
}

void ge2dgen_src_key(ge2d_context_t *wq,
                     int en, int key, int keymask,int keymode)
{
    	ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);
	if(dp_gen_cfg->src1_key_en != en || dp_gen_cfg->src1_key != key || dp_gen_cfg->src1_key_mask != keymask ||
		dp_gen_cfg->src1_key_mode != keymode)      
	{
    		dp_gen_cfg->src1_key_en = en&0x1;
    		dp_gen_cfg->src1_key = key;
    		dp_gen_cfg->src1_key_mask = keymask;
		dp_gen_cfg->src1_key_mode=keymode&0x1;	

		dp_gen_cfg->src1_vsc_bank_length = 4;
    		dp_gen_cfg->src1_hsc_bank_length = 4;
		wq->config.update_flag |= UPDATE_DP_GEN;	
	}
}
EXPORT_SYMBOL(ge2dgen_src_key);
void ge2dgent_src_gbalpha(ge2d_context_t *wq,
                          unsigned char alpha)
{
    	ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);
	if(dp_gen_cfg->src1_gb_alpha != alpha)
	{
    		dp_gen_cfg->src1_gb_alpha = alpha;
		wq->config.update_flag |= UPDATE_DP_GEN;
	}
}

void ge2dgen_src_color(ge2d_context_t *wq,
                       unsigned color)
{
    	ge2d_src1_data_t *src1_data_cfg = ge2d_wq_get_src_data(wq);

    	if(src1_data_cfg->def_color != color)	
    	{
    		src1_data_cfg->def_color = color;
    		wq->config.update_flag |= UPDATE_SRC_DATA;	
    	}

}

void ge2dgen_rendering_dir(ge2d_context_t *wq,
                           int src_x_dir, int src_y_dir,
                           int dst_x_dir, int dst_y_dir,
                           int dst_xy_swap)
{
    ge2d_cmd_t *ge2d_cmd_cfg = ge2d_wq_get_cmd(wq);

    ge2d_cmd_cfg->src1_x_rev = src_x_dir;
    ge2d_cmd_cfg->src1_y_rev = src_y_dir;
    ge2d_cmd_cfg->dst_x_rev  = dst_x_dir;
    ge2d_cmd_cfg->dst_y_rev  = dst_y_dir;
    ge2d_cmd_cfg->dst_xy_swap  =  dst_xy_swap;
}

void ge2dgen_dst_clip(ge2d_context_t *wq,
                      int x, int y, int w, int h, int mode)
{
    	ge2d_src2_dst_gen_t *src2_dst_gen_cfg = ge2d_wq_get_dst_gen(wq);
	//adjust w->x_end h->y_end
	w=x+w-1 ;
	h=y+h-1 ;
	if(src2_dst_gen_cfg->dst_clipx_start != x || src2_dst_gen_cfg->dst_clipx_end   != w||
		src2_dst_gen_cfg->dst_clipy_start != y || src2_dst_gen_cfg->dst_clipy_end   != h ||
		src2_dst_gen_cfg->dst_clip_mode   != mode)
	{
    		src2_dst_gen_cfg->dst_clipx_start = x;
    		src2_dst_gen_cfg->dst_clipx_end   = w;
    		src2_dst_gen_cfg->dst_clipy_start = y;
    		src2_dst_gen_cfg->dst_clipy_end   = h;
    		src2_dst_gen_cfg->dst_clip_mode   = mode;
		wq->config.update_flag |= UPDATE_DST_GEN;	
	}
}

void ge2dgent_src2_clip(ge2d_context_t *wq,
                        int x, int y, int w, int h)
{
    	ge2d_src2_dst_gen_t *src2_dst_gen_cfg = ge2d_wq_get_dst_gen(wq);
	//adjust w->x_end h->y_end
	w=x+w-1 ;
	h=y+h-1 ;
	if(src2_dst_gen_cfg->src2_clipx_start != x||src2_dst_gen_cfg->src2_clipx_end   != w||
		src2_dst_gen_cfg->src2_clipy_start != y || src2_dst_gen_cfg->src2_clipy_end   != h)
	{
    		src2_dst_gen_cfg->src2_clipx_start = x;
    		src2_dst_gen_cfg->src2_clipx_end   = w;
    		src2_dst_gen_cfg->src2_clipy_start = y;
    		src2_dst_gen_cfg->src2_clipy_end   = h;
		wq->config.update_flag |= UPDATE_DST_GEN;
	}
}

void ge2dgen_const_color(ge2d_context_t *wq,
                       unsigned color)
{
    	ge2d_dp_gen_t *dp_gen_cfg = ge2d_wq_get_dp_gen(wq);
	if(dp_gen_cfg->alu_const_color != color)
	{
    		dp_gen_cfg->alu_const_color = color;
		wq->config.update_flag |= UPDATE_DP_GEN;	
	}
}
