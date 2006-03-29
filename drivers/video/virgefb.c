/*
 * linux/drivers/video/virgefb.c -- CyberVision64/3D frame buffer device
 *
 *    Copyright (C) 1997 André Heynatz
 *
 *
 * This file is based on the CyberVision frame buffer device (cyberfb.c):
 *
 *    Copyright (C) 1996 Martin Apel
 *                       Geert Uytterhoeven
 *
 * Zorro II additions :
 *
 *    Copyright (C) 1998-2000 Christian T. Steigies
 *
 * Initialization additions :
 *
 *    Copyright (C) 1998-2000 Ken Tyler
 *
 * Parts of the Initialization code are based on Cyberfb.c by Allan Bair,
 * and on the NetBSD CyberVision64 frame buffer driver by Michael Teske who gave
 * permission for its use.
 *
 * Many thanks to Frank Mariak for his assistance with ZORRO 2 access and other
 * mysteries.
 *
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#undef VIRGEFBDEBUG
#undef VIRGEFBDUMP

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/zorro.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/amigahw.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>

#include "virgefb.h"

#ifdef VIRGEFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#ifdef VIRGEFBDUMP
static void cv64_dump(void);
#define DUMP cv64_dump()
#else
#define DUMP
#endif

/*
 *	Macros for register access and zorro control
 */

static inline void mb_inline(void) { mb(); }	/* for use in comma expressions */

/* Set zorro 2 map */

#define SelectIO \
	mb(); \
	if (on_zorro2) { \
		(*(volatile u16 *)((u8 *)(vcode_switch_base + 0x04)) = 0x01); \
		mb(); \
	}

#define	SelectMMIO \
	mb(); \
	if (on_zorro2) { \
		(*(volatile u16 *)((u8 *)(vcode_switch_base + 0x04)) = 0x02); \
		mb(); \
	}

#define	SelectCFG \
	mb(); \
	if (on_zorro2) { \
		(*(volatile u16 *)((u8 *)(vcode_switch_base + 0x04)) = 0x03); \
		mb(); \
	}

/* Set pass through, 0 = amiga, !=0 = cv64/3d */

#define SetVSwitch(x) \
	mb(); \
	(*(volatile u16 *)((u8 *)(vcode_switch_base)) = \
	(u16)(x ? 0 : 1)); \
	mb();

/* Zorro2 endian 'aperture' */

#define ENDIAN_BYTE	2
#define ENDIAN_WORD	1
#define ENDIAN_LONG	0

#define Select_Zorro2_FrameBuffer(x) \
	do { \
		if (on_zorro2) { \
			mb(); \
			(*(volatile u16 *)((u8 *)(vcode_switch_base + 0x08)) = \
			(x * 0x40)); \
			mb(); \
		} \
	} while (0)

/* SetPortVal - only used for interrupt enable (not yet implemented) */

#if 0
#define SetPortVal(x) \
	mb(); \
	(*(volatile u16 *)((u8 *)(vcode_switch_base + 0x0c)) = \
	(u16)x); \
	mb();
#endif

/* IO access */

#define byte_access_io(x)	(((x) & 0x3ffc) | (((x) & 3)^3) | (((x) & 3) <<14))
#define byte_access_mmio(x)	(((x) & 0xfffc) | (((x) & 3)^3))

/* Write 8 bit VGA register - used once for chip wakeup */

#define wb_vgaio(reg, dat) \
	SelectIO; \
	(*(volatile u8 *)(vgaio_regs + ((u32)byte_access_io(reg) & 0xffff)) = \
	(dat & 0xff)); \
	SelectMMIO;

/* Read 8 bit VGA register - only used in dump (SelectIO not needed on read ?) */

#ifdef VIRGEFBDUMP
#define rb_vgaio(reg) \
	({ \
	u8 __zzyzx; \
	SelectIO; \
	__zzyzx = (*(volatile u8 *)((vgaio_regs)+(u32)byte_access_io(reg))); \
	SelectMMIO; \
	__zzyzx; \
	})
#endif

/* MMIO access */

/* Read 8 bit MMIO register */

#define rb_mmio(reg) \
	(mb_inline(), \
	(*(volatile u8 *)(mmio_regs + 0x8000 + (u32)byte_access_mmio(reg))))

/* Write 8 bit MMIO register */

#define wb_mmio(reg,dat) \
	mb(); \
	(*(volatile u8 *)(mmio_regs + 0x8000 + (byte_access_mmio((reg) & 0xffff))) = \
	(dat & 0xff)); \
	mb();

/* Read 32 bit MMIO register */

#define rl_mmio(reg) \
	(mb_inline(), \
	(*((volatile u32 *)((u8 *)((mmio_regs + (on_zorro2 ? 0x20000 : 0)) + (reg))))))

/* Write 32 bit MMIO register */

#define wl_mmio(reg,dat) \
	mb(); \
	((*(volatile u32 *)((u8 *)((mmio_regs + (on_zorro2 ? 0x20000 : 0)) + (reg)))) = \
	(u32)(dat)); \
	mb();

/* Write to virge graphics register */

#define wgfx(reg, dat)	do { wb_mmio(GCT_ADDRESS, (reg)); wb_mmio(GCT_ADDRESS_W, (dat)); } while (0)

/* Write to virge sequencer register */

#define wseq(reg, dat)	do { wb_mmio(SEQ_ADDRESS, (reg)); wb_mmio(SEQ_ADDRESS_W, (dat)); } while (0)

/* Write to virge CRT controller register */

#define wcrt(reg, dat)	do { wb_mmio(CRT_ADDRESS, (reg)); wb_mmio(CRT_ADDRESS_W, (dat)); } while (0)

/* Write to virge attribute register */

#define watr(reg, dat) \
	do { \
		volatile unsigned char watr_tmp; \
		watr_tmp = rb_mmio(ACT_ADDRESS_RESET); \
		wb_mmio(ACT_ADDRESS_W, (reg)); \
		wb_mmio(ACT_ADDRESS_W, (dat)); \
		udelay(10); \
	} while (0)

/* end of macros */

struct virgefb_par {
   struct fb_var_screeninfo var;
   __u32 type;
   __u32 type_aux;
   __u32 visual;
   __u32 line_length;
};

static struct virgefb_par current_par;

static int current_par_valid = 0;

static struct display disp;
static struct fb_info fb_info;

static union {
#ifdef FBCON_HAS_CFB16
    u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
    u32 cfb32[16];
#endif
} fbcon_cmap;

/*
 *    Switch for Chipset Independency
 */

static struct fb_hwswitch {

   /* Initialisation */

   int (*init)(void);

   /* Display Control */

   int (*encode_fix)(struct fb_fix_screeninfo *fix, struct virgefb_par *par);
   int (*decode_var)(struct fb_var_screeninfo *var, struct virgefb_par *par);
   int (*encode_var)(struct fb_var_screeninfo *var, struct virgefb_par *par);
   int (*getcolreg)(u_int regno, u_int *red, u_int *green, u_int *blue,
                    u_int *transp, struct fb_info *info);
   void (*blank)(int blank);
} *fbhw;

static unsigned char blit_maybe_busy = 0;

/*
 *    Frame Buffer Name
 */

static char virgefb_name[16] = "CyberVision/3D";

/*
 *    CyberVision64/3d Graphics Board
 */

static unsigned char virgefb_colour_table [256][3];
static unsigned long v_ram;
static unsigned long v_ram_size;
static volatile unsigned char *mmio_regs;
static volatile unsigned char *vgaio_regs;

static unsigned long v_ram_phys;
static unsigned long mmio_regs_phys;
static unsigned long vcode_switch_base;
static unsigned char on_zorro2;

/*
 * Offsets from start of video ram to appropriate ZIII aperture
 */

#ifdef FBCON_HAS_CFB8
#define CYBMEM_OFFSET_8  0x800000	/* BGRX */
#endif
#ifdef FBCON_HAS_CFB16
#define CYBMEM_OFFSET_16 0x400000	/* GBXR */
#endif
#ifdef FBCON_HAS_CFB32
#define CYBMEM_OFFSET_32 0x000000	/* XRGB */
#endif

/*
 *    MEMCLOCK was 32MHz, 64MHz works, 72MHz doesn't (on my board)
 */

#define MEMCLOCK 50000000

/*
 *    Predefined Video Modes
 */

static struct {
    const char *name;
    struct fb_var_screeninfo var;
} virgefb_predefined[] __initdata = {
#ifdef FBCON_HAS_CFB8
    {
	"640x480-8", {		/* Cybervision 8 bpp */
	    640, 480, 640, 480, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 31250, 160, 136, 82, 61, 88, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"768x576-8", {		/* Cybervision 8 bpp */
	    768, 576, 768, 576, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 29411, 144, 112, 32, 15, 64, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"800x600-8", {		/* Cybervision 8 bpp */
	    800, 600, 800, 600, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 28571, 168, 104, 22, 1, 48, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
  #if 0 
	"1024x768-8", {		/* Cybervision 8 bpp */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 20833, 272, 168, 39, 2, 72, 1,
	    0, FB_VMODE_NONINTERLACED
	    }
  #else
	"1024x768-8", {
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
   #if 0
	    0, 0, -1, -1, FB_ACCELF_TEXT, 12500, 184, 40, 40, 2, 96, 1,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
    #else
	    0, 0, -1, -1, FB_ACCELF_TEXT, 12699, 176, 16, 28, 1, 96, 3,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
    #endif
  #endif
    }, {
	"1152x886-8", {		/* Cybervision 8 bpp */
	    1152, 886, 1152, 886, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 19230, 280, 168, 45, 1, 64, 10,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1280x1024-8", { 	/* Cybervision 8 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
  #if 0
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    }
  #else
	    0, 0, -1, -1, FB_ACCELF_TEXT, 7414, 232, 64, 38, 1, 112, 3,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
  #endif
    }, {
	"1600x1200-8", { 	/* Cybervision 8 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
  #if 0
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_NONINTERLACED
	    }
  #else
	    0, 0, -1, -1, FB_ACCELF_TEXT, 6411, 256, 32, 52, 10, 160, 8,
	    FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
	    }
  #endif
    },
#endif

#ifdef FBCON_HAS_CFB16
    {
	"640x480-16", {		/* Cybervision 16 bpp */
	    640, 480, 640, 480, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 31250, 152, 144, 82, 61, 88, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"768x576-16", {		/* Cybervision 16 bpp */
	    768, 576, 768, 576, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 29411, 144, 112, 32, 15, 64, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"800x600-16", {		/* Cybervision 16 bpp */
	    800, 600, 800, 600, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 28571, 168, 104, 22, 1, 48, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
#if 0
	"1024x768-16", { 	/* Cybervision 16 bpp */
	    1024, 768, 1024, 768, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 20833, 272, 168, 39, 2, 72, 1,
	    0, FB_VMODE_NONINTERLACED
	    }
#else
         "1024x768-16", {
             1024, 768, 1024, 768, 0, 0, 16, 0,
             {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
             0, 0, -1, -1, FB_ACCELF_TEXT, 12500, 184, 40, 40, 2, 96, 1,
             FB_SYNC_COMP_HIGH_ACT|FB_SYNC_VERT_HIGH_ACT, FB_VMODE_NONINTERLACED
         }
#endif
    }, {
	"1152x886-16", { 	/* Cybervision 16 bpp */
	    1152, 886, 1152, 886, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 19230, 280, 168, 45, 1, 64, 10,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1280x1024-16", { 	/* Cybervision 16 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1600x1200-16", { 	/* Cybervision 16 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_NONINTERLACED
	    }
    },
#endif

#ifdef FBCON_HAS_CFB32
    {
	"640x480-32", {		/* Cybervision 32 bpp */
	    640, 480, 640, 480, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 31250, 160, 136, 82, 61, 88, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
     }, {
	"768x576-32", {		/* Cybervision 32 bpp */
	    768, 576, 768, 576, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 29411, 144, 112, 32, 15, 64, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
     }, {
	"800x600-32", {		/* Cybervision 32 bpp */
	    800, 600, 800, 600, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
  	    0, 0, -1, -1, FB_ACCELF_TEXT, 28571, 168, 104, 22, 1, 48, 2,
	    0, FB_VMODE_NONINTERLACED
	    }
     }, {
	"1024x768-32", {	/* Cybervision 32 bpp */
	    1024, 768, 1024, 768, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 20833, 272, 168, 39, 2, 72, 1,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1152x886-32", {	/* Cybervision 32 bpp */
	    1152, 886, 1152, 886, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 19230, 280, 168, 45, 1, 64, 10,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1280x1024-32", {	/* Cybervision 32 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    0, FB_VMODE_NONINTERLACED
	    }
    }, {
	"1600x1200-32", {	/* Cybervision 32 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_NONINTERLACED
	    }
    },
#endif

/* interlaced modes */

#ifdef FBCON_HAS_CFB8
    {
	"1024x768-8i", {	/* Cybervision 8 bpp */
	    1024, 768, 1024, 768, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 20833, 272, 168, 39, 2, 72, 1,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1280x1024-8i", {	/* Cybervision 8 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1600x1200-8i", {	/* Cybervision 8 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    },
#endif

#ifdef FBCON_HAS_CFB16
    {
	"1024x768-16i", {	/* Cybervision 16 bpp */
	    1024, 768, 1024, 768, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 20833, 272, 168, 39, 2, 72, 1,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1280x1024-16i", {	/* Cybervision 16 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1600x1200-16i", {	/* Cybervision 16 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    },
#endif

#ifdef FBCON_HAS_CFB32
    {
	"1024x768-32i", {	/* Cybervision 32 bpp */
	    1024, 768, 1024, 768, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 22222, 216, 144, 39, 2, 72, 1,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1280x1024-32i", {	/* Cybervision 32 bpp */
	    1280, 1024, 1280, 1024, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {23, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 17857, 232, 232, 71, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    }, {
	"1600x1200-32i", {	/* Cybervision 32 bpp */
	    1600, 1200, 1600, 1200, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 13698, 336, 224, 77, 15, 176, 12,
	    0, FB_VMODE_INTERLACED
	    }
    },
#endif

/* doublescan modes */

#ifdef FBCON_HAS_CFB8
    {
	"320x240-8d", {		/* Cybervision 8 bpp */
	    320, 240, 320, 240, 0, 0, 8, 0,
	    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 59259, 80, 80, 45, 26, 32, 1,
	    0, FB_VMODE_DOUBLE
	    }
    },
#endif

#ifdef FBCON_HAS_CFB16
    {
	"320x240-16d", {	/* Cybervision 16 bpp */
	    320, 240, 320, 240, 0, 0, 16, 0,
	    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 59259, 80, 80, 45, 26, 32, 1,
	    0, FB_VMODE_DOUBLE
	    }
    },
#endif

#ifdef FBCON_HAS_CFB32
    {
	"320x240-32d", {	/* Cybervision 32 bpp */
	    320, 240, 320, 240, 0, 0, 32, 0,
	    {16, 8, 0}, {8, 8, 0}, {0, 8, 0}, {24, 0, 0},
	    0, 0, -1, -1, FB_ACCELF_TEXT, 59259, 80, 80, 45, 26, 32, 1,
	    0, FB_VMODE_DOUBLE
	    }
    },
#endif
};

#define NUM_TOTAL_MODES	ARRAY_SIZE(virgefb_predefined)

/*
 *    Default to 800x600 for video=virge8:, virge16: or virge32:
 */

#ifdef FBCON_HAS_CFB8
#define VIRGE8_DEFMODE	(2)
#endif

#ifdef FBCON_HAS_CFB16
#define VIRGE16_DEFMODE	(9)
#endif

#ifdef FBCON_HAS_CFB32
#define VIRGE32_DEFMODE	(16)
#endif

static struct fb_var_screeninfo virgefb_default;
static int virgefb_inverse = 0;

/*
 *    Interface used by the world
 */

int virgefb_setup(char*);
static int virgefb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info);
static int virgefb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int virgefb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int virgefb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info);
static int virgefb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info);
static int virgefb_blank(int blank, struct fb_info *info);

/*
 *    Interface to the low level console driver
 */

int virgefb_init(void);
static int virgefb_switch(int con, struct fb_info *info);
static int virgefb_updatevar(int con, struct fb_info *info);

/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_virge8;
#endif

#ifdef FBCON_HAS_CFB16
static struct display_switch fbcon_virge16;
#endif

#ifdef FBCON_HAS_CFB32
static struct display_switch fbcon_virge32;
#endif

/*
 *   Hardware Specific Routines
 */

static int virge_init(void);
static int virgefb_encode_fix(struct fb_fix_screeninfo *fix,
				struct virgefb_par *par);
static int virgefb_decode_var(struct fb_var_screeninfo *var,
				struct virgefb_par *par);
static int virgefb_encode_var(struct fb_var_screeninfo *var,
				struct virgefb_par *par);
static int virgefb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
				u_int *transp, struct fb_info *info);
static void virgefb_gfx_on_off(int blank);
static inline void virgefb_wait_for_idle(void);
static void virgefb_BitBLT(u_short curx, u_short cury, u_short destx, u_short desty,
		u_short width, u_short height, u_short stride, u_short depth);
static void virgefb_RectFill(u_short x, u_short y, u_short width, u_short height,
		u_short color, u_short stride, u_short depth);

/*
 *    Internal routines
 */

static void virgefb_get_par(struct virgefb_par *par);
static void virgefb_set_par(struct virgefb_par *par);
static int virgefb_do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
static void virgefb_set_disp(int con, struct fb_info *info);
static int virgefb_get_video_mode(const char *name);
static void virgefb_set_video(struct fb_var_screeninfo *var);

/*
 *    Additions for Initialization
 */

static void virgefb_load_video_mode(struct fb_var_screeninfo *video_mode);
static int cv3d_has_4mb(void);
static unsigned short virgefb_compute_clock(unsigned long freq);
static inline unsigned char rattr(short);
static inline unsigned char rseq(short);
static inline unsigned char rcrt(short);
static inline unsigned char rgfx(short);
static inline void gfx_on_off(int toggle);
static void virgefb_pci_init(void);

/* -------------------- Hardware specific routines ------------------------- */

/*
 *	Functions for register access
 */

/* Read attribute controller register */

static inline unsigned char rattr(short idx)
{
	volatile unsigned char rattr_tmp;

	rattr_tmp = rb_mmio(ACT_ADDRESS_RESET);
	wb_mmio(ACT_ADDRESS_W, idx);
	return (rb_mmio(ACT_ADDRESS_R));
}

/* Read sequencer register */

static inline unsigned char rseq(short idx)
{
	wb_mmio(SEQ_ADDRESS, idx);
	return (rb_mmio(SEQ_ADDRESS_R));
}

/* Read CRT controller register */

static inline unsigned char rcrt(short idx)
{
	wb_mmio(CRT_ADDRESS, idx);
	return (rb_mmio(CRT_ADDRESS_R));
}

/* Read graphics controller register */

static inline unsigned char rgfx(short idx)
{
	wb_mmio(GCT_ADDRESS, idx);
	return (rb_mmio(GCT_ADDRESS_R));
}


/*
 *	Initialization
 */

/* PCI init */

void virgefb_pci_init(void) {

	DPRINTK("ENTER\n");

	SelectCFG;

	if (on_zorro2) {
		*((short *)(vgaio_regs + 0x00000010)) = 0;
		*((long  *)(vgaio_regs + 0x00000004)) = 0x02000003;
	} else {
		*((short *)(vgaio_regs + 0x000e0010)) = 0;
		*((long  *)(vgaio_regs + 0x000e0004)) = 0x02000003;
	}

	/* SelectIO is in wb_vgaio macro */
	wb_vgaio(SREG_VIDEO_SUBS_ENABLE, 0x01);
	/* SelectMMIO is in wb_vgaio macro */

	DPRINTK("EXIT\n");

	return;
}

/* 
 * Initalize all mode independent regs, find mem size and clear mem
*/

static int virge_init(void)
{
	int i;
	unsigned char tmp;

	DPRINTK("ENTER\n");

	virgefb_pci_init();

	wb_mmio(GREG_MISC_OUTPUT_W, 0x07);	/* colour, ram enable, clk sel */

	wseq(SEQ_ID_UNLOCK_EXT, 0x06);		/* unlock extensions */
	tmp = rb_mmio(GREG_MISC_OUTPUT_R);
	wcrt(CRT_ID_REGISTER_LOCK_1, 0x48);	/* unlock CR2D to CR3F */

	wcrt(CRT_ID_BACKWAD_COMP_1, 0x00);	/* irq disable */

	wcrt(CRT_ID_REGISTER_LOCK_2, 0xa5);	/* unlock CR40 to CRFF and more */
	wcrt(CRT_ID_REGISTER_LOCK,0x00);	/* unlock h and v timing */
	wcrt(CRT_ID_SYSTEM_CONFIG, 0x01);	/* unlock enhanced programming registers */

	wb_mmio(GREG_FEATURE_CONTROL_W, 0x00);

	wcrt(CRT_ID_EXT_MISC_CNTL, 0x00);	/* b2 = 0 to allow VDAC mmio access */
#if 0
	/* write strap options ... ? */
	wcrt(CRT_ID_CONFIG_1, 0x08);
	wcrt(CRT_ID_CONFIG_2, 0xff);		/* 0x0x2 bit needs to be set ?? */
	wcrt(CRT_ID_CONFIG_3, 0x0f);
	wcrt(CRT_ID_CONFIG_4, 0x1a);
#endif
	wcrt(CRT_ID_EXT_MISC_CNTL_1, 0x82);	 /* PCI DE and software reset S3D engine */
	/* EXT_MISC_CNTL_1, CR66 bit 0 should be the same as bit 0 MR_ADVANCED_FUNCTION_CONTROL - check */
	wl_mmio(MR_ADVANCED_FUNCTION_CONTROL, 0x00000011); /* enhanced mode, linear addressing */

/* crtc registers */

	wcrt(CRT_ID_PRESET_ROW_SCAN, 0x00);

	/* Disable h/w cursor */

	wcrt(CRT_ID_CURSOR_START, 0x00);
	wcrt(CRT_ID_CURSOR_END, 0x00);
	wcrt(CRT_ID_START_ADDR_HIGH, 0x00);
	wcrt(CRT_ID_START_ADDR_LOW, 0x00);
	wcrt(CRT_ID_CURSOR_LOC_HIGH, 0x00);
	wcrt(CRT_ID_CURSOR_LOC_LOW, 0x00);
	wcrt(CRT_ID_EXT_MODE, 0x00);
	wcrt(CRT_ID_HWGC_MODE, 0x00);
	wcrt(CRT_ID_HWGC_ORIGIN_X_HI, 0x00);
	wcrt(CRT_ID_HWGC_ORIGIN_X_LO, 0x00);
	wcrt(CRT_ID_HWGC_ORIGIN_Y_HI, 0x00);
	wcrt(CRT_ID_HWGC_ORIGIN_Y_LO, 0x00);
	i = rcrt(CRT_ID_HWGC_MODE);
	wcrt(CRT_ID_HWGC_FG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_FG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_FG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_BG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_BG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_BG_STACK, 0x00);
	wcrt(CRT_ID_HWGC_START_AD_HI, 0x00);
	wcrt(CRT_ID_HWGC_START_AD_LO, 0x00);
	wcrt(CRT_ID_HWGC_DSTART_X, 0x00);
	wcrt(CRT_ID_HWGC_DSTART_Y, 0x00);

	wcrt(CRT_ID_UNDERLINE_LOC, 0x00);

	wcrt(CRT_ID_MODE_CONTROL, 0xe3);
	wcrt(CRT_ID_BACKWAD_COMP_2, 0x22);	/* blank bdr bit 5 blanking only on 8 bit */

	wcrt(CRT_ID_EX_SYNC_1, 0x00);

	/* memory */

	wcrt(CRT_ID_EXT_SYS_CNTL_3, 0x00);
	wcrt(CRT_ID_MEMORY_CONF, 0x08);		/* config enhanced map */
	wcrt(CRT_ID_EXT_MEM_CNTL_1, 0x08);	/* MMIO Select (0x0c works as well)*/
	wcrt(CRT_ID_EXT_MEM_CNTL_2, 0x02);	/* why 02 big endian 00 works ? */
	wcrt(CRT_ID_EXT_MEM_CNTL_4, 0x9f);	/* config big endian - 0x00 ?  */
	wcrt(CRT_ID_LAW_POS_HI, 0x00);
	wcrt(CRT_ID_LAW_POS_LO, 0x00);
	wcrt(CRT_ID_EXT_MISC_CNTL_1, 0x81);
	wcrt(CRT_ID_MISC_1, 0x90);		/* must follow CRT_ID_EXT_MISC_CNTL_1 */
	wcrt(CRT_ID_LAW_CNTL, 0x13);		/* force 4 Meg for test */
	if (cv3d_has_4mb()) {
		v_ram_size = 0x00400000;
		wcrt(CRT_ID_LAW_CNTL, 0x13);	/* 4 MB */
	} else {
		v_ram_size = 0x00200000;
		wcrt(CRT_ID_LAW_CNTL, 0x12); 	/* 2 MB */
	}

	if (on_zorro2)
		v_ram_size -= 0x60000;		/* we need some space for the registers */

	wcrt(CRT_ID_EXT_SYS_CNTL_4, 0x00);
	wcrt(CRT_ID_EXT_DAC_CNTL, 0x00);	/* 0x10 for X11 cursor mode */

/* sequencer registers */

	wseq(SEQ_ID_CLOCKING_MODE, 0x01);	/* 8 dot clock */
	wseq(SEQ_ID_MAP_MASK, 0xff);
	wseq(SEQ_ID_CHAR_MAP_SELECT, 0x00);
	wseq(SEQ_ID_MEMORY_MODE, 0x02);
	wseq(SEQ_ID_RAMDAC_CNTL, 0x00);
	wseq(SEQ_ID_SIGNAL_SELECT, 0x00);
	wseq(SEQ_ID_EXT_SEQ_REG9, 0x00);	/* MMIO and PIO reg access enabled */
	wseq(SEQ_ID_EXT_MISC_SEQ, 0x00);
	wseq(SEQ_ID_CLKSYN_CNTL_1, 0x00);
	wseq(SEQ_ID_EXT_SEQ, 0x00);

/* graphic registers */

	wgfx(GCT_ID_SET_RESET, 0x00);
	wgfx(GCT_ID_ENABLE_SET_RESET, 0x00);
	wgfx(GCT_ID_COLOR_COMPARE, 0x00);
	wgfx(GCT_ID_DATA_ROTATE, 0x00);
	wgfx(GCT_ID_READ_MAP_SELECT, 0x00);
	wgfx(GCT_ID_GRAPHICS_MODE, 0x40);
	wgfx(GCT_ID_MISC, 0x01);
	wgfx(GCT_ID_COLOR_XCARE, 0x0f);
	wgfx(GCT_ID_BITMASK, 0xff);

/* attribute  registers */

	for(i = 0; i <= 15; i++)
		watr(ACT_ID_PALETTE0 + i, i);
	watr(ACT_ID_ATTR_MODE_CNTL, 0x41);
	watr(ACT_ID_OVERSCAN_COLOR, 0xff);
	watr(ACT_ID_COLOR_PLANE_ENA, 0x0f);
	watr(ACT_ID_HOR_PEL_PANNING, 0x00);
	watr(ACT_ID_COLOR_SELECT, 0x00);

	wb_mmio(VDAC_MASK, 0xff);

/* init local cmap as greyscale levels */

	for (i = 0; i < 256; i++) {
		virgefb_colour_table [i][0] = i;
		virgefb_colour_table [i][1] = i;
		virgefb_colour_table [i][2] = i;
	}

/* clear framebuffer memory */

	memset((char*)v_ram, 0x00, v_ram_size);

	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    This function should fill in the `fix' structure based on the
 *    values in the `par' structure.
 */

static int virgefb_encode_fix(struct fb_fix_screeninfo *fix,
			    struct virgefb_par *par)
{
	DPRINTK("ENTER set video phys addr\n");

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));
	strcpy(fix->id, virgefb_name);
	if (on_zorro2)
		fix->smem_start = v_ram_phys;
	switch (par->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
		case 8:
			if (on_zorro2)
				Select_Zorro2_FrameBuffer(ENDIAN_BYTE);
			else
				fix->smem_start = (v_ram_phys + CYBMEM_OFFSET_8);
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			if (on_zorro2)
				Select_Zorro2_FrameBuffer(ENDIAN_WORD);
			else
				fix->smem_start = (v_ram_phys + CYBMEM_OFFSET_16);
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			if (on_zorro2)
				Select_Zorro2_FrameBuffer(ENDIAN_LONG);
			else
				fix->smem_start = (v_ram_phys + CYBMEM_OFFSET_32);
			break;
#endif
	}

	fix->smem_len = v_ram_size;
	fix->mmio_start = mmio_regs_phys;
	fix->mmio_len = 0x10000; /* TODO: verify this for the CV64/3D */

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (par->var.bits_per_pixel == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_TRUECOLOR;

	fix->xpanstep = 0;
	fix->ypanstep = 0;
	fix->ywrapstep = 0;
	fix->line_length = par->var.xres_virtual*par->var.bits_per_pixel/8;
	fix->accel = FB_ACCEL_S3_VIRGE;
	DPRINTK("EXIT v_ram_phys = 0x%8.8lx\n", (unsigned long)fix->smem_start);
	return 0;
}


/*
 *	Fill the `par' structure based on the values in `var'.
 *	TODO: Verify and adjust values, return -EINVAL if bad.
 */

static int virgefb_decode_var(struct fb_var_screeninfo *var,
			    struct virgefb_par *par)
{
	DPRINTK("ENTER\n");
	par->var.xres = var->xres;
	par->var.yres = var->yres;
	par->var.xres_virtual = var->xres_virtual;
	par->var.yres_virtual = var->yres_virtual;
	/* roundup and validate */
	par->var.xres = (par->var.xres+7) & ~7;
	par->var.xres_virtual = (par->var.xres_virtual+7) & ~7;
	if (par->var.xres_virtual < par->var.xres)
		par->var.xres_virtual = par->var.xres;
	if (par->var.yres_virtual < par->var.yres)
		par->var.yres_virtual = par->var.yres;
	par->var.xoffset = var->xoffset;
	par->var.yoffset = var->yoffset;
	par->var.bits_per_pixel = var->bits_per_pixel;
	if (par->var.bits_per_pixel <= 8)
		par->var.bits_per_pixel = 8;
	else if (par->var.bits_per_pixel <= 16)
		par->var.bits_per_pixel = 16;
	else
		par->var.bits_per_pixel = 32;
#ifndef FBCON_HAS_CFB32
	if (par->var.bits_per_pixel == 32)
		par->var.bits_per_pixel = 16;
#endif
#ifndef FBCON_HAS_CFB16
	if (par->var.bits_per_pixel == 16)
		par->var.bits_per_pixel = 8;
#endif
	par->var.grayscale = var->grayscale;
	par->var.red = var->red;
	par->var.green = var->green;
	par->var.blue = var->blue;
	par->var.transp = var->transp;
	par->var.nonstd = var->nonstd;
	par->var.activate = var->activate;
	par->var.height = var->height;
	par->var.width = var->width;
	if (var->accel_flags & FB_ACCELF_TEXT) {
		par->var.accel_flags = FB_ACCELF_TEXT;
	} else {
		par->var.accel_flags = 0;
	}
	par->var.pixclock = var->pixclock;
	par->var.left_margin = var->left_margin;
	par->var.right_margin = var->right_margin;
	par->var.upper_margin = var->upper_margin;
	par->var.lower_margin = var->lower_margin;
	par->var.hsync_len = var->hsync_len;
	par->var.vsync_len = var->vsync_len;
	par->var.sync = var->sync;
	par->var.vmode = var->vmode;
	DPRINTK("EXIT\n");
	return 0;
}

/*
 *	Fill the `var' structure based on the values in `par' and maybe
 *	other values read out of the hardware.
 */

static int virgefb_encode_var(struct fb_var_screeninfo *var,
				struct virgefb_par *par)
{
	DPRINTK("ENTER\n");
	memset(var, 0, sizeof(struct fb_var_screeninfo));	/* need this ? */
	var->xres = par->var.xres;
	var->yres = par->var.yres;
	var->xres_virtual = par->var.xres_virtual;
	var->yres_virtual = par->var.yres_virtual;
	var->xoffset = par->var.xoffset;
	var->yoffset = par->var.yoffset;
	var->bits_per_pixel = par->var.bits_per_pixel;
	var->grayscale = par->var.grayscale;
	var->red = par->var.red;
	var->green = par->var.green;
	var->blue = par->var.blue;
	var->transp = par->var.transp;
	var->nonstd = par->var.nonstd;
	var->activate = par->var.activate;
	var->height = par->var.height;
	var->width = par->var.width;
	var->accel_flags = par->var.accel_flags;
	var->pixclock = par->var.pixclock;
	var->left_margin = par->var.left_margin;
	var->right_margin = par->var.right_margin;
	var->upper_margin = par->var.upper_margin;
	var->lower_margin = par->var.lower_margin;
	var->hsync_len = par->var.hsync_len;
	var->vsync_len = par->var.vsync_len;
	var->sync = par->var.sync;
	var->vmode = par->var.vmode;
	DPRINTK("EXIT\n");
	return 0;
}

/*
 *    Set a single color register. The values supplied are already
 *    rounded down to the hardware's capabilities (according to the
 *    entries in the var structure). Return != 0 for invalid regno.
 */

static int virgefb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			     u_int transp, struct fb_info *info)
{
	DPRINTK("ENTER\n");
	if (((current_par.var.bits_per_pixel==8) && (regno>255)) ||
		((current_par.var.bits_per_pixel!=8) && (regno>15))) {
			DPRINTK("EXIT\n");
			return 1;
	}
	if (((current_par.var.bits_per_pixel==8) && (regno<256)) ||
			((current_par.var.bits_per_pixel!=8) && (regno<16))) {
		virgefb_colour_table [regno][0] = red >> 10;
		virgefb_colour_table [regno][1] = green >> 10;
		virgefb_colour_table [regno][2] = blue >> 10;
	}

	switch (current_par.var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
		case 8:
			wb_mmio(VDAC_ADDRESS_W, (unsigned char)regno);
			wb_mmio(VDAC_DATA, ((unsigned char)(red >> 10)));
			wb_mmio(VDAC_DATA, ((unsigned char)(green >> 10)));
			wb_mmio(VDAC_DATA, ((unsigned char)(blue >> 10)));
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			fbcon_cmap.cfb16[regno] =
				((red  & 0xf800) |
				((green & 0xfc00) >> 5) |
				((blue  & 0xf800) >> 11));
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			fbcon_cmap.cfb32[regno] =
				/* transp = 0's or 1's  ? */
				(((red  & 0xff00) << 8) |
				((green & 0xff00) >> 0) |
				((blue  & 0xff00) >> 8));
			break;
#endif
	}
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Read a single color register and split it into
 *    colors/transparent. Return != 0 for invalid regno.
 */

static int virgefb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
			   u_int *transp, struct fb_info *info)
{
	int t;

	DPRINTK("ENTER\n");
	if (regno > 255) {
		DPRINTK("EXIT\n");
		return 1;
	}
	if (((current_par.var.bits_per_pixel==8) && (regno<256)) ||
			((current_par.var.bits_per_pixel!=8) && (regno<16))) {

		t = virgefb_colour_table [regno][0];
		*red = (t<<10) | (t<<4) | (t>>2);
		t = virgefb_colour_table [regno][1];
		*green = (t<<10) | (t<<4) | (t>>2);
		t = virgefb_colour_table [regno][2];
		*blue = (t<<10) | (t<<4) | (t>>2);
	}
	*transp = 0;
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    (Un)Blank the screen
 */

static void virgefb_gfx_on_off(int blank)
{
	DPRINTK("ENTER\n");
	gfx_on_off(blank);
	DPRINTK("EXIT\n");
}

/*
 * CV3D low-level support
 */


static inline void wait_3d_fifo_slots(int n)	/* WaitQueue */
{
	do {
		mb();
	} while (((rl_mmio(MR_SUBSYSTEM_STATUS_R) >> 8) & 0x1f) < (n + 2));
}

static inline void virgefb_wait_for_idle(void)	/* WaitIdle */
{
	while(!(rl_mmio(MR_SUBSYSTEM_STATUS_R) & 0x2000)) ;
	blit_maybe_busy = 0;
}

 /*
  * BitBLT - Through the Plane
  */

static void virgefb_BitBLT(u_short curx, u_short cury, u_short destx, u_short desty,
			u_short width, u_short height, u_short stride, u_short depth)
{
	unsigned int blitcmd = S3V_BITBLT | S3V_DRAW | S3V_BLT_COPY;

	switch (depth) {
#ifdef FBCON_HAS_CFB8
		case 8 :
			blitcmd |= S3V_DST_8BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16 :
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32 :
			/* 32 bit uses 2 by 16 bit values, see fbcon_virge32_bmove */
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
	}

	/* Set drawing direction */
	/* -Y, X maj, -X (default) */
	if (curx > destx) {
		blitcmd |= (1 << 25);  /* Drawing direction +X */
	} else {
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty) {
		blitcmd |= (1 << 26);  /* Drawing direction +Y */
	} else {
		cury  += (height - 1);
		desty += (height - 1);
	}

	wait_3d_fifo_slots(8);		/* wait on fifo slots for 8 writes */

	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	blit_maybe_busy = 1;

	wl_mmio(BLT_PATTERN_COLOR, 1);	/* pattern fb color */
	wl_mmio(BLT_MONO_PATTERN_0, ~0);
	wl_mmio(BLT_MONO_PATTERN_1, ~0);
	wl_mmio(BLT_SIZE_X_Y, ((width << 16) | height));
	wl_mmio(BLT_SRC_X_Y, ((curx << 16)  | cury));
	wl_mmio(BLT_DEST_X_Y, ((destx << 16) | desty));
	wl_mmio(BLT_SRC_DEST_STRIDE, (((stride << 16) | stride) /* & 0x0ff80ff8 */)); /* why is this needed now ? */
	wl_mmio(BLT_COMMAND_SET, blitcmd);
}

/*
 * Rectangle Fill Solid
 */

static void virgefb_RectFill(u_short x, u_short y, u_short width, u_short height,
			u_short color,  u_short stride, u_short depth)
{
	unsigned int blitcmd = S3V_RECTFILL | S3V_DRAW |
		S3V_BLT_CLEAR | S3V_MONO_PAT | (1 << 26) | (1 << 25);

	switch (depth) {
#ifdef FBCON_HAS_CFB8
		case 8 :
			blitcmd |= S3V_DST_8BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16 :
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32 :
			/* 32 bit uses 2 times 16 bit values, see fbcon_virge32_clear */
			blitcmd |= S3V_DST_16BPP;
			break;
#endif
	}

	wait_3d_fifo_slots(5);		/* wait on fifo slots for 5 writes */

	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	blit_maybe_busy = 1;

	wl_mmio(BLT_PATTERN_COLOR, (color & 0xff));
	wl_mmio(BLT_SIZE_X_Y, ((width << 16) | height));
	wl_mmio(BLT_DEST_X_Y, ((x << 16) | y));
	wl_mmio(BLT_SRC_DEST_STRIDE, (((stride << 16) | stride) /* & 0x0ff80ff8 */));
	wl_mmio(BLT_COMMAND_SET, blitcmd);
}

/*
 * Move cursor to x, y
 */

#if 0
static void virgefb_move_cursor(u_short x, u_short y)
{
	DPRINTK("Yuck .... MoveCursor on a 3D\n");
	return 0;
}
#endif

/* -------------------- Interfaces to hardware functions -------------------- */

static struct fb_hwswitch virgefb_hw_switch = {
	.init		= virge_init,
	.encode_fix	= virgefb_encode_fix,
	.decode_var	= virgefb_decode_var,
	.encode_var	= virgefb_encode_var,
	.getcolreg	= virgefb_getcolreg,
	.blank		= virgefb_gfx_on_off
};


/* -------------------- Generic routines ------------------------------------ */


/*
 *    Fill the hardware's `par' structure.
 */

static void virgefb_get_par(struct virgefb_par *par)
{
	DPRINTK("ENTER\n");
	if (current_par_valid) {
		*par = current_par;
	} else {
		fbhw->decode_var(&virgefb_default, par);
	}
	DPRINTK("EXIT\n");
}


static void virgefb_set_par(struct virgefb_par *par)
{
	DPRINTK("ENTER\n");
	current_par = *par;
	current_par_valid = 1;
	DPRINTK("EXIT\n");
}


static void virgefb_set_video(struct fb_var_screeninfo *var)
{
/* Set clipping rectangle to current screen size */

	unsigned int clip;

	DPRINTK("ENTER\n");
	wait_3d_fifo_slots(4);
	clip = ((0 << 16) | (var->xres - 1));
	wl_mmio(BLT_CLIP_LEFT_RIGHT, clip);
	clip = ((0 << 16) | (var->yres - 1));
	wl_mmio(BLT_CLIP_TOP_BOTTOM, clip);
	wl_mmio(BLT_SRC_BASE, 0);		/* seems we need to clear these two */
	wl_mmio(BLT_DEST_BASE, 0);

/* Load the video mode defined by the 'var' data */

	virgefb_load_video_mode(var);
	DPRINTK("EXIT\n");
}

/*
Merge these two functions, Geert's suggestion.
static int virgefb_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info);
static int virgefb_do_fb_set_var(struct fb_var_screeninfo *var, int isactive);
*/

static int virgefb_do_fb_set_var(struct fb_var_screeninfo *var, int isactive)
{
	int err, activate;
	struct virgefb_par par;

	DPRINTK("ENTER\n");
	if ((err = fbhw->decode_var(var, &par))) {
		DPRINTK("EXIT\n");
		return (err);
	}

	activate = var->activate;
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		virgefb_set_par(&par);
	fbhw->encode_var(var, &par);
	var->activate = activate;
        if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW && isactive)
		virgefb_set_video(var);
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Get the Fixed Part of the Display
 */

static int virgefb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	struct virgefb_par par;
	int error = 0;

	DPRINTK("ENTER\n");
	if (con == -1)
		virgefb_get_par(&par);
	else
		error = fbhw->decode_var(&fb_display[con].var, &par);

	if (!error)
		error = fbhw->encode_fix(fix, &par);
	DPRINTK("EXIT\n");
	return(error);
}


/*
 *    Get the User Defined Part of the Display
 */

static int virgefb_get_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	struct virgefb_par par;
	int error = 0;

	DPRINTK("ENTER\n");
	if (con == -1) {
		virgefb_get_par(&par);
		error = fbhw->encode_var(var, &par);
		disp.var = *var;   /* ++Andre: don't know if this is the right place */
	} else {
		*var = fb_display[con].var;
	}
	DPRINTK("EXIT\n");
	return(error);
}

static void virgefb_set_disp(int con, struct fb_info *info)
{
	struct fb_fix_screeninfo fix;
	struct display *display;

	DPRINTK("ENTER\n");
	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	virgefb_get_fix(&fix, con, info);
	if (con == -1)
		con = 0;
	if(on_zorro2) {
		info->screen_base = (char*)v_ram;
	} else {
	        switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
			case 8:
				info->screen_base = (char*)(v_ram + CYBMEM_OFFSET_8);
				break;
#endif
#ifdef FBCON_HAS_CFB16
			case 16:
				info->screen_base = (char*)(v_ram + CYBMEM_OFFSET_16);
				break;
#endif
#ifdef FBCON_HAS_CFB32
			case 32:
				info->screen_base = (char*)(v_ram + CYBMEM_OFFSET_32);
				break;
#endif
		}
	}
	display->visual = fix.visual;
	display->type = fix.type;
	display->type_aux = fix.type_aux;
	display->ypanstep = fix.ypanstep;
	display->ywrapstep = fix.ywrapstep;
	display->can_soft_blank = 1;
	display->inverse = virgefb_inverse;
	display->line_length = display->var.xres_virtual*
			       display->var.bits_per_pixel/8;

	switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB8
		case 8:
			if (display->var.accel_flags & FB_ACCELF_TEXT) {
		   		display->dispsw = &fbcon_virge8;
#warning FIXME: We should reinit the graphics engine here
			} else
				display->dispsw = &fbcon_cfb8;
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			if (display->var.accel_flags & FB_ACCELF_TEXT) {
				display->dispsw = &fbcon_virge16;
			} else
				display->dispsw = &fbcon_cfb16;
			display->dispsw_data = &fbcon_cmap.cfb16;
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			if (display->var.accel_flags & FB_ACCELF_TEXT) {
				display->dispsw = &fbcon_virge32;
			} else
				display->dispsw = &fbcon_cfb32;
			display->dispsw_data = &fbcon_cmap.cfb32;
			break;
#endif
		default:
			display->dispsw = &fbcon_dummy;
			break;
	}
	DPRINTK("EXIT v_ram virt = 0x%8.8lx\n",(unsigned long)display->screen_base);
}


/*
 *    Set the User Defined Part of the Display
 */

static int virgefb_set_var(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
	int err, oldxres, oldyres, oldvxres, oldvyres, oldbpp, oldaccel;

	DPRINTK("ENTER\n");

	if ((err = virgefb_do_fb_set_var(var, con == info->currcon))) {
		DPRINTK("EXIT\n");
		return(err);
	}
	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldxres = fb_display[con].var.xres;
		oldyres = fb_display[con].var.yres;
		oldvxres = fb_display[con].var.xres_virtual;
		oldvyres = fb_display[con].var.yres_virtual;
		oldbpp = fb_display[con].var.bits_per_pixel;
		oldaccel = fb_display[con].var.accel_flags;
		fb_display[con].var = *var;
		if (oldxres != var->xres || oldyres != var->yres ||
		    oldvxres != var->xres_virtual ||
		    oldvyres != var->yres_virtual ||
		    oldbpp != var->bits_per_pixel ||
		    oldaccel != var->accel_flags) {
			virgefb_set_disp(con, info);
			if (fb_info.changevar)
				(*fb_info.changevar)(con);
			fb_alloc_cmap(&fb_display[con].cmap, 0, 0);
			do_install_cmap(con, info);
		}
	}
	var->activate = 0;
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Get the Colormap
 */

static int virgefb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			    struct fb_info *info)
{
	DPRINTK("ENTER\n");
	if (con == info->currcon) { /* current console? */
		DPRINTK("EXIT - console is current console, fb_get_cmap\n");
		return(fb_get_cmap(cmap, kspc, fbhw->getcolreg, info));
	} else if (fb_display[con].cmap.len) { /* non default colormap? */
		DPRINTK("Use console cmap\n");
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	} else {
		DPRINTK("Use default cmap\n");
		fb_copy_cmap(fb_default_cmap(fb_display[con].var.bits_per_pixel==8 ? 256 : 16),
			     cmap, kspc ? 0 : 2);
	}
	DPRINTK("EXIT\n");
	return 0;
}

static struct fb_ops virgefb_ops = {
	.owner =	THIS_MODULE,
	.fb_get_fix =	virgefb_get_fix,
	.fb_get_var =	virgefb_get_var,
	.fb_set_var =	virgefb_set_var,
	.fb_get_cmap =	virgefb_get_cmap,
	.fb_set_cmap =	gen_set_cmap,
	.fb_setcolreg =	virgefb_setcolreg,
	.fb_blank =	virgefb_blank,
};

int __init virgefb_setup(char *options)
{
	char *this_opt;
	fb_info.fontname[0] = '\0';

	DPRINTK("ENTER\n");
	if (!options || !*options) {
		DPRINTK("EXIT\n");
		return 0;
	}

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt)
			continue;
		if (!strcmp(this_opt, "inverse")) {
			virgefb_inverse = 1;
			fb_invert_cmaps();
		} else if (!strncmp(this_opt, "font:", 5))
			strcpy(fb_info.fontname, this_opt+5);
#ifdef FBCON_HAS_CFB8
		else if (!strcmp (this_opt, "virge8")){
			virgefb_default = virgefb_predefined[VIRGE8_DEFMODE].var;
		}
#endif
#ifdef FBCON_HAS_CFB16
		else if (!strcmp (this_opt, "virge16")){
			virgefb_default = virgefb_predefined[VIRGE16_DEFMODE].var;
		}
#endif
#ifdef FBCON_HAS_CFB32
		else if (!strcmp (this_opt, "virge32")){
			virgefb_default = virgefb_predefined[VIRGE32_DEFMODE].var;
		}
#endif
		else
			virgefb_get_video_mode(this_opt);
	}

	printk(KERN_INFO "mode : xres=%d, yres=%d, bpp=%d\n", virgefb_default.xres,
			virgefb_default.yres, virgefb_default.bits_per_pixel);
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Get a Video Mode
 */

static int __init virgefb_get_video_mode(const char *name)
{
	int i;

	DPRINTK("ENTER\n");
	for (i = 0; i < NUM_TOTAL_MODES; i++) {
		if (!strcmp(name, virgefb_predefined[i].name)) {
			virgefb_default = virgefb_predefined[i].var;
			DPRINTK("EXIT\n");
			return(i);
		}
	}
	/* ++Andre: set virgefb default mode */

/* prefer 16 bit depth, 8 if no 16, if no 8 or 16 use 32 */

#ifdef FBCON_HAS_CFB32
	virgefb_default = virgefb_predefined[VIRGE32_DEFMODE].var;
#endif
#ifdef FBCON_HAS_CFB8
	virgefb_default = virgefb_predefined[VIRGE8_DEFMODE].var;
#endif
#ifdef FBCON_HAS_CFB16
	virgefb_default = virgefb_predefined[VIRGE16_DEFMODE].var;
#endif
	DPRINTK("EXIT\n");
	return 0;
}

/*
 *    Initialization
 */

int __init virgefb_init(void)
{
	struct virgefb_par par;
	unsigned long board_addr, board_size;
	struct zorro_dev *z = NULL;

	DPRINTK("ENTER\n");

	z = zorro_find_device(ZORRO_PROD_PHASE5_CYBERVISION64_3D, NULL);
	if (!z)
		return -ENODEV;

	board_addr = z->resource.start;
	if (board_addr < 0x01000000) {

		/* board running in Z2 space. This includes the video memory
		    as well as the S3 register set */

		on_zorro2 = 1;
		board_size = 0x00400000;

		if (!request_mem_region(board_addr, board_size, "S3 ViRGE"))
			return -ENOMEM;

		v_ram_phys = board_addr;
		v_ram = ZTWO_VADDR(v_ram_phys);
		mmio_regs_phys = (unsigned long)(board_addr + 0x003c0000);
		vgaio_regs = (unsigned char *) ZTWO_VADDR(board_addr + 0x003c0000);
		mmio_regs = (unsigned char *)ZTWO_VADDR(mmio_regs_phys);
		vcode_switch_base = (unsigned long) ZTWO_VADDR(board_addr + 0x003a0000);
		printk(KERN_INFO "CV3D detected running in Z2 mode.\n");

	} else {

		/* board running in Z3 space. Separate video memory (3 apertures)
		   and S3 register set */

		on_zorro2 = 0;
		board_size = 0x01000000;

		if (!request_mem_region(board_addr, board_size, "S3 ViRGE"))
			return -ENOMEM;

		v_ram_phys  = board_addr + 0x04000000;
		v_ram = (unsigned long)ioremap(v_ram_phys, 0x01000000);
		mmio_regs_phys = board_addr + 0x05000000;
		vgaio_regs = (unsigned char *)ioremap(board_addr +0x0c000000, 0x00100000); /* includes PCI regs */
		mmio_regs = ioremap(mmio_regs_phys, 0x00010000);
		vcode_switch_base = (unsigned long)ioremap(board_addr + 0x08000000, 0x1000);
		printk(KERN_INFO "CV3D detected running in Z3 mode\n");
	}

#if defined (VIRGEFBDEBUG)
	DPRINTK("board_addr     : 0x%8.8lx\n",board_addr);
	DPRINTK("board_size     : 0x%8.8lx\n",board_size);
	DPRINTK("mmio_regs_phy  : 0x%8.8lx\n",mmio_regs_phys);
	DPRINTK("v_ram_phys     : 0x%8.8lx\n",v_ram_phys);
	DPRINTK("vgaio_regs     : 0x%8.8lx\n",(unsigned long)vgaio_regs);
	DPRINTK("mmio_regs      : 0x%8.8lx\n",(unsigned long)mmio_regs);
	DPRINTK("v_ram          : 0x%8.8lx\n",v_ram);
	DPRINTK("vcode sw base  : 0x%8.8lx\n",vcode_switch_base);
#endif
	fbhw = &virgefb_hw_switch;
	strcpy(fb_info.modename, virgefb_name);
	fb_info.changevar = NULL;
	fb_info.fbops = &virgefb_ops;
	fb_info.disp = &disp;
	fb_info.currcon = -1;
	fb_info.switch_con = &virgefb_switch;
	fb_info.updatevar = &virgefb_updatevar;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	fbhw->init();
	fbhw->decode_var(&virgefb_default, &par);
	fbhw->encode_var(&virgefb_default, &par);
	virgefb_do_fb_set_var(&virgefb_default, 1);
	virgefb_get_var(&fb_display[0].var, -1, &fb_info);
	virgefb_set_disp(-1, &fb_info);
	do_install_cmap(0, &fb_info);

	if (register_framebuffer(&fb_info) < 0) {
		#warning release resources
		printk(KERN_ERR "virgefb.c: register_framebuffer failed\n");
		DPRINTK("EXIT\n");
		return -EINVAL;
	}

	printk(KERN_INFO "fb%d: %s frame buffer device, using %ldK of video memory\n",
	       fb_info.node, fb_info.modename, v_ram_size>>10);

	/* TODO: This driver cannot be unloaded yet */

	DPRINTK("EXIT\n");
	return 0;
}


static int virgefb_switch(int con, struct fb_info *info)
{
	DPRINTK("ENTER\n");
	/* Do we have to save the colormap? */
	if (fb_display[info->currcon].cmap.len)
		fb_get_cmap(&fb_display[info->currcon].cmap, 1,
			    fbhw->getcolreg, info);
	virgefb_do_fb_set_var(&fb_display[con].var, 1);
	info->currcon = con;
	/* Install new colormap */
	do_install_cmap(con, info);
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Update the `var' structure (called by fbcon.c)
 *
 *    This call looks only at yoffset and the FB_VMODE_YWRAP flag in `var'.
 *    Since it's called by a kernel driver, no range checking is done.
 */

static int virgefb_updatevar(int con, struct fb_info *info)
{
	DPRINTK("ENTER\n");
	return 0;
	DPRINTK("EXIT\n");
}

/*
 *    Blank the display.
 */

static int virgefb_blank(int blank, struct fb_info *info)
{
	DPRINTK("ENTER\n");
	fbhw->blank(blank);
	DPRINTK("EXIT\n");
	return 0;
}


/*
 *    Text console acceleration
 */

#ifdef FBCON_HAS_CFB8
static void fbcon_virge8_bmove(struct display *p, int sy, int sx, int dy,
			       int dx, int height, int width)
{
        sx *= 8; dx *= 8; width *= 8;
        virgefb_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
                       (u_short)(dy*fontheight(p)), (u_short)width,
                       (u_short)(height*fontheight(p)), (u_short)p->next_line, 8);
}

static void fbcon_virge8_clear(struct vc_data *conp, struct display *p, int sy,
			       int sx, int height, int width)
{
        unsigned char bg;

        sx *= 8; width *= 8;
        bg = attr_bgcol_ec(p,conp);
        virgefb_RectFill((u_short)sx, (u_short)(sy*fontheight(p)),
                         (u_short)width, (u_short)(height*fontheight(p)),
                         (u_short)bg, (u_short)p->next_line, 8);
}

static void fbcon_virge8_putc(struct vc_data *conp, struct display *p, int c, int yy,
                              int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_virge8_putcs(struct vc_data *conp, struct display *p,
                      const unsigned short *s, int count, int yy, int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_virge8_revc(struct display *p, int xx, int yy)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb8_revc(p, xx, yy);
}

static void fbcon_virge8_clear_margins(struct vc_data *conp, struct display *p,
                              int bottom_only)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb8_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_virge8 = {
	.setup		= fbcon_cfb8_setup,
	.bmove		= fbcon_virge8_bmove,
	.clear		= fbcon_virge8_clear,
	.putc		= fbcon_virge8_putc,
	.putcs		= fbcon_virge8_putcs,
	.revc		= fbcon_virge8_revc,
	.clear_margins	= fbcon_virge8_clear_margins,
	.fontwidthmask	= FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB16
static void fbcon_virge16_bmove(struct display *p, int sy, int sx, int dy,
                               int dx, int height, int width)
{
        sx *= 8; dx *= 8; width *= 8;
        virgefb_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
                       (u_short)(dy*fontheight(p)), (u_short)width,
                       (u_short)(height*fontheight(p)), (u_short)p->next_line, 16);
}

static void fbcon_virge16_clear(struct vc_data *conp, struct display *p, int sy,
                               int sx, int height, int width)
{
        unsigned char bg;

        sx *= 8; width *= 8;
        bg = attr_bgcol_ec(p,conp);
        virgefb_RectFill((u_short)sx, (u_short)(sy*fontheight(p)),
                         (u_short)width, (u_short)(height*fontheight(p)),
                         (u_short)bg, (u_short)p->next_line, 16);
}

static void fbcon_virge16_putc(struct vc_data *conp, struct display *p, int c, int yy,
                              int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb16_putc(conp, p, c, yy, xx);
}

static void fbcon_virge16_putcs(struct vc_data *conp, struct display *p,
                      const unsigned short *s, int count, int yy, int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb16_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_virge16_revc(struct display *p, int xx, int yy)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb16_revc(p, xx, yy);
}

static void fbcon_virge16_clear_margins(struct vc_data *conp, struct display *p,
                              int bottom_only)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb16_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_virge16 = {
	.setup		= fbcon_cfb16_setup,
	.bmove		= fbcon_virge16_bmove,
	.clear		= fbcon_virge16_clear,
	.putc		= fbcon_virge16_putc,
	.putcs		= fbcon_virge16_putcs,
	.revc		= fbcon_virge16_revc,
	.clear_margins	= fbcon_virge16_clear_margins,
	.fontwidthmask	= FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef FBCON_HAS_CFB32
static void fbcon_virge32_bmove(struct display *p, int sy, int sx, int dy,
			       int dx, int height, int width)
{
        sx *= 16; dx *= 16; width *= 16;	/* doubled these values to do 32 bit blit */
        virgefb_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
                       (u_short)(dy*fontheight(p)), (u_short)width,
                       (u_short)(height*fontheight(p)), (u_short)p->next_line, 16);
}

static void fbcon_virge32_clear(struct vc_data *conp, struct display *p, int sy,
			       int sx, int height, int width)
{
        unsigned char bg;

        sx *= 16; width *= 16;			/* doubled these values to do 32 bit blit */
        bg = attr_bgcol_ec(p,conp);
        virgefb_RectFill((u_short)sx, (u_short)(sy*fontheight(p)),
                         (u_short)width, (u_short)(height*fontheight(p)),
                         (u_short)bg, (u_short)p->next_line, 16);
}

static void fbcon_virge32_putc(struct vc_data *conp, struct display *p, int c, int yy,
                              int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb32_putc(conp, p, c, yy, xx);
}

static void fbcon_virge32_putcs(struct vc_data *conp, struct display *p,
                      const unsigned short *s, int count, int yy, int xx)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb32_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_virge32_revc(struct display *p, int xx, int yy)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb32_revc(p, xx, yy);
}

static void fbcon_virge32_clear_margins(struct vc_data *conp, struct display *p,
                              int bottom_only)
{
	if (blit_maybe_busy)
		virgefb_wait_for_idle();
	fbcon_cfb32_clear_margins(conp, p, bottom_only);
}

static struct display_switch fbcon_virge32 = {
	.setup		= fbcon_cfb32_setup,
	.bmove		= fbcon_virge32_bmove,
	.clear		= fbcon_virge32_clear,
	.putc		= fbcon_virge32_putc,
	.putcs		= fbcon_virge32_putcs,
	.revc		= fbcon_virge32_revc,
	.clear_margins	= fbcon_virge32_clear_margins,
	.fontwidthmask	= FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
#endif

#ifdef MODULE
MODULE_LICENSE("GPL");

int init_module(void)
{
	return virgefb_init();
}
#endif /* MODULE */

static int cv3d_has_4mb(void)
{
	/* cyberfb version didn't work, neither does this (not reliably)
	forced to return 4MB */
#if 0
	volatile unsigned long *t0, *t2;
#endif
	DPRINTK("ENTER\n");
#if 0
	/* write patterns in memory and test if they can be read */
	t0 = (volatile unsigned long *)v_ram;
	t2 = (volatile unsigned long *)(v_ram + 0x00200000);
	*t0 = 0x87654321;
	*t2 = 0x12345678;

	if (*t0 != 0x87654321) {
		/* read of first location failed */
		DPRINTK("EXIT - 0MB !\n");
		return 0;
	}

	if (*t2 == 0x87654321) {
		/* should read 0x12345678 if 4MB */
		DPRINTK("EXIT - 2MB(a) \n");
		return 0;
	}

	if (*t2 != 0x12345678) {
		/* upper 2MB read back match failed */
		DPRINTK("EXIT - 2MB(b)\n");
		return 0;
	}

	/* may have 4MB */

	*t2 = 0xAAAAAAAA;

	if(*t2 != 0xAAAAAAAA) {
		/* upper 2MB read back match failed */
		DPRINTK("EXIT - 2MB(c)\n");
		return 0;
	}

	*t2 = 0x55555555;

	if(*t2 != 0x55555555) {
		/* upper 2MB read back match failed */
		DPRINTK("EXIT - 2MB(d)\n");
		return 0;
	}

#endif
	DPRINTK("EXIT - 4MB\n");
	return 1;
}


/*
 * Computes M, N, and R pll params for freq arg.
 * Returns 16 bits - hi 0MMMMMM lo 0RRNNNNN
 */

#define REFCLOCK 14318000

static unsigned short virgefb_compute_clock(unsigned long freq)
{

	unsigned char m, n, r, rpwr;
	unsigned long diff, ftry, save = ~0UL;
	unsigned short mnr;

	DPRINTK("ENTER\n");

	for (r = 0, rpwr = 1 ; r < 4 ; r++, rpwr *= 2) {
		if ((135000000 <= (rpwr * freq)) && ((rpwr * freq) <= 270000000)) {
			for (n = 1 ; n < 32 ; n++) {
				m = ((freq * (n + 2) * rpwr)/REFCLOCK) - 2;
				if (m == 0 || m >127)
					break;
				ftry = ((REFCLOCK / (n + 2)) * (m + 2)) / rpwr;
				if (ftry > freq)
					diff = ftry - freq;
				else
					diff = freq - ftry;
				if (diff < save) {
					save = diff;
					mnr =  (m << 8) | (r<<5) | (n & 0x7f);
				}
			}
		}
	}
	if (save == ~0UL)
		printk("Can't compute clock PLL values for %ld Hz clock\n", freq);
	DPRINTK("EXIT\n");
	return(mnr);
}

static void virgefb_load_video_mode(struct fb_var_screeninfo *video_mode)
{
	unsigned char lace, dblscan, tmp;
	unsigned short mnr;
	unsigned short HT, HDE, HBS, HBW, HSS, HSW;
	unsigned short VT, VDE, VBS, VBW, VSS, VSW;
	unsigned short SCO;
	int cr11;
	int cr67;
	int hmul;
	int xres, xres_virtual, hfront, hsync, hback;
	int yres, vfront, vsync, vback;
	int bpp;
	int i;
	long freq;

	DPRINTK("ENTER : %dx%d-%d\n",video_mode->xres, video_mode->yres,
				video_mode->bits_per_pixel);

	bpp = video_mode->bits_per_pixel;
	xres = video_mode->xres;
	xres_virtual = video_mode->xres_virtual;
	hfront = video_mode->right_margin;
	hsync = video_mode->hsync_len;
	hback = video_mode->left_margin;

	lace = 0;
	dblscan = 0;

	if (video_mode->vmode & FB_VMODE_DOUBLE) {
		yres = video_mode->yres * 2;
		vfront = video_mode->lower_margin * 2;
		vsync = video_mode->vsync_len * 2;
		vback = video_mode->upper_margin * 2;
		dblscan = 1;
	} else if (video_mode->vmode & FB_VMODE_INTERLACED) {
		yres = (video_mode->yres + 1) / 2;
		vfront = (video_mode->lower_margin + 1) / 2;
		vsync = (video_mode->vsync_len + 1) / 2;
		vback = (video_mode->upper_margin + 1) / 2;
		lace = 1;
	} else {
		yres = video_mode->yres;
		vfront = video_mode->lower_margin;
		vsync = video_mode->vsync_len;
		vback = video_mode->upper_margin;
	}

	switch (bpp) {
		case 8:
			video_mode->red.offset = 0;
			video_mode->green.offset = 0;
			video_mode->blue.offset = 0;
			video_mode->transp.offset = 0;
			video_mode->red.length = 8;
			video_mode->green.length = 8;
			video_mode->blue.length = 8;
			video_mode->transp.length = 0;
			hmul = 1;
			cr67 = 0x00;
			SCO = xres_virtual / 8;
			break;
		case 16:
			video_mode->red.offset = 11;
			video_mode->green.offset = 5;
			video_mode->blue.offset = 0;
			video_mode->transp.offset = 0;
			video_mode->red.length = 5;
			video_mode->green.length = 6;
			video_mode->blue.length = 5;
			video_mode->transp.length = 0;
			hmul = 2;
			cr67 = 0x50;
			SCO = xres_virtual / 4;
			break;
		case 32:
			video_mode->red.offset = 16;
			video_mode->green.offset = 8;
			video_mode->blue.offset = 0;
			video_mode->transp.offset = 24;
			video_mode->red.length = 8;
			video_mode->green.length = 8;
			video_mode->blue.length = 8;
			video_mode->transp.length = 8;
			hmul = 1;
			cr67 = 0xd0;
			SCO = xres_virtual / 2;
			break;
	}

	HT  = (((xres + hfront + hsync + hback) / 8) * hmul) - 5;
	HDE = ((xres / 8) * hmul) - 1;
	HBS = (xres / 8) * hmul;
	HSS = ((xres + hfront) / 8) * hmul;
	HSW = (hsync / 8) * hmul;
	HBW = (((hfront + hsync + hback) / 8) * hmul) - 2;

	VT  = yres + vfront + vsync + vback - 2;
	VDE = yres - 1;
	VBS = yres - 1;
	VSS = yres + vfront;
	VSW = vsync;
	VBW = vfront + vsync + vback - 2;

#ifdef VIRGEFBDEBUG
	DPRINTK("HDE       : 0x%4.4x, %4.4d\n", HDE, HDE);
	DPRINTK("HBS       : 0x%4.4x, %4.4d\n", HBS, HBS);
	DPRINTK("HSS       : 0x%4.4x, %4.4d\n", HSS, HSS);
	DPRINTK("HSW       : 0x%4.4x, %4.4d\n", HSW, HSW);
	DPRINTK("HBW       : 0x%4.4x, %4.4d\n", HBW, HBW);
	DPRINTK("HSS + HSW : 0x%4.4x, %4.4d\n", HSS+HSW, HSS+HSW);
	DPRINTK("HBS + HBW : 0x%4.4x, %4.4d\n", HBS+HBW, HBS+HBW);
	DPRINTK("HT        : 0x%4.4x, %4.4d\n", HT, HT);
	DPRINTK("VDE       : 0x%4.4x, %4.4d\n", VDE, VDE);
	DPRINTK("VBS       : 0x%4.4x, %4.4d\n", VBS, VBS);
	DPRINTK("VSS       : 0x%4.4x, %4.4d\n", VSS, VSS);
	DPRINTK("VSW       : 0x%4.4x, %4.4d\n", VSW, VSW);
	DPRINTK("VBW       : 0x%4.4x, %4.4d\n", VBW, VBW);
	DPRINTK("VT        : 0x%4.4x, %4.4d\n", VT, VT);
#endif

/* turn gfx off, don't mess up the display */

 	gfx_on_off(1);

/* H and V sync polarity */

	tmp = rb_mmio(GREG_MISC_OUTPUT_R) & 0x2f;		/* colour, ram enable, clk sr12/s13 sel */
	if (!(video_mode->sync & FB_SYNC_HOR_HIGH_ACT))
		tmp |= 0x40;					/* neg H sync polarity */
	if (!(video_mode->sync & FB_SYNC_VERT_HIGH_ACT))
		tmp |= 0x80;					/* neg V sync polarity */
	tmp |= 0x0c;						/* clk from sr12/sr13 */
	wb_mmio(GREG_MISC_OUTPUT_W, tmp);

/* clocks */

	wseq(SEQ_ID_BUS_REQ_CNTL, 0xc0);			/* 2 clk mem wr and /RAS1 */
	wseq(SEQ_ID_CLKSYN_CNTL_2, 0x80);			/* b7 is 2 mem clk wr */
	mnr = virgefb_compute_clock(MEMCLOCK);
	DPRINTK("mem clock %d, m %d, n %d, r %d.\n", MEMCLOCK, ((mnr>>8)&0x7f), (mnr&0x1f), ((mnr >> 5)&0x03));
	wseq(SEQ_ID_MCLK_LO, (mnr & 0x7f));
	wseq(SEQ_ID_MCLK_HI, ((mnr & 0x7f00) >> 8));
	freq = (1000000000 / video_mode->pixclock) * 1000;	/* pixclock is in ps ... convert to Hz */
	mnr = virgefb_compute_clock(freq);
	DPRINTK("dot clock %ld, m %d, n %d, r %d.\n", freq, ((mnr>>8)&0x7f), (mnr&0x1f), ((mnr>>5)&0x03));
	wseq(SEQ_ID_DCLK_LO, (mnr & 0x7f));
	wseq(SEQ_ID_DCLK_HI, ((mnr & 0x7f00) >> 8));
	wseq(SEQ_ID_CLKSYN_CNTL_2, 0xa0);
	wseq(SEQ_ID_CLKSYN_CNTL_2, 0x80);
	udelay(100);

/* load display parameters into board */

	/* not sure about sync and blanking extensions bits in cr5d and cr5 */

	wcrt(CRT_ID_EXT_HOR_OVF,			/* 0x5d */
		((HT & 0x100) ?         0x01 : 0x00) |
		((HDE & 0x100) ?        0x02 : 0x00) |
		((HBS & 0x100) ?        0x04 : 0x00) |
	/*	(((HBS + HBW) & 0x40) ? 0x08 : 0x00) |	*/
		((HSS & 0x100) ?        0x10 : 0x00) |
	/*	(((HSS + HSW) & 0x20) ? 0x20 : 0x00) |	*/
		((HSW >= 0x20) ?        0x20 : 0x00) |
		(((HT-5) & 0x100) ?     0x40 : 0x00));

	wcrt(CRT_ID_EXT_VER_OVF,			/* 0x5e */
		((VT & 0x400) ? 0x01 : 0x00) |
		((VDE & 0x400) ? 0x02 : 0x00) |
		((VBS & 0x400) ? 0x04 : 0x00) |
		((VSS & 0x400) ? 0x10 : 0x00) |
		0x40);					/* line compare */

	wcrt(CRT_ID_START_VER_RETR, VSS);
	cr11 = rcrt(CRT_ID_END_VER_RETR) | 0x20;	/* vert interrupt flag */
	wcrt(CRT_ID_END_VER_RETR, ((cr11 & 0x20) | ((VSS + VSW) & 0x0f)));	/* keeps vert irq enable state, also has unlock bit cr0 to 7 */
	wcrt(CRT_ID_VER_DISP_ENA_END, VDE);
	wcrt(CRT_ID_START_VER_BLANK, VBS);
	wcrt(CRT_ID_END_VER_BLANK, VBS + VBW);		/* might be +/- 1 out */
	wcrt(CRT_ID_HOR_TOTAL, HT);
	wcrt(CRT_ID_DISPLAY_FIFO, HT - 5);
	wcrt(CRT_ID_BACKWAD_COMP_3, 0x10);		/* enable display fifo */
	wcrt(CRT_ID_HOR_DISP_ENA_END, HDE);
	wcrt(CRT_ID_START_HOR_BLANK , HBS);
	wcrt(CRT_ID_END_HOR_BLANK, (HBS + HBW) & 0x1f);
	wcrt(CRT_ID_START_HOR_RETR, HSS);
	wcrt(CRT_ID_END_HOR_RETR,			/* cr5 */
		((HSS + HSW) & 0x1f) |
		(((HBS + HBW) & 0x20) ? 0x80 : 0x00));
	wcrt(CRT_ID_VER_TOTAL, VT);
	wcrt(CRT_ID_OVERFLOW,
		((VT & 0x100) ? 0x01 : 0x00) |
		((VDE & 0x100) ? 0x02 : 0x00) |
		((VSS & 0x100) ? 0x04 : 0x00) |
		((VBS & 0x100) ? 0x08 : 0x00) |
		0x10 |
		((VT & 0x200) ? 0x20 : 0x00) |
		((VDE & 0x200) ? 0x40 : 0x00) |
		((VSS & 0x200) ? 0x80 : 0x00));
	wcrt(CRT_ID_MAX_SCAN_LINE,
		(dblscan ? 0x80 : 0x00) |
		0x40 |
		((VBS & 0x200) ? 0x20 : 0x00));
	wcrt(CRT_ID_LINE_COMPARE, 0xff);
	wcrt(CRT_ID_LACE_RETR_START, HT / 2);		/* (HT-5)/2 ? */
	wcrt(CRT_ID_LACE_CONTROL, (lace ? 0x20 : 0x00));

	wcrt(CRT_ID_SCREEN_OFFSET, SCO);
	wcrt(CRT_ID_EXT_SYS_CNTL_2, (SCO >> 4) & 0x30 );

	/* wait for vert sync before cr67 update */

	for (i=0; i < 10000; i++) {
		udelay(10);
		mb();
		if (rb_mmio(GREG_INPUT_STATUS1_R) & 0x08)
			break;
	}

	wl_mmio(0x8200, 0x0000c000);	/* fifo control  (0x00110400 ?) */
	wcrt(CRT_ID_EXT_MISC_CNTL_2, cr67);

/* enable video */

	tmp = rb_mmio(ACT_ADDRESS_RESET);
	wb_mmio(ACT_ADDRESS_W, ((bpp == 8) ? 0x20 : 0x00));	/* set b5, ENB PLT in attr idx reg) */
	tmp = rb_mmio(ACT_ADDRESS_RESET);

/* turn gfx on again */

	gfx_on_off(0);

/* pass-through */

	SetVSwitch(1);		/* cv3d */

	DUMP;
	DPRINTK("EXIT\n");
}

static inline void gfx_on_off(int toggle)
{
	unsigned char tmp;

	DPRINTK("ENTER gfx %s\n", (toggle ? "off" : "on"));

	toggle = (toggle & 0x01) << 5;
	tmp = rseq(SEQ_ID_CLOCKING_MODE) & (~(0x01 << 5));
	wseq(SEQ_ID_CLOCKING_MODE, tmp | toggle);

	DPRINTK("EXIT\n");
}

#if defined (VIRGEFBDUMP)

/*
 * Dump board registers
 */

static void cv64_dump(void)
{
	int i;
	u8 c, b;
        u16 w;
	u32 l;

	/* crt, seq, gfx and atr regs */

	SelectMMIO;

	printk("\n");
	for (i = 0; i <= 0x6f; i++) {
		wb_mmio(CRT_ADDRESS, i);
		printk("crt idx : 0x%2.2x : 0x%2.2x\n", i, rb_mmio(CRT_ADDRESS_R));
	}
	for (i = 0; i <= 0x1c; i++) {
		wb_mmio(SEQ_ADDRESS, i);
		printk("seq idx : 0x%2.2x : 0x%2.2x\n", i, rb_mmio(SEQ_ADDRESS_R));
	}
	for (i = 0; i <= 8; i++) {
		wb_mmio(GCT_ADDRESS, i);
		printk("gfx idx : 0x%2.2x : 0x%2.2x\n", i, rb_mmio(GCT_ADDRESS_R));
	}
	for (i = 0; i <= 0x14; i++) {
		c = rb_mmio(ACT_ADDRESS_RESET);
		wb_mmio(ACT_ADDRESS_W, i);
		printk("atr idx : 0x%2.2x : 0x%2.2x\n", i, rb_mmio(ACT_ADDRESS_R));
	}

	/* re-enable video access to palette */

	c = rb_mmio(ACT_ADDRESS_RESET);
	udelay(10);
	wb_mmio(ACT_ADDRESS_W, 0x20);
	c = rb_mmio(ACT_ADDRESS_RESET);
	udelay(10);

	/* general regs */

	printk("0x3cc(w 0x3c2) : 0x%2.2x\n", rb_mmio(0x3cc));	/* GREG_MISC_OUTPUT READ */
	printk("0x3c2(-------) : 0x%2.2x\n", rb_mmio(0x3c2));	/* GREG_INPUT_STATUS 0 READ */
	printk("0x3c3(w 0x3c3) : 0x%2.2x\n", rb_vgaio(0x3c3));	/* GREG_VIDEO_SUBS_ENABLE */
	printk("0x3ca(w 0x3da) : 0x%2.2x\n", rb_vgaio(0x3ca));	/* GREG_FEATURE_CONTROL read */
	printk("0x3da(-------) : 0x%2.2x\n", rb_mmio(0x3da));	/* GREG_INPUT_STATUS 1 READ */

	/* engine regs */

	for (i = 0x8180; i <= 0x8200; i = i + 4)
		printk("0x%8.8x : 0x%8.8x\n", i, rl_mmio(i));

	i = 0x8504;
	printk("0x%8.8x : 0x%8.8x\n", i, rl_mmio(i));
	i = 0x850c;
	printk("0x%8.8x : 0x%8.8x\n", i, rl_mmio(i));
	for (i = 0xa4d4; i <= 0xa50c; i = i + 4)
		printk("0x%8.8x : 0x%8.8x\n", i, rl_mmio(i));

	/* PCI regs */

	SelectCFG;

	for (c = 0; c < 0x08; c = c + 2) {
		w = (*((u16 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)) ^ 2)));
		printk("pci 0x%2.2x : 0x%4.4x\n", c, w);
	}
	c = 8;
	l = (*((u32 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)))));
	printk("pci 0x%2.2x : 0x%8.8x\n", c, l);
	c = 0x0d;
	b = (*((u8 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)) ^ 3)));
	printk("pci 0x%2.2x : 0x%2.2x\n", c, b);
	c = 0x10;
	l = (*((u32 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)))));
	printk("pci 0x%2.2x : 0x%8.8x\n", c, l);
	c = 0x30;
	l = (*((u32 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)))));
	printk("pci 0x%2.2x : 0x%8.8x\n", c, l);
	c = 0x3c;
	b = (*((u8 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)) ^ 3)));
	printk("pci 0x%2.2x : 0x%2.2x\n", c, b);
	c = 0x3d;
	b = (*((u8 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)) ^ 3)));
	printk("pci 0x%2.2x : 0x%2.2x\n", c, b);
	c = 0x3e;
	w = (*((u16 *)((u32)(vgaio_regs + c + (on_zorro2 ? 0 : 0x000e0000)) ^ 2)));
	printk("pci 0x%2.2x : 0x%4.4x\n", c, w);
	SelectMMIO;
}
#endif
