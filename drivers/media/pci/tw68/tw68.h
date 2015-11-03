/*
 *  tw68 driver common header file
 *
 *  Much of this code is derived from the cx88 and sa7134 drivers, which
 *  were in turn derived from the bt87x driver.  The original work was by
 *  Gerd Knorr; more recently the code was enhanced by Mauro Carvalho Chehab,
 *  Hans Verkuil, Andy Walls and many others.  Their work is gratefully
 *  acknowledged.  Full credit goes to them - any problems within this code
 *  are mine.
 *
 *  Copyright (C) 2009  William M. Brack
 *
 *  Refactored and updated to the latest v4l core frameworks:
 *
 *  Copyright (C) 2014 Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/notifier.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/io.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-dma-sg.h>

#include "tw68-reg.h"

#define	UNSET	(-1U)

#define TW68_NORMS ( \
	V4L2_STD_NTSC    | V4L2_STD_PAL       | V4L2_STD_SECAM    | \
	V4L2_STD_PAL_M   | V4L2_STD_PAL_Nc    | V4L2_STD_PAL_60)

#define	TW68_VID_INTS	(TW68_FFERR | TW68_PABORT | TW68_DMAPERR | \
			 TW68_FFOF   | TW68_DMAPI)
/* TW6800 chips have trouble with these, so we don't set them for that chip */
#define	TW68_VID_INTSX	(TW68_FDMIS | TW68_HLOCK | TW68_VLOCK)

#define	TW68_I2C_INTS	(TW68_SBERR | TW68_SBDONE | TW68_SBERR2  | \
			 TW68_SBDONE2)

enum tw68_decoder_type {
	TW6800,
	TW6801,
	TW6804,
	TWXXXX,
};

/* ----------------------------------------------------------- */
/* static data                                                 */

struct tw68_tvnorm {
	char		*name;
	v4l2_std_id	id;

	/* video decoder */
	u32	sync_control;
	u32	luma_control;
	u32	chroma_ctrl1;
	u32	chroma_gain;
	u32	chroma_ctrl2;
	u32	vgate_misc;

	/* video scaler */
	u32	h_delay;
	u32	h_delay0;	/* for TW6800 */
	u32	h_start;
	u32	h_stop;
	u32	v_delay;
	u32	video_v_start;
	u32	video_v_stop;
	u32	vbi_v_start_0;
	u32	vbi_v_stop_0;
	u32	vbi_v_start_1;

	/* Techwell specific */
	u32	format;
};

struct tw68_format {
	char	*name;
	u32	fourcc;
	u32	depth;
	u32	twformat;
};

/* ----------------------------------------------------------- */
/* card configuration					  */

#define TW68_BOARD_NOAUTO		UNSET
#define TW68_BOARD_UNKNOWN		0
#define	TW68_BOARD_GENERIC_6802		1

#define	TW68_MAXBOARDS			16
#define	TW68_INPUT_MAX			4

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define	BUFFER_TIMEOUT	msecs_to_jiffies(500)	/* 0.5 seconds */

struct tw68_dev;	/* forward delclaration */

/* buffer for one video/vbi/ts frame */
struct tw68_buf {
	struct vb2_buffer vb;
	struct list_head list;

	unsigned int   size;
	__le32         *cpu;
	__le32         *jmp;
	dma_addr_t     dma;
};

struct tw68_fmt {
	char			*name;
	u32			fourcc;	/* v4l2 format id */
	int			depth;
	int			flags;
	u32			twformat;
};

/* global device status */
struct tw68_dev {
	struct mutex		lock;
	spinlock_t		slock;
	u16			instance;
	struct v4l2_device	v4l2_dev;

	/* various device info */
	enum tw68_decoder_type	vdecoder;
	struct video_device	vdev;
	struct v4l2_ctrl_handler hdl;

	/* pci i/o */
	char			*name;
	struct pci_dev		*pci;
	unsigned char		pci_rev, pci_lat;
	u32			__iomem *lmmio;
	u8			__iomem *bmmio;
	u32			pci_irqmask;
	/* The irq mask to be used will depend upon the chip type */
	u32			board_virqmask;

	/* video capture */
	const struct tw68_format *fmt;
	unsigned		width, height;
	unsigned		seqnr;
	unsigned		field;
	struct vb2_queue	vidq;
	struct list_head	active;
	void			*alloc_ctx;

	/* various v4l controls */
	const struct tw68_tvnorm *tvnorm;	/* video */

	int			input;
};

/* ----------------------------------------------------------- */

#define tw_readl(reg)		readl(dev->lmmio + ((reg) >> 2))
#define	tw_readb(reg)		readb(dev->bmmio + (reg))
#define tw_writel(reg, value)	writel((value), dev->lmmio + ((reg) >> 2))
#define	tw_writeb(reg, value)	writeb((value), dev->bmmio + (reg))

#define tw_andorl(reg, mask, value) \
		writel((readl(dev->lmmio+((reg)>>2)) & ~(mask)) |\
		((value) & (mask)), dev->lmmio+((reg)>>2))
#define	tw_andorb(reg, mask, value) \
		writeb((readb(dev->bmmio + (reg)) & ~(mask)) |\
		((value) & (mask)), dev->bmmio+(reg))
#define tw_setl(reg, bit)	tw_andorl((reg), (bit), (bit))
#define	tw_setb(reg, bit)	tw_andorb((reg), (bit), (bit))
#define	tw_clearl(reg, bit)	\
		writel((readl(dev->lmmio + ((reg) >> 2)) & ~(bit)), \
		dev->lmmio + ((reg) >> 2))
#define	tw_clearb(reg, bit)	\
		writeb((readb(dev->bmmio+(reg)) & ~(bit)), \
		dev->bmmio + (reg))

#define tw_wait(us) { udelay(us); }

/* ----------------------------------------------------------- */
/* tw68-video.c                                                */

void tw68_set_tvnorm_hw(struct tw68_dev *dev);

int tw68_video_init1(struct tw68_dev *dev);
int tw68_video_init2(struct tw68_dev *dev, int video_nr);
void tw68_irq_video_done(struct tw68_dev *dev, unsigned long status);
int tw68_video_start_dma(struct tw68_dev *dev, struct tw68_buf *buf);

/* ----------------------------------------------------------- */
/* tw68-risc.c                                                 */

int tw68_risc_buffer(struct pci_dev *pci, struct tw68_buf *buf,
	struct scatterlist *sglist, unsigned int top_offset,
	unsigned int bottom_offset, unsigned int bpl,
	unsigned int padding, unsigned int lines);
