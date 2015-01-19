#ifndef __JPEG_ENC_H_
#define __JPEG_ENC_H_

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define JPEGENC_DEV_VERSION "AML-M8"
#define INT_JPEG_ENCODER INT_DOS_MAILBOX_2
#else
#define JPEGENC_DEV_VERSION "AML-MT"
#define INT_JPEG_ENCODER INT_MAILBOX_1A
#endif

#define VDEC_166M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (5 << 9) | (1 << 8) | (5))
#define VDEC_200M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (5 << 9) | (1 << 8) | (4))
#define VDEC_250M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (5 << 9) | (1 << 8) | (3))
#define VDEC_333M()  WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (5 << 9) | (1 << 8) | (2))

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define HDEC_255M()   WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (2 << 25) | (1 << 16) |(1 << 24) | (0xffff&READ_CBUS_REG(HHI_VDEC_CLK_CNTL)))
#define HDEC_319M()   WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 25) | (1 << 16) |(1 << 24) | (0xffff&READ_CBUS_REG(HHI_VDEC_CLK_CNTL)))
#define jpegenc_clock_enable() \
    HDEC_319M(); \
    WRITE_VREG_BITS(DOS_GCLK_EN0, 0x7fff, 12, 15)

#define jpegenc_clock_disable() \
    WRITE_VREG_BITS(DOS_GCLK_EN0, 0, 12, 15); \
    WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  0, 24, 1);
#else
#define HDEC_250M()   WRITE_MPEG_REG(HHI_VDEC_CLK_CNTL, (0 << 25) | (3 << 16) |(1 << 24) | (0xffff&READ_CBUS_REG(HHI_VDEC_CLK_CNTL)))
#define jpegenc_clock_enable() \
    HDEC_250M(); \
    WRITE_VREG(DOS_GCLK_EN0, 0xffffffff)

#define jpegenc_clock_disable() \
    WRITE_MPEG_REG_BITS(HHI_VDEC_CLK_CNTL,  0, 24, 1);
#endif

#define JPEGENC_IOC_MAGIC  'E'

#define JPEGENC_IOC_GET_DEVINFO 				_IOW(JPEGENC_IOC_MAGIC, 0xf0, unsigned int)

#define JPEGENC_IOC_GET_ADDR			 		_IOW(JPEGENC_IOC_MAGIC, 0x00, unsigned int)
#define JPEGENC_IOC_INPUT_UPDATE				_IOW(JPEGENC_IOC_MAGIC, 0x01, unsigned int)
#define JPEGENC_IOC_GET_STATUS					_IOW(JPEGENC_IOC_MAGIC, 0x02, unsigned int)
#define JPEGENC_IOC_NEW_CMD						_IOW(JPEGENC_IOC_MAGIC, 0x03, unsigned int)
#define JPEGENC_IOC_GET_STAGE					_IOW(JPEGENC_IOC_MAGIC, 0x04, unsigned int)
#define JPEGENC_IOC_GET_OUTPUT_SIZE				_IOW(JPEGENC_IOC_MAGIC, 0x05, unsigned int)
#define JPEGENC_IOC_SET_QUALTIY 					_IOW(JPEGENC_IOC_MAGIC, 0x06, unsigned int)
#define JPEGENC_IOC_SET_ENCODER_WIDTH 			_IOW(JPEGENC_IOC_MAGIC, 0x07, unsigned int)
#define JPEGENC_IOC_SET_ENCODER_HEIGHT 			_IOW(JPEGENC_IOC_MAGIC, 0x08, unsigned int)
#define JPEGENC_IOC_CONFIG_INIT 				_IOW(JPEGENC_IOC_MAGIC, 0x09, unsigned int)
#define JPEGENC_IOC_FLUSH_CACHE 				_IOW(JPEGENC_IOC_MAGIC, 0x0a, unsigned int)
#define JPEGENC_IOC_FLUSH_DMA 					_IOW(JPEGENC_IOC_MAGIC, 0x0b, unsigned int)
#define JPEGENC_IOC_GET_BUFFINFO 				_IOW(JPEGENC_IOC_MAGIC, 0x0c, unsigned int)
#define JPEGENC_IOC_INPUT_FORMAT 				_IOW(JPEGENC_IOC_MAGIC, 0x0d, unsigned int)
#define JPEGENC_IOC_SEL_QUANT_TABLE 				_IOW(JPEGENC_IOC_MAGIC, 0x0e, unsigned int)
#define JPEGENC_IOC_SET_EXT_QUANT_TABLE 				_IOW(JPEGENC_IOC_MAGIC, 0x0f, unsigned int)


#define JPEGENC_BUFFER_INPUT              0
#define JPEGENC_BUFFER_OUTPUT           1

#define DCTSIZE2	    64

typedef enum{
    LOCAL_BUFF = 0,
    CANVAS_BUFF,
    PHYSICAL_BUFF,
    MAX_BUFF_TYPE 
}jpegenc_mem_type;

typedef enum{
    FMT_YUV422_SINGLE = 0,
    FMT_YUV444_SINGLE,
    FMT_NV21,
    FMT_NV12,
    FMT_YUV420,    
    FMT_YUV444_PLANE,
    FMT_RGB888,
    FMT_RGB888_PLANE,
    FMT_RGB565,
    FMT_RGBA8888,
    MAX_FRAME_FMT 
}jpegenc_frame_fmt;

///////////////////////////////////////////////////////////////////////////
// Memory Address 
///////////////////////////////////////////////////////////////////////////

// moved to jpeg_enc.s



/********************************************
 *  Interrupt
********************************************/
#define VB_FULL_REQ            0x01
#define VLC_REQ                0x02
#define MAIN_REQ               0x04
#define QDCT_REQ               0x08

/********************************************
 *  Regsiter
********************************************/
#define COMMON_REG_0              r0
#define COMMON_REG_1              r1

#define VB_FULL_REG_0             r2

#define MAIN_REG_0                r8
#define MAIN_REG_1                r9
#define MAIN_REG_2                r10
#define MAIN_REG_3                r11
#define MAIN_REG_4                r12
#define MAIN_REG_5                r13
#define MAIN_REG_6                r14
#define MAIN_REG_7                r15

#define VLC_REG_0                 r8
#define VLC_REG_1                 r9
#define VLC_REG_2                 r10
#define VLC_REG_3                 r11
#define VLC_REG_4                 r12
#define VLC_REG_5                 r13
#define VLC_REG_6                 r14
#define VLC_REG_7                 r15
#define VLC_REG_8                 r16
#define VLC_REG_9                 r17
#define VLC_REG_10                r18
#define VLC_REG_11                r19
#define VLC_REG_12                r20

#define QDCT_REG_0                r8
#define QDCT_REG_1                r9
#define QDCT_REG_2                r10
#define QDCT_REG_3                r11
#define QDCT_REG_4                r12
#define QDCT_REG_5                r13
#define QDCT_REG_6                r14
#define QDCT_REG_7                r15

/********************************************
 *  AV Scratch Register Re-Define
********************************************/
#define ENCODER_STATUS              HENC_SCRATCH_0
#define BITSTREAM_OFFSET            HENC_SCRATCH_1

//---------------------------------------------------
// ENCODER_STATUS define
//---------------------------------------------------
#define ENCODER_IDLE                0
#define ENCODER_START               1
//#define ENCODER_SOS_HEADER          2
#define ENCODER_MCU                 3
#define ENCODER_DONE                4

/********************************************
 *  Local Memory
********************************************/
#if 0 //only defined for micro code
#define INTR_MSK_SAVE               0x000
#define pic_width                   0x001
#define pic_height                  0x002
#define pic_format                  0x003
#define lastcoeff_sel               0x004
#define mcu_x                       0x005
#define mcu_y                       0x006
#define blk_in_mcu                  0x007
#define num_mcu_x                   0x008
#define num_mcu_y                   0x009
#define num_blk_in_mcu              0x00A
#endif
/********************************************
* defines for HENC command 
********************************************/
#define HENC_SEND_RSVD_COMMAND1             1
#define HENC_SEND_RSVD_COMMAND2             2
#define HENC_SEND_RSVD_COMMAND3             3
#define HENC_SEND_RSVD_COMMAND4             4
#define HENC_SEND_RSVD_COMMAND5             5
#define HENC_SEND_COEFF_COMMAND             6
#define HENC_SEND_RSVD_COMMAND7             7
#define HENC_SEND_RSVD_COMMAND8             8
#define HENC_SEND_RSVD_COMMAND9             9
#define HENC_SEND_RSVD_COMMAND10            15



// Define Quantization table:   Max two tables
#define QUANT_SEL_COMP0     0
#define QUANT_SEL_COMP1     1
#define QUANT_SEL_COMP2     1

// Define Huffman table selection: Max two tables per DC/AC
#define DC_HUFF_SEL_COMP0   0
#define DC_HUFF_SEL_COMP1   1
#define DC_HUFF_SEL_COMP2   1
#define AC_HUFF_SEL_COMP0   0
#define AC_HUFF_SEL_COMP1   1
#define AC_HUFF_SEL_COMP2   1


// When create the test for the 1st time, define header_bytes 0, then the C code will write out a new header file;
// when the test is stable, define header_bytes to be the length of the header file already created by C code.
#define JPEG_HEADER_BYTES   0
//`define JPEG_HEADER_BYTES   592

// Simulation parameters for improving test coverage

#define JDCT_INTR_SEL       0   // DCT interrupt select:0=Disable intr;
                                //                      1=Intr at end of each 8x8 block of DCT input;
                                //                      2=Intr at end of each MCU of DCT input;
                                //                      3=Intr at end of a scan of DCT input;
                                //                      4=Intr at end of each 8x8 block of DCT output;
                                //                      5=Intr at end of each MCU of DCT output;
                                //                      6=Intr at end of a scan of DCT output;
#define JDCT_LASTCOEFF_SEL  1   // 0=Mark last coeff at the end of an 8x8 block,
                                // 1=Mark last coeff at the end of an MCU
                                // 2=Mark last coeff at the end of a scan
#define MAX_COMP_PER_SCAN   3



// Perform convertion from Q to 1/Q
unsigned long reciprocal (unsigned int q)
{
    unsigned long q_recip;
    
    switch (q) {
        case   0    : q_recip   = 0;        break;  // Invalid 1/0
        case   1    : q_recip   = 65535;    break;  // 65535 * (1/1)
        case   2    : q_recip   = 32768;    break;  // 65536 * (1/2)
        case   3    : q_recip   = 21845;    break;  // 65536 * (1/3)
        case   4    : q_recip   = 16384;    break;  // 65536 * (1/4)
        case   5    : q_recip   = 13107;    break;  // 65536 * (1/5)
        case   6    : q_recip   = 10923;    break;  // 65536 * (1/6)
        case   7    : q_recip   = 9362;     break;  // 65536 * (1/7)
        case   8    : q_recip   = 8192;     break;  // 65536 * (1/8)
        case   9    : q_recip   = 7282;     break;  // 65536 * (1/9)
        case  10    : q_recip   = 6554;     break;  // 65536 * (1/10)
        case  11    : q_recip   = 5958;     break;  // 65536 * (1/11)
        case  12    : q_recip   = 5461;     break;  // 65536 * (1/12)
        case  13    : q_recip   = 5041;     break;  // 65536 * (1/13)
        case  14    : q_recip   = 4681;     break;  // 65536 * (1/14)
        case  15    : q_recip   = 4369;     break;  // 65536 * (1/15)
        case  16    : q_recip   = 4096;     break;  // 65536 * (1/16)
        case  17    : q_recip   = 3855;     break;  // 65536 * (1/17)
        case  18    : q_recip   = 3641;     break;  // 65536 * (1/18)
        case  19    : q_recip   = 3449;     break;  // 65536 * (1/19)
        case  20    : q_recip   = 3277;     break;  // 65536 * (1/20)
        case  21    : q_recip   = 3121;     break;  // 65536 * (1/21)
        case  22    : q_recip   = 2979;     break;  // 65536 * (1/22)
        case  23    : q_recip   = 2849;     break;  // 65536 * (1/23)
        case  24    : q_recip   = 2731;     break;  // 65536 * (1/24)
        case  25    : q_recip   = 2621;     break;  // 65536 * (1/25)
        case  26    : q_recip   = 2521;     break;  // 65536 * (1/26)
        case  27    : q_recip   = 2427;     break;  // 65536 * (1/27)
        case  28    : q_recip   = 2341;     break;  // 65536 * (1/28)
        case  29    : q_recip   = 2260;     break;  // 65536 * (1/29)
        case  30    : q_recip   = 2185;     break;  // 65536 * (1/30)
        case  31    : q_recip   = 2114;     break;  // 65536 * (1/31)
        case  32    : q_recip   = 2048;     break;  // 65536 * (1/32)
        case  33    : q_recip   = 1986;     break;  // 65536 * (1/33)
        case  34    : q_recip   = 1928;     break;  // 65536 * (1/34)
        case  35    : q_recip   = 1872;     break;  // 65536 * (1/35)
        case  36    : q_recip   = 1820;     break;  // 65536 * (1/36)
        case  37    : q_recip   = 1771;     break;  // 65536 * (1/37)
        case  38    : q_recip   = 1725;     break;  // 65536 * (1/38)
        case  39    : q_recip   = 1680;     break;  // 65536 * (1/39)
        case  40    : q_recip   = 1638;     break;  // 65536 * (1/40)
        case  41    : q_recip   = 1598;     break;  // 65536 * (1/41)
        case  42    : q_recip   = 1560;     break;  // 65536 * (1/42)
        case  43    : q_recip   = 1524;     break;  // 65536 * (1/43)
        case  44    : q_recip   = 1489;     break;  // 65536 * (1/44)
        case  45    : q_recip   = 1456;     break;  // 65536 * (1/45)
        case  46    : q_recip   = 1425;     break;  // 65536 * (1/46)
        case  47    : q_recip   = 1394;     break;  // 65536 * (1/47)
        case  48    : q_recip   = 1365;     break;  // 65536 * (1/48)
        case  49    : q_recip   = 1337;     break;  // 65536 * (1/49)
        case  50    : q_recip   = 1311;     break;  // 65536 * (1/50)
        case  51    : q_recip   = 1285;     break;  // 65536 * (1/51)
        case  52    : q_recip   = 1260;     break;  // 65536 * (1/52)
        case  53    : q_recip   = 1237;     break;  // 65536 * (1/53)
        case  54    : q_recip   = 1214;     break;  // 65536 * (1/54)
        case  55    : q_recip   = 1192;     break;  // 65536 * (1/55)
        case  56    : q_recip   = 1170;     break;  // 65536 * (1/56)
        case  57    : q_recip   = 1150;     break;  // 65536 * (1/57)
        case  58    : q_recip   = 1130;     break;  // 65536 * (1/58)
        case  59    : q_recip   = 1111;     break;  // 65536 * (1/59)
        case  60    : q_recip   = 1092;     break;  // 65536 * (1/60)
        case  61    : q_recip   = 1074;     break;  // 65536 * (1/61)
        case  62    : q_recip   = 1057;     break;  // 65536 * (1/62)
        case  63    : q_recip   = 1040;     break;  // 65536 * (1/63)
        case  64    : q_recip   = 1024;     break;  // 65536 * (1/64)
        case  65    : q_recip   = 1008;     break;  // 65536 * (1/65)
        case  66    : q_recip   = 993;      break;  // 65536 * (1/66)
        case  67    : q_recip   = 978;      break;  // 65536 * (1/67)
        case  68    : q_recip   = 964;      break;  // 65536 * (1/68)
        case  69    : q_recip   = 950;      break;  // 65536 * (1/69)
        case  70    : q_recip   = 936;      break;  // 65536 * (1/70)
        case  71    : q_recip   = 923;      break;  // 65536 * (1/71)
        case  72    : q_recip   = 910;      break;  // 65536 * (1/72)
        case  73    : q_recip   = 898;      break;  // 65536 * (1/73)
        case  74    : q_recip   = 886;      break;  // 65536 * (1/74)
        case  75    : q_recip   = 874;      break;  // 65536 * (1/75)
        case  76    : q_recip   = 862;      break;  // 65536 * (1/76)
        case  77    : q_recip   = 851;      break;  // 65536 * (1/77)
        case  78    : q_recip   = 840;      break;  // 65536 * (1/78)
        case  79    : q_recip   = 830;      break;  // 65536 * (1/79)
        case  80    : q_recip   = 819;      break;  // 65536 * (1/80)
        case  81    : q_recip   = 809;      break;  // 65536 * (1/81)
        case  82    : q_recip   = 799;      break;  // 65536 * (1/82)
        case  83    : q_recip   = 790;      break;  // 65536 * (1/83)
        case  84    : q_recip   = 780;      break;  // 65536 * (1/84)
        case  85    : q_recip   = 771;      break;  // 65536 * (1/85)
        case  86    : q_recip   = 762;      break;  // 65536 * (1/86)
        case  87    : q_recip   = 753;      break;  // 65536 * (1/87)
        case  88    : q_recip   = 745;      break;  // 65536 * (1/88)
        case  89    : q_recip   = 736;      break;  // 65536 * (1/89)
        case  90    : q_recip   = 728;      break;  // 65536 * (1/90)
        case  91    : q_recip   = 720;      break;  // 65536 * (1/91)
        case  92    : q_recip   = 712;      break;  // 65536 * (1/92)
        case  93    : q_recip   = 705;      break;  // 65536 * (1/93)
        case  94    : q_recip   = 697;      break;  // 65536 * (1/94)
        case  95    : q_recip   = 690;      break;  // 65536 * (1/95)
        case  96    : q_recip   = 683;      break;  // 65536 * (1/96)
        case  97    : q_recip   = 676;      break;  // 65536 * (1/97)
        case  98    : q_recip   = 669;      break;  // 65536 * (1/98)
        case  99    : q_recip   = 662;      break;  // 65536 * (1/99)
        case 100    : q_recip   = 655;      break;  // 65536 * (1/100)
        case 101    : q_recip   = 649;      break;  // 65536 * (1/101)
        case 102    : q_recip   = 643;      break;  // 65536 * (1/102)
        case 103    : q_recip   = 636;      break;  // 65536 * (1/103)
        case 104    : q_recip   = 630;      break;  // 65536 * (1/104)
        case 105    : q_recip   = 624;      break;  // 65536 * (1/105)
        case 106    : q_recip   = 618;      break;  // 65536 * (1/106)
        case 107    : q_recip   = 612;      break;  // 65536 * (1/107)
        case 108    : q_recip   = 607;      break;  // 65536 * (1/108)
        case 109    : q_recip   = 601;      break;  // 65536 * (1/109)
        case 110    : q_recip   = 596;      break;  // 65536 * (1/110)
        case 111    : q_recip   = 590;      break;  // 65536 * (1/111)
        case 112    : q_recip   = 585;      break;  // 65536 * (1/112)
        case 113    : q_recip   = 580;      break;  // 65536 * (1/113)
        case 114    : q_recip   = 575;      break;  // 65536 * (1/114)
        case 115    : q_recip   = 570;      break;  // 65536 * (1/115)
        case 116    : q_recip   = 565;      break;  // 65536 * (1/116)
        case 117    : q_recip   = 560;      break;  // 65536 * (1/117)
        case 118    : q_recip   = 555;      break;  // 65536 * (1/118)
        case 119    : q_recip   = 551;      break;  // 65536 * (1/119)
        case 120    : q_recip   = 546;      break;  // 65536 * (1/120)
        case 121    : q_recip   = 542;      break;  // 65536 * (1/121)
        case 122    : q_recip   = 537;      break;  // 65536 * (1/122)
        case 123    : q_recip   = 533;      break;  // 65536 * (1/123)
        case 124    : q_recip   = 529;      break;  // 65536 * (1/124)
        case 125    : q_recip   = 524;      break;  // 65536 * (1/125)
        case 126    : q_recip   = 520;      break;  // 65536 * (1/126)
        case 127    : q_recip   = 516;      break;  // 65536 * (1/127)
        case 128    : q_recip   = 512;      break;  // 65536 * (1/128)
        case 129    : q_recip   = 508;      break;  // 65536 * (1/129)
        case 130    : q_recip   = 504;      break;  // 65536 * (1/130)
        case 131    : q_recip   = 500;      break;  // 65536 * (1/131)
        case 132    : q_recip   = 496;      break;  // 65536 * (1/132)
        case 133    : q_recip   = 493;      break;  // 65536 * (1/133)
        case 134    : q_recip   = 489;      break;  // 65536 * (1/134)
        case 135    : q_recip   = 485;      break;  // 65536 * (1/135)
        case 136    : q_recip   = 482;      break;  // 65536 * (1/136)
        case 137    : q_recip   = 478;      break;  // 65536 * (1/137)
        case 138    : q_recip   = 475;      break;  // 65536 * (1/138)
        case 139    : q_recip   = 471;      break;  // 65536 * (1/139)
        case 140    : q_recip   = 468;      break;  // 65536 * (1/140)
        case 141    : q_recip   = 465;      break;  // 65536 * (1/141)
        case 142    : q_recip   = 462;      break;  // 65536 * (1/142)
        case 143    : q_recip   = 458;      break;  // 65536 * (1/143)
        case 144    : q_recip   = 455;      break;  // 65536 * (1/144)
        case 145    : q_recip   = 452;      break;  // 65536 * (1/145)
        case 146    : q_recip   = 449;      break;  // 65536 * (1/146)
        case 147    : q_recip   = 446;      break;  // 65536 * (1/147)
        case 148    : q_recip   = 443;      break;  // 65536 * (1/148)
        case 149    : q_recip   = 440;      break;  // 65536 * (1/149)
        case 150    : q_recip   = 437;      break;  // 65536 * (1/150)
        case 151    : q_recip   = 434;      break;  // 65536 * (1/151)
        case 152    : q_recip   = 431;      break;  // 65536 * (1/152)
        case 153    : q_recip   = 428;      break;  // 65536 * (1/153)
        case 154    : q_recip   = 426;      break;  // 65536 * (1/154)
        case 155    : q_recip   = 423;      break;  // 65536 * (1/155)
        case 156    : q_recip   = 420;      break;  // 65536 * (1/156)
        case 157    : q_recip   = 417;      break;  // 65536 * (1/157)
        case 158    : q_recip   = 415;      break;  // 65536 * (1/158)
        case 159    : q_recip   = 412;      break;  // 65536 * (1/159)
        case 160    : q_recip   = 410;      break;  // 65536 * (1/160)
        case 161    : q_recip   = 407;      break;  // 65536 * (1/161)
        case 162    : q_recip   = 405;      break;  // 65536 * (1/162)
        case 163    : q_recip   = 402;      break;  // 65536 * (1/163)
        case 164    : q_recip   = 400;      break;  // 65536 * (1/164)
        case 165    : q_recip   = 397;      break;  // 65536 * (1/165)
        case 166    : q_recip   = 395;      break;  // 65536 * (1/166)
        case 167    : q_recip   = 392;      break;  // 65536 * (1/167)
        case 168    : q_recip   = 390;      break;  // 65536 * (1/168)
        case 169    : q_recip   = 388;      break;  // 65536 * (1/169)
        case 170    : q_recip   = 386;      break;  // 65536 * (1/170)
        case 171    : q_recip   = 383;      break;  // 65536 * (1/171)
        case 172    : q_recip   = 381;      break;  // 65536 * (1/172)
        case 173    : q_recip   = 379;      break;  // 65536 * (1/173)
        case 174    : q_recip   = 377;      break;  // 65536 * (1/174)
        case 175    : q_recip   = 374;      break;  // 65536 * (1/175)
        case 176    : q_recip   = 372;      break;  // 65536 * (1/176)
        case 177    : q_recip   = 370;      break;  // 65536 * (1/177)
        case 178    : q_recip   = 368;      break;  // 65536 * (1/178)
        case 179    : q_recip   = 366;      break;  // 65536 * (1/179)
        case 180    : q_recip   = 364;      break;  // 65536 * (1/180)
        case 181    : q_recip   = 362;      break;  // 65536 * (1/181)
        case 182    : q_recip   = 360;      break;  // 65536 * (1/182)
        case 183    : q_recip   = 358;      break;  // 65536 * (1/183)
        case 184    : q_recip   = 356;      break;  // 65536 * (1/184)
        case 185    : q_recip   = 354;      break;  // 65536 * (1/185)
        case 186    : q_recip   = 352;      break;  // 65536 * (1/186)
        case 187    : q_recip   = 350;      break;  // 65536 * (1/187)
        case 188    : q_recip   = 349;      break;  // 65536 * (1/188)
        case 189    : q_recip   = 347;      break;  // 65536 * (1/189)
        case 190    : q_recip   = 345;      break;  // 65536 * (1/190)
        case 191    : q_recip   = 343;      break;  // 65536 * (1/191)
        case 192    : q_recip   = 341;      break;  // 65536 * (1/192)
        case 193    : q_recip   = 340;      break;  // 65536 * (1/193)
        case 194    : q_recip   = 338;      break;  // 65536 * (1/194)
        case 195    : q_recip   = 336;      break;  // 65536 * (1/195)
        case 196    : q_recip   = 334;      break;  // 65536 * (1/196)
        case 197    : q_recip   = 333;      break;  // 65536 * (1/197)
        case 198    : q_recip   = 331;      break;  // 65536 * (1/198)
        case 199    : q_recip   = 329;      break;  // 65536 * (1/199)
        case 200    : q_recip   = 328;      break;  // 65536 * (1/200)
        case 201    : q_recip   = 326;      break;  // 65536 * (1/201)
        case 202    : q_recip   = 324;      break;  // 65536 * (1/202)
        case 203    : q_recip   = 323;      break;  // 65536 * (1/203)
        case 204    : q_recip   = 321;      break;  // 65536 * (1/204)
        case 205    : q_recip   = 320;      break;  // 65536 * (1/205)
        case 206    : q_recip   = 318;      break;  // 65536 * (1/206)
        case 207    : q_recip   = 317;      break;  // 65536 * (1/207)
        case 208    : q_recip   = 315;      break;  // 65536 * (1/208)
        case 209    : q_recip   = 314;      break;  // 65536 * (1/209)
        case 210    : q_recip   = 312;      break;  // 65536 * (1/210)
        case 211    : q_recip   = 311;      break;  // 65536 * (1/211)
        case 212    : q_recip   = 309;      break;  // 65536 * (1/212)
        case 213    : q_recip   = 308;      break;  // 65536 * (1/213)
        case 214    : q_recip   = 306;      break;  // 65536 * (1/214)
        case 215    : q_recip   = 305;      break;  // 65536 * (1/215)
        case 216    : q_recip   = 303;      break;  // 65536 * (1/216)
        case 217    : q_recip   = 302;      break;  // 65536 * (1/217)
        case 218    : q_recip   = 301;      break;  // 65536 * (1/218)
        case 219    : q_recip   = 299;      break;  // 65536 * (1/219)
        case 220    : q_recip   = 298;      break;  // 65536 * (1/220)
        case 221    : q_recip   = 297;      break;  // 65536 * (1/221)
        case 222    : q_recip   = 295;      break;  // 65536 * (1/222)
        case 223    : q_recip   = 294;      break;  // 65536 * (1/223)
        case 224    : q_recip   = 293;      break;  // 65536 * (1/224)
        case 225    : q_recip   = 291;      break;  // 65536 * (1/225)
        case 226    : q_recip   = 290;      break;  // 65536 * (1/226)
        case 227    : q_recip   = 289;      break;  // 65536 * (1/227)
        case 228    : q_recip   = 287;      break;  // 65536 * (1/228)
        case 229    : q_recip   = 286;      break;  // 65536 * (1/229)
        case 230    : q_recip   = 285;      break;  // 65536 * (1/230)
        case 231    : q_recip   = 284;      break;  // 65536 * (1/231)
        case 232    : q_recip   = 282;      break;  // 65536 * (1/232)
        case 233    : q_recip   = 281;      break;  // 65536 * (1/233)
        case 234    : q_recip   = 280;      break;  // 65536 * (1/234)
        case 235    : q_recip   = 279;      break;  // 65536 * (1/235)
        case 236    : q_recip   = 278;      break;  // 65536 * (1/236)
        case 237    : q_recip   = 277;      break;  // 65536 * (1/237)
        case 238    : q_recip   = 275;      break;  // 65536 * (1/238)
        case 239    : q_recip   = 274;      break;  // 65536 * (1/239)
        case 240    : q_recip   = 273;      break;  // 65536 * (1/240)
        case 241    : q_recip   = 272;      break;  // 65536 * (1/241)
        case 242    : q_recip   = 271;      break;  // 65536 * (1/242)
        case 243    : q_recip   = 270;      break;  // 65536 * (1/243)
        case 244    : q_recip   = 269;      break;  // 65536 * (1/244)
        case 245    : q_recip   = 267;      break;  // 65536 * (1/245)
        case 246    : q_recip   = 266;      break;  // 65536 * (1/246)
        case 247    : q_recip   = 265;      break;  // 65536 * (1/247)
        case 248    : q_recip   = 264;      break;  // 65536 * (1/248)
        case 249    : q_recip   = 263;      break;  // 65536 * (1/249)
        case 250    : q_recip   = 262;      break;  // 65536 * (1/250)
        case 251    : q_recip   = 261;      break;  // 65536 * (1/251)
        case 252    : q_recip   = 260;      break;  // 65536 * (1/252)
        case 253    : q_recip   = 259;      break;  // 65536 * (1/253)
        case 254    : q_recip   = 258;      break;  // 65536 * (1/254)
        default     : q_recip   = 257;      break;  // 65536 * (1/255)
    }
    return q_recip;
}   /* reciprocal */

static const unsigned short jpeg_quant[7][DCTSIZE2]   = {
    {       // jpeg_quant[0][] : Luma, Canon
    0x06, 0x06, 0x08, 0x0A, 0x0A, 0x10, 0x15, 0x19,
    0x06, 0x0A, 0x0A, 0x0E, 0x12, 0x1F, 0x29, 0x29,
    0x08, 0x0A, 0x0E, 0x12, 0x21, 0x29, 0x29, 0x29,
    0x0A, 0x0E, 0x12, 0x14, 0x23, 0x29, 0x29, 0x29,
    0x0A, 0x12, 0x21, 0x23, 0x27, 0x29, 0x29, 0x29,
    0x10, 0x1F, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29,
    0x15, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29,
    0x19, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29 
    },
    {       // jpeg_quant[1][] : Chroma, Canon
    0x0A, 0x0E, 0x10, 0x14, 0x15, 0x1D, 0x2B, 0x35,
    0x0E, 0x12, 0x14, 0x1D, 0x25, 0x3E, 0x54, 0x54,
    0x10, 0x14, 0x19, 0x25, 0x40, 0x54, 0x54, 0x54,
    0x14, 0x1D, 0x25, 0x27, 0x48, 0x54, 0x54, 0x54,
    0x15, 0x25, 0x40, 0x48, 0x4E, 0x54, 0x54, 0x54,
    0x1D, 0x3E, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54,
    0x2B, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54,
    0x35, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54, 0x54 
    },
    {       // jpeg_quant[2][] : Luma, spec example Table K.1
      16,   11,   10,   16,   24,   40,    51,  61,
      12,   12,   14,   19,   26,   58,    60,  55,
      14,   13,   16,   24,   40,   57,    69,  56,
      14,   17,   22,   29,   51,   87,    80,  62,
      18,   22,   37,   56,   68,  109,   103,  77,
      24,   35,   55,   64,   81,  104,   113,  92,
      49,   64,   78,   87,  103,  121,   120, 101,
      72,   92,   95,   98,  112,  100,   103,  99 
    },
    {       // jpeg_quant[3][] : Chroma, spec example Table K.2
      17,   18,   24,   47,   99,   99,    99,   99,   // 07
      18,   21,   26,   66,   99,   99,    99,   99,   // 15
      24,   26,   56,   99,   99,   99,    99,   99,   // 23
      47,   66,   99,   99,   99,   99,    99,   99,   // 31
      99,   99,   99,   99,   99,   99,    99,   99,   // 39
      99,   99,   99,   99,   99,   99,    99,   99,   // 47
      99,   99,   99,   99,   99,   99,    99,   99,   // 55
      99,   99,   99,   99,   99,   99,    99,   99    // 63
    },
    {       // jpeg_quant[4][] : Luma, spec example Table K.1, modified to create long ZRL
      16,   11,   10,   16,   24,   40,    51,  61,
      12,   12,   14,   19,   26,   58,    60,  55,
      14,   13,   16,   24,   40,   57,    69,  56,
      14,   17,   22,   29,   51,   87,    80,  62,
      18,   22,   37,   56,   68,  109,   103,  77,
      24,   35,   55,   64,   81,  104,   113,  92,
      49,   64,   78,   87,  103,  121,   120, 101,
      72,   92,   95,   98,  112,  100,   103,  16 
    },
    {       // jpeg_quant[5][] : Chroma, spec example Table K.2, modified to create long ZRL
      17,   18,   24,   47,   99,   99,    99,   99,   // 07
      18,   21,   26,   66,   99,   99,    99,   99,   // 15
      24,   26,   56,   99,   99,   99,    99,   99,   // 23
      47,   66,   99,   99,   99,   99,    99,   99,   // 31
      99,   99,   99,   99,   99,   99,    99,   99,   // 39
      99,   99,   99,   99,   99,   99,    99,   99,   // 47
      99,   99,   99,   99,   99,   99,    99,   99,   // 55
      99,   99,   99,   99,   99,   99,    99,   17    // 63
    },
    {       // jpeg_quant[6][] : no compression
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1,
       1,    1,    1,    1,    1,    1,     1,   1 
    }
};  /* jpeg_quant */

static const unsigned char jpeg_huffman_dc[2][16+12]  = {
    {                                   // jpeg_huffman_dc[0][]
        0x00,                           // number of code length=1
        0x01,                           // number of code length=2
        0x05,                           // number of code length=3
        0x01,                           // number of code length=4
        0x01,                           // number of code length=5
        0x01,                           // number of code length=6
        0x01,                           // number of code length=7
        0x01,                           // number of code length=8
        0x01,                           // number of code length=9
        0x00,                           // number of code length=10
        0x00,                           // number of code length=11
        0x00,                           // number of code length=12
        0x00,                           // number of code length=13
        0x00,                           // number of code length=14
        0x00,                           // number of code length=15
        0x00,                           // number of code length=16
        
        0x00,                           // Entry index for code with minimum code length (=2 in this case)
        0x01, 0x02, 0x03, 0x04, 0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B
    },
    {                                   // jpeg_huffman_dc[1][]
        0x00,                           // number of code length=1 
        0x03,                           // number of code length=2 
        0x01,                           // number of code length=3 
        0x01,                           // number of code length=4 
        0x01,                           // number of code length=5 
        0x01,                           // number of code length=6 
        0x01,                           // number of code length=7 
        0x01,                           // number of code length=8 
        0x01,                           // number of code length=9 
        0x01,                           // number of code length=10
        0x01,                           // number of code length=11
        0x00,                           // number of code length=12
        0x00,                           // number of code length=13
        0x00,                           // number of code length=14
        0x00,                           // number of code length=15
        0x00,                           // number of code length=16

        0x00, 0x01, 0x02,               // Entry index for code with minimum code length (=2 in this case)
        0x03,
        0x04,
        0x05,
        0x06,
        0x07,
        0x08,
        0x09,
        0x0A,
        0x0B
    }
};  /* jpeg_huffman_dc */

static const unsigned char jpeg_huffman_ac[2][16+162]  = {
    {                                   // jpeg_huffman_ac[0][]
        0x00,                           // number of code length=1  
        0x02,                           // number of code length=2 
        0x01,                           // number of code length=3 
        0x03,                           // number of code length=4 
        0x03,                           // number of code length=5 
        0x02,                           // number of code length=6 
        0x04,                           // number of code length=7 
        0x03,                           // number of code length=8 
        0x05,                           // number of code length=9 
        0x05,                           // number of code length=10
        0x04,                           // number of code length=11
        0x04,                           // number of code length=12
        0x00,                           // number of code length=13
        0x00,                           // number of code length=14
        0x01,                           // number of code length=15
        0x7D,                           // number of code length=16

        0x01, 0x02,                     // Entry index for code with minimum code length (=2 in this case)
        0x03,
        0x00, 0x04, 0x11,
        0x05, 0x12, 0x21,
        0x31, 0x41,
        0x06, 0x13, 0x51, 0x61,
        0x07, 0x22, 0x71,
        0x14, 0x32, 0x81, 0x91, 0xA1,
        0x08, 0x23, 0x42, 0xB1, 0xC1,
        0x15, 0x52, 0xD1, 0xF0,
        0x24, 0x33, 0x62, 0x72,
        0x82,
        0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36,
        0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56,
        0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76,
        0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95,
        0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3,
        0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
        0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
        0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
    },
    {                                   // jpeg_huffman_ac[1][]
        0x00,                           // number of code length=1  
        0x02,                           // number of code length=2 
        0x01,                           // number of code length=3 
        0x02,                           // number of code length=4 
        0x04,                           // number of code length=5 
        0x04,                           // number of code length=6 
        0x03,                           // number of code length=7 
        0x04,                           // number of code length=8 
        0x07,                           // number of code length=9 
        0x05,                           // number of code length=10
        0x04,                           // number of code length=11
        0x04,                           // number of code length=12
        0x00,                           // number of code length=13
        0x01,                           // number of code length=14
        0x02,                           // number of code length=15
        0x77,                           // number of code length=16

        0x00, 0x01,                     // Entry index for code with minimum code length (=2 in this case)
        0x02,
        0x03, 0x11,
        0x04, 0x05, 0x21, 0x31,
        0x06, 0x12, 0x41, 0x51,
        0x07, 0x61, 0x71,
        0x13, 0x22, 0x32, 0x81,
        0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1,
        0x09, 0x23, 0x33, 0x52, 0xF0,
        0x15, 0x62, 0x72, 0xD1,
        0x0A, 0x16, 0x24, 0x34,

        0xE1,
        0x25, 0xF1,
        0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43,
        0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63,
        0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82,
        0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
        0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
        0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5,
        0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3,
        0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA
    }
};  /* jpeg_huffman_ac */
#endif
