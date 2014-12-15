/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/3/13   19:46
 *
 *******************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <asm/atomic.h>
#include <linux/spinlock.h>
#include <linux/amports/canvas.h>
#include <linux/amports/vframe.h>
#include <linux/amports/vframe_provider.h>
#include <linux/amports/vframe_receiver.h>
#include <linux/ge2d/ge2d_main.h>
#include <linux/ge2d/ge2d.h>

#include <linux/videodev2.h>
#include <mach/am_regs.h>
#include <mach/mipi_phy_reg.h>
#include <linux/mipi/am_mipi_csi2.h>
#include <linux/tvin/tvin_v4l2.h>

#include "../common/bufq.h"
#include "csi.h"

//#define TEST_FRAME_RATE

#define MIPI_SKIP_FRAME_NUM 1

#define AML_MIPI_DST_Y_CANVAS  MIPI_CANVAS_INDEX

#define MAX_MIPI_COUNT 1

#define RECEIVER_NAME   "mipi"

#ifdef TEST_FRAME_RATE
static int time_count = 0;
#endif

typedef struct vdin_ops_privdata_s {
    int                  dev_id;
    int                  vdin_num;
	
    am_csi2_hw_t hw_info;

    am_csi2_input_t input;
    am_csi2_output_t output;

    struct mutex              buf_lock; /* lock for buff */
    unsigned                     watchdog_cnt;
	
    mipi_buf_t                  out_buff;

    unsigned                    reset_flag;
    bool                           done_flag;
    bool                           run_flag;
    wait_queue_head_t	  complete;

    ge2d_context_t           *context;
    config_para_ex_t        ge2d_config;

    unsigned char             mipi_vdin_skip;

    unsigned char            wr_canvas_index;
    unsigned char            canvas_total_count;

    unsigned                   pbufAddr;
    unsigned                   decbuf_size;

    unsigned char mirror;

    struct vdin_parm_s      param;
} vdin_ops_privdata_t;

static int am_csi2_vdin_init(am_csi2_t* dev);
static int am_csi2_vdin_streamon(am_csi2_t* dev);
static int am_csi2_vdin_streamoff(am_csi2_t* dev);
static int am_csi2_vdin_fillbuff(am_csi2_t* dev);
static struct am_csi2_pixel_fmt* getPixelFormat(u32 fourcc, bool input);
static int am_csi2_vdin_uninit(am_csi2_t* dev);

extern void convert422_to_nv21_vdin(unsigned char* src, unsigned char* dst_y, unsigned char *dst_uv, unsigned int size);
extern void swap_vdin_y(unsigned char* src, unsigned char* dst, unsigned int size);
extern void swap_vdin_uv(unsigned char* src, unsigned char* dst, unsigned int size);
static struct vdin_ops_privdata_s csi2_vdin_data[]=
{
    {
        .dev_id = -1,        
        .vdin_num = -1,
        .hw_info = {0},
        .input = {0},
        .output = {0},
        .watchdog_cnt = 0,
        .reset_flag = 0,
        .done_flag = true,
        .run_flag = false,
        .context = NULL,
        .mipi_vdin_skip = MIPI_SKIP_FRAME_NUM,
        .wr_canvas_index = 0xff,
        .canvas_total_count = 0,
        .pbufAddr = 0,
        .decbuf_size = 0,
        .mirror = 0,
    },
};

const struct am_csi2_ops_s am_csi2_vdin =
{
    .mode = AM_CSI2_VDIN,
    .getPixelFormat = getPixelFormat,
    .init = am_csi2_vdin_init,
    .streamon = am_csi2_vdin_streamon,
    .streamoff = am_csi2_vdin_streamoff,
    .fill = am_csi2_vdin_fillbuff,
    .uninit = am_csi2_vdin_uninit,
    .privdata = &csi2_vdin_data[0],
    .data_num = ARRAY_SIZE(csi2_vdin_data),
};


static const struct am_csi2_pixel_fmt am_csi2_input_pix_formats_vdin[] = 
{
    {
        .name = "4:2:2, packed, UYVY",
        .fourcc = V4L2_PIX_FMT_UYVY,
        .depth = 16,
    },
    {
        .name = "12  Y/CbCr 4:2:0",
        .fourcc = V4L2_PIX_FMT_NV12,
        .depth = 12,
    },
    {
        .name = "12  Y/CbCr 4:2:0",
        .fourcc = V4L2_PIX_FMT_NV21,
        .depth = 12,
    },
};

static const struct am_csi2_pixel_fmt am_csi2_output_pix_formats_vdin[] =
{
    {
        .name = "RGB565",
        .fourcc = V4L2_PIX_FMT_RGB565,
        .depth = 16,
    },
    {
        .name = "RGB888 (24)",
        .fourcc = V4L2_PIX_FMT_RGB24, /* 24  RGB-8-8-8 */
        .depth = 24,
    },
    {
        .name = "BGR888 (24)",
        .fourcc = V4L2_PIX_FMT_BGR24, /* 24  BGR-8-8-8 */
        .depth = 24,
    },
    {
        .name = "12  Y/CbCr 4:2:0",
        .fourcc = V4L2_PIX_FMT_NV12,
        .depth = 12,
    },
    {
        .name = "12  Y/CbCr 4:2:0",
        .fourcc = V4L2_PIX_FMT_NV21,
        .depth = 12,
    },
    {
        .name = "YUV420P",
        .fourcc = V4L2_PIX_FMT_YUV420,
        .depth = 12,
    },
};

/* ------------------------------------------------------------------
       vframe receiver callback
   ------------------------------------------------------------------*/

static struct vframe_receiver_s mipi_vf_recv[MAX_MIPI_COUNT];

static inline vframe_t *mipi_vf_peek(void)
{
    return vf_peek(RECEIVER_NAME);
}

static inline vframe_t *mipi_vf_get(void)
{
    return vf_get(RECEIVER_NAME);
}

static inline void mipi_vf_put(vframe_t *vf)
{
    struct vframe_provider_s *vfp = vf_get_provider(RECEIVER_NAME);
    if (vfp) {
        vf_put(vf, RECEIVER_NAME);
    }
    return;
}

static int mipi_vdin_receiver_event_fun(int type, void* data, void* private_data)
{
    vdin_ops_privdata_t*mipi_data = (vdin_ops_privdata_t*)private_data;
    switch(type){
        case VFRAME_EVENT_PROVIDER_VFRAME_READY:
            if(mipi_data){
                if(mipi_data->reset_flag == 1)
                    mipi_data->reset_flag = 0;
                if((mipi_vf_peek()!=NULL)&&(mipi_data->done_flag == false)){
                    mipi_data->done_flag = true;
                    wake_up_interruptible(&mipi_data->complete);
                }
            }
            break;
        case VFRAME_EVENT_PROVIDER_START:
            break;
        case VFRAME_EVENT_PROVIDER_UNREG:
            break;
        default:
            break;     
    }
    return 0;
}

static const struct vframe_receiver_op_s mipi_vf_receiver =
{
    .event_cb = mipi_vdin_receiver_event_fun
};
/* ------------------------------------------------------------------*/

static int get_input_format(int v4l2_format)
{
    int format = GE2D_FORMAT_M24_NV21;
    switch(v4l2_format){
        case V4L2_PIX_FMT_UYVY:
            format = GE2D_FORMAT_S16_YUV422;
            break;
        case V4L2_PIX_FMT_NV12:
            format = GE2D_FORMAT_M24_NV21;
            break;
        case V4L2_PIX_FMT_NV21:
            format = GE2D_FORMAT_M24_NV12;
            break;
        default:
            break;            
    }   
    return format;
}

static int get_output_format(int v4l2_format)
{
    int format = GE2D_FORMAT_M24_NV21;
    switch(v4l2_format){
        case V4L2_PIX_FMT_RGB565:
            format = GE2D_FORMAT_S16_RGB_565;
            break;
        case V4L2_PIX_FMT_BGR24:
            format = GE2D_FORMAT_S24_BGR;
            break;
        case V4L2_PIX_FMT_RGB24:
            format = GE2D_FORMAT_S24_RGB;
            break;
        case V4L2_PIX_FMT_NV12:
            format = GE2D_FORMAT_M24_NV21;
            break;
        case V4L2_PIX_FMT_NV21:
            format = GE2D_FORMAT_M24_NV12;
            break;
        case V4L2_PIX_FMT_YUV420:
            format = GE2D_FORMAT_S8_Y;
            break;
        default:
            break;            
    }   
    return format;
}

static int config_canvas_index(unsigned address,int v4l2_format,unsigned w,unsigned h,int id)
{
    int canvas = -1;
    unsigned char canvas_y = 0;
    switch(v4l2_format){
        case V4L2_PIX_FMT_RGB565:
        case V4L2_PIX_FMT_UYVY:
            canvas = AML_MIPI_DST_Y_CANVAS+(id*3);
            canvas_config(AML_MIPI_DST_Y_CANVAS+(id*3),(unsigned long)address,w*2, h, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            break; 
        case V4L2_PIX_FMT_BGR24:
        case V4L2_PIX_FMT_RGB24:
            canvas = AML_MIPI_DST_Y_CANVAS+(id*3);
            canvas_config(AML_MIPI_DST_Y_CANVAS+(id*3),(unsigned long)address,w*3, h, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            break; 
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_NV21: 
            canvas_y = AML_MIPI_DST_Y_CANVAS+(id*3);
            canvas = (canvas_y | ((canvas_y+1)<<8));
            canvas_config(canvas_y,(unsigned long)address,w, h, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(canvas_y+1,(unsigned long)(address+w*h),w, h/2, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            break;
        case V4L2_PIX_FMT_YUV420:
            canvas_y = AML_MIPI_DST_Y_CANVAS+(id*3);
            canvas = (canvas_y | ((canvas_y+1)<<8)|((canvas_y+2)<<16));
            canvas_config(canvas_y,(unsigned long)address,w, h, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(canvas_y+1,(unsigned long)(address+w*h),w/2, h/2, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(canvas_y+2,(unsigned long)(address+w*h*5/4),w/2, h/2, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            break;
        default:
            break;
    }
    return canvas;
}

static int calc_zoom(int* top ,int* left , int* bottom, int* right, int zoom)
{
    u32 screen_width, screen_height ;
    s32 start, end;
    s32 video_top, video_left, temp;
    u32 video_width, video_height;
    u32 ratio_x = 0;
    u32 ratio_y = 0;

    if(zoom<100)
        zoom = 100;

    video_top = *top;
    video_left = *left;
    video_width = *right - *left +1;
    video_height = *bottom - *top +1;

    screen_width = video_width * zoom / 100;
    screen_height = video_height * zoom / 100;

    ratio_x = (video_width << 18) / screen_width;
    if (ratio_x * screen_width < (video_width << 18)) {
        ratio_x++;
    }
    ratio_y = (video_height << 18) / screen_height;

    /* vertical */
    start = video_top + video_height / 2 - (video_height << 17)/ratio_y;
    end   = (video_height << 18) / ratio_y + start - 1;

    if (start < video_top) {
        temp = ((video_top - start) * ratio_y) >> 18;
        *top = temp;
    } else {
        *top = 0;
    }

    temp = *top + (video_height * ratio_y >> 18);
    *bottom = (temp <= (video_height - 1)) ? temp : (video_height - 1);

    /* horizontal */
    start = video_left + video_width / 2 - (video_width << 17) /ratio_x;
    end   = (video_width << 18) / ratio_x + start - 1;
    if (start < video_left) {
        temp = ((video_left - start) * ratio_x) >> 18;
        *left = temp;
    } else {
        *left = 0;
    }

    temp = *left + (video_width * ratio_x >> 18);
    *right = (temp <= (video_width - 1)) ? temp : (video_width - 1);
    return 0;
}


static int ge2d_process(vdin_ops_privdata_t* data, vframe_t* in, am_csi2_frame_t* out, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    int ret = -1;
    int src_top = 0,src_left = 0  ,src_width = 0, src_height = 0;
    canvas_t cs,cd ;
    int current_mirror = 0;
    int dst_canvas = -1;
    int cur_angle = data->output.angle;

    if((!data)||(!in)||(!out)){
        mipi_error("[mipi_vdin]:ge2d_process-- pointer NULL !!!!\n");
        return ret;
    }

    current_mirror = data->mirror;

    src_top = 0;
    src_left = 0;

    src_width = in->width;
    src_height = in->height ;

    if(data->output.zoom>100){
        int bottom = 0, right = 0;
        bottom = src_height -src_top -1;
        right = src_width -src_left -1;
        calc_zoom(&src_top, &src_left, &bottom, &right, data->output.zoom);
        src_width = right - src_left + 1;
        src_height = bottom - src_top + 1;
    }

    memset(ge2d_config,0,sizeof(config_para_ex_t));

    dst_canvas = out->index;
    if(dst_canvas<0){
        mipi_error("[mipi_vdin]:ge2d_process-- dst canvas invaild !!!!\n");
        return ret;
    }

    cur_angle = (360 - cur_angle%360);
    /* data operating. */ 
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(in->canvas0Addr&0xff,&cs);
    ge2d_config->src_planes[0].addr = cs.addr;
    ge2d_config->src_planes[0].w = cs.width;
    ge2d_config->src_planes[0].h = cs.height;
    canvas_read(dst_canvas&0xff,&cd);
    ge2d_config->dst_planes[0].addr = cd.addr;
    ge2d_config->dst_planes[0].w = cd.width;
    ge2d_config->dst_planes[0].h = cd.height;
    ge2d_config->src_key.key_enable = 0;
    ge2d_config->src_key.key_mask = 0;
    ge2d_config->src_key.key_mode = 0;
    ge2d_config->src_para.canvas_index=in->canvas0Addr;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(data->input.fourcc);
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = in->width;
    ge2d_config->src_para.height = in->height;
    ge2d_config->src2_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.canvas_index = dst_canvas&0xff;

    if(data->output.fourcc != V4L2_PIX_FMT_YUV420)
        ge2d_config->dst_para.canvas_index = dst_canvas;

    ge2d_config->dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->dst_para.format = get_output_format(data->output.fourcc)|GE2D_LITTLE_ENDIAN;
    ge2d_config->dst_para.fill_color_en = 0;
    ge2d_config->dst_para.fill_mode = 0;
    ge2d_config->dst_para.x_rev = 0;
    ge2d_config->dst_para.y_rev = 0;
    ge2d_config->dst_para.color = 0;
    ge2d_config->dst_para.top = 0;
    ge2d_config->dst_para.left = 0;
    ge2d_config->dst_para.width = data->output.output_pixel;
    ge2d_config->dst_para.height = data->output.output_line;

    if(current_mirror==1){
        ge2d_config->dst_para.x_rev = 1;
        ge2d_config->dst_para.y_rev = 0;
    }else if(current_mirror==2){
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 1;
    }else{
        ge2d_config->dst_para.x_rev = 0;
        ge2d_config->dst_para.y_rev = 0;
    }

    if(cur_angle==90){
        ge2d_config->dst_xy_swap = 1;
        ge2d_config->dst_para.x_rev ^= 1;
    }else if(cur_angle==180){
        ge2d_config->dst_para.x_rev ^= 1;
        ge2d_config->dst_para.y_rev ^= 1;
    }else if(cur_angle==270){
        ge2d_config->dst_xy_swap = 1;
        ge2d_config->dst_para.y_rev ^= 1;
    }

    if(ge2d_context_config_ex(context,ge2d_config)<0) {
        mipi_error("[mipi_vdin]:ge2d_process-- ge2d configing error!!!!\n");
        return ret;
    }
    mipi_dbg("[mipi_vdin]:ge2d_process, src: %d,%d-%dx%d. dst:%dx%d , dst canvas:0x%x\n",
		src_left ,src_top ,src_width, src_height,
		ge2d_config->dst_para.width,ge2d_config->dst_para.height, ge2d_config->dst_para.canvas_index);
    stretchblt_noalpha(context,src_left ,src_top ,src_width, src_height,0,0,ge2d_config->dst_para.width,ge2d_config->dst_para.height );

	/* for cr of  yuv420p. */
    if(data->output.fourcc == V4L2_PIX_FMT_YUV420) {
        /* for cb. */
        canvas_read((dst_canvas>>8)&0xff,&cd);
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;
        ge2d_config->dst_para.canvas_index=(dst_canvas>>8)&0xff;
        ge2d_config->dst_para.format=GE2D_FORMAT_S8_CB |GE2D_LITTLE_ENDIAN;
        ge2d_config->dst_para.width = data->output.output_pixel/2;
        ge2d_config->dst_para.height = data->output.output_line/2;
        ge2d_config->dst_xy_swap = 0;

        if(current_mirror==1){
            ge2d_config->dst_para.x_rev = 1;
            ge2d_config->dst_para.y_rev = 0;
        }else if(current_mirror==2){
            ge2d_config->dst_para.x_rev = 0;
            ge2d_config->dst_para.y_rev = 1;
        }else{
            ge2d_config->dst_para.x_rev = 0;
            ge2d_config->dst_para.y_rev = 0;
        }

        if(cur_angle==90){
            ge2d_config->dst_xy_swap = 1;
            ge2d_config->dst_para.x_rev ^= 1;
        }else if(cur_angle==180){
            ge2d_config->dst_para.x_rev ^= 1;
            ge2d_config->dst_para.y_rev ^= 1;
        }else if(cur_angle==270){
            ge2d_config->dst_xy_swap = 1;
            ge2d_config->dst_para.y_rev ^= 1;
        }

        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            mipi_error("[mipi_vdin]:ge2d_process-- ge2d configing error!!!!\n");
            return ret;
        }
        mipi_dbg("[mipi_vdin]:ge2d_process, src: %d,%d-%dx%d. dst:%dx%d\n",
		src_left ,src_top ,src_width, src_height,
		ge2d_config->dst_para.width,ge2d_config->dst_para.height );
        stretchblt_noalpha(context, src_left, src_top, src_width, src_height,
          0, 0, ge2d_config->dst_para.width,ge2d_config->dst_para.height);


        canvas_read((dst_canvas>>16)&0xff,&cd);
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;
        ge2d_config->dst_para.canvas_index=(dst_canvas>>16)&0xff;
        ge2d_config->dst_para.format=GE2D_FORMAT_S8_CR|GE2D_LITTLE_ENDIAN;
        ge2d_config->dst_para.width = data->output.output_pixel/2;
        ge2d_config->dst_para.height = data->output.output_line/2;
        ge2d_config->dst_xy_swap = 0;

        if(current_mirror==1){
            ge2d_config->dst_para.x_rev = 1;
            ge2d_config->dst_para.y_rev = 0;
        }else if(current_mirror==2){
            ge2d_config->dst_para.x_rev = 0;
            ge2d_config->dst_para.y_rev = 1;
        }else{
            ge2d_config->dst_para.x_rev = 0;
            ge2d_config->dst_para.y_rev = 0;
        }

        if(cur_angle==90){
            ge2d_config->dst_xy_swap = 1;
            ge2d_config->dst_para.x_rev ^= 1;
        }else if(cur_angle==180){
            ge2d_config->dst_para.x_rev ^= 1;
            ge2d_config->dst_para.y_rev ^= 1;
        }else if(cur_angle==270){
            ge2d_config->dst_xy_swap = 1;
            ge2d_config->dst_para.y_rev ^= 1;
        }

        if(ge2d_context_config_ex(context,ge2d_config)<0) {
            mipi_error("[mipi_vdin]:ge2d_process-- ge2d configing error!!!!\n");
            return ret;
        }
        stretchblt_noalpha(context, src_left, src_top, src_width, src_height,
          0, 0, ge2d_config->dst_para.width,ge2d_config->dst_para.height);
    }
    return 0;
}

static bool is_need_ge2d_process(vdin_ops_privdata_t* data)
{
    bool ret = false;
    if(!data){
        mipi_error("[mipi_vdin]:is_need_ge2d_process-- pointer NULL !!!!\n");
        return ret;
    }
    // check the color space
    if(data->input.fourcc == data->output.fourcc){
        ret = false;
    }else if((data->input.fourcc == V4L2_PIX_FMT_UYVY)&&((data->output.fourcc == V4L2_PIX_FMT_NV12)||(data->output.fourcc == V4L2_PIX_FMT_NV21))){
        ret = false;
    }else{
        ret = true;
    }

    //check the size
    if(data->input.active_pixel!=data->output.output_pixel){
        ret |= true;
    }else if(data->input.active_line<data->output.output_line){
        ret |= true;
    }

    if(data->mirror)
        ret |= true;

    if(data->output.zoom>100)
        ret |= true;

    if(data->output.angle)
        ret |= true;

    if((data->input.active_pixel&0x1f)||(data->output.output_pixel&0x1f))
        ret |= true;
    return ret;
}

static int sw_process(vdin_ops_privdata_t* data, vframe_t* in)
{
    canvas_t cs0,cs1;
    void __iomem * buffer_src_y = NULL;
    void __iomem * buffer_src_uv = NULL;
    int ret = -1;
    int i=0, src_width = 0, src_height = 0;
    int poss=0,posd=0,bytesperline = 0;

    if((!data)||(!in)){
        mipi_error("[mipi_vdin]:sw_process-- pointer NULL !!!!\n");
        return ret;
    }

    if(!in->canvas0Addr){
        mipi_error("[mipi_vdin]:sw_process-- invalid canvas !!!!\n");
        return ret;
    }

    src_width = in->width;
    src_height = in->height;
    if((src_width!=data->output.output_pixel)||(src_height<data->output.output_line)){
        mipi_error("[mipi_vdin]:sw_process-- size error !!!!\n");
        return ret;
    }

    canvas_read(in->canvas0Addr&0xff,&cs0);
    buffer_src_y = ioremap_wc(cs0.addr,cs0.width*cs0.height);
    if(!buffer_src_y) {
        if(buffer_src_y)
            iounmap(buffer_src_y);
        mipi_error("[mipi_vdin]:sw_process---mapping buffer error\n");
        return ret;
    }
    if(data->output.fourcc == data->input.fourcc){
        if((data->output.fourcc == V4L2_PIX_FMT_NV12)||(data->output.fourcc == V4L2_PIX_FMT_NV21)) {
            unsigned uv_width = data->output.output_pixel;
            unsigned uv_height = data->output.output_line>>1;
            for(i=data->output.output_line;i>0;i--) { /* copy y */
                swap_vdin_y((unsigned char *)(data->output.vaddr+posd),(unsigned char *)(buffer_src_y+poss),data->output.output_pixel);
                poss += cs0.width;
                posd += data->output.output_pixel;
            }
            canvas_read((in->canvas0Addr>>8)&0xff,&cs1);
            buffer_src_uv = ioremap_wc(cs1.addr,cs1.width*cs1.height);
            poss = 0;
            for(i=uv_height; i > 0; i--){ 
                swap_vdin_uv((unsigned char *)(data->output.vaddr+posd), (unsigned char *)(buffer_src_uv+poss), uv_width);
                poss += cs1.width;
                posd += uv_width;
            }
            iounmap(buffer_src_uv);
            ret = 0;
        }else{
            bytesperline = (data->output.output_pixel*data->output.depth)>>3;
            for(i=data->output.output_line;i>0;i--) { /* copy y */
                memcpy((void *)(data->output.vaddr+posd),(void *)(buffer_src_y+poss),bytesperline);
                poss += cs0.width;
                posd += bytesperline;
            }
            ret = 0;
        }
    }else if((data->input.fourcc == V4L2_PIX_FMT_UYVY)&&((data->output.fourcc == V4L2_PIX_FMT_NV12)||(data->output.fourcc == V4L2_PIX_FMT_NV21))){
        unsigned char* src_line = (unsigned char *)buffer_src_y;
        unsigned char* dst_y = (unsigned char *)data->output.vaddr;
        unsigned char* dst_uv = dst_y+(data->output.output_pixel*data->output.output_line);
        for(i=data->output.output_line;i>0;i-=2) { /* copy y */
            convert422_to_nv21_vdin(src_line, dst_y, dst_uv, data->output.output_pixel);
            src_line += (data->output.output_pixel*4);
            dst_y+= (data->output.output_pixel*2);
            dst_uv+=data->output.output_pixel;
        }
        ret = 0;
    }else{
        //to do
        mipi_error("[mipi_vdin]:sw_process---format is not match.input:0x%x,output:0x%x\n",data->input.fourcc,data->output.fourcc);
    }
    iounmap(buffer_src_y);
    return ret;
}

static struct am_csi2_pixel_fmt* getPixelFormat(u32 fourcc, bool input)
{
    struct am_csi2_pixel_fmt* r = NULL;
    int i = 0;
    if(input){  //mipi input format support
        for( i =0;i<ARRAY_SIZE(am_csi2_input_pix_formats_vdin);i++){
            if(am_csi2_input_pix_formats_vdin[i].fourcc == fourcc){
                r = (struct am_csi2_pixel_fmt*)&am_csi2_input_pix_formats_vdin[i];
                break;
            }
        }
    }else{     //mipi output format support, can be converted by ge2d
        for( i =0;i<ARRAY_SIZE(am_csi2_output_pix_formats_vdin);i++){
            if(am_csi2_output_pix_formats_vdin[i].fourcc == fourcc){
                r = (struct am_csi2_pixel_fmt*)&am_csi2_output_pix_formats_vdin[i];
                break;
            }
        }
    }
    return r;
}

static int fill_buff_from_canvas(am_csi2_frame_t* frame, am_csi2_output_t* output)
{

    int ret = -1;
    void __iomem * buffer_y_start = NULL;
    void __iomem * buffer_u_start = NULL;
    void __iomem * buffer_v_start = NULL;
    canvas_t canvas_work_y;
    canvas_t canvas_work_u;
    canvas_t canvas_work_v;
    int i=0,poss=0,posd=0,bytesperline = 0;
    if((!frame)||(!output->vaddr)||(frame->index<0)){
        mipi_error("[mipi_vdin]:fill_buff_from_canvas---pionter error\n");
        return ret;
    }
	
    canvas_read(frame->index&0xff,&canvas_work_y);
    buffer_y_start = ioremap_wc(frame->ddr_address,output->frame_size);
    //buffer_y_start = ioremap_wc(canvas_work_y.addr,canvas_work_y.width*canvas_work_y.height);

    if(buffer_y_start == NULL) {
        mipi_error("[mipi_vdin]:fill_buff_from_canvas---mapping buffer error\n");
        return ret;
    }
    mipi_dbg("[mipi_vdin]:fill_buff_from_canvas:frame->ddr_address:0x%x,canvas y:0x%x--%dx%d\n",(unsigned long)frame->ddr_address,(unsigned long)canvas_work_y.addr,canvas_work_y.width,canvas_work_y.height);
    if((output->fourcc == V4L2_PIX_FMT_BGR24)||
      (output->fourcc == V4L2_PIX_FMT_RGB24)||
      (output->fourcc == V4L2_PIX_FMT_RGB565)) {
        bytesperline = (output->output_pixel*output->depth)>>3;
        for(i=0;i<output->output_line;i++) {
            memcpy((void *)(output->vaddr+posd),(void *)(buffer_y_start+poss),bytesperline);
            poss += canvas_work_y.width;
            posd += bytesperline;
        }
        ret = 0;
    } else if((output->fourcc == V4L2_PIX_FMT_NV12)||(output->fourcc == V4L2_PIX_FMT_NV21)) {
        unsigned uv_width = output->output_pixel;
        unsigned uv_height = output->output_line>>1;
        for(i=output->output_line;i>0;i--) { /* copy y */
            memcpy((void *)(output->vaddr+posd),(void *)(buffer_y_start+poss),output->output_pixel);
            poss += canvas_work_y.width;
            posd += output->output_pixel;
        }
        canvas_read((frame->index>>8)&0xff,&canvas_work_u);
        buffer_u_start = ioremap_wc(canvas_work_u.addr,canvas_work_u.width*canvas_work_u.height);
        mipi_dbg("[mipi_vdin]:fill_buff_from_canvas:canvas u:0x%x--%dx%d\n",(unsigned long)canvas_work_u.addr,canvas_work_u.width,canvas_work_u.height);
        poss = 0;
        for(i=uv_height; i > 0; i--){
            memcpy((void *)(output->vaddr+posd), (void *)(buffer_u_start+poss), uv_width);
            poss += canvas_work_u.width;
            posd += uv_width;
        }
        iounmap(buffer_u_start);
        ret = 0;
    } else if (output->fourcc == V4L2_PIX_FMT_YUV420) {
        int uv_width = output->output_pixel>>1;
        int uv_height = output->output_line>>1;
        for(i=output->output_line;i>0;i--) { /* copy y */
            memcpy((void *)(output->vaddr+posd),(void *)(buffer_y_start+poss),output->output_pixel);
            poss += canvas_work_y.width;
            posd += output->output_pixel;
        }
        canvas_read((frame->index>>8)&0xff,&canvas_work_u);
        buffer_u_start = ioremap_wc(canvas_work_u.addr,canvas_work_u.width*canvas_work_u.height);
        poss = 0;
        for(i=uv_height; i > 0; i--){
            memcpy((void *)(output->vaddr+posd), (void *)(buffer_u_start+poss), uv_width);
            poss += canvas_work_u.width;
            posd += uv_width;
        }
        canvas_read((frame->index>>16)&0xff,&canvas_work_v);
        poss = 0;
        buffer_u_start = ioremap_wc(canvas_work_v.addr,canvas_work_v.width*canvas_work_v.height);
        for(i=uv_height; i > 0; i--){
            memcpy((void *)(output->vaddr+posd), (void *)(buffer_v_start+poss), uv_width);
            poss += canvas_work_v.width;
            posd += uv_width;
        }
        iounmap(buffer_u_start);
        iounmap(buffer_v_start);
        ret = 0;
    }
    iounmap(buffer_y_start);
    return ret;
}

static int am_csi2_vdin_init(am_csi2_t* dev)
{
    int ret = -1;
    vdin_ops_privdata_t* data = (vdin_ops_privdata_t*)dev->ops->privdata;

    if(dev->id>=dev->ops->data_num){
        mipi_error("[mipi_vdin]:am_csi2_vdin_init ---- id error.\n");
        goto err_exit;
    }

    data = &data[dev->id];
    data->dev_id = dev->id;

    data->context = create_ge2d_work_queue();
    if(!data->context){
        ret =  -ENOENT;
        mipi_error("[mipi_vdin]:am_csi2_vdin_init ---- ge2d context register error.\n");
        goto err_exit;
    }

    data->hw_info.lanes = dev->client->lanes;
    data->hw_info.channel = dev->client->channel;
    data->hw_info.ui_val = dev->ui_val;
    data->hw_info.hs_freq = dev->hs_freq;
    data->hw_info.clock_lane_mode = dev->clock_lane_mode;
    data->hw_info.mode = AM_CSI2_VDIN;

    mutex_init(&data->buf_lock);
    init_waitqueue_head (&data->complete);
    data->out_buff.q_lock= __SPIN_LOCK_UNLOCKED(data->out_buff.q_lock);
    data->run_flag = false;

    vf_receiver_init(&mipi_vf_recv[data->dev_id], RECEIVER_NAME, &mipi_vf_receiver, (void*)data);
    vf_reg_receiver(&mipi_vf_recv[data->dev_id]);

    init_am_mipi_csi2_clock();
    ret = 0;
    mipi_dbg("[mipi_vdin]:am_csi2_vdin_init ok.\n");
err_exit:
    return ret;
}
static int am_csi2_vdin_streamon(am_csi2_t* dev)
{
    vdin_ops_privdata_t* data = (vdin_ops_privdata_t*)dev->ops->privdata;
    vdin_parm_t para;
    csi_parm_t  csi_para;
    int i = 0;
    data = &data[dev->id];
    
    data->hw_info.active_line = dev->input.active_line;
    data->hw_info.active_pixel= dev->input.active_pixel;
    data->hw_info.frame_size = dev->input.frame_size;
    data->hw_info.urgent = 0;

    memcpy(&data->input,&dev->input,sizeof(am_csi2_input_t));
    memcpy(&data->output,&dev->output,sizeof(am_csi2_output_t));
    data->output.vaddr = NULL;
    data->reset_flag = 0;
    data->done_flag = true;
    data->hw_info.frame = NULL;

    am_mipi_csi2_init(&data->hw_info);
    data->watchdog_cnt = 0;
    data->run_flag = true;

    bufq_init(&data->out_buff,&(data->output.frame[0]),data->output.frame_available);
    for(i = 0;i<data->output.frame_available;i++){
        data->output.frame[i].index = config_canvas_index(data->output.frame[i].ddr_address,data->output.fourcc,data->output.output_pixel,data->output.output_line,i);
        if(data->output.frame[i].index<0){
            mipi_error("[mipi_vdin]:am_csi2_vdin_streamon ---- canvas config error. \n");
            return -1;
        }
    }

    data->mirror = dev->mirror;

    if(dev->client->vdin_num>=0)
        data->vdin_num = dev->client->vdin_num;
    else
        data->vdin_num = 0;

    memset( &para, 0, sizeof(para));
    memset( &csi_para, 0, sizeof(para));
    para.port = TVIN_PORT_MIPI;
    if(data->input.fourcc == V4L2_PIX_FMT_NV12){
        csi_para.csi_ofmt = TVIN_YUV422;
        para.cfmt  = TVIN_NV21;
    }else if(data->input.fourcc == V4L2_PIX_FMT_NV21){
        csi_para.csi_ofmt = TVIN_YVYU422;
        para.cfmt  = TVIN_NV21;
    }else{
        csi_para.csi_ofmt = TVIN_YUV422;
        para.cfmt  = TVIN_YUV422;
    }
    csi_para.skip_frames = data->mipi_vdin_skip;
    para.fmt = TVIN_SIG_FMT_MAX;
    para.hsync_phase = 0;
    para.vsync_phase = 0;
    para.frame_rate = dev->frame_rate;
    para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
    para.h_active = data->input.active_pixel;
    para.v_active = data->input.active_line;
    para.reserved = &csi_para;

    start_tvin_service(data->vdin_num, &para);
    mipi_dbg("[mipi_vdin]:am_csi2_vdin_streamon ok.\n");
    return 0;
}

static int am_csi2_vdin_streamoff(am_csi2_t* dev)
{
    vdin_ops_privdata_t* data = (vdin_ops_privdata_t*)dev->ops->privdata;

    data = &data[dev->id];
    mutex_lock(&data->buf_lock);
    //data->reset_flag = 1;
    //wake_up_interruptible(&data->complete);

    data->run_flag = false;
    stop_tvin_service(data->vdin_num);
    mutex_unlock(&data->buf_lock);
    mipi_dbg("[mipi_vdin]:am_csi2_vdin_streamoff ok.\n");
    return 0;
}

static int am_csi2_vdin_fillbuff(am_csi2_t* dev)
{
    vdin_ops_privdata_t* data = (vdin_ops_privdata_t*)dev->ops->privdata;
    int ret = 0;
    vframe_t* frame = NULL;
    unsigned long timeout = msecs_to_jiffies(500) + 1;
    data = &data[dev->id];

    mutex_lock(&data->buf_lock);

    data->output.vaddr = dev->output.vaddr;
    data->output.zoom = dev->output.zoom;
    data->output.angle= dev->output.angle;

    mipi_dbg("[mipi_vdin]:am_csi2_vdin_fillbuff. address:0x%x. size:%dx%d, depth:%d,zoom level:%d, angle:%d.\n",
		(u32)data->output.vaddr,data->output.output_pixel,data->output.output_line,data->output.depth,data->output.zoom,data->output.angle);

    if(mipi_vf_peek()==NULL){
        data->done_flag = false;
        wait_event_interruptible_timeout(data->complete,(data->reset_flag)||(data->done_flag==true),timeout);
    }
    if(!data->reset_flag){
        am_csi2_frame_t* temp_frame = NULL;
        frame= mipi_vf_get();
        if(frame){
            if(is_need_ge2d_process(data))
                temp_frame = bufq_pop_free(&data->out_buff);
            if(temp_frame){
                mipi_dbg("[mipi_vdin]:am_csi2_vdin_fillbuff ---need ge2d to pre process\n");
                memset(&(data->ge2d_config),0,sizeof(config_para_ex_t));
                ret = ge2d_process(data,frame,temp_frame,data->context,&(data->ge2d_config));
            }
            if(temp_frame){
                ret = fill_buff_from_canvas(temp_frame,&(data->output));
                bufq_push_free(&data->out_buff, temp_frame);
            }else{
                ret = sw_process(data,frame);
            }
            mipi_vf_put(frame);
        }
    }
    mutex_unlock(&data->buf_lock);
    return ret;
}

static int am_csi2_vdin_uninit(am_csi2_t* dev)
{
    vdin_ops_privdata_t* data = (vdin_ops_privdata_t*)dev->ops->privdata;

    data = &data[dev->id];
    data->reset_flag = 2;
    wake_up_interruptible(&data->complete);
    if(data->context)
        destroy_ge2d_work_queue(data->context);
    mutex_destroy(&data->buf_lock);

    vf_unreg_receiver(&mipi_vf_recv[data->dev_id]);

    am_mipi_csi2_uninit();
    data->vdin_num = -1;
    mipi_dbg("[mipi_vdin]:am_csi2_vdin_uninit ok.\n");
    return 0;
}

