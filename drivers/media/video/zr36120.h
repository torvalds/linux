/* 
    zr36120.h - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink (middelin@polyware.nl)

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

#ifndef _ZR36120_H
#define _ZR36120_H

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/wait.h>

#include <linux/i2c-old.h>
#include <linux/videodev.h>

#include <asm/io.h>

/*
 * Debug macro's, place an x behind the ) for actual debug-compilation
 * E.g. #define DEBUG(x...)	x
 */
#define DEBUG(x...)			/* Debug driver */
#define IDEBUG(x...)			/* Debug interrupt handler */
#define PDEBUG		0		/* Debug PCI writes */

/* defined in zr36120_i2c */
extern struct i2c_bus zoran_i2c_bus_template;

#define	ZORAN_MAX_FBUFFERS	2
#define	ZORAN_MAX_FBUFFER	(768*576*2)
#define	ZORAN_MAX_FBUFSIZE	(ZORAN_MAX_FBUFFERS*ZORAN_MAX_FBUFFER)

#define	ZORAN_VBI_BUFFERS	2
#define	ZORAN_VBI_BUFSIZE	(22*1024*2)

struct tvcard {
	char*	name;		/* name of the cardtype */
	int	video_inputs;	/* number of channels defined in video_mux */
	int	audio_inputs;	/* number of channels defined in audio_mux */
	__u32	swapi2c:1,	/* need to swap i2c wires SDA/SCL? */
		usegirq1:1,	/* VSYNC at GIRQ1 instead of GIRQ0? */
		vsync_pos:1,	/* positive VSYNC signal? */
		hsync_pos:1,	/* positive HSYNC signal? */
		gpdir:8,	/* General Purpose Direction register */
		gpval:8;	/* General Purpose Value register */
	int	video_mux[6];	/* mapping channel number to physical input */
#define		IS_TUNER	0x80
#define		IS_SVHS		0x40
#define		CHANNEL_MASK	0x3F
	int	audio_mux[6];	/* mapping channel number to physical input */
};
#define	TUNER(x)	((x)|IS_TUNER)
#define	SVHS(x)		((x)|IS_SVHS)

struct vidinfo {
	struct	vidinfo* next;	/* next active buffer			*/
	uint	kindof;
#define	FBUFFER_OVERLAY		0
#define	FBUFFER_GRAB		1
#define	FBUFFER_VBI		2
	uint	status;
#define FBUFFER_FREE		0
#define FBUFFER_BUSY		1
#define FBUFFER_DONE		2
	ulong	fieldnr;	/* # of field, not framer!		*/
	uint	x,y;
	int	w,h;		/* w,h can be negative!			*/
	uint	format;		/* index in palette2fmt[]		*/
	uint	bpp;		/* lookup from palette2fmt[]		*/
	uint	bpl;		/* calc: width * bpp			*/
	ulong	busadr;		/* bus addr for DMA engine		*/
	char*	memadr;		/* kernel addr for making copies	*/
	ulong*	overlay;	/* kernel addr of overlay mask		*/
};

struct zoran 
{
	struct video_device video_dev;
#define CARD_DEBUG	KERN_DEBUG "%s(%lu): "
#define CARD_INFO	KERN_INFO "%s(%lu): "
#define CARD_ERR	KERN_ERR "%s(%lu): "
#define CARD		ztv->video_dev.name,ztv->fieldnr

	/* zoran chip specific details */
	struct i2c_bus	i2c;		/* i2c registration data	*/
	struct pci_dev*	dev;		/* ptr to PCI device		*/
	ulong		zoran_adr;	/* bus address of IO memory	*/
	char*		zoran_mem;	/* kernel address of IO memory	*/
	struct tvcard*	card;		/* the cardtype			*/
	uint		norm;		/* 0=PAL, 1=NTSC, 2=SECAM	*/
	uint		tuner_freq;	/* Current freq in kHz		*/
	struct video_picture picture;	/* Current picture params	*/
  
	/* videocard details */
	uint		swidth;		/* screen width			*/
	uint		sheight;	/* screen height		*/
	uint		depth;		/* depth in bits		*/

	/* State details */
	char*		fbuffer;	/* framebuffers for mmap	*/
	struct vidinfo	overinfo;	/* overlay data			*/
	struct vidinfo	grabinfo[ZORAN_MAX_FBUFFERS];	/* grabbing data*/
	wait_queue_head_t grabq;	/* grabbers queue		*/

	/* VBI details */
	struct video_device vbi_dev;
	struct vidinfo	readinfo[2];	/* VBI data - flip buffers	*/
	wait_queue_head_t vbiq;		/* vbi queue			*/

	/* maintenance data */
	int		have_decoder;	/* did we detect a mux?		*/
	int		have_tuner;	/* did we detect a tuner?	*/
	int		users;		/* howmany video/vbi open?	*/
	int		tuner_type;	/* tuner type, when found	*/
	int		running;	/* are we rolling?		*/
	rwlock_t	lock;
	long		state;		/* what is requested of us?	*/
#define STATE_OVERLAY	0
#define STATE_VBI	1
	struct vidinfo*	workqueue;	/* buffers to grab, head is active */
	ulong		fieldnr;	/* #field, ticked every VSYNC	*/
	ulong		lastfieldnr;	/* #field, ticked every GRAB	*/

	int		vidInterlace;	/* calculated */
	int		vidXshift;	/* calculated */
	uint		vidWidth;	/* calculated */
	uint		vidHeight;	/* calculated */
};

#define zrwrite(dat,adr)    writel((dat),(char *) (ztv->zoran_mem+(adr)))
#define zrread(adr)         readl(ztv->zoran_mem+(adr))

#if PDEBUG == 0
#define zrand(dat,adr)      zrwrite((dat) & zrread(adr), adr)
#define zror(dat,adr)       zrwrite((dat) | zrread(adr), adr)
#define zraor(dat,mask,adr) zrwrite( ((dat)&~(mask)) | ((mask)&zrread(adr)), adr)
#else
#define zrand(dat, adr) \
do { \
	ulong data = (dat) & zrread((adr)); \
	zrwrite(data, (adr)); \
	if (0 != (~(dat) & zrread((adr)))) \
		printk(KERN_DEBUG "zoran: zrand at %d(%d) detected set bits(%x)\n", __LINE__, (adr), (dat)); \
} while(0)

#define zror(dat, adr) \
do { \
	ulong data = (dat) | zrread((adr)); \
	zrwrite(data, (adr)); \
	if ((dat) != ((dat) & zrread(adr))) \
		printk(KERN_DEBUG "zoran: zror at %d(%d) detected unset bits(%x)\n", __LINE__, (adr), (dat)); \
} while(0)

#define zraor(dat, mask, adr) \
do { \
	ulong data; \
	if ((dat) & (mask)) \
		printk(KERN_DEBUG "zoran: zraor at %d(%d) detected bits(%x:%x)\n", __LINE__, (adr), (dat), (mask)); \
	data = ((dat)&~(mask)) | ((mask) & zrread((adr))); \
	zrwrite(data,(adr)); \
	if ( (dat) != (~(mask) & zrread((adr))) ) \
		printk(KERN_DEBUG "zoran: zraor at %d(%d) could not set all bits(%x:%x)\n", __LINE__, (adr), (dat), (mask)); \
} while(0)
#endif

#endif

/* zoran PCI address space */
#define ZORAN_VFEH		0x000	/* Video Front End Horizontal Conf. */
#define	ZORAN_VFEH_HSPOL	(1<<30)
#define	ZORAN_VFEH_HSTART	(0x3FF<<10)
#define	ZORAN_VFEH_HEND		(0x3FF<<0)

#define ZORAN_VFEV		0x004	/* Video Front End Vertical Conf. */
#define	ZORAN_VFEV_VSPOL	(1<<30)
#define	ZORAN_VFEV_VSTART	(0x3FF<<10)
#define	ZORAN_VFEV_VEND		(0x3FF<<0)

#define	ZORAN_VFEC		0x008	/* Video Front End Scaler and Pixel */
#define ZORAN_VFEC_EXTFL	(1<<26)
#define	ZORAN_VFEC_TOPFIELD	(1<<25)
#define	ZORAN_VFEC_VCLKPOL	(1<<24)
#define	ZORAN_VFEC_HFILTER	(7<<21)
#define	ZORAN_VFEC_HFILTER_1	(0<<21)	/* no lumi,    3-tap chromo */
#define	ZORAN_VFEC_HFILTER_2	(1<<21)	/* 3-tap lumi, 3-tap chromo */
#define	ZORAN_VFEC_HFILTER_3	(2<<21)	/* 4-tap lumi, 4-tap chromo */
#define	ZORAN_VFEC_HFILTER_4	(3<<21)	/* 5-tap lumi, 4-tap chromo */
#define	ZORAN_VFEC_HFILTER_5	(4<<21)	/* 4-tap lumi, 4-tap chromo */
#define	ZORAN_VFEC_DUPFLD	(1<<20)
#define	ZORAN_VFEC_HORDCM	(63<<14)
#define	ZORAN_VFEC_VERDCM	(63<<8)
#define	ZORAN_VFEC_DISPMOD	(1<<6)
#define	ZORAN_VFEC_RGB		(3<<3)
#define	ZORAN_VFEC_RGB_YUV422	(0<<3)
#define	ZORAN_VFEC_RGB_RGB888	(1<<3)
#define	ZORAN_VFEC_RGB_RGB565	(2<<3)
#define	ZORAN_VFEC_RGB_RGB555	(3<<3)
#define	ZORAN_VFEC_ERRDIF	(1<<2)
#define	ZORAN_VFEC_PACK24	(1<<1)
#define	ZORAN_VFEC_LE		(1<<0)

#define	ZORAN_VTOP		0x00C	/* Video Display "Top" */

#define	ZORAN_VBOT		0x010	/* Video Display "Bottom" */

#define	ZORAN_VSTR		0x014	/* Video Display Stride */
#define	ZORAN_VSTR_DISPSTRIDE	(0xFFFF<<16)
#define	ZORAN_VSTR_VIDOVF	(1<<8)
#define	ZORAN_VSTR_SNAPSHOT	(1<<1)
#define	ZORAN_VSTR_GRAB		(1<<0)

#define	ZORAN_VDC		0x018	/* Video Display Conf. */
#define	ZORAN_VDC_VIDEN		(1<<31)
#define	ZORAN_VDC_MINPIX	(0x1F<<25)
#define	ZORAN_VDC_TRICOM	(1<<24)
#define	ZORAN_VDC_VIDWINHT	(0x3FF<<12)
#define	ZORAN_VDC_VIDWINWID	(0x3FF<<0)

#define	ZORAN_MTOP		0x01C	/* Masking Map "Top" */

#define	ZORAN_MBOT		0x020	/* Masking Map "Bottom" */

#define	ZORAN_OCR		0x024	/* Overlay Control */
#define	ZORAN_OCR_OVLEN		(1<<15)
#define	ZORAN_OCR_MASKSTRIDE	(0xFF<<0)

#define	ZORAN_PCI		0x028	/* System, PCI and GPP Control */
#define	ZORAN_PCI_SOFTRESET	(1<<24)
#define	ZORAN_PCI_WAITSTATE	(3<<16)
#define	ZORAN_PCI_GENPURDIR	(0xFF<<0)

#define	ZORAN_GUEST		0x02C	/* GuestBus Control */

#define	ZORAN_CSOURCE		0x030	/* Code Source Address */

#define	ZORAN_CTRANS		0x034	/* Code Transfer Control */

#define	ZORAN_CMEM		0x038	/* Code Memory Pointer */

#define	ZORAN_ISR		0x03C	/* Interrupt Status Register */
#define	ZORAN_ISR_CODE		(1<<28)
#define	ZORAN_ISR_GIRQ0		(1<<29)
#define	ZORAN_ISR_GIRQ1		(1<<30)

#define	ZORAN_ICR		0x040	/* Interrupt Control Register */
#define	ZORAN_ICR_EN		(1<<24)
#define	ZORAN_ICR_CODE		(1<<28)
#define	ZORAN_ICR_GIRQ0		(1<<29)
#define	ZORAN_ICR_GIRQ1		(1<<30)

#define	ZORAN_I2C		0x044	/* I2C-Bus */
#define ZORAN_I2C_SCL		(1<<1)
#define ZORAN_I2C_SDA		(1<<0)

#define	ZORAN_POST		0x48	/* PostOffice */
#define	ZORAN_POST_PEN		(1<<25)
#define	ZORAN_POST_TIME		(1<<24)
#define	ZORAN_POST_DIR		(1<<23)
#define	ZORAN_POST_GUESTID	(3<<20)
#define	ZORAN_POST_GUEST	(7<<16)
#define	ZORAN_POST_DATA		(0xFF<<0)

#endif
