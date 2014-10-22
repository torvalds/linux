#ifndef __RK_HDMI_H__
#define __RK_HDMI_H__

#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/display-sys.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/atomic.h>
#include<linux/rk_screen.h>
#include <linux/rk_fb.h>

/* default HDMI output video mode */
#define HDMI_VIDEO_DEFAULT_MODE			HDMI_1280x720p_60Hz

#define HDMI_720X480P_60HZ_VIC		2
#define HDMI_720X480I_60HZ_VIC		6
#define HDMI_720X576P_50HZ_VIC		17
#define HDMI_720X576I_50HZ_VIC		21
#define HDMI_1280X720P_50HZ_VIC		19
#define HDMI_1280X720P_60HZ_VIC		4
#define HDMI_1920X1080P_50HZ_VIC	31
#define HDMI_1920X1080I_50HZ_VIC	20
#define HDMI_1920X1080P_60HZ_VIC	16
#define HDMI_1920X1080I_60HZ_VIC	5
#define HDMI_3840X2160P_24HZ_VIC	93
#define HDMI_3840X2160P_25HZ_VIC	94
#define HDMI_3840X2160P_30HZ_VIC	95
#define HDMI_3840X2160P_50HZ_VIC	96
#define HDMI_3840X2160P_60HZ_VIC	97
#define HDMI_4096X2160P_24HZ_VIC	98
#define HDMI_4096X2160P_25HZ_VIC	99
#define HDMI_4096X2160P_30HZ_VIC	100
#define HDMI_4096X2160P_50HZ_VIC	101
#define HDMI_4096X2160P_60HZ_VIC	102

/* HDMI video source */
enum {
	HDMI_SOURCE_LCDC0 = 0,
	HDMI_SOURCE_LCDC1 = 1
};

enum {
	HDMI_SOC_RK3036,
	HDMI_SOC_RK312X,
	HDMI_SOC_RK3288
};
/*
 * If HDMI_ENABLE, system will auto configure output mode according to EDID
 * If HDMI_DISABLE, system will output mode according to
 * macro HDMI_VIDEO_DEFAULT_MODE
 */
#define HDMI_AUTO_CONFIGURE			HDMI_DISABLE

/* default HDMI output audio mode */
#define HDMI_AUDIO_DEFAULT_CHANNEL		2
#define HDMI_AUDIO_DEFAULT_RATE			HDMI_AUDIO_FS_44100
#define HDMI_AUDIO_DEFAULT_WORD_LENGTH	HDMI_AUDIO_WORD_LENGTH_16bit

enum {
	VIDEO_INPUT_RGB_YCBCR_444 = 0,
	VIDEO_INPUT_YCBCR422,
	VIDEO_INPUT_YCBCR422_EMBEDDED_SYNC,
	VIDEO_INPUT_2X_CLOCK,
	VIDEO_INPUT_2X_CLOCK_EMBEDDED_SYNC,
	VIDEO_INPUT_RGB444_DDR,
	VIDEO_INPUT_YCBCR422_DDR
};

enum {
	VIDEO_OUTPUT_RGB444 = 0,
	VIDEO_OUTPUT_YCBCR444,
	VIDEO_OUTPUT_YCBCR422,
	VIDEO_OUTPUT_YCBCR420
};

enum {
	VIDEO_INPUT_COLOR_RGB = 0,
	VIDEO_INPUT_COLOR_YCBCR444,
	VIDEO_INPUT_COLOR_YCBCR422,
	VIDEO_INPUT_COLOR_YCBCR420
};

/* HDMI video mode code according CEA-861-E */
enum hdmi_video_mode {
	HDMI_640x480p_60Hz = 1,
	HDMI_720x480p_60Hz_4_3,
	HDMI_720x480p_60Hz_16_9,
	HDMI_1280x720p_60Hz,
	HDMI_1920x1080i_60Hz,		/* 5 */
	HDMI_720x480i_60Hz_4_3,
	HDMI_720x480i_60Hz_16_9,
	HDMI_720x240p_60Hz_4_3,
	HDMI_720x240p_60Hz_16_9,
	HDMI_2880x480i_60Hz_4_3,	/* 10 */
	HDMI_2880x480i_60Hz_16_9,
	HDMI_2880x240p_60Hz_4_3,
	HDMI_2880x240p_60Hz_16_9,
	HDMI_1440x480p_60Hz_4_3,
	HDMI_1440x480p_60Hz_16_9,	/* 15 */
	HDMI_1920x1080p_60Hz,
	HDMI_720x576p_50Hz_4_3,
	HDMI_720x576p_50Hz_16_9,
	HDMI_1280x720p_50Hz,
	HDMI_1920x1080i_50Hz,		/* 20 */
	HDMI_720x576i_50Hz_4_3,
	HDMI_720x576i_50Hz_16_9,
	HDMI_720x288p_50Hz_4_3,
	HDMI_720x288p_50Hz_16_9,
	HDMI_2880x576i_50Hz_4_3,	/* 25 */
	HDMI_2880x576i_50Hz_16_9,
	HDMI_2880x288p_50Hz_4_3,
	HDMI_2880x288p_50Hz_16_9,
	HDMI_1440x576p_50Hz_4_3,
	HDMI_1440x576p_50Hz_16_9,	/* 30 */
	HDMI_1920x1080p_50Hz,
	HDMI_1920x1080p_24Hz,
	HDMI_1920x1080p_25Hz,
	HDMI_1920x1080p_30Hz,
	HDMI_2880x480p_60Hz_4_3,	/* 35 */
	HDMI_2880x480p_60Hz_16_9,
	HDMI_2880x576p_50Hz_4_3,
	HDMI_2880x576p_50Hz_16_9,
	HDMI_1920x1080i_50Hz_2,	/* V Line 1250 total */
	HDMI_1920x1080i_100Hz,		/* 40 */
	HDMI_1280x720p_100Hz,
	HDMI_720x576p_100Hz_4_3,
	HDMI_720x576p_100Hz_16_9,
	HDMI_720x576i_100Hz_4_3,
	HDMI_720x576i_100Hz_16_9,	/* 45 */
	HDMI_1920x1080i_120Hz,
	HDMI_1280x720p_120Hz,
	HDMI_720x480p_120Hz_4_3,
	HDMI_720x480p_120Hz_16_9,
	HDMI_720x480i_120Hz_4_3,	/* 50 */
	HDMI_720x480i_120Hz_16_9,
	HDMI_720x576p_200Hz_4_3,
	HDMI_720x576p_200Hz_16_9,
	HDMI_720x576i_200Hz_4_3,
	HDMI_720x576i_200Hz_16_9,	/* 55 */
	HDMI_720x480p_240Hz_4_3,
	HDMI_720x480p_240Hz_16_9,
	HDMI_720x480i_240Hz_4_3,
	HDMI_720x480i_240Hz_16_9,
	HDMI_1280x720p_24Hz,		/* 60 */
	HDMI_1280x720p_25Hz,
	HDMI_1280x720p_30Hz,
	HDMI_1920x1080p_120Hz,
	HDMI_1920x1080p_100Hz,
};

/* HDMI Video Data Color Mode */
enum {
	HDMI_COLOR_RGB = 0,
	HDMI_COLOR_YCbCr422,
	HDMI_COLOR_YCbCr444
};

/* HDMI Video Color Depth */
enum {
	HDMI_COLOR_DEPTH_8BIT = 0x1,
	HDMI_COLOR_DEPTH_10BIT = 0x2,
	HDMI_COLOR_DEPTH_12BIT = 0x4,
	HDMI_COLOR_DEPTH_16BIT = 0x8
};

/* HDMI Audio type */
enum hdmi_audio_type {
	HDMI_AUDIO_LPCM = 1,
	HDMI_AUDIO_AC3,
	HDMI_AUDIO_MPEG1,
	HDMI_AUDIO_MP3,
	HDMI_AUDIO_MPEG2,
	HDMI_AUDIO_AAC_LC,	/* AAC */
	HDMI_AUDIO_DTS,
	HDMI_AUDIO_ATARC,
	HDMI_AUDIO_DSD,		/* One bit Audio */
	HDMI_AUDIO_E_AC3,
	HDMI_AUDIO_DTS_HD,
	HDMI_AUDIO_MLP,
	HDMI_AUDIO_DST,
	HDMI_AUDIO_WMA_PRO
};

/* I2S Fs */
enum hdmi_audio_fs {
	HDMI_AUDIO_FS_32000 = 0x1,
	HDMI_AUDIO_FS_44100 = 0x2,
	HDMI_AUDIO_FS_48000 = 0x4,
	HDMI_AUDIO_FS_88200 = 0x8,
	HDMI_AUDIO_FS_96000 = 0x10,
	HDMI_AUDIO_FS_176400 = 0x20,
	HDMI_AUDIO_FS_192000 = 0x40
};

/* Audio Word Length */
enum hdmi_audio_word_length {
	HDMI_AUDIO_WORD_LENGTH_16bit = 0x1,
	HDMI_AUDIO_WORD_LENGTH_20bit = 0x2,
	HDMI_AUDIO_WORD_LENGTH_24bit = 0x4
};

/* EDID block size */
#define HDMI_EDID_BLOCK_SIZE	128

/* HDMI state machine */
enum hdmi_state {
	HDMI_SLEEP = 0,
	HDMI_INITIAL,
	WAIT_HOTPLUG,
	READ_PARSE_EDID,
	WAIT_HDMI_ENABLE,
	SYSTEM_CONFIG,
	CONFIG_VIDEO,
	CONFIG_AUDIO,
	PLAY_BACK,
};

/* HDMI configuration command */
enum hdmi_change {
	HDMI_CONFIG_NONE = 0,
	HDMI_CONFIG_VIDEO,
	HDMI_CONFIG_AUDIO,
	HDMI_CONFIG_COLOR,
	HDMI_CONFIG_HDCP,
	HDMI_CONFIG_ENABLE,
	HDMI_CONFIG_DISABLE,
	HDMI_CONFIG_DISPLAY
};

/* HDMI Hotplug status */
enum {
	HDMI_HPD_REMOVED = 0,
	HDMI_HPD_INSERT,
	HDMI_HPD_ACTIVED
};

/* HDMI STATUS */
#define HDMI_DISABLE		0
#define HDMI_ENABLE		1
#define HDMI_UNKOWN		0xFF

/* HDMI Error Code */
enum hdmi_errorcode {
	HDMI_ERROR_SUCESS = 0,
	HDMI_ERROR_FALSE,
	HDMI_ERROR_I2C,
	HDMI_ERROR_EDID,
};

/* HDMI audio parameters */
struct hdmi_audio {
	u32 type;		/* Audio type */
	u32 channel;		/* Audio channel number */
	u32 rate;		/* Audio sampling rate */
	u32 word_length;	/* Audio data word length */
};

struct hdmi_edid {
	unsigned char sink_hdmi;	/* HDMI display device flag */
	unsigned char ycbcr444;		/* Display device support YCbCr444 */
	unsigned char ycbcr422;		/* Display device support YCbCr422 */
	unsigned char deepcolor;	/* bit3:DC_48bit; bit2:DC_36bit;
					 * bit1:DC_30bit; bit0:DC_Y444;
					 */
	unsigned char latency_fields_present;
	unsigned char i_latency_fields_present;
	unsigned char video_latency;
	unsigned char audio_latency;
	unsigned char interlaced_video_latency;
	unsigned char interlaced_audio_latency;
	unsigned char video_present;	/* have additional video format
					 * abount 4k and/or 3d
					 */
	unsigned char support_3d;	/* 3D format support */
	unsigned int maxtmdsclock;	/* max tmds clock freq support */
	struct fb_monspecs *specs;	/* Device spec */
	struct list_head modelist;	/* Device supported display mode list */
	struct hdmi_audio *audio;	/* Device supported audio info */
	int audio_num;			/* Device supported audio type number */
	int base_audio_support;		/* Device supported base audio */
	unsigned int  cecaddress;	/* CEC physical address */
};

/* RK HDMI Video Configure Parameters */
struct hdmi_video_para {
	int vic;
	int input_mode;			/* input video data interface */
	int input_color;		/* input video color mode */
	int output_mode;		/* output hdmi or dvi */
	int output_color;		/* output video color mode */
	unsigned char format_3d;	/* output 3d format */
	unsigned char color_depth;	/* color depth: 8bit; 10bit;
					 * 12bit; 16bit;
					 */
	unsigned char pixel_repet;	/* pixel repettion */
	unsigned char pixel_pack_phase;	/* pixel packing default phase */
	unsigned char color_limit_range;	/* quantization range
						 * 0: full range(0~255)
						 * 1:limit range(16~235)
						 */
};

struct rk_hdmi_drvdata  {
	u8 soc_type;
	u32 reversed;
};
struct hdmi;

struct rk_hdmi_drv_ops {
	int (*hdmi_debug) (struct hdmi *hdmi, int cmd);
};


struct hdmi {
	struct device *dev;
	int id;
	int irq;
	struct rk_lcdc_driver *lcdc;
	struct rk_hdmi_drvdata *data;
	struct rk_display_device *ddev;
#ifdef CONFIG_SWITCH
	struct switch_dev switch_hdmi;
#endif

	struct mutex lock;
	struct workqueue_struct *workqueue;
	struct delayed_work delay_work;

	spinlock_t irq_lock;
	struct mutex enable_mutex;

	int wait;
	struct completion complete;

	int suspend;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

	struct hdmi_edid edid;
	int enable;		/* Enable HDMI output or not */
	int vic;		/* HDMI output video mode code */
	struct hdmi_audio audio;	/* HDMI output audio type */

	int pwr_mode;		/* power mode */
	int hotplug;		/* hot plug status */
	int state;		/* hdmi state machine status */
	int autoconfig;		/* if true, auto config hdmi output mode
				 * according to EDID
				 */
	int command;		/* HDMI configuration command */
	int display;		/* HDMI display status */
	int xscale;		/* x direction scale value */
	int yscale;		/* y directoon scale value */
	int tmdsclk;		/* TDMS Clock frequency */
	int pixclock;		/* Pixel Clcok frequency */
	int uboot_logo;

	struct list_head pwrlist_head;

	int (*insert)(struct hdmi *hdmi);
	int (*remove)(struct hdmi *hdmi);
	void (*control_output)(struct hdmi *hdmi, int enable);
	int (*config_video)(struct hdmi *hdmi,
			     struct hdmi_video_para *vpara);
	int (*config_audio)(struct hdmi *hdmi, struct hdmi_audio *audio);
	int (*detect_hotplug)(struct hdmi *hdmi);
	/* call back for edid */
	int (*read_edid)(struct hdmi *hdmi, int block, unsigned char *buff);
	int (*set_vif)(struct hdmi *hdmi, struct rk_screen *screen,
			bool connect);

	/* call back for hdcp operation */
	void (*hdcp_cb)(void);
	void (*hdcp_irq_cb)(int);
	int (*hdcp_power_on_cb)(void);
	void (*hdcp_power_off_cb)(void);

	/*call back for cec operation*/
	void (*cec_irq)(void);
	void (*cec_set_device_pa)(int);
	int (*cec_enumerate)(void);
	struct rk_hdmi_drv_ops *ops;
	unsigned int *support_vic;
	int support_vic_num;
};

#define hdmi_err(dev, format, arg...)		\
	dev_err(dev , format , ## arg)

#ifdef HDMI_DEBUG
#define hdmi_dbg(dev, format, arg...)		\
	dev_info(dev , format , ## arg)
#else
#define hdmi_dbg(dev, format, arg...)
#endif

int hdmi_drv_register(struct hdmi *hdmi_drv);
int hdmi_get_hotplug(void);
int hdmi_set_info(struct rk_screen *screen, unsigned int vic);
void hdmi_init_lcdc(struct rk_screen *screen,
			   struct rk29lcd_info *lcd_info);
int hdmi_sys_init(struct hdmi *hdmi_drv);
int hdmi_sys_parse_edid(struct hdmi *hdmi_drv);
const char *hdmi_get_video_mode_name(unsigned char vic);
int hdmi_videomode_to_vic(struct fb_videomode *vmode);
const struct fb_videomode *hdmi_vic_to_videomode(int vic);
int hdmi_add_videomode(const struct fb_videomode *mode,
			      struct list_head *head);
struct hdmi_video_timing *hdmi_find_mode(int vic);
int hdmi_find_best_mode(struct hdmi *hdmi_drv, int vic);
int hdmi_ouputmode_select(struct hdmi *hdmi_drv, int edid_ok);
int hdmi_switch_fb(struct hdmi *hdmi_drv, int vic);
int hdmi_init_video_para(struct hdmi *hdmi_drv,
				struct hdmi_video_para *video);
void hdmi_work(struct work_struct *work);
void hdmi_register_display_sysfs(struct hdmi *hdmi_drv,
					struct device *parent);
void hdmi_unregister_display_sysfs(struct hdmi *hdmi_drv);

int rk_hdmi_parse_dt(struct hdmi *hdmi_drv);
int rk_hdmi_pwr_enable(struct hdmi *dev_drv);
int rk_hdmi_pwr_disable(struct hdmi *dev_drv);

#endif
