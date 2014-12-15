///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <IT6811.h>
//   @author Hermes.Wu@ite.com.tw
//   @date   2013/05/07
//   @fileversion: ITE_IT6811_6607_SAMPLE_1.06
//******************************************/
#ifndef _IT6681_DEFS_H_
#define _IT6681_DEFS_H_

//6607 demoboard
#define HDMI_DEV  3
#define MHL_DEV	  3
//6811 demoboard
//#define HDMI_DEV  0
//#define MHL_DEV	0


//////////////////////////////////////////////////
// MCU 8051data type
//////////////////////////////////////////////////


#define FALSE 		0
#define TRUE 		1
#define SUCCESS 	0
#define FAIL 		-1
#define FAIL_HPD_CHG 		-2
#define ON 			1
#define OFF        	0
#define HIGH       	1
#define LOW        	0

//typedef bit BOOL ;
#define _CODE code
#define _IDATA idata
#define _XDATA xdata

#define MODE_USB 0
#define MODE_MHL 1











#define VID8BIT         0
#define VID10BIT        1
#define VID12BIT        2
#define VID16BIT        3

#define RGB444          0
#define YCbCr422        1
#define YCbCr444        2

#define DynVESA         0
#define DynCEA          1

#define ITU601          0
#define ITU709          1

#define TDM2CH          0x0
#define TDM4CH          0x1
#define TDM6CH          0x2
#define TDM8CH          0x3

#define AUD32K          0x3
#define AUD48K          0x2
#define AUD96K          0xA
#define AUD192K         0xE
#define AUD44K          0x0
#define AUD88K          0x8
#define AUD176K         0xC
#define AUD768K         0x9

#define I2S             0
#define SPDIF           1

#define LPCM            0
#define NLPCM           1
#define HBR             2
#define DSD             3

#define NOCSC           0
#define RGB2YUV         2
#define YUV2RGB         3

#define _FrmPkt          0
#define _SbSFull         3
#define _TopBtm          6
#define _SbSHalf         8



#define AUD16BIT        0x2
#define AUD18BIT        0x4
#define AUD20BIT        0x3
#define AUD24BIT        0xB

#define AUDCAL1         0x4
#define AUDCAL2         0x0
#define AUDCAL3         0x8

#define PICAR_NO        0
#define PICAR4_3        1
#define PICAR16_9       2

#define ACTAR_PIC       8
#define ACTAR4_3        9
#define ACTAR16_9       10
#define ACTAR14_9       11

#define MHLInt00B       0x20
#define DCAP_CHG        0x01
#define DSCR_CHG        0x02
#define REQ_WRT         0x04
#define GRT_WRT         0x08

#define MHLInt01B       0x21
#define EDID_CHG        0x01

#define MHLSts00B       0x30
#define DCAP_RDY        0x01

#define MHLSts01B       0x31
#define NORM_MODE       0x03
#define PACK_MODE       0x02
#define PATH_EN         0x08
#define MUTED           0x10

#define MSG_MSGE		0x02
#define MSG_RCP 		0x10
#define MSG_RCPK		0x11
#define MSG_RCPE		0x12
#define MSG_RAP 		0x20
#define MSG_RAPK		0x21
#define MSG_UCP			0x30
#define MSG_UCPK		0x31
#define MSG_UCPE		0x32
#define MSG_MOUSE	    0x40
#define MSG_MOUSEK		0x41
#define MSG_MOUSEE		0x42















//////////////////////////////////////////////////
// data structur definition
//////////////////////////////////////////////////


typedef enum _SYS_STATUS {
    ER_SUCCESS = 0,
    ER_FAIL,
    ER_RESERVED
} SYS_STATUS ;

//#define abs(x) (((x)>=0)?(x):(-(x)))



#define PROG 1
#define INTERLACE 0
#define INTR 0
#define Vneg 0
#define Hneg 0
#define Vpos 1
#define Hpos 1


#define B_CAP_AUDIO_ON  (1<<7)
#define B_CAP_HBR_AUDIO (1<<6)
#define B_CAP_DSD_AUDIO (1<<5)
#define B_LAYOUT        (1<<4)
#define B_MULTICH       (1<<4)
#define B_HBR_BY_SPDIF  (1<<3)
#define B_SPDIF         (1<<2)
#define B_CAP_LPCM      (1<<0)


///////////////////////////////////////////////////////////////////////
// Video Data Type
///////////////////////////////////////////////////////////////////////
#define F_MODE_RGB24  0
#define F_MODE_RGB444  0
#define F_MODE_YUV422 1
#define F_MODE_YUV444 2
#define F_MODE_CLRMOD_MASK 3


#define F_MODE_INTERLACE  1

#define F_MODE_ITU709  (1<<4)
#define F_MODE_ITU601  0

#define F_MODE_0_255   0
#define F_MODE_16_235  (1<<5)

#define F_MODE_EN_UDFILT (1<<6) // output mode only, and loaded from EEPROM
#define F_MODE_EN_DITHER (1<<7) // output mode only, and loaded from EEPROM

#define T_MODE_CCIR656 (1<<0)
#define T_MODE_SYNCEMB (1<<1)
#define T_MODE_INDDR   (1<<2)
#define T_MODE_PCLKDIV2 (1<<3)

//////////////////////////////////////////////////////////////////
// Audio relate definition and macro.
//////////////////////////////////////////////////////////////////

// for sample clock
#define AUDFS_22p05KHz  4
#define AUDFS_44p1KHz 0
#define AUDFS_88p2KHz 8
#define AUDFS_176p4KHz    12

#define AUDFS_24KHz  6
#define AUDFS_48KHz  2
#define AUDFS_96KHz  10
#define AUDFS_192KHz 14

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


/////////////////////////////////////////////////////////////////////
// Packet and Info Frame definition and datastructure.
/////////////////////////////////////////////////////////////////////

#define VENDORSPEC_INFOFRAME_TYPE 0x81
#define AVI_INFOFRAME_TYPE 0x82
#define SPD_INFOFRAME_TYPE 0x83
#define AUDIO_INFOFRAME_TYPE 0x84
#define MPEG_INFOFRAME_TYPE 0x85

#define VENDORSPEC_INFOFRAME_VER 0x01
#define AVI_INFOFRAME_VER 0x02
#define SPD_INFOFRAME_VER 0x01
#define AUDIO_INFOFRAME_VER 0x01
#define MPEG_INFOFRAME_VER 0x01

#define VENDORSPEC_INFOFRAME_LEN 8
#define AVI_INFOFRAME_LEN 13
#define SPD_INFOFRAME_LEN 25
#define AUDIO_INFOFRAME_LEN 10
#define MPEG_INFOFRAME_LEN 10

#define ACP_PKT_LEN 9
#define ISRC1_PKT_LEN 16
#define ISRC2_PKT_LEN 16

typedef union _AVI_InfoFrame
{
    struct {
        BYTE Type ;
        BYTE Ver ;
        BYTE Len ;

        BYTE Scan:2 ;
        BYTE BarInfo:2 ;
        BYTE ActiveFmtInfoPresent:1 ;
        BYTE ColorMode:2 ;
        BYTE FU1:1 ;

        BYTE ActiveFormatAspectRatio:4 ;
        BYTE PictureAspectRatio:2 ;
        BYTE Colorimetry:2 ;

        BYTE Scaling:2 ;
        BYTE FU2:6 ;

        BYTE VIC:7 ;
        BYTE FU3:1 ;

        BYTE PixelRepetition:4 ;
        BYTE FU4:4 ;

        SHORT Ln_End_Top ;
        SHORT Ln_Start_Bottom ;
        SHORT Pix_End_Left ;
        SHORT Pix_Start_Right ;
    } info ;
    struct {
        BYTE AVI_HB[3] ;
        BYTE AVI_DB[AVI_INFOFRAME_LEN] ;
    } pktbyte ;
} AVI_InfoFrame ;

typedef union _Audio_InfoFrame {

    struct {
        BYTE Type ;
        BYTE Ver ;
        BYTE Len ;

        BYTE AudioChannelCount:3 ;
        BYTE RSVD1:1 ;
        BYTE AudioCodingType:4 ;

        BYTE SampleSize:2 ;
        BYTE SampleFreq:3 ;
        BYTE Rsvd2:3 ;

        BYTE FmtCoding ;

        BYTE SpeakerPlacement ;

        BYTE Rsvd3:3 ;
        BYTE LevelShiftValue:4 ;
        BYTE DM_INH:1 ;
    } info ;

    struct {
        BYTE AUD_HB[3] ;
        BYTE AUD_DB[AUDIO_INFOFRAME_LEN] ;
    } pktbyte ;

} Audio_InfoFrame ;

typedef union _MPEG_InfoFrame {
    struct {
        BYTE Type ;
        BYTE Ver ;
        BYTE Len ;

        ULONG MpegBitRate ;

        BYTE MpegFrame:2 ;
        BYTE Rvsd1:2 ;
        BYTE FieldRepeat:1 ;
        BYTE Rvsd2:3 ;
    } info ;
    struct {
        BYTE MPG_HB[3] ;
        BYTE MPG_DB[MPEG_INFOFRAME_LEN] ;
    } pktbyte ;
} MPEG_InfoFrame ;

// Source Product Description
typedef union _SPD_InfoFrame {
    struct {
        BYTE Type ;
        BYTE Ver ;
        BYTE Len ;

        char VN[8] ; // vendor name character in 7bit ascii characters
        char PD[16] ; // product description character in 7bit ascii characters
        BYTE SourceDeviceInfomation ;
    } info ;
    struct {
        BYTE SPD_HB[3] ;
        BYTE SPD_DB[SPD_INFOFRAME_LEN] ;
    } pktbyte ;
} SPD_InfoFrame ;

///////////////////////////////////////////////////////////////////////////
// Using for interface.
///////////////////////////////////////////////////////////////////////////
struct VideoTiming {
    ULONG VideoPixelClock ;
    BYTE VIC ;
    BYTE pixelrep ;
	BYTE outputVideoMode ;
} ;

#define F_VIDMODE_ITU709  (1<<4)
#define F_VIDMODE_ITU601  0

#define F_VIDMODE_0_255   0
#define F_VIDMODE_16_235  (1<<5)

#define F_VIDMODE_EN_UDFILT (1<<6) // output mode only, and loaded from EEPROM
#define F_VIDMODE_EN_DITHER (1<<7) // output mode only, and loaded from EEPROM


#define T_MODE_CCIR656 (1<<0)
#define T_MODE_SYNCEMB (1<<1)
#define T_MODE_INDDR (1<<2)
#define T_MODE_DEGEN (1<<3)
#define T_MODE_SYNCGEN (1<<4)

//////////////////////////////////////////////////////////////////
// Audio relate definition and macro.
//////////////////////////////////////////////////////////////////

// for sample clock
#define FS_22K05  4
#define FS_44K1 0
#define FS_88K2 8
#define FS_176K4    12

#define FS_24K  6
#define FS_48K  2
#define FS_96K  10
#define FS_192K 14

#define FS_32K  3
#define FS_OTHER    1

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



typedef enum tagHDMI_Video_Type {
    HDMI_Unkown = 0 ,
    HDMI_640x480p60 = 1 ,
    HDMI_480p60,
    HDMI_480p60_16x9,
    HDMI_720p60,
    HDMI_1080i60,
    HDMI_480i60,
    HDMI_480i60_16x9,
    HDMI_240p60,
    HDMI_1440x480p60,
    HDMI_1080p60 = 16,
    HDMI_576p50,
    HDMI_576p50_16x9,
    HDMI_720p50,
    HDMI_1080i50,
    HDMI_576i50,
    HDMI_576i50_16x9,
    HDMI_288p50,
    HDMI_1440x576p50,
    HDMI_1080p50 = 31,
    HDMI_1080p24,
    HDMI_1080p25,
    HDMI_1080p30,
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

typedef enum tagMODE_ID{
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
	UNKNOWN_MODE
} MODE_ID;


struct IT6681_REG_INI {
    unsigned char ucAddr;
    unsigned char andmask;
    unsigned char ucValue;
}  ;


typedef enum  {
    MHL_USB_PWRDN = 0,
	MHL_USB,
	MHL_Cbusdet,
    MHL_1KDetect,
    MHL_CBUSDiscover,
	MHL_CBUSDisDone,
    MHL_Unknown
}MHLState_Type;

typedef enum  {
    HDMI_Video_REST= 0,
	HDMI_Video_WAIT,
	HDMI_Video_ON,
    HDMI_Video_Unknown
}HDMI_Video_state;


typedef enum  {
    HDCP_Off = 0,
	HDCP_CPStart,
	HDCP_CPGoing,
	HDCP_CPDone,
	HDCP_CPFail,
    HDCP_CPUnknown
} HDCPSts_Type ;

					   
typedef enum _Video_State_Type {
    VSTATE_Off=0,
    VSTATE_PwrOff,
    VSTATE_SyncWait,
    VSTATE_SWReset,
    VSTATE_SyncChecking,
    VSTATE_HDCPSet,
    VSTATE_HDCP_Reset,
    VSTATE_ModeDetecting,
    VSTATE_VideoOn,
    VSTATE_ColorDetectReset,
    VSTATE_HDMI_OFF,
    VSTATE_Reserved
} Video_State_Type;

typedef enum _Audio_State_Type {
    ASTATE_AudioOff=0,
    ASTATE_RequestAudio,
    ASTATE_ResetAudio,
    ASTATE_WaitForReady,
    ASTATE_AudioOn,
    ASTATE_Reserved
} Audio_State_Type;


typedef enum _RxHDCP_State_Type {
    RxHDCP_PwrOff=0,
    RxHDCP_ModeCheck,
    RxHDCP_Receiver,
    RxHDCP_Repeater,
    RxHDCP_SetKSVFifoList,
    RxHDCP_GenVR,
    RxHDCP_WriteVR,
    RxHDCP_Auth_WaitRi,
    RxHDCP_Authenticated,
    RxHDCP_Reserved,
} RxHDCP_State_Type;


typedef struct tag_IT6681_DEVICE {

    Video_State_Type m_VState;
    Audio_State_Type m_AState;
    RxHDCP_State_Type m_RxHDCPState;
    //AUDIO_CAPS m_RxAudioCaps;

    USHORT m_SWResetTimeOut;
    USHORT m_MuteResumingTimer;
    USHORT m_VideoCountingTimer;
    USHORT m_AudioCountingTimer;

    USHORT m_EventFlags ;

    BYTE m_ucCurrentHDMIPort;


    BYTE m_ucVideoOnCount;
    BYTE m_ucSCDTOffCount;
    BYTE m_ucEccCount;
    BYTE m_ucDVISCDToffCNT;

    BYTE m_bOutputVideoMode;
    BYTE m_bInputVideoMode;
    BYTE m_ucAudioSampleClock;
    BYTE m_ucHDMIAudioErrorCount;


    BYTE m_bOldReg8B;
    BYTE m_ucNewSCDT;
    BYTE m_ucOldSCDT;

    BYTE m_ucSCDTonCount;
    BYTE m_ucVideoModeChange;

    #ifdef _IT6607_GeNPacket_Usage_
    BYTE m_PollingPacket;
    BYTE m_PacketState;

    BYTE m_ACPState;

    BYTE m_GeneralRecPackType;
    #endif


    BYTE m_bRxAVmute:1;
    BYTE m_bVideoOnCountFlag:1;
    BYTE m_MuteAutoOff:1;
    BYTE m_bUpHDMIMode:1;
    BYTE m_bUpHDCPMode:1;
    BYTE m_NewAVIInfoFrameF:1;
    BYTE m_NewAUDInfoFrameF:1;
    BYTE m_HDCPRepeater:1;
    BYTE m_MuteByPKG:1;

    #ifdef _IT6607_GeNPacket_Usage_
    BYTE m_GamutPacketRequest:1;

    #endif

} IT6681DEV, *PIT6681DEV ;


/////////////////////////////////////////////////////


/////////////////////////////////////////
//
//NOTE: all the info struct in infoframe
//      is aligned from LSB.
//
////////////////////////////////////////
#define AVI_INFOFRAME_TYPE 0x82
#define AVI_INFOFRAME_VER 0x02
#define AVI_INFOFRAME_LEN 13
//  |Type
//  |Ver
//	|Len
//	|Scan:2 | BarInfo:2 | ActiveFmtInfoPresent:1 | ColorMode:2 | FU1:1|
//  |AspectRatio:4 | ictureAspectRatio:2 | Colorimetry:2
//	|Scaling:2 | FU2:6
//  |VIC:7 | FU3:1
//	|PixelRepetition:4 | FU4:4
//	|Ln_End_Top
//	|Ln_Start_Bottom
//	|Pix_End_Left
//	|Pix_Start_Right

struct AVI_InfoFrame{

	unsigned char AVI_HB[3] ;
    unsigned char AVI_DB[AVI_INFOFRAME_LEN] ;

};

struct it6681_dev_data {

    unsigned char ver;
    unsigned long RCLK;

    //MHL Sink CAPS
    //unsigned char RxMHLVer;
    //power control
    //unsigned char GRCLKPD;
    //unsigned int  RxSenCnt;	//options


    HDMI_Video_state Hdmi_video_state;
    // video format
    unsigned int VidFmt;
    //unsigned int Vidchg;
    //unsigned char ColorDepth;
    unsigned char InColorMode;
    unsigned char OutColorMode;
    unsigned char DynRange ;
    unsigned char YCbCrCoef ;
    unsigned char PixRpt;

    unsigned char NonDirectTransMode;

	struct AVI_InfoFrame *Aviinfo;
    // 3D Option
    //unsigned char En3D;
    //unsigned char Sel3DFmt;      //FrmPkt, TopBtm, SbSHalf, SbSFull
    unsigned char EnPackPix;

    // Audio Option
    //unsigned char AudEn ;
    //unsigned char AudSel ; // I2S or SPDIF
    unsigned char AudFmt ; //audio sampling freq
    unsigned char AudCh ;
    unsigned char AudType ; // LPCM, NLPCM, HBR, DSD


#if(_SUPPORT_HDCP_)
   //HDCP
   unsigned char HDCPEnable;
   HDCPSts_Type Hdcp_state;
   unsigned int HDCPFireCnt ;
   //unsigned int HDCPRiChkCnt ;
   //unsigned int ksvchkcnt ;
   //unsigned int syncdetfailcnt;
#endif


    //CBUS MSC
	MHLState_Type Mhl_state;
    unsigned char CBusPathEn ;
    unsigned char CBusDetCnt ;
    unsigned char Det1KFailCnt ;
    unsigned char DisvFailCnt ;  //Discover fail count determine when to switch to USB mode

	unsigned char Mhl_devcap[16];

	unsigned char txmsgdata[2];
	unsigned char rxmsgdata[2];
	unsigned char txscrpad[16];
	unsigned char rxscrpad[16];

};

typedef void(*RegSetFunc)(unsigned char, unsigned char, unsigned char);

typedef struct
{
    unsigned short i2cTick;
    unsigned long OclkTick1;
    unsigned long OclkTick2;
    unsigned long OclkSum;
    unsigned long OclkTickSum;
    unsigned char EdidStored;
    unsigned char EdidChkSum;
    unsigned char GRCLKPD;
    unsigned char TXAFEPD;
    unsigned char TXHPD;
    unsigned char TXCanReadDevCap;
    unsigned char EdidOffset;
    unsigned char EdidAddr;

    unsigned char IT6682_MCU2VBUSOUT; 
    unsigned char IT6682_MCU2Switch;
    unsigned char IsIT6682;
    unsigned char RxPOW;
    unsigned char ForceVbusOutput;

    unsigned char KeepRxHPD;
    unsigned char ForceRxHPD;

    unsigned short LEDTick;
    unsigned char RxClock80M;
    unsigned char enable_internal_edid;

    Video_State_Type m_VState;

}DRIVER_DATA;

int it6681_fwinit(void);
void it6681_irq(void);
void it6681_poll(void);

int HDMITX_SetAVIInfoFrame(void *p);
//void HDMITX_SET_SignalType(unsigned char DynRange,unsigned char colorcoef,unsigned char pixrep);
//void HDMITX_Set_ColorType(unsigned char inputColorMode,unsigned char outputColorMode);
void HDMITX_SetVideoOutput(int mode);
void HDMITX_change_audio(unsigned char AudType,unsigned char AudFs,unsigned char AudCh);
void it6681_set_packed_pixel_mode(unsigned char mode);
void it6681_set_hdcp(unsigned char mode);

//BOOL CheckHDMIRX(void);

void DumpHDMITXReg(void);

#endif
