/*
 * Copyright (C) 2005-2006 Micronas USA Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */

/*
 * This is the private include file for the go7007 driver.  It should not
 * be included by anybody but the driver itself, and especially not by
 * user-space applications.
 */

struct go7007;

/* IDs to activate board-specific support code */
#define GO7007_BOARDID_MATRIX_II	0
#define GO7007_BOARDID_MATRIX_RELOAD	1
#define GO7007_BOARDID_STAR_TREK	2
#define GO7007_BOARDID_PCI_VOYAGER	3
#define GO7007_BOARDID_XMEN		4
#define GO7007_BOARDID_XMEN_II		5
#define GO7007_BOARDID_XMEN_III		6
#define GO7007_BOARDID_MATRIX_REV	7
#define GO7007_BOARDID_PX_M402U		16
#define GO7007_BOARDID_PX_TV402U_ANY	17 /* need to check tuner model */
#define GO7007_BOARDID_PX_TV402U_NA	18 /* detected NTSC tuner */
#define GO7007_BOARDID_PX_TV402U_EU	19 /* detected PAL tuner */
#define GO7007_BOARDID_PX_TV402U_JP	20 /* detected NTSC-J tuner */
#define GO7007_BOARDID_LIFEVIEW_LR192	21 /* TV Walker Ultra */
#define GO7007_BOARDID_ENDURA		22
#define GO7007_BOARDID_ADLINK_MPG24	23

/* Various characteristics of each board */
#define GO7007_BOARD_HAS_AUDIO		(1<<0)
#define GO7007_BOARD_USE_ONBOARD_I2C	(1<<1)
#define GO7007_BOARD_HAS_TUNER		(1<<2)

/* Characteristics of sensor devices */
#define GO7007_SENSOR_VALID_POLAR	(1<<0)
#define GO7007_SENSOR_HREF_POLAR	(1<<1)
#define GO7007_SENSOR_VREF_POLAR	(1<<2)
#define GO7007_SENSOR_FIELD_ID_POLAR	(1<<3)
#define GO7007_SENSOR_BIT_WIDTH		(1<<4)
#define GO7007_SENSOR_VALID_ENABLE	(1<<5)
#define GO7007_SENSOR_656		(1<<6)
#define GO7007_SENSOR_CONFIG_MASK	0x7f
#define GO7007_SENSOR_TV		(1<<7)
#define GO7007_SENSOR_VBI		(1<<8)
#define GO7007_SENSOR_SCALING		(1<<9)

/* Characteristics of audio sensor devices */
#define GO7007_AUDIO_I2S_MODE_1		(1)
#define GO7007_AUDIO_I2S_MODE_2		(2)
#define GO7007_AUDIO_I2S_MODE_3		(3)
#define GO7007_AUDIO_BCLK_POLAR		(1<<2)
#define GO7007_AUDIO_WORD_14		(14<<4)
#define GO7007_AUDIO_WORD_16		(16<<4)
#define GO7007_AUDIO_ONE_CHANNEL	(1<<11)
#define GO7007_AUDIO_I2S_MASTER		(1<<16)
#define GO7007_AUDIO_OKI_MODE		(1<<17)

struct go7007_board_info {
	char *firmware;
	unsigned int flags;
	int hpi_buffer_cap;
	unsigned int sensor_flags;
	int sensor_width;
	int sensor_height;
	int sensor_framerate;
	int sensor_h_offset;
	int sensor_v_offset;
	unsigned int audio_flags;
	int audio_rate;
	int audio_bclk_div;
	int audio_main_div;
	int num_i2c_devs;
	struct {
		int id;
		int addr;
	} i2c_devs[4];
	int num_inputs;
	struct {
		int video_input;
		int audio_input;
		char *name;
	} inputs[4];
};

struct go7007_hpi_ops {
	int (*interface_reset)(struct go7007 *go);
	int (*write_interrupt)(struct go7007 *go, int addr, int data);
	int (*read_interrupt)(struct go7007 *go);
	int (*stream_start)(struct go7007 *go);
	int (*stream_stop)(struct go7007 *go);
	int (*send_firmware)(struct go7007 *go, u8 *data, int len);
};

/* The video buffer size must be a multiple of PAGE_SIZE */
#define	GO7007_BUF_PAGES	(128 * 1024 / PAGE_SIZE)
#define	GO7007_BUF_SIZE		(GO7007_BUF_PAGES << PAGE_SHIFT)

struct go7007_buffer {
	struct go7007 *go; /* Reverse reference for VMA ops */
	int index; /* Reverse reference for DQBUF */
	enum { BUF_STATE_IDLE, BUF_STATE_QUEUED, BUF_STATE_DONE } state;
	u32 seq;
	struct timeval timestamp;
	struct list_head stream;
	struct page *pages[GO7007_BUF_PAGES + 1]; /* extra for straddling */
	unsigned long user_addr;
	unsigned int page_count;
	unsigned int offset;
	unsigned int bytesused;
	unsigned int frame_offset;
	u32 modet_active;
	int mapped;
};

struct go7007_file {
	struct go7007 *go;
	struct semaphore lock;
	int buf_count;
	struct go7007_buffer *bufs;
};

#define	GO7007_FORMAT_MJPEG	0
#define GO7007_FORMAT_MPEG4	1
#define GO7007_FORMAT_MPEG1	2
#define GO7007_FORMAT_MPEG2	3
#define GO7007_FORMAT_H263	4

#define GO7007_RATIO_1_1	0
#define GO7007_RATIO_4_3	1
#define GO7007_RATIO_16_9	2

enum go7007_parser_state {
	STATE_DATA,
	STATE_00,
	STATE_00_00,
	STATE_00_00_01,
	STATE_FF,
	STATE_VBI_LEN_A,
	STATE_VBI_LEN_B,
	STATE_MODET_MAP,
	STATE_UNPARSED,
};

struct go7007 {
	struct device *dev;
	struct go7007_board_info *board_info;
	unsigned int board_id;
	int tuner_type;
	int channel_number; /* for multi-channel boards like Adlink PCI-MPG24 */
	char name[64];
	struct video_device *video_dev;
	int ref_count;
	enum { STATUS_INIT, STATUS_ONLINE, STATUS_SHUTDOWN } status;
	spinlock_t spinlock;
	struct semaphore hw_lock;
	int streaming;
	int in_use;
	int audio_enabled;

	/* Video input */
	int input;
	enum { GO7007_STD_NTSC, GO7007_STD_PAL, GO7007_STD_OTHER } standard;
	int sensor_framerate;
	int width;
	int height;
	int encoder_h_offset;
	int encoder_v_offset;
	unsigned int encoder_h_halve:1;
	unsigned int encoder_v_halve:1;
	unsigned int encoder_subsample:1;

	/* Encoder config */
	int format;
	int bitrate;
	int fps_scale;
	int pali;
	int aspect_ratio;
	int gop_size;
	unsigned int ipb:1;
	unsigned int closed_gop:1;
	unsigned int repeat_seqhead:1;
	unsigned int seq_header_enable:1;
	unsigned int gop_header_enable:1;
	unsigned int dvd_mode:1;
	unsigned int interlace_coding:1;

	/* Motion detection */
	unsigned int modet_enable:1;
	struct {
		unsigned int enable:1;
		int pixel_threshold;
		int motion_threshold;
		int mb_threshold;
	} modet[4];
	unsigned char modet_map[1624];
	unsigned char active_map[216];

	/* Video streaming */
	struct go7007_buffer *active_buf;
	enum go7007_parser_state state;
	int parse_length;
	u16 modet_word;
	int seen_frame;
	u32 next_seq;
	struct list_head stream;
	wait_queue_head_t frame_waitq;

	/* Audio streaming */
	void (*audio_deliver)(struct go7007 *go, u8 *buf, int length);
	void *snd_context;

	/* I2C */
	int i2c_adapter_online;
	struct i2c_adapter i2c_adapter;

	/* HPI driver */
	struct go7007_hpi_ops *hpi_ops;
	void *hpi_context;
	int interrupt_available;
	wait_queue_head_t interrupt_waitq;
	unsigned short interrupt_value;
	unsigned short interrupt_data;
};

/* All of these must be called with the hpi_lock semaphore held! */
#define go7007_interface_reset(go) \
			((go)->hpi_ops->interface_reset(go))
#define	go7007_write_interrupt(go, x, y) \
			((go)->hpi_ops->write_interrupt)((go), (x), (y))
#define go7007_stream_start(go) \
			((go)->hpi_ops->stream_start(go))
#define go7007_stream_stop(go) \
			((go)->hpi_ops->stream_stop(go))
#define	go7007_send_firmware(go, x, y) \
			((go)->hpi_ops->send_firmware)((go), (x), (y))
#define go7007_write_addr(go, x, y) \
			((go)->hpi_ops->write_interrupt)((go), (x)|0x8000, (y))

/* go7007-driver.c */
int go7007_read_addr(struct go7007 *go, u16 addr, u16 *data);
int go7007_read_interrupt(struct go7007 *go, u16 *value, u16 *data);
int go7007_boot_encoder(struct go7007 *go, int init_i2c);
int go7007_reset_encoder(struct go7007 *go);
int go7007_register_encoder(struct go7007 *go);
int go7007_start_encoder(struct go7007 *go);
void go7007_parse_video_stream(struct go7007 *go, u8 *buf, int length);
struct go7007 *go7007_alloc(struct go7007_board_info *board,
					struct device *dev);
void go7007_remove(struct go7007 *go);

/* go7007-fw.c */
int go7007_construct_fw_image(struct go7007 *go, u8 **fw, int *fwlen);

/* go7007-i2c.c */
int go7007_i2c_init(struct go7007 *go);
int go7007_i2c_remove(struct go7007 *go);

/* go7007-v4l2.c */
int go7007_v4l2_init(struct go7007 *go);
void go7007_v4l2_remove(struct go7007 *go);

/* snd-go7007.c */
int go7007_snd_init(struct go7007 *go);
int go7007_snd_remove(struct go7007 *go);
