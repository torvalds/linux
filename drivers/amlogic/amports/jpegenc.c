/*
 * AMLOGIC JPEG Encoder driver.
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

#define ENC_CANVAS_OFFSET  AMVENC_CANVAS_INDEX


#define LOG_LEVEL_VAR 1
#define debug_level(level, x...) \
	do { \
		if (level >= LOG_LEVEL_VAR) \
			printk(x); \
	} while (0);


#ifdef CONFIG_AM_VDEC_MJPEG_LOG
#define AMLOG
#define LOG_LEVEL_VAR       amlog_level_jpeg
#define LOG_MASK_VAR        amlog_mask_jpeg
#define LOG_LEVEL_ERROR     0
#define LOG_LEVEL_INFO      1
#define LOG_LEVEL_DESC  "0:ERROR, 1:INFO"
#endif
#include <linux/amlogic/amlog.h>
MODULE_AMLOG(LOG_LEVEL_ERROR, 0, LOG_LEVEL_DESC, LOG_DEFAULT_MASK_DESC);

#include "jpegenc.h"
#include "amvdec.h"
#include "jpegenc_mc.h"
static int jpegenc_device_major = 0;
static struct class *jpegenc_class;
static struct device *jpegenc_dev;
#define DRIVER_NAME "jpegenc"
#define MODULE_NAME "jpegenc"
#define DEVICE_NAME "jpegenc"

/* protocol register usage
	#define ENCODER_STATUS            HENC_SCRATCH_0    : encode stage
	#define MEM_OFFSET_REG            HENC_SCRATCH_1    : assit buffer physical address
	#define DEBUG_REG  				  HENC_SCRATCH_2    : debug register
	#define MB_COUNT				  HENC_SCRATCH_3	: MB encoding number
*/

/*output buffer define*/
static unsigned BitstreamStart;
static unsigned BitstreamEnd;  

static unsigned* BitstreamStartVirtAddr;
static unsigned dct_buff_start_addr;  
static unsigned dct_buff_end_addr;

static u32 stat;

static u32 encoder_width = 1280;
static u32 encoder_height = 720;
static jpegenc_frame_fmt input_format;
static jpegenc_frame_fmt output_format;
static int jpeg_quality = 90;
static unsigned short gQuantTable[2][DCTSIZE2];
static unsigned int gQuantTable_id  = 0;
static unsigned short* gExternalQuantTablePtr = NULL;
static bool external_quant_table_available = false;

#ifdef CONFIG_AM_ENCODER
extern bool amvenc_avc_on(void);
#endif

static s32 jpegenc_poweron(void);
static void dma_flush(unsigned buf_start , unsigned buf_size);

static u32 process_irq = 0;

static int encode_inited = 0;
static int encode_opened = 0;
static int encoder_status = 0;

static wait_queue_head_t jpegenc_wait;
atomic_t jpegenc_ready = ATOMIC_INIT(0);
static struct tasklet_struct jpegenc_tasklet;

static DEFINE_SPINLOCK(lock);

static const char jpeg_enc_id[] = "jpegenc-dev";

#define JPEGENC_BUFFER_LEVEL_VGA   0
#define JPEGENC_BUFFER_LEVEL_2M     1
#define JPEGENC_BUFFER_LEVEL_3M     2
#define JPEGENC_BUFFER_LEVEL_5M     3
#define JPEGENC_BUFFER_LEVEL_8M     4
#define JPEGENC_BUFFER_LEVEL_13M   5
#define JPEGENC_BUFFER_LEVEL_HD     6

const char* glevel_str[] = {
    "VGA",
    "2M",
    "3M",
    "5M",
    "8M",
    "13M",
    "HD",
};

typedef struct
{
    u32 buf_start;
    u32 buf_size;
} Buff_t;

typedef struct
{
    u32 lev_id;
    u32 max_width;
    u32 max_height;
    u32 min_buffsize;
    Buff_t input;
    Buff_t bitstream;
} BuffInfo_t;

const BuffInfo_t jpegenc_buffspec[]={
    {
        .lev_id = JPEGENC_BUFFER_LEVEL_VGA,
        .max_width = 640,
        .max_height = 640,
        .min_buffsize = 0x330000,
        .input = {
            .buf_start = 0,
            .buf_size = 0x12c000,
        },
        .bitstream = {
            .buf_start = 0x130000,
            .buf_size = 0x200000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_2M,
        .max_width = 1600,
        .max_height = 1600,
        .min_buffsize = 0x960000,
        .input = {
            .buf_start = 0,
            .buf_size = 0x753000,
        },
        .bitstream = {
            .buf_start = 0x760000,
            .buf_size = 0x200000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_3M,
        .max_width = 2048,
        .max_height = 2048,
        .min_buffsize = 0xe10000,
        .input = {
            .buf_start = 0,
            .buf_size = 0xc00000,
        },
        .bitstream = {
            .buf_start = 0xc10000,
            .buf_size = 0x200000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_5M,
        .max_width = 2624,
        .max_height = 2624,
        .min_buffsize = 0x1800000,
        .input = {
            .buf_start = 0,
            .buf_size = 0x13B3000,
        },
        .bitstream = {
            .buf_start = 0x1400000,
            .buf_size = 0x400000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_8M,
        .max_width = 3264,
        .max_height = 3264,
        .min_buffsize = 0x2300000,
        .input = {
            .buf_start = 0,
            .buf_size = 0x1e7b000,
        },
        .bitstream = {
            .buf_start = 0x1f00000,
            .buf_size = 0x4000000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_13M,
        .max_width = 8192,
        .max_height = 8192,
        .min_buffsize = 0xc400000,
        .input = {
            .buf_start = 0,
            .buf_size = 0xc000000,
        },
        .bitstream = {
            .buf_start = 0xc000000,
            .buf_size = 0x4000000,
        }
    },{
        .lev_id = JPEGENC_BUFFER_LEVEL_HD,
        .max_width = 8192,
        .max_height = 8192,
        .min_buffsize = 0xc400000,
        .input = {
            .buf_start = 0,
            .buf_size = 0xc000000,
        },
        .bitstream = {
            .buf_start = 0xc000000,
            .buf_size = 0x4000000,
        }
    }
};

typedef struct
{
    u32 buf_start;
    u32 buf_size;
    u8 cur_buf_lev;
    BuffInfo_t* bufspec;
} EncBuffer_t;

static EncBuffer_t gJpegEncBuff = {0,0,0,NULL};

static void jpegenc_canvas_init(void);
//static void jpegenc_reset(void);
static void init_jpeg_encoder(void);
static int jpeg_quality_scaling (int quality);
static void convert_quant_table(unsigned short* qtable, unsigned short *basic_table, int scale_factor);
static void write_jpeg_quant_lut(int table_num);
static void write_jpeg_huffman_lut_dc(int table_num);
static void write_jpeg_huffman_lut_ac(int table_num);
static int  zigzag(int i);

static void push_word(int* offset , unsigned word)
{
    unsigned char* ptr;
    int i;
    int bytes = (word >> 24)&0xff;
    for(i = bytes-1 ;i >= 0;i--){
        ptr = (unsigned char*)(BitstreamStartVirtAddr ) + *offset;
        (*offset)++;	
        if(i ==0){
            *ptr = word&0xff;		
        }else if(i ==1){
            *ptr = (word >>8)&0xff;
        }else if(i == 2 ){
            *ptr = (word >>16)&0xff;
        }
    }	
}

static void prepare_jpeg_header(void)
{
    int pic_format; 
    int pic_width, pic_height;
    int q_sel_comp0, q_sel_comp1, q_sel_comp2;
    int dc_huff_sel_comp0, dc_huff_sel_comp1, dc_huff_sel_comp2;
    int ac_huff_sel_comp0, ac_huff_sel_comp1, ac_huff_sel_comp2;
    int lastcoeff_sel;
    int jdct_intr_sel;
    int h_factor_comp0, v_factor_comp0;
    int h_factor_comp1, v_factor_comp1;
    int h_factor_comp2, v_factor_comp2;
    int q_num;
    int tq[2];
    int dc_huff_num, ac_huff_num;
    int dc_th[2], ac_th[2];
    int header_bytes = 0 ;
    int bak_header_bytes = 0;
    int i ,j ;
	
    if(output_format >= MAX_FRAME_FMT){
        debug_level(0,"Input format is wrong!!!!\n");
    }
    switch(output_format){
        case FMT_NV21:
        case FMT_NV12:
        case FMT_YUV420:
            pic_format = 3;
            break;
        case FMT_YUV422_SINGLE:
            pic_format = 2;
            break;
        case FMT_YUV444_SINGLE:
        case FMT_YUV444_PLANE:
            pic_format = 1;
            break;
        default:
            pic_format = 0;
            break;
    }

    pic_width = encoder_width;
    pic_height = encoder_height;
        
    q_sel_comp0 = QUANT_SEL_COMP0;
    q_sel_comp1 = QUANT_SEL_COMP1;
    q_sel_comp2 = QUANT_SEL_COMP2;
       
    dc_huff_sel_comp0 = DC_HUFF_SEL_COMP0;
    dc_huff_sel_comp1 = DC_HUFF_SEL_COMP1;
    dc_huff_sel_comp2 = DC_HUFF_SEL_COMP2;
    ac_huff_sel_comp0 = AC_HUFF_SEL_COMP0;
    ac_huff_sel_comp1 = AC_HUFF_SEL_COMP1;
    ac_huff_sel_comp2 = AC_HUFF_SEL_COMP2;
    lastcoeff_sel = JDCT_LASTCOEFF_SEL;
    jdct_intr_sel = JDCT_INTR_SEL;

    if (pic_format == 2) { // YUV422
        h_factor_comp0 = 1; v_factor_comp0 = 0;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    } else if (pic_format == 3) { // YUV420
        h_factor_comp0 = 1; v_factor_comp0 = 1;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    } else {    // RGB or YUV
        h_factor_comp0 = 0; v_factor_comp0 = 0;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    }	
	
    // SOI marke
    push_word(&header_bytes , (2<< 24)|   // Number of bytes
                       (0xffd8  << 0));     // data: SOI marker
    //header_bytes += 2;  // Add SOI bytes
    
    // Define quantization tables
    q_num   = 1;
    if ((q_sel_comp0 != q_sel_comp1) || (q_sel_comp0 != q_sel_comp2) || (q_sel_comp1 != q_sel_comp2)) {
        q_num ++;
    }
    //tq[0] = q_sel_comp0;
    //tq[1] = (q_sel_comp0 != q_sel_comp1)? q_sel_comp1 :
    //           (q_sel_comp0 != q_sel_comp2)? q_sel_comp2 :
    //            q_sel_comp0;
    tq[0] = 0;
    tq[1] = q_num-1;

    push_word(&header_bytes , (2<< 24)  |
                       (0xffdb<< 0));     // data: DQT marker
    push_word(&header_bytes , (2<< 24)  |
                       ((2+65*q_num)<< 0));     // data: Lq
    for (i = 0; i < q_num; i ++) {
        push_word(&header_bytes  , (1<< 24)  |
                        (i<< 0));     // data: {Pq,Tq}
        for (j = 0; j < DCTSIZE2; j ++) {
            push_word(&header_bytes , (1<< 24)  |
                               ((gQuantTable[tq[i]][zigzag(j)])  << 0));     // data: Qk
        }
    }
    
    //header_bytes += (2 + (2+65*q_num)); // Add Quantization table bytes
    
    // Define Huffman tables
    
    dc_huff_num = 1;
    if ((dc_huff_sel_comp0 != dc_huff_sel_comp1) || (dc_huff_sel_comp0 != dc_huff_sel_comp2) || (dc_huff_sel_comp1 != dc_huff_sel_comp2)) {
        dc_huff_num ++;
    }
    ac_huff_num = 1;
    if ((ac_huff_sel_comp0 != ac_huff_sel_comp1) || (ac_huff_sel_comp0 != ac_huff_sel_comp2) || (ac_huff_sel_comp1 != ac_huff_sel_comp2)) {
        ac_huff_num ++;
    }
    dc_th[0] = dc_huff_sel_comp0;
    dc_th[1] = (dc_huff_sel_comp0 != dc_huff_sel_comp1)? dc_huff_sel_comp1 :
                  (dc_huff_sel_comp0 != dc_huff_sel_comp2)? dc_huff_sel_comp2 :
                  dc_huff_sel_comp0;
    ac_th[0] = ac_huff_sel_comp0;
    ac_th[1] = (ac_huff_sel_comp0 != ac_huff_sel_comp1)? ac_huff_sel_comp1 :
                  (ac_huff_sel_comp0 != ac_huff_sel_comp2)? ac_huff_sel_comp2 :
                  ac_huff_sel_comp0;
    
    push_word(&header_bytes  , (2<< 24)  |
                       (0xffc4<< 0));     // data: DHT marker
    push_word(&header_bytes  , (2<< 24)  |
                       ((2+(1+16+12)*dc_huff_num+(1+16+162)*ac_huff_num)<< 0));     // data: Lh
    for (i = 0; i < dc_huff_num; i ++) {
        push_word(&header_bytes , (1 << 24)  |
                           (i << 0));     // data: {Tc,Th}
        for (j = 0; j < 16+12; j ++) {
            push_word(&header_bytes  , (1<< 24)  |
                               ((jpeg_huffman_dc[dc_th[i]][j])  << 0));     // data: Li then Vi,j
        }
    }
    for (i = 0; i < ac_huff_num; i ++) {
        push_word(&header_bytes  , (1<< 24) |
                           (1<< 4) |   // data: Tc
                           (i<< 0));     // data: Th
        for (j = 0; j < 16+162; j ++) {
            push_word(&header_bytes , (1<< 24)  |
                               ((jpeg_huffman_ac[ac_th[i]][j])  << 0));     // data: Li then Vi,j
        }
    }
    
    //header_bytes += (2 + (2+(1+16+12)*dc_huff_num+(1+16+162)*ac_huff_num)); // Add Huffman table bytes
    
    // Frame header
    push_word(&header_bytes , (2<< 24)  |   // Number of bytes
                       (0xffc0<< 0));     // data: SOF_0 marker
    push_word(&header_bytes  , (2<< 24)  |
                       ((8+3*3)<< 0));     // data: Lf
    push_word(&header_bytes  , (1<< 24)  |
                       (8<< 0));     // data: P -- Sample precision
    push_word(&header_bytes  , (2<< 24)  |
                       (pic_height<< 0));     // data: Y -- Number of lines
    push_word(&header_bytes  , (2<< 24)  |
                       (pic_width<< 0));      // data: X -- Number of samples per line
    push_word(&header_bytes , (1<< 24)  |
                       (3<< 0));     // data: Nf -- Number of components in a frame
    push_word(&header_bytes  , (1<< 24)  |
                       (0<< 0));     // data: C0 -- Comp0 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       ((h_factor_comp0+1)<< 4)   |   // data: H0 -- Comp0 horizontal sampling factor
                       ((v_factor_comp0+1)<< 0));     // data: V0 -- Comp0 vertical sampling factor
    push_word(&header_bytes  , (1<< 24)  |
                       (0<< 0));     // data: Tq0 -- Comp0 quantization table seletor
    push_word(&header_bytes  , (1<< 24)  |
                       (1<< 0));     // data: C1 -- Comp1 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       ((h_factor_comp1+1)<< 4)   |   // data: H1 -- Comp1 horizontal sampling factor
                       ((v_factor_comp1+1)<< 0));     // data: V1 -- Comp1 vertical sampling factor
    push_word(&header_bytes  , (1<< 24)  |
                       (((q_sel_comp0 != q_sel_comp1)? 1 : 0)<< 0));     // data: Tq1 -- Comp1 quantization table seletor
    push_word(&header_bytes  , (1<< 24)  |
                       (2<< 0));     // data: C2 -- Comp2 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       ((h_factor_comp2+1)<< 4)   |   // data: H2 -- Comp2 horizontal sampling factor
                       ((v_factor_comp2+1)<< 0));     // data: V2 -- Comp2 vertical sampling factor
    push_word(&header_bytes  , (1<< 24)  |
                       (((q_sel_comp0 != q_sel_comp2)? 1 : 0)<< 0));     // data: Tq2 -- Comp2 quantization table seletor
    
    //header_bytes += (2 + (8+3*3));  // Add Frame header bytes
    
    // Scan header
    bak_header_bytes  = header_bytes + (2 + (6+2*3));
    
    //header_bytes += (2 + (6+2*3));  // Add Scan header bytes
    // If total header bytes is not multiple of 8, then fill 0xff byte between Frame header segment and the Scan header segment.
    for (i = 0; i < ((bak_header_bytes + 7)/8)*8 - bak_header_bytes; i ++) {
        push_word(&header_bytes  , (1<< 24)  |
                           (0xff<< 0)); // 0xff filler
    }
    // header_bytes = ((header_bytes + 7)/8)*8;
    
    push_word(&header_bytes  , (2<< 24)  |   // Number of bytes
                       (0xffda<< 0));     // data: SOS marker
    push_word(&header_bytes  , (2<< 24)  |
                       ((6+2*3)<< 0));     // data: Ls
    push_word(&header_bytes  , (1<< 24)  |
                       (3<< 0));     // data: Ns -- Number of components in a scan
    push_word(&header_bytes  , (1<< 24)  |
                       (0<< 0));     // data: Cs0 -- Comp0 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       (0<< 4)   |   // data: Td0 -- Comp0 DC Huffman table selector
                       (0<< 0));     // data: Ta0 -- Comp0 AC Huffman table selector
    push_word(&header_bytes  , (1<< 24)  |
                       (1<< 0));     // data: Cs1 -- Comp1 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       (((dc_huff_sel_comp0 != dc_huff_sel_comp1)? 1:0) << 4)   |   // data: Td1 -- Comp1 DC Huffman table selector
                       (((ac_huff_sel_comp0 != ac_huff_sel_comp1)? 1:0) << 0));     // data: Ta1 -- Comp1 AC Huffman table selector
    push_word(&header_bytes  , (1<< 24)  |
                       (2<< 0));     // data: Cs2 -- Comp2 identifier
    push_word(&header_bytes  , (1<< 24)  |
                       (((dc_huff_sel_comp0 != dc_huff_sel_comp2)? 1:0) << 4)   |   // data: Td2 -- Comp2 DC Huffman table selector
                       (((ac_huff_sel_comp0 != ac_huff_sel_comp2)? 1:0) << 0));     // data: Ta2 -- Comp2 AC Huffman table selector
    push_word(&header_bytes  , (3<< 24)  |
                       (0<< 16)  |   // data: Ss = 0
                       (63<< 8)   |   // data: Se = 63
                       (0<< 4)   |   // data: Ah = 0
                       (0<< 0));     // data: Al = 0
    debug_level(0,"jpeg header bytes is %d \n",header_bytes);  
    WRITE_HREG(HCODEC_HENC_SCRATCH_1 , header_bytes); // Send MEM_OFFSET
}

static void init_jpeg_encoder(void)
{
    unsigned long data32 ;
    int pic_format;             // 0=RGB; 1=YUV; 2=YUV422; 3=YUV420
    int pic_x_start, pic_x_end, pic_y_start, pic_y_end;

    int pic_width, pic_height;
    int q_sel_comp0, q_sel_comp1, q_sel_comp2;
    int dc_huff_sel_comp0, dc_huff_sel_comp1, dc_huff_sel_comp2;
    int ac_huff_sel_comp0, ac_huff_sel_comp1, ac_huff_sel_comp2;
    int lastcoeff_sel;
    int jdct_intr_sel;
    int h_factor_comp0, v_factor_comp0;
    int h_factor_comp1, v_factor_comp1;
    int h_factor_comp2, v_factor_comp2;
   
    int header_bytes;
    
    debug_level(0,"Initialize JPEG Encoder ....\n");   
    if(output_format >= MAX_FRAME_FMT){
        debug_level(0,"Input format is wrong!!!!\n");
    }
    switch(output_format){
        case FMT_NV21:
        case FMT_NV12:
        case FMT_YUV420:
            pic_format = 3;
            break;
        case FMT_YUV422_SINGLE:
            pic_format = 2;
            break;
        case FMT_YUV444_SINGLE:
        case FMT_YUV444_PLANE:
            pic_format = 1;
            break;
        default:
            pic_format = 0;
            break;
    }

    pic_x_start = 0;
    pic_x_end = encoder_width -1;
    pic_y_start = 0;
    pic_y_end = encoder_height - 1;
    pic_width = encoder_width;
    pic_height = encoder_height;
        
    q_sel_comp0 = gQuantTable_id*2;
    q_sel_comp1 = q_sel_comp0+1;
    q_sel_comp2 = q_sel_comp1;
       
    dc_huff_sel_comp0 = DC_HUFF_SEL_COMP0;
    dc_huff_sel_comp1 = DC_HUFF_SEL_COMP1;
    dc_huff_sel_comp2 = DC_HUFF_SEL_COMP2;
    ac_huff_sel_comp0 = AC_HUFF_SEL_COMP0;
    ac_huff_sel_comp1 = AC_HUFF_SEL_COMP1;
    ac_huff_sel_comp2 = AC_HUFF_SEL_COMP2;
    lastcoeff_sel =  JDCT_LASTCOEFF_SEL;
    jdct_intr_sel =  JDCT_INTR_SEL;

    if (pic_format == 2) { // YUV422
        h_factor_comp0 = 1; v_factor_comp0 = 0;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    } else if (pic_format == 3) { // YUV420
        h_factor_comp0 = 1; v_factor_comp0 = 1;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    } else {    // RGB or YUV
        h_factor_comp0 = 0; v_factor_comp0 = 0;
        h_factor_comp1 = 0; v_factor_comp1 = 0;
        h_factor_comp2 = 0; v_factor_comp2 = 0;
    }

    //--------------------------------------------------------------------------
    // Configure picture size and format  
    //--------------------------------------------------------------------------

    WRITE_HREG(HCODEC_VLC_PIC_SIZE ,pic_width | (pic_height<<16));
    WRITE_HREG(HCODEC_VLC_PIC_POSITION ,pic_format | (lastcoeff_sel <<4));

    WRITE_HREG(HCODEC_QDCT_JPEG_X_START_END , ((pic_x_end<<16) | (pic_x_start<<0)));
    WRITE_HREG(HCODEC_QDCT_JPEG_Y_START_END ,((pic_y_end<<16) | (pic_y_start<<0)));

    //--------------------------------------------------------------------------
    // Configure quantization tables
    //--------------------------------------------------------------------------
    if(external_quant_table_available){
        convert_quant_table(&gQuantTable[0][0], &gExternalQuantTablePtr[0], jpeg_quality);
        convert_quant_table(&gQuantTable[1][0], &gExternalQuantTablePtr[DCTSIZE2], jpeg_quality);
        q_sel_comp0 = 0;
        q_sel_comp1 = 1;
        q_sel_comp2 = 1;
    }else{
        int tq[2];
        tq[0] = q_sel_comp0;
        tq[1] = (q_sel_comp0 != q_sel_comp1)? q_sel_comp1 : (q_sel_comp0 != q_sel_comp2)? q_sel_comp2:q_sel_comp0;
        convert_quant_table(&gQuantTable[0][0], (unsigned short*)&jpeg_quant[tq[0]], jpeg_quality);
        if(tq[0]!=tq[1])
            convert_quant_table(&gQuantTable[1][0], (unsigned short*)&jpeg_quant[tq[1]], jpeg_quality);
        q_sel_comp0 = tq[0];
        q_sel_comp1 = tq[1];
        q_sel_comp2 = tq[1];
    }

    // Set Quantization LUT start address
    data32  = 0;
    data32 |= 0 << 8;   // [    8] 0=Write LUT, 1=Read
    data32 |= 0 << 0;   // [ 5: 0] Start addr = 0
   
    WRITE_HREG(HCODEC_QDCT_JPEG_QUANT_ADDR ,data32);
    
    // Burst-write Quantization LUT data
    write_jpeg_quant_lut(0);
    if(q_sel_comp0!=q_sel_comp1)
        write_jpeg_quant_lut(1);
#if 0
    write_jpeg_quant_lut(q_sel_comp0);
    if (q_sel_comp1 != q_sel_comp0) {
        write_jpeg_quant_lut(q_sel_comp1);
    }
    if ((q_sel_comp2 != q_sel_comp0) && (q_sel_comp2 != q_sel_comp1)) {
        write_jpeg_quant_lut(q_sel_comp2);
    }
#endif

    //--------------------------------------------------------------------------
    // Configure Huffman tables
    //--------------------------------------------------------------------------

    // Set DC Huffman LUT start address
    data32  = 0;
    data32 |= 0 << 16;  // [   16] 0=Write LUT, 1=Read
    data32 |= 0 << 0;   // [ 8: 0] Start addr = 0
    WRITE_HREG(HCODEC_VLC_HUFFMAN_ADDR ,data32);

    // Burst-write DC Huffman LUT data
    write_jpeg_huffman_lut_dc(dc_huff_sel_comp0);
    if (dc_huff_sel_comp1 != dc_huff_sel_comp0) {
        write_jpeg_huffman_lut_dc(dc_huff_sel_comp1);
    }
    if ((dc_huff_sel_comp2 != dc_huff_sel_comp0) && (dc_huff_sel_comp2 != dc_huff_sel_comp1)) {
        write_jpeg_huffman_lut_dc(dc_huff_sel_comp2);
    }
    
    // Set AC Huffman LUT start address
    data32  = 0;
    data32 |= 0 << 16;  // [   16] 0=Write LUT, 1=Read
    data32 |= 24<< 0;   // [ 8: 0] Start addr = 0
    WRITE_HREG(HCODEC_VLC_HUFFMAN_ADDR ,data32);

    // Burst-write AC Huffman LUT data
    write_jpeg_huffman_lut_ac(ac_huff_sel_comp0);
    if (ac_huff_sel_comp1 != ac_huff_sel_comp0) {
        write_jpeg_huffman_lut_ac(ac_huff_sel_comp1);
    }
    if ((ac_huff_sel_comp2 != ac_huff_sel_comp0) && (ac_huff_sel_comp2 != ac_huff_sel_comp1)) {
        write_jpeg_huffman_lut_ac(ac_huff_sel_comp2);
    }

    //--------------------------------------------------------------------------
    // Configure general control registers
    //--------------------------------------------------------------------------

    data32  = 0;
    data32 |= 0<< 18;  // [19:18] dct_inflow_ctrl: 0=No halt;
                                 //                          1=DCT halts request at end of each 8x8 block;
                                 //                          2=DCT halts request at end of each MCU.
    data32 |= lastcoeff_sel<< 16;  // [17:16] jpeg_coeff_last_sel: 0=Mark last coeff at the end of an 8x8 block,
                                                 //                              1=Mark last coeff at the end of an MCU
                                                 //                              2=Mark last coeff at the end of a scan
    data32 |= ((q_sel_comp2==q_sel_comp0)? 0 : 1)   << 15;  // [   15] jpeg_quant_sel_comp2
    data32 |= v_factor_comp2<< 14;  // [   14] jpeg_v_factor_comp2
    data32 |= h_factor_comp2<< 13;  // [   13] jpeg_h_factor_comp2
    data32 |= 1<< 12;  // [   12] jpeg_comp2_en
    data32 |= ((q_sel_comp1==q_sel_comp0)? 0 : 1)<< 11;  // [   11] jpeg_quant_sel_comp1
    data32 |= v_factor_comp1<< 10;  // [   10] jpeg_v_factor_comp1
    data32 |= h_factor_comp1<< 9;   // [    9] jpeg_h_factor_comp1
    data32 |= 1<< 8;   // [    8] jpeg_comp1_en
    data32 |= 0<< 7;   // [    7] jpeg_quant_sel_comp0
    data32 |= v_factor_comp0<< 6;   // [    6] jpeg_v_factor_comp0
    data32 |= h_factor_comp0<< 5;   // [    5] jpeg_h_factor_comp0
    data32 |= 1<< 4;   // [    4] jpeg_comp0_en
    data32 |= jdct_intr_sel<< 1;   // [ 3: 1] jdct_intr_sel:0=Disable intr;
                                                //                       1=Intr at end of each 8x8 block of DCT input;
                                                //                       2=Intr at end of each MCU of DCT input;
                                                //                       3=Intr at end of a scan of DCT input;
                                                //                       4=Intr at end of each 8x8 block of DCT output;
                                                //                       5=Intr at end of each MCU of DCT output;
                                                //                       6=Intr at end of a scan of DCT output.
    data32 |= 1<< 0;   // [    0] jpeg_en
    WRITE_HREG(HCODEC_QDCT_JPEG_CTRL ,data32);

    data32  = 0;
    data32 |= ((ac_huff_sel_comp2 == ac_huff_sel_comp0)? 0 : 1) << 29;  // [   29] jpeg_comp2_ac_table_sel
    data32 |= ((dc_huff_sel_comp2 == dc_huff_sel_comp0)? 0 : 1) << 28;  // [   28] jpeg_comp2_dc_table_sel
    data32 |= ((h_factor_comp2+1)*(v_factor_comp2+1)-1)<< 25;  // [26:25] jpeg_comp2_cnt_max
    data32 |= 1<< 24;  // [   24] jpeg_comp2_en
    data32 |= ((ac_huff_sel_comp1 == ac_huff_sel_comp0)? 0 : 1) << 21;  // [   21] jpeg_comp1_ac_table_sel
    data32 |= ((dc_huff_sel_comp1 == dc_huff_sel_comp0)? 0 : 1) << 20;  // [   20] jpeg_comp1_dc_table_sel
    data32 |= ((h_factor_comp1+1)*(v_factor_comp1+1)-1)         << 17;  // [18:17] jpeg_comp1_cnt_max
    data32 |= 1<< 16;  // [   16] jpeg_comp1_en
    data32 |= 0<< 13;  // [   13] jpeg_comp0_ac_table_sel
    data32 |= 0<< 12;  // [   12] jpeg_comp0_dc_table_sel
    data32 |= ((h_factor_comp0+1)*(v_factor_comp0+1)-1)         << 9;   // [10: 9] jpeg_comp0_cnt_max
    data32 |= 1<< 8;   // [    8] jpeg_comp0_en
    data32 |= 0<< 0;   // [    0] jpeg_en, will be enbled by amrisc
    WRITE_HREG(HCODEC_VLC_JPEG_CTRL ,data32);

    WRITE_HREG(HCODEC_QDCT_MB_CONTROL, 
                (1<<9) | // mb_info_soft_reset
                (1<<0));  // mb read buffer soft reset

    WRITE_HREG(HCODEC_QDCT_MB_CONTROL ,
              (0<<28) | // ignore_t_p8x8
              (0<<27) | // zero_mc_out_null_non_skipped_mb
              (0<<26) | // no_mc_out_null_non_skipped_mb
              (0<<25) | // mc_out_even_skipped_mb
              (0<<24) | // mc_out_wait_cbp_ready
              (0<<23) | // mc_out_wait_mb_type_ready
              (0<<29) | // ie_start_int_enable
              (0<<19) | // i_pred_enable
              (0<<20) | // ie_sub_enable
              (0<<18) | // iq_enable
              (0<<17) | // idct_enable
              (0<<14) | // mb_pause_enable
              (1<<13) | // q_enable
              (1<<12) | // dct_enable
              (0<<10) | // mb_info_en
              (0<<3) | // endian
              (0<<1) | // mb_read_en
              (0<<0));   // soft reset
    //--------------------------------------------------------------------------
    // Assember JPEG file header
    //--------------------------------------------------------------------------
    //need check
    header_bytes = READ_HREG(HCODEC_HENC_SCRATCH_1);
    prepare_jpeg_header();
}

static int jpeg_quality_scaling (int quality)
{
    if (quality <= 0) quality = 1;
    if (quality > 100) quality = 100;

    if (quality < 50)
        quality = 5000 / quality;
    else
        quality = 200 - quality*2;

    return quality;
}

static void _convert_quant_table(unsigned short* qtable, unsigned short *basic_table, int scale_factor, bool force_baseline)
{
    int i = 0;
    int temp;
    for (i = 0; i < DCTSIZE2; i++) {
        temp = ((int) basic_table[i] * scale_factor + 50) / 100;
        /* limit the values to the valid range */
        if (temp <= 0) temp = 1;
        if (temp > 32767) temp = 32767; /* max quantizer needed for 12 bits */
        if (force_baseline && temp > 255)
            temp = 255;		/* limit to baseline range if requested */
        qtable[i] = (unsigned short) temp;
    }
}

static void convert_quant_table(unsigned short* qtable, unsigned short *basic_table, int scale_factor)
{
    _convert_quant_table(qtable, basic_table, scale_factor, true);
}

static void write_jpeg_quant_lut(int table_num)
{
    int i;
    unsigned long data32;
    
    for (i = 0; i < DCTSIZE2; i += 2) {
        data32  = reciprocal(gQuantTable[table_num][i]);
        data32 |= reciprocal(gQuantTable[table_num][i+1])    << 16;
        WRITE_HREG(HCODEC_QDCT_JPEG_QUANT_DATA,data32);
    }
}   /* write_jpeg_quant_lut */


static void write_jpeg_huffman_lut_dc(int table_num)
{
    unsigned int    code_len, code_word, pos, addr;
    unsigned int    num_code_len;
    unsigned int    lut[12];
    unsigned int    i, j;
    
    code_len    = 1;
    code_word   = 1;
    pos         = 16;
    
    // Construct DC Huffman table
    for (i = 0; i < 16; i ++) {
        num_code_len    = jpeg_huffman_dc[table_num][i];
        for (j = 0; j < num_code_len; j ++) {
            code_word   = (code_word + 1) & ((1<<code_len)-1);
            if (code_len < i + 1) {
                code_word   <<= (i+1-code_len);
                code_len    = i + 1;
            }
            addr = jpeg_huffman_dc[table_num][pos];
            lut[addr] = ((code_len-1)<<16) | (code_word);
            pos++;
        }
    }

    // Write DC Huffman table to HW
    for (i = 0; i < 12; i ++) {
        WRITE_HREG(HCODEC_VLC_HUFFMAN_DATA,lut[i]);
    }
}   /* write_jpeg_huffman_lut_dc */

static void write_jpeg_huffman_lut_ac(int table_num)
{
    unsigned int code_len, code_word, pos;
    unsigned int num_code_len;
    unsigned int run, size;
    unsigned int data, addr = 0;
    unsigned int lut[162];
    unsigned int i, j;
    code_len = 1;
    code_word = 1;
    pos = 16;
    
    // Construct AC Huffman table
    for (i = 0; i < 16; i ++) {
        num_code_len = jpeg_huffman_ac[table_num][i];
        for (j = 0; j < num_code_len; j ++) {
            code_word = (code_word + 1) & ((1<<code_len)-1);
            if (code_len < i + 1) {
                code_word <<= (i+1-code_len);
                code_len = i + 1;
            }
            run = jpeg_huffman_ac[table_num][pos] >> 4;
            size = jpeg_huffman_ac[table_num][pos] & 0xf;
            data = ((code_len-1)<<16) | (code_word);
            if (size == 0) {
                if (run == 0) {
                    addr = 0;        // EOB
                } else if (run == 0xf) {
                    addr = 161;      // ZRL
                } else {                	
                    debug_level(0,"[TEST.C] Error: Illegal AC Huffman table format!\n");
                }
            } else if (size <= 0xa) {
                addr = 1 + 16*(size-1) + run;
            } else {
                debug_level(0,"[TEST.C] Error: Illegal AC Huffman table format!\n");
            }
            lut[addr] = data;
            pos++;
        }
    }

    // Write AC Huffman table to HW
    for (i = 0; i < 162; i ++) {
        WRITE_HREG(HCODEC_VLC_HUFFMAN_DATA ,lut[i]);
    }
}   /* write_jpeg_huffman_lut_ac */

static int  zigzag(int i)
{
    int zigzag_i;
    switch (i) {
        case 0   : zigzag_i = 0;    break;
        case 1   : zigzag_i = 1;    break;
        case 2   : zigzag_i = 8;    break;
        case 3   : zigzag_i = 16;   break;
        case 4   : zigzag_i = 9;    break;
        case 5   : zigzag_i = 2;    break;
        case 6   : zigzag_i = 3;    break;
        case 7   : zigzag_i = 10;   break;
        case 8   : zigzag_i = 17;   break;
        case 9   : zigzag_i = 24;   break;
        case 10  : zigzag_i = 32;   break;
        case 11  : zigzag_i = 25;   break;
        case 12  : zigzag_i = 18;   break;
        case 13  : zigzag_i = 11;   break;
        case 14  : zigzag_i = 4;    break;
        case 15  : zigzag_i = 5;    break;
        case 16  : zigzag_i = 12;   break;
        case 17  : zigzag_i = 19;   break;
        case 18  : zigzag_i = 26;   break;
        case 19  : zigzag_i = 33;   break;
        case 20  : zigzag_i = 40;   break;
        case 21  : zigzag_i = 48;   break;
        case 22  : zigzag_i = 41;   break;
        case 23  : zigzag_i = 34;   break;
        case 24  : zigzag_i = 27;   break;
        case 25  : zigzag_i = 20;   break;
        case 26  : zigzag_i = 13;   break;
        case 27  : zigzag_i = 6;    break;
        case 28  : zigzag_i = 7;    break;
        case 29  : zigzag_i = 14;   break;
        case 30  : zigzag_i = 21;   break;
        case 31  : zigzag_i = 28;   break;
        case 32  : zigzag_i = 35;   break;
        case 33  : zigzag_i = 42;   break;
        case 34  : zigzag_i = 49;   break;
        case 35  : zigzag_i = 56;   break;
        case 36  : zigzag_i = 57;   break;
        case 37  : zigzag_i = 50;   break;
        case 38  : zigzag_i = 43;   break;
        case 39  : zigzag_i = 36;   break;
        case 40  : zigzag_i = 29;   break;
        case 41  : zigzag_i = 22;   break;
        case 42  : zigzag_i = 15;   break;
        case 43  : zigzag_i = 23;   break;
        case 44  : zigzag_i = 30;   break;
        case 45  : zigzag_i = 37;   break;
        case 46  : zigzag_i = 44;   break;
        case 47  : zigzag_i = 51;   break;
        case 48  : zigzag_i = 58;   break;
        case 49  : zigzag_i = 59;   break;
        case 50  : zigzag_i = 52;   break;
        case 51  : zigzag_i = 45;   break;
        case 52  : zigzag_i = 38;   break;
        case 53  : zigzag_i = 31;   break;
        case 54  : zigzag_i = 39;   break;
        case 55  : zigzag_i = 46;   break;
        case 56  : zigzag_i = 53;   break;
        case 57  : zigzag_i = 60;   break;
        case 58  : zigzag_i = 61;   break;
        case 59  : zigzag_i = 54;   break;
        case 60  : zigzag_i = 47;   break;
        case 61  : zigzag_i = 55;   break;
        case 62  : zigzag_i = 62;   break;
        default  : zigzag_i = 63;   break;
    }
    return zigzag_i;
}   /* zigzag */
       
static void jpegenc_init_output_buffer(void)
{
    unsigned mem_offset;
    mem_offset = READ_HREG(HCODEC_HENC_SCRATCH_1);
	
    WRITE_HREG(VLC_VB_MEM_CTL ,((1<<31)|(0x3f<<24)|(0x20<<16)|(2<<0)) );
    WRITE_HREG(VLC_VB_START_PTR, BitstreamStart+mem_offset);
    WRITE_HREG(VLC_VB_WR_PTR, BitstreamStart+mem_offset);
    WRITE_HREG(VLC_VB_SW_RD_PTR, BitstreamStart+mem_offset);	
    WRITE_HREG(VLC_VB_END_PTR, BitstreamEnd);
    WRITE_HREG(VLC_VB_CONTROL, 1);
    WRITE_HREG(VLC_VB_CONTROL, ((0<<14)|(7<<3)|(1<<1)|(0<<0)));
}
/****************************************/

static void jpegenc_canvas_init(void)
{
    /*input dct buffer config */ 
    dct_buff_start_addr = gJpegEncBuff.buf_start+gJpegEncBuff.bufspec->input.buf_start;
    dct_buff_end_addr = dct_buff_start_addr + gJpegEncBuff.bufspec->input.buf_size -1 ;
    debug_level(0,"dct_buff_start_addr is %x \n",dct_buff_start_addr);

    /*output stream buffer config*/
    BitstreamStart  = gJpegEncBuff.buf_start+gJpegEncBuff.bufspec->bitstream.buf_start;
    BitstreamEnd  =  BitstreamStart + gJpegEncBuff.bufspec->bitstream.buf_size-1;
    debug_level(0,"BitstreamStart is %x \n",BitstreamStart);   
    
    BitstreamStartVirtAddr  = ioremap_nocache(BitstreamStart,  gJpegEncBuff.bufspec->bitstream.buf_size); 
    debug_level(0,"BitstreamStartVirtAddr is 0x%x \n",(unsigned)BitstreamStartVirtAddr);  

}

static void mfdin_basic_jpeg (unsigned input, unsigned char iformat, unsigned char oformat, unsigned picsize_x, unsigned picsize_y, unsigned char r2y_en)
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
    //r2y_mode = (r2y_en == 1)?1:0; // Fixed to 1 (TODO)
    r2y_mode =1;
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


    WRITE_HREG(HCODEC_MFDIN_REG1_CTRL , (iformat << 0) |
                                         (oformat << 4) |
                                         (dsample_en <<6) |
                                         (y_size <<8) |
                                         (interp_en <<9) |
                                         (r2y_en <<12) |
                                         (r2y_mode <<13) |
                                         (2 <<29) | // 0:H264_I_PIC_ALL_4x4, 1:H264_P_PIC_Y_16x16_C_8x8, 2:JPEG_ALL_8x8, 3:Reserved
                                         (0 <<31));  // 0:YC interleaved 1:YC non-interleaved(for JPEG)

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

static int  set_jpeg_input_format (jpegenc_mem_type type, jpegenc_frame_fmt input_fmt,jpegenc_frame_fmt output_fmt , unsigned input, unsigned offset, unsigned size, unsigned char need_flush)
{
    int ret = 0;
    unsigned char iformat = MAX_FRAME_FMT, oformat = MAX_FRAME_FMT, r2y_en = 0;
    unsigned picsize_x, picsize_y;
    unsigned canvas_w = 0;
    debug_level(0,"************begin set input format**************\n");
    debug_level(0,"type is %d\n",type);
    debug_level(0,"fmt is %d\n",input_fmt);
    debug_level(0,"input is %d\n",input);
    debug_level(0,"offset is %d\n",offset);
    debug_level(0,"size is %d\n",size);
    debug_level(0,"need_flush is %d\n",need_flush);
    debug_level(0,"************end set input format**************\n");

    if((input_fmt == FMT_RGB565)||(input_fmt>=MAX_FRAME_FMT))
        return -1;

    input_format = input_fmt;
    output_format = output_fmt;
    picsize_x = ((encoder_width+15)>>4)<<4;
    picsize_y = ((encoder_height+15)>>4)<<4;
    if(output_fmt == FMT_YUV422_SINGLE){
        oformat = 1;
    }else{
        oformat = 0;	
    }
    if((type == LOCAL_BUFF)||(type == PHYSICAL_BUFF)){
        if(type == LOCAL_BUFF){
            if(need_flush)
                dma_flush(dct_buff_start_addr + offset, size);
            input = dct_buff_start_addr + offset;
        }
        if(input_fmt <= FMT_YUV444_PLANE)
            r2y_en = 0;
        else
            r2y_en = 1;

        if(input_fmt == FMT_YUV422_SINGLE){
            iformat = 0;
            canvas_w =  picsize_x*2;
            canvas_w =  ((canvas_w+31)>>5)<<5;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input, 
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ENC_CANVAS_OFFSET+6;            
        }else if((input_fmt == FMT_YUV444_SINGLE)||(input_fmt == FMT_RGB888)){
            iformat = 1;
            if(input_fmt == FMT_RGB888)
                r2y_en = 1;
            canvas_w =  picsize_x*3;
            canvas_w =  ((canvas_w+31)>>5)<<5;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ENC_CANVAS_OFFSET+6;
        }else if((input_fmt == FMT_NV21)||(input_fmt == FMT_NV12)){
            canvas_w =  ((encoder_width+31)>>5)<<5;
            iformat = (input_fmt == FMT_NV21)?2:3;
            canvas_config(ENC_CANVAS_OFFSET+6,
                input,
                canvas_w, picsize_y,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            canvas_config(ENC_CANVAS_OFFSET+7,
                input + canvas_w*picsize_y,
                canvas_w , picsize_y/2,
                CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
            input = ((ENC_CANVAS_OFFSET+7)<<8)|(ENC_CANVAS_OFFSET+6);
        }else if(input_fmt == FMT_YUV420){
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
        }else if((input_fmt == FMT_YUV444_PLANE)||(input_fmt == FMT_RGB888_PLANE)){
            iformat = 5;
            if(input_fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
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
        }else if(input_fmt == FMT_RGBA8888){
            iformat = 12;
        }
        ret = 0;
    }else if(type == CANVAS_BUFF){
        r2y_en = 0;
        if(input_fmt == FMT_YUV422_SINGLE){
            iformat = 0;
            input = input&0xff;
        }else if(input_fmt == FMT_YUV444_SINGLE){
            iformat = 1;
            input = input&0xff;
        }else if((input_fmt == FMT_NV21)||(input_fmt == FMT_NV12)){
            iformat = (input_fmt == FMT_NV21)?2:3;
            input = input&0xffff;
        }else if(input_fmt == FMT_YUV420){
            iformat = 4;
            input = input&0xffffff;
        }else if((input_fmt == FMT_YUV444_PLANE)||(input_fmt == FMT_RGB888_PLANE)){
            if(input_fmt == FMT_RGB888_PLANE)
                r2y_en = 1;
            iformat = 5;
            input = input&0xffffff;
        }else{
            ret = -1;
        }
    }
    if(ret == 0)
        mfdin_basic_jpeg(input,iformat,oformat,picsize_x,picsize_y,r2y_en);
    return ret;
}

static void jpegenc_isr_tasklet(ulong data)
{
    debug_level(0,"encoder  is finished %d\n",encoder_status);
    if((encoder_status == ENCODER_DONE)&&(process_irq)){
        atomic_inc(&jpegenc_ready);
        wake_up_interruptible(&jpegenc_wait);
    }
}

static irqreturn_t jpegenc_isr(int irq, void *dev_id)
{
    WRITE_HREG(HCODEC_ASSIST_MBOX2_CLR_REG, 1);
    encoder_status  = READ_HREG(ENCODER_STATUS);
    if(encoder_status == ENCODER_DONE){
        process_irq = 1;
        tasklet_schedule(&jpegenc_tasklet);
    }
    return IRQ_HANDLED;
}

#if 0
static void jpegenc_reset(void)
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
#endif

static void jpegenc_start(void)
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

static void _jpegenc_stop(void)
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

s32 jpegenc_loadmc(const u32 *p)
{
    ulong timeout;
    s32 ret = 0;
    mc_addr = kmalloc(MC_SIZE, GFP_KERNEL);

    if (!mc_addr) {
        return -ENOMEM;
    }

    memcpy(mc_addr, p, MC_SIZE);

    mc_addr_map = dma_map_single(NULL, mc_addr, MC_SIZE, DMA_TO_DEVICE);

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

    dma_unmap_single(NULL, mc_addr_map, MC_SIZE, DMA_TO_DEVICE);
    kfree(mc_addr);
    mc_addr = NULL;
    return ret;
}



#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV
#define  DMC_SEC_PORT8_RANGE0  0x840
#define  DMC_SEC_CTRL  0x829
#endif

static void enable_hcoder_ddr_access(void)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6TV
    WRITE_SEC_REG(DMC_SEC_PORT8_RANGE0 , 0xffff);
    WRITE_SEC_REG(DMC_SEC_CTRL , 0x80000000);
#endif
}

bool jpegenc_on(void)
{
    bool hcodec_on;
    unsigned long flags;

    spin_lock_irqsave(&lock, flags);

    hcodec_on = vdec_on(VDEC_HCODEC);
    hcodec_on |=(encode_opened>0);

    spin_unlock_irqrestore(&lock, flags);
    return hcodec_on;
}

static s32 jpegenc_poweron(void)
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
    jpegenc_clock_enable();
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    //Powerup HCODEC memories
    WRITE_VREG(DOS_MEM_PD_HCODEC, 0x0);

    // Remove HCODEC ISO
    data32 = READ_AOREG(AO_RTI_GEN_PWR_ISO0); 
    data32 = data32 & (~(0x30));
    WRITE_AOREG(AO_RTI_GEN_PWR_ISO0, data32);
    udelay(10);

    // Disable auto-clock gate
    data32 = READ_VREG(DOS_GEN_CTRL0);
    data32 = data32 | 0x1;
    WRITE_VREG(DOS_GEN_CTRL0, data32);
    data32 = READ_VREG(DOS_GEN_CTRL0);
    data32 = data32 & 0xFFFFFFFE;
    WRITE_VREG(DOS_GEN_CTRL0, data32);

    spin_unlock_irqrestore(&lock, flags);
#endif

    mdelay(10);
    return 0;
}

static s32 jpegenc_poweroff(void)
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
    jpegenc_clock_disable();
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    // HCODEC power off
    WRITE_AOREG(AO_RTI_GEN_PWR_SLEEP0, READ_AOREG(AO_RTI_GEN_PWR_SLEEP0) | 0x3);
#endif

    spin_unlock_irqrestore(&lock, flags);

    // release DOS clk81 clock gating
    //CLK_GATE_OFF(DOS);
    switch_mod_gate_by_name("vdec", 0);
    return 0;
}

static s32 jpegenc_init(void)
{
    int r;   
    jpegenc_poweron();
    jpegenc_canvas_init();

    if(IS_MESON_M8M2_CPU)
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x32);
    else
        WRITE_HREG(HCODEC_ASSIST_MMC_CTRL1,0x2);
    debug_level(1,"start to load microcode\n");
    if (jpegenc_loadmc(jpeg_encoder_mc) < 0) {
        //amvdec_disable();
        return -EBUSY;
    }	
    debug_level(1,"succeed to load microcode\n");
    process_irq = 0;

    r = request_irq(INT_JPEG_ENCODER, jpegenc_isr, IRQF_SHARED, "jpegenc-irq", (void *)jpeg_enc_id);//INT_MAILBOX_1A
    WRITE_HREG(ENCODER_STATUS , ENCODER_IDLE);
    encode_inited = 1;
    return 0;
}

static void jpegenc_start_cmd(unsigned* input_info)
{
    set_jpeg_input_format((jpegenc_mem_type)input_info[0], (jpegenc_frame_fmt)input_info[1], (jpegenc_frame_fmt)input_info[2], input_info[3], input_info[4],input_info[5],(unsigned char)input_info[6]);
	
    //prepare_jpeg_header();
    init_jpeg_encoder();
    jpegenc_init_output_buffer();
    /* clear mailbox interrupt */
    WRITE_HREG(HCODEC_ASSIST_MBOX2_CLR_REG, 1);

    /* enable mailbox interrupt */
    WRITE_HREG(HCODEC_ASSIST_MBOX2_MASK, 1);
    encoder_status = ENCODER_IDLE;
    WRITE_HREG(ENCODER_STATUS , ENCODER_IDLE);
    process_irq = 0;
    jpegenc_start();
    debug_level(0,"jpegenc_start\n");
}

static void jpegenc_stop(void)
{
#if 0	
    //register dump 
    int i;
    /*MFDIN*/
    for( i = 0x1010 ;i < 0x101b ; i++){
        debug_level(0,"MFDIN register 0x%x :  0x%x \n",i, READ_HREG(i)); 	
    }	
    /*VLC*/
    for( i = 0x1d00 ;i < 0x1d5d ; i++){
        debug_level(0,"VLC register 0x%x :  0x%x \n",i, READ_HREG(i)); 	
    }		
    /*QDCT*/
    for( i = 0x1f00 ;i < 0x1f5f ; i++){
        debug_level(0,"QDCT register 0x%x :  0x%x \n",i, READ_HREG(i)); 	
    }

    for( i = 0x1001 ;i < 0x1f5f ; i++){
        debug_level(0,"register 0x%x :  0x%x \n",i, READ_HREG(i)); 	
    }	
#endif 		
    //WRITE_HREG(HCODEC_MPSR, 0);	
    _jpegenc_stop();
    jpegenc_poweroff();
    debug_level(1,"jpegenc_stop\n");
}

static int jpegenc_open(struct inode *inode, struct file *file)
{
    int r = 0;
    debug_level(1,"jpegenc open\n");
#ifdef CONFIG_AM_ENCODER
    if(amvenc_avc_on() == true){
        debug_level(1,"hcodec in use for AVC Encode now.\n");
        return -EBUSY;
    }
#endif
    if(encode_opened>0){
        debug_level(1, "jpegenc open busy.\n");
        return -EBUSY;
    }
    init_waitqueue_head(&jpegenc_wait);
    atomic_set(&jpegenc_ready, 0);
    tasklet_init(&jpegenc_tasklet, jpegenc_isr_tasklet, 0);
    encode_opened++;
    BitstreamStartVirtAddr = NULL;
    memset(gQuantTable, 0 ,sizeof(gQuantTable));
    gQuantTable_id = 0;
    gExternalQuantTablePtr = NULL;
    external_quant_table_available = false;
    return r;
}

static int jpegenc_release(struct inode *inode, struct file *file)
{
    if(encode_inited){
        free_irq(INT_JPEG_ENCODER, (void *)jpeg_enc_id);
        //amvdec_disable();
        jpegenc_stop();
        encode_inited = 0;
    }
    memset(gQuantTable, 0 ,sizeof(gQuantTable));
    gQuantTable_id = 0;
    if(gExternalQuantTablePtr)
        kfree(gExternalQuantTablePtr);
    gExternalQuantTablePtr = NULL;
    external_quant_table_available = false;
    if(BitstreamStartVirtAddr){
        iounmap(BitstreamStartVirtAddr);
        BitstreamStartVirtAddr = NULL;
    }
    if(encode_opened>0)
        encode_opened--;
    debug_level(1,"jpegenc release\n");
    return 0;
}

static void dma_flush(unsigned buf_start , unsigned buf_size )
{
    //dma_sync_single_for_cpu(jpegenc_dev,buf_start, buf_size, DMA_TO_DEVICE);
    dma_sync_single_for_device(jpegenc_dev,buf_start ,buf_size, DMA_TO_DEVICE);
}

static void cache_flush(unsigned buf_start , unsigned buf_size )
{
    dma_sync_single_for_cpu(jpegenc_dev , buf_start, buf_size, DMA_FROM_DEVICE);
    //dma_sync_single_for_device(jpegenc_dev ,buf_start , buf_size, DMA_FROM_DEVICE);
}

static long jpegenc_ioctl(struct file *file,
                           unsigned int cmd, ulong arg)
{
    int r = 0;
    unsigned* addr_info;
    unsigned buf_start;
    int quality = 0;
    switch (cmd) {
        case JPEGENC_IOC_GET_ADDR:
            *((unsigned*)arg)  = 1;
            break;
        case JPEGENC_IOC_NEW_CMD:
            addr_info = (unsigned*)arg;
            jpegenc_start_cmd(&addr_info[0]);
            break;
        case JPEGENC_IOC_GET_STAGE:
            *((unsigned*)arg)  = encoder_status;
            break; 
        case JPEGENC_IOC_GET_OUTPUT_SIZE:	
            *((unsigned*)arg) = READ_HREG(VLC_TOTAL_BYTES);
            break;
        case JPEGENC_IOC_SET_ENCODER_WIDTH:
            if(*((unsigned*)arg)>gJpegEncBuff.bufspec->max_width)
                *((unsigned*)arg) = gJpegEncBuff.bufspec->max_width;
            else
                encoder_width = *((unsigned*)arg) ;
            break;
        case JPEGENC_IOC_SET_ENCODER_HEIGHT:
            if(*((unsigned*)arg)>gJpegEncBuff.bufspec->max_height)
                *((unsigned*)arg) = gJpegEncBuff.bufspec->max_height;
            else
                encoder_height = *((unsigned*)arg) ;
            break;	
        case JPEGENC_IOC_INPUT_FORMAT	:
            break;	
        case JPEGENC_IOC_CONFIG_INIT:
            jpegenc_init();
            break;		
        case JPEGENC_IOC_FLUSH_CACHE:
            addr_info  = (unsigned*)arg ;
            switch(addr_info[0]){
                case 0:
                    buf_start = dct_buff_start_addr;
                    break;
                case 1:
                    buf_start = BitstreamStart ;
                    break;
                default:
                    buf_start = dct_buff_start_addr;
                    break;
            }
            dma_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
            break;
        case JPEGENC_IOC_FLUSH_DMA:
            addr_info  = (unsigned*)arg ;
            switch(addr_info[0]){
                case 0:
                    buf_start = dct_buff_start_addr;
                    break;
                case 1:
                    buf_start = BitstreamStart ;
                    break;
                default:
                    buf_start = dct_buff_start_addr;
                    break;
            }	    
            cache_flush(buf_start + addr_info[1] ,addr_info[2] - addr_info[1]);
            break;
        case JPEGENC_IOC_GET_BUFFINFO:
            addr_info  = (unsigned*)arg;
            addr_info[0] = gJpegEncBuff.buf_size;
            addr_info[1] = gJpegEncBuff.bufspec->input.buf_start;
            addr_info[2] = gJpegEncBuff.bufspec->input.buf_size;
            addr_info[3] = gJpegEncBuff.bufspec->bitstream.buf_start;
            addr_info[4] = gJpegEncBuff.bufspec->bitstream.buf_size;
            break;
        case JPEGENC_IOC_GET_DEVINFO:
            strncpy((char *)arg,JPEGENC_DEV_VERSION,strlen(JPEGENC_DEV_VERSION));
            break;
        case JPEGENC_IOC_SET_QUALTIY:
            quality = *((int *)arg) ;
            jpeg_quality = jpeg_quality_scaling(quality);
            debug_level(0,"target quality : %d,  jpeg_quality value: %d. \n", quality,jpeg_quality);
            break;
        case JPEGENC_IOC_SEL_QUANT_TABLE:
            quality = *((int *)arg);
            if(quality<4){
                gQuantTable_id = quality;
                debug_level(0,"JPEGENC_IOC_SEL_QUANT_TABLE : %d. \n", quality);
            }else{
                gQuantTable_id = 0;
                debug_level(1,"JPEGENC_IOC_SEL_QUANT_TABLE invaild. target value: %d. \n", quality);
            }
            break;
        case JPEGENC_IOC_SET_EXT_QUANT_TABLE:
            if(arg == 0){
                if(gExternalQuantTablePtr)
                    kfree(gExternalQuantTablePtr);
                gExternalQuantTablePtr = NULL;
                external_quant_table_available  = false;
            }else{
                void  __user* argp =(void __user*)arg;
                gExternalQuantTablePtr = kmalloc(sizeof(unsigned short)*DCTSIZE2*2, GFP_KERNEL);
                if(gExternalQuantTablePtr){
                    if(copy_from_user(gExternalQuantTablePtr,argp,sizeof(unsigned short)*DCTSIZE2*2)){
                        r=-1;
                        break;
                    }
                    external_quant_table_available = true;
                    r = 0;
                }else{
                    debug_level(1,"gExternalQuantTablePtr malloc fail \n");
                    r= -1;
                }
            }
            break;
        default:
            r= -1;
            break;
    }
    return r;
}

static int jpegenc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vma_size = vma->vm_end - vma->vm_start;

    if (vma_size == 0) {
        debug_level(1,"vma_size is 0 \n");
        return -EAGAIN;
    }
    off += gJpegEncBuff.buf_start;
    debug_level(0,"vma_size is %d , off is %ld \n" , vma_size ,off);
    vma->vm_flags |= VM_RESERVED | VM_IO;
    //vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                        vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        debug_level(1,"set_cached: failed remap_pfn_range\n");
        return -EAGAIN;
    }
    return 0;

}

static unsigned int jpegenc_poll(struct file *file, poll_table *wait_table)
{
    poll_wait(file, &jpegenc_wait, wait_table);

    if (atomic_read(&jpegenc_ready)) {
        atomic_dec(&jpegenc_ready);
        return POLLIN | POLLRDNORM;
    }

    return 0;
}

const static struct file_operations jpegenc_fops = {
    .owner    = THIS_MODULE,
    .open     = jpegenc_open,
    .mmap     = jpegenc_mmap,
    .release  = jpegenc_release,
    .unlocked_ioctl    = jpegenc_ioctl,
    .poll     = jpegenc_poll,
};

int  init_jpegenc_device(void)
{
    int  r =0;
    r =register_chrdev(0,DEVICE_NAME,&jpegenc_fops);
    if(r<=0) 
    {
        debug_level(2,"register jpegenc device error\r\n");
        return  r  ;
    }
    jpegenc_device_major= r ;
    
    jpegenc_class = class_create(THIS_MODULE, DEVICE_NAME);

    jpegenc_dev = device_create(jpegenc_class, NULL,
                                  MKDEV(jpegenc_device_major, 0), NULL,
                                  DEVICE_NAME);
    return r;
}
int uninit_jpegenc_device(void)
{
    device_destroy(jpegenc_class, MKDEV(jpegenc_device_major, 0));

    class_destroy(jpegenc_class);

    unregister_chrdev(jpegenc_device_major, DEVICE_NAME);	
    return 0;
}

static struct resource memobj;
static int jpegenc_probe(struct platform_device *pdev)
{
    struct resource *mem;
    int idx;

    debug_level(1, "jpegenc probe start.\n");

#if 0
    if (!(mem = platform_get_resource(pdev, IORESOURCE_MEM, 0))) {
        debug_level(1, "jpegenc memory resource undefined.\n");
        return -EFAULT;
    }
#else
    mem = &memobj;
    idx = find_reserve_block(pdev->dev.of_node->name,0);
    if(idx < 0){
        debug_level(1, "jpegenc memory resource undefined.\n");
        return -EFAULT;
    }
    mem->start = (phys_addr_t)get_reserve_block_addr(idx);
    mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(idx)-1;
#endif
    gJpegEncBuff.buf_start = mem->start;
    gJpegEncBuff.buf_size = mem->end - mem->start + 1;
    if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_HD].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_HD;
        gJpegEncBuff.bufspec = (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_HD];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_13M].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_13M;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_13M];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_8M].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_8M;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_8M];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_5M].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_5M;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_5M];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_3M].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_3M;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_3M];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_2M].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_2M;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_2M];
    }else if(gJpegEncBuff.buf_size>=jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_VGA].min_buffsize){
        gJpegEncBuff.cur_buf_lev = JPEGENC_BUFFER_LEVEL_VGA;
        gJpegEncBuff.bufspec= (BuffInfo_t*)&jpegenc_buffspec[JPEGENC_BUFFER_LEVEL_VGA];
    }else{
        gJpegEncBuff.buf_start = 0;
        gJpegEncBuff.buf_size = 0;
        debug_level(1, "jpegenc memory resource too small, size is %d.\n",gJpegEncBuff.buf_size);
        return -EFAULT;
    }
    debug_level(1,"jpegenc  memory config sucess, buff start: 0x%x, buff size is 0x%x, select level: %s \n",gJpegEncBuff.buf_start,gJpegEncBuff.buf_size, glevel_str[gJpegEncBuff.cur_buf_lev]);
    init_jpegenc_device();
    debug_level(1, "jpegenc probe end.\n");
    return 0;
}

static int jpegenc_remove(struct platform_device *pdev)
{
    uninit_jpegenc_device();
    debug_level(1, "jpegenc remove.\n");
    return 0;
}

/****************************************/

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_jpegenc_dt_match[]={
	{	.compatible = "amlogic,jpegenc",
	},
	{},
};
#else
#define amlogic_jpegenc_dt_match NULL
#endif

static struct platform_driver jpegenc_driver = {
    .probe      = jpegenc_probe,
    .remove     = jpegenc_remove,
    .driver     = {
        .name   = DRIVER_NAME,
        .of_match_table = amlogic_jpegenc_dt_match,
    }
};
static struct codec_profile_t jpegenc_profile = {
	.name = "jpegenc",
	.profile = ""
};
static int __init jpegenc_driver_init_module(void)
{
    debug_level(1, "jpegenc module init\n");

    if (platform_driver_register(&jpegenc_driver)) {
        debug_level(1, "failed to register jpegenc driver\n");
        return -ENODEV;
    }
    vcodec_profile_register(&jpegenc_profile);
    return 0;
}

static void __exit jpegenc_driver_remove_module(void)
{
    debug_level(1, "jpegenc module remove.\n");
	
    platform_driver_unregister(&jpegenc_driver);
}

/****************************************/

module_param(stat, uint, 0664);
MODULE_PARM_DESC(stat, "\n jpegenc stat \n");

module_init(jpegenc_driver_init_module);
module_exit(jpegenc_driver_remove_module);

MODULE_DESCRIPTION("AMLOGIC JPEG Encoder Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("simon.zheng <simon.zheng@amlogic.com>");
