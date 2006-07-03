/*
 * Motion Eye video4linux driver for Sony Vaio PictureBook
 *
 * Copyright (C) 2001-2004 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
 *
 * Some parts borrowed from various video4linux drivers, especially
 * bttv-driver.c and zoran.c, see original files for credits.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _MEYE_PRIV_H_
#define _MEYE_PRIV_H_

#define MEYE_DRIVER_MAJORVERSION	 1
#define MEYE_DRIVER_MINORVERSION	13

#define MEYE_DRIVER_VERSION __stringify(MEYE_DRIVER_MAJORVERSION) "." \
			    __stringify(MEYE_DRIVER_MINORVERSION)

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kfifo.h>

/****************************************************************************/
/* Motion JPEG chip registers                                               */
/****************************************************************************/

/* Motion JPEG chip PCI configuration registers */
#define MCHIP_PCI_POWER_CSR		0x54
#define MCHIP_PCI_MCORE_STATUS		0x60		/* see HIC_STATUS   */
#define MCHIP_PCI_HOSTUSEREQ_SET	0x64
#define MCHIP_PCI_HOSTUSEREQ_CLR	0x68
#define MCHIP_PCI_LOWPOWER_SET		0x6c
#define MCHIP_PCI_LOWPOWER_CLR		0x70
#define MCHIP_PCI_SOFTRESET_SET		0x74

/* Motion JPEG chip memory mapped registers */
#define MCHIP_MM_REGS			0x200		/* 512 bytes        */
#define MCHIP_REG_TIMEOUT		1000		/* reg access, ~us  */
#define MCHIP_MCC_VRJ_TIMEOUT		1000		/* MCC & VRJ access */

#define MCHIP_MM_PCI_MODE		0x00		/* PCI access mode */
#define MCHIP_MM_PCI_MODE_RETRY		0x00000001	/* retry mode */
#define MCHIP_MM_PCI_MODE_MASTER	0x00000002	/* master access */
#define MCHIP_MM_PCI_MODE_READ_LINE	0x00000004	/* read line */

#define MCHIP_MM_INTA			0x04		/* Int status/mask */
#define MCHIP_MM_INTA_MCC		0x00000001	/* MCC interrupt */
#define MCHIP_MM_INTA_VRJ		0x00000002	/* VRJ interrupt */
#define MCHIP_MM_INTA_HIC_1		0x00000004	/* one frame done */
#define MCHIP_MM_INTA_HIC_1_MASK	0x00000400	/* 1: enable */
#define MCHIP_MM_INTA_HIC_END		0x00000008	/* all frames done */
#define MCHIP_MM_INTA_HIC_END_MASK	0x00000800
#define MCHIP_MM_INTA_JPEG		0x00000010	/* decompress. error */
#define MCHIP_MM_INTA_JPEG_MASK		0x00001000
#define MCHIP_MM_INTA_CAPTURE		0x00000020	/* capture end */
#define MCHIP_MM_INTA_PCI_ERR		0x00000040	/* PCI error */
#define MCHIP_MM_INTA_PCI_ERR_MASK	0x00004000

#define MCHIP_MM_PT_ADDR		0x08		/* page table address*/
							/* n*4kB */
#define MCHIP_NB_PAGES			1024		/* pages for display */
#define MCHIP_NB_PAGES_MJPEG		256		/* pages for mjpeg */

#define MCHIP_MM_FIR(n)			(0x0c+(n)*4)	/* Frame info 0-3 */
#define MCHIP_MM_FIR_RDY		0x00000001	/* frame ready */
#define MCHIP_MM_FIR_FAILFR_MASK	0xf8000000	/* # of failed frames */
#define MCHIP_MM_FIR_FAILFR_SHIFT	27

	/* continuous comp/decomp mode */
#define MCHIP_MM_FIR_C_ENDL_MASK	0x000007fe	/* end DW [10] */
#define MCHIP_MM_FIR_C_ENDL_SHIFT	1
#define MCHIP_MM_FIR_C_ENDP_MASK	0x0007f800	/* end page [8] */
#define MCHIP_MM_FIR_C_ENDP_SHIFT	11
#define MCHIP_MM_FIR_C_STARTP_MASK	0x07f80000	/* start page [8] */
#define MCHIP_MM_FIR_C_STARTP_SHIFT	19

	/* continuous picture output mode */
#define MCHIP_MM_FIR_O_STARTP_MASK	0x7ffe0000	/* start page [10] */
#define MCHIP_MM_FIR_O_STARTP_SHIFT	17

#define MCHIP_MM_FIFO_DATA		0x1c		/* PCI TGT FIFO data */
#define MCHIP_MM_FIFO_STATUS		0x20		/* PCI TGT FIFO stat */
#define MCHIP_MM_FIFO_MASK		0x00000003
#define MCHIP_MM_FIFO_WAIT_OR_READY	0x00000002      /* Bits common to WAIT & READY*/
#define MCHIP_MM_FIFO_IDLE		0x0		/* HIC idle */
#define MCHIP_MM_FIFO_IDLE1		0x1		/* idem ??? */
#define	MCHIP_MM_FIFO_WAIT		0x2		/* wait request */
#define MCHIP_MM_FIFO_READY		0x3		/* data ready */

#define MCHIP_HIC_HOST_USEREQ		0x40		/* host uses MCORE */

#define MCHIP_HIC_TP_BUSY		0x44		/* taking picture */

#define MCHIP_HIC_PIC_SAVED		0x48		/* pic in SDRAM */

#define MCHIP_HIC_LOWPOWER		0x4c		/* clock stopped */

#define MCHIP_HIC_CTL			0x50		/* HIC control */
#define MCHIP_HIC_CTL_SOFT_RESET	0x00000001	/* MCORE reset */
#define MCHIP_HIC_CTL_MCORE_RDY		0x00000002	/* MCORE ready */

#define MCHIP_HIC_CMD			0x54		/* HIC command */
#define MCHIP_HIC_CMD_BITS		0x00000003      /* cmd width=[1:0]*/
#define MCHIP_HIC_CMD_NOOP		0x0
#define MCHIP_HIC_CMD_START		0x1
#define MCHIP_HIC_CMD_STOP		0x2

#define MCHIP_HIC_MODE			0x58
#define MCHIP_HIC_MODE_NOOP		0x0
#define MCHIP_HIC_MODE_STILL_CAP	0x1		/* still pic capt */
#define MCHIP_HIC_MODE_DISPLAY		0x2		/* display */
#define MCHIP_HIC_MODE_STILL_COMP	0x3		/* still pic comp. */
#define MCHIP_HIC_MODE_STILL_DECOMP	0x4		/* still pic decomp. */
#define MCHIP_HIC_MODE_CONT_COMP	0x5		/* cont capt+comp */
#define MCHIP_HIC_MODE_CONT_DECOMP	0x6		/* cont decomp+disp */
#define MCHIP_HIC_MODE_STILL_OUT	0x7		/* still pic output */
#define MCHIP_HIC_MODE_CONT_OUT		0x8		/* cont output */

#define MCHIP_HIC_STATUS		0x5c
#define MCHIP_HIC_STATUS_MCC_RDY	0x00000001	/* MCC reg acc ok */
#define MCHIP_HIC_STATUS_VRJ_RDY	0x00000002	/* VRJ reg acc ok */
#define MCHIP_HIC_STATUS_IDLE           0x00000003
#define MCHIP_HIC_STATUS_CAPDIS		0x00000004	/* cap/disp in prog */
#define MCHIP_HIC_STATUS_COMPDEC	0x00000008	/* (de)comp in prog */
#define MCHIP_HIC_STATUS_BUSY		0x00000010	/* HIC busy */

#define MCHIP_HIC_S_RATE		0x60		/* MJPEG # frames */

#define MCHIP_HIC_PCI_VFMT		0x64		/* video format */
#define MCHIP_HIC_PCI_VFMT_YVYU		0x00000001	/* 0: V Y' U Y */
							/* 1: Y' V Y U */

#define MCHIP_MCC_CMD			0x80		/* MCC commands */
#define MCHIP_MCC_CMD_INITIAL		0x0		/* idle ? */
#define MCHIP_MCC_CMD_IIC_START_SET	0x1
#define MCHIP_MCC_CMD_IIC_END_SET	0x2
#define MCHIP_MCC_CMD_FM_WRITE		0x3		/* frame memory */
#define MCHIP_MCC_CMD_FM_READ		0x4
#define MCHIP_MCC_CMD_FM_STOP		0x5
#define MCHIP_MCC_CMD_CAPTURE		0x6
#define MCHIP_MCC_CMD_DISPLAY		0x7
#define MCHIP_MCC_CMD_END_DISP		0x8
#define MCHIP_MCC_CMD_STILL_COMP	0x9
#define MCHIP_MCC_CMD_STILL_DECOMP	0xa
#define MCHIP_MCC_CMD_STILL_OUTPUT	0xb
#define MCHIP_MCC_CMD_CONT_OUTPUT	0xc
#define MCHIP_MCC_CMD_CONT_COMP		0xd
#define MCHIP_MCC_CMD_CONT_DECOMP	0xe
#define MCHIP_MCC_CMD_RESET		0xf		/* MCC reset */

#define MCHIP_MCC_IIC_WR		0x84

#define MCHIP_MCC_MCC_WR		0x88

#define MCHIP_MCC_MCC_RD		0x8c

#define MCHIP_MCC_STATUS		0x90
#define MCHIP_MCC_STATUS_CAPT		0x00000001	/* capturing */
#define MCHIP_MCC_STATUS_DISP		0x00000002	/* displaying */
#define MCHIP_MCC_STATUS_COMP		0x00000004	/* compressing */
#define MCHIP_MCC_STATUS_DECOMP		0x00000008	/* decompressing */
#define MCHIP_MCC_STATUS_MCC_WR		0x00000010	/* register ready */
#define MCHIP_MCC_STATUS_MCC_RD		0x00000020	/* register ready */
#define MCHIP_MCC_STATUS_IIC_WR		0x00000040	/* register ready */
#define MCHIP_MCC_STATUS_OUTPUT		0x00000080	/* output in prog */

#define MCHIP_MCC_SIG_POLARITY		0x94
#define MCHIP_MCC_SIG_POL_VS_H		0x00000001	/* VS active-high */
#define MCHIP_MCC_SIG_POL_HS_H		0x00000002	/* HS active-high */
#define MCHIP_MCC_SIG_POL_DOE_H		0x00000004	/* DOE active-high */

#define MCHIP_MCC_IRQ			0x98
#define MCHIP_MCC_IRQ_CAPDIS_STRT	0x00000001	/* cap/disp started */
#define MCHIP_MCC_IRQ_CAPDIS_STRT_MASK	0x00000010
#define MCHIP_MCC_IRQ_CAPDIS_END	0x00000002	/* cap/disp ended */
#define MCHIP_MCC_IRQ_CAPDIS_END_MASK	0x00000020
#define MCHIP_MCC_IRQ_COMPDEC_STRT	0x00000004	/* (de)comp started */
#define MCHIP_MCC_IRQ_COMPDEC_STRT_MASK	0x00000040
#define MCHIP_MCC_IRQ_COMPDEC_END	0x00000008	/* (de)comp ended */
#define MCHIP_MCC_IRQ_COMPDEC_END_MASK	0x00000080

#define MCHIP_MCC_HSTART		0x9c		/* video in */
#define MCHIP_MCC_VSTART		0xa0
#define MCHIP_MCC_HCOUNT		0xa4
#define MCHIP_MCC_VCOUNT		0xa8
#define MCHIP_MCC_R_XBASE		0xac		/* capt/disp */
#define MCHIP_MCC_R_YBASE		0xb0
#define MCHIP_MCC_R_XRANGE		0xb4
#define MCHIP_MCC_R_YRANGE		0xb8
#define MCHIP_MCC_B_XBASE		0xbc		/* comp/decomp */
#define MCHIP_MCC_B_YBASE		0xc0
#define MCHIP_MCC_B_XRANGE		0xc4
#define MCHIP_MCC_B_YRANGE		0xc8

#define MCHIP_MCC_R_SAMPLING		0xcc		/* 1: 1:4 */

#define MCHIP_VRJ_CMD			0x100		/* VRJ commands */

/* VRJ registers (see table 12.2.4) */
#define MCHIP_VRJ_COMPRESSED_DATA	0x1b0
#define MCHIP_VRJ_PIXEL_DATA		0x1b8

#define MCHIP_VRJ_BUS_MODE		0x100
#define MCHIP_VRJ_SIGNAL_ACTIVE_LEVEL	0x108
#define MCHIP_VRJ_PDAT_USE		0x110
#define MCHIP_VRJ_MODE_SPECIFY		0x118
#define MCHIP_VRJ_LIMIT_COMPRESSED_LO	0x120
#define MCHIP_VRJ_LIMIT_COMPRESSED_HI	0x124
#define MCHIP_VRJ_COMP_DATA_FORMAT	0x128
#define MCHIP_VRJ_TABLE_DATA		0x140
#define MCHIP_VRJ_RESTART_INTERVAL	0x148
#define MCHIP_VRJ_NUM_LINES		0x150
#define MCHIP_VRJ_NUM_PIXELS		0x158
#define MCHIP_VRJ_NUM_COMPONENTS	0x160
#define MCHIP_VRJ_SOF1			0x168
#define MCHIP_VRJ_SOF2			0x170
#define MCHIP_VRJ_SOF3			0x178
#define MCHIP_VRJ_SOF4			0x180
#define MCHIP_VRJ_SOS			0x188
#define MCHIP_VRJ_SOFT_RESET		0x190

#define MCHIP_VRJ_STATUS		0x1c0
#define MCHIP_VRJ_STATUS_BUSY		0x00001
#define MCHIP_VRJ_STATUS_COMP_ACCESS	0x00002
#define MCHIP_VRJ_STATUS_PIXEL_ACCESS	0x00004
#define MCHIP_VRJ_STATUS_ERROR		0x00008

#define MCHIP_VRJ_IRQ_FLAG		0x1c8
#define MCHIP_VRJ_ERROR_REPORT		0x1d8

#define MCHIP_VRJ_START_COMMAND		0x1a0

/****************************************************************************/
/* Driver definitions.                                                      */
/****************************************************************************/

/* Sony Programmable I/O Controller for accessing the camera commands */
#include <linux/sonypi.h>

/* private API definitions */
#include <linux/meye.h>
#include <linux/mutex.h>


/* Enable jpg software correction */
#define MEYE_JPEG_CORRECTION	1

/* Maximum size of a buffer */
#define MEYE_MAX_BUFSIZE	614400	/* 640 * 480 * 2 */

/* Maximum number of buffers */
#define MEYE_MAX_BUFNBRS	32

/* State of a buffer */
#define MEYE_BUF_UNUSED	0	/* not used */
#define MEYE_BUF_USING	1	/* currently grabbing / playing */
#define MEYE_BUF_DONE	2	/* done */

/* grab buffer */
struct meye_grab_buffer {
	int state;			/* state of buffer */
	unsigned long size;		/* size of jpg frame */
	struct timeval timestamp;	/* timestamp */
	unsigned long sequence;		/* sequence number */
};

/* size of kfifos containings buffer indices */
#define MEYE_QUEUE_SIZE	MEYE_MAX_BUFNBRS

/* Motion Eye device structure */
struct meye {
	struct pci_dev *mchip_dev;	/* pci device */
	u8 mchip_irq;			/* irq */
	u8 mchip_mode;			/* actual mchip mode: HIC_MODE... */
	u8 mchip_fnum;			/* current mchip frame number */
	unsigned char __iomem *mchip_mmregs;/* mchip: memory mapped registers */
	u8 *mchip_ptable[MCHIP_NB_PAGES];/* mchip: ptable */
	void *mchip_ptable_toc;		/* mchip: ptable toc */
	dma_addr_t mchip_dmahandle;	/* mchip: dma handle to ptable toc */
	unsigned char *grab_fbuffer;	/* capture framebuffer */
	unsigned char *grab_temp;	/* temporary buffer */
					/* list of buffers */
	struct meye_grab_buffer grab_buffer[MEYE_MAX_BUFNBRS];
	int vma_use_count[MEYE_MAX_BUFNBRS]; /* mmap count */
	struct mutex lock;		/* mutex for open/mmap... */
	struct kfifo *grabq;		/* queue for buffers to be grabbed */
	spinlock_t grabq_lock;		/* lock protecting the queue */
	struct kfifo *doneq;		/* queue for grabbed buffers */
	spinlock_t doneq_lock;		/* lock protecting the queue */
	wait_queue_head_t proc_list;	/* wait queue */
	struct video_device *video_dev;	/* video device parameters */
	struct video_picture picture;	/* video picture parameters */
	struct meye_params params;	/* additional parameters */
#ifdef CONFIG_PM
	u8 pm_mchip_mode;		/* old mchip mode */
#endif
};

#endif
