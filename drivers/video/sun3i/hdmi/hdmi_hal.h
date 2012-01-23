#ifndef __HDMI_HAL_H__
#define __HDMI_HAL_H__

#include "drv_hdmi_i.h"

#define HDMI_V640x480p_60Hz 		1
#define HDMI_V720x480p_60Hz_4x3 	2
#define HDMI_V720x480p_60Hz_16x9 	3
#define HDMI_V1280x720p_60Hz 		4
#define HDMI_V1280x720p_50Hz 		19
#define HDMI_V1920x1080i_60Hz 		5
#define HDMI_V1920x1080p_60Hz 		16
#define HDMI_V1920x1080p_50Hz 		31
#define HDMI_V1920x1080p_24Hz 		32
#define HDMI_V1920x1080p_25Hz 		33
#define HDMI_V1920x1080p_30Hz 		34
#define HDMI_V1920x1080i_50Hz 		20
#define HDMI_V720x480i_60Hz_4x3 	6
#define HDMI_V720x480i_60Hz_16x9 	7
#define HDMI_V720x576i_50Hz_4x3 	21
#define HDMI_V720x576i_50Hz_16x9 	22
#define HDMI_V720x576p_50Hz_4x3 	17
#define HDMI_V720x576p_50Hz_16x9 	18


typedef enum
{
    HDMI_STATE_INITIAL = 0x01,
    HDMI_STATE_WAIT_HOTPLUG = 0x02,
    HDMI_STATE_WAIT_RX_SENSE = 0x03,
    HDMI_STATE_READ_PARSE_EDID = 0x04,
    HDMI_STATE_CONFIG_VIDEO = 0x05,
    HDMI_STATE_CONFIG_AUDIO = 0x06,
    HDMI_STATE_CONFIG_PACKETS = 0x07,
    HDMI_STATE_HDCP_AUTHENTICATION = 0x08,
    HDMI_STATE_PLAY_BACK = 0x09,
    HDMI_STATE_RESET_LINK = 0x0a,
    HDMI_STATE_UNKNOWN = 0x0b,
}__hdmi_state_t;

extern void delay_ms(__u32 t);
extern __s32 Hdmi_hal_set_display_mode(__u8 hdmi_mode);
extern __s32 Hdmi_hal_set_audio_para(hdmi_audio_t * audio_para);
extern __s32 Hdmi_hal_audio_enable(__u8 mode, __u8 channel);
extern __s32 Hdmi_hal_mode_support(__u8 mode);
extern __s32 Hdmi_hal_get_HPD_status(void);
extern __s32 Hdmi_hal_get_connection_status(void);
extern __s32 Hdmi_hal_standby_exit(void);
extern __s32 Hdmi_hal_main_task(void);
extern __s32 Hdmi_hal_init(void);
extern __s32 Hdmi_hal_exit(void);

#define DelayMS delay_ms


#endif

