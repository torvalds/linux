///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <typedef.h>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/12/20
//   @fileversion: ITE_HDMITX_SAMPLE_3.14
//******************************************/

#ifndef _TYPEDEF_H_
#define _TYPEDEF_H_

//////////////////////////////////////////////////
// data type
//////////////////////////////////////////////////
#ifdef _MCU_8051_
    #define _CODE code
    #define _DATA data
    #define _XDATA xdata
    #define _IDATA idata
    typedef bit BOOL ;
#else
    #define _CODE //const
    #define _DATA
    #define _IDATA
    #define _XDATA
    typedef int BOOL ;
#endif // _MCU_8051_

typedef    _CODE unsigned char    cBYTE;


typedef char CHAR,*PCHAR ;
typedef unsigned char uchar,*puchar ;
typedef unsigned char UCHAR,*PUCHAR ;
typedef unsigned char byte,*pbyte ;
typedef unsigned char BYTE,*PBYTE ;

typedef short SHORT,*PSHORT ;
typedef unsigned short *pushort ;
typedef unsigned short USHORT,*PUSHORT ;
typedef unsigned short word,*pword ;
typedef unsigned short WORD,*PWORD ;
typedef unsigned int UINT,*PUINT ;

typedef long LONG,*PLONG ;
typedef unsigned long *pulong ;
typedef unsigned long ULONG,*PULONG ;
typedef unsigned long dword,*pdword ;
typedef unsigned long DWORD,*PDWORD ;

#define FALSE 0
#define TRUE 1

#define SUCCESS 0
#define FAIL -1

#define ON 1
#define OFF 0

#define LO_ACTIVE TRUE
#define HI_ACTIVE FALSE


typedef enum _SYS_STATUS {
    ER_SUCCESS = 0,
    ER_FAIL,
    ER_RESERVED
} SYS_STATUS ;

#define ABS(x) (((x)>=0)?(x):(-(x)))





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
        BYTE Type ;
        BYTE Ver ;
        BYTE Len ;

        BYTE CheckSum;

        BYTE IEEE_0;//PB1
        BYTE IEEE_1;//PB2
        BYTE IEEE_2;//PB3

        BYTE Rsvd:5 ;//PB4
        BYTE HDMI_Video_Format:3 ;

        BYTE Reserved_PB5:4 ;//PB5
        BYTE _3D_Structure:4 ;

        BYTE Reserved_PB6:4 ;//PB6
        BYTE _3D_Ext_Data:4 ;
    } info ;
    struct {
        BYTE VS_HB[3] ;
        BYTE CheckSum;
        BYTE VS_DB[28] ;
    } pktbyte ;
} VendorSpecific_InfoFrame ;

typedef union _AVI_InfoFrame
{

    struct {
        BYTE Type;
        BYTE Ver;
        BYTE Len;

        BYTE checksum ;

        BYTE Scan:2;
        BYTE BarInfo:2;
        BYTE ActiveFmtInfoPresent:1;
        BYTE ColorMode:2;
        BYTE FU1:1;

        BYTE ActiveFormatAspectRatio:4;
        BYTE PictureAspectRatio:2;
        BYTE Colorimetry:2;

        BYTE Scaling:2;
        BYTE FU2:6;

        BYTE VIC:7;
        BYTE FU3:1;

        BYTE PixelRepetition:4;
        BYTE FU4:4;

        short Ln_End_Top;
        short Ln_Start_Bottom;
        short Pix_End_Left;
        short Pix_Start_Right;
    } info;

    struct {
        BYTE AVI_HB[3];
        BYTE checksum ;
        BYTE AVI_DB[AVI_INFOFRAME_LEN];
    } pktbyte;
} AVI_InfoFrame;

typedef union _Audio_InfoFrame {

    struct {
        BYTE Type;
        BYTE Ver;
        BYTE Len;
        BYTE checksum ;

        BYTE AudioChannelCount:3;
        BYTE RSVD1:1;
        BYTE AudioCodingType:4;

        BYTE SampleSize:2;
        BYTE SampleFreq:3;
        BYTE Rsvd2:3;

        BYTE FmtCoding;

        BYTE SpeakerPlacement;

        BYTE Rsvd3:3;
        BYTE LevelShiftValue:4;
        BYTE DM_INH:1;
    } info;

    struct {
        BYTE AUD_HB[3];
        BYTE checksum ;
        BYTE AUD_DB[5];
    } pktbyte;

} Audio_InfoFrame;

typedef union _MPEG_InfoFrame {
    struct {
        BYTE Type;
        BYTE Ver;
        BYTE Len;
        BYTE checksum ;

        ULONG MpegBitRate;

        BYTE MpegFrame:2;
        BYTE Rvsd1:2;
        BYTE FieldRepeat:1;
        BYTE Rvsd2:3;
    } info;
    struct {
        BYTE MPG_HB[3];
        BYTE checksum ;
        BYTE MPG_DB[MPEG_INFOFRAME_LEN];
    } pktbyte;
} MPEG_InfoFrame;

typedef union _SPD_InfoFrame {
    struct {
        BYTE Type;
        BYTE Ver;
        BYTE Len;
        BYTE checksum ;

        char VN[8];
        char PD[16];
        BYTE SourceDeviceInfomation;
    } info;
    struct {
        BYTE SPD_HB[3];
        BYTE checksum ;
        BYTE SPD_DB[SPD_INFOFRAME_LEN];
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
    WORD    H_ActiveStart;
    WORD    H_ActiveEnd;
    WORD    H_SyncStart;
    WORD    H_SyncEnd;
    WORD    V_ActiveStart;
    WORD    V_ActiveEnd;
    WORD    V_SyncStart;
    WORD    V_SyncEnd;
    WORD    V2_ActiveStart;
    WORD    V2_ActiveEnd;
    WORD    HTotal;
    WORD    VTotal;
} CEAVTiming;

typedef struct {
    BYTE VIC ;
    BYTE PixelRep ;
    WORD    HActive;
    WORD    VActive;
    WORD    HTotal;
    WORD    VTotal;
    ULONG    PCLK;
    BYTE    xCnt;
    WORD    HFrontPorch;
    WORD    HSyncWidth;
    WORD    HBackPorch;
    BYTE    VFrontPorch;
    BYTE    VSyncWidth;
    BYTE    VBackPorch;
    BYTE    ScanMode:1;
    BYTE    VPolarity:1;
    BYTE    HPolarity:1;
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


#endif // _TYPEDEF_H_
