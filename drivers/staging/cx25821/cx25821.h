/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef CX25821_H_
#define CX25821_H_

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kdev_t.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/videobuf-dma-sg.h>
#include <media/videobuf-dvb.h>

#include "btcx-risc.h"
#include "cx25821-reg.h"
#include "cx25821-medusa-reg.h"
#include "cx25821-sram.h"
#include "cx25821-audio.h"
#include "media/cx2341x.h"

#include <linux/version.h>
#include <linux/mutex.h>

#define CX25821_VERSION_CODE KERNEL_VERSION(0, 0, 106)

#define UNSET (-1U)
#define NO_SYNC_LINE (-1U)

#define CX25821_MAXBOARDS 2

#define TRUE    1
#define FALSE   0
#define LINE_SIZE_D1    1440

/* Number of decoders and encoders */
#define MAX_DECODERS            8
#define MAX_ENCODERS            2
#define QUAD_DECODERS           4
#define MAX_CAMERAS             16

/* Max number of inputs by card */
#define MAX_CX25821_INPUT 8
#define INPUT(nr) (&cx25821_boards[dev->board].input[nr])
#define RESOURCE_VIDEO0       1
#define RESOURCE_VIDEO1       2
#define RESOURCE_VIDEO2       4
#define RESOURCE_VIDEO3       8
#define RESOURCE_VIDEO4       16
#define RESOURCE_VIDEO5       32
#define RESOURCE_VIDEO6       64
#define RESOURCE_VIDEO7       128
#define RESOURCE_VIDEO8       256
#define RESOURCE_VIDEO9       512
#define RESOURCE_VIDEO10      1024
#define RESOURCE_VIDEO11      2048
#define RESOURCE_VIDEO_IOCTL  4096

#define BUFFER_TIMEOUT     (HZ)	/* 0.5 seconds */

#define UNKNOWN_BOARD       0
#define CX25821_BOARD        1

/* Currently supported by the driver */
#define CX25821_NORMS (\
	V4L2_STD_NTSC_M |  V4L2_STD_NTSC_M_JP | V4L2_STD_NTSC_M_KR | \
	V4L2_STD_PAL_BG |  V4L2_STD_PAL_DK    |  V4L2_STD_PAL_I    | \
	V4L2_STD_PAL_M  |  V4L2_STD_PAL_N     |  V4L2_STD_PAL_H    | \
	V4L2_STD_PAL_Nc)

#define CX25821_BOARD_CONEXANT_ATHENA10 1
#define MAX_VID_CHANNEL_NUM     12
#define VID_CHANNEL_NUM 8

struct cx25821_fmt {
	char *name;
	u32 fourcc;		/* v4l2 format id */
	int depth;
	int flags;
	u32 cxformat;
};

struct cx25821_ctrl {
	struct v4l2_queryctrl v;
	u32 off;
	u32 reg;
	u32 mask;
	u32 shift;
};

struct cx25821_tvnorm {
	char *name;
	v4l2_std_id id;
	u32 cxiformat;
	u32 cxoformat;
};

struct cx25821_fh {
	struct cx25821_dev *dev;
	enum v4l2_buf_type type;
	int radio;
	u32 resources;

	enum v4l2_priority prio;

	/* video overlay */
	struct v4l2_window win;
	struct v4l2_clip *clips;
	unsigned int nclips;

	/* video capture */
	struct cx25821_fmt *fmt;
	unsigned int width, height;
	int channel_id;

	/* vbi capture */
	struct videobuf_queue vidq;
	struct videobuf_queue vbiq;

	/* H264 Encoder specifics ONLY */
	struct videobuf_queue mpegq;
	atomic_t v4l_reading;
};

enum cx25821_itype {
	CX25821_VMUX_COMPOSITE = 1,
	CX25821_VMUX_SVIDEO,
	CX25821_VMUX_DEBUG,
	CX25821_RADIO,
};

enum cx25821_src_sel_type {
	CX25821_SRC_SEL_EXT_656_VIDEO = 0,
	CX25821_SRC_SEL_PARALLEL_MPEG_VIDEO
};

/* buffer for one video frame */
struct cx25821_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* cx25821 specific */
	unsigned int bpl;
	struct btcx_riscmem risc;
	struct cx25821_fmt *fmt;
	u32 count;
};

struct cx25821_input {
	enum cx25821_itype type;
	unsigned int vmux;
	u32 gpio0, gpio1, gpio2, gpio3;
};

typedef enum {
	CX25821_UNDEFINED = 0,
	CX25821_RAW,
	CX25821_264
} port_t;

struct cx25821_board {
	char *name;
	port_t porta, portb, portc;
	unsigned int tuner_type;
	unsigned int radio_type;
	unsigned char tuner_addr;
	unsigned char radio_addr;

	u32 clk_freq;
	struct cx25821_input input[2];
};

struct cx25821_subid {
	u16 subvendor;
	u16 subdevice;
	u32 card;
};

struct cx25821_i2c {
	struct cx25821_dev *dev;

	int nr;

	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	struct i2c_algo_bit_data i2c_algo;
	struct i2c_client i2c_client;
	u32 i2c_rc;

	/* cx25821 registers used for raw addess */
	u32 i2c_period;
	u32 reg_ctrl;
	u32 reg_stat;
	u32 reg_addr;
	u32 reg_rdata;
	u32 reg_wdata;
};

struct cx25821_dmaqueue {
	struct list_head active;
	struct list_head queued;
	struct timer_list timeout;
	struct btcx_riscmem stopper;
	u32 count;
};

struct cx25821_data {
	struct cx25821_dev *dev;
	struct sram_channel *channel;
};

struct cx25821_channel {
	struct v4l2_prio_state prio;

	int ctl_bright;
	int ctl_contrast;
	int ctl_hue;
	int ctl_saturation;
	struct cx25821_data timeout_data;

	struct video_device *video_dev;
	struct cx25821_dmaqueue vidq;

	struct sram_channel *sram_channels;

	struct mutex lock;
	int resources;

	int pixel_formats;
	int use_cif_resolution;
	int cif_width;
};

struct cx25821_dev {
	struct list_head devlist;
	atomic_t refcount;
	struct v4l2_device v4l2_dev;

	/* pci stuff */
	struct pci_dev *pci;
	unsigned char pci_rev, pci_lat;
	int pci_bus, pci_slot;
	u32 base_io_addr;
	u32 __iomem *lmmio;
	u8 __iomem *bmmio;
	int pci_irqmask;
	int hwrevision;

	u32 clk_freq;

	/* I2C adapters: Master 1 & 2 (External) & Master 3 (Internal only) */
	struct cx25821_i2c i2c_bus[3];

	int nr;
	struct mutex lock;

	struct cx25821_channel channels[MAX_VID_CHANNEL_NUM];

	/* board details */
	unsigned int board;
	char name[32];

	/* Analog video */
	u32 resources;
	unsigned int input;
	u32 tvaudio;
	v4l2_std_id tvnorm;
	unsigned int tuner_type;
	unsigned char tuner_addr;
	unsigned int radio_type;
	unsigned char radio_addr;
	unsigned int has_radio;
	unsigned int videc_type;
	unsigned char videc_addr;
	unsigned short _max_num_decoders;

	/* Analog Audio Upstream */
	int _audio_is_running;
	int _audiopixel_format;
	int _is_first_audio_frame;
	int _audiofile_status;
	int _audio_lines_count;
	int _audioframe_count;
	int _audio_upstream_channel_select;
	int _last_index_irq;    /* The last interrupt index processed. */

	__le32 *_risc_audio_jmp_addr;
	__le32 *_risc_virt_start_addr;
	__le32 *_risc_virt_addr;
	dma_addr_t _risc_phys_addr;
	dma_addr_t _risc_phys_start_addr;

	unsigned int _audiorisc_size;
	unsigned int _audiodata_buf_size;
	__le32 *_audiodata_buf_virt_addr;
	dma_addr_t _audiodata_buf_phys_addr;
	char *_audiofilename;

	/* V4l */
	u32 freq;
	struct video_device *vbi_dev;
	struct video_device *radio_dev;
	struct video_device *ioctl_dev;

	spinlock_t slock;

	/* Video Upstream */
	int _line_size;
	int _prog_cnt;
	int _pixel_format;
	int _is_first_frame;
	int _is_running;
	int _file_status;
	int _lines_count;
	int _frame_count;
	int _channel_upstream_select;
	unsigned int _risc_size;

	__le32 *_dma_virt_start_addr;
	__le32 *_dma_virt_addr;
	dma_addr_t _dma_phys_addr;
	dma_addr_t _dma_phys_start_addr;

	unsigned int _data_buf_size;
	__le32 *_data_buf_virt_addr;
	dma_addr_t _data_buf_phys_addr;
	char *_filename;
	char *_defaultname;

	int _line_size_ch2;
	int _prog_cnt_ch2;
	int _pixel_format_ch2;
	int _is_first_frame_ch2;
	int _is_running_ch2;
	int _file_status_ch2;
	int _lines_count_ch2;
	int _frame_count_ch2;
	int _channel2_upstream_select;
	unsigned int _risc_size_ch2;

	__le32 *_dma_virt_start_addr_ch2;
	__le32 *_dma_virt_addr_ch2;
	dma_addr_t _dma_phys_addr_ch2;
	dma_addr_t _dma_phys_start_addr_ch2;

	unsigned int _data_buf_size_ch2;
	__le32 *_data_buf_virt_addr_ch2;
	dma_addr_t _data_buf_phys_addr_ch2;
	char *_filename_ch2;
	char *_defaultname_ch2;

	/* MPEG Encoder ONLY settings */
	u32 cx23417_mailbox;
	struct cx2341x_mpeg_params mpeg_params;
	struct video_device *v4l_device;
	atomic_t v4l_reader_count;
	struct cx25821_tvnorm encodernorm;

	u32 upstream_riscbuf_size;
	u32 upstream_databuf_size;
	u32 upstream_riscbuf_size_ch2;
	u32 upstream_databuf_size_ch2;
	u32 audio_upstream_riscbuf_size;
	u32 audio_upstream_databuf_size;
	int _isNTSC;
	int _frame_index;
	int _audioframe_index;
	struct workqueue_struct *_irq_queues;
	struct work_struct _irq_work_entry;
	struct workqueue_struct *_irq_queues_ch2;
	struct work_struct _irq_work_entry_ch2;
	struct workqueue_struct *_irq_audio_queues;
	struct work_struct _audio_work_entry;
	char *input_filename;
	char *input_filename_ch2;
	int _frame_index_ch2;
	int _isNTSC_ch2;
	char *vid_stdname_ch2;
	int pixel_format_ch2;
	int channel_select_ch2;
	int command_ch2;
	char *input_audiofilename;
	char *vid_stdname;
	int pixel_format;
	int channel_select;
	int command;
	int channel_opened;
};

struct upstream_user_struct {
	char *input_filename;
	char *vid_stdname;
	int pixel_format;
	int channel_select;
	int command;
};

struct downstream_user_struct {
	char *vid_stdname;
	int pixel_format;
	int cif_resolution_enable;
	int cif_width;
	int decoder_select;
	int command;
	int reg_address;
	int reg_data;
};

extern struct upstream_user_struct *up_data;

static inline struct cx25821_dev *get_cx25821(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct cx25821_dev, v4l2_dev);
}

#define cx25821_call_all(dev, o, f, args...) \
	v4l2_device_call_all(&dev->v4l2_dev, 0, o, f, ##args)

extern struct list_head cx25821_devlist;
extern struct mutex cx25821_devlist_mutex;

extern struct cx25821_board cx25821_boards[];
extern struct cx25821_subid cx25821_subids[];

#define SRAM_CH00  0		/* Video A */
#define SRAM_CH01  1		/* Video B */
#define SRAM_CH02  2		/* Video C */
#define SRAM_CH03  3		/* Video D */
#define SRAM_CH04  4		/* Video E */
#define SRAM_CH05  5		/* Video F */
#define SRAM_CH06  6		/* Video G */
#define SRAM_CH07  7		/* Video H */

#define SRAM_CH08  8		/* Audio A */
#define SRAM_CH09  9		/* Video Upstream I */
#define SRAM_CH10  10		/* Video Upstream J */
#define SRAM_CH11  11		/* Audio Upstream AUD_CHANNEL_B */

#define VID_UPSTREAM_SRAM_CHANNEL_I     SRAM_CH09
#define VID_UPSTREAM_SRAM_CHANNEL_J     SRAM_CH10
#define AUDIO_UPSTREAM_SRAM_CHANNEL_B   SRAM_CH11
#define VIDEO_IOCTL_CH  11

struct sram_channel {
	char *name;
	u32 i;
	u32 cmds_start;
	u32 ctrl_start;
	u32 cdt;
	u32 fifo_start;
	u32 fifo_size;
	u32 ptr1_reg;
	u32 ptr2_reg;
	u32 cnt1_reg;
	u32 cnt2_reg;
	u32 int_msk;
	u32 int_stat;
	u32 int_mstat;
	u32 dma_ctl;
	u32 gpcnt_ctl;
	u32 gpcnt;
	u32 aud_length;
	u32 aud_cfg;
	u32 fld_aud_fifo_en;
	u32 fld_aud_risc_en;

	/* For Upstream Video */
	u32 vid_fmt_ctl;
	u32 vid_active_ctl1;
	u32 vid_active_ctl2;
	u32 vid_cdt_size;

	u32 vip_ctl;
	u32 pix_frmt;
	u32 jumponly;
	u32 irq_bit;
};
extern struct sram_channel cx25821_sram_channels[];

#define STATUS_SUCCESS         0
#define STATUS_UNSUCCESSFUL    -1

#define cx_read(reg)             readl(dev->lmmio + ((reg)>>2))
#define cx_write(reg, value)     writel((value), dev->lmmio + ((reg)>>2))

#define cx_andor(reg, mask, value) \
	writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
	((value) & (mask)), dev->lmmio+((reg)>>2))

#define cx_set(reg, bit)          cx_andor((reg), (bit), (bit))
#define cx_clear(reg, bit)        cx_andor((reg), (bit), 0)

#define Set_GPIO_Bit(Bit)                       (1 << Bit)
#define Clear_GPIO_Bit(Bit)                     (~(1 << Bit))

#define CX25821_ERR(fmt, args...)			\
	pr_err("(%d): " fmt, dev->board, ##args)
#define CX25821_WARN(fmt, args...)			\
	pr_warn("(%d): " fmt, dev->board, ##args)
#define CX25821_INFO(fmt, args...)			\
	pr_info("(%d): " fmt, dev->board, ##args)

extern int cx25821_i2c_register(struct cx25821_i2c *bus);
extern void cx25821_card_setup(struct cx25821_dev *dev);
extern int cx25821_ir_init(struct cx25821_dev *dev);
extern int cx25821_i2c_read(struct cx25821_i2c *bus, u16 reg_addr, int *value);
extern int cx25821_i2c_write(struct cx25821_i2c *bus, u16 reg_addr, int value);
extern int cx25821_i2c_unregister(struct cx25821_i2c *bus);
extern void cx25821_gpio_init(struct cx25821_dev *dev);
extern void cx25821_set_gpiopin_direction(struct cx25821_dev *dev,
					  int pin_number, int pin_logic_value);

extern int medusa_video_init(struct cx25821_dev *dev);
extern int medusa_set_videostandard(struct cx25821_dev *dev);
extern void medusa_set_resolution(struct cx25821_dev *dev, int width,
				  int decoder_select);
extern int medusa_set_brightness(struct cx25821_dev *dev, int brightness,
				 int decoder);
extern int medusa_set_contrast(struct cx25821_dev *dev, int contrast,
			       int decoder);
extern int medusa_set_hue(struct cx25821_dev *dev, int hue, int decoder);
extern int medusa_set_saturation(struct cx25821_dev *dev, int saturation,
				 int decoder);

extern int cx25821_sram_channel_setup(struct cx25821_dev *dev,
				      struct sram_channel *ch, unsigned int bpl,
				      u32 risc);

extern int cx25821_risc_buffer(struct pci_dev *pci, struct btcx_riscmem *risc,
			       struct scatterlist *sglist,
			       unsigned int top_offset,
			       unsigned int bottom_offset,
			       unsigned int bpl,
			       unsigned int padding, unsigned int lines);
extern int cx25821_risc_databuffer_audio(struct pci_dev *pci,
					 struct btcx_riscmem *risc,
					 struct scatterlist *sglist,
					 unsigned int bpl,
					 unsigned int lines, unsigned int lpi);
extern void cx25821_free_buffer(struct videobuf_queue *q,
				struct cx25821_buffer *buf);
extern int cx25821_risc_stopper(struct pci_dev *pci, struct btcx_riscmem *risc,
				u32 reg, u32 mask, u32 value);
extern void cx25821_sram_channel_dump(struct cx25821_dev *dev,
				      struct sram_channel *ch);
extern void cx25821_sram_channel_dump_audio(struct cx25821_dev *dev,
					    struct sram_channel *ch);

extern struct cx25821_dev *cx25821_dev_get(struct pci_dev *pci);
extern void cx25821_print_irqbits(char *name, char *tag, char **strings,
				  int len, u32 bits, u32 mask);
extern void cx25821_dev_unregister(struct cx25821_dev *dev);
extern int cx25821_sram_channel_setup_audio(struct cx25821_dev *dev,
					    struct sram_channel *ch,
					    unsigned int bpl, u32 risc);

extern int cx25821_vidupstream_init_ch1(struct cx25821_dev *dev,
					int channel_select, int pixel_format);
extern int cx25821_vidupstream_init_ch2(struct cx25821_dev *dev,
					int channel_select, int pixel_format);
extern int cx25821_audio_upstream_init(struct cx25821_dev *dev,
				       int channel_select);
extern void cx25821_free_mem_upstream_ch1(struct cx25821_dev *dev);
extern void cx25821_free_mem_upstream_ch2(struct cx25821_dev *dev);
extern void cx25821_free_mem_upstream_audio(struct cx25821_dev *dev);
extern void cx25821_start_upstream_video_ch1(struct cx25821_dev *dev,
					     struct upstream_user_struct
					     *up_data);
extern void cx25821_start_upstream_video_ch2(struct cx25821_dev *dev,
					     struct upstream_user_struct
					     *up_data);
extern void cx25821_start_upstream_audio(struct cx25821_dev *dev,
					 struct upstream_user_struct *up_data);
extern void cx25821_stop_upstream_video_ch1(struct cx25821_dev *dev);
extern void cx25821_stop_upstream_video_ch2(struct cx25821_dev *dev);
extern void cx25821_stop_upstream_audio(struct cx25821_dev *dev);
extern int cx25821_sram_channel_setup_upstream(struct cx25821_dev *dev,
					       struct sram_channel *ch,
					       unsigned int bpl, u32 risc);
extern void cx25821_set_pixel_format(struct cx25821_dev *dev, int channel,
				     u32 format);
extern void cx25821_videoioctl_unregister(struct cx25821_dev *dev);
extern struct video_device *cx25821_vdev_init(struct cx25821_dev *dev,
					      struct pci_dev *pci,
					      struct video_device *template,
					      char *type);
#endif
