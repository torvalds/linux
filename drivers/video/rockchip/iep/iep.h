#ifndef _IEP_H_
#define _IEP_H_

#define IEP_IOC_MAGIC 'i'

#define IEP_SET_PARAMETER_REQ				_IOW(IEP_IOC_MAGIC, 1, unsigned long)
#define IEP_SET_PARAMETER_DEINTERLACE			_IOW(IEP_IOC_MAGIC, 2, unsigned long)
#define IEP_SET_PARAMETER_ENHANCE			_IOW(IEP_IOC_MAGIC, 3, unsigned long)
#define IEP_SET_PARAMETER_CONVERT			_IOW(IEP_IOC_MAGIC, 4, unsigned long)
#define IEP_SET_PARAMETER_SCALE				_IOW(IEP_IOC_MAGIC, 5, unsigned long)
#define IEP_GET_RESULT_SYNC				_IOW(IEP_IOC_MAGIC, 6, unsigned long)
#define IEP_GET_RESULT_ASYNC				_IOW(IEP_IOC_MAGIC, 7, unsigned long)
#define IEP_SET_PARAMETER				_IOW(IEP_IOC_MAGIC, 8, unsigned long)
#define IEP_RELEASE_CURRENT_TASK			_IOW(IEP_IOC_MAGIC, 9, unsigned long)

/* Driver information */
#define DRIVER_DESC		"IEP Device Driver"
#define DRIVER_NAME		"iep"

/* Logging */
#define IEP_DEBUG 0
#if IEP_DEBUG
#define IEP_DBG(format, args...)	printk("%s: " format, DRIVER_NAME, ## args)
#else
#define IEP_DBG(format, args...)
#endif

#define IEP_INFORMATION 1
#if IEP_INFORMATION
#define IEP_INFO(format, args...)	printk(format, ## args)
#else
#define IEP_INFO(format, args...)
#endif

#define IEP_ERR(format, args...)	printk(KERN_ERR "%s: " format, DRIVER_NAME, ## args)
#define IEP_WARNING(format, args...)	printk(KERN_WARNING "%s: " format, DRIVER_NAME, ## args)

enum {
	yuv2rgb_BT_601_l = 0x0,     /* BT.601_1 */
	yuv2rgb_BT_601_f = 0x1,     /* BT.601_f */
	yuv2rgb_BT_709_l = 0x2,     /* BT.709_1 */
	yuv2rgb_BT_709_f = 0x3,     /* BT.709_f */
};

enum {
	rgb2yuv_BT_601_l = 0x0,     /* BT.601_1 */
	rgb2yuv_BT_601_f = 0x1,     /* BT.601_f */
	rgb2yuv_BT_709_l = 0x2,     /* BT.709_1 */
	rgb2yuv_BT_709_f = 0x3,     /* BT.709_f */
};

enum {
	dein_mode_bypass_dis         = 0x0,
	dein_mode_I4O2               = 0x1,
	dein_mode_I4O1B              = 0x2,
	dein_mode_I4O1T              = 0x3,
	dein_mode_I2O1B              = 0x4,
	dein_mode_I2O1T              = 0x5,
	dein_mode_bypass             = 0x6,
};

enum IEP_FIELD_ORDER {
	FIELD_ORDER_TOP_FIRST,
	FIELD_ORDER_BOTTOM_FIRST
};

enum IEP_YUV_DEINTERLACE_MODE {
	IEP_DEINTERLACE_MODE_DISABLE,
	IEP_DEINTERLACE_MODE_I2O1,
	IEP_DEINTERLACE_MODE_I4O1,
	IEP_DEINTERLACE_MODE_I4O2,
	IEP_DEINTERLACE_MODE_BYPASS
};

enum {
	rgb_enhance_bypass          = 0x0,
	rgb_enhance_denoise         = 0x1,
	rgb_enhance_detail          = 0x2,
	rgb_enhance_edge            = 0x3,
};/* for rgb_enhance_mode */

enum {
	rgb_contrast_CC_P_DDE          = 0x0, /* cg prior to dde */
	rgb_contrast_DDE_P_CC          = 0x1, /* dde prior to cg */
}; /* for rgb_contrast_enhance_mode */

enum {
	black_screen                   = 0x0,
	blue_screen                    = 0x1,
	color_bar                      = 0x2,
	normal_mode                    = 0x3,
}; /* for video mode */

/*
          Alpha    Red     Green   Blue  
{  4, 32, {{32,24,   24,16,  16, 8,  8, 0 }}, GGL_RGBA },    IEP_FORMAT_ARGB_8888
{  4, 32, {{32,24,   8, 0,  16, 8,  24,16 }}, GGL_RGB  },    IEP_FORMAT_ABGR_8888
{  4, 32, {{ 8, 0,  32,24,  24,16,  16, 8 }}, GGL_RGB  },    IEP_FORMAT_RGBA_8888
{  4, 32, {{ 8, 0,  16, 8,  24,16,  32,24 }}, GGL_BGRA },    IEP_FORMAT_BGRA_8888
{  2, 16, {{ 0, 0,  16,11,  11, 5,   5, 0 }}, GGL_RGB  },    IEP_FORMAT_RGB_565
{  2, 16, {{ 0, 0,   5, 0,  11, 5,  16,11 }}, GGL_RGB  },    IEP_FORMAT_RGB_565
*/
enum {
	IEP_FORMAT_ARGB_8888    = 0x0,
	IEP_FORMAT_ABGR_8888    = 0x1,
	IEP_FORMAT_RGBA_8888    = 0x2,
	IEP_FORMAT_BGRA_8888    = 0x3,
	IEP_FORMAT_RGB_565      = 0x4,
	IEP_FORMAT_BGR_565      = 0x5,

	IEP_FORMAT_YCbCr_422_SP = 0x10,
	IEP_FORMAT_YCbCr_422_P  = 0x11,
	IEP_FORMAT_YCbCr_420_SP = 0x12,
	IEP_FORMAT_YCbCr_420_P  = 0x13,
	IEP_FORMAT_YCrCb_422_SP = 0x14,
	IEP_FORMAT_YCrCb_422_P  = 0x15,/* same as IEP_FORMAT_YCbCr_422_P */
	IEP_FORMAT_YCrCb_420_SP = 0x16,
	IEP_FORMAT_YCrCb_420_P  = 0x17,/* same as IEP_FORMAT_YCbCr_420_P */
}; /* for format */

struct iep_img
{
	unsigned short act_w;         // act_width
	unsigned short act_h;         // act_height
	signed short   x_off;         // x offset for the vir,word unit
	signed short   y_off;         // y offset for the vir,word unit

	unsigned short vir_w;         //unit :pix 
	unsigned short vir_h;         //unit :pix
	unsigned int   format;
	unsigned int  *mem_addr;
	unsigned int  *uv_addr;
	unsigned int  *v_addr;

	unsigned char   rb_swap;//not be used
	unsigned char   uv_swap;//not be used

	unsigned char   alpha_swap;//not be used
};


struct IEP_MSG {
	struct iep_img src;
	struct iep_img dst;

	struct iep_img src1;   
	struct iep_img dst1;

	struct iep_img src_itemp;
	struct iep_img src_ftemp;

	struct iep_img dst_itemp;
	struct iep_img dst_ftemp;

	u8 dither_up_en;
	u8 dither_down_en;/* not to be used */

	u8 yuv2rgb_mode;
	u8 rgb2yuv_mode;

	u8 global_alpha_value;

	u8 rgb2yuv_clip_en;
	u8 yuv2rgb_clip_en;

	u8 lcdc_path_en;
	int off_x;
	int off_y;
	int width;
	int height;
	int layer;

	u8 yuv_3D_denoise_en;

	/* yuv color enhance */
	u8 yuv_enhance_en;
	int sat_con_int;
	int contrast_int;
	int cos_hue_int;
	int sin_hue_int;
	s8 yuv_enh_brightness;	/*-32<brightness<31*/
	u8 video_mode;		/*0-3*/
	u8 color_bar_y;	/*0-127*/
	u8 color_bar_u;	/*0-127*/
	u8 color_bar_v;	/*0-127*/


	u8 rgb_enhance_en;/*i don't konw what is used*/

	u8 rgb_color_enhance_en;/*sw_rgb_color_enh_en*/
	unsigned int rgb_enh_coe;

	u8 rgb_enhance_mode;/*sw_rgb_enh_sel,dde sel*/

	u8 rgb_cg_en;/*sw_rgb_con_gam_en*/
	unsigned int cg_tab[192];

	/*sw_con_gam_order;0 cg prior to dde,1 dde prior to cg*/
	u8 rgb_contrast_enhance_mode;

	int enh_threshold; 
	int enh_alpha;
	int enh_radius;

	u8 scale_up_mode;

	u8 field_order;
	u8 dein_mode;
	/*DIL HF*/
	u8 dein_high_fre_en;
	u8 dein_high_fre_fct;
	/*DIL EI*/
	u8 dein_ei_mode;
	u8 dein_ei_smooth;
	u8 dein_ei_sel;
	u8 dein_ei_radius;/*when dein_ei_sel=0 will be used*/

	u8 vir_addr_enable;

	void *base;
};

#endif
