
#include <linux/memory.h>
#include "RGA_API.h"
#include "rga.h"
//#include "rga_angle.h"

#define IS_YUV_420(format) \
     ((format == RK_FORMAT_YCbCr_420_P) | (format == RK_FORMAT_YCbCr_420_SP) | \
      (format == RK_FORMAT_YCrCb_420_P) | (format == RK_FORMAT_YCrCb_420_SP))  

#define IS_YUV_422(format) \
     ((format == RK_FORMAT_YCbCr_422_P) | (format == RK_FORMAT_YCbCr_422_SP) | \
      (format == RK_FORMAT_YCrCb_422_P) | (format == RK_FORMAT_YCrCb_422_SP))   

#define IS_YUV(format) \
     ((format == RK_FORMAT_YCbCr_420_P) | (format == RK_FORMAT_YCbCr_420_SP) | \
      (format == RK_FORMAT_YCrCb_420_P) | (format == RK_FORMAT_YCrCb_420_SP) | \
      (format == RK_FORMAT_YCbCr_422_P) | (format == RK_FORMAT_YCbCr_422_SP) | \
      (format == RK_FORMAT_YCrCb_422_P) | (format == RK_FORMAT_YCrCb_422_SP))
            

extern rga_service_info rga_service;


void
matrix_cal(const struct rga_req *msg, TILE_INFO *tile)
{
    uint32_t x_time, y_time;
    uint64_t sina, cosa;

    int s_act_w, s_act_h, d_act_w, d_act_h;

    s_act_w = msg->src.act_w;
    s_act_h = msg->src.act_h;
    d_act_w = msg->dst.act_w;
    d_act_h = msg->dst.act_h;

    if (s_act_w == 1) s_act_w += 1;
    if (s_act_h == 1) s_act_h += 1;
    if (d_act_h == 1) d_act_h += 1;
    if (d_act_w == 1) d_act_w += 1;

    x_time = ((s_act_w - 1)<<16) / (d_act_w - 1);
    y_time = ((s_act_h - 1)<<16) / (d_act_h - 1);
    
    sina = msg->sina;
    cosa = msg->cosa;

    switch(msg->rotate_mode)
    {
        /* 16.16 x 16.16 */
        /* matrix[] is 64 bit wide */
        case 1 :
            tile->matrix[0] =  cosa*x_time;    
            tile->matrix[1] = -sina*y_time;      
            tile->matrix[2] =  sina*x_time;       
            tile->matrix[3] =  cosa*y_time;
            break;
        case 2 :
            tile->matrix[0] = -(x_time<<16);       
            tile->matrix[1] = 0;      
            tile->matrix[2] = 0;       
            tile->matrix[3] = (y_time<<16);
            break;
        case 3 :
            tile->matrix[0] = (x_time<<16);       
            tile->matrix[1] = 0;      
            tile->matrix[2] = 0;       
            tile->matrix[3] = -(y_time<<16);
            break;
        default :
            tile->matrix[0] =  (uint64_t)1<<32;       
            tile->matrix[1] =  0;      
            tile->matrix[2] =  0;       
            tile->matrix[3] =  (uint64_t)1<<32;
            break;            
    }    
}


uint32_t RGA_gen_two_pro(struct rga_req *msg, struct rga_req *msg1)
{
    
    struct rga_req *mp;
    uint32_t w_ratio, h_ratio;
    uint32_t stride;

    uint32_t daw, dah;
    uint32_t pl;

    daw = dah = 0;
            
    mp = msg1;
    w_ratio = (msg->src.act_w << 16) / msg->dst.act_w;
    h_ratio = (msg->src.act_h << 16) / msg->dst.act_h;
   
    memcpy(msg1, msg, sizeof(struct rga_req));

    msg->dst.format = msg->src.format;

    /*pre_scale_w cal*/
    if ((w_ratio >= (2<<16)) && (w_ratio < (4<<16))) {            
        daw = (msg->src.act_w + 1) >> 1;
        if((IS_YUV_420(msg->dst.format)) && (daw & 1)) {
            msg->src.act_w = (daw - 1) << 1;                                                    
        }        
    }
    else if ((w_ratio >= (4<<16)) && (w_ratio < (8<<16))) {
        daw = (msg->src.act_w + 3) >> 2;            
        if((IS_YUV_420(msg->dst.format)) && (daw & 1)) {
            msg->src.act_w = (daw - 1) << 2;                                                    
        }
    }
    else if ((w_ratio >= (8<<16)) && (w_ratio < (16<<16))) {
        daw = (msg->src.act_w + 7) >> 3;
        if((IS_YUV_420(msg->dst.format)) && (daw & 1)) {
            msg->src.act_w = (daw - 1) << 3;                                                    
        }
    }

    pl = (RGA_pixel_width_init(msg->src.format));
    stride = (pl * daw + 3) & (~3);
    msg->dst.act_w = daw;
    msg->dst.vir_w = stride / pl;

    /*pre_scale_h cal*/        
    if ((h_ratio >= (2<<16)) && (h_ratio < (4<<16))) {            
        dah = (msg->src.act_h + 1) >> 1;            
        if((IS_YUV(msg->dst.format)) && (dah & 1)) {
            msg->src.act_h = (dah - 1) << 1;                                                    
        }            
    }
    else if ((h_ratio >= (4<<16)) && (h_ratio < (8<<16))) {
        dah = (msg->src.act_h + 3) >> 2;            
        if((IS_YUV(msg->dst.format)) && (dah & 1)) {
            msg->src.act_h = (dah - 1) << 2;                                                    
        }
    }
    else if ((h_ratio >= (8<<16)) && (h_ratio < (16<<16))) {
        dah = (msg->src.act_h + 7) >> 3;
        if((IS_YUV(msg->dst.format)) && (dah & 1)) {
            msg->src.act_h = (dah - 1) << 3;                                                    
        }
    }

    printk("test_2\n");
    
    msg->dst.act_h = dah;
    msg->dst.vir_h = dah;
            
    //msg->dst.yrgb_addr = (u32)rga_service.pre_scale_buf;
    msg->dst.uv_addr = msg->dst.yrgb_addr + stride * dah;
    msg->dst.v_addr = msg->dst.uv_addr + ((stride * dah) >> 1);

    msg->render_mode = pre_scaling_mode;

    msg1->src.yrgb_addr = msg->dst.yrgb_addr;
    msg1->src.uv_addr = msg->dst.uv_addr;
    msg1->src.v_addr = msg->dst.v_addr;

    msg1->src.act_w = msg->dst.act_w;
    msg1->src.act_h = msg->dst.act_h;
    msg1->src.vir_w = msg->dst.vir_w;
    msg1->src.vir_h = msg->dst.vir_h;
            
    return 0;
}


