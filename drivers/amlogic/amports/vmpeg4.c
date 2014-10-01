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
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <mach/am_regs.h>
#include <plat/io.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>

#include "vdec_reg.h"
#include "vmpeg4.h"

//#define CONFIG_AM_VDEC_MPEG4_LOG
#ifdef CONFIG_AM_VDEC_MPEG4_LOG
#define AMLOG
#define LOG_LEVEL_VAR       amlog_level_vmpeg4
#define LOG_MASK_VAR        amlog_mask_vmpeg4
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_DESC  "0:ERROR, 1:INFO"
#define LOG_MASK_PTS    0x01
#define LOG_MASK_DESC   "0x01:DEBUG_PTS"
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(LOG_LEVEL_ERROR, 0, LOG_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

#include "amvdec.h"
#include "vmpeg4_mc.h"

#define DRIVER_NAME "amvdec_mpeg4"
#define MODULE_NAME "amvdec_mpeg4"

#define DEBUG_PTS

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
#define NV21
#endif

#define I_PICTURE   0
#define P_PICTURE   1
#define B_PICTURE   2

#define ORI_BUFFER_START_ADDR   0x01000000

#define INTERLACE_FLAG          0x80
#define BOTTOM_FIELD_FIRST_FLAG 0x40

/* protocol registers */
#define MP4_PIC_RATIO       AV_SCRATCH_5
#define MP4_RATE            AV_SCRATCH_3
#define MP4_ERR_COUNT       AV_SCRATCH_6
#define MP4_PIC_WH          AV_SCRATCH_7
#define MREG_BUFFERIN       AV_SCRATCH_8
#define MREG_BUFFEROUT      AV_SCRATCH_9
#define MP4_NOT_CODED_CNT   AV_SCRATCH_A
#define MP4_VOP_TIME_INC    AV_SCRATCH_B
#define MP4_OFFSET_REG      AV_SCRATCH_C
#define MEM_OFFSET_REG      AV_SCRATCH_F

#define PARC_FORBIDDEN              0
#define PARC_SQUARE                 1
#define PARC_CIF                    2
#define PARC_10_11                  3
#define PARC_16_11                  4
#define PARC_40_33                  5
#define PARC_RESERVED               6
/* values between 6 and 14 are reserved */
#define PARC_EXTENDED              15

#define VF_POOL_SIZE          16
#define DECODE_BUFFER_NUM_MAX 4
#define PUT_INTERVAL        HZ/100



#define RATE_DETECT_COUNT   5
#define DURATION_UNIT       96000
#define PTS_UNIT            90000

#define DUR2PTS(x) ((x) - ((x) >> 4))

static vframe_t *vmpeg_vf_peek(void*);
static vframe_t *vmpeg_vf_get(void*);
static void vmpeg_vf_put(vframe_t *, void*);
static int  vmpeg_vf_states(vframe_states_t *states, void*);
static int vmpeg_event_cb(int type, void *data, void *private_data);

static void vmpeg4_prot_init(void);
static void vmpeg4_local_init(void);

static const char vmpeg4_dec_id[] = "vmpeg4-dev";

#define PROVIDER_NAME   "decoder.mpeg4"

extern int query_video_status(int type, int *value);

static const struct vframe_operations_s vmpeg_vf_provider = {
    .peek = vmpeg_vf_peek,
    .get = vmpeg_vf_get,
    .put = vmpeg_vf_put,
    .event_cb = vmpeg_event_cb,
    .vf_states = vmpeg_vf_states,
};
static struct vframe_provider_s vmpeg_vf_prov;

static DECLARE_KFIFO(newframe_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(display_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(recycle_q, vframe_t *, VF_POOL_SIZE);

static struct vframe_s vfpool[VF_POOL_SIZE];
static s32 vfbuf_use[DECODE_BUFFER_NUM_MAX];
static u32 frame_width, frame_height, frame_dur, frame_prog;
static struct timer_list recycle_timer;
static u32 stat;
static u32 buf_start, buf_size, buf_offset;
static u32 vmpeg4_ratio;
static u64 vmpeg4_ratio64;
static u32 rate_detect;
static u32 vmpeg4_rotation;

static u32 total_frame;
static u32 last_vop_time_inc, last_duration;
static u32 last_anch_pts, vop_time_inc_since_last_anch, frame_num_since_last_anch;
static u64 last_anch_pts_us64;

#ifdef CONFIG_AM_VDEC_MPEG4_LOG
u32 pts_hit, pts_missed, pts_i_hit, pts_i_missed;
#endif

static DEFINE_SPINLOCK(lock);

static struct dec_sysinfo vmpeg4_amstream_dec_info;

static unsigned char aspect_ratio_table[16] = {
    PARC_FORBIDDEN,
    PARC_SQUARE,
    PARC_CIF,
    PARC_10_11,
    PARC_16_11,
    PARC_40_33,
    PARC_RESERVED, PARC_RESERVED, PARC_RESERVED, PARC_RESERVED,
    PARC_RESERVED, PARC_RESERVED, PARC_RESERVED, PARC_RESERVED,
    PARC_RESERVED, PARC_EXTENDED
};

static inline u32 index2canvas(u32 index)
{
    const u32 canvas_tab[4] = {
#ifdef NV21
        0x010100, 0x030302, 0x050504, 0x070706
#else
        0x020100, 0x050403, 0x080706, 0x0b0a09
#endif
    };

    return canvas_tab[index];
}

static void set_aspect_ratio(vframe_t *vf, unsigned pixel_ratio)
{
    int ar = 0;
    unsigned int num = 0;
    unsigned int den = 0;

	if (vmpeg4_ratio64 != 0) {
		num = vmpeg4_ratio64>>32;
		den = vmpeg4_ratio64 & 0xffffffff;
	} else {		
		num = vmpeg4_ratio>>16;
		den = vmpeg4_ratio & 0xffff;
	
	}
    if ((num == 0) || (den == 0)) {
        num = 1;
        den = 1;
    }
    
    if (vmpeg4_ratio == 0) {
        vf->ratio_control |= (0x90 << DISP_RATIO_ASPECT_RATIO_BIT); // always stretch to 16:9
    } else if (pixel_ratio > 0x0f) {
        num = (pixel_ratio >> 8) * vmpeg4_amstream_dec_info.width * num;
        ar = div_u64((pixel_ratio & 0xff) * vmpeg4_amstream_dec_info.height * den * 0x100ULL + (num>>1),  num);
    } else {
        switch (aspect_ratio_table[pixel_ratio]) {
        case 0:
            num = vmpeg4_amstream_dec_info.width * num;
            ar = (vmpeg4_amstream_dec_info.height * den * 0x100 +  (num>>1))/ num;
            break;
        case 1: 
            num = vf->width * num;
            ar = (vf->height * den * 0x100 + (num>>1)) / num;
            break;
        case 2:
            num = (vf->width * 12) * num;
            ar = (vf->height * den * 0x100  * 11 + ((num)>>1)) / num;
            break;
        case 3:
            num = (vf->width * 10) * num;
            ar = (vf->height * den * 0x100  * 11 + (num>>1))/ num ;
            break;
        case 4:
            num = (vf->width * 16) * num;
            ar = (vf->height * den * 0x100  * 11 + (num>>1)) / num;
            break;
        case 5:
            num = (vf->width * 40) * num;
            ar = (vf->height * den * 0x100 * 33 + (num>>1))/ num;
            break;
        default:
            num = vf->width * num;
            ar = (vf->height * den * 0x100  + (num>>1)) / num;
            break;
        }
    }

    ar = min(ar, DISP_RATIO_ASPECT_RATIO_MAX);

    vf->ratio_control = (ar << DISP_RATIO_ASPECT_RATIO_BIT);
}

static irqreturn_t vmpeg4_isr(int irq, void *dev_id)
{
    u32 reg;
    vframe_t *vf = NULL;
    u32 picture_type;
    u32 buffer_index;
    u32 pts, pts_valid = 0, offset = 0;
    u64 pts_us64 = 0;
    u32 rate, vop_time_inc, repeat_cnt, duration = 3200;

    reg = READ_VREG(MREG_BUFFEROUT);

    if (reg) {
        buffer_index = ((reg & 0x7) - 1) & 3;
        picture_type = (reg >> 3) & 7;
        rate = READ_VREG(MP4_RATE);
        repeat_cnt = READ_VREG(MP4_NOT_CODED_CNT);
        vop_time_inc = READ_VREG(MP4_VOP_TIME_INC);

        if (buffer_index >= DECODE_BUFFER_NUM_MAX) {
            printk("fatal error, invalid buffer index.");
            return IRQ_HANDLED;
        }

        if (vmpeg4_amstream_dec_info.width == 0) {
            vmpeg4_amstream_dec_info.width = READ_VREG(MP4_PIC_WH) >> 16;
        }
#if 0
        else {
            printk("info width = %d, ucode width = %d\n",
                   vmpeg4_amstream_dec_info.width,
                   READ_VREG(MP4_PIC_WH) >> 16);
        }
#endif

        if (vmpeg4_amstream_dec_info.height == 0) {
            vmpeg4_amstream_dec_info.height = READ_VREG(MP4_PIC_WH) & 0xffff;
        }
#if 0
        else {
            printk("info height = %d, ucode height = %d\n",
                   vmpeg4_amstream_dec_info.height,
                   READ_VREG(MP4_PIC_WH) & 0xffff);
        }
#endif
        if (vmpeg4_amstream_dec_info.rate == 0) {
            if ((rate >> 16) != 0) {
                /* fixed VOP rate */
                vmpeg4_amstream_dec_info.rate = (rate & 0xffff) * DURATION_UNIT / (rate >> 16);
                duration = vmpeg4_amstream_dec_info.rate;
            } else if (rate_detect < RATE_DETECT_COUNT) {
                if (vop_time_inc < last_vop_time_inc) {
                    duration = vop_time_inc + rate - last_vop_time_inc;
                } else {
                    duration = vop_time_inc - last_vop_time_inc;
                }

                if (duration == last_duration) {
                    rate_detect++;
                    if (rate_detect >= RATE_DETECT_COUNT) {
                        vmpeg4_amstream_dec_info.rate = duration * DURATION_UNIT / rate;
                        duration = vmpeg4_amstream_dec_info.rate;
                    }
                } else {
                    rate_detect = 0;
                }

                last_duration = duration;
            }
        } else {
            duration = vmpeg4_amstream_dec_info.rate;
#if 0
            printk("info rate = %d, ucode rate = 0x%x:0x%x\n",
                   vmpeg4_amstream_dec_info.rate,
                   READ_VREG(MP4_RATE),
                   vop_time_inc);
#endif
        }

        if ((I_PICTURE == picture_type) || (P_PICTURE == picture_type)) {
            offset = READ_VREG(MP4_OFFSET_REG);
	 	/*2500-->3000,because some mpeg4 video may checkout failed;
                 may have av sync problem.can changed small later.
		 263 may need small?
           */
            if (pts_lookup_offset_us64(PTS_TYPE_VIDEO, offset, &pts, 3000, &pts_us64) == 0) {
                pts_valid = 1;
                last_anch_pts = pts;
                last_anch_pts_us64 = pts_us64;
#ifdef CONFIG_AM_VDEC_MPEG4_LOG
                pts_hit++;
#endif
            } else {
#ifdef CONFIG_AM_VDEC_MPEG4_LOG
                pts_missed++;
#endif
            }
#ifdef CONFIG_AM_VDEC_MPEG4_LOG
            amlog_mask(LOG_MASK_PTS, "I offset 0x%x, pts_valid %d pts=0x%x\n", offset, pts_valid, pts);
#endif
        }

        if (pts_valid) {
            last_anch_pts = pts;
            last_anch_pts_us64 = pts_us64;
            frame_num_since_last_anch = 0;
            vop_time_inc_since_last_anch = 0;
        } else {
            pts = last_anch_pts;
            pts_us64 = last_anch_pts_us64;

            if ((rate != 0) && ((rate >> 16) == 0) && vmpeg4_amstream_dec_info.rate==0) {
                /* variable PTS rate */
		/*bug on variable pts calc,
		 do as dixed vop first if we have rate setting before.
		*/
                if (vop_time_inc > last_vop_time_inc) {
                    vop_time_inc_since_last_anch += vop_time_inc - last_vop_time_inc;
                } else {
                    vop_time_inc_since_last_anch += vop_time_inc + rate - last_vop_time_inc;
                }

                pts += vop_time_inc_since_last_anch * PTS_UNIT / rate;
                pts_us64 += (vop_time_inc_since_last_anch * PTS_UNIT / rate) * 100 / 9;

                if (vop_time_inc_since_last_anch > (1 << 14)) {
                    /* avoid overflow */
                    last_anch_pts = pts;
                    last_anch_pts_us64 = pts_us64;
                    vop_time_inc_since_last_anch = 0;
                }
            } else {
                /* fixed VOP rate */
                frame_num_since_last_anch++;
                pts += DUR2PTS(frame_num_since_last_anch * vmpeg4_amstream_dec_info.rate);
                pts_us64 += DUR2PTS(frame_num_since_last_anch * vmpeg4_amstream_dec_info.rate) * 100 / 9;

                if (frame_num_since_last_anch > (1 << 15)) {
                    /* avoid overflow */
                    last_anch_pts = pts;
                    last_anch_pts_us64 = pts_us64;
                    frame_num_since_last_anch = 0;
                }
            }
        }

        if (reg & INTERLACE_FLAG) { // interlace
            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            vf->index = buffer_index;
            vf->width = vmpeg4_amstream_dec_info.width;
            vf->height = vmpeg4_amstream_dec_info.height;
            vf->bufWidth = 1920;
            vf->flag = 0;
            vf->orientation = vmpeg4_rotation;
            vf->pts = pts;
            vf->pts_us64 = pts_us64;
            vf->duration = duration >> 1;
            vf->duration_pulldown = 0;
            vf->type = (reg & BOTTOM_FIELD_FIRST_FLAG) ? VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
#ifdef NV21
            vf->type |= VIDTYPE_VIU_NV21;
#endif
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);

            set_aspect_ratio(vf, READ_VREG(MP4_PIC_RATIO));

            vfbuf_use[buffer_index]++;

            kfifo_put(&display_q, (const vframe_t **)&vf);

            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);

            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            vf->index = buffer_index;
            vf->width = vmpeg4_amstream_dec_info.width;
            vf->height = vmpeg4_amstream_dec_info.height;
            vf->bufWidth = 1920;
            vf->flag = 0;
            vf->orientation = vmpeg4_rotation;

            vf->pts = 0;
            vf->pts_us64 = 0;
            vf->duration = duration >> 1;

            vf->duration_pulldown = 0;
            vf->type = (reg & BOTTOM_FIELD_FIRST_FLAG) ? VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
#ifdef NV21
            vf->type |= VIDTYPE_VIU_NV21;
#endif
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);

            set_aspect_ratio(vf, READ_VREG(MP4_PIC_RATIO));

            vfbuf_use[buffer_index]++;

            amlog_mask(LOG_MASK_PTS, "[%s:%d] [interlaced] dur=0x%x rate=%d picture_type=%d\n",
                       __FUNCTION__, __LINE__,
                       vf->duration, vmpeg4_amstream_dec_info.rate, picture_type);

            kfifo_put(&display_q, (const vframe_t **)&vf);

            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);

        } else { // progressive
            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            vf->index = buffer_index;
            vf->width = vmpeg4_amstream_dec_info.width;
            vf->height = vmpeg4_amstream_dec_info.height;
            vf->bufWidth = 1920;
            vf->flag = 0;
            vf->orientation = vmpeg4_rotation;
            vf->pts = pts;
            vf->pts_us64 = pts_us64;
            vf->duration = duration;
            vf->duration_pulldown = repeat_cnt * duration;
#ifdef NV21
            vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
#else
            vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
#endif
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(buffer_index);

            set_aspect_ratio(vf, READ_VREG(MP4_PIC_RATIO));

            amlog_mask(LOG_MASK_PTS, "[%s:%d] [prog] dur=0x%x rate=%d picture_type=%d\n",
                       __FUNCTION__, __LINE__, vf->duration, vmpeg4_amstream_dec_info.rate, picture_type);

            vfbuf_use[buffer_index]++;

            kfifo_put(&display_q, (const vframe_t **)&vf);

            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
        }

        total_frame += repeat_cnt + 1;

        WRITE_VREG(MREG_BUFFEROUT, 0);

        last_vop_time_inc = vop_time_inc;
    }

    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

    return IRQ_HANDLED;
}

static vframe_t *vmpeg_vf_peek(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_peek(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static vframe_t *vmpeg_vf_get(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_get(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static void vmpeg_vf_put(vframe_t *vf, void* op_arg)
{
    kfifo_put(&recycle_q, (const vframe_t **)&vf);
}

static int vmpeg_event_cb(int type, void *data, void *private_data)
{
    if(type & VFRAME_EVENT_RECEIVER_RESET){
        unsigned long flags;
        amvdec_stop();
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_light_unreg_provider(&vmpeg_vf_prov);
#endif
        spin_lock_irqsave(&lock, flags);
        vmpeg4_local_init();
        vmpeg4_prot_init();
        spin_unlock_irqrestore(&lock, flags); 
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_reg_provider(&vmpeg_vf_prov);
#endif              
        amvdec_start();
    }
    return 0;        
}

static int  vmpeg_vf_states(vframe_states_t *states, void* op_arg)
{
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);

    states->vf_pool_size = VF_POOL_SIZE;
    states->buf_free_num = kfifo_len(&newframe_q);
    states->buf_avail_num = kfifo_len(&display_q);
    states->buf_recycle_num = kfifo_len(&recycle_q);
    
    spin_unlock_irqrestore(&lock, flags);

    return 0;
}

static void vmpeg_put_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;

    while (!kfifo_is_empty(&recycle_q) &&
           (READ_VREG(MREG_BUFFERIN) == 0)) {
        vframe_t *vf;
        if (kfifo_get(&recycle_q, &vf)) {
            if ((vf->index >= 0) && (--vfbuf_use[vf->index] == 0)) {
                WRITE_VREG(MREG_BUFFERIN, ~(1 << vf->index));
                vf->index = -1;
            }

            kfifo_put(&newframe_q, (const vframe_t **)&vf);
        }
    }

    timer->expires = jiffies + PUT_INTERVAL;

    add_timer(timer);
}

int vmpeg4_dec_status(struct vdec_status *vstatus)
{
    vstatus->width = vmpeg4_amstream_dec_info.width;
    vstatus->height = vmpeg4_amstream_dec_info.height;
    if (0 != vmpeg4_amstream_dec_info.rate) {
        vstatus->fps = DURATION_UNIT / vmpeg4_amstream_dec_info.rate;
    } else {
        vstatus->fps = DURATION_UNIT;
    }
    vstatus->error_count = READ_VREG(MP4_ERR_COUNT);
    vstatus->status = stat;

    return 0;
}

/****************************************/
static void vmpeg4_canvas_init(void)
{
    int i;
    u32 canvas_width, canvas_height;
    u32 decbuf_size, decbuf_y_size, decbuf_uv_size;
    u32 disp_addr = 0xffffffff;

    if (buf_size <= 0x00400000) {
        /* SD only */
        canvas_width = 768;
        canvas_height = 576;
        decbuf_y_size = 0x80000;
        decbuf_uv_size = 0x20000;
        decbuf_size = 0x100000;
    } else {
        /* HD & SD */
        if(vmpeg4_amstream_dec_info.height > vmpeg4_amstream_dec_info.width){
            canvas_width = 1088;
            canvas_height = 1920;            
        }
        else{
            canvas_width = 1920;
            canvas_height = 1088;
        }
        decbuf_y_size = 0x200000;
        decbuf_uv_size = 0x80000;
        decbuf_size = 0x300000;
    }

    if(is_vpp_postblend()){
        canvas_t cur_canvas;

        canvas_read((READ_VCBUS_REG(VD1_IF0_CANVAS0) & 0xff), &cur_canvas);
        disp_addr = (cur_canvas.addr + 7) >> 3;
    }

    for (i = 0; i < 4; i++) {
        if (((buf_start + i * decbuf_size + 7) >> 3) == disp_addr) {
#ifdef NV21
            canvas_config(2 * i + 0,
                          buf_start + 4 * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(2 * i + 1,
                          buf_start + 4 * decbuf_size + decbuf_y_size,
                          canvas_width, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#else
            canvas_config(3 * i + 0,
                          buf_start + 4 * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 1,
                          buf_start + 4 * decbuf_size + decbuf_y_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 2,
                          buf_start + 4 * decbuf_size + decbuf_y_size + decbuf_uv_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif
        } else {
#ifdef NV21
            canvas_config(2 * i + 0,
                          buf_start + i * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(2 * i + 1,
                          buf_start + i * decbuf_size + decbuf_y_size,
                          canvas_width, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#else
            canvas_config(3 * i + 0,
                          buf_start + i * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 1,
                          buf_start + i * decbuf_size + decbuf_y_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 2,
                          buf_start + i * decbuf_size + decbuf_y_size + decbuf_uv_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif
        }
    }
}

static void vmpeg4_prot_init(void)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6));
    WRITE_VREG(DOS_SW_RESET0, 0);
#else
    WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC);
#endif

    vmpeg4_canvas_init();

    /* index v << 16 | u << 8 | y */
#ifdef NV21
    WRITE_VREG(AV_SCRATCH_0, 0x010100);
    WRITE_VREG(AV_SCRATCH_1, 0x030302);
    WRITE_VREG(AV_SCRATCH_2, 0x050504);
    WRITE_VREG(AV_SCRATCH_3, 0x070706);
#else
    WRITE_VREG(AV_SCRATCH_0, 0x020100);
    WRITE_VREG(AV_SCRATCH_1, 0x050403);
    WRITE_VREG(AV_SCRATCH_2, 0x080706);
    WRITE_VREG(AV_SCRATCH_3, 0x0b0a09);
#endif

    /* notify ucode the buffer offset */
    WRITE_VREG(AV_SCRATCH_F, buf_offset);

    /* disable PSCALE for hardware sharing */
    WRITE_VREG(PSCALE_CTRL, 0);

    WRITE_VREG(MREG_BUFFERIN, 0);
    WRITE_VREG(MREG_BUFFEROUT, 0);

    /* clear mailbox interrupt */
    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

    /* enable mailbox interrupt */
    WRITE_VREG(ASSIST_MBOX1_MASK, 1);

    /* clear repeat count */
    WRITE_VREG(MP4_NOT_CODED_CNT, 0);

#ifdef NV21
    SET_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    printk("mpeg4 meson8 prot init\n");
    WRITE_VREG(MDEC_PIC_DC_THRESH, 0x404038aa);
#endif

WRITE_VREG(MP4_PIC_WH, (vmpeg4_amstream_dec_info.width << 16) | vmpeg4_amstream_dec_info.height);
}

static void vmpeg4_local_init(void)
{
    int i;

    vmpeg4_ratio = vmpeg4_amstream_dec_info.ratio;
	
	vmpeg4_ratio64 = vmpeg4_amstream_dec_info.ratio64;

    vmpeg4_rotation = (((u32)vmpeg4_amstream_dec_info.param) >> 16) & 0xffff;

    frame_width = frame_height = frame_dur = frame_prog = 0;

    total_frame = 0;

    last_anch_pts = 0;

    last_anch_pts_us64 = 0;

    last_vop_time_inc = last_duration = 0;

    vop_time_inc_since_last_anch = 0;

    frame_num_since_last_anch = 0;

#ifdef CONFIG_AM_VDEC_MPEG4_LOG
    pts_hit = pts_missed = pts_i_hit = pts_i_missed = 0;
#endif

    for (i = 0; i < DECODE_BUFFER_NUM_MAX; i++) {
        vfbuf_use[i] = 0;
    }

    INIT_KFIFO(display_q);
    INIT_KFIFO(recycle_q);
    INIT_KFIFO(newframe_q);

    for (i=0; i<VF_POOL_SIZE; i++) {
        const vframe_t *vf = &vfpool[i];
        vfpool[i].index = -1;
        kfifo_put(&newframe_q, &vf);
    }
}

static s32 vmpeg4_init(void)
{
    int trickmode_fffb = 0;

    query_video_status(0, &trickmode_fffb);

    amlog_level(LOG_LEVEL_INFO, "vmpeg4_init\n");
    init_timer(&recycle_timer);

    stat |= STAT_TIMER_INIT;

    amvdec_enable();

    vmpeg4_local_init();

    if (vmpeg4_amstream_dec_info.format == VIDEO_DEC_FORMAT_MPEG4_3) {
        amlog_level(LOG_LEVEL_INFO, "load VIDEO_DEC_FORMAT_MPEG4_3\n");
        if (amvdec_loadmc(vmpeg4_mc_311) < 0) {
            amvdec_disable();

            amlog_level(LOG_LEVEL_ERROR, "VIDEO_DEC_FORMAT_MPEG4_3 ucode loading failed\n");
            return -EBUSY;
        }
    } else if (vmpeg4_amstream_dec_info.format == VIDEO_DEC_FORMAT_MPEG4_4) {
        amlog_level(LOG_LEVEL_INFO, "load VIDEO_DEC_FORMAT_MPEG4_4\n");
        if (amvdec_loadmc(vmpeg4_mc_4) < 0) {
            amvdec_disable();

            amlog_level(LOG_LEVEL_ERROR, "VIDEO_DEC_FORMAT_MPEG4_4 ucode loading failed\n");
            return -EBUSY;
        }
    } else if (vmpeg4_amstream_dec_info.format == VIDEO_DEC_FORMAT_MPEG4_5) {
        amlog_level(LOG_LEVEL_INFO, "load VIDEO_DEC_FORMAT_MPEG4_5\n");
        if (amvdec_loadmc(vmpeg4_mc_5) < 0) {
            amvdec_disable();

            amlog_level(LOG_LEVEL_ERROR, "VIDEO_DEC_FORMAT_MPEG4_5 ucode loading failed\n");
            return -EBUSY;
        }
    } else if (vmpeg4_amstream_dec_info.format == VIDEO_DEC_FORMAT_H263) {
        amlog_level(LOG_LEVEL_INFO, "load VIDEO_DEC_FORMAT_H263\n");
        if (amvdec_loadmc(h263_mc) < 0) {
            amvdec_disable();

            amlog_level(LOG_LEVEL_ERROR, "VIDEO_DEC_FORMAT_H263 ucode loading failed\n");
            return -EBUSY;
        }
    } else {
        amlog_level(LOG_LEVEL_ERROR, "not supported MPEG4 format\n");
    }

    stat |= STAT_MC_LOAD;

    /* enable AMRISC side protocol */
    vmpeg4_prot_init();

    if (request_irq(INT_VDEC, vmpeg4_isr,
                    IRQF_SHARED, "vmpeg4-irq", (void *)vmpeg4_dec_id)) {
        amvdec_disable();

        amlog_level(LOG_LEVEL_ERROR, "vmpeg4 irq register error.\n");
        return -ENOENT;
    }

    stat |= STAT_ISR_REG;
#ifdef CONFIG_POST_PROCESS_MANAGER
    vf_provider_init(&vmpeg_vf_prov, PROVIDER_NAME, &vmpeg_vf_provider, NULL);
    vf_reg_provider(&vmpeg_vf_prov);
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
#else 
    vf_provider_init(&vmpeg_vf_prov, PROVIDER_NAME, &vmpeg_vf_provider, NULL);
    vf_reg_provider(&vmpeg_vf_prov);
#endif 
    stat |= STAT_VF_HOOK;

    recycle_timer.data = (ulong) & recycle_timer;
    recycle_timer.function = vmpeg_put_timer_func;
    recycle_timer.expires = jiffies + PUT_INTERVAL;

    add_timer(&recycle_timer);

    stat |= STAT_TIMER_ARM;

    amvdec_start();

    stat |= STAT_VDEC_RUN;

    set_vdec_func(&vmpeg4_dec_status);

    return 0;
}

static int amvdec_mpeg4_probe(struct platform_device *pdev)
{
    struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;

    if (pdata == NULL) {
        amlog_level(LOG_LEVEL_ERROR, "amvdec_mpeg4 memory resource undefined.\n");
        return -EFAULT;
    }

    buf_start = pdata->mem_start;
    buf_size = pdata->mem_end - pdata->mem_start + 1;
    buf_offset = buf_start - ORI_BUFFER_START_ADDR;

    if (pdata->sys_info) {
        vmpeg4_amstream_dec_info = *pdata->sys_info;
    }

    if (vmpeg4_init() < 0) {
        amlog_level(LOG_LEVEL_ERROR, "amvdec_mpeg4 init failed.\n");

        return -ENODEV;
    }

    return 0;
}

static int amvdec_mpeg4_remove(struct platform_device *pdev)
{
    if (stat & STAT_VDEC_RUN) {
        amvdec_stop();
        stat &= ~STAT_VDEC_RUN;
    }

    if (stat & STAT_ISR_REG) {
        free_irq(INT_VDEC, (void *)vmpeg4_dec_id);
        stat &= ~STAT_ISR_REG;
    }

    if (stat & STAT_TIMER_ARM) {
        del_timer_sync(&recycle_timer);
        stat &= ~STAT_TIMER_ARM;
    }

    if (stat & STAT_VF_HOOK) {
        vf_unreg_provider(&vmpeg_vf_prov);
        stat &= ~STAT_VF_HOOK;
    }

    amvdec_disable();

    amlog_mask(LOG_MASK_PTS, "pts hit %d, pts missed %d, i hit %d, missed %d\n",
               pts_hit, pts_missed, pts_i_hit, pts_i_missed);
    amlog_mask(LOG_MASK_PTS, "total frame %d, rate %d\n",
               total_frame, vmpeg4_amstream_dec_info.rate);

    return 0;
}

/****************************************/

static struct platform_driver amvdec_mpeg4_driver = {
    .probe  = amvdec_mpeg4_probe,
    .remove = amvdec_mpeg4_remove,
#ifdef CONFIG_PM
    .suspend = amvdec_suspend,
    .resume  = amvdec_resume,
#endif
    .driver = {
        .name = DRIVER_NAME,
    }
};
static struct codec_profile_t amvdec_mpeg4_profile = {
	.name = "mpeg4",
	.profile = ""
};
static int __init amvdec_mpeg4_driver_init_module(void)
{
    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg4 module init\n");

    if (platform_driver_register(&amvdec_mpeg4_driver)) {
        amlog_level(LOG_LEVEL_ERROR, "failed to register amvdec_mpeg4 driver\n");
        return -ENODEV;
    }
	vcodec_profile_register(&amvdec_mpeg4_profile);
    return 0;
}

static void __exit amvdec_mpeg4_driver_remove_module(void)
{
    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg4 module remove.\n");

    platform_driver_unregister(&amvdec_mpeg4_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_mpeg4 stat \n");

module_init(amvdec_mpeg4_driver_init_module);
module_exit(amvdec_mpeg4_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC MPEG4 Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");

