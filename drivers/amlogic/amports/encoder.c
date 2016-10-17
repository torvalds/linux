/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 *
 * Author:  Simon Zheng <simon.zheng@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <mach/am_regs.h>
#include <mach/power_gate.h>
#include <mach/mod_gate.h>
#include <plat/io.h>
#include <linux/ctype.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/canvas.h>
#include "vdec_reg.h"
#include "vdec.h"
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/dma-contiguous.h>
#include <linux/kthread.h>
#include <linux/sched/rt.h>
#include "amports_config.h"
#include "encoder.h"
#include "amvdec.h"
#include "encoder_mc.h"
#include <linux/amlogic/amlog.h>

//device 
static int avc_device_major = 0;
static struct device *amvenc_avc_dev = NULL;
#define DRIVER_NAME "amvenc_avc"
#define CLASS_NAME "amvenc_avc"
#define DEVICE_NAME "amvenc_avc"

static encode_manager_t encode_manager;

#define MULTI_SLICE_MC
/*same as INIT_ENCODER*/
#define INTRA_IN_P_TOP

#define ENC_CANVAS_OFFSET  AMVENC_CANVAS_INDEX

#define UCODE_MODE_FULL 0
#define UCODE_MODE_SW_MIX 1

#ifdef USE_VDEC2
#define STREAM_WR_PTR               DOS_SCRATCH20
#define DECODABLE_MB_Y              DOS_SCRATCH21
#define DECODED_MB_Y                DOS_SCRATCH22
#endif

static u32 anc0_buffer_id =0;
static u32 ie_me_mb_type  = 0;
static u32 ie_me_mode  = 0;
static u32 ie_pippeline_block = 3;
static u32 ie_cur_ref_sel = 0;
static int avc_endian = 6;
static int clock_level = 1;
static int enable_dblk = 1;  // 0 disable, 1 vdec 2 hdec

static u32 encode_print_level = LOG_LEVEL_DEBUG;
static u32 no_timeout = 0;

static int me_mv_merge_ctl =
              ( 0x1 << 31)  |  // [31] me_merge_mv_en_16
              ( 0x1 << 30)  |  // [30] me_merge_small_mv_en_16
              ( 0x1 << 29)  |  // [29] me_merge_flex_en_16
              ( 0x1 << 28)  |  // [28] me_merge_sad_en_16
              ( 0x1 << 27)  |  // [27] me_merge_mv_en_8
              ( 0x1 << 26)  |  // [26] me_merge_small_mv_en_8
              ( 0x1 << 25)  |  // [25] me_merge_flex_en_8
              ( 0x1 << 24)  |  // [24] me_merge_sad_en_8
              ( 0x12 << 18)  |  // [23:18] me_merge_mv_diff_16 - MV diff <= n pixel can be merged
              ( 0x2b << 12)  |  // [17:12] me_merge_mv_diff_8 - MV diff <= n pixel can be merged
              ( 0x80 << 0);    // [11:0] me_merge_min_sad - SAD >= 0x180 can be merged with other MV
              //( 0x4 << 18)  |  // [23:18] me_merge_mv_diff_16 - MV diff <= n pixel can be merged
              //( 0x3f << 12)  |  // [17:12] me_merge_mv_diff_8 - MV diff <= n pixel can be merged
              //( 0xc0 << 0);    // [11:0] me_merge_min_sad - SAD >= 0x180 can be merged with other MV

static int me_mv_weight_01 = (0x40<<24)|(0x30<<16)|(0x20<<8)|0x30;
static int me_mv_weight_23 = (0x40<<8)|0x30;
static int me_sad_range_inc = 0x03030303;
static int me_step0_close_mv = 0x003ffc21;
static int me_f_skip_sad = 0;
static int me_f_skip_weight = 0;
static int me_sad_enough_01 = 0;//0x00018010;
static int me_sad_enough_23 = 0;//0x00000020;

static int p_intra_config =(30 << 16)|(0xffff << 0);

// [31:16] TARGET_BITS_PER_MB
// [15:8] MIN_QUANT
//  [7:0] MAX_QUANT
static int p_mb_quant_config = (20 << 16)|(24 << 8)|(24 << 0);

// [31:24] INC_4_BITS
// [23:16] INC_3_BITS
// [15:8]  INC_2_BITS
// [7:0]   INC_1_BITS
static int p_mb_quant_inc_cfg = (20 << 24)|(15 << 16)|(10 << 8)|(5 << 0);

// [31:24] DEC_4_BITS
// [23:16] DEC_3_BITS
// [15:8]  DEC_2_BITS
// [7:0]   DEC_1_BITS
static int p_mb_quant_dec_cfg =(60 << 24)|(40 << 16)|(30 << 8)|(20 << 0);

// [31:0] NUM_ROWS_PER_SLICE_P
// [15:0] NUM_ROWS_PER_SLICE_I
static int fixed_slice_cfg = 0;

static DEFINE_SPINLOCK(lock);

static BuffInfo_t amvenc_buffspec[]={
    {
        .lev_id      = AMVENC_BUFFER_LEVEL_480P,
        .max_width = 640,
        .max_height = 480,
        .min_buffsize = 0x580000,
        .dct = {
            .buf_start = 0,
            .buf_size = 0xfe000,
        },
        .dec0_y = {
            .buf_start = 0x100000,
            .buf_size = 0x80000,
        },
        .dec1_y = {
            .buf_start = 0x180000,
            .buf_size = 0x80000,
        },
        .assit = {
            .buf_start = 0x240000,
            .buf_size = 0xc0000,
        },
        .bitstream = {
            .buf_start = 0x300000,
            .buf_size = 0x100000,
        },
        .inter_bits_info = {
            .buf_start = 0x400000,
            .buf_size = 0x2000,
        },
        .inter_mv_info = {
            .buf_start = 0x402000,
            .buf_size = 0x13000,
        },
        .intra_bits_info = {
            .buf_start = 0x420000,
            .buf_size = 0x2000,
        },
        .intra_pred_info = {
            .buf_start = 0x422000,
            .buf_size = 0x13000,
        },
        .qp_info = {
            .buf_start = 0x438000,
            .buf_size = 0x8000,
        }
#ifdef USE_VDEC2
        ,
        .vdec2_info = {
            .buf_start = 0x440000,
            .buf_size = 0x13e000,
        }
#endif
    },{
        .lev_id      = AMVENC_BUFFER_LEVEL_720P,
        .max_width = 1280,
        .max_height = 720,
        .min_buffsize = 0x9e0000,
        .dct = {
            .buf_start = 0,
            .buf_size = 0x2f8000,
        },
        .dec0_y = {
            .buf_start = 0x300000,
            .buf_size = 0x180000,
        },
        .dec1_y = {
            .buf_start = 0x480000,
            .buf_size = 0x180000,
        },
        .assit = {
            .buf_start = 0x640000,
            .buf_size = 0xc0000,
        },
        .bitstream = {
            .buf_start = 0x700000,
            .buf_size = 0x100000,
        },
        .inter_bits_info = {
            .buf_start = 0x800000,
            .buf_size = 0x4000,
        },
        .inter_mv_info = {
            .buf_start = 0x804000,
            .buf_size = 0x40000,
        },
        .intra_bits_info = {
            .buf_start = 0x848000,
            .buf_size = 0x4000,
        },
        .intra_pred_info = {
            .buf_start = 0x84c000,
            .buf_size = 0x40000,
        },
        .qp_info = {
            .buf_start = 0x890000,
            .buf_size = 0x8000,
        }
#ifdef USE_VDEC2
        ,
        .vdec2_info = {
            .buf_start = 0x8a0000,
            .buf_size = 0x13e000,
        }
#endif
    },{
        .lev_id      = AMVENC_BUFFER_LEVEL_1080P,
        .max_width = 1920,
        .max_height = 1088,
        .min_buffsize = 0x1160000,
        .dct = {
            .buf_start = 0,
            .buf_size = 0x6ba000,
        },
        .dec0_y = {
            .buf_start = 0x6d0000,
            .buf_size = 0x300000,
        },
        .dec1_y = {
            .buf_start = 0x9d0000,
            .buf_size = 0x300000,
        },
        .assit = {
            .buf_start = 0xd10000,
            .buf_size = 0xc0000,
        },
        .bitstream = {
            .buf_start = 0xe00000,
            .buf_size = 0x100000,
        },
        .inter_bits_info = {
            .buf_start = 0xf00000,
            .buf_size = 0x8000,
        },
        .inter_mv_info = {
            .buf_start = 0xf08000,
            .buf_size = 0x80000,
        },
        .intra_bits_info = {
            .buf_start = 0xf88000,
            .buf_size = 0x8000,
        },
        .intra_pred_info = {
            .buf_start = 0xf90000,
            .buf_size = 0x80000,
        },
        .qp_info = {
            .buf_start = 0x1010000,
            .buf_size = 0x8000,
        }
#ifdef USE_VDEC2
        ,
        .vdec2_info = {
            .buf_start = 0x1020000,
            .buf_size = 0x13e000,
        }
#endif
    }
};

#ifdef CONFIG_AM_JPEG_ENCODER
extern bool jpegenc_on(void);
#endif

static void dma_flush(unsigned buf_start , unsigned buf_size );

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESONG9TV
static const u32 *select_ucode(u32 ucode_index)
{
    const u32 * p = mix_dump_mc;
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(enable_dblk)
                p = mix_dump_mc_m2_dblk;
            else
                p = mix_dump_mc;
            break;
        case UCODE_MODE_SW_MIX:
            if(enable_dblk)
                p = mix_sw_mc_hdec_m2_dblk;
            else
                p = mix_sw_mc;
            break;
        default:
            break;
    }

    encode_manager.dblk_fix_flag = (p==mix_sw_mc_hdec_m2_dblk);
    return p;
}

#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8B
static const u32 *select_ucode(u32 ucode_index)
{
    const u32 * p = mix_dump_mc;
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(enable_dblk)
                p = mix_dump_mc_dblk;
            else
                p = mix_dump_mc;
            break;
        case UCODE_MODE_SW_MIX:
            if(enable_dblk)
                p = mix_sw_mc_hdec_dblk;
            else
                p = mix_sw_mc;
            break;
        default:
            break;
    }
    encode_manager.dblk_fix_flag = false;
    return p;
}

#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
static const u32 *select_ucode(u32 ucode_index)
{
    const u32 * p = mix_dump_mc;
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(IS_MESON_M8M2_CPU){
                if(enable_dblk)
                    p = mix_dump_mc_m2_dblk;
                else
                    p = mix_dump_mc;
            }else{
                if(enable_dblk)
                    p = mix_dump_mc_dblk;
                else
                    p = mix_dump_mc;
            }
            break;
        case UCODE_MODE_SW_MIX:
            if(IS_MESON_M8M2_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_m2_dblk;
                else
                    p = mix_sw_mc;
            }else{
                if(enable_dblk == 1)
                    p = mix_sw_mc_vdec2_dblk;
                else if (enable_dblk ==2)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }
            break;
        default:
            break;
    }

    if(IS_MESON_M8M2_CPU){
        encode_manager.dblk_fix_flag = (p==mix_sw_mc_hdec_m2_dblk);
    }else{
        encode_manager.dblk_fix_flag = false;
    }
    return p;
}

#else
static const u32 *select_ucode(u32 ucode_index)
{
    const u32 * p = mix_dump_mc;
    encode_manager.dblk_fix_flag = false;
    return p;
}
#endif

/*output stream buffer setting*/
static void avc_init_output_buffer(encode_wq_t* wq)
{
    WRITE_HREG(VLC_VB_MEM_CTL ,((1<<31)|(0x3f<<24)|(0x20<<16)|(2<<0)) );
    WRITE_HREG(VLC_VB_START_PTR, wq->mem.BitstreamStart);
    WRITE_HREG(VLC_VB_WR_PTR, wq->mem.BitstreamStart);
    WRITE_HREG(VLC_VB_SW_RD_PTR, wq->mem.BitstreamStart);
    WRITE_HREG(VLC_VB_END_PTR, wq->mem.BitstreamEnd);
    WRITE_HREG(VLC_VB_CONTROL, 1);
    WRITE_HREG(VLC_VB_CONTROL, ((0<<14)|(7<<3)|(1<<1)|(0<<0)));
}

/*input dct buffer setting*/
static void avc_init_input_buffer(encode_wq_t* wq)
{
    WRITE_HREG(QDCT_MB_START_PTR ,wq->mem.dct_buff_start_addr );
    WRITE_HREG(QDCT_MB_END_PTR, wq->mem.dct_buff_end_addr);
    WRITE_HREG(QDCT_MB_WR_PTR, wq->mem.dct_buff_start_addr);
    WRITE_HREG(QDCT_MB_RD_PTR, wq->mem.dct_buff_start_addr);
    WRITE_HREG(QDCT_MB_BUFF, 0);
}

/*input reference buffer setting*/
static void avc_init_reference_buffer(int canvas)
{
    WRITE_HREG(HCODEC_ANC0_CANVAS_ADDR ,canvas);
    WRITE_HREG(VLC_HCMD_CONFIG ,0);
}

static void avc_init_assit_buffer(encode_wq_t* wq)
{
    WRITE_HREG(MEM_OFFSET_REG,wq->mem.assit_buffer_offset);  //memory offset ?
}

/*deblock buffer setting, same as INI_CANVAS*/
static void avc_init_dblk_buffer(int canvas)
{
    WRITE_HREG(HCODEC_REC_CANVAS_ADDR,canvas);
    WRITE_HREG(HCODEC_DBKR_CANVAS_ADDR,canvas);
    WRITE_HREG(HCODEC_DBKW_CANVAS_ADDR,canvas);
}

static void avc_init_encoder(encode_wq_t* wq, bool idr)
{
    WRITE_HREG(VLC_TOTAL_BYTES, 0);
    WRITE_HREG(VLC_CONFIG, 0x07);
    WRITE_HREG(VLC_INT_CONTROL, 0);
    WRITE_HREG(HCODEC_ASSIST_AMR1_INT0, 0x15);
#ifdef MULTI_SLICE_MC
    if(encode_manager.dblk_fix_flag){
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT2, 0x19);
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT1, 0x8);
    }else{
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT1, 0x19);
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT2, 0x8);
    }
#else
    if(encode_manager.dblk_fix_flag)
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT1, 0x8);
    else
        WRITE_HREG(HCODEC_ASSIST_AMR1_INT2, 0x8);
#endif
    WRITE_HREG(HCODEC_ASSIST_AMR1_INT3, 0x14);
#ifdef INTRA_IN_P_TOP
    WRITE_HREG(HCODEC_ASSIST_DMA_INT_MSK, 0xfd);
    WRITE_HREG(HCODEC_ASSIST_DMA_INT_MSK2, 0xff);
    WRITE_HREG(HCODEC_ASSIST_AMR1_INT4, 0x18);
    //mtspi   0xfd, HCODEC_ASSIST_DMA_INT_MSK // enable lmem_mpeg_dma_int
    //mtspi   0xff, HCODEC_ASSIST_DMA_INT_MSK2 // disable  cpu19_int
    //mtspi   0x18, HCODEC_ASSIST_AMR1_INT4   // lmem_dma_isr
#else
    WRITE_HREG(HCODEC_ASSIST_DMA_INT_MSK, 0xff);
    WRITE_HREG(HCODEC_ASSIST_DMA_INT_MSK2, 0xff);
#endif
    WRITE_HREG(IDR_PIC_ID ,wq->pic.idr_pic_id);
    WRITE_HREG(FRAME_NUMBER ,(idr== true)?0:wq->pic.frame_number);
    WRITE_HREG(PIC_ORDER_CNT_LSB,(idr== true)?0:wq->pic.pic_order_cnt_lsb);
    WRITE_HREG(LOG2_MAX_PIC_ORDER_CNT_LSB ,  wq->pic.log2_max_pic_order_cnt_lsb);
    WRITE_HREG(LOG2_MAX_FRAME_NUM , wq->pic.log2_max_frame_num);
    WRITE_HREG(ANC0_BUFFER_ID, anc0_buffer_id);
    WRITE_HREG(QPPICTURE, wq->pic.init_qppicture);
}

/****************************************/
static void avc_canvas_init(encode_wq_t* wq)
{
    u32 canvas_width, canvas_height;
    int start_addr = wq->mem.buf_start;

    canvas_width = ((wq->pic.encoder_width+31)>>5)<<5;
    canvas_height = ((wq->pic.encoder_height+15)>>4)<<4;

    canvas_config(ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec0_y.buf_start,
        canvas_width, canvas_height,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    canvas_config(1 + ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec0_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    /*here the third plane use the same address as the second plane*/
    canvas_config(2 + ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec0_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

    canvas_config(3 + ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec1_y.buf_start,
        canvas_width, canvas_height,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    canvas_config(4 + ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec1_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    /*here the third plane use the same address as the second plane*/
    canvas_config(5 + ENC_CANVAS_OFFSET,
        start_addr + wq->mem.bufspec.dec1_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

    //wq->mem.dblk_buf_canvas = ((ENC_CANVAS_OFFSET+2) <<16)|((ENC_CANVAS_OFFSET + 1) <<8)|(ENC_CANVAS_OFFSET);
    //wq->mem.ref_buf_canvas = ((ENC_CANVAS_OFFSET +5) <<16)|((ENC_CANVAS_OFFSET + 4) <<8)|(ENC_CANVAS_OFFSET +3);
    //encode_debug_level(LOG_LEVEL_INFO,"dblk_buf_canvas is 0x%x ; ref_buf_canvas is 0x%x, wq: %p.\n",wq->mem.dblk_buf_canvas , wq->mem.ref_buf_canvas,(void*)wq);
}

static void avc_buffspec_init(encode_wq_t* wq)
{
    u32 canvas_width, canvas_height;
    int start_addr = wq->mem.buf_start;

    canvas_width = ((wq->pic.encoder_width+31)>>5)<<5;
    canvas_height = ((wq->pic.encoder_height+15)>>4)<<4;

    /*input dct buffer config */
    wq->mem.dct_buff_start_addr = start_addr+wq->mem.bufspec.dct.buf_start;   //(w>>4)*(h>>4)*864
    wq->mem.dct_buff_end_addr = wq->mem.dct_buff_start_addr + wq->mem.bufspec.dct.buf_size -1 ;
    encode_debug_level(LOG_LEVEL_INFO,"dct_buff_start_addr is 0x%x, wq:%p.\n",wq->mem.dct_buff_start_addr,(void*)wq);

    wq->mem.bufspec.dec0_uv.buf_start = wq->mem.bufspec.dec0_y.buf_start+canvas_width*canvas_height;
    wq->mem.bufspec.dec0_uv.buf_size = canvas_width*canvas_height/2;
    wq->mem.bufspec.dec1_uv.buf_start = wq->mem.bufspec.dec1_y.buf_start+canvas_width*canvas_height;
    wq->mem.bufspec.dec1_uv.buf_size = canvas_width*canvas_height/2;
    wq->mem.assit_buffer_offset = start_addr + wq->mem.bufspec.assit.buf_start;
    encode_debug_level(LOG_LEVEL_INFO,"assit_buffer_offset is 0x%x, wq: %p.\n",wq->mem.assit_buffer_offset,(void*)wq);
    /*output stream buffer config*/
    wq->mem.BitstreamStart  = start_addr + wq->mem.bufspec.bitstream.buf_start;
    wq->mem.BitstreamEnd  =  wq->mem.BitstreamStart + wq->mem.bufspec.bitstream.buf_size -1;

    encode_debug_level(LOG_LEVEL_INFO,"BitstreamStart is 0x%x, wq: %p. \n",wq->mem.BitstreamStart,(void*)wq);

    wq->mem.dblk_buf_canvas = ((ENC_CANVAS_OFFSET+2) <<16)|((ENC_CANVAS_OFFSET + 1) <<8)|(ENC_CANVAS_OFFSET);
    wq->mem.ref_buf_canvas = ((ENC_CANVAS_OFFSET +5) <<16)|((ENC_CANVAS_OFFSET + 4) <<8)|(ENC_CANVAS_OFFSET +3);
    //encode_debug_level(LOG_LEVEL_INFO,"dblk_buf_canvas is %d ; ref_buf_canvas is %d, wq: %p.\n",wq->mem.dblk_buf_canvas , wq->mem.ref_buf_canvas,(void*)wq);
}

#ifdef USE_VDEC2
static int abort_vdec2_flag = 0;
void AbortEncodeWithVdec2(int abort)
{
    abort_vdec2_flag = abort;
}
#endif

static void avc_init_ie_me_parameter(encode_wq_t* wq, int quant)
{
    ie_pippeline_block = 3;

    WRITE_HREG(IE_ME_MB_TYPE,ie_me_mb_type);

    if(ie_pippeline_block == 3){
        ie_cur_ref_sel = ((1<<13) |(1<<12) |(1<<9) |(1<<8));
    }else if(ie_pippeline_block == 0){
        ie_cur_ref_sel = 0xffffffff;
    }else{
        encode_debug_level(LOG_LEVEL_ERROR,"Error : Please calculate IE_CUR_REF_SEL for IE_PIPPELINE_BLOCK. wq:%p \n", (void*)wq);
    }
    ie_me_mode |= (ie_pippeline_block&IE_PIPPELINE_BLOCK_MASK)<<IE_PIPPELINE_BLOCK_SHIFT; // currently disable half and sub pixel
    WRITE_HREG(IE_ME_MODE,ie_me_mode);
    WRITE_HREG(IE_REF_SEL,ie_cur_ref_sel);

    if(me_mv_merge_ctl)
        WRITE_HREG(ME_MV_MERGE_CTL, me_mv_merge_ctl);
    if(me_step0_close_mv)
        WRITE_HREG(ME_STEP0_CLOSE_MV,me_step0_close_mv);
    if(me_f_skip_sad)
        WRITE_HREG(ME_F_SKIP_SAD,me_f_skip_sad);
    if(me_f_skip_weight)
        WRITE_HREG(ME_F_SKIP_WEIGHT,me_f_skip_weight);

    if(me_mv_weight_01)
        WRITE_HREG(ME_MV_WEIGHT_01, me_mv_weight_01);

    if(me_mv_weight_23)
        WRITE_HREG(ME_MV_WEIGHT_23, me_mv_weight_23);

    if(me_sad_range_inc)
        WRITE_HREG(ME_SAD_RANGE_INC, me_sad_range_inc);

    if(fixed_slice_cfg){
        WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
    }else if(wq->pic.rows_per_slice !=  (wq->pic.encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (wq->pic.rows_per_slice<<16)|wq->pic.rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
    WRITE_HREG(P_INTRA_CONFIG, p_intra_config);
    WRITE_HREG(P_MB_QUANT_CONFIG, p_mb_quant_config);
    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        p_mb_quant_config = ( 20 << 16)  |
              ( quant<< 8)  |
              ( quant << 0);
        WRITE_HREG(P_MB_QUANT_CONFIG, p_mb_quant_config);
    }
    WRITE_HREG(P_MB_QUANT_INC_CFG, p_mb_quant_inc_cfg);
    WRITE_HREG(P_MB_QUANT_DEC_CFG, p_mb_quant_dec_cfg);
}

static void mfdin_basic (unsigned input, unsigned char iformat, unsigned char oformat, unsigned picsize_x, unsigned picsize_y, unsigned char r2y_en)
{
    unsigned char dsample_en; // Downsample Enable
    unsigned char interp_en;  // Interpolation Enable
    unsigned char y_size;     // 0:16 Pixels for y direction pickup; 1:8 pixels
    unsigned char r2y_mode;   // RGB2YUV Mode, range(0~3)
    unsigned char canv_idx0_bppx; // mfdin_reg3_canv[25:24];  // bytes per pixel in x direction for index0, 0:half 1:1 2:2 3:3
    unsigned char canv_idx1_bppx; // mfdin_reg3_canv[27:26];  // bytes per pixel in x direction for index1-2, 0:half 1:1 2:2 3:3
    unsigned char canv_idx0_bppy; // mfdin_reg3_canv[29:28];  // bytes per pixel in y direction for index0, 0:half 1:1 2:2 3:3
    unsigned char canv_idx1_bppy; // mfdin_reg3_canv[31:30];  // bytes per pixel in y direction for index1-2, 0:half 1:1 2:2 3:3
    unsigned char ifmt444,ifmt422,ifmt420,linear_bytes4p;
    unsigned linear_bytesperline;
    bool linear_enable = false;

    ifmt444 = ((iformat==1) || (iformat==5) || (iformat==8) || (iformat==9) || (iformat==12)) ? 1 : 0;
    ifmt422 = ((iformat==0) || (iformat==10)) ? 1 : 0;
    ifmt420 = ((iformat==2) || (iformat==3) || (iformat==4) || (iformat==11)) ? 1 : 0;
    dsample_en = ((ifmt444 && (oformat!=2)) || (ifmt422 && (oformat==0))) ? 1 : 0;
    interp_en = ((ifmt422 && (oformat==2)) || (ifmt420 && (oformat!=0))) ? 1 : 0;
    y_size = (oformat!=0) ? 1 : 0;
    r2y_mode = (r2y_en == 1)?1:0; // Fixed to 1 (TODO)
    canv_idx0_bppx = (iformat==1) ? 3 : (iformat==0) ? 2 : 1;
    canv_idx1_bppx = (iformat==4) ? 0 : 1;
    canv_idx0_bppy = 1;
    canv_idx1_bppy = (iformat==5) ? 1 : 0;
    if((iformat==8) || (iformat==9) || (iformat==12)){
        linear_bytes4p = 3;
    }else if(iformat == 10){
        linear_bytes4p = 2;
    }else if(iformat==11){
        linear_bytes4p = 1;
    }else{
        linear_bytes4p = 0;
    }
    linear_bytesperline = picsize_x*linear_bytes4p;

    if(iformat<8)
        linear_enable = false;
    else
        linear_enable = true;

    WRITE_HREG(HCODEC_MFDIN_REG1_CTRL,
            (iformat << 0) |(oformat << 4) |
            (dsample_en <<6) |(y_size <<8) |
            (interp_en <<9) |(r2y_en <<12) |
            (r2y_mode <<13));

    if(linear_enable == false){
        WRITE_HREG(HCODEC_MFDIN_REG3_CANV,
            (input & 0xffffff)|
            (canv_idx1_bppy <<30) |
            (canv_idx0_bppy <<28) |
            (canv_idx1_bppx <<26) |
            (canv_idx0_bppx <<24));
        WRITE_HREG(HCODEC_MFDIN_REG4_LNR0, (0 <<16) |(0 <<0));
        WRITE_HREG(HCODEC_MFDIN_REG5_LNR1, 0);
    }else{
        WRITE_HREG(HCODEC_MFDIN_REG3_CANV,
            (canv_idx1_bppy <<30) |
            (canv_idx0_bppy <<28) |
            (canv_idx1_bppx <<26) |
            (canv_idx0_bppx <<24));
        WRITE_HREG(HCODEC_MFDIN_REG4_LNR0, (linear_bytes4p <<16) |(linear_bytesperline <<0));
        WRITE_HREG(HCODEC_MFDIN_REG5_LNR1, input);
    }

    WRITE_HREG(HCODEC_MFDIN_REG8_DMBL,(picsize_x << 12) |(picsize_y << 0));
    WRITE_HREG(HCODEC_MFDIN_REG9_ENDN,(7<<0)| (6<<3)|( 5<<6)|(4<<9) |(3<<12) |(2<<15) |( 1<<18) |(0<<21));
}

static int  set_input_format (encode_wq_t* wq, encode_request_t* request)
{
    int ret = 0;
    unsigned char iformat = MAX_FRAME_FMT, oformat = MAX_FRAME_FMT, r2y_en = 0;
    unsigned picsize_x, picsize_y;
    unsigned canvas_w = 0;
    unsigned input = request->src;

    if((request->fmt == FMT_RGB565)||(request->fmt>=MAX_FRAME_FMT))
        return -1;

    picsize_x = ((wq->pic.encoder_width+15)>>4)<<4;
    picsize_y = ((wq->pic.encoder_height+15)>>4)<<4;
    oformat = 0;
    if((request->type == LOCAL_BUFF)||(request->type == PHYSICAL_BUFF)){
        if(request->type == LOCAL_BUFF){
            if(request->flush_flag&AMVENC_FLUSH_FLAG_INPUT)
                dma_flush(wq->mem.dct_buff_start_addr, request->framesize);
            input = wq->mem.dct_buff_start_addr;
        }
        if(request->fmt <= FMT_YUV444_PLANE)
            r2y_en = 0;
        else
            r2y_en = 1;

        if(request->fmt == FMT_YUV422_SINGLE){
            iformat = 10;
        }else if((request->fmt == FMT_YUV444_SINGLE)||(request->fmt== FMT_RGB888)){
            iformat = 1;
            if(request->fmt == FMT_RGB888)
                r2y_en = 1;
            canvas_w =  picsize_x*3;
            canvas_w =  ((canvas_w+31)>>5)<<5;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ENC_CANVAS_OFFSET+6;
        }else if((request->fmt == FMT_NV21)||(request->fmt == FMT_NV12)){
            canvas_w =  ((wq->pic.encoder_width+31)>>5)<<5;
            iformat = (request->fmt == FMT_NV21)?2:3;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+7,
                input + canvas_w*picsize_y,
                canvas_w , picsize_y/2,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ((ENC_CANVAS_OFFSET+7)<<8)|(ENC_CANVAS_OFFSET+6);
        }else if(request->fmt == FMT_YUV420){
            iformat = 4;
            canvas_w =  ((wq->pic.encoder_width+63)>>6)<<6;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+7,
                input + canvas_w*picsize_y,
                canvas_w/2, picsize_y/2,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+8,
                input + canvas_w*picsize_y*5/4,
                canvas_w/2 , picsize_y/2,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ((ENC_CANVAS_OFFSET+8)<<16)|((ENC_CANVAS_OFFSET+7)<<8)|(ENC_CANVAS_OFFSET+6);
        }else if((request->fmt == FMT_YUV444_PLANE)||(request->fmt == FMT_RGB888_PLANE)){
            if(request->fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
            iformat = 5;
            canvas_w =  ((wq->pic.encoder_width+31)>>5)<<5;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+7,
                input + canvas_w*picsize_y,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+8,
                input + canvas_w*picsize_y*2,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ((ENC_CANVAS_OFFSET+8)<<16)|((ENC_CANVAS_OFFSET+7)<<8)|(ENC_CANVAS_OFFSET+6);
        }else if(request->fmt == FMT_RGBA8888){
            iformat = 12;
        }
        ret = 0;
    }else if(request->type == CANVAS_BUFF){
        r2y_en = 0;
        if(request->fmt == FMT_YUV422_SINGLE){
            iformat = 0;
            input = input&0xff;
        }else if(request->fmt == FMT_YUV444_SINGLE){
            iformat = 1;
            input = input&0xff;
        }else if((request->fmt == FMT_NV21)||(request->fmt == FMT_NV12)){
            iformat = (request->fmt == FMT_NV21)?2:3;
            input = input&0xffff;
        }else if(request->fmt == FMT_YUV420){
            iformat = 4;
            input = input&0xffffff;
        }else if((request->fmt == FMT_YUV444_PLANE)||(request->fmt == FMT_RGB888_PLANE)){
            if(request->fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
            iformat = 5;
            input = input&0xffffff;
        }else{
            ret = -1;
        }
    }
    if(ret == 0)
        mfdin_basic(input,iformat,oformat,picsize_x,picsize_y,r2y_en);
    wq->control.finish = true;
    return ret;
}

static void avc_prot_init(encode_wq_t*wq, int quant,bool IDR)
{
    unsigned int data32;
    int pic_width, pic_height;
    int pic_mb_nr;
    int pic_mbx, pic_mby;
    int i_pic_qp, p_pic_qp;
    int i_pic_qp_c, p_pic_qp_c;
    pic_width  = wq->pic.encoder_width;
    pic_height = wq->pic.encoder_height;
    pic_mb_nr  = 0;
    pic_mbx    = 0;
    pic_mby    = 0;
    i_pic_qp   = quant;
    p_pic_qp   = quant;
    WRITE_HREG(VLC_PIC_SIZE, pic_width | (pic_height<<16));
    WRITE_HREG(VLC_PIC_POSITION, (pic_mb_nr<<16) | (pic_mby << 8) |  (pic_mbx <<0));	//start mb

    switch (i_pic_qp) {    // synopsys parallel_case full_case
        case 0 : i_pic_qp_c = 0; break;
        case 1 : i_pic_qp_c = 1; break;
        case 2 : i_pic_qp_c = 2; break;
        case 3 : i_pic_qp_c = 3; break;
        case 4 : i_pic_qp_c = 4; break;
        case 5 : i_pic_qp_c = 5; break;
        case 6 : i_pic_qp_c = 6; break;
        case 7 : i_pic_qp_c = 7; break;
        case 8 : i_pic_qp_c = 8; break;
        case 9 : i_pic_qp_c = 9; break;
        case 10 : i_pic_qp_c = 10; break;
        case 11 : i_pic_qp_c = 11; break;
        case 12 : i_pic_qp_c = 12; break;
        case 13 : i_pic_qp_c = 13; break;
        case 14 : i_pic_qp_c = 14; break;
        case 15 : i_pic_qp_c = 15; break;
        case 16 : i_pic_qp_c = 16; break;
        case 17 : i_pic_qp_c = 17; break;
        case 18 : i_pic_qp_c = 18; break;
        case 19 : i_pic_qp_c = 19; break;
        case 20 : i_pic_qp_c = 20; break;
        case 21 : i_pic_qp_c = 21; break;
        case 22 : i_pic_qp_c = 22; break;
        case 23 : i_pic_qp_c = 23; break;
        case 24 : i_pic_qp_c = 24; break;
        case 25 : i_pic_qp_c = 25; break;
        case 26 : i_pic_qp_c = 26; break;
        case 27 : i_pic_qp_c = 27; break;
        case 28 : i_pic_qp_c = 28; break;
        case 29 : i_pic_qp_c = 29; break;
        case 30 : i_pic_qp_c = 29; break;
        case 31 : i_pic_qp_c = 30; break;
        case 32 : i_pic_qp_c = 31; break;
        case 33 : i_pic_qp_c = 32; break;
        case 34 : i_pic_qp_c = 32; break;
        case 35 : i_pic_qp_c = 33; break;
        case 36 : i_pic_qp_c = 34; break;
        case 37 : i_pic_qp_c = 34; break;
        case 38 : i_pic_qp_c = 35; break;
        case 39 : i_pic_qp_c = 35; break;
        case 40 : i_pic_qp_c = 36; break;
        case 41 : i_pic_qp_c = 36; break;
        case 42 : i_pic_qp_c = 37; break;
        case 43 : i_pic_qp_c = 37; break;
        case 44 : i_pic_qp_c = 37; break;
        case 45 : i_pic_qp_c = 38; break;
        case 46 : i_pic_qp_c = 38; break;
        case 47 : i_pic_qp_c = 38; break;
        case 48 : i_pic_qp_c = 39; break;
        case 49 : i_pic_qp_c = 39; break;
        case 50 : i_pic_qp_c = 39; break;
        default : i_pic_qp_c = 39; break; // should only be 51 or more (when index_offset)
    }

    switch (p_pic_qp) {    // synopsys parallel_case full_case
        case 0 : p_pic_qp_c = 0; break;
        case 1 : p_pic_qp_c = 1; break;
        case 2 : p_pic_qp_c = 2; break;
        case 3 : p_pic_qp_c = 3; break;
        case 4 : p_pic_qp_c = 4; break;
        case 5 : p_pic_qp_c = 5; break;
        case 6 : p_pic_qp_c = 6; break;
        case 7 : p_pic_qp_c = 7; break;
        case 8 : p_pic_qp_c = 8; break;
        case 9 : p_pic_qp_c = 9; break;
        case 10 : p_pic_qp_c = 10; break;
        case 11 : p_pic_qp_c = 11; break;
        case 12 : p_pic_qp_c = 12; break;
        case 13 : p_pic_qp_c = 13; break;
        case 14 : p_pic_qp_c = 14; break;
        case 15 : p_pic_qp_c = 15; break;
        case 16 : p_pic_qp_c = 16; break;
        case 17 : p_pic_qp_c = 17; break;
        case 18 : p_pic_qp_c = 18; break;
        case 19 : p_pic_qp_c = 19; break;
        case 20 : p_pic_qp_c = 20; break;
        case 21 : p_pic_qp_c = 21; break;
        case 22 : p_pic_qp_c = 22; break;
        case 23 : p_pic_qp_c = 23; break;
        case 24 : p_pic_qp_c = 24; break;
        case 25 : p_pic_qp_c = 25; break;
        case 26 : p_pic_qp_c = 26; break;
        case 27 : p_pic_qp_c = 27; break;
        case 28 : p_pic_qp_c = 28; break;
        case 29 : p_pic_qp_c = 29; break;
        case 30 : p_pic_qp_c = 29; break;
        case 31 : p_pic_qp_c = 30; break;
        case 32 : p_pic_qp_c = 31; break;
        case 33 : p_pic_qp_c = 32; break;
        case 34 : p_pic_qp_c = 32; break;
        case 35 : p_pic_qp_c = 33; break;
        case 36 : p_pic_qp_c = 34; break;
        case 37 : p_pic_qp_c = 34; break;
        case 38 : p_pic_qp_c = 35; break;
        case 39 : p_pic_qp_c = 35; break;
        case 40 : p_pic_qp_c = 36; break;
        case 41 : p_pic_qp_c = 36; break;
        case 42 : p_pic_qp_c = 37; break;
        case 43 : p_pic_qp_c = 37; break;
        case 44 : p_pic_qp_c = 37; break;
        case 45 : p_pic_qp_c = 38; break;
        case 46 : p_pic_qp_c = 38; break;
        case 47 : p_pic_qp_c = 38; break;
        case 48 : p_pic_qp_c = 39; break;
        case 49 : p_pic_qp_c = 39; break;
        case 50 : p_pic_qp_c = 39; break;
        default : p_pic_qp_c = 39; break; // should only be 51 or more (when index_offset)
    }
    WRITE_HREG(QDCT_Q_QUANT_I,
                (i_pic_qp_c<<22) |
                (i_pic_qp<<16) |
                ((i_pic_qp_c%6)<<12)|((i_pic_qp_c/6)<<8)|((i_pic_qp%6)<<4)|((i_pic_qp/6)<<0));

    WRITE_HREG(QDCT_Q_QUANT_P,
                (p_pic_qp_c<<22) |
                (p_pic_qp<<16) |
                ((p_pic_qp_c%6)<<12)|((p_pic_qp_c/6)<<8)|((p_pic_qp%6)<<4)|((p_pic_qp/6)<<0));

    //avc_init_input_buffer();

    WRITE_HREG(IGNORE_CONFIG ,
                (1<<31) | // ignore_lac_coeff_en
                (1<<26) | // ignore_lac_coeff_else (<1)
                (1<<21) | // ignore_lac_coeff_2 (<1)
                (2<<16) | // ignore_lac_coeff_1 (<2)
                (1<<15) | // ignore_cac_coeff_en
                (1<<10) | // ignore_cac_coeff_else (<1)
                (1<<5)  | // ignore_cac_coeff_2 (<1)
                (2<<0));    // ignore_cac_coeff_1 (<2)

    WRITE_HREG(IGNORE_CONFIG_2,
                (1<<31) | // ignore_t_lac_coeff_en
                (1<<26) | // ignore_t_lac_coeff_else (<1)
                (1<<21) | // ignore_t_lac_coeff_2 (<1)
                (5<<16) | // ignore_t_lac_coeff_1 (<5)
                (0<<0));

    WRITE_HREG(QDCT_MB_CONTROL,
                (1<<9) | // mb_info_soft_reset
                (1<<0)); // mb read buffer soft reset

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        WRITE_HREG(QDCT_MB_CONTROL,
                  (0<<28) | // ignore_t_p8x8
                  (0<<27) | // zero_mc_out_null_non_skipped_mb
                  (0<<26) | // no_mc_out_null_non_skipped_mb
                  (0<<25) | // mc_out_even_skipped_mb
                  (0<<24) | // mc_out_wait_cbp_ready
                  (0<<23) | // mc_out_wait_mb_type_ready
                  (1<<29) | // ie_start_int_enable
                  (1<<19) | // i_pred_enable
                  (1<<20) | // ie_sub_enable
                  (1<<18) | // iq_enable
                  (1<<17) | // idct_enable
                  (1<<14) | // mb_pause_enable
                  (1<<13) | // q_enable
                  (1<<12) | // dct_enable
                  (1<<10) | // mb_info_en
                  (0<<3) | // endian
                  (0<<1) | // mb_read_en
                  (0<<0));   // soft reset
    }else{
        WRITE_HREG(QDCT_MB_CONTROL,
                  (0<<28) | // ignore_t_p8x8
                  (0<<27) | // zero_mc_out_null_non_skipped_mb
                  (0<<26) | // no_mc_out_null_non_skipped_mb
                  (0<<25) | // mc_out_even_skipped_mb
                  (0<<24) | // mc_out_wait_cbp_ready
                  (0<<23) | // mc_out_wait_mb_type_ready
                  (1<<22) | // i_pred_int_enable
                  (1<<19) | // i_pred_enable
                  (1<<20) | // ie_sub_enable
                  (1<<18) | // iq_enable
                  (1<<17) | // idct_enable
                  (1<<14) | // mb_pause_enable
                  (1<<13) | // q_enable
                  (1<<12) | // dct_enable
                  (1<<10) | // mb_info_en
                  (avc_endian<<3) | // endian
                  (1<<1) | // mb_read_en
                  (0<<0));   // soft reset
    }

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        int me_mode  = (ie_me_mode >>ME_PIXEL_MODE_SHIFT)&ME_PIXEL_MODE_MASK;
        WRITE_HREG(SAD_CONTROL,
                  (0<<3) | // ie_result_buff_enable
                  (1<<2) | // ie_result_buff_soft_reset
                  (0<<1) | // sad_enable
                  (1<<0));   // sad soft reset

        WRITE_HREG(IE_RESULT_BUFFER, 0);

        WRITE_HREG(SAD_CONTROL,
                  (1<<3) | // ie_result_buff_enable
                  (0<<2) | // ie_result_buff_soft_reset
                  (1<<1) | // sad_enable
                  (0<<0));   // sad soft reset

        WRITE_HREG(IE_CONTROL,
                  (0<<1) | // ie_enable
                  (1<<0));   // ie soft reset

        WRITE_HREG(IE_CONTROL,
                  (0<<1) | // ie_enable
                  (0<<0)); // ie soft reset

        WRITE_HREG(ME_SAD_ENOUGH_01,me_sad_enough_01);
                  //(0x18<<12) | // me_sad_enough_1
                  //(0x10<<0)); // me_sad_enough_0

        WRITE_HREG(ME_SAD_ENOUGH_23, me_sad_enough_23);
                  //(0x20<<0) | // me_sad_enough_2
                  //(0<<12)); // me_sad_enough_3

        WRITE_HREG(ME_STEP0_CLOSE_MV,
                  (0x100 << 10) | // me_step0_big_sad -- two MV sad diff bigger will use use 1
                  (2<<5) | // me_step0_close_mv_y
                  (2<<0));   // me_step0_close_mv_x

        if(me_mode == 3){
            WRITE_HREG(ME_SKIP_LINE,
                      ( 8 << 24) |  // step_3_skip_line
                      ( 8 << 18) |  // step_2_skip_line
                      ( 2 << 12) |  // step_1_skip_line
                      ( 0 << 6) |  // step_0_skip_line
                      //(8 <<0); // read 8*2 less line to save bandwidth
                      (0 <<0)); // read 8*2 less line to save bandwidth

            WRITE_HREG(ME_F_SKIP_SAD,
                      ( 0x00 << 24) |  // force_skip_sad_3
                      ( 0x00 << 16) |  // force_skip_sad_2
                      ( 0x30 << 8)  |  // force_skip_sad_1
                      ( 0x10 << 0));    // force_skip_sad_0

            WRITE_HREG(ME_F_SKIP_WEIGHT,
                      ( 0x00 << 24) |  // force_skip_weight_3
                      ( 0x08 << 16) |  // force_skip_weight_2
                      ( 0x18 << 8)  |  // force_skip_weight_1
                      ( 0x18 << 0));    // force_skip_weight_0
        }else{
            WRITE_HREG(ME_SKIP_LINE,
                      ( 4 << 24) |  // step_3_skip_line
                      ( 4 << 18) |  // step_2_skip_line
                      ( 2 << 12) |  // step_1_skip_line
                      ( 0 << 6) |  // step_0_skip_line
                      //(8 <<0); // read 8*2 less line to save bandwidth
                      (0 <<0)); // read 8*2 less line to save bandwidth

            WRITE_HREG(ME_F_SKIP_SAD,
                      ( 0x40 << 24) |  // force_skip_sad_3
                      //( 0x40 << 16) |  // force_skip_sad_2
                      ( 0x30 << 16) |  // force_skip_sad_2
                      ( 0x30 << 8)  |  // force_skip_sad_1
                      ( 0x10 << 0));    // force_skip_sad_0

            WRITE_HREG(ME_F_SKIP_WEIGHT,
                      ( 0x18 << 24) |  // force_skip_weight_3
                      ( 0x18 << 16) |  // force_skip_weight_2
                      ( 0x18 << 8)  |  // force_skip_weight_1
                      ( 0x18 << 0));    // force_skip_weight_0
        }

        WRITE_HREG(IE_DATA_FEED_BUFF_INFO,0);
    }

    WRITE_HREG(HCODEC_CURR_CANVAS_CTRL,0);
    data32 = READ_HREG(VLC_CONFIG);
    data32 = data32 | (1<<0); // set pop_coeff_even_all_zero
    WRITE_HREG(VLC_CONFIG , data32);

    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        if(IDR){
            WRITE_HREG(BITS_INFO_DDR_START, wq->mem.intra_bits_info_ddr_start_addr);
            WRITE_HREG(MV_INFO_DDR_START, wq->mem.intra_pred_info_ddr_start_addr);
        }else{
            WRITE_HREG(BITS_INFO_DDR_START, wq->mem.inter_bits_info_ddr_start_addr);
            WRITE_HREG(MV_INFO_DDR_START, wq->mem.inter_mv_info_ddr_start_addr);
        }
    }else{
        WRITE_HREG(SW_CTL_INFO_DDR_START, wq->mem.sw_ctl_info_start_addr);
    }
    /* clear mailbox interrupt */
    WRITE_HREG(HCODEC_IRQ_MBOX_CLR, 1);

    /* enable mailbox interrupt */
    WRITE_HREG(HCODEC_IRQ_MBOX_MASK, 1);
}

void amvenc_reset(void)
{
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    WRITE_VREG(DOS_SW_RESET1, (1<<2)|(1<<6)|(1<<7)|(1<<8)|(1<<16)|(1<<17));
    WRITE_VREG(DOS_SW_RESET1, 0);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);

}

void amvenc_start(void)
{
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    WRITE_VREG(DOS_SW_RESET1, (1<<12)|(1<<11));
    WRITE_VREG(DOS_SW_RESET1, 0);

    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);

    WRITE_HREG(HCODEC_MPSR, 0x0001);
}

void amvenc_stop(void)
{
    ulong timeout = jiffies + HZ;

    WRITE_HREG(HCODEC_MPSR, 0);
    WRITE_HREG(HCODEC_CPSR, 0);
    while (READ_HREG(HCODEC_IMEM_DMA_CTRL) & 0x8000) {
        if (time_after(jiffies, timeout)) {
            break;
        }
    }
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);

    WRITE_VREG(DOS_SW_RESET1, (1<<12)|(1<<11)|(1<<2)|(1<<6)|(1<<7)|(1<<8)|(1<<16)|(1<<17));
    //WRITE_VREG(DOS_SW_RESET1, (1<<12)|(1<<11));
    WRITE_VREG(DOS_SW_RESET1, 0);

    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
    READ_VREG(DOS_SW_RESET1);
}

static void __iomem *mc_addr=NULL;
static unsigned mc_addr_map;
#define MC_SIZE (4096 * 4)
s32 amvenc_loadmc(const u32 *p, encode_wq_t* wq)
{
    ulong timeout;
    s32 ret = 0 ;

    mc_addr_map = wq->mem.assit_buffer_offset;
    mc_addr = ioremap_wc(mc_addr_map,MC_SIZE);
    memcpy(mc_addr, p, MC_SIZE);
    encode_debug_level(LOG_LEVEL_ALL, "address 0 is 0x%x\n", *((u32*)mc_addr));
    encode_debug_level(LOG_LEVEL_ALL, "address 1 is 0x%x\n", *((u32*)mc_addr + 1));
    encode_debug_level(LOG_LEVEL_ALL, "address 2 is 0x%x\n", *((u32*)mc_addr + 2));
    encode_debug_level(LOG_LEVEL_ALL, "address 3 is 0x%x\n", *((u32*)mc_addr + 3));
    WRITE_HREG(HCODEC_MPSR, 0);
    WRITE_HREG(HCODEC_CPSR, 0);

    /* Read CBUS register for timing */
    timeout = READ_HREG(HCODEC_MPSR);
    timeout = READ_HREG(HCODEC_MPSR);

    timeout = jiffies + HZ;

    WRITE_HREG(HCODEC_IMEM_DMA_ADR, mc_addr_map);
    WRITE_HREG(HCODEC_IMEM_DMA_COUNT, 0x1000);
    WRITE_HREG(HCODEC_IMEM_DMA_CTRL, (0x8000 |   (7 << 16)));

    while (READ_HREG(HCODEC_IMEM_DMA_CTRL) & 0x8000) {
        if (time_before(jiffies, timeout)) {
            schedule();
        } else {
            encode_debug_level(LOG_LEVEL_ERROR, "hcodec load mc error\n");
            ret = -EBUSY;
            break;
        }
    }
    iounmap(mc_addr);
    mc_addr=NULL;

    return ret;
}

const u32 fix_mc[] __attribute__ ((aligned (8))) = {
    0x0809c05a, 0x06696000, 0x0c780000, 0x00000000
};


/*
 * DOS top level register access fix.
 * When hcodec is running, a protocol register HCODEC_CCPU_INTR_MSK
 * is set to make hcodec access one CBUS out of DOS domain once
 * to work around a HW bug for 4k2k dual decoder implementation.
 * If hcodec is not running, then a ucode is loaded and executed
 * instead.
 */
void amvenc_dos_top_reg_fix(void)
{
    bool hcodec_on;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    hcodec_on = vdec_on(VDEC_HCODEC);

    if ((hcodec_on) && (READ_VREG(HCODEC_MPSR) & 1)) {
        WRITE_HREG(HCODEC_CCPU_INTR_MSK, 1);
        spin_unlock_irqrestore(&lock, flags);
        return;
    }

    if (!hcodec_on) {
        vdec_poweron(VDEC_HCODEC);
    }

    amhcodec_loadmc(fix_mc);

    amhcodec_start();

    udelay(1000);

    amhcodec_stop();

    if (!hcodec_on) {
        vdec_poweroff(VDEC_HCODEC);
    }

    spin_unlock_irqrestore(&lock, flags);
}

bool amvenc_avc_on(void)
{
    bool hcodec_on;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    hcodec_on = vdec_on(VDEC_HCODEC);
    hcodec_on &=(encode_manager.wq_count>0);

    spin_unlock_irqrestore(&lock, flags);
    return hcodec_on;
}

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
#define  DMC_SEC_PORT8_RANGE0  0x840
#define  DMC_SEC_CTRL  0x829
#endif

void enable_hcoder_ddr_access(void)
{
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
    WRITE_SEC_REG(DMC_SEC_PORT8_RANGE0 , 0xffff);
    WRITE_SEC_REG(DMC_SEC_CTRL , 0x80000000);
#endif
}

static s32 avc_poweron(int clock)
{
    unsigned long flags;
    u32 data32 = 0;
    data32 = 0;
    enable_hcoder_ddr_access();

    //CLK_GATE_ON(DOS);
    switch_mod_gate_by_name("vdec", 1);

    spin_lock_irqsave(&lock, flags);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    data32 = READ_AOREG(AO_RTI_PWR_CNTL_REG0);
    data32 = data32 & (~(0x18));
    WRITE_AOREG(AO_RTI_PWR_CNTL_REG0, data32);
    udelay(10);
    // Powerup HCODEC
    data32 = READ_AOREG(AO_RTI_GEN_PWR_SLEEP0); // [1:0] HCODEC
    data32 = data32 & (~0x3);
    WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, data32);
    udelay(10);
#endif

    WRITE_VREG(DOS_SW_RESET1, 0xffffffff);
    WRITE_VREG(DOS_SW_RESET1, 0);

    // Enable Dos internal clock gating
    hvdec_clock_enable(clock);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    //Powerup HCODEC memories
    WRITE_VREG(DOS_MEM_PD_HCODEC, 0x0);

    // Remove HCODEC ISO
    data32 = READ_AOREG(AO_RTI_GEN_PWR_ISO0);
    data32 = data32 & (~(0x30));
    WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, data32);
    udelay(10);
#endif
    // Disable auto-clock gate
    data32 = READ_VREG(DOS_GEN_CTRL0);
    data32 = data32 | 0x1;
    WRITE_VREG(DOS_GEN_CTRL0, data32);
    data32 = READ_VREG(DOS_GEN_CTRL0);
    data32 = data32 & 0xFFFFFFFE;
    WRITE_VREG(DOS_GEN_CTRL0, data32);

#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if (!vdec_on(VDEC_2) && get_vdec2_usage() == USAGE_NONE) {//++++
            set_vdec2_usage(USAGE_ENCODE);
            vdec_poweron(VDEC_2);//++++
        }
    }
#endif

    spin_unlock_irqrestore(&lock, flags);

    mdelay(10);
    return 0;
}

static s32 avc_poweroff(void)
{
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    // enable HCODEC isolation
    WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, READ_AOREG(AO_RTI_GEN_PWR_ISO0) | 0x30);
    // power off HCODEC memories
    WRITE_VREG(DOS_MEM_PD_HCODEC, 0xffffffffUL);
#endif
    // disable HCODEC clock
    hvdec_clock_disable();

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    // HCODEC power off
    WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 0x3);
#endif

#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if (vdec_on(VDEC_2) && get_vdec2_usage() != USAGE_DEC_4K2K) {//++++
            vdec_poweroff(VDEC_2);//++++
            set_vdec2_usage(USAGE_NONE);
        }
    }
#endif

    spin_unlock_irqrestore(&lock, flags);

    // release DOS clk81 clock gating
    //CLK_GATE_OFF(DOS);
    switch_mod_gate_by_name("vdec", 0);
    return 0;
}

static s32 reload_mc(encode_wq_t* wq)
{
    const u32 * p = select_ucode(encode_manager.ucode_index);

    amvenc_stop();

    WRITE_VREG(DOS_SW_RESET1, 0xffffffff);
    WRITE_VREG(DOS_SW_RESET1, 0);

    udelay(10);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESONG9TV
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
    }else{
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    }
#else
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
#endif

    encode_debug_level(LOG_LEVEL_INFO,"reload microcode\n");

    if (amvenc_loadmc(p,wq) < 0) {
        return -EBUSY;
    }
    return 0;
}

static void encode_isr_tasklet(ulong data)
{
    encode_manager_t*  manager = (encode_manager_t*)data ;
    encode_debug_level(LOG_LEVEL_INFO,"encoder is done %d\n",manager->encode_hw_status);
    if(((manager->encode_hw_status == ENCODER_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)
      ||(manager->encode_hw_status == ENCODER_PICTURE_DONE))&&(manager->process_irq)){
#ifdef USE_VDEC2
        if(IS_MESON_M8_CPU){
            if((abort_vdec2_flag)&&(get_vdec2_usage() == USAGE_ENCODE))
                set_vdec2_usage(USAGE_NONE);
        }
#endif
        wake_up_interruptible(&manager->event.hw_complete);
    }
}

// irq function
static irqreturn_t enc_isr(int  irq_number, void *para)
{
    encode_manager_t*  manager = (encode_manager_t*)para ;
    WRITE_HREG(HCODEC_IRQ_MBOX_CLR, 1);

#ifdef DEBUG_UCODE
//rain
    if(READ_HREG(DEBUG_REG)!=0){
        printk("dbg%x: %x\n",  READ_HREG(DEBUG_REG), READ_HREG(HENC_SCRATCH_1));
        WRITE_HREG(DEBUG_REG, 0);
        return IRQ_HANDLED;
    }
#endif

    manager->encode_hw_status  = READ_HREG(ENCODER_STATUS);
    if((manager->encode_hw_status == ENCODER_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)
      ||(manager->encode_hw_status == ENCODER_PICTURE_DONE)){
        encode_debug_level(LOG_LEVEL_ALL, "encoder stage is %d\n",manager->encode_hw_status);
    }

    if(((manager->encode_hw_status == ENCODER_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)
      ||(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)
      ||(manager->encode_hw_status == ENCODER_PICTURE_DONE))&&(!manager->process_irq)){
        manager->process_irq = true;
        if(manager->encode_hw_status != ENCODER_SEQUENCE_DONE)
            manager->need_reset = true;
        tasklet_schedule(&manager->encode_tasklet);
    }
    return IRQ_HANDLED;
}

static s32 convert_request(encode_wq_t* wq, unsigned* cmd_info)
{
    unsigned cmd = cmd_info[0];
    if(!wq)
        return -1;
    memset(&wq->request, 0, sizeof(encode_request_t));

    if(cmd == ENCODER_SEQUENCE){
        wq->request.cmd = cmd;
        wq->request.ucode_mode = cmd_info[1];
        wq->request.quant = cmd_info[2];
        wq->request.flush_flag = cmd_info[3];
        wq->request.timeout = cmd_info[4];
        wq->request.timeout = 5000; // 5000 ms
    }else if((cmd == ENCODER_IDR)||(cmd == ENCODER_NON_IDR)){
        wq->request.cmd = cmd;
        wq->request.ucode_mode = cmd_info[1];
        if(wq->request.ucode_mode == UCODE_MODE_FULL){
            wq->request.type = cmd_info[2];
            wq->request.fmt = cmd_info[3];
            wq->request.src = cmd_info[4];
            wq->request.framesize = cmd_info[5];
            wq->request.quant = cmd_info[6];
            wq->request.flush_flag = cmd_info[7];
            wq->request.timeout = cmd_info[8];
        }else{
            wq->request.quant = cmd_info[2];
            wq->request.qp_info_size = cmd_info[3];
            wq->request.flush_flag = cmd_info[4];
            wq->request.timeout = cmd_info[5];
        }
    }else{
        encode_debug_level(LOG_LEVEL_ERROR," error cmd = %d, wq: %p.\n", cmd, (void*)wq);
        return -1;
    }
    wq->request.parent = wq;
    return 0;
}

void amvenc_avc_start_cmd(encode_wq_t* wq, encode_request_t* request)
{
    int reload_flag = 0;
#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if((request->ucode_mode == UCODE_MODE_SW_MIX)&&(enable_dblk>0)){
            if((get_vdec2_usage() == USAGE_DEC_4K2K)||(abort_vdec2_flag)){
                enable_dblk = 2;
                if((abort_vdec2_flag)&&(get_vdec2_usage() == USAGE_ENCODE)){
                    encode_debug_level(LOG_LEVEL_DEBUG, "switch encode ucode, wq:%p \n",(void*)wq);
                    set_vdec2_usage(USAGE_NONE);
                }
            }else{
                if(get_vdec2_usage() == USAGE_NONE)
                    set_vdec2_usage(USAGE_ENCODE);
                    if(!vdec_on(VDEC_2)){
                        vdec_poweron(VDEC_2);//++++
                        mdelay(10);
                    }
                    enable_dblk = 1;
                }
            }
    }
#endif
    if(request->ucode_mode!=encode_manager.ucode_index){
        encode_manager.ucode_index = request->ucode_mode;
        if(reload_mc(wq)){
            encode_debug_level(LOG_LEVEL_ERROR, "reload mc fail, wq:%p\n", (void*)wq);
            return;
        }
        reload_flag = 1;
        encode_manager.need_reset= true;
    }else if((request->parent != encode_manager.last_wq)&&(request->ucode_mode == UCODE_MODE_SW_MIX)){
        //walk around to reset the armrisc
        if(reload_mc(wq)){
            encode_debug_level(LOG_LEVEL_ERROR, "reload mc fail, wq:%p\n", (void*)wq);
            return;
        }
        reload_flag = 1;
        encode_manager.need_reset= true;
    }

    wq->hw_status = 0;
    wq->output_size = 0;
    wq->ucode_index = encode_manager.ucode_index;
    if((request->cmd == ENCODER_SEQUENCE)||(request->cmd == ENCODER_PICTURE))
        wq->control.finish = true;

    ie_me_mode |= (0 & ME_PIXEL_MODE_MASK)<<ME_PIXEL_MODE_SHIFT;
    if(encode_manager.need_reset){
        encode_manager.need_reset = false;
        encode_manager.encode_hw_status = ENCODER_IDLE ;
        amvenc_reset();
        avc_canvas_init(wq);
        avc_init_encoder(wq,(request->cmd == ENCODER_IDR)?true:false);
        avc_init_input_buffer(wq);
        avc_init_output_buffer(wq);
        avc_prot_init(wq, request->quant, (request->cmd == ENCODER_IDR)?true:false);
        avc_init_assit_buffer(wq);
        encode_debug_level(LOG_LEVEL_INFO,"begin to new frame, request->cmd: %d, ucode mode: %d, wq:%p.\n",request->cmd, request->ucode_mode, (void*)wq);
    }
    if((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR)){
        avc_init_dblk_buffer(wq->mem.dblk_buf_canvas);
        avc_init_reference_buffer(wq->mem.ref_buf_canvas);
    }
    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        if((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR)){
            set_input_format(wq, request);
        }
        if(request->cmd == ENCODER_IDR)
            ie_me_mb_type = HENC_MB_Type_I4MB;
        else if(request->cmd == ENCODER_NON_IDR)
            ie_me_mb_type = (HENC_SKIP_RUN_AUTO<<16)|(HENC_MB_Type_AUTO<<4) | (HENC_MB_Type_AUTO <<0);
        else
            ie_me_mb_type = 0;
        avc_init_ie_me_parameter(wq,request->quant);
    }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    else{
        if((wq->mem.dblk_buf_canvas&0xff)==ENC_CANVAS_OFFSET){
            WRITE_HREG(CURRENT_Y_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec0_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec0_uv.buf_start);
        }else{
            WRITE_HREG(CURRENT_Y_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec1_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec1_uv.buf_start);
        }
        WRITE_HREG(CANVAS_ROW_SIZE,(((wq->pic.encoder_width+31)>>5)<<5));

#ifdef USE_VDEC2
        if((enable_dblk == 1)&&(IS_MESON_M8_CPU)){
            amvdec2_stop();
            WRITE_VREG(VDEC2_AV_SCRATCH_2, 0xffff);
            // set vdec2 input, clone hcodec input buffer and set to manual mode
            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CONTROL, 0); //++++

            WRITE_VREG(DOS_SW_RESET2, (1<<4));
            WRITE_VREG(DOS_SW_RESET2, 0);
            (void)READ_VREG(DOS_SW_RESET2);
            (void)READ_VREG(DOS_SW_RESET2);
            WRITE_VREG(VDEC2_POWER_CTL_VLD, (1<<4)|(1<<6)|(1<<9));//++++

            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_START_PTR,wq->mem.BitstreamStart);//++++
            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_END_PTR,wq->mem.BitstreamEnd);//++++
            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CURR_PTR,wq->mem.BitstreamStart);//++++

            SET_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_CONTROL, 1);//++++
            CLEAR_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_CONTROL, 1);//++++

            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 2);//++++
            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_WP,wq->mem.BitstreamStart);//++++

            SET_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 1);//++++
            CLEAR_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 1);//++++

            WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CONTROL, (0x11<<16) | (1<<10) | (7<<3) | (1<<2) | (1<<1)); //++++

            amvdec2_loadmc(vdec2_encoder_mc);//++++

            WRITE_VREG(VDEC2_AV_SCRATCH_1, wq->mem.vdec2_start_addr - VDEC2_DEF_BUF_START_ADDR);//++++
            WRITE_VREG(VDEC2_AV_SCRATCH_8, wq->pic.log2_max_pic_order_cnt_lsb);//++++
            WRITE_VREG(VDEC2_AV_SCRATCH_9, wq->pic.log2_max_frame_num);//++++
            WRITE_VREG(VDEC2_AV_SCRATCH_B, wq->pic.init_qppicture);//++++
            WRITE_VREG(VDEC2_AV_SCRATCH_A, (((wq->pic.encoder_height+15)/16) << 16) | ((wq->pic.encoder_width+15)/16));//++++

            // Input/Output canvas
            WRITE_VREG(VDEC2_ANC0_CANVAS_ADDR, wq->mem.ref_buf_canvas);//++++
            WRITE_VREG(VDEC2_ANC1_CANVAS_ADDR, wq->mem.dblk_buf_canvas);//++++

            WRITE_VREG(DECODED_MB_Y, 0);
            // MBY limit
            WRITE_VREG(DECODABLE_MB_Y, 0);
            // VB WP
            WRITE_VREG(STREAM_WR_PTR, wq->mem.BitstreamStart);
            // NV21
            SET_VREG_MASK(VDEC2_MDEC_PIC_DC_CTRL, 1<<17);//++++

            WRITE_VREG(VDEC2_M4_CONTROL_REG, 1<<13); //set h264_en//++++
            WRITE_VREG(VDEC2_MDEC_PIC_DC_THRESH, 0x404038aa);//++++

            amvdec2_start();//amvdec2_start();//++++
        }
#endif
    }
#endif
    encode_manager.encode_hw_status = request->cmd;
    wq->hw_status = request->cmd;
	
    //if((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR)
    //  ||(request->cmd == ENCODER_SEQUENCE)||(request->cmd == ENCODER_PICTURE)){
    //    encode_manager.process_irq = false;
    //}

    WRITE_HREG(ENCODER_STATUS , request->cmd);
    if((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR)
      ||(request->cmd == ENCODER_SEQUENCE)||(request->cmd == ENCODER_PICTURE)){
        encode_manager.process_irq = false;
    }
#ifdef MULTI_SLICE_MC
    if(fixed_slice_cfg){
        WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
    }else if(wq->pic.rows_per_slice !=  (wq->pic.encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (wq->pic.rows_per_slice<<16)|wq->pic.rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
#else
    WRITE_HREG(FIXED_SLICE_CFG, 0);
#endif
    if(reload_flag)
        amvenc_start();
    if((encode_manager.ucode_index == UCODE_MODE_SW_MIX)&&((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR))){
        wq->control.can_update = true;
    }
    encode_debug_level(LOG_LEVEL_ALL,"amvenc_avc_start cmd, wq:%p.\n", (void*)wq);
}

static void dma_flush(unsigned buf_start , unsigned buf_size )
{
    //dma_sync_single_for_cpu(amvenc_avc_dev,buf_start, buf_size, DMA_TO_DEVICE);
    dma_sync_single_for_device(amvenc_avc_dev,buf_start ,buf_size, DMA_TO_DEVICE);
}

static void cache_flush(unsigned buf_start , unsigned buf_size )
{
    dma_sync_single_for_cpu(amvenc_avc_dev , buf_start, buf_size, DMA_FROM_DEVICE);
    //dma_sync_single_for_device(amvenc_avc_dev ,buf_start , buf_size, DMA_FROM_DEVICE);
}

static unsigned getbuffer(encode_wq_t* wq, unsigned type)
{
    unsigned ret = 0;

    switch(type){
        case ENCODER_BUFFER_INPUT:
            ret = wq->mem.dct_buff_start_addr;
            break;
        case ENCODER_BUFFER_REF0:
            ret = wq->mem.dct_buff_start_addr + wq->mem.bufspec.dec0_y.buf_start;
            break;
        case ENCODER_BUFFER_REF1:
            ret = wq->mem.dct_buff_start_addr + wq->mem.bufspec.dec1_y.buf_start;
            break;
        case ENCODER_BUFFER_OUTPUT:
            ret = wq->mem.BitstreamStart ;
            break;
        case ENCODER_BUFFER_INTER_INFO:
            ret = wq->mem.inter_bits_info_ddr_start_addr;
            break;
        case ENCODER_BUFFER_INTRA_INFO:
            ret = wq->mem.intra_bits_info_ddr_start_addr;
            break;
        case ENCODER_BUFFER_QP:
            ret = wq->mem.sw_ctl_info_start_addr;
            break;
        default:
            break;
    }
    return ret;
}

s32 amvenc_avc_start(encode_wq_t* wq, int clock)
{
    const u32 * p = select_ucode(encode_manager.ucode_index);

    avc_poweron(clock);
    avc_canvas_init(wq);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESONG9TV
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
    }else{
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    }
#else
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
#endif

    if (amvenc_loadmc(p,wq) < 0) {
        return -EBUSY;
    }

    encode_manager.need_reset = true;
    encode_manager.process_irq = false;
    encode_manager.encode_hw_status = ENCODER_IDLE ;
    amvenc_reset();
    avc_init_encoder(wq,true);
    avc_init_input_buffer(wq);  //dct buffer setting
    avc_init_output_buffer(wq);  //output stream buffer

    ie_me_mode |= (0 & ME_PIXEL_MODE_MASK)<<ME_PIXEL_MODE_SHIFT;
    avc_prot_init(wq, wq->pic.init_qppicture, true);
    encode_manager.irq_num = request_irq(INT_AMVENCODER, enc_isr, IRQF_SHARED, "enc-irq", (void *)&encode_manager);//INT_MAILBOX_1A
    avc_init_dblk_buffer(wq->mem.dblk_buf_canvas);   //decoder buffer , need set before each frame start
    avc_init_reference_buffer(wq->mem.ref_buf_canvas); //reference  buffer , need set before each frame start
    avc_init_assit_buffer(wq); //assitant buffer for microcode
    if(encode_manager.ucode_index != UCODE_MODE_SW_MIX){
        ie_me_mb_type = 0;
        avc_init_ie_me_parameter(wq, wq->pic.init_qppicture);
    }
    else{
        if((wq->mem.dblk_buf_canvas&0xff)==ENC_CANVAS_OFFSET){
            WRITE_HREG(CURRENT_Y_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec0_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec0_uv.buf_start);
        }
        else{
            WRITE_HREG(CURRENT_Y_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec1_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, wq->mem.buf_start + wq->mem.bufspec.dec1_uv.buf_start);
        }
        WRITE_HREG(CANVAS_ROW_SIZE,(((wq->pic.encoder_width+31)>>5)<<5));
    }
    WRITE_HREG(ENCODER_STATUS , ENCODER_IDLE);

#ifdef MULTI_SLICE_MC
    if(fixed_slice_cfg){
        WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
    }else if(wq->pic.rows_per_slice !=  (wq->pic.encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (wq->pic.rows_per_slice<<16)|wq->pic.rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
#else
    WRITE_HREG(FIXED_SLICE_CFG, 0);
#endif
    amvenc_start();
    return 0;
}

void amvenc_avc_stop(void)
{
    if(encode_manager.irq_num >=0){
        free_irq(INT_AMVENCODER,&encode_manager);
        encode_manager.irq_num = -1;
    }
#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if((get_vdec2_usage() != USAGE_DEC_4K2K)&&(vdec_on(VDEC_2)))
            amvdec2_stop();
    }
#endif
    amvenc_stop();
    avc_poweroff();
}

static s32 avc_init(encode_wq_t* wq)
{
    s32 r = 0;

    encode_manager.ucode_index = wq->ucode_index;
    r = amvenc_avc_start(wq, clock_level);

    encode_debug_level(LOG_LEVEL_DEBUG,"init avc encode. microcode %d, ret=%d, wq:%p.\n", encode_manager.ucode_index, r, (void*)wq);
    return 0;
}

static s32 amvenc_avc_light_reset(encode_wq_t* wq, unsigned value)
{
    s32 r = 0;

    amvenc_avc_stop();

    mdelay(value);

    encode_manager.ucode_index = UCODE_MODE_FULL;
    r = amvenc_avc_start(wq, clock_level);

    encode_debug_level(LOG_LEVEL_DEBUG, "amvenc_avc_light_reset finish, wq:%p. ret=%d\n",(void*)wq,r);
    return r;
}

#ifdef CONFIG_CMA
static int checkCMA(void)
{
    int i = 0, j = 0;
    struct page *buff[MAX_ENCODE_INSTANCE];

    for(i = 0; i< MAX_ENCODE_INSTANCE;i++)
        buff[i] = NULL;

    for(i = 0; i< MAX_ENCODE_INSTANCE;i++){
        buff[i] = dma_alloc_from_contiguous(&encode_manager.this_pdev->dev, (18 * SZ_1M) >> PAGE_SHIFT, 0);
        if(buff[i] == NULL)
            break;
    }
    for(j = 0; j< i;j++){
        if(buff[j])
            dma_release_from_contiguous(&encode_manager.this_pdev->dev, buff[j], (18 * SZ_1M)>>PAGE_SHIFT);
    }    
    return i;
}
#endif

// file operation
static int amvenc_avc_open(struct inode *inode, struct file *file)
{
    int r = 0;
    encode_wq_t* wq = NULL;

    file->private_data = NULL;
    encode_debug_level(LOG_LEVEL_DEBUG,"avc open\n");
#ifdef CONFIG_AM_JPEG_ENCODER
    if(jpegenc_on() == true){
        encode_debug_level(LOG_LEVEL_ERROR,"hcodec in use for JPEG Encode now.\n");
        return -EBUSY;
    }
#endif
#ifdef CONFIG_CMA
    if((encode_manager.use_reserve == false)&&(encode_manager.check_cma == false)){
        encode_manager.max_instance = checkCMA();
        if(encode_manager.max_instance>0){
            encode_debug_level(LOG_LEVEL_DEBUG, "amvenc_avc  check CMA pool sucess, max instance: %d.\n", encode_manager.max_instance);
        }else{
            encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc  CMA pool too small.\n");
        }
        encode_manager.check_cma = true;
    }
#endif

    wq = create_encode_work_queue();
    if(wq == NULL){
        encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc create instance fail.\n");
        return -EBUSY;
    }

#ifdef CONFIG_CMA
    if(encode_manager.use_reserve == false){
        wq->mem.venc_pages = dma_alloc_from_contiguous(&encode_manager.this_pdev->dev, (18 * SZ_1M) >> PAGE_SHIFT, 0);
        if(wq->mem.venc_pages)
        {
            wq->mem.buf_start = page_to_phys(wq->mem.venc_pages);
            wq->mem.buf_size = 18 * SZ_1M;
            encode_debug_level(LOG_LEVEL_DEBUG, "%s: allocating phys %p, size %dk, wq:%p.\n", __func__, (void *)wq->mem.buf_start, wq->mem.buf_size >> 10, (void *)wq);
        }
        else
        {
            encode_debug_level(LOG_LEVEL_ERROR, "CMA failed to allocate dma buffer for %s, wq:%p.\n", encode_manager.this_pdev->name, (void*)wq);
            destroy_encode_work_queue(wq);
            return -ENOMEM;
        }
    }
#endif

    if((wq->mem.buf_start == 0)||(wq->mem.buf_size<amvenc_buffspec[wq->mem.cur_buf_lev].min_buffsize)){
        encode_debug_level(LOG_LEVEL_ERROR, "alloc mem failed, start: 0x%x, size:0x%x, wq:%p.\n", wq->mem.buf_start,wq->mem.buf_size,(void*)wq);
        destroy_encode_work_queue(wq);
        return -ENOMEM;
    }

    wq->mem.cur_buf_lev = AMVENC_BUFFER_LEVEL_1080P;
    memcpy(&wq->mem.bufspec, &amvenc_buffspec[wq->mem.cur_buf_lev], sizeof(BuffInfo_t));
    wq->mem.inter_bits_info_ddr_start_addr = wq->mem.buf_start+wq->mem.bufspec.inter_bits_info.buf_start; // 32 bytes alignment
    wq->mem.inter_mv_info_ddr_start_addr  = wq->mem.buf_start+wq->mem.bufspec.inter_mv_info.buf_start;
    wq->mem.intra_bits_info_ddr_start_addr = wq->mem.buf_start+wq->mem.bufspec.intra_bits_info.buf_start; // 32 bytes alignment
    wq->mem.intra_pred_info_ddr_start_addr  = wq->mem.buf_start+wq->mem.bufspec.intra_pred_info.buf_start;
    wq->mem.sw_ctl_info_start_addr = wq->mem.buf_start+wq->mem.bufspec.qp_info.buf_start;
#ifdef USE_VDEC2
    wq->mem.vdec2_start_addr = wq->mem.buf_start+wq->mem.bufspec.vdec2_info.buf_start;
#endif
    encode_debug_level(LOG_LEVEL_DEBUG,"amvenc_avc  memory config sucess, buff start:0x%x, size is 0x%x, wq:%p.\n",wq->mem.buf_start, wq->mem.buf_size,(void*)wq);

    file->private_data = (void*) wq;
    return r;
}

static int amvenc_avc_release(struct inode *inode, struct file *file)
{
    encode_wq_t* wq = (encode_wq_t*)file->private_data;
    if(wq){
        encode_debug_level(LOG_LEVEL_DEBUG, "avc release, wq:%p\n", (void*)wq);
        destroy_encode_work_queue(wq);
    }
    return 0;
}

static long amvenc_avc_ioctl(struct file *file,
                           unsigned int cmd, ulong arg)
{
    int r = 0;
    unsigned amrisc_cmd = 0;
    encode_wq_t* wq = file->private_data;
    #define MAX_ADDR_INFO_SIZE 30
    unsigned addr_info[MAX_ADDR_INFO_SIZE + 4];
    ulong argV;
    unsigned buf_start;
    int canvas = -1;
    canvas_t dst;
    switch (cmd) {
        case AMVENC_AVC_IOC_GET_ADDR:
            if((wq->mem.ref_buf_canvas & 0xff) == (ENC_CANVAS_OFFSET)){
                put_user(1,(int *)arg);
            }else{
                put_user(2,(int *)arg);
            }
            break;
        case AMVENC_AVC_IOC_INPUT_UPDATE:
            if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE*sizeof(unsigned))){
                encode_debug_level(LOG_LEVEL_ERROR,"avc update input ptr error, wq: %p.\n", (void*)wq);
                return -1;
            }

            wq->control.dct_buffer_write_ptr = addr_info[2];
            if((encode_manager.current_wq == wq)&&(wq->control.can_update == true)){
                buf_start = getbuffer(wq, addr_info[0]);
                if(buf_start)
                    dma_flush(buf_start + wq->control.dct_flush_start, wq->control.dct_buffer_write_ptr - wq->control.dct_flush_start);  // may be move flush operation to process request function
                WRITE_HREG(QDCT_MB_WR_PTR, (wq->mem.dct_buff_start_addr+ wq->control.dct_buffer_write_ptr));
                wq->control.dct_flush_start =  wq->control.dct_buffer_write_ptr;
            }
            wq->control.finish = (addr_info[3] == 1)?true:false;
            break;
        case AMVENC_AVC_IOC_NEW_CMD:
            if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE*sizeof(unsigned))){
                encode_debug_level(LOG_LEVEL_ERROR,"avc get new cmd error, wq:%p.\n",(void*)wq);
                return -1;
            }
            r = convert_request(wq, addr_info);
            if(r == 0)
                r = encode_wq_add_request(wq);
            if(r)
                encode_debug_level(LOG_LEVEL_ERROR,"avc add new request error, wq:%p.\n",(void*)wq);
            break;
        case AMVENC_AVC_IOC_GET_STAGE:
            put_user(wq->hw_status,(int *)arg);
            break;
        case AMVENC_AVC_IOC_GET_OUTPUT_SIZE:
            put_user(wq->output_size,(int *)arg);
            break;
        case AMVENC_AVC_IOC_CONFIG_INIT:
            if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE*sizeof(unsigned))){
                encode_debug_level(LOG_LEVEL_ERROR,"avc config init error, wq:%p.\n",(void*)wq);
                return -1;
            }
            if(addr_info[0] <= UCODE_MODE_SW_MIX)
               wq->ucode_index = addr_info[0];
            else
               wq->ucode_index = UCODE_MODE_FULL;

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
            wq->ucode_index = UCODE_MODE_FULL;
#endif

#ifdef MULTI_SLICE_MC
            wq->pic.rows_per_slice = addr_info[1];
            encode_debug_level(LOG_LEVEL_DEBUG,"avc init -- rows_per_slice: %d, wq: %p.\n", wq->pic.rows_per_slice, (void*)wq);
#endif
            encode_debug_level(LOG_LEVEL_DEBUG,"avc init as mode %d, wq: %p.\n",wq->ucode_index, (void*)wq);

            if((addr_info[2]>wq->mem.bufspec.max_width)||(addr_info[3]>wq->mem.bufspec.max_height)){
                encode_debug_level(LOG_LEVEL_ERROR,"avc config init- encode size %dx%d is larger than supported (%dx%d).  wq:%p.\n",addr_info[2], addr_info[3], wq->mem.bufspec.max_width, wq->mem.bufspec.max_height,(void*)wq);
                return -1;
            }
            wq->pic.encoder_width = addr_info[2];
            wq->pic.encoder_height= addr_info[3];

            avc_buffspec_init(wq);
            up(&encode_manager.event.request_in_sem) ;
            //avc_init();
            addr_info[1] = wq->mem.bufspec.dct.buf_start;
            addr_info[2] = wq->mem.bufspec.dct.buf_size;
            addr_info[3] = wq->mem.bufspec.dec0_y.buf_start;
            addr_info[4] = wq->mem.bufspec.dec0_y.buf_size;
            addr_info[5] = wq->mem.bufspec.dec0_uv.buf_start;
            addr_info[6] = wq->mem.bufspec.dec0_uv.buf_size;
            addr_info[7] = wq->mem.bufspec.dec1_y.buf_start;
            addr_info[8] = wq->mem.bufspec.dec1_y.buf_size;
            addr_info[9] = wq->mem.bufspec.dec1_uv.buf_start;
            addr_info[10] = wq->mem.bufspec.dec1_uv.buf_size;
            addr_info[11] = wq->mem.bufspec.bitstream.buf_start;
            addr_info[12] = wq->mem.bufspec.bitstream.buf_size;
            addr_info[13] = wq->mem.bufspec.inter_bits_info.buf_start;
            addr_info[14] = wq->mem.bufspec.inter_bits_info.buf_size;
            addr_info[15] = wq->mem.bufspec.inter_mv_info.buf_start;
            addr_info[16] = wq->mem.bufspec.inter_mv_info.buf_size;
            addr_info[17] = wq->mem.bufspec.intra_bits_info.buf_start;
            addr_info[18] = wq->mem.bufspec.intra_bits_info.buf_size;
            addr_info[19] = wq->mem.bufspec.intra_pred_info.buf_start;
            addr_info[20] = wq->mem.bufspec.intra_pred_info.buf_size;
            addr_info[21] = wq->mem.bufspec.qp_info.buf_start;
            addr_info[22] = wq->mem.bufspec.qp_info.buf_size;
            r = copy_to_user((unsigned *)arg, addr_info , 23*sizeof(unsigned));
            break;
        case AMVENC_AVC_IOC_FLUSH_CACHE:
            if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE*sizeof(unsigned))){
                encode_debug_level(LOG_LEVEL_ERROR,"avc fluch cache error, wq: %p.\n", (void*)wq);
                return -1;
            }
            buf_start = getbuffer(wq, addr_info[0]);
            if(buf_start)
                dma_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
            break;
        case AMVENC_AVC_IOC_FLUSH_DMA:
            if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE*sizeof(unsigned))){
                encode_debug_level(LOG_LEVEL_ERROR,"avc fluch dma error, wq:%p.\n", (void*)wq);
                return -1;
            }
            buf_start = getbuffer(wq, addr_info[0]);
            if(buf_start)
                cache_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
            break;
        case AMVENC_AVC_IOC_GET_BUFFINFO:
            put_user(wq->mem.buf_size,(unsigned *)arg);
            break;
        case AMVENC_AVC_IOC_GET_DEVINFO:
            r = copy_to_user((char *)arg,AMVENC_DEV_VERSION,strlen(AMVENC_DEV_VERSION));
            break;
        case AMVENC_AVC_IOC_SUBMIT_ENCODE_DONE:
            get_user(amrisc_cmd,((unsigned*)arg));
            if(amrisc_cmd == ENCODER_IDR){
                wq->pic.idr_pic_id ++;
                if(wq->pic.idr_pic_id > 65535)
                    wq->pic.idr_pic_id = 0;
                wq->pic.pic_order_cnt_lsb = 2;
                wq->pic.frame_number = 1;
            }else if(amrisc_cmd == ENCODER_NON_IDR){
                wq->pic.frame_number ++;
                wq->pic.pic_order_cnt_lsb += 2;
                if(wq->pic.frame_number > 65535)
                    wq->pic.frame_number = 0;
            }
            amrisc_cmd = wq->mem.dblk_buf_canvas;
            wq->mem.dblk_buf_canvas = wq->mem.ref_buf_canvas;
            wq->mem.ref_buf_canvas = amrisc_cmd;   //current dblk buffer as next reference buffer
            break;
        case AMVENC_AVC_IOC_READ_CANVAS:
            get_user(argV,((unsigned*)arg));
            canvas = argV;
            if(canvas&0xff){
                canvas_read(canvas&0xff,&dst);
                addr_info[0] = dst.addr;
                if((canvas&0xff00)>>8)
                    canvas_read((canvas&0xff00)>>8,&dst);
                if((canvas&0xff0000)>>16)
                    canvas_read((canvas&0xff0000)>>16,&dst);
                addr_info[1] = dst.addr - addr_info[0] +dst.width*dst.height;
            }else{
                addr_info[0] = 0;
                addr_info[1] = 0;
            }
            r = copy_to_user((unsigned *)arg, addr_info , 2*sizeof(unsigned));
            break;
        case AMVENC_AVC_IOC_MAX_INSTANCE:
            put_user(encode_manager.max_instance,(unsigned *)arg);
            break;
        default:
            r= -1;
            break;
    }
    return r;
}

static int avc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    encode_wq_t* wq = (encode_wq_t*)filp->private_data;
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vma_size = vma->vm_end - vma->vm_start;

    if (vma_size == 0) {
        encode_debug_level(LOG_LEVEL_ERROR,"vma_size is 0, wq:%p.\n", (void*)wq);
        return -EAGAIN;
    }
    if(!off)
        off += wq->mem.buf_start;
    encode_debug_level(LOG_LEVEL_ALL,"vma_size is %d , off is %ld, wq:%p.\n" , vma_size ,off, (void*)wq);
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_IO;
    //vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                        vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        encode_debug_level(LOG_LEVEL_ERROR,"set_cached: failed remap_pfn_range, wq:%p.\n", (void*)wq);
        return -EAGAIN;
    }
    return 0;

}

static unsigned int amvenc_avc_poll(struct file *file, poll_table *wait_table)
{
    encode_wq_t* wq = (encode_wq_t*)file->private_data;
    poll_wait(file, &wq->request_complete, wait_table);

    if (atomic_read(&wq->request_ready)) {
        atomic_dec(&wq->request_ready);
        return POLLIN | POLLRDNORM;
    }

    return 0;
}

const static struct file_operations amvenc_avc_fops = {
    .owner    = THIS_MODULE,
    .open     = amvenc_avc_open,
    .mmap     = avc_mmap,
    .release  = amvenc_avc_release,
    .unlocked_ioctl    = amvenc_avc_ioctl,
    .poll     = amvenc_avc_poll,
};

// work queue function
static int encode_process_request(encode_manager_t*  manager, encode_queue_item_t *pitem)
{
    int ret=0;
    encode_wq_t* wq = pitem->request.parent;
    encode_request_t* request = &pitem->request;
    u32 timeout = (request->timeout == 0)?1:msecs_to_jiffies(request->timeout);
    unsigned buf_start = 0;
    unsigned size = 0;

    if(((request->cmd == ENCODER_IDR)||(request->cmd == ENCODER_NON_IDR))&&(request->ucode_mode == UCODE_MODE_SW_MIX)){
        if(request->flush_flag & AMVENC_FLUSH_FLAG_QP){
            buf_start = getbuffer(wq, ENCODER_BUFFER_QP);
            if((buf_start)&&(request->qp_info_size>0))
               dma_flush(buf_start, request->qp_info_size);
        }
    }

Again:
    amvenc_avc_start_cmd(wq, request);

    while(wq->control.finish == 0)
        wait_event_interruptible_timeout(manager->event.hw_complete, (wq->control.finish == true), msecs_to_jiffies(1));

    if((wq->control.finish == true)&&(wq->control.dct_flush_start<wq->control.dct_buffer_write_ptr)){
        buf_start = getbuffer(wq, ENCODER_BUFFER_INPUT);
        if(buf_start)
            dma_flush(buf_start + wq->control.dct_flush_start, wq->control.dct_buffer_write_ptr - wq->control.dct_flush_start);  // may be move flush operation to process request function
        WRITE_HREG(QDCT_MB_WR_PTR, (wq->mem.dct_buff_start_addr+ wq->control.dct_buffer_write_ptr));
        wq->control.dct_flush_start = wq->control.dct_buffer_write_ptr;
    }

    if(no_timeout){
        wait_event_interruptible(manager->event.hw_complete, 
          ((manager->encode_hw_status == ENCODER_IDR_DONE)
          ||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)
          ||(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)
          ||(manager->encode_hw_status == ENCODER_PICTURE_DONE)));
    }else{
        wait_event_interruptible_timeout(manager->event.hw_complete, 
          ((manager->encode_hw_status == ENCODER_IDR_DONE)
          ||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)
          ||(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)
          ||(manager->encode_hw_status == ENCODER_PICTURE_DONE)),
          timeout);
    }

    if((request->cmd == ENCODER_SEQUENCE)&&(manager->encode_hw_status == ENCODER_SEQUENCE_DONE)){
        wq->sps_size = READ_HREG(VLC_TOTAL_BYTES);
        wq->hw_status = manager->encode_hw_status;
        request->cmd = ENCODER_PICTURE;
        goto Again;
    }else if((request->cmd == ENCODER_PICTURE)&&(manager->encode_hw_status == ENCODER_PICTURE_DONE)){
        wq->pps_size = READ_HREG(VLC_TOTAL_BYTES) - wq->sps_size;
        wq->hw_status = manager->encode_hw_status;
        if(request->flush_flag & AMVENC_FLUSH_FLAG_OUTPUT){
            buf_start = getbuffer(wq, ENCODER_BUFFER_OUTPUT);
            if(buf_start)
                cache_flush(buf_start,wq->sps_size+wq->pps_size);
        }
        wq->output_size = (wq->sps_size<<16)|wq->pps_size;
    }else{
        wq->hw_status = manager->encode_hw_status;
        if((manager->encode_hw_status == ENCODER_IDR_DONE)||(manager->encode_hw_status == ENCODER_NON_IDR_DONE)){
            wq->output_size = READ_HREG(VLC_TOTAL_BYTES);
            if(request->flush_flag & AMVENC_FLUSH_FLAG_OUTPUT){
                buf_start = getbuffer(wq, ENCODER_BUFFER_OUTPUT);
                if(buf_start)
                    cache_flush(buf_start, wq->output_size);
            }
            if(request->flush_flag & AMVENC_FLUSH_FLAG_INTER_INFO){
                buf_start = getbuffer(wq, ENCODER_BUFFER_INTER_INFO);
                size = wq->mem.inter_mv_info_ddr_start_addr-wq->mem.inter_bits_info_ddr_start_addr+wq->mem.bufspec.inter_mv_info.buf_size;
                if(buf_start)
                    cache_flush(buf_start, size);
            }
            if(request->flush_flag & AMVENC_FLUSH_FLAG_INTRA_INFO){
                buf_start = getbuffer(wq, ENCODER_BUFFER_INTRA_INFO);
                size = wq->mem.intra_pred_info_ddr_start_addr-wq->mem.intra_bits_info_ddr_start_addr+wq->mem.bufspec.intra_pred_info.buf_size;
                if(buf_start)
                    cache_flush(buf_start, size);
            }
            if(request->flush_flag & AMVENC_FLUSH_FLAG_REFERENCE){
                u32 ref_id = ENCODER_BUFFER_REF0;
                u32 flush_size = ((wq->pic.encoder_width+31)>>5<<5)*((wq->pic.encoder_height+15)>>4<<4)*3/2;
                if((wq->mem.ref_buf_canvas & 0xff) == (ENC_CANVAS_OFFSET)){
                    ref_id = ENCODER_BUFFER_REF0;
                }else{
                    ref_id = ENCODER_BUFFER_REF1;
                }
                buf_start = getbuffer(wq, ref_id);
                if(buf_start)
                    cache_flush(buf_start, flush_size);
            }
        }else{
            manager->encode_hw_status = ENCODER_ERROR;
            amvenc_avc_light_reset(wq, 30);
        }
    }

    wq->control.can_update = false;
    wq->control.dct_buffer_write_ptr = 0;
    wq->control.dct_flush_start = 0;
    wq->control.finish = false;
    atomic_inc(&wq->request_ready);
    wake_up_interruptible(&wq->request_complete);
    return ret;	
}

int encode_wq_add_request(encode_wq_t *wq)
{
    encode_queue_item_t  *pitem = NULL;
    struct list_head *head = NULL;
    encode_wq_t* tmp = NULL;
    bool find = false;

    spin_lock(&encode_manager.event.sem_lock);

    head=&encode_manager.wq;
    list_for_each_entry(tmp, head, list){
        if((wq == tmp)&&(wq !=NULL)){
            find = true;
            break;
        }
    }

    if(find == false){
        encode_debug_level(LOG_LEVEL_ERROR, "current wq (%p) doesn't register.\n",(void*)wq);
        goto error;
    }

    if(list_empty(&encode_manager.free_queue)){
        encode_debug_level(LOG_LEVEL_ERROR, "work queue no space, wq:%p.\n",(void*)wq);
        goto error;
    }

    pitem=list_entry(encode_manager.free_queue.next,encode_queue_item_t,list); 
    if(IS_ERR(pitem)){
        goto error;
    }

    memcpy(&pitem->request, &wq->request, sizeof(encode_request_t));
    memset(&wq->request, 0, sizeof(encode_request_t));
    wq->hw_status = 0;
    wq->output_size = 0;
    wq->control.dct_buffer_write_ptr = 0;
    wq->control.dct_flush_start = 0;
    wq->control.finish = false;
    wq->control.can_update = false;
    pitem->request.parent = wq;
    list_move_tail(&pitem->list,&encode_manager.process_queue);
    spin_unlock(&encode_manager.event.sem_lock);

    encode_debug_level(LOG_LEVEL_INFO, "add new work ok, cmd:%d, ucode mode: %d, wq:%p.\n", pitem->request.cmd, pitem->request.ucode_mode, (void*)wq); 
    up(&encode_manager.event.request_in_sem) ;//new cmd come in	
    return 0;
error:
    spin_unlock(&encode_manager.event.sem_lock);
    return -1;	
}

encode_wq_t* create_encode_work_queue(void)
{
    encode_wq_t *encode_work_queue = NULL;
    bool done = false;
    int i;

    encode_work_queue=kzalloc(sizeof(encode_wq_t), GFP_KERNEL);
    if(IS_ERR(encode_work_queue)){
        encode_debug_level(LOG_LEVEL_ERROR, "can't create work queue\n");
        return NULL;
    }
    encode_work_queue->pic.init_qppicture = 26;
    encode_work_queue->pic.log2_max_frame_num = 4;
    encode_work_queue->pic.log2_max_pic_order_cnt_lsb= 4;
    encode_work_queue->pic.idr_pic_id = 0;
    encode_work_queue->pic.frame_number = 0;
    encode_work_queue->pic.pic_order_cnt_lsb = 0;
    encode_work_queue->ucode_index = UCODE_MODE_FULL;
    init_waitqueue_head (&encode_work_queue->request_complete);
    atomic_set(&encode_work_queue->request_ready, 0);
    spin_lock(&encode_manager.event.sem_lock);
    if(encode_manager.wq_count<encode_manager.max_instance){
        list_add_tail(&encode_work_queue->list, &encode_manager.wq);
        encode_manager.wq_count++;
        if(encode_manager.use_reserve == true){
            for(i = 0;i<encode_manager.max_instance; i++){
                if(encode_manager.reserve_buff[i].used == false){
                    encode_work_queue->mem.buf_start = encode_manager.reserve_buff[i].buf_start;
                    encode_work_queue->mem.buf_size= encode_manager.reserve_buff[i].buf_size;
                    encode_manager.reserve_buff[i].used = true;
                    done = true;
                    break;
                }
            }
        }else{
            done = true;
        }
    }
    spin_unlock(&encode_manager.event.sem_lock);
    if(done == false){
        kfree(encode_work_queue);
        encode_work_queue = NULL;
        encode_debug_level(LOG_LEVEL_ERROR, "too many work queue!\n");
    }
    return encode_work_queue; //find it 
}

int destroy_encode_work_queue(encode_wq_t* encode_work_queue)
{
    encode_queue_item_t *pitem,*tmp;
    encode_wq_t *wq = NULL, *wp_tmp = NULL;
    int i;
    bool find = false;

    struct list_head *head;
    if (encode_work_queue){
        spin_lock(&encode_manager.event.sem_lock);
        if(encode_manager.current_wq == encode_work_queue){
            encode_manager.remove_flag= true;
            spin_unlock(&encode_manager.event.sem_lock);
            encode_debug_level(LOG_LEVEL_DEBUG, "warning--Destory the running queue, should not be here.\n");
            wait_for_completion(&encode_manager.event.process_complete);
            spin_lock(&encode_manager.event.sem_lock);
        }//else we can delete it safely.
       
        head=&encode_manager.process_queue;
        list_for_each_entry_safe(pitem,tmp,head,list){
            if(pitem){
                if(pitem->request.parent == encode_work_queue){
                    pitem->request.parent = NULL;
                    encode_debug_level(LOG_LEVEL_DEBUG, "warning--remove not process request, should not be here.\n");
                    list_move_tail(&pitem->list,&encode_manager.free_queue);
                }
            }
        }

        head=&encode_manager.wq;
        list_for_each_entry_safe(wq,wp_tmp,head,list){
            if((wq)&&(wq == encode_work_queue)){
                list_del(&wq->list);
                if(encode_manager.use_reserve == true){
                    for(i = 0;i<encode_manager.max_instance; i++){
                        if((encode_manager.reserve_buff[i].used == true)&&(encode_work_queue->mem.buf_start = encode_manager.reserve_buff[i].buf_start)){
                            encode_manager.reserve_buff[i].used = false;
                            break;
                        }
                    }
                }
                find = true;
                encode_manager.wq_count--;
                encode_debug_level(LOG_LEVEL_DEBUG, "remove  encode_work_queue %p sucess, %s line %d.\n",(void*)encode_work_queue, __func__,__LINE__);
                break;
            }
        }
        spin_unlock(&encode_manager.event.sem_lock);
#ifdef CONFIG_CMA
        if(encode_work_queue->mem.venc_pages){
            dma_release_from_contiguous(&encode_manager.this_pdev->dev, wq->mem.venc_pages, (18 * SZ_1M)>>PAGE_SHIFT);
            encode_work_queue->mem.venc_pages = NULL;
        }
#endif
        kfree(encode_work_queue);
        up(&encode_manager.event.request_in_sem);
    }
    return  0;	
}

static int encode_monitor_thread(void *data)
{
    encode_manager_t*  manager = (encode_manager_t*)data ;
    encode_queue_item_t *pitem = NULL;
    struct sched_param param = {.sched_priority = MAX_RT_PRIO - 1 };
    int ret = 0;

    encode_debug_level(LOG_LEVEL_DEBUG, "encode workqueue monitor start.\n");
    sched_setscheduler(current, SCHED_FIFO, &param);
    allow_signal(SIGTERM);
    //setup current_wq here.
    while(manager->process_queue_state!=ENCODE_PROCESS_QUEUE_STOP){
        if(kthread_should_stop())
            break;

        ret = down_interruptible(&manager->event.request_in_sem);

        if (kthread_should_stop())
            break;
        if(manager->inited == false){
            spin_lock(&manager->event.sem_lock);
            if(!list_empty(&manager->wq)){
	         encode_wq_t* first_wq = list_entry(manager->wq.next,encode_wq_t,list);
                manager->current_wq = first_wq;
                spin_unlock(&manager->event.sem_lock);
                if(first_wq){
                    avc_init(first_wq);
                    manager->inited = true;
                }
                spin_lock(&manager->event.sem_lock);
                manager->current_wq = NULL;
                spin_unlock(&manager->event.sem_lock);
                if(manager->remove_flag){
                    complete(&manager->event.process_complete);
                    manager->remove_flag = false;
                }
            }else{
                spin_unlock(&manager->event.sem_lock);
            }
            continue;
        }

        spin_lock(&manager->event.sem_lock);
        pitem = NULL;
        if(list_empty(&manager->wq)){
            spin_unlock(&manager->event.sem_lock);
            manager->inited = false;
            amvenc_avc_stop();
            encode_debug_level(LOG_LEVEL_DEBUG, "power off encode.\n");
            continue;
        }else if(!list_empty(&manager->process_queue)){
            pitem=list_entry(manager->process_queue.next,encode_queue_item_t,list);
            list_del(&pitem->list);
            manager->current_item = pitem;
            manager->current_wq = pitem->request.parent;
        }
        spin_unlock(&manager->event.sem_lock);

        if(pitem){
            encode_process_request(manager,pitem);
            spin_lock(&manager->event.sem_lock);
            list_add_tail(&pitem->list, &manager->free_queue);
            manager->current_item = NULL;
            manager->last_wq = manager->current_wq;
            manager->current_wq = NULL;
            spin_unlock(&manager->event.sem_lock);
        }
        if(manager->remove_flag){
            complete(&manager->event.process_complete);
            manager->remove_flag = false;
        }
    }
    while(!kthread_should_stop())
        msleep(10);

    encode_debug_level(LOG_LEVEL_DEBUG, "exit encode_monitor_thread.\n");
    return 0;
}

static int encode_start_monitor(void)
{
    int ret =0;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESONG9TV
    clock_level = 3;
#elif MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        clock_level = 3;
    }else{
        clock_level = 1;
    }
#else
    clock_level = 1;
#endif

    encode_debug_level(LOG_LEVEL_DEBUG, "encode start monitor.\n");
    encode_manager.process_queue_state=ENCODE_PROCESS_QUEUE_START;
    encode_manager.encode_thread=kthread_run(encode_monitor_thread,&encode_manager,"encode_monitor");
    if (IS_ERR(encode_manager.encode_thread)){
        ret = PTR_ERR(encode_manager.encode_thread);
        encode_manager.process_queue_state=ENCODE_PROCESS_QUEUE_STOP;
        encode_debug_level(LOG_LEVEL_ERROR, "encode monitor : failed to start kthread (%d)\n", ret);
    }
    return ret;
}

static int  encode_stop_monitor(void)
{
    encode_debug_level(LOG_LEVEL_DEBUG, "stop encode monitor thread\n");
    if(encode_manager.encode_thread){
        spin_lock(&encode_manager.event.sem_lock);
        if(!list_empty(&encode_manager.wq)){
            int count = encode_manager.wq_count;
            spin_unlock(&encode_manager.event.sem_lock);
            encode_debug_level(LOG_LEVEL_ERROR, "stop encode monitor thread error, active wq (%d) is not 0.\n", count);
            return -1;
        }
        spin_unlock(&encode_manager.event.sem_lock);
        encode_manager.process_queue_state =ENCODE_PROCESS_QUEUE_STOP;
        send_sig(SIGTERM, encode_manager.encode_thread, 1);
        up(&encode_manager.event.request_in_sem) ;
        kthread_stop(encode_manager.encode_thread);
        encode_manager.encode_thread = NULL;
    }
    return  0;
}

static int encode_wq_init(void)
{
    int i = 0;
    encode_queue_item_t *pitem = NULL;

    encode_debug_level(LOG_LEVEL_DEBUG, "encode_wq_init.\n");    
    encode_manager.irq_num = -1;

    spin_lock_init(&encode_manager.event.sem_lock);
    sema_init (&encode_manager.event.request_in_sem,0); 
    init_waitqueue_head (&encode_manager.event.hw_complete);
    init_completion(&encode_manager.event.process_complete);
    INIT_LIST_HEAD(&encode_manager.process_queue);
    INIT_LIST_HEAD(&encode_manager.free_queue);
    INIT_LIST_HEAD(&encode_manager.wq);

    tasklet_init(&encode_manager.encode_tasklet, encode_isr_tasklet, (ulong)&encode_manager);

    for(i=0;i<MAX_ENCODE_REQUEST;i++){
        pitem=(encode_queue_item_t*)kcalloc(1,sizeof(encode_queue_item_t),GFP_KERNEL);
        if(IS_ERR(pitem)){
            encode_debug_level(LOG_LEVEL_ERROR, "can't request queue item memory.\n");
            return -1;
        }
        pitem->request.parent = NULL;
        list_add_tail(&pitem->list, &encode_manager.free_queue) ;
    }
    encode_manager.current_wq = NULL;
    encode_manager.last_wq = NULL;
    encode_manager.encode_thread = NULL;
    encode_manager.current_item = NULL;
    encode_manager.wq_count = 0;
    encode_manager.remove_flag = false;
    if(encode_start_monitor()){
        encode_debug_level(LOG_LEVEL_ERROR, "encode create thread error.\n");	
        return -1;
    }	
    return 0;
}

static int encode_wq_uninit(void)
{
    encode_queue_item_t *pitem,*tmp;
    struct list_head *head;
    int count = 0;
    int r = -1;
    encode_debug_level(LOG_LEVEL_DEBUG, "uninit encode wq.\n") ;
    if(encode_stop_monitor() == 0){
        if(encode_manager.irq_num >=0){
            free_irq(INT_AMVENCODER,&encode_manager);
            encode_manager.irq_num = -1;
        }
        spin_lock(&encode_manager.event.sem_lock);
        head=&encode_manager.process_queue;
        list_for_each_entry_safe(pitem,tmp,head,list){
            if(pitem){
                list_del(&pitem->list);
                kfree(pitem);
                count++;
            }
        }
        head=&encode_manager.free_queue;
        list_for_each_entry_safe(pitem,tmp,head,list){
            if(pitem){
                list_del(&pitem->list);
                kfree(pitem);
                count++;
            }
        }
        spin_unlock(&encode_manager.event.sem_lock);
        if(count == MAX_ENCODE_REQUEST)
            r = 0;
        else
            encode_debug_level(LOG_LEVEL_ERROR, "lost  some request item %d.\n",MAX_ENCODE_REQUEST-count);
    }
    return  r;
}

static ssize_t encode_status_show(struct class *cla,struct class_attribute *attr,char *buf)
{
    int process_count=0;
    int free_count = 0;
    encode_queue_item_t *pitem = NULL;
    encode_wq_t* current_wq = NULL;
    encode_wq_t* last_wq = NULL;
    struct list_head *head = NULL;
    int irq_num = 0;
    int hw_status = 0;
    int process_queue_state = 0;
    int wq_count = 0;
    u32 ucode_index;
    bool need_reset;
    bool process_irq;
    bool inited;
    bool use_reserve;
    Buff_t reserve_mem;
    u32 max_instance;
#ifdef CONFIG_CMA
    bool check_cma = false;
#endif

    spin_lock(&encode_manager.event.sem_lock);
    head = &encode_manager.free_queue;
    list_for_each_entry(pitem, head , list){
        free_count++;
        if(free_count>MAX_ENCODE_REQUEST)//error has occured
            break;
    }

    head = &encode_manager.process_queue;
    list_for_each_entry(pitem, head , list){
        process_count++;
        if(free_count>MAX_ENCODE_REQUEST)//error has occured
            break;
    }

    current_wq = encode_manager.current_wq;
    last_wq = encode_manager.last_wq;
    pitem = encode_manager.current_item;
    irq_num = encode_manager.irq_num;
    hw_status = encode_manager.encode_hw_status;
    process_queue_state = encode_manager.process_queue_state;
    wq_count = encode_manager.wq_count;
    ucode_index = encode_manager.ucode_index;
    need_reset = encode_manager.need_reset;
    process_irq = encode_manager.process_irq;
    inited = encode_manager.inited;
    use_reserve = encode_manager.use_reserve;
    if(use_reserve){
        reserve_mem.buf_start = encode_manager.reserve_mem.buf_start;
        reserve_mem.buf_size = encode_manager.reserve_mem.buf_size;
    }
    max_instance = encode_manager.max_instance;
#ifdef CONFIG_CMA
    check_cma = encode_manager.check_cma;
#endif

    spin_unlock(&encode_manager.event.sem_lock);
    
    encode_debug_level(LOG_LEVEL_DEBUG, "encode process queue count: %d, free queue count: %d.\n",process_count, free_count);
    encode_debug_level(LOG_LEVEL_DEBUG, "encode curent wq: %p, last wq: %p, wq count: %d, max_instance: %d.\n",current_wq, last_wq, wq_count,max_instance);
    encode_debug_level(LOG_LEVEL_DEBUG, "encode curent pitem: %p, ucode_index: %d, hw_status: %d, need_reset: %s, process_irq: %s.\n",pitem, ucode_index, hw_status, need_reset?"true":"false",process_irq?"true":"false");
    encode_debug_level(LOG_LEVEL_DEBUG, "encode irq num: %d,  inited: %s, process_queue_state: %d.\n",irq_num, inited?"true":"false",  process_queue_state);
    if(use_reserve){
        encode_debug_level(LOG_LEVEL_DEBUG, "encode use reserve memory, buffer start: %d, size: %d.\n",reserve_mem.buf_start, reserve_mem.buf_size);
    }else{
#ifdef CONFIG_CMA
        encode_debug_level(LOG_LEVEL_DEBUG, "encode check cma: %s.\n",check_cma?"true":"false");
#endif
    }
    return snprintf(buf,40,"encode max instance: %d\n", max_instance);
}

static struct class_attribute amvenc_class_attrs[] = {
    __ATTR(encode_status,
        S_IRUGO | S_IWUSR,
        encode_status_show,
        NULL),
    __ATTR_NULL
};

static struct class amvenc_avc_class = {
	.name = CLASS_NAME,
	.class_attrs = amvenc_class_attrs,
};

int  init_avc_device(void)
{
    int  r =0;
    r =register_chrdev(0,DEVICE_NAME,&amvenc_avc_fops);
    if(r<=0){
        encode_debug_level(LOG_LEVEL_ERROR,"register amvenc_avc device error.\n");
        return  r  ;
    }
    avc_device_major= r ;

    r = class_register(&amvenc_avc_class);
    if(r<0){
        encode_debug_level(LOG_LEVEL_ERROR,"error create amvenc_avc class.\n");
        return r;
    }
    
    amvenc_avc_dev = device_create(&amvenc_avc_class, NULL,
                                  MKDEV(avc_device_major, 0), NULL,
                                  DEVICE_NAME);

    if (IS_ERR(amvenc_avc_dev)){
        encode_debug_level(LOG_LEVEL_ERROR,"create amvenc_avc device error.\n");
        class_unregister(&amvenc_avc_class);
        return -1 ;
    }
    return r;
}

int uninit_avc_device(void)
{
    if(amvenc_avc_dev)
        device_destroy(&amvenc_avc_class, MKDEV(avc_device_major, 0));

    class_destroy(&amvenc_avc_class);

    unregister_chrdev(avc_device_major, DEVICE_NAME);
    return 0;
}

static int amvenc_avc_probe(struct platform_device *pdev)
{
    struct resource mem;
    int idx;

    encode_debug_level(LOG_LEVEL_INFO, "amvenc_avc probe start.\n");

#ifdef CONFIG_CMA
    encode_manager.this_pdev = pdev;
    encode_manager.check_cma = false;
#endif
    encode_manager.reserve_mem.buf_start = 0;
    encode_manager.reserve_mem.buf_size= 0;
    encode_manager.use_reserve = false;
    encode_manager.max_instance = 0;
    encode_manager.reserve_buff = NULL;

    idx = find_reserve_block(pdev->dev.of_node->name,0);
    if(idx < 0){
        encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc memory resource undefined.\n");
    }else{
        mem.start = (phys_addr_t)get_reserve_block_addr(idx);
        mem.end = mem.start+ (phys_addr_t)get_reserve_block_size(idx)-1;
        encode_manager.reserve_mem.buf_start = mem.start;
        encode_manager.reserve_mem.buf_size = mem.end - mem.start + 1;
        
        if(encode_manager.reserve_mem.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize){
            encode_manager.max_instance = encode_manager.reserve_mem.buf_size/amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize;
            if(encode_manager.max_instance>MAX_ENCODE_INSTANCE)
                encode_manager.max_instance = MAX_ENCODE_INSTANCE;
            encode_manager.reserve_buff = (Buff_t*)kzalloc(encode_manager.max_instance*sizeof(Buff_t), GFP_KERNEL);
            if(encode_manager.reserve_buff){
                int i = 0;
                for(i = 0; i < encode_manager.max_instance;i++){
                    encode_manager.reserve_buff[i].buf_start = i*amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize+encode_manager.reserve_mem.buf_start;
                    encode_manager.reserve_buff[i].buf_size = encode_manager.reserve_mem.buf_start;
                    encode_manager.reserve_buff[i].used = false;
                }
                encode_manager.use_reserve = true;
                encode_debug_level(LOG_LEVEL_DEBUG, "amvenc_avc  use reserve memory, buff start: 0x%x, size: 0x%x,  max instance is %d\n",encode_manager.reserve_mem.buf_start,encode_manager.reserve_mem.buf_size,encode_manager.max_instance);
            }else{
                encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc alloc reserve buffer pointer fail. max instance is %d.\n",encode_manager.max_instance);
                encode_manager.max_instance = 0;
                encode_manager.reserve_mem.buf_start = 0;
                encode_manager.reserve_mem.buf_size= 0;
            }
        }else{
            encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc memory resource too small, size is 0x%x. Need 0x%x bytes at least.\n",encode_manager.reserve_mem.buf_size, amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize);
            encode_manager.reserve_mem.buf_start = 0;
            encode_manager.reserve_mem.buf_size= 0;
        }
    }
#ifndef CONFIG_CMA
    if(encode_manager.use_reserve == false){
        encode_debug_level(LOG_LEVEL_ERROR, "amvenc_avc memory is invaild, probe fail!\n");
        return -EFAULT;
    }
#endif
    if (encode_wq_init()){
        if(encode_manager.reserve_buff){
            kfree(encode_manager.reserve_buff);
            encode_manager.reserve_buff = NULL;
        }
        encode_debug_level(LOG_LEVEL_ERROR, "encode work queue init error .\n");	
        return -EFAULT;	
    }

    init_avc_device();
    encode_debug_level(LOG_LEVEL_INFO, "amvenc_avc probe end.\n");
    return 0;
}

static int amvenc_avc_remove(struct platform_device *pdev)
{
    if(encode_manager.reserve_buff){
        kfree(encode_manager.reserve_buff);
        encode_manager.reserve_buff = NULL;
    }
    if (encode_wq_uninit()){
        encode_debug_level(LOG_LEVEL_ERROR, "encode work queue uninit error.\n");	
    }
    uninit_avc_device();
    encode_debug_level(LOG_LEVEL_INFO, "amvenc_avc remove.\n");
    return 0;
}

/****************************************/

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_avcenc_dt_match[]={
	{	.compatible = "amlogic,amvenc_avc",
	},
	{},
};
#else
#define amlogic_avcenc_dt_match NULL
#endif

static struct platform_driver amvenc_avc_driver = {
    .probe      = amvenc_avc_probe,
    .remove     = amvenc_avc_remove,
    .driver     = {
        .name   = DRIVER_NAME,
        .of_match_table = amlogic_avcenc_dt_match,
    }
};

static struct codec_profile_t amvenc_avc_profile = {
	.name = "avc",
	.profile = ""
};

static int __init amvenc_avc_driver_init_module(void)
{
    encode_debug_level(LOG_LEVEL_INFO, "amvenc_avc module init\n");

    if (platform_driver_register(&amvenc_avc_driver)) {
        encode_debug_level(LOG_LEVEL_ERROR, "failed to register amvenc_avc driver\n");
        return -ENODEV;
    }
    vcodec_profile_register(&amvenc_avc_profile);
    return 0;
}

static void __exit amvenc_avc_driver_remove_module(void)
{
    encode_debug_level(LOG_LEVEL_INFO, "amvenc_avc module remove.\n");

    platform_driver_unregister(&amvenc_avc_driver);
}

/****************************************/

module_param(me_mv_merge_ctl, uint, 0664);
MODULE_PARM_DESC(me_mv_merge_ctl, "\n me_mv_merge_ctl \n");

module_param(me_step0_close_mv, uint, 0664);
MODULE_PARM_DESC(me_step0_close_mv, "\n me_step0_close_mv \n");

module_param(me_f_skip_sad, uint, 0664);
MODULE_PARM_DESC(me_f_skip_sad, "\n me_f_skip_sad \n");

module_param(me_f_skip_weight, uint, 0664);
MODULE_PARM_DESC(me_f_skip_weight, "\n me_f_skip_weight \n");

module_param(me_mv_weight_01, uint, 0664);
MODULE_PARM_DESC(me_mv_weight_01, "\n me_mv_weight_01 \n");

module_param(me_mv_weight_23, uint, 0664);
MODULE_PARM_DESC(me_mv_weight_23, "\n me_mv_weight_23 \n");

module_param(me_sad_range_inc, uint, 0664);
MODULE_PARM_DESC(me_sad_range_inc, "\n me_sad_range_inc \n");

module_param(me_sad_enough_01, uint, 0664);
MODULE_PARM_DESC(me_sad_enough_01, "\n me_sad_enough_01 \n");

module_param(me_sad_enough_23, uint, 0664);
MODULE_PARM_DESC(me_sad_enough_23, "\n me_sad_enough_23 \n");

module_param(fixed_slice_cfg, uint, 0664);
MODULE_PARM_DESC(fixed_slice_cfg, "\n fixed_slice_cfg \n");

module_param(enable_dblk, uint, 0664);
MODULE_PARM_DESC(enable_dblk, "\n enable_dblk \n");

module_param(clock_level, uint, 0664);
MODULE_PARM_DESC(clock_level, "\n clock_level \n");

module_param(encode_print_level, uint, 0664);
MODULE_PARM_DESC(encode_print_level, "\n encode_print_level \n");

module_param(no_timeout, uint, 0664);
MODULE_PARM_DESC(no_timeout, "\n no_timeout flag for process request \n");

module_init(amvenc_avc_driver_init_module);
module_exit(amvenc_avc_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC AVC Video Encoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("simon.zheng <simon.zheng@amlogic.com>");
