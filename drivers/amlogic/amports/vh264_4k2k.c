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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/platform_device.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/vformat.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/delay.h>

#include <mach/am_regs.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#include <mach/vpu.h>
#endif
#include "vdec_reg.h"

#include "vdec.h"
#include "amvdec.h"

#include "vh264_4k2k_mc.h"

#if  MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TVD
#define DOUBLE_WRITE
#endif

#define DRIVER_NAME "amvdec_h264_4k2k"
#define MODULE_NAME "amvdec_h264_4k2k"

#define PUT_INTERVAL        (HZ/100)
#define ERROR_RESET_COUNT   500

#define H264_4K2K_SINGLE_CORE   IS_MESON_M8M2_CPU


extern void amvenc_dos_top_reg_fix(void);

static int  vh264_4k2k_vf_states(vframe_states_t *states, void*);
static vframe_t *vh264_4k2k_vf_peek(void*);
static vframe_t *vh264_4k2k_vf_get(void*);
static void vh264_4k2k_vf_put(vframe_t *, void*);
static int vh264_4k2k_event_cb(int type, void *data, void *private_data);

static void vh264_4k2k_prot_init(void);
static void vh264_4k2k_local_init(void);
static void vh264_4k2k_put_timer_func(unsigned long arg);

static const char vh264_4k2k_dec_id[] = "vh264_4k2k-dev";
static const char vh264_4k2k_dec_id2[] = "vh264_4k2k-vdec2-dev";

#define PROVIDER_NAME   "decoder.h264_4k2k"

static const struct vframe_operations_s vh264_4k2k_vf_provider = {
    .peek      = vh264_4k2k_vf_peek,
    .get       = vh264_4k2k_vf_get,
    .put       = vh264_4k2k_vf_put,
    .event_cb  = vh264_4k2k_event_cb,
    .vf_states = vh264_4k2k_vf_states,
};
static struct vframe_provider_s vh264_4k2k_vf_prov;

static u32 frame_width, frame_height, frame_dur, frame_ar;
static struct timer_list recycle_timer;
static u32 stat;
static u32 error_watchdog_count;

#ifdef DEBUG_PTS
static unsigned long pts_missed, pts_hit;
#endif

static struct dec_sysinfo vh264_4k2k_amstream_dec_info;
extern u32 trickmode_i;

static DEFINE_SPINLOCK(lock);
static int fatal_error;

static DEFINE_MUTEX(vh264_4k2k_mutex);

static void (*probe_callback)(void) = NULL;
static void (*remove_callback)(void) = NULL;
static struct device *cma_dev;

// bit[3:0] command :
//           0 - command finished
//               (DATA0 - {level_idc_mmco, max_reference_frame_num, width, height}
//           1 - alloc view_0 display_buffer and reference_data_area
//           2 - alloc view_1 display_buffer and reference_data_area
#define MAILBOX_COMMAND         AV_SCRATCH_0
#define MAILBOX_DATA_0          AV_SCRATCH_1
#define MAILBOX_DATA_1          AV_SCRATCH_2
#define MAILBOX_DATA_2          AV_SCRATCH_3
#define MAILBOX_DATA_3          AV_SCRATCH_4
#define MAILBOX_DATA_4          AV_SCRATCH_5
#define CANVAS_START            AV_SCRATCH_6
#define BUFFER_RECYCLE          AV_SCRATCH_7
#define PICTURE_COUNT           AV_SCRATCH_9
#define DECODE_STATUS           AV_SCRATCH_A
#define SPS_STATUS              AV_SCRATCH_B
#define PPS_STATUS              AV_SCRATCH_C
#define MS_ID                   AV_SCRATCH_D
#define WORKSPACE_START         AV_SCRATCH_E
#define DECODED_PIC_NUM         AV_SCRATCH_F
#define DECODE_ERROR_CNT        AV_SCRATCH_G
#define CURRENT_UCODE           AV_SCRATCH_H
#define CURRENT_SPS_PPS         AV_SCRATCH_I // bit[15:9]-SPS, bit[8:0]-PPS
#define DECODE_SKIP_PICTURE     AV_SCRATCH_J
#define RESERVED_REG_K          AV_SCRATCH_K
#define RESERVED_REG_L          AV_SCRATCH_L
#define REF_START_VIEW_0        AV_SCRATCH_M
#define REF_START_VIEW_1        AV_SCRATCH_N

#define VDEC2_MAILBOX_COMMAND         VDEC2_AV_SCRATCH_0
#define VDEC2_MAILBOX_DATA_0          VDEC2_AV_SCRATCH_1
#define VDEC2_MAILBOX_DATA_1          VDEC2_AV_SCRATCH_2
#define VDEC2_MAILBOX_DATA_2          VDEC2_AV_SCRATCH_3
#define VDEC2_MAILBOX_DATA_3          VDEC2_AV_SCRATCH_4
#define VDEC2_MAILBOX_DATA_4          VDEC2_AV_SCRATCH_5
#define VDEC2_CANVAS_START            VDEC2_AV_SCRATCH_6
#define VDEC2_BUFFER_RECYCLE          VDEC2_AV_SCRATCH_7
#define VDEC2_PICTURE_COUNT           VDEC2_AV_SCRATCH_9
#define VDEC2_DECODE_STATUS           VDEC2_AV_SCRATCH_A
#define VDEC2_SPS_STATUS              VDEC2_AV_SCRATCH_B
#define VDEC2_PPS_STATUS              VDEC2_AV_SCRATCH_C
#define VDEC2_MS_ID                   VDEC2_AV_SCRATCH_D
#define VDEC2_WORKSPACE_START         VDEC2_AV_SCRATCH_E
#define VDEC2_DECODED_PIC_NUM         VDEC2_AV_SCRATCH_F
#define VDEC2_DECODE_ERROR_CNT        VDEC2_AV_SCRATCH_G
#define VDEC2_CURRENT_UCODE           VDEC2_AV_SCRATCH_H
#define VDEC2_CURRENT_SPS_PPS         VDEC2_AV_SCRATCH_I // bit[15:9]-SPS, bit[8:0]-PPS
#define VDEC2_DECODE_SKIP_PICTURE     VDEC2_AV_SCRATCH_J
#define VDEC2_RESERVED_REG_K          VDEC2_AV_SCRATCH_K
#define VDEC2_RESERVED_REG_L          VDEC2_AV_SCRATCH_L
#define VDEC2_REF_START_VIEW_0        VDEC2_AV_SCRATCH_M
#define VDEC2_REF_START_VIEW_1        VDEC2_AV_SCRATCH_N

/********************************************
 *  DECODE_STATUS Define
********************************************/
#define DECODE_IDLE             0
#define DECODE_START_HEADER     1
#define DECODE_HEADER           2
#define DECODE_START_MMCO       3
#define DECODE_MMCO             4
#define DECODE_START_SLICE      5
#define DECODE_SLICE            6
#define DECODE_WAIT_BUFFER      7

/********************************************
 *  Dual Core Communication 
********************************************/
#define FATAL_ERROR             DOS_SCRATCH16
#define PRE_MASTER_UPDATE_TIMES DOS_SCRATCH20
// bit[31] - REQUEST
// bit[30:0] - MASTER_UPDATE_TIMES
#define SLAVE_WAIT_DPB_UPDATE   DOS_SCRATCH21
// [15:8] - current_ref, [7:0] current_dpb (0x80 means no buffer found)
#define SLAVE_REF_DPB           DOS_SCRATCH22
#define SAVE_MVC_ENTENSION_0    DOS_SCRATCH23
#define SAVE_I_POC              DOS_SCRATCH24
// bit[31:30] - core_status 0-idle, 1-mmco, 2-decoding, 3-finished
// bit[29:0] - core_pic_count
#define CORE_STATUS_M           DOS_SCRATCH25
#define CORE_STATUS_S           DOS_SCRATCH26
#define SAVE_ref_status_view_0  DOS_SCRATCH27
#define SAVE_ref_status_view_1  DOS_SCRATCH28
#define ALLOC_INFO_0            DOS_SCRATCH29
#define ALLOC_INFO_1            DOS_SCRATCH30

/********************************************
 *  Mailbox command
 ********************************************/
#define CMD_FINISHED               0
#define CMD_ALLOC_VIEW             1
#define CMD_FRAME_DISPLAY          3
#define CMD_DEBUG                  10

static unsigned work_space_adr, decoder_buffer_start, decoder_buffer_end;
static unsigned reserved_buffer;

#define DECODE_BUFFER_NUM_MAX    32
#define DISPLAY_BUFFER_NUM       6

#define video_domain_addr(adr) (adr&0x7fffffff)
#define DECODER_WORK_SPACE_SIZE 0x400000

typedef struct {
    unsigned int y_addr;
    unsigned int uv_addr;
#ifdef DOUBLE_WRITE
    unsigned int y_dw_addr;
    unsigned int uv_dw_addr;
#endif

    int y_canvas_index;
    int uv_canvas_index;
#ifdef DOUBLE_WRITE
    int y_dw_canvas_index;
    int uv_dw_canvas_index;
#endif

    struct page *alloc_pages;
    int alloc_count;
} buffer_spec_t;

static buffer_spec_t buffer_spec[DECODE_BUFFER_NUM_MAX+DISPLAY_BUFFER_NUM];

#ifdef DOUBLE_WRITE
#define spec2canvas(x)  \
    (((x)->uv_dw_canvas_index << 16) | \
     ((x)->uv_dw_canvas_index << 8)  | \
     ((x)->y_dw_canvas_index << 0))
#else
#define spec2canvas(x)  \
    (((x)->uv_canvas_index << 16) | \
     ((x)->uv_canvas_index << 8)  | \
     ((x)->y_canvas_index << 0))
#endif

#define VF_POOL_SIZE        32

static DECLARE_KFIFO(newframe_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(display_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(recycle_q, vframe_t *, VF_POOL_SIZE);

static s32 vfbuf_use[DECODE_BUFFER_NUM_MAX];
static vframe_t vfpool[VF_POOL_SIZE];

static struct work_struct alloc_work;

static void set_frame_info(vframe_t *vf)
{
    unsigned int ar;

#ifdef DOUBLE_WRITE
    vf->width = frame_width / 2;
    vf->height = frame_height / 2;
#else
    vf->width = frame_width;
    vf->height = frame_height;
#endif
    vf->duration = frame_dur;
    vf->duration_pulldown = 0;
    vf->flag = 0;

    ar = min(frame_ar, (u32)DISP_RATIO_ASPECT_RATIO_MAX);
    vf->ratio_control = (ar << DISP_RATIO_ASPECT_RATIO_BIT);

    return;
}

static int vh264_4k2k_vf_states(vframe_states_t *states, void* op_arg)
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

static vframe_t *vh264_4k2k_vf_peek(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_peek(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static vframe_t *vh264_4k2k_vf_get(void* op_arg)
{
    vframe_t *vf;

    if (kfifo_get(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static void vh264_4k2k_vf_put(vframe_t *vf, void* op_arg)
{
    kfifo_put(&recycle_q, (const vframe_t **)&vf);
}

static int vh264_4k2k_event_cb(int type, void *data, void *private_data)
{
    if(type & VFRAME_EVENT_RECEIVER_RESET){
        unsigned long flags;
        amvdec_stop();

        if (!H264_4K2K_SINGLE_CORE) {
            amvdec2_stop();
        }

#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_light_unreg_provider(&vh264_4k2k_vf_prov);
#endif
        spin_lock_irqsave(&lock, flags);
        vh264_4k2k_local_init();
        vh264_4k2k_prot_init();
        spin_unlock_irqrestore(&lock, flags);
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_reg_provider(&vh264_4k2k_vf_prov);
#endif
        amvdec_start();

        if (!H264_4K2K_SINGLE_CORE) {
            amvdec2_start();
        }
    }

    return 0;
}

int init_canvas(int start_addr, long dpb_size, int dpb_number, int mb_width, int mb_height, buffer_spec_t* buffer_spec)
{
    unsigned long dpb_addr, addr;
    int i;
    int mb_total;
    int canvas_addr = ANC0_CANVAS_ADDR;
    int vdec2_canvas_addr = VDEC2_ANC0_CANVAS_ADDR;
    int index = AMVDEC_H264_4K2K_CANVAS_INDEX;
    u32 disp_addr = 0xffffffff;
    bool use_alloc = false;
    int alloc_count = 0;
    canvas_t cur_canvas;

    dpb_addr = start_addr + dpb_size;

    mb_total = mb_width * mb_height;

    canvas_read((READ_VCBUS_REG(VD1_IF0_CANVAS0) & 0xff), &cur_canvas);
    disp_addr = (cur_canvas.addr + 7) >> 3;

    mutex_lock(&vh264_4k2k_mutex);
    
    for (i=0; i<dpb_number; i++) {
        WRITE_VREG(canvas_addr++, index | ((index+1)<<8) | ((index+1)<<16));
        if (!H264_4K2K_SINGLE_CORE) {
            WRITE_VREG(vdec2_canvas_addr++, index | ((index+1)<<8) | ((index+1)<<16));
        }

        if (((dpb_addr + (mb_total << 8) + (mb_total << 7)) >= decoder_buffer_end) && (!use_alloc)) {
            printk("start alloc for %d/%d\n", i, dpb_number);
            use_alloc = true;
        }

        if (use_alloc) {
#ifdef DOUBLE_WRITE
            int page_count = PAGE_ALIGN((mb_total << 8) + (mb_total << 7) + (mb_total << 6) + (mb_total << 5)) / PAGE_SIZE;
#else
            int page_count = PAGE_ALIGN((mb_total << 8) + (mb_total << 7)) / PAGE_SIZE;
#endif

            if (buffer_spec[i].alloc_pages) {
                if (page_count != buffer_spec[i].alloc_count) {
                    printk("Delay released CMA buffer %d\n", i);

                    dma_release_from_contiguous(cma_dev, buffer_spec[i].alloc_pages, buffer_spec[i].alloc_count);
                    buffer_spec[i].alloc_pages = NULL;
                    buffer_spec[i].alloc_count = 0;
                } else {
                    printk("Re-use CMA buffer %d\n", i);
                }
            }

            if (!buffer_spec[i].alloc_pages) {
                buffer_spec[i].alloc_count = page_count;
                buffer_spec[i].alloc_pages = dma_alloc_from_contiguous(cma_dev, page_count, 0);
            } 
            alloc_count++;

            if (!buffer_spec[i].alloc_pages) {
                buffer_spec[i].alloc_count = 0;
                printk("264 4K2K decoder memory allocation failed %d.\n", i);
                mutex_unlock(&vh264_4k2k_mutex);
                return -1;
            }

            addr = page_to_phys(buffer_spec[i].alloc_pages);
            dpb_addr = addr;
        } else {
            if (buffer_spec[i].alloc_pages) {
                dma_release_from_contiguous(cma_dev, buffer_spec[i].alloc_pages, buffer_spec[i].alloc_count);
                buffer_spec[i].alloc_pages = NULL;
                buffer_spec[i].alloc_count = 0;
            }

            addr = dpb_addr;
            dpb_addr += dpb_size;
#ifdef DOUBLE_WRITE
            dpb_addr += dpb_size/4;
#endif
        }

        if (((addr + 7) >> 3) == disp_addr) {
            addr = start_addr;
        }

        buffer_spec[i].y_addr = addr;
        buffer_spec[i].y_canvas_index = index;
        canvas_config(index,
                  addr,
                  mb_width << 4,
                  mb_height << 4,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

        addr += mb_total << 8;
        index++;

        buffer_spec[i].uv_addr = addr;
        buffer_spec[i].uv_canvas_index = index;
        canvas_config(index,
                  addr,
                  mb_width << 4,
                  mb_height << 3,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

        addr += mb_total << 7;
        index++;

#ifdef DOUBLE_WRITE
        buffer_spec[i].y_dw_addr = addr;
        buffer_spec[i].y_dw_canvas_index = index;
        canvas_config(index,
                  addr,
                  mb_width << 3,
                  mb_height << 3,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

        addr += mb_total << 6;
        index++;

        buffer_spec[i].uv_dw_addr = addr;
        buffer_spec[i].uv_dw_canvas_index = index;
        canvas_config(index,
                  addr,
                  mb_width << 3,
                  mb_height << 2,
                  CANVAS_ADDR_NOWRAP,
                  CANVAS_BLKMODE_32X32);

        addr += mb_total << 5;
        index++;
#endif
    }

    mutex_unlock(&vh264_4k2k_mutex);

    printk("H264 4k2k decoder canvas allocation successful, %d CMA blocks allocated, canvas %d-%d\n", alloc_count, AMVDEC_H264_4K2K_CANVAS_INDEX, index-1);

    return 0;
}

static int get_max_dec_frame_buf_size(int level_idc, int max_reference_frame_num, int mb_width, int mb_height)
{
    int pic_size = mb_width * mb_height * 384;

    int size = 0;

    switch (level_idc) {
        case 9:
            size = 152064;
        break;
        case 10:
            size = 152064;
        break;
        case 11:
            size = 345600;
        break;
        case 12:
            size = 912384;
        break;
        case 13:
            size = 912384;
        break;
        case 20:
            size = 912384;
        break;
        case 21:
            size = 1824768;
        break;
        case 22:
            size = 3110400;
        break;
        case 30:
            size = 3110400;
        break;
        case 31:
            size = 6912000;
        break;
        case 32:
            size = 7864320;
        break;
        case 40:
            size = 12582912;
        break;
        case 41:
            size = 12582912;
        break;
        case 42:
            size = 13369344;
        break;
        case 50:
            size = 42393600;
        break;
        case 51:
            size = 70778880;
        break;
        default:
            break;
    }

    size /= pic_size;
    size = size + 1; // need one more buffer

    if (max_reference_frame_num > size)
        size = max_reference_frame_num;

    if (size > DECODE_BUFFER_NUM_MAX)
        size = DECODE_BUFFER_NUM_MAX;

    return size;
}

static void do_alloc_work(struct work_struct *work)
{
    int level_idc, max_reference_frame_num, mb_width, mb_height;
    int dpb_size, ref_size;
    int dpb_start_addr, ref_start_addr, max_dec_frame_buffering, total_dec_frame_buffering;
    int ret = READ_VREG(MAILBOX_COMMAND);

    ref_start_addr = decoder_buffer_start;
    ret = READ_VREG(MAILBOX_DATA_0);
    level_idc = (ret >> 24) & 0xff;
    max_reference_frame_num = (ret >> 16) & 0xff;
    mb_width = (ret >> 8) & 0xff;
    if (mb_width == 0) {
        mb_width = 256;
    }
    mb_height = (ret >> 0) & 0xff;

    max_dec_frame_buffering = get_max_dec_frame_buf_size(level_idc, max_reference_frame_num, mb_width, mb_height);

    total_dec_frame_buffering = max_dec_frame_buffering + DISPLAY_BUFFER_NUM;

    if ((frame_width == 0) || (frame_height == 0)) {
        frame_width = mb_width << 4;
        frame_height = mb_height << 4;
        frame_ar = frame_height * 0x100 / frame_width;
    }

    mb_width = (mb_width+3) & 0xfffffffc;
    mb_height = (mb_height+3) & 0xfffffffc;

    dpb_size = mb_width * mb_height * 384;
    ref_size = mb_width * mb_height * 96;
    dpb_start_addr = ref_start_addr + (ref_size * (max_reference_frame_num+1)) * 2;
    //dpb_start_addr = reserved_buffer + dpb_size;

    printk("dpb_start_addr=0x%x, dpb_size=%d, total_dec_frame_buffering=%d, mb_width=%d, mb_height=%d\n",
        dpb_start_addr, dpb_size, total_dec_frame_buffering, mb_width, mb_height);

    ret = init_canvas(dpb_start_addr, dpb_size,
                          total_dec_frame_buffering, mb_width, mb_height, buffer_spec);

    if (ret == -1) {
         printk(" Un-expected memory alloc problem\n");
         return;
    }

    if (frame_width == 0)
        frame_width = mb_width<<4;
    if (frame_height == 0)
        frame_height = mb_height<<4;

    WRITE_VREG(REF_START_VIEW_0, video_domain_addr(ref_start_addr));
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_REF_START_VIEW_0, video_domain_addr(ref_start_addr));
    }

    WRITE_VREG(MAILBOX_DATA_0, (max_dec_frame_buffering << 8) | (total_dec_frame_buffering << 0));
    WRITE_VREG(MAILBOX_DATA_1, ref_size);
    WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);

/////////////// FAKE FIRST PIC
#if 0
                
printk("Debug: send a fake picture to config VPP %dx%d\n", frame_width, frame_height); 
WRITE_VREG(DOS_SCRATCH0, 4);
WRITE_VREG(DOS_SCRATCH1, 0x004c);
                                                                                  
    if (kfifo_get(&newframe_q, &vf)) {                                            
        vfbuf_use[0]++;
        vf->index = 0;
        vf->pts = 0;
        vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
        vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[0]);         
        set_frame_info(vf);
        kfifo_put(&display_q, (const vframe_t **)&vf);                            
        vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY, NULL);    
    }                                                                             
        
/////////////// FAKE END   
#endif
}

static irqreturn_t vh264_4k2k_isr(int irq, void *dev_id)
{
    int drop_status, display_buff_id, display_POC;
    unsigned stream_offset;
    vframe_t *vf = NULL;
    int ret = READ_VREG(MAILBOX_COMMAND);

    switch(ret & 0xff) {
    case CMD_ALLOC_VIEW:
        schedule_work(&alloc_work);
        break;

    case CMD_FRAME_DISPLAY:
        ret >>= 8;
        display_buff_id = (ret >> 0) & 0x3f;
        drop_status = (ret >> 8) & 0x1;
        display_POC = READ_VREG(MAILBOX_DATA_0);
        stream_offset = READ_VREG(MAILBOX_DATA_1);

//printk("CMD_FRAME_DISPLAY, buffer_id = %d, drop_status = %d, POC = %d, offset = 0x%x, vb_level = 0x%x -- 0x%x\n",
//        display_buff_id, drop_status, display_POC, stream_offset, READ_VREG(VLD_MEM_VIFIFO_LEVEL), READ_VREG(VDEC2_VLD_MEM_VIFIFO_LEVEL));

        smp_rmb();

        WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);

        if (kfifo_get(&newframe_q, &vf) == 0) {
            printk("fatal error, no available buffer slot.");
            return IRQ_HANDLED;
        }
        
        if (vf) {
            vfbuf_use[display_buff_id]++;

            if (pts_lookup_offset(PTS_TYPE_VIDEO, stream_offset, &vf->pts, 0) != 0) {
                vf->pts = 0;
            }

            vf->index = display_buff_id;
            vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
            vf->type |= VIDTYPE_VIU_NV21;
            vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[display_buff_id]);
            set_frame_info(vf);

            kfifo_put(&display_q, (const vframe_t **)&vf);

            vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY, NULL);
        }
        break;

    case CMD_DEBUG:
        printk("M: core_status 0x%08x 0x%08x; ", READ_VREG(CORE_STATUS_M), READ_VREG(CORE_STATUS_S));
        switch (READ_VREG(MAILBOX_DATA_0)) {
            case 1:
                printk("H264_BUFFER_INFO_INDEX = 0x%x\n", READ_VREG(MAILBOX_DATA_1));
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 2:
                printk("H264_BUFFER_INFO_DATA = 0x%x\n", READ_VREG(MAILBOX_DATA_1));
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 3:
                printk("REC_CANVAS_ADDR = 0x%x\n", READ_VREG(MAILBOX_DATA_1));
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 4:
                printk("after DPB_MMCO\n");
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 5:
                printk("MBY = 0x%x, S_MBXY = 0x%x\n", READ_VREG(MAILBOX_DATA_1), READ_VREG(0x2c07));
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 6:
                printk("after FIFO_OUT_FRAME\n");
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 7:
                printk("after RELEASE_EXCEED_REF_BUFF\n");
                WRITE_VREG(MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 0x5a:
                printk("\n");
                break;
            default:
                printk("\n");
                break;
        }
        break;

    default:
        break;
    }

    return IRQ_HANDLED;
}

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
static irqreturn_t vh264_4k2k_vdec2_isr(int irq, void *dev_id)
{
    int ret = READ_VREG(VDEC2_MAILBOX_COMMAND);

    switch(ret & 0xff) {
    case CMD_DEBUG:
        printk("S: core_status 0x%08x 0x%08x; ", READ_VREG(CORE_STATUS_M), READ_VREG(CORE_STATUS_S));
        switch (READ_VREG(VDEC2_MAILBOX_DATA_0)) {
            case 1:
                printk("H264_BUFFER_INFO_INDEX = 0x%x\n", READ_VREG(VDEC2_MAILBOX_DATA_1));
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 2:
                printk("H264_BUFFER_INFO_DATA = 0x%x\n", READ_VREG(VDEC2_MAILBOX_DATA_1));
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 3:
                printk("REC_CANVAS_ADDR = 0x%x\n", READ_VREG(VDEC2_MAILBOX_DATA_1));
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 4:
                printk("after DPB_MMCO\n");
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 5:
                printk("MBY = 0x%x, M/S_MBXY = 0x%x-0x%x\n", READ_VREG(VDEC2_MAILBOX_DATA_1), READ_VREG(0xc07), READ_VREG(0x2c07));
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 6:
                printk("after FIFO_OUT_FRAME\n");
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 7:
                printk("after RELEASE_EXCEED_REF_BUFF\n");
                WRITE_VREG(VDEC2_MAILBOX_COMMAND, CMD_FINISHED);
                break;
            case 0x5a:
                printk("\n");
                break;
            default:
                printk("\n");
                break;
        }
        break;

    default:
        break;
    }

    return IRQ_HANDLED;
}
#endif

static void vh264_4k2k_put_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;
    receviver_start_e state = RECEIVER_INACTIVE;

    if (vf_get_receiver(PROVIDER_NAME)) {
        state = vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_QUREY_STATE, NULL);
        if ((state == RECEIVER_STATE_NULL)||(state == RECEIVER_STATE_NONE)){
            state = RECEIVER_INACTIVE;
        }
    } else {
        state = RECEIVER_INACTIVE;
    }

    // error watchdog
    if (((READ_VREG(VLD_MEM_VIFIFO_CONTROL) & 0x100) == 0) && // decoder has input
        (state == RECEIVER_INACTIVE) &&                       // receiver has no buffer to recycle
        (kfifo_is_empty(&display_q)) &&                       // no buffer in display queue
        (kfifo_is_empty(&recycle_q)) &&                       // no buffer to recycle
        (READ_VREG(MS_ID) & 0x100)
#ifdef CONFIG_H264_2K4K_SINGLE_CORE
        && (READ_VREG(VDEC2_MS_ID) & 0x100)                   // with both decoder have started decoding
#endif
       ) {
        if (++error_watchdog_count == ERROR_RESET_COUNT) {    // and it lasts for a while
            printk("H264 4k2k decoder fatal error watchdog.\n");
            fatal_error = DECODER_FATAL_ERROR_UNKNOW;
        }
    } else {
      error_watchdog_count = 0;
    }

    if (READ_VREG(FATAL_ERROR) != 0) {
        printk("H264 4k2k decoder ucode fatal error.\n");
        fatal_error = DECODER_FATAL_ERROR_UNKNOW;
        WRITE_VREG(FATAL_ERROR, 0);
    }

    while (!kfifo_is_empty(&recycle_q) &&
           (READ_VREG(BUFFER_RECYCLE) == 0)) {
        vframe_t *vf;
        if (kfifo_get(&recycle_q, &vf)) {
            if ((vf->index >= 0) && (--vfbuf_use[vf->index] == 0)) {
                WRITE_VREG(BUFFER_RECYCLE, vf->index + 1);
                vf->index = -1;
            }

            kfifo_put(&newframe_q, (const vframe_t **)&vf);
        }
    }

    timer->expires = jiffies + PUT_INTERVAL;

    add_timer(timer);
}

int vh264_4k2k_dec_status(struct vdec_status *vstatus)
{
    vstatus->width = frame_width;
    vstatus->height = frame_height;
    if (frame_dur != 0) {
        vstatus->fps = 96000 / frame_dur;
    } else {
        vstatus->fps = -1;
    }
    vstatus->error_count = 0;
    vstatus->status = stat | fatal_error;
    return 0;
}

int vh264_4k2k_set_trickmode(unsigned long trickmode)
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

static void H264_DECODE_INIT(void)
{
    int i;

    WRITE_VREG(GCLK_EN, 0x3ff);

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

    // fill_weight_pred
    WRITE_VREG(MC_MPORT_CTRL, 0x0300);
    for(i=0; i<192; i++) WRITE_VREG(MC_MPORT_DAT, 0x100);
    WRITE_VREG(MC_MPORT_CTRL, 0);

    WRITE_VREG(MB_WIDTH, 0xff); // invalid mb_width

    // set slice start to 0x000000 or 0x000001 for check more_rbsp_data
    WRITE_VREG(SLICE_START_BYTE_01, 0x00000000);
    WRITE_VREG(SLICE_START_BYTE_23, 0x01010000);

    WRITE_VREG(MPEG1_2_REG, 1); // set to mpeg2 to enable mismatch logic
    WRITE_VREG(VLD_ERROR_MASK, 0x1011); // disable COEF_GT_64 , error_m4_table and voff_rw_err

    // Config MCPU Amrisc interrupt
    WRITE_VREG(ASSIST_AMR1_INT0, 0x1 ); // viu_vsync_int
    WRITE_VREG(ASSIST_AMR1_INT1, 0x5 ); // mbox_isr
    WRITE_VREG(ASSIST_AMR1_INT2, 0x8 ); // vld_isr
    //WRITE_VREG(ASSIST_AMR1_INT3, 0x15); // vififo_empty
    WRITE_VREG(ASSIST_AMR1_INT4, 0xd ); // rv_ai_mb_finished_int
    WRITE_VREG(ASSIST_AMR1_INT7, 0x14); // dcac_dma_done
    WRITE_VREG(ASSIST_AMR1_INT8, 0x15); // vififo_empty

    // Config MCPU Amrisc interrupt
    WRITE_VREG(ASSIST_AMR1_INT5, 0x9 ); // MCPU interrupt
    WRITE_VREG(ASSIST_AMR1_INT6, 0x17); // CCPU interrupt

    WRITE_VREG(CPC_P, 0xc00); // CCPU Code will start from 0xc00
    WRITE_VREG(CINT_VEC_BASE, (0xc20>>5));
    WRITE_VREG(POWER_CTL_VLD, (1<<10) | // disable cabac_step_2
                              (1<<9)  | // viff_drop_flag_en
                              (1<<6));  // h264_000003_en
    WRITE_VREG(M4_CONTROL_REG, (1<<13));  // H264_DECODE_INFO - h264_en

    WRITE_VREG(CANVAS_START, AMVDEC_H264_4K2K_CANVAS_INDEX);
    WRITE_VREG(WORKSPACE_START, video_domain_addr(work_space_adr)); // Start Address of Workspace (UCODE, temp_data...)
    WRITE_VREG(SPS_STATUS, 0); // Clear all sequence parameter set available
    WRITE_VREG(PPS_STATUS, 0); // Clear all picture parameter set available
    WRITE_VREG(CURRENT_UCODE, 0xff); // Set current microcode to NULL
    WRITE_VREG(CURRENT_SPS_PPS, 0xffff); // Set current SPS/PPS to NULL
    WRITE_VREG(DECODE_STATUS, 1); // Set decode status to DECODE_START_HEADER
}

static void H264_DECODE2_INIT(void)
{
    int i;

    WRITE_VREG(VDEC2_GCLK_EN, 0x3ff);

    WRITE_VREG(DOS_SW_RESET2, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET2, 0);

    READ_VREG(DOS_SW_RESET2);
    READ_VREG(DOS_SW_RESET2);
    READ_VREG(DOS_SW_RESET2);

    WRITE_VREG(DOS_SW_RESET2, (1<<7) | (1<<6) | (1<<4));
    WRITE_VREG(DOS_SW_RESET2, 0);

    WRITE_VREG(DOS_SW_RESET2, (1<<9) | (1<<8));
    WRITE_VREG(DOS_SW_RESET2, 0);

    READ_VREG(DOS_SW_RESET2);
    READ_VREG(DOS_SW_RESET2);
    READ_VREG(DOS_SW_RESET2);

    // fill_weight_pred
    WRITE_VREG(VDEC2_MC_MPORT_CTRL, 0x0300);
    for(i=0; i<192; i++) WRITE_VREG(VDEC2_MC_MPORT_DAT, 0x100);
    WRITE_VREG(VDEC2_MC_MPORT_CTRL, 0);

    WRITE_VREG(VDEC2_MB_WIDTH, 0xff); // invalid mb_width

    // set slice start to 0x000000 or 0x000001 for check more_rbsp_data
    WRITE_VREG(VDEC2_SLICE_START_BYTE_01, 0x00000000);
    WRITE_VREG(VDEC2_SLICE_START_BYTE_23, 0x01010000);

    WRITE_VREG(VDEC2_MPEG1_2_REG, 1); // set to mpeg2 to enable mismatch logic
    WRITE_VREG(VDEC2_VLD_ERROR_MASK, 0x1011); // disable COEF_GT_64 , error_m4_table and voff_rw_err

    // Config MCPU Amrisc interrupt
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT0, 0x1 ); // viu_vsync_int
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT1, 0x5 ); // mbox_isr
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT2, 0x8 ); // vld_isr
    //WRITE_VREG(VDEC2_ASSIST_AMR1_INT3, 0x15); // vififo_empty
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT4, 0xd ); // rv_ai_mb_finished_int
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT7, 0x14); // dcac_dma_done
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT8, 0x15); // vififo_empty

    // Config MCPU Amrisc interrupt
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT5, 0x9 ); // MCPU interrupt
    WRITE_VREG(VDEC2_ASSIST_AMR1_INT6, 0x17); // CCPU interrupt

    WRITE_VREG(VDEC2_CPC_P, 0xc00); // CCPU Code will start from 0xc00
    WRITE_VREG(VDEC2_CINT_VEC_BASE, (0xc20>>5));
    WRITE_VREG(VDEC2_POWER_CTL_VLD, (1<<10) | // disable cabac_step_2
                                    (1<<9)  | // viff_drop_flag_en
                                    (1<<6));  // h264_000003_en
    WRITE_VREG(VDEC2_M4_CONTROL_REG, (1<<13));  // H264_DECODE_INFO - h264_en

    WRITE_VREG(VDEC2_CANVAS_START, AMVDEC_H264_4K2K_CANVAS_INDEX);
    WRITE_VREG(VDEC2_WORKSPACE_START, video_domain_addr(work_space_adr)); // Start Address of Workspace (UCODE, temp_data...)
    WRITE_VREG(VDEC2_SPS_STATUS, 0); // Clear all sequence parameter set available
    WRITE_VREG(VDEC2_PPS_STATUS, 0); // Clear all picture parameter set available
    WRITE_VREG(VDEC2_CURRENT_UCODE, 0xff); // Set current microcode to NULL
    WRITE_VREG(VDEC2_CURRENT_SPS_PPS, 0xffff); // Set current SPS/PPS to NULL
    WRITE_VREG(VDEC2_DECODE_STATUS, 1); // Set decode status to DECODE_START_HEADER
}

static void vh264_4k2k_prot_init(void)
{
    /* clear mailbox interrupt */
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_ASSIST_MBOX0_CLR_REG, 1);
    }
#endif
    WRITE_VREG(VDEC_ASSIST_MBOX1_CLR_REG, 1);

    /* enable mailbox interrupt */
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_ASSIST_MBOX0_MASK, 1);
    }
#endif
    WRITE_VREG(VDEC_ASSIST_MBOX1_MASK, 1);

    /* disable PSCALE for hardware sharing */
    WRITE_VREG(PSCALE_CTRL, 0);

    H264_DECODE_INIT();
    if (!H264_4K2K_SINGLE_CORE) {
        H264_DECODE2_INIT();
    }

    WRITE_VREG(DOS_SW_RESET0, (1<<11));
    WRITE_VREG(DOS_SW_RESET0, 0);

    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);
    READ_VREG(DOS_SW_RESET0);

    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(DOS_SW_RESET2, (1<<11));
        WRITE_VREG(DOS_SW_RESET2, 0);

        READ_VREG(DOS_SW_RESET2);
        READ_VREG(DOS_SW_RESET2);
        READ_VREG(DOS_SW_RESET2);
    }

    WRITE_VREG(MAILBOX_COMMAND, 0);
    WRITE_VREG(BUFFER_RECYCLE, 0);

    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_MAILBOX_COMMAND, 0);
        WRITE_VREG(VDEC2_BUFFER_RECYCLE, 0);
    }

    CLEAR_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
    if (!H264_4K2K_SINGLE_CORE) {
        CLEAR_VREG_MASK(VDEC2_MDEC_PIC_DC_CTRL, 1<<17);
    }

    /* set VDEC Master/ID 0 */
    WRITE_VREG(MS_ID, (1<<7)|(0<<0));
    if (!H264_4K2K_SINGLE_CORE) {
        /* set VDEC2 Slave/ID 0 */
        WRITE_VREG(VDEC2_MS_ID, (0<<7)|(1<<0));
    }
    WRITE_VREG(DECODE_SKIP_PICTURE, 0);
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_DECODE_SKIP_PICTURE, 0);
    }

    WRITE_VREG(PRE_MASTER_UPDATE_TIMES, 0);
    WRITE_VREG(SLAVE_WAIT_DPB_UPDATE, 0);
    WRITE_VREG(SLAVE_REF_DPB, 0);
    WRITE_VREG(SAVE_MVC_ENTENSION_0, 0);
    WRITE_VREG(SAVE_I_POC, 0);
    WRITE_VREG(CORE_STATUS_M, 0);
    WRITE_VREG(CORE_STATUS_S, 0);
    WRITE_VREG(SAVE_ref_status_view_0, 0);
    WRITE_VREG(SAVE_ref_status_view_1, 0);
    WRITE_VREG(ALLOC_INFO_0, 0);
    WRITE_VREG(ALLOC_INFO_1, 0);
    WRITE_VREG(FATAL_ERROR, 0);

    SET_VREG_MASK(MDEC_PIC_DC_CTRL, 1<<17);
    if (!H264_4K2K_SINGLE_CORE) {
        SET_VREG_MASK(VDEC2_MDEC_PIC_DC_CTRL, 1<<17);
    }

    WRITE_VREG(MDEC_PIC_DC_THRESH, 0x404038aa);
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_MDEC_PIC_DC_THRESH, 0x404038aa);
        amvenc_dos_top_reg_fix();
    }

#ifdef DOUBLE_WRITE
    WRITE_VREG(MDEC_DOUBLEW_CFG0, (0   << 31) | // half y address
                                  (1   << 30) | // 0:No Merge 1:Automatic Merge
                                  (0   << 28) | // Field Picture, 0x:no skip 10:top only 11:bottom only
                                  (0   << 27) | // Source from, 1:MCW 0:DBLK
                                  (0   << 24) | // Endian Control for Chroma
                                  (0   << 18) | // DMA ID
                                  (0   << 12) | // DMA Burst Number
                                  (0   << 11) | // DMA Urgent
                                  (0   << 10) | // 1:Round 0:Truncation
                                  (1   <<  9) | // Size by vertical,   0:original size 1: 1/2 shrunken size
                                  (1   <<  8) | // Size by horizontal, 0:original size 1: 1/2 shrunken size
                                  (0   <<  6) | // Pixel sel by vertical,   0x:1/2 10:up 11:down
                                  (0   <<  4) | // Pixel sel by horizontal, 0x:1/2 10:left 11:right
                                  (0   <<  1) | // Endian Control for Luma
                                  (1   <<  0)); // Double Write Enable
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_MDEC_DOUBLEW_CFG0, (0   << 31) | // half y address
                                      (1   << 30) | // 0:No Merge 1:Automatic Merge
                                      (0   << 28) | // Field Picture, 0x:no skip 10:top only 11:bottom only
                                      (0   << 27) | // Source from, 1:MCW 0:DBLK
                                      (0   << 24) | // Endian Control for Chroma
                                      (0   << 18) | // DMA ID
                                      (0   << 12) | // DMA Burst Number
                                      (0   << 11) | // DMA Urgent
                                      (0   << 10) | // 1:Round 0:Truncation
                                      (1   <<  9) | // Size by vertical,   0:original size 1: 1/2 shrunken size
                                      (1   <<  8) | // Size by horizontal, 0:original size 1: 1/2 shrunken size
                                      (0   <<  6) | // Pixel sel by vertical,   0x:1/2 10:up 11:down
                                      (0   <<  4) | // Pixel sel by horizontal, 0x:1/2 10:left 11:right
                                      (0   <<  1) | // Endian Control for Luma
                                      (1   <<  0)); // Double Write Enable
    }
#endif
}

static void vh264_4k2k_local_init(void)
{
    int i;

#ifdef DEBUG_PTS
    pts_missed = 0;
    pts_hit = 0;
#endif

    frame_width = vh264_4k2k_amstream_dec_info.width;
    frame_height = vh264_4k2k_amstream_dec_info.height;
    frame_dur = (vh264_4k2k_amstream_dec_info.rate == 0) ? 3600 : vh264_4k2k_amstream_dec_info.rate;
    if (frame_width && frame_height) {
        frame_ar = frame_height * 0x100 / frame_width;
    }
    error_watchdog_count = 0;

    printk("H264_4K2K: decinfo: %dx%d rate=%d\n", frame_width, frame_height, frame_dur);

    if(frame_dur == 0){
        frame_dur = 96000/24;
    }

    INIT_KFIFO(display_q);
    INIT_KFIFO(recycle_q);
    INIT_KFIFO(newframe_q);

    for (i=0; i<DECODE_BUFFER_NUM_MAX; i++) {
        vfbuf_use[i] = 0;
    }

    for (i=0; i<VF_POOL_SIZE; i++) {
        const vframe_t *vf = &vfpool[i];
        vfpool[i].index = -1;
        kfifo_put(&newframe_q, &vf);
    }

    reserved_buffer = 0;

    INIT_WORK(&alloc_work, do_alloc_work);

    return;
}

static s32 vh264_4k2k_init(void)
{
    void __iomem *p = ioremap_nocache(work_space_adr, DECODER_WORK_SPACE_SIZE);

    if (!p) {
        printk("\nvh264_4k2k_init: Cannot remap ucode swapping memory\n");
        return -ENOMEM;
    }

    printk("\nvh264_4k2k_init\n");

    init_timer(&recycle_timer);

    stat |= STAT_TIMER_INIT;

    vh264_4k2k_local_init();

    amvdec_enable();

    if (H264_4K2K_SINGLE_CORE) {
        if (amvdec_loadmc(vh264_4k2k_mc_single) < 0) {
            amvdec_disable();
            iounmap(p);
            return -EBUSY;
        }
    } else {
        if (amvdec_loadmc(vh264_4k2k_mc) < 0) {
            amvdec_disable();
            iounmap(p);
            return -EBUSY;
        }
    }

    if (!H264_4K2K_SINGLE_CORE) {
        amvdec2_enable();

        if (amvdec2_loadmc(vh264_4k2k_mc) < 0) {
            amvdec_disable();
            amvdec2_disable();
            iounmap(p);
            return -EBUSY;
        }
    }

    if (H264_4K2K_SINGLE_CORE) {
        memcpy(p,
               vh264_4k2k_header_mc_single, sizeof(vh264_4k2k_header_mc_single));

        memcpy((void *)((ulong)p + 0x1000),
               vh264_4k2k_mmco_mc_single, sizeof(vh264_4k2k_mmco_mc_single));

        memcpy((void *)((ulong)p + 0x3000),
               vh264_4k2k_slice_mc_single, sizeof(vh264_4k2k_slice_mc_single));
    } else {
        memcpy(p,
               vh264_4k2k_header_mc, sizeof(vh264_4k2k_header_mc));

        memcpy((void *)((ulong)p + 0x1000),
               vh264_4k2k_mmco_mc, sizeof(vh264_4k2k_mmco_mc));

        memcpy((void *)((ulong)p + 0x3000),
               vh264_4k2k_slice_mc, sizeof(vh264_4k2k_slice_mc));
    }

    iounmap(p);

    stat |= STAT_MC_LOAD;

    /* enable AMRISC side protocol */
    vh264_4k2k_prot_init();

    if (request_irq(INT_VDEC, vh264_4k2k_isr,
                    IRQF_SHARED, "vh264_4k2k-irq", (void *)vh264_4k2k_dec_id)) {
        printk("vh264_4k2k irq register error.\n");
        amvdec_disable();
        if (!H264_4K2K_SINGLE_CORE) {
            amvdec2_disable();
        }
        
        return -ENOENT;
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (!H264_4K2K_SINGLE_CORE) {
        if (request_irq(INT_VDEC2, vh264_4k2k_vdec2_isr,
                        IRQF_SHARED, "vh264_4k2k-vdec2-irq", (void *)vh264_4k2k_dec_id2)) {
            printk("vh264_4k2k irq register error.\n");
            free_irq(INT_VDEC, (void *)vh264_4k2k_dec_id);
            amvdec_disable();
            amvdec2_disable();
            return -ENOENT;
        }
    }
#endif

    stat |= STAT_ISR_REG;

    vf_provider_init(&vh264_4k2k_vf_prov, PROVIDER_NAME, &vh264_4k2k_vf_provider, NULL);
    vf_reg_provider(&vh264_4k2k_vf_prov);
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);

    stat |= STAT_VF_HOOK;

    recycle_timer.data = (ulong) & recycle_timer;
    recycle_timer.function = vh264_4k2k_put_timer_func;
    recycle_timer.expires = jiffies + PUT_INTERVAL;

    add_timer(&recycle_timer);

    stat |= STAT_TIMER_ARM;

    amvdec_start();
    if (!H264_4K2K_SINGLE_CORE) {
        amvdec2_start();
    }

    stat |= STAT_VDEC_RUN;

    set_vdec_func(&vh264_4k2k_dec_status);
    //set_trickmode_func(&vh264_4k2k_set_trickmode);

    return 0;
}

static int vh264_4k2k_stop(void)
{
    int i;
    u32 disp_addr = 0xffffffff;
    canvas_t cur_canvas;

    if (stat & STAT_VDEC_RUN) {
        amvdec_stop();
        if (!H264_4K2K_SINGLE_CORE) {
            amvdec2_stop();
        }
        stat &= ~STAT_VDEC_RUN;
    }

    if (stat & STAT_ISR_REG) {
        WRITE_VREG(VDEC_ASSIST_MBOX1_MASK, 0);
        if (!H264_4K2K_SINGLE_CORE) {
            WRITE_VREG(VDEC2_ASSIST_MBOX0_MASK, 0);
        }

        free_irq(INT_VDEC, (void *)vh264_4k2k_dec_id);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
        if (!H264_4K2K_SINGLE_CORE) {
            free_irq(INT_VDEC2, (void *)vh264_4k2k_dec_id2);
        }
#endif
        stat &= ~STAT_ISR_REG;
    }

    if (stat & STAT_TIMER_ARM) {
        del_timer_sync(&recycle_timer);
        stat &= ~STAT_TIMER_ARM;
    }

    if (stat & STAT_VF_HOOK) {
        vf_unreg_provider(&vh264_4k2k_vf_prov);
        stat &= ~STAT_VF_HOOK;
    }

#ifdef DOUBLE_WRITE
    WRITE_VREG(MDEC_DOUBLEW_CFG0, 0);
    if (!H264_4K2K_SINGLE_CORE) {
        WRITE_VREG(VDEC2_MDEC_DOUBLEW_CFG0, 0);
    }
#endif

    amvdec_disable();
    if (!H264_4K2K_SINGLE_CORE) {
        amvdec2_disable();
    }

    canvas_read((READ_VCBUS_REG(VD1_IF0_CANVAS0) & 0xff), &cur_canvas);
    disp_addr = cur_canvas.addr;

    for (i=0; i<ARRAY_SIZE(buffer_spec); i++) {
        if (buffer_spec[i].alloc_pages) {
            if (disp_addr == page_to_phys(buffer_spec[i].alloc_pages)) {
                printk("Skip releasing CMA buffer %d\n", i);
            } else {
                dma_release_from_contiguous(cma_dev, buffer_spec[i].alloc_pages, buffer_spec[i].alloc_count);
                buffer_spec[i].alloc_pages = NULL;
                buffer_spec[i].alloc_count = 0;
            }
        }

        if (buffer_spec[i].y_addr == disp_addr) {
            printk("4K2K decoder stop, keeping buffer index = %d\n", i);
        }
    }

    return 0;
}

#if (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)&&(HAS_HDEC)
extern void AbortEncodeWithVdec2(int abort);
#endif

static int amvdec_h264_4k2k_probe(struct platform_device *pdev)
{
    struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;

    printk("amvdec_h264_4k2k probe start.\n");

    mutex_lock(&vh264_4k2k_mutex);
    
    fatal_error = 0;

    if (pdata == NULL) {
        printk("\namvdec_h264_4k2k memory resource undefined.\n");
        mutex_unlock(&vh264_4k2k_mutex);
        return -EFAULT;
    }

    work_space_adr = pdata->mem_start;
    decoder_buffer_start = pdata->mem_start + DECODER_WORK_SPACE_SIZE;
    decoder_buffer_end = pdata->mem_end + 1;

    if (pdata->sys_info) {
        vh264_4k2k_amstream_dec_info = *pdata->sys_info;
    }
    cma_dev = pdata->cma_dev;

    printk("H.264 4k2k decoder mem resource 0x%x -- 0x%x\n", decoder_buffer_start, decoder_buffer_end);

    if (!H264_4K2K_SINGLE_CORE) {
#if (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)&&(HAS_HDEC)
        int count = 0;
        if(get_vdec2_usage() != USAGE_NONE)
            AbortEncodeWithVdec2(1);
        while((get_vdec2_usage() != USAGE_NONE)&&(count < 10)){
            msleep(50);
            count++;
        }
  
        if(get_vdec2_usage() != USAGE_NONE){
            printk("\namvdec_h264_4k2k --- vdec2 is used by encode now.\n");
            mutex_unlock(&vh264_4k2k_mutex);
            return -EBUSY;
        }
#endif

        if (vdec_on(VDEC_2)) {//++++
            vdec_poweroff(VDEC_2);//++++
            mdelay(10);
        }
#if (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)&&(HAS_HDEC)
        set_vdec2_usage(USAGE_DEC_4K2K);
        AbortEncodeWithVdec2(0);
#endif
        vdec_poweron(VDEC_2);
    }

    vdec_power_mode(1);
    if (!H264_4K2K_SINGLE_CORE) {
        vdec2_power_mode(1);
    }

    if (vh264_4k2k_init() < 0) {
        printk("\namvdec_h264_4k2k init failed.\n");
#if (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)&&(HAS_HDEC)
        if (!H264_4K2K_SINGLE_CORE) {
            set_vdec2_usage(USAGE_NONE);
            AbortEncodeWithVdec2(0);
        }
#endif
        mutex_unlock(&vh264_4k2k_mutex);
        return -ENODEV;
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    request_vpu_clk_vmod(360000000, VPU_VIU_VD1);
#endif

    if (probe_callback) {
        probe_callback();
    }

    mutex_unlock(&vh264_4k2k_mutex);
    return 0;
}

static int amvdec_h264_4k2k_remove(struct platform_device *pdev)
{
    cancel_work_sync(&alloc_work);

    mutex_lock(&vh264_4k2k_mutex);

    vh264_4k2k_stop();

    if (!H264_4K2K_SINGLE_CORE) {
        vdec_poweroff(VDEC_2);
#if (MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8)&&(HAS_HDEC)
        set_vdec2_usage(USAGE_NONE);
#endif
    }

#ifdef DEBUG_PTS
    printk("pts missed %ld, pts hit %ld, duration %d\n",
           pts_missed, pts_hit, frame_dur);
#endif

    if (remove_callback) {
        remove_callback();
    }

    mutex_unlock(&vh264_4k2k_mutex);
    
    printk("amvdec_h264_4k2k_remove\n");
    return 0;
}

void vh264_4k2k_register_module_callback(void(*enter_func)(void), void(*remove_func)(void))
{
    probe_callback = enter_func;
    remove_callback = remove_func;
}
EXPORT_SYMBOL(vh264_4k2k_register_module_callback);

/****************************************/

static struct platform_driver amvdec_h264_4k2k_driver = {
    .probe   = amvdec_h264_4k2k_probe,
    .remove  = amvdec_h264_4k2k_remove,
#ifdef CONFIG_PM
    .suspend = amvdec_suspend,
    .resume  = amvdec_resume,
#endif
    .driver  = {
        .name = DRIVER_NAME,
    }
};

static struct codec_profile_t amvdec_h264_4k2k_profile = {
    .name = "h264_4k2k",
    .profile = ""
};

static int __init amvdec_h264_4k2k_driver_init_module(void)
{
    printk("amvdec_h264_4k2k module init\n");

    if (platform_driver_register(&amvdec_h264_4k2k_driver)) {
        printk("failed to register amvdec_h264_4k2k driver\n");
        return -ENODEV;
    }

    vcodec_profile_register(&amvdec_h264_4k2k_profile);

    return 0;
}

static void __exit amvdec_h264_4k2k_driver_remove_module(void)
{
    printk("amvdec_h264_4k2k module remove.\n");

    platform_driver_unregister(&amvdec_h264_4k2k_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_h264_4k2k stat \n");

module_init(amvdec_h264_4k2k_driver_init_module);
module_exit(amvdec_h264_4k2k_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC h264_4k2k Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <tim.yao@amlogic.com>");


