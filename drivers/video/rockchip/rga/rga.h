#ifndef _RGA_DRIVER_H_
#define _RGA_DRIVER_H_

#include <linux/mutex.h>

#define RGA_BLIT_SYNC	0x5017
#define RGA_BLIT_ASYNC  0x5018
#define RGA_FLUSH       0x5019
#define RGA_GET_RESULT  0x501a
#define RGA_GET_VERSION 0x501b


#define RGA_REG_CTRL_LEN    0x8    /* 8  */
#define RGA_REG_CMD_LEN     0x1c   /* 28 */
#define RGA_CMD_BUF_SIZE    0x700  /* 16*28*4 */

#define RGA_OUT_OF_RESOURCES    -10
#define RGA_MALLOC_ERROR        -11


#define rgaIS_ERROR(status)			(status < 0)
#define rgaNO_ERROR(status)			(status >= 0)
#define rgaIS_SUCCESS(status)		(status == 0)



/* RGA process mode enum */
enum
{    
    bitblt_mode               = 0x0,
    color_palette_mode        = 0x1,
    color_fill_mode           = 0x2,
    line_point_drawing_mode   = 0x3,
    blur_sharp_filter_mode    = 0x4,
    pre_scaling_mode          = 0x5,
    update_palette_table_mode = 0x6,
    update_patten_buff_mode   = 0x7,
};


enum
{
    rop_enable_mask          = 0x2,
    dither_enable_mask       = 0x8,
    fading_enable_mask       = 0x10,
    PD_enbale_mask           = 0x20,
};

enum
{
    yuv2rgb_mode0            = 0x0,     /* BT.601 MPEG */
    yuv2rgb_mode1            = 0x1,     /* BT.601 JPEG */
    yuv2rgb_mode2            = 0x2,     /* BT.709      */
};


/* RGA rotate mode */
enum 
{
    rotate_mode0             = 0x0,     /* no rotate */
    rotate_mode1             = 0x1,     /* rotate    */
    rotate_mode2             = 0x2,     /* x_mirror  */
    rotate_mode3             = 0x3,     /* y_mirror  */
};

enum
{
    color_palette_mode0      = 0x0,     /* 1K */
    color_palette_mode1      = 0x1,     /* 2K */
    color_palette_mode2      = 0x2,     /* 4K */
    color_palette_mode3      = 0x3,     /* 8K */
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
{  3, 24, {{ 0, 0,  24,16,  16, 8,   8, 0 }}, GGL_BGR  },   // RK_FORMAT_BGB_888

*/
enum
{
	RK_FORMAT_RGBA_8888    = 0x0,
    RK_FORMAT_RGBX_8888    = 0x1,
    RK_FORMAT_RGB_888      = 0x2,
    RK_FORMAT_BGRA_8888    = 0x3,
    RK_FORMAT_RGB_565      = 0x4,
    RK_FORMAT_RGBA_5551    = 0x5,
    RK_FORMAT_RGBA_4444    = 0x6,
    RK_FORMAT_BGR_888      = 0x7,
    
    RK_FORMAT_YCbCr_422_SP = 0x8,    
    RK_FORMAT_YCbCr_422_P  = 0x9,    
    RK_FORMAT_YCbCr_420_SP = 0xa,    
    RK_FORMAT_YCbCr_420_P  = 0xb,

    RK_FORMAT_YCrCb_422_SP = 0xc,    
    RK_FORMAT_YCrCb_422_P  = 0xd,    
    RK_FORMAT_YCrCb_420_SP = 0xe,    
    RK_FORMAT_YCrCb_420_P  = 0xf,
    
    RK_FORMAT_BPP1         = 0x10,
    RK_FORMAT_BPP2         = 0x11,
    RK_FORMAT_BPP4         = 0x12,
    RK_FORMAT_BPP8         = 0x13,
};
    
    
typedef struct rga_img_info_t
{
    unsigned int yrgb_addr;      /* yrgb    mem addr         */
    unsigned int uv_addr;        /* cb/cr   mem addr         */
    unsigned int v_addr;         /* cr      mem addr         */
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
rga_img_info_t;


typedef struct mdp_img_act
{
    unsigned short w;         // width
    unsigned short h;         // height
    short x_off;     // x offset for the vir
    short y_off;     // y offset for the vir
}
mdp_img_act;



typedef struct RANGE
{
    unsigned short min;
    unsigned short max;
}
RANGE;

typedef struct POINT
{
    unsigned short x;
    unsigned short y;
}
POINT;

typedef struct RECT
{
    unsigned short xmin;
    unsigned short xmax; // width - 1
    unsigned short ymin; 
    unsigned short ymax; // height - 1 
} RECT;

typedef struct RGB
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char res;
}RGB;


typedef struct MMU
{
    unsigned char mmu_en;
    uint32_t base_addr;
    uint32_t mmu_flag;     /* [0] mmu enable [1] src_flush [2] dst_flush [3] CMD_flush [4~5] page size*/
} MMU;




typedef struct COLOR_FILL
{
    short gr_x_a;
    short gr_y_a;
    short gr_x_b;
    short gr_y_b;
    short gr_x_g;
    short gr_y_g;
    short gr_x_r;
    short gr_y_r;

    //u8  cp_gr_saturation;
}
COLOR_FILL;

typedef struct FADING
{
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t res;
}
FADING;


typedef struct line_draw_t
{
    POINT start_point;              /* LineDraw_start_point                */
    POINT end_point;                /* LineDraw_end_point                  */
    uint32_t   color;               /* LineDraw_color                      */
    uint32_t   flag;                /* (enum) LineDrawing mode sel         */
    uint32_t   line_width;          /* range 1~16 */
}
line_draw_t;



struct rga_req {
    uint8_t render_mode;            /* (enum) process mode sel */
    
    rga_img_info_t src;             /* src image info */
    rga_img_info_t dst;             /* dst image info */
    rga_img_info_t pat;             /* patten image info */

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
                                    
    MMU mmu_info;                   /* mmu information */

    uint8_t  alpha_rop_mode;        /* ([0~1] alpha mode)       */
                                    /* ([2~3] rop   mode)       */
                                    /* ([4]   zero  mode en)    */
                                    /* ([5]   dst   alpha mode) */

    uint8_t  src_trans_mode;                           
};

    
typedef struct TILE_INFO
{
    int64_t matrix[4];
    
    uint16_t tile_x_num;     /* x axis tile num / tile size is 8x8 pixel */
    uint16_t tile_y_num;     /* y axis tile num */

    int16_t dst_x_tmp;      /* dst pos x = (xstart - xoff) default value 0 */
    int16_t dst_y_tmp;      /* dst pos y = (ystart - yoff) default value 0 */

    uint16_t tile_w;
    uint16_t tile_h;
    int16_t tile_start_x_coor;
    int16_t tile_start_y_coor;
    int32_t tile_xoff;
    int32_t tile_yoff;

    int32_t tile_temp_xstart;
    int32_t tile_temp_ystart;

    /* src tile incr */
    int32_t x_dx;
    int32_t x_dy;
    int32_t y_dx;
    int32_t y_dy;

    mdp_img_act dst_ctrl;
    
}
TILE_INFO;	


/**
 * struct for process session which connect to rga
 *
 * @author ZhangShengqin (2012-2-15)
 */
typedef struct rga_session {
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
} rga_session;

struct rga_reg {    
    rga_session 		*session;
	struct list_head	session_link;		/* link to rga service session */
	struct list_head	status_link;		/* link to register set list */
	uint32_t  sys_reg[RGA_REG_CTRL_LEN];
    uint32_t  cmd_reg[RGA_REG_CMD_LEN];
    
    uint32_t *MMU_base;
    //atomic_t int_enable;   

    //struct rga_req      req;
};



typedef struct rga_service_info {
    struct mutex	lock;
    struct timer_list	timer;			/* timer for power off */
    struct list_head	waiting;		/* link to link_reg in struct vpu_reg */
    struct list_head	running;		/* link to link_reg in struct vpu_reg */
    struct list_head	done;			/* link to link_reg in struct vpu_reg */
    struct list_head	session;		/* link to list_session in struct vpu_session */
    atomic_t		total_running;
    
    struct rga_reg        *reg;
    
    uint32_t            cmd_buff[28*8];/* cmd_buff for rga */
    uint32_t            *pre_scale_buf;
    atomic_t            int_disable;     /* 0 int enable 1 int disable  */
    atomic_t            cmd_num;
    atomic_t            src_format_swt;
    int                 last_prc_src_format;
    atomic_t            rga_working;
    bool                enable;

    //struct rga_req      req[10];

    struct mutex	mutex;	// mutex
} rga_service_info;



#if defined(CONFIG_ARCH_RK2928)
#define RGA_BASE                 0x1010c000
#elif defined(CONFIG_ARCH_RK30)
#define RGA_BASE                 0x10114000
#endif

//General Registers
#define RGA_SYS_CTRL             0x000
#define RGA_CMD_CTRL             0x004
#define RGA_CMD_ADDR             0x008
#define RGA_STATUS               0x00c
#define RGA_INT                  0x010
#define RGA_AXI_ID               0x014
#define RGA_MMU_STA_CTRL         0x018
#define RGA_MMU_STA              0x01c

//Command code start
#define RGA_MODE_CTRL            0x100

//Source Image Registers
#define RGA_SRC_Y_MST            0x104
#define RGA_SRC_CB_MST           0x108
#define RGA_MASK_READ_MST        0x108  //repeat
#define RGA_SRC_CR_MST           0x10c
#define RGA_SRC_VIR_INFO         0x110
#define RGA_SRC_ACT_INFO         0x114
#define RGA_SRC_X_PARA           0x118
#define RGA_SRC_Y_PARA           0x11c
#define RGA_SRC_TILE_XINFO       0x120
#define RGA_SRC_TILE_YINFO       0x124
#define RGA_SRC_TILE_H_INCR      0x128
#define RGA_SRC_TILE_V_INCR      0x12c
#define RGA_SRC_TILE_OFFSETX     0x130
#define RGA_SRC_TILE_OFFSETY     0x134
#define RGA_SRC_BG_COLOR         0x138
#define RGA_SRC_FG_COLOR         0x13c
#define RGA_LINE_DRAWING_COLOR   0x13c  //repeat
#define RGA_SRC_TR_COLOR0        0x140
#define RGA_CP_GR_A              0x140  //repeat
#define RGA_SRC_TR_COLOR1        0x144
#define RGA_CP_GR_B              0x144  //repeat

#define RGA_LINE_DRAW            0x148
#define RGA_PAT_START_POINT      0x148  //repeat

//Destination Image Registers
#define RGA_DST_MST              0x14c
#define RGA_LUT_MST              0x14c  //repeat
#define RGA_PAT_MST              0x14c  //repeat
#define RGA_LINE_DRAWING_MST     0x14c  //repeat

#define RGA_DST_VIR_INFO         0x150

#define RGA_DST_CTR_INFO         0x154
#define RGA_LINE_DRAW_XY_INFO    0x154  //repeat 

//Alpha/ROP Registers
#define RGA_ALPHA_CON            0x158

#define RGA_PAT_CON              0x15c
#define RGA_DST_VIR_WIDTH_PIX    0x15c  //repeat

#define RGA_ROP_CON0             0x160
#define RGA_CP_GR_G              0x160  //repeat
#define RGA_PRESCL_CB_MST        0x160  //repeat

#define RGA_ROP_CON1             0x164
#define RGA_CP_GR_R              0x164  //repeat
#define RGA_PRESCL_CR_MST        0x164  //repeat

//MMU Register
#define RGA_FADING_CON           0x168
#define RGA_MMU_CTRL             0x168  //repeat

#define RGA_MMU_TBL              0x16c  //repeat


#define RGA_BLIT_COMPLETE_EVENT 1




#endif /*_RK29_IPP_DRIVER_H_*/
