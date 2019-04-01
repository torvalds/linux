/*
 * Silicon Motion SM712 frame buffer device
 *
 * Copyright (C) 2006 Silicon Motion Technology Corp.
 * Authors:	Ge Wang, gewang@siliconmotion.com
 *		Boyod boyod.yang@siliconmotion.com.cn
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin, wuzhangjin@gmail.com
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#define FB_ACCEL_SMI_LYNX 88

#define SCREEN_X_RES      1024
#define SCREEN_Y_RES      600
#define SCREEN_BPP        16

#define dac_reg	(0x3c8)
#define dac_val	(0x3c9)

extern void __iomem *smtc_regbaseaddress;
#define smtc_mmiowb(dat, reg)	writeb(dat, smtc_regbaseaddress + reg)

#define smtc_mmiorb(reg)	readb(smtc_regbaseaddress + reg)

#define SIZE_SR00_SR04      (0x04 - 0x00 + 1)
#define SIZE_SR10_SR24      (0x24 - 0x10 + 1)
#define SIZE_SR30_SR75      (0x75 - 0x30 + 1)
#define SIZE_SR80_SR93      (0x93 - 0x80 + 1)
#define SIZE_SRA0_SRAF      (0xAF - 0xA0 + 1)
#define SIZE_GR00_GR08      (0x08 - 0x00 + 1)
#define SIZE_AR00_AR14      (0x14 - 0x00 + 1)
#define SIZE_CR00_CR18      (0x18 - 0x00 + 1)
#define SIZE_CR30_CR4D      (0x4D - 0x30 + 1)
#define SIZE_CR90_CRA7      (0xA7 - 0x90 + 1)

static inline void smtc_crtcw(int reg, int val)
{
	smtc_mmiowb(reg, 0x3d4);
	smtc_mmiowb(val, 0x3d5);
}

static inline void smtc_grphw(int reg, int val)
{
	smtc_mmiowb(reg, 0x3ce);
	smtc_mmiowb(val, 0x3cf);
}

static inline void smtc_attrw(int reg, int val)
{
	smtc_mmiorb(0x3da);
	smtc_mmiowb(reg, 0x3c0);
	smtc_mmiorb(0x3c1);
	smtc_mmiowb(val, 0x3c0);
}

static inline void smtc_seqw(int reg, int val)
{
	smtc_mmiowb(reg, 0x3c4);
	smtc_mmiowb(val, 0x3c5);
}

static inline unsigned int smtc_seqr(int reg)
{
	smtc_mmiowb(reg, 0x3c4);
	return smtc_mmiorb(0x3c5);
}

/* The next structure holds all information relevant for a specific video mode.
 */

struct modeinit {
	int mmsizex;
	int mmsizey;
	int bpp;
	int hz;
	unsigned char init_misc;
	unsigned char init_sr00_sr04[SIZE_SR00_SR04];
	unsigned char init_sr10_sr24[SIZE_SR10_SR24];
	unsigned char init_sr30_sr75[SIZE_SR30_SR75];
	unsigned char init_sr80_sr93[SIZE_SR80_SR93];
	unsigned char init_sra0_sraf[SIZE_SRA0_SRAF];
	unsigned char init_gr00_gr08[SIZE_GR00_GR08];
	unsigned char init_ar00_ar14[SIZE_AR00_AR14];
	unsigned char init_cr00_cr18[SIZE_CR00_CR18];
	unsigned char init_cr30_cr4d[SIZE_CR30_CR4D];
	unsigned char init_cr90_cra7[SIZE_CR90_CRA7];
};

#ifdef __BIG_ENDIAN
#define pal_rgb(r, g, b, val)	(((r & 0xf800) >> 8) | \
				((g & 0xe000) >> 13) | \
				((g & 0x1c00) << 3) | \
				((b & 0xf800) >> 3))
#define big_addr		0x800000
#define mmio_addr		0x00800000
#define seqw17()		smtc_seqw(0x17, 0x30)
#define big_pixel_depth(p, d)	{if (p == 24) {p = 32; d = 32; } }
#define big_swap(p)		((p & 0xff00ff00 >> 8) | (p & 0x00ff00ff << 8))
#else
#define pal_rgb(r, g, b, val)	val
#define big_addr		0
#define mmio_addr		0x00c00000
#define seqw17()		do { } while (0)
#define big_pixel_depth(p, d)	do { } while (0)
#define big_swap(p)		p
#endif
