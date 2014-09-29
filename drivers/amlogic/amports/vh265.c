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
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
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

#include <mach/am_regs.h>
#include "vdec_reg.h"

#include "vdec.h"
#include "amvdec.h"
#include "vh265_mc.h"

//#define ERROR_HANDLE_DEBUG
#ifndef STAT_KTHREAD
#define STAT_KTHREAD 0x40
#endif

#define DRIVER_NAME "amvdec_h265"
#define MODULE_NAME "amvdec_h265"

#define PUT_INTERVAL        (HZ/100)
#define ERROR_SYSTEM_RESET_COUNT   200

#define PTS_NORMAL                0
#define PTS_NONE_REF_USE_DURATION 1

#define PTS_MODE_SWITCHING_THRESHOLD           3
#define PTS_MODE_SWITCHING_RECOVERY_THREASHOLD 3

#define DUR2PTS(x) ((x)*90/96)

static int  vh265_vf_states(vframe_states_t *states, void*);
static vframe_t *vh265_vf_peek(void*);
static vframe_t *vh265_vf_get(void*);
static void vh265_vf_put(vframe_t *, void*);
static int vh265_event_cb(int type, void *data, void *private_data);

static void vh265_prot_init(void);
static int vh265_local_init(void);
static void vh265_put_timer_func(unsigned long arg);

static const char vh265_dec_id[] = "vh265-dev";

#define PROVIDER_NAME   "decoder.h265"

static const struct vframe_operations_s vh265_vf_provider = {
    .peek      = vh265_vf_peek,
    .get       = vh265_vf_get,
    .put       = vh265_vf_put,
    .event_cb  = vh265_event_cb,
    .vf_states = vh265_vf_states,
};
static struct vframe_provider_s vh265_vf_prov;

static u32 frame_width, frame_height, frame_dur, frame_ar;
static bool get_frame_dur;
static struct timer_list recycle_timer;
static u32 stat;
static u32 bit_depth_luma;
static u32 bit_depth_chroma;
static u32 error_watchdog_count;
static u32 error_skip_nal_watchdog_count;
static u32 error_system_watchdog_count;

#define H265_DEBUG_BUFMGR                   0x01
#define H265_DEBUG_BUFMGR_MORE              0x02
#define H265_DEBUG_UCODE                    0x04
#define H265_DEBUG_REG                      0x08
#define H265_DEBUG_MAN_SEARCH_NAL           0x10
#define H265_DEBUG_MAN_SKIP_NAL             0x20
#define H265_DEBUG_DISPLAY_CUR_FRAME        0x40
#define H265_DEBUG_FORCE_CLK                0x80
#define H265_DEBUG_SEND_PARAM_WITH_REG      0x100
#define H265_DEBUG_NO_DISPLAY               0x200
#define H265_DEBUG_DISCARD_NAL              0x400
#define H265_DEBUG_OUT_PTS					0x800
#define H265_DEBUG_DIS_LOC_ERROR_PROC       0x10000
#define H265_DEBUG_DIS_SYS_ERROR_PROC   0x20000
#define H265_DEBUG_DUMP_PIC_LIST       0x40000
#define H265_DEBUG_TRIG_SLICE_SEGMENT_PROC 0x80000
#define H265_DEBUG_HW_RESET               0x100000
#define H265_DEBUG_LOAD_UCODE_FROM_FILE   0x200000
#define H265_DEBUG_ERROR_TRIG             0x400000


static u32 debug = 0;
#ifdef ERROR_HANDLE_DEBUG
static u32 dbg_nal_skip_flag = 0;   //bit[0], skip vps; bit[1], skip sps; bit[2], skip pps
static u32 dbg_nal_skip_count = 0;
#endif
/*for debug*/
static u32 decode_stop_pos = 0;
static u32 decode_stop_pos_pre = 0;
static u32 decode_pic_begin = 0;
static uint slice_parse_begin=0;
static u32 step = 0;
/**/
/* 
bit[1:0]PB_skip_mode: 0, start decoding at begin; 1, start decoding after first I;  2, only decode and display none error picture; 3, start decoding and display after IDR,etc
bit[31:16] PB_skip_count_after_decoding (decoding but not display),  only for mode 0 and 1.
 */
static u32 nal_skip_policy = 2;

static u32 use_cma = 1;
static unsigned char init_flag = 0;
static unsigned char uninit_list = 0;

static struct semaphore  h265_sema;
struct task_struct *h265_task = NULL;

/*
error handling
*/
static u32 error_handle_policy = 0;  /* bit 0: 1, wait vps/sps/pps after error recovery; */
static u32 error_skip_nal_count = 6;
static u32 error_handle_threshold = 30;
static u32 error_handle_nal_skip_threshold = 10;
static u32 error_handle_system_threshold = 30;

#define DEBUG_REG
#ifdef DEBUG_REG
void WRITE_VREG_DBG(unsigned adr, unsigned val)
{
    if(debug&H265_DEBUG_REG)
        printk("%s(%x, %x)\n", __func__, adr, val); 
    WRITE_VREG(adr, val);   
}    
#undef WRITE_VREG
#define WRITE_VREG WRITE_VREG_DBG
#endif
#ifdef DEBUG_PTS
static unsigned long pts_missed, pts_hit;
#endif

static struct dec_sysinfo vh265_amstream_dec_info;
extern u32 trickmode_i;

static DEFINE_SPINLOCK(lock);
static int fatal_error;

static DEFINE_MUTEX(vh265_mutex);

static struct device *cma_dev;

/**************************************************

h265 buffer management include

***************************************************/
enum NalUnitType
{
  NAL_UNIT_CODED_SLICE_TRAIL_N = 0,   // 0
  NAL_UNIT_CODED_SLICE_TRAIL_R,   // 1
  
  NAL_UNIT_CODED_SLICE_TSA_N,     // 2
  NAL_UNIT_CODED_SLICE_TLA,       // 3   // Current name in the spec: TSA_R
  
  NAL_UNIT_CODED_SLICE_STSA_N,    // 4
  NAL_UNIT_CODED_SLICE_STSA_R,    // 5

  NAL_UNIT_CODED_SLICE_RADL_N,    // 6
  NAL_UNIT_CODED_SLICE_DLP,       // 7 // Current name in the spec: RADL_R
  
  NAL_UNIT_CODED_SLICE_RASL_N,    // 8
  NAL_UNIT_CODED_SLICE_TFD,       // 9 // Current name in the spec: RASL_R

  NAL_UNIT_RESERVED_10,
  NAL_UNIT_RESERVED_11,
  NAL_UNIT_RESERVED_12,
  NAL_UNIT_RESERVED_13,
  NAL_UNIT_RESERVED_14,
  NAL_UNIT_RESERVED_15,

  NAL_UNIT_CODED_SLICE_BLA,       // 16   // Current name in the spec: BLA_W_LP
  NAL_UNIT_CODED_SLICE_BLANT,     // 17   // Current name in the spec: BLA_W_DLP
  NAL_UNIT_CODED_SLICE_BLA_N_LP,  // 18
  NAL_UNIT_CODED_SLICE_IDR,       // 19  // Current name in the spec: IDR_W_DLP
  NAL_UNIT_CODED_SLICE_IDR_N_LP,  // 20
  NAL_UNIT_CODED_SLICE_CRA,       // 21
  NAL_UNIT_RESERVED_22,
  NAL_UNIT_RESERVED_23,

  NAL_UNIT_RESERVED_24,
  NAL_UNIT_RESERVED_25,
  NAL_UNIT_RESERVED_26,
  NAL_UNIT_RESERVED_27,
  NAL_UNIT_RESERVED_28,
  NAL_UNIT_RESERVED_29,
  NAL_UNIT_RESERVED_30,
  NAL_UNIT_RESERVED_31,

  NAL_UNIT_VPS,                   // 32
  NAL_UNIT_SPS,                   // 33
  NAL_UNIT_PPS,                   // 34
  NAL_UNIT_ACCESS_UNIT_DELIMITER, // 35
  NAL_UNIT_EOS,                   // 36
  NAL_UNIT_EOB,                   // 37
  NAL_UNIT_FILLER_DATA,           // 38
  NAL_UNIT_SEI,                   // 39 Prefix SEI
  NAL_UNIT_SEI_SUFFIX,            // 40 Suffix SEI
  NAL_UNIT_RESERVED_41,
  NAL_UNIT_RESERVED_42,
  NAL_UNIT_RESERVED_43,
  NAL_UNIT_RESERVED_44,
  NAL_UNIT_RESERVED_45,
  NAL_UNIT_RESERVED_46,
  NAL_UNIT_RESERVED_47,
  NAL_UNIT_UNSPECIFIED_48,
  NAL_UNIT_UNSPECIFIED_49,
  NAL_UNIT_UNSPECIFIED_50,
  NAL_UNIT_UNSPECIFIED_51,
  NAL_UNIT_UNSPECIFIED_52,
  NAL_UNIT_UNSPECIFIED_53,
  NAL_UNIT_UNSPECIFIED_54,
  NAL_UNIT_UNSPECIFIED_55,
  NAL_UNIT_UNSPECIFIED_56,
  NAL_UNIT_UNSPECIFIED_57,
  NAL_UNIT_UNSPECIFIED_58,
  NAL_UNIT_UNSPECIFIED_59,
  NAL_UNIT_UNSPECIFIED_60,
  NAL_UNIT_UNSPECIFIED_61,
  NAL_UNIT_UNSPECIFIED_62,
  NAL_UNIT_UNSPECIFIED_63,
  NAL_UNIT_INVALID,
};


//---------------------------------------------------
// Amrisc Software Interrupt 
//---------------------------------------------------
#define AMRISC_STREAM_EMPTY_REQ 0x01
#define AMRISC_PARSER_REQ       0x02
#define AMRISC_MAIN_REQ         0x04

//---------------------------------------------------
// HEVC_DEC_STATUS define
//---------------------------------------------------
#define HEVC_DEC_IDLE                        0
#define HEVC_NAL_UNIT_VPS                    1
#define HEVC_NAL_UNIT_SPS                    2
#define HEVC_NAL_UNIT_PPS                    3
#define HEVC_NAL_UNIT_CODED_SLICE_SEGMENT    4
#define HEVC_CODED_SLICE_SEGMENT_DAT         5
#define HEVC_DUMP_LMEM				7
#define HEVC_SLICE_SEGMENT_DONE  		8
#define HEVC_NAL_SEARCH_DONE			9

#define HEVC_DISCARD_NAL         0xf0
#define HEVC_ACTION_ERROR        0xfe
#define HEVC_ACTION_DONE         0xff



//---------------------------------------------------
// Include "parser_cmd.h"
//---------------------------------------------------
#define PARSER_CMD_SKIP_CFG_0 0x0000090b

#define PARSER_CMD_SKIP_CFG_1 0x1b14140f

#define PARSER_CMD_SKIP_CFG_2 0x001b1910

#define PARSER_CMD_NUMBER 37

static unsigned short parser_cmd[PARSER_CMD_NUMBER] = {
0x0401,
0x8401,
0x0800,
0x0402,
0x9002,
0x1423,
0x8CC3,
0x1423,
0x8804,
0x9825,
0x0800,
0x04FE,
0x8406,
0x8411,
0x1800,
0x8408,
0x8409,
0x8C2A,
0x9C2B,
0x1C00,
0x840F,
0x8407,
0x8000,
0x8408,
0x2000,
0xA800,
0x8410,
0x04DE,
0x840C,
0x840D,
0xAC00,
0xA000,
0x08C0,
0x08E0,
0xA40E,
0xFC00,
0x7C00
};

/**************************************************

h265 buffer management

***************************************************/
//#define BUFFER_MGR_ONLY
//#define CONFIG_HEVC_CLK_FORCED_ON
//#define ENABLE_SWAP_TEST
#define   MCRCC_ENABLE
#define MEM_MAP_MODE 2  // 0:linear 1:32x32 2:64x32 ; m8baby test1902
#define INVALID_POC 0x80000000


#define HEVC_DEC_STATUS_REG       HEVC_ASSIST_SCRATCH_0
#define HEVC_RPM_BUFFER           HEVC_ASSIST_SCRATCH_1
#define HEVC_SHORT_TERM_RPS       HEVC_ASSIST_SCRATCH_2
#define HEVC_VPS_BUFFER           HEVC_ASSIST_SCRATCH_3
#define HEVC_SPS_BUFFER           HEVC_ASSIST_SCRATCH_4
#define HEVC_PPS_BUFFER           HEVC_ASSIST_SCRATCH_5
#define HEVC_SAO_UP               HEVC_ASSIST_SCRATCH_6
#define HEVC_STREAM_SWAP_BUFFER   HEVC_ASSIST_SCRATCH_7
#define HEVC_STREAM_SWAP_BUFFER2  HEVC_ASSIST_SCRATCH_8
#define HEVC_sao_mem_unit         HEVC_ASSIST_SCRATCH_9
#define HEVC_SAO_ABV              HEVC_ASSIST_SCRATCH_A
#define HEVC_sao_vb_size          HEVC_ASSIST_SCRATCH_B
#define HEVC_SAO_VB               HEVC_ASSIST_SCRATCH_C
#define HEVC_SCALELUT             HEVC_ASSIST_SCRATCH_D
#define HEVC_WAIT_FLAG	          HEVC_ASSIST_SCRATCH_E
#define RPM_CMD_REG               HEVC_ASSIST_SCRATCH_F
#define LMEM_DUMP_ADR		          HEVC_ASSIST_SCRATCH_F
#define HEVC_STREAM_SWAP_TEST     HEVC_ASSIST_SCRATCH_L
#define HEVC_DECODE_PIC_BEGIN_REG HEVC_ASSIST_SCRATCH_M
#define HEVC_DECODE_PIC_NUM_REG   HEVC_ASSIST_SCRATCH_N

#define DEBUG_REG1              HEVC_ASSIST_SCRATCH_G
#define DEBUG_REG2              HEVC_ASSIST_SCRATCH_H
/* 
ucode parser/search control
bit 0:  0, header auto parse; 1, header manual parse
bit 1:  0, auto skip for noneseamless stream; 1, no skip
bit [3:2]: valid when bit1==0;  
0, auto skip nal before first vps/sps/pps/idr; 
1, auto skip nal before first vps/sps/pps
2, auto skip nal before fist  vps/sps/pps, and not decode until the first I slice (with slice address of 0)
*/
#define NAL_SEARCH_CTL		      HEVC_ASSIST_SCRATCH_I
#define CUR_NAL_UNIT_TYPE       HEVC_ASSIST_SCRATCH_J
#define DECODE_STOP_POS         HEVC_ASSIST_SCRATCH_K

#define MAX_INT 0x7FFFFFFF

#define RPM_BEGIN                                              0x100
#define modification_list_cur                                  0x140
#define RPM_END                                                0x180

#define RPS_USED_BIT  		14
//MISC_FLAG0
#define PCM_LOOP_FILTER_DISABLED_FLAG_BIT		0
#define PCM_ENABLE_FLAG_BIT				1
#define LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT	2
#define PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT	3
#define DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT	4
#define PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT		5
#define DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT		6
#define SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT	7
#define SLICE_SAO_LUMA_FLAG_BIT				8
#define SLICE_SAO_CHROMA_FLAG_BIT			9
#define SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT 10

typedef union PARAM_{
    struct{
        unsigned short data[RPM_END-RPM_BEGIN];
    }l;
    struct{ 
        /* from ucode lmem, do not change this struct */
        unsigned short CUR_RPS[0x10];                             
        unsigned short num_ref_idx_l0_active;                 
        unsigned short num_ref_idx_l1_active;                 
        unsigned short slice_type;                            
        unsigned short slice_temporal_mvp_enable_flag;        
        unsigned short dependent_slice_segment_flag;          
        unsigned short slice_segment_address;                 
        unsigned short num_title_rows_minus1;                 
        unsigned short pic_width_in_luma_samples;             
        unsigned short pic_height_in_luma_samples;            
        unsigned short log2_min_coding_block_size_minus3;     
        unsigned short log2_diff_max_min_coding_block_size;   
        unsigned short log2_max_pic_order_cnt_lsb_minus4;     
        unsigned short POClsb;                                
        unsigned short collocated_from_l0_flag;               
        unsigned short collocated_ref_idx;                    
        unsigned short log2_parallel_merge_level;             
        unsigned short five_minus_max_num_merge_cand;         
        unsigned short sps_num_reorder_pics_0;                
        unsigned short modification_flag;                     
        unsigned short tiles_enabled_flag;                    
        unsigned short num_tile_columns_minus1;               
        unsigned short num_tile_rows_minus1;                  
        unsigned short tile_width[4];                          
        unsigned short tile_height[4];                         
        unsigned short misc_flag0;                            
        unsigned short pps_beta_offset_div2;                  
        unsigned short pps_tc_offset_div2;                    
        unsigned short slice_beta_offset_div2;                
        unsigned short slice_tc_offset_div2;                  
        unsigned short pps_cb_qp_offset;                      
        unsigned short pps_cr_qp_offset;                      
        unsigned short first_slice_segment_in_pic_flag;       
        unsigned short m_temporalId;                          
        unsigned short m_nalUnitType;  
        
        unsigned short vui_num_units_in_tick_hi;
        unsigned short vui_num_units_in_tick_lo;
        unsigned short vui_time_scale_hi;
        unsigned short vui_time_scale_lo;
        unsigned short bit_depth;
        unsigned short reserved[3];
        
        unsigned short modification_list[0x20];                      
    }p;
}param_t;


typedef struct
{
    u32 buf_start;
    u32 buf_size;
    u32 buf_end;
} buff_t;

typedef struct
{
    u32 max_width;
    u32 max_height;
    u32 start_adr;
    u32 end_adr;
    buff_t ipp;
    buff_t sao_abv;
    buff_t sao_vb;
    buff_t short_term_rps;
    buff_t vps;
    buff_t sps;
    buff_t pps;
    buff_t sao_up;
    buff_t swap_buf;
    buff_t swap_buf2;
    buff_t scalelut;
    buff_t dblk_para;
    buff_t dblk_data;
    buff_t mpred_above;
    buff_t mpred_mv;
    buff_t rpm;
    buff_t lmem;
} BuffInfo_t;
#define WORK_BUF_SPEC_NUM 2
static BuffInfo_t amvh265_workbuff_spec[WORK_BUF_SPEC_NUM]={
    { //8M bytes
        .max_width = 1920,
        .max_height = 1088,
        .ipp = {
            // IPP work space calculation : 4096 * (Y+CbCr+Flags) = 12k, round to 16k
            .buf_size = 0x4000,
        },
        .sao_abv = {
            .buf_size = 0x30000,
        },
        .sao_vb = {
            .buf_size = 0x30000,
        },
        .short_term_rps = {
            // SHORT_TERM_RPS - Max 64 set, 16 entry every set, total 64x16x2 = 2048 bytes (0x800)
            .buf_size = 0x800,
        },
        .vps = {
            // VPS STORE AREA - Max 16 VPS, each has 0x80 bytes, total 0x0800 bytes
            .buf_size = 0x800,
        },
        .sps = {
            // SPS STORE AREA - Max 16 SPS, each has 0x80 bytes, total 0x0800 bytes
            .buf_size = 0x800,
        },
        .pps = {
            // PPS STORE AREA - Max 64 PPS, each has 0x80 bytes, total 0x2000 bytes
            .buf_size = 0x2000,
        },
        .sao_up = {
            // SAO UP STORE AREA - Max 640(10240/16) LCU, each has 16 bytes total 0x2800 bytes
            .buf_size = 0x2800,
        },
        .swap_buf = {
            // 256cyclex64bit = 2K bytes 0x800 (only 144 cycles valid)
            .buf_size = 0x800,
        },
        .swap_buf2 = {
            .buf_size = 0x800,
        },
        .scalelut = {
            // support up to 32 SCALELUT 1024x32 = 32Kbytes (0x8000)
            .buf_size = 0x8000,
        },
        .dblk_para = {
            // DBLK -> Max 256(4096/16) LCU, each para 512bytes(total:0x20000), data 1024bytes(total:0x40000)
            .buf_size = 0x20000,
        },
        .dblk_data = {
            .buf_size = 0x40000,
        },
        .mpred_above = {
            .buf_size = 0x8000,
        },
        .mpred_mv = {
           .buf_size = 0x40000*16, //1080p, 0x40000 per buffer
        },
        .rpm = {
           .buf_size = 0x80*2,
        },
        .lmem = {
           .buf_size = 0x400*2,    
        }
    },
    { 
        .max_width = 4096,
        .max_height = 2048,
        .ipp = {
            // IPP work space calculation : 4096 * (Y+CbCr+Flags) = 12k, round to 16k
            .buf_size = 0x4000,
        },
        .sao_abv = {
            .buf_size = 0x30000,
        },
        .sao_vb = {
            .buf_size = 0x30000,
        },
        .short_term_rps = {
            // SHORT_TERM_RPS - Max 64 set, 16 entry every set, total 64x16x2 = 2048 bytes (0x800)
            .buf_size = 0x800,
        },
        .vps = {
            // VPS STORE AREA - Max 16 VPS, each has 0x80 bytes, total 0x0800 bytes
            .buf_size = 0x800,
        },
        .sps = {
            // SPS STORE AREA - Max 16 SPS, each has 0x80 bytes, total 0x0800 bytes
            .buf_size = 0x800,
        },
        .pps = {
            // PPS STORE AREA - Max 64 PPS, each has 0x80 bytes, total 0x2000 bytes
            .buf_size = 0x2000,
        },
        .sao_up = {
            // SAO UP STORE AREA - Max 640(10240/16) LCU, each has 16 bytes total 0x2800 bytes
            .buf_size = 0x2800,
        },
        .swap_buf = {
            // 256cyclex64bit = 2K bytes 0x800 (only 144 cycles valid)
            .buf_size = 0x800,
        },
        .swap_buf2 = {
            .buf_size = 0x800,
        },
        .scalelut = {
            // support up to 32 SCALELUT 1024x32 = 32Kbytes (0x8000)
            .buf_size = 0x8000,
        },
        .dblk_para = {
            // DBLK -> Max 256(4096/16) LCU, each para 512bytes(total:0x20000), data 1024bytes(total:0x40000)
            .buf_size = 0x20000,
        },
        .dblk_data = {
            .buf_size = 0x40000,
        },
        .mpred_above = {
            .buf_size = 0x8000,
        },
        .mpred_mv = {
           .buf_size = 0x100000*16, //4k2k , 0x100000 per buffer
        },
        .rpm = {
           .buf_size = 0x80*2,
        },
        .lmem = {
           .buf_size = 0x400*2,    
        }
    }
};

static void init_buff_spec(BuffInfo_t* buf_spec)
{
    buf_spec->ipp.buf_start = buf_spec->start_adr;
    buf_spec->sao_abv.buf_start = buf_spec->ipp.buf_start + buf_spec->ipp.buf_size;

    buf_spec->sao_vb.buf_start   = buf_spec->sao_abv.buf_start + buf_spec->sao_abv.buf_size;
    buf_spec->short_term_rps.buf_start    = buf_spec->sao_vb.buf_start + buf_spec->sao_vb.buf_size;
    buf_spec->vps.buf_start     = buf_spec->short_term_rps.buf_start + buf_spec->short_term_rps.buf_size;
    buf_spec->sps.buf_start     = buf_spec->vps.buf_start + buf_spec->vps.buf_size;
    buf_spec->pps.buf_start     = buf_spec->sps.buf_start + buf_spec->sps.buf_size;
    buf_spec->sao_up.buf_start  = buf_spec->pps.buf_start + buf_spec->pps.buf_size;
    buf_spec->swap_buf.buf_start= buf_spec->sao_up.buf_start + buf_spec->sao_up.buf_size;
    buf_spec->swap_buf2.buf_start  = buf_spec->swap_buf.buf_start + buf_spec->swap_buf.buf_size;
    buf_spec->scalelut.buf_start= buf_spec->swap_buf2.buf_start + buf_spec->swap_buf2.buf_size;
    buf_spec->dblk_para.buf_start = buf_spec->scalelut.buf_start + buf_spec->scalelut.buf_size;
    buf_spec->dblk_data.buf_start = buf_spec->dblk_para.buf_start + buf_spec->dblk_para.buf_size;
    buf_spec->mpred_above.buf_start = buf_spec->dblk_data.buf_start + buf_spec->dblk_data.buf_size;
    buf_spec->mpred_mv.buf_start    = buf_spec->mpred_above.buf_start + buf_spec->mpred_above.buf_size;
    if(debug&H265_DEBUG_SEND_PARAM_WITH_REG){
        buf_spec->end_adr = buf_spec->mpred_mv.buf_start + buf_spec->mpred_mv.buf_size;
    }
    else{
        buf_spec->rpm.buf_start         = buf_spec->mpred_mv.buf_start + buf_spec->mpred_mv.buf_size;
        if(debug&H265_DEBUG_UCODE){
            buf_spec->lmem.buf_start = buf_spec->rpm.buf_start + buf_spec->rpm.buf_size;
            buf_spec->end_adr = buf_spec->lmem.buf_start + buf_spec->lmem.buf_size;
        }
        else{
            buf_spec->end_adr = buf_spec->rpm.buf_start + buf_spec->rpm.buf_size;
        }    
    }

    
    if(debug)printk("%s workspace (%x %x) size = %x\n", __func__,buf_spec->start_adr, buf_spec->end_adr, buf_spec->end_adr-buf_spec->start_adr);
    if(debug){
        printk("ipp.buf_start             :%x\n"  , buf_spec->ipp.buf_start         );
        printk("sao_abv.buf_start          :%x\n"  , buf_spec->sao_abv.buf_start         );
        printk("sao_vb.buf_start          :%x\n"  , buf_spec->sao_vb.buf_start         );
        printk("short_term_rps.buf_start  :%x\n"  , buf_spec->short_term_rps.buf_start );    
        printk("vps.buf_start             :%x\n"  , buf_spec->vps.buf_start            );
        printk("sps.buf_start             :%x\n"  , buf_spec->sps.buf_start            );
        printk("pps.buf_start             :%x\n"  , buf_spec->pps.buf_start            );
        printk("sao_up.buf_start          :%x\n"  , buf_spec->sao_up.buf_start         );
        printk("swap_buf.buf_start        :%x\n"  , buf_spec->swap_buf.buf_start       );
        printk("swap_buf2.buf_start       :%x\n"  , buf_spec->swap_buf2.buf_start      );
        printk("scalelut.buf_start        :%x\n"  , buf_spec->scalelut.buf_start       );
        printk("dblk_para.buf_start       :%x\n"  , buf_spec->dblk_para.buf_start      );
        printk("dblk_data.buf_start       :%x\n"  , buf_spec->dblk_data.buf_start      );
        printk("mpred_above.buf_start     :%x\n"  , buf_spec->mpred_above.buf_start    );
        printk("mpred_mv.buf_start        :%x\n"  , buf_spec->mpred_mv.buf_start       );
        if((debug&H265_DEBUG_SEND_PARAM_WITH_REG)==0){
            printk("rpm.buf_start             :%x\n"  , buf_spec->rpm.buf_start            );
        }
    }
    
}


enum SliceType
{
  B_SLICE,
  P_SLICE,
  I_SLICE
};

#define MAX_REF_PIC_NUM 16
#define MAX_SLICE_NUM 1024
typedef struct PIC_{
	struct PIC_ * next;
        int index;
	int POC;
	int decode_idx;
	int slice_type;
	int RefNum_L0;
	int RefNum_L1;
	int num_reorder_pic;
        int stream_offset;
	unsigned char referenced;
	unsigned char output_mark;
	unsigned char recon_mark;
	unsigned char output_ready;
	unsigned char error_mark;
	/**/
	int slice_idx;
	int m_aiRefPOCList0[MAX_SLICE_NUM][16];
	int m_aiRefPOCList1[MAX_SLICE_NUM][16];
	/*buffer*/
  unsigned int  cma_page_count;
  struct page *alloc_pages;
	unsigned long mpred_mv_wr_start_addr;
	unsigned long mc_y_adr;
	unsigned long mc_u_v_adr;
	int mc_canvas_y;
	int mc_canvas_u_v;
}PIC_t;
static PIC_t m_PIC[MAX_REF_PIC_NUM ];


typedef struct hevc_state_{
    BuffInfo_t* work_space_buf;
    buff_t* mc_buf;
    
    unsigned int pic_list_init_flag;
    unsigned int use_cma_flag;
    
    PIC_t* free_pic_list;
    PIC_t* decode_pic_list;
    unsigned short* rpm_ptr;
    unsigned short* lmem_ptr;
    unsigned short* debug_ptr;
    int debug_ptr_size;
    int     pic_w           ;
    int     pic_h           ;
    int     lcu_x_num;
    int     lcu_y_num;
    int     lcu_total;
    int     lcu_size        ;
    int     lcu_size_log2   ;

    int num_tile_col;
    int num_tile_row;
    int tile_enabled;
    int     tile_x;
    int     tile_y;
    int     tile_y_x;
    int     tile_start_lcu_x;
    int     tile_start_lcu_y; 
    int     tile_width_lcu  ;
    int     tile_height_lcu ; 

    int     slice_type      ;
    int     slice_addr;
    int     slice_segment_addr;

    unsigned short misc_flag0;
    int     m_temporalId;
    int     m_nalUnitType;
    int     TMVPFlag        ;
    int     isNextSliceSegment;
    int     LDCFlag         ;
    int     m_pocRandomAccess;
    int     plevel          ;
    int     MaxNumMergeCand ;

    int     new_pic;
    int     new_tile;
    int     curr_POC        ;
    int     iPrevPOC;
    int     iPrevTid0POC;
    int     list_no;
    int     RefNum_L0       ;
    int     RefNum_L1       ;
    int     ColFromL0Flag   ;
    int     LongTerm_Curr   ;
    int     LongTerm_Col    ;
    int     Col_POC         ;
    int     LongTerm_Ref    ;
    
    
    PIC_t* cur_pic;
    PIC_t* col_pic;
    int skip_flag;
    int decode_idx;
    int slice_idx;    
    unsigned char have_vps;
    unsigned char have_sps;
    unsigned char have_pps;
    unsigned char have_valid_start_slice;
    unsigned char wait_buf;
    unsigned char error_flag;
    unsigned int  error_skip_nal_count;

    unsigned char ignore_bufmgr_error; /* bit 0, for decoding; bit 1, for displaying */
    int PB_skip_mode;
    int PB_skip_count_after_decoding;

    int pts_mode;
    int last_lookup_pts;
    int last_pts;
    u64 last_lookup_pts_us64;
    u64 last_pts_us64;
    u64 shift_byte_count;
    u32 shift_byte_count_lo;
	u32 shift_byte_count_hi;
    int pts_mode_switching_count;
    int pts_mode_recovery_count;
}hevc_stru_t;


static void hevc_init_stru(hevc_stru_t* hevc, BuffInfo_t* buf_spec_i, buff_t* mc_buf_i)
{
    int i;
    hevc->work_space_buf = buf_spec_i;
    hevc->mc_buf = mc_buf_i;

    hevc->curr_POC = INVALID_POC;
    hevc->free_pic_list = NULL;
    hevc->decode_pic_list = NULL;

    hevc->pic_list_init_flag = 0;
    hevc->use_cma_flag = 0;
    hevc->decode_idx = 0;
    hevc->slice_idx = 0;
    hevc->new_pic=0;
    hevc->new_tile=0;
    hevc->iPrevPOC=0;
    hevc->list_no=0;
    //int m_uiMaxCUWidth = 1<<7;
    //int m_uiMaxCUHeight = 1<<7;
    hevc->m_pocRandomAccess = MAX_INT;
    hevc->tile_enabled = 0;
    hevc->tile_x = 0;
    hevc->tile_y = 0;
    hevc->iPrevTid0POC = 0;
    hevc->slice_addr = 0;
    hevc->slice_segment_addr = 0;
    hevc->skip_flag = 0;
    hevc->misc_flag0 = 0;
        
    hevc->cur_pic = NULL;
    hevc->col_pic = NULL;
    hevc->wait_buf = 0;
    hevc->error_flag = 0;
    hevc->error_skip_nal_count = 0;
    hevc->have_vps = 0;
    hevc->have_sps = 0;
    hevc->have_pps = 0;
    hevc->have_valid_start_slice = 0;

    hevc->pts_mode = PTS_NORMAL;
    hevc->last_pts = 0;
    hevc->last_lookup_pts = 0;
    hevc->last_pts_us64 = 0;
    hevc->last_lookup_pts_us64 = 0;
    hevc->shift_byte_count = 0;
    hevc->shift_byte_count_lo = 0;
    hevc->shift_byte_count_hi = 0;
    hevc->pts_mode_switching_count = 0;
    hevc->pts_mode_recovery_count = 0;

    hevc->PB_skip_mode = nal_skip_policy&0x3;
    hevc->PB_skip_count_after_decoding = (nal_skip_policy>>16)&0xffff;
    if(hevc->PB_skip_mode==0){
        hevc->ignore_bufmgr_error = 0x1;
    }
    else{
        hevc->ignore_bufmgr_error = 0x0;
    }

    for(i=0; i<MAX_REF_PIC_NUM; i++){
        m_PIC[i].index = -1;
    }
}    

static int prepare_display_buf(hevc_stru_t* hevc, int display_buff_id, int stream_offset, unsigned short slice_type);

static void get_rpm_param(param_t* params)
{
	int i;
	unsigned int data32;
	for(i=0; i<128; i++){
		do{
			data32 = READ_VREG(RPM_CMD_REG);
			//printk("%x\n", data32);
		}while((data32&0x10000)==0);	
		params->l.data[i] = data32&0xffff;
		//printk("%x\n", data32);
		WRITE_VREG(RPM_CMD_REG, 0);		
	}
}


static void in_q(PIC_t** list_head, PIC_t* pic)
{
	PIC_t* list_tail = *list_head;	
	pic->next = NULL;
	if(*list_head == NULL){
		*list_head = pic;	
	}
	else{
		while(list_tail->next){
			list_tail = list_tail->next;		
		}
		list_tail->next = pic;
	}
}

static PIC_t* out_q(PIC_t** list_head)
{
	PIC_t* pic = *list_head;
	if(pic){
		*list_head = pic->next;
	}
	return pic;
}

static PIC_t* get_pic_by_POC(hevc_stru_t* hevc, int POC)
{
	PIC_t* pic = hevc->decode_pic_list;
	PIC_t* ret_pic = NULL;
	while(pic){
		if(pic->POC==POC){
			if(ret_pic==NULL){
				ret_pic = pic;		
			}
			else{
				if(pic->decode_idx > ret_pic->decode_idx)
					ret_pic = pic;			
			}
		}
		pic = pic->next;
	}
	return ret_pic;
}

static PIC_t* get_ref_pic_by_POC(hevc_stru_t* hevc, int POC)
{
	PIC_t* pic = hevc->decode_pic_list;
	PIC_t* ret_pic = NULL;
	while(pic){
		if((pic->POC==POC)&&(pic->referenced)){
			if(ret_pic==NULL){
			    ret_pic = pic;
		  }
		  else{
    	    if(pic->decode_idx > ret_pic->decode_idx)
				    ret_pic = pic;			
			}
		}
		pic = pic->next;
	}
	
	if(ret_pic==NULL){
		if(debug) printk("Wrong, POC of %d is not in referenced list\n", POC);		
		ret_pic = get_pic_by_POC(hevc, POC);
	}
	return ret_pic;
}

static PIC_t* get_pic_by_IDX(hevc_stru_t* hevc, int idx)
{
	int i = 0;
	PIC_t* pic = hevc->decode_pic_list;
	while(pic){
		if(i==idx)
			break;
		pic = pic->next;
		i++;
	}
	return pic;
}

static unsigned int log2i (unsigned int val) {
    unsigned int ret = -1;
    while (val != 0) {
        val >>= 1;
        ret++;
    }
    return ret;
}

static int init_buf_spec(hevc_stru_t* hevc);

static void uninit_pic_list(hevc_stru_t* hevc)
{
	int i;
	for(i=0; i<MAX_REF_PIC_NUM; i++){
    if(m_PIC[i].alloc_pages!=NULL && m_PIC[i].cma_page_count>0){
        dma_release_from_contiguous(cma_dev, m_PIC[i].alloc_pages, m_PIC[i].cma_page_count);
        printk("release cma buffer[%d] (%d %x)\n", i, m_PIC[i].cma_page_count, (unsigned)m_PIC[i].alloc_pages);
        m_PIC[i].alloc_pages=NULL;
        m_PIC[i].cma_page_count=0;
    }
  }    
  hevc->pic_list_init_flag = 0;
}

static void init_pic_list(hevc_stru_t* hevc)
{
	int i;
	int pic_width = hevc->pic_w;
	int pic_height = hevc->pic_h;
	int lcu_size = hevc->lcu_size ;
        int pic_width_lcu  = ( pic_width %lcu_size  ) ? pic_width /lcu_size  + 1 : pic_width /lcu_size; 
        int pic_height_lcu = ( pic_height %lcu_size ) ? pic_height/lcu_size + 1 : pic_height/lcu_size; 
        int lcu_total       =pic_width_lcu*pic_height_lcu;
        int lcu_size_log2 = hevc->lcu_size_log2;
	//int MV_MEM_UNIT=lcu_size_log2==6 ? 0x100 : lcu_size_log2==5 ? 0x40 : 0x10;
	int MV_MEM_UNIT=lcu_size_log2==6 ? 0x200 : lcu_size_log2==5 ? 0x80 : 0x20;
	int mc_buffer_size_u_v = lcu_total*lcu_size*lcu_size/2;
	int mc_buffer_size_u_v_h = (mc_buffer_size_u_v + 0xffff)>>16;
	int mpred_mv_end = hevc->work_space_buf->mpred_mv.buf_start + hevc->work_space_buf->mpred_mv.buf_size;
	int mc_buffer_end = hevc->mc_buf->buf_start + hevc->mc_buf->buf_size;
	if(mc_buffer_size_u_v&0xffff){ //64k alignment
		mc_buffer_size_u_v_h+=1;
	}

	if(debug)printk("[Buffer Management] init_pic_list (%d %d):\n", hevc->pic_w, hevc->pic_h);	

	WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0x0);
	for(i=0; i<MAX_REF_PIC_NUM; i++){
		if(((i+1)*(mc_buffer_size_u_v_h<<16)*3) > hevc->mc_buf->buf_size){
        if(use_cma){
            hevc->use_cma_flag = 1;
        }
        else{
            if(debug)printk("%s maximum buf size is used\n", __func__);
              break;
        }
    }
    m_PIC[i].index = i;
		m_PIC[i].mpred_mv_wr_start_addr = hevc->work_space_buf->mpred_mv.buf_start + ((i * lcu_total)*MV_MEM_UNIT);
		
		if(hevc->use_cma_flag){
		    if((m_PIC[i].cma_page_count!=0) && (m_PIC[i].alloc_pages!=NULL) &&
		        (m_PIC[i].cma_page_count != PAGE_ALIGN((mc_buffer_size_u_v_h<<16)*3)/PAGE_SIZE)){
            dma_release_from_contiguous(cma_dev, m_PIC[i].alloc_pages, m_PIC[i].cma_page_count);
            printk("release cma buffer[%d] (%d %x)\n", i, m_PIC[i].cma_page_count, (unsigned)m_PIC[i].alloc_pages);
            m_PIC[i].alloc_pages=NULL;
            m_PIC[i].cma_page_count=0;		        
		    }
		    if(m_PIC[i].alloc_pages == NULL){
    		    m_PIC[i].cma_page_count = PAGE_ALIGN((mc_buffer_size_u_v_h<<16)*3)/PAGE_SIZE;
            m_PIC[i].alloc_pages = dma_alloc_from_contiguous(cma_dev, m_PIC[i].cma_page_count, 4);
            if(m_PIC[i].alloc_pages == NULL){
                printk("allocate cma buffer[%d] fail\n", i);
                m_PIC[i].cma_page_count = 0;
                break;
            }
            m_PIC[i].mc_y_adr = page_to_phys(m_PIC[i].alloc_pages);
    		    m_PIC[i].mc_u_v_adr = m_PIC[i].mc_y_adr + ((mc_buffer_size_u_v_h<<16)<<1);
            printk("allocate cma buffer[%d] (%d,%x,%x)\n", i, m_PIC[i].cma_page_count , (unsigned)m_PIC[i].alloc_pages, (unsigned)m_PIC[i].mc_y_adr);
		    }
		    else{
            printk("reuse cma buffer[%d] (%d,%x,%x)\n", i, m_PIC[i].cma_page_count , (unsigned)m_PIC[i].alloc_pages, (unsigned)m_PIC[i].mc_y_adr);
		    }
		}
		else{
		    m_PIC[i].cma_page_count = 0;
		    m_PIC[i].alloc_pages = NULL;
		    m_PIC[i].mc_y_adr = hevc->mc_buf->buf_start + i*(mc_buffer_size_u_v_h<<16)*3;
		    m_PIC[i].mc_u_v_adr = m_PIC[i].mc_y_adr + ((mc_buffer_size_u_v_h<<16)<<1);
    }		    
		m_PIC[i].mc_canvas_y = (i<<1);
		m_PIC[i].mc_canvas_u_v = (i<<1)+1;

		if((((m_PIC[i].mc_y_adr+((mc_buffer_size_u_v_h<<16)*3)) > mc_buffer_end) && (m_PIC[i].alloc_pages==NULL))
		    ||((m_PIC[i].mpred_mv_wr_start_addr+(lcu_total*MV_MEM_UNIT)) > mpred_mv_end)){
	    if(debug) printk("Max mc buffer or mpred_mv buffer is used\n");		
			break;
		}
    in_q(&hevc->free_pic_list, &m_PIC[i]);


		WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, m_PIC[i].mc_y_adr|(m_PIC[i].mc_canvas_y<<8)|0x1);
		WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, m_PIC[i].mc_u_v_adr|(m_PIC[i].mc_canvas_u_v<<8)|0x1);

    if(debug&H265_DEBUG_BUFMGR){
        printk("Buffer %d: canv_y %x  canv_u_v %x mc_y_adr %lx mc_u_v_adr %lx mpred_mv_wr_start_addr %lx\n", i, m_PIC[i].mc_canvas_y,m_PIC[i].mc_canvas_u_v,m_PIC[i].mc_y_adr, m_PIC[i].mc_u_v_adr, m_PIC[i].mpred_mv_wr_start_addr);
    }
	}
	WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0x1);


    // Zero out canvas registers in IPP -- avoid simulation X
	    WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (0 << 8) | (0<<1) | 1);
		for(i=0; i<32; i++){
				WRITE_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
        }

}

static void init_pic_list_hw(void)
{
	int i;
	WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0x0);
	for(i=0; i<MAX_REF_PIC_NUM; i++){
      if(m_PIC[i].index == -1)
         break;
		  WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, m_PIC[i].mc_y_adr|(m_PIC[i].mc_canvas_y<<8)|0x1);
		  WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CMD_ADDR, m_PIC[i].mc_u_v_adr|(m_PIC[i].mc_canvas_u_v<<8)|0x1);
	}
	WRITE_VREG(HEVCD_MPP_ANC2AXI_TBL_CONF_ADDR, 0x1);

 // Zero out canvas registers in IPP -- avoid simulation X
  WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (0 << 8) | (0<<1) | 1);
  for(i=0; i<32; i++){
			WRITE_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR, 0);
  }
    
}    


static void dump_pic_list(hevc_stru_t* hevc)
{
	PIC_t* pic = hevc->decode_pic_list;
	printk("pic_list_init_flag is %d\r\n", hevc->pic_list_init_flag);
	while(pic){
		printk("index %d decode_idx:%d,	POC:%d,	referenced:%d,	num_reorder_pic:%d, output_mark:%d, output_ready:%d, mv_wr_start %lx\n", pic->index, pic->decode_idx, pic->POC, pic->referenced, pic->num_reorder_pic, pic->output_mark, pic->output_ready, pic->mpred_mv_wr_start_addr);
		pic = pic->next;
	}
}

static PIC_t* output_pic(hevc_stru_t* hevc, unsigned char flush_flag)
{
	int num_pic_not_yet_display = 0;
	PIC_t* pic = hevc->decode_pic_list;
	PIC_t* pic_display = NULL;
	while(pic){
		if(pic->output_mark){
			num_pic_not_yet_display++;
		}
		pic = pic->next;
	}

	pic = hevc->decode_pic_list;
	while(pic){
		if(pic->output_mark){
			if(pic_display){
				if(pic->POC < pic_display->POC){
					pic_display = pic;
				}
			}
			else{
				pic_display = pic;			
			}
		}
		pic = pic->next;
	}
	if(pic_display){
		if((num_pic_not_yet_display > pic_display->num_reorder_pic)||flush_flag){
			pic_display->output_mark = 0;
			pic_display->recon_mark = 0;
			pic_display->output_ready = 1;
		}
		else{
			pic_display = NULL;		
		}
	}
	return pic_display;
}

static int config_mc_buffer(hevc_stru_t* hevc, PIC_t* cur_pic)
{
	int i;	
	PIC_t* pic;
    if(debug&H265_DEBUG_BUFMGR) 
        printk("config_mc_buffer entered .....\n");
	if(cur_pic->slice_type != 2){ //P and B pic
	    WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (0 << 8) | (0<<1) | 1);
		for(i=0; i<cur_pic->RefNum_L0; i++){
			pic = get_ref_pic_by_POC(hevc, cur_pic->m_aiRefPOCList0[cur_pic->slice_idx][i]);
			if(pic){
				if(pic->error_mark){
            cur_pic->error_mark = 1;
				}
				WRITE_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR, (pic->mc_canvas_u_v<<16)|(pic->mc_canvas_u_v<<8)|pic->mc_canvas_y);
        if(debug&H265_DEBUG_BUFMGR) 
            printk("refid %x mc_canvas_u_v %x mc_canvas_y %x\n", i,pic->mc_canvas_u_v,pic->mc_canvas_y);
			}
			else{
				if(debug) printk("Error %s, %dth poc (%d) of RPS is not in the pic list0\n", __func__, i, cur_pic->m_aiRefPOCList0[cur_pic->slice_idx][i]);
        cur_pic->error_mark = 1;
				//dump_lmem();
			}
		}
	}
	if(cur_pic->slice_type == 0){ //B pic
        if(debug&H265_DEBUG_BUFMGR) 
            printk("config_mc_buffer RefNum_L1\n");
	    WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (16 << 8) | (0<<1) | 1);
		for(i=0; i<cur_pic->RefNum_L1; i++){
			pic = get_ref_pic_by_POC(hevc, cur_pic->m_aiRefPOCList1[cur_pic->slice_idx][i]);
			if(pic){
				if(pic->error_mark){
            cur_pic->error_mark = 1;
				}
				WRITE_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR, (pic->mc_canvas_u_v<<16)|(pic->mc_canvas_u_v<<8)|pic->mc_canvas_y);
                if(debug&H265_DEBUG_BUFMGR){
                    printk("refid %x mc_canvas_u_v %x mc_canvas_y %x\n", i,pic->mc_canvas_u_v,pic->mc_canvas_y);
                }
			}
			else{
				if(debug) printk("Error %s, %dth poc (%d) of RPS is not in the pic list1\n", __func__, i, cur_pic->m_aiRefPOCList1[cur_pic->slice_idx][i]);
        cur_pic->error_mark = 1;
				//dump_lmem();
			}
		}
	}
	return 0;
}

static void apply_ref_pic_set(hevc_stru_t* hevc, int cur_poc, param_t* params)
{
	int i;
	int poc_tmp;
	PIC_t* pic = hevc->decode_pic_list;
	unsigned char is_referenced;
	//printk("%s cur_poc %d\n", __func__, cur_poc);	
	while(pic){
		if((pic->referenced == 0 || pic->POC == cur_poc)){
			pic = pic->next;
			continue;
		}
		is_referenced = 0;
		for(i=0; i<16; i++){
			int delt;
			if(params->p.CUR_RPS[i]&0x8000)
				break;		
			delt = params->p.CUR_RPS[i]&((1<<(RPS_USED_BIT-1))-1);
			if(params->p.CUR_RPS[i]&(1<<(RPS_USED_BIT-1))){
				poc_tmp = cur_poc - ((1<<(RPS_USED_BIT-1))-delt) ;
			}
			else{
				poc_tmp = cur_poc + delt;
			}
			if(poc_tmp == pic->POC){
				is_referenced = 1;
				//printk("i is %d\n", i);
				break;
			}
		}
		if(is_referenced == 0){
			pic->referenced = 0;
			//printk("set poc %d reference to 0\n", pic->POC);
		}
		pic = pic->next;
	}

}

static void set_ref_pic_list(PIC_t* pic,  param_t* params)
{
	int i, rIdx;
	int num_neg = 0;
	int num_pos = 0;
	int total_num;
	int num_ref_idx_l0_active = params->p.num_ref_idx_l0_active;
	int num_ref_idx_l1_active = params->p.num_ref_idx_l1_active;
	int RefPicSetStCurr0[16];
	int RefPicSetStCurr1[16];
	for(i=0;i<16;i++){
		RefPicSetStCurr0[i]=0; RefPicSetStCurr1[i]=0;
		pic->m_aiRefPOCList0[pic->slice_idx][i] = 0;
		pic->m_aiRefPOCList1[pic->slice_idx][i] = 0;
	}
	for(i=0; i<16; i++){
		if(params->p.CUR_RPS[i]&0x8000)
			break;		
		if((params->p.CUR_RPS[i]>>RPS_USED_BIT)&1){
			int delt = params->p.CUR_RPS[i]&((1<<(RPS_USED_BIT-1))-1);
			if((params->p.CUR_RPS[i]>>(RPS_USED_BIT-1))&1){
				RefPicSetStCurr0[num_neg]=pic->POC - ((1<<(RPS_USED_BIT-1))-delt) ;
				//printk("RefPicSetStCurr0 %x %x %x\n", RefPicSetStCurr0[num_neg], pic->POC, (0x800-(params[i]&0x7ff)));
				num_neg++;
			}
			else{
				RefPicSetStCurr1[num_pos]=pic->POC + delt;
				//printk("RefPicSetStCurr1 %d\n", RefPicSetStCurr1[num_pos]);
				num_pos++;
			}
		}	
	}
	total_num = num_neg + num_pos;
	if(debug&H265_DEBUG_BUFMGR){
	    printk("%s: curpoc %d slice_type %d, total %d num_neg %d num_list0 %d num_list1 %d\n", __func__,
		        pic->POC, params->p.slice_type, total_num, num_neg,num_ref_idx_l0_active, num_ref_idx_l1_active);
	}
	
	if(total_num>0){
		if(params->p.modification_flag&0x1){
			if(debug&H265_DEBUG_BUFMGR) 
			    printk("ref0 POC (modification):");
			for(rIdx=0; rIdx<num_ref_idx_l0_active; rIdx++){
				int cIdx = params->p.modification_list[rIdx];
				pic->m_aiRefPOCList0[pic->slice_idx][rIdx] = cIdx>=num_neg?RefPicSetStCurr1[cIdx-num_neg]:RefPicSetStCurr0[cIdx];	
				if(debug&H265_DEBUG_BUFMGR) 
				    printk("%d ", pic->m_aiRefPOCList0[pic->slice_idx][rIdx]);
			}
		}
		else{
			if(debug&H265_DEBUG_BUFMGR) 
			    printk("ref0 POC:");
			for(rIdx=0; rIdx<num_ref_idx_l0_active; rIdx++){
				int cIdx = rIdx % total_num;		
				pic->m_aiRefPOCList0[pic->slice_idx][rIdx] = cIdx>=num_neg?RefPicSetStCurr1[cIdx-num_neg]:RefPicSetStCurr0[cIdx];	
				if(debug&H265_DEBUG_BUFMGR) 
				    printk("%d ", pic->m_aiRefPOCList0[pic->slice_idx][rIdx]);
			}
		}
		if(debug&H265_DEBUG_BUFMGR) 
		    printk("\n");
		if(params->p.slice_type == B_SLICE){
			if(params->p.modification_flag&0x2){
				if(debug&H265_DEBUG_BUFMGR) 
				    printk("ref1 POC (modification):");
				for(rIdx=0; rIdx<num_ref_idx_l1_active; rIdx++){
					int cIdx;
					if(params->p.modification_flag&0x1){
					    cIdx = params->p.modification_list[num_ref_idx_l0_active+rIdx];
					}
					else{
				            cIdx = params->p.modification_list[rIdx];
					}
					pic->m_aiRefPOCList1[pic->slice_idx][rIdx] = cIdx>=num_pos?RefPicSetStCurr0[cIdx-num_pos]:RefPicSetStCurr1[cIdx];
					if(debug&H265_DEBUG_BUFMGR) 
					    printk("%d ", pic->m_aiRefPOCList1[pic->slice_idx][rIdx]);
				}
			}
			else{
				if(debug&H265_DEBUG_BUFMGR) 
				    printk("ref1 POC:");
				for(rIdx=0; rIdx<num_ref_idx_l1_active; rIdx++){
					int cIdx = rIdx % total_num;		
					pic->m_aiRefPOCList1[pic->slice_idx][rIdx] = cIdx>=num_pos?RefPicSetStCurr0[cIdx-num_pos]:RefPicSetStCurr1[cIdx];
					if(debug&H265_DEBUG_BUFMGR) 
					    printk("%d ", pic->m_aiRefPOCList1[pic->slice_idx][rIdx]);
				}
			}
			if(debug&H265_DEBUG_BUFMGR) 
			    printk("\n");
		}
	}
	/*set m_PIC */
	pic->slice_type = (params->p.slice_type == I_SLICE ) ? 2 :
                                (params->p.slice_type == P_SLICE ) ? 1 :
                                (params->p.slice_type == B_SLICE ) ? 0 : 3;
	pic->RefNum_L0 = num_ref_idx_l0_active;
	pic->RefNum_L1 = num_ref_idx_l1_active;
}

#define MAX_TILE_COL_NUM	5
#define MAX_TILE_ROW_NUM	5
typedef struct{
	int width;
	int height;
	int start_cu_x;
	int start_cu_y;
	
	unsigned long sao_vb_start_addr;
	unsigned long sao_abv_start_addr;
}tile_t;
tile_t m_tile[MAX_TILE_ROW_NUM][MAX_TILE_COL_NUM];

static void update_tile_info(hevc_stru_t* hevc,  int pic_width_cu, int pic_height_cu, int sao_mem_unit, param_t* params)
{
	int i,j;	
	int start_cu_x, start_cu_y;
    int sao_vb_size = (sao_mem_unit+(2<<4))*pic_height_cu;
    int sao_abv_size = sao_mem_unit*pic_width_cu;

	hevc->tile_enabled = params->p.tiles_enabled_flag&1;
	if(params->p.tiles_enabled_flag&1){
		hevc->num_tile_col = params->p.num_tile_columns_minus1 + 1;
		hevc->num_tile_row = params->p.num_tile_rows_minus1 + 1;
		if(debug&H265_DEBUG_BUFMGR){
		    printk("%s pic_w_cu %d pic_h_cu %d tile_enabled num_tile_col %d num_tile_row %d:\n", __func__, pic_width_cu, pic_height_cu, hevc->num_tile_col, hevc->num_tile_row);
		}


		if(params->p.tiles_enabled_flag&2){ //uniform flag
			int w = pic_width_cu/hevc->num_tile_col;
			int h = pic_height_cu/hevc->num_tile_row;
			start_cu_y = 0;			
			for(i=0; i<hevc->num_tile_row; i++){
				start_cu_x = 0;
				for(j=0; j<hevc->num_tile_col; j++){
					if(j == (hevc->num_tile_col-1))
						m_tile[i][j].width = pic_width_cu - start_cu_x;
					else			
						m_tile[i][j].width = w;
					if(i == (hevc->num_tile_row-1))
						m_tile[i][j].height = pic_height_cu - start_cu_y;
					else			
						m_tile[i][j].height = h;
					m_tile[i][j].start_cu_x = start_cu_x;
					m_tile[i][j].start_cu_y = start_cu_y;
					m_tile[i][j].sao_vb_start_addr = hevc->work_space_buf->sao_vb.buf_start + j*sao_vb_size ;
					m_tile[i][j].sao_abv_start_addr = hevc->work_space_buf->sao_abv.buf_start + i*sao_abv_size ;
					if(debug&H265_DEBUG_BUFMGR){
					    printk("{y=%d, x=%d w %d h %d start_x %d start_y %d sao_vb_start 0x%lx sao_abv_start 0x%lx}\n", 
						    i,j,m_tile[i][j].width,m_tile[i][j].height, m_tile[i][j].start_cu_x, m_tile[i][j].start_cu_y,m_tile[i][j].sao_vb_start_addr, m_tile[i][j].sao_abv_start_addr);
					}
					start_cu_x += m_tile[i][j].width;
					
				}
				start_cu_y += m_tile[i][0].height;
			}			
		}
		else{
			start_cu_y = 0;			
			for(i=0; i<hevc->num_tile_row; i++){
				start_cu_x = 0;
				for(j=0; j<hevc->num_tile_col; j++){
					if(j == (hevc->num_tile_col-1))
						m_tile[i][j].width = pic_width_cu - start_cu_x;
					else			
						m_tile[i][j].width = params->p.tile_width[j];
					if(i == (hevc->num_tile_row-1))
						m_tile[i][j].height = pic_height_cu - start_cu_y;
					else			
						m_tile[i][j].height = params->p.tile_height[i];
					m_tile[i][j].start_cu_x = start_cu_x;
					m_tile[i][j].start_cu_y = start_cu_y;
					m_tile[i][j].sao_vb_start_addr = hevc->work_space_buf->sao_vb.buf_start + j*sao_vb_size ;
					m_tile[i][j].sao_abv_start_addr = hevc->work_space_buf->sao_abv.buf_start + i*sao_abv_size ;
					if(debug&H265_DEBUG_BUFMGR){
					    printk("{y=%d, x=%d w %d h %d start_x %d start_y %d sao_vb_start 0x%lx sao_abv_start 0x%lx}\n", 
						    i,j,m_tile[i][j].width,m_tile[i][j].height, m_tile[i][j].start_cu_x, m_tile[i][j].start_cu_y,m_tile[i][j].sao_vb_start_addr, m_tile[i][j].sao_abv_start_addr);
				  }
					start_cu_x += m_tile[i][j].width;
				}
				start_cu_y += m_tile[i][0].height;
			}			
		}
	}
	else{
		hevc->num_tile_col = 1;
		hevc->num_tile_row = 1;
		m_tile[0][0].width = pic_width_cu;
		m_tile[0][0].height = pic_height_cu;
		m_tile[0][0].start_cu_x = 0;
		m_tile[0][0].start_cu_y = 0;
		m_tile[0][0].sao_vb_start_addr = hevc->work_space_buf->sao_vb.buf_start;
		m_tile[0][0].sao_abv_start_addr = hevc->work_space_buf->sao_abv.buf_start;
	}
}

static int get_tile_index(hevc_stru_t* hevc, int cu_adr, int pic_width_lcu)
{
	int cu_x;
	int cu_y;
	int tile_x = 0;
	int tile_y = 0;
	int i;
	if(pic_width_lcu == 0){
	    if(debug) printk("%s Error, pic_width_lcu is 0, pic_w %d, pic_h %d\n", __func__, hevc->pic_w, hevc->pic_h);
	    return -1;
  }
	cu_x = cu_adr%pic_width_lcu;
	cu_y = cu_adr/pic_width_lcu;
	if(hevc->tile_enabled){	
		for(i=0;i<hevc->num_tile_col;i++){	
			if(cu_x>=m_tile[0][i].start_cu_x){
				tile_x = i;		
			}
			else{
				break;
			}
		}
		for(i=0;i<hevc->num_tile_row;i++){	
			if(cu_y>=m_tile[i][0].start_cu_y){
				tile_y = i;		
			}
			else{
				break;
			}
		}
	}
	return (tile_x)|(tile_y<<8);
}

static void print_scratch_error(int error_num)
{
  if(debug) printk(" ERROR : HEVC_ASSIST_SCRATCH_TEST Error : %d\n", error_num);
}

static void hevc_config_work_space_hw(hevc_stru_t* hevc)
{
    BuffInfo_t* buf_spec = hevc->work_space_buf;
    
    if(debug) printk("%s %x %x %x %x %x %x %x %x %x %x %x %x\n", __func__,
			buf_spec->ipp.buf_start,
			buf_spec->start_adr,
			buf_spec->short_term_rps.buf_start,
			buf_spec->vps.buf_start,
			buf_spec->sps.buf_start,
                        buf_spec->pps.buf_start,
                        buf_spec->sao_up.buf_start,
                        buf_spec->swap_buf.buf_start,
			buf_spec->swap_buf2.buf_start,
			buf_spec->scalelut.buf_start,
			buf_spec->dblk_para.buf_start,
			buf_spec->dblk_data.buf_start);
    WRITE_VREG(HEVCD_IPP_LINEBUFF_BASE,buf_spec->ipp.buf_start);
    if((debug&H265_DEBUG_SEND_PARAM_WITH_REG)==0){
        WRITE_VREG(HEVC_RPM_BUFFER, buf_spec->rpm.buf_start);
    }
    WRITE_VREG(HEVC_SHORT_TERM_RPS, buf_spec->short_term_rps.buf_start);
    WRITE_VREG(HEVC_VPS_BUFFER, buf_spec->vps.buf_start);
    WRITE_VREG(HEVC_SPS_BUFFER, buf_spec->sps.buf_start);
    WRITE_VREG(HEVC_PPS_BUFFER, buf_spec->pps.buf_start);
    WRITE_VREG(HEVC_SAO_UP, buf_spec->sao_up.buf_start);
    WRITE_VREG(HEVC_STREAM_SWAP_BUFFER, buf_spec->swap_buf.buf_start);
    WRITE_VREG(HEVC_STREAM_SWAP_BUFFER2, buf_spec->swap_buf2.buf_start);
    WRITE_VREG(HEVC_SCALELUT, buf_spec->scalelut.buf_start);

    WRITE_VREG(HEVC_DBLK_CFG4, buf_spec->dblk_para.buf_start); // cfg_p_addr
    WRITE_VREG(HEVC_DBLK_CFG5, buf_spec->dblk_data.buf_start); // cfg_d_addr
    
    if(debug&H265_DEBUG_UCODE){
        WRITE_VREG(LMEM_DUMP_ADR, buf_spec->lmem.buf_start);
    }
    
}    

static void hevc_init_decoder_hw(int decode_pic_begin, int decode_pic_num)
{
    unsigned int data32;
    int i;

#if 1
// m8baby test1902
    if(debug&H265_DEBUG_BUFMGR) 
        printk("[test.c] Test Parser Register Read/Write\n");
    data32 = READ_VREG(HEVC_PARSER_VERSION);
    if(data32 != 0x00010001) { print_scratch_error(25); return; } 
    WRITE_VREG(HEVC_PARSER_VERSION, 0x5a5a55aa);
    data32 = READ_VREG(HEVC_PARSER_VERSION);
    if(data32 != 0x5a5a55aa) { print_scratch_error(26); return; } 

#if 0
    // test Parser Reset
    WRITE_VREG(DOS_SW_RESET3, 
      (1<<14) | // reset iqit to start mem init again 
      (1<<3)    // reset_whole parser
      );
    WRITE_VREG(DOS_SW_RESET3, 0);      // clear reset_whole parser
    data32 = READ_VREG(HEVC_PARSER_VERSION);
    if(data32 != 0x00010001) { 
        printk("Test Parser Fatal Error\n"); 
        while(1){};
    }
#endif

    WRITE_VREG(DOS_SW_RESET3, 
      (1<<14)  // reset iqit to start mem init again 
      );
    CLEAR_VREG_MASK(HEVC_CABAC_CONTROL, 1);
    CLEAR_VREG_MASK(HEVC_PARSER_CORE_CONTROL, 1);

#endif

    if(debug&H265_DEBUG_BUFMGR) 
        printk("[test.c] Enable BitStream Fetch\n");
    data32 = READ_VREG(HEVC_STREAM_CONTROL);
    data32 = data32 | 
             (1 << 0) // stream_fetch_enable
             ;
    WRITE_VREG(HEVC_STREAM_CONTROL, data32);

    data32 = READ_VREG(HEVC_SHIFT_STARTCODE);
    if(data32 != 0x00000100) { print_scratch_error(29); return; } 
    data32 = READ_VREG(HEVC_SHIFT_EMULATECODE);
    if(data32 != 0x00000300) { print_scratch_error(30); return; } 
    WRITE_VREG(HEVC_SHIFT_STARTCODE, 0x12345678);
    WRITE_VREG(HEVC_SHIFT_EMULATECODE, 0x9abcdef0);
    data32 = READ_VREG(HEVC_SHIFT_STARTCODE);
    if(data32 != 0x12345678) { print_scratch_error(31); return; } 
    data32 = READ_VREG(HEVC_SHIFT_EMULATECODE);
    if(data32 != 0x9abcdef0) { print_scratch_error(32); return; } 
    WRITE_VREG(HEVC_SHIFT_STARTCODE, 0x00000100);
    WRITE_VREG(HEVC_SHIFT_EMULATECODE, 0x00000300);

    if(debug&H265_DEBUG_BUFMGR) 
        printk("[test.c] Enable HEVC Parser Interrupt\n");
    data32 = READ_VREG(HEVC_PARSER_INT_CONTROL);
    data32 = data32 | 
             (1 << 24) |  // stream_buffer_empty_int_amrisc_enable
             (1 << 22) |  // stream_fifo_empty_int_amrisc_enable
             (1 << 7) |  // dec_done_int_cpu_enable
             (1 << 4) |  // startcode_found_int_cpu_enable
             (0 << 3) |  // startcode_found_int_amrisc_enable
             (1 << 0)    // parser_int_enable
             ;
    WRITE_VREG(HEVC_PARSER_INT_CONTROL, data32);

    if(debug&H265_DEBUG_BUFMGR) 
        printk("[test.c] Enable HEVC Parser Shift\n");

    data32 = READ_VREG(HEVC_SHIFT_STATUS);
    data32 = data32 | 
             (1 << 1) |  // emulation_check_on
             (1 << 0)    // startcode_check_on
             ;
    WRITE_VREG(HEVC_SHIFT_STATUS, data32);

    WRITE_VREG(HEVC_SHIFT_CONTROL, 
              (3 << 6) | // sft_valid_wr_position
              (2 << 4) | // emulate_code_length_sub_1
              (2 << 1) | // start_code_length_sub_1
              (1 << 0)   // stream_shift_enable
            );

    WRITE_VREG(HEVC_CABAC_CONTROL, 
              (1 << 0)   // cabac_enable
            );

    WRITE_VREG(HEVC_PARSER_CORE_CONTROL, 
              (1 << 0)   // hevc_parser_core_clk_en
            );


    WRITE_VREG(HEVC_DEC_STATUS_REG, 0);

    // Initial IQIT_SCALELUT memory -- just to avoid X in simulation
    if(debug&H265_DEBUG_BUFMGR) 
        printk("[test.c] Initial IQIT_SCALELUT memory -- just to avoid X in simulation...\n");
    WRITE_VREG(HEVC_IQIT_SCALELUT_WR_ADDR, 0); // cfg_p_addr
    for(i=0; i<1024; i++) WRITE_VREG(HEVC_IQIT_SCALELUT_DATA, 0);


#ifdef ENABLE_SWAP_TEST
    WRITE_VREG(HEVC_STREAM_SWAP_TEST, 100);
#else
    WRITE_VREG(HEVC_STREAM_SWAP_TEST, 0);
#endif

    WRITE_VREG(HEVC_DECODE_PIC_BEGIN_REG, 0);
    WRITE_VREG(HEVC_DECODE_PIC_NUM_REG, 0xffffffff);

    // Send parser_cmd
    if(debug) printk("[test.c] SEND Parser Command ...\n");
    WRITE_VREG(HEVC_PARSER_CMD_WRITE, (1<<16) | (0<<0));
    for(i=0; i<PARSER_CMD_NUMBER; i++){
      WRITE_VREG(HEVC_PARSER_CMD_WRITE, parser_cmd[i]);
    }

    WRITE_VREG(HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
    WRITE_VREG(HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
    WRITE_VREG(HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);

    WRITE_VREG(HEVC_PARSER_IF_CONTROL, 
            //  (1 << 8) | // sao_sw_pred_enable
              (1 << 5) | // parser_sao_if_en
              (1 << 2) | // parser_mpred_if_en
              (1 << 0) // parser_scaler_if_en
            );

    // Changed to Start MPRED in microcode
    /*
    printk("[test.c] Start MPRED\n");
    WRITE_VREG(HEVC_MPRED_INT_STATUS,
            (1<<31)
        ); 
    */

    if(debug) printk("[test.c] Reset IPP\n");
    WRITE_VREG(HEVCD_IPP_TOP_CNTL, 
              (0 << 1) | // enable ipp
              (1 << 0)   // software reset ipp and mpp
            );
    WRITE_VREG(HEVCD_IPP_TOP_CNTL, 
              (1 << 1) | // enable ipp
              (0 << 0)   // software reset ipp and mpp
            );
}

static void decoder_hw_reset(void)
{
    int i;
    unsigned int data32;
    WRITE_VREG(DOS_SW_RESET3, 
      (1<<14)  // reset iqit to start mem init again 
      );
    CLEAR_VREG_MASK(HEVC_CABAC_CONTROL, 1);
    CLEAR_VREG_MASK(HEVC_PARSER_CORE_CONTROL, 1);


    data32 = READ_VREG(HEVC_STREAM_CONTROL);
    data32 = data32 | 
             (1 << 0) // stream_fetch_enable
             ;
    WRITE_VREG(HEVC_STREAM_CONTROL, data32);

    data32 = READ_VREG(HEVC_SHIFT_STARTCODE);
    if(data32 != 0x00000100) { print_scratch_error(29); return; } 
    data32 = READ_VREG(HEVC_SHIFT_EMULATECODE);
    if(data32 != 0x00000300) { print_scratch_error(30); return; } 
    WRITE_VREG(HEVC_SHIFT_STARTCODE, 0x12345678);
    WRITE_VREG(HEVC_SHIFT_EMULATECODE, 0x9abcdef0);
    data32 = READ_VREG(HEVC_SHIFT_STARTCODE);
    if(data32 != 0x12345678) { print_scratch_error(31); return; } 
    data32 = READ_VREG(HEVC_SHIFT_EMULATECODE);
    if(data32 != 0x9abcdef0) { print_scratch_error(32); return; } 
    WRITE_VREG(HEVC_SHIFT_STARTCODE, 0x00000100);
    WRITE_VREG(HEVC_SHIFT_EMULATECODE, 0x00000300);

    data32 = READ_VREG(HEVC_PARSER_INT_CONTROL);
    data32 = data32 | 
             (1 << 24) |  // stream_buffer_empty_int_amrisc_enable
             (1 << 22) |  // stream_fifo_empty_int_amrisc_enable
             (1 << 7) |  // dec_done_int_cpu_enable
             (1 << 4) |  // startcode_found_int_cpu_enable
             (0 << 3) |  // startcode_found_int_amrisc_enable
             (1 << 0)    // parser_int_enable
             ;
    WRITE_VREG(HEVC_PARSER_INT_CONTROL, data32);

    data32 = READ_VREG(HEVC_SHIFT_STATUS);
    data32 = data32 | 
             (1 << 1) |  // emulation_check_on
             (1 << 0)    // startcode_check_on
             ;
    WRITE_VREG(HEVC_SHIFT_STATUS, data32);

    WRITE_VREG(HEVC_SHIFT_CONTROL, 
              (3 << 6) | // sft_valid_wr_position
              (2 << 4) | // emulate_code_length_sub_1
              (2 << 1) | // start_code_length_sub_1
              (1 << 0)   // stream_shift_enable
            );

    WRITE_VREG(HEVC_CABAC_CONTROL, 
              (1 << 0)   // cabac_enable
            );

    WRITE_VREG(HEVC_PARSER_CORE_CONTROL, 
              (1 << 0)   // hevc_parser_core_clk_en
            );


    // Initial IQIT_SCALELUT memory -- just to avoid X in simulation
    WRITE_VREG(HEVC_IQIT_SCALELUT_WR_ADDR, 0); // cfg_p_addr
    for(i=0; i<1024; i++) WRITE_VREG(HEVC_IQIT_SCALELUT_DATA, 0);


    // Send parser_cmd
    WRITE_VREG(HEVC_PARSER_CMD_WRITE, (1<<16) | (0<<0));
    for(i=0; i<PARSER_CMD_NUMBER; i++){
      WRITE_VREG(HEVC_PARSER_CMD_WRITE, parser_cmd[i]);
    }

    WRITE_VREG(HEVC_PARSER_CMD_SKIP_0, PARSER_CMD_SKIP_CFG_0);
    WRITE_VREG(HEVC_PARSER_CMD_SKIP_1, PARSER_CMD_SKIP_CFG_1);
    WRITE_VREG(HEVC_PARSER_CMD_SKIP_2, PARSER_CMD_SKIP_CFG_2);

    WRITE_VREG(HEVC_PARSER_IF_CONTROL, 
            //  (1 << 8) | // sao_sw_pred_enable
              (1 << 5) | // parser_sao_if_en
              (1 << 2) | // parser_mpred_if_en
              (1 << 0) // parser_scaler_if_en
            );

    WRITE_VREG(HEVCD_IPP_TOP_CNTL, 
              (0 << 1) | // enable ipp
              (1 << 0)   // software reset ipp and mpp
            );
    WRITE_VREG(HEVCD_IPP_TOP_CNTL, 
              (1 << 1) | // enable ipp
              (0 << 0)   // software reset ipp and mpp
            );
}

#ifdef CONFIG_HEVC_CLK_FORCED_ON
static void config_hevc_clk_forced_on ()
{
    unsigned int rdata32;
    // IQIT
    rdata32 = READ_VREG(HEVC_IQIT_CLK_RST_CTRL);
    WRITE_VREG(HEVC_IQIT_CLK_RST_CTRL, rdata32 | (0x1<<2));

    // DBLK
    rdata32 = READ_VREG(HEVC_DBLK_CFG0);
    WRITE_VREG(HEVC_DBLK_CFG0, rdata32 | (0x1<<2));

    // SAO
    rdata32 = READ_VREG(HEVC_SAO_CTRL1);
    WRITE_VREG(HEVC_SAO_CTRL1, rdata32 | (0x1<<2));

    // MPRED
    rdata32 = READ_VREG(HEVC_MPRED_CTRL1);
    WRITE_VREG(HEVC_MPRED_CTRL1, rdata32 | (0x1<<24));

    // PARSER
    rdata32 = READ_VREG(HEVC_STREAM_CONTROL);
    WRITE_VREG(HEVC_STREAM_CONTROL, rdata32 | (0x1<<15));
    rdata32 = READ_VREG(HEVC_SHIFT_CONTROL);
    WRITE_VREG(HEVC_SHIFT_CONTROL, rdata32 | (0x1<<15));
    rdata32 = READ_VREG(HEVC_CABAC_CONTROL);
    WRITE_VREG(HEVC_CABAC_CONTROL, rdata32 | (0x1<<13));
    rdata32 = READ_VREG(HEVC_PARSER_CORE_CONTROL);
    WRITE_VREG(HEVC_PARSER_CORE_CONTROL, rdata32 | (0x1<<15));
    rdata32 = READ_VREG(HEVC_PARSER_INT_CONTROL);
    WRITE_VREG(HEVC_PARSER_INT_CONTROL, rdata32 | (0x1<<15));
    rdata32 = READ_VREG(HEVC_PARSER_IF_CONTROL);
    WRITE_VREG(HEVC_PARSER_IF_CONTROL, rdata32 | (0x3<<5) | (0x3<<2) | (0x3<<0));

    // IPP
    rdata32 = READ_VREG(HEVCD_IPP_DYNCLKGATE_CONFIG);
    WRITE_VREG(HEVCD_IPP_DYNCLKGATE_CONFIG, rdata32 | 0xffffffff);

    // MCRCC
    rdata32 = READ_VREG(HEVCD_MCRCC_CTL1);
    WRITE_VREG(HEVCD_MCRCC_CTL1, rdata32 | (0x1<<3));
}
#endif

#ifdef MCRCC_ENABLE
static void  config_mcrcc_axi_hw (int slice_type)
{
    unsigned int rdata32;
    unsigned int rdata32_2;

    WRITE_VREG(HEVCD_MCRCC_CTL1, 0x2); // reset mcrcc
    
    if ( slice_type  == 2 ) { // I-PIC
        WRITE_VREG(HEVCD_MCRCC_CTL1, 0x0); // remove reset -- disables clock 
        return;
    }

    if ( slice_type == 0 ) {  // B-PIC
        // Programme canvas0 
        WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (0 << 8) | (0<<1) | 0);
        rdata32 = READ_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
        rdata32 = rdata32 & 0xffff;
        rdata32 = rdata32 | ( rdata32 << 16);
        WRITE_VREG(HEVCD_MCRCC_CTL2, rdata32);
   
        // Programme canvas1 
        WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (16 << 8) | (1<<1) | 0);
        rdata32_2 = READ_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
        rdata32_2 = rdata32_2 & 0xffff;
        rdata32_2 = rdata32_2 | ( rdata32_2 << 16);
        if( rdata32 == rdata32_2 ) {
            rdata32_2 = READ_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
            rdata32_2 = rdata32_2 & 0xffff;
            rdata32_2 = rdata32_2 | ( rdata32_2 << 16);
        }
        WRITE_VREG(HEVCD_MCRCC_CTL3, rdata32_2);
    } else { // P-PIC 
        WRITE_VREG(HEVCD_MPP_ANC_CANVAS_ACCCONFIG_ADDR, (0 << 8) | (1<<1) | 0);
        rdata32 = READ_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
        rdata32 = rdata32 & 0xffff;
        rdata32 = rdata32 | ( rdata32 << 16);
        WRITE_VREG(HEVCD_MCRCC_CTL2, rdata32);
   
        // Programme canvas1 
        rdata32 = READ_VREG(HEVCD_MPP_ANC_CANVAS_DATA_ADDR);
        rdata32 = rdata32 & 0xffff;
        rdata32 = rdata32 | ( rdata32 << 16);
        WRITE_VREG(HEVCD_MCRCC_CTL3, rdata32);
    }

    WRITE_VREG(HEVCD_MCRCC_CTL1, 0xff0); // enable mcrcc progressive-mode 
    return;
}
#endif


static void  config_title_hw(hevc_stru_t* hevc, int sao_vb_size, int sao_mem_unit)
{
	WRITE_VREG(HEVC_sao_mem_unit, sao_mem_unit);
	WRITE_VREG(HEVC_SAO_ABV, hevc->work_space_buf->sao_abv.buf_start);
	WRITE_VREG(HEVC_sao_vb_size, sao_vb_size);
	WRITE_VREG(HEVC_SAO_VB, hevc->work_space_buf->sao_vb.buf_start);
}

static void config_mpred_hw(hevc_stru_t* hevc)
{
    int i;
    unsigned int data32;
    PIC_t* cur_pic = hevc->cur_pic;
    PIC_t* col_pic = hevc->col_pic;
    int     AMVP_MAX_NUM_CANDS_MEM=3;
    int     AMVP_MAX_NUM_CANDS=2;
    int     NUM_CHROMA_MODE=5;
    int     DM_CHROMA_IDX=36;
    int     above_ptr_ctrl =0;
    int     buffer_linear =1;
    int     cu_size_log2 =3;

    int     mpred_mv_rd_start_addr ;
    int     mpred_curr_lcu_x;
    int     mpred_curr_lcu_y;
    int     mpred_above_buf_start ;
    int     mpred_mv_rd_ptr ;
    int     mpred_mv_rd_ptr_p1 ;
    int     mpred_mv_rd_end_addr;
    int     MV_MEM_UNIT;
    int     mpred_mv_wr_ptr ;
    int     *ref_poc_L0, *ref_poc_L1;

    int     above_en;
    int     mv_wr_en;
    int     mv_rd_en;
    int     col_isIntra;
    if(hevc->slice_type!=2)
    {
        above_en=1;
        mv_wr_en=1;
        mv_rd_en=1;
        col_isIntra=0;
    }
    else 
    {
        above_en=1;
        mv_wr_en=1;
        mv_rd_en=0;
        col_isIntra=0;
    }

    mpred_mv_rd_start_addr=col_pic->mpred_mv_wr_start_addr;
    data32 = READ_VREG(HEVC_MPRED_CURR_LCU);
    mpred_curr_lcu_x   =data32 & 0xffff;
    mpred_curr_lcu_y   =(data32>>16) & 0xffff;
    
    MV_MEM_UNIT=hevc->lcu_size_log2==6 ? 0x200 : hevc->lcu_size_log2==5 ? 0x80 : 0x20;
    mpred_mv_rd_ptr = mpred_mv_rd_start_addr  + (hevc->slice_addr*MV_MEM_UNIT);
    
    mpred_mv_rd_ptr_p1  =mpred_mv_rd_ptr+MV_MEM_UNIT;
    mpred_mv_rd_end_addr=mpred_mv_rd_start_addr + ((hevc->lcu_x_num*hevc->lcu_y_num)*MV_MEM_UNIT);
    
    mpred_above_buf_start = hevc->work_space_buf->mpred_above.buf_start;
    
    mpred_mv_wr_ptr = cur_pic->mpred_mv_wr_start_addr  + (hevc->slice_addr*MV_MEM_UNIT);
    
    if(debug&H265_DEBUG_BUFMGR) 
        printk("cur pic index %d  col pic index %d\n", cur_pic->index, col_pic->index);
    
    WRITE_VREG(HEVC_MPRED_MV_WR_START_ADDR,cur_pic->mpred_mv_wr_start_addr);
    WRITE_VREG(HEVC_MPRED_MV_RD_START_ADDR,mpred_mv_rd_start_addr);
    
    data32 = ((hevc->lcu_x_num - hevc->tile_width_lcu)*MV_MEM_UNIT);
    WRITE_VREG(HEVC_MPRED_MV_WR_ROW_JUMP,data32);
    WRITE_VREG(HEVC_MPRED_MV_RD_ROW_JUMP,data32);
    
    data32 = READ_VREG(HEVC_MPRED_CTRL0);
    data32  =   ( 
        hevc->slice_type | 
        hevc->new_pic<<2 |
        hevc->new_tile<<3|
        hevc->isNextSliceSegment<<4|
        hevc->TMVPFlag<<5|
        hevc->LDCFlag<<6|
        hevc->ColFromL0Flag<<7|
        above_ptr_ctrl<<8 |  
        above_en<<9|
        mv_wr_en<<10|
        mv_rd_en<<11|
        col_isIntra<<12|
        buffer_linear<<13|
        hevc->LongTerm_Curr<<14|
        hevc->LongTerm_Col<<15|
        hevc->lcu_size_log2<<16|
        cu_size_log2<<20|
        hevc->plevel<<24 
        );
    WRITE_VREG(HEVC_MPRED_CTRL0,data32);
    
    data32 = READ_VREG(HEVC_MPRED_CTRL1);
    data32  =   ( 
#if 0
//no set in m8baby test1902
       (data32 & (0x1<<24)) |  // Don't override clk_forced_on , 
#endif       
        hevc->MaxNumMergeCand | 
        AMVP_MAX_NUM_CANDS<<4 |
        AMVP_MAX_NUM_CANDS_MEM<<8|
        NUM_CHROMA_MODE<<12|
        DM_CHROMA_IDX<<16
        );
    WRITE_VREG(HEVC_MPRED_CTRL1,data32);
    
    data32  =   (
          hevc->pic_w|
          hevc->pic_h<<16   
          );
    WRITE_VREG(HEVC_MPRED_PIC_SIZE,data32);
    
    data32  =   (
          (hevc->lcu_x_num-1)   |
          (hevc->lcu_y_num-1)<<16
          );
    WRITE_VREG(HEVC_MPRED_PIC_SIZE_LCU,data32);
    
    data32  =   (
          hevc->tile_start_lcu_x   |
          hevc->tile_start_lcu_y<<16
          );
    WRITE_VREG(HEVC_MPRED_TILE_START,data32);
    
    data32  =   (
          hevc->tile_width_lcu   |
          hevc->tile_height_lcu<<16
          );
    WRITE_VREG(HEVC_MPRED_TILE_SIZE_LCU,data32);
    
    data32  =   (
          hevc->RefNum_L0   |
          hevc->RefNum_L1<<8|
          0
          //col_RefNum_L0<<16|
          //col_RefNum_L1<<24
          );
    WRITE_VREG(HEVC_MPRED_REF_NUM,data32);
    
    data32  =   (
          hevc->LongTerm_Ref   
          );
    WRITE_VREG(HEVC_MPRED_LT_REF,data32);
    
    
    data32=0;
    for(i=0;i<hevc->RefNum_L0;i++)data32=data32|(1<<i);
    WRITE_VREG(HEVC_MPRED_REF_EN_L0,data32);
    
    data32=0;
    for(i=0;i<hevc->RefNum_L1;i++)data32=data32|(1<<i);
    WRITE_VREG(HEVC_MPRED_REF_EN_L1,data32);
    
    
    WRITE_VREG(HEVC_MPRED_CUR_POC,hevc->curr_POC);
    WRITE_VREG(HEVC_MPRED_COL_POC,hevc->Col_POC);
    
    //below MPRED Ref_POC_xx_Lx registers must follow Ref_POC_xx_L0 -> Ref_POC_xx_L1 in pair write order!!!
    ref_poc_L0      = &(cur_pic->m_aiRefPOCList0[cur_pic->slice_idx][0]);
    ref_poc_L1      = &(cur_pic->m_aiRefPOCList1[cur_pic->slice_idx][0]);

    WRITE_VREG(HEVC_MPRED_L0_REF00_POC,ref_poc_L0[0]);
    WRITE_VREG(HEVC_MPRED_L1_REF00_POC,ref_poc_L1[0]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF01_POC,ref_poc_L0[1]);
    WRITE_VREG(HEVC_MPRED_L1_REF01_POC,ref_poc_L1[1]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF02_POC,ref_poc_L0[2]);
    WRITE_VREG(HEVC_MPRED_L1_REF02_POC,ref_poc_L1[2]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF03_POC,ref_poc_L0[3]);
    WRITE_VREG(HEVC_MPRED_L1_REF03_POC,ref_poc_L1[3]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF04_POC,ref_poc_L0[4]);
    WRITE_VREG(HEVC_MPRED_L1_REF04_POC,ref_poc_L1[4]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF05_POC,ref_poc_L0[5]);
    WRITE_VREG(HEVC_MPRED_L1_REF05_POC,ref_poc_L1[5]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF06_POC,ref_poc_L0[6]);
    WRITE_VREG(HEVC_MPRED_L1_REF06_POC,ref_poc_L1[6]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF07_POC,ref_poc_L0[7]);
    WRITE_VREG(HEVC_MPRED_L1_REF07_POC,ref_poc_L1[7]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF08_POC,ref_poc_L0[8]);
    WRITE_VREG(HEVC_MPRED_L1_REF08_POC,ref_poc_L1[8]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF09_POC,ref_poc_L0[9]);
    WRITE_VREG(HEVC_MPRED_L1_REF09_POC,ref_poc_L1[9]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF10_POC,ref_poc_L0[10]);
    WRITE_VREG(HEVC_MPRED_L1_REF10_POC,ref_poc_L1[10]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF11_POC,ref_poc_L0[11]);
    WRITE_VREG(HEVC_MPRED_L1_REF11_POC,ref_poc_L1[11]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF12_POC,ref_poc_L0[12]);
    WRITE_VREG(HEVC_MPRED_L1_REF12_POC,ref_poc_L1[12]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF13_POC,ref_poc_L0[13]);
    WRITE_VREG(HEVC_MPRED_L1_REF13_POC,ref_poc_L1[13]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF14_POC,ref_poc_L0[14]);
    WRITE_VREG(HEVC_MPRED_L1_REF14_POC,ref_poc_L1[14]);
    
    WRITE_VREG(HEVC_MPRED_L0_REF15_POC,ref_poc_L0[15]);
    WRITE_VREG(HEVC_MPRED_L1_REF15_POC,ref_poc_L1[15]);
    
    
    if(hevc->new_pic)
    {
        WRITE_VREG(HEVC_MPRED_ABV_START_ADDR,mpred_above_buf_start);
        WRITE_VREG(HEVC_MPRED_MV_WPTR,mpred_mv_wr_ptr);
        //WRITE_VREG(HEVC_MPRED_MV_RPTR,mpred_mv_rd_ptr);
        WRITE_VREG(HEVC_MPRED_MV_RPTR,mpred_mv_rd_start_addr);
    }
    else if(!hevc->isNextSliceSegment)
    {
        //WRITE_VREG(HEVC_MPRED_MV_RPTR,mpred_mv_rd_ptr_p1);
        WRITE_VREG(HEVC_MPRED_MV_RPTR,mpred_mv_rd_ptr);
    }
    
    WRITE_VREG(HEVC_MPRED_MV_RD_END_ADDR,mpred_mv_rd_end_addr);
}

static void config_sao_hw(hevc_stru_t* hevc, param_t* params)
{
    unsigned int data32, data32_2;
    int misc_flag0 = hevc->misc_flag0; 
    int slice_deblocking_filter_disabled_flag = 0;
               
    int mc_buffer_size_u_v = hevc->lcu_total*hevc->lcu_size*hevc->lcu_size/2;
    int mc_buffer_size_u_v_h = (mc_buffer_size_u_v + 0xffff)>>16;
    PIC_t* cur_pic = hevc->cur_pic;
            
    data32 = READ_VREG(HEVC_SAO_CTRL0);	
    data32 &= (~0xf);
    data32 |= hevc->lcu_size_log2;
    WRITE_VREG(HEVC_SAO_CTRL0, data32);

    data32  =   (
            hevc->pic_w|
            hevc->pic_h<<16   
            );
    WRITE_VREG(HEVC_SAO_PIC_SIZE , data32);

    data32  =   (
            (hevc->lcu_x_num-1)   |
            (hevc->lcu_y_num-1)<<16
            );
    WRITE_VREG(HEVC_SAO_PIC_SIZE_LCU , data32);
    
    if(hevc->new_pic) WRITE_VREG(HEVC_SAO_Y_START_ADDR,0xffffffff);
    data32 = cur_pic->mc_y_adr;
    WRITE_VREG(HEVC_SAO_Y_START_ADDR,data32);
    
    data32 = (mc_buffer_size_u_v_h<<16)<<1;
    //printk("data32 = %x, mc_buffer_size_u_v_h = %x, lcu_total = %x, lcu_size = %x\n", data32, mc_buffer_size_u_v_h, lcu_total, lcu_size);
    WRITE_VREG(HEVC_SAO_Y_LENGTH ,data32);

    data32 = cur_pic->mc_u_v_adr;
    WRITE_VREG(HEVC_SAO_C_START_ADDR,data32);

    data32 = (mc_buffer_size_u_v_h<<16);
    WRITE_VREG(HEVC_SAO_C_LENGTH  ,data32);

    /* multi tile to do... */
    data32 = cur_pic->mc_y_adr;
    WRITE_VREG(HEVC_SAO_Y_WPTR ,data32);

    data32 = cur_pic->mc_u_v_adr;
    WRITE_VREG(HEVC_SAO_C_WPTR ,data32);
    
    // DBLK CONFIG HERE
    if(hevc->new_pic){
        data32  =   (
            hevc->pic_w|
            hevc->pic_h<<16   
        );
        WRITE_VREG( HEVC_DBLK_CFG2, data32);
        
        if((misc_flag0>>PCM_ENABLE_FLAG_BIT)&0x1)
            data32 = ((misc_flag0>>PCM_LOOP_FILTER_DISABLED_FLAG_BIT)&0x1)<<3;
        else data32 = 0;
        data32 |= (((params->p.pps_cb_qp_offset&0x1f)<<4)|((params->p.pps_cr_qp_offset&0x1f)<<9));
        data32 |= (hevc->lcu_size==64)?0:((hevc->lcu_size==32)?1:2);
        
        WRITE_VREG( HEVC_DBLK_CFG1, data32);
    }

#if 0
    data32 = READ_VREG( HEVC_SAO_CTRL1);
    data32 &= (~0x3000);
    data32 |= (MEM_MAP_MODE << 12); // [13:12] axi_aformat, 0-Linear, 1-32x32, 2-64x32
    WRITE_VREG( HEVC_SAO_CTRL1, data32);
    
    data32 = READ_VREG( HEVCD_IPP_AXIIF_CONFIG);
    data32 &= (~0x30);
    data32 |= (MEM_MAP_MODE << 4); // [5:4]    -- address_format 00:linear 01:32x32 10:64x32
    WRITE_VREG( HEVCD_IPP_AXIIF_CONFIG, data32);
#else
// m8baby test1902
   data32 = READ_VREG( HEVC_SAO_CTRL1);
	data32 &= (~0x3000);
	data32 |= (MEM_MAP_MODE << 12); // [13:12] axi_aformat, 0-Linear, 1-32x32, 2-64x32
    data32 &= (~0xff0);
    //data32 |= 0x670;  // Big-Endian per 64-bit
    data32 |= 0x880;  // Big-Endian per 64-bit
    WRITE_VREG( HEVC_SAO_CTRL1, data32);

    data32 = READ_VREG( HEVCD_IPP_AXIIF_CONFIG);
	data32 &= (~0x30);
	data32 |= (MEM_MAP_MODE << 4); // [5:4]    -- address_format 00:linear 01:32x32 10:64x32
	data32 &= (~0xF);
    data32 |= 0x8;    // Big-Endian per 64-bit
    WRITE_VREG( HEVCD_IPP_AXIIF_CONFIG, data32);
#endif    
    data32 = 0;	
    data32_2 = READ_VREG( HEVC_SAO_CTRL0);
    data32_2 &= (~0x300);
    //slice_deblocking_filter_disabled_flag = 0; //ucode has handle it , so read it from ucode directly
    //printk("\nconfig dblk HEVC_DBLK_CFG9: misc_flag0 %x tile_enabled %x; data32 is:", misc_flag0, tile_enabled);
    if(hevc->tile_enabled) {
        data32 |= ((misc_flag0>>LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT)&0x1)<<0;
        data32_2 |= ((misc_flag0>>LOOP_FILER_ACROSS_TILES_ENABLED_FLAG_BIT)&0x1)<<8;
    }
    slice_deblocking_filter_disabled_flag =	(misc_flag0>>SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT)&0x1;	//ucode has handle it , so read it from ucode directly
    if((misc_flag0&(1<<DEBLOCKING_FILTER_OVERRIDE_ENABLED_FLAG_BIT))
        &&(misc_flag0&(1<<DEBLOCKING_FILTER_OVERRIDE_FLAG_BIT))){
        //slice_deblocking_filter_disabled_flag =	(misc_flag0>>SLICE_DEBLOCKING_FILTER_DISABLED_FLAG_BIT)&0x1;	//ucode has handle it , so read it from ucode directly
        data32 |= slice_deblocking_filter_disabled_flag<<2;
        if(debug&H265_DEBUG_BUFMGR) printk("(1,%x)", data32);
        if(!slice_deblocking_filter_disabled_flag){
            data32 |= (params->p.slice_beta_offset_div2&0xf)<<3;
            data32 |= (params->p.slice_tc_offset_div2&0xf)<<7;
            if(debug&H265_DEBUG_BUFMGR) printk("(2,%x)", data32);
        }
    }
    else{
        data32 |= ((misc_flag0>>PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT)&0x1)<<2;
        if(debug&H265_DEBUG_BUFMGR) printk("(3,%x)", data32);
        if(((misc_flag0>>PPS_DEBLOCKING_FILTER_DISABLED_FLAG_BIT)&0x1)==0){
            data32 |= (params->p.pps_beta_offset_div2&0xf)<<3;
            data32 |= (params->p.pps_tc_offset_div2&0xf)<<7;
            if(debug&H265_DEBUG_BUFMGR) printk("(4,%x)", data32);
        }
    }
    if((misc_flag0&(1<<PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT))&&
       ((misc_flag0&(1<<SLICE_SAO_LUMA_FLAG_BIT))||(misc_flag0&(1<<SLICE_SAO_CHROMA_FLAG_BIT))||(!slice_deblocking_filter_disabled_flag))) {
        data32 |= ((misc_flag0>>SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)&0x1)<<1;
        data32_2 |= ((misc_flag0>>SLICE_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)&0x1)<<9;
        if(debug&H265_DEBUG_BUFMGR) printk("(5,%x)\n", data32);
    }
    else{
        data32 |= ((misc_flag0>>PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)&0x1)<<1;
        data32_2 |= ((misc_flag0>>PPS_LOOP_FILTER_ACROSS_SLICES_ENABLED_FLAG_BIT)&0x1)<<9;
        if(debug&H265_DEBUG_BUFMGR) printk("(6,%x)\n", data32);
    }
    WRITE_VREG( HEVC_DBLK_CFG9, data32);
    WRITE_VREG( HEVC_SAO_CTRL0, data32_2);
}                

static PIC_t* get_new_pic(hevc_stru_t* hevc, param_t* rpm_param)
{
    PIC_t* new_pic;
    PIC_t* pic;
    new_pic = out_q(&hevc->free_pic_list);
    if(new_pic == NULL){
        /* recycle un-used pic */
        int ii = 0;
      
        while(1){
            pic = get_pic_by_IDX(hevc, ii++);
            if(pic == NULL)
                break;
            if(pic->output_mark == 0 && pic->referenced == 0
                    && pic->output_ready == 0
                ){
                if(new_pic){
                    if(pic->POC < new_pic->POC)
                        new_pic = pic;
                }
                else{
                    new_pic = pic;
                }
            }
        }
        if(new_pic == NULL){
            //printk("Error: Buffer management, no free buffer\n");
            return NULL;
        }
    }
    else{
        in_q(&hevc->decode_pic_list, new_pic);
    }
    new_pic->decode_idx = hevc->decode_idx;
    new_pic->slice_idx = 0;
    new_pic->referenced = 1;
    new_pic->output_mark = 0;
    new_pic->recon_mark = 0;
    new_pic->error_mark = 0;
    //new_pic->output_ready = 0;
    new_pic->num_reorder_pic = rpm_param->p.sps_num_reorder_pics_0;
    new_pic->POC = hevc->curr_POC;
    return new_pic;
}

static int get_display_pic_num(hevc_stru_t* hevc)
{
    int ii = 0;
    PIC_t* pic;
    int num = 0;      
    while(1){
        pic = get_pic_by_IDX(hevc, ii++);
        if(pic == NULL)
            break;
        if(pic->output_ready == 1){
            num++;
        }
    }
    return num;
}

static int hevc_slice_segment_header_process(hevc_stru_t* hevc, param_t* rpm_param, int decode_pic_begin)
{
    int i;
    int     lcu_x_num_div;
    int     lcu_y_num_div;
    int     Col_ref         ;
    if(hevc->wait_buf == 0){
        hevc->m_temporalId = rpm_param->p.m_temporalId;
        hevc->m_nalUnitType = rpm_param->p.m_nalUnitType;
        if(hevc->m_nalUnitType == NAL_UNIT_EOS){ 
            hevc->m_pocRandomAccess = MAX_INT; //add to fix RAP_B_Bossen_1
        }
        hevc->misc_flag0 = rpm_param->p.misc_flag0;
        if(rpm_param->p.first_slice_segment_in_pic_flag==0){
            hevc->slice_segment_addr = rpm_param->p.slice_segment_address;
            if(!rpm_param->p.dependent_slice_segment_flag){
                hevc->slice_addr = hevc->slice_segment_addr;
            }
        }
        else{
            hevc->slice_segment_addr = 0;
            hevc->slice_addr = 0;
        }
    
        hevc->iPrevPOC = hevc->curr_POC;
        hevc->slice_type =      (rpm_param->p.slice_type == I_SLICE ) ? 2 :
                          (rpm_param->p.slice_type == P_SLICE ) ? 1 :
                          (rpm_param->p.slice_type == B_SLICE ) ? 0 : 3;
        //hevc->curr_predFlag_L0=(hevc->slice_type==2) ? 0:1;
        //hevc->curr_predFlag_L1=(hevc->slice_type==0) ? 1:0;
        hevc->TMVPFlag	= rpm_param->p.slice_temporal_mvp_enable_flag;
        hevc->isNextSliceSegment=rpm_param->p.dependent_slice_segment_flag?1:0;
        
        hevc->pic_w           =rpm_param->p.pic_width_in_luma_samples;
        hevc->pic_h           =rpm_param->p.pic_height_in_luma_samples;
        if(hevc->pic_w == 0 || hevc->pic_h == 0 ){ //it will cause divide 0 error
            if(debug) printk("Fatal Error, pic_w = %d, pic_h = %d\n", hevc->pic_w, hevc->pic_h);
            return 3;
        }
        hevc->lcu_size        = 1<<(rpm_param->p.log2_min_coding_block_size_minus3+3+rpm_param->p.log2_diff_max_min_coding_block_size);
        if(hevc->lcu_size == 0){
            printk("Error, lcu_size = 0 (%d,%d)\n",rpm_param->p.log2_min_coding_block_size_minus3, rpm_param->p.log2_diff_max_min_coding_block_size);
            return 3;
        }
        hevc->lcu_size_log2   =log2i(hevc->lcu_size);
        lcu_x_num_div   =(hevc->pic_w/ hevc->lcu_size);
        lcu_y_num_div   =(hevc->pic_h/ hevc->lcu_size);
        hevc->lcu_x_num       =((hevc->pic_w% hevc->lcu_size) == 0) ? lcu_x_num_div : lcu_x_num_div+1;
        hevc->lcu_y_num       =((hevc->pic_h% hevc->lcu_size) == 0) ? lcu_y_num_div : lcu_y_num_div+1;
        hevc->lcu_total       =hevc->lcu_x_num*hevc->lcu_y_num;
    
    
        if(hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_IDR || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_IDR_N_LP){
            hevc->curr_POC = 0;
            if((hevc->m_temporalId - 1) == 0){
                hevc->iPrevTid0POC = hevc->curr_POC;
            }
        }
        else{
            int iMaxPOClsb = 1<<(rpm_param->p.log2_max_pic_order_cnt_lsb_minus4+4);
            int iPrevPOClsb;
            int iPrevPOCmsb;
            int iPOCmsb;
            int iPOClsb = rpm_param->p.POClsb;
            if(iMaxPOClsb==0){
                printk("error iMaxPOClsb is 0\n");    
                return 3;
            }        
            
            iPrevPOClsb = hevc->iPrevTid0POC%iMaxPOClsb;
            iPrevPOCmsb = hevc->iPrevTid0POC-iPrevPOClsb;
            
            if( ( iPOClsb  <  iPrevPOClsb ) && ( ( iPrevPOClsb - iPOClsb )  >=  ( iMaxPOClsb / 2 ) ) )
            {
                iPOCmsb = iPrevPOCmsb + iMaxPOClsb;
            }
            else if( (iPOClsb  >  iPrevPOClsb )  && ( (iPOClsb - iPrevPOClsb )  >  ( iMaxPOClsb / 2 ) ) ) 
            {
                iPOCmsb = iPrevPOCmsb - iMaxPOClsb;
            }
            else
            {
                iPOCmsb = iPrevPOCmsb;
            }
            if(debug&H265_DEBUG_BUFMGR){
                printk("iPrePOC  %d iMaxPOClsb %d iPOCmsb %d iPOClsb %d\n", hevc->iPrevTid0POC, iMaxPOClsb, iPOCmsb, iPOClsb);
            }
            if ( hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA
                || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLANT
                || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP )
            {
                // For BLA picture types, POCmsb is set to 0.
                iPOCmsb = 0;
            }
            hevc->curr_POC  =  (iPOCmsb+iPOClsb);
            if((hevc->m_temporalId - 1) == 0){
                hevc->iPrevTid0POC = hevc->curr_POC;
            }
            else{
                if(debug&H265_DEBUG_BUFMGR){
                    printk("m_temporalID is %d\n", hevc->m_temporalId); 		
                }
            }
        }
        hevc->RefNum_L0       =rpm_param->p.num_ref_idx_l0_active;
        hevc->RefNum_L1       =rpm_param->p.num_ref_idx_l1_active;
        
        //if(curr_POC==0x10) dump_lmem();
        
        /* skip RASL pictures after CRA/BLA pictures */
        if(hevc->m_pocRandomAccess == MAX_INT){ //first picture
            if (   hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_CRA || 
                hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLANT || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP )
            {
                hevc->m_pocRandomAccess = hevc->curr_POC;
            }
            else{
                hevc->m_pocRandomAccess = - MAX_INT;
            }
        }
        else if (hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLANT || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_BLA_N_LP )
        {
            hevc->m_pocRandomAccess = hevc->curr_POC;
        }
        else if((hevc->curr_POC<hevc->m_pocRandomAccess)&&(hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_RASL_N || hevc->m_nalUnitType == NAL_UNIT_CODED_SLICE_TFD)){ //skip
            if(debug) printk("RASL picture with POC %d < %d (RandomAccess point POC), skip it\n", hevc->curr_POC, hevc->m_pocRandomAccess);
            return 1;
        }
    
        WRITE_VREG(HEVC_WAIT_FLAG, READ_VREG(HEVC_WAIT_FLAG)|0x2);            
        hevc->skip_flag = 0;
        /**/
        
        //	if((iPrevPOC != curr_POC)){
        if(rpm_param->p.slice_segment_address == 0){
            PIC_t* pic;
            hevc->new_pic=1;
            /**/
            if(use_cma == 0){
                if(hevc->pic_list_init_flag == 0){
                    init_pic_list(hevc);
                    init_buf_spec(hevc);
                    hevc->pic_list_init_flag = 3;
                }
            }
            
            if(debug&H265_DEBUG_BUFMGR_MORE) dump_pic_list(hevc);
            /* prev pic */
            if(hevc->curr_POC!=0){
                PIC_t* pic_display;
                pic = get_pic_by_POC(hevc, hevc->iPrevPOC);
                if(pic){
                    /*PB skip control*/
                    if(pic->error_mark==0 && hevc->PB_skip_mode==1){
                        hevc->ignore_bufmgr_error|=0x1;  //start decoding after first I
                    }
                    if(hevc->ignore_bufmgr_error&1){
                        if(hevc->PB_skip_count_after_decoding>0){
                            hevc->PB_skip_count_after_decoding--;
                        }
                        else{
                            hevc->ignore_bufmgr_error|=0x2; //start displaying
                        }
                    }
                    /**/
                    pic->output_mark = 1;
                    pic->recon_mark = 1;
                }
                do{			
                    pic_display = output_pic(hevc, 0);

                    if(pic_display){
                        if((pic_display->error_mark && ((hevc->ignore_bufmgr_error&0x2)==0))
                            ||(debug&H265_DEBUG_DISPLAY_CUR_FRAME)||(debug&H265_DEBUG_NO_DISPLAY)){
                            pic_display->output_ready = 0;
                            if(debug&H265_DEBUG_BUFMGR) printk("[Buffer Management] Display: POC %d, decoding index %d ==> Debug mode or error, recycle it\n", pic_display->POC, pic_display->decode_idx);
                        }
                        else{                    
                            prepare_display_buf(hevc, pic_display->index, pic_display->stream_offset, pic_display->slice_type);
                            if(debug&H265_DEBUG_BUFMGR) printk("[Buffer Management] Display: POC %d, decoding index %d\n", pic_display->POC, pic_display->decode_idx);
                        }
                    }
                }while(pic_display);
            }
            else if(hevc->iPrevPOC!=0){ /* flush */
                PIC_t* pic_display;
                if(debug&H265_DEBUG_BUFMGR){
                    printk("[Buffer Management] current pic is IDR, clear referenced flag of all buffers\n");
                }
                if(debug&H265_DEBUG_BUFMGR){
                    dump_pic_list(hevc);
                }
                pic = get_pic_by_POC(hevc, hevc->iPrevPOC);
                if(pic){
                    /*PB skip control*/
                    if(pic->error_mark==0 && hevc->PB_skip_mode==1){
                        hevc->ignore_bufmgr_error|=0x1;  //start decoding after first I
                    }
                    if(hevc->ignore_bufmgr_error&1){
                        if(hevc->PB_skip_count_after_decoding>0){
                            hevc->PB_skip_count_after_decoding--;
                        }
                        else{
                            hevc->ignore_bufmgr_error|=0x2; //start displaying
                        }
                    }
                    /**/
                    pic->output_mark = 1;
                    pic->recon_mark = 1;
                }
                do{			
                    pic_display = output_pic(hevc, 1);
    
                    if(pic_display){
                        pic_display->referenced = 0;
                        if((pic_display->error_mark && ((hevc->ignore_bufmgr_error&0x2)==0))
                            ||(debug&H265_DEBUG_DISPLAY_CUR_FRAME)||(debug&H265_DEBUG_NO_DISPLAY)){
                       			 pic_display->output_ready = 0;
                             if(debug&H265_DEBUG_BUFMGR) printk("[Buffer Management] Display: POC %d, decoding index %d ==> Debug mode or error, recycle it\n", pic_display->POC, pic_display->decode_idx);
                        }
                        else{
                            prepare_display_buf(hevc, pic_display->index, pic_display->stream_offset, pic_display->slice_type);
                            if(debug&H265_DEBUG_BUFMGR) printk("[Buffer Management] Display: POC %d, decoding index %d\n", pic_display->POC, pic_display->decode_idx);
                        }
                    }
                }while(pic_display);
            }
            
            apply_ref_pic_set(hevc, hevc->curr_POC, rpm_param); //update referenced of old pictures (cur_pic->referenced is 1 and not updated)
            /* new pic */
            hevc->cur_pic = get_new_pic(hevc, rpm_param);
            if(hevc->cur_pic == NULL){
                if(debug&H265_DEBUG_BUFMGR){
                    dump_pic_list(hevc);
                }
                hevc->wait_buf = 1;
                return -1;
            }            
            if(debug&H265_DEBUG_DISPLAY_CUR_FRAME){
                hevc->cur_pic->output_ready = 1;
                prepare_display_buf(hevc, hevc->cur_pic->index, READ_VREG(HEVC_SHIFT_BYTE_COUNT), hevc->cur_pic->slice_type);    
                hevc->wait_buf = 2;
                return -1;
            }        
        }
        else{
            if(hevc->pic_list_init_flag!=3 || hevc->cur_pic==NULL){
                return 3; //make it decode from the first slice segment    
            }
            hevc->cur_pic->slice_idx++;
            hevc->new_pic =0;
        }
    }
    else{
        if(hevc->wait_buf == 1){
            hevc->cur_pic = get_new_pic(hevc, rpm_param);
            if(hevc->cur_pic == NULL){
                return -1;
            }
            hevc->wait_buf = 0;            
        }
        else if(hevc->wait_buf == 2){ // for case: debug&H265_DEBUG_DISPLAY_CUR_FRAME
            if(get_display_pic_num(hevc)>1){ //start decoding only when video is displaying cur buf
                return -1;
            }
            hevc->wait_buf = 0;    
        }
        if(debug&H265_DEBUG_BUFMGR_MORE) dump_pic_list(hevc);
    }
        
    if(hevc->new_pic){
        int sao_mem_unit = ((hevc->lcu_size/8)*2 + 4 )<<4;
        int pic_height_cu = (hevc->pic_h+hevc->lcu_size-1)/hevc->lcu_size;
        int pic_width_cu = (hevc->pic_w+hevc->lcu_size-1)/hevc->lcu_size;
		    int sao_vb_size = (sao_mem_unit+(2<<4))*pic_height_cu;
		    //int sao_abv_size = sao_mem_unit*pic_width_cu;
        if(debug&H265_DEBUG_BUFMGR){
            printk("=========>%s decode index %d\n", __func__, hevc->decode_idx);
        }
        hevc->decode_idx++;
        update_tile_info(hevc, pic_width_cu , pic_height_cu , sao_mem_unit, rpm_param);

        config_title_hw(hevc, sao_vb_size, sao_mem_unit);
    }
    
    if(hevc->iPrevPOC != hevc->curr_POC){
        hevc->new_tile = 1;
        hevc->tile_x = 0;
        hevc->tile_y = 0;
        hevc->tile_y_x = 0;
        if(debug&H265_DEBUG_BUFMGR){
            printk("new_tile (new_pic) tile_x=%d, tile_y=%d\n", hevc->tile_x, hevc->tile_y);
        }
    }
    else if(hevc->tile_enabled){
        if(debug&H265_DEBUG_BUFMGR){
            printk("slice_segment_address is %d\n", rpm_param->p.slice_segment_address);
        }
        hevc->tile_y_x = get_tile_index(hevc, rpm_param->p.slice_segment_address, (hevc->pic_w+hevc->lcu_size-1)/hevc->lcu_size);
        if(hevc->tile_y_x != (hevc->tile_x|(hevc->tile_y<<8))){
            hevc->new_tile = 1;
            hevc->tile_x = hevc->tile_y_x&0xff;
            hevc->tile_y = (hevc->tile_y_x>>8)&0xff;
            if(debug&H265_DEBUG_BUFMGR){
                printk("new_tile segment_adr %d tile_x=%d, tile_y=%d\n", rpm_param->p.slice_segment_address, hevc->tile_x, hevc->tile_y);
            }
        }
        else{
            hevc->new_tile = 0;
        }	
    }
    else{
        hevc->new_tile = 0;
    }
    
    if(hevc->new_tile){
        hevc->tile_start_lcu_x = m_tile[hevc->tile_y][hevc->tile_x].start_cu_x;
        hevc->tile_start_lcu_y = m_tile[hevc->tile_y][hevc->tile_x].start_cu_y;
        hevc->tile_width_lcu  = m_tile[hevc->tile_y][hevc->tile_x].width;
        hevc->tile_height_lcu = m_tile[hevc->tile_y][hevc->tile_x].height;
    }
    
    set_ref_pic_list(hevc->cur_pic, rpm_param);
    
    
    Col_ref=rpm_param->p.collocated_ref_idx;
    
    hevc->LDCFlag = 0;        
    if(rpm_param->p.slice_type != I_SLICE){
        hevc->LDCFlag = 1;
        for(i=0; (i<hevc->RefNum_L0) && hevc->LDCFlag; i++){
            if(hevc->cur_pic->m_aiRefPOCList0[hevc->cur_pic->slice_idx][i]>hevc->curr_POC){
                hevc->LDCFlag = 0;			
            }
        }
        if(rpm_param->p.slice_type == B_SLICE){
            for(i=0; (i<hevc->RefNum_L1) && hevc->LDCFlag; i++){
                if(hevc->cur_pic->m_aiRefPOCList1[hevc->cur_pic->slice_idx][i]>hevc->curr_POC){
                    hevc->LDCFlag = 0;			
                }
            }
        }
    }
    
    hevc->ColFromL0Flag   =rpm_param->p.collocated_from_l0_flag;
    
    hevc->plevel          = rpm_param->p.log2_parallel_merge_level; //rpm_param->p.log2_parallel_merge_level>=2?rpm_param->p.log2_parallel_merge_level-2:0;
    hevc->MaxNumMergeCand = 5 - rpm_param->p.five_minus_max_num_merge_cand;
    
    hevc->LongTerm_Curr   =0; /* to do ... */
    hevc->LongTerm_Col    =0; /* to do ... */
    
    hevc->list_no = 0;	
    if(rpm_param->p.slice_type == B_SLICE ){
        hevc->list_no = 1-hevc->ColFromL0Flag;	
    }
    if(hevc->list_no==0){
        if(Col_ref<hevc->RefNum_L0)
            hevc->Col_POC = hevc->cur_pic->m_aiRefPOCList0[hevc->cur_pic->slice_idx][Col_ref];			
        else
            hevc->Col_POC = INVALID_POC;
    }
    else{
        if(Col_ref<hevc->RefNum_L1)
            hevc->Col_POC = hevc->cur_pic->m_aiRefPOCList1[hevc->cur_pic->slice_idx][Col_ref];			
        else
            hevc->Col_POC = INVALID_POC;
    }
    
    hevc->LongTerm_Ref    = 0; /* to do ... */

    if(hevc->slice_type!=2)
    {
        if(hevc->Col_POC != INVALID_POC){
            hevc->col_pic = get_ref_pic_by_POC(hevc, hevc->Col_POC);
            if(hevc->col_pic == NULL){
                hevc->cur_pic->error_mark = 1;
                if(debug) printk("WRONG, fail to get the picture of Col_POC\n");
            }
            else if(hevc->col_pic->error_mark){
                hevc->cur_pic->error_mark = 1;
                if(debug) printk("WRONG, Col_POC error_mark is 1\n");
            }

            if(hevc->cur_pic->error_mark && ((hevc->ignore_bufmgr_error&0x1)==0)){
                if(debug) printk("Discard this picture\n");
                return 2;    
            }
        }
        else{
            hevc->col_pic = hevc->cur_pic;
        }
    }//
    if(hevc->col_pic == NULL) hevc->col_pic = hevc->cur_pic;     

#ifdef BUFFER_MGR_ONLY
     return 0xf;
#else
     if(decode_pic_begin>0 && hevc->decode_idx<=decode_pic_begin)
          return 0xf;
#endif

    config_mc_buffer(hevc, hevc->cur_pic);

    if(hevc->cur_pic->error_mark && ((hevc->ignore_bufmgr_error&0x1)==0)){
        if(debug) printk("Discard this picture\n");
        return 2;    
    }
#ifdef MCRCC_ENABLE
    config_mcrcc_axi_hw(hevc->cur_pic->slice_type);
#endif
    config_mpred_hw(hevc);

    config_sao_hw(hevc, rpm_param);
    return 0;
}    

/**************************************************

h265 buffer management end

***************************************************/
static buff_t mc_buf_spec;

static hevc_stru_t gHevc;
    
static param_t  rpm_param;

static void hevc_local_uninit(void)
{
    if(gHevc.rpm_ptr){
        iounmap(gHevc.rpm_ptr);
        gHevc.rpm_ptr = NULL;
    }
    if(gHevc.lmem_ptr){
        iounmap(gHevc.lmem_ptr);
        gHevc.lmem_ptr = NULL;
    }
    if(gHevc.debug_ptr){
        iounmap(gHevc.debug_ptr);
        gHevc.debug_ptr = NULL;
    }
}

static int hevc_local_init(void)
{
    int ret = -1;
    BuffInfo_t* cur_buf_info = NULL;
    memset(&rpm_param, 0, sizeof(rpm_param));
    
    if (frame_width <= 1920 &&  frame_height <= 1088) {
        cur_buf_info = &amvh265_workbuff_spec[0]; //1080p work space
    }
    else{
        cur_buf_info = &amvh265_workbuff_spec[1]; //4k2k work space
    }
 
    init_buff_spec(cur_buf_info);

    mc_buf_spec.buf_start = (cur_buf_info->end_adr + 0xffff)&(~0xffff);
    mc_buf_spec.buf_size  = (mc_buf_spec.buf_end - mc_buf_spec.buf_start);
    
    hevc_init_stru(&gHevc, cur_buf_info, &mc_buf_spec);
    
    bit_depth_luma = 8;
    bit_depth_chroma = 8;
    
    if((debug&H265_DEBUG_SEND_PARAM_WITH_REG)==0){
        if(gHevc.rpm_ptr){
            iounmap(gHevc.rpm_ptr);
            gHevc.rpm_ptr = NULL;
        }
        
        gHevc.rpm_ptr = (unsigned short*)ioremap_nocache(cur_buf_info->rpm.buf_start, cur_buf_info->rpm.buf_size);
        if (!gHevc.rpm_ptr) {
                printk("%s: failed to remap rpm.buf_start\n", __func__);
                return ret;
        }
    }    

    if(debug&H265_DEBUG_UCODE){
        if(gHevc.lmem_ptr){
            iounmap(gHevc.lmem_ptr);
            gHevc.lmem_ptr = NULL;
        }
        if(gHevc.debug_ptr){
            iounmap(gHevc.debug_ptr);
            gHevc.debug_ptr = NULL;
        }
        
        gHevc.lmem_ptr = (unsigned short*)ioremap_nocache(cur_buf_info->lmem.buf_start, cur_buf_info->lmem.buf_size);
        if (!gHevc.lmem_ptr) {
                printk("%s: failed to remap lmem.buf_start\n", __func__);
                return ret;
        }
        
        gHevc.debug_ptr_size = 0x60; //cur_buf_info->pps.buf_size;
        gHevc.debug_ptr = (unsigned short*)ioremap_nocache(cur_buf_info->pps.buf_start, cur_buf_info->pps.buf_size);
        if (!gHevc.debug_ptr) {
                printk("%s: failed to remap lmem.buf_start\n", __func__);
                return ret;
        }
        
    }  
    ret = 0;  
    return ret;
}

/********************************************
 *  Mailbox command
 ********************************************/
#define CMD_FINISHED               0
#define CMD_ALLOC_VIEW             1
#define CMD_FRAME_DISPLAY          3
#define CMD_DEBUG                  10

static unsigned reserved_buffer;

#define DECODE_BUFFER_NUM_MAX    32
#define DISPLAY_BUFFER_NUM       6

#define video_domain_addr(adr) (adr&0x7fffffff)
#define DECODER_WORK_SPACE_SIZE 0x800000

typedef struct {
    unsigned int y_addr;
    unsigned int uv_addr;

    int y_canvas_index;
    int uv_canvas_index;
} buffer_spec_t;

static buffer_spec_t buffer_spec[DECODE_BUFFER_NUM_MAX+DISPLAY_BUFFER_NUM];

#define spec2canvas(x)  \
    (((x)->uv_canvas_index << 16) | \
     ((x)->uv_canvas_index << 8)  | \
     ((x)->y_canvas_index << 0))

#define VF_POOL_SIZE        32

static DECLARE_KFIFO(newframe_q, vframe_t *, VF_POOL_SIZE);
static DECLARE_KFIFO(display_q, vframe_t *, VF_POOL_SIZE);

static s32 vfbuf_use[DECODE_BUFFER_NUM_MAX];
static vframe_t vfpool[VF_POOL_SIZE];

static int init_buf_spec(hevc_stru_t* hevc)
{
    int i;
    int pic_width = hevc->pic_w;
    int pic_height = hevc->pic_h;

    //printk("%s1: %d %d\n", __func__, hevc->pic_w, hevc->pic_h);
    printk("%s2 %d %d \n", __func__, pic_width, pic_height);
    //pic_width = hevc->pic_w;
    //pic_height = hevc->pic_h;
    for(i=0; i<MAX_REF_PIC_NUM; i++) { 
        if (m_PIC[i].index == -1) {
            break;
        }

        buffer_spec[i].y_addr = m_PIC[i].mc_y_adr;
        buffer_spec[i].uv_addr = m_PIC[i].mc_u_v_adr;

        buffer_spec[i].y_canvas_index = 128 + i * 2;
        buffer_spec[i].uv_canvas_index = 128 + i * 2 + 1;

        canvas_config(128 + i * 2, buffer_spec[i].y_addr, ALIGN(pic_width, 64), ALIGN(pic_height, 32),
                      CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_64X32);
        canvas_config(128 + i * 2 + 1, buffer_spec[i].uv_addr, ALIGN(pic_width, 64), ALIGN(pic_height>>1, 32),
                      CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_64X32);
    }

    if(frame_width == 0 || frame_height == 0){
        frame_width = pic_width;
        frame_height = pic_height;   
        
    }

    return 0;
}

static void set_frame_info(vframe_t *vf)
{
    unsigned int ar;

    vf->width = frame_width;
    vf->height = frame_height;
    vf->duration = frame_dur;
    vf->duration_pulldown = 0;

    ar = min(frame_ar, (u32)DISP_RATIO_ASPECT_RATIO_MAX);
    vf->ratio_control = (ar << DISP_RATIO_ASPECT_RATIO_BIT);

    return;
}

static int vh265_vf_states(vframe_states_t *states, void* op_arg)
{
    unsigned long flags;
    spin_lock_irqsave(&lock, flags);

    states->vf_pool_size = VF_POOL_SIZE;
    states->buf_free_num = kfifo_len(&newframe_q);
    states->buf_avail_num = kfifo_len(&display_q);

    if(step == 2){
        states->buf_avail_num = 0;
    }
    spin_unlock_irqrestore(&lock, flags);
    return 0;
}

static vframe_t *vh265_vf_peek(void* op_arg)
{
    vframe_t *vf;
    if(step == 2){
        return NULL;
    }

    if (kfifo_peek(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static vframe_t *vh265_vf_get(void* op_arg)
{
    vframe_t *vf;

    if(step == 2){
        return NULL;
    }
    else if(step == 1){
        step = 2;
    }

    if (kfifo_get(&display_q, &vf)) {
        return vf;
    }

    return NULL;
}

static void vh265_vf_put(vframe_t *vf, void* op_arg)
{
    m_PIC[vf->index].output_ready = 0;
    kfifo_put(&newframe_q, (const vframe_t **)&vf);
    if(gHevc.wait_buf!=0){
        WRITE_VREG(HEVC_ASSIST_MBOX1_IRQ_REG, 0x1); 
    }
}

static int vh265_event_cb(int type, void *data, void *private_data)
{
    if(type & VFRAME_EVENT_RECEIVER_RESET){
#if 0
        unsigned long flags;
        amhevc_stop();
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_light_unreg_provider(&vh265_vf_prov);
#endif
        spin_lock_irqsave(&lock, flags);
        vh265_local_init();
        vh265_prot_init();
        spin_unlock_irqrestore(&lock, flags);
#ifndef CONFIG_POST_PROCESS_MANAGER
        vf_reg_provider(&vh265_vf_prov);
#endif
        amhevc_start();
#endif        
    }

    return 0;
}

static int prepare_display_buf(hevc_stru_t* hevc, int display_buff_id, int stream_offset, unsigned short slice_type)
{
    vframe_t *vf = NULL;
    if (kfifo_get(&newframe_q, &vf) == 0) {
        printk("fatal error, no available buffer slot.");
        return -1;
    }
    
    if (vf) {
        /*
        vfbuf_use[display_buff_id]++;
         */
        //if (pts_lookup_offset(PTS_TYPE_VIDEO, stream_offset, &vf->pts, 0) != 0) {
        if (pts_lookup_offset_us64(PTS_TYPE_VIDEO, stream_offset, &vf->pts, 0, &vf->pts_us64)!= 0){
#ifdef DEBUG_PTS
            pts_missed++;
#endif
            vf->pts = 0;
            vf->pts_us64 = 0;
        }
#ifdef DEBUG_PTS
        else {
            pts_hit++;
        }
#endif

        if ((hevc->pts_mode == PTS_NORMAL) && (vf->pts != 0) && get_frame_dur) {
            int pts_diff = (int)vf->pts - hevc->last_lookup_pts;

            if (pts_diff < 0) {
               hevc->pts_mode_switching_count++;
               hevc->pts_mode_recovery_count = 0;

               if (hevc->pts_mode_switching_count >= PTS_MODE_SWITCHING_THRESHOLD) {
                   hevc->pts_mode = PTS_NONE_REF_USE_DURATION;
                   printk("HEVC: pts lookup switch to none_ref_use_duration mode.\n");
               }

            } else {
               hevc->pts_mode_recovery_count++;
               if (hevc->pts_mode_recovery_count > PTS_MODE_SWITCHING_RECOVERY_THREASHOLD) {
                   hevc->pts_mode_switching_count = 0;
                   hevc->pts_mode_recovery_count = 0;
               }
            }
        }

        if (vf->pts != 0) {
            hevc->last_lookup_pts = vf->pts;
        }

        if ((hevc->pts_mode == PTS_NONE_REF_USE_DURATION) && (slice_type != 2)) {
            vf->pts = hevc->last_pts + DUR2PTS(frame_dur);
        }
        hevc->last_pts = vf->pts;

        if (vf->pts_us64 != 0) {
            hevc->last_lookup_pts_us64 = vf->pts_us64;
        }

        if ((hevc->pts_mode == PTS_NONE_REF_USE_DURATION) && (slice_type != 2)) {
            vf->pts_us64 = hevc->last_pts_us64 + (DUR2PTS(frame_dur)*100/9);
        }
        hevc->last_pts_us64 = vf->pts_us64;
        if((debug&H265_DEBUG_OUT_PTS)!=0){
            printk("H265 decoder out pts: vf->pts=%d, vf->pts_us64 = %lld\n", vf->pts, vf->pts_us64);
        }

        vf->index = display_buff_id;
        vf->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_FIELD;
        vf->type |= VIDTYPE_VIU_NV21;
        vf->canvas0Addr = vf->canvas1Addr = spec2canvas(&buffer_spec[display_buff_id]);
        set_frame_info(vf);

        kfifo_put(&display_q, (const vframe_t **)&vf);

        vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY, NULL);
    }

    return 0;
}

static int vh265_stop(void);
static s32 vh265_init(void);

static void hevc_recover(hevc_stru_t* hevc)
{

        u32 rem;
        unsigned hevc_shift_byte_count ;
        unsigned hevc_stream_start_addr;
        unsigned hevc_stream_end_addr ;
        unsigned hevc_stream_rd_ptr ;
        unsigned hevc_stream_wr_ptr ;
        unsigned hevc_stream_control;
        unsigned hevc_stream_fifo_ctl;
        unsigned hevc_stream_buf_size;
#if 0
            for(i=0; i<(hevc->debug_ptr_size/2); i+=4){
                int ii;
                for(ii=0; ii<4; ii++){
                    printk("%04x ", hevc->debug_ptr[i+3-ii]);
                }
                if(((i+ii)&0xf)==0)
                    printk("\n");
            }
#endif
#define ES_VID_MAN_RD_PTR            (1<<0)

        amhevc_stop();

        //reset
        WRITE_MPEG_REG(PARSER_VIDEO_RP, READ_VREG(HEVC_STREAM_RD_PTR));
        SET_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_VID_MAN_RD_PTR);

        hevc_stream_start_addr = READ_VREG(HEVC_STREAM_START_ADDR);
        hevc_stream_end_addr = READ_VREG(HEVC_STREAM_END_ADDR);
        hevc_stream_rd_ptr = READ_VREG(HEVC_STREAM_RD_PTR);
        hevc_stream_wr_ptr = READ_VREG(HEVC_STREAM_WR_PTR);
        hevc_stream_control = READ_VREG(HEVC_STREAM_CONTROL);
        hevc_stream_fifo_ctl = READ_VREG(HEVC_STREAM_FIFO_CTL);
        hevc_stream_buf_size = hevc_stream_end_addr - hevc_stream_start_addr;

        // HEVC streaming buffer will reset and restart from current hevc_stream_rd_ptr position
        // calculate HEVC_SHIFT_BYTE_COUNT value with the new position.
        hevc_shift_byte_count = READ_VREG(HEVC_SHIFT_BYTE_COUNT);
        if ((hevc->shift_byte_count_lo & (1<<31)) && ((hevc_shift_byte_count & (1<<31)) == 0)) {
            hevc->shift_byte_count += 0x100000000ULL;
        }
        div_u64_rem(hevc->shift_byte_count, hevc_stream_buf_size, &rem);
        hevc->shift_byte_count -= rem;
        hevc->shift_byte_count += hevc_stream_rd_ptr - hevc_stream_start_addr;
        if (rem > (hevc_stream_rd_ptr - hevc_stream_start_addr)) {
            hevc->shift_byte_count += hevc_stream_buf_size;
        }
        hevc->shift_byte_count_lo = (u32)hevc->shift_byte_count;

        WRITE_VREG(DOS_SW_RESET3, 
            //(1<<2)|
            (1<<3)|(1<<4)|(1<<8)|(1<<11)|(1<<12)|(1<<14)|(1<<15)|(1<<17)|(1<<18)|(1<<19));
        WRITE_VREG(DOS_SW_RESET3, 0);

        WRITE_VREG(HEVC_STREAM_START_ADDR, hevc_stream_start_addr);
        WRITE_VREG(HEVC_STREAM_END_ADDR, hevc_stream_end_addr);
        WRITE_VREG(HEVC_STREAM_RD_PTR, hevc_stream_rd_ptr);
        WRITE_VREG(HEVC_STREAM_WR_PTR, hevc_stream_wr_ptr);
        WRITE_VREG(HEVC_STREAM_CONTROL, hevc_stream_control);
        WRITE_VREG(HEVC_SHIFT_BYTE_COUNT, hevc->shift_byte_count_lo);
        WRITE_VREG(HEVC_STREAM_FIFO_CTL, hevc_stream_fifo_ctl);

        hevc_config_work_space_hw(&gHevc);
        decoder_hw_reset();

        gHevc.have_vps = 0;
        gHevc.have_sps = 0;
        gHevc.have_pps = 0;

        gHevc.have_valid_start_slice = 0;
        WRITE_VREG(HEVC_WAIT_FLAG, 1);
        /* clear mailbox interrupt */
        WRITE_VREG(HEVC_ASSIST_MBOX1_CLR_REG, 1);
        /* enable mailbox interrupt */
        WRITE_VREG(HEVC_ASSIST_MBOX1_MASK, 1);
        /* disable PSCALE for hardware sharing */
        WRITE_VREG(HEVC_PSCALE_CTRL, 0);

        CLEAR_MPEG_REG_MASK(PARSER_ES_CONTROL, ES_VID_MAN_RD_PTR);

    if(debug&H265_DEBUG_UCODE){
        WRITE_VREG(DEBUG_REG1, 0x1);
    }
    else{
        WRITE_VREG(DEBUG_REG1, 0x0);
    }
    
    WRITE_VREG(NAL_SEARCH_CTL, 0x1); //manual parser NAL
        
    WRITE_VREG(DECODE_STOP_POS, decode_stop_pos);

        //if (amhevc_loadmc(vh265_mc) < 0) {
        //    amhevc_disable();
        //    return -EBUSY;
        //}
#if 0        
            for(i=0; i<(hevc->debug_ptr_size/2); i+=4){
                int ii;
                for(ii=0; ii<4; ii++){
                    //hevc->debug_ptr[i+3-ii]=ttt++;
                    printk("%04x ", hevc->debug_ptr[i+3-ii]);
                }
                if(((i+ii)&0xf)==0)
                    printk("\n");
            }
#endif            
        init_pic_list_hw();
        
        printk("%s HEVC_SHIFT_BYTE_COUNT=%x\n", __func__, READ_VREG(HEVC_SHIFT_BYTE_COUNT));

        amhevc_start();

        //skip, search next start code
        WRITE_VREG(HEVC_WAIT_FLAG, READ_VREG(HEVC_WAIT_FLAG)&(~0x2));            
        hevc->skip_flag = 1;
#ifdef ERROR_HANDLE_DEBUG
        if(dbg_nal_skip_count&0x20000){
            dbg_nal_skip_count &= ~0x20000;
            return;
        }
#endif
        WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);
        // Interrupt Amrisc to excute 
        WRITE_VREG(HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
}

static irqreturn_t vh265_isr(int irq, void *dev_id)
{
    int ret;
    int i;
    unsigned int dec_status;
    hevc_stru_t* hevc = &gHevc;
    dec_status = READ_VREG(HEVC_DEC_STATUS_REG);

    if(init_flag == 0){
   	    return IRQ_HANDLED;
    }
    
    if(debug&H265_DEBUG_BUFMGR){
        printk("265 isr dec status = %d\n", dec_status);
    }

   if(debug&H265_DEBUG_UCODE){
       if(READ_HREG(DEBUG_REG1)&0x10000){
#if 0
            printk("PPS \r\n");
            for(i=0; i<(hevc->debug_ptr_size/2); i+=4){
                int ii;
                for(ii=0; ii<4; ii++){
                    printk("%04x ", hevc->debug_ptr[i+3-ii]);
                }
                if(((i+ii)&0xf)==0)
                    printk("\n");
            }

#endif
            printk("LMEM<tag %x>:\n", READ_HREG(DEBUG_REG1));
            for(i=0; i<0x400; i+=4){
                int ii;
                if((i&0xf)==0)
                    printk("%03x: ",i);
                for(ii=0; ii<4; ii++){
                    printk("%04x ", hevc->lmem_ptr[i+3-ii]);
                }
                if(((i+ii)&0xf)==0)
                    printk("\n");
            }
            WRITE_HREG(DEBUG_REG1, 0);
       }
       else if(READ_HREG(DEBUG_REG1)!=0){
            printk("dbg%x: %x\n",  READ_HREG(DEBUG_REG1), READ_HREG(DEBUG_REG2));
            WRITE_HREG(DEBUG_REG1, 0);
            	return IRQ_HANDLED;
       }
       
   }

    if(hevc->pic_list_init_flag == 1){
        return IRQ_HANDLED;
    }
    
    if(hevc->error_flag==1){
        hevc->error_skip_nal_count = error_skip_nal_count;
        WRITE_VREG(NAL_SEARCH_CTL, 0x1); //manual parser NAL
        WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE); //search new nal

        //printk("%s: error handle\n", __func__);
        hevc->error_flag = 2;
    }
    else if(hevc->error_flag==3){
        printk("error_flag=3, hevc_recover");
        hevc_recover(hevc);
        hevc->error_flag = 0;
        if((error_handle_policy&0x1)==0){
            hevc->have_vps = 1;
            hevc->have_sps = 1;
            hevc->have_pps = 1;
        }
    }
    
    i = READ_VREG(HEVC_SHIFT_BYTE_COUNT);
    if ((hevc->shift_byte_count_lo & (1<<31)) && ((i & (1<<31)) == 0)) {
        hevc->shift_byte_count_hi++;
    }
    hevc->shift_byte_count_lo = i;

    if(dec_status == HEVC_NAL_SEARCH_DONE){
        int naltype = READ_HREG(CUR_NAL_UNIT_TYPE);
        int parse_type = HEVC_DISCARD_NAL;
        error_watchdog_count = 0;
        error_skip_nal_watchdog_count = 0;
        if(slice_parse_begin>0 && debug&H265_DEBUG_DISCARD_NAL){
            printk("nal type %d, discard %d\n", naltype, slice_parse_begin);
            if(naltype<= NAL_UNIT_CODED_SLICE_CRA){
                slice_parse_begin--;
            }
        }
        if(hevc->error_skip_nal_count > 0){
            printk("nal type %d, discard %d\n", naltype, hevc->error_skip_nal_count);
            hevc->error_skip_nal_count--;
            if(hevc->error_skip_nal_count==0){
                hevc_recover(hevc);
                hevc->error_flag = 0;
                if((error_handle_policy&0x1)==0){
                    hevc->have_vps = 1;
                    hevc->have_sps = 1;
                    hevc->have_pps = 1;
                }
            }
        }
        else if(naltype == NAL_UNIT_VPS){
            parse_type = HEVC_NAL_UNIT_VPS;
            hevc->have_vps = 1;
#ifdef ERROR_HANDLE_DEBUG
            if(dbg_nal_skip_flag&1){
                parse_type = HEVC_DISCARD_NAL;
            }
#endif            
        }
        else if(hevc->have_vps){
            if(naltype == NAL_UNIT_SPS){
                parse_type = HEVC_NAL_UNIT_SPS;
                hevc->have_sps = 1;
#ifdef ERROR_HANDLE_DEBUG
                if(dbg_nal_skip_flag&2){
                    parse_type = HEVC_DISCARD_NAL;
                }
#endif            
            }
            else if(naltype == NAL_UNIT_PPS){
                parse_type = HEVC_NAL_UNIT_PPS;
                hevc->have_pps = 1;
#ifdef ERROR_HANDLE_DEBUG
                if(dbg_nal_skip_flag&4){
                    parse_type = HEVC_DISCARD_NAL;
                }
#endif            
            }
            else if(hevc->have_sps && hevc->have_pps){
                if(
                    (naltype == NAL_UNIT_CODED_SLICE_IDR) ||
                    (naltype == NAL_UNIT_CODED_SLICE_IDR_N_LP)||
                    ( naltype == NAL_UNIT_CODED_SLICE_CRA) ||
                    ( naltype == NAL_UNIT_CODED_SLICE_BLA) || 
                    ( naltype == NAL_UNIT_CODED_SLICE_BLANT) ||
                    (naltype == NAL_UNIT_CODED_SLICE_BLA_N_LP )
                 ){
                    if(slice_parse_begin>0){
                        printk("discard %d, for debugging\n", slice_parse_begin);
                        slice_parse_begin--;
                    }
                    else{
                        parse_type = HEVC_NAL_UNIT_CODED_SLICE_SEGMENT;
                    }
                    hevc->have_valid_start_slice = 1;
                }
                else if(naltype<= NAL_UNIT_CODED_SLICE_CRA){
                    if(hevc->have_valid_start_slice || (hevc->PB_skip_mode!=3)){
                        if(slice_parse_begin>0){
                            printk("discard %d, for debugging\n", slice_parse_begin);
                            slice_parse_begin--;
                        }
                        else{
                            parse_type = HEVC_NAL_UNIT_CODED_SLICE_SEGMENT;
                        }
                    }
                }
            }
        }
        if(hevc->have_vps && hevc->have_sps && hevc->have_pps && hevc->have_valid_start_slice
            && hevc->error_flag==0 ){
            if((debug&H265_DEBUG_MAN_SEARCH_NAL)==0)
                WRITE_VREG(NAL_SEARCH_CTL, 0x2); //auot parser NAL; do not check vps/sps/pps/idr
        }
        
        if(debug&H265_DEBUG_BUFMGR){
            printk("naltype = %d  parse_type %d\n %d %d %d %d \n", naltype, parse_type,
                hevc->have_vps ,hevc->have_sps, hevc->have_pps ,hevc->have_valid_start_slice);
        }

        WRITE_VREG(HEVC_DEC_STATUS_REG, parse_type);
        // Interrupt Amrisc to excute 
        WRITE_VREG(HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
            
    
    }
    else if(dec_status == HEVC_SLICE_SEGMENT_DONE){
        error_watchdog_count = 0;
        if(hevc->pic_list_init_flag == 2){
            hevc->pic_list_init_flag = 3;
            printk("set pic_list_init_flag to 3\n");
        }
        else if(hevc->wait_buf == 0){
            u32 vui_time_scale;
            u32 vui_num_units_in_tick;

            if(debug&H265_DEBUG_SEND_PARAM_WITH_REG){
                get_rpm_param(&rpm_param);      
            }
            else{
                for(i=0; i<(RPM_END-RPM_BEGIN); i+=4){
                    int ii;
                    for(ii=0; ii<4; ii++){
                        rpm_param.l.data[i+ii]=hevc->rpm_ptr[i+3-ii];
                    } 
                }
            }
            if(debug&H265_DEBUG_BUFMGR){
                printk("rpm_param: (%d)\n", hevc->slice_idx);
                hevc->slice_idx++;
                for(i=0; i<(RPM_END-RPM_BEGIN); i++){
                    printk("%04x ", rpm_param.l.data[i]);
                    if(((i+1)&0xf)==0)
                        printk("\n");
                } 
                
                printk("vui_timing_info: %x, %x, %x, %x\r\n",         rpm_param.p.vui_num_units_in_tick_hi,
                            rpm_param.p.vui_num_units_in_tick_lo,
                            rpm_param.p.vui_time_scale_hi,
                            rpm_param.p.vui_time_scale_lo);
            }

            vui_time_scale = (u32)(rpm_param.p.vui_time_scale_hi << 16) | rpm_param.p.vui_time_scale_lo;
            vui_num_units_in_tick = (u32)(rpm_param.p.vui_num_units_in_tick_hi << 16) | rpm_param.p.vui_num_units_in_tick_lo;
            if(bit_depth_luma!=((rpm_param.p.bit_depth&0xf)+8)){
                printk("Bit depth luma = %d\n", (rpm_param.p.bit_depth&0xf)+8);    
            }
            if(bit_depth_chroma!=(((rpm_param.p.bit_depth>>4)&0xf)+8)){
                printk("Bit depth chroma = %d\n",  ((rpm_param.p.bit_depth>>4)&0xf) + 8);    
            }
            bit_depth_luma = (rpm_param.p.bit_depth&0xf) + 8;
            bit_depth_chroma = ((rpm_param.p.bit_depth>>4)&0xf) + 8;
            if ((vui_time_scale != 0) && (vui_num_units_in_tick != 0)) {
                frame_dur = div_u64(96000ULL * vui_num_units_in_tick, vui_time_scale);
                get_frame_dur = true;
            }

            if(use_cma&&(rpm_param.p.slice_segment_address == 0)&&(hevc->pic_list_init_flag == 0)){
                hevc->pic_w = rpm_param.p.pic_width_in_luma_samples;
                hevc->pic_h = rpm_param.p.pic_height_in_luma_samples;
                hevc->lcu_size        = 1<<(rpm_param.p.log2_min_coding_block_size_minus3+3+rpm_param.p.log2_diff_max_min_coding_block_size);
                hevc->lcu_size_log2   =log2i(hevc->lcu_size);
	              if(hevc->pic_w==0 || hevc->pic_h==0 || hevc->lcu_size ==0){
                    //skip, search next start code
                    WRITE_VREG(HEVC_WAIT_FLAG, READ_VREG(HEVC_WAIT_FLAG)&(~0x2));            
                    hevc->skip_flag = 1;
                    WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);
                    // Interrupt Amrisc to excute 
                    WRITE_VREG(HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
	                
	              }
                else{                
                    hevc->pic_list_init_flag = 1;
                    up(&h265_sema);
                    printk("set pic_list_init_flag to 1\n");
                }
                return IRQ_HANDLED;
            }

        }    
        ret = hevc_slice_segment_header_process(hevc, &rpm_param, decode_pic_begin);
        if(ret<0){

        }
        else if(ret == 0){
            if ((hevc->new_pic) && (hevc->cur_pic)) {
                hevc->cur_pic->stream_offset = READ_VREG(HEVC_SHIFT_BYTE_COUNT);
            }

            WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_CODED_SLICE_SEGMENT_DAT);
            // Interrupt Amrisc to excute 
            WRITE_VREG(HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
        }
        else{
            //skip, search next start code
            WRITE_VREG(HEVC_WAIT_FLAG, READ_VREG(HEVC_WAIT_FLAG)&(~0x2));            
            hevc->skip_flag = 1;
            WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);
            // Interrupt Amrisc to excute 
            WRITE_VREG(HEVC_MCPU_INTR_REQ, AMRISC_MAIN_REQ);
        }
        
    }

    return IRQ_HANDLED;
}

static void vh265_put_timer_func(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;
    unsigned char empty_flag;
    unsigned int buf_level;	

    receviver_start_e state = RECEIVER_INACTIVE;
    
    if(init_flag == 0){
        return;
    }
    
    if (vf_get_receiver(PROVIDER_NAME)) {
        state = vf_notify_receiver(PROVIDER_NAME, VFRAME_EVENT_PROVIDER_QUREY_STATE, NULL);
        if ((state == RECEIVER_STATE_NULL)||(state == RECEIVER_STATE_NONE)){
            state = RECEIVER_INACTIVE;
        }
    } else {
        state = RECEIVER_INACTIVE;
    }

    empty_flag = (READ_VREG(HEVC_PARSER_INT_STATUS)>>6)&0x1;
    // error watchdog
    if (empty_flag == 0){
        // decoder has input
        if((debug&H265_DEBUG_DIS_LOC_ERROR_PROC)==0){

	buf_level = READ_VREG(HEVC_STREAM_LEVEL);
            if((state == RECEIVER_INACTIVE) &&                       // receiver has no buffer to recycle
                (kfifo_is_empty(&display_q)&& buf_level>0x200)                        // no buffer in display queue  .not to do error recover when buf_level is low
                ){
                if(gHevc.error_flag==0){
                    error_watchdog_count++;
                    if (error_watchdog_count == error_handle_threshold) {    
                        printk("H265 decoder error local reset.\n");
                        gHevc.error_flag = 1;
                        error_watchdog_count = 0;
                        error_skip_nal_watchdog_count = 0;
                        error_system_watchdog_count++;
                        WRITE_VREG(HEVC_ASSIST_MBOX1_IRQ_REG, 0x1); 
                    }
                }
                else if(gHevc.error_flag == 2){
                    error_skip_nal_watchdog_count++;
                    if(error_skip_nal_watchdog_count==error_handle_nal_skip_threshold){
                        gHevc.error_flag = 3;
                        error_watchdog_count = 0;
                        error_skip_nal_watchdog_count = 0;
                        WRITE_VREG(HEVC_ASSIST_MBOX1_IRQ_REG, 0x1); 
                    }
                }
            }
        }

        if((debug&H265_DEBUG_DIS_SYS_ERROR_PROC)==0){
            if((state == RECEIVER_INACTIVE) &&                       // receiver has no buffer to recycle
                (kfifo_is_empty(&display_q))                        // no buffer in display queue
               ){                        // no buffer to recycle
                if((debug&H265_DEBUG_DIS_LOC_ERROR_PROC)!=0){
                    error_system_watchdog_count++;
                }
                if (error_system_watchdog_count == error_handle_system_threshold) {    // and it lasts for a while
                    printk("H265 decoder fatal error watchdog.\n");
                    error_system_watchdog_count = 0;
                    fatal_error = DECODER_FATAL_ERROR_UNKNOW;
                }
            }
        }
    }
    else{
        error_watchdog_count = 0;
        error_system_watchdog_count = 0;
    }

    timer->expires = jiffies + PUT_INTERVAL;

    if(decode_stop_pos != decode_stop_pos_pre){
        WRITE_VREG(DECODE_STOP_POS, decode_stop_pos);
        decode_stop_pos_pre = decode_stop_pos;
    }
    
    if(debug&H265_DEBUG_DUMP_PIC_LIST){
        dump_pic_list(&gHevc);
        debug &= ~H265_DEBUG_DUMP_PIC_LIST;
    }
    if(debug&H265_DEBUG_TRIG_SLICE_SEGMENT_PROC){
        WRITE_VREG(HEVC_ASSIST_MBOX1_IRQ_REG, 0x1); 
        debug &= ~H265_DEBUG_TRIG_SLICE_SEGMENT_PROC;
    }
    if(debug&H265_DEBUG_HW_RESET){
        gHevc.error_skip_nal_count = error_skip_nal_count;
        WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);

        debug &= ~H265_DEBUG_HW_RESET;
    }
    if(debug&H265_DEBUG_ERROR_TRIG){
        WRITE_VREG(DECODE_STOP_POS, 1);
        debug &= ~H265_DEBUG_ERROR_TRIG;
    }

#ifdef ERROR_HANDLE_DEBUG
    if((dbg_nal_skip_count > 0)&&((dbg_nal_skip_count&0x10000)!=0)){
        gHevc.error_skip_nal_count = dbg_nal_skip_count&0xffff;
        dbg_nal_skip_count &= ~0x10000;
        WRITE_VREG(HEVC_DEC_STATUS_REG, HEVC_ACTION_DONE);
    }
#endif
    
    add_timer(timer);
}

static int h265_task_handle(void *data)
{
		int ret = 0;
    while (1)
    {
        if(use_cma==0){
            printk("ERROR: use_cma can not be changed dynamically\n");    
        }
        ret = down_interruptible(&h265_sema);
        if((init_flag!=0) && (gHevc.pic_list_init_flag == 1)){
            init_pic_list(&gHevc);
            init_buf_spec(&gHevc);
            gHevc.pic_list_init_flag = 2;
            printk("set pic_list_init_flag to 2\n");
            
            WRITE_VREG(HEVC_ASSIST_MBOX1_IRQ_REG, 0x1); 
        }
        
        if(uninit_list){    
            uninit_pic_list(&gHevc);
            printk("uninit list\n");
            uninit_list = 0;
        }

    }

    return 0;

}

int vh265_dec_status(struct vdec_status *vstatus)
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

#if 0
static void H265_DECODE_INIT(void)
{
    //enable hevc clocks
    WRITE_VREG(DOS_GCLK_EN3, 0xffffffff);
    //****************************************************************************
    //                 Power ON HEVC
    //****************************************************************************
    // Powerup HEVC
    WRITE_VREG(P_AO_RTI_GEN_PWR_SLEEP0, READ_VREG(P_AO_RTI_GEN_PWR_SLEEP0) & (~(0x3<<6))); // [7:6] HEVC
    WRITE_VREG( DOS_MEM_PD_HEVC, 0x0);
    WRITE_VREG(DOS_SW_RESET3, READ_VREG(DOS_SW_RESET3)|(0x3ffff<<2));
    WRITE_VREG(DOS_SW_RESET3, READ_VREG(DOS_SW_RESET3)&(~(0x3ffff<<2)));
    // remove isolations
    WRITE_VREG(AO_RTI_GEN_PWR_ISO0, READ_VREG(AO_RTI_GEN_PWR_ISO0) & (~(0x3<<10))); // [11:10] HEVC

}
#endif

static void vh265_prot_init(void)
{
//    H265_DECODE_INIT();
    
    hevc_config_work_space_hw(&gHevc);
    
    hevc_init_decoder_hw(0, 0xffffffff);

    WRITE_VREG(HEVC_WAIT_FLAG, 1);

    //WRITE_VREG(P_HEVC_MPSR, 1);

    /* clear mailbox interrupt */
    WRITE_VREG(HEVC_ASSIST_MBOX1_CLR_REG, 1);

    /* enable mailbox interrupt */
    WRITE_VREG(HEVC_ASSIST_MBOX1_MASK, 1);

    /* disable PSCALE for hardware sharing */
    WRITE_VREG(HEVC_PSCALE_CTRL, 0);
    
    if(debug&H265_DEBUG_UCODE){
        WRITE_VREG(DEBUG_REG1, 0x1);
    }
    else{
        WRITE_VREG(DEBUG_REG1, 0x0);
    }
    
    if(debug&(H265_DEBUG_MAN_SKIP_NAL|H265_DEBUG_MAN_SEARCH_NAL)){
        WRITE_VREG(NAL_SEARCH_CTL, 0x1); //manual parser NAL
    }
    else{
        unsigned ctl_val = 0x8; //check vps/sps/pps/i-slice in ucode
        if(gHevc.PB_skip_mode==0){
            ctl_val = 0x4;  // check vps/sps/pps only in ucode
        }
        else if(gHevc.PB_skip_mode==3){
            ctl_val = 0x0;  // check vps/sps/pps/idr in ucode
        }
        WRITE_VREG(NAL_SEARCH_CTL, ctl_val);
    }
        
    WRITE_VREG(DECODE_STOP_POS, decode_stop_pos);

}

static int vh265_local_init(void)
{
    int i;
    int ret;

#ifdef DEBUG_PTS
    pts_missed = 0;
    pts_hit = 0;
#endif
    get_frame_dur = false;
    frame_width = vh265_amstream_dec_info.width;
    frame_height = vh265_amstream_dec_info.height;
    frame_dur = (vh265_amstream_dec_info.rate == 0) ? 3600 : vh265_amstream_dec_info.rate;
    if (frame_width && frame_height) {
        frame_ar = frame_height * 0x100 / frame_width;
    }
    error_watchdog_count = 0;

    printk("h265: decinfo: %dx%d rate=%d\n", frame_width, frame_height, frame_dur);

    if(frame_dur == 0){
        frame_dur = 96000/24;
    }

    INIT_KFIFO(display_q);
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

    ret = hevc_local_init();
    
    return ret;
}

extern unsigned char ucode_buf[4*1024*8];
static s32 vh265_init(void)
{
    init_timer(&recycle_timer);

    stat |= STAT_TIMER_INIT;

    if(vh265_local_init()<0)
       return -EBUSY; 

    amhevc_enable();
#if 0
    if(debug &H265_DEBUG_LOAD_UCODE_FROM_FILE){
        printk("load ucode from file\r\n");
        if (amhevc_loadmc(ucode_buf) < 0) {
            amhevc_disable();
            return -EBUSY;
        }
    }
    else 
#endif
    if(debug&H265_DEBUG_SEND_PARAM_WITH_REG){
        if (amhevc_loadmc(vh265_mc_send_param_with_reg_mc) < 0) {
            amhevc_disable();
            return -EBUSY;
        }
    }
    else{
        if (amhevc_loadmc(vh265_mc) < 0) {
            amhevc_disable();
            return -EBUSY;
        }
    }
    stat |= STAT_MC_LOAD;

    /* enable AMRISC side protocol */
    vh265_prot_init();

    if (request_irq(INT_VDEC, vh265_isr,
                    IRQF_SHARED, "vh265-irq", (void *)vh265_dec_id)) {
        printk("vh265 irq register error.\n");
        amhevc_disable();
        return -ENOENT;
    }

    stat |= STAT_ISR_REG;

    vf_provider_init(&vh265_vf_prov, PROVIDER_NAME, &vh265_vf_provider, NULL);
    vf_reg_provider(&vh265_vf_prov);
    vf_notify_receiver(PROVIDER_NAME,VFRAME_EVENT_PROVIDER_START,NULL);

    stat |= STAT_VF_HOOK;

    recycle_timer.data = (ulong) & recycle_timer;
    recycle_timer.function = vh265_put_timer_func;
    recycle_timer.expires = jiffies + PUT_INTERVAL;

    add_timer(&recycle_timer);

    stat |= STAT_TIMER_ARM;

    if(use_cma){
        if(h265_task==NULL){
            sema_init(&h265_sema,1);
            h265_task = kthread_run(h265_task_handle, NULL, "kthread_h265");
        }
    }
    //stat |= STAT_KTHREAD;

    if(debug&H265_DEBUG_FORCE_CLK){
        printk("%s force clk\n", __func__);
        WRITE_VREG(HEVC_IQIT_CLK_RST_CTRL, READ_VREG(HEVC_IQIT_CLK_RST_CTRL)|((1<<2)|(1<<1)));
        WRITE_VREG(HEVC_DBLK_CFG0, READ_VREG(HEVC_DBLK_CFG0)|((1<<2)|(1<<1)|0x3fff0000)); //2,29:16
        WRITE_VREG(HEVC_SAO_CTRL1 , READ_VREG(HEVC_SAO_CTRL1 )|(1<<2)); //2
        WRITE_VREG( HEVC_MPRED_CTRL1, READ_VREG( HEVC_MPRED_CTRL1)|(1<<24)); //24
        WRITE_VREG(HEVC_STREAM_CONTROL, READ_VREG(HEVC_STREAM_CONTROL)|(1<<15)); //15
        WRITE_VREG(HEVC_CABAC_CONTROL, READ_VREG(HEVC_CABAC_CONTROL)|(1<<13)); //13
        WRITE_VREG(HEVC_PARSER_CORE_CONTROL, READ_VREG(HEVC_PARSER_CORE_CONTROL)|(1<<15)); //15
        WRITE_VREG(HEVC_PARSER_INT_CONTROL, READ_VREG(HEVC_PARSER_INT_CONTROL)|(1<<15)); //15
        WRITE_VREG(HEVC_PARSER_IF_CONTROL, READ_VREG(HEVC_PARSER_IF_CONTROL)|((1<<6)|(1<<3)|(1<<1))); //6, 3, 1
        WRITE_VREG(HEVCD_IPP_DYNCLKGATE_CONFIG, READ_VREG(HEVCD_IPP_DYNCLKGATE_CONFIG)|0xffffffff); //31:0
        WRITE_VREG(HEVCD_MCRCC_CTL1, READ_VREG(HEVCD_MCRCC_CTL1)|(1<<3)); //3
    }

    amhevc_start();

    stat |= STAT_VDEC_RUN;

    set_vdec_func(&vh265_dec_status);

    init_flag = 1;

    //printk("%d, vh265_init, RP=0x%x\n", __LINE__, READ_VREG(HEVC_STREAM_RD_PTR));

    return 0;
}

static int vh265_stop(void)
{
    init_flag = 0;
    
    if (stat & STAT_VDEC_RUN) {
        amhevc_stop();
        stat &= ~STAT_VDEC_RUN;
    }

    if (stat & STAT_ISR_REG) {
        WRITE_VREG(HEVC_ASSIST_MBOX1_MASK, 0);
        free_irq(INT_VDEC, (void *)vh265_dec_id);
        stat &= ~STAT_ISR_REG;
    }

    if (stat & STAT_TIMER_ARM) {
        del_timer_sync(&recycle_timer);
        stat &= ~STAT_TIMER_ARM;
    }

    if (stat & STAT_VF_HOOK) {
        vf_unreg_provider(&vh265_vf_prov);
        stat &= ~STAT_VF_HOOK;
    }

    //hevc_local_uninit();
    
    if(use_cma){
        uninit_list = 1;
        up(&h265_sema);
        while(uninit_list){ //wait uninit complete
            msleep(10);
        }
    }
#if 0    
    if(h265_task)
        kthread_stop(h265_task);
    h265_task = NULL;
#endif
    amhevc_disable();

    return 0;
}

static int amvdec_h265_probe(struct platform_device *pdev)
{
    struct vdec_dev_reg_s *pdata = (struct vdec_dev_reg_s *)pdev->dev.platform_data;
    int i;

    mutex_lock(&vh265_mutex);
    
    fatal_error = 0;

    if (pdata == NULL) {
        printk("\namvdec_h265 memory resource undefined.\n");
        mutex_unlock(&vh265_mutex);
        return -EFAULT;
    }

    mc_buf_spec.buf_end = pdata->mem_end + 1;
    for(i=0;i<WORK_BUF_SPEC_NUM;i++){
        amvh265_workbuff_spec[i].start_adr = pdata->mem_start;
    }

    if(debug) printk("===H.265 decoder mem resource 0x%lx -- 0x%lx\n", pdata->mem_start, pdata->mem_end + 1);

    if (pdata->sys_info) {
        vh265_amstream_dec_info = *pdata->sys_info;
    } else {
        vh265_amstream_dec_info.width = 0;
        vh265_amstream_dec_info.height = 0;
        vh265_amstream_dec_info.rate = 30;
    }

    cma_dev = pdata->cma_dev;

    if (vh265_init() < 0) {
        printk("\namvdec_h265 init failed.\n");
        hevc_local_uninit();
        mutex_unlock(&vh265_mutex);
        return -ENODEV;
    }

    mutex_unlock(&vh265_mutex);
    return 0;
}

static int amvdec_h265_remove(struct platform_device *pdev)
{
    if(debug) printk("amvdec_h265_remove\n");

    mutex_lock(&vh265_mutex);

    vh265_stop();

#ifdef DEBUG_PTS
    printk("pts missed %ld, pts hit %ld, duration %d\n",
           pts_missed, pts_hit, frame_dur);
#endif

    mutex_unlock(&vh265_mutex);
    
    return 0;
}

/****************************************/

static struct platform_driver amvdec_h265_driver = {
    .probe   = amvdec_h265_probe,
    .remove  = amvdec_h265_remove,
#ifdef CONFIG_PM
    .suspend = amvdec_suspend,
    .resume  = amvdec_resume,
#endif
    .driver  = {
        .name = DRIVER_NAME,
    }
};

static struct codec_profile_t amvdec_h265_profile = {
    .name = "hevc",
    .profile = ""
};

static int __init amvdec_h265_driver_init_module(void)
{
    printk("amvdec_h265 module init\n");

    if (platform_driver_register(&amvdec_h265_driver)) {
        printk("failed to register amvdec_h265 driver\n");
        return -ENODEV;
    }

    #if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (IS_MESON_M8_CPU) {
        amvdec_h265_profile.name = "hevc_unsupport"; //not support hevc
    }else if(IS_MESON_M8M2_CPU){
        amvdec_h265_profile.profile = "4k"; //m8m2 support 4k
    }
    #endif

    vcodec_profile_register(&amvdec_h265_profile);

    return 0;
}

static void __exit amvdec_h265_driver_remove_module(void)
{
    printk("amvdec_h265 module remove.\n");

    platform_driver_unregister(&amvdec_h265_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvdec_h265 stat \n");

module_param(use_cma, uint, 0664);
MODULE_PARM_DESC(use_cma, "\n amvdec_h265 use_cma \n");

module_param(bit_depth_luma, uint, 0664);
MODULE_PARM_DESC(bit_depth_luma, "\n amvdec_h265 bit_depth_luma \n");

module_param(bit_depth_chroma, uint, 0664);
MODULE_PARM_DESC(bit_depth_chroma, "\n amvdec_h265 bit_depth_chroma \n");

module_param(debug, uint, 0664);
MODULE_PARM_DESC(debug, "\n amvdec_h265 debug \n");

#ifdef ERROR_HANDLE_DEBUG
module_param(dbg_nal_skip_flag, uint, 0664);
MODULE_PARM_DESC(dbg_nal_skip_flag, "\n amvdec_h265 dbg_nal_skip_flag \n");

module_param(dbg_nal_skip_count, uint, 0664);
MODULE_PARM_DESC(dbg_nal_skip_count, "\n amvdec_h265 dbg_nal_skip_count \n");
#endif

module_param(step, uint, 0664);
MODULE_PARM_DESC(step, "\n amvdec_h265 step \n");

module_param(decode_stop_pos, uint, 0664);
MODULE_PARM_DESC(decode_stop_pos, "\n amvdec_h265 decode_stop_pos \n");

module_param(decode_pic_begin, uint, 0664);
MODULE_PARM_DESC(decode_pic_begin, "\n amvdec_h265 decode_pic_begin \n");

module_param(slice_parse_begin, uint, 0664);
MODULE_PARM_DESC(slice_parse_begin, "\n amvdec_h265 slice_parse_begin \n");

module_param(nal_skip_policy, uint, 0664);
MODULE_PARM_DESC(nal_skip_policy, "\n amvdec_h265 nal_skip_policy \n");

module_param(error_handle_policy, uint, 0664);
MODULE_PARM_DESC(error_handle_policy, "\n amvdec_h265 error_handle_policy \n");

module_param(error_handle_threshold, uint, 0664);
MODULE_PARM_DESC(error_handle_threshold, "\n amvdec_h265 error_handle_threshold \n");

module_param(error_handle_nal_skip_threshold, uint, 0664);
MODULE_PARM_DESC(error_handle_nal_skip_threshold, "\n amvdec_h265 error_handle_nal_skip_threshold \n");

module_param(error_handle_system_threshold, uint, 0664);
MODULE_PARM_DESC(error_handle_system_threshold, "\n amvdec_h265 error_handle_system_threshold \n");

module_param(error_skip_nal_count, uint, 0664);
MODULE_PARM_DESC(error_skip_nal_count, "\n amvdec_h265 error_skip_nal_count \n");

module_init(amvdec_h265_driver_init_module);
module_exit(amvdec_h265_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC h265 Video Decoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <tim.yao@amlogic.com>");


