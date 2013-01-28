///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   >cat66121_sys.h<
//   @author Jau-Chih.Tseng@ite.com.tw
//   @date   2009/08/24
//   @fileversion: cat66121_SAMPLEINTERFACE_1.12
//******************************************/

#ifndef _CAT66121_SYS_H_
#define _CAT66121_SYS_H_
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

/* Follow prototypes need accomplish by ourself */
int cat66121_detect_device(void);
int cat66121_sys_init(struct hdmi *hdmi);
int cat66121_sys_unplug(struct hdmi *hdmi);
int cat66121_sys_detect_hpd(struct hdmi *hdmi, int *hpdstatus);
int cat66121_sys_detect_sink(struct hdmi *hdmi, int *sink_status);
int cat66121_sys_read_edid(struct hdmi *hdmi, int block, unsigned char *buff);
int cat66121_sys_config_video(struct hdmi *hdmi, int vic, int input_color, int output_color);
int cat66121_sys_config_audio(struct hdmi *hdmi, struct hdmi_audio *audio);
int cat66121_sys_config_hdcp(struct hdmi *hdmi, int enable);
int cat66121_sys_enalbe_output(struct hdmi *hdmi, int enable);
int cat66121_sys_check_status(struct hdmi *hdmi);
#endif // _cat66121_SYS_H_
