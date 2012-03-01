#ifndef __RGA_API_H__
#define __RGA_API_H__

#include "rga_reg_info.h"
#include "rga.h"

#define ENABLE      1
#define DISABLE     0

#if 0
int
RGA_set_src_act_info(
		msg_t *msg,
		unsigned int   width,       /* act width  */
		unsigned int   height,      /* act height */
		unsigned int   x_off,       /* x_off      */
		unsigned int   y_off        /* y_off      */
		);

int
RGA_set_src_vir_info(
		msg_t *msg,
		unsigned int   yrgb_addr,       /* yrgb_addr  */
		unsigned int   uv_addr,         /* uv_addr    */
		unsigned int   v_addr,          /* v_addr     */
		unsigned int   vir_w,           /* vir width  */
		unsigned int   vir_h,           /* vir height */
		unsigned char  format,          /* format     */
		unsigned char  a_swap_en
		);

int
RGA_set_dst_act_info(
		msg_t *msg,
		unsigned int   width,       /* act width  */
		unsigned int   height,      /* act height */
		unsigned int   x_off,       /* x_off      */
		unsigned int   y_off        /* y_off      */
		);

int
RGA_set_dst_vir_info(
		msg_t *msg,
		unsigned int   yrgb_addr,   /* yrgb_addr  */
		unsigned int   uv_addr,     /* uv_addr    */
		unsigned int   v_addr,      /* v_addr     */
		unsigned int   vir_w,       /* vir width  */
		unsigned int   vir_h,       /* vir height */
		RECT           clip,        /* clip window*/
		unsigned char  format,      /* format     */
		unsigned char  a_swap_en );



int
RGA_set_rop_mask_info(
    msg_t *msg, 
    u32 rop_mask_addr, 
    u32 rop_mask_endian_mode);

int 
RGA_set_pat_info(
    msg_t *msg,
    u32 width,
    u32 height,
    u32 x_off,
    u32 y_off,
    u32 pat_format);    

int
RGA_set_alpha_en_info(
		msg_t *msg,
		unsigned int alpha_cal_mode,
		unsigned int alpha_mode,
		unsigned int global_a_value,
		unsigned int PD_en,
		unsigned int PD_mode
		);

int
RGA_set_rop_en_info(
		msg_t *msg,
		unsigned int ROP_mode,
		unsigned int ROP_code,
		unsigned int color_mode,
		unsigned int solid_color);

/*
int
RGA_set_MMU_info(
		MSG *msg,
		unsigned int base_addr,
		unsigned int src_flush,
		unsigned int dst_flush,
		unsigned int cmd_flush,
		unsigned int page_size
		);
*/

int
RGA_set_MMU_info(
		msg_t *msg,
		u8  mmu_en,
		u8  src_flush,
		u8  dst_flush,
		u8  cmd_flush,
		u32 base_addr,
		u8  page_size);


int
RGA_set_bitblt_mode(
		msg_t *msg,
		unsigned char scale_mode,    // 0/near  1/bilnear  2/bicubic  
		unsigned char rotate_mode,   // 0/copy 1/rotate_scale 2/x_mirror 3/y_mirror 
		unsigned int  angle,         // rotate angle (0~359)    
		unsigned int  dither_en,     // dither en flag   
		unsigned int  AA_en,         // AA flag          
		unsigned int  yuv2rgb_mode
		);


int
RGA_set_color_palette_mode(
		msg_t *msg,
		u8  palette_mode,    /* 1bpp/2bpp/4bpp/8bpp */
		u8  endian_mode,     /* src endian mode sel */
		u32 bpp1_0_color,    /* BPP1 = 0 */
		u32 bpp1_1_color     /* BPP1 = 1 */
		);



int
RGA_set_color_fill_mode(
		msg_t *msg,
		CF_GR_COLOR  gr_color,      /* gradient color part*/
		u8 gr_satur_mode,           /* saturation mode */
		u8 cf_mode,                 /* patten fill or solid fill */
		u32 color,                  /* solid color */
		u16 pat_width,              /* pat_width */
		u16 pat_height,             /* pat_height */
		u8 pat_x_off,               /* patten x offset */
		u8 pat_y_off,               /* patten y offset */
		u8 aa_en                    /* alpha en */
		);


int
RGA_set_line_point_drawing_mode(
		msg_t *msg,
		POINT sp,                   /* start point */
		POINT ep,                   /* end   point */
		unsigned int color,         /* line point drawing color */
		unsigned int line_width,    /* line width */
		unsigned char AA_en,        /* AA en */
		unsigned char last_point_en /* last point en */
		);



int
RGA_set_blur_sharp_filter_mode(
		msg_t *msg,
		unsigned char filter_mode,   /* blur/sharpness   */
		unsigned char filter_type,   /* filter intensity */
		unsigned char dither_en      /* dither_en flag   */
		);

int
RGA_set_pre_scaling_mode(
		msg_t *msg,
		unsigned char dither_en
		);


int
RGA_update_palette_table_mode(
		msg_t *msg,
		unsigned int LUT_addr,      /* LUT table addr      */
		unsigned int palette_mode   /* 1bpp/2bpp/4bpp/8bpp */
		);


int
RGA_set_update_patten_buff_mode(
		msg_t *msg,
		unsigned int pat_addr, /* patten addr    */
		unsigned int w,        /* patten width   */
		unsigned int h,        /* patten height  */
		unsigned int format    /* patten format  */
		);
/*
int
RGA_set_MMU_info(
		MSG *msg,
		unsigned int base_addr,
		unsigned int src_flush,
		unsigned int dst_flush,
		unsigned int cmd_flush,
		unsigned int page_size
		);
*/

int
RGA_set_mmu_info(
		msg_t *msg,
		u8  mmu_en,
		u8  src_flush,
		u8  dst_flush,
		u8  cmd_flush,
		u32 base_addr,
		u8  page_size);

msg_t * RGA_init_msg(void);
int RGA_free_msg(msg_t *msg);
void matrix_cal(msg_t *msg, TILE_INFO *tile);
unsigned char * RGA_set_reg_info(msg_t *msg, u8 *base);
void RGA_set_cmd_info(u8 cmd_mode, u32 cmd_addr);
void RGA_start(void);
void RGA_soft_reset(void);
#endif

uint32_t RGA_gen_two_pro(struct rga_req *msg, struct rga_req *msg1);


#endif
