#ifndef _CAT6611_HDMI_HW_H
#define _CAT6611_HDMI_HW_H

#include "typedef.h"
#include "config.h"
#include "debug.h"
#include "hdmitx_drv.h"
#define CAT6611_SCL_RATE	100 * 1000
#define I2S 0
#define SPDIF 1

#ifndef I2S_FORMAT
#define I2S_FORMAT 0x01 // 32bit audio
#endif

#ifndef INPUT_SAMPLE_FREQ
    #define INPUT_SAMPLE_FREQ AUDFS_48KHz
#endif //INPUT_SAMPLE_FREQ

#ifndef INPUT_SAMPLE_FREQ_HZ
    #define INPUT_SAMPLE_FREQ_HZ 44100L
#endif //INPUT_SAMPLE_FREQ_HZ

#ifndef OUTPUT_CHANNEL
    #define OUTPUT_CHANNEL 2
#endif //OUTPUT_CHANNEL

#ifndef CNOFIG_INPUT_AUDIO_TYPE
    #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_LPCM
    // #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_NLPCM
    // #define CNOFIG_INPUT_AUDIO_TYPE T_AUDIO_HBR
#endif //CNOFIG_INPUT_AUDIO_TYPE

#ifndef CONFIG_INPUT_AUDIO_SPDIF
    #define CONFIG_INPUT_AUDIO_SPDIF I2S
    // #define CONFIG_INPUT_AUDIO_SPDIF  SPDIF
#endif //CONFIG_INPUT_AUDIO_SPDIF

#ifndef INPUT_SIGNAL_TYPE
#define INPUT_SIGNAL_TYPE 0 // 24 bit sync seperate
#endif

////////////////////////////////////////////////////////////////////////////////
// Internal Data Type
////////////////////////////////////////////////////////////////////////////////
enum {
		OUTPUT_DVI = 0,
		OUTPUT_HDMI
	};
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
    ULONG VideoPixelClock ;
    BYTE VIC ;
    BYTE pixelrep ;
	BYTE outputVideoMode ;
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
    BYTE b16bit:1 ;
    BYTE b20bit:1 ;
    BYTE b24bit:1 ;
    BYTE Rsrv:5 ;
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
        BYTE channel:3 ;
        BYTE AudioFormatCode:4 ;
        BYTE Rsrv1:1 ;

        BYTE b32KHz:1 ;
        BYTE b44_1KHz:1 ;
        BYTE b48KHz:1 ;
        BYTE b88_2KHz:1 ;
        BYTE b96KHz:1 ;
        BYTE b176_4KHz:1 ;
        BYTE b192KHz:1 ;
        BYTE Rsrv2:1 ;
        BYTE ucCode ;
    } s ;
    BYTE uc[3] ;
} AUDDESCRIPTOR ;

typedef union {
    struct {
        BYTE FL_FR:1 ;
        BYTE LFE:1 ;
        BYTE FC:1 ;
        BYTE RL_RR:1 ;
        BYTE RC:1 ;
        BYTE FLC_FRC:1 ;
        BYTE RLC_RRC:1 ;
        BYTE Reserve:1 ;
        BYTE Unuse[2] ;
    } s ;
    BYTE uc[3] ;
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
        BYTE DVI_Dual:1 ;
        BYTE Rsvd:2 ;
        BYTE DC_Y444:1 ;
        BYTE DC_30Bit:1 ;
        BYTE DC_36Bit:1 ;
        BYTE DC_48Bit:1 ;
        BYTE SUPPORT_AI:1 ;
    } info ;
    BYTE uc ;
} DCSUPPORT ;

typedef union _LATENCY_SUPPORT{
    struct {
        BYTE Rsvd:6 ;
        BYTE I_Latency_Present:1 ;
        BYTE Latency_Present:1 ;
    } info ;
    BYTE uc ;
} LATENCY_SUPPORT ;

#define HDMI_IEEEOUI 0x0c03
#define MAX_VODMODE_COUNT 32
#define MAX_AUDDES_COUNT 4

typedef struct _RX_CAP{
    BYTE VideoMode ;
    BYTE NativeVDOMode ;
    BYTE VDOMode[8] ;
    BYTE AUDDesCount ;
    AUDDESCRIPTOR AUDDes[MAX_AUDDES_COUNT] ;
    BYTE PA[2] ;
    ULONG IEEEOUI ;
    DCSUPPORT dc ;
    BYTE MaxTMDSClock ;
    LATENCY_SUPPORT lsupport ;
    SPK_ALLOC   SpeakerAllocBlk ;
    BYTE ValidCEA:1 ;
    BYTE ValidHDMI:1 ;
    BYTE Valid3D:1 ;
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

BYTE HDMITX_ReadI2C_Byte(BYTE RegAddr);
SYS_STATUS HDMITX_WriteI2C_Byte(BYTE RegAddr,BYTE d);
SYS_STATUS HDMITX_ReadI2C_ByteN(BYTE RegAddr,BYTE *pData,int N);
SYS_STATUS HDMITX_WriteI2C_ByteN(BYTE RegAddr,BYTE *pData,int N);
SYS_STATUS HDMITX_SetI2C_Byte(BYTE Reg,BYTE Mask,BYTE Value);

void InitHDMITX_Variable(void);
#if 0
//void HDMITX_ChangeDisplayOption(HDMI_Video_Type VideoMode, HDMI_OutputColorMode OutputColorMode);
//void HDMITX_SetOutput();
//int  HDMITX_DevLoopProc();
//void ConfigfHdmiVendorSpecificInfoFrame(BYTE _3D_Stru);
void HDMITX_ChangeAudioOption(BYTE Option, BYTE channelNum, BYTE AudioFs);
void HDMITX_SetAudioOutput();
void HDMITX_ChangeColorDepth(BYTE colorDepth);
#endif
#endif
