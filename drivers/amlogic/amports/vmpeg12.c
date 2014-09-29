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
#include <mach/am_regs.h>
#include <plat/io.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>

#include "vdec_reg.h"
#include "vmpeg12.h"

#ifdef CONFIG_AM_VDEC_MPEG12_LOG
#define AMLOG
#define LOG_LEVEL_VAR       amlog_level_vmpeg
#define LOG_MASK_VAR        amlog_mask_vmpeg
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_DESC  "0:ERROR, 1:INFO"
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(LOG_LEVEL_ERROR, 0, LOG_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

#include "amvdec.h"
#include "vmpeg12_mc.h"

#define DRIVER_NAME "amvdec_mpeg12"
#define MODULE_NAME "amvdec_mpeg12"

/* protocol registers */
#define MREG_SEQ_INFO       AV_SCRATCH_4
#define MREG_PIC_INFO       AV_SCRATCH_5
#define MREG_PIC_WIDTH      AV_SCRATCH_6
#define MREG_PIC_HEIGHT     AV_SCRATCH_7
#define MREG_BUFFERIN       AV_SCRATCH_8
#define MREG_BUFFEROUT      AV_SCRATCH_9

#define MREG_CMD            AV_SCRATCH_A
#define MREG_CO_MV_START    AV_SCRATCH_B
#define MREG_ERROR_COUNT    AV_SCRATCH_C
#define MREG_FRAME_OFFSET   AV_SCRATCH_D
#define MREG_WAIT_BUFFER    AV_SCRATCH_E
#define MREG_FATAL_ERROR    AV_SCRATCH_F

#define PICINFO_ERROR       0x80000000
#define PICINFO_TYPE_MASK   0x00030000
#define PICINFO_TYPE_I      0x00000000
#define PICINFO_TYPE_P      0x00010000
#define PICINFO_TYPE_B      0x00020000

#define PICINFO_PROG        0x8000
#define PICINFO_RPT_FIRST   0x4000
#define PICINFO_TOP_FIRST   0x2000
#define PICINFO_FRAME       0x1000

#define SEQINFO_EXT_AVAILABLE   0x80000000
#define SEQINFO_PROG            0x00010000

#define VF_POOL_SIZE        32
#define DECODE_BUFFER_NUM_MAX 8
#define PUT_INTERVAL        HZ/100

#define INCPTR(p) ptr_atomic_wrap_inc(&p)



#define DEC_CONTROL_FLAG_FORCE_2500_720_576_INTERLACE  0x0002
#define DEC_CONTROL_FLAG_FORCE_3000_704_480_INTERLACE  0x0004
#define DEC_CONTROL_FLAG_FORCE_2500_704_576_INTERLACE  0x0008
#define DEC_CONTROL_FLAG_FORCE_2500_544_576_INTERLACE  0x0010
#define DEC_CONTROL_FLAG_FORCE_2500_480_576_INTERLACE  0x0020
#define DEC_CONTROL_INTERNAL_MASK                      0x0fff
#define DEC_CONTROL_FLAG_FORCE_SEQ_INTERLACE           0x1000

#define INTERLACE_SEQ_ALWAYS

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6  
#define NV21
#endif
#define CCBUF_SIZE		5*1024

enum {
    FRAME_REPEAT_TOP,
    FRAME_REPEAT_BOT,
    FRAME_REPEAT_NONE
};

static vframe_t *vmpeg_vf_peek(void*);
static vframe_t *vmpeg_vf_get(void*);
static void vmpeg_vf_put(vframe_t *, void*);
static int  vmpeg_vf_states(vframe_states_t *states, void*);
static int vmpeg_event_cb(int type, void *data, void *private_data);

static void vmpeg12_prot_init(void);
static void vmpeg12_local_init(void);

static const char vmpeg12_dec_id[] = "vmpeg12-dev";
#define PROVIDER_NAME   "decoder.mpeg12"
static const struct vframe_operations_s vmpeg_vf_provider =
{
    .peek = vmpeg_vf_peek,
    .get  = vmpeg_vf_get,
    .put  = vmpeg_vf_put,
    .event_cb = vmpeg_event_cb,
    .vf_states = vmpeg_vf_states,
};
static struct vframe_provider_s vmpeg_vf_prov;

static DECLARE_KFIFO(newframe_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(display_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(recycle_q, vframe_t *, VF_POOL_SIZE);

static const u32 frame_rate_tab[16] = {
    96000 / 30, 96000 / 24, 96000 / 24, 96000 / 25,
    96000 / 30, 96000 / 30, 96000 / 50, 96000 / 60,
    96000 / 60,
    /* > 8 reserved, use 24 */
    96000 / 24, 96000 / 24, 96000 / 24, 96000 / 24,
    96000 / 24, 96000 / 24, 96000 / 24
};

static struct vframe_s vfpool[VF_POOL_SIZE];
static s32 vfbuf_use[DECODE_BUFFER_NUM_MAX];
static u32 dec_control = 0;
static u32 frame_width, frame_height, frame_dur, frame_prog;
static struct timer_list recycle_timer;
static u32 stat;
static u32 buf_start, buf_size, ccbuf_phyAddress;
static DEFINE_SPINLOCK(lock);

static u32 frame_rpt_state;

/* for error handling */
static s32 frame_force_skip_flag = 0;
static s32 error_frame_skip_level = 0;
static s32 wait_buffer_counter = 0;
static u32 first_i_frame_ready = 0;

static inline u32 index2canvas(u32 index)
{
    const u32 canvas_tab[8] = {
#ifdef NV21
        0x010100, 0x030302, 0x050504, 0x070706,
        0x090908, 0x0b0b0a, 0x0d0d0c, 0x0f0f0e
#else
        0x020100, 0x050403, 0x080706, 0x0b0a09,
        0x0e0d0c, 0x11100f, 0x141312, 0x171615
#endif
    };

    return canvas_tab[index];
}

static void set_frame_info(vframe_t *vf)
{
    unsigned ar_bits;

#ifdef CONFIG_AM_VDEC_MPEG12_LOG
    bool first = (frame_width == 0) && (frame_height == 0);
#endif

    vf->width  = frame_width = READ_VREG(MREG_PIC_WIDTH);
    vf->height = frame_height = READ_VREG(MREG_PIC_HEIGHT);

    if (frame_dur > 0) {
        vf->duration = frame_dur;
    } else {
        vf->duration = frame_dur =
                           frame_rate_tab[(READ_VREG(MREG_SEQ_INFO) >> 4) & 0xf];
    }

    ar_bits = READ_VREG(MREG_SEQ_INFO) & 0xf;

    if (ar_bits == 0x2) {
        vf->ratio_control = 0xc0 << DISP_RATIO_ASPECT_RATIO_BIT;

    } else if (ar_bits == 0x3) {
        vf->ratio_control = 0x90 << DISP_RATIO_ASPECT_RATIO_BIT;

    } else if (ar_bits == 0x4) {
        vf->ratio_control = 0x74 << DISP_RATIO_ASPECT_RATIO_BIT;

    } else {
        vf->ratio_control = 0;
    }

    amlog_level_if(first, LOG_LEVEL_INFO, "mpeg2dec: w(%d), h(%d), dur(%d), dur-ES(%d)\n",
                   frame_width,
                   frame_height,
                   frame_dur,
                   frame_rate_tab[(READ_VREG(MREG_SEQ_INFO) >> 4) & 0xf]);
}

static bool error_skip(u32 info, vframe_t *vf)
{
    if (error_frame_skip_level) {
        /* skip error frame */
        if ((info & PICINFO_ERROR) || (frame_force_skip_flag)) {
            if ((info & PICINFO_ERROR) == 0) {
                if ((info & PICINFO_TYPE_MASK) == PICINFO_TYPE_I) {
                    frame_force_skip_flag = 0;
                }
            } else {
                if (error_frame_skip_level >= 2) {
                    frame_force_skip_flag = 1;
                }
            }
            if ((info & PICINFO_ERROR) || (frame_force_skip_flag)) {
                return true;
            }
        }
    }

    return false;
}

static irqreturn_t vmpeg12_isr(int irq, void *dev_id)
{
    u32 reg, info, seqinfo, offset, pts, pts_valid = 0;
    vframe_t *vf;
    u64 pts_us64 = 0;;

    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

    reg = READ_VREG(MREG_BUFFEROUT);

    if ((reg >> 16) == 0xfe) {	
        wakeup_userdata_poll(reg & 0xffff, ccbuf_phyAddress, CCBUF_SIZE);
        WRITE_VREG(MREG_BUFFEROUT, 0);
    }
    else if (reg) {
        info = READ_VREG(MREG_PIC_INFO);
        offset = READ_VREG(MREG_FRAME_OFFSET);

        if ((first_i_frame_ready == 0) &&
            ((info & PICINFO_TYPE_MASK) == PICINFO_TYPE_I) &&
            ((info & PICINFO_ERROR) == 0)) {
            first_i_frame_ready = 1;
        }

        if ((((info & PICINFO_TYPE_MASK) == PICINFO_TYPE_I) || ((info & PICINFO_TYPE_MASK) == PICINFO_TYPE_P))
             && (pts_lookup_offset_us64(PTS_TYPE_VIDEO, offset, &pts, 0, &pts_us64) == 0)) {
            pts_valid = 1;
        }

        /*if (frame_prog == 0)*/ {
            frame_prog = info & PICINFO_PROG;
        }

        if ((dec_control & DEC_CONTROL_FLAG_FORCE_2500_720_576_INTERLACE) &&
            (frame_width == 720) &&
            (frame_height == 576) &&
            (frame_dur == 3840)) {
            frame_prog = 0;
        }
        else if ((dec_control & DEC_CONTROL_FLAG_FORCE_3000_704_480_INTERLACE) &&
            (frame_width == 704) &&
            (frame_height == 480) &&
            (frame_dur == 3200)) {
            frame_prog = 0;
        }
        else if ((dec_control & DEC_CONTROL_FLAG_FORCE_2500_704_576_INTERLACE) &&
            (frame_width == 704) &&
            (frame_height == 576) &&
            (frame_dur == 3840)) {
            frame_prog = 0;
        }
        else if ((dec_control & DEC_CONTROL_FLAG_FORCE_2500_544_576_INTERLACE) &&
            (frame_width == 544) &&
            (frame_height == 576) &&
            (frame_dur == 3840)) {
            frame_prog = 0;
        }
	else if ((dec_control & DEC_CONTROL_FLAG_FORCE_2500_480_576_INTERLACE) &&
            (frame_width == 480) &&
            (frame_height == 576) &&
            (frame_dur == 3840)) {
            frame_prog = 0;
        }
        else if (dec_control & DEC_CONTROL_FLAG_FORCE_SEQ_INTERLACE) {
            frame_prog = 0;
        }

        if (frame_prog & PICINFO_PROG) {
            u32 index = ((reg & 0xf) - 1) & 7;

            seqinfo = READ_VREG(MREG_SEQ_INFO);

            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            set_frame_info(vf);

            vf->index = index;
#ifdef NV21
            vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
#else
            vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
#endif
            if ((seqinfo & SEQINFO_EXT_AVAILABLE) && (seqinfo & SEQINFO_PROG)) {
                if (info & PICINFO_RPT_FIRST) {
                    if (info & PICINFO_TOP_FIRST) {
                        vf->duration = vf->duration * 3;    // repeat three times
                    } else {
                        vf->duration = vf->duration * 2;    // repeat two times
                    }
                }
                vf->duration_pulldown = 0; // no pull down

            } else {
                vf->duration_pulldown = (info & PICINFO_RPT_FIRST) ?
                                        vf->duration >> 1 : 0;
            }

            vf->duration += vf->duration_pulldown;
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(index);
            vf->orientation = 0 ;
            vf->pts = (pts_valid) ? pts : 0;
            vf->pts_us64 = (pts_valid) ? pts_us64 : 0;

            vfbuf_use[index] = 1;

            if ((error_skip(info, vf)) ||
                ((first_i_frame_ready == 0) && ((PICINFO_TYPE_MASK & info) != PICINFO_TYPE_I))) {
                kfifo_put(&recycle_q, (const vframe_t **)&vf);
            } else {
                kfifo_put(&display_q, (const vframe_t **)&vf);
                vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
            }

        } else {
            u32 index = ((reg & 0xf) - 1) & 7;
            int first_field_type = (info & PICINFO_TOP_FIRST) ?
                    VIDTYPE_INTERLACE_TOP : VIDTYPE_INTERLACE_BOTTOM;

#ifdef INTERLACE_SEQ_ALWAYS
            // once an interlaced sequence exist, always force interlaced type
            // to make DI easy.
            dec_control |= DEC_CONTROL_FLAG_FORCE_SEQ_INTERLACE;
#endif

            if (info & PICINFO_FRAME) {
                frame_rpt_state = (info & PICINFO_TOP_FIRST) ? FRAME_REPEAT_TOP : FRAME_REPEAT_BOT;
            } else {
                if (frame_rpt_state == FRAME_REPEAT_TOP) {
                    first_field_type = VIDTYPE_INTERLACE_TOP;
                } else if (frame_rpt_state == FRAME_REPEAT_BOT) {
                    first_field_type = VIDTYPE_INTERLACE_BOTTOM;
                }
                frame_rpt_state = FRAME_REPEAT_NONE;
            }

            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            vfbuf_use[index] = 2;

            set_frame_info(vf);

            vf->index = index;
            vf->type = (first_field_type == VIDTYPE_INTERLACE_TOP) ?
                       VIDTYPE_INTERLACE_TOP : VIDTYPE_INTERLACE_BOTTOM;
#ifdef NV21
            vf->type |= VIDTYPE_VIU_NV21;
#endif
            vf->duration >>= 1;
            vf->duration_pulldown = (info & PICINFO_RPT_FIRST) ?
                                    vf->duration >> 1 : 0;
            vf->duration += vf->duration_pulldown;
            vf->orientation = 0 ;
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(index);
            vf->pts = (pts_valid) ? pts : 0;
            vf->pts_us64 = (pts_valid) ? pts_us64 : 0;

            if ((error_skip(info, vf)) ||
                ((first_i_frame_ready == 0) && ((PICINFO_TYPE_MASK & info) != PICINFO_TYPE_I))) {
                kfifo_put(&recycle_q, (const vframe_t **)&vf);
            } else {
                kfifo_put(&display_q, (const vframe_t **)&vf);
                vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
            }

            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            set_frame_info(vf);

            vf->index = index;
            vf->type = (first_field_type == VIDTYPE_INTERLACE_TOP) ?
                       VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
#ifdef NV21
            vf->type |= VIDTYPE_VIU_NV21;
#endif
            vf->duration >>= 1;
            vf->duration_pulldown = (info & PICINFO_RPT_FIRST) ?
                                    vf->duration >> 1 : 0;
            vf->duration += vf->duration_pulldown;
            vf->orientation = 0 ;
            vf->canvas0Addr = vf->canvas1Addr = index2canvas(index);
            vf->pts = 0;
            vf->pts_us64 = 0;

            if ((error_skip(info, vf)) ||
                ((first_i_frame_ready == 0) && ((PICINFO_TYPE_MASK & info) != PICINFO_TYPE_I))) {
                kfifo_put(&recycle_q, (const vframe_t **)&vf);
            } else {
                kfifo_put(&display_q, (const vframe_t **)&vf);
                vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
            }
        }

        WRITE_VREG(MREG_BUFFEROUT, 0);
    }

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
        vmpeg12_local_init();
        vmpeg12_prot_init();
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

#ifdef CONFIG_POST_PROCESS_MANAGER
static void vmpeg12_ppmgr_reset(void)
{
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_RESET,NULL);

    vmpeg12_local_init();

    printk("vmpeg12dec: vf_ppmgr_reset\n");
}
#endif

static void vmpeg_put_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;
    int fatal_reset = 0;

    receviver_start_e state = RECEIVER_INACTIVE ;
    if (vf_get_receiver(PROVIDER_NAME)){
        state = vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_QUREY_STATE,NULL);
        if((state == RECEIVER_STATE_NULL)||(state == RECEIVER_STATE_NONE)){
            /* receiver has no event_cb or receiver's event_cb does not process this event */
            state  = RECEIVER_INACTIVE ;
        }
    }else{
         state  = RECEIVER_INACTIVE ;
    }

    if (READ_VREG(MREG_FATAL_ERROR) == 1) {
        fatal_reset = 1;
    }

    if ((READ_VREG(MREG_WAIT_BUFFER) != 0) &&
         (kfifo_is_empty(&recycle_q)) &&
         (kfifo_is_empty(&display_q)) &&
         (state == RECEIVER_INACTIVE)) {
        if (++wait_buffer_counter > 4) {
            fatal_reset = 1;
        }

    } else {
        wait_buffer_counter = 0;
    }

    if (fatal_reset && (kfifo_is_empty(&display_q))) {
        printk("$$$$$$decoder is waiting for buffer or fatal reset.\n");

        amvdec_stop();

#ifdef CONFIG_POST_PROCESS_MANAGER
        vmpeg12_ppmgr_reset();
#else
        vf_light_unreg_provider(&vmpeg_vf_prov);
        vmpeg12_local_init();
        vf_reg_provider(&vmpeg_vf_prov);
#endif
        vmpeg12_prot_init();
        amvdec_start();
    }

    while (!kfifo_is_empty(&recycle_q) &&
           (READ_VREG(MREG_BUFFERIN) == 0)) {
        vframe_t *vf;
        if (kfifo_get(&recycle_q, &vf)) {
            if ((vf->index >= 0) && (--vfbuf_use[vf->index] == 0)) {
                WRITE_VREG(MREG_BUFFERIN, vf->index + 1);
                vf->index = -1;
            }

            kfifo_put(&newframe_q, (const vframe_t **)&vf);
        }
    }

    timer->expires = jiffies + PUT_INTERVAL;

    add_timer(timer);
}

int vmpeg12_dec_status(struct vdec_status *vstatus)
{
    vstatus->width = frame_width;
    vstatus->height = frame_height;
    if (frame_dur != 0) {
        vstatus->fps = 96000 / frame_dur;
    } else {
        vstatus->fps = 96000;
    }
    vstatus->error_count = READ_VREG(AV_SCRATCH_C);
    vstatus->status = stat;

    return 0;
}

/****************************************/
static void vmpeg12_canvas_init(void)
{
    int i;
    u32 canvas_width, canvas_height;
    u32 decbuf_size, decbuf_y_size, decbuf_uv_size;
    u32 disp_addr = 0xffffffff;

    if (buf_size <= 0x00400000) {
        /* SD only */
        canvas_width   = 768;
        canvas_height  = 576;
        decbuf_y_size  = 0x80000;
        decbuf_uv_size = 0x20000;
        decbuf_size    = 0x100000;
    } else {
        /* HD & SD */
        canvas_width   = 1920;
        canvas_height  = 1088;
        decbuf_y_size  = 0x200000;
        decbuf_uv_size = 0x80000;
        decbuf_size    = 0x300000;
    }

    if(is_vpp_postblend()){
        canvas_t cur_canvas;

        canvas_read((READ_VCBUS_REG(VD1_IF0_CANVAS0) & 0xff), &cur_canvas);
        disp_addr = (cur_canvas.addr + 7) >> 3;
    }

    for (i = 0; i < 8; i++) {
        if (((buf_start + i * decbuf_size + 7) >> 3) == disp_addr) {
#ifdef NV21
            canvas_config(2 * i + 0,
                          buf_start + 8 * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(2 * i + 1,
                          buf_start + 8 * decbuf_size + decbuf_y_size,
                          canvas_width, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#else
            canvas_config(3 * i + 0,
                          buf_start + 8 * decbuf_size,
                          canvas_width, canvas_height,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 1,
                          buf_start + 8 * decbuf_size + decbuf_y_size,
                          canvas_width / 2, canvas_height / 2,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(3 * i + 2,
                          buf_start + 8 * decbuf_size + decbuf_y_size + decbuf_uv_size,
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

    ccbuf_phyAddress = buf_start + 9 * decbuf_size;
    WRITE_VREG(MREG_CO_MV_START, buf_start + 9 * decbuf_size + CCBUF_SIZE);

}

static void vmpeg12_prot_init(void)
{
#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6)
    int save_reg = READ_VREG(POWER_CTL_VLD);

    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET0, 0);

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD)
    WRITE_VREG(MDEC_SW_RESET, (1<<7));
    WRITE_VREG(MDEC_SW_RESET, 0);
#endif

    WRITE_VREG(POWER_CTL_VLD, save_reg);

#else
    WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC);
#endif

    vmpeg12_canvas_init();

#ifdef NV21
    WRITE_VREG(AV_SCRATCH_0, 0x010100);
    WRITE_VREG(AV_SCRATCH_1, 0x030302);
    WRITE_VREG(AV_SCRATCH_2, 0x050504);
    WRITE_VREG(AV_SCRATCH_3, 0x070706);
    WRITE_VREG(AV_SCRATCH_4, 0x090908);
    WRITE_VREG(AV_SCRATCH_5, 0x0b0b0a);
    WRITE_VREG(AV_SCRATCH_6, 0x0d0d0c);
    WRITE_VREG(AV_SCRATCH_7, 0x0f0f0e);
#else
    WRITE_VREG(AV_SCRATCH_0, 0x020100);
    WRITE_VREG(AV_SCRATCH_1, 0x050403);
    WRITE_VREG(AV_SCRATCH_2, 0x080706);
    WRITE_VREG(AV_SCRATCH_3, 0x0b0a09);
    WRITE_VREG(AV_SCRATCH_4, 0x0e0d0c);
    WRITE_VREG(AV_SCRATCH_5, 0x11100f);
    WRITE_VREG(AV_SCRATCH_6, 0x141312);
    WRITE_VREG(AV_SCRATCH_7, 0x171615);
#endif

    /* set to mpeg1 default */
    WRITE_VREG(MPEG1_2_REG, 0);
    /* disable PSCALE for hardware sharing */
    WRITE_VREG(PSCALE_CTRL, 0);
    /* for Mpeg1 default value */
    WRITE_VREG(PIC_HEAD_INFO, 0x380);
    /* disable mpeg4 */
    WRITE_VREG(M4_CONTROL_REG, 0);
    /* clear mailbox interrupt */
    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);
    /* clear buffer IN/OUT registers */
    WRITE_VREG(MREG_BUFFERIN, 0);
    WRITE_VREG(MREG_BUFFEROUT, 0);
    /* set reference width and height */
    if ((frame_width != 0) && (frame_height != 0)) {
        WRITE_VREG(MREG_CMD, (frame_width << 16) | frame_height);
    } else {
        WRITE_VREG(MREG_CMD, 0);
    }
    /* clear error count */
    WRITE_VREG(MREG_ERROR_COUNT, 0);
    WRITE_VREG(MREG_FATAL_ERROR, 0);
    /* clear wait buffer status */
    WRITE_VREG(MREG_WAIT_BUFFER, 0);
#ifdef NV21
    SET_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
#endif
}

static void vmpeg12_local_init(void)
{
    int i;

    INIT_KFIFO(display_q);
    INIT_KFIFO(recycle_q);
    INIT_KFIFO(newframe_q);

    for (i=0; i<VF_POOL_SIZE; i++) {
        const vframe_t *vf = &vfpool[i];
        vfpool[i].index = -1;
        kfifo_put(&newframe_q, &vf);
    }

    for (i = 0; i < DECODE_BUFFER_NUM_MAX; i++) {
        vfbuf_use[i] = 0;
    }

    frame_width = frame_height = frame_dur = frame_prog = 0;
    frame_force_skip_flag = 0;
    wait_buffer_counter = 0;
    first_i_frame_ready = 0;

    dec_control &= DEC_CONTROL_INTERNAL_MASK;
}

static s32 vmpeg12_init(void)
{
    int r;

    init_timer(&recycle_timer);

    stat |= STAT_TIMER_INIT;

    vmpeg12_local_init();

    amvdec_enable();

    if (amvdec_loadmc(vmpeg12_mc) < 0) {
        amvdec_disable();
        return -EBUSY;
    }

    stat |= STAT_MC_LOAD;

    /* enable AMRISC side protocol */
    vmpeg12_prot_init();

    r = request_irq(INT_VDEC, vmpeg12_isr,
                    IRQF_SHARED, "vmpeg12-irq", (void *)vmpeg12_dec_id);

    if (r) {
        amvdec_disable();
        amlog_level(LOG_LEVEL_ERROR, "vmpeg12 irq register error.\n");
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

    recycle_timer.data = (ulong)&recycle_timer;
    recycle_timer.function = vmpeg_put_timer_func;
    recycle_timer.expires = jiffies + PUT_INTERVAL;

    add_timer(&recycle_timer);

    stat |= STAT_TIMER_ARM;

    amvdec_start();

    stat |= STAT_VDEC_RUN;

    set_vdec_func(&vmpeg12_dec_status);

    return 0;
}

static int amvdec_mpeg12_probe(struct platform_device *pdev)
{
    struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;

    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg12 probe start.\n");

    if (pdata == NULL) {
        amlog_level(LOG_LEVEL_ERROR, "amvdec_mpeg12 platform data undefined.\n");
        return -EFAULT;
    }

    buf_start = pdata->mem_start;
    buf_size  = pdata->mem_end - pdata->mem_start + 1;

    if (vmpeg12_init() < 0) {
        amlog_level(LOG_LEVEL_ERROR, "amvdec_mpeg12 init failed.\n");

        return -ENODEV;
    }

    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg12 probe end.\n");

    return 0;
}

static int amvdec_mpeg12_remove(struct platform_device *pdev)
{
    if (stat & STAT_VDEC_RUN) {
        amvdec_stop();
        stat &= ~STAT_VDEC_RUN;
    }

    if (stat & STAT_ISR_REG) {
        free_irq(INT_VDEC, (void *)vmpeg12_dec_id);
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

    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg12 remove.\n");

    return 0;
}

/****************************************/

static struct platform_driver amvdec_mpeg12_driver = {
    .probe      = amvdec_mpeg12_probe,
    .remove     = amvdec_mpeg12_remove,
#ifdef CONFIG_PM
    .suspend    = amvdec_suspend,
    .resume     = amvdec_resume,
#endif
    .driver     = {
        .name   = DRIVER_NAME,
    }
};

static struct codec_profile_t amvdec_mpeg12_profile = {
	.name = "mpeg12",
	.profile = ""
};

static int __init amvdec_mpeg12_driver_init_module(void)
{
    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg12 module init\n");

    if (platform_driver_register(&amvdec_mpeg12_driver)) {
        amlog_level(LOG_LEVEL_ERROR, "failed to register amvdec_mpeg12 driver\n");
        return -ENODEV;
    }
	vcodec_profile_register(&amvdec_mpeg12_profile);
    return 0;
}

static void __exit amvdec_mpeg12_driver_remove_module(void)
{
    amlog_level(LOG_LEVEL_INFO, "amvdec_mpeg12 module remove.\n");

    platform_driver_unregister(&amvdec_mpeg12_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_mpeg12 stat \n");
module_param(dec_control, uint, 0664);
MODULE_PARM_DESC(dec_control, "\n amvmpeg12 decoder control \n");
module_param(error_frame_skip_level, uint, 0664);
MODULE_PARM_DESC(error_frame_skip_level, "\n amvdec_mpeg12 error_frame_skip_level \n");

module_init(amvdec_mpeg12_driver_init_module);
module_exit(amvdec_mpeg12_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC MPEG1/2 Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
