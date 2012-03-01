
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




u32
RGA_dst_act_addr_temp(const struct rga_req *msg)
{
    uint32_t pw;
    uint32_t x_off, y_off;
    uint32_t stride;
    uint32_t p;
    
    pw = RGA_pixel_width_init(msg->dst.format);
    stride = (msg->dst.vir_w * pw + 3) & (~3);    
    
    x_off = msg->dst.x_offset;
    y_off = msg->dst.y_offset;

    p = (u32)((stride * y_off) + (x_off * pw));

    return p;
}

void
RGA_set_cmd_info(uint8_t cmd_mode, uint32_t cmd_addr)
{
    uint32_t reg = 0;
    
    reg |= ((cmd_mode & 1) << 1);
    rRGA_SYS_CTRL = reg; 
    rRGA_CMD_ADDR = cmd_addr;
}

void
RGA_start(void) 
{
    uint32_t reg = 0;
    uint8_t  cmd_mode;
    
    reg = rRGA_SYS_CTRL;
    cmd_mode = (reg >> 2) & 1;
    
    if (cmd_mode == 0)
    {
        /* passive */
        reg |= (1<<1);
        rRGA_SYS_CTRL = reg;       
    }
    else
    {
        /* master */
        reg = rRGA_CMD_CTRL;
        reg |= 1;
        rRGA_CMD_CTRL = reg;        
    }    
}


void
RGA_soft_reset(void)
{
    uint32_t reg = 0;

    reg = rRGA_SYS_CTRL;
    reg |= 1;
    rRGA_SYS_CTRL = reg;       
}


#if 0
/*****************************************/
//hxx add,2011.6.24
void rga_one_op_st_master(RGA_INFO *p_rga_info)
{
	rRGA_SYS_CTRL = 0x4;
	
	rRGA_INT = s_RGA_INT_ALL_CMD_DONE_INT_EN(p_rga_info->int_info.all_cmd_done_int_en)|
	           s_RGA_INT_MMU_INT_EN(p_rga_info->int_info.mmu_int_en)|
	           s_RGA_INT_ERROR_INT_EN(p_rga_info->int_info.error_int_en);
	
	rRGA_CMD_ADDR = (u32) p_rga_info->sys_info.p_cmd_mst;
	
	rRGA_CMD_CTRL = 0x3;
}


void rga_set_int_info(MSG *p_msg,RGA_INFO *p_rga_info)
{
	p_msg->CMD_fin_int_enable = p_rga_info->int_info.cur_cmd_done_int_en;
}


void rga_check_int_all_cmd_finish(RGA_INFO *p_rga_info)
{
	u8 int_flag;
	
	int_flag = 0;
	while(!int_flag)
	{
		int_flag = rRGA_INT & m_RGA_INT_ALL_CMD_DONE_INT_FLAG;
	}
	rRGA_INT = rRGA_INT | s_RGA_INT_ALL_CMD_DONE_INT_CLEAR(0x1);
	
    //if(p_rga_info->sys_info.p_cmd_mst != NULL)
    //	free(p_rga_info->sys_info.p_cmd_mst);
}
#endif

void rga_start_cmd_AXI(uint8_t *base, uint32_t num)
{
    rRGA_SYS_CTRL = 0x4;
    rRGA_INT = s_RGA_INT_ALL_CMD_DONE_INT_EN(ENABLE)| s_RGA_INT_MMU_INT_EN(ENABLE)| s_RGA_INT_ERROR_INT_EN(ENABLE);
    rRGA_CMD_ADDR = (u32)base;          
    rRGA_CMD_CTRL |= (num<<3);                
    rRGA_CMD_CTRL |= 0x3;
}

void rga_check_cmd_finish(void)
{
	uint8_t int_flag;
    uint8_t error_flag;
	
	int_flag = 0;
    error_flag = 0;
    
	while(!int_flag)
	{
		int_flag = rRGA_INT & m_RGA_INT_ALL_CMD_DONE_INT_FLAG;
        error_flag = rRGA_INT & (m_RGA_INT_ERROR_INT_FLAG);

        if(error_flag)
        {            
            printk("~~~~~ ERROR INTTUR OCCUR ~~~~~\n");
        }
        error_flag = rRGA_INT & m_RGA_INT_MMU_INT_FLAG;
        if(error_flag)
        {            
            printk("~~~~~ MMU ERROR INTTUR OCCUR ~~~~~\n");
        }
	}
	rRGA_INT = rRGA_INT | s_RGA_INT_ALL_CMD_DONE_INT_CLEAR(0x1);	
}



void rga_start_cmd_AHB(uint8_t *base)
{
    uint32_t *base_p32;
    
    base_p32 = (u32 *)base;
    *base_p32 = (*base_p32 | (1<<29));

    memcpy((u8 *)(RGA_BASE + 0x100), base, 28*4);
   
    rRGA_SYS_CTRL = 0x2;
}


void rga_check_cmd_AHB_finish(void)
{
	uint8_t int_flag;
	
	int_flag = 0;
	while(!int_flag)
	{
		int_flag = rRGA_INT & m_RGA_INT_NOW_CMD_DONE_INT_FLAG;
	}
	rRGA_INT = rRGA_INT | s_RGA_INT_NOW_CMD_DONE_INT_CLEAR(0x1);
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

    memcpy(&mp->src, &msg->src, sizeof(rga_img_info_t));

    mp->dst.format = msg->src.format;

    /*pre_scale_w cal*/
    if ((w_ratio >= (2<<16)) && (w_ratio < (4<<16))) {            
        daw = (msg->src.act_w + 1) >> 1;
        if((IS_YUV_420(mp->dst.format)) && (daw & 1)) {
            mp->src.act_w = (daw - 1) << 1;                                                    
        }        
    }
    else if ((w_ratio >= (4<<16)) && (w_ratio < (8<<16))) {
        daw = (msg->src.act_w + 3) >> 2;            
        if((IS_YUV_420(mp->dst.format)) && (daw & 1)) {
            mp->src.act_w = (daw - 1) << 2;                                                    
        }
    }
    else if ((w_ratio >= (8<<16)) && (w_ratio < (16<<16))) {
        daw = (msg->src.act_w + 7) >> 3;
        if((IS_YUV_420(mp->dst.format)) && (daw & 1)) {
            mp->src.act_w = (daw - 1) << 3;                                                    
        }
    }

    pl = (RGA_pixel_width_init(msg->src.format));
    stride = (pl * daw + 3) & (~3);
    mp->dst.act_w = daw;
    mp->dst.vir_w = stride / pl;

    /*pre_scale_h cal*/        
    if ((h_ratio >= (2<<16)) && (h_ratio < (4<<16))) {            
        dah = (msg->src.act_h + 1) >> 1;            
        if((IS_YUV(mp->dst.format)) && (dah & 1)) {
            mp->src.act_h = (dah - 1) << 1;                                                    
        }            
    }
    else if ((h_ratio >= (4<<16)) && (h_ratio < (8<<16))) {
        dah = (msg->src.act_h + 3) >> 2;            
        if((IS_YUV(mp->dst.format)) && (dah & 1)) {
            mp->src.act_h = (dah - 1) << 2;                                                    
        }
    }
    else if ((h_ratio >= (8<<16)) && (h_ratio < (16<<16))) {
        dah = (msg->src.act_h + 7) >> 3;
        if((IS_YUV(mp->dst.format)) && (dah & 1)) {
            mp->src.act_h = (dah - 1) << 3;                                                    
        }
    }
    mp->dst.act_h = dah;
    mp->dst.vir_h = dah;
            
    mp->dst.yrgb_addr = (u32)rga_service.pre_scale_buf;
    mp->dst.uv_addr = mp->dst.yrgb_addr + stride * dah;
    mp->dst.v_addr = mp->dst.uv_addr + ((stride * dah) >> 1);

    mp->render_mode = pre_scaling_mode;

    memcpy(&msg->src, &mp->dst, sizeof(rga_img_info_t));
        
    return 0;
}


