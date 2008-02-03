/*
 * SiS 300/540/630[S]/730[S],
 * SiS 315[E|PRO]/550/[M]65x/[M]661[F|M]X/740/[M]741[GX]/330/[M]76x[GX],
 * XGI V3XT/V5/V8, Z7
 * frame buffer driver for Linux kernels >=2.4.14 and >=2.6.3
 *
 * Copyright (C) 2001-2005 Thomas Winischhofer, Vienna, Austria.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _SIS_H_
#define _SIS_H_

#include <linux/version.h>

#include "osdef.h"
#include <video/sisfb.h>

#include "vgatypes.h"
#include "vstruct.h"

#define VER_MAJOR		1
#define VER_MINOR		8
#define VER_LEVEL		9

#include <linux/spinlock.h>

#ifdef CONFIG_COMPAT
#define SIS_NEW_CONFIG_COMPAT
#endif	/* CONFIG_COMPAT */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
#define SIS_IOTYPE1 void __iomem
#define SIS_IOTYPE2 __iomem
#define SISINITSTATIC static
#else
#define SIS_IOTYPE1 unsigned char
#define SIS_IOTYPE2
#define SISINITSTATIC
#endif

#undef SISFBDEBUG

#ifdef SISFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#define TWDEBUG(x) printk(KERN_INFO x "\n");
#else
#define DPRINTK(fmt, args...)
#define TWDEBUG(x)
#endif

#define SISFAIL(x) do { printk(x "\n"); return -EINVAL; } while(0)

/* To be included in pci_ids.h */
#ifndef PCI_DEVICE_ID_SI_650_VGA
#define PCI_DEVICE_ID_SI_650_VGA	0x6325
#endif
#ifndef PCI_DEVICE_ID_SI_650
#define PCI_DEVICE_ID_SI_650		0x0650
#endif
#ifndef PCI_DEVICE_ID_SI_651
#define PCI_DEVICE_ID_SI_651		0x0651
#endif
#ifndef PCI_DEVICE_ID_SI_740
#define PCI_DEVICE_ID_SI_740		0x0740
#endif
#ifndef PCI_DEVICE_ID_SI_330
#define PCI_DEVICE_ID_SI_330		0x0330
#endif
#ifndef PCI_DEVICE_ID_SI_660_VGA
#define PCI_DEVICE_ID_SI_660_VGA	0x6330
#endif
#ifndef PCI_DEVICE_ID_SI_661
#define PCI_DEVICE_ID_SI_661		0x0661
#endif
#ifndef PCI_DEVICE_ID_SI_741
#define PCI_DEVICE_ID_SI_741		0x0741
#endif
#ifndef PCI_DEVICE_ID_SI_660
#define PCI_DEVICE_ID_SI_660		0x0660
#endif
#ifndef PCI_DEVICE_ID_SI_760
#define PCI_DEVICE_ID_SI_760		0x0760
#endif
#ifndef PCI_DEVICE_ID_SI_761
#define PCI_DEVICE_ID_SI_761		0x0761
#endif

#ifndef PCI_VENDOR_ID_XGI
#define PCI_VENDOR_ID_XGI		0x18ca
#endif

#ifndef PCI_DEVICE_ID_XGI_20
#define PCI_DEVICE_ID_XGI_20		0x0020
#endif

#ifndef PCI_DEVICE_ID_XGI_40
#define PCI_DEVICE_ID_XGI_40		0x0040
#endif

/* To be included in fb.h */
#ifndef FB_ACCEL_SIS_GLAMOUR_2
#define FB_ACCEL_SIS_GLAMOUR_2	40	/* SiS 315, 65x, 740, 661, 741  */
#endif
#ifndef FB_ACCEL_SIS_XABRE
#define FB_ACCEL_SIS_XABRE	41	/* SiS 330 ("Xabre"), 76x 	*/
#endif
#ifndef FB_ACCEL_XGI_VOLARI_V
#define FB_ACCEL_XGI_VOLARI_V	47	/* XGI Volari Vx (V3XT, V5, V8)	*/
#endif
#ifndef FB_ACCEL_XGI_VOLARI_Z
#define FB_ACCEL_XGI_VOLARI_Z	48	/* XGI Volari Z7		*/
#endif

/* ivideo->caps */
#define HW_CURSOR_CAP		0x80
#define TURBO_QUEUE_CAP		0x40
#define AGP_CMD_QUEUE_CAP	0x20
#define VM_CMD_QUEUE_CAP	0x10
#define MMIO_CMD_QUEUE_CAP	0x08

/* For 300 series */
#define TURBO_QUEUE_AREA_SIZE	(512 * 1024)	/* 512K */
#define HW_CURSOR_AREA_SIZE_300	4096		/* 4K */

/* For 315/Xabre series */
#define COMMAND_QUEUE_AREA_SIZE	(512 * 1024)	/* 512K */
#define COMMAND_QUEUE_AREA_SIZE_Z7 (128 * 1024)	/* 128k for XGI Z7 */
#define HW_CURSOR_AREA_SIZE_315	16384		/* 16K */
#define COMMAND_QUEUE_THRESHOLD	0x1F

#define SIS_OH_ALLOC_SIZE	4000
#define SENTINEL		0x7fffffff

#define SEQ_ADR			0x14
#define SEQ_DATA		0x15
#define DAC_ADR			0x18
#define DAC_DATA		0x19
#define CRTC_ADR		0x24
#define CRTC_DATA		0x25
#define DAC2_ADR		(0x16-0x30)
#define DAC2_DATA		(0x17-0x30)
#define VB_PART1_ADR		(0x04-0x30)
#define VB_PART1_DATA		(0x05-0x30)
#define VB_PART2_ADR		(0x10-0x30)
#define VB_PART2_DATA		(0x11-0x30)
#define VB_PART3_ADR		(0x12-0x30)
#define VB_PART3_DATA		(0x13-0x30)
#define VB_PART4_ADR		(0x14-0x30)
#define VB_PART4_DATA		(0x15-0x30)

#define SISSR			ivideo->SiS_Pr.SiS_P3c4
#define SISCR			ivideo->SiS_Pr.SiS_P3d4
#define SISDACA			ivideo->SiS_Pr.SiS_P3c8
#define SISDACD			ivideo->SiS_Pr.SiS_P3c9
#define SISPART1		ivideo->SiS_Pr.SiS_Part1Port
#define SISPART2		ivideo->SiS_Pr.SiS_Part2Port
#define SISPART3		ivideo->SiS_Pr.SiS_Part3Port
#define SISPART4		ivideo->SiS_Pr.SiS_Part4Port
#define SISPART5		ivideo->SiS_Pr.SiS_Part5Port
#define SISDAC2A		SISPART5
#define SISDAC2D		(SISPART5 + 1)
#define SISMISCR		(ivideo->SiS_Pr.RelIO + 0x1c)
#define SISMISCW		ivideo->SiS_Pr.SiS_P3c2
#define SISINPSTAT		(ivideo->SiS_Pr.RelIO + 0x2a)
#define SISPEL			ivideo->SiS_Pr.SiS_P3c6
#define SISVGAENABLE		(ivideo->SiS_Pr.RelIO + 0x13)
#define SISVID			(ivideo->SiS_Pr.RelIO + 0x02 - 0x30)
#define SISCAP			(ivideo->SiS_Pr.RelIO + 0x00 - 0x30)

#define IND_SIS_PASSWORD		0x05  /* SRs */
#define IND_SIS_COLOR_MODE		0x06
#define IND_SIS_RAMDAC_CONTROL		0x07
#define IND_SIS_DRAM_SIZE		0x14
#define IND_SIS_MODULE_ENABLE		0x1E
#define IND_SIS_PCI_ADDRESS_SET		0x20
#define IND_SIS_TURBOQUEUE_ADR		0x26
#define IND_SIS_TURBOQUEUE_SET		0x27
#define IND_SIS_POWER_ON_TRAP		0x38
#define IND_SIS_POWER_ON_TRAP2		0x39
#define IND_SIS_CMDQUEUE_SET		0x26
#define IND_SIS_CMDQUEUE_THRESHOLD	0x27

#define IND_SIS_AGP_IO_PAD	0x48

#define SIS_CRT2_WENABLE_300	0x24  /* Part1 */
#define SIS_CRT2_WENABLE_315	0x2F

#define SIS_PASSWORD		0x86  /* SR05 */

#define SIS_INTERLACED_MODE	0x20  /* SR06 */
#define SIS_8BPP_COLOR_MODE	0x0
#define SIS_15BPP_COLOR_MODE	0x1
#define SIS_16BPP_COLOR_MODE	0x2
#define SIS_32BPP_COLOR_MODE	0x4

#define SIS_ENABLE_2D		0x40  /* SR1E */

#define SIS_MEM_MAP_IO_ENABLE	0x01  /* SR20 */
#define SIS_PCI_ADDR_ENABLE	0x80

#define SIS_AGP_CMDQUEUE_ENABLE		0x80  /* 315/330/340 series SR26 */
#define SIS_VRAM_CMDQUEUE_ENABLE	0x40
#define SIS_MMIO_CMD_ENABLE		0x20
#define SIS_CMD_QUEUE_SIZE_512k		0x00
#define SIS_CMD_QUEUE_SIZE_1M		0x04
#define SIS_CMD_QUEUE_SIZE_2M		0x08
#define SIS_CMD_QUEUE_SIZE_4M		0x0C
#define SIS_CMD_QUEUE_RESET		0x01
#define SIS_CMD_AUTO_CORR		0x02

#define SIS_CMD_QUEUE_SIZE_Z7_64k	0x00 /* XGI Z7 */
#define SIS_CMD_QUEUE_SIZE_Z7_128k	0x04

#define SIS_SIMULTANEOUS_VIEW_ENABLE	0x01  /* CR30 */
#define SIS_MODE_SELECT_CRT2		0x02
#define SIS_VB_OUTPUT_COMPOSITE		0x04
#define SIS_VB_OUTPUT_SVIDEO		0x08
#define SIS_VB_OUTPUT_SCART		0x10
#define SIS_VB_OUTPUT_LCD		0x20
#define SIS_VB_OUTPUT_CRT2		0x40
#define SIS_VB_OUTPUT_HIVISION		0x80

#define SIS_VB_OUTPUT_DISABLE	0x20  /* CR31 */
#define SIS_DRIVER_MODE		0x40

#define SIS_VB_COMPOSITE	0x01  /* CR32 */
#define SIS_VB_SVIDEO		0x02
#define SIS_VB_SCART		0x04
#define SIS_VB_LCD		0x08
#define SIS_VB_CRT2		0x10
#define SIS_CRT1		0x20
#define SIS_VB_HIVISION		0x40
#define SIS_VB_YPBPR		0x80
#define SIS_VB_TV		(SIS_VB_COMPOSITE | SIS_VB_SVIDEO | \
				SIS_VB_SCART | SIS_VB_HIVISION | SIS_VB_YPBPR)

#define SIS_EXTERNAL_CHIP_MASK			0x0E  /* CR37 (< SiS 660) */
#define SIS_EXTERNAL_CHIP_SIS301		0x01  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_LVDS			0x02
#define SIS_EXTERNAL_CHIP_TRUMPION		0x03
#define SIS_EXTERNAL_CHIP_LVDS_CHRONTEL		0x04
#define SIS_EXTERNAL_CHIP_CHRONTEL		0x05
#define SIS310_EXTERNAL_CHIP_LVDS		0x02
#define SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL	0x03

#define SIS_AGP_2X		0x20  /* CR48 */

/* vbflags, private entries (others in sisfb.h) */
#define VB_CONEXANT		0x00000800	/* 661 series only */
#define VB_TRUMPION		VB_CONEXANT	/* 300 series only */
#define VB_302ELV		0x00004000
#define VB_301			0x00100000	/* Video bridge type */
#define VB_301B			0x00200000
#define VB_302B			0x00400000
#define VB_30xBDH		0x00800000	/* 30xB DH version (w/o LCD support) */
#define VB_LVDS			0x01000000
#define VB_CHRONTEL		0x02000000
#define VB_301LV		0x04000000
#define VB_302LV		0x08000000
#define VB_301C			0x10000000

#define VB_SISBRIDGE		(VB_301|VB_301B|VB_301C|VB_302B|VB_301LV|VB_302LV|VB_302ELV)
#define VB_VIDEOBRIDGE		(VB_SISBRIDGE | VB_LVDS | VB_CHRONTEL | VB_CONEXANT)

/* vbflags2 (static stuff only!) */
#define VB2_SISUMC		0x00000001
#define VB2_301			0x00000002	/* Video bridge type */
#define VB2_301B		0x00000004
#define VB2_301C		0x00000008
#define VB2_307T		0x00000010
#define VB2_302B		0x00000800
#define VB2_301LV		0x00001000
#define VB2_302LV		0x00002000
#define VB2_302ELV		0x00004000
#define VB2_307LV		0x00008000
#define VB2_30xBDH		0x08000000      /* 30xB DH version (w/o LCD support) */
#define VB2_CONEXANT		0x10000000
#define VB2_TRUMPION		0x20000000
#define VB2_LVDS		0x40000000
#define VB2_CHRONTEL		0x80000000

#define VB2_SISLVDSBRIDGE	(VB2_301LV | VB2_302LV | VB2_302ELV | VB2_307LV)
#define VB2_SISTMDSBRIDGE	(VB2_301   | VB2_301B  | VB2_301C   | VB2_302B | VB2_307T)
#define VB2_SISBRIDGE		(VB2_SISLVDSBRIDGE | VB2_SISTMDSBRIDGE)

#define VB2_SISTMDSLCDABRIDGE	(VB2_301C | VB2_307T)
#define VB2_SISLCDABRIDGE	(VB2_SISTMDSLCDABRIDGE | VB2_301LV | VB2_302LV | VB2_302ELV | VB2_307LV)

#define VB2_SISHIVISIONBRIDGE	(VB2_301  | VB2_301B | VB2_302B)
#define VB2_SISYPBPRBRIDGE	(VB2_301C | VB2_307T | VB2_SISLVDSBRIDGE)
#define VB2_SISYPBPRARBRIDGE	(VB2_301C | VB2_307T | VB2_307LV)
#define VB2_SISTAP4SCALER	(VB2_301C | VB2_307T | VB2_302ELV | VB2_307LV)
#define VB2_SISTVBRIDGE		(VB2_SISHIVISIONBRIDGE | VB2_SISYPBPRBRIDGE)

#define VB2_SISVGA2BRIDGE	(VB2_301 | VB2_301B | VB2_301C | VB2_302B | VB2_307T)

#define VB2_VIDEOBRIDGE		(VB2_SISBRIDGE | VB2_LVDS | VB2_CHRONTEL | VB2_CONEXANT)

#define VB2_30xB		(VB2_301B  | VB2_301C   | VB2_302B  | VB2_307T)
#define VB2_30xBLV		(VB2_30xB  | VB2_SISLVDSBRIDGE)
#define VB2_30xC		(VB2_301C  | VB2_307T)
#define VB2_30xCLV		(VB2_301C  | VB2_307T   | VB2_302ELV| VB2_307LV)
#define VB2_SISEMIBRIDGE	(VB2_302LV | VB2_302ELV | VB2_307LV)
#define VB2_LCD162MHZBRIDGE	(VB2_301C  | VB2_307T)
#define VB2_LCDOVER1280BRIDGE	(VB2_301C  | VB2_307T   | VB2_302LV | VB2_302ELV | VB2_307LV)
#define VB2_LCDOVER1600BRIDGE	(VB2_307T  | VB2_307LV)
#define VB2_RAMDAC202MHZBRIDGE	(VB2_301C  | VB2_307T)

/* I/O port access macros */
#define inSISREG(base)		inb(base)

#define outSISREG(base,val)	outb(val,base)

#define orSISREG(base,val)      			\
		do {					\
			u8 __Temp = inSISREG(base); 	\
			outSISREG(base, __Temp | (val));\
		} while (0)

#define andSISREG(base,val)     			\
		do {					\
			u8 __Temp = inSISREG(base); 	\
			outSISREG(base, __Temp & (val));\
		} while (0)

#define inSISIDXREG(base,idx,var)			\
		do {					\
			outSISREG(base, idx); 		\
			var = inSISREG((base)+1);	\
		} while (0)

#define outSISIDXREG(base,idx,val)			\
		do {					\
			outSISREG(base, idx);		\
			outSISREG((base)+1, val);	\
		} while (0)

#define orSISIDXREG(base,idx,val)				\
		do {						\
			u8 __Temp; 				\
			outSISREG(base, idx);   		\
			__Temp = inSISREG((base)+1) | (val); 	\
			outSISREG((base)+1, __Temp);		\
		} while (0)

#define andSISIDXREG(base,idx,and)				\
		do {						\
			u8 __Temp; 				\
			outSISREG(base, idx);   		\
			__Temp = inSISREG((base)+1) & (and); 	\
			outSISREG((base)+1, __Temp);		\
		} while (0)

#define setSISIDXREG(base,idx,and,or)   				\
		do {							\
			u8 __Temp; 					\
			outSISREG(base, idx);				\
			__Temp = (inSISREG((base)+1) & (and)) | (or); 	\
			outSISREG((base)+1, __Temp);			\
		} while (0)

/* MMIO access macros */
#define MMIO_IN8(base, offset)  readb((base+offset))
#define MMIO_IN16(base, offset) readw((base+offset))
#define MMIO_IN32(base, offset) readl((base+offset))

#define MMIO_OUT8(base, offset, val)  writeb(((u8)(val)), (base+offset))
#define MMIO_OUT16(base, offset, val) writew(((u16)(val)), (base+offset))
#define MMIO_OUT32(base, offset, val) writel(((u32)(val)), (base+offset))

/* Queue control MMIO registers */
#define Q_BASE_ADDR		0x85C0  /* Base address of software queue */
#define Q_WRITE_PTR		0x85C4  /* Current write pointer */
#define Q_READ_PTR		0x85C8  /* Current read pointer */
#define Q_STATUS		0x85CC  /* queue status */

#define MMIO_QUEUE_PHYBASE      Q_BASE_ADDR
#define MMIO_QUEUE_WRITEPORT    Q_WRITE_PTR
#define MMIO_QUEUE_READPORT     Q_READ_PTR

#ifndef FB_BLANK_UNBLANK
#define FB_BLANK_UNBLANK	0
#endif
#ifndef FB_BLANK_NORMAL
#define FB_BLANK_NORMAL		1
#endif
#ifndef FB_BLANK_VSYNC_SUSPEND
#define FB_BLANK_VSYNC_SUSPEND	2
#endif
#ifndef FB_BLANK_HSYNC_SUSPEND
#define FB_BLANK_HSYNC_SUSPEND	3
#endif
#ifndef FB_BLANK_POWERDOWN
#define FB_BLANK_POWERDOWN	4
#endif

enum _SIS_LCD_TYPE {
    LCD_INVALID = 0,
    LCD_800x600,
    LCD_1024x768,
    LCD_1280x1024,
    LCD_1280x960,
    LCD_640x480,
    LCD_1600x1200,
    LCD_1920x1440,
    LCD_2048x1536,
    LCD_320x240,	/* FSTN */
    LCD_1400x1050,
    LCD_1152x864,
    LCD_1152x768,
    LCD_1280x768,
    LCD_1024x600,
    LCD_320x240_2,	/* DSTN */
    LCD_320x240_3,	/* DSTN */
    LCD_848x480,
    LCD_1280x800,
    LCD_1680x1050,
    LCD_1280x720,
    LCD_1280x854,
    LCD_CUSTOM,
    LCD_UNKNOWN
};

enum _SIS_CMDTYPE {
    MMIO_CMD = 0,
    AGP_CMD_QUEUE,
    VM_CMD_QUEUE,
};

struct SIS_OH {
	struct SIS_OH *poh_next;
	struct SIS_OH *poh_prev;
	u32            offset;
	u32            size;
};

struct SIS_OHALLOC {
	struct SIS_OHALLOC *poha_next;
	struct SIS_OH aoh[1];
};

struct SIS_HEAP {
	struct SIS_OH	oh_free;
	struct SIS_OH	oh_used;
	struct SIS_OH	*poh_freelist;
	struct SIS_OHALLOC *poha_chain;
	u32		max_freesize;
	struct sis_video_info *vinfo;
};

/* Our "par" */
struct sis_video_info {
	int		cardnumber;
	struct fb_info  *memyselfandi;

	struct SiS_Private SiS_Pr;

	struct sisfb_info sisfbinfo;	/* For ioctl SISFB_GET_INFO */

	struct fb_var_screeninfo default_var;

	struct fb_fix_screeninfo sisfb_fix;
	u32		pseudo_palette[16];

	struct sisfb_monitor {
		u16 hmin;
		u16 hmax;
		u16 vmin;
		u16 vmax;
		u32 dclockmax;
		u8  feature;
		bool datavalid;
	}		sisfb_thismonitor;

	unsigned short	chip_id;	/* PCI ID of chip */
	unsigned short	chip_vendor;	/* PCI ID of vendor */
	char		myid[40];

	struct pci_dev  *nbridge;
	struct pci_dev  *lpcdev;

	int		mni;	/* Mode number index */

	unsigned long	video_size;
	unsigned long	video_base;
	unsigned long	mmio_size;
	unsigned long	mmio_base;
	unsigned long	vga_base;

	unsigned long	video_offset;

	unsigned long	UMAsize, LFBsize;

	SIS_IOTYPE1	*video_vbase;
	SIS_IOTYPE1	*mmio_vbase;

	unsigned char	*bios_abase;

	int		mtrr;

	u32		sisfb_mem;

	u32		sisfb_parm_mem;
	int		sisfb_accel;
	int		sisfb_ypan;
	int		sisfb_max;
	int		sisfb_userom;
	int		sisfb_useoem;
	int		sisfb_mode_idx;
	int		sisfb_parm_rate;
	int		sisfb_crt1off;
	int		sisfb_forcecrt1;
	int		sisfb_crt2type;
	int		sisfb_crt2flags;
	int		sisfb_dstn;
	int		sisfb_fstn;
	int		sisfb_tvplug;
	int		sisfb_tvstd;
	int		sisfb_nocrt2rate;

	u32		heapstart;		/* offset  */
	SIS_IOTYPE1	*sisfb_heap_start;	/* address */
	SIS_IOTYPE1	*sisfb_heap_end;	/* address */
	u32		sisfb_heap_size;
	int		havenoheap;

	struct SIS_HEAP	sisfb_heap;		/* This card's vram heap */

	int		video_bpp;
	int		video_cmap_len;
	int		video_width;
	int		video_height;
	unsigned int	refresh_rate;

	unsigned int	chip;
	u8		revision_id;
	int		sisvga_enabled;		/* PCI device was enabled */

	int		video_linelength;	/* real pitch */
	int		scrnpitchCRT1;		/* pitch regarding interlace */

	u16		DstColor;		/* For 2d acceleration */
	u32		SiS310_AccelDepth;
	u32		CommandReg;
	int		cmdqueuelength;		/* Current (for accel) */
	u32		cmdQueueSize;		/* Total size in KB */

	spinlock_t	lockaccel;		/* Do not use outside of kernel! */

	unsigned int	pcibus;
	unsigned int	pcislot;
	unsigned int	pcifunc;

	int		accel;
	int		engineok;

	u16		subsysvendor;
	u16		subsysdevice;

	u32		vbflags;		/* Replacing deprecated stuff from above */
	u32		currentvbflags;
	u32		vbflags2;

	int		lcdxres, lcdyres;
	int		lcddefmodeidx, tvdefmodeidx, defmodeidx;
	u32		CRT2LCDType;		/* defined in "SIS_LCD_TYPE" */
	u32		curFSTN, curDSTN;

	int		current_bpp;
	int		current_width;
	int		current_height;
	int		current_htotal;
	int		current_vtotal;
	int		current_linelength;
	__u32		current_pixclock;
	int		current_refresh_rate;

	unsigned int	current_base;

	u8		mode_no;
	u8		rate_idx;
	int		modechanged;
	unsigned char	modeprechange;

	u8		sisfb_lastrates[128];

	int		newrom;
	int		haveXGIROM;
	int		registered;
	int		warncount;

	int		sisvga_engine;
	int		hwcursor_size;
	int		CRT2_write_enable;
	u8		caps;

	u8		detectedpdc;
	u8		detectedpdca;
	u8		detectedlcda;

	SIS_IOTYPE1	*hwcursor_vbase;

	int		chronteltype;
	int		tvxpos, tvypos;
	u8		p2_1f,p2_20,p2_2b,p2_42,p2_43,p2_01,p2_02;
	int		tvx, tvy;

	u8		sisfblocked;

	struct sisfb_info sisfb_infoblock;

	struct sisfb_cmd sisfb_command;

	u32		sisfb_id;

	u8		sisfb_can_post;
	u8		sisfb_card_posted;
	u8		sisfb_was_boot_device;

	struct sis_video_info *next;
};

#endif
