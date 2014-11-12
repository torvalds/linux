/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Qi Wang <qi.wang@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/vformat.h>
#include <mach/am_regs.h>
#include <linux/module.h>

#include "vdec_reg.h"
#include "streambuf_reg.h"
#include "amvdec.h"
#include "vavs_mc.h"

#define DRIVER_NAME "amvdec_avs"
#define MODULE_NAME "amvdec_avs"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
#define NV21
#endif


//#define USE_AVS_SEQ_INFO_ONLY
#define HANDLE_AVS_IRQ
#define DEBUG_PTS

#define I_PICTURE   0
#define P_PICTURE   1
#define B_PICTURE   2

//#define ORI_BUFFER_START_ADDR   0x81000000
#define ORI_BUFFER_START_ADDR   0x80000000

#define INTERLACE_FLAG          0x80
#define TOP_FIELD_FIRST_FLAG 0x40

/* protocol registers */
#define AVS_PIC_RATIO       AV_SCRATCH_0
#define AVS_PIC_WIDTH      AV_SCRATCH_1
#define AVS_PIC_HEIGHT     AV_SCRATCH_2
#define AVS_FRAME_RATE     AV_SCRATCH_3

#define AVS_ERROR_COUNT    AV_SCRATCH_6
#define AVS_SOS_COUNT     AV_SCRATCH_7
#define AVS_BUFFERIN       AV_SCRATCH_8
#define AVS_BUFFEROUT      AV_SCRATCH_9
#define AVS_REPEAT_COUNT    AV_SCRATCH_A
#define AVS_TIME_STAMP      AV_SCRATCH_B
#define AVS_OFFSET_REG      AV_SCRATCH_C
#define MEM_OFFSET_REG      AV_SCRATCH_F

#define VF_POOL_SIZE        12
#define PUT_INTERVAL        (HZ/100)

#define INCPTR(p) ptr_atomic_wrap_inc(&p)

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define INT_AMVENCODER INT_DOS_MAILBOX_1
#else
//#define AMVENC_DEV_VERSION "AML-MT"
#define INT_AMVENCODER INT_MAILBOX_1A
#endif

static int debug_flag = 0;

static vframe_t *vavs_vf_peek(void*);
static vframe_t *vavs_vf_get(void*);
static void vavs_vf_put(vframe_t *, void*);

static const char vavs_dec_id[] = "vavs-dev";

#define PROVIDER_NAME   "decoder.avs"

static const struct vframe_operations_s vavs_vf_provider = {
        .peek = vavs_vf_peek,
        .get = vavs_vf_get,
        .put = vavs_vf_put,
};

static struct vframe_provider_s vavs_vf_prov;

static struct vframe_s vfpool[VF_POOL_SIZE];
static u32 vfpool_idx[VF_POOL_SIZE];
static s32 vfbuf_use[4];
static s32 fill_ptr, get_ptr, putting_ptr, put_ptr;
static u32 frame_width, frame_height, frame_dur, frame_prog;
static struct timer_list recycle_timer;
static u32 stat;
static u32 buf_start, buf_size, buf_offset;
static u32 avi_flag = 0;
static u32 vavs_ratio;
static u32 pic_type = 0;
static u32 pts_by_offset = 1;
static u32 total_frame;
static u32 next_pts;
static unsigned char throw_pb_flag;
#ifdef DEBUG_PTS
static u32 pts_hit, pts_missed, pts_i_hit, pts_i_missed;
#endif

static struct dec_sysinfo vavs_amstream_dec_info;

static inline u32 index2canvas(u32 index)
{
        const u32 canvas_tab[4] = {
#ifdef NV21
                0x010100, 0x040403, 0x070706, 0x0a0a09
#else
                0x020100, 0x050403, 0x080706, 0x0b0a09
#endif                
        };

        return canvas_tab[index];
}

static inline void ptr_atomic_wrap_inc(u32 *ptr)
{
        u32 i = *ptr;

        i++;

        if (i >= VF_POOL_SIZE)
                i = 0;

        *ptr = i;
}

static const u32 frame_rate_tab[16] = {
    96000 / 30, /* forbidden*/
    96000 / 24, /* 24000/1001 (23.967) */
    96000 / 24,
    96000 / 25, 
    96000 / 30, /* 30000/1001 (29.97) */
    96000 / 30,
    96000 / 50,
    96000 / 60, /* 60000/1001 (59.94) */
    96000 / 60,
    /* > 8 reserved, use 24 */
    96000 / 24, 96000 / 24, 96000 / 24, 96000 / 24,
    96000 / 24, 96000 / 24, 96000 / 24
};

static void set_frame_info(vframe_t *vf, unsigned* duration)
{
    int ar = 0;

    unsigned pixel_ratio = READ_VREG(AVS_PIC_RATIO);
#ifndef USE_AVS_SEQ_INFO
    if(vavs_amstream_dec_info.width>0 && vavs_amstream_dec_info.height>0){
        vf->width = vavs_amstream_dec_info.width;
        vf->height = vavs_amstream_dec_info.height;
    }
    else
#endif        
    {
        vf->width  = READ_VREG(AVS_PIC_WIDTH);
        vf->height = READ_VREG(AVS_PIC_HEIGHT);
        frame_width = vf->width;
        frame_height = vf->height; 
        //printk("%s: (%d,%d)\n", __func__,vf->width, vf->height);
    }

#ifndef USE_AVS_SEQ_INFO
    if(vavs_amstream_dec_info.rate > 0){
        *duration = vavs_amstream_dec_info.rate;
    }
    else
#endif        
    {
        *duration = frame_rate_tab[READ_VREG(AVS_FRAME_RATE)&0xf];
        //printk("%s: duration = %d\n", __func__, *duration);
        frame_dur = *duration;
    }
    
        if (vavs_ratio == 0)
        {
                vf->ratio_control |=(0x90<<DISP_RATIO_ASPECT_RATIO_BIT); // always stretch to 16:9
        }
        else
        {
                switch (pixel_ratio)
                {
   		        case 1:
   		            ar = (vf->height*vavs_ratio)/vf->width;
   		            break;
   		        case 2:
   		            ar = (vf->height*3*vavs_ratio)/(vf->width*4);
   		            break;
   		        case 3:
   		            ar = (vf->height*9*vavs_ratio)/(vf->width*16);
   		            break;
   		        case 4:
   		            ar = (vf->height*100*vavs_ratio)/(vf->width*221);
   		            break;
                default:
                    ar = (vf->height*vavs_ratio)/vf->width;
                    break;
                }
        }

        ar = min(ar, DISP_RATIO_ASPECT_RATIO_MAX);

        vf->ratio_control = (ar<<DISP_RATIO_ASPECT_RATIO_BIT);
        //vf->ratio_control |= DISP_RATIO_FORCECONFIG | DISP_RATIO_KEEPRATIO;

    vf->flag = 0;
}

#ifdef HANDLE_AVS_IRQ
static irqreturn_t vavs_isr(int irq, void *dev_id)
#else
static void vavs_isr(void)
#endif
{
        u32 reg;
        vframe_t *vf;
        u32 dur;
        u32 repeat_count;
        u32 picture_type;
        u32 buffer_index;
        unsigned int pts, pts_valid=0, offset;
       if(debug_flag&2){
           if(READ_VREG(AV_SCRATCH_E)!=0){
                printk("dbg%x: %x\n",  READ_VREG(AV_SCRATCH_E), READ_VREG(AV_SCRATCH_D));
                WRITE_VREG(AV_SCRATCH_E, 0);
           }
        }

        reg = READ_VREG(AVS_BUFFEROUT);

        if (reg)
        {
                if(debug_flag&1)
                    printk("AVS_BUFFEROUT=%x\n", reg);
                if (pts_by_offset)
                {
                        offset = READ_VREG(AVS_OFFSET_REG);
                        if(debug_flag&1)
                            printk("AVS OFFSET=%x\n", offset);
                        if (pts_lookup_offset(PTS_TYPE_VIDEO, offset, &pts, 0) == 0)
                        {
                                pts_valid = 1;
                        #ifdef DEBUG_PTS
                                pts_hit++;
                        #endif
                        }
                        else
                        {
                        #ifdef DEBUG_PTS
                                pts_missed++;
                        #endif
                        }
                }

                repeat_count = READ_VREG(AVS_REPEAT_COUNT);
                buffer_index = ((reg & 0x7) - 1) & 3;
                picture_type = (reg >> 3) & 7;
            #ifdef DEBUG_PTS
                if (picture_type == I_PICTURE)
                {
                    //printk("I offset 0x%x, pts_valid %d\n", offset, pts_valid);
                    if (!pts_valid)
                        pts_i_missed++;
                    else
                        pts_i_hit++;
                }
            #endif

                if(throw_pb_flag && picture_type != I_PICTURE){
                        
                        if(debug_flag&1)
                            printk("picture type %d throwed\n", picture_type);
                        WRITE_VREG(AVS_BUFFERIN, ~(1<<buffer_index));
                }
                else if (reg & INTERLACE_FLAG) // interlace
                {
                        throw_pb_flag = 0;
                        
                        if(debug_flag&1)
                            printk("interlace, picture type %d\n", picture_type);

                        vfpool_idx[fill_ptr] = buffer_index;
                        vf = &vfpool[fill_ptr];
                        set_frame_info(vf, &dur);
                        vf->bufWidth = 1920;
                        pic_type = 2;
                        if ((I_PICTURE == picture_type) && pts_valid)
                        {
                                vf->pts = pts;
                                if ((repeat_count > 1) && avi_flag)
                                {
                                        //next_pts = pts + (vavs_amstream_dec_info.rate * repeat_count >> 1)*15/16;
                                        next_pts = pts + (dur * repeat_count >> 1)*15/16;
                                }
                                else
                                {
                                        next_pts = 0;
                                }
                        }
                        else
                        {
                                vf->pts = next_pts;
                                if ((repeat_count > 1) && avi_flag)
                                {
                                        //vf->duration = vavs_amstream_dec_info.rate * repeat_count >> 1;
                                        vf->duration = dur * repeat_count >> 1;
                                        if (next_pts != 0)
                                        {
                                                next_pts += ((vf->duration) - ((vf->duration)>>4));
                                        }
                                }
                                else
                                {
                                        //vf->duration = vavs_amstream_dec_info.rate >> 1;
                                        vf->duration = dur >> 1;
                                        next_pts = 0;
                                }
                        }

                        vf->duration_pulldown = 0;
                        vf->type = (reg & TOP_FIELD_FIRST_FLAG) ? VIDTYPE_INTERLACE_TOP:VIDTYPE_INTERLACE_BOTTOM;
#ifdef NV21
                        vf->type |= VIDTYPE_VIU_NV21;
#endif
                        vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);

                        if(debug_flag&1)
                            printk("buffer_index %d, canvas addr %x\n",buffer_index, vf->canvas0Addr);

                        vfbuf_use[buffer_index]++;

                        INCPTR(fill_ptr);
                        vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);

                        vfpool_idx[fill_ptr] = buffer_index;
                        vf = &vfpool[fill_ptr];
                        set_frame_info(vf, &dur);
                        vf->bufWidth = 1920;

                        vf->pts = next_pts;
                        if ((repeat_count > 1) && avi_flag)
                        {
                                //vf->duration = vavs_amstream_dec_info.rate * repeat_count >> 1;
                                vf->duration = dur * repeat_count >> 1;
                                if (next_pts != 0)
                                {
                                        next_pts += ((vf->duration) - ((vf->duration)>>4));
                                }
                        }
                        else
                        {
                                //vf->duration = vavs_amstream_dec_info.rate >> 1;
                                vf->duration = dur >> 1;
                                next_pts = 0;
                        }

                        vf->duration_pulldown = 0;
                        vf->type = (reg & TOP_FIELD_FIRST_FLAG) ? VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
#ifdef NV21
                        vf->type |= VIDTYPE_VIU_NV21;
#endif
                        vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);


                        vfbuf_use[buffer_index]++;

                        INCPTR(fill_ptr);
                        vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                        total_frame++;
                }
                else  // progressive
                {
                        throw_pb_flag = 0;
                        
                        if(debug_flag&1)
                            printk("progressive picture type %d\n", picture_type);
                            
                        vfpool_idx[fill_ptr] = buffer_index;
                        vf = &vfpool[fill_ptr];
                        set_frame_info(vf, &dur);
                        vf->bufWidth = 1920;
                        pic_type = 1;

                        if ((I_PICTURE == picture_type) && pts_valid)
                        {
                                vf->pts = pts;
                                if ((repeat_count > 1) && avi_flag)
                                {
                                        //next_pts = pts + (vavs_amstream_dec_info.rate * repeat_count)*15/16;
                                        next_pts = pts + (dur * repeat_count)*15/16;
                                }
                                else
                                {
                                        next_pts = 0;
                                }
                        }
                        else
                        {
                                vf->pts = next_pts;
                                if ((repeat_count > 1) && avi_flag)
                                {
                                        //vf->duration = vavs_amstream_dec_info.rate * repeat_count;
                                        vf->duration = dur * repeat_count;
                                        if (next_pts != 0)
                                        {
                                                next_pts += ((vf->duration) - ((vf->duration)>>4));
                                        }
                                }
                                else
                                {
                                        //vf->duration = vavs_amstream_dec_info.rate;
                                        vf->duration = dur;
                                        next_pts = 0;
                                }
                        }

                        vf->duration_pulldown = 0;
                        vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
#ifdef NV21
                        vf->type |= VIDTYPE_VIU_NV21;
#endif
                        vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);
                        
                        if(debug_flag&1)
                            printk("buffer_index %d, canvas addr %x\n",buffer_index, vf->canvas0Addr);

                        vfbuf_use[buffer_index]++;

                        INCPTR(fill_ptr);
                        vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                        total_frame++;
                }


                //printk("PicType = %d, PTS = 0x%x\n", picture_type, vf->pts);
                WRITE_VREG(AVS_BUFFEROUT, 0);
        }

        WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

#ifdef HANDLE_AVS_IRQ
        return IRQ_HANDLED;
#else
        return;
#endif
}

static int run_flag=1;
static int step_flag=0;
static vframe_t *vavs_vf_peek(void* op_arg)
{
        if(run_flag==0)
            return NULL;
        if (get_ptr == fill_ptr)
                return NULL;

        return &vfpool[get_ptr];
}

static vframe_t *vavs_vf_get(void* op_arg)
{
        vframe_t *vf;
        if(run_flag==0)
            return NULL;

        if (get_ptr == fill_ptr)
                return NULL;

        vf = &vfpool[get_ptr];

        INCPTR(get_ptr);
        if(step_flag)
            run_flag=0;        
        return vf;
}

static void vavs_vf_put(vframe_t *vf, void* op_arg)
{
        INCPTR(putting_ptr);
}

int vavs_dec_status(struct vdec_status *vstatus)
{
    vstatus->width = frame_width ;//vavs_amstream_dec_info.width;
    vstatus->height = frame_height;//vavs_amstream_dec_info.height;
    if(0!= frame_dur/*vavs_amstream_dec_info.rate*/)
        vstatus->fps = 96000/frame_dur;// vavs_amstream_dec_info.rate;
    else
        vstatus->fps = 96000;
    vstatus->error_count = READ_VREG(AVS_ERROR_COUNT);
    vstatus->status = stat;

    return 0;
}

/****************************************/
static void vavs_canvas_init(void)
{
        int i;
        u32 canvas_width, canvas_height;
        u32 decbuf_size, decbuf_y_size, decbuf_uv_size;
        u32 disp_addr = 0xffffffff;
        int canvas_num = 3;

        if (buf_size <= 0x00400000)
        {
                /* SD only */
                canvas_width = 768;
                canvas_height = 576;
                decbuf_y_size = 0x80000;
                decbuf_uv_size = 0x20000;
                decbuf_size = 0x100000;
        printk("avs (SD only): buf_start %x, buf_size %x, buf_offset %x\n", buf_start, buf_size, buf_offset);
        }
        else
        {
                /* HD & SD */
                canvas_width = 1920;
                canvas_height = 1088;
                decbuf_y_size = 0x200000;
                decbuf_uv_size = 0x80000;
                decbuf_size = 0x300000;
        printk("avs: buf_start %x, buf_size %x, buf_offset %x\n", buf_start, buf_size, buf_offset);
        }

        if (READ_MPEG_REG(VPP_MISC) & VPP_VD1_POSTBLEND)
        {
	        canvas_t cur_canvas;

	        canvas_read((READ_MPEG_REG(VD1_IF0_CANVAS0) & 0xff), &cur_canvas);
            disp_addr = (cur_canvas.addr + 7) >> 3;
        }

        for (i = 0; i < 4; i++)
        {
            if (((buf_start + i * decbuf_size + 7) >> 3) == disp_addr)
            {
#ifdef NV21
                canvas_config(canvas_num * i + 0,
                        buf_start + 4 * decbuf_size,
                        canvas_width, canvas_height,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 1,
                        buf_start + 4 * decbuf_size + decbuf_y_size,
                        canvas_width, canvas_height / 2,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#else
                canvas_config(canvas_num * i + 0,
                        buf_start + 4 * decbuf_size,
                        canvas_width, canvas_height,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 1,
                        buf_start + 4 * decbuf_size + decbuf_y_size,
                        canvas_width / 2, canvas_height / 2,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 2,
                        buf_start + 4 * decbuf_size + decbuf_y_size + decbuf_uv_size,
                        canvas_width/2, canvas_height/2,
                        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif                        
                if(debug_flag&1)
                    printk("canvas config %d, addr %x\n", 4, buf_start + 4 * decbuf_size);        

            }
            else
            {
#ifdef NV21
                canvas_config(canvas_num * i + 0,
                              buf_start + i * decbuf_size,
                              canvas_width, canvas_height,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 1,
                              buf_start + i * decbuf_size + decbuf_y_size,
                              canvas_width, canvas_height / 2,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#else
                canvas_config(canvas_num * i + 0,
                              buf_start + i * decbuf_size,
                              canvas_width, canvas_height,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 1,
                              buf_start + i * decbuf_size + decbuf_y_size,
                              canvas_width / 2, canvas_height / 2,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(canvas_num * i + 2,
                              buf_start + i * decbuf_size + decbuf_y_size + decbuf_uv_size,
                              canvas_width / 2, canvas_height / 2,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif
                if(debug_flag&1)
                    printk("canvas config %d, addr %x\n", i, buf_start + i * decbuf_size);        
            }
        }
}

static void vavs_prot_init(void)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET0, 0);

    READ_VREG(DOS_SW_RESET0);

    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET0, 0);

    WRITE_VREG(DOS_SW_RESET0, (1<<9) | (1<<8));
    WRITE_VREG(DOS_SW_RESET0, 0);

#else
        WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC | RESET_VLD_PART);
        READ_MPEG_REG(RESET0_REGISTER);
        WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC | RESET_VLD_PART);

        WRITE_MPEG_REG(RESET2_REGISTER, RESET_PIC_DC | RESET_DBLK);
#endif

        WRITE_VREG(POWER_CTL_VLD, 0x10);
    	WRITE_VREG_BITS(VLD_MEM_VIFIFO_CONTROL, 2, MEM_FIFO_CNT_BIT, 2);
    	WRITE_VREG_BITS(VLD_MEM_VIFIFO_CONTROL, 8, MEM_LEVEL_CNT_BIT, 6);

        vavs_canvas_init();

#ifdef NV21
        WRITE_VREG(AV_SCRATCH_0, 0x010100);
        WRITE_VREG(AV_SCRATCH_1, 0x040403);
        WRITE_VREG(AV_SCRATCH_2, 0x070706);
        WRITE_VREG(AV_SCRATCH_3, 0x0a0a09);
#else
        /* index v << 16 | u << 8 | y */
        WRITE_VREG(AV_SCRATCH_0, 0x020100);
        WRITE_VREG(AV_SCRATCH_1, 0x050403);
        WRITE_VREG(AV_SCRATCH_2, 0x080706);
        WRITE_VREG(AV_SCRATCH_3, 0x0b0a09);
#endif
        /* notify ucode the buffer offset */
        WRITE_VREG(AV_SCRATCH_F, buf_offset);

        /* disable PSCALE for hardware sharing */
        WRITE_VREG(PSCALE_CTRL, 0);

        WRITE_VREG(AVS_SOS_COUNT, 0);
        WRITE_VREG(AVS_BUFFERIN, 0);
        WRITE_VREG(AVS_BUFFEROUT, 0);

        /* clear mailbox interrupt */
        WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

        /* enable mailbox interrupt */
        WRITE_VREG(ASSIST_MBOX1_MASK, 1);
#if 1 //def DEBUG_UCODE
        WRITE_VREG(AV_SCRATCH_D, 0);
#endif

#ifdef NV21
        SET_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
#endif

#ifdef PIC_DC_NEED_CLEAR
	CLEAR_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<31);
#endif

}

static void vavs_local_init(void)
{
        int i;

        vavs_ratio = vavs_amstream_dec_info.ratio;

        avi_flag = (u32)vavs_amstream_dec_info.param;

        fill_ptr = get_ptr = put_ptr = putting_ptr = 0;

        frame_width = frame_height = frame_dur = frame_prog = 0;

        throw_pb_flag = 1;
        
        total_frame = 0;

        next_pts = 0;

#ifdef DEBUG_PTS
        pts_hit = pts_missed = pts_i_hit = pts_i_missed = 0;
#endif

        for (i = 0; i < 4; i++)
                vfbuf_use[i] = 0;
}

static void vavs_put_timer_func(unsigned long arg)
{
        struct timer_list *timer = (struct timer_list *)arg;

#ifndef HANDLE_AVS_IRQ
        vavs_isr();
#endif

#if 0
        if ( READ_VREG(AVS_SOS_COUNT) > 10 )
        {
                amvdec_stop();
                vf_light_unreg_provider(&vavs_vf_prov);
                vavs_local_init();
                vavs_prot_init();
                vf_reg_provider(&vavs_vf_prov);
                amvdec_start();
        }
#endif

        if ((putting_ptr != put_ptr) && (READ_VREG(AVS_BUFFERIN) == 0))
        {
                u32 index = vfpool_idx[put_ptr];

                if (--vfbuf_use[index] == 0)
                {
                        WRITE_VREG(AVS_BUFFERIN, ~(1<<index));
                }

                INCPTR(put_ptr);
        }

        timer->expires = jiffies + PUT_INTERVAL;

        add_timer(timer);
}

static s32 vavs_init(void)
{
        printk("vavs_init\n");
        init_timer(&recycle_timer);

        stat |= STAT_TIMER_INIT;

        amvdec_enable();
        
        vavs_local_init();

        if(debug_flag&2){
            if (amvdec_loadmc(vavs_mc_debug) < 0)
            {
                    amvdec_disable();
                    printk("failed\n");
                    return -EBUSY;
            }
        }
        else{
            if (amvdec_loadmc(vavs_mc) < 0)
            {
                    amvdec_disable();
                    printk("failed\n");
                    return -EBUSY;
            }
        }
        
        stat |= STAT_MC_LOAD;

        /* enable AMRISC side protocol */
        vavs_prot_init();

#ifdef HANDLE_AVS_IRQ
        if (request_irq(INT_AMVENCODER, vavs_isr,
                        IRQF_SHARED, "vavs-irq", (void *)vavs_dec_id))
        {
                amvdec_disable();
                printk("vavs irq register error.\n");
                return -ENOENT;
        }
#endif

        stat |= STAT_ISR_REG;

 #ifdef CONFIG_POST_PROCESS_MANAGER
    vf_provider_init(&vavs_vf_prov, PROVIDER_NAME, &vavs_vf_provider, NULL);
    vf_reg_provider(&vavs_vf_prov);
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
 #else 
    vf_provider_init(&vavs_vf_prov, PROVIDER_NAME, &vavs_vf_provider, NULL);
    vf_reg_provider(&vavs_vf_prov);
 #endif 

        vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_FR_HINT, (void *)vavs_amstream_dec_info.rate);

        stat |= STAT_VF_HOOK;

        recycle_timer.data = (ulong) & recycle_timer;
        recycle_timer.function = vavs_put_timer_func;
        recycle_timer.expires = jiffies + PUT_INTERVAL;

        add_timer(&recycle_timer);

        stat |= STAT_TIMER_ARM;

        amvdec_start();

        stat |= STAT_VDEC_RUN;

        set_vdec_func(&vavs_dec_status);

        return 0;
}

static int amvdec_avs_probe(struct platform_device *pdev)
{
        struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;

        if (pdata == NULL)
        {
                printk("amvdec_avs memory resource undefined.\n");
                return -EFAULT;
        }

        buf_start = pdata->mem_start;
        buf_size = pdata->mem_end - pdata->mem_start + 1;

        if(buf_start>ORI_BUFFER_START_ADDR)
            buf_offset = buf_start - ORI_BUFFER_START_ADDR;
        else
            buf_offset = buf_start;

	if (pdata->sys_info)
            vavs_amstream_dec_info = *pdata->sys_info;

        printk("%s (%d,%d) %d\n", __func__, vavs_amstream_dec_info.width, vavs_amstream_dec_info.height, vavs_amstream_dec_info.rate);
        if (vavs_init() < 0)
        {
                printk("amvdec_avs init failed.\n");

                return -ENODEV;
        }

        return 0;
}

static int amvdec_avs_remove(struct platform_device *pdev)
{
        if (stat & STAT_VDEC_RUN)
        {
                amvdec_stop();
                stat &= ~STAT_VDEC_RUN;
        }

        if (stat & STAT_ISR_REG)
        {
                free_irq(INT_AMVENCODER, (void *)vavs_dec_id);
                stat &= ~STAT_ISR_REG;
        }

        if (stat & STAT_TIMER_ARM)
        {
                del_timer_sync(&recycle_timer);
                stat &= ~STAT_TIMER_ARM;
        }

        if (stat & STAT_VF_HOOK)
        {
                vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_FR_END_HINT, NULL);

                vf_unreg_provider(&vavs_vf_prov);
                stat &= ~STAT_VF_HOOK;
        }

        amvdec_disable();
       
        pic_type = 0;
#ifdef DEBUG_PTS
       printk("pts hit %d, pts missed %d, i hit %d, missed %d\n", pts_hit, pts_missed, pts_i_hit, pts_i_missed);
       printk("total frame %d, avi_flag %d, rate %d\n", total_frame, avi_flag, vavs_amstream_dec_info.rate);
#endif

        return 0;
}

/****************************************/

static struct platform_driver amvdec_avs_driver = {
        .probe  = amvdec_avs_probe,
        .remove = amvdec_avs_remove,
        .driver = {
                .name = DRIVER_NAME,
        }
};

static int __init amvdec_avs_driver_init_module(void)
{
        printk("amvdec_avs module init\n");

        if (platform_driver_register(&amvdec_avs_driver))
        {
                printk("failed to register amvdec_avs driver\n");
                return -ENODEV;
        }

        return 0;
}

static void __exit amvdec_avs_driver_remove_module(void)
{
        printk("amvdec_avs module remove.\n");

        platform_driver_unregister(&amvdec_avs_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_avs stat \n");

module_param(run_flag, uint, 0664);
MODULE_PARM_DESC(run_flag, "\n run_flag\n");

module_param(step_flag, uint, 0664);
MODULE_PARM_DESC(step_flag, "\n step_flag\n");

module_param(debug_flag, uint, 0664);
MODULE_PARM_DESC(debug_flag, "\n debug_flag\n");

module_param(pic_type, uint, 0444);
MODULE_PARM_DESC(pic_type, "\n amdec_vas picture type \n");

module_init(amvdec_avs_driver_init_module);
module_exit(amvdec_avs_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC AVS Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Qi Wang <qi.wang@amlogic.com>");

