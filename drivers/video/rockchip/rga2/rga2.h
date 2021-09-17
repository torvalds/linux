/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RGA_DRIVER_H_
#define _RGA_DRIVER_H_

#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/dma-buf.h>


#define RGA_BLIT_SYNC	0x5017
#define RGA_BLIT_ASYNC  0x5018
#define RGA_FLUSH       0x5019
#define RGA_GET_RESULT  0x501a
#define RGA_GET_VERSION 0x501b
#define RGA_CACHE_FLUSH 0x501c

#define RGA2_BLIT_SYNC	 0x6017
#define RGA2_BLIT_ASYNC  0x6018
#define RGA2_FLUSH       0x6019
#define RGA2_GET_RESULT  0x601a
#define RGA2_GET_VERSION 0x601b


#define RGA2_REG_CTRL_LEN    0x8    /* 8  */
#define RGA2_REG_CMD_LEN     0x20   /* 32 */
#define RGA2_CMD_BUF_SIZE    0x700  /* 16*28*4 */

#define RGA2_OUT_OF_RESOURCES    -10
#define RGA2_MALLOC_ERROR        -11

#define SCALE_DOWN_LARGE 1

#define rgaIS_ERROR(status)			(status < 0)
#define rgaNO_ERROR(status)			(status >= 0)
#define rgaIS_SUCCESS(status)		(status == 0)

#define RGA_BUF_GEM_TYPE_MASK      0xC0
#define RGA_BUF_GEM_TYPE_DMA       0x80
#define RGA2_MAJOR_VERSION_MASK     (0xFF000000)
#define RGA2_MINOR_VERSION_MASK     (0x00F00000)
#define RGA2_SVN_VERSION_MASK       (0x000FFFFF)

/* RGA2 process mode enum */
enum
{
    bitblt_mode               = 0x0,
    color_palette_mode        = 0x1,
    color_fill_mode           = 0x2,
    update_palette_table_mode = 0x3,
    update_patten_buff_mode   = 0x4,
};  /*render mode*/

enum
{
    A_B_B =0x0,
    A_B_C =0x1,
};  //bitblt_mode select

enum
{
    rop_enable_mask          = 0x2,
    dither_enable_mask       = 0x8,
    fading_enable_mask       = 0x10,
    PD_enbale_mask           = 0x20,
};



/*
//          Alpha    Red     Green   Blue
{  4, 32, {{32,24,   8, 0,  16, 8,  24,16 }}, GGL_RGBA },   // RK_FORMAT_RGBA_8888
{  4, 24, {{ 0, 0,   8, 0,  16, 8,  24,16 }}, GGL_RGB  },   // RK_FORMAT_RGBX_8888
{  3, 24, {{ 0, 0,   8, 0,  16, 8,  24,16 }}, GGL_RGB  },   // RK_FORMAT_RGB_888
{  4, 32, {{32,24,  24,16,  16, 8,   8, 0 }}, GGL_BGRA },   // RK_FORMAT_BGRA_8888
{  2, 16, {{ 0, 0,  16,11,  11, 5,   5, 0 }}, GGL_RGB  },   // RK_FORMAT_RGB_565
{  2, 16, {{ 1, 0,  16,11,  11, 6,   6, 1 }}, GGL_RGBA },   // RK_FORMAT_RGBA_5551
{  2, 16, {{ 4, 0,  16,12,  12, 8,   8, 4 }}, GGL_RGBA },   // RK_FORMAT_RGBA_4444
{  2, 16, {{ 0, 0,   5, 0   11, 5,   16,11}}, GGL_BGR  },   // RK_FORMAT_BGR_565
{  2, 16, {{ 1, 0,   6, 1,  11, 6,   16,11}}, GGL_BGRA },   // RK_FORMAT_BGRA_5551
{  2, 16, {{ 4, 0,   8, 4,  12, 8,   16,12}}, GGL_BGRA },   // RK_FORMAT_BGRA_4444

*/
enum
{
	RGA2_FORMAT_RGBA_8888    = 0x0,
    RGA2_FORMAT_RGBX_8888    = 0x1,
    RGA2_FORMAT_RGB_888      = 0x2,
    RGA2_FORMAT_BGRA_8888    = 0x3,
    RGA2_FORMAT_BGRX_8888    = 0x4,
    RGA2_FORMAT_BGR_888      = 0x5,
    RGA2_FORMAT_RGB_565      = 0x6,
    RGA2_FORMAT_RGBA_5551    = 0x7,
    RGA2_FORMAT_RGBA_4444    = 0x8,
    RGA2_FORMAT_BGR_565      = 0x9,
    RGA2_FORMAT_BGRA_5551    = 0xa,
    RGA2_FORMAT_BGRA_4444    = 0xb,

    RGA2_FORMAT_Y4           = 0xe,
    RGA2_FORMAT_YCbCr_400    = 0xf,

    RGA2_FORMAT_YCbCr_422_SP = 0x10,
    RGA2_FORMAT_YCbCr_422_P  = 0x11,
    RGA2_FORMAT_YCbCr_420_SP = 0x12,
    RGA2_FORMAT_YCbCr_420_P  = 0x13,
    RGA2_FORMAT_YCrCb_422_SP = 0x14,
    RGA2_FORMAT_YCrCb_422_P  = 0x15,
    RGA2_FORMAT_YCrCb_420_SP = 0x16,
    RGA2_FORMAT_YCrCb_420_P  = 0x17,

	RGA2_FORMAT_YVYU_422 = 0x18,
	RGA2_FORMAT_YVYU_420 = 0x19,
	RGA2_FORMAT_VYUY_422 = 0x1a,
	RGA2_FORMAT_VYUY_420 = 0x1b,
	RGA2_FORMAT_YUYV_422 = 0x1c,
	RGA2_FORMAT_YUYV_420 = 0x1d,
	RGA2_FORMAT_UYVY_422 = 0x1e,
	RGA2_FORMAT_UYVY_420 = 0x1f,

    RGA2_FORMAT_YCbCr_420_SP_10B = 0x20,
    RGA2_FORMAT_YCrCb_420_SP_10B = 0x21,
    RGA2_FORMAT_YCbCr_422_SP_10B = 0x22,
    RGA2_FORMAT_YCrCb_422_SP_10B = 0x23,

	RGA2_FORMAT_BPP_1            = 0x24,
	RGA2_FORMAT_BPP_2            = 0x25,
	RGA2_FORMAT_BPP_4            = 0x26,
	RGA2_FORMAT_BPP_8            = 0x27,

	RGA2_FORMAT_ARGB_8888    = 0x28,
	RGA2_FORMAT_XRGB_8888    = 0x29,
	RGA2_FORMAT_ARGB_5551    = 0x2a,
	RGA2_FORMAT_ARGB_4444    = 0x2b,
	RGA2_FORMAT_ABGR_8888    = 0x2c,
	RGA2_FORMAT_XBGR_8888    = 0x2d,
	RGA2_FORMAT_ABGR_5551    = 0x2e,
	RGA2_FORMAT_ABGR_4444    = 0x2f,
};

typedef struct mdp_img
{
    u16 width;
    u16 height;
    u32 format;
    u32 mem_addr;
}
mdp_img;

typedef struct mdp_img_act
{
    u16 width;     // width
    u16 height;    // height
    s16 x_off;     // x offset for the vir
    s16 y_off;     // y offset for the vir
    s16 uv_x_off;
    s16 uv_y_off;
}
mdp_img_act;

typedef struct mdp_img_vir
{
    u16 width;
    u16 height;
    u32 format;
    u32 mem_addr;
    u32 uv_addr;
    u32 v_addr;
}
mdp_img_vir;


typedef struct MMU_INFO
{
    unsigned long src0_base_addr;
    unsigned long src1_base_addr;
    unsigned long dst_base_addr;
    unsigned long els_base_addr;

    u8 src0_mmu_flag;     /* [0] src0 mmu enable [1] src0_flush [2] src0_prefetch_en [3] src0_prefetch dir */
    u8 src1_mmu_flag;     /* [0] src1 mmu enable [1] src1_flush [2] src1_prefetch_en [3] src1_prefetch dir */
    u8 dst_mmu_flag;      /* [0] dst  mmu enable [1] dst_flush  [2] dst_prefetch_en  [3] dst_prefetch dir  */
    u8 els_mmu_flag;      /* [0] els  mmu enable [1] els_flush  [2] els_prefetch_en  [3] els_prefetch dir  */
} MMU_INFO;


enum
{
	MMU_DIS = 0x0,
	MMU_EN  = 0x1
};
enum
{
	MMU_FLUSH_DIS = 0x0,
	MMU_FLUSH_EN  = 0x2
};
enum
{
	MMU_PRE_DIS = 0x0,
	MMU_PRE_EN  = 0x4
};
enum
{
	MMU_PRE_DIR_FORW  = 0x0,
	MMU_PRE_DIR_BACK  = 0x8
};
typedef struct COLOR_FILL
{
    s16 gr_x_a;
    s16 gr_y_a;
    s16 gr_x_b;
    s16 gr_y_b;
    s16 gr_x_g;
    s16 gr_y_g;
    s16 gr_x_r;
    s16 gr_y_r;
}
COLOR_FILL;

enum
{
	ALPHA_ORIGINAL = 0x0,
	ALPHA_NO_128   = 0x1
};

enum
{
	R2_BLACK       = 0x00,
	R2_COPYPEN     = 0xf0,
	R2_MASKNOTPEN  = 0x0a,
	R2_MASKPEN     = 0xa0,
	R2_MASKPENNOT  = 0x50,
	R2_MERGENOTPEN = 0xaf,
	R2_MERGEPEN    = 0xfa,
	R2_MERGEPENNOT = 0xf5,
	R2_NOP         = 0xaa,
	R2_NOT         = 0x55,
	R2_NOTCOPYPEN  = 0x0f,
	R2_NOTMASKPEN  = 0x5f,
	R2_NOTMERGEPEN = 0x05,
	R2_NOTXORPEN   = 0xa5,
	R2_WHITE       = 0xff,
	R2_XORPEN      = 0x5a
};


/***************************************/
/* porting from rga.h for msg convert  */
/***************************************/

typedef struct FADING
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t res;
}
FADING;

typedef struct MMU
{
    unsigned char mmu_en;
    unsigned long base_addr;
    uint32_t mmu_flag;     /* [0] mmu enable [1] src_flush [2] dst_flush [3] CMD_flush [4~5] page size*/
} MMU;

typedef struct MMU_32
{
    unsigned char mmu_en;
    uint32_t base_addr;
    uint32_t mmu_flag;     /* [0] mmu enable [1] src_flush [2] dst_flush [3] CMD_flush [4~5] page size*/
} MMU_32;

typedef struct RECT
{
    unsigned short xmin;
    unsigned short xmax; // width - 1
    unsigned short ymin;
    unsigned short ymax; // height - 1
} RECT;

typedef struct POINT
{
    unsigned short x;
    unsigned short y;
}
POINT;

typedef struct line_draw_t
{
    POINT start_point;              /* LineDraw_start_point                */
    POINT end_point;                /* LineDraw_end_point                  */
    uint32_t   color;               /* LineDraw_color                      */
    uint32_t   flag;                /* (enum) LineDrawing mode sel         */
    uint32_t   line_width;          /* range 1~16 */
}
line_draw_t;

/* color space convert coefficient. */
typedef struct csc_coe_t {
    int16_t r_v;
    int16_t g_y;
    int16_t b_u;
    int32_t off;
} csc_coe_t;

typedef struct full_csc_t {
    unsigned char flag;
    csc_coe_t coe_y;
    csc_coe_t coe_u;
    csc_coe_t coe_v;
} full_csc_t;

typedef struct rga_img_info_t
{
    unsigned long yrgb_addr;      /* yrgb    mem addr         */
    unsigned long uv_addr;        /* cb/cr   mem addr         */
    unsigned long v_addr;         /* cr      mem addr         */
    unsigned int format;         //definition by RK_FORMAT

    unsigned short act_w;
    unsigned short act_h;
    unsigned short x_offset;
    unsigned short y_offset;

    unsigned short vir_w;
    unsigned short vir_h;

    unsigned short endian_mode; //for BPP
    unsigned short alpha_swap;    /* not use */
}
rga_img_info_t;

typedef struct rga_img_info_32_t
{
    uint32_t yrgb_addr;      /* yrgb    mem addr         */
    uint32_t uv_addr;        /* cb/cr   mem addr         */
    uint32_t v_addr;         /* cr      mem addr         */
    unsigned int format;         //definition by RK_FORMAT
    unsigned short act_w;
    unsigned short act_h;
    unsigned short x_offset;
    unsigned short y_offset;
    unsigned short vir_w;
    unsigned short vir_h;
    unsigned short endian_mode; //for BPP
    unsigned short alpha_swap;
}
rga_img_info_32_t;

struct rga_dma_buffer_t {
	/* DMABUF information */
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;

	dma_addr_t iova;
	unsigned long size;
	void *vaddr;
	enum dma_data_direction dir;
};

struct rga_req {
    uint8_t render_mode;            /* (enum) process mode sel */

    rga_img_info_t src;             /* src image info */
    rga_img_info_t dst;             /* dst image info */
    rga_img_info_t pat;             /* patten image info */

    unsigned long rop_mask_addr;         /* rop4 mask addr */
    unsigned long LUT_addr;              /* LUT addr */

    RECT clip;                      /* dst clip window default value is dst_vir */
                                    /* value from [0, w-1] / [0, h-1]*/

    int32_t sina;                   /* dst angle  default value 0  16.16 scan from table */
    int32_t cosa;                   /* dst angle  default value 0  16.16 scan from table */

    uint16_t alpha_rop_flag;        /* alpha rop process flag           */
                                    /* ([0] = 1 alpha_rop_enable)       */
                                    /* ([1] = 1 rop enable)             */
                                    /* ([2] = 1 fading_enable)          */
                                    /* ([3] = 1 PD_enable)              */
                                    /* ([4] = 1 alpha cal_mode_sel)     */
                                    /* ([5] = 1 dither_enable)          */
                                    /* ([6] = 1 gradient fill mode sel) */
                                    /* ([7] = 1 AA_enable)              */

    uint8_t  scale_mode;            /* 0 nearst / 1 bilnear / 2 bicubic */

    uint32_t color_key_max;         /* color key max */
    uint32_t color_key_min;         /* color key min */

    uint32_t fg_color;              /* foreground color */
    uint32_t bg_color;              /* background color */

    COLOR_FILL gr_color;            /* color fill use gradient */

    line_draw_t line_draw_info;

    FADING fading;

    uint8_t PD_mode;                /* porter duff alpha mode sel */

    uint8_t alpha_global_value;     /* global alpha value */

    uint16_t rop_code;              /* rop2/3/4 code  scan from rop code table*/

    uint8_t bsfilter_flag;          /* [2] 0 blur 1 sharp / [1:0] filter_type*/

    uint8_t palette_mode;           /* (enum) color palatte  0/1bpp, 1/2bpp 2/4bpp 3/8bpp*/

    uint8_t yuv2rgb_mode;           /* (enum) BT.601 MPEG / BT.601 JPEG / BT.709  */

    uint8_t endian_mode;            /* 0/big endian 1/little endian*/

    uint8_t rotate_mode;            /* (enum) rotate mode  */
                                    /* 0x0,     no rotate  */
                                    /* 0x1,     rotate     */
                                    /* 0x2,     x_mirror   */
                                    /* 0x3,     y_mirror   */

    uint8_t color_fill_mode;        /* 0 solid color / 1 patten color */

    MMU mmu_info;                   /* mmu information */

    uint8_t  alpha_rop_mode;        /* ([0~1] alpha mode)            */
                                    /* ([2~3] rop   mode)            */
                                    /* ([4]   zero  mode en)         */
                                    /* ([5]   dst   alpha mode)      */
                                    /* ([6]   alpha output mode sel) 0 src / 1 dst*/

    uint8_t  src_trans_mode;

    uint8_t dither_mode;

    full_csc_t full_csc;            /* full color space convert */
};
struct rga_req_32
{
    uint8_t render_mode;            /* (enum) process mode sel */
    rga_img_info_32_t src;             /* src image info */
    rga_img_info_32_t dst;             /* dst image info */
    rga_img_info_32_t pat;             /* patten image info */
    uint32_t rop_mask_addr;         /* rop4 mask addr */
    uint32_t LUT_addr;              /* LUT addr */
    RECT clip;                      /* dst clip window default value is dst_vir */
                                    /* value from [0, w-1] / [0, h-1]*/
    int32_t sina;                   /* dst angle  default value 0  16.16 scan from table */
    int32_t cosa;                   /* dst angle  default value 0  16.16 scan from table */
    uint16_t alpha_rop_flag;        /* alpha rop process flag           */
                                    /* ([0] = 1 alpha_rop_enable)       */
                                    /* ([1] = 1 rop enable)             */
                                    /* ([2] = 1 fading_enable)          */
                                    /* ([3] = 1 PD_enable)              */
                                    /* ([4] = 1 alpha cal_mode_sel)     */
                                    /* ([5] = 1 dither_enable)          */
                                    /* ([6] = 1 gradient fill mode sel) */
                                    /* ([7] = 1 AA_enable)              */
    uint8_t  scale_mode;            /* 0 nearst / 1 bilnear / 2 bicubic */
    uint32_t color_key_max;         /* color key max */
    uint32_t color_key_min;         /* color key min */
    uint32_t fg_color;              /* foreground color */
    uint32_t bg_color;              /* background color */
    COLOR_FILL gr_color;            /* color fill use gradient */
    line_draw_t line_draw_info;
    FADING fading;
    uint8_t PD_mode;                /* porter duff alpha mode sel */
    uint8_t alpha_global_value;     /* global alpha value */
    uint16_t rop_code;              /* rop2/3/4 code  scan from rop code table*/
    uint8_t bsfilter_flag;          /* [2] 0 blur 1 sharp / [1:0] filter_type*/
    uint8_t palette_mode;           /* (enum) color palatte  0/1bpp, 1/2bpp 2/4bpp 3/8bpp*/
    uint8_t yuv2rgb_mode;           /* (enum) BT.601 MPEG / BT.601 JPEG / BT.709  */
    uint8_t endian_mode;            /* 0/big endian 1/little endian*/
    uint8_t rotate_mode;            /* (enum) rotate mode  */
                                    /* 0x0,     no rotate  */
                                    /* 0x1,     rotate     */
                                    /* 0x2,     x_mirror   */
                                    /* 0x3,     y_mirror   */
    uint8_t color_fill_mode;        /* 0 solid color / 1 patten color */
    MMU_32 mmu_info;                   /* mmu information */
    uint8_t  alpha_rop_mode;        /* ([0~1] alpha mode)            */
                                    /* ([2~3] rop   mode)            */
                                    /* ([4]   zero  mode en)         */
                                    /* ([5]   dst   alpha mode)      */
                                    /* ([6]   alpha output mode sel) 0 src / 1 dst*/
    uint8_t  src_trans_mode;

    uint8_t dither_mode;

    full_csc_t full_csc;            /* full color space convert */
};



struct rga2_req
{
    u8 render_mode;          /* (enum) process mode sel */

    rga_img_info_t src;    // src  active window
    rga_img_info_t src1;   // src1 active window
    rga_img_info_t dst;    // dst  active window
    rga_img_info_t pat;    // patten active window

    unsigned long rop_mask_addr;       // rop4 mask addr
    unsigned long LUT_addr;            // LUT addr

    u32 rop_mask_stride;

    u8 bitblt_mode;          /* 0: SRC + DST  => DST     */
                             /* 1: SRC + SRC1 => DST     */

    u8 rotate_mode;          /* [1:0]                           */
                             /* 0   degree 0x0                  */
                             /* 90  degree 0x1                  */
                             /* 180 degree 0x2                  */
                             /* 270 degree 0x3                  */
                             /* [5:4]                           */
                             /* none                0x0         */
                             /* x_mirror            0x1         */
                             /* y_mirror            0x2         */
                             /* x_mirror + y_mirror 0x3         */

    u16 alpha_rop_flag;         /* alpha rop process flag           */
                                /* ([0] = 1 alpha_rop_enable)       */
                                /* ([1] = 1 rop enable)             */
                                /* ([2] = 1 fading_enable)          */
                                /* ([3] = 1 alpha cal_mode_sel)     */
                                /* ([4] = 1 src_dither_up_enable)   */
                                /* ([5] = 1 dst_dither_up_enable)   */
                                /* ([6] = 1 dither_down_enable)     */
                                /* ([7] = 1 gradient fill mode sel) */


    u16 alpha_mode_0;           /* [0]     SrcAlphaMode0          */
                                /* [2:1]   SrcGlobalAlphaMode0    */
                                /* [3]     SrcAlphaSelectMode0    */
                                /* [6:4]   SrcFactorMode0         */
                                /* [7]     SrcColorMode           */

                                /* [8]     DstAlphaMode0          */
                                /* [10:9]  DstGlobalAlphaMode0    */
                                /* [11]    DstAlphaSelectMode0    */
                                /* [14:12] DstFactorMode0         */
                                /* [15]    DstColorMode0          */

    u16 alpha_mode_1;           /* [0]     SrcAlphaMode1          */
                                /* [2:1]   SrcGlobalAlphaMode1    */
                                /* [3]     SrcAlphaSelectMode1    */
                                /* [6:4]   SrcFactorMode1         */

                                /* [8]     DstAlphaMode1          */
                                /* [10:9]  DstGlobalAlphaMode1    */
                                /* [11]    DstAlphaSelectMode1    */
                                /* [14:12] DstFactorMode1         */

    u8  scale_bicu_mode;    /* 0   1   2  3 */

    u32 color_key_max;      /* color key max */
    u32 color_key_min;      /* color key min */

    u32 fg_color;           /* foreground color */
    u32 bg_color;           /* background color */

    u8 color_fill_mode;
    COLOR_FILL gr_color;    /* color fill use gradient */

    u8 fading_alpha_value;  /* Fading value */
    u8 fading_r_value;
    u8 fading_g_value;
    u8 fading_b_value;

    u8 src_a_global_val;    /* src global alpha value        */
    u8 dst_a_global_val;    /* dst global alpha value        */

    u8  rop_mode;	    /* rop mode select 0 : rop2 1 : rop3 2 : rop4 */
    u16 rop_code;           /* rop2/3/4 code */

    u8 palette_mode;        /* (enum) color palatte  0/1bpp, 1/2bpp 2/4bpp 3/8bpp*/

    u8 yuv2rgb_mode;        /* (enum) BT.601 MPEG / BT.601 JPEG / BT.709  */
                            /* [1:0]   src0 csc mode        */
                            /* [3:2]   dst csc mode         */
                            /* [4]     dst csc clip enable  */
                            /* [6:5]   src1 csc mdoe        */
                            /* [7]     src1 csc clip enable */
    full_csc_t full_csc;    /* full color space convert */

    u8 endian_mode;         /* 0/little endian 1/big endian */

    u8 CMD_fin_int_enable;

    MMU_INFO mmu_info;               /* mmu infomation */

    u8 alpha_zero_key;
    u8 src_trans_mode;

    u8 alpha_swp;           /* not use */
    u8 dither_mode;

    u8 rgb2yuv_mode;

	u8 buf_type;
};

struct rga2_mmu_buf_t {
    int32_t front;
    int32_t back;
    int32_t size;
    int32_t curr;
    unsigned int *buf;
    unsigned int *buf_virtual;

    struct page **pages;

    u8 buf_order;
    u8 pages_order;
};

enum
{
    BB_ROTATE_OFF   = 0x0,     /* no rotate  */
    BB_ROTATE_90    = 0x1,     /* rotate 90  */
    BB_ROTATE_180   = 0x2,     /* rotate 180 */
    BB_ROTATE_270   = 0x3,     /* rotate 270 */
};  /*rotate mode*/

enum
{
    BB_MIRROR_OFF   = (0x0 << 4),     /* no mirror  */
    BB_MIRROR_X     = (0x1 << 4),     /* x  mirror  */
    BB_MIRROR_Y     = (0x2 << 4),     /* y  mirror  */
    BB_MIRROR_XY    = (0x3 << 4),     /* xy mirror  */
};  /*mirror mode*/

enum
{
    BB_COPY_USE_TILE = (0x1 << 6),    /* bitblt mode copy but use Tile mode */
};

enum
{
	//BYPASS        = 0x0,
    BT_601_RANGE0   = 0x1,
    BT_601_RANGE1   = 0x2,
    BT_709_RANGE0   = 0x3,
}; /*yuv2rgb_mode*/

enum
{
    BPP1        = 0x0,     /* BPP1 */
    BPP2        = 0x1,     /* BPP2 */
    BPP4        = 0x2,     /* BPP4 */
    BPP8        = 0x3      /* BPP8 */
}; /*palette_mode*/

enum
{
	SOLID_COLOR   = 0x0, //color fill mode; ROP4: SOLID_rop4_mask_addr COLOR
	PATTERN_COLOR = 0x1  //pattern_fill_mode;ROP4:PATTERN_COLOR
};  /*color fill mode*/

enum
{
	COLOR_FILL_CLIP     = 0x0,
	COLOR_FILL_NOT_CLIP = 0x1
};

enum
{
    CATROM    = 0x0,
    MITCHELL  = 0x1,
    HERMITE   = 0x2,
    B_SPLINE  = 0x3,
};  /*bicubic coefficient*/

enum
{
	ROP2 = 0x0,
	ROP3 = 0x1,
	ROP4 = 0x2
};  /*ROP mode*/

enum
{
	BIG_ENDIAN    = 0x0,
	LITTLE_ENDIAN = 0x1
};  /*endian mode*/

enum
{
	MMU_TABLE_4KB  = 0x0,
	MMU_TABLE_64KB = 0x1,
};  /*MMU table size*/

enum
{
    RGB_2_666 = 0x0,
    RGB_2_565 = 0x1,
    RGB_2_555 = 0x2,
    RGB_2_444 = 0x3,
};  /*dither down mode*/



/**
 * struct for process session which connect to rga
 *
 * @author ZhangShengqin (2012-2-15)
 */
typedef struct rga2_session {
	/* a linked list of data so we can access them for debugging */
	struct list_head    list_session;
	/* a linked list of register data waiting for process */
	struct list_head    waiting;
	/* a linked list of register data in processing */
	struct list_head    running;
	/* all coommand this thread done */
    atomic_t            done;
	wait_queue_head_t   wait;
	pid_t           pid;
	atomic_t        task_running;
    atomic_t        num_done;
} rga2_session;

struct rga2_reg {
	rga2_session		*session;
	struct list_head	session_link;
	struct list_head	status_link;
	uint32_t  sys_reg[8];
	uint32_t  csc_reg[12];
	uint32_t  cmd_reg[32];

	uint32_t *MMU_src0_base;
	uint32_t *MMU_src1_base;
	uint32_t *MMU_dst_base;
	uint32_t MMU_src0_count;
	uint32_t MMU_src1_count;
	uint32_t MMU_dst_count;

	uint32_t MMU_len;
	bool MMU_map;

	struct rga_dma_buffer_t dma_buffer_src0;
	struct rga_dma_buffer_t dma_buffer_src1;
	struct rga_dma_buffer_t dma_buffer_dst;
	struct rga_dma_buffer_t dma_buffer_els;
};

struct rga2_service_info {
    struct mutex	lock;
    struct timer_list	timer;			/* timer for power off */
    struct list_head	waiting;		/* link to link_reg in struct vpu_reg */
    struct list_head	running;		/* link to link_reg in struct vpu_reg */
    struct list_head	done;			/* link to link_reg in struct vpu_reg */
    struct list_head	session;		/* link to list_session in struct vpu_session */
    atomic_t		total_running;

    struct rga2_reg        *reg;

    uint32_t            cmd_buff[32*8];/* cmd_buff for rga */
    uint32_t            *pre_scale_buf;
    atomic_t            int_disable;     /* 0 int enable 1 int disable  */
    atomic_t            cmd_num;
    atomic_t            src_format_swt;
    int                 last_prc_src_format;
    atomic_t            rga_working;
    bool                enable;
    uint32_t            dev_mode;

    //struct rga_req      req[10];

    struct mutex	mutex;	// mutex
};

#define RGA2_TEST_CASE 0

//General Registers
#define RGA2_SYS_CTRL             0x000
#define RGA2_CMD_CTRL             0x004
#define RGA2_CMD_BASE             0x008
#define RGA2_STATUS               0x00c
#define RGA2_INT                  0x010
#define RGA2_MMU_CTRL0            0x018
#define RGA2_MMU_CMD_BASE         0x01c

//Full Csc Coefficient
#define RGA2_CSC_COE_BASE         0x60

//Command code start
#define RGA2_MODE_CTRL            0x100
#define RGA_BLIT_COMPLETE_EVENT 1

#endif /*_RK29_IPP_DRIVER_H_*/
