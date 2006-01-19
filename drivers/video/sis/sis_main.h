/*
 * SiS 300/305/540/630(S)/730(S),
 * SiS 315[E|PRO]/550/[M]65x/[M]66x[F|M|G]X/[M]74x[GX]/330/[M]76x[GX],
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

#ifndef _SISFB_MAIN
#define _SISFB_MAIN

#include "vstruct.h"
#include "sis.h"

/* Fbcon stuff */
static struct fb_var_screeninfo my_default_var = {
	.xres            = 0,
	.yres            = 0,
	.xres_virtual    = 0,
	.yres_virtual    = 0,
	.xoffset         = 0,
	.yoffset         = 0,
	.bits_per_pixel  = 0,
	.grayscale       = 0,
	.red             = {0, 8, 0},
	.green           = {0, 8, 0},
	.blue            = {0, 8, 0},
	.transp          = {0, 0, 0},
	.nonstd          = 0,
	.activate        = FB_ACTIVATE_NOW,
	.height          = -1,
	.width           = -1,
	.accel_flags     = 0,
	.pixclock        = 0,
	.left_margin     = 0,
	.right_margin    = 0,
	.upper_margin    = 0,
	.lower_margin    = 0,
	.hsync_len       = 0,
	.vsync_len       = 0,
	.sync            = 0,
	.vmode           = FB_VMODE_NONINTERLACED,
};

#define MODE_INDEX_NONE           0  /* index for mode=none */

/* Boot-time parameters */
static int sisfb_off = 0;
static int sisfb_parm_mem = 0;
static int sisfb_accel = -1;
static int sisfb_ypan = -1;
static int sisfb_max = -1;
static int sisfb_userom = 1;
static int sisfb_useoem = -1;
#ifdef MODULE
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
static int sisfb_mode_idx = -1;
#else
static int sisfb_mode_idx = MODE_INDEX_NONE;  /* Don't use a mode by default if we are a module */
#endif
#else
static int sisfb_mode_idx = -1;               /* Use a default mode if we are inside the kernel */
#endif
static int sisfb_parm_rate = -1;
static int sisfb_crt1off = 0;
static int sisfb_forcecrt1 = -1;
static int sisfb_crt2type  = -1;	/* CRT2 type (for overriding autodetection) */
static int sisfb_crt2flags = 0;
static int sisfb_pdc = 0xff;
static int sisfb_pdca = 0xff;
static int sisfb_scalelcd = -1;
static int sisfb_specialtiming = CUT_NONE;
static int sisfb_lvdshl = -1;
static int sisfb_dstn = 0;
static int sisfb_fstn = 0;
static int sisfb_tvplug = -1;		/* Tv plug type (for overriding autodetection) */
static int sisfb_tvstd  = -1;
static int sisfb_tvxposoffset = 0;
static int sisfb_tvyposoffset = 0;
static int sisfb_nocrt2rate = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int  sisfb_inverse = 0;
static char sisfb_fontname[40];
#endif
#if !defined(__i386__) && !defined(__x86_64__)
static int sisfb_resetcard = 0;
static int sisfb_videoram = 0;
#endif

/* List of supported chips */
static struct sisfb_chip_info {
	int		chip;
	int		vgaengine;
	int		mni;
	int		hwcursor_size;
	int		CRT2_write_enable;
	const char	*chip_name;
} sisfb_chip_info[] __devinitdata = {
	{ SIS_300,    SIS_300_VGA, 0, HW_CURSOR_AREA_SIZE_300 * 2, SIS_CRT2_WENABLE_300, "SiS 300/305" },
	{ SIS_540,    SIS_300_VGA, 0, HW_CURSOR_AREA_SIZE_300 * 2, SIS_CRT2_WENABLE_300, "SiS 540" },
	{ SIS_630,    SIS_300_VGA, 0, HW_CURSOR_AREA_SIZE_300 * 2, SIS_CRT2_WENABLE_300, "SiS 630" },
	{ SIS_315H,   SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 315H" },
	{ SIS_315,    SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 315" },
	{ SIS_315PRO, SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 315PRO" },
	{ SIS_550,    SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 55x" },
	{ SIS_650,    SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 650" },
	{ SIS_330,    SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 330" },
	{ SIS_660,    SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "SiS 660" },
	{ XGI_20,     SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "XGI Z7" },
	{ XGI_40,     SIS_315_VGA, 1, HW_CURSOR_AREA_SIZE_315 * 4, SIS_CRT2_WENABLE_315, "XGI V3XT/V5/V8" },
};

static struct pci_device_id __devinitdata sisfb_pci_table[] = {
#ifdef CONFIG_FB_SIS_300
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_300,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_540_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2},
#endif
#ifdef CONFIG_FB_SIS_315
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315H,    PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315PRO,  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_550_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_650_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_330,     PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8},
	{ PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_660_VGA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 9},
	{ PCI_VENDOR_ID_XGI,PCI_DEVICE_ID_XGI_20,     PCI_ANY_ID, PCI_ANY_ID, 0, 0,10},
	{ PCI_VENDOR_ID_XGI,PCI_DEVICE_ID_XGI_40,     PCI_ANY_ID, PCI_ANY_ID, 0, 0,11},
#endif
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, sisfb_pci_table);

static struct sis_video_info *card_list = NULL;

/* The memory heap is now handled card-wise, by using
   sis_malloc_new/sis_free_new. However, the DRM does
   not do this yet. Until it does, we keep a "global"
   heap which is actually the first card's one.
 */
static struct SIS_HEAP	*sisfb_heap;

#define MD_SIS300 1
#define MD_SIS315 2

/* Mode table */
static const struct _sisbios_mode {
	char name[15];
	u8  mode_no[2];
	u16 vesa_mode_no_1;  /* "SiS defined" VESA mode number */
	u16 vesa_mode_no_2;  /* Real VESA mode numbers */
	u16 xres;
	u16 yres;
	u16 bpp;
	u16 rate_idx;
	u16 cols;
	u16 rows;
	u8  chipset;
} sisbios_mode[] = {
/*0*/	{"none",         {0xff,0xff}, 0x0000, 0x0000,    0,    0,  0, 0,   0,  0, MD_SIS300|MD_SIS315},
	{"320x200x8",    {0x59,0x59}, 0x0138, 0x0000,  320,  200,  8, 1,  40, 12, MD_SIS300|MD_SIS315},
	{"320x200x16",   {0x41,0x41}, 0x010e, 0x0000,  320,  200, 16, 1,  40, 12, MD_SIS300|MD_SIS315},
	{"320x200x24",   {0x4f,0x4f}, 0x0000, 0x0000,  320,  200, 32, 1,  40, 12, MD_SIS300|MD_SIS315},  /* That's for people who mix up color- and fb depth */
	{"320x200x32",   {0x4f,0x4f}, 0x0000, 0x0000,  320,  200, 32, 1,  40, 12, MD_SIS300|MD_SIS315},
	{"320x240x8",    {0x50,0x50}, 0x0132, 0x0000,  320,  240,  8, 1,  40, 15, MD_SIS300|MD_SIS315},
	{"320x240x16",   {0x56,0x56}, 0x0135, 0x0000,  320,  240, 16, 1,  40, 15, MD_SIS300|MD_SIS315},
	{"320x240x24",   {0x53,0x53}, 0x0000, 0x0000,  320,  240, 32, 1,  40, 15, MD_SIS300|MD_SIS315},
	{"320x240x32",   {0x53,0x53}, 0x0000, 0x0000,  320,  240, 32, 1,  40, 15, MD_SIS300|MD_SIS315},
#define MODE_FSTN_8	9
#define MODE_FSTN_16	10
	{"320x240x8",    {0x5a,0x5a}, 0x0132, 0x0000,  320,  240,  8, 1,  40, 15,           MD_SIS315},  /* FSTN */
/*10*/	{"320x240x16",   {0x5b,0x5b}, 0x0135, 0x0000,  320,  240, 16, 1,  40, 15,           MD_SIS315},  /* FSTN */
	{"400x300x8",    {0x51,0x51}, 0x0133, 0x0000,  400,  300,  8, 1,  50, 18, MD_SIS300|MD_SIS315},
	{"400x300x16",   {0x57,0x57}, 0x0136, 0x0000,  400,  300, 16, 1,  50, 18, MD_SIS300|MD_SIS315},
	{"400x300x24",   {0x54,0x54}, 0x0000, 0x0000,  400,  300, 32, 1,  50, 18, MD_SIS300|MD_SIS315},
	{"400x300x32",   {0x54,0x54}, 0x0000, 0x0000,  400,  300, 32, 1,  50, 18, MD_SIS300|MD_SIS315},
	{"512x384x8",    {0x52,0x52}, 0x0000, 0x0000,  512,  384,  8, 1,  64, 24, MD_SIS300|MD_SIS315},
	{"512x384x16",   {0x58,0x58}, 0x0000, 0x0000,  512,  384, 16, 1,  64, 24, MD_SIS300|MD_SIS315},
	{"512x384x24",   {0x5c,0x5c}, 0x0000, 0x0000,  512,  384, 32, 1,  64, 24, MD_SIS300|MD_SIS315},
	{"512x384x32",   {0x5c,0x5c}, 0x0000, 0x0000,  512,  384, 32, 1,  64, 24, MD_SIS300|MD_SIS315},
	{"640x400x8",    {0x2f,0x2f}, 0x0000, 0x0000,  640,  400,  8, 1,  80, 25, MD_SIS300|MD_SIS315},
/*20*/	{"640x400x16",   {0x5d,0x5d}, 0x0000, 0x0000,  640,  400, 16, 1,  80, 25, MD_SIS300|MD_SIS315},
	{"640x400x24",   {0x5e,0x5e}, 0x0000, 0x0000,  640,  400, 32, 1,  80, 25, MD_SIS300|MD_SIS315},
	{"640x400x32",   {0x5e,0x5e}, 0x0000, 0x0000,  640,  400, 32, 1,  80, 25, MD_SIS300|MD_SIS315},
	{"640x480x8",    {0x2e,0x2e}, 0x0101, 0x0101,  640,  480,  8, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"640x480x16",   {0x44,0x44}, 0x0111, 0x0111,  640,  480, 16, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"640x480x24",   {0x62,0x62}, 0x013a, 0x0112,  640,  480, 32, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"640x480x32",   {0x62,0x62}, 0x013a, 0x0112,  640,  480, 32, 1,  80, 30, MD_SIS300|MD_SIS315},
	{"720x480x8",    {0x31,0x31}, 0x0000, 0x0000,  720,  480,  8, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x480x16",   {0x33,0x33}, 0x0000, 0x0000,  720,  480, 16, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x480x24",   {0x35,0x35}, 0x0000, 0x0000,  720,  480, 32, 1,  90, 30, MD_SIS300|MD_SIS315},
/*30*/	{"720x480x32",   {0x35,0x35}, 0x0000, 0x0000,  720,  480, 32, 1,  90, 30, MD_SIS300|MD_SIS315},
	{"720x576x8",    {0x32,0x32}, 0x0000, 0x0000,  720,  576,  8, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x16",   {0x34,0x34}, 0x0000, 0x0000,  720,  576, 16, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x24",   {0x36,0x36}, 0x0000, 0x0000,  720,  576, 32, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"720x576x32",   {0x36,0x36}, 0x0000, 0x0000,  720,  576, 32, 1,  90, 36, MD_SIS300|MD_SIS315},
	{"768x576x8",    {0x5f,0x5f}, 0x0000, 0x0000,  768,  576,  8, 1,  96, 36, MD_SIS300|MD_SIS315},
	{"768x576x16",   {0x60,0x60}, 0x0000, 0x0000,  768,  576, 16, 1,  96, 36, MD_SIS300|MD_SIS315},
	{"768x576x24",   {0x61,0x61}, 0x0000, 0x0000,  768,  576, 32, 1,  96, 36, MD_SIS300|MD_SIS315},
	{"768x576x32",   {0x61,0x61}, 0x0000, 0x0000,  768,  576, 32, 1,  96, 36, MD_SIS300|MD_SIS315},
	{"800x480x8",    {0x70,0x70}, 0x0000, 0x0000,  800,  480,  8, 1, 100, 30, MD_SIS300|MD_SIS315},
/*40*/	{"800x480x16",   {0x7a,0x7a}, 0x0000, 0x0000,  800,  480, 16, 1, 100, 30, MD_SIS300|MD_SIS315},
	{"800x480x24",   {0x76,0x76}, 0x0000, 0x0000,  800,  480, 32, 1, 100, 30, MD_SIS300|MD_SIS315},
	{"800x480x32",   {0x76,0x76}, 0x0000, 0x0000,  800,  480, 32, 1, 100, 30, MD_SIS300|MD_SIS315},
#define DEFAULT_MODE		43 /* index for 800x600x8 */
#define DEFAULT_LCDMODE		43 /* index for 800x600x8 */
#define DEFAULT_TVMODE		43 /* index for 800x600x8 */
	{"800x600x8",    {0x30,0x30}, 0x0103, 0x0103,  800,  600,  8, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x16",   {0x47,0x47}, 0x0114, 0x0114,  800,  600, 16, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x24",   {0x63,0x63}, 0x013b, 0x0115,  800,  600, 32, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"800x600x32",   {0x63,0x63}, 0x013b, 0x0115,  800,  600, 32, 2, 100, 37, MD_SIS300|MD_SIS315},
	{"848x480x8",    {0x39,0x39}, 0x0000, 0x0000,  848,  480,  8, 2, 106, 30, MD_SIS300|MD_SIS315},
#define DEFAULT_MODE_848	48
	{"848x480x16",   {0x3b,0x3b}, 0x0000, 0x0000,  848,  480, 16, 2, 106, 30, MD_SIS300|MD_SIS315},
	{"848x480x24",   {0x3e,0x3e}, 0x0000, 0x0000,  848,  480, 32, 2, 106, 30, MD_SIS300|MD_SIS315},
/*50*/	{"848x480x32",   {0x3e,0x3e}, 0x0000, 0x0000,  848,  480, 32, 2, 106, 30, MD_SIS300|MD_SIS315},
	{"856x480x8",    {0x3f,0x3f}, 0x0000, 0x0000,  856,  480,  8, 2, 107, 30, MD_SIS300|MD_SIS315},
#define DEFAULT_MODE_856	52
	{"856x480x16",   {0x42,0x42}, 0x0000, 0x0000,  856,  480, 16, 2, 107, 30, MD_SIS300|MD_SIS315},
	{"856x480x24",   {0x45,0x45}, 0x0000, 0x0000,  856,  480, 32, 2, 107, 30, MD_SIS300|MD_SIS315},
	{"856x480x32",   {0x45,0x45}, 0x0000, 0x0000,  856,  480, 32, 2, 107, 30, MD_SIS300|MD_SIS315},
	{"960x540x8",    {0x1d,0x1d}, 0x0000, 0x0000,  960,  540,  8, 1, 120, 33,           MD_SIS315},
	{"960x540x16",   {0x1e,0x1e}, 0x0000, 0x0000,  960,  540, 16, 1, 120, 33,           MD_SIS315},
	{"960x540x24",   {0x1f,0x1f}, 0x0000, 0x0000,  960,  540, 32, 1, 120, 33,           MD_SIS315},
	{"960x540x32",   {0x1f,0x1f}, 0x0000, 0x0000,  960,  540, 32, 1, 120, 33,           MD_SIS315},
	{"960x600x8",    {0x20,0x20}, 0x0000, 0x0000,  960,  600,  8, 1, 120, 37,           MD_SIS315},
/*60*/	{"960x600x16",   {0x21,0x21}, 0x0000, 0x0000,  960,  600, 16, 1, 120, 37,           MD_SIS315},
	{"960x600x24",   {0x22,0x22}, 0x0000, 0x0000,  960,  600, 32, 1, 120, 37,           MD_SIS315},
	{"960x600x32",   {0x22,0x22}, 0x0000, 0x0000,  960,  600, 32, 1, 120, 37,           MD_SIS315},
	{"1024x576x8",   {0x71,0x71}, 0x0000, 0x0000, 1024,  576,  8, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x16",  {0x74,0x74}, 0x0000, 0x0000, 1024,  576, 16, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x24",  {0x77,0x77}, 0x0000, 0x0000, 1024,  576, 32, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x576x32",  {0x77,0x77}, 0x0000, 0x0000, 1024,  576, 32, 1, 128, 36, MD_SIS300|MD_SIS315},
	{"1024x600x8",   {0x20,0x20}, 0x0000, 0x0000, 1024,  600,  8, 1, 128, 37, MD_SIS300          },
	{"1024x600x16",  {0x21,0x21}, 0x0000, 0x0000, 1024,  600, 16, 1, 128, 37, MD_SIS300          },
	{"1024x600x24",  {0x22,0x22}, 0x0000, 0x0000, 1024,  600, 32, 1, 128, 37, MD_SIS300          },
/*70*/	{"1024x600x32",  {0x22,0x22}, 0x0000, 0x0000, 1024,  600, 32, 1, 128, 37, MD_SIS300          },
	{"1024x768x8",   {0x38,0x38}, 0x0105, 0x0105, 1024,  768,  8, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x16",  {0x4a,0x4a}, 0x0117, 0x0117, 1024,  768, 16, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x24",  {0x64,0x64}, 0x013c, 0x0118, 1024,  768, 32, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1024x768x32",  {0x64,0x64}, 0x013c, 0x0118, 1024,  768, 32, 2, 128, 48, MD_SIS300|MD_SIS315},
	{"1152x768x8",   {0x23,0x23}, 0x0000, 0x0000, 1152,  768,  8, 1, 144, 48, MD_SIS300          },
	{"1152x768x16",  {0x24,0x24}, 0x0000, 0x0000, 1152,  768, 16, 1, 144, 48, MD_SIS300          },
	{"1152x768x24",  {0x25,0x25}, 0x0000, 0x0000, 1152,  768, 32, 1, 144, 48, MD_SIS300          },
	{"1152x768x32",  {0x25,0x25}, 0x0000, 0x0000, 1152,  768, 32, 1, 144, 48, MD_SIS300          },
	{"1152x864x8",   {0x29,0x29}, 0x0000, 0x0000, 1152,  864,  8, 1, 144, 54, MD_SIS300|MD_SIS315},
/*80*/	{"1152x864x16",  {0x2a,0x2a}, 0x0000, 0x0000, 1152,  864, 16, 1, 144, 54, MD_SIS300|MD_SIS315},
	{"1152x864x24",  {0x2b,0x2b}, 0x0000, 0x0000, 1152,  864, 32, 1, 144, 54, MD_SIS300|MD_SIS315},
	{"1152x864x32",  {0x2b,0x2b}, 0x0000, 0x0000, 1152,  864, 32, 1, 144, 54, MD_SIS300|MD_SIS315},
	{"1280x720x8",   {0x79,0x79}, 0x0000, 0x0000, 1280,  720,  8, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x16",  {0x75,0x75}, 0x0000, 0x0000, 1280,  720, 16, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x24",  {0x78,0x78}, 0x0000, 0x0000, 1280,  720, 32, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x720x32",  {0x78,0x78}, 0x0000, 0x0000, 1280,  720, 32, 1, 160, 45, MD_SIS300|MD_SIS315},
	{"1280x768x8",   {0x55,0x23}, 0x0000, 0x0000, 1280,  768,  8, 1, 160, 48, MD_SIS300|MD_SIS315},
	{"1280x768x16",  {0x5a,0x24}, 0x0000, 0x0000, 1280,  768, 16, 1, 160, 48, MD_SIS300|MD_SIS315},
	{"1280x768x24",  {0x5b,0x25}, 0x0000, 0x0000, 1280,  768, 32, 1, 160, 48, MD_SIS300|MD_SIS315},
/*90*/	{"1280x768x32",  {0x5b,0x25}, 0x0000, 0x0000, 1280,  768, 32, 1, 160, 48, MD_SIS300|MD_SIS315},
	{"1280x800x8",   {0x14,0x14}, 0x0000, 0x0000, 1280,  800,  8, 1, 160, 50,           MD_SIS315},
	{"1280x800x16",  {0x15,0x15}, 0x0000, 0x0000, 1280,  800, 16, 1, 160, 50,           MD_SIS315},
	{"1280x800x24",  {0x16,0x16}, 0x0000, 0x0000, 1280,  800, 32, 1, 160, 50,           MD_SIS315},
	{"1280x800x32",  {0x16,0x16}, 0x0000, 0x0000, 1280,  800, 32, 1, 160, 50,           MD_SIS315},
	{"1280x854x8",   {0x14,0x14}, 0x0000, 0x0000, 1280,  854,  8, 1, 160, 53,           MD_SIS315},
	{"1280x854x16",  {0x15,0x15}, 0x0000, 0x0000, 1280,  854, 16, 1, 160, 53,           MD_SIS315},
	{"1280x854x24",  {0x16,0x16}, 0x0000, 0x0000, 1280,  854, 32, 1, 160, 53,           MD_SIS315},
	{"1280x854x32",  {0x16,0x16}, 0x0000, 0x0000, 1280,  854, 32, 1, 160, 53,           MD_SIS315},
	{"1280x960x8",   {0x7c,0x7c}, 0x0000, 0x0000, 1280,  960,  8, 1, 160, 60, MD_SIS300|MD_SIS315},
/*100*/	{"1280x960x16",  {0x7d,0x7d}, 0x0000, 0x0000, 1280,  960, 16, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x960x24",  {0x7e,0x7e}, 0x0000, 0x0000, 1280,  960, 32, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x960x32",  {0x7e,0x7e}, 0x0000, 0x0000, 1280,  960, 32, 1, 160, 60, MD_SIS300|MD_SIS315},
	{"1280x1024x8",  {0x3a,0x3a}, 0x0107, 0x0107, 1280, 1024,  8, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x16", {0x4d,0x4d}, 0x011a, 0x011a, 1280, 1024, 16, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x24", {0x65,0x65}, 0x013d, 0x011b, 1280, 1024, 32, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1280x1024x32", {0x65,0x65}, 0x013d, 0x011b, 1280, 1024, 32, 2, 160, 64, MD_SIS300|MD_SIS315},
	{"1360x768x8",   {0x48,0x48}, 0x0000, 0x0000, 1360,  768,  8, 1, 170, 48, MD_SIS300|MD_SIS315},
	{"1360x768x16",  {0x4b,0x4b}, 0x0000, 0x0000, 1360,  768, 16, 1, 170, 48, MD_SIS300|MD_SIS315},
	{"1360x768x24",  {0x4e,0x4e}, 0x0000, 0x0000, 1360,  768, 32, 1, 170, 48, MD_SIS300|MD_SIS315},
/*110*/	{"1360x768x32",  {0x4e,0x4e}, 0x0000, 0x0000, 1360,  768, 32, 1, 170, 48, MD_SIS300|MD_SIS315},
	{"1360x1024x8",  {0x67,0x67}, 0x0000, 0x0000, 1360, 1024,  8, 1, 170, 64, MD_SIS300          },
#define DEFAULT_MODE_1360	112
	{"1360x1024x16", {0x6f,0x6f}, 0x0000, 0x0000, 1360, 1024, 16, 1, 170, 64, MD_SIS300          },
	{"1360x1024x24", {0x72,0x72}, 0x0000, 0x0000, 1360, 1024, 32, 1, 170, 64, MD_SIS300          },
	{"1360x1024x32", {0x72,0x72}, 0x0000, 0x0000, 1360, 1024, 32, 1, 170, 64, MD_SIS300          },
	{"1400x1050x8",  {0x26,0x26}, 0x0000, 0x0000, 1400, 1050,  8, 1, 175, 65,           MD_SIS315},
	{"1400x1050x16", {0x27,0x27}, 0x0000, 0x0000, 1400, 1050, 16, 1, 175, 65,           MD_SIS315},
	{"1400x1050x24", {0x28,0x28}, 0x0000, 0x0000, 1400, 1050, 32, 1, 175, 65,           MD_SIS315},
	{"1400x1050x32", {0x28,0x28}, 0x0000, 0x0000, 1400, 1050, 32, 1, 175, 65,           MD_SIS315},
	{"1600x1200x8",  {0x3c,0x3c}, 0x0130, 0x011c, 1600, 1200,  8, 1, 200, 75, MD_SIS300|MD_SIS315},
/*120*/	{"1600x1200x16", {0x3d,0x3d}, 0x0131, 0x011e, 1600, 1200, 16, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1600x1200x24", {0x66,0x66}, 0x013e, 0x011f, 1600, 1200, 32, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1600x1200x32", {0x66,0x66}, 0x013e, 0x011f, 1600, 1200, 32, 1, 200, 75, MD_SIS300|MD_SIS315},
	{"1680x1050x8",  {0x17,0x17}, 0x0000, 0x0000, 1680, 1050,  8, 1, 210, 65,           MD_SIS315},
	{"1680x1050x16", {0x18,0x18}, 0x0000, 0x0000, 1680, 1050, 16, 1, 210, 65,           MD_SIS315},
	{"1680x1050x24", {0x19,0x19}, 0x0000, 0x0000, 1680, 1050, 32, 1, 210, 65,           MD_SIS315},
	{"1680x1050x32", {0x19,0x19}, 0x0000, 0x0000, 1680, 1050, 32, 1, 210, 65,           MD_SIS315},
	{"1920x1080x8",  {0x2c,0x2c}, 0x0000, 0x0000, 1920, 1080,  8, 1, 240, 67,           MD_SIS315},
	{"1920x1080x16", {0x2d,0x2d}, 0x0000, 0x0000, 1920, 1080, 16, 1, 240, 67,           MD_SIS315},
	{"1920x1080x24", {0x73,0x73}, 0x0000, 0x0000, 1920, 1080, 32, 1, 240, 67,           MD_SIS315},
/*130*/	{"1920x1080x32", {0x73,0x73}, 0x0000, 0x0000, 1920, 1080, 32, 1, 240, 67,           MD_SIS315},
	{"1920x1440x8",  {0x68,0x68}, 0x013f, 0x0000, 1920, 1440,  8, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x16", {0x69,0x69}, 0x0140, 0x0000, 1920, 1440, 16, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x24", {0x6b,0x6b}, 0x0141, 0x0000, 1920, 1440, 32, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"1920x1440x32", {0x6b,0x6b}, 0x0141, 0x0000, 1920, 1440, 32, 1, 240, 75, MD_SIS300|MD_SIS315},
	{"2048x1536x8",  {0x6c,0x6c}, 0x0000, 0x0000, 2048, 1536,  8, 1, 256, 96,           MD_SIS315},
	{"2048x1536x16", {0x6d,0x6d}, 0x0000, 0x0000, 2048, 1536, 16, 1, 256, 96,           MD_SIS315},
	{"2048x1536x24", {0x6e,0x6e}, 0x0000, 0x0000, 2048, 1536, 32, 1, 256, 96,           MD_SIS315},
	{"2048x1536x32", {0x6e,0x6e}, 0x0000, 0x0000, 2048, 1536, 32, 1, 256, 96,           MD_SIS315},
	{"\0", {0x00,0x00}, 0, 0, 0, 0, 0, 0, 0}
};

#define SIS_LCD_NUMBER 18
static struct _sis_lcd_data {
	u32 lcdtype;
	u16 xres;
	u16 yres;
	u8  default_mode_idx;
} sis_lcd_data[] __devinitdata = {
	{ LCD_640x480,    640,  480,  23 },
	{ LCD_800x600,    800,  600,  43 },
	{ LCD_1024x600,  1024,  600,  67 },
	{ LCD_1024x768,  1024,  768,  71 },
	{ LCD_1152x768,  1152,  768,  75 },
	{ LCD_1152x864,  1152,  864,  79 },
	{ LCD_1280x720,  1280,  720,  83 },
	{ LCD_1280x768,  1280,  768,  87 },
	{ LCD_1280x800,  1280,  800,  91 },
	{ LCD_1280x854,  1280,  854,  95 },
	{ LCD_1280x960,  1280,  960,  99 },
	{ LCD_1280x1024, 1280, 1024, 103 },
	{ LCD_1400x1050, 1400, 1050, 115 },
	{ LCD_1680x1050, 1680, 1050, 123 },
	{ LCD_1600x1200, 1600, 1200, 119 },
	{ LCD_320x240_2,  320,  240,   9 },
	{ LCD_320x240_3,  320,  240,   9 },
	{ LCD_320x240,    320,  240,   9 },
};

/* CR36 evaluation */
static unsigned short sis300paneltype[] __devinitdata = {
	LCD_UNKNOWN,   LCD_800x600,   LCD_1024x768,  LCD_1280x1024,
	LCD_1280x960,  LCD_640x480,   LCD_1024x600,  LCD_1152x768,
	LCD_UNKNOWN,   LCD_UNKNOWN,   LCD_UNKNOWN,   LCD_UNKNOWN,
	LCD_UNKNOWN,   LCD_UNKNOWN,   LCD_UNKNOWN,   LCD_UNKNOWN
};

static unsigned short sis310paneltype[] __devinitdata = {
	LCD_UNKNOWN,   LCD_800x600,   LCD_1024x768,  LCD_1280x1024,
	LCD_640x480,   LCD_1024x600,  LCD_1152x864,  LCD_1280x960,
	LCD_1152x768,  LCD_1400x1050, LCD_1280x768,  LCD_1600x1200,
	LCD_320x240_2, LCD_320x240_3, LCD_UNKNOWN,   LCD_UNKNOWN
};

static unsigned short sis661paneltype[] __devinitdata = {
	LCD_UNKNOWN,   LCD_800x600,   LCD_1024x768,  LCD_1280x1024,
	LCD_640x480,   LCD_1024x600,  LCD_1152x864,  LCD_1280x960,
	LCD_1280x854,  LCD_1400x1050, LCD_1280x768,  LCD_1600x1200,
	LCD_1280x800,  LCD_1680x1050, LCD_1280x720,  LCD_UNKNOWN
};

#define FL_550_DSTN 0x01
#define FL_550_FSTN 0x02
#define FL_300      0x04
#define FL_315      0x08

static struct _sis_crt2type {
	char name[32];
	u32 type_no;
	u32 tvplug_no;
	u16 flags;
} sis_crt2type[] __initdata = {
	{"NONE", 	     0, 	-1,                     FL_300|FL_315},
	{"LCD",  	     CRT2_LCD, 	-1,                     FL_300|FL_315},
	{"TV",   	     CRT2_TV, 	-1,                     FL_300|FL_315},
	{"VGA",  	     CRT2_VGA, 	-1,                     FL_300|FL_315},
	{"SVIDEO", 	     CRT2_TV, 	TV_SVIDEO,              FL_300|FL_315},
	{"COMPOSITE", 	     CRT2_TV, 	TV_AVIDEO,              FL_300|FL_315},
	{"CVBS", 	     CRT2_TV, 	TV_AVIDEO,              FL_300|FL_315},
	{"SVIDEO+COMPOSITE", CRT2_TV,   TV_AVIDEO|TV_SVIDEO,    FL_300|FL_315},
	{"COMPOSITE+SVIDEO", CRT2_TV,   TV_AVIDEO|TV_SVIDEO,    FL_300|FL_315},
	{"SVIDEO+CVBS",      CRT2_TV,   TV_AVIDEO|TV_SVIDEO,    FL_300|FL_315},
	{"CVBS+SVIDEO",      CRT2_TV,   TV_AVIDEO|TV_SVIDEO,    FL_300|FL_315},
	{"SCART", 	     CRT2_TV, 	TV_SCART,               FL_300|FL_315},
	{"HIVISION",	     CRT2_TV,   TV_HIVISION,            FL_315},
	{"YPBPR480I",	     CRT2_TV,   TV_YPBPR|TV_YPBPR525I,  FL_315},
	{"YPBPR480P",	     CRT2_TV,   TV_YPBPR|TV_YPBPR525P,  FL_315},
	{"YPBPR720P",	     CRT2_TV,   TV_YPBPR|TV_YPBPR750P,  FL_315},
	{"YPBPR1080I",	     CRT2_TV,   TV_YPBPR|TV_YPBPR1080I, FL_315},
	{"DSTN",             CRT2_LCD,  -1,                     FL_315|FL_550_DSTN},
	{"FSTN",             CRT2_LCD,  -1,                     FL_315|FL_550_FSTN},
	{"\0",  	     -1, 	-1,                     0}
};

/* TV standard */
static struct _sis_tvtype {
	char name[6];
	u32 type_no;
} sis_tvtype[] __initdata = {
	{"PAL",  	TV_PAL},
	{"NTSC", 	TV_NTSC},
	{"PALM",  	TV_PAL|TV_PALM},
	{"PALN",  	TV_PAL|TV_PALN},
	{"NTSCJ",  	TV_NTSC|TV_NTSCJ},
	{"\0",   	-1}
};

static const struct _sis_vrate {
	u16 idx;
	u16 xres;
	u16 yres;
	u16 refresh;
	BOOLEAN SiS730valid32bpp;
} sisfb_vrate[] = {
	{1,  320,  200,  70,  TRUE},
	{1,  320,  240,  60,  TRUE},
	{1,  400,  300,  60,  TRUE},
	{1,  512,  384,  60,  TRUE},
	{1,  640,  400,  72,  TRUE},
	{1,  640,  480,  60,  TRUE}, {2,  640,  480,  72,  TRUE}, {3,  640,  480,  75,  TRUE},
	{4,  640,  480,  85,  TRUE}, {5,  640,  480, 100,  TRUE}, {6,  640,  480, 120,  TRUE},
	{7,  640,  480, 160,  TRUE}, {8,  640,  480, 200,  TRUE},
	{1,  720,  480,  60,  TRUE},
	{1,  720,  576,  58,  TRUE},
	{1,  768,  576,  58,  TRUE},
	{1,  800,  480,  60,  TRUE}, {2,  800,  480,  75,  TRUE}, {3,  800,  480,  85,  TRUE},
	{1,  800,  600,  56,  TRUE}, {2,  800,  600,  60,  TRUE}, {3,  800,  600,  72,  TRUE},
	{4,  800,  600,  75,  TRUE}, {5,  800,  600,  85,  TRUE}, {6,  800,  600, 105,  TRUE},
	{7,  800,  600, 120,  TRUE}, {8,  800,  600, 160,  TRUE},
	{1,  848,  480,  39,  TRUE}, {2,  848,  480,  60,  TRUE},
	{1,  856,  480,  39,  TRUE}, {2,  856,  480,  60,  TRUE},
	{1,  960,  540,  60,  TRUE},
	{1,  960,  600,  60,  TRUE},
	{1, 1024,  576,  60,  TRUE}, {2, 1024,  576,  75,  TRUE}, {3, 1024,  576,  85,  TRUE},
	{1, 1024,  600,  60,  TRUE},
	{1, 1024,  768,  43,  TRUE}, {2, 1024,  768,  60,  TRUE}, {3, 1024,  768,  70, FALSE},
	{4, 1024,  768,  75, FALSE}, {5, 1024,  768,  85,  TRUE}, {6, 1024,  768, 100,  TRUE},
	{7, 1024,  768, 120,  TRUE},
	{1, 1152,  768,  60,  TRUE},
	{1, 1152,  864,  60,  TRUE}, {2, 1152,  864,  75,  TRUE}, {3, 1152,  864,  84,  TRUE},
	{1, 1280,  720,  60,  TRUE}, {2, 1280,  720,  75,  TRUE}, {3, 1280,  720,  85,  TRUE},
	{1, 1280,  768,  60,  TRUE},
	{1, 1280,  800,  60,  TRUE},
	{1, 1280,  854,  60,  TRUE},
	{1, 1280,  960,  60,  TRUE}, {2, 1280,  960,  85,  TRUE},
	{1, 1280, 1024,  43,  TRUE}, {2, 1280, 1024,  60,  TRUE}, {3, 1280, 1024,  75,  TRUE},
	{4, 1280, 1024,  85,  TRUE},
	{1, 1360,  768,  60,  TRUE},
	{1, 1360, 1024,  59,  TRUE},
	{1, 1400, 1050,  60,  TRUE}, {2, 1400, 1050,  75,  TRUE},
	{1, 1600, 1200,  60,  TRUE}, {2, 1600, 1200,  65,  TRUE}, {3, 1600, 1200,  70,  TRUE},
	{4, 1600, 1200,  75,  TRUE}, {5, 1600, 1200,  85,  TRUE}, {6, 1600, 1200, 100,  TRUE},
	{7, 1600, 1200, 120,  TRUE},
	{1, 1680, 1050,  60,  TRUE},
	{1, 1920, 1080,  30,  TRUE},
	{1, 1920, 1440,  60,  TRUE}, {2, 1920, 1440,  65,  TRUE}, {3, 1920, 1440,  70,  TRUE},
	{4, 1920, 1440,  75,  TRUE}, {5, 1920, 1440,  85,  TRUE}, {6, 1920, 1440, 100,  TRUE},
	{1, 2048, 1536,  60,  TRUE}, {2, 2048, 1536,  65,  TRUE}, {3, 2048, 1536,  70,  TRUE},
	{4, 2048, 1536,  75,  TRUE}, {5, 2048, 1536,  85,  TRUE},
	{0,    0,    0,   0, FALSE}
};

static struct _sisfbddcsmodes {
	u32 mask;
	u16 h;
	u16 v;
	u32 d;
} sisfb_ddcsmodes[] __devinitdata = {
	{ 0x10000, 67, 75, 108000},
	{ 0x08000, 48, 72,  50000},
	{ 0x04000, 46, 75,  49500},
	{ 0x01000, 35, 43,  44900},
	{ 0x00800, 48, 60,  65000},
	{ 0x00400, 56, 70,  75000},
	{ 0x00200, 60, 75,  78800},
	{ 0x00100, 80, 75, 135000},
	{ 0x00020, 31, 60,  25200},
	{ 0x00008, 38, 72,  31500},
	{ 0x00004, 37, 75,  31500},
	{ 0x00002, 35, 56,  36000},
	{ 0x00001, 38, 60,  40000}
};

static struct _sisfbddcfmodes {
	u16 x;
	u16 y;
	u16 v;
	u16 h;
	u32 d;
} sisfb_ddcfmodes[] __devinitdata = {
	{ 1280, 1024, 85, 92, 157500},
	{ 1600, 1200, 60, 75, 162000},
	{ 1600, 1200, 65, 82, 175500},
	{ 1600, 1200, 70, 88, 189000},
	{ 1600, 1200, 75, 94, 202500},
	{ 1600, 1200, 85, 107,229500},
	{ 1920, 1440, 60, 90, 234000},
	{ 1920, 1440, 75, 113,297000}
};

#ifdef CONFIG_FB_SIS_300
static struct _chswtable {
	u16  subsysVendor;
	u16  subsysCard;
	char *vendorName;
	char *cardName;
} mychswtable[] __devinitdata = {
	{ 0x1631, 0x1002, "Mitachi", "0x1002" },
	{ 0x1071, 0x7521, "Mitac"  , "7521P"  },
	{ 0,      0,      ""       , ""       }
};
#endif

static struct _customttable {
	u16   chipID;
	char  *biosversion;
	char  *biosdate;
	u32   bioschksum;
	u16   biosFootprintAddr[5];
	u8    biosFootprintData[5];
	u16   pcisubsysvendor;
	u16   pcisubsyscard;
	char  *vendorName;
	char  *cardName;
	u32   SpecialID;
	char  *optionName;
} mycustomttable[] __devinitdata = {
	{ SIS_630, "2.00.07", "09/27/2002-13:38:25",
	  0x3240A8,
	  { 0x220, 0x227, 0x228, 0x229, 0x0ee },
	  {  0x01,  0xe3,  0x9a,  0x6a,  0xef },
	  0x1039, 0x6300,
	  "Barco", "iQ R200L/300/400", CUT_BARCO1366, "BARCO_1366"
	},
	{ SIS_630, "2.00.07", "09/27/2002-13:38:25",
	  0x323FBD,
	  { 0x220, 0x227, 0x228, 0x229, 0x0ee },
	  {  0x00,  0x5a,  0x64,  0x41,  0xef },
	  0x1039, 0x6300,
	  "Barco", "iQ G200L/300/400/500", CUT_BARCO1024, "BARCO_1024"
	},
	{ SIS_650, "", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x0e11, 0x083c,
	  "Inventec (Compaq)", "3017cl/3045US", CUT_COMPAQ12802, "COMPAQ_1280"
	},
	{ SIS_650, "", "",
	  0,
	  { 0x00c, 0, 0, 0, 0 },
	  { 'e'  , 0, 0, 0, 0 },
	  0x1558, 0x0287,
	  "Clevo", "L285/L287 (Version 1)", CUT_CLEVO1024, "CLEVO_L28X_1"
	},
	{ SIS_650, "", "",
	  0,
	  { 0x00c, 0, 0, 0, 0 },
	  { 'y'  , 0, 0, 0, 0 },
	  0x1558, 0x0287,
	  "Clevo", "L285/L287 (Version 2)", CUT_CLEVO10242, "CLEVO_L28X_2"
	},
	{ SIS_650, "", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1558, 0x0400,  /* possibly 401 and 402 as well; not panelsize specific (?) */
	  "Clevo", "D400S/D410S/D400H/D410H", CUT_CLEVO1400, "CLEVO_D4X0"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1558, 0x2263,
	  "Clevo", "D22ES/D27ES", CUT_UNIWILL1024, "CLEVO_D2X0ES"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1734, 0x101f,
	  "Uniwill", "N243S9", CUT_UNIWILL1024, "UNIWILL_N243S9"
	},
	{ SIS_650, "", "",
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1584, 0x5103,
	  "Uniwill", "N35BS1", CUT_UNIWILL10242, "UNIWILL_N35BS1"
	},
	{ SIS_650, "1.09.2c", "",  /* Other versions, too? */
	  0,	/* Shift LCD in LCD-via-CRT1 mode */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1019, 0x0f05,
	  "ECS", "A928", CUT_UNIWILL1024, "ECS_A928"
	},
	{ SIS_740, "1.11.27a", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "L3000D/L3500D", CUT_ASUSL3000D, "ASUS_L3X00"
	},
	{ SIS_650, "1.10.9k", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1025, 0x0028,
	  "Acer", "Aspire 1700", CUT_ACER1280, "ACER_ASPIRE1700"
	},
	{ SIS_650, "1.10.7w", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x14c0, 0x0012,
	  "Compal", "??? (V1)", CUT_COMPAL1400_1, "COMPAL_1400_1"
	},
	{ SIS_650, "1.10.7x", "",
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x14c0, 0x0012,
	  "Compal", "??? (V2)", CUT_COMPAL1400_2, "COMPAL_1400_2"
	},
	{ SIS_650, "1.10.8o", "",
	  0,	/* For EMI (unknown) */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "A2H (V1)", CUT_ASUSA2H_1, "ASUS_A2H_1"
	},
	{ SIS_650, "1.10.8q", "",
	  0,	/* For EMI */
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0x1043, 0x1612,
	  "Asus", "A2H (V2)", CUT_ASUSA2H_2, "ASUS_A2H_2"
	},
	{ 4321, "", "",			/* never autodetected */
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0, 0,
	  "Generic", "LVDS/Parallel 848x480", CUT_PANEL848, "PANEL848x480"
	},
	{ 4322, "", "",			/* never autodetected */
	  0,
	  { 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0 },
	  0, 0,
	  "Generic", "LVDS/Parallel 856x480", CUT_PANEL856, "PANEL856x480"
	},
	{ 0, "", "",
	  0,
	  { 0, 0, 0, 0 },
	  { 0, 0, 0, 0 },
	  0, 0,
	  "", "", CUT_NONE, ""
	}
};

/* ---------------------- Prototypes ------------------------- */

/* Interface used by the world */
#ifndef MODULE
SISINITSTATIC int sisfb_setup(char *options);
#endif

/* Interface to the low level console driver */
SISINITSTATIC int sisfb_init(void);

/* fbdev routines */
static int	sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
				struct fb_info *info);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
static int	sisfb_get_fix(struct fb_fix_screeninfo *fix,
				int con,
				struct fb_info *info);
static int	sisfb_get_var(struct fb_var_screeninfo *var,
				int con,
				struct fb_info *info);
static int	sisfb_set_var(struct fb_var_screeninfo *var,
				int con,
				struct fb_info *info);
static void	sisfb_crtc_to_var(struct sis_video_info *ivideo,
				struct fb_var_screeninfo *var);
static int	sisfb_get_cmap(struct fb_cmap *cmap,
				int kspc,
				int con,
				struct fb_info *info);
static int	sisfb_set_cmap(struct fb_cmap *cmap,
				int kspc,
				int con,
				struct fb_info *info);
static int	sisfb_update_var(int con,
				struct fb_info *info);
static int	sisfb_switch(int con,
			     struct fb_info *info);
static void	sisfb_blank(int blank,
				struct fb_info *info);
static void	sisfb_set_disp(int con,
				struct fb_var_screeninfo *var,
				struct fb_info *info);
static int	sis_getcolreg(unsigned regno, unsigned *red, unsigned *green,
				unsigned *blue, unsigned *transp,
				struct fb_info *fb_info);
static void	sisfb_do_install_cmap(int con,
				struct fb_info *info);
static int	sisfb_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg, int con,
				struct fb_info *info);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15)
static int	sisfb_ioctl(struct fb_info *info, unsigned int cmd,
			    unsigned long arg);
#else
static int	sisfb_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg,
				struct fb_info *info);
#endif
static int	sisfb_set_par(struct fb_info *info);
static int	sisfb_blank(int blank,
				struct fb_info *info);
extern void	fbcon_sis_fillrect(struct fb_info *info,
				const struct fb_fillrect *rect);
extern void	fbcon_sis_copyarea(struct fb_info *info,
				const struct fb_copyarea *area);
extern int	fbcon_sis_sync(struct fb_info *info);
#endif

/* Internal 2D accelerator functions */
extern int	sisfb_initaccel(struct sis_video_info *ivideo);
extern void	sisfb_syncaccel(struct sis_video_info *ivideo);

/* Internal general routines */
static void	sisfb_search_mode(char *name, BOOLEAN quiet);
static int	sisfb_validate_mode(struct sis_video_info *ivideo, int modeindex, u32 vbflags);
static u8	sisfb_search_refresh_rate(struct sis_video_info *ivideo, unsigned int rate,
				int index);
static int	sisfb_setcolreg(unsigned regno, unsigned red, unsigned green,
				unsigned blue, unsigned transp,
				struct fb_info *fb_info);
static int	sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
				struct fb_info *info);
static void	sisfb_pre_setmode(struct sis_video_info *ivideo);
static void	sisfb_post_setmode(struct sis_video_info *ivideo);
static BOOLEAN	sisfb_CheckVBRetrace(struct sis_video_info *ivideo);
static BOOLEAN	sisfbcheckvretracecrt2(struct sis_video_info *ivideo);
static BOOLEAN	sisfbcheckvretracecrt1(struct sis_video_info *ivideo);
static BOOLEAN	sisfb_bridgeisslave(struct sis_video_info *ivideo);
static void	sisfb_detect_VB_connect(struct sis_video_info *ivideo);
static void	sisfb_get_VB_type(struct sis_video_info *ivideo);
static void	sisfb_set_TVxposoffset(struct sis_video_info *ivideo, int val);
static void	sisfb_set_TVyposoffset(struct sis_video_info *ivideo, int val);
#ifdef CONFIG_FB_SIS_300
unsigned int	sisfb_read_nbridge_pci_dword(struct SiS_Private *SiS_Pr, int reg);
void		sisfb_write_nbridge_pci_dword(struct SiS_Private *SiS_Pr, int reg, unsigned int val);
unsigned int	sisfb_read_lpc_pci_dword(struct SiS_Private *SiS_Pr, int reg);
#endif
#ifdef CONFIG_FB_SIS_315
void		sisfb_write_nbridge_pci_byte(struct SiS_Private *SiS_Pr, int reg, unsigned char val);
unsigned int	sisfb_read_mio_pci_word(struct SiS_Private *SiS_Pr, int reg);
#endif

/* SiS-specific exported functions */
void			sis_malloc(struct sis_memreq *req);
void			sis_malloc_new(struct pci_dev *pdev, struct sis_memreq *req);
void			sis_free(u32 base);
void			sis_free_new(struct pci_dev *pdev, u32 base);

/* Internal heap routines */
static int		sisfb_heap_init(struct sis_video_info *ivideo);
static struct SIS_OH *	sisfb_poh_new_node(struct SIS_HEAP *memheap);
static struct SIS_OH *	sisfb_poh_allocate(struct SIS_HEAP *memheap, u32 size);
static void		sisfb_delete_node(struct SIS_OH *poh);
static void		sisfb_insert_node(struct SIS_OH *pohList, struct SIS_OH *poh);
static struct SIS_OH *	sisfb_poh_free(struct SIS_HEAP *memheap, u32 base);
static void		sisfb_free_node(struct SIS_HEAP *memheap, struct SIS_OH *poh);

/* Routines from init.c/init301.c */
extern unsigned short	SiS_GetModeID_LCD(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, BOOLEAN FSTN, unsigned short CustomT,
				int LCDwith, int LCDheight, unsigned int VBFlags2);
extern unsigned short	SiS_GetModeID_TV(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, unsigned int VBFlags2);
extern unsigned short	SiS_GetModeID_VGA2(int VGAEngine, unsigned int VBFlags, int HDisplay,
				int VDisplay, int Depth, unsigned int VBFlags2);
extern void		SiSRegInit(struct SiS_Private *SiS_Pr, SISIOADDRESS BaseAddr);
extern BOOLEAN		SiSSetMode(struct SiS_Private *SiS_Pr, unsigned short ModeNo);
extern void		SiS_SetEnableDstn(struct SiS_Private *SiS_Pr, int enable);
extern void		SiS_SetEnableFstn(struct SiS_Private *SiS_Pr, int enable);

extern BOOLEAN		SiSDetermineROMLayout661(struct SiS_Private *SiS_Pr);

extern BOOLEAN		sisfb_gettotalfrommode(struct SiS_Private *SiS_Pr, unsigned char modeno,
				int *htotal, int *vtotal, unsigned char rateindex);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
extern int		sisfb_mode_rate_to_dclock(struct SiS_Private *SiS_Pr,
				unsigned char modeno, unsigned char rateindex);
extern int		sisfb_mode_rate_to_ddata(struct SiS_Private *SiS_Pr, unsigned char modeno,
				unsigned char rateindex, struct fb_var_screeninfo *var);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)
extern void		SiS_Generic_ConvertCRData(struct SiS_Private *SiS_Pr, unsigned char *crdata, int xres,
				int yres, struct fb_var_screeninfo *var, BOOLEAN writeres);
#endif

/* Chrontel TV, DDC and DPMS functions */
extern unsigned short	SiS_GetCH700x(struct SiS_Private *SiS_Pr, unsigned short reg);
extern void		SiS_SetCH700x(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val);
extern unsigned short	SiS_GetCH701x(struct SiS_Private *SiS_Pr, unsigned short reg);
extern void		SiS_SetCH701x(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val);
extern void		SiS_SetCH70xxANDOR(struct SiS_Private *SiS_Pr, unsigned short reg,
				unsigned char myor, unsigned char myand);
extern void		SiS_DDC2Delay(struct SiS_Private *SiS_Pr, unsigned int delaytime);
extern void		SiS_SetChrontelGPIO(struct SiS_Private *SiS_Pr, unsigned short myvbinfo);
extern unsigned short	SiS_HandleDDC(struct SiS_Private *SiS_Pr, unsigned int VBFlags, int VGAEngine,
				unsigned short adaptnum, unsigned short DDCdatatype, unsigned char *buffer,
				unsigned int VBFlags2);
extern unsigned short	SiS_ReadDDC1Bit(struct SiS_Private *SiS_Pr);
#ifdef CONFIG_FB_SIS_315
extern void		SiS_Chrontel701xBLOn(struct SiS_Private *SiS_Pr);
extern void		SiS_Chrontel701xBLOff(struct SiS_Private *SiS_Pr);
#endif
extern void		SiS_SiS30xBLOn(struct SiS_Private *SiS_Pr);
extern void		SiS_SiS30xBLOff(struct SiS_Private *SiS_Pr);
#endif


