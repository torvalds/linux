/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __SOLO6X10_H
#define __SOLO6X10_H

#include <linux/version.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/atomic.h>
#include <linux/videodev2.h>
#include <media/v4l2-dev.h>
#include <media/videobuf-core.h>
#include "registers.h"

#ifndef PCI_VENDOR_ID_SOFTLOGIC
#define PCI_VENDOR_ID_SOFTLOGIC		0x9413
#define PCI_DEVICE_ID_SOLO6010		0x6010
#define PCI_DEVICE_ID_SOLO6110		0x6110
#endif

#ifndef PCI_VENDOR_ID_BLUECHERRY
#define PCI_VENDOR_ID_BLUECHERRY	0x1BB3
/* Neugent Softlogic 6010 based cards */
#define PCI_DEVICE_ID_NEUSOLO_4		0x4304
#define PCI_DEVICE_ID_NEUSOLO_9		0x4309
#define PCI_DEVICE_ID_NEUSOLO_16	0x4310
/* Bluecherry Softlogic 6010 based cards */
#define PCI_DEVICE_ID_BC_SOLO_4		0x4E04
#define PCI_DEVICE_ID_BC_SOLO_9		0x4E09
#define PCI_DEVICE_ID_BC_SOLO_16	0x4E10
/* Bluecherry Softlogic 6110 based cards */
#define PCI_DEVICE_ID_BC_6110_4		0x5304
#define PCI_DEVICE_ID_BC_6110_8		0x5308
#define PCI_DEVICE_ID_BC_6110_16	0x5310
#endif /* Bluecherry */

#define SOLO6X10_NAME			"solo6x10"

#define SOLO_MAX_CHANNELS		16

/* Make sure these two match */
#define SOLO6X10_VERSION		"2.1.0"
#define SOLO6X10_VER_MAJOR		2
#define SOLO6X10_VER_MINOR		0
#define SOLO6X10_VER_SUB		0
#define SOLO6X10_VER_NUM \
	KERNEL_VERSION(SOLO6X10_VER_MAJOR, SOLO6X10_VER_MINOR, SOLO6X10_VER_SUB)

#define FLAGS_6110			1

/*
 * The SOLO6x10 actually has 8 i2c channels, but we only use 2.
 * 0 - Techwell chip(s)
 * 1 - SAA7128
 */
#define SOLO_I2C_ADAPTERS		2
#define SOLO_I2C_TW			0
#define SOLO_I2C_SAA			1

/* DMA Engine setup */
#define SOLO_NR_P2M			4
#define SOLO_NR_P2M_DESC		256
/* MPEG and JPEG share the same interrupt and locks so they must be together
 * in the same dma channel. */
#define SOLO_P2M_DMA_ID_MP4E		0
#define SOLO_P2M_DMA_ID_JPEG		0
#define SOLO_P2M_DMA_ID_MP4D		1
#define SOLO_P2M_DMA_ID_G723D		1
#define SOLO_P2M_DMA_ID_DISP		2
#define SOLO_P2M_DMA_ID_OSG		2
#define SOLO_P2M_DMA_ID_G723E		3
#define SOLO_P2M_DMA_ID_VIN		3

/* Encoder standard modes */
#define SOLO_ENC_MODE_CIF		2
#define SOLO_ENC_MODE_HD1		1
#define SOLO_ENC_MODE_D1		9

#define SOLO_DEFAULT_GOP		30
#define SOLO_DEFAULT_QP			3

/* There is 8MB memory available for solo to buffer MPEG4 frames.
 * This gives us 512 * 16kbyte queues. */
#define SOLO_NR_RING_BUFS		512

#define SOLO_CLOCK_MHZ			108

#ifndef V4L2_BUF_FLAG_MOTION_ON
#define V4L2_BUF_FLAG_MOTION_ON		0x0400
#define V4L2_BUF_FLAG_MOTION_DETECTED	0x0800
#endif
#ifndef V4L2_CID_MOTION_ENABLE
#define PRIVATE_CIDS
#define V4L2_CID_MOTION_ENABLE		(V4L2_CID_PRIVATE_BASE+0)
#define V4L2_CID_MOTION_THRESHOLD	(V4L2_CID_PRIVATE_BASE+1)
#define V4L2_CID_MOTION_TRACE		(V4L2_CID_PRIVATE_BASE+2)
#endif

enum SOLO_I2C_STATE {
	IIC_STATE_IDLE,
	IIC_STATE_START,
	IIC_STATE_READ,
	IIC_STATE_WRITE,
	IIC_STATE_STOP
};

struct p2m_desc {
	u32 ctrl;
	u32 ext;
	u32 ta;
	u32 fa;
};

struct solo_p2m_dev {
	struct mutex		mutex;
	struct completion	completion;
	int			error;
};

#define OSD_TEXT_MAX		30

enum solo_enc_types {
	SOLO_ENC_TYPE_STD,
	SOLO_ENC_TYPE_EXT,
};

struct solo_enc_dev {
	struct solo_dev		*solo_dev;
	/* V4L2 Items */
	struct video_device	*vfd;
	/* General accounting */
	wait_queue_head_t	thread_wait;
	spinlock_t		lock;
	atomic_t		readers;
	u8			ch;
	u8			mode, gop, qp, interlaced, interval;
	u8			reset_gop;
	u8			bw_weight;
	u8			motion_detected;
	u16			motion_thresh;
	u16			width;
	u16			height;
	char			osd_text[OSD_TEXT_MAX + 1];
};

struct solo_enc_buf {
	u8			vop;
	u8			ch;
	enum solo_enc_types	type;
	u32			off;
	u32			size;
	u32			jpeg_off;
	u32			jpeg_size;
	struct timeval		ts;
};

/* The SOLO6x10 PCI Device */
struct solo_dev {
	/* General stuff */
	struct pci_dev		*pdev;
	u8 __iomem		*reg_base;
	int			nr_chans;
	int			nr_ext;
	u32			flags;
	u32			irq_mask;
	u32			motion_mask;
	spinlock_t		reg_io_lock;

	/* tw28xx accounting */
	u8			tw2865, tw2864, tw2815;
	u8			tw28_cnt;

	/* i2c related items */
	struct i2c_adapter	i2c_adap[SOLO_I2C_ADAPTERS];
	enum SOLO_I2C_STATE	i2c_state;
	struct mutex		i2c_mutex;
	int			i2c_id;
	wait_queue_head_t	i2c_wait;
	struct i2c_msg		*i2c_msg;
	unsigned int		i2c_msg_num;
	unsigned int		i2c_msg_ptr;

	/* P2M DMA Engine */
	struct solo_p2m_dev	p2m_dev[SOLO_NR_P2M];

	/* V4L2 Display items */
	struct video_device	*vfd;
	unsigned int		erasing;
	unsigned int		frame_blank;
	u8			cur_disp_ch;
	wait_queue_head_t	disp_thread_wait;

	/* V4L2 Encoder items */
	struct solo_enc_dev	*v4l2_enc[SOLO_MAX_CHANNELS];
	u16			enc_bw_remain;
	/* IDX into hw mp4 encoder */
	u8			enc_idx;
	/* Our software ring of enc buf references */
	u16			enc_wr_idx;
	struct solo_enc_buf	enc_buf[SOLO_NR_RING_BUFS];

	/* Current video settings */
	u32			video_type;
	u16			video_hsize, video_vsize;
	u16			vout_hstart, vout_vstart;
	u16			vin_hstart, vin_vstart;
	u8			fps;

	/* Audio components */
	struct snd_card		*snd_card;
	struct snd_pcm		*snd_pcm;
	atomic_t		snd_users;
	int			g723_hw_idx;
};

static inline u32 solo_reg_read(struct solo_dev *solo_dev, int reg)
{
	unsigned long flags;
	u32 ret;
	u16 val;

	spin_lock_irqsave(&solo_dev->reg_io_lock, flags);

	ret = readl(solo_dev->reg_base + reg);
	rmb();
	pci_read_config_word(solo_dev->pdev, PCI_STATUS, &val);
	rmb();

	spin_unlock_irqrestore(&solo_dev->reg_io_lock, flags);

	return ret;
}

static inline void solo_reg_write(struct solo_dev *solo_dev, int reg, u32 data)
{
	unsigned long flags;
	u16 val;

	spin_lock_irqsave(&solo_dev->reg_io_lock, flags);

	writel(data, solo_dev->reg_base + reg);
	wmb();
	pci_read_config_word(solo_dev->pdev, PCI_STATUS, &val);
	rmb();

	spin_unlock_irqrestore(&solo_dev->reg_io_lock, flags);
}

void solo_irq_on(struct solo_dev *solo_dev, u32 mask);
void solo_irq_off(struct solo_dev *solo_dev, u32 mask);

/* Init/exit routeines for subsystems */
int solo_disp_init(struct solo_dev *solo_dev);
void solo_disp_exit(struct solo_dev *solo_dev);

int solo_gpio_init(struct solo_dev *solo_dev);
void solo_gpio_exit(struct solo_dev *solo_dev);

int solo_i2c_init(struct solo_dev *solo_dev);
void solo_i2c_exit(struct solo_dev *solo_dev);

int solo_p2m_init(struct solo_dev *solo_dev);
void solo_p2m_exit(struct solo_dev *solo_dev);

int solo_v4l2_init(struct solo_dev *solo_dev);
void solo_v4l2_exit(struct solo_dev *solo_dev);

int solo_enc_init(struct solo_dev *solo_dev);
void solo_enc_exit(struct solo_dev *solo_dev);

int solo_enc_v4l2_init(struct solo_dev *solo_dev);
void solo_enc_v4l2_exit(struct solo_dev *solo_dev);

int solo_g723_init(struct solo_dev *solo_dev);
void solo_g723_exit(struct solo_dev *solo_dev);

/* ISR's */
int solo_i2c_isr(struct solo_dev *solo_dev);
void solo_p2m_isr(struct solo_dev *solo_dev, int id);
void solo_p2m_error_isr(struct solo_dev *solo_dev, u32 status);
void solo_enc_v4l2_isr(struct solo_dev *solo_dev);
void solo_g723_isr(struct solo_dev *solo_dev);
void solo_motion_isr(struct solo_dev *solo_dev);
void solo_video_in_isr(struct solo_dev *solo_dev);

/* i2c read/write */
u8 solo_i2c_readbyte(struct solo_dev *solo_dev, int id, u8 addr, u8 off);
void solo_i2c_writebyte(struct solo_dev *solo_dev, int id, u8 addr, u8 off,
			u8 data);

/* P2M DMA */
int solo_p2m_dma_t(struct solo_dev *solo_dev, u8 id, int wr,
		   dma_addr_t dma_addr, u32 ext_addr, u32 size);
int solo_p2m_dma(struct solo_dev *solo_dev, u8 id, int wr,
		 void *sys_addr, u32 ext_addr, u32 size);
int solo_p2m_dma_sg(struct solo_dev *solo_dev, u8 id,
		    struct p2m_desc *pdesc, int wr,
		    struct scatterlist *sglist, u32 sg_off,
		    u32 ext_addr, u32 size);
void solo_p2m_push_desc(struct p2m_desc *desc, int wr, dma_addr_t dma_addr,
			u32 ext_addr, u32 size, int repeat, u32 ext_size);
int solo_p2m_dma_desc(struct solo_dev *solo_dev, u8 id,
		      struct p2m_desc *desc, int desc_count);

/* Set the threshold for motion detection */
void solo_set_motion_threshold(struct solo_dev *solo_dev, u8 ch, u16 val);
#define SOLO_DEF_MOT_THRESH		0x0300

/* Write text on OSD */
int solo_osd_print(struct solo_enc_dev *solo_enc);

#endif /* __SOLO6X10_H */
