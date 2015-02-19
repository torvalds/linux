/*
   cx231xx.h - driver for Conexant Cx23100/101/102 USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>
	Based on em28xx driver

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

#ifndef _CX231XX_H
#define _CX231XX_H

#include <linux/videodev2.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/usb.h>

#include <media/cx2341x.h>

#include <media/videobuf-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/rc-core.h>
#include <media/ir-kbd-i2c.h>
#include <media/videobuf-dvb.h>

#include "cx231xx-reg.h"
#include "cx231xx-pcb-cfg.h"
#include "cx231xx-conf-reg.h"

#define DRIVER_NAME                     "cx231xx"
#define PWR_SLEEP_INTERVAL              10

/* I2C addresses for control block in Cx231xx */
#define     AFE_DEVICE_ADDRESS		0x60
#define     I2S_BLK_DEVICE_ADDRESS	0x98
#define     VID_BLK_I2C_ADDRESS		0x88
#define     VERVE_I2C_ADDRESS           0x40
#define     DIF_USE_BASEBAND            0xFFFFFFFF

/* Boards supported by driver */
#define CX231XX_BOARD_UNKNOWN		    0
#define CX231XX_BOARD_CNXT_CARRAERA	1
#define CX231XX_BOARD_CNXT_SHELBY	2
#define CX231XX_BOARD_CNXT_RDE_253S	3
#define CX231XX_BOARD_CNXT_RDU_253S	4
#define CX231XX_BOARD_CNXT_VIDEO_GRABBER	5
#define CX231XX_BOARD_CNXT_RDE_250	6
#define CX231XX_BOARD_CNXT_RDU_250	7
#define CX231XX_BOARD_HAUPPAUGE_EXETER  8
#define CX231XX_BOARD_HAUPPAUGE_USBLIVE2 9
#define CX231XX_BOARD_PV_PLAYTV_USB_HYBRID 10
#define CX231XX_BOARD_PV_XCAPTURE_USB 11
#define CX231XX_BOARD_KWORLD_UB430_USB_HYBRID 12
#define CX231XX_BOARD_ICONBIT_U100 13
#define CX231XX_BOARD_HAUPPAUGE_USB2_FM_PAL 14
#define CX231XX_BOARD_HAUPPAUGE_USB2_FM_NTSC 15
#define CX231XX_BOARD_ELGATO_VIDEO_CAPTURE_V2 16
#define CX231XX_BOARD_OTG102 17
#define CX231XX_BOARD_KWORLD_UB445_USB_HYBRID 18
#define CX231XX_BOARD_HAUPPAUGE_930C_HD_1113xx 19
#define CX231XX_BOARD_HAUPPAUGE_930C_HD_1114xx 20

/* Limits minimum and default number of buffers */
#define CX231XX_MIN_BUF                 4
#define CX231XX_DEF_BUF                 12
#define CX231XX_DEF_VBI_BUF             6

#define VBI_LINE_COUNT                  17
#define VBI_LINE_LENGTH                 1440

/*Limits the max URB message size */
#define URB_MAX_CTRL_SIZE               80

/* Params for validated field */
#define CX231XX_BOARD_NOT_VALIDATED     1
#define CX231XX_BOARD_VALIDATED		0

/* maximum number of cx231xx boards */
#define CX231XX_MAXBOARDS               8

/* maximum number of frames that can be queued */
#define CX231XX_NUM_FRAMES              5

/* number of buffers for isoc transfers */
#define CX231XX_NUM_BUFS                8

/* number of packets for each buffer
   windows requests only 40 packets .. so we better do the same
   this is what I found out for all alternate numbers there!
 */
#define CX231XX_NUM_PACKETS             40

/* default alternate; 0 means choose the best */
#define CX231XX_PINOUT                  0

#define CX231XX_INTERLACED_DEFAULT      1

/* time to wait when stopping the isoc transfer */
#define CX231XX_URB_TIMEOUT		\
		msecs_to_jiffies(CX231XX_NUM_BUFS * CX231XX_NUM_PACKETS)

#define CX231xx_NORMS (\
	V4L2_STD_NTSC_M |  V4L2_STD_NTSC_M_JP |  V4L2_STD_NTSC_443 | \
	V4L2_STD_PAL_BG |  V4L2_STD_PAL_DK    |  V4L2_STD_PAL_I    | \
	V4L2_STD_PAL_M  |  V4L2_STD_PAL_N     |  V4L2_STD_PAL_Nc   | \
	V4L2_STD_PAL_60 |  V4L2_STD_SECAM_L   |  V4L2_STD_SECAM_DK)

#define SLEEP_S5H1432    30
#define CX23417_OSC_EN   8
#define CX23417_RESET    9

struct cx23417_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
	int   flags;
	u32   cxformat;
};
enum cx231xx_mode {
	CX231XX_SUSPEND,
	CX231XX_ANALOG_MODE,
	CX231XX_DIGITAL_MODE,
};

enum cx231xx_std_mode {
	CX231XX_TV_AIR = 0,
	CX231XX_TV_CABLE
};

enum cx231xx_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON,
};

struct cx231xx;

struct cx231xx_isoc_ctl {
	/* max packet size of isoc transaction */
	int max_pkt_size;

	/* number of allocated urbs */
	int num_bufs;

	/* urb for isoc transfers */
	struct urb **urb;

	/* transfer buffers for isoc transfer */
	char **transfer_buffer;

	/* Last buffer command and region */
	u8 cmd;
	int pos, size, pktsize;

	/* Last field: ODD or EVEN? */
	int field;

	/* Stores incomplete commands */
	u32 tmp_buf;
	int tmp_buf_len;

	/* Stores already requested buffers */
	struct cx231xx_buffer *buf;

	/* Stores the number of received fields */
	int nfields;

	/* isoc urb callback */
	int (*isoc_copy) (struct cx231xx *dev, struct urb *urb);
};

struct cx231xx_bulk_ctl {
	/* max packet size of bulk transaction */
	int max_pkt_size;

	/* number of allocated urbs */
	int num_bufs;

	/* urb for bulk transfers */
	struct urb **urb;

	/* transfer buffers for bulk transfer */
	char **transfer_buffer;

	/* Last buffer command and region */
	u8 cmd;
	int pos, size, pktsize;

	/* Last field: ODD or EVEN? */
	int field;

	/* Stores incomplete commands */
	u32 tmp_buf;
	int tmp_buf_len;

	/* Stores already requested buffers */
	struct cx231xx_buffer *buf;

	/* Stores the number of received fields */
	int nfields;

	/* bulk urb callback */
	int (*bulk_copy) (struct cx231xx *dev, struct urb *urb);
};

struct cx231xx_fmt {
	char *name;
	u32 fourcc;		/* v4l2 format id */
	int depth;
	int reg;
};

/* buffer for one video frame */
struct cx231xx_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct list_head frame;
	int top_field;
	int receiving;
};

enum ps_package_head {
	CX231XX_NEED_ADD_PS_PACKAGE_HEAD = 0,
	CX231XX_NONEED_PS_PACKAGE_HEAD
};

struct cx231xx_dmaqueue {
	struct list_head active;
	struct list_head queued;

	wait_queue_head_t wq;

	/* Counters to control buffer fill */
	int pos;
	u8 is_partial_line;
	u8 partial_buf[8];
	u8 last_sav;
	int current_field;
	u32 bytes_left_in_line;
	u32 lines_completed;
	u8 field1_done;
	u32 lines_per_field;

	/*Mpeg2 control buffer*/
	u8 *p_left_data;
	u32 left_data_count;
	u8 mpeg_buffer_done;
	u32 mpeg_buffer_completed;
	enum ps_package_head add_ps_package_head;
	char ps_head[10];
};

/* inputs */

#define MAX_CX231XX_INPUT               4

enum cx231xx_itype {
	CX231XX_VMUX_COMPOSITE1 = 1,
	CX231XX_VMUX_SVIDEO,
	CX231XX_VMUX_TELEVISION,
	CX231XX_VMUX_CABLE,
	CX231XX_RADIO,
	CX231XX_VMUX_DVB,
	CX231XX_VMUX_DEBUG
};

enum cx231xx_v_input {
	CX231XX_VIN_1_1 = 0x1,
	CX231XX_VIN_2_1,
	CX231XX_VIN_3_1,
	CX231XX_VIN_4_1,
	CX231XX_VIN_1_2 = 0x01,
	CX231XX_VIN_2_2,
	CX231XX_VIN_3_2,
	CX231XX_VIN_1_3 = 0x1,
	CX231XX_VIN_2_3,
	CX231XX_VIN_3_3,
};

/* cx231xx has two audio inputs: tuner and line in */
enum cx231xx_amux {
	/* This is the only entry for cx231xx tuner input */
	CX231XX_AMUX_VIDEO,	/* cx231xx tuner */
	CX231XX_AMUX_LINE_IN,	/* Line In */
};

struct cx231xx_reg_seq {
	unsigned char bit;
	unsigned char val;
	int sleep;
};

struct cx231xx_input {
	enum cx231xx_itype type;
	unsigned int vmux;
	enum cx231xx_amux amux;
	struct cx231xx_reg_seq *gpio;
};

#define INPUT(nr) (&cx231xx_boards[dev->model].input[nr])

enum cx231xx_decoder {
	CX231XX_NODECODER,
	CX231XX_AVDECODER
};

enum CX231XX_I2C_MASTER_PORT {
	I2C_0 = 0,       /* master 0 - internal connection */
	I2C_1 = 1,       /* master 1 - used with mux */
	I2C_2 = 2,       /* master 2 */
	I2C_1_MUX_1 = 3, /* master 1 - port 1 (I2C_DEMOD_EN = 0) */
	I2C_1_MUX_3 = 4  /* master 1 - port 3 (I2C_DEMOD_EN = 1) */
};

struct cx231xx_board {
	char *name;
	int vchannels;
	int tuner_type;
	int tuner_addr;
	v4l2_std_id norm;	/* tv norm */

	/* demod related */
	int demod_addr;
	u8 demod_xfer_mode;	/* 0 - Serial; 1 - parallel */

	/* GPIO Pins */
	struct cx231xx_reg_seq *dvb_gpio;
	struct cx231xx_reg_seq *suspend_gpio;
	struct cx231xx_reg_seq *tuner_gpio;
		/* Negative means don't use it */
	s8 tuner_sif_gpio;
	s8 tuner_scl_gpio;
	s8 tuner_sda_gpio;

	/* PIN ctrl */
	u32 ctl_pin_status_mask;
	u8 agc_analog_digital_select_gpio;
	u32 gpio_pin_status_mask;

	/* i2c masters */
	u8 tuner_i2c_master;
	u8 demod_i2c_master;
	u8 ir_i2c_master;

	/* for devices with I2C chips for IR */
	char *rc_map_name;

	unsigned int max_range_640_480:1;
	unsigned int has_dvb:1;
	unsigned int has_417:1;
	unsigned int valid:1;
	unsigned int no_alt_vanc:1;
	unsigned int external_av:1;

	unsigned char xclk, i2c_speed;

	enum cx231xx_decoder decoder;
	int output_mode;

	struct cx231xx_input input[MAX_CX231XX_INPUT];
	struct cx231xx_input radio;
	struct rc_map *ir_codes;
};

/* device states */
enum cx231xx_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
};

enum AFE_MODE {
	AFE_MODE_LOW_IF,
	AFE_MODE_BASEBAND,
	AFE_MODE_EU_HI_IF,
	AFE_MODE_US_HI_IF,
	AFE_MODE_JAPAN_HI_IF
};

enum AUDIO_INPUT {
	AUDIO_INPUT_MUTE,
	AUDIO_INPUT_LINE,
	AUDIO_INPUT_TUNER_TV,
	AUDIO_INPUT_SPDIF,
	AUDIO_INPUT_TUNER_FM
};

#define CX231XX_AUDIO_BUFS              5
#define CX231XX_NUM_AUDIO_PACKETS       16
#define CX231XX_ISO_NUM_AUDIO_PACKETS	64

/* cx231xx extensions */
#define CX231XX_AUDIO                   0x10
#define CX231XX_DVB                     0x20

struct cx231xx_audio {
	char name[50];
	char *transfer_buffer[CX231XX_AUDIO_BUFS];
	struct urb *urb[CX231XX_AUDIO_BUFS];
	struct usb_device *udev;
	unsigned int capture_transfer_done;
	struct snd_pcm_substream *capture_pcm_substream;

	unsigned int hwptr_done_capture;
	struct snd_card *sndcard;

	int users, shutdown;
	/* locks */
	spinlock_t slock;

	int alt;		/* alternate */
	int max_pkt_size;	/* max packet size of isoc transaction */
	int num_alt;		/* Number of alternative settings */
	unsigned int *alt_max_pkt_size;	/* array of wMaxPacketSize */
	u16 end_point_addr;
};

struct cx231xx;

struct cx231xx_fh {
	struct v4l2_fh fh;
	struct cx231xx *dev;
	unsigned int stream_on:1;	/* Locks streams */
	enum v4l2_buf_type type;

	struct videobuf_queue vb_vidq;

	/* vbi capture */
	struct videobuf_queue      vidq;
	struct videobuf_queue      vbiq;

	/* MPEG Encoder specifics ONLY */

	atomic_t                   v4l_reading;
};

/*****************************************************************/
/* set/get i2c */
/* 00--1Mb/s, 01-400kb/s, 10--100kb/s, 11--5Mb/s */
#define I2C_SPEED_1M            0x0
#define I2C_SPEED_400K          0x1
#define I2C_SPEED_100K          0x2
#define I2C_SPEED_5M            0x3

/* 0-- STOP transaction */
#define I2C_STOP                0x0
/* 1-- do not transmit STOP at end of transaction */
#define I2C_NOSTOP              0x1
/* 1--allow slave to insert clock wait states */
#define I2C_SYNC                0x1

struct cx231xx_i2c {
	struct cx231xx *dev;

	int nr;

	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	u32 i2c_rc;

	/* different settings for each bus */
	u8 i2c_period;
	u8 i2c_nostop;
	u8 i2c_reserve;
};

struct cx231xx_i2c_xfer_data {
	u8 dev_addr;
	u8 direction;		/* 1 - IN, 0 - OUT */
	u8 saddr_len;		/* sub address len */
	u16 saddr_dat;		/* sub addr data */
	u8 buf_size;		/* buffer size */
	u8 *p_buffer;		/* pointer to the buffer */
};

struct VENDOR_REQUEST_IN {
	u8 bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
	u8 direction;
	u8 bData;
	u8 *pBuff;
};

struct cx231xx_tvnorm {
	char		*name;
	v4l2_std_id	id;
	u32		cxiformat;
	u32		cxoformat;
};

enum TRANSFER_TYPE {
	Raw_Video = 0,
	Audio,
	Vbi,			/* VANC */
	Sliced_cc,		/* HANC */
	TS1_serial_mode,
	TS2,
	TS1_parallel_mode
} ;

struct cx231xx_video_mode {
	/* Isoc control struct */
	struct cx231xx_dmaqueue vidq;
	struct cx231xx_isoc_ctl isoc_ctl;
	struct cx231xx_bulk_ctl bulk_ctl;
	/* locks */
	spinlock_t slock;

	/* usb transfer */
	int alt;		/* alternate */
	int max_pkt_size;	/* max packet size of isoc transaction */
	int num_alt;		/* Number of alternative settings */
	unsigned int *alt_max_pkt_size;	/* array of wMaxPacketSize */
	u16 end_point_addr;
};

struct cx231xx_tsport {
	struct cx231xx *dev;

	int                        nr;
	int                        sram_chno;

	struct videobuf_dvb_frontends frontends;

	/* dma queues */

	u32                        ts_packet_size;
	u32                        ts_packet_count;

	int                        width;
	int                        height;

	/* locks */
	spinlock_t                 slock;

	/* registers */
	u32                        reg_gpcnt;
	u32                        reg_gpcnt_ctl;
	u32                        reg_dma_ctl;
	u32                        reg_lngth;
	u32                        reg_hw_sop_ctrl;
	u32                        reg_gen_ctrl;
	u32                        reg_bd_pkt_status;
	u32                        reg_sop_status;
	u32                        reg_fifo_ovfl_stat;
	u32                        reg_vld_misc;
	u32                        reg_ts_clk_en;
	u32                        reg_ts_int_msk;
	u32                        reg_ts_int_stat;
	u32                        reg_src_sel;

	/* Default register vals */
	int                        pci_irqmask;
	u32                        dma_ctl_val;
	u32                        ts_int_msk_val;
	u32                        gen_ctrl_val;
	u32                        ts_clk_en_val;
	u32                        src_sel_val;
	u32                        vld_misc_val;
	u32                        hw_sop_ctrl_val;

	/* Allow a single tsport to have multiple frontends */
	u32                        num_frontends;
	void                       *port_priv;
};

/* main device struct */
struct cx231xx {
	/* generic device properties */
	char name[30];		/* name (including minor) of the device */
	int model;		/* index in the device_data struct */
	int devno;		/* marks the number of this device */
	struct device *dev;	/* pointer to USB interface's dev */

	struct cx231xx_board board;

	/* For I2C IR support */
	struct IR_i2c_init_data    init_data;
	struct i2c_client          *ir_i2c_client;

	unsigned int stream_on:1;	/* Locks streams */
	unsigned int vbi_stream_on:1;	/* Locks streams for VBI */
	unsigned int has_audio_class:1;
	unsigned int has_alsa_audio:1;

	unsigned int i2c_scan_running:1; /* true only during i2c_scan */

	struct cx231xx_fmt *format;

	struct v4l2_device v4l2_dev;
	struct v4l2_subdev *sd_cx25840;
	struct v4l2_subdev *sd_tuner;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl_handler radio_ctrl_handler;
	struct cx2341x_handler mpeg_ctrl_handler;

	struct work_struct wq_trigger;		/* Trigger to start/stop audio for alsa module */
	atomic_t	   stream_started;	/* stream should be running if true */

	struct list_head devlist;

	int tuner_type;		/* type of the tuner */
	int tuner_addr;		/* tuner address */

	/* I2C adapters: Master 1 & 2 (External) & Master 3 (Internal only) */
	struct cx231xx_i2c i2c_bus[3];
	struct i2c_adapter *i2c_mux_adap[2];

	unsigned int xc_fw_load_done:1;
	unsigned int port_3_switch_enabled:1;
	/* locks */
	struct mutex gpio_i2c_lock;
	struct mutex i2c_lock;

	/* video for linux */
	int users;		/* user count for exclusive use */
	struct video_device *vdev;	/* video for linux device struct */
	v4l2_std_id norm;	/* selected tv norm */
	int ctl_freq;		/* selected frequency */
	unsigned int ctl_ainput;	/* selected audio input */

	/* frame properties */
	int width;		/* current frame width */
	int height;		/* current frame height */
	int interlaced;		/* 1=interlace fileds, 0=just top fileds */

	struct cx231xx_audio adev;

	/* states */
	enum cx231xx_dev_state state;

	struct work_struct request_module_wk;

	/* locks */
	struct mutex lock;
	struct mutex ctrl_urb_lock;	/* protects urb_buf */
	struct list_head inqueue, outqueue;
	wait_queue_head_t open, wait_frame, wait_stream;
	struct video_device *vbi_dev;
	struct video_device *radio_dev;

	unsigned char eedata[256];

	struct cx231xx_video_mode video_mode;
	struct cx231xx_video_mode vbi_mode;
	struct cx231xx_video_mode sliced_cc_mode;
	struct cx231xx_video_mode ts1_mode;

	atomic_t devlist_count;

	struct usb_device *udev;	/* the usb device */
	char urb_buf[URB_MAX_CTRL_SIZE];	/* urb control msg buffer */

	/* helper funcs that call usb_control_msg */
	int (*cx231xx_read_ctrl_reg) (struct cx231xx *dev, u8 req, u16 reg,
				      char *buf, int len);
	int (*cx231xx_write_ctrl_reg) (struct cx231xx *dev, u8 req, u16 reg,
				       char *buf, int len);
	int (*cx231xx_send_usb_command) (struct cx231xx_i2c *i2c_bus,
				struct cx231xx_i2c_xfer_data *req_data);
	int (*cx231xx_gpio_i2c_read) (struct cx231xx *dev, u8 dev_addr,
				      u8 *buf, u8 len);
	int (*cx231xx_gpio_i2c_write) (struct cx231xx *dev, u8 dev_addr,
				       u8 *buf, u8 len);

	int (*cx231xx_set_analog_freq) (struct cx231xx *dev, u32 freq);
	int (*cx231xx_reset_analog_tuner) (struct cx231xx *dev);

	enum cx231xx_mode mode;

	struct cx231xx_dvb *dvb;

	/* Cx231xx supported PCB config's */
	struct pcb_config current_pcb_config;
	u8 current_scenario_idx;
	u8 interface_count;
	u8 max_iad_interface_count;

	/* GPIO related register direction and values */
	u32 gpio_dir;
	u32 gpio_val;

	/* Power Modes */
	int power_mode;

	/* afe parameters */
	enum AFE_MODE afe_mode;
	u32 afe_ref_count;

	/* video related parameters */
	u32 video_input;
	u32 active_mode;
	u8 vbi_or_sliced_cc_mode;	/* 0 - vbi ; 1 - sliced cc mode */
	enum cx231xx_std_mode std_mode;	/* 0 - Air; 1 - cable */

	/*mode: digital=1 or analog=0*/
	u8 mode_tv;

	u8 USE_ISO;
	struct cx231xx_tvnorm      encodernorm;
	struct cx231xx_tsport      ts1, ts2;
	struct video_device        *v4l_device;
	atomic_t                   v4l_reader_count;
	u32                        freq;
	unsigned int               input;
	u32                        cx23417_mailbox;
	u32                        __iomem *lmmio;
	u8                         __iomem *bmmio;
};

extern struct list_head cx231xx_devlist;

#define cx25840_call(cx231xx, o, f, args...) \
	v4l2_subdev_call(cx231xx->sd_cx25840, o, f, ##args)
#define tuner_call(cx231xx, o, f, args...) \
	v4l2_subdev_call(cx231xx->sd_tuner, o, f, ##args)
#define call_all(dev, o, f, args...) \
	v4l2_device_call_until_err(&dev->v4l2_dev, 0, o, f, ##args)

struct cx231xx_ops {
	struct list_head next;
	char *name;
	int id;
	int (*init) (struct cx231xx *);
	int (*fini) (struct cx231xx *);
};

/* call back functions in dvb module */
int cx231xx_set_analog_freq(struct cx231xx *dev, u32 freq);
int cx231xx_reset_analog_tuner(struct cx231xx *dev);

/* Provided by cx231xx-i2c.c */
void cx231xx_do_i2c_scan(struct cx231xx *dev, int i2c_port);
int cx231xx_i2c_register(struct cx231xx_i2c *bus);
int cx231xx_i2c_unregister(struct cx231xx_i2c *bus);
int cx231xx_i2c_mux_register(struct cx231xx *dev, int mux_no);
void cx231xx_i2c_mux_unregister(struct cx231xx *dev, int mux_no);
struct i2c_adapter *cx231xx_get_i2c_adap(struct cx231xx *dev, int i2c_port);

/* Internal block control functions */
int cx231xx_read_i2c_master(struct cx231xx *dev, u8 dev_addr, u16 saddr,
		 u8 saddr_len, u32 *data, u8 data_len, int master);
int cx231xx_write_i2c_master(struct cx231xx *dev, u8 dev_addr, u16 saddr,
		 u8 saddr_len, u32 data, u8 data_len, int master);
int cx231xx_read_i2c_data(struct cx231xx *dev, u8 dev_addr,
			  u16 saddr, u8 saddr_len, u32 *data, u8 data_len);
int cx231xx_write_i2c_data(struct cx231xx *dev, u8 dev_addr,
			   u16 saddr, u8 saddr_len, u32 data, u8 data_len);
int cx231xx_reg_mask_write(struct cx231xx *dev, u8 dev_addr, u8 size,
			   u16 register_address, u8 bit_start, u8 bit_end,
			   u32 value);
int cx231xx_read_modify_write_i2c_dword(struct cx231xx *dev, u8 dev_addr,
					u16 saddr, u32 mask, u32 value);
u32 cx231xx_set_field(u32 field_mask, u32 data);

/*verve r/w*/
void initGPIO(struct cx231xx *dev);
void uninitGPIO(struct cx231xx *dev);
/* afe related functions */
int cx231xx_afe_init_super_block(struct cx231xx *dev, u32 ref_count);
int cx231xx_afe_init_channels(struct cx231xx *dev);
int cx231xx_afe_setup_AFE_for_baseband(struct cx231xx *dev);
int cx231xx_afe_set_input_mux(struct cx231xx *dev, u32 input_mux);
int cx231xx_afe_set_mode(struct cx231xx *dev, enum AFE_MODE mode);
int cx231xx_afe_update_power_control(struct cx231xx *dev,
					enum AV_MODE avmode);
int cx231xx_afe_adjust_ref_count(struct cx231xx *dev, u32 video_input);

/* i2s block related functions */
int cx231xx_i2s_blk_initialize(struct cx231xx *dev);
int cx231xx_i2s_blk_update_power_control(struct cx231xx *dev,
					enum AV_MODE avmode);
int cx231xx_i2s_blk_set_audio_input(struct cx231xx *dev, u8 audio_input);

/* DIF related functions */
int cx231xx_dif_configure_C2HH_for_low_IF(struct cx231xx *dev, u32 mode,
					  u32 function_mode, u32 standard);
void cx231xx_set_Colibri_For_LowIF(struct cx231xx *dev, u32 if_freq,
					 u8 spectral_invert, u32 mode);
u32 cx231xx_Get_Colibri_CarrierOffset(u32 mode, u32 standerd);
void cx231xx_set_DIF_bandpass(struct cx231xx *dev, u32 if_freq,
					 u8 spectral_invert, u32 mode);
void cx231xx_Setup_AFE_for_LowIF(struct cx231xx *dev);
void reset_s5h1432_demod(struct cx231xx *dev);
void cx231xx_dump_HH_reg(struct cx231xx *dev);
void update_HH_register_after_set_DIF(struct cx231xx *dev);



int cx231xx_dif_set_standard(struct cx231xx *dev, u32 standard);
int cx231xx_tuner_pre_channel_change(struct cx231xx *dev);
int cx231xx_tuner_post_channel_change(struct cx231xx *dev);

/* video parser functions */
u8 cx231xx_find_next_SAV_EAV(u8 *p_buffer, u32 buffer_size,
			     u32 *p_bytes_used);
u8 cx231xx_find_boundary_SAV_EAV(u8 *p_buffer, u8 *partial_buf,
				 u32 *p_bytes_used);
int cx231xx_do_copy(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
		    u8 *p_buffer, u32 bytes_to_copy);
void cx231xx_reset_video_buffer(struct cx231xx *dev,
				struct cx231xx_dmaqueue *dma_q);
u8 cx231xx_is_buffer_done(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q);
u32 cx231xx_copy_video_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			    u8 *p_line, u32 length, int field_number);
u32 cx231xx_get_video_line(struct cx231xx *dev, struct cx231xx_dmaqueue *dma_q,
			   u8 sav_eav, u8 *p_buffer, u32 buffer_size);
void cx231xx_swab(u16 *from, u16 *to, u16 len);

/* Provided by cx231xx-core.c */

u32 cx231xx_request_buffers(struct cx231xx *dev, u32 count);
void cx231xx_queue_unusedframes(struct cx231xx *dev);
void cx231xx_release_buffers(struct cx231xx *dev);

/* read from control pipe */
int cx231xx_read_ctrl_reg(struct cx231xx *dev, u8 req, u16 reg,
			  char *buf, int len);

/* write to control pipe */
int cx231xx_write_ctrl_reg(struct cx231xx *dev, u8 req, u16 reg,
			   char *buf, int len);
int cx231xx_mode_register(struct cx231xx *dev, u16 address, u32 mode);

int cx231xx_send_vendor_cmd(struct cx231xx *dev,
				struct VENDOR_REQUEST_IN *ven_req);
int cx231xx_send_usb_command(struct cx231xx_i2c *i2c_bus,
				struct cx231xx_i2c_xfer_data *req_data);

/* Gpio related functions */
int cx231xx_send_gpio_cmd(struct cx231xx *dev, u32 gpio_bit, u8 *gpio_val,
			  u8 len, u8 request, u8 direction);
int cx231xx_set_gpio_value(struct cx231xx *dev, int pin_number, int pin_value);
int cx231xx_set_gpio_direction(struct cx231xx *dev, int pin_number,
			       int pin_value);

int cx231xx_gpio_i2c_start(struct cx231xx *dev);
int cx231xx_gpio_i2c_end(struct cx231xx *dev);
int cx231xx_gpio_i2c_write_byte(struct cx231xx *dev, u8 data);
int cx231xx_gpio_i2c_read_byte(struct cx231xx *dev, u8 *buf);
int cx231xx_gpio_i2c_read_ack(struct cx231xx *dev);
int cx231xx_gpio_i2c_write_ack(struct cx231xx *dev);
int cx231xx_gpio_i2c_write_nak(struct cx231xx *dev);

int cx231xx_gpio_i2c_read(struct cx231xx *dev, u8 dev_addr, u8 *buf, u8 len);
int cx231xx_gpio_i2c_write(struct cx231xx *dev, u8 dev_addr, u8 *buf, u8 len);

/* audio related functions */
int cx231xx_set_audio_decoder_input(struct cx231xx *dev,
				    enum AUDIO_INPUT audio_input);

int cx231xx_capture_start(struct cx231xx *dev, int start, u8 media_type);
int cx231xx_set_video_alternate(struct cx231xx *dev);
int cx231xx_set_alt_setting(struct cx231xx *dev, u8 index, u8 alt);
int is_fw_load(struct cx231xx *dev);
int cx231xx_check_fw(struct cx231xx *dev);
int cx231xx_init_isoc(struct cx231xx *dev, int max_packets,
		      int num_bufs, int max_pkt_size,
		      int (*isoc_copy) (struct cx231xx *dev,
					struct urb *urb));
int cx231xx_init_bulk(struct cx231xx *dev, int max_packets,
		      int num_bufs, int max_pkt_size,
		      int (*bulk_copy) (struct cx231xx *dev,
					struct urb *urb));
void cx231xx_stop_TS1(struct cx231xx *dev);
void cx231xx_start_TS1(struct cx231xx *dev);
void cx231xx_uninit_isoc(struct cx231xx *dev);
void cx231xx_uninit_bulk(struct cx231xx *dev);
int cx231xx_set_mode(struct cx231xx *dev, enum cx231xx_mode set_mode);
int cx231xx_unmute_audio(struct cx231xx *dev);
int cx231xx_ep5_bulkout(struct cx231xx *dev, u8 *firmware, u16 size);
void cx231xx_disable656(struct cx231xx *dev);
void cx231xx_enable656(struct cx231xx *dev);
int cx231xx_demod_reset(struct cx231xx *dev);
int cx231xx_gpio_set(struct cx231xx *dev, struct cx231xx_reg_seq *gpio);

/* Device list functions */
void cx231xx_release_resources(struct cx231xx *dev);
void cx231xx_release_analog_resources(struct cx231xx *dev);
int cx231xx_register_analog_devices(struct cx231xx *dev);
void cx231xx_remove_from_devlist(struct cx231xx *dev);
void cx231xx_add_into_devlist(struct cx231xx *dev);
void cx231xx_init_extension(struct cx231xx *dev);
void cx231xx_close_extension(struct cx231xx *dev);

/* hardware init functions */
int cx231xx_dev_init(struct cx231xx *dev);
void cx231xx_dev_uninit(struct cx231xx *dev);
void cx231xx_config_i2c(struct cx231xx *dev);
int cx231xx_config(struct cx231xx *dev);

/* Stream control functions */
int cx231xx_start_stream(struct cx231xx *dev, u32 ep_mask);
int cx231xx_stop_stream(struct cx231xx *dev, u32 ep_mask);

int cx231xx_initialize_stream_xfer(struct cx231xx *dev, u32 media_type);

/* Power control functions */
int cx231xx_set_power_mode(struct cx231xx *dev, enum AV_MODE mode);
int cx231xx_power_suspend(struct cx231xx *dev);

/* chip specific control functions */
int cx231xx_init_ctrl_pin_status(struct cx231xx *dev);
int cx231xx_set_agc_analog_digital_mux_select(struct cx231xx *dev,
					      u8 analog_or_digital);
int cx231xx_enable_i2c_port_3(struct cx231xx *dev, bool is_port_3);

/* video audio decoder related functions */
void video_mux(struct cx231xx *dev, int index);
int cx231xx_set_video_input_mux(struct cx231xx *dev, u8 input);
int cx231xx_set_decoder_video_input(struct cx231xx *dev, u8 pin_type, u8 input);
int cx231xx_do_mode_ctrl_overrides(struct cx231xx *dev);
int cx231xx_set_audio_input(struct cx231xx *dev, u8 input);

/* Provided by cx231xx-video.c */
int cx231xx_register_extension(struct cx231xx_ops *dev);
void cx231xx_unregister_extension(struct cx231xx_ops *dev);
void cx231xx_init_extension(struct cx231xx *dev);
void cx231xx_close_extension(struct cx231xx *dev);
int cx231xx_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap);
int cx231xx_g_tuner(struct file *file, void *priv, struct v4l2_tuner *t);
int cx231xx_s_tuner(struct file *file, void *priv, const struct v4l2_tuner *t);
int cx231xx_g_frequency(struct file *file, void *priv,
			      struct v4l2_frequency *f);
int cx231xx_s_frequency(struct file *file, void *priv,
			      const struct v4l2_frequency *f);
int cx231xx_enum_input(struct file *file, void *priv,
			     struct v4l2_input *i);
int cx231xx_g_input(struct file *file, void *priv, unsigned int *i);
int cx231xx_s_input(struct file *file, void *priv, unsigned int i);
int cx231xx_g_chip_info(struct file *file, void *fh, struct v4l2_dbg_chip_info *chip);
int cx231xx_g_register(struct file *file, void *priv,
			     struct v4l2_dbg_register *reg);
int cx231xx_s_register(struct file *file, void *priv,
			     const struct v4l2_dbg_register *reg);

/* Provided by cx231xx-cards.c */
extern void cx231xx_pre_card_setup(struct cx231xx *dev);
extern void cx231xx_card_setup(struct cx231xx *dev);
extern struct cx231xx_board cx231xx_boards[];
extern struct usb_device_id cx231xx_id_table[];
extern const unsigned int cx231xx_bcount;
int cx231xx_tuner_callback(void *ptr, int component, int command, int arg);

/* cx23885-417.c                                               */
extern int cx231xx_417_register(struct cx231xx *dev);
extern void cx231xx_417_unregister(struct cx231xx *dev);

/* cx23885-input.c                                             */

#if defined(CONFIG_VIDEO_CX231XX_RC)
int cx231xx_ir_init(struct cx231xx *dev);
void cx231xx_ir_exit(struct cx231xx *dev);
#else
#define cx231xx_ir_init(dev)	(0)
#define cx231xx_ir_exit(dev)	(0)
#endif

static inline unsigned int norm_maxw(struct cx231xx *dev)
{
	if (dev->board.max_range_640_480)
		return 640;
	else
		return 720;
}

static inline unsigned int norm_maxh(struct cx231xx *dev)
{
	if (dev->board.max_range_640_480)
		return 480;
	else
		return (dev->norm & V4L2_STD_625_50) ? 576 : 480;
}
#endif
