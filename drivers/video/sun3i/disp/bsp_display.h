
#ifndef __EBSP_DISPLAY_H__
#define __EBSP_DISPLAY_H__

#include "linux/kernel.h"
#include "linux/mm.h"
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h>   //wake_up_process()
#include <linux/kthread.h> //kthread_create()、kthread_run()
#include <linux/err.h> //IS_ERR()、PTR_ERR()
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cdev.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef __BSP_DRV_DISPLAY_H__
#define __BSP_DRV_DISPLAY_H__

typedef struct {__u8  alpha;__u8 red;__u8 green; __u8 blue; }__disp_color_t;
typedef struct {__s32 x; __s32 y; __u32 width; __u32 height;}__disp_rect_t;
typedef struct {__u32 width;__u32 height;                   }__disp_rectsz_t;
typedef struct {__s32 x; __s32 y;                           }__disp_pos_t;


typedef enum
{
    DISP_FORMAT_1BPP        =0x0,
    DISP_FORMAT_2BPP        =0x1,
    DISP_FORMAT_4BPP        =0x2,
    DISP_FORMAT_8BPP        =0x3,
    DISP_FORMAT_RGB655      =0x4,
    DISP_FORMAT_RGB565      =0x5,
    DISP_FORMAT_RGB556      =0x6,
    DISP_FORMAT_ARGB1555    =0x7,
    DISP_FORMAT_RGBA5551    =0x8,
    DISP_FORMAT_RGB888      =0x9,
    DISP_FORMAT_ARGB8888    =0xa,

    DISP_FORMAT_YUV444      =0xb,
    DISP_FORMAT_YUV422      =0xc,
    DISP_FORMAT_YUV420      =0xd,
    DISP_FORMAT_YUV411      =0xe,
    DISP_FORMAT_CSIRGB      =0xf,
}__disp_pixel_fmt_t;


typedef enum
{
    DISP_MOD_INTERLEAVED        =0x1,   //interleaved,1个地址
    DISP_MOD_NON_MB_PLANAR      =0x0,   //无宏块平面模式,3个地址,RGB/YUV每个channel分别存放
    DISP_MOD_NON_MB_UV_COMBINED =0x2,   //无宏块UV打包模式,2个地址,Y和UV分别存放
    DISP_MOD_MB_PLANAR          =0x4,   //宏块平面模式,3个地址,RGB/YUV每个channel分别存放
    DISP_MOD_MB_UV_COMBINED     =0x6,   //宏块UV打包模式 ,2个地址,Y和UV分别存放
}__disp_pixel_mod_t;

typedef enum
{
//for interleave argb8888
    DISP_SEQ_ARGB   =0x0,//A在高位
    DISP_SEQ_BGRA   =0x2,

//for nterleaved yuv422
    DISP_SEQ_UYVY   =0x3,
    DISP_SEQ_YUYV   =0x4,
    DISP_SEQ_VYUY   =0x5,
    DISP_SEQ_YVYU   =0x6,

//for interleaved yuv444
    DISP_SEQ_AYUV   =0x7,
    DISP_SEQ_VUYA   =0x8,

//for uv_combined yuv420
    DISP_SEQ_UVUV   =0x9,
    DISP_SEQ_VUVU   =0xa,

//for 16bpp rgb
    DISP_SEQ_P10    = 0xd,//p1在高位
    DISP_SEQ_P01    = 0xe,//p0在高位

//for planar format or 8bpp rgb
    DISP_SEQ_P3210  = 0xf,//p3在高位
    DISP_SEQ_P0123  = 0x10,//p0在高位

//for 4bpp rgb
    DISP_SEQ_P76543210  = 0x11,
    DISP_SEQ_P67452301  = 0x12,
    DISP_SEQ_P10325476  = 0x13,
    DISP_SEQ_P01234567  = 0x14,

//for 2bpp rgb
    DISP_SEQ_2BPP_BIG_BIG       = 0x15,//15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    DISP_SEQ_2BPP_BIG_LITTER    = 0x16,//12,13,14,15,8,9,10,11,4,5,6,7,0,1,2,3
    DISP_SEQ_2BPP_LITTER_BIG    = 0x17,//3,2,1,0,7,6,5,4,11,10,9,8,15,14,13,12
    DISP_SEQ_2BPP_LITTER_LITTER = 0x18,//0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15

//for 1bpp rgb
    DISP_SEQ_1BPP_BIG_BIG       = 0x19,//31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    DISP_SEQ_1BPP_BIG_LITTER    = 0x1a,//24,25,26,27,28,29,30,31,16,17,18,19,20,21,22,23,8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,7
    DISP_SEQ_1BPP_LITTER_BIG    = 0x1b,//7,6,5,4,3,2,1,0,15,14,13,12,11,10,9,8,23,22,21,20,19,18,17,16,31,30,29,28,27,26,25,24
    DISP_SEQ_1BPP_LITTER_LITTER = 0x1c,//0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
}__disp_pixel_seq_t;

typedef enum
{
    DISP_BT601  = 0,
    DISP_BT709  = 1,
    DISP_YCC    = 2,
    DISP_VXYCC  = 3,
}__disp_cs_mode_t;


typedef enum
{
    DISP_OUTPUT_TYPE_NONE   = 0,
    DISP_OUTPUT_TYPE_LCD    = 1,
    DISP_OUTPUT_TYPE_TV     = 2,
    DISP_OUTPUT_TYPE_HDMI   = 4,
    DISP_OUTPUT_TYPE_VGA    = 8,
}__disp_output_type_t;

typedef enum
{
    DISP_TV_NONE    = 0,
    DISP_TV_CVBS    = 1,
    DISP_TV_YPBPR   = 2,
    DISP_TV_SVIDEO  = 4,
}__disp_tv_output_t;

typedef enum
{
    DISP_TV_MOD_480I                = 0,
    DISP_TV_MOD_576I                = 1,
    DISP_TV_MOD_480P                = 2,
    DISP_TV_MOD_576P                = 3,
    DISP_TV_MOD_720P_50HZ           = 4,
    DISP_TV_MOD_720P_60HZ           = 5,
    DISP_TV_MOD_1080I_50HZ          = 6,
    DISP_TV_MOD_1080I_60HZ          = 7,
    DISP_TV_MOD_1080P_24HZ          = 8,
    DISP_TV_MOD_1080P_50HZ          = 9,
    DISP_TV_MOD_1080P_60HZ          = 0xa,
    DISP_TV_MOD_PAL                 = 0xb,
    DISP_TV_MOD_PAL_SVIDEO          = 0xc,
    DISP_TV_MOD_PAL_CVBS_SVIDEO     = 0xd,
    DISP_TV_MOD_NTSC                = 0xe,
    DISP_TV_MOD_NTSC_SVIDEO         = 0xf,
    DISP_TV_MOD_NTSC_CVBS_SVIDEO    = 0x10,
    DISP_TV_MOD_PAL_M               = 0x11,
    DISP_TV_MOD_PAL_M_SVIDEO        = 0x12,
    DISP_TV_MOD_PAL_M_CVBS_SVIDEO   = 0x13,
    DISP_TV_MOD_PAL_NC              = 0x14,
    DISP_TV_MOD_PAL_NC_SVIDEO       = 0x15,
    DISP_TV_MOD_PAL_NC_CVBS_SVIDEO  = 0x16,
}__disp_tv_mode_t;

typedef enum
{
    DISP_TV_DAC_SRC_COMPOSITE = 0,
    DISP_TV_DAC_SRC_LUMA = 1,
    DISP_TV_DAC_SRC_CHROMA = 2,
    DISP_TV_DAC_SRC_Y = 4,
    DISP_TV_DAC_SRC_PB = 5,
    DISP_TV_DAC_SRC_PR = 6,
    DISP_TV_DAC_SRC_NONE = 7,
}__disp_tv_dac_source;

typedef enum
{
    DISP_VGA_H1680_V1050    = 0,
    DISP_VGA_H1440_V900     = 1,
    DISP_VGA_H1360_V768     = 2,
    DISP_VGA_H1280_V1024    = 3,
    DISP_VGA_H1024_V768     = 4,
    DISP_VGA_H800_V600      = 5,
    DISP_VGA_H640_V480      = 6,
    DISP_VGA_H1440_V900_RB  = 7,//not support yet
    DISP_VGA_H1680_V1050_RB = 8,//not support yet
    DISP_VGA_H1920_V1080_RB = 9,
    DISP_VGA_H1920_V1080    = 0xa,
}__disp_vga_mode_t;


typedef enum
{
    DISP_LCDC_SRC_DE_CH1    = 0,
    DISP_LCDC_SRC_DE_CH2    = 1,
    DISP_LCDC_SRC_DMA       = 2,
    DISP_LCDC_SRC_WHITE     = 3,
    DISP_LCDC_SRC_BLACK     = 4,
    DISP_LCDC_SRC_BLUT      = 5,
}__disp_lcdc_src_t;


typedef enum
{
    DISP_LCD_BRIGHT_LEVEL0  = 0,
    DISP_LCD_BRIGHT_LEVEL1  = 1,
    DISP_LCD_BRIGHT_LEVEL2  = 2,
    DISP_LCD_BRIGHT_LEVEL3  = 3,
    DISP_LCD_BRIGHT_LEVEL4  = 4,
    DISP_LCD_BRIGHT_LEVEL5  = 5,
    DISP_LCD_BRIGHT_LEVEL6  = 6,
    DISP_LCD_BRIGHT_LEVEL7  = 7,
    DISP_LCD_BRIGHT_LEVEL8  = 8,
    DISP_LCD_BRIGHT_LEVEL9  = 9,
    DISP_LCD_BRIGHT_LEVEL10 = 0xa,
    DISP_LCD_BRIGHT_LEVEL11 = 0xb,
    DISP_LCD_BRIGHT_LEVEL12 = 0xc,
    DISP_LCD_BRIGHT_LEVEL13 = 0xd,
    DISP_LCD_BRIGHT_LEVEL14 = 0xe,
    DISP_LCD_BRIGHT_LEVEL15 = 0xf,
}__disp_lcd_bright_t;

typedef enum
{
    DISP_LAYER_WORK_MODE_NORMAL     = 0,    //normal work mode
    DISP_LAYER_WORK_MODE_PALETTE    = 1,    //palette work mode
    DISP_LAYER_WORK_MODE_INTER_BUF  = 2,    //internal frame buffer work mode
    DISP_LAYER_WORK_MODE_GAMMA      = 3,    //gamma correction work mode
    DISP_LAYER_WORK_MODE_SCALER     = 4,    //scaler work mode
}__disp_layer_work_mode_t;


typedef enum
{
    DISP_VIDEO_NATUAL       = 0,
    DISP_VIDEO_SOFT         = 1,
    DISP_VIDEO_VERYSOFT     = 2,
    DISP_VIDEO_SHARP        = 3,
    DISP_VIDEO_VERYSHARP    = 4
}__disp_video_smooth_t;

typedef enum
{
    DISP_HWC_MOD_H32_V32_8BPP = 0,
    DISP_HWC_MOD_H64_V64_2BPP = 1,
    DISP_HWC_MOD_H64_V32_4BPP = 2,
    DISP_HWC_MOD_H32_V64_4BPP = 3,
}__disp_hwc_mode_t;

typedef enum
{
    DISP_EXIT_MODE_CLEAN_ALL    = 0,
    DISP_EXIT_MODE_CLEAN_PARTLY = 1,//only clean interrupt temply
}__disp_exit_mode_t;

typedef struct
{
    __u32               addr[3];    // frame buffer的内容地址，对于rgb类型，只有addr[0]有效
    __disp_rectsz_t     size;//单位是pixel
    __disp_pixel_fmt_t  format;
    __disp_pixel_seq_t  seq;
    __disp_pixel_mod_t  mode;
    __bool              br_swap;    // blue red color swap flag, FALSE:RGB; TRUE:BGR,only used in rgb format
    __disp_cs_mode_t    cs_mode;    //color space
}__disp_fb_t;

typedef struct
{
    __disp_layer_work_mode_t    mode;       //layer work mode
    __u8                        pipe;       //layer pipe,0/1,if in scaler mode, scaler0 must be pipe0, scaler1 must be pipe1
    __u8                        prio;       //layer priority,can get layer prio,but never set layer prio,从顶至顶,优先级由低至高
    __bool                      alpha_en;   //layer global alpha enable
    __u16                       alpha_val;  //layer global alpha value
    __bool                      ck_enable;  //layer color key enable
    __disp_rect_t               src_win;    // framebuffer source window,only care x,y if is not scaler mode
    __disp_rect_t               scn_win;    // screen window
    __disp_fb_t                 fb;         //framebuffer
}__disp_layer_info_t;



typedef struct
{
    __disp_color_t   ck_max;
    __disp_color_t   ck_min;
    __u32             red_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
    __u32             green_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
    __u32             blue_match_rule;//0/1:always match; 2:match if min<=color<=max; 3:match if color>max or color<min
}__disp_colorkey_t;


typedef struct
{
    __s32   id;
    __u32   addr[3];
    __bool  interlace;
    __bool  top_field_first;
    __u32   frame_rate; // *FRAME_RATE_BASE(现在定为1000)
    __u32   flag_addr;//dit maf flag address
    __u32   flag_stride;//dit maf flag line stride
    __bool  maf_valid;
    __bool  pre_frame_valid;
}__disp_video_fb_t;

typedef struct
{
    __bool maf_enable;
    __bool pre_frame_enable;
}__disp_dit_info_t;

typedef struct
{
    __disp_hwc_mode_t     pat_mode;
    __u32                 addr;
}__disp_hwc_pattern_t;

typedef struct
{
    __disp_fb_t     input_fb;
    __disp_rect_t   source_regn;
    __disp_fb_t     output_fb;
    //__disp_rect_t   out_regn;
}__disp_scaler_para_t;


typedef struct
{
    __disp_fb_t       fb;
    __disp_rect_t   src_win;//source region,only care x,y because of not scaler
    __disp_rect_t   scn_win;// sceen region
}__disp_sprite_block_para_t;

#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef void (*LCD_FUNC) (__u32 sel);
typedef struct lcd_function
{
    LCD_FUNC func;
    __u32 delay;//ms
}__lcd_function_t;

typedef struct lcd_flow
{
    __lcd_function_t func[5];
    __u32 func_num;
}__lcd_flow_t;


typedef struct
{
	__u32 base_image0;
	__u32 base_image1;
	__u32 base_scaler0;
	__u32 base_scaler1;
	__u32 base_lcdc0;
	__u32 base_lcdc1;
	__u32 base_tvec;
	__u32 base_pioc;
	__u32 base_sdram;
	__u32 base_ccmu;

	__s32 (*scaler_begin) (__u32 sel);
	void (*scaler_finish) (__u32 sel);
	void (*tve_interrup) (__u32 sel);
	__s32 (*hdmi_set_mode)(__disp_tv_mode_t mode);
	__s32 (*Hdmi_open)(void);
	__s32 (*Hdmi_close)(void);
	__s32 (*hdmi_mode_support)(__u8 mode);
	__s32 (*hdmi_get_HPD_status)(void);
	__s32 (*disp_int_process)(__u32 sel);
}__disp_bsp_init_para;

typedef struct
{
	__u32   tcon_index; //0:tcon0, 1:tcon1

	__u8    lcd_if;
	__u8    lcd_swap;
	__u16   lcd_x;
	__u16   lcd_y;
	__u16   lcd_dclk_freq;

	__u8    lcd_uf;
	__u16   lcd_vt;
	__u16   lcd_ht;
	__u16   lcd_vbp;
	__u16   lcd_hbp;

	__u8    lcd_hv_if;
	__u8    lcd_hv_smode;
	__u8    lcd_hv_s888_if;
	__u8    lcd_hv_syuv_if;
	__u8    lcd_hv_vspw;
	__u16   lcd_hv_hspw;

	__u8    lcd_hv_lde_used;
	__u8    lcd_hv_lde_iovalue;

	__u32   lcd_ttl_stvh;
	__u32   lcd_ttl_stvdl;
	__u32   lcd_ttl_stvdp;

	__u32   lcd_ttl_ckvt;
	__u32   lcd_ttl_ckvh;
	__u32   lcd_ttl_ckvd;

	__u32   lcd_ttl_oevt;
	__u32   lcd_ttl_oevh;
	__u32   lcd_ttl_oevd;

	__u32   lcd_ttl_sthh;
	__u32   lcd_ttl_sthd;
	__u32   lcd_ttl_oehh;
	__u32   lcd_ttl_oehd;

	__u32   lcd_ttl_revd;

	__u32   lcd_ttl_datarate;
	__u32   lcd_ttl_revsel;
	__u32   lcd_ttl_datainv_en;
	__u32   lcd_ttl_datainv_sel;
	__u8    lcd_cpu_if;
	__u8    lcd_cpu_da;

	__u32   lcd_io_cfg0;
	__u32   lcd_io_cfg1;

	__u32   lcd_srgb;
	__u32   lcd_io_strength;

	__u32   lcd_pwm_freq;
	__u32   lcd_pwm_pol;

	__u32   start_delay;
}__panel_para_t;

typedef struct
{
    void (*cfg_panel_info)(__panel_para_t * info);
    __s32 (*cfg_open_flow)(__u32 sel);
    __s32 (*cfg_close_flow)(__u32 sel);
}__lcd_panel_fun_t;

typedef struct
{
	__u32 base_lcdc0;
	__u32 base_lcdc1;
	__u32 base_pioc;
	__u32 base_ccmu;
}__lcd_panel_init_para_t;

extern __s32 BSP_disp_clk_on(void);
extern __s32 BSP_disp_clk_off(void);
extern __s32 BSP_disp_init(__disp_bsp_init_para * para);
extern __s32 BSP_disp_exit(__u32 mode);
extern __s32 BSP_disp_open(void);
extern __s32 BSP_disp_close(void);
extern __s32 BSP_disp_cmd_cache(__u32 sel);
extern __s32 BSP_disp_cmd_submit(__u32 sel);
extern __s32 BSP_disp_set_bk_color(__u32 sel, __disp_color_t *color);
extern __s32 BSP_disp_get_bk_color(__u32 sel, __disp_color_t *color);
extern __s32 BSP_disp_set_color_key(__u32 sel, __disp_colorkey_t *ck_mode);
extern __s32 BSP_disp_get_color_key(__u32 sel, __disp_colorkey_t *ck_mode);
extern __s32 BSP_disp_set_palette_table(__u32 sel, __u32 *pbuffer, __u32 offset, __u32 size);
extern __s32 BSP_disp_get_palette_table(__u32 sel, __u32 * pbuffer, __u32 offset,__u32 size);
extern __s32 BSP_disp_get_screen_height(__u32 sel);
extern __s32 BSP_disp_get_screen_width(__u32 sel);
extern __s32 BSP_disp_get_output_type(__u32 sel);
extern __s32 BSP_disp_gamma_correction_enable(__u32 sel);
extern __s32 BSP_disp_gamma_correction_disable(__u32 sel);
extern __s32 BSP_disp_set_bright(__u32 sel, __u32 bright);
extern __s32 BSP_disp_get_bright(__u32 sel);
extern __s32 BSP_disp_set_contrast(__u32 sel, __u32 contrast);
extern __s32 BSP_disp_get_contrast(__u32 sel);
extern __s32 BSP_disp_set_saturation(__u32 sel, __u32 saturation);
extern __s32 BSP_disp_get_saturation(__u32 sel);
extern __s32 BSP_disp_enhance_enable(__u32 sel, __bool enable);
extern __s32 BSP_disp_get_enhance_enable(__u32 sel);

extern __s32 BSP_disp_layer_request(__u32 sel, __disp_layer_work_mode_t mode);
extern __s32 BSP_disp_layer_release(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_open(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_close(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_framebuffer(__u32 sel, __u32 hid,__disp_fb_t *fbinfo);
extern __s32 BSP_disp_layer_get_framebuffer(__u32 sel, __u32 hid,__disp_fb_t*fbinfo);
extern __s32 BSP_disp_layer_set_src_window(__u32 sel, __u32 hid,__disp_rect_t *regn);
extern __s32 BSP_disp_layer_get_src_window(__u32 sel, __u32 hid,__disp_rect_t *regn);
extern __s32 BSP_disp_layer_set_screen_window(__u32 sel, __u32 hid,__disp_rect_t* regn);
extern __s32 BSP_disp_layer_get_screen_window(__u32 sel, __u32 hid,__disp_rect_t *regn);
extern __s32 BSP_disp_layer_set_para(__u32 sel, __u32 hid, __disp_layer_info_t * layer_para);
extern __s32 BSP_disp_layer_get_para(__u32 sel, __u32 hid, __disp_layer_info_t * layer_para);
extern __s32 BSP_disp_layer_set_top(__u32 sel, __u32  handle);
extern __s32 BSP_disp_layer_set_bottom(__u32 sel, __u32  handle);
extern __s32 BSP_disp_layer_set_alpha_value(__u32 sel, __u32 hid,__u8 value);
extern __s32 BSP_disp_layer_get_alpha_value(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_alpha_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_alpha_enable(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_pipe(__u32 sel, __u32 hid,__u8 pipe);
extern __s32 BSP_disp_layer_get_pipe(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_piro(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_colorkey_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_colorkey_enable(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_smooth(__u32 sel, __u32 hid, __disp_video_smooth_t  mode);
extern __s32 BSP_disp_layer_get_smooth(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_set_bright(__u32 sel, __u32 hid, __u32 bright);
extern __s32 BSP_disp_layer_set_contrast(__u32 sel, __u32 hid, __u32 contrast);
extern __s32 BSP_disp_layer_set_saturation(__u32 sel, __u32 hid, __u32 saturation);
extern __s32 BSP_disp_layer_set_hue(__u32 sel, __u32 hid, __u32 hue);
extern __s32 BSP_disp_layer_enhance_enable(__u32 sel, __u32 hid, __bool enable);
extern __s32 BSP_disp_layer_get_bright(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_contrast(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_saturation(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_hue(__u32 sel, __u32 hid);
extern __s32 BSP_disp_layer_get_enhance_enable(__u32 sel, __u32 hid);

extern __s32 BSP_disp_scaler_get_smooth(__u32 sel);
extern __s32 BSP_disp_scaler_set_smooth(__u32 sel, __disp_video_smooth_t  mode);
extern __s32 BSP_disp_scaler_request(void);
extern __s32 BSP_disp_scaler_release(__u32 handle);
extern __s32 BSP_disp_scaler_start(__u32 handle,__disp_scaler_para_t *scl);

extern __s32 BSP_disp_hwc_enable(__u32 sel, __bool enable);
extern __s32 BSP_disp_hwc_set_pos(__u32 sel, __disp_pos_t *pos);
extern __s32 BSP_disp_hwc_get_pos(__u32 sel, __disp_pos_t *pos);
extern __s32 BSP_disp_hwc_set_framebuffer(__u32 sel, __disp_hwc_pattern_t *patmem);
extern __s32 BSP_disp_hwc_set_palette(__u32 sel, void *palette,__u32 offset, __u32 palette_size);

extern __s32 BSP_disp_video_set_fb(__u32 sel, __u32 hid, __disp_video_fb_t *in_addr);
extern __s32 BSP_disp_video_get_frame_id(__u32 sel, __u32 hid);
extern __s32 BSP_disp_video_get_dit_info(__u32 sel, __u32 hid, __disp_dit_info_t * dit_info);
extern __s32 BSP_disp_video_start(__u32 sel, __u32 hid);
extern __s32 BSP_disp_video_stop(__u32 sel, __u32 hid);

extern __s32 BSP_disp_lcd_open_before(__u32 sel);
extern __s32 BSP_disp_lcd_open_after(__u32 sel);
extern __lcd_flow_t * BSP_disp_lcd_get_open_flow(__u32 sel);
extern __s32 BSP_disp_lcd_close_befor(__u32 sel);
extern __s32 BSP_disp_lcd_close_after(__u32 sel);
extern __lcd_flow_t * BSP_disp_lcd_get_close_flow(__u32 sel);
extern __s32 BSP_disp_lcd_xy_switch(__u32 sel, __s32 mode);
extern __s32 BSP_disp_set_gamma_table(__u32 sel, __u32 *gamtbl_addr,__u32 gamtbl_size);
extern __s32 BSP_disp_lcd_set_bright(__u32 sel, __disp_lcd_bright_t  bright);
extern __s32 BSP_disp_lcd_get_bright(__u32 sel);
extern __s32 BSP_disp_lcd_set_src(__u32 sel, __disp_lcdc_src_t src);

extern __s32 BSP_disp_tv_open(__u32 sel);
extern __s32 BSP_disp_tv_close(__u32 sel);
extern __s32 BSP_disp_tv_set_mode(__u32 sel, __disp_tv_mode_t tv_mod);
extern __s32 BSP_disp_tv_get_mode(__u32 sel);
extern __s32 BSP_disp_tv_get_interface(__u32 sel);
extern __s32 BSP_disp_tv_auto_check_enable(__u32 sel);
extern __s32 BSP_disp_tv_auto_check_disable(__u32 sel);
extern __s32 BSP_disp_tv_set_src(__u32 sel, __disp_lcdc_src_t src);
extern __s32 BSP_disp_tv_get_dac_status(__u32 sel, __u32 index);
extern __s32 BSP_disp_tv_set_dac_source(__u32 sel, __u32 index, __disp_tv_dac_source source);
extern __s32 BSP_disp_tv_get_dac_source(__u32 sel, __u32 index);

extern __s32 BSP_disp_hdmi_open(__u32 sel);
extern __s32 BSP_disp_hdmi_close(__u32 sel);
extern __s32 BSP_disp_hdmi_set_mode(__u32 sel, __disp_tv_mode_t  mode);
extern __s32 BSP_disp_hdmi_get_mode(__u32 sel);
extern __s32 BSP_disp_hdmi_check_support_mode(__u32 sel, __u8  mode);
extern __s32 BSP_disp_hdmi_get_hpd_status(__u32 sel);
extern __s32 BSP_disp_hdmi_set_src(__u32 sel, __disp_lcdc_src_t src);

extern __s32 BSP_disp_vga_open(__u32 sel);
extern __s32 BSP_disp_vga_close(__u32 sel);
extern __s32 BSP_disp_vga_set_mode(__u32 sel, __disp_vga_mode_t  mode);
extern __s32 BSP_disp_vga_get_mode(__u32 sel);
extern __s32 BSP_disp_vga_set_src(__u32 sel, __disp_lcdc_src_t src);

extern __s32 BSP_disp_sprite_init(__u32 sel);
extern __s32 BSP_disp_sprite_exit(__u32 sel);
extern __s32 BSP_disp_sprite_open(__u32 sel);
extern __s32 BSP_disp_sprite_close(__u32 sel);
extern __s32 BSP_disp_sprite_alpha_enable(__u32 sel);
extern __s32 BSP_disp_sprite_alpha_disable(__u32 sel);
extern __s32 BSP_disp_sprite_get_alpha_enable(__u32 sel);
extern __s32 BSP_disp_sprite_set_alpha_vale(__u32 sel, __u32 alpha);
extern __s32 BSP_disp_sprite_get_alpha_value(__u32 sel);
extern __s32 BSP_disp_sprite_set_format(__u32 sel, __disp_pixel_fmt_t format, __disp_pixel_seq_t pixel_seq);
extern __s32 BSP_disp_sprite_set_palette_table(__u32 sel, __u32 *buffer, __u32 offset, __u32 size);
extern __s32 BSP_disp_sprite_set_order(__u32 sel, __s32 hid,__s32 dst_hid);
extern __s32 BSP_disp_sprite_get_top_block(__u32 sel);
extern __s32 BSP_disp_sprite_get_bottom_block(__u32 sel);
extern __s32 BSP_disp_sprite_get_block_number(__u32 sel);
extern __s32 BSP_disp_sprite_block_request(__u32 sel, __disp_sprite_block_para_t *para);
extern __s32 BSP_disp_sprite_block_release(__u32 sel, __s32 hid);
extern __s32 BSP_disp_sprite_block_set_screen_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win);
extern __s32 BSP_disp_sprite_block_get_srceen_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win);
extern __s32 BSP_disp_sprite_block_set_src_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win);
extern __s32 BSP_disp_sprite_block_get_src_win(__u32 sel, __s32 hid, __disp_rect_t * scn_win);
extern __s32 BSP_disp_sprite_block_set_framebuffer(__u32 sel, __s32 hid, __disp_fb_t * fb);
extern __s32 BSP_disp_sprite_block_get_framebufer(__u32 sel, __s32 hid,__disp_fb_t *fb);
extern __s32 BSP_disp_sprite_block_set_top(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_set_bottom(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_pre_block(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_next_block(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_get_prio(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_open(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_close(__u32 sel, __u32 hid);
extern __s32 BSP_disp_sprite_block_set_para(__u32 sel, __u32 hid,__disp_sprite_block_para_t *para);
extern __s32 BSP_disp_sprite_block_get_para(__u32 sel, __u32 hid,__disp_sprite_block_para_t *para);

#endif
