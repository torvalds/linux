/*
   em28xx.h - driver for Empia EM2800/EM2820/2840 USB video capture devices

   Copyright (C) 2005 Markus Rechberger <mrechberger@gmail.com>
		      Ludovico Cavedon <cavedon@sssup.it>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
   Copyright (C) 2012 Frank Schäfer <fschaefer.oss@googlemail.com>

   Based on the em2800 driver from Sascha Sommer <saschasommer@freenet.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _EM28XX_H
#define _EM28XX_H

#include <linux/workqueue.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>

#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/ir-kbd-i2c.h>
#include <media/rc-core.h>
#include "tuner-xc2028.h"
#include "xc5000.h"
#include "em28xx-reg.h"

/* Boards supported by driver */
#define EM2800_BOARD_UNKNOWN			  0
#define EM2820_BOARD_UNKNOWN			  1
#define EM2820_BOARD_TERRATEC_CINERGY_250	  2
#define EM2820_BOARD_PINNACLE_USB_2		  3
#define EM2820_BOARD_HAUPPAUGE_WINTV_USB_2	  4
#define EM2820_BOARD_MSI_VOX_USB_2		  5
#define EM2800_BOARD_TERRATEC_CINERGY_200	  6
#define EM2800_BOARD_LEADTEK_WINFAST_USBII	  7
#define EM2800_BOARD_KWORLD_USB2800		  8
#define EM2820_BOARD_PINNACLE_DVC_90		  9
#define EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900	  10
#define EM2880_BOARD_TERRATEC_HYBRID_XS		  11
#define EM2820_BOARD_KWORLD_PVRTV2800RF		  12
#define EM2880_BOARD_TERRATEC_PRODIGY_XS	  13
#define EM2820_BOARD_PROLINK_PLAYTV_USB2	  14
#define EM2800_BOARD_VGEAR_POCKETTV		  15
#define EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950	  16
#define EM2880_BOARD_PINNACLE_PCTV_HD_PRO	  17
#define EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2	  18
#define EM2860_BOARD_SAA711X_REFERENCE_DESIGN	  19
#define EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600	  20
#define EM2800_BOARD_GRABBEEX_USB2800		  21
#define EM2750_BOARD_UNKNOWN			  22
#define EM2750_BOARD_DLCW_130			  23
#define EM2820_BOARD_DLINK_USB_TV		  24
#define EM2820_BOARD_GADMEI_UTV310		  25
#define EM2820_BOARD_HERCULES_SMART_TV_USB2	  26
#define EM2820_BOARD_PINNACLE_USB_2_FM1216ME	  27
#define EM2820_BOARD_LEADTEK_WINFAST_USBII_DELUXE 28
#define EM2860_BOARD_TVP5150_REFERENCE_DESIGN	  29
#define EM2820_BOARD_VIDEOLOGY_20K14XUSB	  30
#define EM2821_BOARD_USBGEAR_VD204		  31
#define EM2821_BOARD_SUPERCOMP_USB_2		  32
#define EM2860_BOARD_ELGATO_VIDEO_CAPTURE	  33
#define EM2860_BOARD_TERRATEC_HYBRID_XS		  34
#define EM2860_BOARD_TYPHOON_DVD_MAKER		  35
#define EM2860_BOARD_NETGMBH_CAM		  36
#define EM2860_BOARD_GADMEI_UTV330		  37
#define EM2861_BOARD_YAKUMO_MOVIE_MIXER		  38
#define EM2861_BOARD_KWORLD_PVRTV_300U		  39
#define EM2861_BOARD_PLEXTOR_PX_TV100U		  40
#define EM2870_BOARD_KWORLD_350U		  41
#define EM2870_BOARD_KWORLD_355U		  42
#define EM2870_BOARD_TERRATEC_XS		  43
#define EM2870_BOARD_TERRATEC_XS_MT2060		  44
#define EM2870_BOARD_PINNACLE_PCTV_DVB		  45
#define EM2870_BOARD_COMPRO_VIDEOMATE		  46
#define EM2880_BOARD_KWORLD_DVB_305U		  47
#define EM2880_BOARD_KWORLD_DVB_310U		  48
#define EM2880_BOARD_MSI_DIGIVOX_AD		  49
#define EM2880_BOARD_MSI_DIGIVOX_AD_II		  50
#define EM2880_BOARD_TERRATEC_HYBRID_XS_FR	  51
#define EM2881_BOARD_DNT_DA2_HYBRID		  52
#define EM2881_BOARD_PINNACLE_HYBRID_PRO	  53
#define EM2882_BOARD_KWORLD_VS_DVBT		  54
#define EM2882_BOARD_TERRATEC_HYBRID_XS		  55
#define EM2882_BOARD_PINNACLE_HYBRID_PRO_330E	  56
#define EM2883_BOARD_KWORLD_HYBRID_330U		  57
#define EM2820_BOARD_COMPRO_VIDEOMATE_FORYOU	  58
#define EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850	  60
#define EM2820_BOARD_PROLINK_PLAYTV_BOX4_USB2	  61
#define EM2820_BOARD_GADMEI_TVR200		  62
#define EM2860_BOARD_KAIOMY_TVNPC_U2		  63
#define EM2860_BOARD_EASYCAP			  64
#define EM2820_BOARD_IODATA_GVMVP_SZ		  65
#define EM2880_BOARD_EMPIRE_DUAL_TV		  66
#define EM2860_BOARD_TERRATEC_GRABBY		  67
#define EM2860_BOARD_TERRATEC_AV350		  68
#define EM2882_BOARD_KWORLD_ATSC_315U		  69
#define EM2882_BOARD_EVGA_INDTUBE		  70
#define EM2820_BOARD_SILVERCREST_WEBCAM		  71
#define EM2861_BOARD_GADMEI_UTV330PLUS		  72
#define EM2870_BOARD_REDDO_DVB_C_USB_BOX	  73
#define EM2800_BOARD_VC211A			  74
#define EM2882_BOARD_DIKOM_DK300		  75
#define EM2870_BOARD_KWORLD_A340		  76
#define EM2874_BOARD_LEADERSHIP_ISDBT		  77
#define EM28174_BOARD_PCTV_290E			  78
#define EM2884_BOARD_TERRATEC_H5		  79
#define EM28174_BOARD_PCTV_460E			  80
#define EM2884_BOARD_HAUPPAUGE_WINTV_HVR_930C	  81
#define EM2884_BOARD_CINERGY_HTC_STICK		  82
#define EM2860_BOARD_HT_VIDBOX_NW03		  83
#define EM2874_BOARD_MAXMEDIA_UB425_TC		  84
#define EM2884_BOARD_PCTV_510E			  85
#define EM2884_BOARD_PCTV_520E			  86
#define EM2884_BOARD_TERRATEC_HTC_USB_XS	  87

/* Limits minimum and default number of buffers */
#define EM28XX_MIN_BUF 4
#define EM28XX_DEF_BUF 8

/*Limits the max URB message size */
#define URB_MAX_CTRL_SIZE 80

/* Params for validated field */
#define EM28XX_BOARD_NOT_VALIDATED 1
#define EM28XX_BOARD_VALIDATED	   0

/* Params for em28xx_cmd() audio */
#define EM28XX_START_AUDIO      1
#define EM28XX_STOP_AUDIO       0

/* maximum number of em28xx boards */
#define EM28XX_MAXBOARDS 4 /*FIXME: should be bigger */

/* maximum number of frames that can be queued */
#define EM28XX_NUM_FRAMES 5
/* number of frames that get used for v4l2_read() */
#define EM28XX_NUM_READ_FRAMES 2

/* number of buffers for isoc transfers */
#define EM28XX_NUM_BUFS 5
#define EM28XX_DVB_NUM_BUFS 5

/* isoc transfers: number of packets for each buffer
   windows requests only 64 packets .. so we better do the same
   this is what I found out for all alternate numbers there!
 */
#define EM28XX_NUM_ISOC_PACKETS 64
#define EM28XX_DVB_NUM_ISOC_PACKETS 64

/* bulk transfers: transfer buffer size = packet size * packet multiplier
   USB 2.0 spec says bulk packet size is always 512 bytes
 */
#define EM28XX_BULK_PACKET_MULTIPLIER 384
#define EM28XX_DVB_BULK_PACKET_MULTIPLIER 384

#define EM28XX_INTERLACED_DEFAULT 1

/* time in msecs to wait for i2c writes to finish */
#define EM2800_I2C_XFER_TIMEOUT		20

enum em28xx_mode {
	EM28XX_SUSPEND,
	EM28XX_ANALOG_MODE,
	EM28XX_DIGITAL_MODE,
};


struct em28xx;

struct em28xx_usb_bufs {
		/* max packet size of isoc transaction */
	int				max_pkt_size;

		/* number of packets in each buffer */
	int				num_packets;

		/* number of allocated urbs */
	int				num_bufs;

		/* urb for isoc/bulk transfers */
	struct urb			**urb;

		/* transfer buffers for isoc/bulk transfer */
	char				**transfer_buffer;
};

struct em28xx_usb_ctl {
		/* isoc/bulk transfer buffers for analog mode */
	struct em28xx_usb_bufs		analog_bufs;

		/* isoc/bulk transfer buffers for digital mode */
	struct em28xx_usb_bufs		digital_bufs;

		/* Stores already requested buffers */
	struct em28xx_buffer    	*vid_buf;
	struct em28xx_buffer    	*vbi_buf;

		/* copy data from URB */
	int (*urb_data_copy) (struct em28xx *dev, struct urb *urb);

};

/* Struct to enumberate video formats */
struct em28xx_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   reg;
};

/* buffer for one video frame */
struct em28xx_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_buffer vb;
	struct list_head list;

	void *mem;
	unsigned int length;
	int top_field;

	/* counter to control buffer fill */
	unsigned int pos;
	/* NOTE; in interlaced mode, this value is reset to zero at
	 * the start of each new field (not frame !)		   */

	/* pointer to vmalloc memory address in vb */
	char *vb_buf;
};

struct em28xx_dmaqueue {
	struct list_head       active;

	wait_queue_head_t          wq;
};

/* inputs */

#define MAX_EM28XX_INPUT 4
enum enum28xx_itype {
	EM28XX_VMUX_COMPOSITE1 = 1,
	EM28XX_VMUX_COMPOSITE2,
	EM28XX_VMUX_COMPOSITE3,
	EM28XX_VMUX_COMPOSITE4,
	EM28XX_VMUX_SVIDEO,
	EM28XX_VMUX_TELEVISION,
	EM28XX_VMUX_CABLE,
	EM28XX_VMUX_DVB,
	EM28XX_VMUX_DEBUG,
	EM28XX_RADIO,
};

enum em28xx_ac97_mode {
	EM28XX_NO_AC97 = 0,
	EM28XX_AC97_EM202,
	EM28XX_AC97_SIGMATEL,
	EM28XX_AC97_OTHER,
};

struct em28xx_audio_mode {
	enum em28xx_ac97_mode ac97;

	u16 ac97_feat;
	u32 ac97_vendor_id;

	unsigned int has_audio:1;

	unsigned int i2s_3rates:1;
	unsigned int i2s_5rates:1;
};

/* em28xx has two audio inputs: tuner and line in.
   However, on most devices, an auxiliary AC97 codec device is used.
   The AC97 device may have several different inputs and outputs,
   depending on their model. So, it is possible to use AC97 mixer to
   address more than two different entries.
 */
enum em28xx_amux {
	/* This is the only entry for em28xx tuner input */
	EM28XX_AMUX_VIDEO,	/* em28xx tuner, AC97 mixer Video */

	EM28XX_AMUX_LINE_IN,	/* AC97 mixer Line In */

	/* Some less-common mixer setups */
	EM28XX_AMUX_VIDEO2,	/* em28xx Line in, AC97 mixer Video */
	EM28XX_AMUX_PHONE,
	EM28XX_AMUX_MIC,
	EM28XX_AMUX_CD,
	EM28XX_AMUX_AUX,
	EM28XX_AMUX_PCM_OUT,
};

enum em28xx_aout {
	/* AC97 outputs */
	EM28XX_AOUT_MASTER = 1 << 0,
	EM28XX_AOUT_LINE   = 1 << 1,
	EM28XX_AOUT_MONO   = 1 << 2,
	EM28XX_AOUT_LFE    = 1 << 3,
	EM28XX_AOUT_SURR   = 1 << 4,

	/* PCM IN Mixer - used by AC97_RECORD_SELECT register */
	EM28XX_AOUT_PCM_IN = 1 << 7,

	/* Bits 10-8 are used to indicate the PCM IN record select */
	EM28XX_AOUT_PCM_MIC_PCM = 0 << 8,
	EM28XX_AOUT_PCM_CD	= 1 << 8,
	EM28XX_AOUT_PCM_VIDEO	= 2 << 8,
	EM28XX_AOUT_PCM_AUX	= 3 << 8,
	EM28XX_AOUT_PCM_LINE	= 4 << 8,
	EM28XX_AOUT_PCM_STEREO	= 5 << 8,
	EM28XX_AOUT_PCM_MONO	= 6 << 8,
	EM28XX_AOUT_PCM_PHONE	= 7 << 8,
};

static inline int ac97_return_record_select(int a_out)
{
	return (a_out & 0x700) >> 8;
}

struct em28xx_reg_seq {
	int reg;
	unsigned char val, mask;
	int sleep;
};

struct em28xx_input {
	enum enum28xx_itype type;
	unsigned int vmux;
	enum em28xx_amux amux;
	enum em28xx_aout aout;
	struct em28xx_reg_seq *gpio;
};

#define INPUT(nr) (&em28xx_boards[dev->model].input[nr])

enum em28xx_decoder {
	EM28XX_NODECODER = 0,
	EM28XX_TVP5150,
	EM28XX_SAA711X,
};

enum em28xx_sensor {
	EM28XX_NOSENSOR = 0,
	EM28XX_MT9V011,
	EM28XX_MT9M001,
	EM28XX_MT9M111,
};

enum em28xx_adecoder {
	EM28XX_NOADECODER = 0,
	EM28XX_TVAUDIO,
};

struct em28xx_board {
	char *name;
	int vchannels;
	int tuner_type;
	int tuner_addr;

	/* i2c flags */
	unsigned int tda9887_conf;

	/* GPIO sequences */
	struct em28xx_reg_seq *dvb_gpio;
	struct em28xx_reg_seq *suspend_gpio;
	struct em28xx_reg_seq *tuner_gpio;
	struct em28xx_reg_seq *mute_gpio;

	unsigned int is_em2800:1;
	unsigned int has_msp34xx:1;
	unsigned int mts_firmware:1;
	unsigned int max_range_640_480:1;
	unsigned int has_dvb:1;
	unsigned int has_snapshot_button:1;
	unsigned int is_webcam:1;
	unsigned int valid:1;
	unsigned int has_ir_i2c:1;

	unsigned char xclk, i2c_speed;
	unsigned char radio_addr;
	unsigned short tvaudio_addr;

	enum em28xx_decoder decoder;
	enum em28xx_adecoder adecoder;

	struct em28xx_input       input[MAX_EM28XX_INPUT];
	struct em28xx_input	  radio;
	char			  *ir_codes;
};

struct em28xx_eeprom {
	u8 id[4];			/* 1a eb 67 95 */
	__le16 vendor_ID;
	__le16 product_ID;

	__le16 chip_conf;

	__le16 board_conf;

	__le16 string1, string2, string3;

	u8 string_idx_table;
};

#define EM28XX_AUDIO_BUFS 5
#define EM28XX_NUM_AUDIO_PACKETS 64
#define EM28XX_AUDIO_MAX_PACKET_SIZE 196 /* static value */
#define EM28XX_CAPTURE_STREAM_EN 1

/* em28xx extensions */
#define EM28XX_AUDIO   0x10
#define EM28XX_DVB     0x20
#define EM28XX_RC      0x30

/* em28xx resource types (used for res_get/res_lock etc */
#define EM28XX_RESOURCE_VIDEO 0x01
#define EM28XX_RESOURCE_VBI   0x02

struct em28xx_audio {
	char name[50];
	char *transfer_buffer[EM28XX_AUDIO_BUFS];
	struct urb *urb[EM28XX_AUDIO_BUFS];
	struct usb_device *udev;
	unsigned int capture_transfer_done;
	struct snd_pcm_substream   *capture_pcm_substream;

	unsigned int hwptr_done_capture;
	struct snd_card            *sndcard;

	int users;
	spinlock_t slock;
};

struct em28xx;

struct em28xx_fh {
	struct v4l2_fh fh;
	struct em28xx *dev;

	enum v4l2_buf_type           type;
};

/* main device struct */
struct em28xx {
	/* generic device properties */
	char name[30];		/* name (including minor) of the device */
	int model;		/* index in the device_data struct */
	int devno;		/* marks the number of this device */
	enum em28xx_chip_id chip_id;

	unsigned char disconnected:1;	/* device has been diconnected */

	int audio_ifnum;

	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	struct em28xx_board board;

	/* Webcam specific fields */
	enum em28xx_sensor em28xx_sensor;
	int sensor_xres, sensor_yres;
	int sensor_xtal;

	/* Progressive (non-interlaced) mode */
	int progressive;

	/* Vinmode/Vinctl used at the driver */
	int vinmode, vinctl;

	unsigned int has_audio_class:1;
	unsigned int has_alsa_audio:1;
	unsigned int is_audio_only:1;

	/* Controls audio streaming */
	struct work_struct wq_trigger;	/* Trigger to start/stop audio for alsa module */
	atomic_t       stream_started;	/* stream should be running if true */

	struct em28xx_fmt *format;

	struct em28xx_IR *ir;

	/* Some older em28xx chips needs a waiting time after writing */
	unsigned int wait_after_write;

	struct list_head	devlist;

	u32 i2s_speed;		/* I2S speed for audio digital stream */

	struct em28xx_audio_mode audio_mode;

	int tuner_type;		/* type of the tuner */
	int tuner_addr;		/* tuner address */
	int tda9887_conf;
	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;
	unsigned char eeprom_addrwidth_16bit:1;
	/* video for linux */
	int users;		/* user count for exclusive use */
	int streaming_users;    /* Number of actively streaming users */
	struct video_device *vdev;	/* video for linux device struct */
	v4l2_std_id norm;	/* selected tv norm */
	int ctl_freq;		/* selected frequency */
	unsigned int ctl_input;	/* selected input */
	unsigned int ctl_ainput;/* selected audio input */
	unsigned int ctl_aoutput;/* selected audio output */
	int mute;
	int volume;
	/* frame properties */
	int width;		/* current frame width */
	int height;		/* current frame height */
	unsigned hscale;	/* horizontal scale factor (see datasheet) */
	unsigned vscale;	/* vertical scale factor (see datasheet) */
	int interlaced;		/* 1=interlace fileds, 0=just top fileds */
	unsigned int video_bytesread;	/* Number of bytes read */

	unsigned long hash;	/* eeprom hash - for boards with generic ID */
	unsigned long i2c_hash;	/* i2c devicelist hash -
				   for boards with generic ID */

	struct em28xx_audio adev;

	/* capture state tracking */
	int capture_type;
	unsigned char top_field:1;
	int vbi_read;
	unsigned int vbi_width;
	unsigned int vbi_height; /* lines per field */

	struct work_struct         request_module_wk;

	/* locks */
	struct mutex lock;
	struct mutex ctrl_urb_lock;	/* protects urb_buf */
	/* spinlock_t queue_lock; */
	struct list_head inqueue, outqueue;
	struct video_device *vbi_dev;
	struct video_device *radio_dev;

	/* Videobuf2 */
	struct vb2_queue vb_vidq;
	struct vb2_queue vb_vbiq;
	struct mutex vb_queue_lock;
	struct mutex vb_vbi_queue_lock;

	/* resources in use */
	unsigned int resources;

	u8 *eedata;	/* currently always 256 bytes */

	/* Isoc control struct */
	struct em28xx_dmaqueue vidq;
	struct em28xx_dmaqueue vbiq;
	struct em28xx_usb_ctl usb_ctl;
	spinlock_t slock;

	unsigned int field_count;
	unsigned int vbi_field_count;

	/* usb transfer */
	struct usb_device *udev;	/* the usb device */
	u8 analog_ep_isoc;	/* address of isoc endpoint for analog */
	u8 analog_ep_bulk;	/* address of bulk endpoint for analog */
	u8 dvb_ep_isoc;		/* address of isoc endpoint for DVB */
	u8 dvb_ep_bulk;		/* address of bulk endpoint for DVB */
	int alt;		/* alternate setting */
	int max_pkt_size;	/* max packet size of the selected ep at alt */
	int packet_multiplier;	/* multiplier for wMaxPacketSize, used for
				   URB buffer size definition */
	int num_alt;		/* number of alternative settings */
	unsigned int *alt_max_pkt_size_isoc; /* array of isoc wMaxPacketSize */
	unsigned int analog_xfer_bulk:1;	/* use bulk instead of isoc
						   transfers for analog      */
	int dvb_alt_isoc;	/* alternate setting for DVB isoc transfers */
	unsigned int dvb_max_pkt_size_isoc;	/* isoc max packet size of the
						   selected DVB ep at dvb_alt */
	unsigned int dvb_xfer_bulk:1;		/* use bulk instead of isoc
						   transfers for DVB          */
	char urb_buf[URB_MAX_CTRL_SIZE];	/* urb control msg buffer */

	/* helper funcs that call usb_control_msg */
	int (*em28xx_write_regs) (struct em28xx *dev, u16 reg,
					char *buf, int len);
	int (*em28xx_read_reg) (struct em28xx *dev, u16 reg);
	int (*em28xx_read_reg_req_len) (struct em28xx *dev, u8 req, u16 reg,
					char *buf, int len);
	int (*em28xx_write_regs_req) (struct em28xx *dev, u8 req, u16 reg,
				      char *buf, int len);
	int (*em28xx_read_reg_req) (struct em28xx *dev, u8 req, u16 reg);

	enum em28xx_mode mode;

	/* register numbers for GPO/GPIO registers */
	u16 reg_gpo_num, reg_gpio_num;

	/* Caches GPO and GPIO registers */
	unsigned char	reg_gpo, reg_gpio;

	/* Snapshot button */
	char snapshot_button_path[30];	/* path of the input dev */
	struct input_dev *sbutton_input_dev;
	struct delayed_work sbutton_query_work;

	struct em28xx_dvb *dvb;
};

struct em28xx_ops {
	struct list_head next;
	char *name;
	int id;
	int (*init)(struct em28xx *);
	int (*fini)(struct em28xx *);
};

/* Provided by em28xx-i2c.c */
void em28xx_do_i2c_scan(struct em28xx *dev);
int  em28xx_i2c_register(struct em28xx *dev);
int  em28xx_i2c_unregister(struct em28xx *dev);

/* Provided by em28xx-core.c */
int em28xx_read_reg_req_len(struct em28xx *dev, u8 req, u16 reg,
			    char *buf, int len);
int em28xx_read_reg_req(struct em28xx *dev, u8 req, u16 reg);
int em28xx_read_reg(struct em28xx *dev, u16 reg);
int em28xx_write_regs_req(struct em28xx *dev, u8 req, u16 reg, char *buf,
			  int len);
int em28xx_write_regs(struct em28xx *dev, u16 reg, char *buf, int len);
int em28xx_write_reg(struct em28xx *dev, u16 reg, u8 val);
int em28xx_write_reg_bits(struct em28xx *dev, u16 reg, u8 val,
				 u8 bitmask);

int em28xx_read_ac97(struct em28xx *dev, u8 reg);
int em28xx_write_ac97(struct em28xx *dev, u8 reg, u16 val);

int em28xx_audio_analog_set(struct em28xx *dev);
int em28xx_audio_setup(struct em28xx *dev);

int em28xx_colorlevels_set_default(struct em28xx *dev);
int em28xx_capture_start(struct em28xx *dev, int start);
int em28xx_vbi_supported(struct em28xx *dev);
int em28xx_set_outfmt(struct em28xx *dev);
int em28xx_resolution_set(struct em28xx *dev);
int em28xx_set_alternate(struct em28xx *dev);
int em28xx_alloc_urbs(struct em28xx *dev, enum em28xx_mode mode, int xfer_bulk,
		      int num_bufs, int max_pkt_size, int packet_multiplier);
int em28xx_init_usb_xfer(struct em28xx *dev, enum em28xx_mode mode,
			 int xfer_bulk,
			 int num_bufs, int max_pkt_size, int packet_multiplier,
			 int (*urb_data_copy)
					(struct em28xx *dev, struct urb *urb));
void em28xx_uninit_usb_xfer(struct em28xx *dev, enum em28xx_mode mode);
void em28xx_stop_urbs(struct em28xx *dev);
int em28xx_set_mode(struct em28xx *dev, enum em28xx_mode set_mode);
int em28xx_gpio_set(struct em28xx *dev, struct em28xx_reg_seq *gpio);
void em28xx_wake_i2c(struct em28xx *dev);
int em28xx_register_extension(struct em28xx_ops *dev);
void em28xx_unregister_extension(struct em28xx_ops *dev);
void em28xx_init_extension(struct em28xx *dev);
void em28xx_close_extension(struct em28xx *dev);

/* Provided by em28xx-video.c */
int em28xx_vb2_setup(struct em28xx *dev);
int em28xx_register_analog_devices(struct em28xx *dev);
void em28xx_release_analog_resources(struct em28xx *dev);
void em28xx_ctrl_notify(struct v4l2_ctrl *ctrl, void *priv);
int em28xx_start_analog_streaming(struct vb2_queue *vq, unsigned int count);
int em28xx_stop_vbi_streaming(struct vb2_queue *vq);
extern const struct v4l2_ctrl_ops em28xx_ctrl_ops;

/* Provided by em28xx-cards.c */
extern struct em28xx_board em28xx_boards[];
extern struct usb_device_id em28xx_id_table[];
int em28xx_tuner_callback(void *ptr, int component, int command, int arg);
void em28xx_release_resources(struct em28xx *dev);

/* Provided by em28xx-vbi.c */
extern struct vb2_ops em28xx_vbi_qops;

/* printk macros */

#define em28xx_err(fmt, arg...) do {\
	printk(KERN_ERR fmt , ##arg); } while (0)

#define em28xx_errdev(fmt, arg...) do {\
	printk(KERN_ERR "%s: "fmt,\
			dev->name , ##arg); } while (0)

#define em28xx_info(fmt, arg...) do {\
	printk(KERN_INFO "%s: "fmt,\
			dev->name , ##arg); } while (0)
#define em28xx_warn(fmt, arg...) do {\
	printk(KERN_WARNING "%s: "fmt,\
			dev->name , ##arg); } while (0)

static inline int em28xx_compression_disable(struct em28xx *dev)
{
	/* side effect of disabling scaler and mixer */
	return em28xx_write_reg(dev, EM28XX_R26_COMPR, 0x00);
}

/*FIXME: maxw should be dependent of alt mode */
static inline unsigned int norm_maxw(struct em28xx *dev)
{
	if (dev->board.is_webcam)
		return dev->sensor_xres;

	if (dev->board.max_range_640_480)
		return 640;

	return 720;
}

static inline unsigned int norm_maxh(struct em28xx *dev)
{
	if (dev->board.is_webcam)
		return dev->sensor_yres;

	if (dev->board.max_range_640_480)
		return 480;

	return (dev->norm & V4L2_STD_625_50) ? 576 : 480;
}
#endif
