///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <hdmirx.c>
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2012/07/24
//   @fileversion: HDMIRX_SAMPLE_2.18
//******************************************/



// #define OUTPUT_CCIR656
// #define OUTPUT_SYNC_EMBEDDED
// #define OUTPUT_16BIT_YUV422
// #define OUTPUT_24BIT_YUV422
// #define OUTPUT_24BIT_YUV444
#define OUTPUT_24BIT_RGB444
// #define HDMI_REPEATER    // always output signal withtout tristate


///////////////////////////////////////////////////
// Rev 1.01
///////////////////////////////////////////////////
// reg6C = 0x03
// reg6B = 0x11
// reg3B = 0x40
// reg6E = 0x0C
///////////////////////////////////////////////////
// Rev 1.09
///////////////////////////////////////////////////
//Reg6C=0x00
//Reg93=0x43
//Reg94=0x4F
//Reg95=0x87
//Reg96=0x33
///////////////////////////////////////////////////


/*********************************************************************************
 * HDMIRX HDMI RX sample code                                                   *
 *********************************************************************************/
#include "hdmirx.h"

#ifndef DEBUG_PORT_ENABLE
#define DEBUG_PORT_ENABLE 0
#else
#pragma message("DEBUG_PORT_ENABLE defined\n")
#endif


///////////////////////////////////////////////////////////
// Definition.
///////////////////////////////////////////////////////////

#define SetSPDIFMUTE(x) SetMUTE(~(1<<O_TRI_SPDIF),(x)?(1<<O_TRI_SPDIF):0)
#define SetI2S3MUTE(x) SetMUTE(~(1<<O_TRI_I2S3),(x)?(1<<O_TRI_I2S3):0)
#define SetI2S2MUTE(x) SetMUTE(~(1<<O_TRI_I2S2),(x)?(1<<O_TRI_I2S2):0)
#define SetI2S1MUTE(x) SetMUTE(~(1<<O_TRI_I2S1),(x)?(1<<O_TRI_I2S1):0)
#define SetI2S0MUTE(x) SetMUTE(~(1<<O_TRI_I2S0),(x)?(1<<O_TRI_I2S0):0)
//#define SetALLMute() SetMUTE(B_VDO_MUTE_DISABLE,(B_VDO_MUTE_DISABLE|B_TRI_ALL))

#define SwitchHDMIRXBank(x) HDMIRX_WriteI2C_Byte(REG_RX_BANK,(x)&1)


char _CODE * VStateStr[] = {
    "VSTATE_Off",
    "VSTATE_PwrOff",
    "VSTATE_SyncWait ",
    "VSTATE_SWReset",
    "VSTATE_SyncChecking",
    "VSTATE_HDCPSet",
    "VSTATE_HDCP_Reset",
    "VSTATE_ModeDetecting",
    "VSTATE_VideoOn",
    "VSTATE_ColorDetectReset",
    "VSTATE_Reserved"
} ;

char _CODE *AStateStr[] = {
    "ASTATE_AudioOff",
    "ASTATE_RequestAudio",
    "ASTATE_ResetAudio",
    "ASTATE_WaitForReady",
    "ASTATE_AudioOn",
    "ASTATE_Reserved"
};


#if defined(OUTPUT_CCIR656)
    #pragma message("OUTPUT_CCIR656 defined")
    #define HDMIRX_OUTPUT_MAPPING (B_OUTPUT_16BIT)
    #define HDMIRX_OUTPUT_TYPE (B_SYNC_EMBEDDED|B_CCIR656)
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_YUV422<<O_OUTPUT_COLOR_MODE)
#elif defined(OUTPUT_SYNC_EMBEDDED)
    #pragma message("OUTPUT_SYNC_EMBEDDED defined")
    #define HDMIRX_OUTPUT_MAPPING (B_OUTPUT_16BIT)
    #define HDMIRX_OUTPUT_TYPE (B_SYNC_EMBEDDED)
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_YUV422<<O_OUTPUT_COLOR_MODE)
#elif defined(OUTPUT_16BIT_YUV422)
    #pragma message("OUTPUT_16BIT_YUV422 defined")
    #define HDMIRX_OUTPUT_MAPPING (B_OUTPUT_16BIT)
    #define HDMIRX_OUTPUT_TYPE 0
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_YUV422<<O_OUTPUT_COLOR_MODE)
#elif defined(OUTPUT_24BIT_YUV422)
    #pragma message("OUTPUT_24BIT_YUV422 defined")
    #define HDMIRX_OUTPUT_MAPPING   0
    #define HDMIRX_OUTPUT_TYPE  0
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_YUV422<<O_OUTPUT_COLOR_MODE)
#elif defined(OUTPUT_24BIT_YUV444)
    #pragma message("OUTPUT_24BIT_YUV444 defined")
    #define HDMIRX_OUTPUT_MAPPING   0
    #define HDMIRX_OUTPUT_TYPE  0
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_YUV444<<O_OUTPUT_COLOR_MODE)
#else // if defined(OUTPUT_24BIT_RGB444)
    #pragma message("OUTPUT_24BIT_RGB444 defined")
    #define HDMIRX_OUTPUT_MAPPING   0
    #define HDMIRX_OUTPUT_TYPE  0
    #define HDMIRX_OUTPUT_COLORMODE (B_OUTPUT_RGB24<<O_OUTPUT_COLOR_MODE)
#endif
// 2009/02/10 added by Jau-Chih.Tseng@ite.com.tw
#define DEFAULT_START_FIXED_AUD_SAMPLEFREQ AUDFS_192KHz
//~Jau-Chih.Tseng@ite.com.tw 2009/02/10

#ifndef I2S_DSP_SETTING
#pragma message ("I2S_DSP_SETTING as 0x60 for 0T delay, 32bit I2S left justify audio.")
#define I2S_DSP_SETTING 0x60
#else
#pragma message ("predefined I2S_DSP_SETTING ....")
#endif

// 2009/11/04  modified by jau-chih.tseng@ite.com.tw
// marked for moving into typedef.h
// typedef struct {
//     WORD HActive ;
//     WORD VActive ;
//     WORD HTotal ;
//     WORD VTotal ;
//     LONG PCLK ;
//     BYTE xCnt ;
//     WORD HFrontPorch ;
//     WORD HSyncWidth ;
//     WORD HBackPorch ;
//     BYTE VFrontPorch ;
//     BYTE VSyncWidth ;
//     BYTE VBackPorch ;
//     BYTE ScanMode:1 ;
//     BYTE VPolarity:1 ;
//     BYTE HPolarity:1 ;
// } HDMI_VTiming ;
//
// #define PROG 1
// #define INTERLACE 0
// #define Vneg 0
// #define Hneg 0
// #define Vpos 1
// #define Hpos 1
//~jau-chih.tseng@ite.com.tw 2009/11/04
///////////////////////////////////////////////////////////
// Public Data
///////////////////////////////////////////////////////////
BYTE	ucDVISCDToffCNT=0;		// 20091021 for VG-859 HDMI / DVI change issue
_IDATA Video_State_Type VState = VSTATE_PwrOff ;
_IDATA Audio_State_Type AState = ASTATE_AudioOff ;

///////////////////////////////////////////////////////////
// Global Data
///////////////////////////////////////////////////////////
static _IDATA USHORT VideoCountingTimer = 0 ;
static _IDATA USHORT AudioCountingTimer = 0 ;
static _IDATA USHORT MuteResumingTimer = 0 ;
static BOOL MuteAutoOff = FALSE ;
static _IDATA BYTE bGetSyncFailCount = 0 ;
static BYTE _IDATA bOutputVideoMode = F_MODE_EN_UDFILT | F_MODE_RGB24 ;
static BOOL EnaSWCDRRest = FALSE ;
BYTE _XDATA bDisableAutoAVMute = 0 ;
static BYTE ucRevision = 0xFF ;
BYTE _XDATA bHDCPMode = 0 ;


//2008/06/17 modified by jj_tseng@chipadvanced.com
#ifndef LOOP_MSEC
#define LOOP_MSEC 32
#else
	#pragma message ("LOOP_MSEC defined.")
#endif

#define MS_TimeOut(x)      (((x)+LOOP_MSEC-1)/LOOP_MSEC)
// #define VSTATE_MISS_SYNC_COUNT MS_TimeOut(2000)// 2000ms, 2sec
#define VSTATE_MISS_SYNC_COUNT MS_TimeOut(15000)// 8000ms, 8sec
#define VSATE_CONFIRM_SCDT_COUNT MS_TimeOut(150)// 150ms // direct change into 8 times of loop.
#define AUDIO_READY_TIMEOUT MS_TimeOut(200)
#define AUDIO_CLEARERROR_TIMEOUT MS_TimeOut(1000)
#define MUTE_RESUMING_TIMEOUT MS_TimeOut(2500)// 2.5 sec
#define HDCP_WAITING_TIMEOUT MS_TimeOut(3000)// 3 sec
#define CDRRESET_TIMEOUT MS_TimeOut(3000)// 3 sec
#define VSTATE_SWRESET_COUNT MS_TimeOut(500)// 500ms
#define FORCE_SWRESET_TIMEOUT  MS_TimeOut(16000)// 15000ms, 15sec
#define VIDEO_TIMER_CHECK_COUNT MS_TimeOut(1000)

#define SCDT_LOST_TIMEOUT  15
static _XDATA USHORT SWResetTimeOut = FORCE_SWRESET_TIMEOUT;

static _XDATA BYTE ucHDMIAudioErrorCount = 0 ;
// 2009/02/10 modified by Jau-Chih.Tseng@ite.com.tw
// static _XDATA BYTE ucAudioSampleClock = AUDFS_192KHz ; // 192KHz, to changed 48KHz
static _XDATA BYTE ucAudioSampleClock = DEFAULT_START_FIXED_AUD_SAMPLEFREQ ; // 192KHz, to changed 48KHz
//~Jau-Chih.Tseng@ite.com.tw 2009/02/10

BOOL bIntPOL = FALSE ;
static BOOL NewAVIInfoFrameF = FALSE ;
static BOOL MuteByPKG = OFF ;
static _XDATA BYTE bInputVideoMode ;

// 2006/12/04 added by jj_tseng@chipadvanced.com
static _XDATA BYTE prevAVIDB1 = 0 ;
static _XDATA BYTE prevAVIDB2 = 0 ;
//~jjtseng 2006/12/04

static BOOL bVSIpresent=FALSE ;
static _XDATA USHORT currHTotal ;
static _XDATA BYTE currXcnt ;
static BOOL currScanMode ;
static BOOL bGetSyncInfo();
// BYTE iVTimingIndex = 0xFF ;

// 2011/10/17 added by jau-chih.tseng@ite.com.tw
#ifndef DISABLE_COLOR_DEPTH_RESET
static BOOL bDisableColorDepthResetState = FALSE ;
#endif
//~jau-chih.tseng@ite.com.tw 2011/10/17

// 2011/06/15 added by jau-chih.tseng@ite.com.tw
static BOOL bIgnoreVideoChgEvent=FALSE ;
//~jau-chih.tseng@ite.com.tw

static _XDATA AUDIO_CAPS AudioCaps ;

// 2009/11/04 removed by jau-chih.tseng@ite.com.tw
// to avoid the wrong space using on 8051 code.
// _XDATA HDMI_VTiming code *pVTiming ;
_XDATA HDMI_VTiming s_CurrentVM ;
//~jau-chih.tseng@ite.com.tw 2009/11/04

BYTE bVideoOutputOption = VIDEO_AUTO ;

BYTE SCDTErrorCnt = 0;

///////////////////////////////////////////////////////////////////////
// Global Table
///////////////////////////////////////////////////////////////////////
#ifdef USE_MODE_TABLE
static HDMI_VTiming _CODE s_VMTable[] = {
    {640,480,800,525,25175L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},    //640x480@60Hz
    {720,480,858,525,27000L,0x80,16,62,60,9,6,30,PROG,Vneg,Hneg},    //720x480@60Hz
    {1280,720,1650,750,74000L,0x2E,110,40,220,5,5,20,PROG,Vpos,Hpos},    //1280x720@60Hz
    {1920,540,2200,562,74000L,0x2E,88,44,148,2,5,15,INTERLACE,Vpos,Hpos},    //1920x1080(I)@60Hz
    {720,240,858,262,13500L,0xFF,19,62,57,4,3,15,INTERLACE,Vneg,Hneg},    //720x480(I)@60Hz
    {720,240,858,262,13500L,0xFF,19,62,57,4,3,15,PROG,Vneg,Hneg},    //720x480(I)@60Hz
    {1440,240,1716,262,27000L,0x80,38,124,114,5,3,15,INTERLACE,Vneg,Hneg},    //1440x480(I)@60Hz
    {1440,240,1716,263,27000L,0x80,38,124,114,5,3,15,PROG,Vneg,Hneg},    //1440x240@60Hz
    {2880,240,3432,262,54000L,0x40,76,248,288,4,3,15,INTERLACE,Vneg,Hneg},    //2880x480(I)@60Hz
    {2880,240,3432,262,54000L,0x40,76,248,288,4,3,15,PROG,Vneg,Hneg},    //2880x240@60Hz
    {2880,240,3432,263,54000L,0x40,76,248,288,5,3,15,PROG,Vneg,Hneg},    //2880x240@60Hz
    {1440,480,1716,525,54000L,0x40,32,124,120,9,6,30,PROG,Vneg,Hneg},    //1440x480@60Hz
    {1920,1080,2200,1125,148352L,0x17,88,44,148,4,5,36,PROG,Vpos,Hpos},    //1920x1080@60Hz
    {720,576,864,625,27000L,0x80,12,64,68,5,5,36,PROG,Vneg,Hneg},    //720x576@50Hz
    {1280,720,1980,750,74000L,0x2E,440,40,220,5,5,20,PROG,Vpos,Hpos},    //1280x720@50Hz
    {1920,540,2640,562,74000L,0x2E,528,44,148,2,5,15,INTERLACE,Vpos,Hpos},    //1920x1080(I)@50Hz
    {1440/2,288,1728/2,312,13500L,0xFF,24/2,126/2,138/2,2,3,19,INTERLACE,Vneg,Hneg},    //1440x576(I)@50Hz
    {1440,288,1728,312,27000L,0x80,24,126,138,2,3,19,INTERLACE,Vneg,Hneg},    //1440x576(I)@50Hz
    {1440/2,288,1728/2,312,13500L,0xFF,24/2,126/2,138/2,2,3,19,PROG,Vneg,Hneg},    //1440x288@50Hz
    {1440,288,1728,313,27000L,0x80,24,126,138,3,3,19,PROG,Vneg,Hneg},    //1440x288@50Hz
    {1440,288,1728,314,27000L,0x80,24,126,138,4,3,19,PROG,Vneg,Hneg},    //1440x288@50Hz
    {2880,288,3456,312,54000L,0x40,48,252,276,2,3,19,INTERLACE,Vneg,Hneg},    //2880x576(I)@50Hz
    {2880,288,3456,312,54000L,0x40,48,252,276,2,3,19,PROG,Vneg,Hneg},    //2880x288@50Hz
    {2880,288,3456,313,54000L,0x40,48,252,276,3,3,19,PROG,Vneg,Hneg},    //2880x288@50Hz
    {2880,288,3456,314,54000L,0x40,48,252,276,4,3,19,PROG,Vneg,Hneg},    //2880x288@50Hz
    {1440,576,1728,625,54000L,0x40,24,128,136,5,5,39,PROG,Vpos,Hneg},    //1440x576@50Hz
    {1920,1080,2640,1125,148000L,0x17,528,44,148,4,5,36,PROG,Vpos,Hpos},    //1920x1080@50Hz
    {1920,1080,2750,1125,74000L,0x2E,638,44,148,4,5,36,PROG,Vpos,Hpos},    //1920x1080@24Hz
    {1920,1080,2640,1125,74000L,0x2E,528,44,148,4,5,36,PROG,Vpos,Hpos},    //1920x1080@25Hz
    {1920,1080,2200,1125,74000L,0x2E,88,44,148,4,5,36,PROG,Vpos,Hpos},    //1920x1080@30Hz
    // VESA mode
    {640,350,832,445,31500L,0x6D,32,64,96,32,3,60,PROG,Vneg,Hpos},         // 640x350@85
    {640,400,832,445,31500L,0x6D,32,64,96,1,3,41,PROG,Vneg,Hneg},          // 640x400@85
    {832,624,1152,667,57283L,0x3C,32,64,224,1,3,39,PROG,Vneg,Hneg},        // 832x624@75Hz
    {720,350,900,449,28322L,0x7A,18,108,54,59,2,38,PROG,Vneg,Hneg},        // 720x350@70Hz
    {720,400,900,449,28322L,0x7A,18,108,54,13,2,34,PROG,Vpos,Hneg},        // 720x400@70Hz
    {720,400,936,446,35500L,0x61,36,72,108,1,3,42,PROG,Vpos,Hneg},         // 720x400@85
    {640,480,800,525,25175L,0x89,16,96,48,10,2,33,PROG,Vneg,Hneg},         // 640x480@60
    {640,480,832,520,31500L,0x6D,24,40,128,9,3,28,PROG,Vneg,Hneg},         // 640x480@72
    {640,480,840,500,31500L,0x6D,16,64,120,1,3,16,PROG,Vneg,Hneg},         // 640x480@75
    {640,480,832,509,36000L,0x60,56,56,80,1,3,25,PROG,Vneg,Hneg},          // 640x480@85
    {800,600,1024,625,36000L,0x60,24,72,128,1,2,22,PROG,Vpos,Hpos},        // 800x600@56
    {800,600,1056,628,40000L,0x56,40,128,88,1,4,23,PROG,Vpos,Hpos},        // 800x600@60
    {800,600,1040,666,50000L,0x45,56,120,64,37,6,23,PROG,Vpos,Hpos},       // 800x600@72
    {800,600,1056,625,49500L,0x45,16,80,160,1,3,21,PROG,Vpos,Hpos},        // 800x600@75
    {800,600,1048,631,56250L,0x3D,32,64,152,1,3,27,PROG,Vpos,Hpos},        // 800X600@85
    {848,480,1088,517,33750L,0x66,16,112,112,6,8,23,PROG,Vpos,Hpos},       // 840X480@60
    {1024,384,1264,408,44900L,0x4d,8,176,56,0,4,20,INTERLACE,Vpos,Hpos},    //1024x768(I)@87Hz
    {1024,768,1344,806,65000L,0x35,24,136,160,3,6,29,PROG,Vneg,Hneg},      // 1024x768@60
    {1024,768,1328,806,75000L,0x2E,24,136,144,3,6,29,PROG,Vneg,Hneg},      // 1024x768@70
    {1024,768,1312,800,78750L,0x2B,16,96,176,1,3,28,PROG,Vpos,Hpos},       // 1024x768@75
    {1024,768,1376,808,94500L,0x24,48,96,208,1,3,36,PROG,Vpos,Hpos},       // 1024x768@85
    {1152,864,1600,900,108000L,0x20,64,128,256,1,3,32,PROG,Vpos,Hpos},     // 1152x864@75


    {1280,768,1440,790,68250L,0x32,48,32,80,3,7,12,PROG,Vneg,Hpos},        // 1280x768@60-R
    {1280,768,1664,798,79500L,0x2B,64,128,192,3,7,20,PROG,Vpos,Hneg},      // 1280x768@60
    {1280,768,1696,805,102250L,0x21,80,128,208,3,7,27,PROG,Vpos,Hneg},     // 1280x768@75
    {1280,768,1712,809,117500L,0x1D,80,136,216,3,7,31,PROG,Vpos,Hneg},     // 1280x768@85

    {1280,800,1440,823,71000L,0x31,48,32,80,3,6,14,PROG,Vneg,Hpos}, // 1280x800@60-R HReq = 49.306KHz
    {1280,800,1680,831,83500L,0x2A,72,128,200,3,6,22,PROG,Vpos,Hneg},//1280x800@60, HReq = 49.702KHz


    {1280,960,1800,1000,108000L,0x20,96,112,312,1,3,36,PROG,Vpos,Hpos},    // 1280x960@60
    {1280,960,1728,1011,148500L,0x17,64,160,224,1,3,47,PROG,Vpos,Hpos},    // 1280x960@85
    {1280,1024,1688,1066,108000L,0x20,48,112,248,1,3,38,PROG,Vpos,Hpos},   // 1280x1024@60
    {1280,1024,1688,1066,135000L,0x19,16,144,248,1,3,38,PROG,Vpos,Hpos},   // 1280x1024@75
    {1280,1024,1728,1072,157500L,0x15,64,160,224,1,3,44,PROG,Vpos,Hpos},   // 1280X1024@85
    {1360,768,1792,795,85500L,0x28,64,112,256,3,6,18,PROG,Vpos,Hpos},      // 1360X768@60
    {1366,768,1500,800, 72000L, 0x30, 14,56,64,1,3,28,PROG,Vpos,Hpos} , // 1366x768@60-R, HReq = 48KHz
    {1366,768,1792,798, 85500L, 0x29, 70, 143, 213,3,3,24,PROG,Vpos,Hpos} , // 1366x768@60, HReq = 47.712KHz

    {1400,1050,1560,1080,101000L,0x22,48,32,80,3,4,23,PROG,Vneg,Hpos},     // 1400x768@60-R
    {1400,1050,1864,1089,121750L,0x1C,88,144,232,3,4,32,PROG,Vpos,Hneg},   // 1400x768@60
    {1400,1050,1896,1099,156000L,0x16,104,144,248,3,4,42,PROG,Vpos,Hneg},  // 1400x1050@75
    {1400,1050,1912,1105,179500L,0x13,104,152,256,3,4,48,PROG,Vpos,Hneg},  // 1400x1050@85
    {1440,900,1600,926,88750L,0x26,48,32,80,3,6,17,PROG,Vneg,Hpos},        // 1440x900@60-R
    {1440,900,1904,934,106500L,0x20,80,152,232,3,6,25,PROG,Vpos,Hneg},     // 1440x900@60
    {1440,900,1936,942,136750L,0x19,96,152,248,3,6,33,PROG,Vpos,Hneg},     // 1440x900@75
    {1440,900,1952,948,157000L,0x16,104,152,256,3,6,39,PROG,Vpos,Hneg},    // 1440x900@85
    {1600,1200,2160,1250,162000L,0x15,64,192,304,1,3,46,PROG,Vpos,Hpos},   // 1600x1200@60
    {1600,1200,2160,1250,175500L,0x13,64,192,304,1,3,46,PROG,Vpos,Hpos},   // 1600x1200@65
    {1600,1200,2160,1250,189000L,0x12,64,192,304,1,3,46,PROG,Vpos,Hpos},   // 1600x1200@70
    {1600,1200,2160,1250,202500L,0x11,64,192,304,1,3,46,PROG,Vpos,Hpos},   // 1600x1200@75
    {1600,1200,2160,1250,229500L,0x0F,64,192,304,1,3,46,PROG,Vpos,Hpos},   // 1600x1200@85
    {1680,1050,1840,1080,119000L,0x1D,48,32,80,3,6,21,PROG,Vneg,Hpos},     // 1680x1050@60-R
    {1680,1050,2240,1089,146250L,0x17,104,176,280,3,6,30,PROG,Vpos,Hneg},  // 1680x1050@60
    {1680,1050,2272,1099,187000L,0x12,120,176,296,3,6,40,PROG,Vpos,Hneg},  // 1680x1050@75
    {1680,1050,2288,1105,214750L,0x10,128,176,304,3,6,46,PROG,Vpos,Hneg},  // 1680x1050@85
    {1792,1344,2448,1394,204750L,0x10,128,200,328,1,3,46,PROG,Vpos,Hneg},  // 1792x1344@60
    {1792,1344,2456,1417,261000L,0x0D,96,216,352,1,3,69,PROG,Vpos,Hneg},   // 1792x1344@75
    {1856,1392,2528,1439,218250L,0x0F,96,224,352,1,3,43,PROG,Vpos,Hneg},   // 1856x1392@60
    {1856,1392,2560,1500,288000L,0x0C,128,224,352,1,3,104,PROG,Vpos,Hneg}, // 1856x1392@75
    {1920,1200,2080,1235,154000L,0x16,48,32,80,3,6,26,PROG,Vneg,Hpos},     // 1920x1200@60-R
    {1920,1200,2592,1245,193250L,0x11,136,200,336,3,6,36,PROG,Vpos,Hneg},  // 1920x1200@60
    {1920,1200,2608,1255,245250L,0x0E,136,208,344,3,6,46,PROG,Vpos,Hneg},  // 1920x1200@75
    {1920,1200,2624,1262,281250L,0x0C,144,208,352,3,6,53,PROG,Vpos,Hneg},  // 1920x1200@85
    {1920,1440,2600,1500,234000L,0x0E,128,208,344,1,3,56,PROG,Vpos,Hneg},  // 1920x1440@60
    {1920,1440,2640,1500,297000L,0x0B,144,224,352,1,3,56,PROG,Vpos,Hneg},  // 1920x1440@75
};

#define     SizeofVMTable(sizeof(s_VMTable)/sizeof(HDMI_VTiming))
#else
#define     SizeofVMTable    0
#endif

extern _CODE BYTE bCSCOffset_16_235[] ;
extern _CODE BYTE bCSCOffset_0_255[] ;
#if (defined(SUPPORT_OUTPUTYUV))&&(defined(SUPPORT_INPUTRGB))
    extern _CODE BYTE bCSCMtx_RGB2YUV_ITU601_16_235[] ;
    extern _CODE BYTE bCSCMtx_RGB2YUV_ITU601_0_255[] ;
    extern _CODE BYTE bCSCMtx_RGB2YUV_ITU709_16_235[] ;
    extern _CODE BYTE bCSCMtx_RGB2YUV_ITU709_0_255[] ;
#endif

#if (defined(SUPPORT_OUTPUTRGB))&&(defined(SUPPORT_INPUTYUV))
    extern _CODE BYTE bCSCMtx_YUV2RGB_ITU601_16_235[] ;
    extern _CODE BYTE bCSCMtx_YUV2RGB_ITU601_0_255[] ;
    extern _CODE BYTE bCSCMtx_YUV2RGB_ITU709_16_235[] ;
    extern _CODE BYTE bCSCMtx_YUV2RGB_ITU709_0_255[] ;

#endif


static BYTE ucCurrentHDMIPort = 0 ;
static BOOL AcceptCDRReset = TRUE ;

#ifdef AUTO_SEARCH_EQ_SETTING
static _CODE BYTE EQValue[] = {0x87, 0x81, 0x80} ;
#define MAX_EQ_IDX (sizeof(EQValue)-1)
BYTE minInitEQIdx = 0 ; // 1 ; // 2 ; // for the minimal starting search.
BYTE initEQTestIdx = 0 ;
BYTE EccErrorCounter = 0 ;
BYTE SyncDetectFailCounter = 0 ;
BYTE SyncWaitCounter = 0;
BYTE SyncCheckCounter = 0;
USHORT VideoOnTick = 0;
static USHORT EQSum[]={0,0,0};
static USHORT gTestRep = 1;
static USHORT gEqInc = 0;
BYTE gAutoEQTestReset = 0;
BYTE eqTest = 0;
BOOL EnableAutoEQ = TRUE ;
static void AutoAdjustEQ();
static BOOL IncreaseEQ();
#endif

///////////////////////////////////////////////////////////
// Function Prototype
///////////////////////////////////////////////////////////
BOOL CheckHDMIRX(void);

void DumpHDMIRX(void);
// void DumpSync(PSYNC_INFO pSyncInfo);
// void GetSyncInfo(PSYNC_INFO pSyncInfo);
// static BOOL CheckOutOfRange(PSYNC_INFO pSyncInfo);
// BOOL ValidateMode(PSYNC_INFO pSyncInfo);
void Interrupt_Handler(void);
void Timer_Handler(void);
void Video_Handler(void);

static void HWReset_HDMIRX(void);
static void SWReset_HDMIRX(void);
// void Terminator_Reset();
void setHDMIRX_TerminatorOff(void);
void setHDMIRX_TerminatorOn(void);
void Terminator_Off(void);
void Terminator_On(void);

void Check_RDROM(void);
void RDROM_Reset(void);
void SetDefaultRegisterValue(void);
// static void LoadDefaultSyncPolarity();
// static void LoadDefaultHWMuteControl();
// static void LoadDefaultHWAmpControl();
// static void LoadDefaultAudioOutputMap();
// static void LoadDefaultVideoOutput();
// static void LoadDefaultInterruptType();
// static void LoadDefaultAudioSampleClock();
// static void LoadDefaultROMSetting();
static void LoadCustomizeDefaultSetting(void);
static void SetupAudio(void);

BOOL ReadRXIntPin(void);
// USHORT GetVFreq();
static void ClearIntFlags(BYTE flag);
static void ClearHDCPIntFlags(void);
BOOL IsSCDT(void);
BOOL CheckPlg5VPwr(void);
// BOOL CheckPlg5VPwrOn();
// BOOL CheckPlg5VPwrOff();
BOOL CheckHDCPFail(void);
void SetMUTE(BYTE AndMask, BYTE OrMask);
void SetMCLKInOUt(BYTE MCLKSelect);
void SetIntMask1(BYTE AndMask,BYTE OrMask);
void SetIntMask2(BYTE AndMask,BYTE OrMask);
void SetIntMask3(BYTE AndMask,BYTE OrMask);
void SetIntMask4(BYTE AndMask,BYTE OrMask);
void SetGeneralPktType(BYTE type);
BOOL IsHDMIRXHDMIMode(void);
static void EnableAudio(void);

///////////////////////////////////////////////////////////
// Audio Macro
///////////////////////////////////////////////////////////

#define SetForceHWMute(){ SetHWMuteCTRL((~B_HW_FORCE_MUTE),(B_HW_FORCE_MUTE)); }
#define SetHWMuteClrMode(){ SetHWMuteCTRL((~B_HW_AUDMUTE_CLR_MODE),(B_HW_AUDMUTE_CLR_MODE));}
#define SetHWMuteClr(){ SetHWMuteCTRL((~B_HW_MUTE_CLR),(B_HW_MUTE_CLR)); }
#define SetHWMuteEnable(){ SetHWMuteCTRL((~B_HW_MUTE_EN),(B_HW_MUTE_EN)); }
#define ClearForceHWMute(){ SetHWMuteCTRL((~B_HW_FORCE_MUTE),0); }
#define ClearHWMuteClrMode(){ SetHWMuteCTRL((~B_HW_AUDMUTE_CLR_MODE),0); }
#define ClearHWMuteClr(){ SetHWMuteCTRL((~B_HW_MUTE_CLR),0); }
#define ClearHWMuteEnable(){ SetHWMuteCTRL((~B_HW_MUTE_EN),0);}
///////////////////////////////////////////////////////////
// Function Prototype
///////////////////////////////////////////////////////////
void RXINT_5V_PwrOn(void);
void RXINT_5V_PwrOff(void);
void RXINT_SCDT_On(void);
void RXINT_SCDT_Off(void);
void RXINT_RXCKON(void);
void RXINT_VideoMode_Chg(void);
void RXINT_HDMIMode_Chg(void);
void RXINT_AVMute_Set(void);
void RXINT_AVMute_Clear(void);
void RXINT_SetNewAVIInfo(void);
void RXINT_ResetAudio(void);
void RXINT_ResetHDCP(void);
void TimerServiceISR(void);
static void VideoTimerHandler(void);
static void AudioTimerHandler(void);
static void MuteProcessTimerHandler(void);

void AssignVideoTimerTimeout(USHORT TimeOut);
void ResetVideoTimerTimeout(void);
void SwitchVideoState(Video_State_Type state);

void AssignAudioTimerTimeout(USHORT TimeOut);
void ResetAudioTimerTimeout(void);
void SwitchAudioState(Audio_State_Type state);
#define EnableMuteProcessTimer(){ MuteResumingTimer = MuteByPKG?MUTE_RESUMING_TIMEOUT:0 ; }
#define DisableMuteProcessTimer(){ MuteResumingTimer = 0 ; }

static void DumpSyncInfo(HDMI_VTiming *pVTiming);
#define StartAutoMuteOffTimer(){ MuteAutoOff = ON ; }
#define EndAutoMuteOffTimer(){ MuteAutoOff = OFF ; }
static void CDR_Reset(void);
//static void Reset_SCDTOFF(void);


static void SetVideoInputFormatWithoutInfoFrame(BYTE bInMode);
static void SetColorimetryByMode(void/* PSYNC_INFO pSyncInfo */);
void SetVideoInputFormatWithInfoFrame(void);
BOOL SetColorimetryByInfoFrame(void);
void SetColorSpaceConvert(void);
// BOOL CompareSyncInfo(PSYNC_INFO pSyncInfo1,PSYNC_INFO pSyncInfo2);
// void HDCP_Reset();
void SetDVIVideoOutput(void);
void SetNewInfoVideoOutput(void);
void ResetAudio(void);
void SetHWMuteCTRL(BYTE AndMask, BYTE OrMask);
void SetAudioMute(BOOL bMute);
static void SetVideoMute(BOOL bMute);
static void SetALLMute(void) ;
// void DelayUS(ULONG us);
//#ifndef _MCU_8051_
//void delay1ms(USHORT ms);
//void ErrorF(char *fmt,...);
//#endif

///////////////////////////////////////////////////////////
// Connection Interface
///////////////////////////////////////////////////////////
void
Check_HDMInterrupt()
{
	Interrupt_Handler();
}



BOOL
CheckHDMIRX()
{
    Timer_Handler();
    Video_Handler();

    if(VState == VSTATE_VideoOn &&(!MuteByPKG))
    {
        return TRUE ;
    }

    return FALSE ;
}

void
SelectHDMIPort(BYTE ucPort)
{

    if(ucPort != CAT_HDMI_PORTA)
    {
        ucPort = CAT_HDMI_PORTB ;
    }

    if(ucPort != ucCurrentHDMIPort)
    {
        ucCurrentHDMIPort = ucPort ;
    }

    HDMIRX_DEBUG_PRINTF(("SelectHDMIPort ucPort = %d, ucCurrentHDMIPort = %d\n",(int)ucPort, (int)ucCurrentHDMIPort));
	// switch HDMI port should
	// 1. power down HDMI
	// 2. Select HDMI Port
	// 3. call InitCAT6011();
}

BYTE
GetCurrentHDMIPort()
{
	return ucCurrentHDMIPort ;
}


void
InitHDMIRX(BOOL bFullInit)
{
    BYTE uc ;

    #ifndef DISABLE_HWRESET
    if(bFullInit)
    {
        HWReset_HDMIRX();
    }
    #endif
    Terminator_Off();
    //////////////////////////////////////////////
    // Initialize HDMIRX chip uc.
    //////////////////////////////////////////////

    HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL0, 0);

    #ifndef DISABLE_RESET_REFCLK
    if(bFullInit)
    {
        // this reset will activate the I2C no ACK in this call.
        HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_REGRST); // register reset
    }
    #endif
    // delay1ms(1); // wait for B_REGRST down to zero

    // uc = HDMIRX_ReadI2C_Byte(REG_RX_HDCP_CTRL);

    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_SWRST|B_CDRRST|B_EN_AUTOVDORST); // sw reset
    // delay1ms(1);
    ucRevision = HDMIRX_ReadI2C_Byte(0x04);

    //// uc = 0x89 ; // for external ROM
    //uc = B_EXTROM | B_HDCP_ROMDISWR | B_HDCP_EN ;
    //HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, uc);

	if(ucCurrentHDMIPort==CAT_HDMI_PORTA)
	{
		uc = B_PORT_SEL_A|B_PWD_AFEALL|B_PWDC_ETC ;
	}
	else
	{
		uc = B_PORT_SEL_B|B_PWD_AFEALL|B_PWDC_ETC ;
	}
	HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL1, uc);
    HDMIRX_DEBUG_PRINTF(("InitHDMIRX(FALSE): reg07 = %02X, ucCurrentHDMIPort = %d\n",(int)HDMIRX_ReadI2C_Byte(07),(int)ucCurrentHDMIPort));

    SetIntMask1(0,B_PWR5VON|B_SCDTON|B_PWR5VOFF|B_SCDTOFF);
    SetIntMask2(0,B_NEW_AVI_PKG|B_PKT_SET_MUTE|B_PKT_CLR_MUTE);
    SetIntMask3(0,B_ECCERR|B_R_AUTH_DONE|B_R_AUTH_START);
    SetIntMask4(0,0) ; // B_M_RXCKON_DET);


    SetDefaultRegisterValue();
    LoadCustomizeDefaultSetting();

    SetALLMute(); // MUTE ALL with tristate video, SPDIF and all I2S channel

    // 2006/10/31 marked by jjtseng
    // HDMIRX_WriteI2C_Byte(REG_RX_REGPKTFLAG_CTRL,B_INT_EVERYAVI);
    //~jjtseng 2006/10/31
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST); // normal operation
    bDisableAutoAVMute = FALSE ;
    if(ucRevision == 0xA2)
    {
        HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, 0x09);
        HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, 0x19);
    }

    #ifdef DISABLE_HDCP
        HDMIRX_WriteI2C_Byte(0x0A, 0);
        HDMIRX_WriteI2C_Byte(0x0B, 0);
        HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, 0x89);
        HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, 0x99);
        HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_SWRST|B_HDCPRST|B_EN_AUTOVDORST); // sw reset
        HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST); // sw reset
        HDMIRX_WriteI2C_Byte(REG_RX_HDCP_CTRL, 0x00);
    #endif


    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // DO NOT MOVE THE ACTION LOCATION!!
    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

    if( bFullInit )
    {
        // this delay is the experience by previous project support compatibility issue.
        delay1ms(200); // delay 0.2 sec by TPV project experience.
    }

    RDROM_Reset(); // it should be do SWRESET again AFTER RDROM_Reset().

    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_SWRST|B_EN_AUTOVDORST); // sw reset
    delay1ms(1);
    SetALLMute();
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST); // normal operation

    // Terminator_Reset();
    //
    // // 2006/10/26 modified by jjtseng
    // // SwitchVideoState(VSTATE_SyncWait);
    // SwitchVideoState(VSTATE_PwrOff);
    // //~jjtseng 2006/10/26
    #ifndef MANUAL_TURNON_HDMIRX
    if(bFullInit)
    {
        Terminator_Off();
        SwitchVideoState(VSTATE_SWReset);
    }
    #endif

#ifdef SUPPORT_REPEATER
	RxHDCPRepeaterCapabilityClear(B_ENABLE_FEATURE_1P1|B_ENABLE_FAST);
    if(bHDCPMode & HDCP_REPEATER)
    {
	    RxHDCPRepeaterCapabilitySet(B_ENABLE_REPEATER);
	    RxHDCPRepeaterCapabilityClear(B_KSV_READY);
	}
	else
	{
	    RxHDCPRepeaterCapabilityClear(B_KSV_READY|B_ENABLE_REPEATER);
        SetIntMask3(~(B_R_AUTH_DONE|B_R_AUTH_START),B_ECCERR);
	}
#else
	HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL)&0xF);
#endif // SUPPORT_REPEATER
    AcceptCDRReset = TRUE;

    #ifndef MANUAL_TURNON_HDMIRX
    if(!bFullInit)
    {
        // if switch from power saving, the terminator should be off at first.
        Terminator_On();
        SwitchVideoState(VSTATE_PwrOff);
    }
    #endif

    #ifdef MANUAL_TURNON_HDMIRX
        Terminator_Off();   // turn on the HDMIRX state machine need to call Turn_HDMIRX(ON);
        SwitchVideoState(VSTATE_Off);
    #endif

}

void Turn_HDMIRX(BOOL bEnable)
{
    HDMIRX_DEBUG_PRINTF3(("Turn_HDMIRX(%s)\n",bEnable?"ON":"OFF")) ;
    if( bEnable )
    {
        SWReset_HDMIRX() ;
    }
    else
    {
        Terminator_Off();
        SwitchVideoState(VSTATE_Off);
    }
}

void PowerDownHDMI()
{
	HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL1, B_PWD_AFEALL|B_PWDC_ETC|B_PWDC_SRV|B_EN_AUTOPWD);
	HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL0, B_PWD_ALL);
}

BOOL IsHDMIRXInterlace()
{
    if(HDMIRX_ReadI2C_Byte(REG_RX_VID_MODE)&B_INTERLACE)
    {
        return TRUE ;
    }
    return FALSE ;
}

BYTE
getHDMIRX_InputColor()
{
    BYTE uc ;
    if(IsHDMIRXHDMIMode())
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB1) ;
        switch(uc&0x60)
        {
        case 0x40: return 2 ;
        case 0x20: return 1 ;
        default: return 0 ;
        }
    }
    return 0 ;

}

WORD getHDMIRXHorzTotal()
{
    BYTE uc[2] ;
	WORD hTotal ;

	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_L);
	uc[1] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_H);
	hTotal =(WORD)(uc [1] & M_HTOTAL_H);
	hTotal <<= 8 ;
	hTotal |=(WORD)uc[0] ;

	return hTotal ;
}

WORD getHDMIRXHorzActive()
{
    BYTE uc[3] ;

	WORD hTotal, hActive ;

	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_L);
	uc[1] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_H);
	uc[2] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HACT_L);

	hTotal =(WORD)(uc [1] & M_HTOTAL_H);
	hTotal <<= 8 ;
	hTotal |=(WORD)uc[0] ;

	hActive =(WORD)(uc[1] >> O_HACT_H)& M_HACT_H ;
	hActive <<= 8 ;
	hActive |=(WORD)uc[2] ;

	if((hActive |(1<<11))< hTotal)
	{
		hActive |= 1<<11 ;
	}

	return hActive ;

}

WORD getHDMIRXHorzFrontPorch()
{
    BYTE uc[2] ;
	WORD hFrontPorch ;

	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_H_FT_PORCH_L);
	uc[1] =(HDMIRX_ReadI2C_Byte(REG_RX_VID_HSYNC_WID_H)>> O_H_FT_PORCH)& M_H_FT_PORCH ;
	hFrontPorch =(WORD)uc[1] ;
	hFrontPorch <<= 8 ;
	hFrontPorch |=(WORD)uc[0] ;

	return hFrontPorch ;
}

WORD getHDMIRXHorzSyncWidth()
{
    BYTE uc[2] ;
	WORD hSyncWidth ;

	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HSYNC_WID_L);
	uc[1] = HDMIRX_ReadI2C_Byte(REG_RX_VID_HSYNC_WID_H)& M_HSYNC_WID_H ;

	hSyncWidth =(WORD)uc[1] ;
	hSyncWidth <<= 8 ;
	hSyncWidth |=(WORD)uc[0] ;

	return hSyncWidth ;
}

WORD getHDMIRXHorzBackPorch()
{
	WORD hBackPorch ;

	hBackPorch = getHDMIRXHorzTotal()- getHDMIRXHorzActive()- getHDMIRXHorzFrontPorch()- getHDMIRXHorzSyncWidth();

	return hBackPorch ;
}

WORD getHDMIRXVertTotal()
{
    BYTE uc[3] ;
	WORD vTotal, vActive ;
	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_L);
	uc[1] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_H);
	uc[2] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VACT_L);

	vTotal =(WORD)uc[1] & M_VTOTAL_H ;
	vTotal <<= 8 ;
	vTotal |=(WORD)uc[0] ;

	vActive =(WORD)(uc[1] >> O_VACT_H)& M_VACT_H ;
	vActive |=(WORD)uc[2] ;

	if(vTotal >(vActive |(1<<10)))
	{
		vActive |= 1<<10 ;
	}

	// for vertical front porch bit lost, ...
	#if 0
	if(vActive == 600 && vTotal == 634)
	{
		vTotal = 666 ; // fix the 800x600@72 issue
	}
	#endif

	return vTotal ;
}

WORD getHDMIRXVertActive()
{
    BYTE uc[3] ;
	WORD vTotal, vActive ;
	uc[0] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_L);
	uc[1] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_H);
	uc[2] = HDMIRX_ReadI2C_Byte(REG_RX_VID_VACT_L);

	vTotal =(WORD)uc[1] & M_VTOTAL_H ;
	vTotal <<= 8 ;
	vTotal |=(WORD)uc[0] ;

	vActive =(WORD)(uc[1] >> O_VACT_H)& M_VACT_H ;
	vActive <<= 8 ;
	vActive |=(WORD)uc[2] ;

	if(vTotal >(vActive |(1<<10)))
	{
		vActive |= 1<<10 ;
	}

	return vActive ;
}

WORD getHDMIRXVertFrontPorch()
{
    WORD vFrontPorch ;

	vFrontPorch =(WORD)HDMIRX_ReadI2C_Byte(REG_RX_VID_V_FT_PORCH)& 0xF ;

	if(getHDMIRXVertActive()== 600 && getHDMIRXVertTotal()== 666)
	{
		vFrontPorch |= 0x20 ;
	}

	return vFrontPorch ;

}

WORD getHDMIRXVertSyncToDE()
{
    WORD vSync2DE ;

    vSync2DE =(WORD)HDMIRX_ReadI2C_Byte(REG_RX_VID_VSYNC2DE);
    return vSync2DE ;
}

WORD getHDMIRXVertSyncWidth()
{
    WORD vSync2DE ;
    WORD vTotal, vActive, hActive  ;

    vSync2DE = getHDMIRXVertSyncToDE();
    vTotal = getHDMIRXVertTotal();
    vActive = getHDMIRXVertActive();
    hActive = getHDMIRXHorzActive();
#ifndef HDMIRX_A1
    // estamite value.
    if(vActive < 300)
    {
    	return 3 ;
    }

    if(hActive == 640 && hActive == 480)
    {
    	if(HDMIRX_ReadI2C_Byte(REG_RX_VID_XTALCNT_128PEL)< 0x80)
    	{
    		return 3 ;
    	}

    	return 2;
    }

    return 5 ;
#endif
}

WORD getHDMIRXVertSyncBackPorch()
{
    WORD vBackPorch ;

    vBackPorch = getHDMIRXVertSyncToDE()- getHDMIRXVertSyncWidth();
    return vBackPorch ;
}

BYTE getHDMIRXxCnt()
{
    return HDMIRX_ReadI2C_Byte(REG_RX_VID_XTALCNT_128PEL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BOOL getHDMIRXAudioInfo(BYTE *pbSampleFreq, BYTE *pbValidCh);
// Parameter:	pointer of BYTE pbSampleFreq - return sample freq
// pointer of BYTE pbValidCh - return valid audio channel.
// Return:	FALSE - no valid audio information during DVI mode.
//         TRUE - valid audio information returned.
// Remark:	if pbSampleFreq is not NULL, *pbSampleFreq will be filled in with one of the following values:
//         0 - 44.1KHz
//         2 - 48KHz
//         3 - 32KHz
//         8 - 88.2 KHz
//         10 - 96 KHz
//         12 - 176.4 KHz
//         14 - 192KHz
//         Otherwise - invalid audio frequence.
//         if pbValidCh is not NULL, *pbValidCh will be identified with the bit valie:
//         bit[0] - '0' means audio channel 0 is not valid, '1' means it is valid.
//         bit[1] - '0' means audio channel 1 is not valid, '1' means it is valid.
//         bit[2] - '0' means audio channel 2 is not valid, '1' means it is valid.
//         bit[3] - '0' means audio channel 3 is not valid, '1' means it is valid.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL getHDMIRXAudioInfo(BYTE *pbAudioSampleFreq, BYTE *pbValidCh)
{
#ifndef DISABLE_AUDIO_SUPPORT
    if(IsHDMIRXHDMIMode())
    {
        if(pbAudioSampleFreq)
        {
            *pbAudioSampleFreq = HDMIRX_ReadI2C_Byte(REG_RX_FS)& M_Fs ;
        }

        if(pbValidCh)
        {
            *pbValidCh = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT);
            if(*pbValidCh & B_AUDIO_LAYOUT)
            {
                *pbValidCh &= M_AUDIO_CH ;
            }
            else
            {
                *pbValidCh = B_AUDIO_SRC_VALID_0 ;
            }
        }
        return TRUE ;
    }
    else
#endif
    {
        return FALSE ;
    }
}

///////////////////////////////////////////////////////////
// Get Info Frame and HDMI Package
// Need upper program pass information and read them.
///////////////////////////////////////////////////////////
#ifdef GET_PACKAGE
// 2006/07/03 added by jjtseng
BOOL
GetAVIInfoFrame(BYTE *pData)
{
    // BYTE checksum ;
    // int i ;

    if(pData == NULL)
    {
        return ER_FAIL ;
    }

    pData[0] = AVI_INFOFRAME_TYPE|0x80 ; // AVI InfoFrame
    pData[1] = HDMIRX_ReadI2C_Byte(REG_RX_AVI_VER);
    pData[2] = AVI_INFOFRAME_LEN ;

    HDMIRX_ReadI2C_ByteN(REG_RX_AVI_DB0, pData+3,AVI_INFOFRAME_LEN+1);

    return TRUE ;
}
//~jjtseng 2006/07/03

// 2006/07/03 added by jjtseng
BOOL
GetAudioInfoFrame(BYTE *pData)
{
    // BYTE checksum ;
    BYTE i ;

    if(pData == NULL)
    {
        return FALSE ;
    }

    pData[0] = AUDIO_INFOFRAME_TYPE|0x80 ; // AUDIO InfoFrame
    pData[1] = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_VER);
    pData[2] = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_LEN); ;

    HDMIRX_ReadI2C_ByteN(REG_RX_AUDIO_DB0, pData+3,6);
    for( i = 5 ; i <= pData[2] ; i++ )
    {
        pData[4+i] = 0 ;
    }

    return TRUE ;
}
//~jjtseng 2006/07/03
//
//
// // 2006/07/03 added by jjtseng
// BOOL
// GetMPEGInfoFrame(BYTE *pData)
// {
//     // BYTE checksum ;
//     // int i ;
//
//     if(pData == NULL)
//     {
//         return FALSE ;
//     }
//
//     pData[0] = MPEG_INFOFRAME_TYPE|0x80 ; // MPEG InfoFrame
//     pData[1] = HDMIRX_ReadI2C_Byte(REG_RX_MPEG_VER);
//     pData[2] = MPEG_INFOFRAME_LEN ;
//
//     HDMIRX_ReadI2C_ByteN(REG_RX_MPEG_DB0, pData+3,MPEG_INFOFRAME_LEN+1);
//
//     return TRUE ;
// }
// //~jjtseng 2006/07/03

// 2006/07/03 added by jjtseng
BOOL
GetVENDORSPECInfoFrame(BYTE *pData)
{
    // BYTE checksum ;
    int i ;
    BYTE uc ;

    if(pData == NULL)
    {
        return FALSE ;
    }

	if(bVSIpresent == FALSE)
	{
		return FALSE ;
	}
    uc = HDMIRX_ReadI2C_Byte(REG_RX_GENPKT_HB0);
    if(uc !=(VENDORSPEC_INFOFRAME_TYPE|0x80))
    {
        return FALSE ;
    }
    pData[0] = uc ;
    for(i = 1 ; i < 3+28 ; i++)
    {
        pData[i] = HDMIRX_ReadI2C_Byte(REG_RX_GENPKT_HB0+i);
    }

    return TRUE ;
}
//~jjtseng 2006/07/03



#endif
//~jjtseng 2006/07/03

///////////////////////////////////////////////////////////
//  Testing Function
///////////////////////////////////////////////////////////

void
getHDMIRXRegs(BYTE *pData)
{
    int i, j ;

    SwitchHDMIRXBank(0);
    for(i = j = 0 ; i < 256 ; i++,j++)
    {
        pData[j] = HDMIRX_ReadI2C_Byte((BYTE)(i&0xFF));
    }
    SwitchHDMIRXBank(1);
    for(i = 0xA0 ; i <= 0xF2 ; i++, j++)
    {
        pData[j] = HDMIRX_ReadI2C_Byte((BYTE)(i&0xFF));
    }
    SwitchHDMIRXBank(0);
}

BYTE
getHDMIRXOutputColorMode()
{
    return bOutputVideoMode & F_MODE_CLRMOD_MASK ;
}

BYTE
getHDMIRXOutputColorDepth()
{
    BYTE uc ;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_FS)& M_GCP_CD ;
    return uc >> O_GCP_CD ;
}

// Initialization
///////////////////////////////////////////////////////////

static void
HWReset_HDMIRX()
{
    // reset HW Reset Pin.
#ifdef _MCU_8051_
    // Write HDMIRX pin = 1 ;
#endif
}

#if 0
// void
// setHDMIRX_TerminatorOff()
// {
// 	SwitchVideoState(VSTATE_Off);
// 	Terminator_Off() ;
// }
//
// void
// setHDMIRX_TerminatorOn()
// {
// 	// SwitchVideoState(VSTATE_SWReset);
// 	SWReset_HDMIRX() ;
// }
#endif 

void
Terminator_Off()
{
    BYTE uc ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_PWD_CTRL1)|(B_PWD_AFEALL|B_PWDC_ETC);
    HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL1, uc);
    HDMIRX_DEBUG_PRINTF(("Terminator_Off, reg07 = %02x\n",(int)uc));
}

void
Terminator_On()
{
    BYTE uc ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_PWD_CTRL1)& ~(B_PWD_AFEALL|B_PWDC_ETC);
    HDMIRX_WriteI2C_Byte(REG_RX_PWD_CTRL1, uc);
    HDMIRX_DEBUG_PRINTF(("Terminator_On, reg07 = %02x\n",(int)uc));
}

/*
static void
Terminator_Reset()
{
    Terminator_Off();
    delay1ms(500); // delay 500 ms
    Terminator_On();
}
*/

void
RDROM_Reset()
{
    BYTE i ;
    BYTE uc ;

    HDMIRX_DEBUG_PRINTF(("RDROM_Reset()\n"));
    // uc =((bDisableAutoAVMute)?B_VDO_MUTE_DISABLE:0)|1;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_RDROM_CLKCTRL)& ~(B_ROM_CLK_SEL_REG|B_ROM_CLK_VALUE);
    for(i=0 ;i < 16 ; i++)
    {
        HDMIRX_WriteI2C_Byte(REG_RX_RDROM_CLKCTRL, B_ROM_CLK_SEL_REG|uc);
        HDMIRX_WriteI2C_Byte(REG_RX_RDROM_CLKCTRL, B_ROM_CLK_SEL_REG|B_ROM_CLK_VALUE|uc);
    }
    // 2006/10/31 modified by jjtseng
    // added oring bDisableAutoAVMute
    HDMIRX_WriteI2C_Byte(REG_RX_RDROM_CLKCTRL,uc);
    //~jjtseng 2006/10/31
}

void
Check_RDROM()
{
    BYTE uc ;
    HDMIRX_DEBUG_PRINTF(("Check_HDCP_RDROM()\n"));

    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST);

    if(IsSCDT())
    {
        // int count ;
        // for(count = 0 ;; count++)
		{
            uc = HDMIRX_ReadI2C_Byte(REG_RX_RDROM_STATUS);
            if((uc & 0xF)!= 0x9)
            {
                RDROM_Reset();
            }
            HDMIRX_DEBUG_PRINTF(("Check_HDCP_RDROM()done.\n"));
            return ;
        }
    }
}

static void
SWReset_HDMIRX()
{
    Check_RDROM();
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_SWRST|B_EN_AUTOVDORST); // sw reset
    delay1ms(1);
    SetALLMute();
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST); // normal operation

    // Terminator_Reset();

    // 2006/10/26 modified by jjtseng
    // SwitchVideoState(VSTATE_SyncWait);
    // SwitchVideoState(VSTATE_PwrOff);
    //~jjtseng 2006/10/26

    Terminator_Off();
    SwitchVideoState(VSTATE_SWReset);


    //2008/10/02 modified by hermes
    SCDTErrorCnt = 0;

}

// 2006/10/31 added by jjtseng
// for customized uc
typedef struct _REGPAIR {
    BYTE ucAddr ;
    BYTE ucValue ;
} REGPAIR ;
//~jjtseng 2006/10/31

/////////////////////////////////////////////////////////////////
// Customer Defined uc area.
/////////////////////////////////////////////////////////////////
// 2006/10/31 added by jjtseng
// for customized uc
static REGPAIR _CODE acCustomizeValue[] =
{
    //2009/12/08 added by jau-chih.tseng@ite.com.tw
    // {REG_RX_VCLK_CTRL, 0x20}, // request by Clive for adjusting A2 version.
    {REG_RX_VCLK_CTRL, 0x30}, // request by Clive for IT6603 board .... !@#$!@$#! .
    //~jau-chih.tseng 2009/12/08
    // {REG_RX_I2S_CTRL,0x61},
    {REG_RX_I2S_CTRL,I2S_DSP_SETTING},
    // CCIR656
    {REG_RX_PG_CTRL2,HDMIRX_OUTPUT_COLORMODE},
    {REG_RX_VIDEO_MAP,HDMIRX_OUTPUT_MAPPING},
    {REG_RX_VIDEO_CTRL1,HDMIRX_OUTPUT_TYPE},
    {REG_RX_MCLK_CTRL, 0xC1},
    {0xFF,0xFF}
} ;
// jjtseng 2006/10/31

static void
LoadCustomizeDefaultSetting()
{
    BYTE i, uc ;
    for(i = 0 ; acCustomizeValue[i].ucAddr != 0xFF ; i++)
    {
        HDMIRX_WriteI2C_Byte(acCustomizeValue[i].ucAddr,acCustomizeValue[i].ucValue);
    }

    /*
    uc = HDMIRX_ReadI2C_Byte(REG_RX_PG_CTRL2)& ~(M_OUTPUT_COLOR_MASK<<O_OUTPUT_COLOR_MODE);
    switch(bOutputVideoMode&F_MODE_CLRMOD_MASK)
    {
    case F_MODE_YUV444:
        uc |= B_OUTPUT_YUV444 << O_OUTPUT_COLOR_MODE ;
        break ;
    case F_MODE_YUV422:
        uc |= B_OUTPUT_YUV422 << O_OUTPUT_COLOR_MODE ;
        break ;
    }
    HDMIRX_WriteI2C_Byte(REG_RX_PG_CTRL2, uc);
    */
    bOutputVideoMode&=~F_MODE_CLRMOD_MASK;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_PG_CTRL2)&(M_OUTPUT_COLOR_MASK<<O_OUTPUT_COLOR_MODE);

    switch(uc)
    {
    case(B_OUTPUT_YUV444 << O_OUTPUT_COLOR_MODE): bOutputVideoMode|=F_MODE_YUV444; break ;
    case(B_OUTPUT_YUV422 << O_OUTPUT_COLOR_MODE): bOutputVideoMode|=F_MODE_YUV422; break ;
    case 0: bOutputVideoMode|=F_MODE_RGB444; break ;
    default: bOutputVideoMode|=F_MODE_RGB444; break ;
    }
    bIntPOL =(HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_CTRL)& B_INTPOL)?LO_ACTIVE:HI_ACTIVE ;
}

//////////////////////////////////////////////////
// SetDefaultRegisterValue
// some register value have to be hard coded and
// need to adjust by case. Set here.
//////////////////////////////////////////////////
//  There are some register default setting has changed, please make sure
// when release to customer.
///////////////////////////////////////////////////
// Rev 1.09
///////////////////////////////////////////////////
//reg3B=0x40
//reg68=0x03
//reg69=0x00//HW DEF
//reg6A=0xA8//HW DEF
//reg6B=0x11
//Reg6C=0x00
//reg6D=0x64//HW DEF
//reg6E=0x0C//HW DEF
//Reg93=0x43
//Reg94=0x4F
//Reg95=0x87
//Reg96=0x33
///////////////////////////////////////////////////

// 2006/10/31 added by jjtseng
static REGPAIR _CODE acDefaultValue[] =
{
    {0x0F,0x00},// Reg08
	// 2010/10/13 modified by jau-chih.tseng@ite.com.tw
	// recommand by Clive to modify the driving from 0xAE to 0xCE
    // {REG_RX_VIO_CTRL,0xAE},// Reg08
    {REG_RX_VIO_CTRL,0xCE},// Reg08
	//~jau-chih.tseng@ite.com.tw
    //~jj_tseng@chipadvanced.com
    // 2008/09/25 added by jj_tseng@chiopadvanced.com
    // by IT's command, reg3B should set default value with 0x40
    {REG_RX_DESKEW_CTRL, 0x40},
    //~jj_tseng@chipadvanced.com 2008/09/25
    {REG_RX_PLL_CTRL,0x03},// Reg68=0x03
    // {REG_RX_TERM_CTRL1,0x00},// Reg69=0x00 // HW Default


	{REG_RX_EQUAL_CTRL1,0x11}, 	// reg6B = 0x11
    {REG_RX_EQUAL_CTRL2, 0x00}, // reg6C = 0x00
    // reg6D = HW Default
    // {REG_RX_DES_CTRL1, 0x64},
    // reg6E = HW Default
    // {REG_RX_DES_CTRL2, 0x0C}, // CDR Auto Reset, only CDR

    {0x93,0x43},
    {0x94,0x4F},
    {0x95,0x87},
    {0x96,0x33},

    // {0x9B, 0x01},
//20100928 added by jau-chih.tseng@ite.com.tw
    {0x56, 0x01},
    {0x97, 0x0E},
//~20100928 jau-chih.tseng@ite.com.tw
// 2011/08/10 modified by jau-chih.tseng@ite.com.tw
    {REG_RX_CSC_CTRL,B_VIO_SEL},
    {REG_RX_TRISTATE_CTRL,0x5F}, // set Tri-state
//~jau-chih.tseng@ite.com.tw 2011/08/10
    {0xFF,0xFF}


} ;
//~jjtseng 2006/10/31

void
SetDefaultRegisterValue()
{
    BYTE i ;

    for(i = 0 ; acDefaultValue[i].ucAddr != 0xFF ; i++)
    {
        HDMIRX_WriteI2C_Byte(acDefaultValue[i].ucAddr, acDefaultValue[i].ucValue);
    }

    if(ucRevision >= 0xA3)
    {
        HDMIRX_WriteI2C_Byte(0x9B, 0x01);
    }
}

///////////////////////////////////////////////////////////
// Basic IO
///////////////////////////////////////////////////////////




static void
ClearIntFlags(BYTE flag)
{
    BYTE uc ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_CTRL);
    uc &= FLAG_CLEAR_INT_MASK ;
    uc |= flag ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_CTRL,uc);
    delay1ms(1);
    uc &= FLAG_CLEAR_INT_MASK ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_CTRL,uc);  // write 1, then write 0, the corresponded clear action is activated.
    delay1ms(1);
    // HDMIRX_DEBUG_PRINTF(("ClearIntFlags with %02X\n",(int)uc));
}

static void
ClearHDCPIntFlags()
{
    BYTE uc ;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_CTRL1);
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_CTRL1,(BYTE)B_CLR_HDCP_INT|uc);
    delay1ms(1);
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_CTRL1, uc&((BYTE)~B_CLR_HDCP_INT));
}

///////////////////////////////////////////////////
// IsSCDT()
// return TRUE if SCDT ON
// return FALSE if SCDT OFF
///////////////////////////////////////////////////

BOOL
IsSCDT()
{
    BYTE uc ;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE)&(B_SCDT|B_VCLK_DET/*|B_PWR5V_DET*/);
    return(uc==(B_SCDT|B_VCLK_DET/*|B_PWR5V_DET*/))?TRUE:FALSE ;
}

#if 0
//BOOL
//IsSCDTOn()
//{
//    BYTE bData ;
//
//    bData = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
//    // HDMIRX_DEBUG_PRINTF(("IsSCDTOn(): Int1 = %02X\n",(int)bData));
//
//    return(bData&B_SCDTON)?TRUE:FALSE ;
//}
//
//BOOL
//IsSCDTOff()
//{
//    BYTE bData ;
//
//    bData = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
//    // HDMIRX_DEBUG_PRINTF(("IsSCDTOff(): Int1 = %02X\n",(int)bData));
//    return(bData&B_SCDTOFF)?TRUE:FALSE ;
//}
//
//BOOL
//IsSCDTOnOff()
//{
//    BYTE bData ;
//
//    bData = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
//    return(bData&(B_SCDTOFF|B_SCDTON))?TRUE:FALSE ;
//}
#endif


BOOL
CheckPlg5VPwr()
{
    BYTE uc ;

    // HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1,&uc);
    uc = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);
    // HDMIRX_DEBUG_PRINTF(("CheckPlg5VPwr(): REG_RX_SYS_STATE = %02X %s\n",(int)uc,(uc&B_PWR5V_DET)?"TRUE":"FALSE"));

    if(ucCurrentHDMIPort == CAT_HDMI_PORTB)
    {
        return(uc&B_PWR5V_DET_PORTB)?TRUE:FALSE ;

    }

    return(uc&B_PWR5V_DET_PORTA)?TRUE:FALSE ;
}

//BOOL
//CheckPlg5VPwrOn()
//{
//    BYTE uc ;
//
//    uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
//    // HDMIRX_DEBUG_PRINTF(("CheckPlg5VPwrOn(): REG_RX_INTERRUPT1 = %02X %s\n",(int)uc,(uc&B_PWR5VON)?"TRUE":"FALSE"));
//    return(uc&B_PWR5VON)?TRUE:FALSE ;
//}
//
//BOOL
//CheckPlg5VPwrOff()
//{
//    BYTE uc ;
//
//    uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
//    // HDMIRX_DEBUG_PRINTF(("CheckPlg5VPwrOff(): REG_RX_INTERRUPT1 = %02X %s\n",(int)uc,(uc&B_PWR5VOFF)?"TRUE":"FALSE"));
//    return(uc&B_PWR5VOFF)?TRUE:FALSE ;
//}

#if 0
// BOOL
// CheckHDCPFail()
// {
//     BYTE uc ;
//     uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);
//     //HDMIRX_DEBUG_PRINTF(("CheckHDCPFail, uc = %02X, %s\n",(int)uc,(uc&B_ECCERR)?"TRUE":"FALSE"));
//     return(uc&B_ECCERR)?TRUE:FALSE ;
// }
#endif


void
SetMUTE(BYTE AndMask, BYTE OrMask)
{
    BYTE uc = 0;

    //HDMIRX_DEBUG_PRINTF(("SetMUTE(%02X,%02X)",(int)AndMask,(int)OrMask));

    if(AndMask)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
        //HDMIRX_DEBUG_PRINTF(("%02X ",(int)uc));
    }
    uc &= AndMask ;
    uc |= OrMask ;
    #ifdef HDMI_REPEATER
    #pragma message("HDMI Repeating TTL to next stage, do not gatting the video sync.")
    uc &= 0x1F ;
    uc |= 0x80 ;
    #endif
    HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL,uc);
    uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
    //HDMIRX_DEBUG_PRINTF(("-> %02x\n",(int)uc));
}

#if 0
//void
//SetMCLKInOUt(BYTE MCLKSelect)
//{
//    BYTE uc ;
//    uc = HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL);
//    uc &= ~M_MCLKSEL ;
//    uc |= MCLKSelect ;
//    HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL, uc);
//}
#endif

void
SetIntMask1(BYTE AndMask,BYTE OrMask)
{
    BYTE uc = 0;
    if(AndMask != 0)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_MASK1);
    }
    uc &= AndMask ;
    uc |= OrMask ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_MASK1, uc);
}

void
SetIntMask2(BYTE AndMask,BYTE OrMask)
{
    BYTE uc = 0;
    if(AndMask != 0)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_MASK2);
    }
    uc &= AndMask ;
    uc |= OrMask ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_MASK2, uc);
}

void
SetIntMask3(BYTE AndMask,BYTE OrMask)
{
    BYTE uc = 0;
    if(AndMask != 0)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_MASK3);
    }
    uc &= AndMask ;
    uc |= OrMask ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_MASK3, uc);
}

void
SetIntMask4(BYTE AndMask,BYTE OrMask)
{
    BYTE uc = 0;
    if(AndMask != 0)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_MASK4);
    }
    uc &= AndMask ;
    uc |= OrMask ;
    HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_MASK4, uc);
}

#if 0
void
SetGeneralPktType(BYTE type)
{
    HDMIRX_WriteI2C_Byte(REG_RX_GEN_PKT_TYPE,type);
}
#endif

BOOL
IsHDMIRXHDMIMode()
{
    BYTE uc ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);
    // HDMIRX_DEBUG_PRINTF(("IsHDMIRXHDMIMode(): read %02x from reg%02x, result is %s\n",
    //(int)uc,(int)REG_RX_SYS_STATE,(uc&B_HDMIRX_MODE)?"TRUE":"FALSE"));
    return (uc&B_HDMIRX_MODE)?TRUE:FALSE ;
}

BOOL
IsHDCPOn()
{
    BYTE uc,stat ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_HDCP_STATUS);
    stat = HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST) ;
    HDMIRX_DEBUG_PRINTF(("reg12 = %02X reg65 = %02X\n",(int)uc,(int)stat)) ;
    if((uc & 1 )&&(!(stat&8)))
    {
        return TRUE ;
    }

    return FALSE ;
}

BOOL
IsHDMIRX_VideoReady()
{
    if(VState == VSTATE_VideoOn &&(!MuteByPKG))
    {
        return TRUE ;
    }

    return FALSE ;
}

BOOL
IsHDMIRX_AudioReady()
{
    if(AState == ASTATE_AudioOn &&(!MuteByPKG))
    {
        return TRUE ;
    }

    return FALSE ;
}

BOOL
IsHDMIRX_VideoOn(void)
{
    return (VState == VSTATE_VideoOn)?TRUE:FALSE ;
}


BOOL
EnableHDMIRXVideoOutput(BYTE Option)
{
    BYTE uc ;
    switch(Option)
    {
    case VIDEO_ON: bVideoOutputOption = VIDEO_ON ; break ;
    case VIDEO_OFF: bVideoOutputOption = VIDEO_OFF ; break ;
    case VIDEO_AUTO: bVideoOutputOption = VIDEO_AUTO ; break ;
    default:
        bVideoOutputOption = VIDEO_AUTO ; break ;
    }

    switch(bVideoOutputOption)
    {
    case VIDEO_ON:
        SetMUTE(~B_TRI_VIDEO,0) ;
        break ;
    case VIDEO_OFF:
        SetMUTE(~B_TRI_VIDEO,B_TRI_VIDEO) ;
        break ;
    case VIDEO_AUTO:
        uc = (VState == VSTATE_VideoOn)?0:B_TRI_VIDEO ;
        SetMUTE(~B_TRI_VIDEO,uc) ;
        break ;
    }
    return FALSE ;
}
///////////////////////////////////////////////////////////
// Interrupt Service
///////////////////////////////////////////////////////////

void
Interrupt_Handler()
{
	BYTE int1data = 0 ;
	BYTE int2data = 0 ;
	BYTE int3data = 0 ;
	BYTE int4data = 0 ;
	BYTE sys_state ;
	BYTE flag = FLAG_CLEAR_INT_ALL;

    // ClearIntFlags(0);
    if(VState == VSTATE_SWReset || VState == VSTATE_Off )
    {
    	return ; // if SWReset, ignore all interrupt.
    }

    sys_state = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);


    // int4data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT4);
    int1data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);

    if(int1data /*||(int4data&B_RXCKON_DET) */)
    {
        HDMIRX_DEBUG_PRINTF3(("system state = %02X\n",(int)sys_state));
        HDMIRX_DEBUG_PRINTF3(("Interrupt 1 = %02X\n",(int)int1data));
        // HDMIRX_DEBUG_PRINTF3(("Interrupt 4 = %02X\n",(int)int4data));
        ClearIntFlags(B_CLR_MODE_INT);

		if(!CheckPlg5VPwr())
		{
			if(VState != VSTATE_SWReset && VState != VSTATE_PwrOff)
			{
				SWReset_HDMIRX();
				return ;
			}
		}

        if(int1data & B_PWR5VOFF)
        {
            HDMIRX_DEBUG_PRINTF(("5V Power Off interrupt\n"));
            RXINT_5V_PwrOff();
        }

        if(VState == VSTATE_SWReset)
        {
        	return ;
        }

        if(int1data & B_SCDTOFF)
        {
            HDMIRX_DEBUG_PRINTF(("SCDT Off interrupt\n"));
            RXINT_SCDT_Off();
        }

        if(int1data & B_PWR5VON)
        {
            HDMIRX_DEBUG_PRINTF(("5V Power On interrupt\n"));
            RXINT_5V_PwrOn();
        }


        if(VState == VSTATE_SyncWait)
        {

            if(int1data & B_SCDTON)
            {
                HDMIRX_DEBUG_PRINTF(("SCDT On interrupt\n"));
                RXINT_SCDT_On();
            }

            // if(int4data & B_RXCKON_DET)
            // {
            //     HDMIRX_DEBUG_PRINTF(("RXCKON DET interrupt\n"));
            //     RXINT_RXCKON();
            // }
        }

        if( VState == VSTATE_VideoOn || VState == VSTATE_HDCP_Reset)
        {
            if(int1data & B_HDMIMODE_CHG)
            {
                HDMIRX_DEBUG_PRINTF(("HDMI Mode change interrupt.\n"));
                RXINT_HDMIMode_Chg();
            }

            if(int1data & B_VIDMODE_CHG)
            {
                HDMIRX_DEBUG_PRINTF(("Video mode change interrupt.\n:"));
                RXINT_VideoMode_Chg();
            }
        }

    }

    int2data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT2);
	if(VState == VSTATE_VideoOn || VState == VSTATE_HDCP_Reset)
	{
        int4data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT4);
	}
	else
	{
	    int4data = 0 ;
	}
    if(int2data||(int4data & B_GENPKT_DET))
    {
        BYTE vid_stat = HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST);
        HDMIRX_DEBUG_PRINTF2(("Interrupt 2 = %02X\n",(int)int2data));
        ClearIntFlags(B_CLR_PKT_INT|B_CLR_MUTECLR_INT|B_CLR_MUTESET_INT);

        if(int2data & B_PKT_SET_MUTE)
        {
            HDMIRX_DEBUG_PRINTF(("AVMute set interrupt.\n"));
            RXINT_AVMute_Set();
        }

        if(int2data & B_NEW_AVI_PKG)
        {
            HDMIRX_DEBUG_PRINTF(("New AVI Info Frame Change interrupt\n"));
            RXINT_SetNewAVIInfo();
        }

        if((int2data & B_PKT_CLR_MUTE))
        {
            HDMIRX_DEBUG_PRINTF(("AVMute clear interrupt.\n"));
            RXINT_AVMute_Clear();
        }

    	if(VState == VSTATE_VideoOn || VState == VSTATE_HDCP_Reset)
    	{

            if(int4data & B_GENPKT_DET)
            {
                RXINT_CheckVendorSpecInfo() ;
            }
    	}
    }

    int3data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);

    if(int3data &(B_R_AUTH_DONE|B_R_AUTH_START))
    {
        ClearHDCPIntFlags();
    #ifdef SUPPORT_REPEATER
        if(bHDCPMode & HDCP_REPEATER)
        {
	        if(int3data & B_R_AUTH_START)
	        {
	            HDMIRX_DEBUG_PRINTF((" B_R_AUTH_START\n"));
	            SwitchRxHDCPState(RXHDCP_AuthStart);
	        }
	        if(int3data & B_R_AUTH_DONE)
	        {
	            HDMIRX_DEBUG_PRINTF(("B_R_AUTH_DONE \n"));
	            SwitchRxHDCPState(RXHDCP_AuthDone);
	        }
        }
    #endif // SUPPORT_REPEATER
    }

	if(VState == VSTATE_VideoOn || VState == VSTATE_HDCP_Reset)
	{
	    // int3data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);

	    if(int3data &(B_ECCERR|B_AUDFIFOERR|B_AUTOAUDMUTE))
	    {
	        ClearIntFlags(B_CLR_AUDIO_INT|B_CLR_ECC_INT);
	        if(AState != ASTATE_AudioOff)
	        {
		        if(int3data &(B_AUTOAUDMUTE|B_AUDFIFOERR))
		        {
		            HDMIRX_DEBUG_PRINTF3(("Audio Error interupt, int3 = %02X\n",(int)int3data));
		            RXINT_ResetAudio();
		            SetIntMask3(~(B_AUTOAUDMUTE|B_AUDFIFOERR),0);
		        }
	        }

	        if(int3data & B_ECCERR)
	        {
            #ifdef AUTO_SEARCH_EQ_SETTING
                EccErrorCounter++;
            #endif
	            HDMIRX_DEBUG_PRINTF(("int3 = %02X,ECC error interrupt\n",(int)int3data));
	            RXINT_ResetHDCP();
	        }
	    }
	}

    #ifdef DEBUG
    if(int1data | int2data)
    {
        int1data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1);
        int2data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT2);
        int3data = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);
        sys_state = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);
        HDMIRX_DEBUG_PRINTF2(("%02X %02X %02X %02X\n",
                                (int)int1data,
                                (int)int2data,
                                (int)int3data,
                                (int)HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT4),
                                (int)sys_state));
    }
    #endif

}

void
RXINT_CheckVendorSpecInfo(void)
{
	BYTE uc ;

    if(ucRevision >= 0xA3)
    {

        if(HDMIRX_ReadI2C_Byte(REG_RX_GENPKT_HB0)==(VENDORSPEC_INFOFRAME_TYPE|0x80))
        {
            HDMIRX_DEBUG_PRINTF2(("Detecting a VENDORSPECIFIC_INFOFRAME\n")) ;
			bVSIpresent = TRUE ;
            if((HDMIRX_ReadI2C_Byte(REG_RX_GENPKT_DB4)&0xE0)== 0x40)
            {
                HDMIRX_DEBUG_PRINTF2(("Detecting a FramePacking\n")) ;
                uc = HDMIRX_ReadI2C_Byte(0x3C);
                uc |=(1<<2);
                HDMIRX_WriteI2C_Byte(0x3C,uc);
            }
            uc = HDMIRX_ReadI2C_Byte(REG_RX_REGPKTFLAG_CTRL) ;
            uc &= ~B_INT_GENERAL_EVERY ;
            HDMIRX_WriteI2C_Byte(REG_RX_REGPKTFLAG_CTRL, uc) ;
        }
        AssignVideoTimerTimeout(VIDEO_TIMER_CHECK_COUNT);
    }
}

void
RXINT_5V_PwrOn(void)
{
    // BYTE sys_state ;

    if(VState == VSTATE_PwrOff)
    {
        // sys_state = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);

        // if(sys_state & B_PWR5VON)
        if(CheckPlg5VPwr())
        {
            SwitchVideoState(VSTATE_SyncWait);
        }
    }
}

void
RXINT_5V_PwrOff()
{
    BYTE sys_state ;

    sys_state = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);

    SWReset_HDMIRX();
}

void
RXINT_SCDT_On()
{
    if(VState == VSTATE_SyncWait)
    {
        if(IsSCDT())
        {
        #ifdef DISABLE_COLOR_DEPTH_RESET
            SwitchVideoState(VSTATE_SyncChecking);
        #else
		// 2011/10/17 added by jau-chih.tseng@ite.com.tw
			if( bDisableColorDepthResetState == FALSE )
			{
            SwitchVideoState(VSTATE_ColorDetectReset);
        }
			else
			{
            	SwitchVideoState(VSTATE_SyncChecking);
			}
		//~jau-chih.tseng@ite.com.tw 2011/10/17
        #endif
        }
    }
}

//=============================================================================
// 1. Reg97[5] = '1'
// 2. Reg05[7][1] = '1' '1'
// 3. Reg73[3]  = '1'
//
// 4. Reg97[5] = '0'
// 5. REg05[7][1] = '0''0'
// 6. Reg73[3] = '0'
//=============================================================================

static
void CDR_Reset()
{
//
// //	BYTE uc ;
// // 2009/10/22 modified by Jau-chih.tseng@ite.com.tw
// // //max7088 20081112 for A2 Jitter Tolerance Issue
// // 	uc = HDMIRX_ReadI2C_Byte(0x97);
// // 	HDMIRX_WriteI2C_Byte(0x97,uc|0x20);
// // 	delay1ms(1);
// // 	HDMIRX_WriteI2C_Byte(0x97,uc&(~0x20));
// // //end
// //
// //     if(EnaSWCDRRest)
// //     {
// //         uc =  B_SWRST | B_CDRRST ;
// //     }
// //     else
// //     {
// //         uc =  B_CDRRST ;
// //     }
// //     EnaSWCDRRest = FALSE ;
// //     HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, uc|B_EN_AUTOVDORST);
// //     delay1ms(1);
// //     HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL,B_EN_AUTOVDORST);
// //
// //     uc = HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL);
// //     HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,uc|B_RSTCD);
// //     delay1ms(1);
// //     HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,uc&(~B_RSTCD));
// //
//
// //20091028 follow Ann Suggestion to modify CDRReset()
//     BYTE uc;
// 	HDMIRX_DEBUG_PRINTF(("CDR_Reset()\n"));
//
//     SetIntMask4(0,0);
//     SetIntMask1(0,0);
//
// // 1. Reg97[5] = '1'
//     uc = HDMIRX_ReadI2C_Byte(0x97);
//     HDMIRX_WriteI2C_Byte(0x97,uc|0x20);
//
// // 2. Reg05[7][1] = '1' '1'
//     if(EnaSWCDRRest)
//     {
//         HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_SWRST | B_CDRRST|B_EN_AUTOVDORST);
//     }
//     else
//     {
//         HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_VDORST | B_CDRRST|B_EN_AUTOVDORST);
//     }
// // 3. Reg73[3]  = '1'
//
//     uc = HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL);
//     HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,uc |B_RSTCD);
//
// // 4. Reg97[5] = '0'
//     uc = HDMIRX_ReadI2C_Byte(0x97);
//     HDMIRX_WriteI2C_Byte(0x97,uc&(~0x20));
//
// // 5. REg05[7][1] = '0''0'
//     HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST);
// // 6. Reg73[3] = '0'
//     uc = HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL);
//     HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,uc &(~B_RSTCD));
// //~Jau-chih.tseng@ite.com.tw 2009/10/22
//
// #ifdef SUPPORT_REPEATER
//     RxHDCPRepeaterCapabilityClear(B_KSV_READY);
// #endif //SUPPORT_REPEATER
//     // 2010/08/10 added by jau-chih.tseng@ite.com.tw
//     // avoid the INT of mode by CDR Reset, clear the mode interrupt by CDR Reset
//     SetIntMask4(0,B_M_RXCKON_DET);
//     SetIntMask1(0,B_PWR5VON|B_SCDTON|B_PWR5VOFF);
//     ClearIntFlags(B_CLR_MODE_INT);
//     //~jau-chih.tseng@ite.com.tw 2010/08/10
//
//     AcceptCDRReset = FALSE ;
// 	ucDVISCDToffCNT=0;		// 20091021 for VG-859 HDMI / DVI change issue
//
}



void
RXINT_SCDT_Off()
{

    if(VState != VSTATE_PwrOff)
    {
        HDMIRX_DEBUG_PRINTF(("GetSCDT OFF\n"));
        SwitchVideoState(VSTATE_SyncWait);

        //2008/10/02 modified by hermes
        SCDTErrorCnt++;
    }
}

void
RXINT_VideoMode_Chg()
{
    BYTE sys_state ;

    // CAT6023/IT6605 only detect video mode change while AVMute clear, thus
    // the first Video mode change after AVMUTE clear and video on should be
    // ignore.
    HDMIRX_DEBUG_PRINTF(("RXINT_VideoMode_Chg\n"));

    sys_state = HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);
    // SetALLMute();

    if(CheckPlg5VPwr())
    {
        if( bIgnoreVideoChgEvent == FALSE )
        {
        SwitchVideoState(VSTATE_SyncWait);
    }
    }
    else
    {
        SWReset_HDMIRX();
    }
    bIgnoreVideoChgEvent = FALSE ;
}

void
RXINT_HDMIMode_Chg()
{
    if(VState == VSTATE_VideoOn)
    {
        if(IsHDMIRXHDMIMode())
        {
            HDMIRX_DEBUG_PRINTF(("HDMI Mode.\n"));
            SwitchAudioState(ASTATE_RequestAudio);
            // wait for new AVIInfoFrame to switch color space.
        }
        else
        {
            HDMIRX_DEBUG_PRINTF(("DVI Mode.\n"));
            SwitchAudioState(ASTATE_AudioOff);
            NewAVIInfoFrameF = FALSE ;

            // should switch input color mode to RGB24 mode.
            SetDVIVideoOutput();
            // No info frame active.
        }
    }
}

void RXINT_RXCKON()
{
    // if(AcceptCDRReset == TRUE)
    // {
    //     if((HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE)&(B_VCLK_DET|B_RXCK_VALID))==(B_VCLK_DET|B_RXCK_VALID))
    //     {
    //         CDR_Reset();
    //     }
    // }
}

void
RXINT_AVMute_Set()
{
    BYTE uc ;
    MuteByPKG = ON ;
    // SetALLMute();
    SetAudioMute(ON);
    SetVideoMute(ON);
    StartAutoMuteOffTimer(); // start AutoMute Timer.
    SetIntMask2(~(B_PKT_CLR_MUTE),(B_PKT_CLR_MUTE)); // enable the CLR MUTE interrupt.

    bDisableAutoAVMute = 0 ;
//     uc = HDMIRX_ReadI2C_Byte(REG_RX_RDROM_CLKCTRL);
    uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
    uc &= ~B_VDO_MUTE_DISABLE ;
//     HDMIRX_WriteI2C_Byte(REG_RX_RDROM_CLKCTRL, uc);
    HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
}

void
RXINT_AVMute_Clear()
{
    BYTE uc ;
    MuteByPKG = OFF ;

    bDisableAutoAVMute = 0 ;
    // HDMIRX_WriteI2C_Byte(REG_RX_RDROM_CLKCTRL, HDMIRX_ReadI2C_Byte(REG_RX_RDROM_CLKCTRL)&(~B_VDO_MUTE_DISABLE));
    uc =  HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
    uc &= ~B_VDO_MUTE_DISABLE ;
    HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);

    EndAutoMuteOffTimer();

    if(VState == VSTATE_VideoOn)
    {
        SetVideoMute(OFF);

    }

    if(AState == ASTATE_AudioOn)
    {
        SetHWMuteClr();
        ClearHWMuteClr();

        SetAudioMute(OFF);
    }
    SetIntMask2(~(B_PKT_CLR_MUTE),0); // clear the CLR MUTE interrupt.
}

void
RXINT_SetNewAVIInfo()
{
    NewAVIInfoFrameF = TRUE ;

    if(VState == VSTATE_VideoOn)
    {
        SetNewInfoVideoOutput();
    }

    prevAVIDB1 = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB1);
    prevAVIDB2 = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB2);

}

void
RXINT_ResetAudio()
{
    // audio error.
    if(AState != ASTATE_AudioOff)
    {
        SetAudioMute(ON);
        SwitchAudioState(ASTATE_RequestAudio);
    }
}


void
RXINT_ResetHDCP()
{
    BYTE uc ;

    if(VState == VSTATE_VideoOn)
    {
        ClearIntFlags(B_CLR_ECC_INT);
        delay1ms(1);
        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);

        if(uc & B_ECCERR)
        {
    		SwitchVideoState(VSTATE_HDCP_Reset);
        }

        // HDCP_Reset();
        // SetVideoMute(MuteByPKG);
        // RXINT_ResetAudio(); // reset Audio
    }
}

#ifdef AUTO_SEARCH_EQ_SETTING
BOOL
IncreaseEQ()
{
    if ( initEQTestIdx < MAX_EQ_IDX )
    {
        initEQTestIdx++;
        HDMIRX_WriteI2C_Byte(0x95, EQValue[initEQTestIdx]) ;

        return TRUE;
    }
    return FALSE;
}
#endif
///////////////////////////////////////////////////////////
// Timer Service
///////////////////////////////////////////////////////////

void
Timer_Handler()
{

	Interrupt_Handler();
    VideoTimerHandler();
    MuteProcessTimerHandler();
    AudioTimerHandler();
#ifdef SUPPORT_REPEATER
    RxHDCP_Handler();
#endif // SUPPORT_REPEATER
}

static void
VideoTimerHandler()
{
	UCHAR uc ;

    if( VState == VSTATE_Off )
    {
        return ;
    }

	if( VideoCountingTimer > 0 )
	{
	    VideoCountingTimer -- ;
	}

#ifndef AUTO_SEARCH_EQ_SETTING
	//2008/10/02 modified by hermes
	if(SCDTErrorCnt>= SCDT_LOST_TIMEOUT)
    {
		SWReset_HDMIRX();
	}
#else
	//2008/10/02 modified by hermes
	if(SCDTErrorCnt>= SCDT_LOST_TIMEOUT)
    {
        HDMIRX_DEBUG_PRINTF(("SCDTErrorCnt==%d, EQ++\n", (int)SCDTErrorCnt));
        IncreaseEQ();
		SWReset_HDMIRX();
        return;
	}

    if( SyncDetectFailCounter > 8 && SyncCheckCounter > 8 )
    {
        if( IncreaseEQ() )
        {
            HDMIRX_DEBUG_PRINTF(("SyncWait/SyncCheck loop, EQ++ ( %d/%d/%d)\n", (int)SyncWaitCounter, (int)SyncCheckCounter, (int)SyncDetectFailCounter ));
            SyncWaitCounter = 0;
            SyncCheckCounter = 0;
            SyncDetectFailCounter = 0;
    		SWReset_HDMIRX();
            return;
        }
    }

    if ( (VState == VSTATE_VideoOn) )
    {
        if( VideoOnTick >= 3000/20 )
        {
            static BYTE eq;

            if ( VideoOnTick == 6000/20 )
            {
                eq = HDMIRX_ReadI2C_Byte(0x95);
                switch( eq )
                {
                case 0x87 : EQSum[0]++; break;
                case 0x81 : EQSum[1]++; break;
                case 0x80 : EQSum[2]++; break;
                }
            }

            if( VideoOnTick % 150 == 0 )
            {
                HDMIRX_DEBUG_PRINTF(("VideoOnTick = %u, ",VideoOnTick));
                HDMIRX_DEBUG_PRINTF(("9a=%bX, ", HDMIRX_ReadI2C_Byte(0x9a)));
                HDMIRX_DEBUG_PRINTF(("85=%bX, ", HDMIRX_ReadI2C_Byte(0x85)));
                HDMIRX_DEBUG_PRINTF(("95=%bX, ", eq));
                HDMIRX_DEBUG_PRINTF(("eq+: 87(%u),81(%u),80(%u), rep=%u ", EQSum[0], EQSum[1], EQSum[2], gTestRep ));
                HDMIRX_DEBUG_PRINTF(("ecc=%d\n", (int)EccErrorCounter));
            }
        }

        if( VideoOnTick >= 5000/20 )
        {
            if ( EccErrorCounter )
            {
                if( IncreaseEQ() )
                {
                    HDMIRX_DEBUG_PRINTF(("ECC Error Count after video on, reset !!\n"));
                    EccErrorCounter = 0;
                    SWReset_HDMIRX();
                    return;
                }
            }
        }

    }

    // increase EQ when ECC error
    if( EccErrorCounter > 100 )
    {
        if( IncreaseEQ() )
        {
            HDMIRX_DEBUG_PRINTF(("ECC Error Count reach, reset !!\n"));
            EccErrorCounter = 0;
            SWReset_HDMIRX();
            return;
        }
    }

#endif
	// monitor if no state
	if(VState == VSTATE_SWReset)
	{
		if(VideoCountingTimer==0)
		{
			Terminator_On();
			SwitchVideoState(VSTATE_PwrOff);
			return ;
		}

		return ;
	}

#ifdef AUTO_SEARCH_EQ_SETTING
    if(!CheckPlg5VPwr())
    {
        initEQTestIdx = minInitEQIdx ;
        HDMIRX_WriteI2C_Byte(0x95, EQValue[initEQTestIdx]) ;
    }
#endif
	if(VState == VSTATE_PwrOff)
	{
	    if(CheckPlg5VPwr())
	    {
            SwitchVideoState(VSTATE_SyncWait);
            return ;
	    }
	}

    // if(VState == VSTATE_SyncWait)//20091021 modify
    // {
    //     if(AcceptCDRReset == TRUE)
    //     {
    //         if((HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE)&(B_VCLK_DET|B_RXCK_VALID))==(B_VCLK_DET|B_RXCK_VALID))
    //         {
    //         	AcceptCDRReset = FALSE;
    //         	EnaSWCDRRest = FALSE;           //add by hermes 20090323
    //         	CDR_Reset();
    //         }
    //     }
    // }

	if((VState != VSTATE_PwrOff)&&(VState != VSTATE_SyncWait)&&(VState != VSTATE_SWReset)&&(VState != VSTATE_ColorDetectReset))
	{
	    if(!IsSCDT())
	    {
            SwitchVideoState(VSTATE_SyncWait);
            return ;
	    }
	}
	else if((VState != VSTATE_PwrOff)&&(VState != VSTATE_SWReset))
	{
	    if(!CheckPlg5VPwr())
	    {
            // SwitchVideoState(VSTATE_PwrOff);
            SWReset_HDMIRX();
            return ;
	    }
	}

    // 2007/01/12 added by jjtseng
    // add the software reset timeout setting.
    if(VState == VSTATE_SyncWait || VState == VSTATE_SyncChecking)
    {
        SWResetTimeOut-- ;
        if(SWResetTimeOut == 0)
        {
        #ifdef AUTO_SEARCH_EQ_SETTING
            HDMIRX_DEBUG_PRINTF(("SWResetTimeOut\n"));
            IncreaseEQ();
        #endif
            SWReset_HDMIRX();
            return ;
        }
    }
    //~jjtseng

    if(VState == VSTATE_SyncWait)
    {

        if(VideoCountingTimer == 0)
        {
            HDMIRX_DEBUG_PRINTF(("VsyncWaitResetTimer up, call SWReset_HDMIRX()\n",VideoCountingTimer));
            SWReset_HDMIRX();
            return ;
            // AssignVideoTimerTimeout(VSTATE_MISS_SYNC_COUNT);
        }
        else
        {

    		uc=HDMIRX_ReadI2C_Byte(REG_RX_SYS_STATE);
            HDMIRX_DEBUG_PRINTF(("REG_RX_SYS_STATE = %X\r",(int)uc));
    		uc &=(B_RXPLL_LOCK|B_RXCK_VALID|B_SCDT|B_VCLK_DET);

        #ifdef AUTO_SEARCH_EQ_SETTING
            if((uc &(B_RXPLL_LOCK|B_RXCK_VALID))==(B_RXPLL_LOCK|B_RXCK_VALID)) // locked
            {
                SyncDetectFailCounter++ ;
                HDMIRX_DEBUG_PRINTF(("SyncDetectFailCounter = %d\n",(int)SyncDetectFailCounter)) ;
                if( SyncDetectFailCounter > 50 )
                {
                    SyncDetectFailCounter = 0;
                    IncreaseEQ();
                    SWReset_HDMIRX();
                    return;
                }
            }
        #endif

    		if(uc ==(B_RXPLL_LOCK|B_RXCK_VALID|B_SCDT|B_VCLK_DET))// for check SCDT !!
            {
                #ifdef DISABLE_COLOR_DEPTH_RESET
                    SwitchVideoState(VSTATE_SyncChecking);
                #else
        		// 2011/10/17 added by jau-chih.tseng@ite.com.tw
        			if( bDisableColorDepthResetState == FALSE )
        			{
                SwitchVideoState(VSTATE_ColorDetectReset);
        			}
        			else
        			{
                    	SwitchVideoState(VSTATE_SyncChecking);
        			}
        		//~jau-chih.tseng@ite.com.tw 2011/10/17
                #endif
                return ;
            }

        }
    }

    if(VState==VSTATE_ColorDetectReset)
    {
        if(VideoCountingTimer==0 /*|| (IsSCDT() == TRUE) */)
        {
            // SwitchVideoState(VSTATE_ModeDetecting);
            SwitchVideoState(VSTATE_SyncChecking);
            return;
        }
    }

    if(VState == VSTATE_SyncChecking)
    {
        // HDMIRX_DEBUG_PRINTF(("SyncChecking %d\n",VideoCountingTimer));
        if(VideoCountingTimer == 0)
        {
            SwitchVideoState(VSTATE_ModeDetecting);
            return ;
        }
    }

    if(VState == VSTATE_HDCP_Reset)
    {
        // HDMIRX_DEBUG_PRINTF(("SyncChecking %d\n",VideoCountingTimer));
        if(VideoCountingTimer == 0)
        {
        	HDMIRX_DEBUG_PRINTF(("HDCP timer reach, reset !!\n"));
            // SwitchVideoState(VSTATE_PwrOff);
            SWReset_HDMIRX();
            return ;
        }
        else
        {
            HDMIRX_DEBUG_PRINTF(("VideoTimerHandler[VSTATE_HDCP_Reset](%d)\n",VideoCountingTimer));
            do {
	        	ClearIntFlags(B_CLR_ECC_INT);
	        	delay1ms(1);
	        	uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);
	        	if(uc & B_ECCERR)
	        	{
	                break ;
	        	}
	        	delay1ms(1);
	        	ClearIntFlags(B_CLR_ECC_INT);
	        	delay1ms(1);
	        	uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3);
	        	if(!(uc & B_ECCERR))
	        	{
	                SwitchVideoState(VSTATE_VideoOn);
	                return ;
	        	}
	        }while(0);
        }
    }

    if(VState == VSTATE_VideoOn)
    {
		char diff ;
		unsigned short HTotal ;
		unsigned char xCnt ;
		BOOL bVidModeChange = FALSE ;
		BOOL ScanMode ;
		// bGetSyncInfo();

        #ifdef AUTO_SEARCH_EQ_SETTING
        VideoOnTick ++;
        #endif

        if( MuteByPKG == ON )
        {
            // if AVMute, ignore the video parameter compare.
            // if AVMute clear, the video parameter compare should be ignored.
            AssignVideoTimerTimeout(5) ;
        }
        else
        {
            if(VideoCountingTimer == 1)
            {
                bGetSyncInfo();
        		currHTotal = s_CurrentVM.HTotal ;
        		currXcnt = s_CurrentVM.xCnt ;
        		currScanMode = s_CurrentVM.ScanMode ;
            }
        }

        if(VideoCountingTimer == 0)
        {
            SCDTErrorCnt = 0 ;

            if( MuteByPKG == OFF )
            {
                // modified by jau-chih.tseng@ite.com.tw
                // Only AVMUTE OFF the video mode can be detected.
		HTotal =(unsigned short)HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_L);
		HTotal |=(unsigned short)(HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_H)&M_HTOTAL_H)<< 8 ;
        		// if(ABS((int)HTotal -(int)currHTotal)>4)
        		if(HTotal > currHTotal)
        		{
        			HTotal -= currHTotal ;
        		}
        		else
        		{
        			HTotal = currHTotal - HTotal ;
        		}

        		if(HTotal>4)
		{
			bVidModeChange = TRUE ;
			HDMIRX_DEBUG_PRINTF(("HTotal changed.\n"));
		}

		if(!bVidModeChange)
		{
			xCnt =(unsigned char)HDMIRX_ReadI2C_Byte(REG_RX_VID_XTALCNT_128PEL);

        			// 2011/02/18 modified by jau-chih.tseng@ite.com.tw
        			// to avoid the compiler calculation error. Change calculating
        			// method.
			//diff =(char)currXcnt -(char)xCnt ;
        			if(currXcnt > xCnt )
        			{
        				diff = currXcnt - xCnt ;
      }
        			else
        			{
        				diff = xCnt - currXcnt ;
        			}
        			//~jau-chih.tseng@ite.com.tw 2011/02/18


			if(xCnt > 0x80)
			{
        				if(diff> 6)
				{
					HDMIRX_DEBUG_PRINTF(("Xcnt changed. %02x -> %02x ",(int)xCnt,(int)currXcnt));
					HDMIRX_DEBUG_PRINTF(("diff = %d\r\n",(int)diff));
					bVidModeChange = TRUE ;
				}
			}
			else if(xCnt > 0x40)
			{
        				if(diff> 4)
				{
					HDMIRX_DEBUG_PRINTF(("Xcnt changed. %02x -> %02x ",(int)xCnt,(int)currXcnt));
					HDMIRX_DEBUG_PRINTF(("diff = %d\r\n",(int)diff));
					bVidModeChange = TRUE ;
				}
			}
			else if(xCnt > 0x20)
			{
        				if(diff> 2)
				{
					HDMIRX_DEBUG_PRINTF(("Xcnt changed. %02x -> %02x ",(int)xCnt,(int)currXcnt));
					HDMIRX_DEBUG_PRINTF(("diff = %d\n\r",(int)diff));
					bVidModeChange = TRUE ;
				}
			}
			else
			{
        				if(diff> 1)
				{
					HDMIRX_DEBUG_PRINTF(("Xcnt changed. %02x -> %02x ",(int)xCnt,(int)currXcnt));
					HDMIRX_DEBUG_PRINTF(("diff = %d\r\n",(int)diff));
					bVidModeChange = TRUE ;
				}
			}
		}

        if(s_CurrentVM.VActive < 300)
        {
    		if(!bVidModeChange)
    		{
    			ScanMode =(HDMIRX_ReadI2C_Byte(REG_RX_VID_MODE)&B_INTERLACE)?INTERLACE:PROG ;
    			if(ScanMode != currScanMode)
    			{
    				HDMIRX_DEBUG_PRINTF(("ScanMode change.\r\n"));
    				bVidModeChange = TRUE ;
    			}
    		}
        }
            }
        }



		if(bVidModeChange)
		{

			SwitchVideoState(VSTATE_SyncWait);
			return ;
		}
        else
        {
            unsigned char currAVI_DB1, currAVI_DB2 ;

            currAVI_DB1 = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB1);
            currAVI_DB2 = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB2);

            if(IsHDMIRXHDMIMode()){
                if((currAVI_DB1 != prevAVIDB1)||(currAVI_DB2 != prevAVIDB2)){
                    RXINT_SetNewAVIInfo();
                }
            }
            prevAVIDB1 = currAVI_DB1 ;
            prevAVIDB2 = currAVI_DB2 ;
        }

    }
}

static void
SetupAudio()
{
    BYTE uc ;
    BYTE RxAudioCtrl ;
    getHDMIRXInputAudio(&AudioCaps);

    if(AudioCaps.AudioFlag & B_CAP_AUDIO_ON)
    {
        // bCurRxLPCM=(HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT0)&0x02)?FALSE:TRUE;
        uc=HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL)& 0xF8; // default set 256Fs
        uc |=0x1; // 256xFs
        HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL,uc);

        uc=(HDMIRX_ReadI2C_Byte(REG_RX_FS_SET)&0xCF); // avoid audio jitter
        // uc|=0x10;
        // 2009/08/24 modified by jjtseng
        // jitter control set to maximum valud.
        uc |= 0x70 ;
        //~jjtseng
        HDMIRX_WriteI2C_Byte(REG_RX_FS_SET,uc);

        if(AudioCaps.AudioFlag& B_CAP_HBR_AUDIO)
        {

            Switch_HDMIRX_Bank(0);

            #ifdef _HBR_I2S_
            uc = HDMIRX_ReadI2C_Byte(REG_RX_HWAMP_CTRL);
            uc &= ~(1<<4);
            HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL, uc);
            #else
            HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL,(HDMIRX_ReadI2C_Byte(REG_RX_HWAMP_CTRL)|0x10));
            #endif

        #if 1 // for TI DSP HBR only accept 128Fs MCLK
            uc=HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL)& 0xF8;
            uc |=0x0; // 128xFs
            HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL,uc);
        #endif

            SetHWMuteClrMode();
            ResetAudio();//mingchih add
        }
        else if(AudioCaps.AudioFlag& B_CAP_DSD_AUDIO)
        {
            // TBD.
            SetHWMuteClrMode();
            ResetAudio();//mingchih add
        }
        else // if(AudioCaps.AudioFlag& B_CAP_LPCM)// not only LPCM but all use audio sample packet need this fixing.
        {
            uc = HDMIRX_ReadI2C_Byte(REG_RX_HWAMP_CTRL);
            HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL,uc &(~0x10));

            ucHDMIAudioErrorCount++;
            RxAudioCtrl=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CTRL);

            if(ucHDMIAudioErrorCount > 10)
            {
                ucHDMIAudioErrorCount=0;
                // 2009/02/10 added by Jau-Chih.Tseng@ite.com.tw
                // change the force FS to be toggled to avoid something unknow wrong.
                if(RxAudioCtrl & B_FORCE_FS)// 20090211
                {
                    RxAudioCtrl &= ~B_FORCE_FS;
                }
                //~Jau-Chih.Tseng@ite.com.tw 2009/02/10
                else
                {
                    // Force Sample FS setting progress:
                    // a. if find Audio Error in a period timers,
                    // assum the FS message is wrong,then try to force FS setting.
                    // force sequence : 48KHz -> 44.1KHz -> 32KHz -> 96KHz ->  192KHz ->
                    //(88.2KHz -> 176.4KHz)
                    // -> 48KHz
                    switch(ucAudioSampleClock)
                    {
                    case AUDFS_192KHz: ucAudioSampleClock=AUDFS_48KHz;break ;// default: -> 48KHz

                    case AUDFS_48KHz: ucAudioSampleClock=AUDFS_44p1KHz;break ;//
                    case AUDFS_44p1KHz: ucAudioSampleClock=AUDFS_32KHz;break ;//
                    case AUDFS_32KHz: ucAudioSampleClock=AUDFS_96KHz;break ;//

                #ifndef SUPPORT_FORCE_88p2_176p4

                    case AUDFS_96KHz: ucAudioSampleClock=AUDFS_192KHz;break ;//

                #else // SUPPORT_FORCE_88p2_176p4

                    case AUDFS_88p2KHz: ucAudioSampleClock=AUDFS_176p4KHz;break ;//
                    case AUDFS_96KHz: ucAudioSampleClock=AUDFS_88p2KHz;break ;//
                    case AUDFS_176p4KHz: ucAudioSampleClock=AUDFS_192KHz;break ;//

                #endif

                    default: ucAudioSampleClock=AUDFS_48KHz;break;// ? -> 48KHz
                    }
                    HDMIRX_DEBUG_PRINTF(("===[Audio FS Error ]===\n"));
                    RxAudioCtrl |=B_FORCE_FS;
                }
                // if B_FORCE_FS changed, update REG_RX_audio change.
            }
            RxAudioCtrl |= B_EN_I2S_NLPCM ;
            HDMIRX_WriteI2C_Byte(REG_RX_AUDIO_CTRL,RxAudioCtrl);// reg77[6]=?


            // 2010/12/03 modified by Max.Kao@ite.com.tw
            // This bit will affect the force Fs fail.
            // Do not set this.
            // uc=HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL);
            // if(RxAudioCtrl & B_FORCE_FS)
            // {
            //     // b. set Reg0x77[6]=1=> select Force FS mode.
            //     // c. set Reg0x78[5]=1=> CTSINI_EN=1
            //     uc|=B_CTSINI_EN;
            // }
            // else
            // {
            //     uc &= ~B_CTSINI_EN;
            // }
            // HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL, uc);
            //~Max.Kao@ite.com.tw 2010/12/03

            SetHWMuteClrMode();
            // d. set Reg0x05=04=> reset Audio
            // e. set Reg0x05=0
            ResetAudio();

            if(RxAudioCtrl & B_FORCE_FS)
            {
                // f. set Reg0x7e[3:0]=0(at leasst three times)=> force FS value
                // g. if Audio still Error,then repeat b~f setps.(on f setp,set another FS value
                // 0:44,1K,2: 48K,3:32K,8:88.2K,A:96K,C:176.4K,E:192K)
                uc=HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL);
                uc &= 0xF0 ;
                uc |= ucAudioSampleClock & 0xF ;
                HDMIRX_WriteI2C_Byte(REG_RX_FS_SET,uc);
                HDMIRX_WriteI2C_Byte(REG_RX_FS_SET,uc);
                HDMIRX_WriteI2C_Byte(REG_RX_FS_SET,uc);
                HDMIRX_WriteI2C_Byte(REG_RX_FS_SET,uc);
            }
            SetIntMask3(~(B_AUTOAUDMUTE|B_AUDFIFOERR),(B_AUTOAUDMUTE|B_AUDFIFOERR)); // enable Audio Error Interrupt
        }
        /*
        else // NLPCM
        {
            uc = HDMIRX_ReadI2C_Byte(REG_RX_HWAMP_CTRL);
            HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL,uc &(~0x10));
            SetHWMuteClrMode();
            ResetAudio();//mingchih add

        }
        */
        ClearIntFlags(B_CLR_AUDIO_INT);
        SetIntMask3(~(B_AUTOAUDMUTE|B_AUDFIFOERR),(B_AUTOAUDMUTE|B_AUDFIFOERR));
        SwitchAudioState(ASTATE_WaitForReady);

    }
    else
    {
        ucHDMIAudioErrorCount = 0 ;
    // 2009/02/10 added by Jau-Chih.Tseng@ite.com.tw
        ucAudioSampleClock=DEFAULT_START_FIXED_AUD_SAMPLEFREQ ;
        // ucAudioSampleClock=AUDFS_192KHz ;
    //~Jau-Chih.Tseng@ite.com.tw 2009/02/10

        uc=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CTRL);
        uc &= ~B_FORCE_FS ;
        HDMIRX_WriteI2C_Byte(REG_RX_AUDIO_CTRL, uc);
        uc = HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL);
        uc &= ~B_CTSINI_EN;
        HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL, uc);
        HDMIRX_DEBUG_PRINTF2(("Audio Off, clear Audio Error Count.\n"));
    }
}

static void
EnableAudio()
{
#ifndef DISABLE_AUDIO_SUPPORT
    // Enable Audio
    SetupAudio();

    delay1ms(5);

    if(AudioCaps.AudioFlag & B_CAP_AUDIO_ON)
    {
        if(HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT3)&(B_AUTOAUDMUTE|B_AUDFIFOERR))
        {
            SwitchAudioState(ASTATE_RequestAudio);
        }
        else
        {
            SwitchAudioState(ASTATE_AudioOn);
        }
    }
    else
    {
        SwitchAudioState(ASTATE_RequestAudio);
    }
#else
        SwitchAudioState(ASTATE_AudioOff);
#endif

}

void AudioTimerHandler()
{
#ifndef DISABLE_AUDIO_SUPPORT
    BYTE uc;
    AUDIO_CAPS CurAudioCaps ;

    switch(AState)
    {
    case ASTATE_RequestAudio:
        SetupAudio();
        break;

    case ASTATE_WaitForReady:
        if(AudioCountingTimer==0)
        {
            SwitchAudioState(ASTATE_AudioOn);
        }
        else
        {
            AudioCountingTimer --;
        }

        break;

    case ASTATE_AudioOn:
        getHDMIRXInputAudio(&CurAudioCaps);

        if(AudioCaps.AudioFlag != CurAudioCaps.AudioFlag
           /* || AudioCaps.AudSrcEnable != CurAudioCaps.AudSrcEnable
           || AudioCaps.SampleFreq != CurAudioCaps.SampleFreq */)
        {

            ucHDMIAudioErrorCount=0;
            // 2009/02/10 added by Jau-Chih.Tseng@ite.com.tw
            // ucAudioSampleClock=AUDFS_48KHz ;
            ucAudioSampleClock = DEFAULT_START_FIXED_AUD_SAMPLEFREQ;
            //~Jau-Chih.Tseng@ite.com.tw 2009/02/10
            uc=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CTRL);
            uc &= ~B_FORCE_FS ;
            HDMIRX_WriteI2C_Byte(REG_RX_AUDIO_CTRL, uc);
            uc = HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL);
            uc &= ~B_CTSINI_EN;
            HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL, uc);

            HDMIRX_DEBUG_PRINTF(("Audio change, clear Audio Error Count.\n"));

            SetAudioMute(ON);
            SwitchAudioState(ASTATE_RequestAudio);

        }

		if(AudioCountingTimer != 0)
        {
            AudioCountingTimer -- ;
            if(AudioCountingTimer == 0)
            {
                ucHDMIAudioErrorCount=0 ;
    			HDMIRX_DEBUG_PRINTF(("Audio On, clear Audio Error Count.\n"));
            }
        }


        break;
    }
#endif
}


BYTE    HDMIRXFsGet()
{
    BYTE RxFS ;
    RxFS=HDMIRX_ReadI2C_Byte(REG_RX_FS)& 0x0F;
    return    RxFS;
}

//=============================================================================
BOOL    HDMIRXHDAudioGet()
{
    BOOL bRxHBR ;
    bRxHBR=(HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)&(1<<6))?TRUE:FALSE;
    return    bRxHBR;
}

//=============================================================================
BOOL    HDMIRXMultiPCM()
{
    BOOL bRxMultiCh ;
    bRxMultiCh=(HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)&(1<<4))?TRUE:FALSE;
    return    bRxMultiCh;
}

//=============================================================================
BYTE    HDMIRXAudioChannelNum()
{
    BYTE RxChEn ;
    RxChEn=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)& M_AUDIO_CH;

    return     RxChEn;
}
//=============================================================================
void    HDMIRXHBRMclkSet(BYTE cFs)
{
    BYTE    uc;
    uc=HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL)& 0xF8;

    switch(cFs)
    {
    case    9:
        uc |=0x0;    // 128xFs
        break;
    default:
        uc |=0x1;    // 256xFs
        break;

    }
    HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL,uc);

}

void getHDMIRXInputAudio(AUDIO_CAPS *pAudioCaps)
{
    BYTE uc ;

    if(!pAudioCaps)
    {
        return ;
    }
    Switch_HDMIRX_Bank(0);

    uc = HDMIRX_ReadI2C_Byte(REG_RX_FS);
    pAudioCaps->SampleFreq=uc&M_Fs ;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT);
    pAudioCaps->AudioFlag = uc & 0xF0 ;
    pAudioCaps->AudSrcEnable=uc&M_AUDIO_CH ;
    delay1ms(1);
    pAudioCaps->AudSrcEnable|=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)&M_AUDIO_CH ;
    delay1ms(1);
    pAudioCaps->AudSrcEnable|=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)&M_AUDIO_CH ;
    delay1ms(1);
    pAudioCaps->AudSrcEnable|=HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT)&M_AUDIO_CH ;

    if((uc &(B_HBRAUDIO|B_DSDAUDIO))== 0)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT0);

        if((uc & B_AUD_NLPCM)== 0)
        {
            pAudioCaps->AudioFlag |= B_CAP_LPCM;
        }
    }

}

void getHDMIRXInputChStat(AUDIO_CAPS *pAudioCaps)
{
    BYTE uc ;

    if(!pAudioCaps)
    {
        return ;
    }

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT0);
    pAudioCaps->ChStat[0] = uc;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT1);
    pAudioCaps->ChStat[1] = uc;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT2);
    pAudioCaps->ChStat[2] = uc;

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT3);

    pAudioCaps->ChStat[3] = uc & M_CLK_ACCURANCE;
    pAudioCaps->ChStat[3] <<= 4 ;
    pAudioCaps->ChStat[3] |=((BYTE)pAudioCaps->SampleFreq)&0xF ;

    pAudioCaps->ChStat[4] =(~((BYTE)pAudioCaps->SampleFreq))&0xF ;
    pAudioCaps->ChStat[4] <<= 4 ;
    pAudioCaps->ChStat[4] |=(uc & M_SW_LEN)>>O_SW_LEN;


}

static void
MuteProcessTimerHandler()
{
    BYTE uc ;
    BOOL TurnOffMute = FALSE ;

    if(MuteByPKG == ON)
    {
        // HDMIRX_DEBUG_PRINTF(("MuteProcessTimerHandler()\n"));
        if((MuteResumingTimer > 0)&&(VState == VSTATE_VideoOn))
        {
            MuteResumingTimer -- ;
            uc = HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST);
            HDMIRX_DEBUG_PRINTF(("MuteResumingTimer = %d uc = %02X\n",MuteResumingTimer ,(int)uc));

            if(!(uc&B_AVMUTE))
            {
                TurnOffMute = TRUE ;
                MuteByPKG = OFF ;
            }
            else if((MuteResumingTimer == 0))
            {
                bDisableAutoAVMute = B_VDO_MUTE_DISABLE ;

                uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
                uc |= B_VDO_MUTE_DISABLE ;
                HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);

                TurnOffMute = TRUE ;
                MuteByPKG = OFF ;
            }
        }

        if(MuteAutoOff)
        {
            uc = HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST);
            if(!(uc & B_AVMUTE))
            {
                EndAutoMuteOffTimer();
                TurnOffMute = TRUE ;
            }
        }
    }

    if(TurnOffMute)
    {
        if(VState == VSTATE_VideoOn)
        {
            SetVideoMute(OFF);
            if(AState == ASTATE_AudioOn)
            {
                SetAudioMute(OFF);
            }
            TurnOffMute = FALSE ;
        }
    }
}


void
AssignVideoTimerTimeout(USHORT TimeOut)
{
    VideoCountingTimer = TimeOut ;
}

void
AssignAudioTimerTimeout(USHORT TimeOut)
{
    AudioCountingTimer = TimeOut ;

}

#if 0
void
ResetVideoTimerTimeout()
{
    VideoCountingTimer = 0 ;
}

void
ResetAudioTimerTimeout()
{
    AudioCountingTimer = 0 ;
}
#endif

void
SwitchVideoState(Video_State_Type state)
{
    BYTE uc ;
	if(VState == state)
	{
		return ;
	}

    if(VState == VSTATE_VideoOn && state != VSTATE_VideoOn)
    {
    	// SetALLMute();

        SwitchAudioState(ASTATE_AudioOff);
    }

    HDMIRX_DEBUG_PRINTF(("RX VState %s -> %s\n",VStateStr[VState],VStateStr[state]));
    VState = state ;

    if(VState != VSTATE_SyncWait && VState != VSTATE_SyncChecking)
    {
        SWResetTimeOut = FORCE_SWRESET_TIMEOUT;
        // init the SWResetTimeOut, decreasing when timer.
        // if down to zero when SyncWait or SyncChecking,
        // SWReset.
    }

    switch(bVideoOutputOption)
    {
    case VIDEO_OFF:
        SetMUTE(~B_TRI_VIDEO,B_TRI_VIDEO) ;
        break ;
    case VIDEO_ON:
        SetMUTE(~B_TRI_VIDEO,0) ;
        break ;
    case VIDEO_AUTO:
    default:
        if( VState == VSTATE_VideoOn
            || VState == VSTATE_HDCP_Reset
            || VState == VSTATE_SyncChecking
            || VState == VSTATE_ModeDetecting )
        {
            SetMUTE(~B_TRI_VIDEO,0) ;
        }
        else
        {
            SetMUTE(~B_TRI_VIDEO,B_TRI_VIDEO) ;
        }
    }


    switch(VState)
    {
    case VSTATE_PwrOff:
        AcceptCDRReset = TRUE ;
        #ifdef AUTO_SEARCH_EQ_SETTING
        SyncDetectFailCounter = 0;
        SyncWaitCounter = 0;
        SyncCheckCounter = 0;
        #endif

        break ;
    case VSTATE_SWReset:
        // HDMIRX_WriteI2C_Byte(REG_RX_GEN_PKT_TYPE, 0x03); // set default general control packet received in 0xA8
        AssignVideoTimerTimeout(VSTATE_SWRESET_COUNT);
    	break ;
    case VSTATE_SyncWait:
        #ifdef AUTO_SEARCH_EQ_SETTING
        SyncWaitCounter++;
        #endif
        HDMIRX_WriteI2C_Byte(REG_RX_REGPKTFLAG_CTRL,0);
        SetIntMask1(~(B_SCDTOFF|B_VIDMODE_CHG),0);
        if(ucRevision >= 0xA3)
        {
            uc = HDMIRX_ReadI2C_Byte(0x3C);
            uc &= ~(1<<2);
            HDMIRX_WriteI2C_Byte(0x3C, uc);
        }
		bVSIpresent=FALSE ;
        SetVideoMute(ON);
        AssignVideoTimerTimeout(VSTATE_MISS_SYNC_COUNT);
        break ;
    #ifndef DISABLE_COLOR_DEPTH_RESET
    case VSTATE_ColorDetectReset:

        uc = HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT_MASK1) &(~B_SCDTOFF) ;
        HDMIRX_WriteI2C_Byte(REG_RX_INTERRUPT_MASK1, uc) ;
        ClearIntFlags(B_CLR_MODE_INT);
        uc = HDMIRX_ReadI2C_Byte(REG_RX_CDEPTH_CTRL) & (~B_RSTCD);
        HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,B_RSTCD|uc) ;
        HDMIRX_WriteI2C_Byte(REG_RX_CDEPTH_CTRL,uc) ;
        AssignVideoTimerTimeout(MS_TimeOut(400));
        break ;
    #endif

    case VSTATE_SyncChecking:
        #ifdef AUTO_SEARCH_EQ_SETTING
        SyncCheckCounter++;
        #endif
        HDMIRX_WriteI2C_Byte(REG_RX_GEN_PKT_TYPE, 0x81); // set default general control packet received in 0xA8
        HDMIRX_WriteI2C_Byte(REG_RX_REGPKTFLAG_CTRL, B_INT_GENERAL_EVERY) ;
        AssignVideoTimerTimeout(VSATE_CONFIRM_SCDT_COUNT);
        HDMIRX_ReadI2C_Byte(REG_RX_CHANNEL_ERR) ; // read 0x85 for clear CDR counter in reg9A.
        HDMIRX_DEBUG_PRINTF(("switch VSTATE_SyncChecking, reg9A = %02X\n",(int)HDMIRX_ReadI2C_Byte(0x9A))) ;
        break ;
	case VSTATE_HDCP_Reset:
        SetVideoMute(ON);
		AssignVideoTimerTimeout(HDCP_WAITING_TIMEOUT);
		break ;
    case VSTATE_VideoOn:

        #ifdef AUTO_SEARCH_EQ_SETTING
        VideoOnTick = 0;
        EccErrorCounter = 0;
        #endif

        SetIntMask1(~(B_SCDTOFF|B_VIDMODE_CHG),(B_SCDTOFF|B_VIDMODE_CHG));
        AssignVideoTimerTimeout(5);
        // AcceptCDRReset = TRUE ;

        AssignVideoTimerTimeout(CDRRESET_TIMEOUT);
        if(!NewAVIInfoFrameF)
        {
            SetVideoInputFormatWithoutInfoFrame(F_MODE_RGB24);
            SetColorimetryByMode(/*&SyncInfo*/);
            SetColorSpaceConvert();
        }

        if(!IsHDMIRXHDMIMode())
        {
            SetIntMask1(~(B_SCDTOFF|B_PWR5VOFF),(B_SCDTOFF|B_PWR5VOFF));
            SetVideoMute(OFF); // turned on Video.
            SwitchAudioState(ASTATE_AudioOff);
            NewAVIInfoFrameF = FALSE ;
        }
        else
        {

            if(NewAVIInfoFrameF)
            {
                SetNewInfoVideoOutput();
            }

        #ifdef SUPPORT_REPEATER
            if(bHDCPMode & HDCP_REPEATER)
            {
                SetIntMask3(0,B_ECCERR|B_R_AUTH_DONE|B_R_AUTH_START);
        	}
        	else
        #endif // SUPPORT_REPEATER
        	{
                SetIntMask3(~(B_R_AUTH_DONE|B_R_AUTH_START),B_ECCERR);
        	}
            SetIntMask2(~(B_NEW_AVI_PKG|B_PKT_SET_MUTE|B_PKT_CLR_MUTE),(B_NEW_AVI_PKG|B_PKT_SET_MUTE|B_PKT_CLR_MUTE));
            SetIntMask1(~(B_SCDTOFF|B_PWR5VOFF),(B_SCDTOFF|B_PWR5VOFF));
            SetIntMask4(0,B_M_RXCKON_DET);

            MuteByPKG =(HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST)& B_AVMUTE)?TRUE:FALSE ;
            bIgnoreVideoChgEvent = MuteByPKG ;

            SetVideoMute(MuteByPKG); // turned on Video.
            ucHDMIAudioErrorCount = 0 ;
            // 2009/02/10 added by Jau-Chih.Tseng@ite.com.tw
            ucAudioSampleClock=DEFAULT_START_FIXED_AUD_SAMPLEFREQ ;
            // ucAudioSampleClock=3 ;
            //~Jau-Chih.Tseng@ite.com.tw 2009/02/10
            uc = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CTRL);
            uc &= ~B_FORCE_FS ;
            HDMIRX_WriteI2C_Byte(REG_RX_AUDIO_CTRL, uc);

            uc = HDMIRX_ReadI2C_Byte(REG_RX_MCLK_CTRL)&(~B_CTSINI_EN);
            HDMIRX_WriteI2C_Byte(REG_RX_MCLK_CTRL, uc);

            HDMIRX_DEBUG_PRINTF2(("[%s:%d] reg%02X = %02X\n",__FILE__,__LINE__,(int)REG_RX_AUDIO_CTRL,(int)uc));

			#ifndef DISABLE_AUDIO_SUPPORT
				EnableAudio();
			#else
            	SwitchAudioState(ASTATE_AudioOff);
			#endif
        }

		currHTotal = s_CurrentVM.HTotal ;
		currXcnt = s_CurrentVM.xCnt ;
		currScanMode = s_CurrentVM.ScanMode ;

        break ;
    }
}

void
SwitchAudioState(Audio_State_Type state)
{
#ifdef DISABLE_AUDIO_SUPPORT
    AState = ASTATE_AudioOff ;
    SetAudioMute(TRUE);
	return ;
#else
    AState = state ;
    HDMIRX_DEBUG_PRINTF(("AState -> %s\n",AStateStr[AState]));

    switch(AState)
    {
    case ASTATE_AudioOff:
        SetAudioMute(TRUE);
        break ;

    case ASTATE_WaitForReady:
        AssignAudioTimerTimeout(AUDIO_READY_TIMEOUT);
        break ;
    case ASTATE_AudioOn:
        SetAudioMute(MuteByPKG);
        AssignAudioTimerTimeout(AUDIO_CLEARERROR_TIMEOUT); // set one second adjusting to reset ucAudioErrorCount.
        if(MuteByPKG)
        {
            HDMIRX_DEBUG_PRINTF(("AudioOn, but still in mute.\n"));
            EnableMuteProcessTimer();
        }
        break ;
    }
#endif
}



static void
DumpSyncInfo(HDMI_VTiming *pVTiming)
{
    double VFreq ;
    HDMIRX_DEBUG_PRINTF2(("{%4d,",pVTiming->HActive));
    HDMIRX_DEBUG_PRINTF2(("%4d,",pVTiming->VActive));
    HDMIRX_DEBUG_PRINTF2(("%4d,",pVTiming->HTotal));
    HDMIRX_DEBUG_PRINTF2(("%4d,",pVTiming->VTotal));
    HDMIRX_DEBUG_PRINTF2(("%8ld,",pVTiming->PCLK));
    HDMIRX_DEBUG_PRINTF2(("0x%02x,",pVTiming->xCnt));
    HDMIRX_DEBUG_PRINTF2(("%3d,",pVTiming->HFrontPorch));
    HDMIRX_DEBUG_PRINTF2(("%3d,",pVTiming->HSyncWidth));
    HDMIRX_DEBUG_PRINTF2(("%3d,",pVTiming->HBackPorch));
    HDMIRX_DEBUG_PRINTF2(("%2d,",pVTiming->VFrontPorch));
    HDMIRX_DEBUG_PRINTF2(("%2d,",pVTiming->VSyncWidth));
    HDMIRX_DEBUG_PRINTF2(("%2d,",pVTiming->VBackPorch));
    HDMIRX_DEBUG_PRINTF2(("%s,",pVTiming->ScanMode?"PROG":"INTERLACE"));
    HDMIRX_DEBUG_PRINTF2(("%s,",pVTiming->VPolarity?"Vpos":"Vneg"));
    HDMIRX_DEBUG_PRINTF2(("%s},",pVTiming->HPolarity?"Hpos":"Hneg"));
    VFreq =(double)pVTiming->PCLK ;
    VFreq *= 1000.0 ;
    VFreq /= pVTiming->HTotal ;
    VFreq /= pVTiming->VTotal ;
    HDMIRX_DEBUG_PRINTF2(("/* %dx%d@%5.2lfHz */\n",pVTiming->HActive,pVTiming->VActive,VFreq));
}

static BOOL
bGetSyncInfo()
{

    BYTE uc1, uc2, uc3 ;
#ifdef USE_MODE_TABLE
    long diff ;
    int i ;
#endif
//    pVTiming = NULL ;

//    pVTiming = &s_CurrentVM ;
    uc1 = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_L);
    uc2 = HDMIRX_ReadI2C_Byte(REG_RX_VID_HTOTAL_H);
    uc3 = HDMIRX_ReadI2C_Byte(REG_RX_VID_HACT_L);

    s_CurrentVM.HTotal =((WORD)(uc2&0xF)<<8)|(WORD)uc1;
    s_CurrentVM.HActive =((WORD)(uc2 & 0x70)<<4)|(WORD)uc3 ;
    if((s_CurrentVM.HActive |(1<<11))<s_CurrentVM.HTotal)
    {
        s_CurrentVM.HActive |=(1<<11);
    }
    uc1 = HDMIRX_ReadI2C_Byte(REG_RX_VID_HSYNC_WID_L);
    uc2 = HDMIRX_ReadI2C_Byte(REG_RX_VID_HSYNC_WID_H);
    uc3 = HDMIRX_ReadI2C_Byte(REG_RX_VID_H_FT_PORCH_L);

    s_CurrentVM.HSyncWidth =((WORD)(uc2&0x1)<<8)|(WORD)uc1;
    s_CurrentVM.HFrontPorch =((WORD)(uc2 & 0xf0)<<4)|(WORD)uc3 ;
    s_CurrentVM.HBackPorch = s_CurrentVM.HTotal - s_CurrentVM.HActive - s_CurrentVM.HSyncWidth - s_CurrentVM.HFrontPorch ;

    uc1 = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_L);
    uc2 = HDMIRX_ReadI2C_Byte(REG_RX_VID_VTOTAL_H);
    uc3 = HDMIRX_ReadI2C_Byte(REG_RX_VID_VACT_L);

    s_CurrentVM.VTotal =((WORD)(uc2&0x7)<<8)|(WORD)uc1;
    s_CurrentVM.VActive =((WORD)(uc2 & 0x30)<<4)|(WORD)uc3 ;
    if((s_CurrentVM.VActive |(1<<10))<s_CurrentVM.VTotal)
    {
        s_CurrentVM.VActive |=(1<<10);
    }

    s_CurrentVM.VBackPorch = HDMIRX_ReadI2C_Byte(REG_RX_VID_VSYNC2DE);
    s_CurrentVM.VFrontPorch = HDMIRX_ReadI2C_Byte(REG_RX_VID_V_FT_PORCH);
    s_CurrentVM.VSyncWidth = 0 ;

    s_CurrentVM.ScanMode =(HDMIRX_ReadI2C_Byte(REG_RX_VID_MODE)&B_INTERLACE)?INTERLACE:PROG ;

    s_CurrentVM.xCnt = HDMIRX_ReadI2C_Byte(REG_RX_VID_XTALCNT_128PEL);

    if(s_CurrentVM.xCnt)
    {
        s_CurrentVM.PCLK = 128L * 27000L / s_CurrentVM.xCnt ;
    }
    else
    {
        HDMIRX_DEBUG_PRINTF(("s_CurrentVM.xCnt == %02x\n",s_CurrentVM.xCnt));
        s_CurrentVM.PCLK = 1234 ;
        /*
        for(i = 0x58 ; i < 0x66 ; i++)
        {
            HDMIRX_DEBUG_PRINTF(("HDMIRX_ReadI2C_Byte(%02x)= %02X\n",i,(int)HDMIRX_ReadI2C_Byte(i)));
        }
        */
        return FALSE ;
    }

    // HDMIRX_DEBUG_PRINTF(("Current Get: ")); DumpSyncInfo(&s_CurrentVM);
    // HDMIRX_DEBUG_PRINTF(("Matched %d Result in loop 1: ", i)); DumpSyncInfo(pVTiming);

#ifndef USE_MODE_TABLE
	if((s_CurrentVM.VActive > 200)
		&&(s_CurrentVM.VTotal>s_CurrentVM.VActive)
		&&(s_CurrentVM.HActive > 300)
		&&(s_CurrentVM.HTotal>s_CurrentVM.HActive))
	{
		return TRUE ;
	}
#else
    #pragma message("USE_MODE_TABLE definition enabled.")
	// return TRUE ;
    for(i = 0 ; i < SizeofVMTable ; i++)
    {
        // 2006/10/17 modified by jjtseng
        // Compare PCLK in 3% difference instead of comparing xCnt

        // diff =(long)s_VMTable[i].xCnt -(long)s_CurrentVM.xCnt ;
        // if(ABS(diff)> 1)
        // {
        //     continue ;
        // }
        //~jjtseng 2006/10/17

		// 2011/02/18 modified by jau-chih.tseng@ite.com.tw
		// to avoid the compiler calculation error. Change calculating
		// method.
        // diff = ABS(s_VMTable[i].PCLK - s_CurrentVM.PCLK);
        if(s_VMTable[i].PCLK - s_CurrentVM.PCLK)
		{
			diff = s_VMTable[i].PCLK - s_CurrentVM.PCLK;
		}
		else
		{

	        diff = s_CurrentVM.PCLK - s_VMTable[i].PCLK;
		}
		//~jau-chih.tseng@ite.com.tw 2011/02/18

        diff *= 100 ;
        diff /= s_VMTable[i].PCLK ;

        if(diff > 3)
        {
            // over 3%
            continue ;
        }

        if(s_VMTable[i].HActive != s_CurrentVM.HActive)
        {
            continue ;
        }

        //if(s_VMTable[i].VActive != s_CurrentVM.VActive)
        //{
        //    continue ;
        //}

        if((long)s_VMTable[i].HTotal >=(long)s_CurrentVM.HTotal )
        {
        diff =(long)s_VMTable[i].HTotal -(long)s_CurrentVM.HTotal ;
        }
        else
        {
            diff = (long)s_CurrentVM.HTotal  - (long)s_VMTable[i].HTotal  ;
        }
        if(diff>4)
        {
            continue ;
        }

        if((long)s_VMTable[i].VActive >= (long)s_CurrentVM.VActive )
        {
        diff =(long)s_VMTable[i].VActive -(long)s_CurrentVM.VActive ;
        }
        else
        {
            diff = (long)s_CurrentVM.VActive  - (long)s_VMTable[i].VActive  ;
        }
        if(diff>10)
        {
            continue ;
        }

        if((long)s_VMTable[i].VTotal >= (long)s_CurrentVM.VTotal )
        {
        diff =(long)s_VMTable[i].VTotal -(long)s_CurrentVM.VTotal ;
        }
        else
        {
            diff = (long)s_CurrentVM.VTotal  - (long)s_VMTable[i].VTotal  ;
        }
        if(diff>40)
        {
            continue ;
        }

        if(s_VMTable[i].ScanMode != s_CurrentVM.ScanMode)
        {
            continue ;
        }

        s_CurrentVM = s_VMTable[i] ;
        // HDMIRX_DEBUG_PRINTF(("Matched %d Result in loop 1: ", i)); DumpSyncInfo(pVTiming);
        return TRUE ;
    }


    for(i = 0 ; i < SizeofVMTable ; i++)
    {
        if( s_VMTable[i].PCLK >= s_CurrentVM.PCLK)
        {
            diff = s_VMTable[i].PCLK  -  s_CurrentVM.PCLK ;
        }
        else
        {
            diff = s_CurrentVM.PCLK - s_VMTable[i].PCLK   ;
        }
        diff *= 100 ;
        diff /= s_VMTable[i].PCLK ;

        if(diff > 3)
        {
            // over 3%
            continue ;
        }

        if(s_VMTable[i].HActive != s_CurrentVM.HActive)
        {
            continue ;
        }

        //if(s_VMTable[i].VActive != s_CurrentVM.VActive)
        //{
        //    continue ;
        //}

        if((long)s_VMTable[i].HTotal >=(long)s_CurrentVM.HTotal )
        {
        diff =(long)s_VMTable[i].HTotal -(long)s_CurrentVM.HTotal ;
        }
        else
        {
            diff = (long)s_CurrentVM.HTotal  - (long)s_VMTable[i].HTotal  ;
        }
        if(diff>4)
        {
            continue ;
        }

        if((long)s_VMTable[i].VActive >= (long)s_CurrentVM.VActive )
        {
        diff =(long)s_VMTable[i].VActive -(long)s_CurrentVM.VActive ;
        }
        else
        {
            diff = (long)s_CurrentVM.VActive  - (long)s_VMTable[i].VActive  ;
        }
        if(diff>10)
        {
            continue ;
        }

        if((long)s_VMTable[i].VTotal >=(long)s_CurrentVM.VTotal )
        {
        diff =(long)s_VMTable[i].VTotal -(long)s_CurrentVM.VTotal ;
        }
        else
        {
            diff = (long)s_CurrentVM.VTotal  - (long)s_VMTable[i].VTotal  ;
        }
        if(diff>40)
        {
            continue ;
        }
        s_CurrentVM = s_VMTable[i] ;
        // HDMIRX_DEBUG_PRINTF(("Matched %d Result in loop 2: ", i)); DumpSyncInfo(pVTiming);
        return TRUE ;
    }
#endif
    return FALSE ;
}




#define SIZE_OF_CSCOFFSET (REG_RX_CSC_RGBOFF - REG_RX_CSC_YOFF + 1)
#define SIZE_OF_CSCMTX (REG_RX_CSC_MTX33_H - REG_RX_CSC_MTX11_L + 1)
#define SIZE_OF_CSCGAIN (REG_RX_CSC_GAIN3V_H - REG_RX_CSC_GAIN1V_L + 1)

///////////////////////////////////////////////////////////
// video.h
///////////////////////////////////////////////////////////
void
Video_Handler()
{
    // SYNC_INFO SyncInfo, NewSyncInfo ;
    BOOL bHDMIMode;
    BYTE uc ;

    if(VState == VSTATE_ModeDetecting)
    {
        HDMIRX_DEBUG_PRINTF(("Video_Handler, VState = VSTATE_ModeDetecting.\n"));
        // HDMIRX_DEBUG_PRINTF(("Video Mode Detecting ... , REG_RX_RST_CTRL = %02X -> ",(int)HDMIRX_ReadI2C_Byte(REG_RX_RST_CTRL)));
        // HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL,HDMIRX_ReadI2C_Byte(REG_RX_RST_CTRL)& ~B_HDCPRST|B_EN_AUTOVDORST);
        // HDMIRX_DEBUG_PRINTF(("%02X\n",(int)HDMIRX_ReadI2C_Byte(REG_RX_RST_CTRL)));
        uc = HDMIRX_ReadI2C_Byte(0x9A) ;
        HDMIRX_DEBUG_PRINTF(("Video_Handler(): reg9A = %02X\n", (int)uc)) ;

        if( uc == 0xFF )
        {
            HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_CDRRST|B_EN_AUTOVDORST) ;
            HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST) ;
            SwitchVideoState(VSTATE_SyncWait);
            return ;
        }
        ClearIntFlags(B_CLR_MODE_INT);

        bGetSyncInfo();

        bHDMIMode = IsHDMIRXHDMIMode();

        if(!bHDMIMode)
        {
            HDMIRX_DEBUG_PRINTF(("This is DVI Mode.\n"));
            NewAVIInfoFrameF = FALSE ;
        }

        // GetSyncInfo(&NewSyncInfo);

        // if(CompareSyncInfo(&NewSyncInfo,&SyncInfo))
        if(HDMIRX_ReadI2C_Byte(REG_RX_INTERRUPT1)&(B_SCDTOFF|B_PWR5VOFF))
        {
            SwitchVideoState(VSTATE_SyncWait);
            // SwitchAudioState(ASTATE_AudioOff); // SwitchVideoState will switch audio state to AudioOff if any non VideoOn mode.
        }
        else
        {
            // HDCP_Reset(); // even though in DVI mode, Tx also can set HDCP.

            SwitchVideoState(VSTATE_VideoOn);
        }

        return ;
    }
}

static void
SetVideoInputFormatWithoutInfoFrame(BYTE bInMode)
{
    BYTE uc ;

    // HDMIRX_DEBUG_PRINTF(("SetVideoInputFormat: NewAVIInfoFrameF = %s, bInMode = %d",(NewAVIInfoFrameF==TRUE)?"TRUE":"FALSE",bInMode));
    // only set force input color mode selection under no AVI Info Frame case
    uc = HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL);
    uc |= B_FORCE_COLOR_MODE ;
    bInputVideoMode &= ~F_MODE_CLRMOD_MASK ;
    // bInputVideoMode |=(bInMode)&F_MODE_CLRMOD_MASK ;

    switch(bInMode)
    {
    case F_MODE_YUV444:
        uc &= ~(M_INPUT_COLOR_MASK<<O_INPUT_COLOR_MODE);
        uc |= B_INPUT_YUV444 << O_INPUT_COLOR_MODE ;
        bInputVideoMode |= F_MODE_YUV444 ;
        break ;
    case F_MODE_YUV422:
        uc &= ~(M_INPUT_COLOR_MASK<<O_INPUT_COLOR_MODE);
        uc |= B_INPUT_YUV422 << O_INPUT_COLOR_MODE ;
        bInputVideoMode |= F_MODE_YUV422 ;
        break ;
    case F_MODE_RGB24:
        uc &= ~(M_INPUT_COLOR_MASK<<O_INPUT_COLOR_MODE);
        uc |= B_INPUT_RGB24 << O_INPUT_COLOR_MODE ;
        bInputVideoMode |= F_MODE_RGB24 ;
        break ;
    default:
        HDMIRX_DEBUG_PRINTF(("Invalid Color mode %d, ignore.\n", bInMode));
        return ;
    }
    HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL, uc);

}

static void
SetColorimetryByMode(/*PSYNC_INFO pSyncInfo*/)
{
    // USHORT HRes, VRes ;
    bInputVideoMode &= ~F_MODE_ITU709 ;
    // HRes = pVTiming->HActive ;
    // VRes = pVTiming->VActive ;
    // VRes *=(pSyncInfo->Mode & F_MODE_INTERLACE)?2:1 ;
    if((s_CurrentVM.HActive == 1920)||(s_CurrentVM.HActive == 1280 && s_CurrentVM.VActive == 720))
    {
        // only 1080p, 1080i, and 720p use ITU 709
        bInputVideoMode |= F_MODE_ITU709 ;
    }
    else
    {
        // 480i,480p,576i,576p,and PC mode use 601
        bInputVideoMode &= ~F_MODE_ITU709 ; // set mode as ITU601
    }
}

void
SetVideoInputFormatWithInfoFrame()
{
    BYTE uc ;
    BOOL bAVIColorModeIndicated = FALSE ;
    BOOL bOldInputVideoMode = bInputVideoMode ;

    HDMIRX_DEBUG_PRINTF(("SetVideoInputFormatWithInfoFrame(): "));

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB1);
    HDMIRX_DEBUG_PRINTF(("REG_RX_AVI_DB1 %02X get uc %02X ",(int)REG_RX_AVI_DB1,(int)uc));

    prevAVIDB1 = uc ;
    bInputVideoMode &= ~F_MODE_CLRMOD_MASK ;

    switch((uc>>O_AVI_COLOR_MODE)&M_AVI_COLOR_MASK)
    {
    case B_AVI_COLOR_YUV444:
        HDMIRX_DEBUG_PRINTF(("input YUV444 mode "));
        bInputVideoMode |= F_MODE_YUV444 ;
        break ;
    case B_AVI_COLOR_YUV422:
        HDMIRX_DEBUG_PRINTF(("input YUV422 mode "));
        bInputVideoMode |= F_MODE_YUV422 ;
        break ;
    case B_AVI_COLOR_RGB24:
        HDMIRX_DEBUG_PRINTF(("input RGB24 mode "));
        bInputVideoMode |= F_MODE_RGB24 ;
        break ;
    default:
        HDMIRX_DEBUG_PRINTF(("Invalid input color mode, ignore.\n"));
        return ; // do nothing.
    }

    if((bInputVideoMode & F_MODE_CLRMOD_MASK)!=(bOldInputVideoMode & F_MODE_CLRMOD_MASK))
    {
        HDMIRX_DEBUG_PRINTF(("Input Video mode changed."));
    }

    uc = HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL);
    uc &= ~B_FORCE_COLOR_MODE ; // color mode indicated by Info Frame.
    HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL, uc);

    HDMIRX_DEBUG_PRINTF(("\n"));
}

BOOL
SetColorimetryByInfoFrame()
{
    BYTE uc ;
    BOOL bOldInputVideoMode = bInputVideoMode ;

    HDMIRX_DEBUG_PRINTF(("SetColorimetryByInfoFrame: NewAVIInfoFrameF = %s ",NewAVIInfoFrameF?"TRUE":"FALSE"));

    if(NewAVIInfoFrameF)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_AVI_DB2);
        uc &= M_AVI_CLRMET_MASK<<O_AVI_CLRMET ;
        if(uc ==(B_AVI_CLRMET_ITU601<<O_AVI_CLRMET))
        {
            HDMIRX_DEBUG_PRINTF(("F_MODE_ITU601\n"));
            bInputVideoMode &= ~F_MODE_ITU709 ;
            return TRUE ;
        }
        else if(uc ==(B_AVI_CLRMET_ITU709<<O_AVI_CLRMET))
        {
            HDMIRX_DEBUG_PRINTF(("F_MODE_ITU709\n"));
            bInputVideoMode |= F_MODE_ITU709 ;
            return TRUE ;
        }
        // if no uc, ignore
        if((bInputVideoMode & F_MODE_ITU709)!=(bOldInputVideoMode & F_MODE_ITU709))
        {
            HDMIRX_DEBUG_PRINTF(("Input Video mode changed."));
            // SetVideoMute(ON); // turned off Video for input color format change .
        }
    }
    HDMIRX_DEBUG_PRINTF(("\n"));
    return FALSE ;
}

void
SetColorSpaceConvert()
{
    BYTE uc, csc = 0;
    BYTE filter = 0 ; // filter is for Video CTRL DN_FREE_GO, EN_DITHER, and ENUDFILT

    // HDMIRX_DEBUG_PRINTF(("Input mode is YUV444 "));
    switch(bOutputVideoMode&F_MODE_CLRMOD_MASK)
    {
    #if defined(SUPPORT_OUTPUTYUV444)
    case F_MODE_YUV444:
        // HDMIRX_DEBUG_PRINTF(("Output mode is YUV444\n"));
	    switch(bInputVideoMode&F_MODE_CLRMOD_MASK)
	    {
	    case F_MODE_YUV444:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV444\n"));
	        csc = B_CSC_BYPASS ;
	        break ;
	    case F_MODE_YUV422:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV422\n"));
            csc = B_CSC_BYPASS ;
            if(bOutputVideoMode & F_MODE_EN_UDFILT)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER ;
            }

            if(bOutputVideoMode & F_MODE_EN_DITHER)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER | B_RX_DNFREE_GO ;
            }

            break ;
	    case F_MODE_RGB24:
            // HDMIRX_DEBUG_PRINTF(("Input mode is RGB444\n"));
            csc = B_CSC_RGB2YUV ;
            break ;
	    }
        break ;
    #endif

    #if defined(SUPPORT_OUTPUTYUV422)

    case F_MODE_YUV422:
	    switch(bInputVideoMode&F_MODE_CLRMOD_MASK)
	    {
	    case F_MODE_YUV444:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV444\n"));
	        if(bOutputVideoMode & F_MODE_EN_UDFILT)
	        {
	            filter |= B_RX_EN_UDFILTER ;
	        }
	        csc = B_CSC_BYPASS ;
	        break ;
	    case F_MODE_YUV422:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV422\n"));
            csc = B_CSC_BYPASS ;

            // if output is YUV422 and 16 bit or 656, then the dither is possible when
            // the input is YUV422 with 24bit input, however, the dither should be selected
            // by customer, thus the requirement should set in ROM, no need to check
            // the register value .
            if(bOutputVideoMode & F_MODE_EN_DITHER)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER | B_RX_DNFREE_GO ;
            }
	    	break ;
	    case F_MODE_RGB24:
            // HDMIRX_DEBUG_PRINTF(("Input mode is RGB444\n"));
            if(bOutputVideoMode & F_MODE_EN_UDFILT)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER ;
            }
            csc = B_CSC_RGB2YUV ;
	    	break ;
	    }
	    break ;
    #endif

    #if defined(SUPPORT_OUTPUTRGB)
    case F_MODE_RGB24:
        // HDMIRX_DEBUG_PRINTF(("Output mode is RGB24\n"));
	    switch(bInputVideoMode&F_MODE_CLRMOD_MASK)
	    {
	    case F_MODE_YUV444:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV444\n"));
	        csc = B_CSC_YUV2RGB ;
	        break ;
	    case F_MODE_YUV422:
            // HDMIRX_DEBUG_PRINTF(("Input mode is YUV422\n"));
            csc = B_CSC_YUV2RGB ;
            if(bOutputVideoMode & F_MODE_EN_UDFILT)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER ;
            }
            if(bOutputVideoMode & F_MODE_EN_DITHER)// RGB24 to YUV422 need up/dn filter.
            {
                filter |= B_RX_EN_UDFILTER | B_RX_DNFREE_GO ;
            }
	    	break ;
	    case F_MODE_RGB24:
            // HDMIRX_DEBUG_PRINTF(("Input mode is RGB444\n"));
            csc = B_CSC_BYPASS ;
	    	break ;
	    }
	    break ;
    #endif
    }


    #if defined(SUPPORT_OUTPUTYUV)
    // set the CSC associated registers
    if(csc == B_CSC_RGB2YUV)
    {
        // HDMIRX_DEBUG_PRINTF(("CSC = RGB2YUV "));
        if(bInputVideoMode & F_MODE_ITU709)
        {
            HDMIRX_DEBUG_PRINTF(("ITU709 "));

            if(bInputVideoMode & F_MODE_16_235)
            {
                HDMIRX_DEBUG_PRINTF((" 16-235\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_16_235,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_RGB2YUV_ITU709_16_235,18);
            }
            else
            {
                HDMIRX_DEBUG_PRINTF((" 0-255\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_0_255,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_RGB2YUV_ITU709_0_255,18);
            }
        }
        else
        {
            HDMIRX_DEBUG_PRINTF(("ITU601 "));
            if(bInputVideoMode & F_MODE_16_235)
            {
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_16_235,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_RGB2YUV_ITU601_16_235,18);
                HDMIRX_DEBUG_PRINTF((" 16-235\n"));
            }
            else
            {
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_0_255,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_RGB2YUV_ITU601_0_255,18);
                HDMIRX_DEBUG_PRINTF((" 0-255\n"));
            }
        }
    }
    #endif
    #if defined(SUPPORT_OUTPUTRGB)
	if(csc == B_CSC_YUV2RGB)
    {
        HDMIRX_DEBUG_PRINTF(("CSC = YUV2RGB "));
        if(bInputVideoMode & F_MODE_ITU709)
        {
            HDMIRX_DEBUG_PRINTF(("ITU709 "));
            if(bOutputVideoMode & F_MODE_16_235)
            {
                HDMIRX_DEBUG_PRINTF(("16-235\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_16_235,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_YUV2RGB_ITU709_16_235,18);
            }
            else
            {
                HDMIRX_DEBUG_PRINTF(("0-255\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_0_255,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_YUV2RGB_ITU709_0_255,18);
            }
        }
        else
        {
            HDMIRX_DEBUG_PRINTF(("ITU601 "));
            if(bOutputVideoMode & F_MODE_16_235)
            {
                HDMIRX_DEBUG_PRINTF(("16-235\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_16_235,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_YUV2RGB_ITU601_16_235,18);
            }
            else
            {
                HDMIRX_DEBUG_PRINTF(("0-255\n"));
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_YOFF,bCSCOffset_0_255,3);
                HDMIRX_WriteI2C_ByteN(REG_RX_CSC_MTX11_L,bCSCMtx_YUV2RGB_ITU601_0_255,18);
            }
        }

    }
	#endif // SUPPORT_OUTPUTRGB


    uc = HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL);
    uc =(uc & ~M_CSC_SEL_MASK)|csc ;
    HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL,uc);

    // set output Up/Down Filter, Dither control

    uc = HDMIRX_ReadI2C_Byte(REG_RX_VIDEO_CTRL1);
    uc &= ~(B_RX_DNFREE_GO|B_RX_EN_DITHER|B_RX_EN_UDFILTER);
    uc |= filter ;
    HDMIRX_WriteI2C_Byte(REG_RX_VIDEO_CTRL1, uc);
}


void
SetDVIVideoOutput()
{
    // SYNC_INFO SyncInfo ;
    // GetSyncInfo(&SyncInfo);
    SetVideoInputFormatWithoutInfoFrame(F_MODE_RGB24);
    SetColorimetryByMode(/*&SyncInfo*/);
    SetColorSpaceConvert();
}

void
SetNewInfoVideoOutput()
{
    SetVideoInputFormatWithInfoFrame();
    SetColorimetryByInfoFrame();
    SetColorSpaceConvert();
    // DumpHDMIRXReg();
}

void
SetHDMIRXVideoOutputFormat(BYTE bOutputMapping, BYTE bOutputType, BYTE bOutputColorMode)
{
    BYTE uc ;
    SetVideoMute(ON);

    HDMIRX_DEBUG_PRINTF3(("SetHDMIRXVideoOutputFormat(%02X,%02X,%02X)\n",(int)bOutputMapping,(int)bOutputType,(int)bOutputColorMode));
    HDMIRX_WriteI2C_Byte(REG_RX_VIDEO_CTRL1,bOutputType);
    HDMIRX_WriteI2C_Byte(REG_RX_VIDEO_MAP,bOutputMapping);
    bOutputVideoMode&=~F_MODE_CLRMOD_MASK;

    bOutputVideoMode |= bOutputColorMode&F_MODE_CLRMOD_MASK ;
    uc = HDMIRX_ReadI2C_Byte(REG_RX_PG_CTRL2)& ~(M_OUTPUT_COLOR_MASK<<O_OUTPUT_COLOR_MODE);

    switch(bOutputVideoMode&F_MODE_CLRMOD_MASK)
    {
    case F_MODE_YUV444:
        uc |= B_OUTPUT_YUV444 << O_OUTPUT_COLOR_MODE ;
        break ;
    case F_MODE_YUV422:
        uc |= B_OUTPUT_YUV422 << O_OUTPUT_COLOR_MODE ;
        break ;
    }
    HDMIRX_DEBUG_PRINTF3(("write %02X %02X\n",(int)REG_RX_PG_CTRL2,(int)uc));
    HDMIRX_WriteI2C_Byte(REG_RX_PG_CTRL2, uc);

    if(VState == VSTATE_VideoOn)
    {
        if(IsHDMIRXHDMIMode())
        {
            SetNewInfoVideoOutput();
        }
        else
        {
            SetDVIVideoOutput();
        }
        SetVideoMute(MuteByPKG);
    }

}


///////////////////////////////////////////////////////////
// Audio Function
///////////////////////////////////////////////////////////


void
ResetAudio()
{
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_AUDRST|B_EN_AUTOVDORST);
    delay1ms(1);
    HDMIRX_WriteI2C_Byte(REG_RX_RST_CTRL, B_EN_AUTOVDORST);
}

static void
SetALLMute()
{
    BYTE uc ;
    uc = (bVideoOutputOption==VIDEO_ON)?B_TRI_ALL:(B_TRI_ALL|B_TRI_VIDEO);
    SetMUTE(B_VDO_MUTE_DISABLE,uc);
}

void
SetHWMuteCTRL(BYTE AndMask, BYTE OrMask)
{
    BYTE uc = 0;

    if(AndMask)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_HWMUTE_CTRL);
    }
    uc &= AndMask ;
    uc |= OrMask ;
    HDMIRX_WriteI2C_Byte(REG_RX_HWMUTE_CTRL,uc);

}

void
SetVideoMute(BOOL bMute)
{
    BYTE uc ;
#ifdef SUPPORT_REPEATER
    if(bHDCPMode & HDCP_REPEATER)
    {
        uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
        uc &= ~(B_TRI_VIDEO | B_TRI_VDIO);
        uc |= B_VDO_MUTE_DISABLE ;
        HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
        return ;
    }

#endif //SUPPORT_REPEATER
    if(bMute)
    {
        // 2009/11/04 added by jau-chih.tseng@ite.com.tw
        // implement the video gatting for video output.
		uc = HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL);
		uc |= B_VDIO_GATTING | B_VIO_SEL ; // video data set to low.
		HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL, uc);

        uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
        uc &= ~(B_TRI_VDIO) ;
        if( VState != VSTATE_VideoOn )
        {
            uc |= B_TRI_VIDEO|B_TRI_VDIO ;
        }

        HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
        //~jau-chih.tseng@ite.com.tw 2009/11/04

    }
    else
    {
        if(VState == VSTATE_VideoOn)
        {
            // modified by jjtseng 2012/07/24
            // the reset will gatting the video sync out and may make some scalar crazy.
            uc = HDMIRX_ReadI2C_Byte(REG_RX_VIDEO_CTRL1);
            if( uc & B_CCIR656 ) // just reset under CCIR656 mode.
            {
                HDMIRX_WriteI2C_Byte(REG_RX_VIDEO_CTRL1,uc|B_656FFRST);
                HDMIRX_WriteI2C_Byte(REG_RX_VIDEO_CTRL1,uc&(~B_656FFRST));
            }
            //~jjtseng

            if(HDMIRX_ReadI2C_Byte(REG_RX_VID_INPUT_ST)&B_AVMUTE)
            {
                uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
                uc &= ~(B_TRI_VDIO );
                uc |= B_VDO_MUTE_DISABLE ;
                HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
            }
            else
            {
                uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
                uc &= ~B_VDO_MUTE_DISABLE ;
                // HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);

        		// enable video io gatting
        		// uc = HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL);
        		uc |= B_TRI_VDIO ;
        		HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
        		HDMIRX_DEBUG_PRINTF(("reg %02X <- %02X = %02X\n",(int)REG_RX_TRISTATE_CTRL,(int)uc, (int)HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL)));
        		uc &= ~(B_TRI_VDIO|B_TRI_VIDEO) ;
        		HDMIRX_WriteI2C_Byte(REG_RX_TRISTATE_CTRL, uc);
        		HDMIRX_DEBUG_PRINTF(("reg %02X <- %02X = %02X\n",(int)REG_RX_TRISTATE_CTRL,(int)uc, (int)HDMIRX_ReadI2C_Byte(REG_RX_TRISTATE_CTRL)));

        		uc = HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL);
        		uc |= B_VDIO_GATTING|B_VIO_SEL ;
        		HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL, uc);
        		HDMIRX_DEBUG_PRINTF(("reg %02X <- %02X = %02X\n",(int)REG_RX_CSC_CTRL,(int)uc,(int)HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL)));
        		uc &= ~B_VDIO_GATTING ;
        		HDMIRX_WriteI2C_Byte(REG_RX_CSC_CTRL, uc);
        		HDMIRX_DEBUG_PRINTF(("reg %02X <- %02X = %02X\n",(int)REG_RX_CSC_CTRL,(int)uc,(int)HDMIRX_ReadI2C_Byte(REG_RX_CSC_CTRL)));
        	}

        }
    }
}

void
SetAudioMute(BOOL bMute)
{
    if(bMute)
    {
        SetMUTE(~B_TRI_AUDIO, B_TRI_AUDIO);
    }
    else
    {
        // uc = ReadEEPROMByte(EEPROM_AUD_TRISTATE);
        // uc &= B_TRI_AUDIO ;
        SetMUTE(~B_TRI_AUDIO, 0);
    }
}

// 2008/08/15 added by jj_tseng@chipadvanced.com
// added Audio parameter
/////////////////////////////////////////////////////////////////////////////
// Name - getHDMIRXAudioStatus()
// Parameter - N/A
// return -
//         D[7:4] - audio type
//                  1100 - high bit rate
//                  1010 - one bit audio(DSD)
//                  1001 - NLPCM audio(compress)
//                  1000 - LPCM audio
//         D[3]   - layout
//         D[2:0] - enabled source.
/////////////////////////////////////////////////////////////////////////////
BYTE
getHDMIRXAudioStatus()
{
    BYTE uc,audio_status ;

    SwitchHDMIRXBank(0);

    uc = HDMIRX_ReadI2C_Byte(REG_RX_AUDIO_CH_STAT);
    audio_status = 0 ;

    if((uc &(B_AUDIO_ON|B_HBRAUDIO|B_DSDAUDIO))==(BYTE)(B_AUDIO_ON|B_HBRAUDIO))
    {
        audio_status = T_AUDIO_HBR ;
    }
    else if((uc &(B_AUDIO_ON|B_HBRAUDIO|B_DSDAUDIO))==(BYTE)(B_AUDIO_ON|B_DSDAUDIO))
    {
        audio_status = T_AUDIO_DSD ;
    }
    else if(uc & B_AUDIO_ON)
    {
        if(HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT0)&(1<<1))
        {
            // NLPCM/compressed audio
            audio_status = T_AUDIO_NLPCM ;
        }
        else
        {
            audio_status = T_AUDIO_LPCM ;
        }

        if(uc & B_AUDIO_LAYOUT)
        {
            audio_status |= F_AUDIO_LAYOUT_1 ;
        }

        if(uc &(1<<3))
        {
            audio_status |= 4 ;
        }
        else if(uc &(1<<2))
        {
            audio_status |= 3 ;
        }
        else if(uc &(1<<1))
        {
            audio_status |= 2 ;
        }
        else if(uc &(1<<0))
        {
            audio_status |= 1 ;
        }
    }

    return audio_status ;
}

///////////////////////////////////////////////////////////////////////
// Parameter out - ucIEC60958ChStat[5]
// return - TRUE if ucIEC60958ChStat is returned
///////////////////////////////////////////////////////////////////////
//                 ucIEC60958ChStat[0]
//                 - D[0] Comsumer used for channel status block
//                 - D[1] 0 - LPCM 1 - for IEC61937 spec.
//                 - D[2] 0 - Software for which copyright assert.
//                        1 - Software for which no copyright assert.
//                 - D[5:3] addition information.
//                 - D[7:6] channel status mode
///////////////////////////////////////////////////////////////////////
//                 ucIEC60958ChStat[1]
//                 - D[7:0] categery of audio.
///////////////////////////////////////////////////////////////////////
//                 ucIEC60958ChStat[2]
//                 - D[7:4] - Channel number(0/1/2)
//                 - D[3:0] - Source number(0..15)
///////////////////////////////////////////////////////////////////////
//                 ucIEC60958ChStat[3]
//                 - D[5:4] = Clock accurency    - ret9F[1:0]
//                 - D[3:0] = Sample Word Length - reg84[3:0]
///////////////////////////////////////////////////////////////////////
//                 ucIEC60958ChStat[4]
//                 - D[7:4] = Original sampling frequency
//                 - D[3:0] = Sample Word Length - reg9F[7:4]
///////////////////////////////////////////////////////////////////////
BOOL
getHDMIRXAudioChannelStatus(BYTE ucIEC60958ChStat[])
{
    BYTE fs,audio_status ;


    audio_status = getHDMIRXAudioStatus();

    if(((audio_status & T_AUDIO_MASK)== T_AUDIO_OFF)||
((audio_status & T_AUDIO_MASK)== T_AUDIO_DSD))
    {
        // return false if no audio or one-bit audio.
        return FALSE ;
    }

    SwitchHDMIRXBank(0);
    ucIEC60958ChStat[0] = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT0);
    ucIEC60958ChStat[1] = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT1);
    ucIEC60958ChStat[2] = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT2);
    fs = HDMIRX_ReadI2C_Byte(REG_RX_FS)& M_Fs ;

    if((audio_status & T_AUDIO_MASK)== T_AUDIO_HBR)
    {
        fs = B_Fs_HBR ;
        ucIEC60958ChStat[0] |= B_AUD_NLPCM ;
    }

    ucIEC60958ChStat[3] = HDMIRX_ReadI2C_Byte(REG_RX_AUD_CHSTAT3);
    //
    ucIEC60958ChStat[4] =(ucIEC60958ChStat[3] >> 4)& 0xF ;
    ucIEC60958ChStat[4] |=((~fs)& 0xF)<<4 ;

    ucIEC60958ChStat[3] &= 3 ;
    ucIEC60958ChStat[3] <<= 4 ;
    ucIEC60958ChStat[3] |= fs & 0xF ;

    return TRUE ;
}

//////////////////////////////////////////////////////////////////////////
// Name - setHDMIRX_HBROutput
// Parameter - HBR_SPDIF
//             0 - output HBR through I2S channel
//             1 - output HBR through SPDIF
// return N/A
//////////////////////////////////////////////////////////////////////////

void
setHDMIRX_HBROutput(BOOL HBR_SPDIF)
{
    BYTE uc ;
    SwitchHDMIRXBank(0);
    uc = HDMIRX_ReadI2C_Byte(REG_RX_HWAMP_CTRL);

    if(HBR_SPDIF)
    {
        HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL, uc | B_HBR_SPDIF);

        uc = HDMIRX_ReadI2C_Byte(REG_RX_FS_SET);
        uc &= ~0x30 ;
        uc |= 0x20 ; // reg7E[5:4] = '10'
        uc = HDMIRX_WriteI2C_Byte(REG_RX_FS_SET, uc);
        SetMUTE(~B_TRI_AUDIO, B_TRI_I2S3|B_TRI_I2S2|B_TRI_I2S1|B_TRI_I2S0);
        // enabled SPDIF output and disable all I2S channel
    }
    else
    {
        HDMIRX_WriteI2C_Byte(REG_RX_HWAMP_CTRL, uc | B_HBR_SPDIF);
        SetMUTE(~B_TRI_AUDIO, B_TRI_SPDIF);
        // disable SPDIF output and enable all I2S channel
    }
}

//////////////////////////////////////////////////////////////////////////
// Name - setHDMIRX_SPDIFOutput
// Parameter - N/A
// return N/A
// comment :
//          set output to SPDIF audio.
//////////////////////////////////////////////////////////////////////////
void
setHDMIRX_SPDIFOutput()
{
    BYTE uc ;
    SwitchHDMIRXBank(0);
    uc = HDMIRX_ReadI2C_Byte(REG_RX_FS_SET);
    uc &= ~0x30 ;
    uc |= 0x20 ; // reg7E[5:4] = '10'
    uc = HDMIRX_WriteI2C_Byte(REG_RX_FS_SET, uc);
    SetMUTE(~B_TRI_AUDIO, B_TRI_I2S3|B_TRI_I2S2|B_TRI_I2S1|B_TRI_I2S0);
}


//////////////////////////////////////////////////////////////////////////
// Name - setHDMIRX_SPDIFOutput
// Parameter - src_enable
//             D[0] - enable I2S0
//             D[1] - enable I2S1
//             D[2] - enable I2S2
//             D[3] - enable I2S3
// return N/A
// comment :
//          set output to SPDIF audio.
//////////////////////////////////////////////////////////////////////////
void
setHDMIRX_I2SOutput(BYTE src_enable)
{
    SwitchHDMIRXBank(0);

    src_enable &= 0xF ;
    src_enable ^= 0xF ; // invert lower four enable bit to tristate bit.
    SetMUTE(~B_TRI_AUDIO, B_TRI_SPDIF|src_enable);
}
//~jj_tseng@chipadvanced.com


#ifdef DEBUG
void
DumpHDMIRXReg()
{
    int i,j ;
    BYTE ucData ;

    HDMIRX_DEBUG_PRINTF1(("       "));
    for(j = 0 ; j < 16 ; j++)
    {
        HDMIRX_DEBUG_PRINTF1((" %02X",(int)j));
        if((j == 3)||(j==7)||(j==11))
        {
            HDMIRX_DEBUG_PRINTF1(("  "));
        }
    }
    HDMIRX_DEBUG_PRINTF1(("\n        -----------------------------------------------------\n"));

    Switch_HDMIRX_Bank(0);

    for(i = 0 ; i < 0x100 ; i+=16)
    {
        HDMIRX_DEBUG_PRINTF1(("[%3X]  ",(int)i));
        for(j = 0 ; j < 16 ; j++)
        {
            ucData = HDMIRX_ReadI2C_Byte((BYTE)((i+j)&0xFF));
            HDMIRX_DEBUG_PRINTF1((" %02X",(int)ucData));
            if((j == 3)||(j==7)||(j==11))
            {
                HDMIRX_DEBUG_PRINTF1((" -"));
            }
        }
        HDMIRX_DEBUG_PRINTF1(("\n"));
        if((i % 0x40)== 0x30)
        {
            HDMIRX_DEBUG_PRINTF1(("        -----------------------------------------------------\n"));
        }
    }

    Switch_HDMIRX_Bank(1);
    for(i = 0x180; i < 0x200 ; i+=16)
    {
        HDMIRX_DEBUG_PRINTF1(("[%3X]  ",(int)i));
        for(j = 0 ; j < 16 ; j++)
        {
            ucData = HDMIRX_ReadI2C_Byte((BYTE)((i+j)&0xFF));
            HDMIRX_DEBUG_PRINTF1((" %02X",(int)ucData));
            if((j == 3)||(j==7)||(j==11))
            {
                HDMIRX_DEBUG_PRINTF1((" -"));
            }
        }
        HDMIRX_DEBUG_PRINTF1(("\n"));
        if((i % 0x40)== 0x30)
        {
            HDMIRX_DEBUG_PRINTF1(("        -----------------------------------------------------\n"));
        }

    }

    Switch_HDMIRX_Bank(0);
}
#endif

