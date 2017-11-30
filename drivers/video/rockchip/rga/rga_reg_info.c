
//#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
//#include <mach/io.h>
//#include <mach/irqs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <asm/cacheflush.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/wakelock.h>

#include "rga_reg_info.h"
#include "rga_rop.h"
#include "rga.h"


/*************************************************************
Func:
    RGA_pixel_width_init
Description:
    select pixel_width form data format
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/
unsigned char
RGA_pixel_width_init(unsigned int format)
{
    unsigned char pixel_width;

    pixel_width = 0;

    switch(format)
    {
        /* RGB FORMAT */
        case RK_FORMAT_RGBA_8888 :   pixel_width = 4;   break;
        case RK_FORMAT_RGBX_8888 :   pixel_width = 4;   break;
        case RK_FORMAT_RGB_888   :   pixel_width = 3;   break;
        case RK_FORMAT_BGRA_8888 :   pixel_width = 4;   break;
        case RK_FORMAT_RGB_565   :   pixel_width = 2;   break;
        case RK_FORMAT_RGBA_5551 :   pixel_width = 2;   break;
        case RK_FORMAT_RGBA_4444 :   pixel_width = 2;   break;
        case RK_FORMAT_BGR_888   :   pixel_width = 3;   break;

        /* YUV FORMAT */
        case RK_FORMAT_YCbCr_422_SP :   pixel_width = 1;  break;
        case RK_FORMAT_YCbCr_422_P  :   pixel_width = 1;  break;
        case RK_FORMAT_YCbCr_420_SP :   pixel_width = 1;  break;
        case RK_FORMAT_YCbCr_420_P  :   pixel_width = 1;  break;
        case RK_FORMAT_YCrCb_422_SP :   pixel_width = 1;  break;
        case RK_FORMAT_YCrCb_422_P  :   pixel_width = 1;  break;
        case RK_FORMAT_YCrCb_420_SP :   pixel_width = 1;  break;
        case RK_FORMAT_YCrCb_420_P :    pixel_width = 1;  break;
        //case default :                  pixel_width = 0;  break;
    }

    return pixel_width;
}

/*************************************************************
Func:
    dst_ctrl_cal
Description:
    calculate dst act window position / width / height
    and set the tile struct
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/
void
dst_ctrl_cal(const struct rga_req *msg, TILE_INFO *tile)
{
    u32 width   = msg->dst.act_w;
    u32 height  = msg->dst.act_h;
    s32 xoff    = msg->dst.x_offset;
    s32 yoff    = msg->dst.y_offset;

    s32 x0, y0, x1, y1, x2, y2;
    s32 x00,y00,x10,y10,x20,y20;
    s32 xx, xy, yx, yy;
    s32 pos[8];

    s32 xmax, xmin, ymax, ymin;

    s32 sina = msg->sina; /* 16.16 */
    s32 cosa = msg->cosa; /* 16.16 */

    xmax = xmin = ymax = ymin = 0;

    if((msg->rotate_mode == 0)||(msg->rotate_mode == 2)||(msg->rotate_mode == 3))
    {
        pos[0] = xoff;
        pos[1] = yoff;

        pos[2] = xoff;
        pos[3] = yoff + height - 1;

        pos[4] = xoff + width - 1;
        pos[5] = yoff + height - 1;

        pos[6] = xoff + width - 1;
        pos[7] = yoff;

        xmax = MIN(MAX(MAX(MAX(pos[0], pos[2]), pos[4]), pos[6]), msg->clip.xmax);
        xmin = MAX(MIN(MIN(MIN(pos[0], pos[2]), pos[4]), pos[6]), msg->clip.xmin);

        ymax = MIN(MAX(MAX(MAX(pos[1], pos[3]), pos[5]), pos[7]), msg->clip.ymax);
        ymin = MAX(MIN(MIN(MIN(pos[1], pos[3]), pos[5]), pos[7]), msg->clip.ymin);

        //printk("xmax = %d, xmin = %d, ymin = %d, ymax = %d\n", xmax, xmin, ymin, ymax);
    }
    else if(msg->rotate_mode == 1)
    {
        if((sina == 0) || (cosa == 0))
        {
            if((sina == 0) && (cosa == -65536))
            {
                /* 180 */
                pos[0] = xoff - width + 1;
                pos[1] = yoff - height + 1;

                pos[2] = xoff - width  + 1;
                pos[3] = yoff;

                pos[4] = xoff;
                pos[5] = yoff;

                pos[6] = xoff;
                pos[7] = yoff - height + 1;
            }
            else if((cosa == 0)&&(sina == 65536))
            {
                /* 90 */
                pos[0] = xoff - height + 1;
                pos[1] = yoff;

                pos[2] = xoff - height + 1;
                pos[3] = yoff + width - 1;

                pos[4] = xoff;
                pos[5] = yoff + width - 1;

                pos[6] = xoff;
                pos[7] = yoff;
            }
            else if((cosa == 0)&&(sina == -65536))
            {
                /* 270 */
                pos[0] = xoff;
                pos[1] = yoff - width + 1;

                pos[2] = xoff;
                pos[3] = yoff;

                pos[4] = xoff + height - 1;
                pos[5] = yoff;

                pos[6] = xoff + height - 1;
                pos[7] = yoff - width + 1;
            }
            else
            {
                /* 0 */
                pos[0] = xoff;
                pos[1] = yoff;

                pos[2] = xoff;
                pos[3] = yoff + height - 1;

                pos[4] = xoff + width - 1;
                pos[5] = yoff + height - 1;

                pos[6] = xoff + width - 1;
                pos[7] = yoff;
            }

            xmax = MIN(MAX(MAX(MAX(pos[0], pos[2]), pos[4]), pos[6]), msg->clip.xmax);
            xmin = MAX(MIN(MIN(MIN(pos[0], pos[2]), pos[4]), pos[6]), msg->clip.xmin);

            ymax = MIN(MAX(MAX(MAX(pos[1], pos[3]), pos[5]), pos[7]), msg->clip.ymax);
            ymin = MAX(MIN(MIN(MIN(pos[1], pos[3]), pos[5]), pos[7]), msg->clip.ymin);
        }
        else
        {
            xx = msg->cosa;
            xy = msg->sina;
            yx = xy;
            yy = xx;

            x0 = width + xoff;
            y0 = yoff;

            x1 = xoff;
            y1 = height + yoff;

            x2 = width + xoff;
            y2 = height + yoff;

            pos[0] = xoff;
            pos[1] = yoff;

            pos[2] = x00 = (((x0 - xoff)*xx - (y0 - yoff)*xy)>>16) + xoff;
            pos[3] = y00 = (((x0 - xoff)*yx + (y0 - yoff)*yy)>>16) + yoff;

            pos[4] = x10 = (((x1 - xoff)*xx - (y1 - yoff)*xy)>>16) + xoff;
            pos[5] = y10 = (((x1 - xoff)*yx + (y1 - yoff)*yy)>>16) + yoff;

            pos[6] = x20 = (((x2 - xoff)*xx - (y2 - yoff)*xy)>>16) + xoff;
            pos[7] = y20 = (((x2 - xoff)*yx + (y2 - yoff)*yy)>>16) + yoff;

            xmax = MAX(MAX(MAX(x00, xoff), x10), x20) + 2;
            xmin = MIN(MIN(MIN(x00, xoff), x10), x20) - 1;

            ymax = MAX(MAX(MAX(y00, yoff), y10), y20) + 2;
            ymin = MIN(MIN(MIN(y00, yoff), y10), y20) - 1;

            xmax = MIN(xmax, msg->clip.xmax);
            xmin = MAX(xmin, msg->clip.xmin);

            ymax = MIN(ymax, msg->clip.ymax);
            ymin = MAX(ymin, msg->clip.ymin);

            //printk("xmin = %d, xmax = %d, ymin = %d, ymax = %d\n", xmin, xmax, ymin, ymax);
        }
    }

    if ((xmax < xmin) || (ymax < ymin)) {
        xmin = xmax;
        ymin = ymax;
    }

    if ((xmin >= msg->dst.vir_w)||(xmax < 0)||(ymin >= msg->dst.vir_h)||(ymax < 0)) {
        xmin = xmax = ymin = ymax = 0;
    }

    //printk("xmin = %d, xmax = %d, ymin = %d, ymax = %d\n", xmin, xmax, ymin, ymax);

    tile->dst_ctrl.w = (xmax - xmin);
    tile->dst_ctrl.h = (ymax - ymin);
    tile->dst_ctrl.x_off = xmin;
    tile->dst_ctrl.y_off = ymin;

    //printk("tile->dst_ctrl.w = %x, tile->dst_ctrl.h = %x\n", tile->dst_ctrl.w, tile->dst_ctrl.h);

    tile->tile_x_num = (xmax - xmin + 1 + 7)>>3;
    tile->tile_y_num = (ymax - ymin + 1 + 7)>>3;

    tile->dst_x_tmp = xmin - msg->dst.x_offset;
    tile->dst_y_tmp = ymin - msg->dst.y_offset;
}

/*************************************************************
Func:
    src_tile_info_cal
Description:
    calculate src remap window position / width / height
    and set the tile struct
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
src_tile_info_cal(const struct rga_req *msg, TILE_INFO *tile)
{
    s32 x0, x1, x2, x3, y0, y1, y2, y3;

    int64_t xx, xy, yx, yy;

    int64_t pos[8];
    int64_t epos[8];

    int64_t x_dx, x_dy, y_dx, y_dy;
    int64_t x_temp_start, y_temp_start;
    int64_t xmax, xmin, ymax, ymin;

    int64_t t_xoff, t_yoff;

    xx = tile->matrix[0]; /* 32.32 */
    xy = tile->matrix[1]; /* 32.32 */
    yx = tile->matrix[2]; /* 32.32 */
    yy = tile->matrix[3]; /* 32.32 */

    if(msg->rotate_mode == 1)
    {
        x0 = tile->dst_x_tmp;
        y0 = tile->dst_y_tmp;

        x1 = x0;
        y1 = y0 + 8;

        x2 = x0 + 8;
        y2 = y0 + 8;

        x3 = x0 + 8;
        y3 = y0;

        pos[0] = (x0*xx + y0*yx);
        pos[1] = (x0*xy + y0*yy);

        pos[2] = (x1*xx + y1*yx);
        pos[3] = (x1*xy + y1*yy);

        pos[4] = (x2*xx + y2*yx);
        pos[5] = (x2*xy + y2*yy);

        pos[6] = (x3*xx + y3*yx);
        pos[7] = (x3*xy + y3*yy);

        y1 = y0 + 7;
        x2 = x0 + 7;
        y2 = y0 + 7;
        x3 = x0 + 7;

        epos[0] = pos[0];
        epos[1] = pos[1];

        epos[2] = (x1*xx + y1*yx);
        epos[3] = (x1*xy + y1*yy);

        epos[4] = (x2*xx + y2*yx);
        epos[5] = (x2*xy + y2*yy);

        epos[6] = (x3*xx + y3*yx);
        epos[7] = (x3*xy + y3*yy);

        x_dx = pos[6] - pos[0];
        x_dy = pos[7] - pos[1];

        y_dx = pos[2] - pos[0];
        y_dy = pos[3] - pos[1];

        tile->x_dx = (s32)(x_dx >> 22 );
        tile->x_dy = (s32)(x_dy >> 22 );
        tile->y_dx = (s32)(y_dx >> 22 );
        tile->y_dy = (s32)(y_dy >> 22 );

        x_temp_start = x0*xx + y0*yx;
        y_temp_start = x0*xy + y0*yy;

        xmax = (MAX(MAX(MAX(epos[0], epos[2]), epos[4]), epos[6]));
        xmin = (MIN(MIN(MIN(epos[0], epos[2]), epos[4]), epos[6]));

        ymax = (MAX(MAX(MAX(epos[1], epos[3]), epos[5]), epos[7]));
        ymin = (MIN(MIN(MIN(epos[1], epos[3]), epos[5]), epos[7]));

        t_xoff = (x_temp_start - xmin)>>18;
        t_yoff = (y_temp_start - ymin)>>18;

        tile->tile_xoff = (s32)t_xoff;
        tile->tile_yoff = (s32)t_yoff;

        tile->tile_w = (u16)((xmax - xmin)>>21); //.11
        tile->tile_h = (u16)((ymax - ymin)>>21); //.11

        tile->tile_start_x_coor = (s16)(xmin>>29); //.3
        tile->tile_start_y_coor = (s16)(ymin>>29); //.3
    }
    else if (msg->rotate_mode == 2)
    {
        tile->x_dx = (s32)((8*xx)>>22);
        tile->x_dy = 0;
        tile->y_dx = 0;
        tile->y_dy = (s32)((8*yy)>>22);

        tile->tile_w = ABS((s32)((7*xx)>>21));
        tile->tile_h = ABS((s32)((7*yy)>>21));

        tile->tile_xoff = ABS((s32)((7*xx)>>18));
        tile->tile_yoff = 0;

        tile->tile_start_x_coor = (((msg->src.act_w - 1)<<11) - (tile->tile_w))>>8;
        tile->tile_start_y_coor = 0;
    }
    else if (msg->rotate_mode == 3)
    {
        tile->x_dx = (s32)((8*xx)>>22);
        tile->x_dy = 0;
        tile->y_dx = 0;
        tile->y_dy = (s32)((8*yy)>>22);

        tile->tile_w = ABS((s32)((7*xx)>>21));
        tile->tile_h = ABS((s32)((7*yy)>>21));

        tile->tile_xoff = 0;
        tile->tile_yoff = ABS((s32)((7*yy)>>18));

        tile->tile_start_x_coor = 0;
        tile->tile_start_y_coor = (((msg->src.act_h - 1)<<11) - (tile->tile_h))>>8;
    }

    if ((msg->scale_mode == 2)||(msg->alpha_rop_flag >> 7))
    {
        tile->tile_start_x_coor -= (1<<3);
        tile->tile_start_y_coor -= (1<<3);
        tile->tile_w += (2 << 11);
        tile->tile_h += (2 << 11);
        tile->tile_xoff += (1<<14);
        tile->tile_yoff += (1<<14);
    }
}


/*************************************************************
Func:
    RGA_set_mode_ctrl
Description:
    fill mode ctrl reg info
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
RGA_set_mode_ctrl(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_MODE_CTL;
    u32 reg = 0;

    u8 src_rgb_pack = 0;
    u8 src_format = 0;
    u8 src_rb_swp = 0;
    u8 src_a_swp = 0;
    u8 src_cbcr_swp = 0;

    u8 dst_rgb_pack = 0;
    u8 dst_format = 0;
    u8 dst_rb_swp = 0;
    u8 dst_a_swp = 0;

    bRGA_MODE_CTL = (u32 *)(base + RGA_MODE_CTRL_OFFSET);

    reg = ((reg & (~m_RGA_MODE_CTRL_2D_RENDER_MODE)) | (s_RGA_MODE_CTRL_2D_RENDER_MODE(msg->render_mode)));

    /* src info set */

    if (msg->render_mode == color_palette_mode || msg->render_mode == update_palette_table_mode)
    {
        src_format = 0x10 | (msg->palette_mode & 3);
    }
    else
    {
        switch (msg->src.format)
        {
            case RK_FORMAT_RGBA_8888    : src_format = 0x0; break;
            case RK_FORMAT_RGBA_4444    : src_format = 0x3; break;
            case RK_FORMAT_RGBA_5551    : src_format = 0x2; break;
            case RK_FORMAT_BGRA_8888    : src_format = 0x0; src_rb_swp = 0x1; break;
            case RK_FORMAT_RGBX_8888    : src_format = 0x0; break;
            case RK_FORMAT_RGB_565      : src_format = 0x1; break;
            case RK_FORMAT_RGB_888      : src_format = 0x0; src_rgb_pack = 1; break;
            case RK_FORMAT_BGR_888      : src_format = 0x0; src_rgb_pack = 1; src_rb_swp = 1; break;

            case RK_FORMAT_YCbCr_422_SP : src_format = 0x4; break;
            case RK_FORMAT_YCbCr_422_P  : src_format = 0x5; break;
            case RK_FORMAT_YCbCr_420_SP : src_format = 0x6; break;
            case RK_FORMAT_YCbCr_420_P  : src_format = 0x7; break;

            case RK_FORMAT_YCrCb_422_SP : src_format = 0x4; src_cbcr_swp = 1; break;
            case RK_FORMAT_YCrCb_422_P  : src_format = 0x5; src_cbcr_swp = 1; break;
            case RK_FORMAT_YCrCb_420_SP : src_format = 0x6; src_cbcr_swp = 1; break;
            case RK_FORMAT_YCrCb_420_P  : src_format = 0x7; src_cbcr_swp = 1; break;
        }
    }

    src_a_swp = msg->src.alpha_swap & 1;

    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_RGB_PACK))      | (s_RGA_MODE_CTRL_SRC_RGB_PACK(src_rgb_pack)));
    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_FORMAT))        | (s_RGA_MODE_CTRL_SRC_FORMAT(src_format)));
    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_RB_SWAP))       | (s_RGA_MODE_CTRL_SRC_RB_SWAP(src_rb_swp)));
    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_ALPHA_SWAP))    | (s_RGA_MODE_CTRL_SRC_ALPHA_SWAP(src_a_swp)));
    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_UV_SWAP_MODE )) | (s_RGA_MODE_CTRL_SRC_UV_SWAP_MODE (src_cbcr_swp)));


    /* YUV2RGB MODE */
    reg = ((reg & (~m_RGA_MODE_CTRL_YUV2RGB_CON_MODE)) | (s_RGA_MODE_CTRL_YUV2RGB_CON_MODE(msg->yuv2rgb_mode)));

    /* ROTATE MODE */
    reg = ((reg & (~m_RGA_MODE_CTRL_ROTATE_MODE)) | (s_RGA_MODE_CTRL_ROTATE_MODE(msg->rotate_mode)));

    /* SCALE MODE */
    reg = ((reg & (~m_RGA_MODE_CTRL_SCALE_MODE)) | (s_RGA_MODE_CTRL_SCALE_MODE(msg->scale_mode)));

    /* COLOR FILL MODE */
    reg = ((reg & (~m_RGA_MODE_CTRL_PAT_SEL)) | (s_RGA_MODE_CTRL_PAT_SEL(msg->color_fill_mode)));


    if ((msg->render_mode == update_palette_table_mode)||(msg->render_mode == update_patten_buff_mode))
    {
        dst_format = msg->pat.format;
    }
    else
    {
        dst_format = (u8)msg->dst.format;
    }

    /* dst info set */
    switch (dst_format)
    {
        case RK_FORMAT_BGRA_8888 : dst_format = 0x0; dst_rb_swp = 0x1; break;
        case RK_FORMAT_RGBA_4444 : dst_format = 0x3; break;
        case RK_FORMAT_RGBA_5551 : dst_format = 0x2; break;
        case RK_FORMAT_RGBA_8888 : dst_format = 0x0; break;
        case RK_FORMAT_RGB_565   : dst_format = 0x1; break;
        case RK_FORMAT_RGB_888   : dst_format = 0x0; dst_rgb_pack = 0x1; break;
        case RK_FORMAT_BGR_888   : dst_format = 0x0; dst_rgb_pack = 0x1; dst_rb_swp = 1; break;
        case RK_FORMAT_RGBX_8888 : dst_format = 0x0; break;
    }

    dst_a_swp = msg->dst.alpha_swap & 1;

    reg = ((reg & (~m_RGA_MODE_CTRL_DST_FORMAT))       | (s_RGA_MODE_CTRL_DST_FORMAT(dst_format)));
    reg = ((reg & (~m_RGA_MODE_CTRL_DST_RGB_PACK))     | (s_RGA_MODE_CTRL_DST_RGB_PACK(dst_rgb_pack)));
    reg = ((reg & (~m_RGA_MODE_CTRL_DST_RB_SWAP))      | (s_RGA_MODE_CTRL_DST_RB_SWAP(dst_rb_swp)));
    reg = ((reg & (~m_RGA_MODE_CTRL_DST_ALPHA_SWAP))   | (s_RGA_MODE_CTRL_DST_ALPHA_SWAP(dst_a_swp)));
    reg = ((reg & (~m_RGA_MODE_CTRL_LUT_ENDIAN_MODE))  | (s_RGA_MODE_CTRL_LUT_ENDIAN_MODE(msg->endian_mode & 1)));
    reg = ((reg & (~m_RGA_MODE_CTRL_SRC_TRANS_MODE))   | (s_RGA_MODE_CTRL_SRC_TRANS_MODE(msg->src_trans_mode)));
    reg = ((reg & (~m_RGA_MODE_CTRL_ZERO_MODE_ENABLE)) | (s_RGA_MODE_CTRL_ZERO_MODE_ENABLE(msg->alpha_rop_mode >> 4)));
    reg = ((reg & (~m_RGA_MODE_CTRL_DST_ALPHA_ENABLE)) | (s_RGA_MODE_CTRL_DST_ALPHA_ENABLE(msg->alpha_rop_mode >> 5)));

    *bRGA_MODE_CTL = reg;

}



/*************************************************************
Func:
    RGA_set_src
Description:
    fill src relate reg info
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
RGA_set_src(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_SRC_VIR_INFO;
    u32 *bRGA_SRC_ACT_INFO;
    u32 *bRGA_SRC_Y_MST;
    u32 *bRGA_SRC_CB_MST;
    u32 *bRGA_SRC_CR_MST;

    s16 x_off, y_off, stride;
    s16 uv_x_off, uv_y_off, uv_stride;
    u32 pixel_width;

    uv_x_off = uv_y_off = uv_stride = 0;

    bRGA_SRC_Y_MST = (u32 *)(base + RGA_SRC_Y_MST_OFFSET);
    bRGA_SRC_CB_MST = (u32 *)(base + RGA_SRC_CB_MST_OFFSET);
    bRGA_SRC_CR_MST = (u32 *)(base + RGA_SRC_CR_MST_OFFSET);
    bRGA_SRC_VIR_INFO = (u32 *)(base + RGA_SRC_VIR_INFO_OFFSET);
    bRGA_SRC_ACT_INFO = (u32 *)(base + RGA_SRC_ACT_INFO_OFFSET);

    x_off  = msg->src.x_offset;
    y_off  = msg->src.y_offset;

    pixel_width = RGA_pixel_width_init(msg->src.format);

    stride = ((msg->src.vir_w * pixel_width) + 3) & (~3);

    switch(msg->src.format)
    {
        case RK_FORMAT_YCbCr_422_SP :
            uv_stride = stride;
            uv_x_off = x_off;
            uv_y_off = y_off;
            break;
        case RK_FORMAT_YCbCr_422_P  :
            uv_stride = stride >> 1;
            uv_x_off = x_off >> 1;
            uv_y_off = y_off;
            break;
        case RK_FORMAT_YCbCr_420_SP :
            uv_stride = stride;
            uv_x_off = x_off;
            uv_y_off = y_off >> 1;
            break;
        case RK_FORMAT_YCbCr_420_P :
            uv_stride = stride >> 1;
            uv_x_off = x_off >> 1;
            uv_y_off = y_off >> 1;
            break;
        case RK_FORMAT_YCrCb_422_SP :
            uv_stride = stride;
            uv_x_off = x_off;
            uv_y_off = y_off;
            break;
        case RK_FORMAT_YCrCb_422_P  :
            uv_stride = stride >> 1;
            uv_x_off = x_off >> 1;
            uv_y_off = y_off;
            break;
        case RK_FORMAT_YCrCb_420_SP :
            uv_stride = stride;
            uv_x_off = x_off;
            uv_y_off = y_off >> 1;
            break;
        case RK_FORMAT_YCrCb_420_P :
            uv_stride = stride >> 1;
            uv_x_off = x_off >> 1;
            uv_y_off = y_off >> 1;
            break;
    }


    /* src addr set */
    *bRGA_SRC_Y_MST = msg->src.yrgb_addr + (y_off * stride) + (x_off * pixel_width);
    *bRGA_SRC_CB_MST = msg->src.uv_addr + uv_y_off * uv_stride + uv_x_off;
    *bRGA_SRC_CR_MST = msg->src.v_addr + uv_y_off * uv_stride + uv_x_off;

    if((msg->alpha_rop_flag >> 1) & 1)
        *bRGA_SRC_CB_MST = (u32)msg->rop_mask_addr;

    if (msg->render_mode == color_palette_mode)
    {
        u8 shift;
        u16 sw, byte_num;
        shift = 3 - (msg->palette_mode & 3);
        sw = msg->src.vir_w;

        byte_num = sw >> shift;
        stride = (byte_num + 3) & (~3);
    }

    /* src act window / vir window set */
    *bRGA_SRC_VIR_INFO = ((stride >> 2) | (msg->src.vir_h)<<16);
    *bRGA_SRC_ACT_INFO = ((msg->src.act_w-1) | (msg->src.act_h-1)<<16);
}


/*************************************************************
Func:
    RGA_set_dst
Description:
    fill dst relate reg info
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

s32 RGA_set_dst(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_DST_MST;
    u32 *bRGA_DST_UV_MST;
    u32 *bRGA_DST_VIR_INFO;
    u32 *bRGA_DST_CTR_INFO;
    u32 *bRGA_PRESCL_CB_MST;
    u32 *bRGA_PRESCL_CR_MST;
    u32 *bRGA_YUV_OUT_CFG;

    u32 reg = 0;

    u8 pw;
    s16 x_off = msg->dst.x_offset;
    s16 y_off = msg->dst.y_offset;
    u16 stride, rop_mask_stride;

    bRGA_DST_MST = (u32 *)(base + RGA_DST_MST_OFFSET);
    bRGA_DST_UV_MST = (u32 *)(base + RGA_DST_UV_MST_OFFSET);
    bRGA_DST_VIR_INFO = (u32 *)(base + RGA_DST_VIR_INFO_OFFSET);
    bRGA_DST_CTR_INFO = (u32 *)(base + RGA_DST_CTR_INFO_OFFSET);
    bRGA_PRESCL_CB_MST = (u32 *)(base + RGA_PRESCL_CB_MST_OFFSET);
    bRGA_PRESCL_CR_MST = (u32 *)(base + RGA_PRESCL_CR_MST_OFFSET);
    bRGA_YUV_OUT_CFG = (u32 *)(base + RGA_YUV_OUT_CFG_OFFSET);

    pw = RGA_pixel_width_init(msg->dst.format);

    stride = (msg->dst.vir_w * pw + 3) & (~3);

    *bRGA_DST_MST = (u32)msg->dst.yrgb_addr + (y_off * stride) + (x_off * pw);

    *bRGA_DST_UV_MST = 0;
    *bRGA_YUV_OUT_CFG = 0;

	if (msg->rotate_mode == 1) {
		if (msg->sina == 65536 && msg->cosa == 0) {
			/* rotate 90 */
			x_off = msg->dst.x_offset - msg->dst.act_h + 1;
		} else if (msg->sina == 0 && msg->cosa == -65536) {
			/* rotate 180 */
			x_off = msg->dst.x_offset - msg->dst.act_w + 1;
			y_off = msg->dst.y_offset - msg->dst.act_h + 1;
		} else if (msg->sina == -65536 && msg->cosa == 0) {
			/* totate 270 */
			y_off = msg->dst.y_offset - msg->dst.act_w + 1;
		}
	}

    switch(msg->dst.format)
    {
        case RK_FORMAT_YCbCr_422_SP :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off) * stride) + ((x_off) * pw);
			*bRGA_DST_UV_MST = (u32)msg->dst.uv_addr + (y_off * stride) + x_off;
			*bRGA_YUV_OUT_CFG |= (((msg->yuv2rgb_mode >> 4) & 3) << 4) | (0 << 3) | (0 << 1) | 1;
            break;
        case RK_FORMAT_YCbCr_422_P  :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off) * stride) + ((x_off>>1) * pw);
            *bRGA_PRESCL_CR_MST = (u32)msg->dst.v_addr  + ((y_off) * stride) + ((x_off>>1) * pw);
            break;
        case RK_FORMAT_YCbCr_420_SP :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + ((x_off) * pw);
			*bRGA_DST_UV_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + x_off;
			*bRGA_YUV_OUT_CFG |= (((msg->yuv2rgb_mode >> 4) & 3) << 4) | (0 << 3) | (1 << 1) | 1;
            break;
        case RK_FORMAT_YCbCr_420_P :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + ((x_off>>1) * pw);
            *bRGA_PRESCL_CR_MST = (u32)msg->dst.v_addr  + ((y_off>>1) * stride) + ((x_off>>1) * pw);
            break;
        case RK_FORMAT_YCrCb_422_SP :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off) * stride) + ((x_off) * pw);
			*bRGA_DST_UV_MST = (u32)msg->dst.uv_addr + (y_off * stride) + x_off;
			*bRGA_YUV_OUT_CFG |= (((msg->yuv2rgb_mode >> 4) & 3) << 4) | (1 << 3) | (0 << 1) | 1;
            break;
        case RK_FORMAT_YCrCb_422_P  :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off) * stride) + ((x_off>>1) * pw);
            *bRGA_PRESCL_CR_MST = (u32)msg->dst.v_addr  + ((y_off) * stride) + ((x_off>>1) * pw);
            break;
        case RK_FORMAT_YCrCb_420_SP :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + ((x_off) * pw);
			*bRGA_DST_UV_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + x_off;
			*bRGA_YUV_OUT_CFG |= (((msg->yuv2rgb_mode >> 4) & 3) << 4) | (1 << 3) | (1 << 1) | 1;
            break;
        case RK_FORMAT_YCrCb_420_P :
            *bRGA_PRESCL_CB_MST = (u32)msg->dst.uv_addr + ((y_off>>1) * stride) + ((x_off>>1) * pw);
            *bRGA_PRESCL_CR_MST = (u32)msg->dst.v_addr  + ((y_off>>1) * stride) + ((x_off>>1) * pw);
            break;
    }

    rop_mask_stride = (((msg->src.vir_w + 7)>>3) + 3) & (~3);//not dst_vir.w,hxx,2011.7.21

    reg = (stride >> 2) & 0xffff;
    reg = reg | ((rop_mask_stride>>2) << 16);

    #if defined(CONFIG_ARCH_RK2928) || defined(CONFIG_ARCH_RK3188)
    //reg = reg | ((msg->alpha_rop_mode & 3) << 28);
    reg = reg | (1 << 28);
    #endif

    if (msg->render_mode == line_point_drawing_mode)
    {
        reg &= 0xffff;
        reg = reg | (msg->dst.vir_h << 16);
    }

    *bRGA_DST_VIR_INFO = reg;
    *bRGA_DST_CTR_INFO = (msg->dst.act_w - 1) | ((msg->dst.act_h - 1) << 16);

    if (msg->render_mode == pre_scaling_mode) {
        *bRGA_YUV_OUT_CFG &= 0xfffffffe;
    }

    return 0;
}


/*************************************************************
Func:
    RGA_set_alpha_rop
Description:
    fill alpha rop some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/
void
RGA_set_alpha_rop(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_ALPHA_CON;
    u32 *bRGA_ROP_CON0;
    u32 *bRGA_ROP_CON1;
    u32 reg = 0;
    u32 rop_con0, rop_con1;

    u8 rop_mode = (msg->alpha_rop_mode) & 3;
    u8 alpha_mode = msg->alpha_rop_mode & 3;

    rop_con0 = rop_con1 = 0;

    bRGA_ALPHA_CON = (u32 *)(base + RGA_ALPHA_CON_OFFSET);

    reg = ((reg & (~m_RGA_ALPHA_CON_ENABLE) )| (s_RGA_ALPHA_CON_ENABLE(msg->alpha_rop_flag & 1)));
    reg = ((reg & (~m_RGA_ALPHA_CON_A_OR_R_SEL)) | (s_RGA_ALPHA_CON_A_OR_R_SEL((msg->alpha_rop_flag >> 1) & 1)));
    reg = ((reg & (~m_RGA_ALPHA_CON_ALPHA_MODE)) | (s_RGA_ALPHA_CON_ALPHA_MODE(alpha_mode)));
    reg = ((reg & (~m_RGA_ALPHA_CON_PD_MODE)) | (s_RGA_ALPHA_CON_PD_MODE(msg->PD_mode)));
    reg = ((reg & (~m_RGA_ALPHA_CON_SET_CONSTANT_VALUE)) | (s_RGA_ALPHA_CON_SET_CONSTANT_VALUE(msg->alpha_global_value)));
    reg = ((reg & (~m_RGA_ALPHA_CON_PD_M_SEL)) | (s_RGA_ALPHA_CON_PD_M_SEL(msg->alpha_rop_flag >> 3)));
    reg = ((reg & (~m_RGA_ALPHA_CON_FADING_ENABLE)) | (s_RGA_ALPHA_CON_FADING_ENABLE(msg->alpha_rop_flag >> 2)));
    reg = ((reg & (~m_RGA_ALPHA_CON_ROP_MODE_SEL)) | (s_RGA_ALPHA_CON_ROP_MODE_SEL(rop_mode)));
    reg = ((reg & (~m_RGA_ALPHA_CON_CAL_MODE_SEL)) | (s_RGA_ALPHA_CON_CAL_MODE_SEL(msg->alpha_rop_flag >> 4)));
    reg = ((reg & (~m_RGA_ALPHA_CON_DITHER_ENABLE)) | (s_RGA_ALPHA_CON_DITHER_ENABLE(msg->alpha_rop_flag >> 5)));
    reg = ((reg & (~m_RGA_ALPHA_CON_GRADIENT_CAL_MODE)) | (s_RGA_ALPHA_CON_GRADIENT_CAL_MODE(msg->alpha_rop_flag >> 6)));
    reg = ((reg & (~m_RGA_ALPHA_CON_AA_SEL)) | (s_RGA_ALPHA_CON_AA_SEL(msg->alpha_rop_flag >> 7)));

    *bRGA_ALPHA_CON = reg;

    if(rop_mode == 0) {
        rop_con0 =  ROP3_code[(msg->rop_code & 0xff)];
    }
    else if(rop_mode == 1) {
        rop_con0 =  ROP3_code[(msg->rop_code & 0xff)];
    }
    else if(rop_mode == 2) {
        rop_con0 =  ROP3_code[(msg->rop_code & 0xff)];
        rop_con1 =  ROP3_code[(msg->rop_code & 0xff00)>>8];
    }

    bRGA_ROP_CON0 = (u32 *)(base + RGA_ROP_CON0_OFFSET);
    bRGA_ROP_CON1 = (u32 *)(base + RGA_ROP_CON1_OFFSET);

    *bRGA_ROP_CON0 = (u32)rop_con0;
    *bRGA_ROP_CON1 = (u32)rop_con1;
}


/*************************************************************
Func:
    RGA_set_color
Description:
    fill color some relate reg bit
    bg_color/fg_color
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
RGA_set_color(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_SRC_TR_COLOR0;
    u32 *bRGA_SRC_TR_COLOR1;
    u32 *bRGA_SRC_BG_COLOR;
    u32 *bRGA_SRC_FG_COLOR;


    bRGA_SRC_BG_COLOR  = (u32 *)(base + RGA_SRC_BG_COLOR_OFFSET);
    bRGA_SRC_FG_COLOR  = (u32 *)(base + RGA_SRC_FG_COLOR_OFFSET);

    *bRGA_SRC_BG_COLOR = msg->bg_color;    /* 1bpp 0 */
    *bRGA_SRC_FG_COLOR = msg->fg_color;    /* 1bpp 1 */

    bRGA_SRC_TR_COLOR0 = (u32 *)(base + RGA_SRC_TR_COLOR0_OFFSET);
    bRGA_SRC_TR_COLOR1 = (u32 *)(base + RGA_SRC_TR_COLOR1_OFFSET);

    *bRGA_SRC_TR_COLOR0 = msg->color_key_min;
    *bRGA_SRC_TR_COLOR1 = msg->color_key_max;
}


/*************************************************************
Func:
    RGA_set_fading
Description:
    fill fading some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

s32
RGA_set_fading(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_FADING_CON;
    u8 r, g, b;
    u32 reg = 0;

    bRGA_FADING_CON = (u32 *)(base + RGA_FADING_CON_OFFSET);

    b = msg->fading.b;
    g = msg->fading.g;
    r = msg->fading.r;

    reg = (r<<8) | (g<<16) | (b<<24) | reg;

    *bRGA_FADING_CON = reg;

    return 0;
}


/*************************************************************
Func:
    RGA_set_pat
Description:
    fill patten some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

s32
RGA_set_pat(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_PAT_CON;
    u32 *bRGA_PAT_START_POINT;
    u32 reg = 0;

    bRGA_PAT_START_POINT = (u32 *)(base + RGA_PAT_START_POINT_OFFSET);

    bRGA_PAT_CON = (u32 *)(base + RGA_PAT_CON_OFFSET);

    *bRGA_PAT_START_POINT = (msg->pat.act_w * msg->pat.y_offset) + msg->pat.x_offset;

    reg = (msg->pat.act_w - 1) | ((msg->pat.act_h - 1) << 8) | (msg->pat.x_offset << 16) | (msg->pat.y_offset << 24);
    *bRGA_PAT_CON = reg;

    return 0;
}




/*************************************************************
Func:
    RGA_set_bitblt_reg_info
Description:
    fill bitblt mode relate ren info
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
RGA_set_bitblt_reg_info(u8 *base, const struct rga_req * msg, TILE_INFO *tile)
{
    u32 *bRGA_SRC_Y_MST;
    u32 *bRGA_SRC_CB_MST;
    u32 *bRGA_SRC_CR_MST;
    u32 *bRGA_SRC_X_PARA;
    u32 *bRGA_SRC_Y_PARA;
    u32 *bRGA_SRC_TILE_XINFO;
    u32 *bRGA_SRC_TILE_YINFO;
    u32 *bRGA_SRC_TILE_H_INCR;
    u32 *bRGA_SRC_TILE_V_INCR;
    u32 *bRGA_SRC_TILE_OFFSETX;
    u32 *bRGA_SRC_TILE_OFFSETY;

    u32 *bRGA_DST_MST;
    u32 *bRGA_DST_CTR_INFO;

    s32 m0, m1, m2, m3;
    s32 pos[8];
    //s32 x_dx, x_dy, y_dx, y_dy;
    s32 xmin, xmax, ymin, ymax;
    s32 xp, yp;
    u32 y_addr, u_addr, v_addr;
    u32 pixel_width, stride;

    u_addr = v_addr = 0;

    /* src info */

    bRGA_SRC_Y_MST = (u32 *)(base + RGA_SRC_Y_MST_OFFSET);
    bRGA_SRC_CB_MST = (u32 *)(base + RGA_SRC_CB_MST_OFFSET);
    bRGA_SRC_CR_MST = (u32 *)(base + RGA_SRC_CR_MST_OFFSET);

    bRGA_SRC_X_PARA = (u32 *)(base + RGA_SRC_X_PARA_OFFSET);
    bRGA_SRC_Y_PARA = (u32 *)(base + RGA_SRC_Y_PARA_OFFSET);

    bRGA_SRC_TILE_XINFO = (u32 *)(base + RGA_SRC_TILE_XINFO_OFFSET);
    bRGA_SRC_TILE_YINFO = (u32 *)(base + RGA_SRC_TILE_YINFO_OFFSET);
    bRGA_SRC_TILE_H_INCR = (u32 *)(base + RGA_SRC_TILE_H_INCR_OFFSET);
    bRGA_SRC_TILE_V_INCR = (u32 *)(base + RGA_SRC_TILE_V_INCR_OFFSET);
    bRGA_SRC_TILE_OFFSETX = (u32 *)(base + RGA_SRC_TILE_OFFSETX_OFFSET);
    bRGA_SRC_TILE_OFFSETY = (u32 *)(base + RGA_SRC_TILE_OFFSETY_OFFSET);

    bRGA_DST_MST = (u32 *)(base + RGA_DST_MST_OFFSET);
    bRGA_DST_CTR_INFO = (u32 *)(base + RGA_DST_CTR_INFO_OFFSET);

    /* Matrix reg fill */
    m0 = (s32)(tile->matrix[0] >> 18);
    m1 = (s32)(tile->matrix[1] >> 18);
    m2 = (s32)(tile->matrix[2] >> 18);
    m3 = (s32)(tile->matrix[3] >> 18);

    *bRGA_SRC_X_PARA = (m0 & 0xffff) | (m2 << 16);
    *bRGA_SRC_Y_PARA = (m1 & 0xffff) | (m3 << 16);

    /* src tile information setting */
    if(msg->rotate_mode != 0)//add by hxx,2011.7.12,for rtl0707,when line scanning ,do not calc src tile info
    {
        *bRGA_SRC_TILE_XINFO = (tile->tile_start_x_coor & 0xffff) | (tile->tile_w << 16);
        *bRGA_SRC_TILE_YINFO = (tile->tile_start_y_coor & 0xffff) | (tile->tile_h << 16);

        *bRGA_SRC_TILE_H_INCR = ((tile->x_dx) & 0xffff) | ((tile->x_dy) << 16);
        *bRGA_SRC_TILE_V_INCR = ((tile->y_dx) & 0xffff) | ((tile->y_dy) << 16);

        *bRGA_SRC_TILE_OFFSETX = tile->tile_xoff;
        *bRGA_SRC_TILE_OFFSETY = tile->tile_yoff;
    }

    pixel_width = RGA_pixel_width_init(msg->src.format);

    stride = ((msg->src.vir_w * pixel_width) + 3) & (~3);

    if ((msg->rotate_mode == 1)||(msg->rotate_mode == 2)||(msg->rotate_mode == 3))
    {
        pos[0] = tile->tile_start_x_coor<<8;
        pos[1] = tile->tile_start_y_coor<<8;

        pos[2] = pos[0];
        pos[3] = pos[1] + tile->tile_h;

        pos[4] = pos[0] + tile->tile_w;
        pos[5] = pos[1] + tile->tile_h;

        pos[6] = pos[0] + tile->tile_w;
        pos[7] = pos[1];

        pos[0] >>= 11;
        pos[1] >>= 11;

        pos[2] >>= 11;
        pos[3] >>= 11;

        pos[4] >>= 11;
        pos[5] >>= 11;

        pos[6] >>= 11;
        pos[7] >>= 11;

        xmax = (MAX(MAX(MAX(pos[0], pos[2]), pos[4]), pos[6]) + 1);
        xmin = (MIN(MIN(MIN(pos[0], pos[2]), pos[4]), pos[6]));

        ymax = (MAX(MAX(MAX(pos[1], pos[3]), pos[5]), pos[7]) + 1);
        ymin = (MIN(MIN(MIN(pos[1], pos[3]), pos[5]), pos[7]));

        xp = xmin + msg->src.x_offset;
        yp = ymin + msg->src.y_offset;

        if (!((xmax < 0)||(xmin > msg->src.act_w - 1)||(ymax < 0)||(ymin > msg->src.act_h - 1)))
        {
            xp = CLIP(xp, msg->src.x_offset, msg->src.x_offset + msg->src.act_w - 1);
            yp = CLIP(yp, msg->src.y_offset, msg->src.y_offset + msg->src.act_h - 1);
        }

        switch(msg->src.format)
        {
            case RK_FORMAT_YCbCr_420_P :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp>>1)*(stride>>1) + (xp>>1);
                v_addr = msg->src.v_addr  + (yp>>1)*(stride>>1) + (xp>>1);
                break;
            case RK_FORMAT_YCbCr_420_SP :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp>>1)*stride + ((xp>>1)<<1);
                break;
            case RK_FORMAT_YCbCr_422_P :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp)*(stride>>1) + (xp>>1);
                v_addr = msg->src.v_addr  + (yp)*(stride>>1) + (xp>>1);
                break;
            case RK_FORMAT_YCbCr_422_SP:
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr  + yp*stride + ((xp>>1)<<1);
                break;
            case RK_FORMAT_YCrCb_420_P :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp>>1)*(stride>>1) + (xp>>1);
                v_addr = msg->src.v_addr  + (yp>>1)*(stride>>1) + (xp>>1);
                break;
            case RK_FORMAT_YCrCb_420_SP :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp>>1)*stride + ((xp>>1)<<1);
                break;
            case RK_FORMAT_YCrCb_422_P :
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr + (yp)*(stride>>1) + (xp>>1);
                v_addr = msg->src.v_addr  + (yp)*(stride>>1) + (xp>>1);
                break;
            case RK_FORMAT_YCrCb_422_SP:
                y_addr = msg->src.yrgb_addr + yp*stride + xp;
                u_addr = msg->src.uv_addr  + yp*stride + ((xp>>1)<<1);
                break;
            default :
                y_addr = msg->src.yrgb_addr + yp*stride + xp*pixel_width;
                break;
        }

        *bRGA_SRC_Y_MST = y_addr;
        *bRGA_SRC_CB_MST = u_addr;
        *bRGA_SRC_CR_MST = v_addr;
    }

    /*dst info*/
    pixel_width = RGA_pixel_width_init(msg->dst.format);
    stride = (msg->dst.vir_w * pixel_width + 3) & (~3);
    *bRGA_DST_MST = (u32)msg->dst.yrgb_addr + (tile->dst_ctrl.y_off * stride) + (tile->dst_ctrl.x_off * pixel_width);
    *bRGA_DST_CTR_INFO = (tile->dst_ctrl.w) | ((tile->dst_ctrl.h) << 16);

    *bRGA_DST_CTR_INFO |= ((1<<29) | (1<<28));
}




/*************************************************************
Func:
    RGA_set_color_palette_reg_info
Description:
    fill color palette process some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

void
RGA_set_color_palette_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_SRC_Y_MST;
    u32 p;
    s16 x_off, y_off;
    u16 src_stride;
    u8  shift;
    u16 sw, byte_num;

    x_off = msg->src.x_offset;
    y_off = msg->src.y_offset;

    sw = msg->src.vir_w;
    shift = 3 - (msg->palette_mode & 3);
    byte_num = sw >> shift;
    src_stride = (byte_num + 3) & (~3);

    p = msg->src.yrgb_addr;
    p = p + (x_off>>shift) + y_off*src_stride;

    bRGA_SRC_Y_MST = (u32 *)(base + RGA_SRC_Y_MST_OFFSET);
    *bRGA_SRC_Y_MST = (u32)p;
}


/*************************************************************
Func:
    RGA_set_color_fill_reg_info
Description:
    fill color fill process some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/
void
RGA_set_color_fill_reg_info(u8 *base, const struct rga_req *msg)
{

    u32 *bRGA_CP_GR_A;
    u32 *bRGA_CP_GR_B;
    u32 *bRGA_CP_GR_G;
    u32 *bRGA_CP_GR_R;

    u32 *bRGA_PAT_CON;

    bRGA_CP_GR_A = (u32 *)(base + RGA_CP_GR_A_OFFSET);
    bRGA_CP_GR_B = (u32 *)(base + RGA_CP_GR_B_OFFSET);
    bRGA_CP_GR_G = (u32 *)(base + RGA_CP_GR_G_OFFSET);
    bRGA_CP_GR_R = (u32 *)(base + RGA_CP_GR_R_OFFSET);

    bRGA_PAT_CON = (u32 *)(base + RGA_PAT_CON_OFFSET);

    *bRGA_CP_GR_A = (msg->gr_color.gr_x_a & 0xffff) | (msg->gr_color.gr_y_a << 16);
    *bRGA_CP_GR_B = (msg->gr_color.gr_x_b & 0xffff) | (msg->gr_color.gr_y_b << 16);
    *bRGA_CP_GR_G = (msg->gr_color.gr_x_g & 0xffff) | (msg->gr_color.gr_y_g << 16);
    *bRGA_CP_GR_R = (msg->gr_color.gr_x_r & 0xffff) | (msg->gr_color.gr_y_r << 16);

    *bRGA_PAT_CON = (msg->pat.vir_w-1) | ((msg->pat.vir_h-1) << 8) | (msg->pat.x_offset << 16) | (msg->pat.y_offset << 24);

}


/*************************************************************
Func:
    RGA_set_line_drawing_reg_info
Description:
    fill line drawing process some relate reg bit
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

s32 RGA_set_line_drawing_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_LINE_DRAW;
    u32 *bRGA_DST_VIR_INFO;
    u32 *bRGA_LINE_DRAW_XY_INFO;
    u32 *bRGA_LINE_DRAW_WIDTH;
    u32 *bRGA_LINE_DRAWING_COLOR;
    u32 *bRGA_LINE_DRAWING_MST;

    u32  reg = 0;

    s16 x_width, y_width;
    u16 abs_x, abs_y, delta;
    u16 stride;
    u8 pw;
    u32 start_addr;
    u8 line_dir, dir_major, dir_semi_major;
    u16 major_width;

    bRGA_LINE_DRAW = (u32 *)(base + RGA_LINE_DRAW_OFFSET);
    bRGA_DST_VIR_INFO = (u32 *)(base + RGA_DST_VIR_INFO_OFFSET);
    bRGA_LINE_DRAW_XY_INFO = (u32 *)(base + RGA_LINE_DRAW_XY_INFO_OFFSET);
    bRGA_LINE_DRAW_WIDTH = (u32 *)(base + RGA_LINE_DRAWING_WIDTH_OFFSET);
    bRGA_LINE_DRAWING_COLOR = (u32 *)(base + RGA_LINE_DRAWING_COLOR_OFFSET);
    bRGA_LINE_DRAWING_MST = (u32 *)(base + RGA_LINE_DRAWING_MST_OFFSET);

    pw = RGA_pixel_width_init(msg->dst.format);

    stride = (msg->dst.vir_w * pw + 3) & (~3);

    start_addr = msg->dst.yrgb_addr
                + (msg->line_draw_info.start_point.y * stride)
                + (msg->line_draw_info.start_point.x * pw);

    x_width = msg->line_draw_info.start_point.x - msg->line_draw_info.end_point.x;
    y_width = msg->line_draw_info.start_point.y - msg->line_draw_info.end_point.y;

    abs_x = abs(x_width);
    abs_y = abs(y_width);

    if (abs_x >= abs_y)
    {
        if (y_width > 0)
            dir_semi_major = 1;
        else
            dir_semi_major = 0;

        if (x_width > 0)
            dir_major = 1;
        else
            dir_major = 0;

        if((abs_x == 0)||(abs_y == 0))
            delta = 0;
        else
            delta = (abs_y<<12)/abs_x;

        if (delta >> 12)
            delta -= 1;

        major_width = abs_x;
        line_dir = 0;
    }
    else
    {
        if (x_width > 0)
            dir_semi_major = 1;
        else
            dir_semi_major = 0;

        if (y_width > 0)
            dir_major = 1;
        else
            dir_major = 0;

        delta = (abs_x<<12)/abs_y;
        major_width = abs_y;
        line_dir = 1;
    }

    reg = (reg & (~m_RGA_LINE_DRAW_MAJOR_WIDTH))     | (s_RGA_LINE_DRAW_MAJOR_WIDTH(major_width));
    reg = (reg & (~m_RGA_LINE_DRAW_LINE_DIRECTION))  | (s_RGA_LINE_DRAW_LINE_DIRECTION(line_dir));
    reg = (reg & (~m_RGA_LINE_DRAW_LINE_WIDTH))      | (s_RGA_LINE_DRAW_LINE_WIDTH(msg->line_draw_info.line_width - 1));
    reg = (reg & (~m_RGA_LINE_DRAW_INCR_VALUE))      | (s_RGA_LINE_DRAW_INCR_VALUE(delta));
    reg = (reg & (~m_RGA_LINE_DRAW_DIR_SEMI_MAJOR))  | (s_RGA_LINE_DRAW_DIR_SEMI_MAJOR(dir_semi_major));
    reg = (reg & (~m_RGA_LINE_DRAW_DIR_MAJOR))       | (s_RGA_LINE_DRAW_DIR_MAJOR(dir_major));
    reg = (reg & (~m_RGA_LINE_DRAW_LAST_POINT))      | (s_RGA_LINE_DRAW_LAST_POINT(msg->line_draw_info.flag >> 1));
    reg = (reg & (~m_RGA_LINE_DRAW_ANTI_ALISING))    | (s_RGA_LINE_DRAW_ANTI_ALISING(msg->line_draw_info.flag));

    *bRGA_LINE_DRAW = reg;

    reg = (msg->line_draw_info.start_point.x & 0xfff) | ((msg->line_draw_info.start_point.y & 0xfff) << 16);
    *bRGA_LINE_DRAW_XY_INFO = reg;

    *bRGA_LINE_DRAW_WIDTH = msg->dst.vir_w;

    *bRGA_LINE_DRAWING_COLOR = msg->line_draw_info.color;

    *bRGA_LINE_DRAWING_MST = (u32)start_addr;

    return 0;
}


/*full*/
s32
RGA_set_filter_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_BLUR_SHARP_INFO;
    u32  reg = 0;

    bRGA_BLUR_SHARP_INFO = (u32 *)(base + RGA_ALPHA_CON_OFFSET);

    reg = *bRGA_BLUR_SHARP_INFO;

    reg = ((reg & (~m_RGA_BLUR_SHARP_FILTER_TYPE)) | (s_RGA_BLUR_SHARP_FILTER_TYPE(msg->bsfilter_flag & 3)));
    reg = ((reg & (~m_RGA_BLUR_SHARP_FILTER_MODE)) | (s_RGA_BLUR_SHARP_FILTER_MODE(msg->bsfilter_flag >>2)));

    *bRGA_BLUR_SHARP_INFO = reg;

    return 0;
}


/*full*/
s32
RGA_set_pre_scale_reg_info(u8 *base, const struct rga_req *msg)
{
   u32 *bRGA_PRE_SCALE_INFO;
   u32 reg = 0;
   u32 h_ratio = 0;
   u32 v_ratio = 0;
   u32 ps_yuv_flag = 0;
   u32 src_width, src_height;
   u32 dst_width, dst_height;

   src_width = msg->src.act_w;
   src_height = msg->src.act_h;

   dst_width = msg->dst.act_w;
   dst_height = msg->dst.act_h;

   if((dst_width == 0) || (dst_height == 0))
   {
        printk("pre scale reg info error ratio is divide zero\n");
        return -EINVAL;
   }

   h_ratio = (src_width <<16) / dst_width;
   v_ratio = (src_height<<16) / dst_height;

   if (h_ratio <= (1<<16))
       h_ratio = 0;
   else if (h_ratio <= (2<<16))
       h_ratio = 1;
   else if (h_ratio <= (4<<16))
       h_ratio = 2;
   else if (h_ratio <= (8<<16))
       h_ratio = 3;

   if (v_ratio <= (1<<16))
       v_ratio = 0;
   else if (v_ratio <= (2<<16))
       v_ratio = 1;
   else if (v_ratio <= (4<<16))
       v_ratio = 2;
   else if (v_ratio <= (8<<16))
       v_ratio = 3;

   if(msg->src.format == msg->dst.format)
        ps_yuv_flag = 0;
    else
        ps_yuv_flag = 1;

   bRGA_PRE_SCALE_INFO = (u32 *)(base + RGA_ALPHA_CON_OFFSET);

   reg = *bRGA_PRE_SCALE_INFO;
   reg = ((reg & (~m_RGA_PRE_SCALE_HOR_RATIO)) | (s_RGA_PRE_SCALE_HOR_RATIO((u8)h_ratio)));
   reg = ((reg & (~m_RGA_PRE_SCALE_VER_RATIO)) | (s_RGA_PRE_SCALE_VER_RATIO((u8)v_ratio)));
   reg = ((reg & (~m_RGA_PRE_SCALE_OUTPUT_FORMAT)) | (s_RGA_PRE_SCALE_OUTPUT_FORMAT(ps_yuv_flag)));

   *bRGA_PRE_SCALE_INFO = reg;

   return 0;
}



/*full*/
int
RGA_set_update_palette_table_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_LUT_MST;

    if (!msg->LUT_addr) {
        return -1;
    }

    bRGA_LUT_MST  = (u32 *)(base + RGA_LUT_MST_OFFSET);

    *bRGA_LUT_MST = (u32)msg->LUT_addr;

    return 0;
}



/*full*/
int
RGA_set_update_patten_buff_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *bRGA_PAT_MST;
    u32 *bRGA_PAT_CON;
    u32 *bRGA_PAT_START_POINT;
    u32 reg = 0;
    rga_img_info_t *pat;

    pat = (rga_img_info_t *)&msg->pat;

    bRGA_PAT_START_POINT = (u32 *)(base + RGA_PAT_START_POINT_OFFSET);
    bRGA_PAT_MST = (u32 *)(base + RGA_PAT_MST_OFFSET);
    bRGA_PAT_CON = (u32 *)(base + RGA_PAT_CON_OFFSET);

    if ( !pat->yrgb_addr ) {
        return -1;
    }
    *bRGA_PAT_MST = (u32)pat->yrgb_addr;

    if ((pat->vir_w > 256)||(pat->x_offset > 256)||(pat->y_offset > 256)) {
        return -1;
    }
    *bRGA_PAT_START_POINT = (pat->vir_w * pat->y_offset) + pat->x_offset;

    reg = (pat->vir_w-1) | ((pat->vir_h-1) << 8) | (pat->x_offset << 16) | (pat->y_offset << 24);
    *bRGA_PAT_CON = reg;

    return 0;
}


/*************************************************************
Func:
    RGA_set_mmu_ctrl_reg_info
Description:
    fill mmu relate some reg info
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/

s32
RGA_set_mmu_ctrl_reg_info(u8 *base, const struct rga_req *msg)
{
    u32 *RGA_MMU_TLB, *RGA_MMU_CTRL_ADDR;
    u32  mmu_addr;
    u8   TLB_size, mmu_enable, src_flag, dst_flag, CMD_flag;
    u32  reg = 0;

    mmu_addr = (u32)msg->mmu_info.base_addr;
    TLB_size = (msg->mmu_info.mmu_flag >> 4) & 0x3;
    mmu_enable = msg->mmu_info.mmu_flag & 0x1;

    src_flag = (msg->mmu_info.mmu_flag >> 1) & 0x1;
    dst_flag = (msg->mmu_info.mmu_flag >> 2) & 0x1;
    CMD_flag = (msg->mmu_info.mmu_flag >> 3) & 0x1;

    RGA_MMU_TLB = (u32 *)(base + RGA_MMU_TLB_OFFSET);
    RGA_MMU_CTRL_ADDR = (u32 *)(base + RGA_FADING_CON_OFFSET);

    reg = ((reg & (~m_RGA_MMU_CTRL_TLB_ADDR)) | s_RGA_MMU_CTRL_TLB_ADDR(mmu_addr));
    *RGA_MMU_TLB = reg;

    reg = *RGA_MMU_CTRL_ADDR;
    reg = ((reg & (~m_RGA_MMU_CTRL_PAGE_TABLE_SIZE)) | s_RGA_MMU_CTRL_PAGE_TABLE_SIZE(TLB_size));
    reg = ((reg & (~m_RGA_MMU_CTRL_MMU_ENABLE)) | s_RGA_MMU_CTRL_MMU_ENABLE(mmu_enable));
    reg = ((reg & (~m_RGA_MMU_CTRL_SRC_FLUSH)) | s_RGA_MMU_CTRL_SRC_FLUSH(1));
    reg = ((reg & (~m_RGA_MMU_CTRL_DST_FLUSH)) | s_RGA_MMU_CTRL_DST_FLUSH(1));
    reg = ((reg & (~m_RGA_MMU_CTRL_CMD_CHAN_FLUSH)) | s_RGA_MMU_CTRL_CMD_CHAN_FLUSH(1));
    *RGA_MMU_CTRL_ADDR = reg;

    return 0;
}



/*************************************************************
Func:
    RGA_gen_reg_info
Description:
    Generate RGA command reg list from rga_req struct.
Author:
    ZhangShengqin
Date:
    20012-2-2 10:59:25
**************************************************************/
int
RGA_gen_reg_info(const struct rga_req *msg, unsigned char *base)
{
    TILE_INFO tile;

    memset(base, 0x0, 28*4);
    RGA_set_mode_ctrl(base, msg);

    switch(msg->render_mode)
    {
        case bitblt_mode :
            RGA_set_alpha_rop(base, msg);
            RGA_set_src(base, msg);
            RGA_set_dst(base, msg);
            RGA_set_color(base, msg);
            RGA_set_fading(base, msg);
            RGA_set_pat(base, msg);
            matrix_cal(msg, &tile);
            dst_ctrl_cal(msg, &tile);
            src_tile_info_cal(msg, &tile);
            RGA_set_bitblt_reg_info(base, msg, &tile);
            break;
        case color_palette_mode :
            RGA_set_src(base, msg);
            RGA_set_dst(base, msg);
            RGA_set_color(base, msg);
            RGA_set_color_palette_reg_info(base, msg);
            break;
        case color_fill_mode :
            RGA_set_alpha_rop(base, msg);
            RGA_set_dst(base, msg);
            RGA_set_color(base, msg);
            RGA_set_pat(base, msg);
            RGA_set_color_fill_reg_info(base, msg);
            break;
        case line_point_drawing_mode :
            RGA_set_alpha_rop(base, msg);
            RGA_set_dst(base, msg);
            RGA_set_color(base, msg);
            RGA_set_line_drawing_reg_info(base, msg);
            break;
        case blur_sharp_filter_mode :
            RGA_set_src(base, msg);
            RGA_set_dst(base, msg);
            RGA_set_filter_reg_info(base, msg);
            break;
        case pre_scaling_mode :
            RGA_set_src(base, msg);
            RGA_set_dst(base, msg);
            if(RGA_set_pre_scale_reg_info(base, msg) == -EINVAL)
                return -1;
            break;
        case update_palette_table_mode :
            if (RGA_set_update_palette_table_reg_info(base, msg)) {
                return -1;
            }
			break;
        case update_patten_buff_mode:
            if (RGA_set_update_patten_buff_reg_info(base, msg)){
                return -1;
            }

            break;
    }

    RGA_set_mmu_ctrl_reg_info(base, msg);

    return 0;
}



