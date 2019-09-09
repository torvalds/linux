/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *  Based on Steven Toth <stoth@linuxtv.org> cx23885 driver
 */

#ifndef CX25821_H_
#define CX25821_H_

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/kdev_t.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-sg.h>

#include "cx25821-reg.h"
#include "cx25821-medusa-reg.h"
#include "cx25821-sram.h"
#include "cx25821-audio.h"

#include <linux/version.h>
#include <linux/mutex.h>

#define UNSET (-1U)
#define NO_SYNC_LINE (-1U)

#define CX25821_MAXBOARDS 2

#define LINE_SIZE_D1    1440

/* Number of decoders and encoders */
#define MAX_DECODERS            8
#define MAX_ENCODERS            2
#define QUAD_DECODERS           4
#define MAX_CAMERAS             16

/* Max number of inputs by card */
#define MAX_CX25821_INPUT     8
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

#define BUFFER_TIMEOUT     (HZ)	/* 0.5 seconds */

#define UNKNOWN_BOARD        0
#define CX25821_BOARD        1

/* Currently supported by the driver */
#define CX25821_NORMS (\
	V4L2_STD_NTSC_M |  V4L2_STD_NTSC_M_JP | V4L2_STD_NTSC_M_KR | \
	V4L2_STD_PAL_BG |  V4L2_STD_PAL_DK    |  V4L2_STD_PAL_I    | \
	V4L2_STD_PAL_M  |  V4L2_STD_PAL_N     |  V4L2_STD_PAL_H    | \
	V4L2_STD_PAL_Nc)

#define CX25821_BOARD_CONEXANT_ATHENA10 1
#define MAX_VID_CHANNEL_NUM     12

/*
 * Maximum capture-only channels. This can go away once video/audio output
 * is fully supported in this driver.
 */
#define MAX_VID_CAP_CHANNEL_NUM     10

#define VID_CHANNEL_NUM 8

struct cx25821_fmt {
	char *name;
	u32 fourcc;		/* v4l2 format id */
	int depth;
	int flags;
	u32 cxformat;
};

struct cx25821_tvnorm {
	char *name;
	v4l2_std_id id;
	u32 cxiformat;
	u32 cxoformat;
};

enum cx25821_src_sel_type {
	CX25821_SRC_SEL_EXT_656_VIDEO = 0,
	CX25821_SRC_SEL_PARALLEL_MPEG_VIDEO
};

struct cx25821_riscmem {
	unsigned int   size;
	__le32         *cpu;
	__le32         *jmp;
	dma_addr_t     dma;
};

/* buffer for one video frame */
struct cx25821_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head queue;

	/* cx25821 specific */
	unsigned int bpl;
	struct cx25821_riscmem risc;
	const struct cx25821_fmt *fmt;
};

enum port {
	CX25821_UNDEFINED = 0,
	CX25821_RAW,
	CX25821_264
};

struct cx25821_board {
	const char *name;
	enum port porta;
	enum port portb;
	enum port portc;

	u32 clk_freq;
};

struct cx25821_i2c {
	struct cx25821_dev *dev;

	int nr;

	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;
	u32 i2c_rc;

	/* cx25821 registers used for raw address */
	u32 i2c_period;
	u32 reg_ctrl;
	u32 reg_stat;
	u32 reg_addr;
	u32 reg_rdata;
	u32 reg_wdata;
};

struct cx25821_dmaqueue {
	struct list_head active;
	u32 count;
};

struct cx25821_dev;

struct cx25821_channel;

struct cx25821_video_out_data {
	struct cx25821_channel *chan;
	int _line_size;
	int _prog_cnt;
	int _pixel_format;
	int _is_first_frame;
	int _is_running;
	int _file_status;
	int _lines_count;
	int _frame_count;
	unsigned int _risc_size;

	__le32 *_dma_virt_start_addr;
	__le32 *_dma_virt_addr;
	dma_addr_t _dma_phys_addr;
	dma_addr_t _dma_phys_start_addr;

	unsigned int _data_buf_size;
	__le32 *_data_buf_virt_addr;
	dma_addr_t _data_buf_phys_addr;

	u32 upstream_riscbuf_size;
	u32 upstream_databuf_size;
	int is_60hz;
	int _frame_index;
	int cur_frame_index;
	int curpos;
	wait_queue_head_t waitq;
};

struct cx25821_channel {
	unsigned id;
	struct cx25821_dev *dev;

	struct v4l2_ctrl_handler hdl;

	struct video_device vdev;
	struct cx25821_dmaqueue dma_vidq;
	struct vb2_queue vidq;

	const struct sram_channel *sram_channels;

	const struct cx25821_fmt *fmt;
	unsigned field;
	unsigned int width, height;
	int pixel_formats;
	int use_cif_resolution;
	int cif_width;

	/* video output data for the video output channel */
	struct cx25821_video_out_data *out;
};

struct snd_card;

struct cx25821_dev {
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
	/* used by cx25821-alsa */
	struct snd_card *card;

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
	unsigned int input;
	v4l2_std_id tvnorm;
	unsigned short _max_num_decoders;

	/* Analog Audio Upstream */
	int _audio_is_running;
	int _audiopixel_format;
	int _is_first_audio_frame;
	int _audiofile_status;
	int _audio_lines_count;
	int _audioframe_count;
	int _audio_upstream_channel;
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
	u32 audio_upstream_riscbuf_size;
	u32 audio_upstream_databuf_size;
	int _audioframe_index;
	struct work_struct _audio_work_entry;
	char *input_audiofilename;

	/* V4l */
	spinlock_t slock;

	/* Video Upstream */
	struct cx25821_video_out_data vid_out_data[2];
};

static inline struct cx25821_dev *get_cx25821(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct cx25821_dev, v4l2_dev);
}

extern struct cx25821_board cx25821_boards[];

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

extern const struct sram_channel cx25821_sram_channels[];

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
				      const struct sram_channel *ch, unsigned int bpl,
				      u32 risc);

extern int cx25821_riscmem_alloc(struct pci_dev *pci,
				 struct cx25821_riscmem *risc,
				 unsigned int size);
extern int cx25821_risc_buffer(struct pci_dev *pci, struct cx25821_riscmem *risc,
			       struct scatterlist *sglist,
			       unsigned int top_offset,
			       unsigned int bottom_offset,
			       unsigned int bpl,
			       unsigned int padding, unsigned int lines);
extern int cx25821_risc_databuffer_audio(struct pci_dev *pci,
					 struct cx25821_riscmem *risc,
					 struct scatterlist *sglist,
					 unsigned int bpl,
					 unsigned int lines, unsigned int lpi);
extern void cx25821_free_buffer(struct cx25821_dev *dev,
				struct cx25821_buffer *buf);
extern void cx25821_sram_channel_dump(struct cx25821_dev *dev,
				      const struct sram_channel *ch);
extern void cx25821_sram_channel_dump_audio(struct cx25821_dev *dev,
					    const struct sram_channel *ch);

extern struct cx25821_dev *cx25821_dev_get(struct pci_dev *pci);
extern void cx25821_print_irqbits(char *name, char *tag, char **strings,
				  int len, u32 bits, u32 mask);
extern void cx25821_dev_unregister(struct cx25821_dev *dev);
extern int cx25821_sram_channel_setup_audio(struct cx25821_dev *dev,
					    const struct sram_channel *ch,
					    unsigned int bpl, u32 risc);

extern void cx25821_set_pixel_format(struct cx25821_dev *dev, int channel,
				     u32 format);

#endif
