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
 * Author:  Chen Zhang <chen.zhang@amlogic.com>
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
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/vformat.h>
#include <linux/workqueue.h>
#include <linux/dma-mapping.h>
#include <asm/atomic.h>
#include <plat/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "amports_priv.h"


#include "vdec.h"
#include "vdec_reg.h"
#include "amvdec.h"
#include "vh264_mc.h"

#ifdef CONFIG_GE2D_KEEP_FRAME
#include <linux/amlogic/logo/logo.h>
#endif

#define DRIVER_NAME "amvdec_h264"
#define MODULE_NAME "amvdec_h264"

#define HANDLE_H264_IRQ
//#define DEBUG_PTS
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6TV
#define DROP_B_FRAME_FOR_1080P_50_60FPS
#endif

#define RATE_MEASURE_NUM 8
#define RATE_CORRECTION_THRESHOLD 5
#define RATE_24_FPS  4004   /* 23.97 */
#define RATE_25_FPS  3840   /* 25 */
#define DUR2PTS(x) ((x)*90/96)
#define PTS2DUR(x) ((x)*96/90)
#define DUR2PTS_REM(x) (x*90 - DUR2PTS(x)*96)

static inline bool close_to(int a, int b, int m)
{
    return (abs(a-b) < m);
}


#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define NV21
#endif
static DEFINE_MUTEX(vh264_mutex);
/* 12M for L41 */
#define MAX_DPB_BUFF_SIZE       (12*1024*1024)
#define DEFAULT_MEM_SIZE        (32*1024*1024)
#define AVIL_DPB_BUFF_SIZE      0x01ec2000


#define DEF_BUF_START_ADDR            0x1000000
#define V_BUF_ADDR_OFFSET             (0x13e000)

#define PIC_SINGLE_FRAME        0
#define PIC_TOP_BOT_TOP         1
#define PIC_BOT_TOP_BOT         2
#define PIC_DOUBLE_FRAME        3
#define PIC_TRIPLE_FRAME        4
#define PIC_TOP_BOT              5
#define PIC_BOT_TOP              6
#define PIC_INVALID              7

#define EXTEND_SAR                      0xff

#define VF_POOL_SIZE        64
#define VF_BUF_NUM          24
#define PUT_INTERVAL        (HZ/100)
#define NO_DISP_WD_COUNT    (3 * HZ / PUT_INTERVAL)

#define SWITCHING_STATE_OFF       0
#define SWITCHING_STATE_ON_CMD3   1
#define SWITCHING_STATE_ON_CMD1   2

#define DEC_CONTROL_FLAG_FORCE_2997_1080P_INTERLACE 0x0001
#define DEC_CONTROL_FLAG_FORCE_2500_576P_INTERLACE  0x0002

#define INCPTR(p) ptr_atomic_wrap_inc(&p)

typedef struct {
    unsigned int y_addr;
    unsigned int u_addr;
    unsigned int v_addr;

    int y_canvas_index;
    int u_canvas_index;
    int v_canvas_index;
#ifdef CONFIG_GE2D_KEEP_FRAME
    unsigned int y_canvas_width;
    unsigned int u_canvas_width;
    unsigned int v_canvas_width;

    unsigned int y_canvas_height;
    unsigned int u_canvas_height;
    unsigned int v_canvas_height;
#endif
} buffer_spec_t;

#define spec2canvas(x)  \
    (((x)->v_canvas_index << 16) | \
     ((x)->u_canvas_index << 8)  | \
     ((x)->y_canvas_index << 0))

extern int query_video_status(int type, int *value);

static vframe_t *vh264_vf_peek(void*);
static vframe_t *vh264_vf_get(void*);
static void vh264_vf_put(vframe_t *, void*);
static int  vh264_vf_states(vframe_states_t *states, void*);
static int vh264_event_cb(int type, void *data, void *private_data);

static void vh264_prot_init(void);
static void vh264_local_init(void);
static void vh264_put_timer_func(unsigned long arg);
static void stream_switching_done(void);

static const char vh264_dec_id[] = "vh264-dev";

#define PROVIDER_NAME   "decoder.h264"

static const struct vframe_operations_s vh264_vf_provider_ops = {
    .peek = vh264_vf_peek,
    .get = vh264_vf_get,
    .put = vh264_vf_put,
    .event_cb = vh264_event_cb,
    .vf_states = vh264_vf_states,
};
static struct vframe_provider_s vh264_vf_prov;

static u32 frame_buffer_size;
static u32 frame_width, frame_height, frame_dur, frame_prog, frame_packing_type;
static u32 last_mb_width, last_mb_height;

static DECLARE_KFIFO(newframe_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(display_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(recycle_q, vframe_t *, VF_POOL_SIZE);

static struct vframe_s vfpool[VF_POOL_SIZE];
static s32 vfbuf_use[VF_BUF_NUM];
static buffer_spec_t buffer_spec[VF_BUF_NUM];
static vframe_t switching_fense_vf;

static struct timer_list recycle_timer;
static u32 stat;
static u32 buf_start, buf_size;
static s32 buf_offset;
static u32 ucode_map_start;
static u32 pts_outside = 0;
static u32 sync_outside = 0;
static u32 dec_control = 0;
static u32 vh264_ratio;
static u32 vh264_rotation;
static u32 use_idr_framerate = 0;

static u32 seq_info;
static u32 timing_info_present_flag;
static u32 fixed_frame_rate_flag;
static u32 aspect_ratio_info;
static u32 num_units_in_tick;
static u32 time_scale;
static u32 h264_ar;
#ifdef DROP_B_FRAME_FOR_1080P_50_60FPS
static u32 last_interlaced;
#endif
static unsigned char h264_first_pts_ready;
static bool h264_first_valid_pts_ready;
static u32 h264pts1, h264pts2;
static u32 h264_pts_count, duration_from_pts_done,duration_on_correcting;
static u32 vh264_error_count;
static u32 vh264_no_disp_count;
static u32 fatal_error_flag;
static u32 fatal_error_reset = 0;
static u32 max_refer_buf = 1;
#if 0
static u32 vh264_no_disp_wd_count;
#endif
static u32 vh264_running;
static s32 vh264_stream_switching_state;
static s32 vh264_eos;
static struct vframe_s *p_last_vf;
static u32 last_pts, last_pts_remainder;
static bool check_pts_discontinue;
static u32 wait_buffer_counter;
static uint error_recovery_mode = 0;
static uint error_recovery_mode_in = 3;
static uint error_recovery_mode_use = 3;

static uint mb_total = 0, mb_width = 0,  mb_height=0;
#define UCODE_IP_ONLY 2
#define UCODE_IP_ONLY_PARAM 1
static uint ucode_type = 0;

#ifdef DEBUG_PTS
static unsigned long pts_missed, pts_hit;
#endif
static uint debugfirmware=0;

static atomic_t vh264_active = ATOMIC_INIT(0);
static struct work_struct error_wd_work;
static struct work_struct stream_switching_work;

static struct dec_sysinfo vh264_amstream_dec_info;
extern u32 trickmode_i;
static dma_addr_t mc_dma_handle;
static void *mc_cpu_addr;

#define MC_OFFSET_HEADER    0x0000
#define MC_OFFSET_DATA      0x1000
#define MC_OFFSET_MMCO      0x2000
#define MC_OFFSET_LIST      0x3000
#define MC_OFFSET_SLICE     0x4000

#define MC_TOTAL_SIZE       (20*SZ_1K)
#define MC_SWAP_SIZE        ( 4*SZ_1K)

#define MODE_ERROR 0
#define MODE_FULL  1

static DEFINE_SPINLOCK(lock);

static int vh264_stop(int mode);
static s32 vh264_init(void);
extern u32 set_blackout_policy(int policy);
extern u32 get_blackout_policy(void);

#define DFS_HIGH_THEASHOLD 3

#ifdef CONFIG_GE2D_KEEP_FRAME
static ge2d_context_t *ge2d_videoh264_context = NULL;

static int ge2d_videoh264task_init(void)
{
    if (ge2d_videoh264_context == NULL)
            ge2d_videoh264_context = create_ge2d_work_queue();

    if (ge2d_videoh264_context == NULL){
            printk("create_ge2d_work_queue video task failed \n");
            return -1;
    }
    return 0;
}
static int ge2d_videoh264task_release(void)
{
    if (ge2d_videoh264_context) {
            destroy_ge2d_work_queue(ge2d_videoh264_context);
            ge2d_videoh264_context = NULL;
    }
    return 0;
}
static int ge2d_canvas_dup(canvas_t *srcy ,canvas_t *srcu,canvas_t *des,
    int format,u32 srcindex,u32 desindex)
{

    config_para_ex_t ge2d_config;
    printk("ge2d_canvas_dupvh264 ADDR srcy[0x%lx] srcu[0x%lx] des[0x%lx]\n",srcy->addr,srcu->addr,des->addr);
    memset(&ge2d_config,0,sizeof(config_para_ex_t));

    ge2d_config.alu_const_color= 0;
    ge2d_config.bitmask_en  = 0;
    ge2d_config.src1_gb_alpha = 0;

    ge2d_config.src_planes[0].addr = srcy->addr;
    ge2d_config.src_planes[0].w = srcy->width;
    ge2d_config.src_planes[0].h = srcy->height;

    ge2d_config.src_planes[1].addr = srcu->addr;
    ge2d_config.src_planes[1].w = srcu->width;
    ge2d_config.src_planes[1].h = srcu->height;

    ge2d_config.dst_planes[0].addr = des->addr;
    ge2d_config.dst_planes[0].w = des->width;
    ge2d_config.dst_planes[0].h = des->height;

    ge2d_config.src_para.canvas_index=srcindex;
    ge2d_config.src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config.src_para.format = format;
    ge2d_config.src_para.fill_color_en = 0;
    ge2d_config.src_para.fill_mode = 0;
    ge2d_config.src_para.color = 0;
    ge2d_config.src_para.top = 0;
    ge2d_config.src_para.left = 0;
    ge2d_config.src_para.width = srcy->width;
    ge2d_config.src_para.height = srcy->height;

    ge2d_config.dst_para.canvas_index=desindex;
    ge2d_config.dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config.dst_para.format = format;
    ge2d_config.dst_para.fill_color_en = 0;
    ge2d_config.dst_para.fill_mode = 0;
    ge2d_config.dst_para.color = 0;
    ge2d_config.dst_para.top = 0;
    ge2d_config.dst_para.left = 0;
    ge2d_config.dst_para.width = srcy->width;
    ge2d_config.dst_para.height = srcy->height;

    if(ge2d_context_config_ex(ge2d_videoh264_context,&ge2d_config)<0) {
        printk("ge2d_context_config_ex failed \n");
        return -1;
    }

    stretchblt_noalpha(ge2d_videoh264_context ,0, 0,srcy->width, srcy->height,
        0, 0,srcy->width,srcy->height);

    return 0;
}
#endif

static inline int fifo_level(void)
{
    return VF_POOL_SIZE - kfifo_len(&newframe_q);
}

static void vdec_dfs(void)
{
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8B
    vdec_power_mode((fifo_level() > DFS_HIGH_THEASHOLD) ? 0 : 1);
#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if (IS_MESON_M8M2_CPU) {
        vdec_power_mode((fifo_level() > DFS_HIGH_THEASHOLD) ? 0 : 1);
    }
#endif
}

void spec_set_canvas(buffer_spec_t *spec,
                     unsigned width,
                     unsigned height)
{
#ifdef NV21
    canvas_config(spec->y_canvas_index,
                  spec->y_addr,
                  width,
                  height,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

    canvas_config(spec->u_canvas_index,
                  spec->u_addr,
                  width,
                  height / 2,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);
#else
    canvas_config(spec->y_canvas_index,
                  spec->y_addr,
                  width,
                  height,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

    canvas_config(spec->u_canvas_index,
                  spec->u_addr,
                  width / 2,
                  height / 2,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

    canvas_config(spec->v_canvas_index,
                  spec->v_addr,
                  width / 2,
                  height / 2,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);
#endif
    return;
}

static vframe_t *vh264_vf_peek(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_peek(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static vframe_t *vh264_vf_get(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_get(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static void vh264_vf_put(vframe_t *vf, void* op_arg)
{
    kfifo_put(&recycle_q, (const vframe_t **)&vf);
}

static int vh264_event_cb(int type, void *data, void *private_data)
{
    if(type & VFRAME_EVENT_RECEIVER_RESET){
        unsigned long flags;
        amvdec_stop();
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_light_unreg_provider(&vh264_vf_prov);
#endif
        spin_lock_irqsave(&lock, flags);
        vh264_local_init();
        vh264_prot_init();
        spin_unlock_irqrestore(&lock, flags);
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_reg_provider(&vh264_vf_prov);
#endif
        amvdec_start();
    }
    return 0;
}

static int  vh264_vf_states(vframe_states_t *states, void* op_arg)
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

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
static tvin_trans_fmt_t convert_3d_format(u32 type)
{
    const tvin_trans_fmt_t conv_tab[] = {
        0,                      // checkerboard
        0,                      // column alternation
        TVIN_TFMT_3D_LA,        // row alternation
        TVIN_TFMT_3D_LRH_OLER,  // side by side
        TVIN_TFMT_3D_FA         // top bottom
    };

    return (type <= 4) ? conv_tab[type] : 0;
}
#endif

static void set_frame_info(vframe_t *vf)
{
    vf->width = frame_width;
    vf->height = frame_height;
    vf->duration = frame_dur;
    vf->ratio_control = (min(h264_ar, (u32)DISP_RATIO_ASPECT_RATIO_MAX)) << DISP_RATIO_ASPECT_RATIO_BIT;
    vf->orientation = vh264_rotation;
    vf->flag = 0;

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    vf->trans_fmt = convert_3d_format(frame_packing_type);

    if ((vf->trans_fmt == TVIN_TFMT_3D_LRF) ||
        (vf->trans_fmt == TVIN_TFMT_3D_LA)) {
        vf->left_eye.start_x = 0;
        vf->left_eye.start_y = 0;
        vf->left_eye.width = frame_width / 2;
        vf->left_eye.height = frame_height;

        vf->right_eye.start_x = 0;
        vf->right_eye.start_y = 0;
        vf->right_eye.width = frame_width / 2;
        vf->right_eye.height = frame_height;
    }
    else if ((vf->trans_fmt == TVIN_TFMT_3D_LRH_OLER) ||
        (vf->trans_fmt == TVIN_TFMT_3D_TB)) {
        vf->left_eye.start_x = 0;
        vf->left_eye.start_y = 0;
        vf->left_eye.width = frame_width / 2;
        vf->left_eye.height = frame_height;

        vf->right_eye.start_x = 0;
        vf->right_eye.start_y = 0;
        vf->right_eye.width = frame_width / 2;
        vf->right_eye.height = frame_height;
    }
#endif

    return;
}

#ifdef CONFIG_POST_PROCESS_MANAGER
static void vh264_ppmgr_reset(void)
{
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_RESET,NULL);

    vh264_local_init();

    //vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);

    printk("vh264dec: vf_ppmgr_reset\n");
}
#endif

static void vh264_set_params(void)
{
    int aspect_ratio_info_present_flag, aspect_ratio_idc;
    int max_dpb_size, actual_dpb_size, max_reference_size;
    int i, mb_mv_byte;
    unsigned addr;
    unsigned int post_canvas;
    unsigned int frame_mbs_only_flag;
    unsigned int chroma_format_idc, chroma444;
    unsigned int crop_infor, crop_bottom,crop_right;

    post_canvas = get_post_canvas();

    timing_info_present_flag = 0;
    mb_width = READ_VREG(AV_SCRATCH_1);
    seq_info = READ_VREG(AV_SCRATCH_2);
    aspect_ratio_info = READ_VREG(AV_SCRATCH_3);
    num_units_in_tick = READ_VREG(AV_SCRATCH_4);
    time_scale = READ_VREG(AV_SCRATCH_5);
    mb_total = (mb_width >> 8) & 0xffff;
    max_reference_size = (mb_width >> 24) & 0x7f;
    mb_mv_byte = (mb_width & 0x80000000) ? 24 : 96;
    mb_width = mb_width & 0xff;
    mb_height = mb_total / mb_width;

    /* AV_SCRATCH_2
       bit 15: frame_mbs_only_flag
       bit 13-14: chroma_format_idc */
    frame_mbs_only_flag = (seq_info >> 15) & 0x01;
    chroma_format_idc = (seq_info >> 13) & 0x03;
    chroma444 = (chroma_format_idc == 3) ? 1 : 0;

    /* @AV_SCRATCH_6.31-16 =  (left  << 8 | right ) << 1
       @AV_SCRATCH_6.15-0   =  (top << 8  | bottom ) <<  (2 - frame_mbs_only_flag) */
    crop_infor = READ_VREG(AV_SCRATCH_6);
    crop_bottom = (crop_infor & 0xff) >> (2 - frame_mbs_only_flag);
    crop_right      = ((crop_infor >>16) & 0xff) >> (2 - frame_mbs_only_flag);

    /* if width or height from outside is not equal to mb, then use mb */
    /* add: for seeking stream with other resolution */
    if ((last_mb_width && (last_mb_width != mb_width))
        || (mb_width != ((frame_width+15) >> 4))) {
        frame_width = 0;
    }
    if ((last_mb_height && (last_mb_height != mb_height))
        || (mb_height != ((frame_height+15) >> 4))) {
        frame_height = 0;
    }
    last_mb_width = mb_width;
    last_mb_height = mb_height;

    if ((frame_width == 0) ||( frame_height == 0) ||crop_infor) {
        frame_width = mb_width << 4;
        frame_height = mb_height << 4;
        if (frame_mbs_only_flag) {
            frame_height = frame_height - (2>>chroma444)*min(crop_bottom, (unsigned int)((8<<chroma444)-1));
            frame_width  = frame_width - (2>>chroma444)*min(crop_right, (unsigned int)((8<<chroma444)-1));
        } else {
            frame_height = frame_height - (4>>chroma444)*min(crop_bottom, (unsigned int)((8<<chroma444)-1));
            frame_width   = frame_width - (4>>chroma444)*min(crop_right, (unsigned int)((8<<chroma444)-1));			
        }
        printk("frame_mbs_only_flag %d, crop_bottom %d,  frame_height %d, mb_height %d,crop_right %d, frame_width %d, mb_width %d\n",
            frame_mbs_only_flag, crop_bottom,frame_height, mb_height,crop_right,frame_width, mb_height);

        if (frame_height == 1088) {
            frame_height = 1080;
        }
    }

    mb_width = (mb_width + 3) & 0xfffffffc;
    mb_height = (mb_height + 3) & 0xfffffffc;
    mb_total = mb_width * mb_height;

    max_dpb_size = (frame_buffer_size - mb_total * 384 * 4 - mb_total * mb_mv_byte) / (mb_total * 384 + mb_total * mb_mv_byte);
    if (max_reference_size <= max_dpb_size) {
        max_dpb_size = MAX_DPB_BUFF_SIZE / (mb_total * 384);
        if (max_dpb_size > 16) {
            max_dpb_size = 16;
        }

        if (max_refer_buf && (max_reference_size < max_dpb_size)) {
            max_reference_size = max_dpb_size + 1;
        } else {
            max_dpb_size = max_reference_size;
            max_reference_size++;
        }
    } else {
        max_dpb_size = max_reference_size;
        max_reference_size++;
    }

    if (mb_total * 384 * (max_dpb_size + 3) + mb_total * mb_mv_byte * max_reference_size > frame_buffer_size) {
        max_dpb_size = (frame_buffer_size - mb_total * 384 * 3 - mb_total * mb_mv_byte) / (mb_total * 384 + mb_total * mb_mv_byte);
        max_reference_size = max_dpb_size + 1;
    }

    actual_dpb_size = (frame_buffer_size - mb_total * mb_mv_byte * max_reference_size) / (mb_total * 384);
    if (actual_dpb_size > 24) {
        actual_dpb_size = 24;
    }

    if (max_dpb_size > 5) {
        if (actual_dpb_size < max_dpb_size + 3) {
            actual_dpb_size = max_dpb_size + 3;
        if (actual_dpb_size > 24) {
            actual_dpb_size = 24;
        }
            max_reference_size = (frame_buffer_size - mb_total * 384 * actual_dpb_size) / (mb_total * mb_mv_byte);
        }
    } else {
        if (actual_dpb_size < max_dpb_size + 4) {
            actual_dpb_size = max_dpb_size + 4;
        if (actual_dpb_size > 24) {
            actual_dpb_size = 24;
        }
            max_reference_size = (frame_buffer_size - mb_total * 384 * actual_dpb_size) / (mb_total * mb_mv_byte);
        }
    }

    if (!(READ_VREG(AV_SCRATCH_F) & 0x1)) {
        addr = buf_start;

        if (actual_dpb_size <= 21) {
            for (i = 0 ; i < actual_dpb_size ; i++) {
                buffer_spec[i].y_addr = addr;
                addr += mb_total << 8;
#ifdef NV21
                buffer_spec[i].u_addr = addr;
                buffer_spec[i].v_addr = addr;
                addr += mb_total << 7;
#else
                buffer_spec[i].u_addr = addr;
                addr += mb_total << 6;
                buffer_spec[i].v_addr = addr;
                addr += mb_total << 6;
#endif
                vfbuf_use[i] = 0;

#ifdef NV21
                buffer_spec[i].y_canvas_index = 128 + i * 2;
                buffer_spec[i].u_canvas_index = 128 + i * 2 + 1;
                buffer_spec[i].v_canvas_index = 128 + i * 2 + 1;
#ifdef CONFIG_GE2D_KEEP_FRAME
                buffer_spec[i].y_canvas_width = mb_width << 4;
                buffer_spec[i].y_canvas_height = mb_height << 4;
                buffer_spec[i].u_canvas_width = mb_width << 4;
                buffer_spec[i].u_canvas_height = mb_height << 4;
                buffer_spec[i].v_canvas_width = mb_width << 4;
                buffer_spec[i].v_canvas_height = mb_height << 4;
#endif
                canvas_config(128 + i * 2, buffer_spec[i].y_addr, mb_width << 4, mb_height << 4,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 2 + 1, buffer_spec[i].u_addr, mb_width << 4, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#else
                buffer_spec[i].y_canvas_index = 128 + i * 3;
                buffer_spec[i].u_canvas_index = 128 + i * 3 + 1;
                buffer_spec[i].v_canvas_index = 128 + i * 3 + 2;

                canvas_config(128 + i * 3, buffer_spec[i].y_addr, mb_width << 4, mb_height << 4,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 3 + 1, buffer_spec[i].u_addr, mb_width << 3, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 3 + 2, buffer_spec[i].v_addr, mb_width << 3, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#endif
            }
        } else {
            for (i = 0 ; i < 21 ; i++) {
#ifdef NV21
                buffer_spec[i].y_addr = addr;
                addr += mb_total << 8;
                buffer_spec[i].u_addr = addr;
                buffer_spec[i].v_addr = addr;
                addr += mb_total << 7;
                vfbuf_use[i] = 0;

                buffer_spec[i].y_canvas_index = 128 + i * 2;
                buffer_spec[i].u_canvas_index = 128 + i * 2 + 1;
                buffer_spec[i].v_canvas_index = 128 + i * 2 + 1;
#ifdef CONFIG_GE2D_KEEP_FRAME
                buffer_spec[i].y_canvas_width = mb_width << 4;
                buffer_spec[i].y_canvas_height = mb_height << 4;
                buffer_spec[i].u_canvas_width = mb_width << 4;
                buffer_spec[i].u_canvas_height = mb_height << 4;
                buffer_spec[i].v_canvas_width = mb_width << 4;
                buffer_spec[i].v_canvas_height = mb_height << 4;
#endif
                canvas_config(128 + i * 2, buffer_spec[i].y_addr, mb_width << 4, mb_height << 4,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 2 + 1, buffer_spec[i].u_addr, mb_width << 4, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#else
                buffer_spec[i].y_addr = addr;
                addr += mb_total << 8;
                buffer_spec[i].u_addr = addr;
                addr += mb_total << 6;
                buffer_spec[i].v_addr = addr;
                addr += mb_total << 6;
                vfbuf_use[i] = 0;

                buffer_spec[i].y_canvas_index = 128 + i * 3;
                buffer_spec[i].u_canvas_index = 128 + i * 3 + 1;
                buffer_spec[i].v_canvas_index = 128 + i * 3 + 2;

                canvas_config(128 + i * 3, buffer_spec[i].y_addr, mb_width << 4, mb_height << 4,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 3 + 1, buffer_spec[i].u_addr, mb_width << 3, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                canvas_config(128 + i * 3 + 2, buffer_spec[i].v_addr, mb_width << 3, mb_height << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#endif
            }

            for (i = 21 ; i < actual_dpb_size ; i++) {
#ifdef NV21
                buffer_spec[i].y_canvas_index = 2 * (i - 21) + 2;
                buffer_spec[i].y_addr = addr;
                addr += mb_total << 8;
                buffer_spec[i].u_canvas_index = 2 * (i - 21) + 3;
                buffer_spec[i].v_canvas_index = 2 * (i - 21) + 3;
                buffer_spec[i].u_addr = addr;
                addr += mb_total << 7;
                vfbuf_use[i] = 0;
#ifdef CONFIG_GE2D_KEEP_FRAME
                buffer_spec[i].y_canvas_width = mb_width << 4;
                buffer_spec[i].y_canvas_height = mb_height << 4;
                buffer_spec[i].u_canvas_width = mb_width << 4;
                buffer_spec[i].u_canvas_height = mb_height << 4;
                buffer_spec[i].v_canvas_width = mb_width << 4;
                buffer_spec[i].v_canvas_height = mb_height << 4;
#endif
                spec_set_canvas(&buffer_spec[i], mb_width << 4, mb_height << 4);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#else
                buffer_spec[i].y_canvas_index = 3 * (i - 21) + 3;
                buffer_spec[i].y_addr = addr;
                addr += mb_total << 8;
                buffer_spec[i].u_canvas_index = 3 * (i - 21) + 4;
                buffer_spec[i].u_addr = addr;
                addr += mb_total << 6;
                buffer_spec[i].v_canvas_index = 3 * (i - 21) + 5;
                buffer_spec[i].v_addr = addr;
                addr += mb_total << 6;
                vfbuf_use[i] = 0;

                spec_set_canvas(&buffer_spec[i], mb_width << 4, mb_height << 4);
                WRITE_VREG(ANC0_CANVAS_ADDR + i, spec2canvas(&buffer_spec[i]));
#endif
            }
        }
    } else {
        addr = buf_start + mb_total * 384 * actual_dpb_size;
    }

    timing_info_present_flag = seq_info & 0x2;
    fixed_frame_rate_flag = 0;
    aspect_ratio_info_present_flag = seq_info & 0x1;
    aspect_ratio_idc = (seq_info >> 16) & 0xff;

    if (timing_info_present_flag) {
        fixed_frame_rate_flag = seq_info & 0x40;

        if (((num_units_in_tick * 120) >= time_scale && ((!sync_outside) || (!frame_dur))) && num_units_in_tick && time_scale) {
            if (use_idr_framerate || !frame_dur || !duration_from_pts_done || vh264_running) {
                u32 frame_dur_es = div_u64(96000ULL * 2 * num_units_in_tick, time_scale);

                // hack to avoid use ES frame duration when it's half of the rate from system info
                // sometimes the encoder is given a wrong frame rate but the system side infomation is more reliable
                if ((frame_dur * 2) != frame_dur_es) {
                    frame_dur = frame_dur_es;
                }
            }
        }
    } else {
        printk("H.264: timing_info not present\n");
    }

    if (aspect_ratio_info_present_flag) {
        if (aspect_ratio_idc == EXTEND_SAR) {
            //printk("v264dec: aspect_ratio_idc = EXTEND_SAR, aspect_ratio_info = 0x%x\n", aspect_ratio_info);
            h264_ar = div_u64(256ULL * (aspect_ratio_info >> 16) * frame_height, (aspect_ratio_info & 0xffff) * frame_width);
        } else {
            //printk("v264dec: aspect_ratio_idc = %d\n", aspect_ratio_idc);

            switch (aspect_ratio_idc) {
            case 1:
                h264_ar = 0x100 * frame_height / frame_width;
                break;
            case 2:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 12);
                break;
            case 3:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 10);
                break;
            case 4:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 16);
                break;
            case 5:
                h264_ar = 0x100 * frame_height * 33 / (frame_width * 40);
                break;
            case 6:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 24);
                break;
            case 7:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 20);
                break;
            case 8:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 32);
                break;
            case 9:
                h264_ar = 0x100 * frame_height * 33 / (frame_width * 80);
                break;
            case 10:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 18);
                break;
            case 11:
                h264_ar = 0x100 * frame_height * 11 / (frame_width * 15);
                break;
            case 12:
                h264_ar = 0x100 * frame_height * 33 / (frame_width * 64);
                break;
            case 13:
                h264_ar = 0x100 * frame_height * 99 / (frame_width * 160);
                break;
            case 14:
                h264_ar = 0x100 * frame_height * 3 / (frame_width * 4);
                break;
            case 15:
                h264_ar = 0x100 * frame_height * 2 / (frame_width * 3);
                break;
            case 16:
                h264_ar = 0x100 * frame_height * 1 / (frame_width * 2);
                break;
            default:
                if (vh264_ratio>>16) {
                    h264_ar = (frame_height * (vh264_ratio&0xffff)  *0x100 + ((vh264_ratio>>16) *  frame_width/2))/((vh264_ratio>>16) *  frame_width);
                } else {
                    h264_ar = frame_height * 0x100 / frame_width;
                }
                break;
            }
        }
    } else {
        printk("v264dec: aspect_ratio not available from source\n");
        if (vh264_ratio>>16) {
            /* high 16 bit is width, low 16 bit is height */
            h264_ar = ((vh264_ratio&0xffff) * frame_height* 0x100 + (vh264_ratio>>16)*frame_width/2) /( (vh264_ratio>>16) * frame_width);
        } else {
            h264_ar = frame_height * 0x100 / frame_width;
        }
    }

    WRITE_VREG(AV_SCRATCH_1, addr);
    WRITE_VREG(AV_SCRATCH_3, post_canvas); // should be modified later
    addr += mb_total * mb_mv_byte * max_reference_size;
    WRITE_VREG(AV_SCRATCH_4, addr);
    WRITE_VREG(AV_SCRATCH_0, (max_reference_size << 24) | (actual_dpb_size << 16) | (max_dpb_size << 8));
}

static unsigned pts_inc_by_duration(unsigned *new_pts, unsigned *new_pts_rem)
{
    unsigned r, rem;

    r = last_pts + DUR2PTS(frame_dur);
    rem = last_pts_remainder + DUR2PTS_REM(frame_dur);

    if (rem >= 96) {
        r++;
        rem -= 96;
    }

    if (new_pts) *new_pts = r;
    if (new_pts_rem) *new_pts_rem = rem;

    return r;
}

#ifdef HANDLE_H264_IRQ
static irqreturn_t vh264_isr(int irq, void *dev_id)
#else
static void vh264_isr(void)
#endif
{
    unsigned int buffer_index;
    vframe_t *vf;
    unsigned int cpu_cmd;
    unsigned int pts, pts_valid = 0, pts_duration = 0;
	u64 pts_us64;
    bool force_interlaced_frame = false;

    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

    if (0 == (stat & STAT_VDEC_RUN)) {
        printk("decoder is not running\n");
#ifdef HANDLE_H264_IRQ
        return IRQ_HANDLED;
#else
        return;
#endif
    }

    cpu_cmd = READ_VREG(AV_SCRATCH_0);

#ifdef DROP_B_FRAME_FOR_1080P_50_60FPS
    if((frame_dur < 2004) &&
       (frame_width >= 1400) &&
       (frame_height >= 1000) &&
       (last_interlaced == 0)) {
        SET_VREG_MASK(AV_SCRATCH_F, 0x8);
    }
#endif

    if ((cpu_cmd & 0xff) == 1) {
        if (unlikely(vh264_running && (kfifo_len(&newframe_q) != VF_POOL_SIZE))) {
            // a cmd 1 sent during decoding w/o getting a cmd 3.
            // should not happen but the original code has such case, do the same process
            vh264_stream_switching_state = SWITCHING_STATE_ON_CMD1;

            printk("Enter switching mode cmd1.\n");
            schedule_work(&stream_switching_work);
            return IRQ_HANDLED;
        }

        vh264_set_params();

    } else if ((cpu_cmd & 0xff) == 2) {
        int frame_mb_only, pic_struct_present, pic_struct, prog_frame, poc_sel, idr_flag, eos, error;
        int i, status, num_frame, b_offset;
        int current_error_count;

        vh264_running = 1;
        vh264_no_disp_count = 0;
        num_frame = (cpu_cmd >> 8) & 0xff;
        frame_mb_only = seq_info & 0x8000;
        pic_struct_present = seq_info & 0x10;

        current_error_count = READ_VREG(AV_SCRATCH_D);
        if (vh264_error_count != current_error_count) {
            //printk("decoder error happened, count %d\n", current_error_count);
            vh264_error_count = current_error_count;
        }

        for (i = 0 ; (i < num_frame) && (!vh264_eos) ; i++) {
            status = READ_VREG(AV_SCRATCH_1 + i);
            buffer_index = status & 0x1f;
            error = status & 0x200;

            if ((error_recovery_mode_use & 2) && error) {
                check_pts_discontinue = true;
            }

            if ((p_last_vf != NULL) && (p_last_vf->index == buffer_index)) {
                continue;
            }

            if (buffer_index >= VF_BUF_NUM) {
                continue;
            }

            pic_struct = (status >> 5) & 0x7;
            prog_frame = status & 0x100;
            poc_sel = status & 0x200;
            idr_flag = status & 0x400;
            frame_packing_type = (status >> 12) & 0x7;
            eos = (status >> 15) & 1;

            if (eos) {
                vh264_eos = 1;
            }

            b_offset = (status >> 16) & 0xffff;
#ifdef DROP_B_FRAME_FOR_1080P_50_60FPS
            last_interlaced = prog_frame ? 0 : 1;
#endif
            if (kfifo_get(&newframe_q, &vf) == 0) {
                printk("fatal error, no available buffer slot.");
                return IRQ_HANDLED;
            }

            set_frame_info(vf);

            switch (i) {
            case 0:
                b_offset |= (READ_VREG(AV_SCRATCH_A) & 0xffff) << 16;
                break;
            case 1:
                b_offset |= READ_VREG(AV_SCRATCH_A) & 0xffff0000;
                break;
            case 2:
                b_offset |= (READ_VREG(AV_SCRATCH_B) & 0xffff) << 16;
                break;
            case 3:
                b_offset |= READ_VREG(AV_SCRATCH_B) & 0xffff0000;
                break;
            case 4:
                b_offset |= (READ_VREG(AV_SCRATCH_C) & 0xffff) << 16;
                break;
            case 5:
                b_offset |= READ_VREG(AV_SCRATCH_C) & 0xffff0000;
                break;
            default:
                break;
            }

            // add 64bit pts us ;
            if (pts_lookup_offset_us64(PTS_TYPE_VIDEO, b_offset, &pts, 0,&pts_us64) == 0){
                pts_valid = 1;
#ifdef DEBUG_PTS
                pts_hit++;
#endif
            } else {
                pts_valid = 0;
#ifdef DEBUG_PTS
                pts_missed++;
#endif
            }

            if (READ_VREG(AV_SCRATCH_F) & 2) {
                // for I only mode, ignore the PTS information and only uses frame duration for each I frame decoded
                if (p_last_vf) {
                    pts_valid = 0;
                }
                // also skip frame duration calculation based on PTS
                duration_from_pts_done = 1;
                // and add a default duration for 1/30 second if there is no valid frame duration available
                if (frame_dur == 0) {
                    frame_dur = 96000/30;
                }
            }

            if (sync_outside == 0) {
                if (h264_first_pts_ready == 0) {
                    if (pts_valid == 0) {
                        vfbuf_use[buffer_index]++;
                        vf->index = buffer_index;
                        kfifo_put(&recycle_q, (const vframe_t **)&vf);
                        continue;
                    }

                    h264pts1 = pts;
                    h264_pts_count = 0;
                    h264_first_pts_ready = 1;
                } else {

                    if (pts_valid && (pts > h264pts1) && h264_pts_count > 24) {
                        if (duration_from_pts_done == 0) {
                            unsigned int old_duration=frame_dur;
                            h264pts2 = pts;

                            pts_duration = (h264pts2 - h264pts1) * 16 / (h264_pts_count * 15);

                            if ((pts_duration != frame_dur) && (!pts_outside)) {
                                if (use_idr_framerate) {
                                    if ((close_to(pts_duration, RATE_24_FPS, RATE_CORRECTION_THRESHOLD) &&
                                         close_to(frame_dur, RATE_25_FPS, RATE_CORRECTION_THRESHOLD))
                                         || (close_to(pts_duration, RATE_25_FPS, RATE_CORRECTION_THRESHOLD) &&
                                             close_to(frame_dur, RATE_24_FPS, RATE_CORRECTION_THRESHOLD))) {
                                        printk("H.264: Correct frame duration from %d to duration based on PTS %d\n",
                                                frame_dur, pts_duration);
                                        frame_dur = pts_duration;
                                        duration_from_pts_done = 1;
                                        //printk("used calculate frame rate,on frame_dur problem=%d\n",frame_dur);
                                    } else if (((frame_dur<96000/240) && (pts_duration>96000/240)) || !duration_on_correcting){//>if frameRate>240fps,I think have error,use calculate rate.
                                        printk("H.264: Correct frame duration from %d to duration based on PTS %d\n",
                                                frame_dur, pts_duration);
                                        frame_dur = pts_duration;
                                        //printk("used calculate frame rate,on frame_dur error=%d\n",frame_dur);
                                        duration_on_correcting=1;
                                    }
                                } else {
                                    if (close_to(pts_duration, frame_dur, 2000)) {
                                        frame_dur = pts_duration;
                                        printk("used calculate frame rate,on duration =%d\n", frame_dur);
                                    } else {
                                        printk("don't use calculate frame rate pts_duration =%d\n", pts_duration);
                                    }
                                }
                            }

                            if (duration_from_pts_done == 0) {
			        if (close_to(pts_duration, old_duration, RATE_CORRECTION_THRESHOLD)) {
                                    //printk("finished correct frame duration new=%d,old_duration=%d,cnt=%d\n",pts_duration,old_duration,h264_pts_count);
                                    duration_from_pts_done = 1;
                                }else{/*not the same,redo it.*/
                                    //printk("restart correct frame duration new=%d,old_duration=%d,cnt=%d\n",pts_duration,old_duration,h264_pts_count);
                                    h264pts1 = h264pts2;
                                    h264_pts_count = 0;
                                    duration_from_pts_done = 0;
                                }
                            }
                        }
                    }
                }

                h264_pts_count++;
            } else {
                if (!(idr_flag && pts_valid)) {
                    pts = 0;
                }
            }

            // if use_idr_framerate or fixed frame rate, only use PTS for IDR frames
            if (timing_info_present_flag &&
                frame_dur && 
                (use_idr_framerate || (fixed_frame_rate_flag != 0)) &&
                pts_valid &&
                h264_first_valid_pts_ready) {
                pts_valid = (idr_flag) ? 1 : 0;
            }

            if (!h264_first_valid_pts_ready && pts_valid) {
                h264_first_valid_pts_ready = true;
                last_pts = pts - DUR2PTS(frame_dur);
                last_pts_remainder = 0;
            }

            // calculate PTS of next frame and smooth PTS for fixed rate source
            if (pts_valid) {
                if ((fixed_frame_rate_flag) &&
                    (abs(pts_inc_by_duration(NULL, NULL) - pts)  < DUR2PTS(frame_dur))) {
                    pts = pts_inc_by_duration(&pts, &last_pts_remainder);
                } else {
                    last_pts_remainder = 0;
                }

            } else {
                pts = pts_inc_by_duration(&pts, &last_pts_remainder);
                pts_valid = 1;
            }

            if ((dec_control & DEC_CONTROL_FLAG_FORCE_2997_1080P_INTERLACE) &&
                (frame_width == 1920) &&
                (frame_height >= 1080) &&
                (vf->duration == 3203)) {
                force_interlaced_frame = true;
            }
            else if ((dec_control & DEC_CONTROL_FLAG_FORCE_2500_576P_INTERLACE) &&
                (frame_width == 720) &&
                (frame_height == 576) &&
                (vf->duration == 3840)) {
                force_interlaced_frame = true;
            }

            /* for frames with PTS, check if there is PTS discontinue based on previous frames (including error frames),
             * force no VPTS discontinue reporting if we saw errors earlier but only once.
             */
            if ((pts_valid) && (check_pts_discontinue) && (!error)) {
                if ((pts - last_pts) < 90000) {
                    vf->flag = VFRAME_FLAG_NO_DISCONTINUE;
                    check_pts_discontinue = false;
                }
            }

            last_pts = pts;

            if (pic_struct_present) {
                if ((pic_struct == PIC_TOP_BOT) || (pic_struct == PIC_BOT_TOP)) {
                    prog_frame = 0;
                }
            }

            if ((!force_interlaced_frame) && (prog_frame || (pic_struct_present && pic_struct <= PIC_TRIPLE_FRAME))) {
                if (pic_struct_present) {
                    if (pic_struct == PIC_TOP_BOT_TOP || pic_struct == PIC_BOT_TOP_BOT) {
                        vf->duration += vf->duration >> 1;
                    } else if (pic_struct == PIC_DOUBLE_FRAME) {
                        vf->duration += vf->duration;
                    } else if (pic_struct == PIC_TRIPLE_FRAME) {
                        vf->duration += vf->duration << 1;
                    }
                }

                vf->index = buffer_index;
#ifdef NV21
                vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD | VIDTYPE_VIU_NV21;
#else
                vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
#endif
                vf->duration_pulldown = 0;
                vf->index = buffer_index;
                vf->pts = (pts_valid) ? pts : 0;
                vf->pts_us64= (pts_valid) ? pts_us64 : 0;
                vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[buffer_index]);
                vfbuf_use[buffer_index]++;

                if ((error_recovery_mode_use & 2) && error) {
                    kfifo_put(&recycle_q, (const vframe_t **)&vf);
                } else {
                    p_last_vf = vf;

                    kfifo_put(&display_q, (const vframe_t **)&vf);

                    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                }

            } else {
                if (pic_struct_present && pic_struct == PIC_TOP_BOT) {
                    vf->type = VIDTYPE_INTERLACE_TOP;
                } else if (pic_struct_present && pic_struct == PIC_BOT_TOP) {
                    vf->type = VIDTYPE_INTERLACE_BOTTOM;
                } else {
                    vf->type = poc_sel ? VIDTYPE_INTERLACE_BOTTOM : VIDTYPE_INTERLACE_TOP;
                }
#ifdef NV21
                vf->type |= VIDTYPE_VIU_NV21;
#endif
                vf->type |= VIDTYPE_INTERLACE_FIRST;

                vf->duration >>= 1;
                vf->duration_pulldown = 0;
                vf->index = buffer_index;
                vf->pts = (pts_valid) ? pts : 0;
                vf->pts_us64= (pts_valid) ? pts_us64 : 0;
                vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[buffer_index]);
                vfbuf_use[buffer_index]++;
                vf->ready_jiffies64=jiffies_64;

                if ((error_recovery_mode_use & 2) && error) {
                    kfifo_put(&recycle_q, (const vframe_t **)&vf);
                    continue;
                } else {
                    kfifo_put(&display_q, (const vframe_t **)&vf);
                }

                if (READ_VREG(AV_SCRATCH_F) & 2) {
                    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                    continue;
                }

                if (kfifo_get(&newframe_q, &vf) == 0) {
                    printk("fatal error, no available buffer slot.");
                    return IRQ_HANDLED;
                }

                set_frame_info(vf);

                if (pic_struct_present && pic_struct == PIC_TOP_BOT) {
                    vf->type = VIDTYPE_INTERLACE_BOTTOM;
                } else if (pic_struct_present && pic_struct == PIC_BOT_TOP) {
                    vf->type = VIDTYPE_INTERLACE_TOP;
                } else {
                    vf->type = poc_sel ? VIDTYPE_INTERLACE_TOP : VIDTYPE_INTERLACE_BOTTOM;
                }

#ifdef NV21
                vf->type |= VIDTYPE_VIU_NV21;
#endif

                vf->duration >>= 1;
                vf->duration_pulldown = 0;
                vf->index = buffer_index;
                vf->pts = 0;

                vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[buffer_index]);
                vfbuf_use[buffer_index]++;

                p_last_vf = vf;
                vf->ready_jiffies64=jiffies_64;

                kfifo_put(&display_q, (const vframe_t **)&vf);

                vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
            }
        }

        WRITE_VREG(AV_SCRATCH_0, 0);
    } else if ((cpu_cmd & 0xff) == 3) {
        vh264_running = 1;
        vh264_stream_switching_state = SWITCHING_STATE_ON_CMD3;

        printk("Enter switching mode cmd3.\n");
        schedule_work(&stream_switching_work);

    } else if ((cpu_cmd & 0xff) == 4) {
        vh264_running = 1;
        // reserved for slice group
        WRITE_VREG(AV_SCRATCH_0, 0);
    } else if ((cpu_cmd & 0xff) == 5) {
        vh264_running = 1;
        // reserved for slice group
        WRITE_VREG(AV_SCRATCH_0, 0);
    } else if ((cpu_cmd & 0xff) == 6) {
        vh264_running = 0;
        fatal_error_flag = DECODER_FATAL_ERROR_UNKNOW;
        // this is fatal error, need restart
        printk("fatal error happend\n");
        if (!fatal_error_reset) {
            schedule_work(&error_wd_work);
        }
    } else if ((cpu_cmd & 0xff) == 7) {
        vh264_running = 0;
        frame_width = (READ_VREG(AV_SCRATCH_1) + 1) * 16;
        printk("Over decoder supported size, width = %d\n", frame_width);
        fatal_error_flag = DECODER_FATAL_ERROR_SIZE_OVERFLOW;
    } else if ((cpu_cmd & 0xff) == 8) {
        vh264_running = 0;
        frame_height = (READ_VREG(AV_SCRATCH_1) + 1) * 16;
        printk("Over decoder supported size, height = %d\n", frame_height);
        fatal_error_flag = DECODER_FATAL_ERROR_SIZE_OVERFLOW;
    }

#ifdef HANDLE_H264_IRQ
    return IRQ_HANDLED;
#else
    return;
#endif
}

static void vh264_put_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;
    unsigned int wait_buffer_status;
    unsigned int wait_i_pass_frames;
    unsigned int reg_val;

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
#ifndef HANDLE_H264_IRQ
    vh264_isr();
#endif

    if (vh264_stream_switching_state != SWITCHING_STATE_OFF) {
        wait_buffer_counter = 0;
    } else {
        reg_val = READ_VREG(AV_SCRATCH_9);
        wait_buffer_status = reg_val & (1 << 31);
        wait_i_pass_frames = reg_val & 0xff;
        if (wait_buffer_status) {
            if (kfifo_is_empty(&display_q) &&
                kfifo_is_empty(&recycle_q) &&
                (state == RECEIVER_INACTIVE)) {
                printk("$$$$$$decoder is waiting for buffer\n");
                if (++wait_buffer_counter > 4) {
                    amvdec_stop();

#ifdef CONFIG_POST_PROCESS_MANAGER
                    vh264_ppmgr_reset();
#else
                    vf_light_unreg_provider(&vh264_vf_prov);
                    vh264_local_init();
                    vf_reg_provider(&vh264_vf_prov);
#endif
                    vh264_prot_init();
                    amvdec_start();
                }
            } else {
                wait_buffer_counter = 0;
            }
        } else if (wait_i_pass_frames > 1000) {
            printk("i passed frames > 1000\n");
            amvdec_stop();
#ifdef CONFIG_POST_PROCESS_MANAGER
            vh264_ppmgr_reset();
#else
            vf_light_unreg_provider(&vh264_vf_prov);
            vh264_local_init();
            vf_reg_provider(&vh264_vf_prov);
#endif
            vh264_prot_init();
            amvdec_start();
        }
    }

#if 0
    if (!wait_buffer_status) {
        if (vh264_no_disp_count++ > NO_DISP_WD_COUNT) {
            printk("$$$decoder did not send frame out\n");
            amvdec_stop();
#ifdef CONFIG_POST_PROCESS_MANAGER
            vh264_ppmgr_reset();
#else
            vf_light_unreg_provider(PROVIDER_NAME);
            vh264_local_init();
            vf_reg_provider(vh264_vf_prov);
#endif
            vh264_prot_init();
            amvdec_start();

            vh264_no_disp_count = 0;
            vh264_no_disp_wd_count++;
        }
    }
#endif

    while (!kfifo_is_empty(&recycle_q) &&
           ((READ_VREG(AV_SCRATCH_7) == 0) || (READ_VREG(AV_SCRATCH_8) == 0)) &&
           (vh264_stream_switching_state == SWITCHING_STATE_OFF)) {
        vframe_t *vf;
        if (kfifo_get(&recycle_q, &vf)) {
            if ((vf->index >= 0) && (vf != &switching_fense_vf)) {
                if (--vfbuf_use[vf->index] == 0) {
                    if (READ_VREG(AV_SCRATCH_7) == 0) {
                        WRITE_VREG(AV_SCRATCH_7, vf->index + 1);
                    } else {
                        WRITE_VREG(AV_SCRATCH_8, vf->index + 1);
                    }
                }

                vf->index = VF_BUF_NUM;
                kfifo_put(&newframe_q, (const vframe_t **)&vf);
            }
        }
    }

    if (vh264_stream_switching_state != SWITCHING_STATE_OFF) {
        while (!kfifo_is_empty(&recycle_q)) {
            vframe_t *vf;
            if (kfifo_get(&recycle_q, &vf)) {
                if ((vf->index >= 0) && (vf != &switching_fense_vf)) {
                    vf->index = VF_BUF_NUM;
                    kfifo_put(&newframe_q, (const vframe_t **)&vf);
                }
            }
        }

        WRITE_VREG(AV_SCRATCH_7, 0);
        WRITE_VREG(AV_SCRATCH_8, 0);

        if (kfifo_len(&newframe_q) == VF_POOL_SIZE) {
            stream_switching_done();
        }
    }

    timer->expires = jiffies + PUT_INTERVAL;
    vdec_dfs();

    add_timer(timer);
}

int vh264_dec_status(struct vdec_status *vstatus)
{
    vstatus->width = frame_width;
    vstatus->height = frame_height;
    if (frame_dur != 0) {
        vstatus->fps = 96000 / frame_dur;
    } else {
        vstatus->fps = -1;
    }
    vstatus->error_count = READ_VREG(AV_SCRATCH_D);
    vstatus->status = stat;
    if (fatal_error_reset) {
        vstatus->status |= fatal_error_flag;
    }
    return 0;
}

int vh264_set_trickmode(unsigned long trickmode)
{
    if (trickmode == TRICKMODE_I) {
        WRITE_VREG(AV_SCRATCH_F, (READ_VREG(AV_SCRATCH_F) & 0xfffffffc) | 2);
        trickmode_i = 1;
    } else if (trickmode == TRICKMODE_NONE) {
        WRITE_VREG(AV_SCRATCH_F, READ_VREG(AV_SCRATCH_F) & 0xfffffffc);
        trickmode_i = 0;
    }

    return 0;
}

static void vh264_prot_init(void)
{

    while (READ_VREG(DCAC_DMA_CTRL) & 0x8000) {
        ;
    }
    while (READ_VREG(LMEM_DMA_CTRL) & 0x8000) {
        ;    // reg address is 0x350
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET0, 0);

    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);

    WRITE_VREG(DOS_SW_RESET0, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET0, 0);

    WRITE_VREG(DOS_SW_RESET0, (1<<9) | (1<<8));
    WRITE_VREG(DOS_SW_RESET0, 0);

    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);

#else
    WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC | RESET_VLD_PART);
    READ_MPEG_REG(RESET0_REGISTER);
    WRITE_MPEG_REG(RESET0_REGISTER, RESET_IQIDCT | RESET_MC | RESET_VLD_PART);

    WRITE_MPEG_REG(RESET2_REGISTER, RESET_PIC_DC | RESET_DBLK);
#endif
    WRITE_VREG(POWER_CTL_VLD, READ_VREG(POWER_CTL_VLD) | (0 << 10) | (1 << 9) | (1 << 6));

    /* disable PSCALE for hardware sharing */
    WRITE_VREG(PSCALE_CTRL, 0);

    WRITE_VREG(AV_SCRATCH_0, 0);
    WRITE_VREG(AV_SCRATCH_1, buf_offset);
    WRITE_VREG(AV_SCRATCH_G, mc_dma_handle);
    WRITE_VREG(AV_SCRATCH_7, 0);
    WRITE_VREG(AV_SCRATCH_8, 0);
    WRITE_VREG(AV_SCRATCH_9, 0);

    error_recovery_mode_use = (error_recovery_mode != 0) ? error_recovery_mode : error_recovery_mode_in;
    WRITE_VREG(AV_SCRATCH_F, (READ_VREG(AV_SCRATCH_F) & 0xffffffc3) | ((error_recovery_mode_use & 0x1) << 4));

    /* clear mailbox interrupt */
    WRITE_VREG(ASSIST_MBOX1_CLR_REG, 1);

    /* enable mailbox interrupt */
    WRITE_VREG(ASSIST_MBOX1_MASK, 1);

#ifdef NV21
    SET_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
#endif
    if (ucode_type == UCODE_IP_ONLY_PARAM )
    {
        SET_VREG_MASK(AV_SCRATCH_F, 1<<6);
    }
    else
    {
        CLEAR_VREG_MASK(AV_SCRATCH_F, 1<<6);
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    //printk("vh264 meson8 prot init\n");
    WRITE_VREG(MDEC_PIC_DC_THRESH, 0x404038aa);
#endif
}

static void vh264_local_init(void)
{
    int i;

    vh264_ratio = vh264_amstream_dec_info.ratio;
    //vh264_ratio = 0x100;

    vh264_rotation = (((u32)vh264_amstream_dec_info.param) >> 16) & 0xffff;

    frame_buffer_size = AVIL_DPB_BUFF_SIZE + buf_size - DEFAULT_MEM_SIZE;
    frame_prog = 0;
    frame_width = vh264_amstream_dec_info.width;
    frame_height = vh264_amstream_dec_info.height;
    frame_dur = vh264_amstream_dec_info.rate;
    pts_outside = ((u32)vh264_amstream_dec_info.param) & 0x01;
    sync_outside = ((u32)vh264_amstream_dec_info.param & 0x02) >> 1;
    use_idr_framerate = ((u32)vh264_amstream_dec_info.param & 0x04) >> 2;
    max_refer_buf = !(((u32)vh264_amstream_dec_info.param & 0x10) >> 4);

    printk("H264 sysinfo: %dx%d duration=%d, pts_outside=%d, sync_outside=%d, use_idr_framerate=%d\n",
            frame_width, frame_height, frame_dur, pts_outside, sync_outside, use_idr_framerate);

    if ((u32)vh264_amstream_dec_info.param & 0x08)
    {
        ucode_type = UCODE_IP_ONLY_PARAM ;
    } else {
        ucode_type = 0;
    }

    if ((u32)vh264_amstream_dec_info.param & 0x20)
    {
        error_recovery_mode_in = 1;
    } else {
        error_recovery_mode_in = 3;
    }

    if (!vh264_running) {
        last_mb_width = 0;
        last_mb_height = 0;
    }

    for (i = 0; i < VF_BUF_NUM; i++) {
        vfbuf_use[i] = 0;
    }

    INIT_KFIFO(display_q);
    INIT_KFIFO(recycle_q);
    INIT_KFIFO(newframe_q);

    for (i = 0; i < VF_POOL_SIZE; i++) {
        const vframe_t *vf = &vfpool[i];
        vfpool[i].index = VF_BUF_NUM;
		vfpool[i].bufWidth = 1920;
        kfifo_put(&newframe_q, &vf);
    }

#ifdef DROP_B_FRAME_FOR_1080P_50_60FPS
    last_interlaced = 1;
#endif
    h264_first_pts_ready = 0;
    h264_first_valid_pts_ready=false;
    h264pts1 = 0;
    h264pts2 = 0;
    h264_pts_count = 0;
    duration_from_pts_done = 0;
    vh264_error_count = READ_VREG(AV_SCRATCH_D);

    p_last_vf = NULL;
    check_pts_discontinue = false;
    last_pts = 0;
    wait_buffer_counter = 0;
    vh264_no_disp_count = 0;
    fatal_error_flag = 0;
#ifdef DEBUG_PTS
    pts_missed = 0;
    pts_hit = 0;
#endif

    return;
}

static s32 vh264_init(void)
{
    int trickmode_fffb = 0;
    int firmwareloaded=0;

    //printk("\nvh264_init\n");
    init_timer(&recycle_timer);

    stat |= STAT_TIMER_INIT;

    vh264_running = 0;    //init here to reset last_mb_width&last_mb_height
    vh264_eos = 0;
    duration_on_correcting=0;
    vh264_local_init();

    query_video_status(0, &trickmode_fffb);

    if (!trickmode_fffb) {
        void __iomem *p = ioremap_nocache(ucode_map_start, V_BUF_ADDR_OFFSET);
        if (p != NULL) {
            memset(p, 0, V_BUF_ADDR_OFFSET);
            iounmap(p);
        }
    }

    amvdec_enable();

    // -- ucode loading (amrisc and swap code)
    mc_cpu_addr = dma_alloc_coherent(NULL, MC_TOTAL_SIZE, &mc_dma_handle, GFP_KERNEL);
    if (!mc_cpu_addr) {
        amvdec_disable();

        printk("vh264_init: Can not allocate mc memory.\n");
        return -ENOMEM;
    }

    printk("264 ucode swap area: physical address 0x%x, cpu virtual addr %p\n", mc_dma_handle, mc_cpu_addr);
	/*while for easy break out, always  run once.*/
    while(debugfirmware){
		int size;
        char *mbuf, *mc;
        const char *pvh264_header_mc, *pvh264_data_mc, *pvh264_mmco_mc, *pvh264_list_mc, *pvh264_slice_mc;
        printk("start debug load firmware ...\n");
        mbuf=kmalloc(4096 * 4*6, GFP_KERNEL);
        if (!mbuf) {
            printk("vh264_init: Cannot malloc mbuf  memory1\n");
            break;
        }
        memset(mbuf,0,4096 * 4*6);
        size=request_video_firmware("vh264_mc",mbuf,4096 * 4*6);
        if(size<0 || size < (0x800 + 0x400*4)*4){
            printk("vh264_init: not valied ucode for vh264");
            kfree(mbuf);
            break;
        }
        pvh264_header_mc=mbuf+(0x800 + 0x400*2)*4;
        pvh264_data_mc=mbuf+(0x800 + 0x400*1)*4;
        pvh264_mmco_mc=mbuf+(0x800 + 0x400*4)*4;
        pvh264_list_mc=mbuf+(0x800 + 0x400*3)*4;
        pvh264_slice_mc=mbuf+(0x800 + 0x400*0)*4;
        mc=kmalloc(4096 * 4, GFP_KERNEL);
        if (!mc) {
            kfree(mbuf);
            printk("vh264_init: Cannot malloc mbuf  memory3\n");
            break;
        }
        memcpy(mc,mbuf,0x800*4);
        memcpy(mc+0x800*4,pvh264_data_mc,0x400*4);
        memcpy(mc+0x800*4+0x400*4,pvh264_list_mc,0x400*4);
        if (amvdec_loadmc((const u32 *)mc) < 0) {
            kfree(mbuf);
            kfree(mc);
            dma_free_coherent(NULL, MC_TOTAL_SIZE, mc_cpu_addr, mc_dma_handle);
            mc_cpu_addr = NULL;
            break;
        }

        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_HEADER, pvh264_header_mc, MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_DATA,   pvh264_data_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MMCO,   pvh264_mmco_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_LIST,   pvh264_list_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_SLICE,  pvh264_slice_mc,  MC_SWAP_SIZE);

        kfree(mbuf);
        kfree(mc);
		firmwareloaded=1;
		break;
    };

	if(!firmwareloaded){
        printk("start load original firmware ...\n");
        if (amvdec_loadmc(vh264_mc) < 0) {
            amvdec_disable();
            return -EBUSY;
        }

        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_HEADER, vh264_header_mc, MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_DATA,   vh264_data_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_MMCO,   vh264_mmco_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_LIST,   vh264_list_mc,   MC_SWAP_SIZE);
        memcpy((u8 *)mc_cpu_addr + MC_OFFSET_SLICE,  vh264_slice_mc,  MC_SWAP_SIZE);
    }

    stat |= STAT_MC_LOAD;

    /* enable AMRISC side protocol */
    vh264_prot_init();

#ifdef HANDLE_H264_IRQ
    if (request_irq(INT_VDEC, vh264_isr,
                    IRQF_SHARED, "vh264-irq", (void *)vh264_dec_id)) {
        printk("vh264 irq register error.\n");
        amvdec_disable();
        return -ENOENT;
    }
#endif

    stat |= STAT_ISR_REG;


 #ifdef CONFIG_POST_PROCESS_MANAGER
    vf_provider_init(&vh264_vf_prov, PROVIDER_NAME, &vh264_vf_provider_ops, NULL);
    vf_reg_provider(&vh264_vf_prov);
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
 #else
    vf_provider_init(&vh264_vf_prov, PROVIDER_NAME, &vh264_vf_provider_ops, NULL);
    vf_reg_provider(&vh264_vf_prov);
 #endif

    vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_FR_HINT, (void *)frame_dur);

    stat |= STAT_VF_HOOK;

    recycle_timer.data = (ulong) & recycle_timer;
    recycle_timer.function = vh264_put_timer_func;
    recycle_timer.expires = jiffies + PUT_INTERVAL;

    add_timer(&recycle_timer);

    stat |= STAT_TIMER_ARM;

    vh264_stream_switching_state = SWITCHING_STATE_OFF;

    stat |= STAT_VDEC_RUN;
    wmb();

    // -- start decoder
    amvdec_start();

    set_vdec_func(&vh264_dec_status);
    set_trickmode_func(&vh264_set_trickmode);

    return 0;
}

static int vh264_stop(int mode)
{
    if (stat & STAT_VDEC_RUN) {
        amvdec_stop();
        stat &= ~STAT_VDEC_RUN;
    }

    if (stat & STAT_ISR_REG) {
        WRITE_VREG(ASSIST_MBOX1_MASK, 0);
        free_irq(INT_VDEC,
                 (void *)vh264_dec_id);
        stat &= ~STAT_ISR_REG;
    }

    if (stat & STAT_TIMER_ARM) {
        del_timer_sync(&recycle_timer);
        stat &= ~STAT_TIMER_ARM;
    }

    if (stat & STAT_VF_HOOK) {
        if (mode == MODE_FULL) {
            vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_FR_END_HINT, NULL);
        }

        vf_unreg_provider(&vh264_vf_prov);
        stat &= ~STAT_VF_HOOK;
    }

    if (stat & STAT_MC_LOAD) {
        if (mc_cpu_addr != NULL) {
            dma_free_coherent(NULL, MC_TOTAL_SIZE, mc_cpu_addr, mc_dma_handle);
            mc_cpu_addr = NULL;
        }
    }
    amvdec_disable();

    return 0;
}

static void error_do_work(struct work_struct *work)
{
    mutex_lock(&vh264_mutex);

    /*
     * we need to lock vh264_stop/vh264_init.
     * because we will call amvdec_h264_remove on this step;
     * then we may call more than once on free_irq/deltimer/..and some other.
     */
    if (atomic_read(&vh264_active)) {
        amvdec_stop();

#ifdef CONFIG_POST_PROCESS_MANAGER
        vh264_ppmgr_reset();
#else
        vf_light_unreg_provider(&vh264_vf_prov);

        vh264_local_init();

        vf_reg_provider(&vh264_vf_prov);
#endif

        vh264_prot_init();

        amvdec_start();
    }

    mutex_unlock(&vh264_mutex);
}

static void stream_switching_done(void)
{
    int state = vh264_stream_switching_state;

    WRITE_VREG(AV_SCRATCH_7, 0);
    WRITE_VREG(AV_SCRATCH_8, 0);
    WRITE_VREG(AV_SCRATCH_9, 0);

    if (state == SWITCHING_STATE_ON_CMD1) {
        vh264_set_params();
    }

    vh264_stream_switching_state = SWITCHING_STATE_OFF;

    wmb();

    if (state == SWITCHING_STATE_ON_CMD3) {
        WRITE_VREG(AV_SCRATCH_0, 0);
    }

    printk("Leaving switching mode.\n");
}

#if !defined(NV21)|| !defined(CONFIG_GE2D_KEEP_FRAME)
static int canvas_dup(u8 *dst, ulong src_paddr, ulong size)
{
    void __iomem *p = ioremap_wc(src_paddr, size);
    if (p) {
        memcpy(dst, p, size);
        iounmap(p);
        return 1;
    }
    return 0;
}
#endif

/* construt a new frame as a copy of last frame so frame receiver can
 * release all buffer resources to decoder.
 */
static void stream_switching_do(struct work_struct *work)
{
    unsigned int buffer_index;
    bool do_copy = true;
    int mb_total_num, mb_width_num, mb_height_num;
    vframe_t *vf;

#ifdef CONFIG_GE2D_KEEP_FRAME
    u32 y_index, u_index,src_index,des_index,y_desindex,u_dexindex;
    canvas_t csy,csu,cyd;
#endif

    if ((!p_last_vf) || (vh264_stream_switching_state == SWITCHING_STATE_OFF)) {
        return;
    }

    if (atomic_read(&vh264_active)) {
        ulong videoKeepBuf[3], videoKeepBufPhys[3];

        get_video_keep_buffer(videoKeepBuf, videoKeepBufPhys);
#ifdef NV21
        if (!videoKeepBuf[0] || !videoKeepBuf[1]) {
            do_copy = false;
        }
#else
        if (!videoKeepBuf[0] || !videoKeepBuf[1] || !videoKeepBuf[2]) {
            do_copy = false;
        }
#endif
        buffer_index = p_last_vf->index;
        mb_total_num = mb_total;
        mb_width_num = mb_width;
        mb_height_num = mb_height;

        /* construct a clone of the frame from last frame */
        if (do_copy) {
            /* construct a clone of the frame from last frame */
#ifdef NV21
#ifdef CONFIG_GE2D_KEEP_FRAME
            printk("src yaddr[0x%x] index[%d] width[%d] heigth[%d]\n",buffer_spec[buffer_index].y_addr,buffer_spec[buffer_index].y_canvas_index,\
                buffer_spec[buffer_index].y_canvas_width,buffer_spec[buffer_index].y_canvas_height);

            printk("src uaddr[0x%x] index[%d] width[%d] heigth[%d]\n",buffer_spec[buffer_index].u_addr,buffer_spec[buffer_index].u_canvas_index,\
                buffer_spec[buffer_index].u_canvas_width,buffer_spec[buffer_index].u_canvas_height);
            y_index = buffer_spec[buffer_index].y_canvas_index;
            u_index = buffer_spec[buffer_index].u_canvas_index;
            canvas_read(y_index,&csy);
            canvas_read(u_index,&csu);

            canvas_config(0, videoKeepBufPhys[0], mb_width_num << 4, mb_height_num << 4,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);//CANVAS_BLKMODE_32X32);
            canvas_config(1, videoKeepBufPhys[1], mb_width_num << 4, mb_height_num << 3,
                              CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);//CANVAS_BLKMODE_32X32);

            y_desindex = 0;
            u_dexindex = 1;
            canvas_read(y_desindex,&cyd);
            src_index = ((y_index&0xff) | (( u_index << 8)& 0x0000ff00 ));
            des_index = ((y_desindex&0xff) | (( u_dexindex << 8)& 0x0000ff00 ));
            ge2d_canvas_dup(&csy,&csu,&cyd,GE2D_FORMAT_M24_NV21,src_index,des_index);
#else
            canvas_dup((u8 *)videoKeepBuf[0], buffer_spec[buffer_index].y_addr, mb_total_num<<8);
            canvas_dup((u8 *)videoKeepBuf[1], buffer_spec[buffer_index].u_addr, mb_total_num<<7);

            canvas_config(0, videoKeepBufPhys[0], mb_width_num << 4, mb_height_num << 4,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(1, videoKeepBufPhys[1], mb_width_num << 4, mb_height_num << 3,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif
#else
            canvas_dup((u8 *)videoKeepBuf[0], buffer_spec[buffer_index].y_addr, mb_total_num<<8);
            canvas_dup((u8 *)videoKeepBuf[1], buffer_spec[buffer_index].u_addr, mb_total_num<<6);
            canvas_dup((u8 *)videoKeepBuf[2], buffer_spec[buffer_index].v_addr, mb_total_num<<6);

            canvas_config(0, videoKeepBufPhys[0], mb_width_num << 4, mb_height_num << 4,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(1, videoKeepBufPhys[1], mb_width_num << 3, mb_height_num << 3,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
            canvas_config(2, videoKeepBufPhys[2], mb_width_num << 3, mb_height_num << 3,
                          CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_32X32);
#endif
        }

        switching_fense_vf = *p_last_vf;
        switching_fense_vf.duration = 1;
        switching_fense_vf.index = -1;
        if (do_copy) {
#ifdef NV21
            switching_fense_vf.canvas0Addr = 0x010100;
#else
            switching_fense_vf.canvas0Addr = 0x020100;
#endif
        }


        vf = &switching_fense_vf;

        if (vh264_stream_switching_state != SWITCHING_STATE_OFF) {
            /* we only insert the fense frame when necessary,
             * if all buffers are already recycled to decoder then it's not
             * necessary to post fense frame. Such cases may happen when 
             * receiver side generates its own buffer for final output, such
             * as ppmgr and ionvideo. The frame buffer from provider will
             * be recycled pretty fast. It could happen that all frames are 
             * returned to decoder before a fense frame is ready from this delay work queue.
             */
            kfifo_put(&display_q, (const vframe_t **)&vf);
            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);

            printk("Switching fense frame post\n");
        }
    }
}

static int amvdec_h264_probe(struct platform_device *pdev)
{
    struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;

    mutex_lock(&vh264_mutex);

    if (pdata == NULL) {
        printk("\namvdec_h264 memory resource undefined.\n");
        mutex_unlock(&vh264_mutex);
        return -EFAULT;
    }

    ucode_map_start = pdata->mem_start;
    buf_size = pdata->mem_end - pdata->mem_start + 1;
    if (buf_size < DEFAULT_MEM_SIZE) {
        printk("\namvdec_h264 memory size not enough.\n");
        return -ENOMEM;
    }

    buf_offset = pdata->mem_start - DEF_BUF_START_ADDR;
    buf_start = V_BUF_ADDR_OFFSET + pdata->mem_start;

    if (pdata->sys_info) {
        vh264_amstream_dec_info = *pdata->sys_info;
    }

    printk("amvdec_h264 mem-addr=%lx,buff_offset=%x,buf_start=%x\n",pdata->mem_start,buf_offset,buf_start);

    if (vh264_init() < 0) {
        printk("\namvdec_h264 init failed.\n");
        mutex_unlock(&vh264_mutex);
        return -ENODEV;
    }

    INIT_WORK(&error_wd_work, error_do_work);
    INIT_WORK(&stream_switching_work, stream_switching_do);

    atomic_set(&vh264_active, 1);

    mutex_unlock(&vh264_mutex);

    return 0;
}

static int amvdec_h264_remove(struct platform_device *pdev)
{
    cancel_work_sync(&error_wd_work);
    cancel_work_sync(&stream_switching_work);

    mutex_lock(&vh264_mutex);
    vh264_stop(MODE_FULL);

    atomic_set(&vh264_active, 0);

#ifdef DEBUG_PTS
    printk("pts missed %ld, pts hit %ld, pts_outside %d, duration %d, sync_outside %d, use_idr_framerate %d\n",
           pts_missed, pts_hit, pts_outside, frame_dur, sync_outside, use_idr_framerate);
#endif
    mutex_unlock(&vh264_mutex);
    return 0;
}

/****************************************/

static struct platform_driver amvdec_h264_driver = {
    .probe   = amvdec_h264_probe,
    .remove  = amvdec_h264_remove,
#ifdef CONFIG_PM
    .suspend = amvdec_suspend,
    .resume  = amvdec_resume,
#endif
    .driver  = {
        .name = DRIVER_NAME,
    }
};

static struct codec_profile_t amvdec_h264_profile = {
    .name = "h264",
    .profile = ""
};

static int __init amvdec_h264_driver_init_module(void)
{
    printk("amvdec_h264 module init\n");
#ifdef CONFIG_GE2D_KEEP_FRAME	
    ge2d_videoh264task_init();
#endif
    if (platform_driver_register(&amvdec_h264_driver)) {
        printk("failed to register amvdec_h264 driver\n");
        return -ENODEV;
    }
    vcodec_profile_register(&amvdec_h264_profile);
    return 0;
}

static void __exit amvdec_h264_driver_remove_module(void)
{
    printk("amvdec_h264 module remove.\n");

    platform_driver_unregister(&amvdec_h264_driver);
#ifdef CONFIG_GE2D_KEEP_FRAME	
    ge2d_videoh264task_release();
#endif
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_h264 stat \n");
module_param(error_recovery_mode, uint, 0664);
MODULE_PARM_DESC(error_recovery_mode, "\n amvdec_h264 error_recovery_mode \n");
module_param(sync_outside, uint, 0664);
MODULE_PARM_DESC(sync_outside, "\n amvdec_h264 sync_outside \n");
module_param(dec_control, uint, 0664);
MODULE_PARM_DESC(dec_control, "\n amvdec_h264 decoder control \n");
module_param(fatal_error_reset, uint, 0664);
MODULE_PARM_DESC(fatal_error_reset, "\n amvdec_h264 decoder reset when fatal error happens \n");
module_param(max_refer_buf, uint, 0664);
MODULE_PARM_DESC(max_refer_buf, "\n amvdec_h264 decoder buffering or not for reference frame \n");
module_param(ucode_type, uint, 0664);
MODULE_PARM_DESC(ucode_type, "\n amvdec_h264 decoder buffering or not for reference frame \n");
module_param(debugfirmware, uint, 0664);
MODULE_PARM_DESC(debugfirmware, "\n amvdec_h264 debug load firmware \n");

module_init(amvdec_h264_driver_init_module);
module_exit(amvdec_h264_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC H264 Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chen Zhang <chen.zhang@amlogic.com>");
