#include <linux/amlogic/ge2d/ge2d.h>

static const  unsigned int filt_coef0[] =   //bicubic
			{
			0x00800000,
			0x007f0100,
			0xff7f0200,
			0xfe7f0300,
			0xfd7e0500,
			0xfc7e0600,
			0xfb7d0800,
			0xfb7c0900,
			0xfa7b0b00,
			0xfa7a0dff,
			0xf9790fff,
			0xf97711ff,
			0xf87613ff,
			0xf87416fe,
			0xf87218fe,
			0xf8701afe,
			0xf76f1dfd,
			0xf76d1ffd,
			0xf76b21fd,
			0xf76824fd,
			0xf76627fc,
			0xf76429fc,
			0xf7612cfc,
			0xf75f2ffb,
			0xf75d31fb,
			0xf75a34fb,
			0xf75837fa,
			0xf7553afa,
			0xf8523cfa,
			0xf8503ff9,
			0xf84d42f9,
			0xf84a45f9,
			0xf84848f8
			};
			
static const    unsigned int filt_coef1[] =  //2 point bilinear
			{
			0x00800000,
			0x007e0200,
			0x007c0400,
			0x007a0600,
			0x00780800,
			0x00760a00,
			0x00740c00,
			0x00720e00,
			0x00701000,
			0x006e1200,
			0x006c1400,
			0x006a1600,
			0x00681800,
			0x00661a00,
			0x00641c00,
			0x00621e00,
			0x00602000,
			0x005e2200,
			0x005c2400,
			0x005a2600,
			0x00582800,
			0x00562a00,
			0x00542c00,
			0x00522e00,
			0x00503000,
			0x004e3200,
			0x004c3400,
			0x004a3600,
			0x00483800,
			0x00463a00,
			0x00443c00,
			0x00423e00,
			0x00404000
			};

static const    unsigned int filt_coef2[] =  //3 point triangle
			{
            0x40400000, 
            0x3f400100,  
            0x3d410200,
            0x3c410300, 
            0x3a420400,
            0x39420500,
            0x37430600,
            0x36430700, 
            0x35430800, 
            0x33450800,
            0x32450900, 
            0x31450a00, 
            0x30450b00, 
            0x2e460c00, 
            0x2d460d00, 
            0x2c470d00, 
            0x2b470e00, 
            0x29480f00, 
            0x28481000, 
            0x27481100, 
            0x26491100, 
            0x25491200, 
            0x24491300, 
            0x234a1300, 
            0x224a1400, 
            0x214a1500, 
            0x204a1600, 
            0x1f4b1600, 
            0x1e4b1700, 
            0x1d4b1800, 
            0x1c4c1800, 
            0x1b4c1900, 
            0x1a4c1a00
            }; 
static const        unsigned int filt_coef3[] =  //3 point triangle
		{
            0x20402000, 
            0x00,  
            0x00,
            0x00, 
            0x00,
            0x00,
            0x00,
            0x00, 
            0x00, 
            0x00,
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00, 
            0x00
            };     
			
//####################################################################################################
// ge2d_set_src1_data
//####################################################################################################
void ge2d_set_src1_data (ge2d_src1_data_t *cfg)
{
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->urgent_en,  10, 1);

   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->ddr_burst_size_y,  20, 2);
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->ddr_burst_size_cb, 18, 2);
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->ddr_burst_size_cr, 16, 2);

   WRITE_MPEG_REG(GE2D_SRC1_CANVAS, 
   				  ((cfg->canaddr & 0xff) << 24) |
                  (((cfg->canaddr >> 8) & 0xff) << 16) |
                  (((cfg->canaddr >> 16) & 0xff) << 8));

   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, ((cfg->x_yc_ratio << 1) | cfg->y_yc_ratio), 10, 2);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->sep_en, 0, 1);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->endian, 7, 1);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->color_map, 3, 4);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->format, 0, 2);
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->mode_8b_sel, 5, 2);
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->lut_en, 3, 1);

   WRITE_MPEG_REG(GE2D_SRC1_DEF_COLOR, cfg->def_color);
   if (cfg->x_yc_ratio) 
     WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, 1, 18, 1); //horizontal formatter enable 
   else
     WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, 0, 18, 1); //horizontal formatter disable 
   if (cfg->y_yc_ratio) 
     WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, 1, 16, 1);  //vertical formatter enable
   else
     WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, 0, 16, 1);  //vertical formatter disable
}

//####################################################################################################
// ge2d_set_src1_scale_coef
//####################################################################################################
void ge2d_set_src1_scale_coef (unsigned v_filt_type, unsigned h_filt_type)
{  
    int i;
   
    //write vert filter coefs
    WRITE_MPEG_REG(GE2D_SCALE_COEF_IDX, 0x0000);
	for (i = 0; i < 33; i++)
	{
        if (v_filt_type == FILTER_TYPE_BICUBIC)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef0[i]); //bicubic
        else if (v_filt_type == FILTER_TYPE_BILINEAR)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef1[i]); //bilinear
        else if ((v_filt_type & 0xf) == FILTER_TYPE_TRIANGLE)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef2[i]); //3 pointer triangle
        else
        {
            //todo
             WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef3[i]);
        }
	}

    //write horz filter coefs
    WRITE_MPEG_REG(GE2D_SCALE_COEF_IDX, 0x0100);
	for (i = 0; i < 33; i++)
	{
        if (h_filt_type == FILTER_TYPE_BICUBIC)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef0[i]); //bicubic
        else if (h_filt_type == FILTER_TYPE_BILINEAR)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef1[i]); //bilinear
        else if (h_filt_type == FILTER_TYPE_TRIANGLE)
	        WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef2[i]); //3 pointer triangle
        else
        {
            //todo
            WRITE_MPEG_REG(GE2D_SCALE_COEF, filt_coef3[i]);
        }
    }			
    
}

//####################################################################################################
// ge2d_set_src1_gen
//####################################################################################################
void ge2d_set_src1_gen (ge2d_src1_gen_t *cfg)
{
   WRITE_MPEG_REG(GE2D_SRC1_CLIPX_START_END, 
                         (cfg->clipx_start_ex << 31) | 
                         (cfg->clipx_start << 16) |
                         (cfg->clipx_end_ex << 15) |
                         (cfg->clipx_end << 0)
                         ); 

   WRITE_MPEG_REG(GE2D_SRC1_CLIPY_START_END, 
                         (cfg->clipy_start_ex << 31) | 
                         (cfg->clipy_start << 16) |
                         (cfg->clipy_end_ex << 15) |
                         (cfg->clipy_end << 0)
                         ); 

   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->pic_struct, 1, 2);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, (cfg->fill_mode & 0x1), 4, 1); 

   WRITE_MPEG_REG_BITS (GE2D_SRC_OUTSIDE_ALPHA, ((cfg->fill_mode & 0x2) <<7) | 
                                        cfg->outside_alpha, 0, 9);
   
   WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, cfg->chfmt_rpt_pix, 19, 1); 
   WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, cfg->cvfmt_rpt_pix, 17, 1);   
}


//####################################################################################################
// ge2d_set_src2_dst_data
//####################################################################################################
void ge2d_set_src2_dst_data (ge2d_src2_dst_data_t *cfg)
{
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->urgent_en,  9, 1);

	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->ddr_burst_size, 22, 2);

	/* only for m6 and later chips. */
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	WRITE_MPEG_REG(GE2D_SRC2_DST_CANVAS, (cfg->src2_canaddr << 8) |
			 ((cfg->dst_canaddr & 0xff) << 0) |
			 ((cfg->dst_canaddr & 0xff00) << 8)
			 ); 
#else
	WRITE_MPEG_REG(GE2D_SRC2_DST_CANVAS, (cfg->src2_canaddr << 8) |
			 (cfg->dst_canaddr << 0)
			 ); 

#endif
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->src2_endian, 15, 1);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->src2_color_map, 11, 4);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->src2_format, 8, 2);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->dst_endian, 23, 1);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->dst_color_map, 19, 4);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL2, cfg->dst_format, 16, 2);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->src2_mode_8b_sel, 15, 2);  
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->dst_mode_8b_sel, 24, 2); 

	/* only for m6 and later chips. */
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL3, cfg->dst2_pixel_byte_width, 16, 2);
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL3, cfg->dst2_color_map, 19, 4);
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL3, cfg->dst2_discard_mode, 10, 4);
	//WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL3, 1, 0, 1);
	WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL3, cfg->dst2_enable, 8, 1);
#endif
	WRITE_MPEG_REG(GE2D_SRC2_DEF_COLOR, cfg-> src2_def_color);
}

//####################################################################################################
// ge2d_set_src2_dst_gen
//####################################################################################################
void ge2d_set_src2_dst_gen (ge2d_src2_dst_gen_t *cfg)
{
   WRITE_MPEG_REG(GE2D_SRC2_CLIPX_START_END, 
                         (cfg->src2_clipx_start << 16) |
                         (cfg->src2_clipx_end << 0)
                         ); 

   WRITE_MPEG_REG(GE2D_SRC2_CLIPY_START_END, 
                         (cfg->src2_clipy_start << 16) |
                         (cfg->src2_clipy_end << 0)
                         ); 
                            
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->src2_pic_struct, 12, 2);  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, (cfg->src2_fill_mode & 0x1), 14, 1);  

   WRITE_MPEG_REG_BITS (GE2D_SRC_OUTSIDE_ALPHA, ((cfg->src2_fill_mode & 0x2) <<7) | 
                                        cfg->src2_outside_alpha, 16, 9);
   

   WRITE_MPEG_REG(GE2D_DST_CLIPX_START_END, 
                         (cfg->dst_clipx_start << 16) |
                         (cfg->dst_clipx_end << 0)
                         ); 

   WRITE_MPEG_REG(GE2D_DST_CLIPY_START_END, 
                         (cfg->dst_clipy_start << 16) |
                         (cfg->dst_clipy_end << 0)
                         ); 
  
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0, cfg->dst_clip_mode,  23, 1);                              
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->dst_pic_struct, 14, 2);   
}

//####################################################################################################
// ge2d_set_dp_gen
//####################################################################################################
void ge2d_set_dp_gen (ge2d_dp_gen_t *cfg)
{
    unsigned int antiflick_color_filter_n1[] = {0,   8,    16, 32};
    unsigned int antiflick_color_filter_n2[] = {128, 112,  96, 64};
    unsigned int antiflick_color_filter_n3[] = {0,   8,    16, 32};
    unsigned int antiflick_color_filter_th[] = {8, 16, 64};
    unsigned int antiflick_alpha_filter_n1[] = {0,    8,  16, 32};
    unsigned int antiflick_alpha_filter_n2[] = {128,112,  96, 64};
    unsigned int antiflick_alpha_filter_n3[] = {0,    8,  16, 32};
    unsigned int antiflick_alpha_filter_th[] = {8, 16, 64};        

    if( cfg->conv_matrix_en )
    {
    	cfg->antiflick_ycbcr_rgb_sel = 0;  //0: yuv2rgb 1:rgb2rgb
    	cfg->antiflick_cbcr_en = 1;
    	cfg->antiflick_r_coef = 0; 
    	cfg->antiflick_g_coef = 0;
    	cfg->antiflick_b_coef = 0;
     }else{
       	cfg->antiflick_ycbcr_rgb_sel = 1;  	//0: yuv2rgb 1:rgb2rgb
    	cfg->antiflick_cbcr_en = 1;
    	cfg->antiflick_r_coef = 0x42;    	//0.257 
    	cfg->antiflick_g_coef = 0x81;     	//0.504
    	cfg->antiflick_b_coef = 0x19;     	//0.098
     }   
    memcpy(cfg->antiflick_color_filter_n1, antiflick_color_filter_n1, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_color_filter_n2, antiflick_color_filter_n2, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_color_filter_n3, antiflick_color_filter_n3, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_color_filter_th, antiflick_color_filter_th, 3 * sizeof(unsigned int));
    memcpy(cfg->antiflick_alpha_filter_n1, antiflick_alpha_filter_n1, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_alpha_filter_n2, antiflick_alpha_filter_n2, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_alpha_filter_n3, antiflick_alpha_filter_n3, 4 * sizeof(unsigned int));
    memcpy(cfg->antiflick_alpha_filter_th, antiflick_alpha_filter_th, 3 * sizeof(unsigned int));    
   cfg->src1_vsc_bank_length = 4;
   cfg->src1_hsc_bank_length = 4;
   WRITE_MPEG_REG_BITS (GE2D_SC_MISC_CTRL, 
                         ((cfg->src1_hsc_rpt_ctrl << 9) |
                         (cfg->src1_vsc_rpt_ctrl << 8) |
                         (cfg->src1_vsc_phase0_always_en << 7) |
                         (cfg->src1_vsc_bank_length << 4) |
                         (cfg->src1_hsc_phase0_always_en << 3) |
                         (cfg->src1_hsc_bank_length << 0)),  0, 10); 

   WRITE_MPEG_REG_BITS(GE2D_SC_MISC_CTRL, ((cfg->src1_vsc_nearest_en <<1) |
                                  (cfg->src1_hsc_nearest_en << 0)), 29, 2);
  if (cfg->antiflick_en == 1) {
     //Wr(GE2D_ANTIFLICK_CTRL0, 0x81000100); 
     WRITE_MPEG_REG(GE2D_ANTIFLICK_CTRL0, 0x80000000); 
     WRITE_MPEG_REG(GE2D_ANTIFLICK_CTRL1, 
            (cfg->antiflick_ycbcr_rgb_sel << 25) |
            (cfg->antiflick_cbcr_en << 24) |
            ((cfg->antiflick_r_coef & 0xff) << 16) |
            ((cfg->antiflick_g_coef & 0xff) << 8) |
            ((cfg->antiflick_b_coef & 0xff) << 0)
            );

     WRITE_MPEG_REG (GE2D_ANTIFLICK_COLOR_FILT0, 
            ((cfg->antiflick_color_filter_th[0] & 0xff) << 24) | 
            ((cfg->antiflick_color_filter_n3[0] & 0xff) << 16) | 
            ((cfg->antiflick_color_filter_n2[0] & 0xff) << 8) | 
            ((cfg->antiflick_color_filter_n1[0] & 0xff) << 0)
            );
            
     WRITE_MPEG_REG (GE2D_ANTIFLICK_COLOR_FILT1, 
            ((cfg->antiflick_color_filter_th[1] & 0xff) << 24) | 
            ((cfg->antiflick_color_filter_n3[1] & 0xff) << 16) | 
            ((cfg->antiflick_color_filter_n2[1] & 0xff) << 8) | 
            ((cfg->antiflick_color_filter_n1[1] & 0xff) << 0)
            );

     WRITE_MPEG_REG (GE2D_ANTIFLICK_COLOR_FILT2, 
            ((cfg->antiflick_color_filter_th[2] & 0xff) << 24) | 
            ((cfg->antiflick_color_filter_n3[2] & 0xff) << 16) | 
            ((cfg->antiflick_color_filter_n2[2] & 0xff) << 8) | 
            ((cfg->antiflick_color_filter_n1[2] & 0xff) << 0)
            );

     WRITE_MPEG_REG (GE2D_ANTIFLICK_COLOR_FILT3, 
            ((cfg->antiflick_color_filter_n3[3] & 0xff) << 16) | 
            ((cfg->antiflick_color_filter_n2[3] & 0xff) << 8) | 
            ((cfg->antiflick_color_filter_n1[3] & 0xff) << 0)
            );


     WRITE_MPEG_REG (GE2D_ANTIFLICK_ALPHA_FILT0, 
            ((cfg->antiflick_alpha_filter_th[0] & 0xff) << 24) | 
            ((cfg->antiflick_alpha_filter_n3[0] & 0xff) << 16) | 
            ((cfg->antiflick_alpha_filter_n2[0] & 0xff) << 8) | 
            ((cfg->antiflick_alpha_filter_n1[0] & 0xff) << 0)
            );
            
     WRITE_MPEG_REG (GE2D_ANTIFLICK_ALPHA_FILT1, 
            ((cfg->antiflick_alpha_filter_th[1] & 0xff) << 24) | 
            ((cfg->antiflick_alpha_filter_n3[1] & 0xff) << 16) | 
            ((cfg->antiflick_alpha_filter_n2[1] & 0xff) << 8) | 
            ((cfg->antiflick_alpha_filter_n1[1] & 0xff) << 0)
            );

     WRITE_MPEG_REG (GE2D_ANTIFLICK_ALPHA_FILT2, 
            ((cfg->antiflick_alpha_filter_th[2] & 0xff) << 24) | 
            ((cfg->antiflick_alpha_filter_n3[2] & 0xff) << 16) | 
            ((cfg->antiflick_alpha_filter_n2[2] & 0xff) << 8) | 
            ((cfg->antiflick_alpha_filter_n1[2] & 0xff) << 0)
            );

     WRITE_MPEG_REG (GE2D_ANTIFLICK_ALPHA_FILT3, 
            ((cfg->antiflick_alpha_filter_n3[3] & 0xff) << 16) | 
            ((cfg->antiflick_alpha_filter_n2[3] & 0xff) << 8) | 
            ((cfg->antiflick_alpha_filter_n1[3] & 0xff) << 0)
            );
  }
  else
  {
     WRITE_MPEG_REG_BITS(GE2D_ANTIFLICK_CTRL0, 0, 31, 1);
  }
  if (cfg->use_matrix_default == MATRIX_YCC_TO_RGB) {
     //ycbcr(16-235) to rgb(0-255)
     cfg->matrix_coef[0] = 0x4a8;
     cfg->matrix_coef[1] = 0;
     cfg->matrix_coef[2] = 0x662;
     cfg->matrix_coef[3] = 0x4a8;
     cfg->matrix_coef[4] = 0x1e6f;
     cfg->matrix_coef[5] = 0x1cbf;
     cfg->matrix_coef[6] = 0x4a8;
     cfg->matrix_coef[7] = 0x811;
     cfg->matrix_coef[8] = 0x0;
     cfg->matrix_offset[0] = 0;
     cfg->matrix_offset[1] = 0;
     cfg->matrix_offset[2] = 0;
     cfg->matrix_sat_in_en = 1;
     cfg->matrix_minus_16_ctrl = 0x4;
     cfg->matrix_sign_ctrl = 0x3;
  }
  else if (cfg->use_matrix_default == MATRIX_RGB_TO_YCC)
  {
     //rgb(0-255) to ycbcr(16-235)
     //0.257     0.504   0.098
     //-0.148    -0.291  0.439
     //0.439     -0.368 -0.071
     cfg->matrix_coef[0] = 0x107;
     cfg->matrix_coef[1] = 0x204;
     cfg->matrix_coef[2] = 0x64;
     cfg->matrix_coef[3] = 0x1f68;
     cfg->matrix_coef[4] = 0x1ed6;
     cfg->matrix_coef[5] = 0x1c2;
     cfg->matrix_coef[6] = 0x1c2;
     cfg->matrix_coef[7] = 0x1e87;
     cfg->matrix_coef[8] = 0x1fb7;
     cfg->matrix_offset[0] = 16;
     cfg->matrix_offset[1] = 128;
     cfg->matrix_offset[2] = 128;
     cfg->matrix_sat_in_en = 0;
     cfg->matrix_minus_16_ctrl = 0;
     cfg->matrix_sign_ctrl = 0;
  }
  else if (cfg->use_matrix_default == MATRIX_FULL_RANGE_YCC_TO_RGB)
  {//ycbcr (0-255) to rgb(0-255)
   //1,     0,      1.402
   //1, -0.34414,   -0.71414
   //1, 1.772       0
     cfg->matrix_coef[0] = 0x400;
     cfg->matrix_coef[1] = 0;
     cfg->matrix_coef[2] = 0x59c;
     cfg->matrix_coef[3] = 0x400;
     cfg->matrix_coef[4] = 0x1ea0;
     cfg->matrix_coef[5] = 0x1d25;
     cfg->matrix_coef[6] = 0x400;
     cfg->matrix_coef[7] = 0x717;
     cfg->matrix_coef[8] = 0;
     cfg->matrix_offset[0] = 0;
     cfg->matrix_offset[1] = 0;
     cfg->matrix_offset[2] = 0;
     cfg->matrix_sat_in_en = 0;
     cfg->matrix_minus_16_ctrl = 0;
     cfg->matrix_sign_ctrl = 0x3;
  }
                         
	if (cfg->matrix_minus_16_ctrl) {
		WRITE_MPEG_REG_BITS(GE2D_MATRIX_PRE_OFFSET, 0x1f0, 20, 9);
	} else {
		WRITE_MPEG_REG_BITS(GE2D_MATRIX_PRE_OFFSET, 0, 20, 9);
	}
	
	if (cfg->matrix_sign_ctrl & 3) {
		WRITE_MPEG_REG_BITS(GE2D_MATRIX_PRE_OFFSET, ((0x180 << 10) | 0x180), 0, 20);
	} else {
		WRITE_MPEG_REG_BITS(GE2D_MATRIX_PRE_OFFSET, 0, 0, 20);
	}
   WRITE_MPEG_REG(GE2D_MATRIX_COEF00_01, 
                        (cfg->matrix_coef[0] << 16) | 
                        (cfg->matrix_coef[1] << 0)
                        ); 
                                               
   WRITE_MPEG_REG(GE2D_MATRIX_COEF02_10, 
                        (cfg->matrix_coef[2] << 16) | 
                        (cfg->matrix_coef[3] << 0)
                        ); 

   WRITE_MPEG_REG(GE2D_MATRIX_COEF11_12, 
                        (cfg->matrix_coef[4] << 16) | 
                        (cfg->matrix_coef[5] << 0)
                        ); 

   WRITE_MPEG_REG(GE2D_MATRIX_COEF20_21, 
                        (cfg->matrix_coef[6] << 16) | 
                        (cfg->matrix_coef[7] << 0)
                        ); 
                         
   WRITE_MPEG_REG(GE2D_MATRIX_COEF22_CTRL, 
                        (cfg->matrix_coef[8] << 16) | 
                        (cfg->matrix_sat_in_en << 7) |
#if 0
                        (cfg->matrix_minus_16_ctrl << 4) |
                        (cfg->matrix_sign_ctrl << 1) |
#endif
                        (cfg->conv_matrix_en << 0)
                        ); 

   WRITE_MPEG_REG(GE2D_MATRIX_OFFSET,
                       (cfg->matrix_offset[0] << 20) |  
                       (cfg->matrix_offset[1] << 10) |  
                       (cfg->matrix_offset[2] << 0)
                       );  

                            
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->src1_gb_alpha, 0, 8);
     
   WRITE_MPEG_REG(GE2D_ALU_CONST_COLOR, cfg->alu_const_color);  

   WRITE_MPEG_REG(GE2D_SRC1_KEY, cfg->src1_key);  
   WRITE_MPEG_REG(GE2D_SRC1_KEY_MASK, cfg->src1_key_mask);  

   WRITE_MPEG_REG(GE2D_SRC2_KEY, cfg->src2_key);  
   WRITE_MPEG_REG(GE2D_SRC2_KEY_MASK, cfg->src2_key_mask);  

   WRITE_MPEG_REG(GE2D_DST_BITMASK, cfg->bitmask);    
   
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL0,
                    ((cfg->bytemask_only << 5) |
                     (cfg->bitmask_en << 4) |
                     (cfg->src2_key_en << 3) |
                     (cfg->src2_key_mode << 2) |
                     (cfg->src1_key_en << 1) |
                     (cfg->src1_key_mode << 0)) , 26, 6); 
}

int ge2d_cmd_fifo_full(void)
{
    return READ_MPEG_REG(GE2D_STATUS0) & (1 << 1);
}

//####################################################################################################
// ge2d_set_cmd
//####################################################################################################
void ge2d_set_cmd (ge2d_cmd_t *cfg)
{
    unsigned int widthi, heighti, tmp_widthi, tmp_heighti, widtho, heighto;
    unsigned int multo;
    unsigned x_extra_bit_start = 0, x_extra_bit_end = 0;
    unsigned y_extra_bit_start = 0, y_extra_bit_end = 0;
    unsigned x_chr_phase = 0, y_chr_phase = 0;
    unsigned x_yc_ratio, y_yc_ratio;
    int sc_prehsc_en, sc_prevsc_en;
    unsigned int src1_y_end=cfg->src1_y_end+1; //expand src region with one line. 	
    
    while ((READ_MPEG_REG(GE2D_STATUS0) & (1 << 1))) {}
    
    x_yc_ratio = READ_MPEG_REG_BITS(GE2D_GEN_CTRL0, 11, 1);
    y_yc_ratio = READ_MPEG_REG_BITS(GE2D_GEN_CTRL0, 10, 1);

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6	
    if (x_yc_ratio) {
        if (cfg->src1_x_rev) {
            x_extra_bit_start = 0;
            x_extra_bit_end   = 3;
            x_chr_phase = 0x80;
        } else {
            x_extra_bit_start = 3;
            x_extra_bit_end   = 0;
            x_chr_phase = 0x08;
        }
    }
    
    if (y_yc_ratio) {
        if (cfg->src1_y_rev) {
            y_extra_bit_start = 2;
            y_extra_bit_end   = 3;
            y_chr_phase = 0xc4;
        } else {
            y_extra_bit_start = 3;
            y_extra_bit_end   = 2;
            y_chr_phase = 0x4c;
        }
    }
#else
    if (x_yc_ratio) {
        if( (cfg->src1_x_rev+cfg->dst_x_rev) == 1) {
            x_extra_bit_start = 3;
            x_extra_bit_end   = 2;
            x_chr_phase = 0x08;
        } else {
            x_extra_bit_start = 2;
            x_extra_bit_end   = 3;
            x_chr_phase = 0x08;
        }
    }

    if (y_yc_ratio) {
        if( (cfg->src1_y_rev+cfg->dst_y_rev) == 1) {
            y_extra_bit_start = 3;
            y_extra_bit_end   = 2;
            y_chr_phase = 0x4c;
        } else {
            y_extra_bit_start = 2;
            y_extra_bit_end   = 3;
            y_chr_phase = 0x4c;
        }
    }

#endif

    WRITE_MPEG_REG(GE2D_SRC1_X_START_END, 
                         (x_extra_bit_start << 30) |  //x start extra
                         ((cfg->src1_x_start & 0x3fff) << 16) | // Limit the range in case of the sign bit extending to the top
                         (x_extra_bit_end << 14) |    //x end extra
                         ((cfg->src1_x_end & 0x3fff) << 0) // Limit the range in case of the sign bit extending to the top
                         ); 

    WRITE_MPEG_REG(GE2D_SRC1_Y_START_END, 
                         (y_extra_bit_start << 30) |  //y start extra
                         ((cfg->src1_y_start & 0x3fff) << 16) | // Limit the range in case of the sign bit extending to the top
                         (y_extra_bit_end << 14) |    //y end extra
                         ((src1_y_end & 0x3fff) << 0) // Limit the range in case of the sign bit extending to the top
                         ); 

    WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, x_chr_phase, 8, 8); 
    WRITE_MPEG_REG_BITS (GE2D_SRC1_FMT_CTRL, y_chr_phase, 0, 8); 
 
 
    WRITE_MPEG_REG(GE2D_SRC2_X_START_END, 
                         (cfg->src2_x_start << 16) | 
                         (cfg->src2_x_end << 0)
                         ); 
 
    WRITE_MPEG_REG(GE2D_SRC2_Y_START_END, 
                         (cfg->src2_y_start << 16) | 
                         (cfg->src2_y_end << 0)
                         ); 
  
    WRITE_MPEG_REG(GE2D_DST_X_START_END, 
                         (cfg->dst_x_start << 16) | 
                         (cfg->dst_x_end << 0)
                         ); 
 
    WRITE_MPEG_REG(GE2D_DST_Y_START_END, 
                         (cfg->dst_y_start << 16) | 
                         (cfg->dst_y_end << 0)
                         );            
                         
    widthi  = cfg->src1_x_end - cfg->src1_x_start + 1;
    heighti = cfg->src1_y_end - cfg->src1_y_start + 1;
 
    widtho  = cfg->dst_xy_swap ? (cfg->dst_y_end - cfg->dst_y_start + 1): (cfg->dst_x_end - cfg->dst_x_start + 1);
    heighto = cfg->dst_xy_swap ? (cfg->dst_x_end - cfg->dst_x_start + 1): (cfg->dst_y_end - cfg->dst_y_start + 1);
 
	sc_prehsc_en = (widthi > widtho*2) ? 1 : 0;
	sc_prevsc_en = (heighti > heighto*2) ? 1 : 0;
    
    tmp_widthi  = sc_prehsc_en ? ((widthi+1) >>1): widthi; 
    tmp_heighti = sc_prevsc_en ? ((heighti+1) >>1): heighti; 
   
    if (cfg->hsc_phase_step == 0)
       cfg->hsc_phase_step = ((tmp_widthi << 18) / widtho) <<6;//width no more than 8192

    if (cfg->vsc_phase_step == 0)
       cfg->vsc_phase_step = ((tmp_heighti << 18) / heighto) << 6;//height no more than 8192
       
    if ((cfg->sc_hsc_en) && (cfg->hsc_div_en)) {
        cfg->hsc_div_length = (124 << 24) / cfg->hsc_phase_step;

        multo = cfg->hsc_phase_step * cfg->hsc_div_length;
        cfg->hsc_adv_num   = multo >> 24;
        cfg->hsc_adv_phase = multo & 0xffffff;
    }
       
    WRITE_MPEG_REG_BITS (GE2D_SC_MISC_CTRL, 
                        ((cfg->hsc_div_en << 17) |
                        (cfg->hsc_div_length << 4) |
                        (sc_prehsc_en << 3) |
                        (sc_prevsc_en << 2) |
                        (cfg->sc_vsc_en << 1) |
                        (cfg->sc_hsc_en << 0)), 11, 18);

    WRITE_MPEG_REG(GE2D_HSC_ADV_CTRL, 
                        (cfg->hsc_adv_num << 24) | 
                        (cfg->hsc_adv_phase << 0)
                        );
   
    WRITE_MPEG_REG(GE2D_HSC_START_PHASE_STEP, cfg->hsc_phase_step);

    WRITE_MPEG_REG(GE2D_HSC_PHASE_SLOPE, cfg->hsc_phase_slope);

    WRITE_MPEG_REG(GE2D_HSC_INI_CTRL, 
                        (cfg->hsc_rpt_p0_num << 29) | 
                        (cfg->hsc_ini_phase << 0)
                        );
                        

    WRITE_MPEG_REG(GE2D_VSC_START_PHASE_STEP, cfg->vsc_phase_step);

    WRITE_MPEG_REG(GE2D_VSC_PHASE_SLOPE, cfg->vsc_phase_slope);

    WRITE_MPEG_REG(GE2D_VSC_INI_CTRL, 
                        (cfg->vsc_rpt_l0_num << 29) | 
                        (cfg->vsc_ini_phase << 0)
                        );
                        
  
    WRITE_MPEG_REG(GE2D_ALU_OP_CTRL, 
                       (cfg->src1_cmult_asel << 25) |
                       (cfg->src2_cmult_asel << 24) |
                       (cfg->color_blend_mode << 20) |
                       (cfg->color_src_blend_factor << 16) |
                       (((cfg->color_blend_mode == 5) ? cfg->color_logic_op: cfg->color_dst_blend_factor) << 12) |
                       (cfg->alpha_blend_mode << 8) |
                       (cfg->alpha_src_blend_factor << 4) |
                       (((cfg->alpha_blend_mode == 5) ? cfg->alpha_logic_op: cfg->alpha_dst_blend_factor) << 0)
                       ); 
                       
    WRITE_MPEG_REG(GE2D_CMD_CTRL, 
                       (cfg->src2_fill_color_en << 9) |
                       (cfg->src1_fill_color_en << 8) |
                       (cfg->dst_xy_swap << 7) |
                       (cfg->dst_x_rev << 6) |
                       (cfg->dst_y_rev << 5) |
                       (cfg->src2_x_rev << 4) |
                       (cfg->src2_y_rev << 3) |
                       (cfg->src1_x_rev << 2) |
                       (cfg->src1_y_rev << 1) |
                       1  << 0                  //start cmd
                       );  
    cfg->release_flag |= START_FLAG;                                        
}
//####################################################################################################
// ge2d_wait_done
//####################################################################################################
void ge2d_wait_done (void)
{
    while (READ_MPEG_REG(GE2D_STATUS0) & 1) 
	{
		
	}    
}


//####################################################################################################
// ge2d_wait_done
//####################################################################################################
int ge2d_is_busy (void)
{
    return (READ_MPEG_REG(GE2D_STATUS0) & 1);   
}

//####################################################################################################
// ge2d_soft_rst
//####################################################################################################
void ge2d_soft_rst (void)
{
      WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, 1, 31, 1);
      WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, 0, 31, 1);
}

//####################################################################################################
// ge2d_set_gen
//####################################################################################################
void ge2d_set_gen (ge2d_gen_t *cfg)
{
   WRITE_MPEG_REG_BITS (GE2D_GEN_CTRL1, cfg->interrupt_ctrl, 24, 2);  
   
   WRITE_MPEG_REG(GE2D_DP_ONOFF_CTRL, 
                         (cfg->dp_onoff_mode << 31) |
                         (cfg->dp_on_cnt << 16) |
                         (cfg->vfmt_onoff_en << 15) |
                         (cfg->dp_off_cnt << 0)
                         ); 

}

