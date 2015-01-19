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
#include <linux/ge2d/ge2d_main.h>
#include <linux/ge2d/ge2d.h>

#include <linux/videodev2.h>
#include <mach/am_regs.h>
#include <mach/mipi_phy_reg.h>
#include <linux/mipi/am_mipi_csi2.h>

#include "../common/bufq.h"

//#define TEST_FRAME_RATE

//#define ENABLE_CACHE_MODE

#define CSI2_CHECK_INTERVAL       HZ/100

#define MIPI_SKIP_FRAME_NUM 1

#define AML_MIPI_SRC_CANVAS  MIPI_CANVAS_INDEX
#define AML_MIPI_DST_Y_CANVAS  (MIPI_CANVAS_INDEX+1)

typedef struct mem_ops_privdata_s {
#ifdef ENABLE_CACHE_MODE
    struct task_struct* task;
    struct semaphore start_sem;
#endif
    int                  irq;
    int                  dev_id;

    am_csi2_hw_t hw_info;

    unsigned frame_rate;
    unsigned char mirror;

    am_csi2_input_t input;
    am_csi2_output_t output;

    struct timer_list          timer;
    struct mutex              buf_lock; /* lock for buff */
    spinlock_t                   isr_lock;
    struct tasklet_struct    isr_tasklet;
    unsigned long              pre_irq_time;
    unsigned                     watchdog_cnt;
	
    mipi_buf_t                  in_buff;
    mipi_buf_t                  out_buff;

    am_csi2_frame_t*       wr_frame;
    unsigned                    disable_ddr;
    unsigned                    reset_flag;
    bool                           done_flag;
    wait_queue_head_t	  complete;
    ge2d_context_t           *context;
    config_para_ex_t        ge2d_config;
    int                             mipi_mem_skip ;
} mem_ops_privdata_t;

static int am_csi2_mem_init(am_csi2_t* dev);
static int am_csi2_mem_streamon(am_csi2_t* dev);
static int am_csi2_mem_streamoff(am_csi2_t* dev);
static int am_csi2_mem_fillbuff(am_csi2_t* dev);
static struct am_csi2_pixel_fmt* getPixelFormat(u32 fourcc, bool input);
static int am_csi2_mem_uninit(am_csi2_t* dev);

extern void convert422_to_nv21_mem(unsigned char* src, unsigned char* dst_y, unsigned char *dst_uv, unsigned int size);
extern void swap_mem(unsigned char* src, unsigned char* dst, unsigned int size);

static struct mem_ops_privdata_s csi2_mem_data[]=
{
    {
#ifdef ENABLE_CACHE_MODE
        .task = NULL,
#endif
        .irq = -1,
        .dev_id = -1,        
        .hw_info = {0},
        .frame_rate = 0,
        .mirror = 0,
        .input = {0},
        .output = {0},
        .isr_tasklet = {0},
        .watchdog_cnt = 0,
        .wr_frame = NULL,
        .disable_ddr = 0,
        .reset_flag = 0,
        .done_flag = true,
        .context = NULL,
        .mipi_mem_skip = MIPI_SKIP_FRAME_NUM,
    },
};

const struct am_csi2_ops_s am_csi2_mem =
{
    .mode = AM_CSI2_ALL_MEM,
    .getPixelFormat = getPixelFormat,
    .init = am_csi2_mem_init,
    .streamon = am_csi2_mem_streamon,
    .streamoff = am_csi2_mem_streamoff,
    .fill = am_csi2_mem_fillbuff,
    .uninit = am_csi2_mem_uninit,
    .privdata = &csi2_mem_data[0],
    .data_num  = ARRAY_SIZE(csi2_mem_data),
};

static const struct am_csi2_pixel_fmt am_csi2_input_pix_formats_mem[] = 
{
    {
        .name = "RGB565",
        .fourcc = V4L2_PIX_FMT_RGB565,
        .depth = 16,
    },
    {
        .name = "4:2:2, packed, YVYU",
        .fourcc = V4L2_PIX_FMT_YVYU,
        .depth    = 16,
    },
};

static const struct am_csi2_pixel_fmt am_csi2_output_pix_formats_mem[] =
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

static int get_input_format(int v4l2_format)
{
    int format = GE2D_FORMAT_S16_YUV422;
    switch(v4l2_format){
        case V4L2_PIX_FMT_RGB565:
            format = GE2D_FORMAT_S16_RGB_565;
            break;
        case V4L2_PIX_FMT_YVYU:
            format = GE2D_FORMAT_S16_YUV422;
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
            format = GE2D_FORMAT_S24_BGR ;
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
        case V4L2_PIX_FMT_YVYU:
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


static int ge2d_process(mem_ops_privdata_t* data,am_csi2_frame_t* in, am_csi2_frame_t* out, ge2d_context_t *context,config_para_ex_t* ge2d_config)
{
    int ret = -1;
    int src_top = 0,src_left = 0  ,src_width = 0, src_height = 0;
    canvas_t cs,cd;
    int current_mirror = 0;
    int dst_canvas = -1;
    int cur_angle = data->output.angle;

    if((!data)||(!in)||(!out)){
        mipi_error("[mipi_mem]:ge2d_process-- pointer NULL !!!!\n");
        return ret;
    }

    current_mirror = data->mirror;
    src_top = 0;
    src_left = 0;

    src_width = (in->w*8)/data->input.depth;
    src_height = in->h;

    if(data->output.zoom>100){
        int bottom = 0, right = 0;
        bottom = src_height -src_top -1;
        right = src_width -src_left -1;
        calc_zoom(&src_top, &src_left, &bottom, &right, data->output.zoom);
        src_width = right - src_left + 1;
        src_height = bottom - src_top + 1;
    }

    memset(ge2d_config,0,sizeof(config_para_ex_t));

    if(data->input.fourcc == V4L2_PIX_FMT_YVYU){
        void __iomem * buffer_src = ioremap_wc(in->ddr_address,data->input.frame_size);
        unsigned char* src = (unsigned char *)buffer_src;
        int i = in->h;
        if(buffer_src ==NULL){
            mipi_error("[mipi_mem]:ge2d_process-- swap remap fail!!!!\n");
            return ret;
        }
        for(;i>0;i--) {
            swap_mem(src, src, data->input.active_pixel);
            src += (data->input.active_pixel*2);
        }
        iounmap(buffer_src);
    }

    canvas_config(AML_MIPI_SRC_CANVAS,
        (unsigned long)in->ddr_address,
        in->w, in->h,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    
    dst_canvas = out->index;
    if(dst_canvas<0){
        mipi_error("[mipi_mem]:ge2d_process-- dst canvas invaild !!!!\n");
        return ret;
    }
    cur_angle = (360 - cur_angle%360);
    /* data operating. */ 
    ge2d_config->alu_const_color= 0;//0x000000ff;
    ge2d_config->bitmask_en  = 0;
    ge2d_config->src1_gb_alpha = 0;//0xff;
    ge2d_config->dst_xy_swap = 0;

    canvas_read(AML_MIPI_SRC_CANVAS&0xff,&cs);
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
    ge2d_config->src_para.canvas_index=AML_MIPI_SRC_CANVAS;
    ge2d_config->src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config->src_para.format = get_input_format(data->input.fourcc)|GE2D_LITTLE_ENDIAN;
    ge2d_config->src_para.fill_color_en = 0;
    ge2d_config->src_para.fill_mode = 0;
    ge2d_config->src_para.x_rev = 0;
    ge2d_config->src_para.y_rev = 0;
    ge2d_config->src_para.color = 0xffffffff;
    ge2d_config->src_para.top = 0;
    ge2d_config->src_para.left = 0;
    ge2d_config->src_para.width = (in->w*8)/data->input.depth;
    ge2d_config->src_para.height = in->h;
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
        mipi_error("[mipi_mem]:ge2d_process-- ge2d configing error!!!!\n");
        return ret;
    }

    mipi_dbg("[mipi_mem]:ge2d_process, src: %d,%d-%dx%d. dst:%dx%d , dst canvas:0x%x\n",
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
            mipi_error("[mipi_mem]:ge2d_process-- ge2d configing error!!!!\n");
            return ret;
        }
        mipi_dbg("[mipi_mem]:ge2d_process, src: %d,%d-%dx%d. dst:%dx%d\n",
		src_left ,src_top ,src_width, src_height,
		ge2d_config->dst_para.width,ge2d_config->dst_para.height );
        stretchblt_noalpha(context, src_left, src_top, src_width, src_height,
          0, 0, ge2d_config->dst_para.width,ge2d_config->dst_para.height);


        canvas_read((dst_canvas>>16)&0xff,&cd);
        ge2d_config->dst_planes[0].addr = cd.addr;
        ge2d_config->dst_planes[0].w = cd.width;
        ge2d_config->dst_planes[0].h = cd.height;
        ge2d_config->dst_para.canvas_index=(dst_canvas>>16)&0xff;
        ge2d_config->dst_para.format=GE2D_FORMAT_S8_CR |GE2D_LITTLE_ENDIAN;
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
            mipi_error("[mipi_mem]:ge2d_process-- ge2d configing error!!!!\n");
            return ret;
        }
        stretchblt_noalpha(context, src_left, src_top, src_width, src_height,
          0, 0, ge2d_config->dst_para.width,ge2d_config->dst_para.height);
    }
    return 0;
}

static bool is_need_ge2d_process(mem_ops_privdata_t* data,am_csi2_frame_t* in)
{
    bool ret = false;
    if((!in)||(!data)){
        mipi_error("[mipi_mem]:is_need_ge2d_process-- pointer NULL !!!!\n");
        return ret;
    }
    // check the color space
    if(data->input.fourcc == data->output.fourcc){
        ret = false;
    }else if((data->input.fourcc == V4L2_PIX_FMT_YVYU)&&((data->output.fourcc == V4L2_PIX_FMT_NV12)||(data->output.fourcc == V4L2_PIX_FMT_NV21))){
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
    return ret;
}

#ifndef ENABLE_CACHE_MODE
static int sw_process(mem_ops_privdata_t* data,am_csi2_frame_t* in)
{
    void __iomem * buffer_src = NULL;
    int ret = -1;
    int i=0, src_width = 0, src_height = 0;

    if((!data)||(!in)){
        mipi_error("[mipi_mem]:sw_process-- pointer NULL !!!!\n");
        return ret;
    }

    src_width = (in->w*8)/data->input.depth;
    src_height = in->h;
    if((src_width!=data->output.output_pixel)||(src_height<data->output.output_line)){
        mipi_error("[mipi_mem]:sw_process-- size error !!!!\n");
        return ret;
    }

    buffer_src = ioremap_wc(in->ddr_address,data->input.frame_size);
    if(!buffer_src) {
        if(buffer_src)
            iounmap(buffer_src);
        mipi_error("[mipi_mem]:sw_process---mapping buffer error\n");
        return ret;
    }
    if(data->output.fourcc == data->input.fourcc){
        memcpy((void *)data->output.vaddr,(void *)buffer_src,data->output.frame_size);
        ret = 0;
    }else if((data->input.fourcc == V4L2_PIX_FMT_YVYU)&&((data->output.fourcc == V4L2_PIX_FMT_NV12)||(data->output.fourcc == V4L2_PIX_FMT_NV21))){
        unsigned char* src_line = (unsigned char *)buffer_src;
        unsigned char* dst_y = (unsigned char *)data->output.vaddr;
        unsigned char* dst_uv = dst_y+(data->output.output_pixel*data->output.output_line);
        for(i=data->output.output_line;i>0;i-=2) { /* copy y */
            convert422_to_nv21_mem(src_line, dst_y, dst_uv, data->output.output_pixel);
            src_line += (data->output.output_pixel*4);
            dst_y+= (data->output.output_pixel*2);
            dst_uv+=data->output.output_pixel;
        }
        ret = 0;
    }else{
        //to do
        mipi_error("[mipi_mem]:sw_process---format is not match.input:0x%x,output:0x%x\n",data->input.fourcc,data->output.fourcc);
    }
    iounmap(buffer_src);
    return ret;
}
#endif

static struct am_csi2_pixel_fmt* getPixelFormat(u32 fourcc, bool input)
{
    struct am_csi2_pixel_fmt* r = NULL;
    int i = 0;
    if(input){  //mipi input format support
        for(i =0;i<ARRAY_SIZE(am_csi2_input_pix_formats_mem);i++){
            if(am_csi2_input_pix_formats_mem[i].fourcc == fourcc){
                r = (struct am_csi2_pixel_fmt*)&am_csi2_input_pix_formats_mem[i];
                break;
            }       
        }
    }else{     //mipi output format support, can be converted by ge2d
        for(i =0;i<ARRAY_SIZE(am_csi2_output_pix_formats_mem);i++){
            if(am_csi2_output_pix_formats_mem[i].fourcc == fourcc){
                r = (struct am_csi2_pixel_fmt*)&am_csi2_output_pix_formats_mem[i];
                break;
            }       
        }
    }
    return r;
}

static bool checkframe(am_csi2_frame_t *frame, am_csi2_input_t* input)
{
    unsigned data1 = 0;
    unsigned data2 = 0;
    unsigned data3 = 0;
    bool ret = false;
    if(frame == NULL)
        return ret;
    frame->w = READ_CBUS_REG(CSI2_MEM_PIXEL_BYTE_CNT);
    frame->w = frame->w & 0xffff;
    frame->h = READ_CBUS_REG(CSI2_MEM_PIXEL_LINE_CNT);
    frame->err = READ_CBUS_REG(CSI2_ERR_STAT0);
    data1 = READ_CBUS_REG(CSI2_PIXEL_DDR_START);
    data2 = READ_CBUS_REG(CSI2_PIXEL_DDR_END);
    frame->read_cnt = (int)(data2-data1)+1;
    data2 = READ_CBUS_REG(CSI2_DATA_TYPE_IN_MEM);
    data3 = READ_CBUS_REG(CSI2_GEN_STAT0);

    if((frame->err)||((frame->w*frame->h)!=frame->read_cnt)){
        mipi_error("[mipi_mem]:checkframe error---pixel byte cnt:%d, line cnt:%d. error state:0x%x. address 0x%x,size:0x%x. mem type:0x%x, status:0x%x\n",
                      frame->w,frame->h,frame->err,data1,frame->read_cnt,data2,data3);
    }else{
        mipi_dbg("[mipi_mem]:checkframe---pixel byte cnt:%d, line cnt:%d. address 0x%x, size:0x%x. mem type:0x%x, status:0x%x\n",
                      frame->w,frame->h,data1,frame->read_cnt,data2,data3);
        ret = true;
    } 
    return ret;
}

static void csi2_mem_check_timer_func(unsigned long arg)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)arg;

    data->watchdog_cnt++;
    if(data->watchdog_cnt>10){
        data->reset_flag = 1;
        data->watchdog_cnt = 0;
        data->timer.expires = jiffies + CSI2_CHECK_INTERVAL* (1000/data->frame_rate)*5;
        add_timer(&data->timer);
        tasklet_hi_schedule(&data->isr_tasklet);
        mipi_error("[mipi_mem]:csi2_mem_check_timer_func-- time out !!!!\n");
        return;
    }
    data->timer.expires = jiffies + CSI2_CHECK_INTERVAL* (1000/data->frame_rate);
    add_timer(&data->timer);
    return;
}

#ifdef TEST_FRAME_RATE
static int time_count = 0;
#endif
static void csi2_mem_isr_tasklet(unsigned long arg)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)arg;
    am_csi2_frame_t* frame = NULL;
    u32 data32 = 0;
    unsigned char new_frame = 0;
    unsigned char reload = 0;

    spin_lock(&data->isr_lock);

    if(data->reset_flag){
        data->wr_frame = NULL;
        data->disable_ddr = 0;
        data->watchdog_cnt = 0;
        bufq_init(&data->in_buff,&(data->input.frame[0]),data->input.frame_available);
        bufq_init(&data->out_buff,&(data->output.frame[0]),data->output.frame_available);
        data->hw_info.frame = bufq_pop_free(&data->in_buff);
        if(data->hw_info.frame){
            data->mipi_mem_skip = MIPI_SKIP_FRAME_NUM;
            data->wr_frame = data->hw_info.frame;
            am_mipi_csi2_init(&data->hw_info);
            data->reset_flag = 0;
            mipi_error("[mipi_mem]:csi2_mem_isr_tasklet ---- reset mipi.\n");
        }else{
            mipi_error("[mipi_mem]:csi2_mem_isr_tasklet ---- reset mipi  but no free buff. \n");
        }
        spin_unlock(&data->isr_lock);
        return;
    }
#ifdef TEST_FRAME_RATE
    if(time_count==0){
        mipi_error("[mipi_mem]:csi2_mem_isr_tasklet ---- time start\n");
    }
    time_count++;
    if(time_count>49){
        time_count = 0;
    }
#endif
    if(data->wr_frame){
        if(data->mipi_mem_skip>0){
            data->mipi_mem_skip--;
            bufq_push_free(&data->in_buff, data->wr_frame);			
        }else if(checkframe(data->wr_frame,&data->input) == true){
            bufq_push_available(&data->in_buff, data->wr_frame);
            new_frame = 1;
        }else{
            //bufq_push_free(&data->in_buff, data->wr_frame);
            reload = 1;
        }
        if(!reload)
            data->wr_frame = NULL;
    }
    if(reload)
        frame = data->wr_frame;
    else
        frame = bufq_pop_free(&data->in_buff);

    if(frame){  //get next buff
        if(data->disable_ddr){
            data32 = READ_CBUS_REG(CSI2_GEN_CTRL0);
            data32 |= (1<<CSI2_CFG_DDR_EN);
            WRITE_CBUS_REG(CSI2_GEN_CTRL0, data32);
            data->disable_ddr = 0;
        }
        WRITE_CBUS_REG(CSI2_DDR_START_ADDR, frame->ddr_address);
        WRITE_CBUS_REG(CSI2_DDR_END_ADDR, frame->ddr_address+data->input.frame_size);
        data->wr_frame = frame;
    }else if(!data->disable_ddr){
        data32 = READ_CBUS_REG(CSI2_GEN_CTRL0);
        data32  &= ~(1<<CSI2_CFG_DDR_EN);
        WRITE_CBUS_REG(CSI2_GEN_CTRL0, data32);
        data->disable_ddr = 1;
        mipi_error("[mipi_mem]:csi2_mem_isr_tasklet---no free frame!\n");
    }else{
        mipi_error("[mipi_mem]:csi2_mem_isr_tasklet---no free frame and disable write ddr now!\n");
    }
    data->watchdog_cnt = 0;
    WRITE_CBUS_REG(CSI2_INTERRUPT_CTRL_STAT, (1 << CSI2_CFG_VS_FAIL_INTERRUPT_CLR) | (1 << CSI2_CFG_VS_FAIL_INTERRUPT));
    // Clear error flag
    WRITE_CBUS_REG(CSI2_ERR_STAT0, 0);
    if(new_frame){
#ifdef ENABLE_CACHE_MODE
        up(&data->start_sem);
#else
        if((!bufq_empty_available(&data->in_buff))&&(data->done_flag == false)){
            data->done_flag = true;
            wake_up_interruptible(&data->complete);
        }
#endif
    }
    spin_unlock(&data->isr_lock);
}

static irqreturn_t am_csi2_mem_irq(int irq, void *dev_id)
{
    ulong flags = 0, cur_irq_time =0;
    struct timeval now;
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev_id;

    spin_lock_irqsave(&data->isr_lock, flags);
    do_gettimeofday(&now);
    cur_irq_time = (now.tv_sec*1000) + (now.tv_usec/1000);

    if(time_after(cur_irq_time, data->pre_irq_time))
    {
        if((cur_irq_time - data->pre_irq_time) < 7)   //short time
        {
            data->pre_irq_time = cur_irq_time;
            spin_unlock_irqrestore(&data->isr_lock, flags);
            mipi_dbg("[mipi_mem]:am_csi2_mem_irq ---- irq too open.\n");
            return IRQ_HANDLED;
        }
    }
    data->pre_irq_time = cur_irq_time;
    tasklet_hi_schedule(&data->isr_tasklet);
    spin_unlock_irqrestore(&data->isr_lock, flags);
    return IRQ_HANDLED;
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
        mipi_error("[mipi_mem]:fill_buff_from_canvas---pionter error\n");
        return ret;
    }
	
    canvas_read(frame->index&0xff,&canvas_work_y);
    buffer_y_start = ioremap_wc(frame->ddr_address,output->frame_size);
    //buffer_y_start = ioremap_wc(canvas_work_y.addr,canvas_work_y.width*canvas_work_y.height);
	
    if(buffer_y_start == NULL) {
        mipi_error("[mipi_mem]:fill_buff_from_canvas---mapping buffer error\n");
        return ret;
    }
    mipi_dbg("[mipi_mem]:fill_buff_from_canvas:frame->ddr_address:0x%x,canvas y:0x%x--%dx%d\n",(unsigned long)frame->ddr_address,(unsigned long)canvas_work_y.addr,canvas_work_y.width,canvas_work_y.height);
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
        mipi_dbg("[mipi_mem]:fill_buff_from_canvas:canvas u:0x%x--%dx%d\n",(unsigned long)canvas_work_u.addr,canvas_work_u.width,canvas_work_u.height);
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

#ifdef ENABLE_CACHE_MODE
static int am_csi2_mem_task(void *data)
{
    mem_ops_privdata_t* ops_data = (mem_ops_privdata_t*)data;
    am_csi2_frame_t* in_frame = NULL, *out_frame = NULL;
    int ret = -1;

    memset(&(ops_data->ge2d_config),0,sizeof(config_para_ex_t));
    allow_signal(SIGTERM);

    while(down_interruptible(&ops_data->start_sem)==0) {
Again:
        if (kthread_should_stop())
            break;
        if((bufq_empty_available(&ops_data->in_buff))||(bufq_empty_free(&ops_data->out_buff))){
            continue;
        }
        if(!ops_data->reset_flag){
            in_frame = bufq_pop_available(&ops_data->in_buff);
            out_frame = bufq_pop_free(&ops_data->out_buff);
            if((in_frame)&&(out_frame)){
                ret = ge2d_process(ops_data,in_frame,out_frame,ops_data->context,&(ops_data->ge2d_config));
                if(ret)
                    bufq_push_free(&ops_data->out_buff, out_frame);
                else
                    bufq_push_available(&ops_data->out_buff, out_frame);
                bufq_push_free(&ops_data->in_buff, in_frame);
            }else{
                if(in_frame)
                    bufq_push_free(&ops_data->in_buff, in_frame);
                if(out_frame)
                    bufq_push_free(&ops_data->out_buff, out_frame);
            }
            if((!bufq_empty_available(&ops_data->out_buff))&&(ops_data->done_flag == false)){
                ops_data->done_flag = true;
                wake_up_interruptible(&ops_data->complete);
            }else{
                goto Again;
            }
        }
    }
    
    while(!kthread_should_stop()){
        msleep(10);
    }
    return 0;
}
#endif

static int am_csi2_mem_init(am_csi2_t* dev)
{
    int ret = -1;
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev->ops->privdata;

    if(dev->id>=dev->ops->data_num){
        mipi_error("[mipi_mem]:am_csi2_mem_init ---- id error.\n");
        goto err_irq;
    }

    data = &data[dev->id];
    data->dev_id = dev->id;

    if(dev->irq<0){
        mipi_error("[mipi_mem]:am_csi2_mem_init ---- irq invaild.\n");
        goto err_irq;
    }
    ret = request_irq(dev->irq, am_csi2_mem_irq, IRQF_SHARED, dev_name(&(dev->pdev->dev)), (void *)data);
    if (ret){
        ret =  -ENOENT;
        mipi_error("[mipi_mem]:am_csi2_mem_init ---- irq register error.\n");
        goto err_irq;
    }
    data->context = create_ge2d_work_queue();
    if(!data->context){
        ret =  -ENOENT;
        free_irq(data->irq,(void *)data);
        mipi_error("[mipi_mem]:am_csi2_mem_init ---- ge2d context register error.\n");
        goto err_irq;
    }
    data->irq = dev->irq;
    data->hw_info.lanes = dev->client->lanes;
    data->hw_info.channel = dev->client->channel;
    data->hw_info.ui_val = dev->ui_val;
    data->hw_info.hs_freq = dev->hs_freq;
    data->hw_info.clock_lane_mode = dev->clock_lane_mode;
    data->hw_info.mode = AM_CSI2_ALL_MEM;
    disable_irq(data->irq);

    mutex_init(&data->buf_lock);
    init_waitqueue_head (&data->complete);
    data->isr_lock = __SPIN_LOCK_UNLOCKED(data->isr_lock);
    data->in_buff.q_lock= __SPIN_LOCK_UNLOCKED(data->in_buff.q_lock);
    data->out_buff.q_lock= __SPIN_LOCK_UNLOCKED(data->out_buff.q_lock);
    tasklet_init(&data->isr_tasklet, csi2_mem_isr_tasklet, (unsigned long)data);
    tasklet_disable(&data->isr_tasklet);

    init_am_mipi_csi2_clock();
    ret = 0;
    mipi_dbg("[mipi_mem]:am_csi2_mem_init ok.\n");
err_irq:
    return ret;
}

static int am_csi2_mem_streamon(am_csi2_t* dev)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev->ops->privdata;
    int i = 0;

    data = &data[dev->id];
#ifdef ENABLE_CACHE_MODE
    sema_init(&data->start_sem,0);
    data->task = kthread_run(am_csi2_mem_task, (void*)data, dev->pdev->name);
    if(IS_ERR(data->task)){
        mipi_error("[mipi_mem]:am_csi2_mem_streamon ---- create task fail. \n");
        return -1;
    }
#endif

    data->hw_info.active_line = dev->input.active_line;
    data->hw_info.active_pixel= dev->input.active_pixel;
    data->hw_info.frame_size = dev->input.frame_size;
    data->hw_info.urgent = 1;

    memcpy(&data->input,&dev->input,sizeof(am_csi2_input_t));
    memcpy(&data->output,&dev->output,sizeof(am_csi2_output_t));
    data->output.vaddr = NULL;
    data->frame_rate = dev->frame_rate;
    data->mirror = dev->mirror;

    data->wr_frame = NULL;
    data->disable_ddr = 0;
    data->reset_flag = 0;
    data->done_flag = true;
    bufq_init(&data->in_buff,&(data->input.frame[0]),data->input.frame_available);
    bufq_init(&data->out_buff,&(data->output.frame[0]),data->output.frame_available);
    for(i = 0;i<data->output.frame_available;i++){
        data->output.frame[i].index = config_canvas_index(data->output.frame[i].ddr_address,data->output.fourcc,data->output.output_pixel,data->output.output_line,i);
        if(data->output.frame[i].index<0){
#ifdef ENABLE_CACHE_MODE
            if (data->task) {
                send_sig(SIGTERM, data->task, 1);
                kthread_stop(data->task);
                data->task = NULL;
            }
#endif
            mipi_error("[mipi_mem]:am_csi2_mem_streamon ---- canvas config error. \n");
            return -1;
        }
    }
    data->hw_info.frame = bufq_pop_free(&data->in_buff);
    if(!data->hw_info.frame){
#ifdef ENABLE_CACHE_MODE
        if (data->task) {
            send_sig(SIGTERM, data->task, 1);
            kthread_stop(data->task);
            data->task = NULL;
        }
#endif
        mipi_error("[mipi_mem]:am_csi2_mem_streamon ---- no free buff. \n");
        return -1;
    }
    data->mipi_mem_skip = MIPI_SKIP_FRAME_NUM;
    data->wr_frame = data->hw_info.frame;
    am_mipi_csi2_init(&data->hw_info);
    init_timer(&data->timer);
    data->timer.data = (ulong) data;
    data->timer.function = &csi2_mem_check_timer_func;
    data->timer.expires = jiffies + CSI2_CHECK_INTERVAL* (1000/data->frame_rate)*5;
    data->watchdog_cnt = 0;
    tasklet_enable(&data->isr_tasklet);
    data->pre_irq_time = jiffies;
    add_timer(&data->timer);    
    enable_irq(data->irq); 
    msleep(100);
    mipi_dbg("[mipi_mem]:am_csi2_mem_streamon ok.\n");
    return 0;
}

static int am_csi2_mem_streamoff(am_csi2_t* dev)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev->ops->privdata;

    data = &data[dev->id];
    mutex_lock(&data->buf_lock);
    //data->reset_flag = 1;
    //wake_up_interruptible(&data->complete);
#ifdef ENABLE_CACHE_MODE
    if (data->task) {
        send_sig(SIGTERM, data->task, 1);
        kthread_stop(data->task);
        data->task = NULL;
    }
#endif
    disable_irq_nosync(data->irq); 
    tasklet_disable_nosync(&data->isr_tasklet);
    del_timer(&data->timer);
    am_mipi_csi2_uninit();
    mutex_unlock(&data->buf_lock);
    mipi_dbg("[mipi_mem]:am_csi2_mem_streamoff ok.\n");
    return 0;
}

static int am_csi2_mem_fillbuff(am_csi2_t* dev)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev->ops->privdata;
    int ret = 0;
    am_csi2_frame_t* frame = NULL;
    unsigned long timeout = msecs_to_jiffies(500) + 1;
    data = &data[dev->id];

    mutex_lock(&data->buf_lock);
    data->output.vaddr = dev->output.vaddr;
    data->output.zoom= dev->output.zoom;
    data->output.angle= dev->output.angle;

    mipi_dbg("[mipi_mem]:am_csi2_mem_fillbuff. address:0x%x. size:%dx%d, depth:%d,zoom level:%d, angle:%d.\n",
		(u32)data->output.vaddr,data->output.output_pixel,data->output.output_line,data->output.depth,data->output.zoom,data->output.angle);

#ifdef ENABLE_CACHE_MODE
    if(bufq_empty_available(&data->out_buff)){
        up(&data->start_sem);
        data->done_flag = false;
        wait_event_interruptible_timeout(data->complete,(data->reset_flag)||(data->done_flag==true),timeout);
    }
    if(!data->reset_flag){
        frame = bufq_pop_available(&data->out_buff);
        if(frame){
            ret = fill_buff_from_canvas(frame,&(data->output));
            bufq_push_free(&data->out_buff, frame);
        }
    }
#else
    if(bufq_empty_available(&data->in_buff)){
        data->done_flag = false;
        wait_event_interruptible_timeout(data->complete,(data->reset_flag)||(data->done_flag==true),timeout);
    }
    if(!data->reset_flag){
        am_csi2_frame_t* temp_frame = NULL;
        frame= bufq_pop_available(&data->in_buff);
        if(frame){
            if(is_need_ge2d_process(data,frame))
                temp_frame = bufq_pop_free(&data->out_buff);
            if(temp_frame){
                mipi_dbg("[mipi_mem]:am_csi2_mem_fillbuff. ---need ge2d to pre process\n");
                memset(&(data->ge2d_config),0,sizeof(config_para_ex_t));
                ret = ge2d_process(data,frame,temp_frame,data->context,&(data->ge2d_config));
            }
            if(temp_frame){
                ret = fill_buff_from_canvas(temp_frame,&(data->output));
                bufq_push_free(&data->out_buff, temp_frame);
            }else{
                ret = sw_process(data,frame);
            }
            bufq_push_free(&data->in_buff, frame);
        }
    }
#endif

#ifdef ENABLE_CACHE_MODE
    up(&data->start_sem); 
#endif
    mutex_unlock(&data->buf_lock);
    return ret;
}

static int am_csi2_mem_uninit(am_csi2_t* dev)
{
    mem_ops_privdata_t* data = (mem_ops_privdata_t*)dev->ops->privdata;

    data = &data[dev->id];
    data->reset_flag = 1;
    wake_up_interruptible(&data->complete);
#ifdef ENABLE_CACHE_MODE
    if (data->task) {
        send_sig(SIGTERM, data->task, 1);
        kthread_stop(data->task);
        data->task = NULL;
    }
#endif
    if(data->context)
        destroy_ge2d_work_queue(data->context);
    mutex_destroy(&data->buf_lock);
    tasklet_kill(&data->isr_tasklet);
    free_irq(data->irq,(void *)data);
    am_mipi_csi2_uninit();
    mipi_dbg("[mipi_mem]:am_csi2_mem_uninit ok.\n");
    return 0;
}

