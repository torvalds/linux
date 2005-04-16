/* 
    planb - PlanB frame grabber driver

    PlanB is used in the 7x00/8x00 series of PowerMacintosh
    Computers as video input DMA controller.

    Copyright (C) 1998 Michel Lanners (mlan@cpu.lu)

    Based largely on the bttv driver by Ralph Metzler (rjkm@thp.uni-koeln.de)

    Additional debugging and coding by Takashi Oe (toe@unlserve.unl.edu)


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

/* $Id: planb.h,v 1.13 1999/05/03 19:28:56 mlan Exp $ */

#ifndef _PLANB_H_
#define _PLANB_H_

#ifdef __KERNEL__
#include <asm/dbdma.h>
#include "saa7196.h"
#endif /* __KERNEL__ */

#define PLANB_DEVICE_NAME	"Apple PlanB Video-In"
#define PLANB_REV		"1.0"

#ifdef __KERNEL__
//#define PLANB_GSCANLINE	/* use this if apps have the notion of */
				/* grab buffer scanline */
/* This should be safe for both PAL and NTSC */
#define PLANB_MAXPIXELS 768
#define PLANB_MAXLINES 576
#define PLANB_NTSC_MAXLINES 480

/* Uncomment your preferred norm ;-) */
#define PLANB_DEF_NORM VIDEO_MODE_PAL
//#define PLANB_DEF_NORM VIDEO_MODE_NTSC
//#define PLANB_DEF_NORM VIDEO_MODE_SECAM

/* fields settings */
#define PLANB_GRAY	0x1	/*  8-bit mono? */
#define PLANB_COLOUR15	0x2	/* 16-bit mode */
#define PLANB_COLOUR32	0x4	/* 32-bit mode */
#define PLANB_CLIPMASK	0x8	/* hardware clipmasking */

/* misc. flags for PlanB DMA operation */
#define	CH_SYNC		0x1	/* synchronize channels (set by ch1;
				   cleared by ch2) */
#define FIELD_SYNC	0x2     /* used for the start of each field
				   (0 -> 1 -> 0 for ch1; 0 -> 1 for ch2) */
#define EVEN_FIELD	0x0	/* even field is detected if unset */
#define DMA_ABORT	0x2	/* error or just out of sync if set */
#define ODD_FIELD	0x4	/* odd field is detected if set */

/* for capture operations */
#define MAX_GBUFFERS	2
/* note PLANB_MAX_FBUF must be divisible by PAGE_SIZE */
#ifdef PLANB_GSCANLINE
#define PLANB_MAX_FBUF	0x240000	/* 576 * 1024 * 4 */
#define TAB_FACTOR	(1)
#else
#define PLANB_MAX_FBUF	0x1b0000	/* 576 * 768 * 4 */
#define TAB_FACTOR	(2)
#endif
#endif /* __KERNEL__ */

struct planb_saa_regs {
	unsigned char addr;
	unsigned char val;
};

struct planb_stat_regs {
	unsigned int ch1_stat;
	unsigned int ch2_stat;
	unsigned char saa_stat0;
	unsigned char saa_stat1;
};

struct planb_any_regs {
	unsigned int offset;
	unsigned int bytes;
	unsigned char data[128];
};

/* planb private ioctls */
#define PLANBIOCGSAAREGS	_IOWR('v', BASE_VIDIOCPRIVATE, struct planb_saa_regs)	/* Read a saa7196 reg value */
#define PLANBIOCSSAAREGS	_IOW('v', BASE_VIDIOCPRIVATE + 1, struct planb_saa_regs)	/* Set a saa7196 reg value */
#define PLANBIOCGSTAT		_IOR('v', BASE_VIDIOCPRIVATE + 2, struct planb_stat_regs)	/* Read planb status */
#define PLANB_TV_MODE		1
#define PLANB_VTR_MODE		2
#define PLANBIOCGMODE		_IOR('v', BASE_VIDIOCPRIVATE + 3, int)	/* Get TV/VTR mode */
#define PLANBIOCSMODE		_IOW('v', BASE_VIDIOCPRIVATE + 4, int)	/* Set TV/VTR mode */

#ifdef PLANB_GSCANLINE
#define PLANBG_GRAB_BPL		_IOR('v', BASE_VIDIOCPRIVATE + 5, int)	/* # of bytes per scanline in grab buffer */
#endif

/* call wake_up_interruptible() with appropriate actions */
#define PLANB_INTR_DEBUG	_IOW('v', BASE_VIDIOCPRIVATE + 20, int)
/* investigate which reg does what */
#define PLANB_INV_REGS		_IOWR('v', BASE_VIDIOCPRIVATE + 21, struct planb_any_regs)

#ifdef __KERNEL__

/* Potentially useful macros */
#define PLANB_SET(x)	((x) << 16 | (x))
#define PLANB_CLR(x)	((x) << 16)

/* This represents the physical register layout */
struct planb_registers {
	volatile struct dbdma_regs	ch1;		/* 0x00: video in */
	volatile unsigned int		even;		/* 0x40: even field setting */
	volatile unsigned int		odd;		/* 0x44; odd field setting */
	unsigned int			pad1[14];	/* empty? */
	volatile struct dbdma_regs	ch2;		/* 0x80: clipmask out */
	unsigned int			pad2[16];	/* 0xc0: empty? */
	volatile unsigned int		reg3;		/* 0x100: ???? */
	volatile unsigned int		intr_stat;	/* 0x104: irq status */
#define PLANB_CLR_IRQ		0x00		/* clear Plan B interrupt */
#define PLANB_GEN_IRQ		0x01		/* assert Plan B interrupt */
#define PLANB_FRM_IRQ		0x0100		/* end of frame */
	unsigned int			pad3[1];	/* empty? */
	volatile unsigned int		reg5;		/* 0x10c: ??? */
	unsigned int			pad4[60];	/* empty? */
	volatile unsigned char		saa_addr;	/* 0x200: SAA subadr */
	char				pad5[3];
	volatile unsigned char		saa_regval;	/* SAA7196 write reg. val */
	char				pad6[3];
	volatile unsigned char		saa_status;	/* SAA7196 status byte */
	/* There is more unused stuff here */
};

struct planb_window {
	int	x, y;
	ushort	width, height;
	ushort	bpp, bpl, depth, pad;
	ushort	swidth, sheight;
	int	norm;
	int	interlace;
	u32	color_fmt;
	int	chromakey;
	int	mode;		/* used to switch between TV/VTR modes */
};

struct planb_suspend {
	int overlay;
	int frame;
	struct dbdma_cmd cmd;
};

struct planb {
	struct	video_device video_dev;
	struct	video_picture picture;		/* Current picture params */
	struct	video_audio audio_dev;		/* Current audio params */
  
	volatile struct planb_registers *planb_base;	/* virt base of planb */
	struct planb_registers *planb_base_phys;	/* phys base of planb */
	void	*priv_space;			/* Org. alloc. mem for kfree */
	int	user;
	unsigned int tab_size;
	int     maxlines;
	struct semaphore lock;
	unsigned int	irq;			/* interrupt number */
	volatile unsigned int intr_mask;

	int	overlay;			/* overlay running? */
	struct	planb_window win;
	unsigned long frame_buffer_phys;	/* We need phys for DMA */
	int	offset;				/* offset of pixel 1 */
	volatile struct dbdma_cmd *ch1_cmd;	/* Video In DMA cmd buffer */
	volatile struct dbdma_cmd *ch2_cmd;	/* Clip Out DMA cmd buffer */
	volatile struct dbdma_cmd *overlay_last1;
	volatile struct dbdma_cmd *overlay_last2;
	unsigned long ch1_cmd_phys;
	volatile unsigned char *mask;		/* Clipmask buffer */
	int suspend;
	wait_queue_head_t suspendq;
	struct planb_suspend suspended;
	int	cmd_buff_inited;		/* cmd buffer inited? */

	int grabbing;
	unsigned int gcount;
	wait_queue_head_t capq;
	int last_fr;
	int prev_last_fr;
	unsigned char **rawbuf;
	int rawbuf_size;
	int gbuf_idx[MAX_GBUFFERS];
	volatile struct dbdma_cmd *cap_cmd[MAX_GBUFFERS];
	volatile struct dbdma_cmd *last_cmd[MAX_GBUFFERS];
	volatile struct dbdma_cmd *pre_cmd[MAX_GBUFFERS];
	int need_pre_capture[MAX_GBUFFERS];
#define PLANB_DUMMY 40	/* # of command buf's allocated for pre-capture seq. */
	int gwidth[MAX_GBUFFERS], gheight[MAX_GBUFFERS];
	unsigned int gfmt[MAX_GBUFFERS];
	int gnorm_switch[MAX_GBUFFERS];
        volatile unsigned int *frame_stat;
#define GBUFFER_UNUSED       0x00U
#define GBUFFER_GRABBING     0x01U
#define GBUFFER_DONE         0x02U
#ifdef PLANB_GSCANLINE
	int gbytes_per_line;
#else
#define MAX_LNUM 431	/* change this if PLANB_MAXLINES or */
			/* PLANB_MAXPIXELS changes */
	int l_fr_addr_idx[MAX_GBUFFERS];
	unsigned char *l_to_addr[MAX_GBUFFERS][MAX_LNUM];
	int l_to_next_idx[MAX_GBUFFERS][MAX_LNUM];
	int l_to_next_size[MAX_GBUFFERS][MAX_LNUM];
	int lsize[MAX_GBUFFERS], lnum[MAX_GBUFFERS];
#endif
};

#endif /* __KERNEL__ */

#endif /* _PLANB_H_ */
