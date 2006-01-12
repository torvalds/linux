/*
 * linux/drivers/video/stifb.c - 
 * Low level Frame buffer driver for HP workstations with 
 * STI (standard text interface) video firmware.
 *
 * Copyright (C) 2001-2005 Helge Deller <deller@gmx.de>
 * Portions Copyright (C) 2001 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 * 
 * Based on:
 * - linux/drivers/video/artistfb.c -- Artist frame buffer driver
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *   - based on skeletonfb, which was
 *	Created 28 Dec 1997 by Geert Uytterhoeven
 * - HP Xhp cfb-based X11 window driver for XFree86
 *	(c)Copyright 1992 Hewlett-Packard Co.
 *
 * 
 *  The following graphics display devices (NGLE family) are supported by this driver:
 *
 *  HPA4070A	known as "HCRX", a 1280x1024 color device with 8 planes
 *  HPA4071A	known as "HCRX24", a 1280x1024 color device with 24 planes,
 *		optionally available with a hardware accelerator as HPA4071A_Z
 *  HPA1659A	known as "CRX", a 1280x1024 color device with 8 planes
 *  HPA1439A	known as "CRX24", a 1280x1024 color device with 24 planes,
 *		optionally available with a hardware accelerator.
 *  HPA1924A	known as "GRX", a 1280x1024 grayscale device with 8 planes
 *  HPA2269A	known as "Dual CRX", a 1280x1024 color device with 8 planes,
 *		implements support for two displays on a single graphics card.
 *  HP710C	internal graphics support optionally available on the HP9000s710 SPU,
 *		supports 1280x1024 color displays with 8 planes.
 *  HP710G	same as HP710C, 1280x1024 grayscale only
 *  HP710L	same as HP710C, 1024x768 color only
 *  HP712	internal graphics support on HP9000s712 SPU, supports 640x480, 
 *		1024x768 or 1280x1024 color displays on 8 planes (Artist)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

/* TODO:
 *	- 1bpp mode is completely untested
 *	- add support for h/w acceleration
 *	- add hardware cursor
 *	- automatically disable double buffering (e.g. on RDI precisionbook laptop)
 */


/* on supported graphic devices you may:
 * #define FALLBACK_TO_1BPP to fall back to 1 bpp, or
 * #undef  FALLBACK_TO_1BPP to reject support for unsupported cards */
#undef FALLBACK_TO_1BPP

#undef DEBUG_STIFB_REGS		/* debug sti register accesses */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/pci.h>

#include <asm/grfioctl.h>	/* for HP-UX compatibility */
#include <asm/uaccess.h>

#include "sticore.h"

/* REGION_BASE(fb_info, index) returns the virtual address for region <index> */
#define REGION_BASE(fb_info, index) \
	F_EXTEND(fb_info->sti->glob_cfg->region_ptrs[index])

#define NGLEDEVDEPROM_CRT_REGION 1

#define NR_PALETTE 256

typedef struct {
	__s32	video_config_reg;
	__s32	misc_video_start;
	__s32	horiz_timing_fmt;
	__s32	serr_timing_fmt;
	__s32	vert_timing_fmt;
	__s32	horiz_state;
	__s32	vert_state;
	__s32	vtg_state_elements;
	__s32	pipeline_delay;
	__s32	misc_video_end;
} video_setup_t;

typedef struct {                  
	__s16	sizeof_ngle_data;
	__s16	x_size_visible;	    /* visible screen dim in pixels  */
	__s16	y_size_visible;
	__s16	pad2[15];
	__s16	cursor_pipeline_delay;
	__s16	video_interleaves;
	__s32	pad3[11];
} ngle_rom_t;

struct stifb_info {
	struct fb_info info;
	unsigned int id;
	ngle_rom_t ngle_rom;
	struct sti_struct *sti;
	int deviceSpecificConfig;
	u32 pseudo_palette[16];
};

static int __initdata stifb_bpp_pref[MAX_STI_ROMS];

/* ------------------- chipset specific functions -------------------------- */

/* offsets to graphic-chip internal registers */

#define REG_1		0x000118
#define REG_2		0x000480
#define REG_3		0x0004a0
#define REG_4		0x000600
#define REG_6		0x000800
#define REG_8		0x000820
#define REG_9		0x000a04
#define REG_10		0x018000
#define REG_11		0x018004
#define REG_12		0x01800c
#define REG_13		0x018018
#define REG_14  	0x01801c
#define REG_15		0x200000
#define REG_15b0	0x200000
#define REG_16b1	0x200005
#define REG_16b3	0x200007
#define REG_21		0x200218
#define REG_22		0x0005a0
#define REG_23		0x0005c0
#define REG_26		0x200118
#define REG_27		0x200308
#define REG_32		0x21003c
#define REG_33		0x210040
#define REG_34		0x200008
#define REG_35		0x018010
#define REG_38		0x210020
#define REG_39		0x210120
#define REG_40		0x210130
#define REG_42		0x210028
#define REG_43		0x21002c
#define REG_44		0x210030
#define REG_45		0x210034

#define READ_BYTE(fb,reg)		gsc_readb((fb)->info.fix.mmio_start + (reg))
#define READ_WORD(fb,reg)		gsc_readl((fb)->info.fix.mmio_start + (reg))


#ifndef DEBUG_STIFB_REGS
# define  DEBUG_OFF()
# define  DEBUG_ON()
# define WRITE_BYTE(value,fb,reg)	gsc_writeb((value),(fb)->info.fix.mmio_start + (reg))
# define WRITE_WORD(value,fb,reg)	gsc_writel((value),(fb)->info.fix.mmio_start + (reg))
#else
  static int debug_on = 1;
# define  DEBUG_OFF() debug_on=0
# define  DEBUG_ON()  debug_on=1
# define WRITE_BYTE(value,fb,reg)	do { if (debug_on) \
						printk(KERN_DEBUG "%30s: WRITE_BYTE(0x%06x) = 0x%02x (old=0x%02x)\n", \
							__FUNCTION__, reg, value, READ_BYTE(fb,reg)); 		  \
					gsc_writeb((value),(fb)->info.fix.mmio_start + (reg)); } while (0)
# define WRITE_WORD(value,fb,reg)	do { if (debug_on) \
						printk(KERN_DEBUG "%30s: WRITE_WORD(0x%06x) = 0x%08x (old=0x%08x)\n", \
							__FUNCTION__, reg, value, READ_WORD(fb,reg)); 		  \
					gsc_writel((value),(fb)->info.fix.mmio_start + (reg)); } while (0)
#endif /* DEBUG_STIFB_REGS */


#define ENABLE	1	/* for enabling/disabling screen */	
#define DISABLE 0

#define NGLE_LOCK(fb_info)	do { } while (0) 
#define NGLE_UNLOCK(fb_info)	do { } while (0)

static void
SETUP_HW(struct stifb_info *fb)
{
	char stat;

	do {
		stat = READ_BYTE(fb, REG_15b0);
		if (!stat)
	    		stat = READ_BYTE(fb, REG_15b0);
	} while (stat);
}


static void
SETUP_FB(struct stifb_info *fb)
{	
	unsigned int reg10_value = 0;
	
	SETUP_HW(fb);
	switch (fb->id)
	{
		case CRT_ID_VISUALIZE_EG:
		case S9000_ID_ARTIST:
		case S9000_ID_A1659A:
			reg10_value = 0x13601000;
			break;
		case S9000_ID_A1439A:
			if (fb->info.var.bits_per_pixel == 32)						
				reg10_value = 0xBBA0A000;
			else 
				reg10_value = 0x13601000;
			break;
		case S9000_ID_HCRX:
			if (fb->info.var.bits_per_pixel == 32)
				reg10_value = 0xBBA0A000;
			else					
				reg10_value = 0x13602000;
			break;
		case S9000_ID_TIMBER:
		case CRX24_OVERLAY_PLANES:
			reg10_value = 0x13602000;
			break;
	}
	if (reg10_value)
		WRITE_WORD(reg10_value, fb, REG_10);
	WRITE_WORD(0x83000300, fb, REG_14);
	SETUP_HW(fb);
	WRITE_BYTE(1, fb, REG_16b1);
}

static void
START_IMAGE_COLORMAP_ACCESS(struct stifb_info *fb)
{
	SETUP_HW(fb);
	WRITE_WORD(0xBBE0F000, fb, REG_10);
	WRITE_WORD(0x03000300, fb, REG_14);
	WRITE_WORD(~0, fb, REG_13);
}

static void
WRITE_IMAGE_COLOR(struct stifb_info *fb, int index, int color) 
{
	SETUP_HW(fb);
	WRITE_WORD(((0x100+index)<<2), fb, REG_3);
	WRITE_WORD(color, fb, REG_4);
}

static void
FINISH_IMAGE_COLORMAP_ACCESS(struct stifb_info *fb) 
{		
	WRITE_WORD(0x400, fb, REG_2);
	if (fb->info.var.bits_per_pixel == 32) {
		WRITE_WORD(0x83000100, fb, REG_1);
	} else {
		if (fb->id == S9000_ID_ARTIST || fb->id == CRT_ID_VISUALIZE_EG)
			WRITE_WORD(0x80000100, fb, REG_26);
		else							
			WRITE_WORD(0x80000100, fb, REG_1);
	}
	SETUP_FB(fb);
}

static void
SETUP_RAMDAC(struct stifb_info *fb) 
{
	SETUP_HW(fb);
	WRITE_WORD(0x04000000, fb, 0x1020);
	WRITE_WORD(0xff000000, fb, 0x1028);
}

static void 
CRX24_SETUP_RAMDAC(struct stifb_info *fb) 
{
	SETUP_HW(fb);
	WRITE_WORD(0x04000000, fb, 0x1000);
	WRITE_WORD(0x02000000, fb, 0x1004);
	WRITE_WORD(0xff000000, fb, 0x1008);
	WRITE_WORD(0x05000000, fb, 0x1000);
	WRITE_WORD(0x02000000, fb, 0x1004);
	WRITE_WORD(0x03000000, fb, 0x1008);
}

#if 0
static void 
HCRX_SETUP_RAMDAC(struct stifb_info *fb)
{
	WRITE_WORD(0xffffffff, fb, REG_32);
}
#endif

static void 
CRX24_SET_OVLY_MASK(struct stifb_info *fb)
{
	SETUP_HW(fb);
	WRITE_WORD(0x13a02000, fb, REG_11);
	WRITE_WORD(0x03000300, fb, REG_14);
	WRITE_WORD(0x000017f0, fb, REG_3);
	WRITE_WORD(0xffffffff, fb, REG_13);
	WRITE_WORD(0xffffffff, fb, REG_22);
	WRITE_WORD(0x00000000, fb, REG_23);
}

static void
ENABLE_DISABLE_DISPLAY(struct stifb_info *fb, int enable)
{
	unsigned int value = enable ? 0x43000000 : 0x03000000;
        SETUP_HW(fb);
        WRITE_WORD(0x06000000,	fb, 0x1030);
        WRITE_WORD(value, 	fb, 0x1038);
}

static void 
CRX24_ENABLE_DISABLE_DISPLAY(struct stifb_info *fb, int enable)
{
	unsigned int value = enable ? 0x10000000 : 0x30000000;
	SETUP_HW(fb);
	WRITE_WORD(0x01000000,	fb, 0x1000);
	WRITE_WORD(0x02000000,	fb, 0x1004);
	WRITE_WORD(value,	fb, 0x1008);
}

static void
ARTIST_ENABLE_DISABLE_DISPLAY(struct stifb_info *fb, int enable) 
{
	u32 DregsMiscVideo = REG_21;
	u32 DregsMiscCtl = REG_27;
	
	SETUP_HW(fb);
	if (enable) {
	  WRITE_WORD(READ_WORD(fb, DregsMiscVideo) | 0x0A000000, fb, DregsMiscVideo);
	  WRITE_WORD(READ_WORD(fb, DregsMiscCtl)   | 0x00800000, fb, DregsMiscCtl);
	} else {
	  WRITE_WORD(READ_WORD(fb, DregsMiscVideo) & ~0x0A000000, fb, DregsMiscVideo);
	  WRITE_WORD(READ_WORD(fb, DregsMiscCtl)   & ~0x00800000, fb, DregsMiscCtl);
	}
}

#define GET_ROMTABLE_INDEX(fb) \
	(READ_BYTE(fb, REG_16b3) - 1)

#define HYPER_CONFIG_PLANES_24 0x00000100
	
#define IS_24_DEVICE(fb) \
	(fb->deviceSpecificConfig & HYPER_CONFIG_PLANES_24)

#define IS_888_DEVICE(fb) \
	(!(IS_24_DEVICE(fb)))

#define GET_FIFO_SLOTS(fb, cnt, numslots)	\
{	while (cnt < numslots) 			\
		cnt = READ_WORD(fb, REG_34);	\
	cnt -= numslots;			\
}

#define	    IndexedDcd	0	/* Pixel data is indexed (pseudo) color */
#define	    Otc04	2	/* Pixels in each longword transfer (4) */
#define	    Otc32	5	/* Pixels in each longword transfer (32) */
#define	    Ots08	3	/* Each pixel is size (8)d transfer (1) */
#define	    OtsIndirect	6	/* Each bit goes through FG/BG color(8) */
#define	    AddrLong	5	/* FB address is Long aligned (pixel) */
#define	    BINovly	0x2	/* 8 bit overlay */
#define	    BINapp0I	0x0	/* Application Buffer 0, Indexed */
#define	    BINapp1I	0x1	/* Application Buffer 1, Indexed */
#define	    BINapp0F8	0xa	/* Application Buffer 0, Fractional 8-8-8 */
#define	    BINattr	0xd	/* Attribute Bitmap */
#define	    RopSrc 	0x3
#define	    BitmapExtent08  3	/* Each write hits ( 8) bits in depth */
#define	    BitmapExtent32  5	/* Each write hits (32) bits in depth */
#define	    DataDynamic	    0	/* Data register reloaded by direct access */
#define	    MaskDynamic	    1	/* Mask register reloaded by direct access */
#define	    MaskOtc	    0	/* Mask contains Object Count valid bits */

#define MaskAddrOffset(offset) (offset)
#define StaticReg(en) (en)
#define BGx(en) (en)
#define FGx(en) (en)

#define BAJustPoint(offset) (offset)
#define BAIndexBase(base) (base)
#define BA(F,C,S,A,J,B,I) \
	(((F)<<31)|((C)<<27)|((S)<<24)|((A)<<21)|((J)<<16)|((B)<<12)|(I))

#define IBOvals(R,M,X,S,D,L,B,F) \
	(((R)<<8)|((M)<<16)|((X)<<24)|((S)<<29)|((D)<<28)|((L)<<31)|((B)<<1)|(F))

#define NGLE_QUICK_SET_IMAGE_BITMAP_OP(fb, val) \
	WRITE_WORD(val, fb, REG_14)

#define NGLE_QUICK_SET_DST_BM_ACCESS(fb, val) \
	WRITE_WORD(val, fb, REG_11)

#define NGLE_QUICK_SET_CTL_PLN_REG(fb, val) \
	WRITE_WORD(val, fb, REG_12)

#define NGLE_REALLY_SET_IMAGE_PLANEMASK(fb, plnmsk32) \
	WRITE_WORD(plnmsk32, fb, REG_13)

#define NGLE_REALLY_SET_IMAGE_FG_COLOR(fb, fg32) \
	WRITE_WORD(fg32, fb, REG_35)

#define NGLE_SET_TRANSFERDATA(fb, val) \
	WRITE_WORD(val, fb, REG_8)

#define NGLE_SET_DSTXY(fb, val) \
	WRITE_WORD(val, fb, REG_6)

#define NGLE_LONG_FB_ADDRESS(fbaddrbase, x, y) (		\
	(u32) (fbaddrbase) +					\
	    (	(unsigned int)  ( (y) << 13      ) |		\
		(unsigned int)  ( (x) << 2       )	)	\
	)

#define NGLE_BINC_SET_DSTADDR(fb, addr) \
	WRITE_WORD(addr, fb, REG_3)

#define NGLE_BINC_SET_SRCADDR(fb, addr) \
	WRITE_WORD(addr, fb, REG_2)

#define NGLE_BINC_SET_DSTMASK(fb, mask) \
	WRITE_WORD(mask, fb, REG_22)

#define NGLE_BINC_WRITE32(fb, data32) \
	WRITE_WORD(data32, fb, REG_23)

#define START_COLORMAPLOAD(fb, cmapBltCtlData32) \
	WRITE_WORD((cmapBltCtlData32), fb, REG_38)

#define SET_LENXY_START_RECFILL(fb, lenxy) \
	WRITE_WORD(lenxy, fb, REG_9)

static void
HYPER_ENABLE_DISABLE_DISPLAY(struct stifb_info *fb, int enable)
{
	u32 DregsHypMiscVideo = REG_33;
	unsigned int value;
	SETUP_HW(fb);
	value = READ_WORD(fb, DregsHypMiscVideo);
	if (enable)
		value |= 0x0A000000;
	else
		value &= ~0x0A000000;
	WRITE_WORD(value, fb, DregsHypMiscVideo);
}


/* BufferNumbers used by SETUP_ATTR_ACCESS() */
#define BUFF0_CMAP0	0x00001e02
#define BUFF1_CMAP0	0x02001e02
#define BUFF1_CMAP3	0x0c001e02
#define ARTIST_CMAP0	0x00000102
#define HYPER_CMAP8	0x00000100
#define HYPER_CMAP24	0x00000800

static void
SETUP_ATTR_ACCESS(struct stifb_info *fb, unsigned BufferNumber)
{
	SETUP_HW(fb);
	WRITE_WORD(0x2EA0D000, fb, REG_11);
	WRITE_WORD(0x23000302, fb, REG_14);
	WRITE_WORD(BufferNumber, fb, REG_12);
	WRITE_WORD(0xffffffff, fb, REG_8);
}

static void
SET_ATTR_SIZE(struct stifb_info *fb, int width, int height) 
{
	/* REG_6 seems to have special values when run on a 
	   RDI precisionbook parisc laptop (INTERNAL_EG_DX1024 or
	   INTERNAL_EG_X1024).  The values are:
		0x2f0: internal (LCD) & external display enabled
		0x2a0: external display only
		0x000: zero on standard artist graphic cards
	*/ 
	WRITE_WORD(0x00000000, fb, REG_6);
	WRITE_WORD((width<<16) | height, fb, REG_9);
	WRITE_WORD(0x05000000, fb, REG_6);
	WRITE_WORD(0x00040001, fb, REG_9);
}

static void
FINISH_ATTR_ACCESS(struct stifb_info *fb) 
{
	SETUP_HW(fb);
	WRITE_WORD(0x00000000, fb, REG_12);
}

static void
elkSetupPlanes(struct stifb_info *fb)
{
	SETUP_RAMDAC(fb);
	SETUP_FB(fb);
}

static void 
ngleSetupAttrPlanes(struct stifb_info *fb, int BufferNumber)
{
	SETUP_ATTR_ACCESS(fb, BufferNumber);
	SET_ATTR_SIZE(fb, fb->info.var.xres, fb->info.var.yres);
	FINISH_ATTR_ACCESS(fb);
	SETUP_FB(fb);
}


static void
rattlerSetupPlanes(struct stifb_info *fb)
{
	CRX24_SETUP_RAMDAC(fb);
    
	/* replacement for: SETUP_FB(fb, CRX24_OVERLAY_PLANES); */
	WRITE_WORD(0x83000300, fb, REG_14);
	SETUP_HW(fb);
	WRITE_BYTE(1, fb, REG_16b1);

	fb_memset(fb->info.fix.smem_start, 0xff,
		fb->info.var.yres*fb->info.fix.line_length);
    
	CRX24_SET_OVLY_MASK(fb);
	SETUP_FB(fb);
}


#define HYPER_CMAP_TYPE				0
#define NGLE_CMAP_INDEXED0_TYPE			0
#define NGLE_CMAP_OVERLAY_TYPE			3

/* typedef of LUT (Colormap) BLT Control Register */
typedef union	/* Note assumption that fields are packed left-to-right */
{	u32 all;
	struct
	{
		unsigned enable              :  1;
		unsigned waitBlank           :  1;
		unsigned reserved1           :  4;
		unsigned lutOffset           : 10;   /* Within destination LUT */
		unsigned lutType             :  2;   /* Cursor, image, overlay */
		unsigned reserved2           :  4;
		unsigned length              : 10;
	} fields;
} NgleLutBltCtl;


#if 0
static NgleLutBltCtl
setNgleLutBltCtl(struct stifb_info *fb, int offsetWithinLut, int length)
{
	NgleLutBltCtl lutBltCtl;

	/* set enable, zero reserved fields */
	lutBltCtl.all           = 0x80000000;
	lutBltCtl.fields.length = length;

	switch (fb->id) 
	{
	case S9000_ID_A1439A:		/* CRX24 */
		if (fb->var.bits_per_pixel == 8) {
			lutBltCtl.fields.lutType = NGLE_CMAP_OVERLAY_TYPE;
			lutBltCtl.fields.lutOffset = 0;
		} else {
			lutBltCtl.fields.lutType = NGLE_CMAP_INDEXED0_TYPE;
			lutBltCtl.fields.lutOffset = 0 * 256;
		}
		break;
		
	case S9000_ID_ARTIST:
		lutBltCtl.fields.lutType = NGLE_CMAP_INDEXED0_TYPE;
		lutBltCtl.fields.lutOffset = 0 * 256;
		break;
		
	default:
		lutBltCtl.fields.lutType = NGLE_CMAP_INDEXED0_TYPE;
		lutBltCtl.fields.lutOffset = 0;
		break;
	}

	/* Offset points to start of LUT.  Adjust for within LUT */
	lutBltCtl.fields.lutOffset += offsetWithinLut;

	return lutBltCtl;
}
#endif

static NgleLutBltCtl
setHyperLutBltCtl(struct stifb_info *fb, int offsetWithinLut, int length) 
{
	NgleLutBltCtl lutBltCtl;

	/* set enable, zero reserved fields */
	lutBltCtl.all = 0x80000000;

	lutBltCtl.fields.length = length;
	lutBltCtl.fields.lutType = HYPER_CMAP_TYPE;

	/* Expect lutIndex to be 0 or 1 for image cmaps, 2 or 3 for overlay cmaps */
	if (fb->info.var.bits_per_pixel == 8)
		lutBltCtl.fields.lutOffset = 2 * 256;
	else
		lutBltCtl.fields.lutOffset = 0 * 256;

	/* Offset points to start of LUT.  Adjust for within LUT */
	lutBltCtl.fields.lutOffset += offsetWithinLut;

	return lutBltCtl;
}


static void hyperUndoITE(struct stifb_info *fb)
{
	int nFreeFifoSlots = 0;
	u32 fbAddr;

	NGLE_LOCK(fb);

	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 1);
	WRITE_WORD(0xffffffff, fb, REG_32);

	/* Write overlay transparency mask so only entry 255 is transparent */

	/* Hardware setup for full-depth write to "magic" location */
	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 7);
	NGLE_QUICK_SET_DST_BM_ACCESS(fb, 
		BA(IndexedDcd, Otc04, Ots08, AddrLong,
		BAJustPoint(0), BINovly, BAIndexBase(0)));
	NGLE_QUICK_SET_IMAGE_BITMAP_OP(fb,
		IBOvals(RopSrc, MaskAddrOffset(0),
		BitmapExtent08, StaticReg(0),
		DataDynamic, MaskOtc, BGx(0), FGx(0)));

	/* Now prepare to write to the "magic" location */
	fbAddr = NGLE_LONG_FB_ADDRESS(0, 1532, 0);
	NGLE_BINC_SET_DSTADDR(fb, fbAddr);
	NGLE_REALLY_SET_IMAGE_PLANEMASK(fb, 0xffffff);
	NGLE_BINC_SET_DSTMASK(fb, 0xffffffff);

	/* Finally, write a zero to clear the mask */
	NGLE_BINC_WRITE32(fb, 0);

	NGLE_UNLOCK(fb);
}

static void 
ngleDepth8_ClearImagePlanes(struct stifb_info *fb)
{
	/* FIXME! */
}

static void 
ngleDepth24_ClearImagePlanes(struct stifb_info *fb)
{
	/* FIXME! */
}

static void
ngleResetAttrPlanes(struct stifb_info *fb, unsigned int ctlPlaneReg)
{
	int nFreeFifoSlots = 0;
	u32 packed_dst;
	u32 packed_len;

	NGLE_LOCK(fb);

	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 4);
	NGLE_QUICK_SET_DST_BM_ACCESS(fb, 
				     BA(IndexedDcd, Otc32, OtsIndirect,
					AddrLong, BAJustPoint(0),
					BINattr, BAIndexBase(0)));
	NGLE_QUICK_SET_CTL_PLN_REG(fb, ctlPlaneReg);
	NGLE_SET_TRANSFERDATA(fb, 0xffffffff);

	NGLE_QUICK_SET_IMAGE_BITMAP_OP(fb,
				       IBOvals(RopSrc, MaskAddrOffset(0),
					       BitmapExtent08, StaticReg(1),
					       DataDynamic, MaskOtc,
					       BGx(0), FGx(0)));
	packed_dst = 0;
	packed_len = (fb->info.var.xres << 16) | fb->info.var.yres;
	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 2);
	NGLE_SET_DSTXY(fb, packed_dst);
	SET_LENXY_START_RECFILL(fb, packed_len);

	/*
	 * In order to work around an ELK hardware problem (Buffy doesn't
	 * always flush it's buffers when writing to the attribute
	 * planes), at least 4 pixels must be written to the attribute
	 * planes starting at (X == 1280) and (Y != to the last Y written
	 * by BIF):
	 */

	if (fb->id == S9000_ID_A1659A) {   /* ELK_DEVICE_ID */
		/* It's safe to use scanline zero: */
		packed_dst = (1280 << 16);
		GET_FIFO_SLOTS(fb, nFreeFifoSlots, 2);
		NGLE_SET_DSTXY(fb, packed_dst);
		packed_len = (4 << 16) | 1;
		SET_LENXY_START_RECFILL(fb, packed_len);
	}   /* ELK Hardware Kludge */

	/**** Finally, set the Control Plane Register back to zero: ****/
	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 1);
	NGLE_QUICK_SET_CTL_PLN_REG(fb, 0);
	
	NGLE_UNLOCK(fb);
}
    
static void
ngleClearOverlayPlanes(struct stifb_info *fb, int mask, int data)
{
	int nFreeFifoSlots = 0;
	u32 packed_dst;
	u32 packed_len;
    
	NGLE_LOCK(fb);

	/* Hardware setup */
	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 8);
	NGLE_QUICK_SET_DST_BM_ACCESS(fb, 
				     BA(IndexedDcd, Otc04, Ots08, AddrLong,
					BAJustPoint(0), BINovly, BAIndexBase(0)));

        NGLE_SET_TRANSFERDATA(fb, 0xffffffff);  /* Write foreground color */

        NGLE_REALLY_SET_IMAGE_FG_COLOR(fb, data);
        NGLE_REALLY_SET_IMAGE_PLANEMASK(fb, mask);
    
        packed_dst = 0;
        packed_len = (fb->info.var.xres << 16) | fb->info.var.yres;
        NGLE_SET_DSTXY(fb, packed_dst);
    
        /* Write zeroes to overlay planes */		       
	NGLE_QUICK_SET_IMAGE_BITMAP_OP(fb,
				       IBOvals(RopSrc, MaskAddrOffset(0),
					       BitmapExtent08, StaticReg(0),
					       DataDynamic, MaskOtc, BGx(0), FGx(0)));
		       
        SET_LENXY_START_RECFILL(fb, packed_len);

	NGLE_UNLOCK(fb);
}

static void 
hyperResetPlanes(struct stifb_info *fb, int enable)
{
	unsigned int controlPlaneReg;

	NGLE_LOCK(fb);

	if (IS_24_DEVICE(fb))
		if (fb->info.var.bits_per_pixel == 32)
			controlPlaneReg = 0x04000F00;
		else
			controlPlaneReg = 0x00000F00;   /* 0x00000800 should be enought, but lets clear all 4 bits */
	else
		controlPlaneReg = 0x00000F00; /* 0x00000100 should be enought, but lets clear all 4 bits */

	switch (enable) {
	case ENABLE:
		/* clear screen */
		if (IS_24_DEVICE(fb))
			ngleDepth24_ClearImagePlanes(fb);
		else
			ngleDepth8_ClearImagePlanes(fb);

		/* Paint attribute planes for default case.
		 * On Hyperdrive, this means all windows using overlay cmap 0. */
		ngleResetAttrPlanes(fb, controlPlaneReg);

		/* clear overlay planes */
	        ngleClearOverlayPlanes(fb, 0xff, 255);

		/**************************************************
		 ** Also need to counteract ITE settings 
		 **************************************************/
		hyperUndoITE(fb);
		break;

	case DISABLE:
		/* clear screen */
		if (IS_24_DEVICE(fb))
			ngleDepth24_ClearImagePlanes(fb);
		else
			ngleDepth8_ClearImagePlanes(fb);
		ngleResetAttrPlanes(fb, controlPlaneReg);
		ngleClearOverlayPlanes(fb, 0xff, 0);
		break;

	case -1:	/* RESET */
		hyperUndoITE(fb);
		ngleResetAttrPlanes(fb, controlPlaneReg);
		break;
    	}
	
	NGLE_UNLOCK(fb);
}

/* Return pointer to in-memory structure holding ELK device-dependent ROM values. */

static void 
ngleGetDeviceRomData(struct stifb_info *fb)
{
#if 0
XXX: FIXME: !!!
	int	*pBytePerLongDevDepData;/* data byte == LSB */
	int 	*pRomTable;
	NgleDevRomData	*pPackedDevRomData;
	int	sizePackedDevRomData = sizeof(*pPackedDevRomData);
	char	*pCard8;
	int	i;
	char	*mapOrigin = NULL;
    
	int romTableIdx;

	pPackedDevRomData = fb->ngle_rom;

	SETUP_HW(fb);
	if (fb->id == S9000_ID_ARTIST) {
		pPackedDevRomData->cursor_pipeline_delay = 4;
		pPackedDevRomData->video_interleaves     = 4;
	} else {
		/* Get pointer to unpacked byte/long data in ROM */
		pBytePerLongDevDepData = fb->sti->regions[NGLEDEVDEPROM_CRT_REGION];

		/* Tomcat supports several resolutions: 1280x1024, 1024x768, 640x480 */
		if (fb->id == S9000_ID_TOMCAT)
	{
	    /*  jump to the correct ROM table  */
	    GET_ROMTABLE_INDEX(romTableIdx);
	    while  (romTableIdx > 0)
	    {
		pCard8 = (Card8 *) pPackedDevRomData;
		pRomTable = pBytePerLongDevDepData;
		/* Pack every fourth byte from ROM into structure */
		for (i = 0; i < sizePackedDevRomData; i++)
		{
		    *pCard8++ = (Card8) (*pRomTable++);
		}

		pBytePerLongDevDepData = (Card32 *)
			((Card8 *) pBytePerLongDevDepData +
			       pPackedDevRomData->sizeof_ngle_data);

		romTableIdx--;
	    }
	}

	pCard8 = (Card8 *) pPackedDevRomData;

	/* Pack every fourth byte from ROM into structure */
	for (i = 0; i < sizePackedDevRomData; i++)
	{
	    *pCard8++ = (Card8) (*pBytePerLongDevDepData++);
	}
    }

    SETUP_FB(fb);
#endif
}


#define HYPERBOWL_MODE_FOR_8_OVER_88_LUT0_NO_TRANSPARENCIES	4
#define HYPERBOWL_MODE01_8_24_LUT0_TRANSPARENT_LUT1_OPAQUE	8
#define HYPERBOWL_MODE01_8_24_LUT0_OPAQUE_LUT1_OPAQUE		10
#define HYPERBOWL_MODE2_8_24					15

/* HCRX specific boot-time initialization */
static void __init
SETUP_HCRX(struct stifb_info *fb)
{
	int	hyperbowl;
        int	nFreeFifoSlots = 0;

	if (fb->id != S9000_ID_HCRX)
		return;

	/* Initialize Hyperbowl registers */
	GET_FIFO_SLOTS(fb, nFreeFifoSlots, 7);
	
	if (IS_24_DEVICE(fb)) {
		hyperbowl = (fb->info.var.bits_per_pixel == 32) ?
			HYPERBOWL_MODE01_8_24_LUT0_TRANSPARENT_LUT1_OPAQUE :
			HYPERBOWL_MODE01_8_24_LUT0_OPAQUE_LUT1_OPAQUE;

		/* First write to Hyperbowl must happen twice (bug) */
		WRITE_WORD(hyperbowl, fb, REG_40);
		WRITE_WORD(hyperbowl, fb, REG_40);
		
		WRITE_WORD(HYPERBOWL_MODE2_8_24, fb, REG_39);
		
		WRITE_WORD(0x014c0148, fb, REG_42); /* Set lut 0 to be the direct color */
		WRITE_WORD(0x404c4048, fb, REG_43);
		WRITE_WORD(0x034c0348, fb, REG_44);
		WRITE_WORD(0x444c4448, fb, REG_45);
	} else {
		hyperbowl = HYPERBOWL_MODE_FOR_8_OVER_88_LUT0_NO_TRANSPARENCIES;

		/* First write to Hyperbowl must happen twice (bug) */
		WRITE_WORD(hyperbowl, fb, REG_40);
		WRITE_WORD(hyperbowl, fb, REG_40);

		WRITE_WORD(0x00000000, fb, REG_42);
		WRITE_WORD(0x00000000, fb, REG_43);
		WRITE_WORD(0x00000000, fb, REG_44);
		WRITE_WORD(0x444c4048, fb, REG_45);
	}
}


/* ------------------- driver specific functions --------------------------- */

#define TMPBUFLEN 2048

static ssize_t
stifb_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	char tmpbuf[TMPBUFLEN];

	if (!info || ! info->screen_base)
		return -ENODEV;

	if (p >= info->fix.smem_len)
	    return 0;
	if (count >= info->fix.smem_len)
	    count = info->fix.smem_len;
	if (count + p > info->fix.smem_len)
		count = info->fix.smem_len - p;
	if (count > sizeof(tmpbuf))
		count = sizeof(tmpbuf);
	if (count) {
	    char *base_addr;

	    base_addr = info->screen_base;
	    memcpy_fromio(&tmpbuf, base_addr+p, count);
	    count -= copy_to_user(buf, &tmpbuf, count);
	    if (!count)
		return -EFAULT;
	    *ppos += count;
	}
	return count;
}

static ssize_t
stifb_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = iminor(inode);
	struct fb_info *info = registered_fb[fbidx];
	unsigned long p = *ppos;
	size_t c;
	int err;
	char tmpbuf[TMPBUFLEN];

	if (!info || !info->screen_base)
		return -ENODEV;

	if (p > info->fix.smem_len)
	    return -ENOSPC;
	if (count >= info->fix.smem_len)
	    count = info->fix.smem_len;
	err = 0;
	if (count + p > info->fix.smem_len) {
	    count = info->fix.smem_len - p;
	    err = -ENOSPC;
	}

	p += (unsigned long)info->screen_base;
	c = count;
	while (c) {
	    int len = c > sizeof(tmpbuf) ? sizeof(tmpbuf) : c;
	    err = -EFAULT;
	    if (copy_from_user(&tmpbuf, buf, len))
		    break;
	    memcpy_toio(p, &tmpbuf, len);
	    c -= len;
	    p += len;
	    buf += len;
	    *ppos += len;
	}
	if (count-c)
		return (count-c);
	return err;
}

static int
stifb_setcolreg(u_int regno, u_int red, u_int green,
	      u_int blue, u_int transp, struct fb_info *info)
{
	struct stifb_info *fb = (struct stifb_info *) info;
	u32 color;

	if (regno >= NR_PALETTE)
		return 1;

	red   >>= 8;
	green >>= 8;
	blue  >>= 8;

	DEBUG_OFF();

	START_IMAGE_COLORMAP_ACCESS(fb);

	if (unlikely(fb->info.var.grayscale)) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		color = ((red * 77) +
			 (green * 151) +
			 (blue * 28)) >> 8;
	} else {
		color = ((red << 16) |
			 (green << 8) |
			 (blue));
	}

	if (fb->info.fix.visual == FB_VISUAL_DIRECTCOLOR) {
		struct fb_var_screeninfo *var = &fb->info.var;
		if (regno < 16)
			((u32 *)fb->info.pseudo_palette)[regno] =
				regno << var->red.offset |
				regno << var->green.offset |
				regno << var->blue.offset;
	}

	WRITE_IMAGE_COLOR(fb, regno, color);

	if (fb->id == S9000_ID_HCRX) {
		NgleLutBltCtl lutBltCtl;

		lutBltCtl = setHyperLutBltCtl(fb,
				0,	/* Offset w/i LUT */
				256);	/* Load entire LUT */
		NGLE_BINC_SET_SRCADDR(fb,
				NGLE_LONG_FB_ADDRESS(0, 0x100, 0)); 
				/* 0x100 is same as used in WRITE_IMAGE_COLOR() */
		START_COLORMAPLOAD(fb, lutBltCtl.all);
		SETUP_FB(fb);
	} else {
		/* cleanup colormap hardware */
		FINISH_IMAGE_COLORMAP_ACCESS(fb);
	}

	DEBUG_ON();

	return 0;
}

static int
stifb_blank(int blank_mode, struct fb_info *info)
{
	struct stifb_info *fb = (struct stifb_info *) info;
	int enable = (blank_mode == 0) ? ENABLE : DISABLE;

	switch (fb->id) {
	case S9000_ID_A1439A:
		CRX24_ENABLE_DISABLE_DISPLAY(fb, enable);
		break;
	case CRT_ID_VISUALIZE_EG:
	case S9000_ID_ARTIST:
		ARTIST_ENABLE_DISABLE_DISPLAY(fb, enable);
		break;
	case S9000_ID_HCRX:
		HYPER_ENABLE_DISABLE_DISPLAY(fb, enable);
		break;
	case S9000_ID_A1659A:	/* fall through */
	case S9000_ID_TIMBER:
	case CRX24_OVERLAY_PLANES:
	default:
		ENABLE_DISABLE_DISPLAY(fb, enable);
		break;
	}
	
	SETUP_FB(fb);
	return 0;
}

static void __init
stifb_init_display(struct stifb_info *fb)
{
	int id = fb->id;

	SETUP_FB(fb);

	/* HCRX specific initialization */
	SETUP_HCRX(fb);
	
	/*
	if (id == S9000_ID_HCRX)
		hyperInitSprite(fb);
	else
		ngleInitSprite(fb);
	*/
	
	/* Initialize the image planes. */ 
        switch (id) {
	 case S9000_ID_HCRX:
	    hyperResetPlanes(fb, ENABLE);
	    break;
	 case S9000_ID_A1439A:
	    rattlerSetupPlanes(fb);
	    break;
	 case S9000_ID_A1659A:
	 case S9000_ID_ARTIST:
	 case CRT_ID_VISUALIZE_EG:
	    elkSetupPlanes(fb);
	    break;
	}

	/* Clear attribute planes on non HCRX devices. */
        switch (id) {
	 case S9000_ID_A1659A:
	 case S9000_ID_A1439A:
	    if (fb->info.var.bits_per_pixel == 32)
		ngleSetupAttrPlanes(fb, BUFF1_CMAP3);
	    else {
		ngleSetupAttrPlanes(fb, BUFF1_CMAP0);
	    }
	    if (id == S9000_ID_A1439A)
		ngleClearOverlayPlanes(fb, 0xff, 0);
	    break;
	 case S9000_ID_ARTIST:
	 case CRT_ID_VISUALIZE_EG:
	    if (fb->info.var.bits_per_pixel == 32)
		ngleSetupAttrPlanes(fb, BUFF1_CMAP3);
	    else {
		ngleSetupAttrPlanes(fb, ARTIST_CMAP0);
	    }
	    break;
	}
	stifb_blank(0, (struct fb_info *)fb);	/* 0=enable screen */

	SETUP_FB(fb);
}

/* ------------ Interfaces to hardware functions ------------ */

static struct fb_ops stifb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= stifb_read,
	.fb_write	= stifb_write,
	.fb_setcolreg	= stifb_setcolreg,
	.fb_blank	= stifb_blank,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};


/*
 *  Initialization
 */

int __init
stifb_init_fb(struct sti_struct *sti, int bpp_pref)
{
	struct fb_fix_screeninfo *fix;
	struct fb_var_screeninfo *var;
	struct stifb_info *fb;
	struct fb_info *info;
	unsigned long sti_rom_address;
	char *dev_name;
	int bpp, xres, yres;

	fb = kmalloc(sizeof(*fb), GFP_ATOMIC);
	if (!fb) {
		printk(KERN_ERR "stifb: Could not allocate stifb structure\n");
		return -ENODEV;
	}
	
	info = &fb->info;

	/* set struct to a known state */
	memset(fb, 0, sizeof(*fb));
	fix = &info->fix;
	var = &info->var;

	fb->sti = sti;
	/* store upper 32bits of the graphics id */
	fb->id = fb->sti->graphics_id[0];

	/* only supported cards are allowed */
	switch (fb->id) {
	case CRT_ID_VISUALIZE_EG:
		/* look for a double buffering device like e.g. the 
		   "INTERNAL_EG_DX1024" in the RDI precisionbook laptop
		   which won't work. The same device in non-double 
		   buffering mode returns "INTERNAL_EG_X1024". */
		if (strstr(sti->outptr.dev_name, "EG_DX")) {
		   printk(KERN_WARNING 
			"stifb: ignoring '%s'. Disable double buffering in IPL menu.\n",
			sti->outptr.dev_name);
		   goto out_err0;
		}
		/* fall though */
	case S9000_ID_ARTIST:
	case S9000_ID_HCRX:
	case S9000_ID_TIMBER:
	case S9000_ID_A1659A:
	case S9000_ID_A1439A:
		break;
	default:
		printk(KERN_WARNING "stifb: '%s' (id: 0x%08x) not supported.\n",
			sti->outptr.dev_name, fb->id);
		goto out_err0;
	}
	
	/* default to 8 bpp on most graphic chips */
	bpp = 8;
	xres = sti_onscreen_x(fb->sti);
	yres = sti_onscreen_y(fb->sti);

	ngleGetDeviceRomData(fb);

	/* get (virtual) io region base addr */
	fix->mmio_start = REGION_BASE(fb,2);
	fix->mmio_len   = 0x400000;

       	/* Reject any device not in the NGLE family */
	switch (fb->id) {
	case S9000_ID_A1659A:	/* CRX/A1659A */
		break;
	case S9000_ID_ELM:	/* GRX, grayscale but else same as A1659A */
		var->grayscale = 1;
		fb->id = S9000_ID_A1659A;
		break;
	case S9000_ID_TIMBER:	/* HP9000/710 Any (may be a grayscale device) */
		dev_name = fb->sti->outptr.dev_name;
		if (strstr(dev_name, "GRAYSCALE") || 
		    strstr(dev_name, "Grayscale") ||
		    strstr(dev_name, "grayscale"))
			var->grayscale = 1;
		break;
	case S9000_ID_TOMCAT:	/* Dual CRX, behaves else like a CRX */
		/* FIXME: TomCat supports two heads:
		 * fb.iobase = REGION_BASE(fb_info,3);
		 * fb.screen_base = (void*) REGION_BASE(fb_info,2);
		 * for now we only support the left one ! */
		xres = fb->ngle_rom.x_size_visible;
		yres = fb->ngle_rom.y_size_visible;
		fb->id = S9000_ID_A1659A;
		break;
	case S9000_ID_A1439A:	/* CRX24/A1439A */
		bpp = 32;
		break;
	case S9000_ID_HCRX:	/* Hyperdrive/HCRX */
		memset(&fb->ngle_rom, 0, sizeof(fb->ngle_rom));
		if ((fb->sti->regions_phys[0] & 0xfc000000) ==
		    (fb->sti->regions_phys[2] & 0xfc000000))
			sti_rom_address = F_EXTEND(fb->sti->regions_phys[0]);
		else
			sti_rom_address = F_EXTEND(fb->sti->regions_phys[1]);

		fb->deviceSpecificConfig = gsc_readl(sti_rom_address);
		if (IS_24_DEVICE(fb)) {
			if (bpp_pref == 8 || bpp_pref == 32)
				bpp = bpp_pref;
			else
				bpp = 32;
		} else
			bpp = 8;
		READ_WORD(fb, REG_15);
		SETUP_HW(fb);
		break;
	case CRT_ID_VISUALIZE_EG:
	case S9000_ID_ARTIST:	/* Artist */
		break;
	default: 
#ifdef FALLBACK_TO_1BPP
	       	printk(KERN_WARNING 
			"stifb: Unsupported graphics card (id=0x%08x) "
				"- now trying 1bpp mode instead\n",
			fb->id);
		bpp = 1;	/* default to 1 bpp */
		break;
#else
	       	printk(KERN_WARNING 
			"stifb: Unsupported graphics card (id=0x%08x) "
				"- skipping.\n",
			fb->id);
		goto out_err0;
#endif
	}


	/* get framebuffer physical and virtual base addr & len (64bit ready) */
	fix->smem_start = F_EXTEND(fb->sti->regions_phys[1]);
	fix->smem_len = fb->sti->regions[1].region_desc.length * 4096;

	fix->line_length = (fb->sti->glob_cfg->total_x * bpp) / 8;
	if (!fix->line_length)
		fix->line_length = 2048; /* default */
	
	/* limit fbsize to max visible screen size */
	if (fix->smem_len > yres*fix->line_length)
		fix->smem_len = yres*fix->line_length;
	
	fix->accel = FB_ACCEL_NONE;

	switch (bpp) {
	    case 1:
		fix->type = FB_TYPE_PLANES;	/* well, sort of */
		fix->visual = FB_VISUAL_MONO10;
		var->red.length = var->green.length = var->blue.length = 1;
		break;
	    case 8:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
		var->red.length = var->green.length = var->blue.length = 8;
		break;
	    case 32:
		fix->type = FB_TYPE_PACKED_PIXELS;
		fix->visual = FB_VISUAL_DIRECTCOLOR;
		var->red.length = var->green.length = var->blue.length = var->transp.length = 8;
		var->blue.offset = 0;
		var->green.offset = 8;
		var->red.offset = 16;
		var->transp.offset = 24;
		break;
	    default:
		break;
	}
	
	var->xres = var->xres_virtual = xres;
	var->yres = var->yres_virtual = yres;
	var->bits_per_pixel = bpp;

	strcpy(fix->id, "stifb");
	info->fbops = &stifb_ops;
	info->screen_base = (void*) REGION_BASE(fb,1);
	info->flags = FBINFO_DEFAULT;
	info->pseudo_palette = &fb->pseudo_palette;

	/* This has to been done !!! */
	fb_alloc_cmap(&info->cmap, NR_PALETTE, 0);
	stifb_init_display(fb);

	if (!request_mem_region(fix->smem_start, fix->smem_len, "stifb fb")) {
		printk(KERN_ERR "stifb: cannot reserve fb region 0x%04lx-0x%04lx\n",
				fix->smem_start, fix->smem_start+fix->smem_len);
		goto out_err1;
	}
		
	if (!request_mem_region(fix->mmio_start, fix->mmio_len, "stifb mmio")) {
		printk(KERN_ERR "stifb: cannot reserve sti mmio region 0x%04lx-0x%04lx\n",
				fix->mmio_start, fix->mmio_start+fix->mmio_len);
		goto out_err2;
	}

	if (register_framebuffer(&fb->info) < 0)
		goto out_err3;

	sti->info = info; /* save for unregister_framebuffer() */

	printk(KERN_INFO 
	    "fb%d: %s %dx%d-%d frame buffer device, %s, id: %04x, mmio: 0x%04lx\n",
		fb->info.node, 
		fix->id,
		var->xres, 
		var->yres,
		var->bits_per_pixel,
		sti->outptr.dev_name,
		fb->id, 
		fix->mmio_start);

	return 0;


out_err3:
	release_mem_region(fix->mmio_start, fix->mmio_len);
out_err2:
	release_mem_region(fix->smem_start, fix->smem_len);
out_err1:
	fb_dealloc_cmap(&info->cmap);
out_err0:
	kfree(fb);
	return -ENXIO;
}

static int stifb_disabled __initdata;

int __init
stifb_setup(char *options);

int __init
stifb_init(void)
{
	struct sti_struct *sti;
	struct sti_struct *def_sti;
	int i;
	
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("stifb", &option))
		return -ENODEV;
	stifb_setup(option);
#endif
	if (stifb_disabled) {
		printk(KERN_INFO "stifb: disabled by \"stifb=off\" kernel parameter\n");
		return -ENXIO;
	}
	
	def_sti = sti_get_rom(0);
	if (def_sti) {
		for (i = 1; i <= MAX_STI_ROMS; i++) {
			sti = sti_get_rom(i);
			if (!sti)
				break;
			if (sti == def_sti) {
				stifb_init_fb(sti, stifb_bpp_pref[i - 1]);
				break;
			}
		}
	}

	for (i = 1; i <= MAX_STI_ROMS; i++) {
		sti = sti_get_rom(i);
		if (!sti)
			break;
		if (sti == def_sti)
			continue;
		stifb_init_fb(sti, stifb_bpp_pref[i - 1]);
	}
	return 0;
}

/*
 *  Cleanup
 */

static void __exit
stifb_cleanup(void)
{
	struct sti_struct *sti;
	int i;
	
	for (i = 1; i <= MAX_STI_ROMS; i++) {
		sti = sti_get_rom(i);
		if (!sti)
			break;
		if (sti->info) {
			struct fb_info *info = sti->info;
			unregister_framebuffer(sti->info);
			release_mem_region(info->fix.mmio_start, info->fix.mmio_len);
		        release_mem_region(info->fix.smem_start, info->fix.smem_len);
		        fb_dealloc_cmap(&info->cmap);
		        kfree(info); 
		}
		sti->info = NULL;
	}
}

int __init
stifb_setup(char *options)
{
	int i;
	
	if (!options || !*options)
		return 0;
	
	if (strncmp(options, "off", 3) == 0) {
		stifb_disabled = 1;
		options += 3;
	}

	if (strncmp(options, "bpp", 3) == 0) {
		options += 3;
		for (i = 0; i < MAX_STI_ROMS; i++) {
			if (*options++ != ':')
				break;
			stifb_bpp_pref[i] = simple_strtoul(options, &options, 10);
		}
	}
	return 0;
}

__setup("stifb=", stifb_setup);

module_init(stifb_init);
module_exit(stifb_cleanup);

MODULE_AUTHOR("Helge Deller <deller@gmx.de>, Thomas Bogendoerfer <tsbogend@alpha.franken.de>");
MODULE_DESCRIPTION("Framebuffer driver for HP's NGLE series graphics cards in HP PARISC machines");
MODULE_LICENSE("GPL v2");
