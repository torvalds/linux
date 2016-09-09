/*
 * Copyright (C) 2015 Industrial Research Institute for Automation
 * and Measurements PIAP
 *
 * Written by Krzysztof Ha?asa.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

#define TYPE_MAX_CHANNELS 0x0F
#define TYPE_SECOND_GEN   0x10

struct tw686x_format {
	char *name;
	unsigned int fourcc;
	unsigned int depth;
	unsigned int mode;
};

struct dma_desc {
	dma_addr_t phys;
	void *virt;
	unsigned int size;
};

struct vdma_desc {
	__le32 flags_length;	/* 3 MSBits for flags, 13 LSBits for length */
	__le32 phys;
};

struct tw686x_vb2_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct tw686x_video_channel {
	struct tw686x_dev *dev;

	struct vb2_queue vidq;
	struct list_head vidq_queued;
	struct video_device *device;
	struct dma_desc sg_tables[2];
	struct tw686x_vb2_buf *curr_bufs[2];
	void *alloc_ctx;
	struct vdma_desc *sg_descs[2];

	struct v4l2_ctrl_handler ctrl_handler;
	const struct tw686x_format *format;
	struct mutex vb_mutex;
	spinlock_t qlock;
	v4l2_std_id video_standard;
	unsigned int width, height;
	enum v4l2_field field; /* supported TOP, BOTTOM, SEQ_TB and SEQ_BT */
	unsigned int seq;	       /* video field or frame counter */
	unsigned int ch;
};

/* global device status */
struct tw686x_dev {
	spinlock_t irq_lock;

	struct v4l2_device v4l2_dev;
	struct snd_card *card;	/* sound card */

	unsigned int video_active;	/* active video channel mask */

	char name[32];
	unsigned int type;
	struct pci_dev *pci_dev;
	__u32 __iomem *mmio;

	struct task_struct *video_thread;
	wait_queue_head_t video_thread_wait;
	u32 dma_requests;

	struct tw686x_video_channel video_channels[0];
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

static inline unsigned int is_second_gen(struct tw686x_dev *dev)
{
	/* each channel has its own DMA SG table */
	return dev->type & TYPE_SECOND_GEN;
}

int tw686x_kh_video_irq(struct tw686x_dev *dev);
int tw686x_kh_video_init(struct tw686x_dev *dev);
void tw686x_kh_video_free(struct tw686x_dev *dev);
