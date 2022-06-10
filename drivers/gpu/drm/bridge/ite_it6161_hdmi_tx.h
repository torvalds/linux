// SPDX-License-Identifier: GPL-2.0
///*****************************************
//  Copyright (C) 2009-2019
//  ITE Tech. Inc. All Rights Reserved
///*****************************************
//   @file   <hdmitx_sys.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2019/02/15
//   @fileversion: IT6161_SAMPLE_0.50
//******************************************/

typedef enum _SYS_STATUS {
    ER_SUCCESS = 0,
    ER_FAIL,
    ER_RESERVED
} SYS_STATUS ;
    
//#define FALSE 0
//#define TRUE 1
#ifndef NULL
    #define NULL ((void *) 0)
#endif

struct register_load_table
{
    unsigned char offset;
    unsigned char mask;
    unsigned char value;
};

///////////////////////////////////////////////////////////////////////
// Video Data Type
///////////////////////////////////////////////////////////////////////

#define F_MODE_RGB444  0
#define F_MODE_YUV422 1
#define F_MODE_YUV444 2
#define F_MODE_CLRMOD_MASK 3


#define F_MODE_INTERLACE  1

#define F_VIDMODE_ITU709  (1<<4)
#define F_VIDMODE_ITU601  0

#define F_VIDMODE_0_255   0
#define F_VIDMODE_16_235  (1<<5)

#define F_VIDMODE_EN_UDFILT (1<<6)
#define F_VIDMODE_EN_DITHER (1<<7)

#define T_MODE_CCIR656 (1<<0)
#define T_MODE_SYNCEMB (1<<1)
#define T_MODE_INDDR   (1<<2)
#define T_MODE_PCLKDIV2 (1<<3)
#define T_MODE_DEGEN (1<<4)
#define T_MODE_SYNCGEN (1<<5)
/////////////////////////////////////////////////////////////////////
// Packet and Info Frame definition and datastructure.
/////////////////////////////////////////////////////////////////////


#define VENDORSPEC_INFOFRAME_TYPE 0x81
#define AVI_INFOFRAME_TYPE  0x82
#define SPD_INFOFRAME_TYPE 0x83
#define AUDIO_INFOFRAME_TYPE 0x84
#define MPEG_INFOFRAME_TYPE 0x85

#define VENDORSPEC_INFOFRAME_VER 0x01
#define AVI_INFOFRAME_VER  0x02
#define SPD_INFOFRAME_VER 0x01
#define AUDIO_INFOFRAME_VER 0x01
#define MPEG_INFOFRAME_VER 0x01

#define VENDORSPEC_INFOFRAME_LEN 5
#define AVI_INFOFRAME_LEN 13
#define SPD_INFOFRAME_LEN 25
#define AUDIO_INFOFRAME_LEN 10
#define MPEG_INFOFRAME_LEN 10

#define ACP_PKT_LEN 9
#define ISRC1_PKT_LEN 16
#define ISRC2_PKT_LEN 16

typedef union _VendorSpecific_InfoFrame
{
    struct {
        u8 Type ;
        u8 Ver ;
        u8 Len ;

        u8 CheckSum;

        u8 IEEE_0;//PB1
        u8 IEEE_1;//PB2
        u8 IEEE_2;//PB3

        u8 Rsvd:5 ;//PB4
        u8 HDMI_Video_Format:3 ;

        u8 Reserved_PB5:4 ;//PB5
        u8 _3D_Structure:4 ;

        u8 Reserved_PB6:4 ;//PB6
        u8 _3D_Ext_Data:4 ;
    } info ;
    struct {
        u8 VS_HB[3] ;
        u8 CheckSum;
        u8 VS_DB[28] ;
    } pktbyte ;
} VendorSpecific_InfoFrame ;


typedef union _AVI_InfoFrame
{
    struct {
        u8 Type;
        u8 Ver;
        u8 Len;

        u8 checksum ;

        u8 Scan:2;
        u8 BarInfo:2;
        u8 ActiveFmtInfoPresent:1;
        u8 ColorMode:2;
        u8 FU1:1;

        u8 ActiveFormatAspectRatio:4;
        u8 PictureAspectRatio:2;
        u8 Colorimetry:2;

        u8 Scaling:2;
        u8 FU2:6;

        u8 VIC:7;
        u8 FU3:1;

        u8 PixelRepetition:4;
        u8 FU4:4;

        u16 Ln_End_Top;
        u16 Ln_Start_Bottom;
        u16 Pix_End_Left;
        u16 Pix_Start_Right;
    } info;

    struct {
        u8 AVI_HB[3];
        u8 checksum ;
        u8 AVI_DB[AVI_INFOFRAME_LEN];
    } pktbyte;
} AVI_InfoFrame;

typedef union _Audio_InfoFrame {

    struct {
        u8 Type;
        u8 Ver;
        u8 Len;
        u8 checksum ;

        u8 AudioChannelCount:3;
        u8 RSVD1:1;
        u8 AudioCodingType:4;

        u8 SampleSize:2;
        u8 SampleFreq:3;
        u8 Rsvd2:3;

        u8 FmtCoding;

        u8 SpeakerPlacement;

        u8 Rsvd3:3;
        u8 LevelShiftValue:4;
        u8 DM_INH:1;
    } info;

    struct {
        u8 AUD_HB[3];
        u8 checksum ;
        u8 AUD_DB[10];
    } pktbyte;

} Audio_InfoFrame;

typedef union _MPEG_InfoFrame {
    struct {
        u8 Type;
        u8 Ver;
        u8 Len;
        u8 checksum ;

        u32 MpegBitRate;

        u8 MpegFrame:2;
        u8 Rvsd1:2;
        u8 FieldRepeat:1;
        u8 Rvsd2:3;
    } info;
    struct {
        u8 MPG_HB[3];
        u8 checksum ;
        u8 MPG_DB[MPEG_INFOFRAME_LEN];
    } pktbyte;
} MPEG_InfoFrame;

typedef union _SPD_InfoFrame {
    struct {
        u8 Type;
        u8 Ver;
        u8 Len;
        u8 checksum ;

        char VN[8];
        char PD[16];
        u8 SourceDeviceInfomation;
    } info;
    struct {
        u8 SPD_HB[3];
        u8 checksum ;
        u8 SPD_DB[SPD_INFOFRAME_LEN];
    } pktbyte;
} SPD_InfoFrame;


///////////////////////////////////////////////////////////////////////////
// Using for interface.
///////////////////////////////////////////////////////////////////////////

#define PROG 1
#define INTERLACE 0
#define Vneg 0
#define Hneg 0
#define Vpos 1
#define Hpos 1

typedef struct {
    u32    H_ActiveStart;
    u32    H_ActiveEnd;
    u32    H_SyncStart;
    u32    H_SyncEnd;
    u32    V_ActiveStart;
    u32    V_ActiveEnd;
    u32    V_SyncStart;
    u32    V_SyncEnd;
    u32    V2_ActiveStart;
    u32    V2_ActiveEnd;
    u32    HTotal;
    u32    VTotal;
} CEAVTiming;

typedef struct {
    u8 VIC ;
    u8 PixelRep ;
    u32    HActive;
    u32    VActive;
    u32    HTotal;
    u32    VTotal;
    u32    PCLK;
    u16    xCnt;
    u32    HFrontPorch;
    u32    HSyncWidth;
    u32    HBackPorch;
    u8    VFrontPorch;
    u8    VSyncWidth;
    u8    VBackPorch;
    u8    ScanMode:1;
    u8    VPolarity:1;
    u8    HPolarity:1;
} HDMI_VTiming;

//////////////////////////////////////////////////////////////////
// Audio relate definition and macro.
//////////////////////////////////////////////////////////////////

// 2008/08/15 added by jj_tseng@chipadvanced
#define F_AUDIO_ON  (1<<7)
#define F_AUDIO_HBR (1<<6)
#define F_AUDIO_DSD (1<<5)
#define F_AUDIO_NLPCM (1<<4)
#define F_AUDIO_LAYOUT_1 (1<<3)
#define F_AUDIO_LAYOUT_0 (0<<3)

// HBR - 1100
// DSD - 1010
// NLPCM - 1001
// LPCM - 1000

#define T_AUDIO_MASK 0xF0
#define T_AUDIO_OFF 0
#define T_AUDIO_HBR (F_AUDIO_ON|F_AUDIO_HBR)
#define T_AUDIO_DSD (F_AUDIO_ON|F_AUDIO_DSD)
#define T_AUDIO_NLPCM (F_AUDIO_ON|F_AUDIO_NLPCM)
#define T_AUDIO_LPCM (F_AUDIO_ON)

// for sample clock
#define AUDFS_22p05KHz  4
#define AUDFS_44p1KHz 0
#define AUDFS_88p2KHz 8
#define AUDFS_176p4KHz    12

#define AUDFS_24KHz  6
#define AUDFS_48KHz  2
#define AUDFS_96KHz  10
#define AUDFS_192KHz 14

#define AUDFS_768KHz 9

#define AUDFS_32KHz  3
#define AUDFS_OTHER    1

// Audio Enable
#define ENABLE_SPDIF    (1<<4)
#define ENABLE_I2S_SRC3  (1<<3)
#define ENABLE_I2S_SRC2  (1<<2)
#define ENABLE_I2S_SRC1  (1<<1)
#define ENABLE_I2S_SRC0  (1<<0)

#define AUD_SWL_NOINDICATE  0x0
#define AUD_SWL_16          0x2
#define AUD_SWL_17          0xC
#define AUD_SWL_18          0x4
#define AUD_SWL_20          0xA // for maximum 20 bit
#define AUD_SWL_21          0xD
#define AUD_SWL_22          0x5
#define AUD_SWL_23          0x9
#define AUD_SWL_24          0xB





#ifndef _IT6161_CONFIG_H_
#define _IT6161_CONFIG_H_


#define IC_VERSION (0xC0)
#if (IC_VERSION == 0xC0)
#pragma message("Defined IC_VERSION C0")
#endif // EXTERN_HDCPROM

/*************************************************************************************************/
//HDMITX 
/*************************************************************************************************/
#pragma message("config.h")

#ifdef EXTERN_HDCPROM
#pragma message("Defined EXTERN_HDCPROM")
#endif // EXTERN_HDCPROM

#define SUPPORT_EDID
#define SUPPORT_HDCP
#define SUPPORT_SHA
//#define SUPPORT_AUDIO_MONITOR
#define AudioOutDelayCnt 250

// #define SUPPORT_CEC


//////////////////////////////////////////////////////////////////////////////////////////
// Video Configuration
//////////////////////////////////////////////////////////////////////////////////////////
// 2010/01/26 added a option to disable HDCP.
#define SUPPORT_OUTPUTYUV
#define SUPPORT_OUTPUTRGB
// #define DISABLE_HDMITX_CSC

#define SUPPORT_INPUTRGB
#define SUPPORT_INPUTYUV444
#define SUPPORT_INPUTYUV422
// #define SUPPORT_SYNCEMBEDDED
// #define SUPPORT_DEGEN
#define NON_SEQUENTIAL_YCBCR422



#define INPUT_COLOR_MODE F_MODE_RGB444
//#define INPUT_COLOR_MODE F_MODE_YUV422
//#define INPUT_COLOR_MODE F_MODE_YUV444

#define INPUT_COLOR_DEPTH 24
// #define INPUT_COLOR_DEPTH 30
// #define INPUT_COLOR_DEPTH 36

//#define OUTPUT_3D_MODE Frame_Pcaking
//#define OUTPUT_3D_MODE Top_and_Botton
//#define OUTPUT_3D_MODE Side_by_Side

// #define INV_INPUT_ACLK
// #define INV_INPUT_PCLK
#ifdef USING_IT66120
    #pragma message("Defined Using IT66120")
    #define SUPPORT_SYNCEMBEDDED
#endif

#ifdef SUPPORT_SYNCEMBEDDED
    #ifndef USING_IT66120
    // #define INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB)                 // 16 bit sync embedded
    // #define INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB | T_MODE_CCIR656) // 8 bit sync embedded
    #define INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB|T_MODE_INDDR|T_MODE_PCLKDIV2) // 16 bit sync embedded DDR
    // #define INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB|T_MODE_INDDR)      // 8  bit sync embedded DDR
    #else
        #define INPUT_SIGNAL_TYPE (T_MODE_SYNCEMB | T_MODE_CCIR656) // 8 bit sync embedded
    #endif

    #define SUPPORT_INPUTYUV422
    #ifdef INPUT_COLOR_MODE
    #undef INPUT_COLOR_MODE
    #endif // INPUT_COLOR_MODE
    #define INPUT_COLOR_MODE F_MODE_YUV422
#else
    #pragma message ("Defined seperated sync.")
    #define INPUT_SIGNAL_TYPE 0 // 24 bit sync seperate
    //#define INPUT_SIGNAL_TYPE ( T_MODE_DEGEN )
    //#define INPUT_SIGNAL_TYPE ( T_MODE_INDDR)
    //#define INPUT_SIGNAL_TYPE ( T_MODE_SYNCEMB)
    //#define INPUT_SIGNAL_TYPE ( T_MODE_CCIR656 | T_MODE_SYNCEMB )
#endif


#if defined(SUPPORT_INPUTYUV444) || defined(SUPPORT_INPUTYUV422)
#define SUPPORT_INPUTYUV
#endif

#ifdef SUPPORT_SYNCEMBEDDED
#pragma message("defined SUPPORT_SYNCEMBEDDED for Sync Embedded timing input or CCIR656 input.")
#endif


//////////////////////////////////////////////////////////////////////////////////////////
// Audio Configuration
//////////////////////////////////////////////////////////////////////////////////////////

// #define SUPPORT_HBR_AUDIO
#define USE_SPDIF_CHSTAT
#ifndef SUPPORT_HBR_AUDIO
    #define INPUT_SAMPLE_FREQ AUDFS_48KHz
    #define INPUT_SAMPLE_FREQ_HZ 48000L
    #define OUTPUT_CHANNEL 2 // 3 // 4 // 5//6 //7 //8

    #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_LPCM
    // #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_NLPCM
    #define CONFIG_INPUT_AUDIO_INTERFACE I2S
    // #define CONFIG_INPUT_AUDIO_INTERFACE SPDIF
    // #define CONFIG_INPUT_AUDIO_INTERFACE TDM

    // #define I2S_FORMAT 0x00 // 24bit I2S audio
    #define I2S_FORMAT 0x01 // 32bit I2S audio
    // #define I2S_FORMAT 0x02 // 24bit I2S audio, right justify
    // #define I2S_FORMAT 0x03 // 32bit I2S audio, right justify

#else // SUPPORT_HBR_AUDIO

    #define INPUT_SAMPLE_FREQ AUDFS_768KHz
    #define INPUT_SAMPLE_FREQ_HZ 768000L
    #define OUTPUT_CHANNEL 8
    #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_HBR
    #define CONFIG_INPUT_AUDIO_INTERFACE FALSE // I2S
    // #define CONFIG_INPUT_AUDIO_INTERFACE TRUE // SPDIF
    #define I2S_FORMAT 0x47 // 32bit audio
#endif



//////////////////////////////////////////////////////////////////////////////////////////
// Audio Monitor Configuration
//////////////////////////////////////////////////////////////////////////////////////////

//#define HDMITX_AUTO_MONITOR_INPUT
#define HDMITX_INPUT_INFO

#ifdef  HDMITX_AUTO_MONITOR_INPUT
#define HDMITX_INPUT_INFO
#endif

//////////////////////////////////////////////////////////////////////////////////////////
// Reduce Source Clock Jitter
//////////////////////////////////////////////////////////////////////////////////////////
 //#define REDUCE_HDMITX_SRC_JITTER

//////////////////////////////////////////////////////////////////////////////////////////
// MIPI Rx Configuration
//////////////////////////////////////////////////////////////////////////////////////////
#define MIPIRX_LANE_NUM		4 //1~4

#endif




#ifndef _HDMITX_H_
#define _HDMITX_H_

#define HDMITX_MAX_DEV_COUNT 1


///////////////////////////////////////////////////////////////////////
// Output Mode Type
///////////////////////////////////////////////////////////////////////

#define RES_ASPEC_4x3 0
#define RES_ASPEC_16x9 1
#define F_MODE_REPT_NO 0
#define F_MODE_REPT_TWICE 1
#define F_MODE_REPT_QUATRO 3
#define F_MODE_CSC_ITU601 0
#define F_MODE_CSC_ITU709 1


#define TIMER_LOOP_LEN 10
#define MS(x) (((x)+(TIMER_LOOP_LEN-1))/TIMER_LOOP_LEN); // for timer loop

// #define SUPPORT_AUDI_AudSWL 16 // Jeilin case.
#define SUPPORT_AUDI_AudSWL 24 // Jeilin case.

#if(SUPPORT_AUDI_AudSWL==16)
    #define CHTSTS_SWCODE 0x02
#elif(SUPPORT_AUDI_AudSWL==18)
    #define CHTSTS_SWCODE 0x04
#elif(SUPPORT_AUDI_AudSWL==20)
    #define CHTSTS_SWCODE 0x03
#else
    #define CHTSTS_SWCODE 0x0B
#endif

#endif // _HDMITX_H_


///*****************************************
//  Copyright (C) 2009-2019
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmitx_drv.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2019/02/15
//   @fileversion: IT6161_SAMPLE_0.50
//******************************************/

#ifndef _HDMITX_DRV_H_
#define _HDMITX_DRV_H_

//#define EXTERN_HDCPROM
/////////////////////////////////////////
// DDC Address
/////////////////////////////////////////
#define DDC_HDCP_ADDRESS 0x74
#define DDC_EDID_ADDRESS 0xA0
#define DDC_FIFO_MAXREQ 0x20

// I2C address

#define _80MHz 80000000
#define HDMI_TX_I2C_SLAVE_ADDR 0x98
#define CEC_I2C_SLAVE_ADDR 0x9C
///////////////////////////////////////////////////////////////////////
// Register offset
///////////////////////////////////////////////////////////////////////

#define REG_TX_VENDOR_ID0   0x00
#define REG_TX_VENDOR_ID1   0x01
#define REG_TX_DEVICE_ID0   0x02
#define REG_TX_DEVICE_ID1   0x03

    #define O_TX_DEVID 0
    #define M_TX_DEVID 0xF
    #define O_TX_REVID 4
    #define M_TX_REVID 0xF

#define REG_TX_SW_RST       0x04
    #define B_TX_ENTEST    (1<<7)
    #define B_TX_REF_RST_HDMITX (1<<5)
    #define B_TX_AREF_RST (1<<4)
    #define B_HDMITX_VID_RST (1<<3)
    #define B_HDMITX_AUD_RST (1<<2)
    #define B_TX_HDMI_RST (1<<1)
    #define B_TX_HDCP_RST_HDMITX (1<<0)

#define REG_TX_INT_CTRL 0x05
    #define B_TX_INTPOL_ACTL 0
    #define B_TX_INTPOL_ACTH (1<<7)
    #define B_TX_INT_PUSHPULL 0
    #define B_TX_INT_OPENDRAIN (1<<6)

#define REG_TX_INT_STAT1    0x06
    #define B_TX_INT_AUD_OVERFLOW  (1<<7)
    #define B_TX_INT_ROMACQ_NOACK  (1<<6)
    #define B_TX_INT_RDDC_NOACK    (1<<5)
    #define B_TX_INT_DDCFIFO_ERR   (1<<4)
    #define B_TX_INT_ROMACQ_BUS_HANG   (1<<3)
    #define B_TX_INT_DDC_BUS_HANG  (1<<2)
    #define B_TX_INT_RX_SENSE  (1<<1)
    #define B_TX_INT_HPD_PLUG  (1<<0)

#define REG_TX_INT_STAT2    0x07
    #define B_TX_INT_HDCP_SYNC_DET_FAIL  (1<<7)
    #define B_TX_INT_VID_UNSTABLE  (1<<6)
    #define B_TX_INT_PKTACP    (1<<5)
    #define B_TX_INT_PKTNULL  (1<<4)
    #define B_TX_INT_PKTGENERAL   (1<<3)
    #define B_TX_INT_KSVLIST_CHK   (1<<2)
    #define B_TX_INT_AUTH_DONE (1<<1)
    #define B_TX_INT_AUTH_FAIL (1<<0)

#define REG_TX_INT_STAT3    0x08
    #define B_TX_INT_AUD_CTS   (1<<6)
    #define B_TX_INT_VSYNC     (1<<5)
    #define B_TX_INT_VIDSTABLE (1<<4)
    #define B_TX_INT_PKTMPG    (1<<3)
    #define B_TX_INT_PKTSPD    (1<<2)
    #define B_TX_INT_PKTAUD    (1<<1)
    #define B_TX_INT_PKTAVI    (1<<0)

#define REG_TX_INT_MASK1    0x09
    #define B_TX_AUDIO_OVFLW_MASK (1<<7)
    #define B_TX_DDC_NOACK_MASK (1<<5)
    #define B_TX_DDC_FIFO_ERR_MASK (1<<4)
    #define B_TX_DDC_BUS_HANG_MASK (1<<2)
    #define B_TX_RXSEN_MASK (1<<1)
    #define B_TX_HPD_MASK (1<<0)

#define REG_TX_INT_MASK2    0x0A
    #define B_TX_PKT_AVI_MASK (1<<7)
    #define B_TX_PKT_VID_UNSTABLE_MASK (1<<6)
    #define B_TX_PKT_ACP_MASK (1<<5)
    #define B_TX_PKT_NULL_MASK (1<<4)
    #define B_TX_PKT_GEN_MASK (1<<3)
    #define B_TX_KSVLISTCHK_MASK (1<<2)
    #define B_TX_AUTH_DONE_MASK (1<<1)
    #define B_TX_AUTH_FAIL_MASK (1<<0)

#define REG_TX_INT_MASK3    0x0B
    #define B_TX_HDCP_SYNC_DET_FAIL_MASK (1<<6)
    #define B_TX_AUDCTS_MASK (1<<5)
    #define B_TX_VSYNC_MASK (1<<4)
    #define B_TX_VIDSTABLE_MASK (1<<3)
    #define B_TX_PKT_MPG_MASK (1<<2)
    #define B_TX_PKT_SPD_MASK (1<<1)
    #define B_TX_PKT_AUD_MASK (1<<0)

#define REG_TX_INT_CLR0      0x0C
    #define B_TX_CLR_PKTACP    (1<<7)
    #define B_TX_CLR_PKTNULL   (1<<6)
    #define B_TX_CLR_PKTGENERAL    (1<<5)
    #define B_TX_CLR_KSVLISTCHK    (1<<4)
    #define B_TX_CLR_AUTH_DONE  (1<<3)
    #define B_TX_CLR_AUTH_FAIL  (1<<2)
    #define B_TX_CLR_RXSENSE   (1<<1)
    #define B_TX_CLR_HPD       (1<<0)

#define REG_TX_INT_CLR1       0x0D
    #define B_TX_CLR_VSYNC (1<<7)
    #define B_TX_CLR_VIDSTABLE (1<<6)
    #define B_TX_CLR_PKTMPG    (1<<5)
    #define B_TX_CLR_PKTSPD    (1<<4)
    #define B_TX_CLR_PKTAUD    (1<<3)
    #define B_TX_CLR_PKTAVI    (1<<2)
    #define B_TX_CLR_HDCP_SYNC_DET_FAIL  (1<<1)
    #define B_TX_CLR_VID_UNSTABLE        (1<<0)

#define REG_TX_SYS_STATUS     0x0E
    // readonly
    #define B_TX_INT_ACTIVE    (1<<7)
    #define B_TX_HPDETECT      (1<<6)
    #define B_TX_RXSENDETECT   (1<<5)
    #define B_TXVIDSTABLE   (1<<4)
    // read/write
    #define O_TX_CTSINTSTEP    2
    #define M_TX_CTSINTSTEP    (3<<2)
    #define B_TX_CLR_AUD_CTS     (1<<1)
    #define B_TX_INTACTDONE    (1<<0)

#define REG_TX_BANK_CTRL        0x0F
    #define B_TX_BANK0 0
    #define B_TX_BANK1 1

// DDC

#define REG_TX_DDC_MASTER_CTRL   0x10
    #define B_TX_MASTERROM (1<<1)
    #define B_TX_MASTERDDC (0<<1)
    #define B_TX_MASTERHOST    (1<<0)
    #define B_TX_MASTERHDCP    (0<<0)

#define REG_TX_DDC_HEADER  0x11
#define REG_TX_DDC_REQOFF  0x12
#define REG_TX_DDC_REQCOUNT    0x13
#define REG_TX_DDC_EDIDSEG 0x14
#define REG_TX_DDC_CMD 0x15
    #define CMD_DDC_SEQ_BURSTREAD 0
    #define CMD_LINK_CHKREAD  2
    #define CMD_EDID_READ   3
    #define CMD_FIFO_CLR    9
    #define CMD_GEN_SCLCLK  0xA
    #define CMD_DDC_ABORT   0xF

#define REG_TX_DDC_STATUS  0x16
    #define B_TX_DDC_DONE  (1<<7)
    #define B_TX_DDC_ACT   (1<<6)
    #define B_TX_DDC_NOACK (1<<5)
    #define B_TX_DDC_WAITBUS   (1<<4)
    #define B_TX_DDC_ARBILOSE  (1<<3)
    #define B_TX_DDC_ERROR     (B_TX_DDC_NOACK|B_TX_DDC_WAITBUS|B_TX_DDC_ARBILOSE)
    #define B_TX_DDC_FIFOFULL  (1<<2)
    #define B_TX_DDC_FIFOEMPTY (1<<1)

#define REG_TX_DDC_READFIFO    0x17
#define REG_TX_ROM_STARTADDR   0x18
#define REG_TX_HDCP_HEADER 0x19
#define REG_TX_ROM_HEADER  0x1A
#define REG_TX_BUSHOLD_T   0x1B
#define REG_TX_ROM_STAT    0x1C
    #define B_TX_ROM_DONE  (1<<7)
    #define B_TX_ROM_ACTIVE	(1<<6)
    #define B_TX_ROM_NOACK	(1<<5)
    #define B_TX_ROM_WAITBUS	(1<<4)
    #define B_TX_ROM_ARBILOSE	(1<<3)
    #define B_TX_ROM_BUSHANG	(1<<2)

// HDCP
#define REG_TX_AN_GENERATE 0x1F
    #define B_TX_START_CIPHER_GEN  1
    #define B_TX_STOP_CIPHER_GEN   0

#define REG_TX_CLK_CTRL0 0x58
    #define O_TX_OSCLK_SEL 5
    #define M_TX_OSCLK_SEL 3
    #define B_TX_AUTO_OVER_SAMPLING_CLOCK (1<<4)
    #define O_TX_EXT_MCLK_SEL  2
    #define M_TX_EXT_MCLK_SEL  (3<<O_TX_EXT_MCLK_SEL)
    #define B_TX_EXT_128FS (0<<O_TX_EXT_MCLK_SEL)
    #define B_TX_EXT_256FS (1<<O_TX_EXT_MCLK_SEL)
    #define B_TX_EXT_512FS (2<<O_TX_EXT_MCLK_SEL)
    #define B_TX_EXT_1024FS (3<<O_TX_EXT_MCLK_SEL)

#define REG_TX_SHA_SEL       0x50
#define REG_TX_SHA_RD_BYTE1  0x51
#define REG_TX_SHA_RD_BYTE2  0x52
#define REG_TX_SHA_RD_BYTE3  0x53
#define REG_TX_SHA_RD_BYTE4  0x54
#define REG_TX_AKSV_RD_BYTE5 0x55


#define REG_TX_CLK_CTRL1 0x59
    #define B_TX_EN_TXCLK_COUNT    (1<<5)
    #define B_TX_VDO_LATCH_EDGE    (1<<3)

#define REG_TX_CLK_STATUS1 0x5E
#define REG_TX_CLK_STATUS2 0x5F
    #define B_TX_IP_LOCK (1<<7)
    #define B_TX_XP_LOCK (1<<6)
    #define B_TX_OSF_LOCK (1<<5)

#define REG_TX_AUD_COUNT 0x60
#define REG_TX_AFE_DRV_CTRL 0x61

    #define B_TX_AFE_DRV_PWD    (1<<5)
    #define B_TX_AFE_DRV_RST    (1<<4)

// Input Data Format Register
#define REG_TX_INPUT_MODE  0x70
    #define O_TX_INCLKDLY	0
    #define M_TX_INCLKDLY	3
    #define B_TX_INDDR	    (1<<2)
    #define B_TX_SYNCEMB	(1<<3)
    #define B_TX_2X656CLK	(1<<4)
	#define B_TX_PCLKDIV2  (1<<5)
    #define M_TX_INCOLMOD	(3<<6)
    #define B_TX_IN_RGB    0
    #define B_TX_IN_YUV422 (1<<6)
    #define B_TX_IN_YUV444 (2<<6)

#define REG_TX_TXFIFO_RST  0x71
    #define B_TX_ENAVMUTERST	1
    #define B_TXFFRST	(1<<1)

#define REG_TX_CSC_CTRL    0x72
    #define B_HDMITX_CSC_BYPASS    0
    #define B_HDMITX_CSC_RGB2YUV   2
    #define B_HDMITX_CSC_YUV2RGB   3
    #define M_TX_CSC_SEL       3
    #define B_TX_EN_DITHER      (1<<7)
    #define B_TX_EN_UDFILTER    (1<<6)
    #define B_TX_DNFREE_GO      (1<<5)

#define SIZEOF_CSCMTX 21
#define SIZEOF_CSCGAIN 6
#define SIZEOF_CSCOFFSET 3


#define REG_TX_CSC_YOFF 0x73
#define REG_TX_CSC_COFF 0x74
#define REG_TX_CSC_RGBOFF 0x75

#define REG_TX_CSC_MTX11_L 0x76
#define REG_TX_CSC_MTX11_H 0x77
#define REG_TX_CSC_MTX12_L 0x78
#define REG_TX_CSC_MTX12_H 0x79
#define REG_TX_CSC_MTX13_L 0x7A
#define REG_TX_CSC_MTX13_H 0x7B
#define REG_TX_CSC_MTX21_L 0x7C
#define REG_TX_CSC_MTX21_H 0x7D
#define REG_TX_CSC_MTX22_L 0x7E
#define REG_TX_CSC_MTX22_H 0x7F
#define REG_TX_CSC_MTX23_L 0x80
#define REG_TX_CSC_MTX23_H 0x81
#define REG_TX_CSC_MTX31_L 0x82
#define REG_TX_CSC_MTX31_H 0x83
#define REG_TX_CSC_MTX32_L 0x84
#define REG_TX_CSC_MTX32_H 0x85
#define REG_TX_CSC_MTX33_L 0x86
#define REG_TX_CSC_MTX33_H 0x87

#define REG_TX_CSC_GAIN1V_L 0x88
#define REG_TX_CSC_GAIN1V_H 0x89
#define REG_TX_CSC_GAIN2V_L 0x8A
#define REG_TX_CSC_GAIN2V_H 0x8B
#define REG_TX_CSC_GAIN3V_L 0x8C
#define REG_TX_CSC_GAIN3V_H 0x8D

#define REG_TX_HVPol 0x90
#define REG_TX_HfPixel 0x91
#define REG_TX_HSSL 0x95
#define REG_TX_HSEL 0x96
#define REG_TX_HSH 0x97
#define REG_TX_VSS1 0xA0
#define REG_TX_VSE1 0xA1
#define REG_TX_VSS2 0xA2
#define REG_TX_VSE2 0xA3

// HDMI General Control Registers

#define REG_TX_HDMI_MODE   0xC0
    #define B_TX_HDMI_MODE 1
    #define B_TX_DVI_MODE  0
#define REG_TX_AV_MUTE 0xC1
#define REG_TX_GCP     0xC1
    #define B_TX_CLR_AVMUTE    0
    #define B_TX_SET_AVMUTE    1
    #define B_TX_SETAVMUTE        (1<<0)
    #define B_TX_BLUE_SCR_MUTE   (1<<1)
    #define B_TX_NODEF_PHASE    (1<<2)
    #define B_TX_PHASE_RESYNC   (1<<3)

    #define O_TX_COLOR_DEPTH     4
    #define M_TX_COLOR_DEPTH     7
    #define B_TX_COLOR_DEPTH_MASK (M_TX_COLOR_DEPTH<<O_TX_COLOR_DEPTH)
    #define B_TX_CD_NODEF  0
    #define B_TX_CD_24     (4<<4)
    #define B_TX_CD_30     (5<<4)
    #define B_TX_CD_36     (6<<4)
    #define B_TX_CD_48     (7<<4)
#define REG_TX_PKT_GENERAL_CTRL    0xC6

#define REG_TX_OESS_CYCLE  0xC3

/////////////////////////////////////////////////////////////////////
// data structure
/////////////////////////////////////////////////////////////////////
#ifdef _SUPPORT_HDCP_REPEATER_
typedef enum {
    TxHDCP_Off=0,//0
    TxHDCP_AuthRestart,//1
    TxHDCP_AuthStart,//2
    TxHDCP_Receiver,//3
    TxHDCP_Repeater,//4
    TxHDCP_CheckFIFORDY,//5
    TxHDCP_VerifyRevocationList,//6
    TxHDCP_CheckSHA,//7
    TxHDCP_Authenticated,//8
    TxHDCP_AuthFail,//9
    TxHDCP_RepeaterFail,//10
    TxHDCP_RepeaterSuccess,//11
    TxHDCP_Reserved                 //12
} HDMITX_HDCP_State ;
#endif

typedef struct _HDMITXDEV_STRUCT {

	u8 I2C_DEV ;
	u8 I2C_ADDR ;

	/////////////////////////////////////////////////
	// Interrupt Type
	/////////////////////////////////////////////////
	u8 bIntType ; // = 0 ;
	/////////////////////////////////////////////////
	// Video Property
	/////////////////////////////////////////////////
	u8 bInputVideoSignalType ; // for Sync Embedded,CCIR656,InputDDR
	/////////////////////////////////////////////////
	// Audio Property
	/////////////////////////////////////////////////
	u8 bOutputAudioMode ; // = 0 ;
	u8 bAudioChannelSwap ; // = 0 ;
    u8 bAudioChannelEnable ;
    u8 bAudFs ;
    u32 TMDSClock ;
    u32 RCLK ;
    #ifdef _SUPPORT_HDCP_REPEATER_
        HDMITX_HDCP_State TxHDCP_State ;
        u16 usHDCPTimeOut ;
        u16 Tx_BStatus ;
    #endif
	u8 bAuthenticated:1 ;
	u8 bHDMIMode: 1;
	u8 bIntPOL:1 ; // 0 = Low Active
	u8 bHPD:1 ;
	// 2009/11/11 added by jj_tseng@ite.com.tw
    u8 bAudInterface;
    u8 TxEMEMStatus:1 ;
    //~jau-chih.tseng@ite.com.tw 2009/11/11
} HDMITXDEV ;

// 2008/02/27 added by jj_tseng@chipadvanced.com
typedef enum _mode_id {
    UNKNOWN_MODE=0,
    CEA_640x480p60,
    CEA_720x480p60,
    CEA_1280x720p60,
    CEA_1920x1080i60,
    CEA_720x480i60,
    CEA_720x240p60,
    CEA_1440x480i60,
    CEA_1440x240p60,
    CEA_2880x480i60,
    CEA_2880x240p60,
    CEA_1440x480p60,
    CEA_1920x1080p60,
    CEA_720x576p50,
    CEA_1280x720p50,
    CEA_1920x1080i50,
    CEA_720x576i50,
    CEA_1440x576i50,
    CEA_720x288p50,
    CEA_1440x288p50,
    CEA_2880x576i50,
    CEA_2880x288p50,
    CEA_1440x576p50,
    CEA_1920x1080p50,
    CEA_1920x1080p24,
    CEA_1920x1080p25,
    CEA_1920x1080p30,
    VESA_640x350p85,
    VESA_640x400p85,
    VESA_720x400p85,
    VESA_640x480p60,
    VESA_640x480p72,
    VESA_640x480p75,
    VESA_640x480p85,
    VESA_800x600p56,
    VESA_800x600p60,
    VESA_800x600p72,
    VESA_800x600p75,
    VESA_800X600p85,
    VESA_840X480p60,
    VESA_1024x768p60,
    VESA_1024x768p70,
    VESA_1024x768p75,
    VESA_1024x768p85,
    VESA_1152x864p75,
    VESA_1280x768p60R,
    VESA_1280x768p60,
    VESA_1280x768p75,
    VESA_1280x768p85,
    VESA_1280x960p60,
    VESA_1280x960p85,
    VESA_1280x1024p60,
    VESA_1280x1024p75,
    VESA_1280X1024p85,
    VESA_1360X768p60,
    VESA_1400x768p60R,
    VESA_1400x768p60,
    VESA_1400x1050p75,
    VESA_1400x1050p85,
    VESA_1440x900p60R,
    VESA_1440x900p60,
    VESA_1440x900p75,
    VESA_1440x900p85,
    VESA_1600x1200p60,
    VESA_1600x1200p65,
    VESA_1600x1200p70,
    VESA_1600x1200p75,
    VESA_1600x1200p85,
    VESA_1680x1050p60R,
    VESA_1680x1050p60,
    VESA_1680x1050p75,
    VESA_1680x1050p85,
    VESA_1792x1344p60,
    VESA_1792x1344p75,
    VESA_1856x1392p60,
    VESA_1856x1392p75,
    VESA_1920x1200p60R,
    VESA_1920x1200p60,
    VESA_1920x1200p75,
    VESA_1920x1200p85,
    VESA_1920x1440p60,
    VESA_1920x1440p75,
} MODE_ID ;

//~jj_tseng@chipadvanced.com

typedef struct structRegSetEntry {
    u8 offset;
    u8 mask;
    u8 value;
} RegSetEntry;

// Audio Channel Control
#define REG_TX_AUDIO_CTRL0 0xE0
	#define M_TX_AUD_SWL (3<<6)
	#define M_TX_AUD_16BIT (0<<6)
	#define M_TX_AUD_18BIT (1<<6)
	#define M_TX_AUD_20BIT (2<<6)
	#define M_TX_AUD_24BIT (3<<6)

	#define B_TX_SPDIFTC (1<<5)

	#define B_TX_AUD_SPDIF (1<<4)
	#define B_TX_AUD_I2S (0<<4)
	#define B_TX_AUD_EN_I2S3   (1<<3)
	#define B_TX_AUD_EN_I2S2   (1<<2)
	#define B_TX_AUD_EN_I2S1   (1<<1)
	#define B_TX_AUD_EN_I2S0   (1<<0)
    #define B_TX_AUD_EN_SPDIF  1

#define REG_TX_AUDIO_CTRL1 0xE1
	#define B_TX_AUD_FULLPKT (1<<6)

	#define B_TX_AUDFMT_STD_I2S (0<<0)
	#define B_TX_AUDFMT_32BIT_I2S (1<<0)
	#define B_TX_AUDFMT_LEFT_JUSTIFY (0<<1)
	#define B_TX_AUDFMT_RIGHT_JUSTIFY (1<<1)
	#define B_TX_AUDFMT_DELAY_1T_TO_WS (0<<2)
	#define B_TX_AUDFMT_NO_DELAY_TO_WS (1<<2)
	#define B_TX_AUDFMT_WS0_LEFT   (0<<3)
	#define B_TX_AUDFMT_WS0_RIGHT   (1<<3)
	#define B_TX_AUDFMT_MSB_SHIFT_FIRST (0<<4)
	#define B_TX_AUDFMT_LSB_SHIFT_FIRST (1<<4)
	#define B_TX_AUDFMT_RISE_EDGE_SAMPLE_WS (0<<5)
	#define B_TX_AUDFMT_FALL_EDGE_SAMPLE_WS (1<<5)

#define REG_TX_AUDIO_FIFOMAP 0xE2
	#define O_TX_FIFO3SEL 6
	#define O_TX_FIFO2SEL 4
	#define O_TX_FIFO1SEL 2
	#define O_TX_FIFO0SEL 0
	#define B_TX_SELSRC3  3
	#define B_TX_SELSRC2  2
	#define B_TX_SELSRC1  1
	#define B_TX_SELSRC0  0

#define REG_TX_AUDIO_CTRL3 0xE3
	#define B_TX_AUD_MULCH (1<<7)
	#define B_TX_EN_ZERO_CTS (1<<6)
	#define B_TX_CHSTSEL (1<<4)
	#define B_TX_S3RLCHG (1<<3)
	#define B_TX_S2RLCHG (1<<2)
	#define B_TX_S1RLCHG (1<<1)
	#define B_TX_S0RLCHG (1<<0)

#define REG_TX_AUD_SRCVALID_FLAT 0xE4
	#define B_TX_AUD_SPXFLAT_SRC3 (1<<7)
	#define B_TX_AUD_SPXFLAT_SRC2 (1<<6)
	#define B_TX_AUD_SPXFLAT_SRC1 (1<<5)
	#define B_TX_AUD_SPXFLAT_SRC0 (1<<4)
	#define B_TX_AUD_ERR2FLAT (1<<3)
	#define B_TX_AUD_S3VALID (1<<2)
	#define B_TX_AUD_S2VALID (1<<1)
	#define B_TX_AUD_S1VALID (1<<0)

#define REG_TX_AUD_HDAUDIO 0xE5
#define B_TX_HBR   (1<<3)
#define B_TX_DSD   (1<<1)
#define B_TX_TDM   (1<<0)

//////////////////////////////////////////
// Bank 1
//////////////////////////////////////////

#define REGPktAudCTS0 0x30  // 7:0
#define REGPktAudCTS1 0x31  // 15:8
#define REGPktAudCTS2 0x32  // 19:16
#define REGPktAudN0 0x33    // 7:0
#define REGPktAudN1 0x34    // 15:8
#define REGPktAudN2 0x35    // 19:16
#define REGPktAudCTSCnt0 0x35   // 3:0
#define REGPktAudCTSCnt1 0x36   // 11:4
#define REGPktAudCTSCnt2 0x37   // 19:12


#define REG_TX_AUDCHST_MODE    0x91 // 191 REG_TX_AUD_CHSTD[2:0] 6:4
                                 //     REG_TX_AUD_CHSTC 3
                                 //     REG_TX_AUD_NLPCM 2
                                 //     REG_TX_AUD_MONO 0
#define REG_TX_AUDCHST_CAT     0x92 // 192 REG_TX_AUD_CHSTCAT 7:0
#define REG_TX_AUDCHST_SRCNUM  0x93 // 193 REG_TX_AUD_CHSTSRC 3:0
#define REG_TX_AUD0CHST_CHTNUM 0x94 // 194 REG_TX_AUD0_CHSTCHR 7:4
                                 //     REG_TX_AUD0_CHSTCHL 3:0
#define REG_TX_AUD1CHST_CHTNUM 0x95 // 195 REG_TX_AUD1_CHSTCHR 7:4
                                 //     REG_TX_AUD1_CHSTCHL 3:0
#define REG_TX_AUD2CHST_CHTNUM 0x96 // 196 REG_TX_AUD2_CHSTCHR 7:4
                                 //     REG_TX_AUD2_CHSTCHL 3:0
#define REG_TX_AUD3CHST_CHTNUM 0x97 // 197 REG_TX_AUD3_CHSTCHR 7:4
                                 //     REG_TX_AUD3_CHSTCHL 3:0
#define REG_TX_AUDCHST_CA_FS   0x98 // 198 REG_TX_AUD_CHSTCA 5:4
                                 //     REG_TX_AUD_CHSTFS 3:0
#define REG_TX_AUDCHST_OFS_WL  0x99 // 199 REG_TX_AUD_CHSTOFS 7:4
                                 //     REG_TX_AUD_CHSTWL 3:0

#define REG_TX_PKT_SINGLE_CTRL 0xC5
    #define B_TX_SINGLE_PKT    1
    #define B_TX_BURST_PKT
    #define B_TX_SW_CTS    (1<<1)

#define REG_TX_NULL_CTRL 0xC9
#define REG_TX_ACP_CTRL 0xCA
#define REG_TX_ISRC1_CTRL 0xCB
#define REG_TX_ISRC2_CTRL 0xCC
#define REG_TX_AVI_INFOFRM_CTRL 0xCD
#define REG_TX_AUD_INFOFRM_CTRL 0xCE
#define REG_TX_SPD_INFOFRM_CTRL 0xCF
#define REG_TX_MPG_INFOFRM_CTRL 0xD0
    #define B_TX_ENABLE_PKT    1
    #define B_TX_REPEAT_PKT    (1<<1)

#define REG_TX_3D_INFO_CTRL 0xD2

//////////////////////////////////////////
// COMMON PACKET for NULL,ISRC1,ISRC2,SPD
//////////////////////////////////////////

#define	REG_TX_PKT_HB00 0x38
#define	REG_TX_PKT_HB01 0x39
#define	REG_TX_PKT_HB02 0x3A

#define	REG_TX_PKT_PB00 0x3B
#define	REG_TX_PKT_PB01 0x3C
#define	REG_TX_PKT_PB02 0x3D
#define	REG_TX_PKT_PB03 0x3E
#define	REG_TX_PKT_PB04 0x3F
#define	REG_TX_PKT_PB05 0x40
#define	REG_TX_PKT_PB06 0x41
#define	REG_TX_PKT_PB07 0x42
#define	REG_TX_PKT_PB08 0x43
#define	REG_TX_PKT_PB09 0x44
#define	REG_TX_PKT_PB10 0x45
#define	REG_TX_PKT_PB11 0x46
#define	REG_TX_PKT_PB12 0x47
#define	REG_TX_PKT_PB13 0x48
#define	REG_TX_PKT_PB14 0x49
#define	REG_TX_PKT_PB15 0x4A
#define	REG_TX_PKT_PB16 0x4B
#define	REG_TX_PKT_PB17 0x4C
#define	REG_TX_PKT_PB18 0x4D
#define	REG_TX_PKT_PB19 0x4E
#define	REG_TX_PKT_PB20 0x4F
#define	REG_TX_PKT_PB21 0x50
#define	REG_TX_PKT_PB22 0x51
#define	REG_TX_PKT_PB23 0x52
#define	REG_TX_PKT_PB24 0x53
#define	REG_TX_PKT_PB25 0x54
#define	REG_TX_PKT_PB26 0x55
#define	REG_TX_PKT_PB27 0x56

#define REG_TX_AVIINFO_DB1 0x58
#define REG_TX_AVIINFO_DB2 0x59
#define REG_TX_AVIINFO_DB3 0x5A
#define REG_TX_AVIINFO_DB4 0x5B
#define REG_TX_AVIINFO_DB5 0x5C
#define REG_TX_AVIINFO_DB6 0x5E
#define REG_TX_AVIINFO_DB7 0x5F
#define REG_TX_AVIINFO_DB8 0x60
#define REG_TX_AVIINFO_DB9 0x61
#define REG_TX_AVIINFO_DB10 0x62
#define REG_TX_AVIINFO_DB11 0x63
#define REG_TX_AVIINFO_DB12 0x64
#define REG_TX_AVIINFO_DB13 0x65
#define REG_TX_AVIINFO_SUM 0x5D

#define REG_TX_PKT_AUDINFO_CC 0x68 // [2:0]
#define REG_TX_PKT_AUDINFO_SF 0x69 // [4:2]
#define REG_TX_PKT_AUDINFO_CA 0x6B // [7:0]

#define REG_TX_PKT_AUDINFO_DM_LSV 0x6C // [7][6:3]
#define REG_TX_PKT_AUDINFO_SUM 0x6D // [7:0]

// Source Product Description Info Frame
#define REG_TX_PKT_SPDINFO_SUM 0x70
#define REG_TX_PKT_SPDINFO_PB1 0x71
#define REG_TX_PKT_SPDINFO_PB2 0x72
#define REG_TX_PKT_SPDINFO_PB3 0x73
#define REG_TX_PKT_SPDINFO_PB4 0x74
#define REG_TX_PKT_SPDINFO_PB5 0x75
#define REG_TX_PKT_SPDINFO_PB6 0x76
#define REG_TX_PKT_SPDINFO_PB7 0x77
#define REG_TX_PKT_SPDINFO_PB8 0x78
#define REG_TX_PKT_SPDINFO_PB9 0x79
#define REG_TX_PKT_SPDINFO_PB10 0x7A
#define REG_TX_PKT_SPDINFO_PB11 0x7B
#define REG_TX_PKT_SPDINFO_PB12 0x7C
#define REG_TX_PKT_SPDINFO_PB13 0x7D
#define REG_TX_PKT_SPDINFO_PB14 0x7E
#define REG_TX_PKT_SPDINFO_PB15 0x7F
#define REG_TX_PKT_SPDINFO_PB16 0x80
#define REG_TX_PKT_SPDINFO_PB17 0x81
#define REG_TX_PKT_SPDINFO_PB18 0x82
#define REG_TX_PKT_SPDINFO_PB19 0x83
#define REG_TX_PKT_SPDINFO_PB20 0x84
#define REG_TX_PKT_SPDINFO_PB21 0x85
#define REG_TX_PKT_SPDINFO_PB22 0x86
#define REG_TX_PKT_SPDINFO_PB23 0x87
#define REG_TX_PKT_SPDINFO_PB24 0x88
#define REG_TX_PKT_SPDINFO_PB25 0x89

#define REG_TX_PKT_MPGINFO_FMT 0x8A
#define B_TX_MPG_FR 1
#define B_TX_MPG_MF_I  (1<<1)
#define B_TX_MPG_MF_B  (2<<1)
#define B_TX_MPG_MF_P  (3<<1)
#define B_TX_MPG_MF_MASK (3<<1)
#define REG_TX_PKG_MPGINFO_DB0 0x8B
#define REG_TX_PKG_MPGINFO_DB1 0x8C
#define REG_TX_PKG_MPGINFO_DB2 0x8D
#define REG_TX_PKG_MPGINFO_DB3 0x8E
#define REG_TX_PKG_MPGINFO_SUM 0x8F

#define Frame_Pcaking 0
#define Top_and_Botton 6
#define Side_by_Side 8

////////////////////////////////////////////////////
// Function Prototype
////////////////////////////////////////////////////
#define hdmitx_ENABLE_NULL_PKT()         { it6161_hdmi_tx_write(it6161, REG_TX_NULL_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_ACP_PKT()          { it6161_hdmi_tx_write(it6161, REG_TX_ACP_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_ISRC1_PKT()        { it6161_hdmi_tx_write(it6161, REG_TX_ISRC1_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_ISRC2_PKT()        { it6161_hdmi_tx_write(it6161, REG_TX_ISRC2_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_AVI_INFOFRM_PKT()  { it6161_hdmi_tx_write(it6161, REG_TX_AVI_INFOFRM_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_AUD_INFOFRM_PKT()  { it6161_hdmi_tx_write(it6161, REG_TX_AUD_INFOFRM_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_SPD_INFOFRM_PKT()  { it6161_hdmi_tx_write(it6161, REG_TX_SPD_INFOFRM_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_MPG_INFOFRM_PKT()  { it6161_hdmi_tx_write(it6161, REG_TX_MPG_INFOFRM_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_ENABLE_GeneralPurpose_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_NULL_CTRL,B_TX_ENABLE_PKT|B_TX_REPEAT_PKT); }
#define hdmitx_DISABLE_VSDB_PKT()        { it6161_hdmi_tx_write(it6161, REG_TX_3D_INFO_CTRL,0); }
#define hdmitx_DISABLE_NULL_PKT()        { it6161_hdmi_tx_write(it6161, REG_TX_NULL_CTRL,0); }
#define hdmitx_DISABLE_ACP_PKT()         { it6161_hdmi_tx_write(it6161, REG_TX_ACP_CTRL,0); }
#define hdmitx_DISABLE_ISRC1_PKT()       { it6161_hdmi_tx_write(it6161, REG_TX_ISRC1_CTRL,0); }
#define hdmitx_DISABLE_ISRC2_PKT()       { it6161_hdmi_tx_write(it6161, REG_TX_ISRC2_CTRL,0); }
#define hdmitx_DISABLE_AVI_INFOFRM_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_AVI_INFOFRM_CTRL,0); }
#define hdmitx_DISABLE_AUD_INFOFRM_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_AUD_INFOFRM_CTRL,0); }
#define hdmitx_DISABLE_SPD_INFOFRM_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_SPD_INFOFRM_CTRL,0); }
#define hdmitx_DISABLE_MPG_INFOFRM_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_MPG_INFOFRM_CTRL,0); }
#define hdmitx_DISABLE_GeneralPurpose_PKT() { it6161_hdmi_tx_write(it6161, REG_TX_NULL_CTRL,0); }

//////////////////////////////////////////////////////////////////////
// External Interface
//////////////////////////////////////////////////////////////////////

typedef enum {
    PCLK_LOW = 0 ,
    PCLK_MEDIUM,
    PCLK_HIGH
} VIDEOPCLKLEVEL ;

u8 CheckHDMITX(u8 *pHPD,u8 *pHPDChange);
static bool getHDMITX_LinkStatus(void);
void HDMITX_PowerDown(void);
bool HDMITX_EnableVideoOutput(VIDEOPCLKLEVEL level,u8 inputColorMode,u8 outputColorMode,u8 bHDMI);
bool setHDMITX_VideoSignalType(u8 inputSignalType);
void setHDMITX_ColorDepthPhase(u8 ColorDepth,u8 bPhase);

// TBD ...
// #ifdef SUPPORT_DEGEN
// bool ProgramDEGenModeByID(MODE_ID id,u8 bInputSignalType);
// #endif // SUPPORT_DEGEN

#ifdef SUPPORT_SYNCEMBEDDED
    bool setHDMITX_SyncEmbeddedByVIC(u8 VIC,u8 bInputSignalType);
#endif

/////////////////////////////////////////////////////////////////////////////////////
// HDMITX audio function prototype
/////////////////////////////////////////////////////////////////////////////////////
#ifdef SUPPORT_AUDIO_MONITOR
//void setHDMITX_AudioChannelEnable(bool EnableAudio_b);
#endif //#ifdef SUPPORT_AUDIO_MONITOR
static void setHDMITX_DSDAudio(void);
static void setHDMITX_HBRAudio(u8 bAudInterface /*I2S/SPDIF/TDM*/);
static void setHDMITX_LPCMAudio(u8 AudioSrcNum, u8 AudSWL, u8 bAudInterface /*I2S/SPDIF/TDM*/);
static void setHDMITX_NCTS(u8 Fs);
static void setHDMITX_NLPCMAudio(u8 bAudInterface /*I2S/SPDIF/TDM*/);
//void setHDMITX_UpdateChStatFs(u32 Fs);
#ifdef SUPPORT_AUDIO_MONITOR
bool hdmitx_IsAudioChang(void);
void hdmitx_AutoAdjustAudio(void);
#endif //#ifdef SUPPORT_AUDIO_MONITOR

/////////////////////////////////////////////////////////////////////////////////////
// HDMITX pkt/infoframe function prototype
/////////////////////////////////////////////////////////////////////////////////////

//SYS_STATUS hdmitx_SetSPDInfoFrame(SPD_InfoFrame *pSPDInfoFrame);
//SYS_STATUS hdmitx_SetMPEGInfoFrame(MPEG_InfoFrame *pMPGInfoFrame);
//SYS_STATUS hdmitx_Set_GeneralPurpose_PKT(u8 *pData);

////////////////////////////////////////////////////////////////////
// Required Interfance
////////////////////////////////////////////////////////////////////
/*u8 HDMITX_ReadI2C_Byte(u8 RegAddr);
SYS_STATUS it6161_hdmi_tx_write(it6161, u8 RegAddr,u8 d);
SYS_STATUS HDMITX_ReadI2C_ByteN(u8 RegAddr,u8 *pData,int N);
SYS_STATUS HDMITX_WriteI2C_ByteN(u8 RegAddr,u8 *pData,int N);
SYS_STATUS HDMITX_SetI2C_Byte(u8 Reg,u8 Mask,u8 Value);
SYS_STATUS HDMITX_ToggleBit(u8 Reg,u8 n);*/


#endif // _HDMITX_DRV_H_

#ifndef _HDMITX_SYS_H_
#define _HDMITX_SYS_H_

#define I2S 0
#define SPDIF 1
#define TDM 2

#ifndef I2S_FORMAT
#define I2S_FORMAT 0x01 // 32bit audio
#endif

#ifndef INPUT_SAMPLE_FREQ
    #define INPUT_SAMPLE_FREQ AUDFS_48KHz
#endif //INPUT_SAMPLE_FREQ

#ifndef INPUT_SAMPLE_FREQ_HZ
    #define INPUT_SAMPLE_FREQ_HZ 48000L
#endif //INPUT_SAMPLE_FREQ_HZ

#ifndef OUTPUT_CHANNEL
    #define OUTPUT_CHANNEL 2
#endif //OUTPUT_CHANNEL

#ifndef CNOFIG_INPUT_AUDIO_TYPE
    #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_LPCM
    // #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_NLPCM
    // #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_HBR
#endif //CNOFIG_INPUT_AUDIO_TYPE

#ifndef CONFIG_INPUT_AUDIO_INTERFACE
    #define CONFIG_INPUT_AUDIO_INTERFACE I2S
    // #define CONFIG_INPUT_AUDIO_INTERFACE  SPDIF
#endif //CONFIG_INPUT_AUDIO_INTERFACE

#ifndef INPUT_SIGNAL_TYPE
#define INPUT_SIGNAL_TYPE 0 // 24 bit sync seperate
#endif

////////////////////////////////////////////////////////////////////////////////
// Internal Data Type
////////////////////////////////////////////////////////////////////////////////

typedef enum tagHDMI_Video_Type {
    HDMI_Unkown = 0 ,
    HDMI_640x480p60 = 1 ,
    HDMI_480p60,
    HDMI_480p60_16x9,
    HDMI_720p60,
    HDMI_1080i60,
    HDMI_480i60,
    HDMI_480i60_16x9,
    HDMI_1080p60 = 16,
    HDMI_576p50,
    HDMI_576p50_16x9,
    HDMI_720p50,
    HDMI_1080i50,
    HDMI_576i50,
    HDMI_576i50_16x9,
    HDMI_1080p50 = 31,
    HDMI_1080p24,
    HDMI_1080p25,
    HDMI_1080p30,
    HDMI_720p30 = 61,
} HDMI_Video_Type ;

typedef enum tagHDMI_Aspec {
    HDMI_4x3 ,
    HDMI_16x9
} HDMI_Aspec;

typedef enum tagHDMI_OutputColorMode {
    HDMI_RGB444,
    HDMI_YUV444,
    HDMI_YUV422
} HDMI_OutputColorMode ;

typedef enum tagHDMI_Colorimetry {
    HDMI_ITU601,
    HDMI_ITU709
} HDMI_Colorimetry ;

struct VideoTiming {
    u32 VideoPixelClock ;
    u8 VIC ;
    u8 pixelrep ;
    u8 outputVideoMode ;
} ;



typedef enum _TXVideo_State_Type {
    TXVSTATE_Unplug = 0,
    TXVSTATE_HPD,
    TXVSTATE_WaitForMode,
    TXVSTATE_WaitForVStable,
    TXVSTATE_VideoInit,
    TXVSTATE_VideoSetup,
    TXVSTATE_VideoOn,
    TXVSTATE_Reserved
} TXVideo_State_Type ;


typedef enum _TXAudio_State_Type {
    TXASTATE_AudioOff = 0,
    TXASTATE_AudioPrepare,
    TXASTATE_AudioOn,
    TXASTATE_AudioFIFOFail,
    TXASTATE_Reserved
} TXAudio_State_Type ;
/////////////////////////////////////////
// RX Capability.
/////////////////////////////////////////
typedef struct {
    u8 b16bit:1 ;
    u8 b20bit:1 ;
    u8 b24bit:1 ;
    u8 Rsrv:5 ;
} LPCM_BitWidth ;

typedef enum {
    AUD_RESERVED_0 = 0 ,
    AUD_LPCM,
    AUD_AC3,
    AUD_MPEG1,
    AUD_MP3,
    AUD_MPEG2,
    AUD_AAC,
    AUD_DTS,
    AUD_ATRAC,
    AUD_ONE_BIT_AUDIO,
    AUD_DOLBY_DIGITAL_PLUS,
    AUD_DTS_HD,
    AUD_MAT_MLP,
    AUD_DST,
    AUD_WMA_PRO,
    AUD_RESERVED_15
} AUDIO_FORMAT_CODE ;

typedef union {
    struct {
        u8 channel:3 ;
        u8 AudioFormatCode:4 ;
        u8 Rsrv1:1 ;

        u8 b32KHz:1 ;
        u8 b44_1KHz:1 ;
        u8 b48KHz:1 ;
        u8 b88_2KHz:1 ;
        u8 b96KHz:1 ;
        u8 b176_4KHz:1 ;
        u8 b192KHz:1 ;
        u8 Rsrv2:1 ;
        u8 ucCode ;
    } s ;
    u8 uc[3] ;
} AUDDESCRIPTOR ;

typedef union {
    struct {
        u8 FL_FR:1 ;
        u8 LFE:1 ;
        u8 FC:1 ;
        u8 RL_RR:1 ;
        u8 RC:1 ;
        u8 FLC_FRC:1 ;
        u8 RLC_RRC:1 ;
        u8 Reserve:1 ;
        u8 Unuse[2] ;
    } s ;
    u8 uc[3] ;
} SPK_ALLOC ;

#define CEA_SUPPORT_UNDERSCAN (1<<7)
#define CEA_SUPPORT_AUDIO (1<<6)
#define CEA_SUPPORT_YUV444 (1<<5)
#define CEA_SUPPORT_YUV422 (1<<4)
#define CEA_NATIVE_MASK 0xF


#define HDMI_DC_SUPPORT_AI (1<<7)
#define HDMI_DC_SUPPORT_48 (1<<6)
#define HDMI_DC_SUPPORT_36 (1<<5)
#define HDMI_DC_SUPPORT_30 (1<<4)
#define HDMI_DC_SUPPORT_Y444 (1<<3)
#define HDMI_DC_SUPPORT_DVI_DUAL 1

typedef union _tag_DCSUPPORT {
    struct {
        u8 DVI_Dual:1 ;
        u8 Rsvd:2 ;
        u8 DC_Y444:1 ;
        u8 DC_30Bit:1 ;
        u8 DC_36Bit:1 ;
        u8 DC_48Bit:1 ;
        u8 SUPPORT_AI:1 ;
    } info ;
    u8 uc ;
} DCSUPPORT ;

typedef union _LATENCY_SUPPORT{
    struct {
        u8 Rsvd:6 ;
        u8 I_Latency_Present:1 ;
        u8 Latency_Present:1 ;
    } info ;
    u8 uc ;
} LATENCY_SUPPORT ;

#define HDMI_IEEEOUI 0x0c03
#define MAX_VODMODE_COUNT 32
#define MAX_AUDDES_COUNT 4

typedef struct _RX_CAP{
    u8 VideoMode ;
    u8 NativeVDOMode ;
    u8 VDOMode[8] ;
    u8 AUDDesCount ;
    AUDDESCRIPTOR AUDDes[MAX_AUDDES_COUNT] ;
    u8 PA[2] ;
    u32 IEEEOUI ;
    DCSUPPORT dc ;
    u8 MaxTMDSClock ;
    LATENCY_SUPPORT lsupport ;
    SPK_ALLOC   SpeakerAllocBlk ;
    u8 ValidCEA:1 ;
    u8 ValidHDMI:1 ;
    u8 Valid3D:1 ;
} RX_CAP ;

///////////////////////////////////////////////////////////////////////
// Output Mode Type
///////////////////////////////////////////////////////////////////////

#define RES_ASPEC_4x3 0
#define RES_ASPEC_16x9 1
#define F_MODE_REPT_NO 0
#define F_MODE_REPT_TWICE 1
#define F_MODE_REPT_QUATRO 3
#define F_MODE_CSC_ITU601 0
#define F_MODE_CSC_ITU709 1

void HDMITX_DevLoopProc(void);
//void HDMITX_ChangeAudioOption(u8 Option, u8 channelNum, u8 AudioFs);
//void HDMITX_ChangeColorDepth(u8 colorDepth);

#endif // _HDMITX_SYS_H_

#ifndef _HDMITX_HDCP_H_
#define _HDMITX_HDCP_H_

#define REG_TX_HDCP_DESIRE 0x20
    #define B_TX_ENABLE_HDPC11 (1<<1)
    #define B_TX_CPDESIRE  (1<<0)

#define REG_TX_AUTHFIRE    0x21
#define REG_TX_LISTCTRL    0x22
    #define B_TX_LISTFAIL  (1<<1)
    #define B_TX_LISTDONE  (1<<0)

#define REG_TX_AKSV    0x23
#define REG_TX_AKSV0   0x23
#define REG_TX_AKSV1   0x24
#define REG_TX_AKSV2   0x25
#define REG_TX_AKSV3   0x26
#define REG_TX_AKSV4   0x27

#define REG_TX_AN  0x28
#define REG_TX_AN_GEN  0x30
#define REG_TX_ARI     0x38
#define REG_TX_ARI0    0x38
#define REG_TX_ARI1    0x39
#define REG_TX_APJ     0x3A

#define REG_TX_BKSV    0x3B
#define REG_TX_BRI     0x40
#define REG_TX_BRI0    0x40
#define REG_TX_BRI1    0x41
#define REG_TX_BPJ     0x42
#define REG_TX_BCAP    0x43
    #define B_TX_CAP_HDMI_REPEATER (1<<6)
    #define B_TX_CAP_KSV_FIFO_RDY  (1<<5)
    #define B_TX_CAP_HDMI_FAST_MODE    (1<<4)
    #define B_CAP_HDCP_1p1  (1<<1)
    #define B_TX_CAP_FAST_REAUTH   (1<<0)
#define REG_TX_BSTAT   0x44
#define REG_TX_BSTAT0   0x44
#define REG_TX_BSTAT1   0x45
    #define B_TX_CAP_HDMI_MODE (1<<12)
    #define B_TX_CAP_DVI_MODE (0<<12)
    #define B_TX_MAX_CASCADE_EXCEEDED  (1<<11)
    #define M_TX_REPEATER_DEPTH    (0x7<<8)
    #define O_TX_REPEATER_DEPTH    8
    #define B_TX_DOWNSTREAM_OVER   (1<<7)
    #define M_TX_DOWNSTREAM_COUNT  0x7F

#define REG_TX_AUTH_STAT 0x46
#define B_TX_AUTH_DONE (1<<7)
////////////////////////////////////////////////////
// Function Prototype
////////////////////////////////////////////////////

//SYS_STATUS hdmitx_hdcp_VerifyIntegration();
//static SYS_STATUS hdmitx_hdcp_CheckSHA(u8 pM0[],u16 BStatus,u8 pKSVList[],int cDownStream,u8 Vr[]);
#endif // _HDMITX_HDCP_H_


#if (defined (SUPPORT_OUTPUTYUV)) && (defined (SUPPORT_INPUTRGB))

    static u8 bCSCMtx_RGB2YUV_ITU601_16_235[] =
    {
        0x00,0x80,0x00,
        0xB2,0x04,0x65,0x02,0xE9,0x00,
        0x93,0x3C,0x18,0x04,0x55,0x3F,
        0x49,0x3D,0x9F,0x3E,0x18,0x04
    } ;

    static u8 bCSCMtx_RGB2YUV_ITU601_0_255[] =
    {
        0x10,0x80,0x10,
        0x09,0x04,0x0E,0x02,0xC9,0x00,
        0x0F,0x3D,0x84,0x03,0x6D,0x3F,
        0xAB,0x3D,0xD1,0x3E,0x84,0x03
    } ;

    static u8 bCSCMtx_RGB2YUV_ITU709_16_235[] =
    {
        0x00,0x80,0x00,
        0xB8,0x05,0xB4,0x01,0x94,0x00,
        0x4a,0x3C,0x17,0x04,0x9F,0x3F,
        0xD9,0x3C,0x10,0x3F,0x17,0x04
    } ;

    static u8 bCSCMtx_RGB2YUV_ITU709_0_255[] =
    {
        0x10,0x80,0x10,
        0xEa,0x04,0x77,0x01,0x7F,0x00,
        0xD0,0x3C,0x83,0x03,0xAD,0x3F,
        0x4B,0x3D,0x32,0x3F,0x83,0x03
    } ;
#endif

#if (defined (SUPPORT_OUTPUTRGB)) && (defined (SUPPORT_INPUTYUV))

    static u8 bCSCMtx_YUV2RGB_ITU601_16_235[] =
    {
        0x00,0x00,0x00,
        0x00,0x08,0x6B,0x3A,0x50,0x3D,
        0x00,0x08,0xF5,0x0A,0x02,0x00,
        0x00,0x08,0xFD,0x3F,0xDA,0x0D
    } ;

    static u8 bCSCMtx_YUV2RGB_ITU601_0_255[] =
    {
        0x04,0x00,0xA7,
        0x4F,0x09,0x81,0x39,0xDD,0x3C,
        0x4F,0x09,0xC4,0x0C,0x01,0x00,
        0x4F,0x09,0xFD,0x3F,0x1F,0x10
    } ;

    static u8 bCSCMtx_YUV2RGB_ITU709_16_235[] =
    {
        0x00,0x00,0x00,
        0x00,0x08,0x55,0x3C,0x88,0x3E,
        0x00,0x08,0x51,0x0C,0x00,0x00,
        0x00,0x08,0x00,0x00,0x84,0x0E
    } ;

    static u8 bCSCMtx_YUV2RGB_ITU709_0_255[] =
    {
        0x04,0x00,0xA7,
        0x4F,0x09,0xBA,0x3B,0x4B,0x3E,
        0x4F,0x09,0x57,0x0E,0x02,0x00,
        0x4F,0x09,0xFE,0x3F,0xE8,0x10
    } ;
#endif
