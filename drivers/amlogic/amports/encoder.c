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
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/vformat.h>
#include "vdec_reg.h"
#include "vdec.h"
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/dma-contiguous.h>
#include "amports_config.h"

#define MULTI_SLICE_MC
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
#define USE_VDEC2
#endif
#define ENC_CANVAS_OFFSET  AMVENC_CANVAS_INDEX
//#define DUMP_PRE_ENCODE_INTRA

#define UCODE_MODE_FULL 0
#define UCODE_MODE_SW_MIX 1

#define LOG_LEVEL_VAR 1
#define debug_level(level, x...) \
	do { \
		if (level >= LOG_LEVEL_VAR) \
			printk(x); \
	} while (0);


#ifdef CONFIG_AM_VDEC_MJPEG_LOG
#define AMLOG
#define LOG_LEVEL_VAR       amlog_level_avc
#define LOG_MASK_VAR        amlog_mask_avc
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_DESC  "0:ERROR, 1:INFO"
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(LOG_LEVEL_ERROR, 0, LOG_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

#include "encoder.h"
#include "amvdec.h"
#include "encoder_mc.h"

static int avc_device_major = 0;
static struct class *amvenc_avc_class;
static struct device *amvenc_avc_dev;
#define DRIVER_NAME "amvenc_avc"
#define MODULE_NAME "amvenc_avc"
#define DEVICE_NAME "amvenc_avc"

/* protocol register usage
	#define ENCODER_STATUS            HENC_SCRATCH_0    : encode stage
	#define MEM_OFFSET_REG            HENC_SCRATCH_1    : assit buffer physical address
	#define DEBUG_REG  				  HENC_SCRATCH_2    : debug register
	#define MB_COUNT				  HENC_SCRATCH_3	: MB encoding number
*/
#ifdef USE_VDEC2
#define STREAM_WR_PTR               DOS_SCRATCH20
#define DECODABLE_MB_Y              DOS_SCRATCH21
#define DECODED_MB_Y                DOS_SCRATCH22
#endif

/*output buffer define*/
static unsigned BitstreamStart;
static unsigned BitstreamEnd;
//static unsigned BitstreamIntAddr;
/*input buffer define*/
static unsigned dct_buff_start_addr;
static unsigned dct_buff_end_addr;

/*deblock buffer define*/
//static unsigned dblk_buf_addr;
static unsigned dblk_buf_canvas;

/*reference buffer define*/
//static unsigned ref_buf_addr ; //192
static unsigned ref_buf_canvas;  //((192<<16)|(192<<8)|(192<<0))

/*microcode assitant buffer*/
static unsigned assit_buffer_offset;
//static struct dec_sysinfo avc_amstream_dec_info;
   
u32 inter_bits_info_ddr_start_addr = 0; 
u32 inter_mv_info_ddr_start_addr = 0;
u32 intra_bits_info_ddr_start_addr = 0; 
u32 intra_pred_info_ddr_start_addr = 0;
u32 sw_ctl_info_start_addr = 0;
#ifdef USE_VDEC2
#define VDEC2_DEF_BUF_START_ADDR            0x01000000
u32 vdec2_start_addr = 0;
#endif

static u32 stat;
//static u32 cur_stage;
static u32 frame_start;//0: processing 1:restart
static u32 quant = 28;
static u32 encoder_width = 1280;
static u32 encoder_height = 720;
static void avc_prot_init(bool IDR);
static s32 avc_poweron(int clock);
static void dma_flush(unsigned buf_start , unsigned buf_size );
//static void avc_local_init(void);
static int idr_pic_id = 0;  //need reset as 0 for IDR
static u32 frame_number = 0 ;   //need plus each frame
static u32 pic_order_cnt_lsb = 0 ; //need reset as 0 for IDR and plus 2 for NON-IDR

static u32 log2_max_pic_order_cnt_lsb = 4 ;
static u32 log2_max_frame_num =4 ;
static u32 anc0_buffer_id =0;
static u32 qppicture  =28;
static u32 process_irq = 0;
static u32 ie_me_mb_type  = 0;
static u32 ie_me_mode  = 0;
static u32 ie_pippeline_block = 3;
static u32 ie_cur_ref_sel = 0;
static int encode_inited = 0;
static int encode_opened = 0;
static int encoder_status = 0;
static int avc_endian = 6;
static int clock_level = 1;

static wait_queue_head_t avc_wait;
atomic_t avc_ready = ATOMIC_INIT(0);
static struct tasklet_struct encode_tasklet;
#ifdef MULTI_SLICE_MC  
static int rows_per_slice = 1;
#endif

static bool dblk_fix_flag = false;

static DEFINE_SPINLOCK(lock);

static const char avc_dec_id[] = "avc-dev";

#define AMVENC_BUFFER_LEVEL_480P   0
#define AMVENC_BUFFER_LEVEL_720P   1
#define AMVENC_BUFFER_LEVEL_1080P 2

static int ucode_index = UCODE_MODE_FULL;
static int enable_dblk = 1;  // 0 disable, 1 vdec 2 hdec
#ifdef DUMP_PRE_ENCODE_INTRA
static bool preencode_intra;
static unsigned intra_pic_id = 0;
#endif

typedef struct
{
    u32 buf_start;
    u32 buf_size;
} Buff_t;

typedef struct
{
    u32 lev_id;
    u32 min_buffsize;
    u32 max_width;
    u32 max_height;
    Buff_t dct;
    Buff_t dec0_y;
    Buff_t dec0_uv;
    Buff_t dec1_y;
    Buff_t dec1_uv;
    Buff_t assit;
    Buff_t bitstream;
    Buff_t inter_bits_info;
    Buff_t inter_mv_info;
    Buff_t intra_bits_info;
    Buff_t intra_pred_info;
    Buff_t qp_info;
#ifdef USE_VDEC2
    Buff_t vdec2_info;
#endif
} BuffInfo_t;

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

typedef struct
{
    u32 buf_start;
    u32 buf_size;
    u8 cur_buf_lev;
    BuffInfo_t* bufspec;
} EncBuffer_t;

static EncBuffer_t gAmvencbuff = {0,0,0,NULL};

static void avc_canvas_init(void);

void amvenc_reset(void);

#ifdef CONFIG_AM_JPEG_ENCODER
extern bool jpegenc_on(void);
#endif

int avc_dec_status(struct vdec_status *vstatus)
{
    return 0;
}

/*output stream buffer setting*/
static void avc_init_output_buffer(void)
{
	WRITE_HREG(VLC_VB_MEM_CTL ,((1<<31)|(0x3f<<24)|(0x20<<16)|(2<<0)) );
	WRITE_HREG(VLC_VB_START_PTR, BitstreamStart);
	WRITE_HREG(VLC_VB_WR_PTR, BitstreamStart);
	WRITE_HREG(VLC_VB_SW_RD_PTR, BitstreamStart);
	WRITE_HREG(VLC_VB_END_PTR, BitstreamEnd);
	WRITE_HREG(VLC_VB_CONTROL, 1);
	WRITE_HREG(VLC_VB_CONTROL, ((0<<14)|(7<<3)|(1<<1)|(0<<0)));
}

/*input dct buffer setting*/
static void avc_init_input_buffer(void)
{
	WRITE_HREG(QDCT_MB_START_PTR ,dct_buff_start_addr );
	WRITE_HREG(QDCT_MB_END_PTR, dct_buff_end_addr);
	WRITE_HREG(QDCT_MB_WR_PTR, dct_buff_start_addr);
	WRITE_HREG(QDCT_MB_RD_PTR, dct_buff_start_addr);
	WRITE_HREG(QDCT_MB_BUFF, 0);
}

/*input reference buffer setting*/
static void avc_init_reference_buffer(int canvas)
{
	WRITE_HREG(HCODEC_ANC0_CANVAS_ADDR ,canvas);
	WRITE_HREG(VLC_HCMD_CONFIG ,0);
}

static void avc_init_assit_buffer(void)
{
	WRITE_HREG(MEM_OFFSET_REG,assit_buffer_offset);  //memory offset ?
}

/*deblock buffer setting, same as INI_CANVAS*/
static void avc_init_dblk_buffer(int canvas)
{
	WRITE_HREG(HCODEC_REC_CANVAS_ADDR,canvas);
	WRITE_HREG(HCODEC_DBKR_CANVAS_ADDR,canvas);
	WRITE_HREG(HCODEC_DBKW_CANVAS_ADDR,canvas);
}

/*same as INIT_ENCODER*/
#define INTRA_IN_P_TOP
static void avc_init_encoder(bool idr)
{
	WRITE_HREG(VLC_TOTAL_BYTES, 0);
	WRITE_HREG(VLC_CONFIG, 0x07);
	WRITE_HREG(VLC_INT_CONTROL, 0);
	WRITE_HREG(HCODEC_ASSIST_AMR1_INT0, 0x15);
#ifdef MULTI_SLICE_MC
	if(dblk_fix_flag){  
		WRITE_HREG(HCODEC_ASSIST_AMR1_INT2, 0x19);
		WRITE_HREG(HCODEC_ASSIST_AMR1_INT1, 0x8);
	}else{
		WRITE_HREG(HCODEC_ASSIST_AMR1_INT1, 0x19);
		WRITE_HREG(HCODEC_ASSIST_AMR1_INT2, 0x8);
	}
#else
	if(dblk_fix_flag)
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
#ifdef DUMP_PRE_ENCODE_INTRA
	if(preencode_intra == true)
		WRITE_HREG(IDR_PIC_ID ,intra_pic_id);
	else	
#endif
		WRITE_HREG(IDR_PIC_ID ,idr_pic_id);
	WRITE_HREG(FRAME_NUMBER ,(idr== true)?0:frame_number);
	WRITE_HREG(PIC_ORDER_CNT_LSB,(idr== true)?0:pic_order_cnt_lsb);
	log2_max_pic_order_cnt_lsb= 4;
	log2_max_frame_num = 4;
	WRITE_HREG(LOG2_MAX_PIC_ORDER_CNT_LSB ,  log2_max_pic_order_cnt_lsb);
	WRITE_HREG(LOG2_MAX_FRAME_NUM , log2_max_frame_num);
	WRITE_HREG(ANC0_BUFFER_ID, anc0_buffer_id);
	WRITE_HREG(QPPICTURE, qppicture);
}

/****************************************/
static void avc_canvas_init(void)
{
    u32 canvas_width, canvas_height;
    int start_addr = gAmvencbuff.buf_start;

    canvas_width = ((encoder_width+31)>>5)<<5;
    canvas_height = ((encoder_height+15)>>4)<<4;

	/*input dct buffer config */
    dct_buff_start_addr = start_addr+gAmvencbuff.bufspec->dct.buf_start;   //(w>>4)*(h>>4)*864
    dct_buff_end_addr = dct_buff_start_addr + gAmvencbuff.bufspec->dct.buf_size -1 ;
    debug_level(0,"dct_buff_start_addr is %x \n",dct_buff_start_addr);

    canvas_config(ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec0_y.buf_start,
        canvas_width, canvas_height,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

    gAmvencbuff.bufspec->dec0_uv.buf_start = gAmvencbuff.bufspec->dec0_y.buf_start+canvas_width*canvas_height;
    gAmvencbuff.bufspec->dec0_uv.buf_size = canvas_width*canvas_height/2;
    canvas_config(1 + ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec0_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    /*here the third plane use the same address as the second plane*/
    canvas_config(2 + ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec0_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

    canvas_config(3 + ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec1_y.buf_start,
        canvas_width, canvas_height,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    gAmvencbuff.bufspec->dec1_uv.buf_start = gAmvencbuff.bufspec->dec1_y.buf_start+canvas_width*canvas_height;
    gAmvencbuff.bufspec->dec1_uv.buf_size = canvas_width*canvas_height/2;
    canvas_config(4 + ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec1_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    /*here the third plane use the same address as the second plane*/
    canvas_config(5 + ENC_CANVAS_OFFSET,
        start_addr + gAmvencbuff.bufspec->dec1_uv.buf_start,
        canvas_width , canvas_height/2,
        CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);

    assit_buffer_offset = start_addr + gAmvencbuff.bufspec->assit.buf_start;
    debug_level(0,"assit_buffer_offset is %x \n",assit_buffer_offset);
	/*output stream buffer config*/
    BitstreamStart  = start_addr + gAmvencbuff.bufspec->bitstream.buf_start;
    BitstreamEnd  =  BitstreamStart + gAmvencbuff.bufspec->bitstream.buf_size -1;

    debug_level(0,"BitstreamStart is %x \n",BitstreamStart);

    dblk_buf_canvas = ((ENC_CANVAS_OFFSET+2) <<16)|((ENC_CANVAS_OFFSET + 1) <<8)|(ENC_CANVAS_OFFSET);
    ref_buf_canvas = ((ENC_CANVAS_OFFSET +5) <<16)|((ENC_CANVAS_OFFSET + 4) <<8)|(ENC_CANVAS_OFFSET +3);
    debug_level(0,"dblk_buf_canvas is %d ; ref_buf_canvas is %d \n",dblk_buf_canvas , ref_buf_canvas);
}

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

#define TARGET_BITS_PER_MB 20

#define P_INTRA_QUANT   30
#define INTRA_MIN_BITS 0xffff

static int p_intra_config = 
              ( P_INTRA_QUANT << 16) |
              ( INTRA_MIN_BITS << 0);


#define MB_INC_1_BITS                  5
#define MB_INC_2_BITS                  10
#define MB_INC_3_BITS                  15
#define MB_INC_4_BITS                  20
#define MB_DEC_1_BITS                  20
#define MB_DEC_2_BITS                  30
#define MB_DEC_3_BITS                  40
#define MB_DEC_4_BITS                  60
#define MB_MIN_QUANT                   24
#define MB_MAX_QUANT                   24

// [31:16] TARGET_BITS_PER_MB
// [15:8] MIN_QUANT
//  [7:0] MAX_QUANT
static int p_mb_quant_config = 
              ( TARGET_BITS_PER_MB << 16)  |
              ( MB_MIN_QUANT << 8)  |
              ( MB_MAX_QUANT << 0);  

// [31:24] INC_4_BITS
// [23:16] INC_3_BITS
// [15:8]  INC_2_BITS
// [7:0]   INC_1_BITS
static int p_mb_quant_inc_cfg = 
              ( MB_INC_4_BITS << 24)  |
              ( MB_INC_3_BITS << 16)  |
              ( MB_INC_2_BITS << 8)  |
              ( MB_INC_1_BITS << 0);  

// [31:24] DEC_4_BITS
// [23:16] DEC_3_BITS
// [15:8]  DEC_2_BITS
// [7:0]   DEC_1_BITS
static int p_mb_quant_dec_cfg = 
              ( MB_DEC_4_BITS << 24)  |
              ( MB_DEC_3_BITS << 16)  |
              ( MB_DEC_2_BITS << 8)  |
              ( MB_DEC_1_BITS << 0);  

// [31:0] NUM_ROWS_PER_SLICE_P
// [15:0] NUM_ROWS_PER_SLICE_I
static int fixed_slice_cfg = 0;

#ifdef USE_VDEC2
static int abort_vdec2_flag = 0;
void AbortEncodeWithVdec2(int abort)
{
    abort_vdec2_flag = abort;
}
#endif

static void avc_init_ie_me_parameter(void)
{
    ie_pippeline_block = 3;

    WRITE_HREG(IE_ME_MB_TYPE,ie_me_mb_type);

    if(ie_pippeline_block == 3){
        ie_cur_ref_sel = ((1<<13) |(1<<12) |(1<<9) |(1<<8));
    }else if(ie_pippeline_block == 0){
        ie_cur_ref_sel = 0xffffffff;
    }else{
        debug_level(1,"Error : Please calculate IE_CUR_REF_SEL for IE_PIPPELINE_BLOCK. \n");
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
    }else if(rows_per_slice !=  (encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (rows_per_slice<<16)|rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
    WRITE_HREG(P_INTRA_CONFIG, p_intra_config); 
    WRITE_HREG(P_MB_QUANT_CONFIG, p_mb_quant_config);
    if(ucode_index != UCODE_MODE_SW_MIX){
        p_mb_quant_config = ( TARGET_BITS_PER_MB << 16)  |
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

static int  set_input_format (amvenc_mem_type type, amvenc_frame_fmt fmt, unsigned input, unsigned offset, unsigned size, unsigned char need_flush)
{
    int ret = 0;
    unsigned char iformat = MAX_FRAME_FMT, oformat = MAX_FRAME_FMT, r2y_en = 0;
    unsigned picsize_x, picsize_y;
    unsigned canvas_w = 0;

    if((fmt == FMT_RGB565)||(fmt>=MAX_FRAME_FMT))
        return -1;

    picsize_x = ((encoder_width+15)>>4)<<4;
    picsize_y = ((encoder_height+15)>>4)<<4;
    oformat = 0;
    if((type == LOCAL_BUFF)||(type == PHYSICAL_BUFF)){
        if(type == LOCAL_BUFF){
            if(need_flush)
                dma_flush(dct_buff_start_addr + offset, size);
            input = dct_buff_start_addr + offset;
        }
        if(fmt <= FMT_YUV444_PLANE)
            r2y_en = 0;
        else
            r2y_en = 1;

        if(fmt == FMT_YUV422_SINGLE){
            iformat = 10;
        }else if((fmt == FMT_YUV444_SINGLE)||(fmt== FMT_RGB888)){
            iformat = 1;
            if(fmt == FMT_RGB888)
                r2y_en = 1;
            canvas_w =  picsize_x*3;
            canvas_w =  ((canvas_w+31)>>5)<<5;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ENC_CANVAS_OFFSET+6;
        }else if((fmt == FMT_NV21)||(fmt == FMT_NV12)){
            canvas_w =  ((encoder_width+31)>>5)<<5;
            iformat = (fmt == FMT_NV21)?2:3;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+7,
                input + canvas_w*picsize_y,
                canvas_w , picsize_y/2,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ((ENC_CANVAS_OFFSET+7)<<8)|(ENC_CANVAS_OFFSET+6);
        }else if(fmt == FMT_YUV420){
            iformat = 4;
            canvas_w =  ((encoder_width+63)>>6)<<6;
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
        }else if((fmt == FMT_YUV444_PLANE)||(fmt == FMT_RGB888_PLANE)){
            if(fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
            iformat = 5;
            canvas_w =  ((encoder_width+31)>>5)<<5;
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
        }else if(fmt == FMT_RGBA8888){
            iformat = 12;
        }
        ret = 0;
    }else if(type == CANVAS_BUFF){
        r2y_en = 0;
        if(fmt == FMT_YUV422_SINGLE){
            iformat = 0;
            input = input&0xff;
        }else if(fmt == FMT_YUV444_SINGLE){
            iformat = 1;
            input = input&0xff;
        }else if((fmt == FMT_NV21)||(fmt == FMT_NV12)){
            iformat = (fmt == FMT_NV21)?2:3;
            input = input&0xffff;
        }else if(fmt == FMT_YUV420){
            iformat = 4;
            input = input&0xffffff;
        }else if((fmt == FMT_YUV444_PLANE)||(fmt == FMT_RGB888_PLANE)){
            if(fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
            iformat = 5;
            input = input&0xffffff;
        }else{
            ret = -1;
        }
    }
    if(ret == 0)
        mfdin_basic(input,iformat,oformat,picsize_x,picsize_y,r2y_en);
    return ret;
}

static void encode_isr_tasklet(ulong data)
{
    debug_level(0,"encoder is done %d\n",encoder_status);
    if(((encoder_status == ENCODER_IDR_DONE)
	||(encoder_status == ENCODER_NON_IDR_DONE))&&(process_irq)){
#ifdef USE_VDEC2
        if(IS_MESON_M8_CPU){
            if((abort_vdec2_flag)&&(get_vdec2_usage() == USAGE_ENCODE))
                set_vdec2_usage(USAGE_NONE);
        }
#endif
        atomic_inc(&avc_ready);
        wake_up_interruptible(&avc_wait);
    }
}

static irqreturn_t enc_isr(int irq, void *dev_id)
{
	WRITE_HREG(HCODEC_IRQ_MBOX_CLR, 1);

#ifdef DEBUG_UCODE
//rain 
       if(READ_HREG(DEBUG_REG)!=0){
            printk("dbg%x: %x\n",  READ_HREG(DEBUG_REG), READ_HREG(HENC_SCRATCH_1));
            WRITE_HREG(DEBUG_REG, 0);
            return IRQ_HANDLED;
       }
#endif

	encoder_status  = READ_HREG(ENCODER_STATUS);
	if((encoder_status == ENCODER_IDR_DONE)
	||(encoder_status == ENCODER_NON_IDR_DONE)
	||(encoder_status == ENCODER_SEQUENCE_DONE)
	||(encoder_status == ENCODER_PICTURE_DONE)){
		debug_level(0,"encoder stage is %d\n",encoder_status);
	}
	if(encoder_status == ENCODER_PICTURE_DONE)
		frame_start = 1;
	if(((encoder_status == ENCODER_IDR_DONE)
	||(encoder_status == ENCODER_NON_IDR_DONE))&&(!process_irq)){
		process_irq = 1;
		frame_start = 1;
		tasklet_schedule(&encode_tasklet);
	}
	return IRQ_HANDLED;
}

static void avc_prot_init(bool IDR)
{
	unsigned int data32;

	int pic_width, pic_height;
	int pic_mb_nr;
	int pic_mbx, pic_mby;
	int i_pic_qp, p_pic_qp;

	int i_pic_qp_c, p_pic_qp_c;
	pic_width  = encoder_width;
	pic_height = encoder_height;
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

    if(ucode_index != UCODE_MODE_SW_MIX){
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

    if(ucode_index != UCODE_MODE_SW_MIX){
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
    //debug_level(0,"current endian is %d \n" , avc_endian);
    data32 = READ_HREG(VLC_CONFIG);
    data32 = data32 | (1<<0); // set pop_coeff_even_all_zero
    WRITE_HREG(VLC_CONFIG , data32);	

    if(ucode_index != UCODE_MODE_SW_MIX){
        if(IDR){
            WRITE_HREG(BITS_INFO_DDR_START, intra_bits_info_ddr_start_addr); 
            WRITE_HREG(MV_INFO_DDR_START,intra_pred_info_ddr_start_addr);
        }else{
            WRITE_HREG(BITS_INFO_DDR_START, inter_bits_info_ddr_start_addr); 
            WRITE_HREG(MV_INFO_DDR_START, inter_mv_info_ddr_start_addr);
        }
    }else{
        WRITE_HREG(SW_CTL_INFO_DDR_START,sw_ctl_info_start_addr);
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
s32 amvenc_loadmc(const u32 *p)
{
    ulong timeout;
    s32 ret = 0 ;

    mc_addr_map = assit_buffer_offset;
    mc_addr = ioremap_wc(mc_addr_map,MC_SIZE);
    memcpy(mc_addr, p, MC_SIZE);
    debug_level(0,"address 0 is 0x%x\n", *((u32*)mc_addr));
    debug_level(0,"address 1 is 0x%x\n", *((u32*)mc_addr + 1));
    debug_level(0,"address 2 is 0x%x\n", *((u32*)mc_addr + 2));
    debug_level(0,"address 3 is 0x%x\n", *((u32*)mc_addr + 3));
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
            debug_level(1,"hcodec load mc error\n");
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
    hcodec_on |=(encode_opened>0);

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

static s32 reload_mc(void)
{
    const u32 * p = mix_dump_mc;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(enable_dblk)
                p = mix_dump_mc_dblk;
            else
                p = mix_dump_mc;    
            break;
        case UCODE_MODE_SW_MIX:
            if(IS_MESON_M8B_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
            else if(IS_MESON_M8_CPU){
                if(enable_dblk == 1)
                    p = mix_sw_mc_vdec2_dblk;
                else if (enable_dblk ==2)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }else if(IS_MESON_M8M2_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_m2_dblk;
                else
                    p = mix_sw_mc;
            }
#endif
            break;
        default:
            break;
    }
#endif

    amvenc_stop();

    WRITE_VREG(DOS_SW_RESET1, 0xffffffff);
    WRITE_VREG(DOS_SW_RESET1, 0);
	
    udelay(10);

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
        dblk_fix_flag = (p==mix_sw_mc_hdec_m2_dblk);
    }else{
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
        dblk_fix_flag = false;
    }
#else
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    dblk_fix_flag = false;
#endif

    debug_level(0,"reload microcode\n");

    if (amvenc_loadmc(p) < 0) {
        //amvdec_disable();
        return -EBUSY;
    }
    return 0;
}

static s32 avc_init(void)
{
    int r;
    const u32 * p = mix_dump_mc;

    avc_poweron(clock_level);
    avc_canvas_init();

    debug_level(1,"start to load microcode %d\n", ucode_index);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(enable_dblk)
                p = mix_dump_mc_dblk;
            else
                p = mix_dump_mc;    
            break;
        case UCODE_MODE_SW_MIX:
            if(IS_MESON_M8B_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
            else if(IS_MESON_M8_CPU){
                if(enable_dblk == 1)
                    p = mix_sw_mc_vdec2_dblk;
                else if (enable_dblk ==2)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }else if(IS_MESON_M8M2_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_m2_dblk;
                else
                    p = mix_sw_mc;
            }
#endif
            break;
        default:
            break;
    }

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
        dblk_fix_flag = (p==mix_sw_mc_hdec_m2_dblk);
    }else
#endif
    {
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
        dblk_fix_flag = false;
    }
#else
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    dblk_fix_flag = false;
#endif

    if (amvenc_loadmc(p) < 0) {
        //amvdec_disable();
        return -EBUSY;
    }	
    debug_level(1,"succeed to load microcode\n");
    //avc_canvas_init();
    frame_start = 0;
    idr_pic_id = 0 ;	
#ifdef DUMP_PRE_ENCODE_INTRA
    intra_pic_id = 0;	
#endif
    frame_number = 0 ;
    process_irq = 0;
    pic_order_cnt_lsb = 0 ;
    encoder_status = ENCODER_IDLE ;
    amvenc_reset();
  
    avc_init_encoder(true); 
    avc_init_input_buffer();  //dct buffer setting
    avc_init_output_buffer();  //output stream buffer
    avc_prot_init(true);
    r = request_irq(INT_AMVENCODER, enc_isr, IRQF_SHARED, "enc-irq", (void *)avc_dec_id);//INT_MAILBOX_1A
    avc_init_dblk_buffer(dblk_buf_canvas);   //decoder buffer , need set before each frame start
    avc_init_reference_buffer(ref_buf_canvas); //reference  buffer , need set before each frame start
    avc_init_assit_buffer(); //assitant buffer for microcode
    if(ucode_index != UCODE_MODE_SW_MIX){
        avc_init_ie_me_parameter();
    }
    else{
        if((dblk_buf_canvas&0xff)==ENC_CANVAS_OFFSET){
            WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_uv.buf_start);
        }
        else{
            WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_uv.buf_start);
        }
        WRITE_HREG(CANVAS_ROW_SIZE,(((encoder_width+31)>>5)<<5));
    }
    WRITE_HREG(ENCODER_STATUS , ENCODER_IDLE);

#ifdef MULTI_SLICE_MC    		
    if(fixed_slice_cfg){
        WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
    }else if(rows_per_slice !=  (encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (rows_per_slice<<16)|rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
#else
    WRITE_HREG(FIXED_SLICE_CFG, 0);
#endif
    amvenc_start();
    encode_inited = 1;
    return 0;
}

void amvenc_avc_start_cmd(int cmd, unsigned* input_info, int ucode_mode)
{
	int reload_flag = 0;
#ifdef USE_VDEC2
	if(IS_MESON_M8_CPU){
		if(((ucode_mode&0x1) == UCODE_MODE_SW_MIX)&&(enable_dblk>0)){
			if((get_vdec2_usage() == USAGE_DEC_4K2K)||(abort_vdec2_flag)){
				enable_dblk = 2;
				if((abort_vdec2_flag)&&(get_vdec2_usage() == USAGE_ENCODE)){
					debug_level(1,"switch encode ucode\n");
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
	if(ucode_mode!=ucode_index){
		ucode_index = ucode_mode&0x1;
		if(reload_mc()){
			debug_level(1,"reload mc fail\n");
			return;
		}
		reload_flag = 1;
		frame_start = 1;
       }

	if(frame_start){
		frame_start = 0;
		encoder_status = ENCODER_IDLE ;
		amvenc_reset();
		avc_init_encoder((cmd == ENCODER_IDR)?true:false);
		avc_init_input_buffer();
		avc_init_output_buffer();		
		avc_prot_init((cmd == ENCODER_IDR)?true:false);
		avc_init_assit_buffer(); 
		debug_level(0,"begin to new frame\n");
	}
	if((cmd == ENCODER_IDR)||(cmd == ENCODER_NON_IDR)){
		avc_init_dblk_buffer(dblk_buf_canvas);   
		avc_init_reference_buffer(ref_buf_canvas);
	}
	if(ucode_index != UCODE_MODE_SW_MIX){
		if((cmd == ENCODER_IDR)||(cmd == ENCODER_NON_IDR)){
			set_input_format((amvenc_mem_type)input_info[0], (amvenc_frame_fmt)input_info[1], input_info[2], input_info[3], input_info[4],(unsigned char)input_info[5]);
		}
		avc_init_ie_me_parameter();
	}
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	else{
		if((dblk_buf_canvas&0xff)==ENC_CANVAS_OFFSET){
			WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_y.buf_start);
			WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_uv.buf_start);
		}else{
			WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_y.buf_start);
			WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_uv.buf_start);
		}
		WRITE_HREG(CANVAS_ROW_SIZE,(((encoder_width+31)>>5)<<5));

#ifdef USE_VDEC2
		if((enable_dblk == 1)&&(IS_MESON_M8_CPU)){
			amvdec2_stop();// amvdec2_stop(); //++++
			WRITE_VREG(VDEC2_AV_SCRATCH_2, 0xffff);
			// set vdec2 input, clone hcodec input buffer and set to manual mode
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CONTROL, 0); //++++

			WRITE_VREG(DOS_SW_RESET2, (1<<4));
			WRITE_VREG(DOS_SW_RESET2, 0);
			(void)READ_VREG(DOS_SW_RESET2);
			(void)READ_VREG(DOS_SW_RESET2);
			WRITE_VREG(VDEC2_POWER_CTL_VLD, (1<<4)|(1<<6)|(1<<9));//++++

			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_START_PTR,BitstreamStart);//++++
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_END_PTR,BitstreamEnd);//++++
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CURR_PTR,BitstreamStart);//++++
    
			SET_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_CONTROL, 1);//++++
			CLEAR_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_CONTROL, 1);//++++
    
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 2);//++++
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_WP,BitstreamStart);//++++
    
			SET_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 1);//++++
			CLEAR_VREG_MASK(VDEC2_VLD_MEM_VIFIFO_BUF_CNTL, 1);//++++
    
			WRITE_VREG(VDEC2_VLD_MEM_VIFIFO_CONTROL, (0x11<<16) | (1<<10) | (7<<3) | (1<<2) | (1<<1)); //++++
            
			amvdec2_loadmc(vdec2_encoder_mc);//++++

			WRITE_VREG(VDEC2_AV_SCRATCH_1, vdec2_start_addr - VDEC2_DEF_BUF_START_ADDR);//++++
			WRITE_VREG(VDEC2_AV_SCRATCH_8, log2_max_pic_order_cnt_lsb);//++++
			WRITE_VREG(VDEC2_AV_SCRATCH_9, log2_max_frame_num);//++++
			WRITE_VREG(VDEC2_AV_SCRATCH_B, qppicture);//++++
			WRITE_VREG(VDEC2_AV_SCRATCH_A, (((encoder_height+15)/16) << 16) | ((encoder_width+15)/16));//++++
    
			// Input/Output canvas
			WRITE_VREG(VDEC2_ANC0_CANVAS_ADDR, ref_buf_canvas);//++++
			WRITE_VREG(VDEC2_ANC1_CANVAS_ADDR, dblk_buf_canvas);//++++
    
			WRITE_VREG(DECODED_MB_Y, 0);
			// MBY limit
			WRITE_VREG(DECODABLE_MB_Y, 0);
			// VB WP
			WRITE_VREG(STREAM_WR_PTR, BitstreamStart);
			// NV21
			SET_VREG_MASK(VDEC2_MDEC_PIC_DC_CTRL, 1<<17);//++++
    
			WRITE_VREG(VDEC2_M4_CONTROL_REG, 1<<13); //set h264_en//++++
			WRITE_VREG(VDEC2_MDEC_PIC_DC_THRESH, 0x404038aa);//++++
    	      
			amvdec2_start();//amvdec2_start();//++++
		}
#endif	    
	}	    
#endif
	encoder_status = cmd;
	WRITE_HREG(ENCODER_STATUS , cmd);
	if((cmd == ENCODER_IDR)||(cmd == ENCODER_NON_IDR)){
		process_irq = 0;
	}
#ifdef MULTI_SLICE_MC    		
	if(fixed_slice_cfg){
		WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
	}else if(rows_per_slice !=  (encoder_height+15)>>4){
		WRITE_HREG(FIXED_SLICE_CFG, (rows_per_slice<<16)|rows_per_slice);
	}else{
		WRITE_HREG(FIXED_SLICE_CFG, 0);
	}
#else
	WRITE_HREG(FIXED_SLICE_CFG, 0);
#endif
	if(reload_flag)
	    amvenc_start();
	debug_level(0,"amvenc_avc_start\n");
}

void amvenc_avc_stop(void)
{
#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if(get_vdec2_usage() != USAGE_DEC_4K2K)
            amvdec2_stop();// amvdec2_stop(); //++++
    }
#endif
    amvenc_stop();
    avc_poweroff();
    debug_level(1,"amvenc_avc_stop\n");
}

#ifdef CONFIG_CMA
static struct platform_device *this_pdev;
static struct page *venc_pages;
#endif

static int amvenc_avc_open(struct inode *inode, struct file *file)
{
    int r = 0;
    debug_level(1,"avc open\n");
#ifdef CONFIG_AM_JPEG_ENCODER
    if(jpegenc_on() == true){
        debug_level(1,"hcodec in use for JPEG Encode now.\n");
        return -EBUSY;
    }
#endif
    if(encode_opened>0){
        amlog_level(LOG_LEVEL_ERROR, "amvenc_avc open busy.\n");
        return -EBUSY;
    }

#ifdef CONFIG_CMA
    venc_pages = dma_alloc_from_contiguous(&this_pdev->dev, (18 * SZ_1M) >> PAGE_SHIFT, 0);
    if(venc_pages)
    {
        gAmvencbuff.buf_start = page_to_phys(venc_pages);
        gAmvencbuff.buf_size = 18 * SZ_1M;
        pr_info("%s: allocating phys %p, size %dk\n", __func__, (void *)gAmvencbuff.buf_start, gAmvencbuff.buf_size >> 10);
    }
    else
    {
        pr_err("CMA failed to allocate dma buffer for %s\n", this_pdev->name);
        return -ENOMEM;
    }

    if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_1080P;
        gAmvencbuff.bufspec = (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P];
    }else if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_720P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_720P;
        gAmvencbuff.bufspec= (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_720P];
    }else if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_480P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_480P;
        gAmvencbuff.bufspec= (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_480P];
    }else{
        gAmvencbuff.buf_start = 0;
        gAmvencbuff.buf_size = 0;
        amlog_level(LOG_LEVEL_ERROR, "amvenc_avc memory resource too small, size is %d.\n",gAmvencbuff.buf_size);
        return -EFAULT;
    }
    inter_bits_info_ddr_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->inter_bits_info.buf_start; // 32 bytes alignment
    inter_mv_info_ddr_start_addr  = gAmvencbuff.buf_start+gAmvencbuff.bufspec->inter_mv_info.buf_start;
    intra_bits_info_ddr_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->intra_bits_info.buf_start; // 32 bytes alignment
    intra_pred_info_ddr_start_addr  = gAmvencbuff.buf_start+gAmvencbuff.bufspec->intra_pred_info.buf_start;
    sw_ctl_info_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->qp_info.buf_start;
#ifdef USE_VDEC2
    vdec2_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->vdec2_info.buf_start;
#endif
    debug_level(1,"amvenc_avc  memory config sucess, buff size is 0x%x, level is %s\n",gAmvencbuff.buf_size,(gAmvencbuff.cur_buf_lev == 0)?"480P":(gAmvencbuff.cur_buf_lev == 1)?"720P":"1080P");

#endif

    init_waitqueue_head(&avc_wait);
    atomic_set(&avc_ready, 0);
    tasklet_init(&encode_tasklet, encode_isr_tasklet, 0);
    encode_opened++;
#ifdef USE_VDEC2
    abort_vdec2_flag = 0;
#endif
    return r;
}

static int amvenc_avc_release(struct inode *inode, struct file *file)
{
    if(encode_inited){
        free_irq(INT_AMVENCODER, (void *)avc_dec_id);
        //amvdec_disable();
        amvenc_avc_stop();
        encode_inited = 0;
    }
    if(encode_opened>0)
        encode_opened--;

#ifdef CONFIG_CMA
    if(venc_pages){
        dma_release_from_contiguous(&this_pdev->dev, venc_pages, (18 * SZ_1M)>>PAGE_SHIFT); 
        venc_pages = 0;
    }
#endif
    debug_level(1,"avc release\n");
    return 0;
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

static u32 amvenc_avc_light_reset(unsigned value)
{
    const u32 * p = mix_dump_mc;
    int r = 0;
    free_irq(INT_AMVENCODER, (void *)avc_dec_id);
	
#ifdef USE_VDEC2
    if(IS_MESON_M8_CPU){
        if(get_vdec2_usage() != USAGE_DEC_4K2K)
            amvdec2_stop();// amvdec2_stop(); //++++
    }
#endif
    amvenc_stop();
    avc_poweroff();

    mdelay(value);
    avc_poweron(1);
    avc_canvas_init();

    ucode_index = UCODE_MODE_FULL;

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    switch(ucode_index){
        case UCODE_MODE_FULL:
            if(enable_dblk)
                p = mix_dump_mc_dblk;
            else
                p = mix_dump_mc;    
            break;
        case UCODE_MODE_SW_MIX:
            if(IS_MESON_M8B_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
            else if(IS_MESON_M8_CPU){
                if(enable_dblk == 1)
                    p = mix_sw_mc_vdec2_dblk;
                else if (enable_dblk ==2)
                    p = mix_sw_mc_hdec_dblk;
                else
                    p = mix_sw_mc;
            }else if(IS_MESON_M8M2_CPU){
                if(enable_dblk)
                    p = mix_sw_mc_hdec_m2_dblk;
                else
                    p = mix_sw_mc;
            }
#endif
            break;
        default:
            break;
    }

#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON8
    if(IS_MESON_M8M2_CPU){
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
        dblk_fix_flag = (p==mix_sw_mc_hdec_m2_dblk);
    }else
#endif
    {
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
        dblk_fix_flag = false;
    }
#else
    WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    dblk_fix_flag = false;
#endif

    if (amvenc_loadmc(p) < 0) {
        debug_level(1,"amvenc_avc_light_reset fail \n");
        return -EBUSY;
    }	

    frame_start = 1;
    process_irq = 0;
    encoder_status = ENCODER_IDLE ;
    amvenc_reset();
    avc_init_encoder(true); 
    avc_init_input_buffer();  //dct buffer setting
    avc_init_output_buffer();  //output stream buffer
    avc_prot_init(true);
    r = request_irq(INT_AMVENCODER, enc_isr, IRQF_SHARED, "enc-irq", (void *)avc_dec_id);//INT_MAILBOX_1A
    avc_init_dblk_buffer(dblk_buf_canvas);   //decoder buffer , need set before each frame start
    avc_init_reference_buffer(ref_buf_canvas); //reference  buffer , need set before each frame start
    avc_init_assit_buffer(); //assitant buffer for microcode
    if(ucode_index != UCODE_MODE_SW_MIX){
        avc_init_ie_me_parameter();
    }
    else{
        if((dblk_buf_canvas&0xff)==ENC_CANVAS_OFFSET){
            WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec0_uv.buf_start);
        }
        else{
            WRITE_HREG(CURRENT_Y_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_y.buf_start);
            WRITE_HREG(CURRENT_C_CANVAS_START, gAmvencbuff.buf_start + gAmvencbuff.bufspec->dec1_uv.buf_start);
        }
        WRITE_HREG(CANVAS_ROW_SIZE,(((encoder_width+31)>>5)<<5));
    }
    WRITE_HREG(ENCODER_STATUS , ENCODER_IDLE);

#ifdef MULTI_SLICE_MC    		
    if(fixed_slice_cfg){
        WRITE_HREG(FIXED_SLICE_CFG, fixed_slice_cfg);
    }else if(rows_per_slice !=  (encoder_height+15)>>4){
        WRITE_HREG(FIXED_SLICE_CFG, (rows_per_slice<<16)|rows_per_slice);
    }else{
        WRITE_HREG(FIXED_SLICE_CFG, 0);
    }
#else
    WRITE_HREG(FIXED_SLICE_CFG, 0);
#endif
    amvenc_start();
    debug_level(1,"amvenc_avc_light_reset finish \n");
    return 0;
}

static long amvenc_avc_ioctl(struct file *file,
                           unsigned int cmd, ulong arg)
{
    int r = 0;
    unsigned amrisc_cmd = 0;
    unsigned offset;
    #define MAX_ADDR_INFO_SIZE 30
    unsigned addr_info[MAX_ADDR_INFO_SIZE + 4];     
    ulong argV;
    unsigned buf_start;
    int canvas = -1;
    canvas_t dst;
    switch (cmd) {
	case AMVENC_AVC_IOC_GET_ADDR:
		if((ref_buf_canvas & 0xff) == (ENC_CANVAS_OFFSET)){
			 put_user(1,(int *)arg);
		}else{
			 put_user(2,(int *)arg);
		}
		break;
	case AMVENC_AVC_IOC_INPUT_UPDATE:
		get_user(offset,((unsigned*)arg));
		WRITE_HREG(QDCT_MB_WR_PTR, (dct_buff_start_addr+ offset));
		break;    
	case AMVENC_AVC_IOC_NEW_CMD:
		if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE)){
			debug_level(1,"avc get new cmd error\n");
			return -1;
		}
#ifdef DUMP_PRE_ENCODE_INTRA
		if(addr_info[0] == ENCODER_IDR && addr_info[8] == 1)
			preencode_intra = true;
		else
			preencode_intra = false;
#endif
		if((addr_info[1]&0x1) != UCODE_MODE_SW_MIX){ // mc mode
			amvenc_avc_start_cmd(addr_info[0], &addr_info[2], addr_info[1]);
		}else{
			amvenc_avc_start_cmd(addr_info[0], NULL, addr_info[1]);
		}
		break;
	case AMVENC_AVC_IOC_GET_STAGE:
		put_user(encoder_status,(int *)arg);
		break; 
	case AMVENC_AVC_IOC_GET_OUTPUT_SIZE:	
		put_user(READ_HREG(VLC_TOTAL_BYTES),(int *)arg);
		break;
	case AMVENC_AVC_IOC_SET_QUANT:
		get_user(quant,((unsigned*)arg));
		break;
	case AMVENC_AVC_IOC_SET_ENCODER_WIDTH:
		get_user(argV,((unsigned*)arg));
		if(argV>gAmvencbuff.bufspec->max_width)
		    argV = gAmvencbuff.bufspec->max_width;
		else
		    encoder_width = argV;
		put_user(argV,(int *)arg);
		break;
	case AMVENC_AVC_IOC_SET_ENCODER_HEIGHT:
		get_user(argV,((unsigned*)arg));
		if(argV > gAmvencbuff.bufspec->max_height)
		    argV = gAmvencbuff.bufspec->max_height;
		else
		    encoder_height = argV;
		put_user(argV,(int *)arg);
		break;	
	case AMVENC_AVC_IOC_CONFIG_INIT:
		if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE)){
			debug_level(1,"avc config init error\n");
			return -1;
		}
		if(addr_info[0] <= UCODE_MODE_SW_MIX)
			ucode_index = addr_info[0];
		else
			ucode_index = UCODE_MODE_FULL;

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
		ucode_index = UCODE_MODE_FULL;
#endif

#ifdef MULTI_SLICE_MC
		rows_per_slice = addr_info[1];
		debug_level(1,"avc init -- rows_per_slice: %d \n", rows_per_slice);
#endif
		debug_level(1,"avc init as mode %d \n",ucode_index);
		avc_init();
		addr_info[1] = gAmvencbuff.bufspec->dct.buf_start;
		addr_info[2] = gAmvencbuff.bufspec->dct.buf_size;
		addr_info[3] = gAmvencbuff.bufspec->dec0_y.buf_start;
		addr_info[4] = gAmvencbuff.bufspec->dec0_y.buf_size;
		addr_info[5] = gAmvencbuff.bufspec->dec0_uv.buf_start;
		addr_info[6] = gAmvencbuff.bufspec->dec0_uv.buf_size;
		addr_info[7] = gAmvencbuff.bufspec->dec1_y.buf_start;
		addr_info[8] = gAmvencbuff.bufspec->dec1_y.buf_size;
		addr_info[9] = gAmvencbuff.bufspec->dec1_uv.buf_start;
		addr_info[10] = gAmvencbuff.bufspec->dec1_uv.buf_size;
		addr_info[11] = gAmvencbuff.bufspec->bitstream.buf_start;
		addr_info[12] = gAmvencbuff.bufspec->bitstream.buf_size;
		addr_info[13] = gAmvencbuff.bufspec->inter_bits_info.buf_start;
		addr_info[14] = gAmvencbuff.bufspec->inter_bits_info.buf_size;
		addr_info[15] = gAmvencbuff.bufspec->inter_mv_info.buf_start;
		addr_info[16] = gAmvencbuff.bufspec->inter_mv_info.buf_size;
		addr_info[17] = gAmvencbuff.bufspec->intra_bits_info.buf_start;
		addr_info[18] = gAmvencbuff.bufspec->intra_bits_info.buf_size;
		addr_info[19] = gAmvencbuff.bufspec->intra_pred_info.buf_start;
		addr_info[20] = gAmvencbuff.bufspec->intra_pred_info.buf_size;
		addr_info[21] = gAmvencbuff.bufspec->qp_info.buf_start;
		addr_info[22] = gAmvencbuff.bufspec->qp_info.buf_size;
		r = copy_to_user((unsigned *)arg, addr_info , 23*sizeof(unsigned));
		break;		
	case AMVENC_AVC_IOC_FLUSH_CACHE:
		if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE)){
			debug_level(1,"avc fluch cache error\n");
			return -1;
		}
		switch(addr_info[0]){
			case 0:
			buf_start = dct_buff_start_addr;
			break;
			case 1:
			buf_start = dct_buff_start_addr + gAmvencbuff.bufspec->dec0_y.buf_start;
			break;
			case 2:
			buf_start = dct_buff_start_addr + gAmvencbuff.bufspec->dec1_y.buf_start;
			break;
			case 3:
			buf_start = BitstreamStart ;
			break;
			case 4:
			buf_start = inter_bits_info_ddr_start_addr;
			break;
			case 5:
			buf_start = intra_bits_info_ddr_start_addr;
			break;
			case 6:
			buf_start = sw_ctl_info_start_addr;
			break;
			default:
			buf_start = dct_buff_start_addr;
			break;
		}
		dma_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
		break;
	case AMVENC_AVC_IOC_FLUSH_DMA:
		if(copy_from_user(addr_info,(void*)arg,MAX_ADDR_INFO_SIZE)){
			debug_level(1,"avc fluch dma error\n");
			return -1;
		}
		switch(addr_info[0]){
			case 0:
			buf_start = dct_buff_start_addr;
			break;
			case 1:
			buf_start = dct_buff_start_addr + gAmvencbuff.bufspec->dec0_y.buf_start;
			break;
			case 2:
			buf_start = dct_buff_start_addr + gAmvencbuff.bufspec->dec1_y.buf_start;
			break;
			case 3:
			buf_start = BitstreamStart ;
			break;
			case 4:
			buf_start = inter_bits_info_ddr_start_addr;
			break;
			case 5:
			buf_start = intra_bits_info_ddr_start_addr;
			break;
			default:
			buf_start = dct_buff_start_addr;
			break;
		}	    
 		cache_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
		break;
	case AMVENC_AVC_IOC_GET_BUFFINFO:
		put_user(gAmvencbuff.buf_size,(unsigned *)arg);
		break;
	case AMVENC_AVC_IOC_SET_IE_ME_MB_TYPE:
		get_user(ie_me_mb_type,((unsigned*)arg));
		break;
	case AMVENC_AVC_IOC_SET_ME_PIXEL_MODE:
		get_user(argV,((unsigned*)arg));
		ie_me_mode |= (argV & ME_PIXEL_MODE_MASK)<<ME_PIXEL_MODE_SHIFT;
		break;
	case AMVENC_AVC_IOC_GET_DEVINFO:
		r = copy_to_user((char *)arg,AMVENC_DEV_VERSION,strlen(AMVENC_DEV_VERSION));
		break;
	case AMVENC_AVC_IOC_SUBMIT_ENCODE_DONE:
		get_user(amrisc_cmd,((unsigned*)arg));
		if(amrisc_cmd == ENCODER_IDR){
			idr_pic_id ++;
			if(idr_pic_id > 65535)
				idr_pic_id = 0;
			pic_order_cnt_lsb = 2;	
			frame_number = 1;
#ifdef DUMP_PRE_ENCODE_INTRA
			intra_pic_id++;
			if(intra_pic_id > 65535)
				intra_pic_id = 0;
#endif
		}else if(amrisc_cmd == ENCODER_NON_IDR){
			frame_number ++;
			pic_order_cnt_lsb += 2;
			if(frame_number > 65535)
				frame_number = 0;
#ifdef DUMP_PRE_ENCODE_INTRA
			intra_pic_id++;
			if(intra_pic_id > 65535)
				intra_pic_id = 0;
#endif
		}
		amrisc_cmd = dblk_buf_canvas;
		dblk_buf_canvas = ref_buf_canvas;
		ref_buf_canvas = amrisc_cmd;   //current dblk buffer as next reference buffer
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
	case AMVENC_AVC_IOC_LIGHT_RESET:
		get_user(argV,((unsigned*)arg));
		amvenc_avc_light_reset(argV);
		break;
	default:
		r= -1;
		break;
    }
    return r;
}

static int avc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vma_size = vma->vm_end - vma->vm_start;

    if (vma_size == 0) {
        debug_level(1,"vma_size is 0 \n");
        return -EAGAIN;
    }
    if(!off)
        off += gAmvencbuff.buf_start;
    debug_level(0,"vma_size is %d , off is %ld \n" , vma_size ,off);
    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_IO;
    //vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                        vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        debug_level(1,"set_cached: failed remap_pfn_range\n");
        return -EAGAIN;
    }
    return 0;

}

static unsigned int amvenc_avc_poll(struct file *file, poll_table *wait_table)
{
    poll_wait(file, &avc_wait, wait_table);

    if (atomic_read(&avc_ready)) {
        atomic_dec(&avc_ready);
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

int  init_avc_device(void)
{
    int  r =0;
    r =register_chrdev(0,DEVICE_NAME,&amvenc_avc_fops);
    if(r<=0) 
    {
        amlog_level(LOG_LEVEL_HIGH,"register amvenc_avc device error\r\n");
        return  r  ;
    }
    avc_device_major= r ;
    
    amvenc_avc_class = class_create(THIS_MODULE, DEVICE_NAME);

    amvenc_avc_dev = device_create(amvenc_avc_class, NULL,
                                  MKDEV(avc_device_major, 0), NULL,
                                  DEVICE_NAME);
    return r;
}
int uninit_avc_device(void)
{
    device_destroy(amvenc_avc_class, MKDEV(avc_device_major, 0));

    class_destroy(amvenc_avc_class);

    unregister_chrdev(avc_device_major, DEVICE_NAME);	
    return 0;
}

#ifndef CONFIG_CMA
static struct resource memobj;
#endif
static int amvenc_avc_probe(struct platform_device *pdev)
{

    amlog_level(LOG_LEVEL_INFO, "amvenc_avc probe start.\n");

#ifdef CONFIG_CMA
    this_pdev = pdev;
#else
    struct resource *mem;
    int idx;
    mem = &memobj;
    idx = find_reserve_block(pdev->dev.of_node->name,0);
    if(idx < 0){
		amlog_level(LOG_LEVEL_ERROR, "amvenc_avc memory resource undefined.\n");
        return -EFAULT;
    }
    mem->start = (phys_addr_t)get_reserve_block_addr(idx);
    mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(idx)-1;
    gAmvencbuff.buf_start = mem->start;
    gAmvencbuff.buf_size = mem->end - mem->start + 1;

    if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_1080P;
        gAmvencbuff.bufspec = (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_1080P];
    }else if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_720P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_720P;
        gAmvencbuff.bufspec= (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_720P];
    }else if(gAmvencbuff.buf_size>=amvenc_buffspec[AMVENC_BUFFER_LEVEL_480P].min_buffsize){
        gAmvencbuff.cur_buf_lev = AMVENC_BUFFER_LEVEL_480P;
        gAmvencbuff.bufspec= (BuffInfo_t*)&amvenc_buffspec[AMVENC_BUFFER_LEVEL_480P];
    }else{
        gAmvencbuff.buf_start = 0;
        gAmvencbuff.buf_size = 0;
        amlog_level(LOG_LEVEL_ERROR, "amvenc_avc memory resource too small, size is %d.\n",gAmvencbuff.buf_size);
        return -EFAULT;
    }

    inter_bits_info_ddr_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->inter_bits_info.buf_start; // 32 bytes alignment
    inter_mv_info_ddr_start_addr  = gAmvencbuff.buf_start+gAmvencbuff.bufspec->inter_mv_info.buf_start;
    intra_bits_info_ddr_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->intra_bits_info.buf_start; // 32 bytes alignment
    intra_pred_info_ddr_start_addr  = gAmvencbuff.buf_start+gAmvencbuff.bufspec->intra_pred_info.buf_start;
    sw_ctl_info_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->qp_info.buf_start;
#ifdef USE_VDEC2
    vdec2_start_addr = gAmvencbuff.buf_start+gAmvencbuff.bufspec->vdec2_info.buf_start;
#endif
    debug_level(1,"amvenc_avc  memory config sucess, buff size is 0x%x, level is %s\n",gAmvencbuff.buf_size,(gAmvencbuff.cur_buf_lev == 0)?"480P":(gAmvencbuff.cur_buf_lev == 1)?"720P":"1080P");
#endif

    init_avc_device();
    amlog_level(LOG_LEVEL_INFO, "amvenc_avc probe end.\n");
    return 0;
}

static int amvenc_avc_remove(struct platform_device *pdev)
{
    uninit_avc_device();
    amlog_level(LOG_LEVEL_INFO, "amvenc_avc remove.\n");
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
    amlog_level(LOG_LEVEL_INFO, "amvenc_avc module init\n");

    if (platform_driver_register(&amvenc_avc_driver)) {
        amlog_level(LOG_LEVEL_ERROR, "failed to register amvenc_avc driver\n");
        return -ENODEV;
    }
    vcodec_profile_register(&amvenc_avc_profile);
    return 0;
}

static void __exit amvenc_avc_driver_remove_module(void)
{
    amlog_level(LOG_LEVEL_INFO, "amvenc_avc module remove.\n");
	
    platform_driver_unregister(&amvenc_avc_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n amvenc_avc stat \n");

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

module_param(ucode_index, uint, 0664);
MODULE_PARM_DESC(ucode_index, "\n ucode_index \n");

module_param(enable_dblk, uint, 0664);
MODULE_PARM_DESC(enable_dblk, "\n enable_dblk \n");

module_param(clock_level, uint, 0664);
MODULE_PARM_DESC(clock_level, "\n clock_level \n");

module_init(amvenc_avc_driver_init_module);
module_exit(amvenc_avc_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC AVC Video Encoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("simon.zheng <simon.zheng@amlogic.com>");
