#ifndef __ROCKCHIP_HDMI_H__
#define __ROCKCHIP_HDMI_H__

#include <linux/fb.h>
#include <linux/rk_fb.h>
#include <linux/display-sys.h>
#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
#include <sound/pcm_params.h>
#include <linux/reboot.h>

#define HDMI_VIDEO_NORMAL				0
#define HDMI_VIDEO_DMT					BIT(9)
#define HDMI_VIDEO_YUV420				BIT(10)
#define HDMI_VIDEO_DISCRETE_VR				BIT(11)

#define HDMI_VIC_MASK					(0xFF)
#define HDMI_TYPE_MASK					(0xFF << 8)
#define HDMI_MAX_ID					4

#define HDMI_UBOOT_NOT_INIT				BIT(16)
#define HDMI_UBOOT_VIC_MASK				0xFFFF

/* HDMI video information code according CEA-861-F */
enum hdmi_video_information_code {
	HDMI_640X480P_60HZ = 1,
	HDMI_720X480P_60HZ_4_3,
	HDMI_720X480P_60HZ_16_9,
	HDMI_1280X720P_60HZ,
	HDMI_1920X1080I_60HZ,		/*5*/
	HDMI_720X480I_60HZ_4_3,
	HDMI_720X480I_60HZ_16_9,
	HDMI_720X240P_60HZ_4_3,
	HDMI_720X240P_60HZ_16_9,
	HDMI_2880X480I_60HZ_4_3,	/*10*/
	HDMI_2880X480I_60HZ_16_9,
	HDMI_2880X240P_60HZ_4_3,
	HDMI_2880X240P_60HZ_16_9,
	HDMI_1440X480P_60HZ_4_3,
	HDMI_1440X480P_60HZ_16_9,	/*15*/
	HDMI_1920X1080P_60HZ,
	HDMI_720X576P_50HZ_4_3,
	HDMI_720X576P_50HZ_16_9,
	HDMI_1280X720P_50HZ,
	HDMI_1920X1080I_50HZ,		/*20*/
	HDMI_720X576I_50HZ_4_3,
	HDMI_720X576I_50HZ_16_9,
	HDMI_720X288P_50HZ_4_3,
	HDMI_720X288P_50HZ_16_9,
	HDMI_2880X576I_50HZ_4_3,	/*25*/
	HDMI_2880X576I_50HZ_16_9,
	HDMI_2880X288P_50HZ_4_3,
	HDMI_2880X288P_50HZ_16_9,
	HDMI_1440X576P_50HZ_4_3,
	HDMI_1440X576P_50HZ_16_9,	/*30*/
	HDMI_1920X1080P_50HZ,
	HDMI_1920X1080P_24HZ,
	HDMI_1920X1080P_25HZ,
	HDMI_1920X1080P_30HZ,
	HDMI_2880X480P_60HZ_4_3,	/*35*/
	HDMI_2880X480P_60HZ_16_9,
	HDMI_2880X576P_50HZ_4_3,
	HDMI_2880X576P_50HZ_16_9,
	HDMI_1920X1080I_50HZ_1250,	/* V Line 1250 total*/
	HDMI_1920X1080I_100HZ,		/*40*/
	HDMI_1280X720P_100HZ,
	HDMI_720X576P_100HZ_4_3,
	HDMI_720X576P_100HZ_16_9,
	HDMI_720X576I_100HZ_4_3,
	HDMI_720X576I_100HZ_16_9,	/*45*/
	HDMI_1920X1080I_120HZ,
	HDMI_1280X720P_120HZ,
	HDMI_720X480P_120HZ_4_3,
	HDMI_720X480P_120HZ_16_9,
	HDMI_720X480I_120HZ_4_3,	/*50*/
	HDMI_720X480I_120HZ_16_9,
	HDMI_720X576P_200HZ_4_3,
	HDMI_720X576P_200HZ_16_9,
	HDMI_720X576I_200HZ_4_3,
	HDMI_720X576I_200HZ_16_9,	/*55*/
	HDMI_720X480P_240HZ_4_3,
	HDMI_720X480P_240HZ_16_9,
	HDMI_720X480I_240HZ_4_3,
	HDMI_720X480I_240HZ_16_9,
	HDMI_1280X720P_24HZ,		/*60*/
	HDMI_1280X720P_25HZ,
	HDMI_1280X720P_30HZ,
	HDMI_1920X1080P_120HZ,
	HDMI_1920X1080P_100HZ,
	HDMI_1280X720P_24HZ_21_9,	/*65*/
	HDMI_1280X720P_25HZ_21_9,
	HDMI_1280X720P_30HZ_21_9,
	HDMI_1280X720P_50HZ_21_9,
	HDMI_1280X720P_60HZ_21_9,
	HDMI_1280X720P_100HZ_21_9,	/*70*/
	HDMI_1280X720P_120HZ_21_9,
	HDMI_1920X1080P_24HZ_21_9,
	HDMI_1920X1080P_25HZ_21_9,
	HDMI_1920X1080P_30HZ_21_9,
	HDMI_1920X1080P_50HZ_21_9,	/*75*/
	HDMI_1920X1080P_60HZ_21_9,
	HDMI_1920X1080P_100HZ_21_9,
	HDMI_1920X1080P_120HZ_21_9,
	HDMI_1680X720P_24HZ,
	HDMI_1680X720P_25HZ,		/*80*/
	HDMI_1680X720P_30HZ,
	HDMI_1680X720P_50HZ,
	HDMI_1680X720P_60HZ,
	HDMI_1680X720P_100HZ,
	HDMI_1680X720P_120HZ,		/*85*/
	HDMI_2560X1080P_24HZ,
	HDMI_2560X1080P_25HZ,
	HDMI_2560X1080P_30HZ,
	HDMI_2560X1080P_50HZ,
	HDMI_2560X1080P_60HZ,		/*90*/
	HDMI_2560X1080P_100HZ,
	HDMI_2560X1080P_120HZ,
	HDMI_3840X2160P_24HZ,
	HDMI_3840X2160P_25HZ,
	HDMI_3840X2160P_30HZ,		/*95*/
	HDMI_3840X2160P_50HZ,
	HDMI_3840X2160P_60HZ,
	HDMI_4096X2160P_24HZ,
	HDMI_4096X2160P_25HZ,
	HDMI_4096X2160P_30HZ,		/*100*/
	HDMI_4096X2160P_50HZ,
	HDMI_4096X2160P_60HZ,
	HDMI_3840X2160P_24HZ_21_9,
	HDMI_3840X2160P_25HZ_21_9,
	HDMI_3840X2160P_30HZ_21_9,	/*105*/
	HDMI_3840X2160P_50HZ_21_9,
	HDMI_3840X2160P_60HZ_21_9,
};

/* HDMI Extended Resolution */
enum {
	HDMI_VIC_4KX2K_30HZ = 1,
	HDMI_VIC_4KX2K_25HZ,
	HDMI_VIC_4KX2K_24HZ,
	HDMI_VIC_4KX2K_24HZ_SMPTE
};

/* HDMI Video Format */
enum {
	HDMI_VIDEO_FORMAT_NORMAL = 0,
	HDMI_VIDEO_FORMAT_4KX2K,
	HDMI_VIDEO_FORMAT_3D,
};

/* HDMI 3D type */
enum {
	HDMI_3D_NONE = -1,
	HDMI_3D_FRAME_PACKING = 0,
	HDMI_3D_TOP_BOOTOM = 6,
	HDMI_3D_SIDE_BY_SIDE_HALF = 8,
};

/* HDMI Video Data Color Mode */
enum hdmi_video_color_mode {
	HDMI_COLOR_AUTO	= 0,
	HDMI_COLOR_RGB_0_255,
	HDMI_COLOR_RGB_16_235,
	HDMI_COLOR_YCBCR444,
	HDMI_COLOR_YCBCR422,
	HDMI_COLOR_YCBCR420
};

/* HDMI Video Data Color Depth */
enum hdmi_deep_color {
	HDMI_DEPP_COLOR_AUTO = 0,
	HDMI_DEEP_COLOR_Y444 = 0x1,
	HDMI_DEEP_COLOR_30BITS = 0x2,
	HDMI_DEEP_COLOR_36BITS = 0x4,
	HDMI_DEEP_COLOR_48BITS = 0x8,
};

enum hdmi_colorimetry {
	HDMI_COLORIMETRY_NO_DATA = 0,
	HDMI_COLORIMETRY_SMTPE_170M,
	HDMI_COLORIMETRY_ITU709,
	HDMI_COLORIMETRY_EXTEND_XVYCC_601,
	HDMI_COLORIMETRY_EXTEND_XVYCC_709,
	HDMI_COLORIMETRY_EXTEND_SYCC_601,
	HDMI_COLORIMETRY_EXTEND_ADOBE_YCC601,
	HDMI_COLORIMETRY_EXTEND_ADOBE_RGB,
	HDMI_COLORIMETRY_EXTEND_BT_2020_YCC_C, /*constant luminance*/
	HDMI_COLORIMETRY_EXTEND_BT_2020_YCC,
	HDMI_COLORIMETRY_EXTEND_BT_2020_RGB,
};

/* HDMI Audio source */
enum {
	HDMI_AUDIO_SRC_IIS = 0,
	HDMI_AUDIO_SRC_SPDIF
};

/* HDMI Audio Type */
enum hdmi_audio_type {
	HDMI_AUDIO_NLPCM = 0,
	HDMI_AUDIO_LPCM = 1,
	HDMI_AUDIO_AC3,
	HDMI_AUDIO_MPEG1,
	HDMI_AUDIO_MP3,
	HDMI_AUDIO_MPEG2,
	HDMI_AUDIO_AAC_LC,		/*AAC */
	HDMI_AUDIO_DTS,
	HDMI_AUDIO_ATARC,
	HDMI_AUDIO_DSD,			/* One bit Audio */
	HDMI_AUDIO_E_AC3,
	HDMI_AUDIO_DTS_HD,
	HDMI_AUDIO_MLP,
	HDMI_AUDIO_DST,
	HDMI_AUDIO_WMA_PRO
};

/* HDMI Audio Sample Rate */
enum hdmi_audio_samplerate {
	HDMI_AUDIO_FS_32000  = 0x1,
	HDMI_AUDIO_FS_44100  = 0x2,
	HDMI_AUDIO_FS_48000  = 0x4,
	HDMI_AUDIO_FS_88200  = 0x8,
	HDMI_AUDIO_FS_96000  = 0x10,
	HDMI_AUDIO_FS_176400 = 0x20,
	HDMI_AUDIO_FS_192000 = 0x40
};

/* HDMI Audio Word Length */
enum hdmi_audio_word_length {
	HDMI_AUDIO_WORD_LENGTH_16bit = 0x1,
	HDMI_AUDIO_WORD_LENGTH_20bit = 0x2,
	HDMI_AUDIO_WORD_LENGTH_24bit = 0x4
};

/* HDMI Hotplug Status */
enum hdmi_hotpulg_status {
	HDMI_HPD_REMOVED = 0,	/* HDMI is disconnected */
	HDMI_HPD_INSERT,	/* HDMI is connected, but HDP is low
				 * or TMDS link is not pull up to 3.3V.
				 */
	HDMI_HPD_ACTIVATED	/* HDMI is connected, all singnal
				 * is normal
				 */
};

enum hdmi_mute_status {
	HDMI_AV_UNMUTE = 0,
	HDMI_VIDEO_MUTE = 0x1,
	HDMI_AUDIO_MUTE = 0x2,
};

/* HDMI Error Code */
enum hdmi_error_code {
	HDMI_ERROR_SUCCESS = 0,
	HDMI_ERROR_FALSE,
	HDMI_ERROR_I2C,
	HDMI_ERROR_EDID,
};

/* HDMI Video Timing */
struct hdmi_video_timing {
	struct fb_videomode mode;	/* Video timing*/
	unsigned int vic;		/* Video information code*/
	unsigned int vic_2nd;
	unsigned int pixelrepeat;	/* Video pixel repeat rate*/
	unsigned int interface;		/* Video input interface*/
};

/* HDMI Video Parameters */
struct hdmi_video {
	unsigned int vic;		/* Video information code*/
	unsigned int color_input;	/* Input video color mode*/
	unsigned int color_output;	/* Output video color mode*/
	unsigned int color_output_depth;/* Output video Color Depth*/
	unsigned int colorimetry;	/* Output Colorimetry */
	unsigned int sink_hdmi;		/* Output signal is DVI or HDMI*/
	unsigned int format_3d;		/* Output 3D mode*/
	unsigned int eotf;		/* EOTF */
};

/* HDMI Audio Parameters */
struct hdmi_audio {
	u32	type;			/*Audio type*/
	u32	channel;		/*Audio channel number*/
	u32	rate;			/*Audio sampling rate*/
	u32	word_length;		/*Audio data word length*/
};

enum hdmi_hdr_eotf {
	EOTF_TRADITIONAL_GMMA_SDR = 1,
	EOFT_TRADITIONAL_GMMA_HDR = 2,
	EOTF_ST_2084 = 4,
};

struct hdmi_hdr_metadata {
	u32	prim_x0;
	u32	prim_y0;
	u32	prim_x1;
	u32	prim_y1;
	u32	prim_x2;
	u32	prim_y2;
	u32	white_px;
	u32	white_py;
	u32	max_dml;
	u32	min_dml;
	u32	max_cll;		/*max content light level*/
	u32	max_fall;		/*max frame-average light level*/
};

struct hdmi_hdr {
	u8	eotf;
	u8	metadata;	/*Staic Metadata Descriptor*/
	u8	maxluminance;
	u8	max_average_luminance;
	u8	minluminance;
};

#define HDMI_MAX_EDID_BLOCK		8

struct edid_prop_value {
	int vid;
	int pid;
	int sn;
	int xres;
	int yres;
	int vic;
	int width;
	int height;
	int x_w;
	int x_h;
	int hwrotation;
	int einit;
	int vsync;
	int panel;
	int scan;
};

struct edid_prop_data {
	struct edid_prop_value value;

	int valid;
	int last_vid;
	int last_pid;
	int last_sn;
	int last_xres;
	int last_yres;
};

/* HDMI EDID Information */
struct hdmi_edid {
	unsigned char sink_hdmi;	/* HDMI display device flag */
	unsigned char ycbcr444;		/* Display device support YCbCr444 */
	unsigned char ycbcr422;		/* Display device support YCbCr422 */
	unsigned char ycbcr420;		/* Display device support YCbCr420 */
	unsigned char deepcolor;	/* bit3:DC_48bit; bit2:DC_36bit;
					 * bit1:DC_30bit; bit0:DC_Y444;
					 */
	unsigned char deepcolor_420;
	unsigned int  cecaddress;	/* CEC physical address */
	unsigned int  maxtmdsclock;	/* Max supported tmds clock */
	unsigned char fields_present;	/* bit7: latency
					 * bit6: i_lantency
					 * bit5: hdmi_video
					 */
	unsigned char video_latency;
	unsigned char audio_latency;
	unsigned char interlaced_video_latency;
	unsigned char interlaced_audio_latency;
	/* for hdmi 2.0 */
	unsigned char hf_vsdb_version;
	unsigned char scdc_present;
	unsigned char rr_capable;
	unsigned char lte_340mcsc_scramble;
	unsigned char independent_view;
	unsigned char dual_view;
	unsigned char osd_disparity_3d;

	struct edid_prop_value value;

	unsigned int colorimetry;
	struct fb_monspecs	*specs;	/*Device spec*/
	struct list_head modelist;	/*Device supported display mode list*/
	unsigned char baseaudio_support;
	struct hdmi_audio *audio;	/*Device supported audio info*/
	unsigned int  audio_num;	/*Device supported audio type number*/

	unsigned int status;		/*EDID read status, success or failed*/
	u8 *raw[HDMI_MAX_EDID_BLOCK];	/*Raw EDID Data*/
	union {
		u8	data[5];
		struct hdmi_hdr hdrinfo;
	} hdr;
};

struct hdmi;

struct hdmi_ops {
	int (*enable)(struct hdmi *);
	int (*disable)(struct hdmi *);
	int (*getstatus)(struct hdmi *);
	int (*insert)(struct hdmi *);
	int (*remove)(struct hdmi *);
	int (*getedid)(struct hdmi *, int, unsigned char *);
	int (*setvideo)(struct hdmi *, struct hdmi_video *);
	int (*setaudio)(struct hdmi *, struct hdmi_audio *);
	int (*setmute)(struct hdmi *, int);
	int (*setvsi)(struct hdmi *, unsigned char, unsigned char);
	int (*setcec)(struct hdmi *);
	void (*sethdr)(struct hdmi *, int, struct hdmi_hdr_metadata *);
	void (*setavi)(struct hdmi *, struct hdmi_video *);
	/* call back for hdcp operatoion */
	void (*hdcp_cb)(struct hdmi *);
	void (*hdcp_auth2nd)(struct hdmi *);
	void (*hdcp_irq_cb)(int);
	int (*hdcp_power_on_cb)(void);
	void (*hdcp_power_off_cb)(struct hdmi *);
};

enum rk_hdmi_feature {
	SUPPORT_480I_576I	=	(1 << 0),
	SUPPORT_1080I		=	(1 << 1),
	SUPPORT_DEEP_10BIT	=	(1 << 2),
	SUPPORT_DEEP_12BIT	=	(1 << 3),
	SUPPORT_DEEP_16BIT	=	(1 << 4),
	SUPPORT_4K		=	(1 << 5),
	SUPPORT_4K_4096		=	(1 << 6),
	SUPPORT_TMDS_600M	=	(1 << 7),
	SUPPORT_YUV420		=	(1 << 8),
	SUPPORT_CEC		=	(1 << 9),
	SUPPORT_HDCP		=	(1 << 10),
	SUPPORT_HDCP2		=	(1 << 11),
	SUPPORT_YCBCR_INPUT	=	(1 << 12),
	SUPPORT_VESA_DMT	=	(1 << 13),
	SUPPORT_RK_DISCRETE_VR	=	(1 << 14)
};

struct hdmi_property {
	char *name;
	int videosrc;
	int display;
	int feature;
	int defaultmode;
	int defaultdepth;
	void *priv;
};

enum {
	HDMI_SOC_RK3036 = 0,
	HDMI_SOC_RK312X,
	HDMI_SOC_RK322X,
	HDMI_SOC_RK3288,
	HDMI_SOC_RK3366,
	HDMI_SOC_RK3368,
	HDMI_SOC_RK3399,
};

/* HDMI Information */
struct hdmi {
	int id;					/*HDMI id*/
	int soctype;
	struct device	*dev;			/*HDMI device*/
	struct rk_lcdc_driver *lcdc;		/*HDMI linked lcdc*/
	struct rk_display_device *ddev;		/*Registered display device*/
	#ifdef CONFIG_SWITCH
	struct switch_dev	switchdev;	/*Registered switch device*/
	#endif

	struct hdmi_property *property;
	struct hdmi_ops *ops;

	struct mutex lock;			/* mutex for hdmi operation */
	struct mutex pclk_lock;			/* mutex for pclk operation */
	struct workqueue_struct *workqueue;

	bool uboot;	/* if true, HDMI is initialized in uboot*/

	int hotplug;	/* hot plug status*/
	int autoset;	/* if true, auto set hdmi output mode according EDID.*/
	int mute;	/* HDMI display status:
			 * 2 - mute audio,
			 * 1 - mute display;
			 * 0 - unmute
			 */
	int colordepth;			/* Output color depth*/
	int colormode;			/* Output color mode*/
	int colorimetry;		/* Output colorimetry */
	struct hdmi_edid edid;		/* EDID information*/
	struct edid_prop_data prop;	/* Property for dp */
	struct edid_prop_value *pvalue;
	int nstates;
	int edid_auto_support;		/* Auto dp enable flag */

	int enable;			/* Enable flag*/
	int sleep;			/* Sleep flag*/
	int vic;			/* HDMI output video information code*/
	int mode_3d;			/* HDMI output video 3d mode*/
	int eotf;			/* HDMI HDR EOTF */
	struct hdmi_hdr_metadata hdr;	/* HDMI HDR MedeData */
	struct hdmi_audio audio;	/* HDMI output audio information.*/
	struct hdmi_video video;	/* HDMI output video information.*/
	int xscale;
	int yscale;
};

/* HDMI EDID Block Size */
#define HDMI_EDID_BLOCK_SIZE	128

/* SCDC Registers */
#define SCDC_SINK_VER		0x01	/* sink version		*/
#define SCDC_SOURCE_VER		0x02	/* source version	*/
#define SCDC_UPDATE_0		0x10	/* Update_0		*/
#define SCDC_UPDATE_1		0x11	/* Update_1		*/
#define SCDC_UPDATE_RESERVED	0x12	/* 0x12-0x1f - Reserved */
#define SCDC_TMDS_CONFIG	0x20	/* TMDS_Config   */
#define SCDC_SCRAMBLER_STAT	0x21	/* Scrambler_Status   */
#define SCDC_CONFIG_0		0x30	/* Config_0           */
#define SCDC_CONFIG_RESERVED	0x31	/* 0x31-0x3f - Reserved */
#define SCDC_STATUS_FLAG_0	0x40	/* Status_Flag_0        */
#define SCDC_STATUS_FLAG_1	0x41	/* Status_Flag_1        */
#define SCDC_STATUS_RESERVED	0x42	/* 0x42-0x4f - Reserved */
#define SCDC_ERR_DET_0_L	0x50	/* Err_Det_0_L          */
#define SCDC_ERR_DET_0_H	0x51	/* Err_Det_0_H          */
#define SCDC_ERR_DET_1_L	0x52	/* Err_Det_1_L          */
#define SCDC_ERR_DET_1_H	0x53	/* Err_Det_1_H          */
#define SCDC_ERR_DET_2_L	0x54	/* Err_Det_2_L          */
#define SCDC_ERR_DET_2_H	0x55	/* Err_Det_2_H          */
#define SCDC_ERR_DET_CHKSUM	0x56	/* Err_Det_Checksum     */
#define SCDC_TEST_CFG_0		0xc0	/* Test_config_0        */
#define SCDC_TEST_RESERVED	0xc1	/* 0xc1-0xcf		*/
#define SCDC_MAN_OUI_3RD	0xd0	/* Manufacturer IEEE OUI,
					 * Third Octet
					 */
#define SCDC_MAN_OUI_2ND	0xd1	/* Manufacturer IEEE OUI,
					 * Second Octet
					 */
#define SCDC_MAN_OUI_1ST	0xd2	/* Manufacturer IEEE OUI,
					 * First Octet
					 */
#define SCDC_DEVICE_ID		0xd3	/* 0xd3-0xdd - Device ID            */
#define SCDC_MAN_SPECIFIC	0xde	/* 0xde-0xff - ManufacturerSpecific */

/* Event source */
#define HDMI_SRC_SHIFT		8
#define HDMI_SYSFS_SRC		(0x1 << HDMI_SRC_SHIFT)
#define HDMI_SUSPEND_SRC	(0x2 << HDMI_SRC_SHIFT)
#define HDMI_IRQ_SRC		(0x4 << HDMI_SRC_SHIFT)
#define HDMI_WORKQUEUE_SRC	(0x8 << HDMI_SRC_SHIFT)

/* Event */
#define HDMI_ENABLE_CTL			(HDMI_SYSFS_SRC		| 0)
#define HDMI_DISABLE_CTL		(HDMI_SYSFS_SRC		| 1)
#define HDMI_SUSPEND_CTL		(HDMI_SUSPEND_SRC	| 2)
#define HDMI_RESUME_CTL			(HDMI_SUSPEND_SRC	| 3)
#define HDMI_HPD_CHANGE			(HDMI_IRQ_SRC		| 4)
#define HDMI_SET_VIDEO			(HDMI_SYSFS_SRC		| 5)
#define HDMI_SET_AUDIO			(HDMI_SYSFS_SRC		| 6)
#define HDMI_SET_3D			(HDMI_SYSFS_SRC		| 7)
#define HDMI_MUTE_AUDIO			(HDMI_SYSFS_SRC		| 8)
#define HDMI_UNMUTE_AUDIO		(HDMI_SYSFS_SRC		| 9)
#define HDMI_SET_COLOR			(HDMI_SYSFS_SRC		| 10)
#define HDMI_ENABLE_HDCP		(HDMI_SYSFS_SRC		| 11)
#define HDMI_HDCP_AUTH_2ND		(HDMI_IRQ_SRC		| 12)
#define HDMI_SET_HDR			(HDMI_SYSFS_SRC		| 13)

#define HDMI_DEFAULT_SCALE		95
#define HDMI_AUTO_CONFIG		false

/* HDMI default vide mode */
#define HDMI_VIDEO_DEFAULT_MODE			HDMI_1280X720P_60HZ
						/*HDMI_1920X1080P_60HZ*/
#define HDMI_VIDEO_DEFAULT_COLORMODE		HDMI_COLOR_AUTO
#define HDMI_VIDEO_DEFAULT_COLORDEPTH		8

/* HDMI default audio parameter */
#define HDMI_AUDIO_DEFAULT_TYPE			HDMI_AUDIO_LPCM
#define HDMI_AUDIO_DEFAULT_CHANNEL		2
#define HDMI_AUDIO_DEFAULT_RATE			HDMI_AUDIO_FS_44100
#define HDMI_AUDIO_DEFAULT_WORDLENGTH	HDMI_AUDIO_WORD_LENGTH_16bit

extern int hdmi_dbg_level;
#define HDMIDBG(x, format, ...) do {			\
	if (unlikely(hdmi_dbg_level >= x))	\
		pr_info(format, ## __VA_ARGS__); \
			} while (0)

struct hdmi *rockchip_hdmi_register(struct hdmi_property *property,
				    struct hdmi_ops *ops);
void rockchip_hdmi_unregister(struct hdmi *hdmi);
void hdmi_submit_work(struct hdmi *hdmi,
		      int event, int delay, int sync);

struct rk_display_device *hdmi_register_display_sysfs(struct hdmi *hdmi,
						      struct device *parent);
void hdmi_unregister_display_sysfs(struct hdmi *hdmi);

int hdmi_edid_parse_base(struct hdmi *hdmi, unsigned char *buf,
			 int *extend_num, struct hdmi_edid *pedid);
int hdmi_edid_parse_extensions(unsigned char *buf,
			       struct hdmi_edid *pedid);

void hdmi_init_modelist(struct hdmi *hdmi);
int hdmi_set_lcdc(struct hdmi *hdmi);
int hdmi_ouputmode_select(struct hdmi *hdmi, int edid_ok);
int hdmi_add_vic(int vic, struct list_head *head);
int hdmi_find_best_mode(struct hdmi *hdmi, int vic);
int hdmi_videomode_to_vic(struct fb_videomode *vmode);
const struct fb_videomode *hdmi_vic_to_videomode(int vic);
const struct hdmi_video_timing *hdmi_vic2timing(int vic);
int hdmi_config_audio(struct hdmi_audio *audio);
int hdmi_get_hotplug(void);
int snd_config_hdmi_audio(struct snd_pcm_hw_params *params);
#endif
