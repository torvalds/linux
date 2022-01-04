/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 VanguardiaSur - www.vanguardiasur.com.ar
 *
 * Copyright (C) 2015 Industrial Research Institute for Automation
 * and Measurements PIAP
 * Written by Krzysztof Ha?asa
 */

#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <sound/pcm.h>

#include "tw686x-regs.h"

#define TYPE_MAX_CHANNELS	0x0f
#define TYPE_SECOND_GEN		0x10
#define TW686X_DEF_PHASE_REF	0x1518

#define TW686X_AUDIO_PAGE_MAX		16
#define TW686X_AUDIO_PERIODS_MIN	2
#define TW686X_AUDIO_PERIODS_MAX	TW686X_AUDIO_PAGE_MAX

#define TW686X_DMA_MODE_MEMCPY		0
#define TW686X_DMA_MODE_CONTIG		1
#define TW686X_DMA_MODE_SG		2

struct tw686x_format {
	char *name;
	unsigned int fourcc;
	unsigned int depth;
	unsigned int mode;
};

struct tw686x_dma_desc {
	dma_addr_t phys;
	void *virt;
	unsigned int size;
};

struct tw686x_sg_desc {
	/* 3 MSBits for flags, 13 LSBits for length */
	__le32 flags_length;
	__le32 phys;
};

struct tw686x_audio_buf {
	dma_addr_t dma;
	void *virt;
	struct list_head list;
};

struct tw686x_v4l2_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct tw686x_audio_channel {
	struct tw686x_dev *dev;
	struct snd_pcm_substream *ss;
	unsigned int ch;
	struct tw686x_audio_buf *curr_bufs[2];
	struct tw686x_dma_desc dma_descs[2];
	dma_addr_t ptr;

	struct tw686x_audio_buf buf[TW686X_AUDIO_PAGE_MAX];
	struct list_head buf_list;
	spinlock_t lock;
};

struct tw686x_video_channel {
	struct tw686x_dev *dev;

	struct vb2_queue vidq;
	struct list_head vidq_queued;
	struct video_device *device;
	struct tw686x_v4l2_buf *curr_bufs[2];
	struct tw686x_dma_desc dma_descs[2];
	struct tw686x_sg_desc *sg_descs[2];

	struct v4l2_ctrl_handler ctrl_handler;
	const struct tw686x_format *format;
	struct mutex vb_mutex;
	spinlock_t qlock;
	v4l2_std_id video_standard;
	unsigned int width, height;
	unsigned int h_halve, v_halve;
	unsigned int ch;
	unsigned int num;
	unsigned int fps;
	unsigned int input;
	unsigned int sequence;
	unsigned int pb;
	bool no_signal;
};

struct tw686x_dma_ops {
	int (*setup)(struct tw686x_dev *dev);
	int (*alloc)(struct tw686x_video_channel *vc, unsigned int pb);
	void (*free)(struct tw686x_video_channel *vc, unsigned int pb);
	void (*buf_refill)(struct tw686x_video_channel *vc, unsigned int pb);
	const struct vb2_mem_ops *mem_ops;
	enum v4l2_field field;
	u32 hw_dma_mode;
};

/* struct tw686x_dev - global device status */
struct tw686x_dev {
	/*
	 * spinlock controlling access to the shared device registers
	 * (DMA enable/disable)
	 */
	spinlock_t lock;

	struct v4l2_device v4l2_dev;
	struct snd_card *snd_card;

	char name[32];
	unsigned int type;
	unsigned int dma_mode;
	struct pci_dev *pci_dev;
	__u32 __iomem *mmio;

	const struct tw686x_dma_ops *dma_ops;
	struct tw686x_video_channel *video_channels;
	struct tw686x_audio_channel *audio_channels;

	/* Per-device audio parameters */
	int audio_rate;
	int period_size;
	int audio_enabled;

	struct timer_list dma_delay_timer;
	u32 pending_dma_en; /* must be protected by lock */
	u32 pending_dma_cmd; /* must be protected by lock */
};

static inline uint32_t reg_read(struct tw686x_dev *dev, unsigned int reg)
{
	return readl(dev->mmio + reg);
}

static inline void reg_write(struct tw686x_dev *dev, unsigned int reg,
			     uint32_t value)
{
	writel(value, dev->mmio + reg);
}

static inline unsigned int max_channels(struct tw686x_dev *dev)
{
	return dev->type & TYPE_MAX_CHANNELS; /* 4 or 8 channels */
}

static inline unsigned is_second_gen(struct tw686x_dev *dev)
{
	/* each channel has its own DMA SG table */
	return dev->type & TYPE_SECOND_GEN;
}

void tw686x_enable_channel(struct tw686x_dev *dev, unsigned int channel);
void tw686x_disable_channel(struct tw686x_dev *dev, unsigned int channel);

int tw686x_video_init(struct tw686x_dev *dev);
void tw686x_video_free(struct tw686x_dev *dev);
void tw686x_video_irq(struct tw686x_dev *dev, unsigned long requests,
		      unsigned int pb_status, unsigned int fifo_status,
		      unsigned int *reset_ch);

int tw686x_audio_init(struct tw686x_dev *dev);
void tw686x_audio_free(struct tw686x_dev *dev);
void tw686x_audio_irq(struct tw686x_dev *dev, unsigned long requests,
		      unsigned int pb_status);
