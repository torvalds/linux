/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tm6000.h - driver for TM5600/TM6000/TM6010 USB video capture devices
 *
 * Copyright (c) 2006-2007 Mauro Carvalho Chehab <mchehab@kernel.org>
 *
 * Copyright (c) 2007 Michel Ludwig <michel.ludwig@gmail.com>
 *	- DVB-T support
 */

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/videobuf-vmalloc.h>
#include "tm6000-usb-isoc.h"
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>

#include <linux/dvb/frontend.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dmxdev.h>

/* Inputs */
enum tm6000_itype {
	TM6000_INPUT_TV	= 1,
	TM6000_INPUT_COMPOSITE1,
	TM6000_INPUT_COMPOSITE2,
	TM6000_INPUT_SVIDEO,
	TM6000_INPUT_DVB,
	TM6000_INPUT_RADIO,
};

enum tm6000_mux {
	TM6000_VMUX_VIDEO_A = 1,
	TM6000_VMUX_VIDEO_B,
	TM6000_VMUX_VIDEO_AB,
	TM6000_AMUX_ADC1,
	TM6000_AMUX_ADC2,
	TM6000_AMUX_SIF1,
	TM6000_AMUX_SIF2,
	TM6000_AMUX_I2S,
};

enum tm6000_devtype {
	TM6000 = 0,
	TM5600,
	TM6010,
};

struct tm6000_input {
	enum tm6000_itype	type;
	enum tm6000_mux		vmux;
	enum tm6000_mux		amux;
	unsigned int		v_gpio;
	unsigned int		a_gpio;
};

/* ------------------------------------------------------------------
 *	Basic structures
 * ------------------------------------------------------------------
 */

struct tm6000_fmt {
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

/* buffer for one video frame */
struct tm6000_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	struct tm6000_fmt      *fmt;
};

struct tm6000_dmaqueue {
	struct list_head       active;
	struct list_head       queued;

	/* thread for generating video stream*/
	struct task_struct         *kthread;
	wait_queue_head_t          wq;
	/* Counters to control fps rate */
	int                        frame;
	int                        ini_jiffies;
};

/* device states */
enum tm6000_core_state {
	DEV_INITIALIZED   = 0x01,
	DEV_DISCONNECTED  = 0x02,
	DEV_MISCONFIGURED = 0x04,
};

/* io methods */
enum tm6000_io_method {
	IO_NONE,
	IO_READ,
	IO_MMAP,
};

enum tm6000_mode {
	TM6000_MODE_UNKNOWN = 0,
	TM6000_MODE_ANALOG,
	TM6000_MODE_DIGITAL,
};

struct tm6000_gpio {
	int		tuner_reset;
	int		tuner_on;
	int		demod_reset;
	int		demod_on;
	int		power_led;
	int		dvb_led;
	int		ir;
};

struct tm6000_capabilities {
	unsigned int    has_tuner:1;
	unsigned int    has_tda9874:1;
	unsigned int    has_dvb:1;
	unsigned int    has_zl10353:1;
	unsigned int    has_eeprom:1;
	unsigned int    has_remote:1;
	unsigned int    has_radio:1;
};

struct tm6000_dvb {
	struct dvb_adapter	adapter;
	struct dvb_demux	demux;
	struct dvb_frontend	*frontend;
	struct dmxdev		dmxdev;
	unsigned int		streams;
	struct urb		*bulk_urb;
	struct mutex		mutex;
};

struct snd_tm6000_card {
	struct snd_card			*card;
	spinlock_t			reg_lock;
	struct tm6000_core		*core;
	struct snd_pcm_substream	*substream;

	/* temporary data for buffer fill processing */
	unsigned			buf_pos;
	unsigned			period_pos;
};

struct tm6000_endpoint {
	struct usb_host_endpoint	*endp;
	__u8				bInterfaceNumber;
	__u8				bAlternateSetting;
	unsigned			maxsize;
};

#define TM6000_QUIRK_NO_USB_DELAY (1 << 0)

struct tm6000_core {
	/* generic device properties */
	char				name[30];	/* name (including minor) of the device */
	int				model;		/* index in the device_data struct */
	int				devno;		/* marks the number of this device */
	enum tm6000_devtype		dev_type;	/* type of device */
	unsigned char			eedata[256];	/* Eeprom data */
	unsigned			eedata_size;	/* Size of the eeprom info */

	v4l2_std_id                     norm;           /* Current norm */
	int				width, height;	/* Selected resolution */

	enum tm6000_core_state		state;

	/* Device Capabilities*/
	struct tm6000_capabilities	caps;

	/* Used to load alsa/dvb */
	struct work_struct		request_module_wk;

	/* Tuner configuration */
	int				tuner_type;		/* type of the tuner */
	int				tuner_addr;		/* tuner address */

	struct tm6000_gpio		gpio;

	char				*ir_codes;

	__u8				radio;

	/* Demodulator configuration */
	int				demod_addr;	/* demodulator address */

	int				audio_bitrate;
	/* i2c i/o */
	struct i2c_adapter		i2c_adap;
	struct i2c_client		i2c_client;


	/* extension */
	struct list_head		devlist;

	/* video for linux */
	int				users;

	/* various device info */
	struct tm6000_fh		*resources;	/* Points to fh that is streaming */
	bool				is_res_read;

	struct video_device		vfd;
	struct video_device		radio_dev;
	struct tm6000_dmaqueue		vidq;
	struct v4l2_device		v4l2_dev;
	struct v4l2_ctrl_handler	ctrl_handler;
	struct v4l2_ctrl_handler	radio_ctrl_handler;

	int				input;
	struct tm6000_input		vinput[3];	/* video input */
	struct tm6000_input		rinput;		/* radio input */

	int				freq;
	unsigned int			fourcc;

	enum tm6000_mode		mode;

	int				ctl_mute;             /* audio */
	int				ctl_volume;
	int				amode;

	/* DVB-T support */
	struct tm6000_dvb		*dvb;

	/* audio support */
	struct snd_tm6000_card		*adev;
	struct work_struct		wq_trigger;   /* Trigger to start/stop audio for alsa module */
	atomic_t			stream_started;  /* stream should be running if true */

	struct tm6000_IR		*ir;

	/* locks */
	struct mutex			lock;
	struct mutex			usb_lock;

	/* usb transfer */
	struct usb_device		*udev;		/* the usb device */

	struct tm6000_endpoint		bulk_in, bulk_out, isoc_in, isoc_out;
	struct tm6000_endpoint		int_in, int_out;

	/* scaler!=0 if scaler is active*/
	int				scaler;

		/* Isoc control struct */
	struct usb_isoc_ctl          isoc_ctl;

	spinlock_t                   slock;

	/* urb dma buffers */
	char				**urb_buffer;
	dma_addr_t			*urb_dma;
	unsigned int			urb_size;

	unsigned long quirks;
};

enum tm6000_ops_type {
	TM6000_AUDIO = 0x10,
	TM6000_DVB = 0x20,
};

struct tm6000_ops {
	struct list_head	next;
	char			*name;
	enum tm6000_ops_type	type;
	int (*init)(struct tm6000_core *);
	int (*fini)(struct tm6000_core *);
	int (*fillbuf)(struct tm6000_core *, char *buf, int size);
};

struct tm6000_fh {
	struct v4l2_fh		     fh;
	struct tm6000_core           *dev;
	unsigned int                 radio;

	/* video capture */
	struct tm6000_fmt            *fmt;
	unsigned int                 width, height;
	struct videobuf_queue        vb_vidq;

	enum v4l2_buf_type           type;
};

#define TM6000_STD	(V4L2_STD_PAL|V4L2_STD_PAL_N|V4L2_STD_PAL_Nc|    \
			V4L2_STD_PAL_M|V4L2_STD_PAL_60|V4L2_STD_NTSC_M| \
			V4L2_STD_NTSC_M_JP|V4L2_STD_SECAM)

/* In tm6000-cards.c */

int tm6000_tuner_callback(void *ptr, int component, int command, int arg);
int tm6000_xc5000_callback(void *ptr, int component, int command, int arg);
int tm6000_cards_setup(struct tm6000_core *dev);
void tm6000_flash_led(struct tm6000_core *dev, u8 state);

/* In tm6000-core.c */

int tm6000_read_write_usb(struct tm6000_core *dev, u8 reqtype, u8 req,
			   u16 value, u16 index, u8 *buf, u16 len);
int tm6000_get_reg(struct tm6000_core *dev, u8 req, u16 value, u16 index);
int tm6000_get_reg16(struct tm6000_core *dev, u8 req, u16 value, u16 index);
int tm6000_get_reg32(struct tm6000_core *dev, u8 req, u16 value, u16 index);
int tm6000_set_reg(struct tm6000_core *dev, u8 req, u16 value, u16 index);
int tm6000_set_reg_mask(struct tm6000_core *dev, u8 req, u16 value,
						u16 index, u16 mask);
int tm6000_i2c_reset(struct tm6000_core *dev, u16 tsleep);
int tm6000_init(struct tm6000_core *dev);
int tm6000_reset(struct tm6000_core *dev);

int tm6000_init_analog_mode(struct tm6000_core *dev);
int tm6000_init_digital_mode(struct tm6000_core *dev);
int tm6000_set_audio_bitrate(struct tm6000_core *dev, int bitrate);
int tm6000_set_audio_rinput(struct tm6000_core *dev);
int tm6000_tvaudio_set_mute(struct tm6000_core *dev, u8 mute);
void tm6000_set_volume(struct tm6000_core *dev, int vol);

int tm6000_v4l2_register(struct tm6000_core *dev);
int tm6000_v4l2_unregister(struct tm6000_core *dev);
int tm6000_v4l2_exit(void);
void tm6000_set_fourcc_format(struct tm6000_core *dev);

void tm6000_remove_from_devlist(struct tm6000_core *dev);
void tm6000_add_into_devlist(struct tm6000_core *dev);
int tm6000_register_extension(struct tm6000_ops *ops);
void tm6000_unregister_extension(struct tm6000_ops *ops);
void tm6000_init_extension(struct tm6000_core *dev);
void tm6000_close_extension(struct tm6000_core *dev);
int tm6000_call_fillbuf(struct tm6000_core *dev, enum tm6000_ops_type type,
			char *buf, int size);


/* In tm6000-stds.c */
void tm6000_get_std_res(struct tm6000_core *dev);
int tm6000_set_standard(struct tm6000_core *dev);

/* In tm6000-i2c.c */
int tm6000_i2c_register(struct tm6000_core *dev);
int tm6000_i2c_unregister(struct tm6000_core *dev);

/* In tm6000-queue.c */

int tm6000_v4l2_mmap(struct file *filp, struct vm_area_struct *vma);

int tm6000_vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type i);
int tm6000_vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type i);
int tm6000_vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *rb);
int tm6000_vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *b);
int tm6000_vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *b);
int tm6000_vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *b);
ssize_t tm6000_v4l2_read(struct file *filp, char __user * buf, size_t count,
			 loff_t *f_pos);
unsigned int tm6000_v4l2_poll(struct file *file,
			      struct poll_table_struct *wait);
int tm6000_queue_init(struct tm6000_core *dev);

/* In tm6000-alsa.c */
/*int tm6000_audio_init(struct tm6000_core *dev, int idx);*/

/* In tm6000-input.c */
int tm6000_ir_init(struct tm6000_core *dev);
int tm6000_ir_fini(struct tm6000_core *dev);
void tm6000_ir_wait(struct tm6000_core *dev, u8 state);
int tm6000_ir_int_start(struct tm6000_core *dev);
void tm6000_ir_int_stop(struct tm6000_core *dev);

/* Debug stuff */

extern int tm6000_debug;

#define dprintk(dev, level, fmt, arg...) do {\
	if (tm6000_debug & level) \
		printk(KERN_INFO "(%lu) %s %s :"fmt, jiffies, \
			 dev->name, __func__ , ##arg); } while (0)

#define V4L2_DEBUG_REG		0x0004
#define V4L2_DEBUG_I2C		0x0008
#define V4L2_DEBUG_QUEUE	0x0010
#define V4L2_DEBUG_ISOC		0x0020
#define V4L2_DEBUG_RES_LOCK	0x0040	/* Resource locking */
#define V4L2_DEBUG_OPEN		0x0080	/* video open/close debug */

#define tm6000_err(fmt, arg...) do {\
	printk(KERN_ERR "tm6000 %s :"fmt, \
		__func__ , ##arg); } while (0)
