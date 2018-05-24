/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Mode initializing code (CRT2 section)
 * for SiS 300/305/540/630/730,
 *     SiS 315/550/[M]650/651/[M]661[FGM]X/[M]74x[GX]/330/[M]76x[GX],
 *     XGI V3XT/V5/V8, Z7
 * (Universal module for Linux kernel framebuffer and X.org/XFree86 4.x)
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 * Formerly based on non-functional code-fragements for 300 series by SiS, Inc.
 * Used by permission.
 *
 */

#if 1
#define SET_EMI		/* 302LV/ELV: Set EMI values */
#endif

#if 1
#define SET_PWD		/* 301/302LV: Set PWD */
#endif

#define COMPAL_HACK	/* Needed for Compal 1400x1050 (EMI) */
#define COMPAQ_HACK	/* Needed for Inventec/Compaq 1280x1024 (EMI) */
#define ASUS_HACK	/* Needed for Asus A2H 1024x768 (EMI) */

#include "init301.h"

#ifdef CONFIG_FB_SIS_300
#include "oem300.h"
#endif

#ifdef CONFIG_FB_SIS_315
#include "oem310.h"
#endif

#define SiS_I2CDELAY      1000
#define SiS_I2CDELAYSHORT  150

static const unsigned char SiS_YPbPrTable[3][64] = {
  {
    0x17,0x1d,0x03,0x09,0x05,0x06,0x0c,0x0c,
    0x94,0x49,0x01,0x0a,0x06,0x0d,0x04,0x0a,
    0x06,0x14,0x0d,0x04,0x0a,0x00,0x85,0x1b,
    0x0c,0x50,0x00,0x97,0x00,0xda,0x4a,0x17,
    0x7d,0x05,0x4b,0x00,0x00,0xe2,0x00,0x02,
    0x03,0x0a,0x65,0x9d /*0x8d*/,0x08,0x92,0x8f,0x40,
    0x60,0x80,0x14,0x90,0x8c,0x60,0x14,0x53 /*0x50*/,
    0x00,0x40,0x44,0x00,0xdb,0x02,0x3b,0x00
  },
  {
    0x33,0x06,0x06,0x09,0x0b,0x0c,0x0c,0x0c,
    0x98,0x0a,0x01,0x0d,0x06,0x0d,0x04,0x0a,
    0x06,0x14,0x0d,0x04,0x0a,0x00,0x85,0x3f,
    0x0c,0x50,0xb2,0x9f,0x16,0x59,0x4f,0x13,
    0xad,0x11,0xad,0x1d,0x40,0x8a,0x3d,0xb8,
    0x51,0x5e,0x60,0x49,0x7d,0x92,0x0f,0x40,
    0x60,0x80,0x14,0x90,0x8c,0x60,0x14,0x4e,
    0x43,0x41,0x11,0x00,0xfc,0xff,0x32,0x00
  },
  {
#if 0 /* OK, but sticks to left edge */
    0x13,0x1d,0xe8,0x09,0x09,0xed,0x0c,0x0c,
    0x98,0x0a,0x01,0x0c,0x06,0x0d,0x04,0x0a,
    0x06,0x14,0x0d,0x04,0x0a,0x00,0x85,0x3f,
    0xed,0x50,0x70,0x9f,0x16,0x59,0x21 /*0x2b*/,0x13,
    0x27,0x0b,0x27,0xfc,0x30,0x27,0x1c,0xb0,
    0x4b,0x4b,0x65 /*0x6f*/,0x2f,0x63,0x92,0x0f,0x40,
    0x60,0x80,0x14,0x90,0x8c,0x60,0x14,0x27,
    0x00,0x40,0x11,0x00,0xfc,0xff,0x32,0x00
#endif
#if 1 /* Perfect */
    0x23,0x2d,0xe8,0x09,0x09,0xed,0x0c,0x0c,
    0x98,0x0a,0x01,0x0c,0x06,0x0d,0x04,0x0a,
    0x06,0x14,0x0d,0x04,0x0a,0x00,0x85,0x3f,
    0xed,0x50,0x70,0x9f,0x16,0x59,0x60,0x13,
    0x27,0x0b,0x27,0xfc,0x30,0x27,0x1c,0xb0,
    0x4b,0x4b,0x6f,0x2f,0x63,0x92,0x0f,0x40,
    0x60,0x80,0x14,0x90,0x8c,0x60,0x14,0x73,
    0x00,0x40,0x11,0x00,0xfc,0xff,0x32,0x00
#endif
  }
};

static const unsigned char SiS_TVPhase[] =
{
	0x21,0xED,0xBA,0x08,	/* 0x00 SiS_NTSCPhase */
	0x2A,0x05,0xE3,0x00,	/* 0x01 SiS_PALPhase */
	0x21,0xE4,0x2E,0x9B,	/* 0x02 SiS_PALMPhase */
	0x21,0xF4,0x3E,0xBA,	/* 0x03 SiS_PALNPhase */
	0x1E,0x8B,0xA2,0xA7,
	0x1E,0x83,0x0A,0xE0,	/* 0x05 SiS_SpecialPhaseM */
	0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,
	0x21,0xF0,0x7B,0xD6,	/* 0x08 SiS_NTSCPhase2 */
	0x2A,0x09,0x86,0xE9,	/* 0x09 SiS_PALPhase2 */
	0x21,0xE6,0xEF,0xA4,	/* 0x0a SiS_PALMPhase2 */
	0x21,0xF6,0x94,0x46,	/* 0x0b SiS_PALNPhase2 */
	0x1E,0x8B,0xA2,0xA7,
	0x1E,0x83,0x0A,0xE0,	/* 0x0d SiS_SpecialPhaseM */
	0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,
	0x1e,0x8c,0x5c,0x7a,	/* 0x10 SiS_SpecialPhase */
	0x25,0xd4,0xfd,0x5e	/* 0x11 SiS_SpecialPhaseJ */
};

static const unsigned char SiS_HiTVGroup3_1[] = {
    0x00, 0x14, 0x15, 0x25, 0x55, 0x15, 0x0b, 0x13,
    0xb1, 0x41, 0x62, 0x62, 0xff, 0xf4, 0x45, 0xa6,
    0x25, 0x2f, 0x67, 0xf6, 0xbf, 0xff, 0x8e, 0x20,
    0xac, 0xda, 0x60, 0xfe, 0x6a, 0x9a, 0x06, 0x10,
    0xd1, 0x04, 0x18, 0x0a, 0xff, 0x80, 0x00, 0x80,
    0x3b, 0x77, 0x00, 0xef, 0xe0, 0x10, 0xb0, 0xe0,
    0x10, 0x4f, 0x0f, 0x0f, 0x05, 0x0f, 0x08, 0x6e,
    0x1a, 0x1f, 0x25, 0x2a, 0x4c, 0xaa, 0x01
};

static const unsigned char SiS_HiTVGroup3_2[] = {
    0x00, 0x14, 0x15, 0x25, 0x55, 0x15, 0x0b, 0x7a,
    0x54, 0x41, 0xe7, 0xe7, 0xff, 0xf4, 0x45, 0xa6,
    0x25, 0x2f, 0x67, 0xf6, 0xbf, 0xff, 0x8e, 0x20,
    0xac, 0x6a, 0x60, 0x2b, 0x52, 0xcd, 0x61, 0x10,
    0x51, 0x04, 0x18, 0x0a, 0x1f, 0x80, 0x00, 0x80,
    0xff, 0xa4, 0x04, 0x2b, 0x94, 0x21, 0x72, 0x94,
    0x26, 0x05, 0x01, 0x0f, 0xed, 0x0f, 0x0a, 0x64,
    0x18, 0x1d, 0x23, 0x28, 0x4c, 0xaa, 0x01
};

/* 301C / 302ELV extended Part2 TV registers (4 tap scaler) */

static const unsigned char SiS_Part2CLVX_1[] = {
    0x00,0x00,
    0x00,0x20,0x00,0x00,0x7F,0x20,0x02,0x7F,0x7D,0x20,0x04,0x7F,0x7D,0x1F,0x06,0x7E,
    0x7C,0x1D,0x09,0x7E,0x7C,0x1B,0x0B,0x7E,0x7C,0x19,0x0E,0x7D,0x7C,0x17,0x11,0x7C,
    0x7C,0x14,0x14,0x7C,0x7C,0x11,0x17,0x7C,0x7D,0x0E,0x19,0x7C,0x7E,0x0B,0x1B,0x7C,
    0x7E,0x09,0x1D,0x7C,0x7F,0x06,0x1F,0x7C,0x7F,0x04,0x20,0x7D,0x00,0x02,0x20,0x7E
};

static const unsigned char SiS_Part2CLVX_2[] = {
    0x00,0x00,
    0x00,0x20,0x00,0x00,0x7F,0x20,0x02,0x7F,0x7D,0x20,0x04,0x7F,0x7D,0x1F,0x06,0x7E,
    0x7C,0x1D,0x09,0x7E,0x7C,0x1B,0x0B,0x7E,0x7C,0x19,0x0E,0x7D,0x7C,0x17,0x11,0x7C,
    0x7C,0x14,0x14,0x7C,0x7C,0x11,0x17,0x7C,0x7D,0x0E,0x19,0x7C,0x7E,0x0B,0x1B,0x7C,
    0x7E,0x09,0x1D,0x7C,0x7F,0x06,0x1F,0x7C,0x7F,0x04,0x20,0x7D,0x00,0x02,0x20,0x7E
};

static const unsigned char SiS_Part2CLVX_3[] = {  /* NTSC, 525i, 525p */
    0xE0,0x01,
    0x04,0x1A,0x04,0x7E,0x03,0x1A,0x06,0x7D,0x01,0x1A,0x08,0x7D,0x00,0x19,0x0A,0x7D,
    0x7F,0x19,0x0C,0x7C,0x7E,0x18,0x0E,0x7C,0x7E,0x17,0x10,0x7B,0x7D,0x15,0x12,0x7C,
    0x7D,0x13,0x13,0x7D,0x7C,0x12,0x15,0x7D,0x7C,0x10,0x17,0x7D,0x7C,0x0E,0x18,0x7E,
    0x7D,0x0C,0x19,0x7E,0x7D,0x0A,0x19,0x00,0x7D,0x08,0x1A,0x01,0x7E,0x06,0x1A,0x02,
    0x58,0x02,
    0x07,0x14,0x07,0x7E,0x06,0x14,0x09,0x7D,0x05,0x14,0x0A,0x7D,0x04,0x13,0x0B,0x7E,
    0x03,0x13,0x0C,0x7E,0x02,0x12,0x0D,0x7F,0x01,0x12,0x0E,0x7F,0x01,0x11,0x0F,0x7F,
    0x00,0x10,0x10,0x00,0x7F,0x0F,0x11,0x01,0x7F,0x0E,0x12,0x01,0x7E,0x0D,0x12,0x03,
    0x7E,0x0C,0x13,0x03,0x7E,0x0B,0x13,0x04,0x7E,0x0A,0x14,0x04,0x7D,0x09,0x14,0x06,
    0x00,0x03,
    0x09,0x0F,0x09,0x7F,0x08,0x0F,0x09,0x00,0x07,0x0F,0x0A,0x00,0x06,0x0F,0x0A,0x01,
    0x06,0x0E,0x0B,0x01,0x05,0x0E,0x0B,0x02,0x04,0x0E,0x0C,0x02,0x04,0x0D,0x0C,0x03,
    0x03,0x0D,0x0D,0x03,0x02,0x0C,0x0D,0x05,0x02,0x0C,0x0E,0x04,0x01,0x0B,0x0E,0x06,
    0x01,0x0B,0x0E,0x06,0x00,0x0A,0x0F,0x07,0x00,0x0A,0x0F,0x07,0x00,0x09,0x0F,0x08,
    0xFF,0xFF
};

static const unsigned char SiS_Part2CLVX_4[] = {   /* PAL */
    0x58,0x02,
    0x05,0x19,0x05,0x7D,0x03,0x19,0x06,0x7E,0x02,0x19,0x08,0x7D,0x01,0x18,0x0A,0x7D,
    0x00,0x18,0x0C,0x7C,0x7F,0x17,0x0E,0x7C,0x7E,0x16,0x0F,0x7D,0x7E,0x14,0x11,0x7D,
    0x7D,0x13,0x13,0x7D,0x7D,0x11,0x14,0x7E,0x7D,0x0F,0x16,0x7E,0x7D,0x0E,0x17,0x7E,
    0x7D,0x0C,0x18,0x7F,0x7D,0x0A,0x18,0x01,0x7D,0x08,0x19,0x02,0x7D,0x06,0x19,0x04,
    0x00,0x03,
    0x08,0x12,0x08,0x7E,0x07,0x12,0x09,0x7E,0x06,0x12,0x0A,0x7E,0x05,0x11,0x0B,0x7F,
    0x04,0x11,0x0C,0x7F,0x03,0x11,0x0C,0x00,0x03,0x10,0x0D,0x00,0x02,0x0F,0x0E,0x01,
    0x01,0x0F,0x0F,0x01,0x01,0x0E,0x0F,0x02,0x00,0x0D,0x10,0x03,0x7F,0x0C,0x11,0x04,
    0x7F,0x0C,0x11,0x04,0x7F,0x0B,0x11,0x05,0x7E,0x0A,0x12,0x06,0x7E,0x09,0x12,0x07,
    0x40,0x02,
    0x04,0x1A,0x04,0x7E,0x02,0x1B,0x05,0x7E,0x01,0x1A,0x07,0x7E,0x00,0x1A,0x09,0x7D,
    0x7F,0x19,0x0B,0x7D,0x7E,0x18,0x0D,0x7D,0x7D,0x17,0x10,0x7C,0x7D,0x15,0x12,0x7C,
    0x7C,0x14,0x14,0x7C,0x7C,0x12,0x15,0x7D,0x7C,0x10,0x17,0x7D,0x7C,0x0D,0x18,0x7F,
    0x7D,0x0B,0x19,0x7F,0x7D,0x09,0x1A,0x00,0x7D,0x07,0x1A,0x02,0x7E,0x05,0x1B,0x02,
    0xFF,0xFF
};

static const unsigned char SiS_Part2CLVX_5[] = {   /* 750p */
    0x00,0x03,
    0x05,0x19,0x05,0x7D,0x03,0x19,0x06,0x7E,0x02,0x19,0x08,0x7D,0x01,0x18,0x0A,0x7D,
    0x00,0x18,0x0C,0x7C,0x7F,0x17,0x0E,0x7C,0x7E,0x16,0x0F,0x7D,0x7E,0x14,0x11,0x7D,
    0x7D,0x13,0x13,0x7D,0x7D,0x11,0x14,0x7E,0x7D,0x0F,0x16,0x7E,0x7D,0x0E,0x17,0x7E,
    0x7D,0x0C,0x18,0x7F,0x7D,0x0A,0x18,0x01,0x7D,0x08,0x19,0x02,0x7D,0x06,0x19,0x04,
    0xFF,0xFF
};

static const unsigned char SiS_Part2CLVX_6[] = {   /* 1080i */
    0x00,0x04,
    0x04,0x1A,0x04,0x7E,0x02,0x1B,0x05,0x7E,0x01,0x1A,0x07,0x7E,0x00,0x1A,0x09,0x7D,
    0x7F,0x19,0x0B,0x7D,0x7E,0x18,0x0D,0x7D,0x7D,0x17,0x10,0x7C,0x7D,0x15,0x12,0x7C,
    0x7C,0x14,0x14,0x7C,0x7C,0x12,0x15,0x7D,0x7C,0x10,0x17,0x7D,0x7C,0x0D,0x18,0x7F,
    0x7D,0x0B,0x19,0x7F,0x7D,0x09,0x1A,0x00,0x7D,0x07,0x1A,0x02,0x7E,0x05,0x1B,0x02,
    0xFF,0xFF,
};

#ifdef CONFIG_FB_SIS_315
/* 661 et al LCD data structure (2.03.00) */
static const unsigned char SiS_LCDStruct661[] = {
    /* 1024x768 */
/*  type|CR37|   HDE   |   VDE   |    HT   |    VT   |   hss    | hse   */
    0x02,0xC0,0x00,0x04,0x00,0x03,0x40,0x05,0x26,0x03,0x10,0x00,0x88,
    0x00,0x02,0x00,0x06,0x00,0x41,0x5A,0x64,0x00,0x00,0x00,0x00,0x04,
    /*  | vss     |    vse  |clck|  clock  |CRT2DataP|CRT2DataP|idx     */
    /*					      VESA    non-VESA  noscale */
    /* 1280x1024 */
    0x03,0xC0,0x00,0x05,0x00,0x04,0x98,0x06,0x2A,0x04,0x30,0x00,0x70,
    0x00,0x01,0x00,0x03,0x00,0x6C,0xF8,0x2F,0x00,0x00,0x00,0x00,0x08,
    /* 1400x1050 */
    0x09,0x20,0x78,0x05,0x1A,0x04,0x98,0x06,0x2A,0x04,0x18,0x00,0x38,
    0x00,0x01,0x00,0x03,0x00,0x6C,0xF8,0x2F,0x00,0x00,0x00,0x00,0x09,
    /* 1600x1200 */
    0x0B,0xE0,0x40,0x06,0xB0,0x04,0x70,0x08,0xE2,0x04,0x40,0x00,0xC0,
    0x00,0x01,0x00,0x03,0x00,0xA2,0x70,0x24,0x00,0x00,0x00,0x00,0x0A,
    /* 1280x768 (_2) */
    0x0A,0xE0,0x00,0x05,0x00,0x03,0x7C,0x06,0x26,0x03,0x30,0x00,0x70,
    0x00,0x03,0x00,0x06,0x00,0x4D,0xC8,0x48,0x00,0x00,0x00,0x00,0x06,
    /* 1280x720 */
    0x0E,0xE0,0x00,0x05,0xD0,0x02,0x80,0x05,0x26,0x03,0x10,0x00,0x20,
    0x00,0x01,0x00,0x06,0x00,0x45,0x9C,0x62,0x00,0x00,0x00,0x00,0x05,
    /* 1280x800 (_2) */
    0x0C,0xE0,0x00,0x05,0x20,0x03,0x10,0x06,0x2C,0x03,0x30,0x00,0x70,
    0x00,0x04,0x00,0x03,0x00,0x49,0xCE,0x1E,0x00,0x00,0x00,0x00,0x09,
    /* 1680x1050 */
    0x0D,0xE0,0x90,0x06,0x1A,0x04,0x6C,0x07,0x2A,0x04,0x1A,0x00,0x4C,
    0x00,0x03,0x00,0x06,0x00,0x79,0xBE,0x44,0x00,0x00,0x00,0x00,0x06,
    /* 1280x800_3 */
    0x0C,0xE0,0x00,0x05,0x20,0x03,0xAA,0x05,0x2E,0x03,0x30,0x00,0x50,
    0x00,0x04,0x00,0x03,0x00,0x47,0xA9,0x10,0x00,0x00,0x00,0x00,0x07,
    /* 800x600 */
    0x01,0xC0,0x20,0x03,0x58,0x02,0x20,0x04,0x74,0x02,0x2A,0x00,0x80,
    0x00,0x06,0x00,0x04,0x00,0x28,0x63,0x4B,0x00,0x00,0x00,0x00,0x00,
    /* 1280x854 */
    0x08,0xE0,0x00,0x05,0x56,0x03,0x80,0x06,0x5d,0x03,0x10,0x00,0x70,
    0x00,0x01,0x00,0x03,0x00,0x54,0x75,0x13,0x00,0x00,0x00,0x00,0x08
};
#endif

#ifdef CONFIG_FB_SIS_300
static unsigned char SiS300_TrumpionData[14][80] = {
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0x7F,0x00,0x80,0x02,
    0x20,0x03,0x0B,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x10,0x00,0x00,0x04,0x23,
    0x00,0x00,0x03,0x28,0x03,0x10,0x05,0x08,0x40,0x10,0x00,0x10,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xBC,0x01,0xFF,0x03,0xFF,0x19,0x01,0x00,0x05,0x09,0x04,0x04,0x05,
    0x04,0x0C,0x09,0x05,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x5A,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0x27,0x00,0x80,0x02,
    0x20,0x03,0x07,0x00,0x5E,0x01,0x0D,0x02,0x60,0x0C,0x30,0x11,0x00,0x00,0x04,0x23,
    0x00,0x00,0x03,0x80,0x03,0x28,0x06,0x08,0x40,0x11,0x00,0x11,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0x90,0x01,0xFF,0x0F,0xF4,0x19,0x01,0x00,0x05,0x01,0x00,0x04,0x05,
    0x04,0x0C,0x02,0x01,0x02,0xB0,0x00,0x00,0x02,0xBA,0xEC,0x57,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0x8A,0x00,0xD8,0x02,
    0x84,0x03,0x16,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x1C,0x00,0x20,0x04,0x23,
    0x00,0x01,0x03,0x53,0x03,0x28,0x06,0x08,0x40,0x1C,0x00,0x16,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xD9,0x01,0xFF,0x0F,0xF4,0x18,0x07,0x05,0x05,0x13,0x04,0x04,0x05,
    0x01,0x0B,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x59,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0x72,0x00,0xD8,0x02,
    0x84,0x03,0x16,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x1C,0x00,0x20,0x04,0x23,
    0x00,0x01,0x03,0x53,0x03,0x28,0x06,0x08,0x40,0x1C,0x00,0x16,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xDA,0x01,0xFF,0x0F,0xF4,0x18,0x07,0x05,0x05,0x13,0x04,0x04,0x05,
    0x01,0x0B,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x55,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x02,0x00,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0x7F,0x00,0x80,0x02,
    0x20,0x03,0x16,0x00,0xE0,0x01,0x0D,0x02,0x60,0x0C,0x30,0x98,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x45,0x03,0x48,0x06,0x08,0x40,0x98,0x00,0x98,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xF4,0x01,0xFF,0x0F,0xF4,0x18,0x01,0x00,0x05,0x01,0x00,0x05,0x05,
    0x04,0x0C,0x08,0x05,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x5B,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x02,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0xBF,0x00,0x20,0x03,
    0x20,0x04,0x0D,0x00,0x58,0x02,0x71,0x02,0x80,0x0C,0x30,0x9A,0x00,0xFA,0x03,0x1D,
    0x00,0x01,0x03,0x22,0x03,0x28,0x06,0x08,0x40,0x98,0x00,0x98,0x04,0x1D,0x00,0x1D,
    0x03,0x11,0x60,0x39,0x03,0x40,0x05,0xF4,0x18,0x07,0x02,0x06,0x04,0x01,0x06,0x0B,
    0x02,0x0A,0x20,0x19,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x5B,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x0D,0x00,0x0D,0x10,0xEF,0x00,0x00,0x04,
    0x40,0x05,0x13,0x00,0x00,0x03,0x26,0x03,0x88,0x0C,0x30,0x90,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x24,0x03,0x28,0x06,0x08,0x40,0x90,0x00,0x90,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0x40,0x05,0xFF,0x0F,0xF4,0x18,0x01,0x00,0x08,0x01,0x00,0x08,0x01,
    0x00,0x08,0x01,0x01,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x5B,0x01,0xBE,0x01,0x00 },
  /* variant 2 */
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0x7F,0x00,0x80,0x02,
    0x20,0x03,0x15,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x18,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x44,0x03,0x28,0x06,0x08,0x40,0x18,0x00,0x18,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xA6,0x01,0xFF,0x03,0xFF,0x19,0x01,0x00,0x05,0x13,0x04,0x04,0x05,
    0x04,0x0C,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x55,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0x7F,0x00,0x80,0x02,
    0x20,0x03,0x15,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x18,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x44,0x03,0x28,0x06,0x08,0x40,0x18,0x00,0x18,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xA6,0x01,0xFF,0x03,0xFF,0x19,0x01,0x00,0x05,0x13,0x04,0x04,0x05,
    0x04,0x0C,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x55,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0x8A,0x00,0xD8,0x02,
    0x84,0x03,0x16,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x1C,0x00,0x20,0x04,0x23,
    0x00,0x01,0x03,0x53,0x03,0x28,0x06,0x08,0x40,0x1C,0x00,0x16,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xDA,0x01,0xFF,0x0F,0xF4,0x18,0x07,0x05,0x05,0x13,0x04,0x04,0x05,
    0x01,0x0B,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x55,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0x72,0x00,0xD8,0x02,
    0x84,0x03,0x16,0x00,0x90,0x01,0xC1,0x01,0x60,0x0C,0x30,0x1C,0x00,0x20,0x04,0x23,
    0x00,0x01,0x03,0x53,0x03,0x28,0x06,0x08,0x40,0x1C,0x00,0x16,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xDA,0x01,0xFF,0x0F,0xF4,0x18,0x07,0x05,0x05,0x13,0x04,0x04,0x05,
    0x01,0x0B,0x13,0x0A,0x02,0xB0,0x00,0x00,0x02,0xBA,0xF0,0x55,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x02,0x00,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0x7F,0x00,0x80,0x02,
    0x20,0x03,0x16,0x00,0xE0,0x01,0x0D,0x02,0x60,0x0C,0x30,0x98,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x45,0x03,0x48,0x06,0x08,0x40,0x98,0x00,0x98,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0xF4,0x01,0xFF,0x0F,0xF4,0x18,0x01,0x00,0x05,0x01,0x00,0x05,0x05,
    0x04,0x0C,0x08,0x05,0x02,0xB0,0x00,0x00,0x02,0xBA,0xEA,0x58,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x02,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0xBF,0x00,0x20,0x03,
    0x20,0x04,0x0D,0x00,0x58,0x02,0x71,0x02,0x80,0x0C,0x30,0x9A,0x00,0xFA,0x03,0x1D,
    0x00,0x01,0x03,0x22,0x03,0x28,0x06,0x08,0x40,0x98,0x00,0x98,0x04,0x1D,0x00,0x1D,
    0x03,0x11,0x60,0x39,0x03,0x40,0x05,0xF4,0x18,0x07,0x02,0x06,0x04,0x01,0x06,0x0B,
    0x02,0x0A,0x20,0x19,0x02,0xB0,0x00,0x00,0x02,0xBA,0xEA,0x58,0x01,0xBE,0x01,0x00 },
  { 0x02,0x0A,0x0A,0x01,0x04,0x01,0x00,0x03,0x11,0x00,0x0D,0x10,0xEF,0x00,0x00,0x04,
    0x40,0x05,0x13,0x00,0x00,0x03,0x26,0x03,0x88,0x0C,0x30,0x90,0x00,0x00,0x04,0x23,
    0x00,0x01,0x03,0x24,0x03,0x28,0x06,0x08,0x40,0x90,0x00,0x90,0x04,0x23,0x00,0x23,
    0x03,0x11,0x60,0x40,0x05,0xFF,0x0F,0xF4,0x18,0x01,0x00,0x08,0x01,0x00,0x08,0x01,
    0x00,0x08,0x01,0x01,0x02,0xB0,0x00,0x00,0x02,0xBA,0xEA,0x58,0x01,0xBE,0x01,0x00 }
};
#endif

#ifdef CONFIG_FB_SIS_315
static void	SiS_Chrontel701xOn(struct SiS_Private *SiS_Pr);
static void	SiS_Chrontel701xOff(struct SiS_Private *SiS_Pr);
static void	SiS_ChrontelInitTVVSync(struct SiS_Private *SiS_Pr);
static void	SiS_ChrontelDoSomething1(struct SiS_Private *SiS_Pr);
#endif /* 315 */

#ifdef CONFIG_FB_SIS_300
static  bool	SiS_SetTrumpionBlock(struct SiS_Private *SiS_Pr, unsigned char *dataptr);
#endif

static unsigned short	SiS_InitDDCRegs(struct SiS_Private *SiS_Pr, unsigned int VBFlags,
				int VGAEngine, unsigned short adaptnum, unsigned short DDCdatatype,
				bool checkcr32, unsigned int VBFlags2);
static unsigned short	SiS_ProbeDDC(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_ReadDDC(struct SiS_Private *SiS_Pr, unsigned short DDCdatatype,
				unsigned char *buffer);
static void		SiS_SetSwitchDDC2(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_SetStart(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_SetStop(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_SetSCLKLow(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_SetSCLKHigh(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_ReadDDC2Data(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_WriteDDC2Data(struct SiS_Private *SiS_Pr, unsigned short tempax);
static unsigned short	SiS_CheckACK(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_WriteDABDDC(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_PrepareReadDDC(struct SiS_Private *SiS_Pr);
static unsigned short	SiS_PrepareDDC(struct SiS_Private *SiS_Pr);
static void		SiS_SendACK(struct SiS_Private *SiS_Pr, unsigned short yesno);
static unsigned short	SiS_DoProbeDDC(struct SiS_Private *SiS_Pr);

#ifdef CONFIG_FB_SIS_300
static void		SiS_OEM300Setting(struct SiS_Private *SiS_Pr,
				unsigned short ModeNo, unsigned short ModeIdIndex, unsigned short RefTabindex);
static void		SetOEMLCDData2(struct SiS_Private *SiS_Pr,
				unsigned short ModeNo, unsigned short ModeIdIndex,unsigned short RefTableIndex);
#endif
#ifdef CONFIG_FB_SIS_315
static void		SiS_OEM310Setting(struct SiS_Private *SiS_Pr,
				unsigned short ModeNo,unsigned short ModeIdIndex, unsigned short RRTI);
static void		SiS_OEM661Setting(struct SiS_Private *SiS_Pr,
				unsigned short ModeNo,unsigned short ModeIdIndex, unsigned short RRTI);
static void		SiS_FinalizeLCD(struct SiS_Private *, unsigned short, unsigned short);
#endif

static unsigned short	SiS_GetBIOSLCDResInfo(struct SiS_Private *SiS_Pr);
static void		SiS_SetCH70xx(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val);

/*********************************************/
/*         HELPER: Lock/Unlock CRT2          */
/*********************************************/

void
SiS_UnLockCRT2(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType == XGI_20)
      return;
   else if(SiS_Pr->ChipType >= SIS_315H)
      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2f,0x01);
   else
      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x24,0x01);
}

static
void
SiS_LockCRT2(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType == XGI_20)
      return;
   else if(SiS_Pr->ChipType >= SIS_315H)
      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2F,0xFE);
   else
      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x24,0xFE);
}

/*********************************************/
/*            HELPER: Write SR11             */
/*********************************************/

static void
SiS_SetRegSR11ANDOR(struct SiS_Private *SiS_Pr, unsigned short DataAND, unsigned short DataOR)
{
   if(SiS_Pr->ChipType >= SIS_661) {
      DataAND &= 0x0f;
      DataOR  &= 0x0f;
   }
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x11,DataAND,DataOR);
}

/*********************************************/
/*    HELPER: Get Pointer to LCD structure   */
/*********************************************/

#ifdef CONFIG_FB_SIS_315
static unsigned char *
GetLCDStructPtr661(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned char  *myptr = NULL;
   unsigned short romindex = 0, reg = 0, idx = 0;

   /* Use the BIOS tables only for LVDS panels; TMDS is unreliable
    * due to the variaty of panels the BIOS doesn't know about.
    * Exception: If the BIOS has better knowledge (such as in case
    * of machines with a 301C and a panel that does not support DDC)
    * use the BIOS data as well.
    */

   if((SiS_Pr->SiS_ROMNew) &&
      ((SiS_Pr->SiS_VBType & VB_SISLVDS) || (!SiS_Pr->PanelSelfDetected))) {

      if(SiS_Pr->ChipType < SIS_661) reg = 0x3c;
      else                           reg = 0x7d;

      idx = (SiS_GetReg(SiS_Pr->SiS_P3d4,reg) & 0x1f) * 26;

      if(idx < (8*26)) {
         myptr = (unsigned char *)&SiS_LCDStruct661[idx];
      }
      romindex = SISGETROMW(0x100);
      if(romindex) {
         romindex += idx;
         myptr = &ROMAddr[romindex];
      }
   }
   return myptr;
}

static unsigned short
GetLCDStructPtr661_2(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short romptr = 0;

   /* Use the BIOS tables only for LVDS panels; TMDS is unreliable
    * due to the variaty of panels the BIOS doesn't know about.
    * Exception: If the BIOS has better knowledge (such as in case
    * of machines with a 301C and a panel that does not support DDC)
    * use the BIOS data as well.
    */

   if((SiS_Pr->SiS_ROMNew) &&
      ((SiS_Pr->SiS_VBType & VB_SISLVDS) || (!SiS_Pr->PanelSelfDetected))) {
      romptr = SISGETROMW(0x102);
      romptr += ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4) * SiS_Pr->SiS661LCD2TableSize);
   }

   return romptr;
}
#endif

/*********************************************/
/*           Adjust Rate for CRT2            */
/*********************************************/

static bool
SiS_AdjustCRT2Rate(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RRTI, unsigned short *i)
{
   unsigned short checkmask=0, modeid, infoflag;

   modeid = SiS_Pr->SiS_RefIndex[RRTI + (*i)].ModeID;

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {

	 checkmask |= SupportRAMDAC2;
	 if(SiS_Pr->ChipType >= SIS_315H) {
	    checkmask |= SupportRAMDAC2_135;
	    if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	       checkmask |= SupportRAMDAC2_162;
	       if(SiS_Pr->SiS_VBType & VB_SISRAMDAC202) {
		  checkmask |= SupportRAMDAC2_202;
	       }
	    }
	 }

      } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {

	 checkmask |= SupportLCD;
	 if(SiS_Pr->ChipType >= SIS_315H) {
	    if(SiS_Pr->SiS_VBType & VB_SISVB) {
	       if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (SiS_Pr->SiS_LCDInfo & LCDPass11)) {
	          if(modeid == 0x2e) checkmask |= Support64048060Hz;
	       }
	    }
	 }

      } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

	 checkmask |= SupportHiVision;

      } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToYPbPr525750|SetCRT2ToAVIDEO|SetCRT2ToSVIDEO|SetCRT2ToSCART)) {

	 checkmask |= SupportTV;
	 if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	    checkmask |= SupportTV1024;
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	       if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) {
	          checkmask |= SupportYPbPr750p;
	       }
	    }
	 }

      }

   } else {	/* LVDS */

      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	    checkmask |= SupportCHTV;
	 }
      }

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	 checkmask |= SupportLCD;
      }

   }

   /* Look backwards in table for matching CRT2 mode */
   for(; SiS_Pr->SiS_RefIndex[RRTI + (*i)].ModeID == modeid; (*i)--) {
      infoflag = SiS_Pr->SiS_RefIndex[RRTI + (*i)].Ext_InfoFlag;
      if(infoflag & checkmask) return true;
      if((*i) == 0) break;
   }

   /* Look through the whole mode-section of the table from the beginning
    * for a matching CRT2 mode if no mode was found yet.
    */
   for((*i) = 0; ; (*i)++) {
      if(SiS_Pr->SiS_RefIndex[RRTI + (*i)].ModeID != modeid) break;
      infoflag = SiS_Pr->SiS_RefIndex[RRTI + (*i)].Ext_InfoFlag;
      if(infoflag & checkmask) return true;
   }
   return false;
}

/*********************************************/
/*              Get rate index               */
/*********************************************/

unsigned short
SiS_GetRatePtr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short RRTI,i,backup_i;
   unsigned short modeflag,index,temp,backupindex;
   static const unsigned short LCDRefreshIndex[] = {
		0x00, 0x00, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01,
		0x00, 0x00, 0x00, 0x00
   };

   /* Do NOT check for UseCustomMode here, will skrew up FIFO */
   if(ModeNo == 0xfe) return 0;

   if(ModeNo <= 0x13) {
      modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
   } else {
      modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
   }

   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	 if(modeflag & HalfDCLK) return 0;
      }
   }

   if(ModeNo < 0x14) return 0xFFFF;

   index = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x33) >> SiS_Pr->SiS_SelectCRT2Rate) & 0x0F;
   backupindex = index;

   if(index > 0) index--;

   if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {
      if(SiS_Pr->SiS_VBType & VB_SISVB) {
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	    if(SiS_Pr->SiS_VBType & VB_NoLCD)		 index = 0;
	    else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index = backupindex = 0;
	 }
	 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	    if(!(SiS_Pr->SiS_VBType & VB_NoLCD)) {
	       temp = LCDRefreshIndex[SiS_GetBIOSLCDResInfo(SiS_Pr)];
	       if(index > temp) index = temp;
	    }
	 }
      } else {
	 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) index = 0;
	 if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) index = 0;
	 }
      }
   }

   RRTI = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
   ModeNo = SiS_Pr->SiS_RefIndex[RRTI].ModeID;

   if(SiS_Pr->ChipType >= SIS_315H) {
      if(!(SiS_Pr->SiS_VBInfo & DriverMode)) {
	 if( (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x105) ||
	     (SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_VESAID == 0x107) ) {
	    if(backupindex <= 1) RRTI++;
	 }
      }
   }

   i = 0;
   do {
      if(SiS_Pr->SiS_RefIndex[RRTI + i].ModeID != ModeNo) break;
      temp = SiS_Pr->SiS_RefIndex[RRTI + i].Ext_InfoFlag;
      temp &= ModeTypeMask;
      if(temp < SiS_Pr->SiS_ModeType) break;
      i++;
      index--;
   } while(index != 0xFFFF);

   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	 temp = SiS_Pr->SiS_RefIndex[RRTI + i - 1].Ext_InfoFlag;
	 if(temp & InterlaceMode) i++;
      }
   }

   i--;

   if((SiS_Pr->SiS_SetFlag & ProgrammingCRT2) && (!(SiS_Pr->SiS_VBInfo & DisableCRT2Display))) {
      backup_i = i;
      if(!(SiS_AdjustCRT2Rate(SiS_Pr, ModeNo, ModeIdIndex, RRTI, &i))) {
	 i = backup_i;
      }
   }

   return (RRTI + i);
}

/*********************************************/
/*            STORE CRT2 INFO in CR34        */
/*********************************************/

static void
SiS_SaveCRT2Info(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   unsigned short temp1, temp2;

   /* Store CRT1 ModeNo in CR34 */
   SiS_SetReg(SiS_Pr->SiS_P3d4,0x34,ModeNo);
   temp1 = (SiS_Pr->SiS_VBInfo & SetInSlaveMode) >> 8;
   temp2 = ~(SetInSlaveMode >> 8);
   SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x31,temp2,temp1);
}

/*********************************************/
/*    HELPER: GET SOME DATA FROM BIOS ROM    */
/*********************************************/

#ifdef CONFIG_FB_SIS_300
static bool
SiS_CR36BIOSWord23b(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short temp,temp1;

   if(SiS_Pr->SiS_UseROM) {
      if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
	 temp = 1 << ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4) & 0x0f);
	 temp1 = SISGETROMW(0x23b);
	 if(temp1 & temp) return true;
      }
   }
   return false;
}

static bool
SiS_CR36BIOSWord23d(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short temp,temp1;

   if(SiS_Pr->SiS_UseROM) {
      if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
	 temp = 1 << ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4) & 0x0f);
	 temp1 = SISGETROMW(0x23d);
	 if(temp1 & temp) return true;
      }
   }
   return false;
}
#endif

/*********************************************/
/*          HELPER: DELAY FUNCTIONS          */
/*********************************************/

void
SiS_DDC2Delay(struct SiS_Private *SiS_Pr, unsigned int delaytime)
{
   while (delaytime-- > 0)
      SiS_GetReg(SiS_Pr->SiS_P3c4, 0x05);
}

#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
static void
SiS_GenericDelay(struct SiS_Private *SiS_Pr, unsigned short delay)
{
   SiS_DDC2Delay(SiS_Pr, delay * 36);
}
#endif

#ifdef CONFIG_FB_SIS_315
static void
SiS_LongDelay(struct SiS_Private *SiS_Pr, unsigned short delay)
{
   while(delay--) {
      SiS_GenericDelay(SiS_Pr, 6623);
   }
}
#endif

#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
static void
SiS_ShortDelay(struct SiS_Private *SiS_Pr, unsigned short delay)
{
   while(delay--) {
      SiS_GenericDelay(SiS_Pr, 66);
   }
}
#endif

static void
SiS_PanelDelay(struct SiS_Private *SiS_Pr, unsigned short DelayTime)
{
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short PanelID, DelayIndex, Delay=0;
#endif

   if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300

      PanelID = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);
      if(SiS_Pr->SiS_VBType & VB_SISVB) {
	 if(SiS_Pr->SiS_VBType & VB_SIS301) PanelID &= 0xf7;
	 if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x18) & 0x10)) PanelID = 0x12;
      }
      DelayIndex = PanelID >> 4;
      if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
	 Delay = 3;
      } else {
	 if(DelayTime >= 2) DelayTime -= 2;
	 if(!(DelayTime & 0x01)) {
	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
	 } else {
	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
	 }
	 if(SiS_Pr->SiS_UseROM) {
	    if(ROMAddr[0x220] & 0x40) {
	       if(!(DelayTime & 0x01)) Delay = (unsigned short)ROMAddr[0x225];
	       else 	    	       Delay = (unsigned short)ROMAddr[0x226];
	    }
	 }
      }
      SiS_ShortDelay(SiS_Pr, Delay);

#endif  /* CONFIG_FB_SIS_300 */

   } else {

#ifdef CONFIG_FB_SIS_315

      if((SiS_Pr->ChipType >= SIS_661)    ||
	 (SiS_Pr->ChipType <= SIS_315PRO) ||
	 (SiS_Pr->ChipType == SIS_330)    ||
	 (SiS_Pr->SiS_ROMNew)) {

	 if(!(DelayTime & 0x01)) {
	    SiS_DDC2Delay(SiS_Pr, 0x1000);
	 } else {
	    SiS_DDC2Delay(SiS_Pr, 0x4000);
	 }

      } else if((SiS_Pr->SiS_IF_DEF_LVDS == 1) /* ||
	 (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	 (SiS_Pr->SiS_CustomT == CUT_CLEVO1400) */ ) {			/* 315 series, LVDS; Special */

	 if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	    PanelID = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);
	    if(SiS_Pr->SiS_CustomT == CUT_CLEVO1400) {
	       if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x1b) & 0x10)) PanelID = 0x12;
	    }
	    if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
	       DelayIndex = PanelID & 0x0f;
	    } else {
	       DelayIndex = PanelID >> 4;
	    }
	    if((DelayTime >= 2) && ((PanelID & 0x0f) == 1))  {
	       Delay = 3;
	    } else {
	       if(DelayTime >= 2) DelayTime -= 2;
	       if(!(DelayTime & 0x01)) {
		  Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[0];
		} else {
		  Delay = SiS_Pr->SiS_PanelDelayTblLVDS[DelayIndex].timer[1];
	       }
	       if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {
		  if(ROMAddr[0x13c] & 0x40) {
		     if(!(DelayTime & 0x01)) {
			Delay = (unsigned short)ROMAddr[0x17e];
		     } else {
			Delay = (unsigned short)ROMAddr[0x17f];
		     }
		  }
	       }
	    }
	    SiS_ShortDelay(SiS_Pr, Delay);
	 }

      } else if(SiS_Pr->SiS_VBType & VB_SISVB) {			/* 315 series, all bridges */

	 DelayIndex = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
	 if(!(DelayTime & 0x01)) {
	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[0];
	 } else {
	    Delay = SiS_Pr->SiS_PanelDelayTbl[DelayIndex].timer[1];
	 }
	 Delay <<= 8;
	 SiS_DDC2Delay(SiS_Pr, Delay);

      }

#endif /* CONFIG_FB_SIS_315 */

   }
}

#ifdef CONFIG_FB_SIS_315
static void
SiS_PanelDelayLoop(struct SiS_Private *SiS_Pr, unsigned short DelayTime, unsigned short DelayLoop)
{
   int i;
   for(i = 0; i < DelayLoop; i++) {
      SiS_PanelDelay(SiS_Pr, DelayTime);
   }
}
#endif

/*********************************************/
/*    HELPER: WAIT-FOR-RETRACE FUNCTIONS     */
/*********************************************/

void
SiS_WaitRetrace1(struct SiS_Private *SiS_Pr)
{
   unsigned short watchdog;

   if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x1f) & 0xc0) return;
   if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x17) & 0x80)) return;

   watchdog = 65535;
   while((SiS_GetRegByte(SiS_Pr->SiS_P3da) & 0x08) && --watchdog);
   watchdog = 65535;
   while((!(SiS_GetRegByte(SiS_Pr->SiS_P3da) & 0x08)) && --watchdog);
}

#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
static void
SiS_WaitRetrace2(struct SiS_Private *SiS_Pr, unsigned short reg)
{
   unsigned short watchdog;

   watchdog = 65535;
   while((SiS_GetReg(SiS_Pr->SiS_Part1Port,reg) & 0x02) && --watchdog);
   watchdog = 65535;
   while((!(SiS_GetReg(SiS_Pr->SiS_Part1Port,reg) & 0x02)) && --watchdog);
}
#endif

static void
SiS_WaitVBRetrace(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	 if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x20)) return;
      }
      if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x80)) {
	 SiS_WaitRetrace1(SiS_Pr);
      } else {
	 SiS_WaitRetrace2(SiS_Pr, 0x25);
      }
#endif
   } else {
#ifdef CONFIG_FB_SIS_315
      if(!(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x40)) {
	 SiS_WaitRetrace1(SiS_Pr);
      } else {
	 SiS_WaitRetrace2(SiS_Pr, 0x30);
      }
#endif
   }
}

static void
SiS_VBWait(struct SiS_Private *SiS_Pr)
{
   unsigned short tempal,temp,i,j;

   temp = 0;
   for(i = 0; i < 3; i++) {
     for(j = 0; j < 100; j++) {
        tempal = SiS_GetRegByte(SiS_Pr->SiS_P3da);
        if(temp & 0x01) {
	   if((tempal & 0x08))  continue;
	   else break;
        } else {
	   if(!(tempal & 0x08)) continue;
	   else break;
        }
     }
     temp ^= 0x01;
   }
}

static void
SiS_VBLongWait(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      SiS_VBWait(SiS_Pr);
   } else {
      SiS_WaitRetrace1(SiS_Pr);
   }
}

/*********************************************/
/*               HELPER: MISC                */
/*********************************************/

#ifdef CONFIG_FB_SIS_300
static bool
SiS_Is301B(struct SiS_Private *SiS_Pr)
{
   if(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x01) >= 0xb0) return true;
   return false;
}
#endif

static bool
SiS_CRT2IsLCD(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType == SIS_730) {
      if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x20) return true;
   }
   if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x30) & 0x20) return true;
   return false;
}

bool
SiS_IsDualEdge(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType >= SIS_315H) {
      if((SiS_Pr->ChipType != SIS_650) || (SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xf0)) {
	 if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x38) & EnableDualEdge) return true;
      }
   }
#endif
   return false;
}

bool
SiS_IsVAMode(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   unsigned short flag;

   if(SiS_Pr->ChipType >= SIS_315H) {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      if((flag & EnableDualEdge) && (flag & SetToLCDA)) return true;
   }
#endif
   return false;
}

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsVAorLCD(struct SiS_Private *SiS_Pr)
{
   if(SiS_IsVAMode(SiS_Pr))  return true;
   if(SiS_CRT2IsLCD(SiS_Pr)) return true;
   return false;
}
#endif

static bool
SiS_IsDualLink(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType >= SIS_315H) {
      if((SiS_CRT2IsLCD(SiS_Pr)) ||
         (SiS_IsVAMode(SiS_Pr))) {
	 if(SiS_Pr->SiS_LCDInfo & LCDDualLink) return true;
      }
   }
#endif
   return false;
}

#ifdef CONFIG_FB_SIS_315
static bool
SiS_TVEnabled(struct SiS_Private *SiS_Pr)
{
   if((SiS_GetReg(SiS_Pr->SiS_Part2Port,0x00) & 0x0f) != 0x0c) return true;
   if(SiS_Pr->SiS_VBType & VB_SISYPBPR) {
      if(SiS_GetReg(SiS_Pr->SiS_Part2Port,0x4d) & 0x10) return true;
   }
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_LCDAEnabled(struct SiS_Private *SiS_Pr)
{
   if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x13) & 0x04) return true;
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_WeHaveBacklightCtrl(struct SiS_Private *SiS_Pr)
{
   if((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->ChipType < SIS_661)) {
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x79) & 0x10) return true;
   }
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsNotM650orLater(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   if(SiS_Pr->ChipType == SIS_650) {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xf0;
      /* Check for revision != A0 only */
      if((flag == 0xe0) || (flag == 0xc0) ||
         (flag == 0xb0) || (flag == 0x90)) return false;
   } else if(SiS_Pr->ChipType >= SIS_661) return false;
   return true;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsYPbPr(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType >= SIS_315H) {
      /* YPrPb = 0x08 */
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x38) & EnableCHYPbPr) return true;
   }
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsChScart(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType >= SIS_315H) {
      /* Scart = 0x04 */
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x38) & EnableCHScart) return true;
   }
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsTVOrYPbPrOrScart(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   if(SiS_Pr->ChipType >= SIS_315H) {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
      if(flag & SetCRT2ToTV)        return true;
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      if(flag & EnableCHYPbPr)      return true;  /* = YPrPb = 0x08 */
      if(flag & EnableCHScart)      return true;  /* = Scart = 0x04 - TW */
   } else {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
      if(flag & SetCRT2ToTV)        return true;
   }
   return false;
}
#endif

#ifdef CONFIG_FB_SIS_315
static bool
SiS_IsLCDOrLCDA(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   if(SiS_Pr->ChipType >= SIS_315H) {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
      if(flag & SetCRT2ToLCD) return true;
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      if(flag & SetToLCDA)    return true;
   } else {
      flag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
      if(flag & SetCRT2ToLCD) return true;
   }
   return false;
}
#endif

static bool
SiS_HaveBridge(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
      return true;
   } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
      flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);
      if((flag == 1) || (flag == 2)) return true;
   }
   return false;
}

static bool
SiS_BridgeIsEnabled(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   if(SiS_HaveBridge(SiS_Pr)) {
      flag = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
      if(SiS_Pr->ChipType < SIS_315H) {
	flag &= 0xa0;
	if((flag == 0x80) || (flag == 0x20)) return true;
      } else {
	flag &= 0x50;
	if((flag == 0x40) || (flag == 0x10)) return true;
      }
   }
   return false;
}

static bool
SiS_BridgeInSlavemode(struct SiS_Private *SiS_Pr)
{
   unsigned short flag1;

   flag1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31);
   if(flag1 & (SetInSlaveMode >> 8)) return true;
   return false;
}

/*********************************************/
/*       GET VIDEO BRIDGE CONFIG INFO        */
/*********************************************/

/* Setup general purpose IO for Chrontel communication */
#ifdef CONFIG_FB_SIS_300
void
SiS_SetChrontelGPIO(struct SiS_Private *SiS_Pr, unsigned short myvbinfo)
{
   unsigned int   acpibase;
   unsigned short temp;

   if(!(SiS_Pr->SiS_ChSW)) return;

   acpibase = sisfb_read_lpc_pci_dword(SiS_Pr, 0x74);
   acpibase &= 0xFFFF;
   if(!acpibase) return;
   temp = SiS_GetRegShort((acpibase + 0x3c));	/* ACPI register 0x3c: GP Event 1 I/O mode select */
   temp &= 0xFEFF;
   SiS_SetRegShort((acpibase + 0x3c), temp);
   temp = SiS_GetRegShort((acpibase + 0x3c));
   temp = SiS_GetRegShort((acpibase + 0x3a));	/* ACPI register 0x3a: GP Pin Level (low/high) */
   temp &= 0xFEFF;
   if(!(myvbinfo & SetCRT2ToTV)) temp |= 0x0100;
   SiS_SetRegShort((acpibase + 0x3a), temp);
   temp = SiS_GetRegShort((acpibase + 0x3a));
}
#endif

void
SiS_GetVBInfo(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, int checkcrt2mode)
{
   unsigned short tempax, tempbx, temp;
   unsigned short modeflag, resinfo = 0;

   SiS_Pr->SiS_SetFlag = 0;

   modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

   SiS_Pr->SiS_ModeType = modeflag & ModeTypeMask;

   if((ModeNo > 0x13) && (!SiS_Pr->UseCustomMode)) {
      resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
   }

   tempbx = 0;

   if(SiS_HaveBridge(SiS_Pr)) {

	temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
	tempbx |= temp;
	tempax = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) << 8;
	tempax &= (DriverMode | LoadDACFlag | SetNotSimuMode | SetPALTV);
	tempbx |= tempax;

#ifdef CONFIG_FB_SIS_315
	if(SiS_Pr->ChipType >= SIS_315H) {
	   if(SiS_Pr->SiS_VBType & VB_SISLCDA) {
	      if(ModeNo == 0x03) {
		 /* Mode 0x03 is never in driver mode */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x31,0xbf);
	      }
	      if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8))) {
		 /* Reset LCDA setting if not driver mode */
		 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0xfc);
	      }
	      if(IS_SIS650) {
		 if(SiS_Pr->SiS_UseLCDA) {
		    if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xF0) {
		       if((ModeNo <= 0x13) || (!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & (DriverMode >> 8)))) {
			  SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x38,(EnableDualEdge | SetToLCDA));
		       }
		    }
		 }
	      }
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	      if((temp & (EnableDualEdge | SetToLCDA)) == (EnableDualEdge | SetToLCDA)) {
		 tempbx |= SetCRT2ToLCDA;
	      }
	   }

	   if(SiS_Pr->ChipType >= SIS_661) { /* New CR layout */
	      tempbx &= ~(SetCRT2ToYPbPr525750 | SetCRT2ToHiVision);
	      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x38) & 0x04) {
		 temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35) & 0xe0;
		 if(temp == 0x60) tempbx |= SetCRT2ToHiVision;
		 else if(SiS_Pr->SiS_VBType & VB_SISYPBPR) {
		    tempbx |= SetCRT2ToYPbPr525750;
		 }
	      }
	   }

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	      if(temp & SetToLCDA) {
		 tempbx |= SetCRT2ToLCDA;
	      }
	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		 if(temp & EnableCHYPbPr) {
		    tempbx |= SetCRT2ToCHYPbPr;
		 }
	      }
	   }
	}

#endif  /* CONFIG_FB_SIS_315 */

        if(!(SiS_Pr->SiS_VBType & VB_SISVGA2)) {
	   tempbx &= ~(SetCRT2ToRAMDAC);
	}

	if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   temp = SetCRT2ToSVIDEO   |
		  SetCRT2ToAVIDEO   |
		  SetCRT2ToSCART    |
		  SetCRT2ToLCDA     |
		  SetCRT2ToLCD      |
		  SetCRT2ToRAMDAC   |
		  SetCRT2ToHiVision |
		  SetCRT2ToYPbPr525750;
	} else {
	   if(SiS_Pr->ChipType >= SIS_315H) {
	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		 temp = SetCRT2ToAVIDEO |
		        SetCRT2ToSVIDEO |
		        SetCRT2ToSCART  |
		        SetCRT2ToLCDA   |
		        SetCRT2ToLCD    |
		        SetCRT2ToCHYPbPr;
	      } else {
		 temp = SetCRT2ToLCDA   |
		        SetCRT2ToLCD;
	      }
	   } else {
	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		 temp = SetCRT2ToTV | SetCRT2ToLCD;
	      } else {
		 temp = SetCRT2ToLCD;
	      }
	   }
	}

	if(!(tempbx & temp)) {
	   tempax = DisableCRT2Display;
	   tempbx = 0;
	}

	if(SiS_Pr->SiS_VBType & VB_SISVB) {

	   unsigned short clearmask = ( DriverMode |
				DisableCRT2Display |
				LoadDACFlag 	   |
				SetNotSimuMode 	   |
				SetInSlaveMode 	   |
				SetPALTV 	   |
				SwitchCRT2	   |
				SetSimuScanMode );

	   if(tempbx & SetCRT2ToLCDA)        tempbx &= (clearmask | SetCRT2ToLCDA);
	   if(tempbx & SetCRT2ToRAMDAC)      tempbx &= (clearmask | SetCRT2ToRAMDAC);
	   if(tempbx & SetCRT2ToLCD)         tempbx &= (clearmask | SetCRT2ToLCD);
	   if(tempbx & SetCRT2ToSCART)       tempbx &= (clearmask | SetCRT2ToSCART);
	   if(tempbx & SetCRT2ToHiVision)    tempbx &= (clearmask | SetCRT2ToHiVision);
	   if(tempbx & SetCRT2ToYPbPr525750) tempbx &= (clearmask | SetCRT2ToYPbPr525750);

	} else {

	   if(SiS_Pr->ChipType >= SIS_315H) {
	      if(tempbx & SetCRT2ToLCDA) {
		 tempbx &= (0xFF00|SwitchCRT2|SetSimuScanMode);
	      }
	   }
	   if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	      if(tempbx & SetCRT2ToTV) {
		 tempbx &= (0xFF00|SetCRT2ToTV|SwitchCRT2|SetSimuScanMode);
	      }
	   }
	   if(tempbx & SetCRT2ToLCD) {
	      tempbx &= (0xFF00|SetCRT2ToLCD|SwitchCRT2|SetSimuScanMode);
	   }
	   if(SiS_Pr->ChipType >= SIS_315H) {
	      if(tempbx & SetCRT2ToLCDA) {
	         tempbx |= SetCRT2ToLCD;
	      }
	   }

	}

	if(tempax & DisableCRT2Display) {
	   if(!(tempbx & (SwitchCRT2 | SetSimuScanMode))) {
	      tempbx = SetSimuScanMode | DisableCRT2Display;
	   }
	}

	if(!(tempbx & DriverMode)) tempbx |= SetSimuScanMode;

	/* LVDS/CHRONTEL (LCD/TV) and 301BDH (LCD) can only be slave in 8bpp modes */
	if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
	       ((SiS_Pr->SiS_VBType & VB_NoLCD) && (tempbx & SetCRT2ToLCD)) ) {
	      modeflag &= (~CRT2Mode);
	   }
	}

	if(!(tempbx & SetSimuScanMode)) {
	   if(tempbx & SwitchCRT2) {
	      if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
		 if(resinfo != SIS_RI_1600x1200) {
		    tempbx |= SetSimuScanMode;
		 }
              }
	   } else {
	      if(SiS_BridgeIsEnabled(SiS_Pr)) {
		 if(!(tempbx & DriverMode)) {
		    if(SiS_BridgeInSlavemode(SiS_Pr)) {
		       tempbx |= SetSimuScanMode;
		    }
		 }
	      }
	   }
	}

	if(!(tempbx & DisableCRT2Display)) {
	   if(tempbx & DriverMode) {
	      if(tempbx & SetSimuScanMode) {
		 if((!(modeflag & CRT2Mode)) && (checkcrt2mode)) {
		    if(resinfo != SIS_RI_1600x1200) {
		       tempbx |= SetInSlaveMode;
		    }
		 }
	      }
	   } else {
	      tempbx |= SetInSlaveMode;
	   }
	}

   }

   SiS_Pr->SiS_VBInfo = tempbx;

#ifdef CONFIG_FB_SIS_300
   if(SiS_Pr->ChipType == SIS_630) {
      SiS_SetChrontelGPIO(SiS_Pr, SiS_Pr->SiS_VBInfo);
   }
#endif

#if 0
   printk(KERN_DEBUG "sisfb: (init301: VBInfo= 0x%04x, SetFlag=0x%04x)\n",
      SiS_Pr->SiS_VBInfo, SiS_Pr->SiS_SetFlag);
#endif
}

/*********************************************/
/*           DETERMINE YPbPr MODE            */
/*********************************************/

void
SiS_SetYPbPr(struct SiS_Private *SiS_Pr)
{

   unsigned char temp;

   /* Note: This variable is only used on 30xLV systems.
    * CR38 has a different meaning on LVDS/CH7019 systems.
    * On 661 and later, these bits moved to CR35.
    *
    * On 301, 301B, only HiVision 1080i is supported.
    * On 30xLV, 301C, only YPbPr 1080i is supported.
    */

   SiS_Pr->SiS_YPbPr = 0;
   if(SiS_Pr->ChipType >= SIS_661) return;

   if(SiS_Pr->SiS_VBType) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	 SiS_Pr->SiS_YPbPr = YPbPrHiVision;
      }
   }

   if(SiS_Pr->ChipType >= SIS_315H) {
      if(SiS_Pr->SiS_VBType & VB_SISYPBPR) {
	 temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	 if(temp & 0x08) {
	    switch((temp >> 4)) {
	    case 0x00: SiS_Pr->SiS_YPbPr = YPbPr525i;     break;
	    case 0x01: SiS_Pr->SiS_YPbPr = YPbPr525p;     break;
	    case 0x02: SiS_Pr->SiS_YPbPr = YPbPr750p;     break;
	    case 0x03: SiS_Pr->SiS_YPbPr = YPbPrHiVision; break;
	    }
	 }
      }
   }

}

/*********************************************/
/*           DETERMINE TVMode flag           */
/*********************************************/

void
SiS_SetTVMode(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short temp, temp1, resinfo = 0, romindex = 0;
   unsigned char  OutputSelect = *SiS_Pr->pSiS_OutputSelect;

   SiS_Pr->SiS_TVMode = 0;

   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) return;
   if(SiS_Pr->UseCustomMode) return;

   if(ModeNo > 0x13) {
      resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
   }

   if(SiS_Pr->ChipType < SIS_661) {

      if(SiS_Pr->SiS_VBInfo & SetPALTV) SiS_Pr->SiS_TVMode |= TVSetPAL;

      if(SiS_Pr->SiS_VBType & VB_SISVB) {
	 temp = 0;
	 if((SiS_Pr->ChipType == SIS_630) ||
	    (SiS_Pr->ChipType == SIS_730)) {
	    temp = 0x35;
	    romindex = 0xfe;
	 } else if(SiS_Pr->ChipType >= SIS_315H) {
	    temp = 0x38;
	    if(SiS_Pr->ChipType < XGI_20) {
	       romindex = 0xf3;
	       if(SiS_Pr->ChipType >= SIS_330) romindex = 0x11b;
	    }
	 }
	 if(temp) {
	    if(romindex && SiS_Pr->SiS_UseROM && (!(SiS_Pr->SiS_ROMNew))) {
	       OutputSelect = ROMAddr[romindex];
	       if(!(OutputSelect & EnablePALMN)) {
		  SiS_SetRegAND(SiS_Pr->SiS_P3d4,temp,0x3F);
	       }
	    }
	    temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,temp);
	    if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	       if(temp1 & EnablePALM) {		/* 0x40 */
		  SiS_Pr->SiS_TVMode |= TVSetPALM;
		  SiS_Pr->SiS_TVMode &= ~TVSetPAL;
	       } else if(temp1 & EnablePALN) {	/* 0x80 */
		  SiS_Pr->SiS_TVMode |= TVSetPALN;
	       }
	    } else {
	       if(temp1 & EnableNTSCJ) {	/* 0x40 */
		  SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
	       }
	    }
	 }
	 /* Translate HiVision/YPbPr to our new flags */
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	    if(SiS_Pr->SiS_YPbPr == YPbPr750p)          SiS_Pr->SiS_TVMode |= TVSetYPbPr750p;
	    else if(SiS_Pr->SiS_YPbPr == YPbPr525p)     SiS_Pr->SiS_TVMode |= TVSetYPbPr525p;
	    else if(SiS_Pr->SiS_YPbPr == YPbPrHiVision) SiS_Pr->SiS_TVMode |= TVSetHiVision;
	    else				        SiS_Pr->SiS_TVMode |= TVSetYPbPr525i;
	    if(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p | TVSetYPbPr525p | TVSetYPbPr525i)) {
	       SiS_Pr->SiS_VBInfo &= ~SetCRT2ToHiVision;
	       SiS_Pr->SiS_VBInfo |= SetCRT2ToYPbPr525750;
	    } else if(SiS_Pr->SiS_TVMode & TVSetHiVision) {
	       SiS_Pr->SiS_TVMode |= TVSetPAL;
	    }
	 }
      } else if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	 if(SiS_Pr->SiS_CHOverScan) {
	    if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	       temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
	       if((temp & TVOverScan) || (SiS_Pr->SiS_CHOverScan == 1)) {
		  SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
	       }
	    } else if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	       temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x79);
	       if((temp & 0x80) || (SiS_Pr->SiS_CHOverScan == 1)) {
		  SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
	       }
	    }
	    if(SiS_Pr->SiS_CHSOverScan) {
	       SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
	    }
	 }
	 if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	    temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	    if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	       if(temp & EnablePALM)      SiS_Pr->SiS_TVMode |= TVSetPALM;
	       else if(temp & EnablePALN) SiS_Pr->SiS_TVMode |= TVSetPALN;
	    } else {
	       if(temp & EnableNTSCJ) {
		  SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
	       }
	    }
	 }
      }

   } else {  /* 661 and later */

      temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      if(temp1 & 0x01) {
	 SiS_Pr->SiS_TVMode |= TVSetPAL;
	 if(temp1 & 0x08) {
	    SiS_Pr->SiS_TVMode |= TVSetPALN;
	 } else if(temp1 & 0x04) {
	    if(SiS_Pr->SiS_VBType & VB_SISVB) {
	       SiS_Pr->SiS_TVMode &= ~TVSetPAL;
	    }
	    SiS_Pr->SiS_TVMode |= TVSetPALM;
	 }
      } else {
	 if(temp1 & 0x02) {
	    SiS_Pr->SiS_TVMode |= TVSetNTSCJ;
	 }
      }
      if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	 if(SiS_Pr->SiS_CHOverScan) {
	    if((temp1 & 0x10) || (SiS_Pr->SiS_CHOverScan == 1)) {
	       SiS_Pr->SiS_TVMode |= TVSetCHOverScan;
	    }
	 }
      }
      if(SiS_Pr->SiS_VBType & VB_SISVB) {
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	    temp1 &= 0xe0;
	    if(temp1 == 0x00)      SiS_Pr->SiS_TVMode |= TVSetYPbPr525i;
	    else if(temp1 == 0x20) SiS_Pr->SiS_TVMode |= TVSetYPbPr525p;
	    else if(temp1 == 0x40) SiS_Pr->SiS_TVMode |= TVSetYPbPr750p;
	 } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	    SiS_Pr->SiS_TVMode |= (TVSetHiVision | TVSetPAL);
	 }
	 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToYPbPr525750 | SetCRT2ToHiVision)) {
	    if(resinfo == SIS_RI_800x480 || resinfo == SIS_RI_1024x576 || resinfo == SIS_RI_1280x720) {
	       SiS_Pr->SiS_TVMode |= TVAspect169;
	    } else {
	       temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x39);
	       if(temp1 & 0x02) {
		  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p | TVSetHiVision)) {
		     SiS_Pr->SiS_TVMode |= TVAspect169;
		  } else {
		     SiS_Pr->SiS_TVMode |= TVAspect43LB;
		  }
	       } else {
		  SiS_Pr->SiS_TVMode |= TVAspect43;
	       }
	    }
	 }
      }
   }

   if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART) SiS_Pr->SiS_TVMode |= TVSetPAL;

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	 SiS_Pr->SiS_TVMode |= TVSetPAL;
	 SiS_Pr->SiS_TVMode &= ~(TVSetPALM | TVSetPALN | TVSetNTSCJ);
      } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	 if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525i | TVSetYPbPr525p | TVSetYPbPr750p)) {
	    SiS_Pr->SiS_TVMode &= ~(TVSetPAL | TVSetNTSCJ | TVSetPALM | TVSetPALN);
	 }
      }

      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	 if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
	    SiS_Pr->SiS_TVMode |= TVSetTVSimuMode;
	 }
      }

      if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
	 if(resinfo == SIS_RI_1024x768) {
	    if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	       SiS_Pr->SiS_TVMode |= TVSet525p1024;
	    } else if(!(SiS_Pr->SiS_TVMode & (TVSetHiVision | TVSetYPbPr750p))) {
	       SiS_Pr->SiS_TVMode |= TVSetNTSC1024;
	    }
	 }
      }

      SiS_Pr->SiS_TVMode |= TVRPLLDIV2XO;
      if((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) &&
	 (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	 SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
      } else if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) {
	 SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
      } else if(!(SiS_Pr->SiS_VBType & VB_SIS30xBLV)) {
	 if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	    SiS_Pr->SiS_TVMode &= ~TVRPLLDIV2XO;
	 }
      }

   }

   SiS_Pr->SiS_VBInfo &= ~SetPALTV;
}

/*********************************************/
/*               GET LCD INFO                */
/*********************************************/

static unsigned short
SiS_GetBIOSLCDResInfo(struct SiS_Private *SiS_Pr)
{
   unsigned short temp = SiS_Pr->SiS_LCDResInfo;
   /* Translate my LCDResInfo to BIOS value */
   switch(temp) {
   case Panel_1280x768_2: temp = Panel_1280x768;    break;
   case Panel_1280x800_2: temp = Panel_1280x800;    break;
   case Panel_1280x854:   temp = Panel661_1280x854; break;
   }
   return temp;
}

static void
SiS_GetLCDInfoBIOS(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   unsigned char  *ROMAddr;
   unsigned short temp;

   if((ROMAddr = GetLCDStructPtr661(SiS_Pr))) {
      if((temp = SISGETROMW(6)) != SiS_Pr->PanelHT) {
	 SiS_Pr->SiS_NeedRomModeData = true;
	 SiS_Pr->PanelHT  = temp;
      }
      if((temp = SISGETROMW(8)) != SiS_Pr->PanelVT) {
	 SiS_Pr->SiS_NeedRomModeData = true;
	 SiS_Pr->PanelVT  = temp;
      }
      SiS_Pr->PanelHRS = SISGETROMW(10);
      SiS_Pr->PanelHRE = SISGETROMW(12);
      SiS_Pr->PanelVRS = SISGETROMW(14);
      SiS_Pr->PanelVRE = SISGETROMW(16);
      SiS_Pr->PanelVCLKIdx315 = VCLK_CUSTOM_315;
      SiS_Pr->SiS_VCLKData[VCLK_CUSTOM_315].CLOCK =
	 SiS_Pr->SiS_VBVCLKData[VCLK_CUSTOM_315].CLOCK = (unsigned short)((unsigned char)ROMAddr[18]);
      SiS_Pr->SiS_VCLKData[VCLK_CUSTOM_315].SR2B =
	 SiS_Pr->SiS_VBVCLKData[VCLK_CUSTOM_315].Part4_A = ROMAddr[19];
      SiS_Pr->SiS_VCLKData[VCLK_CUSTOM_315].SR2C =
	 SiS_Pr->SiS_VBVCLKData[VCLK_CUSTOM_315].Part4_B = ROMAddr[20];

   }
#endif
}

static void
SiS_CheckScaling(struct SiS_Private *SiS_Pr, unsigned short resinfo,
			const unsigned char *nonscalingmodes)
{
   int i = 0;
   while(nonscalingmodes[i] != 0xff) {
      if(nonscalingmodes[i++] == resinfo) {
	 if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) ||
	    (SiS_Pr->UsePanelScaler == -1)) {
	    SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	 }
	 break;
      }
   }
}

void
SiS_GetLCDResInfo(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned short temp,modeflag,resinfo=0,modexres=0,modeyres=0;
  bool panelcanscale = false;
#ifdef CONFIG_FB_SIS_300
  unsigned char *ROMAddr = SiS_Pr->VirtualRomBase;
  static const unsigned char SiS300SeriesLCDRes[] =
          { 0,  1,  2,  3,  7,  4,  5,  8,
	    0,  0, 10,  0,  0,  0,  0, 15 };
#endif
#ifdef CONFIG_FB_SIS_315
  unsigned char   *myptr = NULL;
#endif

  SiS_Pr->SiS_LCDResInfo  = 0;
  SiS_Pr->SiS_LCDTypeInfo = 0;
  SiS_Pr->SiS_LCDInfo     = 0;
  SiS_Pr->PanelHRS        = 999; /* HSync start */
  SiS_Pr->PanelHRE        = 999; /* HSync end */
  SiS_Pr->PanelVRS        = 999; /* VSync start */
  SiS_Pr->PanelVRE        = 999; /* VSync end */
  SiS_Pr->SiS_NeedRomModeData = false;

  /* Alternative 1600x1200@60 timing for 1600x1200 LCDA */
  SiS_Pr->Alternate1600x1200 = false;

  if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA))) return;

  modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

  if((ModeNo > 0x13) && (!SiS_Pr->UseCustomMode)) {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     modexres = SiS_Pr->SiS_ModeResInfo[resinfo].HTotal;
     modeyres = SiS_Pr->SiS_ModeResInfo[resinfo].VTotal;
  }

  temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);

  /* For broken BIOSes: Assume 1024x768 */
  if(temp == 0) temp = 0x02;

  if((SiS_Pr->ChipType >= SIS_661) || (SiS_Pr->SiS_ROMNew)) {
     SiS_Pr->SiS_LCDTypeInfo = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x39) & 0x7c) >> 2;
  } else if((SiS_Pr->ChipType < SIS_315H) || (SiS_Pr->ChipType >= SIS_661)) {
     SiS_Pr->SiS_LCDTypeInfo = temp >> 4;
  } else {
     SiS_Pr->SiS_LCDTypeInfo = (temp & 0x0F) - 1;
  }
  temp &= 0x0f;
#ifdef CONFIG_FB_SIS_300
  if(SiS_Pr->ChipType < SIS_315H) {
     /* Very old BIOSes only know 7 sizes (NetVista 2179, 1.01g) */
     if(SiS_Pr->SiS_VBType & VB_SIS301) {
        if(temp < 0x0f) temp &= 0x07;
     }
     /* Translate 300 series LCDRes to 315 series for unified usage */
     temp = SiS300SeriesLCDRes[temp];
  }
#endif

  /* Translate to our internal types */
#ifdef CONFIG_FB_SIS_315
  if(SiS_Pr->ChipType == SIS_550) {
     if     (temp == Panel310_1152x768)  temp = Panel_320x240_2; /* Verified working */
     else if(temp == Panel310_320x240_2) temp = Panel_320x240_2;
     else if(temp == Panel310_320x240_3) temp = Panel_320x240_3;
  } else if(SiS_Pr->ChipType >= SIS_661) {
     if(temp == Panel661_1280x854)       temp = Panel_1280x854;
  }
#endif

  if(SiS_Pr->SiS_VBType & VB_SISLVDS) {		/* SiS LVDS */
     if(temp == Panel310_1280x768) {
        temp = Panel_1280x768_2;
     }
     if(SiS_Pr->SiS_ROMNew) {
	if(temp == Panel661_1280x800) {
	   temp = Panel_1280x800_2;
	}
     }
  }

  SiS_Pr->SiS_LCDResInfo = temp;

#ifdef CONFIG_FB_SIS_300
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
	SiS_Pr->SiS_LCDResInfo = Panel_Barco1366;
     } else if(SiS_Pr->SiS_CustomT == CUT_PANEL848) {
	SiS_Pr->SiS_LCDResInfo = Panel_848x480;
     } else if(SiS_Pr->SiS_CustomT == CUT_PANEL856) {
	SiS_Pr->SiS_LCDResInfo = Panel_856x480;
     }
  }
#endif

  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMin301)
	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMin301;
  } else {
     if(SiS_Pr->SiS_LCDResInfo < SiS_Pr->SiS_PanelMinLVDS)
	SiS_Pr->SiS_LCDResInfo = SiS_Pr->SiS_PanelMinLVDS;
  }

  temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
  SiS_Pr->SiS_LCDInfo = temp & ~0x000e;
  /* Need temp below! */

  /* These must/can't scale no matter what */
  switch(SiS_Pr->SiS_LCDResInfo) {
  case Panel_320x240_1:
  case Panel_320x240_2:
  case Panel_320x240_3:
  case Panel_1280x960:
      SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
      break;
  case Panel_640x480:
      SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
  }

  panelcanscale = (bool)(SiS_Pr->SiS_LCDInfo & DontExpandLCD);

  if(!SiS_Pr->UsePanelScaler)          SiS_Pr->SiS_LCDInfo &= ~DontExpandLCD;
  else if(SiS_Pr->UsePanelScaler == 1) SiS_Pr->SiS_LCDInfo |= DontExpandLCD;

  /* Dual link, Pass 1:1 BIOS default, etc. */
#ifdef CONFIG_FB_SIS_315
  if(SiS_Pr->ChipType >= SIS_661) {
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	if(temp & 0x08) SiS_Pr->SiS_LCDInfo |= LCDPass11;
     }
     if(SiS_Pr->SiS_VBType & VB_SISDUALLINK) {
	if(SiS_Pr->SiS_ROMNew) {
	   if(temp & 0x02) SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	} else if((myptr = GetLCDStructPtr661(SiS_Pr))) {
	   if(myptr[2] & 0x01) SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	}
     }
  } else if(SiS_Pr->ChipType >= SIS_315H) {
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x39) & 0x01) SiS_Pr->SiS_LCDInfo |= LCDPass11;
     }
     if((SiS_Pr->SiS_ROMNew) && (!(SiS_Pr->PanelSelfDetected))) {
	SiS_Pr->SiS_LCDInfo &= ~(LCDRGB18Bit);
	temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
	if(temp & 0x01) SiS_Pr->SiS_LCDInfo |= LCDRGB18Bit;
	if(SiS_Pr->SiS_VBType & VB_SISDUALLINK) {
	   if(temp & 0x02) SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	}
     } else if(!(SiS_Pr->SiS_ROMNew)) {
	if(SiS_Pr->SiS_VBType & VB_SISDUALLINK) {
	   if((SiS_Pr->SiS_CustomT == CUT_CLEVO1024) &&
	      (SiS_Pr->SiS_LCDResInfo == Panel_1024x768)) {
	      SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	   }
	   if((SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) ||
	      (SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) ||
	      (SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) ||
	      (SiS_Pr->SiS_LCDResInfo == Panel_1680x1050)) {
	      SiS_Pr->SiS_LCDInfo |= LCDDualLink;
	   }
	}
     }
  }
#endif

  /* Pass 1:1 */
  if((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (SiS_Pr->SiS_VBType & VB_NoLCD)) {
     /* Always center screen on LVDS (if scaling is disabled) */
     SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
  } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	/* Always center screen on SiS LVDS (if scaling is disabled) */
	SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
     } else {
	/* By default, pass 1:1 on SiS TMDS (if scaling is supported) */
	if(panelcanscale)             SiS_Pr->SiS_LCDInfo |= LCDPass11;
	if(SiS_Pr->CenterScreen == 1) SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
     }
  }

  SiS_Pr->PanelVCLKIdx300 = VCLK65_300;
  SiS_Pr->PanelVCLKIdx315 = VCLK108_2_315;

  switch(SiS_Pr->SiS_LCDResInfo) {
     case Panel_320x240_1:
     case Panel_320x240_2:
     case Panel_320x240_3:  SiS_Pr->PanelXRes =  640; SiS_Pr->PanelYRes =  480;
			    SiS_Pr->PanelVRS  =   24; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx300 = VCLK28;
			    SiS_Pr->PanelVCLKIdx315 = VCLK28;
			    break;
     case Panel_640x480:    SiS_Pr->PanelXRes =  640; SiS_Pr->PanelYRes =  480;
						      SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx300 = VCLK28;
			    SiS_Pr->PanelVCLKIdx315 = VCLK28;
			    break;
     case Panel_800x600:    SiS_Pr->PanelXRes =  800; SiS_Pr->PanelYRes =  600;
     			    SiS_Pr->PanelHT   = 1056; SiS_Pr->PanelVT   =  628;
			    SiS_Pr->PanelHRS  =   40; SiS_Pr->PanelHRE  =  128;
			    SiS_Pr->PanelVRS  =    1; SiS_Pr->PanelVRE  =    4;
			    SiS_Pr->PanelVCLKIdx300 = VCLK40;
			    SiS_Pr->PanelVCLKIdx315 = VCLK40;
			    break;
     case Panel_1024x600:   SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  600;
			    SiS_Pr->PanelHT   = 1344; SiS_Pr->PanelVT   =  800;
			    SiS_Pr->PanelHRS  =   24; SiS_Pr->PanelHRE  =  136;
			    SiS_Pr->PanelVRS  =    2 /* 88 */ ; SiS_Pr->PanelVRE  =    6;
			    SiS_Pr->PanelVCLKIdx300 = VCLK65_300;
			    SiS_Pr->PanelVCLKIdx315 = VCLK65_315;
			    break;
     case Panel_1024x768:   SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  768;
			    SiS_Pr->PanelHT   = 1344; SiS_Pr->PanelVT   =  806;
			    SiS_Pr->PanelHRS  =   24; SiS_Pr->PanelHRE  =  136;
			    SiS_Pr->PanelVRS  =    3; SiS_Pr->PanelVRE  =    6;
			    if(SiS_Pr->ChipType < SIS_315H) {
			       SiS_Pr->PanelHRS = 23;
						      SiS_Pr->PanelVRE  =    5;
			    }
			    SiS_Pr->PanelVCLKIdx300 = VCLK65_300;
			    SiS_Pr->PanelVCLKIdx315 = VCLK65_315;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1152x768:   SiS_Pr->PanelXRes = 1152; SiS_Pr->PanelYRes =  768;
			    SiS_Pr->PanelHT   = 1344; SiS_Pr->PanelVT   =  806;
			    SiS_Pr->PanelHRS  =   24; SiS_Pr->PanelHRE  =  136;
			    SiS_Pr->PanelVRS  =    3; SiS_Pr->PanelVRE  =    6;
			    if(SiS_Pr->ChipType < SIS_315H) {
			       SiS_Pr->PanelHRS = 23;
						      SiS_Pr->PanelVRE  =    5;
			    }
			    SiS_Pr->PanelVCLKIdx300 = VCLK65_300;
			    SiS_Pr->PanelVCLKIdx315 = VCLK65_315;
			    break;
     case Panel_1152x864:   SiS_Pr->PanelXRes = 1152; SiS_Pr->PanelYRes =  864;
			    break;
     case Panel_1280x720:   SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  720;
			    SiS_Pr->PanelHT   = 1650; SiS_Pr->PanelVT   =  750;
			    SiS_Pr->PanelHRS  =  110; SiS_Pr->PanelHRE  =   40;
			    SiS_Pr->PanelVRS  =    5; SiS_Pr->PanelVRE  =    5;
			    SiS_Pr->PanelVCLKIdx315 = VCLK_1280x720;
			    /* Data above for TMDS (projector); get from BIOS for LVDS */
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1280x768:   SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  768;
			    if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
			       SiS_Pr->PanelHT   = 1408; SiS_Pr->PanelVT   =  806;
			       SiS_Pr->PanelVCLKIdx300 = VCLK81_300; /* ? */
			       SiS_Pr->PanelVCLKIdx315 = VCLK81_315; /* ? */
			    } else {
			       SiS_Pr->PanelHT   = 1688; SiS_Pr->PanelVT   =  802;
			       SiS_Pr->PanelHRS  =   48; SiS_Pr->PanelHRE  =  112;
			       SiS_Pr->PanelVRS  =    3; SiS_Pr->PanelVRE  =    6;
			       SiS_Pr->PanelVCLKIdx300 = VCLK81_300;
			       SiS_Pr->PanelVCLKIdx315 = VCLK81_315;
			    }
			    break;
     case Panel_1280x768_2: SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  768;
			    SiS_Pr->PanelHT   = 1660; SiS_Pr->PanelVT   =  806;
			    SiS_Pr->PanelHRS  =   48; SiS_Pr->PanelHRE  =  112;
			    SiS_Pr->PanelVRS  =    3; SiS_Pr->PanelVRE  =    6;
			    SiS_Pr->PanelVCLKIdx315 = VCLK_1280x768_2;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1280x800:   SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  800;
			    SiS_Pr->PanelHT   = 1408; SiS_Pr->PanelVT   =  816;
			    SiS_Pr->PanelHRS   =  21; SiS_Pr->PanelHRE  =   24;
			    SiS_Pr->PanelVRS   =   4; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx315 = VCLK_1280x800_315;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1280x800_2: SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  800;
			    SiS_Pr->PanelHT   = 1552; SiS_Pr->PanelVT   =  812;
			    SiS_Pr->PanelHRS   =  48; SiS_Pr->PanelHRE  =  112;
			    SiS_Pr->PanelVRS   =   4; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx315 = VCLK_1280x800_315_2;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1280x854:   SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  854;
			    SiS_Pr->PanelHT   = 1664; SiS_Pr->PanelVT   =  861;
			    SiS_Pr->PanelHRS   =  16; SiS_Pr->PanelHRE  =  112;
			    SiS_Pr->PanelVRS   =   1; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx315 = VCLK_1280x854;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1280x960:   SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes =  960;
			    SiS_Pr->PanelHT   = 1800; SiS_Pr->PanelVT   = 1000;
			    SiS_Pr->PanelVCLKIdx300 = VCLK108_3_300;
			    SiS_Pr->PanelVCLKIdx315 = VCLK108_3_315;
			    if(resinfo == SIS_RI_1280x1024) {
			       SiS_Pr->PanelVCLKIdx300 = VCLK100_300;
			       SiS_Pr->PanelVCLKIdx315 = VCLK100_315;
			    }
			    break;
     case Panel_1280x1024:  SiS_Pr->PanelXRes = 1280; SiS_Pr->PanelYRes = 1024;
			    SiS_Pr->PanelHT   = 1688; SiS_Pr->PanelVT   = 1066;
			    SiS_Pr->PanelHRS  =   48; SiS_Pr->PanelHRE  =  112;
			    SiS_Pr->PanelVRS  =    1; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx300 = VCLK108_3_300;
			    SiS_Pr->PanelVCLKIdx315 = VCLK108_2_315;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1400x1050:  SiS_Pr->PanelXRes = 1400; SiS_Pr->PanelYRes = 1050;
			    SiS_Pr->PanelHT   = 1688; SiS_Pr->PanelVT   = 1066;
			    SiS_Pr->PanelHRS  =   48; SiS_Pr->PanelHRE  =  112;
			    SiS_Pr->PanelVRS  =    1; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx315 = VCLK108_2_315;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1600x1200:  SiS_Pr->PanelXRes = 1600; SiS_Pr->PanelYRes = 1200;
			    SiS_Pr->PanelHT   = 2160; SiS_Pr->PanelVT   = 1250;
			    SiS_Pr->PanelHRS  =   64; SiS_Pr->PanelHRE  =  192;
			    SiS_Pr->PanelVRS  =    1; SiS_Pr->PanelVRE  =    3;
			    SiS_Pr->PanelVCLKIdx315 = VCLK162_315;
			    if(SiS_Pr->SiS_VBType & VB_SISTMDSLCDA) {
			       if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
				  SiS_Pr->PanelHT  = 1760; SiS_Pr->PanelVT  = 1235;
				  SiS_Pr->PanelHRS =   48; SiS_Pr->PanelHRE =   32;
				  SiS_Pr->PanelVRS =    2; SiS_Pr->PanelVRE =    4;
				  SiS_Pr->PanelVCLKIdx315 = VCLK130_315;
				  SiS_Pr->Alternate1600x1200 = true;
			       }
			    } else if(SiS_Pr->SiS_IF_DEF_LVDS) {
			       SiS_Pr->PanelHT  = 2048; SiS_Pr->PanelVT  = 1320;
			       SiS_Pr->PanelHRS = SiS_Pr->PanelHRE = 999;
			       SiS_Pr->PanelVRS = SiS_Pr->PanelVRE = 999;
			    }
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_1680x1050:  SiS_Pr->PanelXRes = 1680; SiS_Pr->PanelYRes = 1050;
			    SiS_Pr->PanelHT   = 1900; SiS_Pr->PanelVT   = 1066;
			    SiS_Pr->PanelHRS  =   26; SiS_Pr->PanelHRE  =   76;
			    SiS_Pr->PanelVRS  =    3; SiS_Pr->PanelVRE  =    6;
			    SiS_Pr->PanelVCLKIdx315 = VCLK121_315;
			    SiS_GetLCDInfoBIOS(SiS_Pr);
			    break;
     case Panel_Barco1366:  SiS_Pr->PanelXRes = 1360; SiS_Pr->PanelYRes = 1024;
			    SiS_Pr->PanelHT   = 1688; SiS_Pr->PanelVT   = 1066;
			    break;
     case Panel_848x480:    SiS_Pr->PanelXRes =  848; SiS_Pr->PanelYRes =  480;
			    SiS_Pr->PanelHT   = 1088; SiS_Pr->PanelVT   =  525;
			    break;
     case Panel_856x480:    SiS_Pr->PanelXRes =  856; SiS_Pr->PanelYRes =  480;
			    SiS_Pr->PanelHT   = 1088; SiS_Pr->PanelVT   =  525;
			    break;
     case Panel_Custom:     SiS_Pr->PanelXRes = SiS_Pr->CP_MaxX;
			    SiS_Pr->PanelYRes = SiS_Pr->CP_MaxY;
			    SiS_Pr->PanelHT   = SiS_Pr->CHTotal;
			    SiS_Pr->PanelVT   = SiS_Pr->CVTotal;
			    if(SiS_Pr->CP_PreferredIndex != -1) {
			       SiS_Pr->PanelXRes = SiS_Pr->CP_HDisplay[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelYRes = SiS_Pr->CP_VDisplay[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelHT   = SiS_Pr->CP_HTotal[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelVT   = SiS_Pr->CP_VTotal[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelHRS  = SiS_Pr->CP_HSyncStart[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelHRE  = SiS_Pr->CP_HSyncEnd[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelVRS  = SiS_Pr->CP_VSyncStart[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelVRE  = SiS_Pr->CP_VSyncEnd[SiS_Pr->CP_PreferredIndex];
			       SiS_Pr->PanelHRS -= SiS_Pr->PanelXRes;
			       SiS_Pr->PanelHRE -= SiS_Pr->PanelHRS;
			       SiS_Pr->PanelVRS -= SiS_Pr->PanelYRes;
			       SiS_Pr->PanelVRE -= SiS_Pr->PanelVRS;
			       if(SiS_Pr->CP_PrefClock) {
				  int idx;
				  SiS_Pr->PanelVCLKIdx315 = VCLK_CUSTOM_315;
				  SiS_Pr->PanelVCLKIdx300 = VCLK_CUSTOM_300;
				  if(SiS_Pr->ChipType < SIS_315H) idx = VCLK_CUSTOM_300;
				  else				   idx = VCLK_CUSTOM_315;
				  SiS_Pr->SiS_VCLKData[idx].CLOCK =
				     SiS_Pr->SiS_VBVCLKData[idx].CLOCK = SiS_Pr->CP_PrefClock;
				  SiS_Pr->SiS_VCLKData[idx].SR2B =
				     SiS_Pr->SiS_VBVCLKData[idx].Part4_A = SiS_Pr->CP_PrefSR2B;
				  SiS_Pr->SiS_VCLKData[idx].SR2C =
				     SiS_Pr->SiS_VBVCLKData[idx].Part4_B = SiS_Pr->CP_PrefSR2C;
			       }
			    }
			    break;
     default:		    SiS_Pr->PanelXRes = 1024; SiS_Pr->PanelYRes =  768;
			    SiS_Pr->PanelHT   = 1344; SiS_Pr->PanelVT   =  806;
			    break;
  }

  /* Special cases */
  if( (SiS_Pr->SiS_IF_DEF_FSTN)              ||
      (SiS_Pr->SiS_IF_DEF_DSTN)              ||
      (SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
      (SiS_Pr->SiS_CustomT == CUT_BARCO1024) ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL848)  ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL856) ) {
     SiS_Pr->PanelHRS = 999;
     SiS_Pr->PanelHRE = 999;
  }

  if( (SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
      (SiS_Pr->SiS_CustomT == CUT_BARCO1024) ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL848)  ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL856) ) {
     SiS_Pr->PanelVRS = 999;
     SiS_Pr->PanelVRE = 999;
  }

  /* DontExpand overrule */
  if((SiS_Pr->SiS_VBType & VB_SISVB) && (!(SiS_Pr->SiS_VBType & VB_NoLCD))) {

     if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (modeflag & NoSupportLCDScale)) {
	/* No scaling for this mode on any panel (LCD=CRT2)*/
	SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
     }

     switch(SiS_Pr->SiS_LCDResInfo) {

     case Panel_Custom:
     case Panel_1152x864:
     case Panel_1280x768:	/* TMDS only */
	SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	break;

     case Panel_800x600: {
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, 0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1024x768: {
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1280x720: {
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	if(SiS_Pr->PanelHT == 1650) {
	   SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	}
	break;
     }
     case Panel_1280x768_2: {  /* LVDS only */
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	switch(resinfo) {
	case SIS_RI_1280x720:  if(SiS_Pr->UsePanelScaler == -1) {
				  SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
			       }
			       break;
	}
	break;
     }
     case Panel_1280x800: {  	/* SiS TMDS special (Averatec 6200 series) */
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,SIS_RI_1280x720,SIS_RI_1280x768,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1280x800_2:  { 	/* SiS LVDS */
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	switch(resinfo) {
	case SIS_RI_1280x720:
	case SIS_RI_1280x768:  if(SiS_Pr->UsePanelScaler == -1) {
				  SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
			       }
			       break;
	}
	break;
     }
     case Panel_1280x854: {  	/* SiS LVDS */
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	switch(resinfo) {
	case SIS_RI_1280x720:
	case SIS_RI_1280x768:
	case SIS_RI_1280x800:  if(SiS_Pr->UsePanelScaler == -1) {
				  SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
			       }
			       break;
	}
	break;
     }
     case Panel_1280x960: {
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,SIS_RI_1152x864,SIS_RI_1280x720,SIS_RI_1280x768,SIS_RI_1280x800,
	   SIS_RI_1280x854,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1280x1024: {
	static const unsigned char nonscalingmodes[] = {
	   SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	   SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	   SIS_RI_1152x768,SIS_RI_1152x864,SIS_RI_1280x720,SIS_RI_1280x768,SIS_RI_1280x800,
	   SIS_RI_1280x854,SIS_RI_1280x960,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1400x1050: {
	static const unsigned char nonscalingmodes[] = {
	     SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	     SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	     SIS_RI_1152x768,SIS_RI_1152x864,SIS_RI_1280x768,SIS_RI_1280x800,SIS_RI_1280x854,
	     SIS_RI_1280x960,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	switch(resinfo) {
	case SIS_RI_1280x720:  if(SiS_Pr->UsePanelScaler == -1) {
				  SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
			       }
			       break;
	case SIS_RI_1280x1024: SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
			       break;
	}
	break;
     }
     case Panel_1600x1200: {
	static const unsigned char nonscalingmodes[] = {
	     SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	     SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	     SIS_RI_1152x768,SIS_RI_1152x864,SIS_RI_1280x720,SIS_RI_1280x768,SIS_RI_1280x800,
	     SIS_RI_1280x854,SIS_RI_1280x960,SIS_RI_1360x768,SIS_RI_1360x1024,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     case Panel_1680x1050: {
	static const unsigned char nonscalingmodes[] = {
	     SIS_RI_720x480, SIS_RI_720x576, SIS_RI_768x576, SIS_RI_800x480, SIS_RI_848x480,
	     SIS_RI_856x480, SIS_RI_960x540, SIS_RI_960x600, SIS_RI_1024x576,SIS_RI_1024x600,
	     SIS_RI_1152x768,SIS_RI_1152x864,SIS_RI_1280x854,SIS_RI_1280x960,SIS_RI_1360x768,
	     SIS_RI_1360x1024,0xff
	};
	SiS_CheckScaling(SiS_Pr, resinfo, nonscalingmodes);
	break;
     }
     }
  }

#ifdef CONFIG_FB_SIS_300
  if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
     if(SiS_Pr->SiS_CustomT == CUT_PANEL848 || SiS_Pr->SiS_CustomT == CUT_PANEL856) {
	SiS_Pr->SiS_LCDInfo = 0x80 | 0x40 | 0x20;   /* neg h/v sync, RGB24(D0 = 0) */
     }
  }

  if(SiS_Pr->ChipType < SIS_315H) {
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	if(SiS_Pr->SiS_UseROM) {
	   if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
	      if(!(ROMAddr[0x235] & 0x02)) {
		 SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
	      }
	   }
	}
     } else if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	if((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo == 0x10))) {
	   SiS_Pr->SiS_LCDInfo &= (~DontExpandLCD);
	}
     }
  }
#endif

  /* Special cases */

  if(modexres == SiS_Pr->PanelXRes && modeyres == SiS_Pr->PanelYRes) {
     SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
  }

  if(SiS_Pr->SiS_IF_DEF_TRUMPION) {
     SiS_Pr->SiS_LCDInfo |= (DontExpandLCD | LCDPass11);
  }

  switch(SiS_Pr->SiS_LCDResInfo) {
  case Panel_640x480:
     SiS_Pr->SiS_LCDInfo |= (DontExpandLCD | LCDPass11);
     break;
  case Panel_1280x800:
     /* Don't pass 1:1 by default (TMDS special) */
     if(SiS_Pr->CenterScreen == -1) SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
     break;
  case Panel_1280x960:
     SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
     break;
  case Panel_Custom:
     if((!SiS_Pr->CP_PrefClock) ||
        (modexres > SiS_Pr->PanelXRes) || (modeyres > SiS_Pr->PanelYRes)) {
        SiS_Pr->SiS_LCDInfo |= LCDPass11;
     }
     break;
  }

  if((SiS_Pr->UseCustomMode) || (SiS_Pr->SiS_CustomT == CUT_UNKNOWNLCD)) {
     SiS_Pr->SiS_LCDInfo |= (DontExpandLCD | LCDPass11);
  }

  /* (In)validate LCDPass11 flag */
  if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
     SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
  }

  /* LVDS DDA */
  if(!((SiS_Pr->ChipType < SIS_315H) && (SiS_Pr->SiS_SetFlag & SetDOSMode))) {

     if((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (SiS_Pr->SiS_VBType & VB_NoLCD)) {
	if(SiS_Pr->SiS_IF_DEF_TRUMPION == 0) {
	   if(ModeNo == 0x12) {
	      if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
		 SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	      }
	   } else if(ModeNo > 0x13) {
	      if(SiS_Pr->SiS_LCDResInfo == Panel_1024x600) {
		 if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
		    if((resinfo == SIS_RI_800x600) || (resinfo == SIS_RI_400x300)) {
		       SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
		    }
		 }
	      }
	   }
	}
     }

     if(modeflag & HalfDCLK) {
	if(SiS_Pr->SiS_IF_DEF_TRUMPION == 1) {
	   SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	} else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	   SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	} else if(SiS_Pr->SiS_LCDResInfo == Panel_640x480) {
	   SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	} else if(ModeNo > 0x13) {
	   if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	      if(resinfo == SIS_RI_512x384) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	   } else if(SiS_Pr->SiS_LCDResInfo == Panel_800x600) {
	      if(resinfo == SIS_RI_400x300) SiS_Pr->SiS_SetFlag |= EnableLVDSDDA;
	   }
	}
     }

  }

  /* VESA timing */
  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
     if(SiS_Pr->SiS_VBInfo & SetNotSimuMode) {
	SiS_Pr->SiS_SetFlag |= LCDVESATiming;
     }
  } else {
     SiS_Pr->SiS_SetFlag |= LCDVESATiming;
  }

#if 0
  printk(KERN_DEBUG "sisfb: (LCDInfo=0x%04x LCDResInfo=0x%02x LCDTypeInfo=0x%02x)\n",
	SiS_Pr->SiS_LCDInfo, SiS_Pr->SiS_LCDResInfo, SiS_Pr->SiS_LCDTypeInfo);
#endif
}

/*********************************************/
/*                 GET VCLK                  */
/*********************************************/

unsigned short
SiS_GetVCLK2Ptr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned short CRT2Index, VCLKIndex = 0, VCLKIndexGEN = 0, VCLKIndexGENCRT = 0;
  unsigned short resinfo, tempbx;
  const unsigned char *CHTVVCLKPtr = NULL;

  if(ModeNo <= 0x13) {
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
     CRT2Index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
     VCLKIndexGEN = (SiS_GetRegByte((SiS_Pr->SiS_P3ca+0x02)) >> 2) & 0x03;
     VCLKIndexGENCRT = VCLKIndexGEN;
  } else {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     CRT2Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
     VCLKIndexGEN = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
     VCLKIndexGENCRT = SiS_GetRefCRTVCLK(SiS_Pr, RefreshRateTableIndex,
		(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) ? SiS_Pr->SiS_UseWideCRT2 : SiS_Pr->SiS_UseWide);
  }

  if(SiS_Pr->SiS_VBType & VB_SISVB) {    /* 30x/B/LV */

     if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {

	CRT2Index >>= 6;
	if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {      	/*  LCD */

	   if(SiS_Pr->ChipType < SIS_315H) {
	      VCLKIndex = SiS_Pr->PanelVCLKIdx300;
	      if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (SiS_Pr->SiS_LCDInfo & LCDPass11)) {
		 VCLKIndex = VCLKIndexGEN;
	      }
	   } else {
	      VCLKIndex = SiS_Pr->PanelVCLKIdx315;
	      if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (SiS_Pr->SiS_LCDInfo & LCDPass11)) {
		 switch(resinfo) {
		 /* Correct those whose IndexGEN doesn't match VBVCLK array */
		 case SIS_RI_720x480:  VCLKIndex = VCLK_720x480;  break;
		 case SIS_RI_720x576:  VCLKIndex = VCLK_720x576;  break;
		 case SIS_RI_768x576:  VCLKIndex = VCLK_768x576;  break;
		 case SIS_RI_848x480:  VCLKIndex = VCLK_848x480;  break;
		 case SIS_RI_856x480:  VCLKIndex = VCLK_856x480;  break;
		 case SIS_RI_800x480:  VCLKIndex = VCLK_800x480;  break;
		 case SIS_RI_1024x576: VCLKIndex = VCLK_1024x576; break;
		 case SIS_RI_1152x864: VCLKIndex = VCLK_1152x864; break;
		 case SIS_RI_1280x720: VCLKIndex = VCLK_1280x720; break;
		 case SIS_RI_1360x768: VCLKIndex = VCLK_1360x768; break;
		 default:              VCLKIndex = VCLKIndexGEN;
		 }

		 if(ModeNo <= 0x13) {
		    if(SiS_Pr->ChipType <= SIS_315PRO) {
		       if(SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x42;
		    } else {
		       if(SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC == 1) VCLKIndex = 0x00;
		    }
		 }
		 if(SiS_Pr->ChipType <= SIS_315PRO) {
		    if(VCLKIndex == 0) VCLKIndex = 0x41;
		    if(VCLKIndex == 1) VCLKIndex = 0x43;
		    if(VCLKIndex == 4) VCLKIndex = 0x44;
		 }
	      }
	   }

	} else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {                 	/*  TV */

	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	      if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO) 	   VCLKIndex = HiTVVCLKDIV2;
	      else                                  	   VCLKIndex = HiTVVCLK;
	      if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)     VCLKIndex = HiTVSimuVCLK;
	   } else if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)  VCLKIndex = YPbPr750pVCLK;
	   else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)    VCLKIndex = TVVCLKDIV2;
	   else if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO)      VCLKIndex = TVVCLKDIV2;
	   else						   VCLKIndex = TVVCLK;

	   if(SiS_Pr->ChipType < SIS_315H) VCLKIndex += TVCLKBASE_300;
	   else				   VCLKIndex += TVCLKBASE_315;

	} else {							/* VGA2 */

	   VCLKIndex = VCLKIndexGENCRT;
	   if(SiS_Pr->ChipType < SIS_315H) {
	      if(ModeNo > 0x13) {
		 if( (SiS_Pr->ChipType == SIS_630) &&
		     (SiS_Pr->ChipRevision >= 0x30)) {
		    if(VCLKIndex == 0x14) VCLKIndex = 0x34;
		 }
		 /* Better VGA2 clock for 1280x1024@75 */
		 if(VCLKIndex == 0x17) VCLKIndex = 0x45;
	      }
	   }
	}

     } else {   /* If not programming CRT2 */

	VCLKIndex = VCLKIndexGENCRT;
	if(SiS_Pr->ChipType < SIS_315H) {
	   if(ModeNo > 0x13) {
	      if( (SiS_Pr->ChipType != SIS_630) &&
		  (SiS_Pr->ChipType != SIS_300) ) {
		 if(VCLKIndex == 0x1b) VCLKIndex = 0x48;
	      }
	   }
	}
     }

  } else {       /*   LVDS  */

     VCLKIndex = CRT2Index;

     if(SiS_Pr->SiS_SetFlag & ProgrammingCRT2) {

	if( (SiS_Pr->SiS_IF_DEF_CH70xx != 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) ) {

	   VCLKIndex &= 0x1f;
	   tempbx = 0;
	   if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	   if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	      tempbx += 2;
	      if(SiS_Pr->SiS_ModeType > ModeVGA) {
		 if(SiS_Pr->SiS_CHSOverScan) tempbx = 8;
	      }
	      if(SiS_Pr->SiS_TVMode & TVSetPALM) {
		 tempbx = 4;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      } else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
		 tempbx = 6;
		 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx += 1;
	      }
	   }
	   switch(tempbx) {
	     case  0: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUNTSC;  break;
	     case  1: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKONTSC;  break;
	     case  2: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPAL;   break;
	     case  3: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
	     case  4: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALM;  break;
	     case  5: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALM;  break;
	     case  6: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKUPALN;  break;
	     case  7: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPALN;  break;
	     case  8: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKSOPAL;  break;
	     default: CHTVVCLKPtr = SiS_Pr->SiS_CHTVVCLKOPAL;   break;
	   }
	   VCLKIndex = CHTVVCLKPtr[VCLKIndex];

	} else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

	   if(SiS_Pr->ChipType < SIS_315H) {
	      VCLKIndex = SiS_Pr->PanelVCLKIdx300;
	   } else {
	      VCLKIndex = SiS_Pr->PanelVCLKIdx315;
	   }

#ifdef CONFIG_FB_SIS_300
	   /* Special Timing: Barco iQ Pro R series */
	   if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) VCLKIndex = 0x44;

	   /* Special Timing: 848x480 and 856x480 parallel lvds panels */
	   if(SiS_Pr->SiS_CustomT == CUT_PANEL848 || SiS_Pr->SiS_CustomT == CUT_PANEL856) {
	      if(SiS_Pr->ChipType < SIS_315H) {
		 VCLKIndex = VCLK34_300;
		 /* if(resinfo == SIS_RI_1360x768) VCLKIndex = ?; */
	      } else {
		 VCLKIndex = VCLK34_315;
		 /* if(resinfo == SIS_RI_1360x768) VCLKIndex = ?; */
	      }
	   }
#endif

	} else {

	   VCLKIndex = VCLKIndexGENCRT;
	   if(SiS_Pr->ChipType < SIS_315H) {
	      if(ModeNo > 0x13) {
		 if( (SiS_Pr->ChipType == SIS_630) &&
		     (SiS_Pr->ChipRevision >= 0x30) ) {
		    if(VCLKIndex == 0x14) VCLKIndex = 0x2e;
		 }
	      }
	   }
	}

     } else {  /* if not programming CRT2 */

	VCLKIndex = VCLKIndexGENCRT;
	if(SiS_Pr->ChipType < SIS_315H) {
	   if(ModeNo > 0x13) {
	      if( (SiS_Pr->ChipType != SIS_630) &&
		  (SiS_Pr->ChipType != SIS_300) ) {
		 if(VCLKIndex == 0x1b) VCLKIndex = 0x48;
	      }
#if 0
	      if(SiS_Pr->ChipType == SIS_730) {
		 if(VCLKIndex == 0x0b) VCLKIndex = 0x40;   /* 1024x768-70 */
		 if(VCLKIndex == 0x0d) VCLKIndex = 0x41;   /* 1024x768-75 */
	      }
#endif
	   }
        }

     }

  }

  return VCLKIndex;
}

/*********************************************/
/*        SET CRT2 MODE TYPE REGISTERS       */
/*********************************************/

static void
SiS_SetCRT2ModeRegs(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned short i, j, modeflag, tempah=0;
  short tempcl;
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
  unsigned short tempbl;
#endif
#ifdef CONFIG_FB_SIS_315
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short tempah2, tempbl2;
#endif

  modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {

     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xAF,0x40);
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2E,0xF7);

  } else {

     for(i=0,j=4; i<3; i++,j++) SiS_SetReg(SiS_Pr->SiS_Part1Port,j,0);
     if(SiS_Pr->ChipType >= SIS_315H) {
        SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0x7F);
     }

     tempcl = SiS_Pr->SiS_ModeType;

     if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300    /* ---- 300 series ---- */

	/* For 301BDH: (with LCD via LVDS) */
	if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	   tempbl = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32);
	   tempbl &= 0xef;
	   tempbl |= 0x02;
	   if((SiS_Pr->SiS_VBInfo & SetCRT2ToTV) || (SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
	      tempbl |= 0x10;
	      tempbl &= 0xfd;
	   }
	   SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,tempbl);
	}

	if(ModeNo > 0x13) {
	   tempcl -= ModeVGA;
	   if(tempcl >= 0) {
	      tempah = ((0x10 >> tempcl) | 0x80);
	   }
	} else tempah = 0x80;

	if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  tempah ^= 0xA0;

#endif  /* CONFIG_FB_SIS_300 */

     } else {

#ifdef CONFIG_FB_SIS_315    /* ------- 315/330 series ------ */

	if(ModeNo > 0x13) {
	   tempcl -= ModeVGA;
	   if(tempcl >= 0) {
	      tempah = (0x08 >> tempcl);
	      if (tempah == 0) tempah = 1;
	      tempah |= 0x40;
	   }
	} else tempah = 0x40;

	if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempah ^= 0x50;

#endif  /* CONFIG_FB_SIS_315 */

     }

     if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) tempah = 0;

     if(SiS_Pr->ChipType < SIS_315H) {
	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,tempah);
     } else {
#ifdef CONFIG_FB_SIS_315
	if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);
	} else if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   if(IS_SIS740) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,tempah);
	   } else {
	      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x00,0xa0,tempah);
	   }
	}
#endif
     }

     if(SiS_Pr->SiS_VBType & VB_SISVB) {

	tempah = 0x01;
	if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	   tempah |= 0x02;
	}
	if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
	   tempah ^= 0x05;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	      tempah ^= 0x01;
	   }
	}

	if(SiS_Pr->ChipType < SIS_315H) {

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)  tempah = 0;

	   tempah = (tempah << 5) & 0xFF;
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,tempah);
	   tempah = (tempah >> 5) & 0xFF;

	} else {

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display)  tempah = 0x08;
	   else if(!(SiS_IsDualEdge(SiS_Pr)))           tempah |= 0x08;
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2E,0xF0,tempah);
	   tempah &= ~0x08;

	}

	if((SiS_Pr->SiS_ModeType == ModeVGA) && (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))) {
	   tempah |= 0x10;
	}

	tempah |= 0x80;
	if(SiS_Pr->SiS_VBType & VB_SIS301) {
	   if(SiS_Pr->PanelXRes < 1280 && SiS_Pr->PanelYRes < 960) tempah &= ~0x80;
	}

	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	   if(!(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p | TVSetYPbPr525p))) {
	      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		 tempah |= 0x20;
	      }
	   }
	}

	SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0D,0x40,tempah);

	tempah = 0x80;
	if(SiS_Pr->SiS_VBType & VB_SIS301) {
	   if(SiS_Pr->PanelXRes < 1280 && SiS_Pr->PanelYRes < 960) tempah = 0;
	}

	if(SiS_IsDualLink(SiS_Pr)) tempah |= 0x40;

	if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	   if(SiS_Pr->SiS_TVMode & TVRPLLDIV2XO) {
	      tempah |= 0x40;
	   }
	}

	SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0C,tempah);

     } else {  /* LVDS */

	if(SiS_Pr->ChipType >= SIS_315H) {

#ifdef CONFIG_FB_SIS_315
	   /* LVDS can only be slave in 8bpp modes */
	   tempah = 0x80;
	   if((modeflag & CRT2Mode) && (SiS_Pr->SiS_ModeType > ModeVGA)) {
	      if(SiS_Pr->SiS_VBInfo & DriverMode) {
	         tempah |= 0x02;
	      }
	   }

	   if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))  tempah |= 0x02;

	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)        tempah ^= 0x01;

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) tempah = 1;

	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2e,0xF0,tempah);
#endif

	} else {

#ifdef CONFIG_FB_SIS_300
	   tempah = 0;
	   if( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) && (SiS_Pr->SiS_ModeType > ModeVGA) ) {
	      tempah |= 0x02;
	   }
	   tempah <<= 5;

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) tempah = 0;

	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,tempah);
#endif

	}

     }

  }  /* LCDA */

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(SiS_Pr->ChipType >= SIS_315H) {

#ifdef CONFIG_FB_SIS_315
	/* unsigned char bridgerev = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x01); */

	/* The following is nearly unpreditable and varies from machine
	 * to machine. Especially the 301DH seems to be a real trouble
	 * maker. Some BIOSes simply set the registers (like in the
	 * NoLCD-if-statements here), some set them according to the
	 * LCDA stuff. It is very likely that some machines are not
	 * treated correctly in the following, very case-orientated
	 * code. What do I do then...?
	 */

	/* 740 variants match for 30xB, 301B-DH, 30xLV */

	if(!(IS_SIS740)) {
	   tempah = 0x04;						   /* For all bridges */
	   tempbl = 0xfb;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	      tempah = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr)) {
	         tempbl = 0xff;
	      }
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);
	}

	/* The following two are responsible for eventually wrong colors
	 * in TV output. The DH (VB_NoLCD) conditions are unknown; the
	 * b0 was found in some 651 machine (Pim; P4_23=0xe5); the b1 version
	 * in a 650 box (Jake). What is the criteria?
	 * Addendum: Another combination 651+301B-DH(b1) (Rapo) needs same
	 * treatment like the 651+301B-DH(b0) case. Seems more to be the
	 * chipset than the bridge revision.
	 */

	if((IS_SIS740) || (SiS_Pr->ChipType >= SIS_661) || (SiS_Pr->SiS_ROMNew)) {
	   tempah = 0x30;
	   tempbl = 0xc0;
	   if((SiS_Pr->SiS_VBInfo & DisableCRT2Display) ||
	      ((SiS_Pr->SiS_ROMNew) && (!(ROMAddr[0x5b] & 0x04)))) {
	      tempah = 0x00;
	      tempbl = 0x00;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,0xcf,tempah);
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0x3f,tempbl);
	} else if(SiS_Pr->SiS_VBType & VB_SIS301) {
	   /* Fixes "TV-blue-bug" on 315+301 */
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2c,0xcf);	/* For 301   */
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x21,0x3f);
	} else if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);	/* For 30xLV */
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x21,0xc0);
	} else if(SiS_Pr->SiS_VBType & VB_NoLCD) {		/* For 301B-DH */
	   tempah = 0x30; tempah2 = 0xc0;
	   tempbl = 0xcf; tempbl2 = 0x3f;
	   if(SiS_Pr->SiS_TVBlue == 0) {
	         tempah = tempah2 = 0x00;
	   } else if(SiS_Pr->SiS_TVBlue == -1) {
	      /* Set on 651/M650, clear on 315/650 */
	      if(!(IS_SIS65x)) /* (bridgerev != 0xb0) */ {
	         tempah = tempah2 = 0x00;
	      }
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,tempbl,tempah);
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,tempbl2,tempah2);
	} else {
	   tempah = 0x30; tempah2 = 0xc0;		       /* For 30xB, 301C */
	   tempbl = 0xcf; tempbl2 = 0x3f;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	      tempah = tempah2 = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr)) {
		 tempbl = tempbl2 = 0xff;
	      }
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2c,tempbl,tempah);
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,tempbl2,tempah2);
	}

	if(IS_SIS740) {
	   tempah = 0x80;
	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) tempah = 0x00;
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,0x7f,tempah);
	} else {
	   tempah = 0x00;
	   tempbl = 0x7f;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	      tempbl = 0xff;
	      if(!(SiS_IsDualEdge(SiS_Pr))) tempah = 0x80;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x23,tempbl,tempah);
	}

#endif /* CONFIG_FB_SIS_315 */

     } else if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {

#ifdef CONFIG_FB_SIS_300
	SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x21,0x3f);

	if((SiS_Pr->SiS_VBInfo & DisableCRT2Display) ||
	   ((SiS_Pr->SiS_VBType & VB_NoLCD) &&
	    (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD))) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x23,0x7F);
	} else {
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x23,0x80);
	}
#endif

     }

     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x0D,0x80);
	if(SiS_Pr->SiS_VBType & VB_SIS30xCLV) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x3A,0xC0);
        }
     }

  } else {  /* LVDS */

#ifdef CONFIG_FB_SIS_315
     if(SiS_Pr->ChipType >= SIS_315H) {

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {

	   tempah = 0x04;
	   tempbl = 0xfb;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	      tempah = 0x00;
	      if(SiS_IsDualEdge(SiS_Pr)) tempbl = 0xff;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,tempbl,tempah);

	   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   }

	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);

	} else if(SiS_Pr->ChipType == SIS_550) {

	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2c,0x30);

	}

     }
#endif

  }

}

/*********************************************/
/*            GET RESOLUTION DATA            */
/*********************************************/

unsigned short
SiS_GetResInfo(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   if(ModeNo <= 0x13)
      return ((unsigned short)SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo);
   else
      return ((unsigned short)SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO);
}

static void
SiS_GetCRT2ResInfo(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short xres, yres, modeflag=0, resindex;

   if(SiS_Pr->UseCustomMode) {
      xres = SiS_Pr->CHDisplay;
      if(SiS_Pr->CModeFlag & HalfDCLK) xres <<= 1;
      SiS_Pr->SiS_VGAHDE = SiS_Pr->SiS_HDE = xres;
      /* DoubleScanMode-check done in CheckCalcCustomMode()! */
      SiS_Pr->SiS_VGAVDE = SiS_Pr->SiS_VDE = SiS_Pr->CVDisplay;
      return;
   }

   resindex = SiS_GetResInfo(SiS_Pr,ModeNo,ModeIdIndex);

   if(ModeNo <= 0x13) {
      xres = SiS_Pr->SiS_StResInfo[resindex].HTotal;
      yres = SiS_Pr->SiS_StResInfo[resindex].VTotal;
   } else {
      xres = SiS_Pr->SiS_ModeResInfo[resindex].HTotal;
      yres = SiS_Pr->SiS_ModeResInfo[resindex].VTotal;
      modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
   }

   if(!SiS_Pr->SiS_IF_DEF_DSTN && !SiS_Pr->SiS_IF_DEF_FSTN) {

      if((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->SiS_IF_DEF_LVDS == 1)) {
	 if((ModeNo != 0x03) && (SiS_Pr->SiS_SetFlag & SetDOSMode)) {
	    if(yres == 350) yres = 400;
	 }
	 if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x3a) & 0x01) {
	    if(ModeNo == 0x12) yres = 400;
	 }
      }

      if(modeflag & HalfDCLK)       xres <<= 1;
      if(modeflag & DoubleScanMode) yres <<= 1;

   }

   if((SiS_Pr->SiS_VBType & VB_SISVB) && (!(SiS_Pr->SiS_VBType & VB_NoLCD))) {

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	 switch(SiS_Pr->SiS_LCDResInfo) {
	   case Panel_1024x768:
	      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
		 if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
		    if(yres == 350) yres = 357;
		    if(yres == 400) yres = 420;
		    if(yres == 480) yres = 525;
		 }
	      }
	      break;
	   case Panel_1280x1024:
	      if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
		 /* BIOS bug - does this regardless of scaling */
		 if(yres == 400) yres = 405;
	      }
	      if(yres == 350) yres = 360;
	      if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
		 if(yres == 360) yres = 375;
	      }
	      break;
	   case Panel_1600x1200:
	      if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
		 if(yres == 1024) yres = 1056;
	      }
	      break;
	 }
      }

   } else {

      if(SiS_Pr->SiS_VBType & VB_SISVB) {
	 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToHiVision)) {
	    if(xres == 720) xres = 640;
	 }
      } else if(xres == 720) xres = 640;

      if(SiS_Pr->SiS_SetFlag & SetDOSMode) {
	 yres = 400;
	 if(SiS_Pr->ChipType >= SIS_315H) {
	    if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x17) & 0x80) yres = 480;
	 } else {
	    if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x80) yres = 480;
	 }
	 if(SiS_Pr->SiS_IF_DEF_DSTN || SiS_Pr->SiS_IF_DEF_FSTN) yres = 480;
      }

   }
   SiS_Pr->SiS_VGAHDE = SiS_Pr->SiS_HDE = xres;
   SiS_Pr->SiS_VGAVDE = SiS_Pr->SiS_VDE = yres;
}

/*********************************************/
/*           GET CRT2 TIMING DATA            */
/*********************************************/

static void
SiS_GetCRT2Ptr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
	       unsigned short RefreshRateTableIndex, unsigned short *CRT2Index,
	       unsigned short *ResIndex)
{
  unsigned short tempbx=0, tempal=0, resinfo=0;

  if(ModeNo <= 0x13) {
     tempal = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_IF_DEF_LVDS == 0)) {

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {                            /* LCD */

	tempbx = SiS_Pr->SiS_LCDResInfo;
	if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx += 32;

	/* patch index */
	if(SiS_Pr->SiS_LCDResInfo == Panel_1680x1050) {
	   if     (resinfo == SIS_RI_1280x800)  tempal =  9;
	   else if(resinfo == SIS_RI_1400x1050) tempal = 11;
	} else if((SiS_Pr->SiS_LCDResInfo == Panel_1280x800) ||
		  (SiS_Pr->SiS_LCDResInfo == Panel_1280x800_2) ||
		  (SiS_Pr->SiS_LCDResInfo == Panel_1280x854)) {
	   if     (resinfo == SIS_RI_1280x768)  tempal =  9;
	}

	if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	   /* Pass 1:1 only (center-screen handled outside) */
	   /* This is never called for the panel's native resolution */
	   /* since Pass1:1 will not be set in this case */
	   tempbx = 100;
	   if(ModeNo >= 0x13) {
	      tempal = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC_NS;
	   }
	}

#ifdef CONFIG_FB_SIS_315
	if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
	   if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) {
	      if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
		 tempbx = 200;
		 if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx++;
	      }
	   }
	}
#endif

     } else {						  	/* TV */

	if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	   /* if(SiS_Pr->SiS_VGAVDE > 480) SiS_Pr->SiS_TVMode &= (~TVSetTVSimuMode); */
	   tempbx = 2;
	   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	      tempbx = 13;
	      if(!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) tempbx = 14;
	   }
	} else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	   if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)	tempbx = 7;
	   else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)	tempbx = 6;
	   else						tempbx = 5;
	   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)	tempbx += 5;
	} else {
	   if(SiS_Pr->SiS_TVMode & TVSetPAL)		tempbx = 3;
	   else						tempbx = 4;
	   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)	tempbx += 5;
	}

     }

     tempal &= 0x3F;

     if(ModeNo > 0x13) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision) {
	   switch(resinfo) {
	   case SIS_RI_720x480:
	      tempal = 6;
	      if(SiS_Pr->SiS_TVMode & (TVSetPAL | TVSetPALN))	tempal = 9;
	      break;
	   case SIS_RI_720x576:
	   case SIS_RI_768x576:
	   case SIS_RI_1024x576: /* Not in NTSC or YPBPR mode (except 1080i)! */
	      tempal = 6;
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
		 if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)	tempal = 8;
	      }
	      break;
	   case SIS_RI_800x480:
	      tempal = 4;
	      break;
	   case SIS_RI_512x384:
	   case SIS_RI_1024x768:
	      tempal = 7;
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
		 if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)	tempal = 8;
	      }
	      break;
	   case SIS_RI_1280x720:
	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
		 if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)	tempal = 9;
	      }
	      break;
	   }
	}
     }

     *CRT2Index = tempbx;
     *ResIndex = tempal;

  } else {   /* LVDS, 301B-DH (if running on LCD) */

     tempbx = 0;
     if((SiS_Pr->SiS_IF_DEF_CH70xx) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {

	tempbx = 90;
	if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	   tempbx = 92;
	   if(SiS_Pr->SiS_ModeType > ModeVGA) {
	      if(SiS_Pr->SiS_CHSOverScan) tempbx = 99;
	   }
	   if(SiS_Pr->SiS_TVMode & TVSetPALM)      tempbx = 94;
	   else if(SiS_Pr->SiS_TVMode & TVSetPALN) tempbx = 96;
	}
	if(tempbx != 99) {
	   if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) tempbx++;
	}

     } else {

	switch(SiS_Pr->SiS_LCDResInfo) {
	case Panel_640x480:   tempbx = 12; break;
	case Panel_320x240_1: tempbx = 10; break;
	case Panel_320x240_2:
	case Panel_320x240_3: tempbx = 14; break;
	case Panel_800x600:   tempbx = 16; break;
	case Panel_1024x600:  tempbx = 18; break;
	case Panel_1152x768:
	case Panel_1024x768:  tempbx = 20; break;
	case Panel_1280x768:  tempbx = 22; break;
	case Panel_1280x1024: tempbx = 24; break;
	case Panel_1400x1050: tempbx = 26; break;
	case Panel_1600x1200: tempbx = 28; break;
#ifdef CONFIG_FB_SIS_300
	case Panel_Barco1366: tempbx = 80; break;
#endif
	}

	switch(SiS_Pr->SiS_LCDResInfo) {
	case Panel_320x240_1:
	case Panel_320x240_2:
	case Panel_320x240_3:
	case Panel_640x480:
	   break;
	default:
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
	}

	if(SiS_Pr->SiS_LCDInfo & LCDPass11) tempbx = 30;

#ifdef CONFIG_FB_SIS_300
	if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
	   tempbx = 82;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
	} else if(SiS_Pr->SiS_CustomT == CUT_PANEL848 || SiS_Pr->SiS_CustomT == CUT_PANEL856) {
	   tempbx = 84;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
	}
#endif

     }

     (*CRT2Index) = tempbx;
     (*ResIndex) = tempal & 0x1F;
  }
}

static void
SiS_GetRAMDAC2DATA(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned short tempax=0, tempbx=0, index, dotclock;
  unsigned short temp1=0, modeflag=0, tempcx=0;

  SiS_Pr->SiS_RVBHCMAX  = 1;
  SiS_Pr->SiS_RVBHCFACT = 1;

  if(ModeNo <= 0x13) {

     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     index = SiS_GetModePtr(SiS_Pr,ModeNo,ModeIdIndex);

     tempax = SiS_Pr->SiS_StandTable[index].CRTC[0];
     tempbx = SiS_Pr->SiS_StandTable[index].CRTC[6];
     temp1 = SiS_Pr->SiS_StandTable[index].CRTC[7];

     dotclock = (modeflag & Charx8Dot) ? 8 : 9;

  } else {

     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     index = SiS_GetRefCRT1CRTC(SiS_Pr, RefreshRateTableIndex, SiS_Pr->SiS_UseWideCRT2);

     tempax = SiS_Pr->SiS_CRT1Table[index].CR[0];
     tempax |= (SiS_Pr->SiS_CRT1Table[index].CR[14] << 8);
     tempax &= 0x03FF;
     tempbx = SiS_Pr->SiS_CRT1Table[index].CR[6];
     tempcx = SiS_Pr->SiS_CRT1Table[index].CR[13] << 8;
     tempcx &= 0x0100;
     tempcx <<= 2;
     tempbx |= tempcx;
     temp1  = SiS_Pr->SiS_CRT1Table[index].CR[7];

     dotclock = 8;

  }

  if(temp1 & 0x01) tempbx |= 0x0100;
  if(temp1 & 0x20) tempbx |= 0x0200;

  tempax += 5;
  tempax *= dotclock;
  if(modeflag & HalfDCLK) tempax <<= 1;

  tempbx++;

  SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = tempax;
  SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = tempbx;
}

static void
SiS_CalcPanelLinkTiming(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RefreshRateTableIndex)
{
   unsigned short ResIndex;

   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
	 if(SiS_Pr->UseCustomMode) {
	    ResIndex = SiS_Pr->CHTotal;
	    if(SiS_Pr->CModeFlag & HalfDCLK) ResIndex <<= 1;
	    SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = ResIndex;
	    SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = SiS_Pr->CVTotal;
	 } else {
	    if(ModeNo < 0x13) {
	       ResIndex = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
	    } else {
	       ResIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC_NS;
	    }
	    if(ResIndex == 0x09) {
	       if(SiS_Pr->Alternate1600x1200)        ResIndex = 0x20; /* 1600x1200 LCDA */
	       else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) ResIndex = 0x21; /* 1600x1200 LVDS */
	    }
	    SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_NoScaleData[ResIndex].VGAHT;
	    SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_NoScaleData[ResIndex].VGAVT;
	    SiS_Pr->SiS_HT    = SiS_Pr->SiS_NoScaleData[ResIndex].LCDHT;
	    SiS_Pr->SiS_VT    = SiS_Pr->SiS_NoScaleData[ResIndex].LCDVT;
	 }
      } else {
	 SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = SiS_Pr->PanelHT;
	 SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = SiS_Pr->PanelVT;
      }
   } else {
      /* This handles custom modes and custom panels */
      SiS_Pr->SiS_HDE = SiS_Pr->PanelXRes;
      SiS_Pr->SiS_VDE = SiS_Pr->PanelYRes;
      SiS_Pr->SiS_HT  = SiS_Pr->PanelHT;
      SiS_Pr->SiS_VT  = SiS_Pr->PanelVT;
      SiS_Pr->SiS_VGAHT = SiS_Pr->PanelHT - (SiS_Pr->PanelXRes - SiS_Pr->SiS_VGAHDE);
      SiS_Pr->SiS_VGAVT = SiS_Pr->PanelVT - (SiS_Pr->PanelYRes - SiS_Pr->SiS_VGAVDE);
   }
}

static void
SiS_GetCRT2DataLVDS(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
                    unsigned short RefreshRateTableIndex)
{
   unsigned short CRT2Index, ResIndex, backup;
   const struct SiS_LVDSData *LVDSData = NULL;

   SiS_GetCRT2ResInfo(SiS_Pr, ModeNo, ModeIdIndex);

   if(SiS_Pr->SiS_VBType & VB_SISVB) {
      SiS_Pr->SiS_RVBHCMAX  = 1;
      SiS_Pr->SiS_RVBHCFACT = 1;
      SiS_Pr->SiS_NewFlickerMode = 0;
      SiS_Pr->SiS_RVBHRS = 50;
      SiS_Pr->SiS_RY1COE = 0;
      SiS_Pr->SiS_RY2COE = 0;
      SiS_Pr->SiS_RY3COE = 0;
      SiS_Pr->SiS_RY4COE = 0;
      SiS_Pr->SiS_RVBHRS2 = 0;
   }

   if((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {

#ifdef CONFIG_FB_SIS_315
      SiS_CalcPanelLinkTiming(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      SiS_CalcLCDACRT1Timing(SiS_Pr, ModeNo, ModeIdIndex);
#endif

   } else {

      /* 301BDH needs LVDS Data */
      backup = SiS_Pr->SiS_IF_DEF_LVDS;
      if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	 SiS_Pr->SiS_IF_DEF_LVDS = 1;
      }

      SiS_GetCRT2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                     		            &CRT2Index, &ResIndex);

      SiS_Pr->SiS_IF_DEF_LVDS = backup;

      switch(CRT2Index) {
	 case 10: LVDSData = SiS_Pr->SiS_LVDS320x240Data_1;    break;
	 case 14: LVDSData = SiS_Pr->SiS_LVDS320x240Data_2;    break;
	 case 12: LVDSData = SiS_Pr->SiS_LVDS640x480Data_1;    break;
	 case 16: LVDSData = SiS_Pr->SiS_LVDS800x600Data_1;    break;
	 case 18: LVDSData = SiS_Pr->SiS_LVDS1024x600Data_1;   break;
	 case 20: LVDSData = SiS_Pr->SiS_LVDS1024x768Data_1;   break;
#ifdef CONFIG_FB_SIS_300
	 case 80: LVDSData = SiS_Pr->SiS_LVDSBARCO1366Data_1;  break;
	 case 81: LVDSData = SiS_Pr->SiS_LVDSBARCO1366Data_2;  break;
	 case 82: LVDSData = SiS_Pr->SiS_LVDSBARCO1024Data_1;  break;
	 case 84: LVDSData = SiS_Pr->SiS_LVDS848x480Data_1;    break;
	 case 85: LVDSData = SiS_Pr->SiS_LVDS848x480Data_2;    break;
#endif
	 case 90: LVDSData = SiS_Pr->SiS_CHTVUNTSCData;        break;
	 case 91: LVDSData = SiS_Pr->SiS_CHTVONTSCData;        break;
	 case 92: LVDSData = SiS_Pr->SiS_CHTVUPALData;         break;
	 case 93: LVDSData = SiS_Pr->SiS_CHTVOPALData;         break;
	 case 94: LVDSData = SiS_Pr->SiS_CHTVUPALMData;        break;
	 case 95: LVDSData = SiS_Pr->SiS_CHTVOPALMData;        break;
	 case 96: LVDSData = SiS_Pr->SiS_CHTVUPALNData;        break;
	 case 97: LVDSData = SiS_Pr->SiS_CHTVOPALNData;        break;
	 case 99: LVDSData = SiS_Pr->SiS_CHTVSOPALData;	       break;
      }

      if(LVDSData) {
	 SiS_Pr->SiS_VGAHT = (LVDSData+ResIndex)->VGAHT;
	 SiS_Pr->SiS_VGAVT = (LVDSData+ResIndex)->VGAVT;
	 SiS_Pr->SiS_HT    = (LVDSData+ResIndex)->LCDHT;
	 SiS_Pr->SiS_VT    = (LVDSData+ResIndex)->LCDVT;
      } else {
	 SiS_CalcPanelLinkTiming(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      }

      if( (!(SiS_Pr->SiS_VBType & VB_SISVB)) &&
	  (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&
	  (!(SiS_Pr->SiS_LCDInfo & LCDPass11)) ) {
	 if( (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ||
	     (SiS_Pr->SiS_SetFlag & SetDOSMode) ) {
	    SiS_Pr->SiS_HDE = SiS_Pr->PanelXRes;
            SiS_Pr->SiS_VDE = SiS_Pr->PanelYRes;
#ifdef CONFIG_FB_SIS_300
	    if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
	       if(ResIndex < 0x08) {
		  SiS_Pr->SiS_HDE = 1280;
		  SiS_Pr->SiS_VDE = 1024;
	       }
	    }
#endif
         }
      }
   }
}

static void
SiS_GetCRT2Data301(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned char  *ROMAddr = NULL;
  unsigned short tempax, tempbx, modeflag, romptr=0;
  unsigned short resinfo, CRT2Index, ResIndex;
  const struct SiS_LCDData *LCDPtr = NULL;
  const struct SiS_TVData  *TVPtr  = NULL;
#ifdef CONFIG_FB_SIS_315
  short resinfo661;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
     resinfo = 0;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
#ifdef CONFIG_FB_SIS_315
     resinfo661 = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].ROMMODEIDX661;
     if( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)   &&
	 (SiS_Pr->SiS_SetFlag & LCDVESATiming) &&
	 (resinfo661 >= 0)                     &&
	 (SiS_Pr->SiS_NeedRomModeData) ) {
	if((ROMAddr = GetLCDStructPtr661(SiS_Pr))) {
	   if((romptr = (SISGETROMW(21)))) {
	      romptr += (resinfo661 * 10);
	      ROMAddr = SiS_Pr->VirtualRomBase;
	   }
	}
     }
#endif
  }

  SiS_Pr->SiS_NewFlickerMode = 0;
  SiS_Pr->SiS_RVBHRS = 50;
  SiS_Pr->SiS_RY1COE = 0;
  SiS_Pr->SiS_RY2COE = 0;
  SiS_Pr->SiS_RY3COE = 0;
  SiS_Pr->SiS_RY4COE = 0;
  SiS_Pr->SiS_RVBHRS2 = 0;

  SiS_GetCRT2ResInfo(SiS_Pr,ModeNo,ModeIdIndex);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {

     if(SiS_Pr->UseCustomMode) {

	SiS_Pr->SiS_RVBHCMAX  = 1;
	SiS_Pr->SiS_RVBHCFACT = 1;
	SiS_Pr->SiS_HDE       = SiS_Pr->SiS_VGAHDE;
	SiS_Pr->SiS_VDE       = SiS_Pr->SiS_VGAVDE;

	tempax = SiS_Pr->CHTotal;
	if(modeflag & HalfDCLK) tempax <<= 1;
	SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = tempax;
	SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = SiS_Pr->CVTotal;

     } else {

	SiS_GetRAMDAC2DATA(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);

     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {

     SiS_GetCRT2Ptr(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
		    &CRT2Index,&ResIndex);

     switch(CRT2Index) {
	case  2: TVPtr = SiS_Pr->SiS_ExtHiTVData;   break;
	case  3: TVPtr = SiS_Pr->SiS_ExtPALData;    break;
	case  4: TVPtr = SiS_Pr->SiS_ExtNTSCData;   break;
	case  5: TVPtr = SiS_Pr->SiS_Ext525iData;   break;
	case  6: TVPtr = SiS_Pr->SiS_Ext525pData;   break;
	case  7: TVPtr = SiS_Pr->SiS_Ext750pData;   break;
	case  8: TVPtr = SiS_Pr->SiS_StPALData;     break;
	case  9: TVPtr = SiS_Pr->SiS_StNTSCData;    break;
	case 10: TVPtr = SiS_Pr->SiS_St525iData;    break;
	case 11: TVPtr = SiS_Pr->SiS_St525pData;    break;
	case 12: TVPtr = SiS_Pr->SiS_St750pData;    break;
	case 13: TVPtr = SiS_Pr->SiS_St1HiTVData;   break;
	case 14: TVPtr = SiS_Pr->SiS_St2HiTVData;   break;
	default: TVPtr = SiS_Pr->SiS_StPALData;     break;
     }

     SiS_Pr->SiS_RVBHCMAX  = (TVPtr+ResIndex)->RVBHCMAX;
     SiS_Pr->SiS_RVBHCFACT = (TVPtr+ResIndex)->RVBHCFACT;
     SiS_Pr->SiS_VGAHT     = (TVPtr+ResIndex)->VGAHT;
     SiS_Pr->SiS_VGAVT     = (TVPtr+ResIndex)->VGAVT;
     SiS_Pr->SiS_HDE       = (TVPtr+ResIndex)->TVHDE;
     SiS_Pr->SiS_VDE       = (TVPtr+ResIndex)->TVVDE;
     SiS_Pr->SiS_RVBHRS2   = (TVPtr+ResIndex)->RVBHRS2 & 0x0fff;
     if(modeflag & HalfDCLK) {
	SiS_Pr->SiS_RVBHRS = (TVPtr+ResIndex)->HALFRVBHRS;
	if(SiS_Pr->SiS_RVBHRS2) {
	   SiS_Pr->SiS_RVBHRS2 = ((SiS_Pr->SiS_RVBHRS2 + 3) >> 1) - 3;
	   tempax = ((TVPtr+ResIndex)->RVBHRS2 >> 12) & 0x07;
	   if((TVPtr+ResIndex)->RVBHRS2 & 0x8000) SiS_Pr->SiS_RVBHRS2 -= tempax;
	   else                                   SiS_Pr->SiS_RVBHRS2 += tempax;
	}
     } else {
	SiS_Pr->SiS_RVBHRS    = (TVPtr+ResIndex)->RVBHRS;
     }
     SiS_Pr->SiS_NewFlickerMode = ((TVPtr+ResIndex)->FlickerMode) << 7;

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

	if((resinfo == SIS_RI_960x600)   ||
	   (resinfo == SIS_RI_1024x768)  ||
	   (resinfo == SIS_RI_1280x1024) ||
	   (resinfo == SIS_RI_1280x720)) {
	   SiS_Pr->SiS_NewFlickerMode = 0x40;
	}

	if(SiS_Pr->SiS_VGAVDE == 350) SiS_Pr->SiS_TVMode |= TVSetTVSimuMode;

	SiS_Pr->SiS_HT = ExtHiTVHT;
	SiS_Pr->SiS_VT = ExtHiTVVT;
	if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	      SiS_Pr->SiS_HT = StHiTVHT;
	      SiS_Pr->SiS_VT = StHiTVVT;
	   }
	}

     } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {

	if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) {
	   SiS_Pr->SiS_HT = 1650;
	   SiS_Pr->SiS_VT = 750;
	} else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	   SiS_Pr->SiS_HT = NTSCHT;
	   if(SiS_Pr->SiS_TVMode & TVSet525p1024) SiS_Pr->SiS_HT = NTSC2HT;
	   SiS_Pr->SiS_VT = NTSCVT;
	} else {
	   SiS_Pr->SiS_HT = NTSCHT;
	   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) SiS_Pr->SiS_HT = NTSC2HT;
	   SiS_Pr->SiS_VT = NTSCVT;
	}

     } else {

	SiS_Pr->SiS_RY1COE = (TVPtr+ResIndex)->RY1COE;
	SiS_Pr->SiS_RY2COE = (TVPtr+ResIndex)->RY2COE;
	SiS_Pr->SiS_RY3COE = (TVPtr+ResIndex)->RY3COE;
	SiS_Pr->SiS_RY4COE = (TVPtr+ResIndex)->RY4COE;

	if(modeflag & HalfDCLK) {
	   SiS_Pr->SiS_RY1COE = 0x00;
	   SiS_Pr->SiS_RY2COE = 0xf4;
	   SiS_Pr->SiS_RY3COE = 0x10;
	   SiS_Pr->SiS_RY4COE = 0x38;
	}

	if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
	   SiS_Pr->SiS_HT = NTSCHT;
	   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) SiS_Pr->SiS_HT = NTSC2HT;
	   SiS_Pr->SiS_VT = NTSCVT;
	} else {
	   SiS_Pr->SiS_HT = PALHT;
	   SiS_Pr->SiS_VT = PALVT;
	}

     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

     SiS_Pr->SiS_RVBHCMAX  = 1;
     SiS_Pr->SiS_RVBHCFACT = 1;

     if(SiS_Pr->UseCustomMode) {

	SiS_Pr->SiS_HDE   = SiS_Pr->SiS_VGAHDE;
	SiS_Pr->SiS_VDE   = SiS_Pr->SiS_VGAVDE;

	tempax = SiS_Pr->CHTotal;
	if(modeflag & HalfDCLK) tempax <<= 1;
	SiS_Pr->SiS_VGAHT = SiS_Pr->SiS_HT = tempax;
	SiS_Pr->SiS_VGAVT = SiS_Pr->SiS_VT = SiS_Pr->CVTotal;

     } else {

	bool gotit = false;

	if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {

	   SiS_Pr->SiS_VGAHT = SiS_Pr->PanelHT;
	   SiS_Pr->SiS_VGAVT = SiS_Pr->PanelVT;
	   SiS_Pr->SiS_HT    = SiS_Pr->PanelHT;
	   SiS_Pr->SiS_VT    = SiS_Pr->PanelVT;
	   gotit = true;

	} else if( (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) && (romptr) && (ROMAddr) ) {

#ifdef CONFIG_FB_SIS_315
	   SiS_Pr->SiS_RVBHCMAX  = ROMAddr[romptr];
	   SiS_Pr->SiS_RVBHCFACT = ROMAddr[romptr+1];
	   SiS_Pr->SiS_VGAHT     = ROMAddr[romptr+2] | ((ROMAddr[romptr+3] & 0x0f) << 8);
	   SiS_Pr->SiS_VGAVT     = (ROMAddr[romptr+4] << 4) | ((ROMAddr[romptr+3] & 0xf0) >> 4);
	   SiS_Pr->SiS_HT        = ROMAddr[romptr+5] | ((ROMAddr[romptr+6] & 0x0f) << 8);
	   SiS_Pr->SiS_VT        = (ROMAddr[romptr+7] << 4) | ((ROMAddr[romptr+6] & 0xf0) >> 4);
	   SiS_Pr->SiS_RVBHRS2   = ROMAddr[romptr+8] | ((ROMAddr[romptr+9] & 0x0f) << 8);
	   if((SiS_Pr->SiS_RVBHRS2) && (modeflag & HalfDCLK)) {
	      SiS_Pr->SiS_RVBHRS2 = ((SiS_Pr->SiS_RVBHRS2 + 3) >> 1) - 3;
	      tempax = (ROMAddr[romptr+9] >> 4) & 0x07;
	      if(ROMAddr[romptr+9] & 0x80) SiS_Pr->SiS_RVBHRS2 -= tempax;
	      else                         SiS_Pr->SiS_RVBHRS2 += tempax;
	   }
	   if(SiS_Pr->SiS_VGAHT) gotit = true;
	   else {
	      SiS_Pr->SiS_LCDInfo |= DontExpandLCD;
	      SiS_Pr->SiS_LCDInfo &= ~LCDPass11;
	      SiS_Pr->SiS_RVBHCMAX  = 1;
	      SiS_Pr->SiS_RVBHCFACT = 1;
	      SiS_Pr->SiS_VGAHT   = SiS_Pr->PanelHT;
	      SiS_Pr->SiS_VGAVT   = SiS_Pr->PanelVT;
	      SiS_Pr->SiS_HT      = SiS_Pr->PanelHT;
	      SiS_Pr->SiS_VT      = SiS_Pr->PanelVT;
	      SiS_Pr->SiS_RVBHRS2 = 0;
	      gotit = true;
	   }
#endif

	}

	if(!gotit) {

	   SiS_GetCRT2Ptr(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex,
			  &CRT2Index,&ResIndex);

	   switch(CRT2Index) {
	      case Panel_1024x768      : LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;   break;
	      case Panel_1024x768  + 32: LCDPtr = SiS_Pr->SiS_St2LCD1024x768Data;   break;
	      case Panel_1280x720      :
	      case Panel_1280x720  + 32: LCDPtr = SiS_Pr->SiS_LCD1280x720Data;      break;
	      case Panel_1280x768_2    : LCDPtr = SiS_Pr->SiS_ExtLCD1280x768_2Data; break;
	      case Panel_1280x768_2+ 32: LCDPtr = SiS_Pr->SiS_StLCD1280x768_2Data;  break;
	      case Panel_1280x800      :
	      case Panel_1280x800  + 32: LCDPtr = SiS_Pr->SiS_LCD1280x800Data;      break;
	      case Panel_1280x800_2    :
	      case Panel_1280x800_2+ 32: LCDPtr = SiS_Pr->SiS_LCD1280x800_2Data;    break;
	      case Panel_1280x854      :
	      case Panel_1280x854  + 32: LCDPtr = SiS_Pr->SiS_LCD1280x854Data;      break;
	      case Panel_1280x960      :
	      case Panel_1280x960  + 32: LCDPtr = SiS_Pr->SiS_LCD1280x960Data;      break;
	      case Panel_1280x1024     : LCDPtr = SiS_Pr->SiS_ExtLCD1280x1024Data;  break;
	      case Panel_1280x1024 + 32: LCDPtr = SiS_Pr->SiS_St2LCD1280x1024Data;  break;
	      case Panel_1400x1050     : LCDPtr = SiS_Pr->SiS_ExtLCD1400x1050Data;  break;
	      case Panel_1400x1050 + 32: LCDPtr = SiS_Pr->SiS_StLCD1400x1050Data;   break;
	      case Panel_1600x1200     : LCDPtr = SiS_Pr->SiS_ExtLCD1600x1200Data;  break;
	      case Panel_1600x1200 + 32: LCDPtr = SiS_Pr->SiS_StLCD1600x1200Data;   break;
	      case Panel_1680x1050     :
	      case Panel_1680x1050 + 32: LCDPtr = SiS_Pr->SiS_LCD1680x1050Data;     break;
	      case 100		       : LCDPtr = SiS_Pr->SiS_NoScaleData;	    break;
#ifdef CONFIG_FB_SIS_315
	      case 200                 : LCDPtr = SiS310_ExtCompaq1280x1024Data;    break;
	      case 201                 : LCDPtr = SiS_Pr->SiS_St2LCD1280x1024Data;  break;
#endif
	      default                  : LCDPtr = SiS_Pr->SiS_ExtLCD1024x768Data;   break;
	   }

	   SiS_Pr->SiS_RVBHCMAX  = (LCDPtr+ResIndex)->RVBHCMAX;
	   SiS_Pr->SiS_RVBHCFACT = (LCDPtr+ResIndex)->RVBHCFACT;
	   SiS_Pr->SiS_VGAHT     = (LCDPtr+ResIndex)->VGAHT;
	   SiS_Pr->SiS_VGAVT     = (LCDPtr+ResIndex)->VGAVT;
	   SiS_Pr->SiS_HT        = (LCDPtr+ResIndex)->LCDHT;
	   SiS_Pr->SiS_VT        = (LCDPtr+ResIndex)->LCDVT;

        }

	tempax = SiS_Pr->PanelXRes;
	tempbx = SiS_Pr->PanelYRes;

	switch(SiS_Pr->SiS_LCDResInfo) {
	case Panel_1024x768:
	   if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
	      if(SiS_Pr->ChipType < SIS_315H) {
		 if     (SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
		 else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
	      }
	   } else {
	      if     (SiS_Pr->SiS_VGAVDE == 357) tempbx = 527;
	      else if(SiS_Pr->SiS_VGAVDE == 420) tempbx = 620;
	      else if(SiS_Pr->SiS_VGAVDE == 525) tempbx = 775;
	      else if(SiS_Pr->SiS_VGAVDE == 600) tempbx = 775;
	      else if(SiS_Pr->SiS_VGAVDE == 350) tempbx = 560;
	      else if(SiS_Pr->SiS_VGAVDE == 400) tempbx = 640;
	   }
	   break;
	case Panel_1280x960:
	   if     (SiS_Pr->SiS_VGAVDE == 350)  tempbx = 700;
	   else if(SiS_Pr->SiS_VGAVDE == 400)  tempbx = 800;
	   else if(SiS_Pr->SiS_VGAVDE == 1024) tempbx = 960;
	   break;
	case Panel_1280x1024:
	   if     (SiS_Pr->SiS_VGAVDE == 360) tempbx = 768;
	   else if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 800;
	   else if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 864;
	   break;
	case Panel_1600x1200:
	   if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
	      if     (SiS_Pr->SiS_VGAVDE == 350)  tempbx = 875;
	      else if(SiS_Pr->SiS_VGAVDE == 400)  tempbx = 1000;
	   }
	   break;
	}

	if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	   tempax = SiS_Pr->SiS_VGAHDE;
	   tempbx = SiS_Pr->SiS_VGAVDE;
	}

	SiS_Pr->SiS_HDE = tempax;
	SiS_Pr->SiS_VDE = tempbx;
     }
  }
}

static void
SiS_GetCRT2Data(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
                unsigned short RefreshRateTableIndex)
{

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
         SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      } else {
	 if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	    /* Need LVDS Data for LCD on 301B-DH */
	    SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 } else {
	    SiS_GetCRT2Data301(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 }
      }

   } else {

      SiS_GetCRT2DataLVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);

   }
}

/*********************************************/
/*         GET LVDS DES (SKEW) DATA          */
/*********************************************/

static const struct SiS_LVDSDes *
SiS_GetLVDSDesPtr(struct SiS_Private *SiS_Pr)
{
   const struct SiS_LVDSDes *PanelDesPtr = NULL;

#ifdef CONFIG_FB_SIS_300
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

      if(SiS_Pr->ChipType < SIS_315H) {
	 if(SiS_Pr->SiS_LCDTypeInfo == 4) {
	    if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
	       PanelDesPtr = SiS_Pr->SiS_PanelType04_1a;
	       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
		  PanelDesPtr = SiS_Pr->SiS_PanelType04_2a;
	       }
            } else if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
	       PanelDesPtr = SiS_Pr->SiS_PanelType04_1b;
	       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
		  PanelDesPtr = SiS_Pr->SiS_PanelType04_2b;
	       }
	    }
	 }
      }
   }
#endif
   return PanelDesPtr;
}

static void
SiS_GetLVDSDesData(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
                   unsigned short RefreshRateTableIndex)
{
  unsigned short modeflag, ResIndex;
  const struct SiS_LVDSDes *PanelDesPtr = NULL;

  SiS_Pr->SiS_LCDHDES = 0;
  SiS_Pr->SiS_LCDVDES = 0;

  /* Some special cases */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {

     /* Trumpion */
     if(SiS_Pr->SiS_IF_DEF_TRUMPION) {
	if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   if(SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) {
	      SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	   }
	}
	return;
     }

     /* 640x480 on LVDS */
     if(SiS_Pr->ChipType < SIS_315H) {
	if(SiS_Pr->SiS_LCDResInfo == Panel_640x480 && SiS_Pr->SiS_LCDTypeInfo == 3) {
	   SiS_Pr->SiS_LCDHDES = 8;
	   if     (SiS_Pr->SiS_VGAVDE >= 480) SiS_Pr->SiS_LCDVDES = 512;
	   else if(SiS_Pr->SiS_VGAVDE >= 400) SiS_Pr->SiS_LCDVDES = 436;
	   else if(SiS_Pr->SiS_VGAVDE >= 350) SiS_Pr->SiS_LCDVDES = 440;
	   return;
	}
     }

  } /* LCD */

  if( (SiS_Pr->UseCustomMode) 		         ||
      (SiS_Pr->SiS_LCDResInfo == Panel_Custom)   ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL848)      ||
      (SiS_Pr->SiS_CustomT == CUT_PANEL856)      ||
      (SiS_Pr->SiS_LCDInfo & LCDPass11) ) {
     return;
  }

  if(ModeNo <= 0x13) ResIndex = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else               ResIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  if((SiS_Pr->SiS_VBType & VB_SIS30xBLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {

#ifdef CONFIG_FB_SIS_315
     if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	/* non-pass 1:1 only, see above */
	if(SiS_Pr->SiS_VGAHDE != SiS_Pr->PanelXRes) {
	   SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_HT - ((SiS_Pr->PanelXRes - SiS_Pr->SiS_VGAHDE) / 2);
	}
	if(SiS_Pr->SiS_VGAVDE != SiS_Pr->PanelYRes) {
	   SiS_Pr->SiS_LCDVDES = SiS_Pr->SiS_VT - ((SiS_Pr->PanelYRes - SiS_Pr->SiS_VGAVDE) / 2);
	}
     }
     if(SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) {
	switch(SiS_Pr->SiS_CustomT) {
	case CUT_UNIWILL1024:
	case CUT_UNIWILL10242:
	case CUT_CLEVO1400:
	   if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	      SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	   }
	   break;
	}
	switch(SiS_Pr->SiS_LCDResInfo) {
	case Panel_1280x1024:
	   if(SiS_Pr->SiS_CustomT != CUT_COMPAQ1280) {
	      SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	   }
	   break;
	case Panel_1280x800:	/* Verified for Averatec 6240 */
	case Panel_1280x800_2:	/* Verified for Asus A4L */
	case Panel_1280x854:    /* Not verified yet FIXME */
	   SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	   break;
	}
     }
#endif

  } else {

     if((SiS_Pr->SiS_IF_DEF_CH70xx != 0) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {

	if((SiS_Pr->SiS_TVMode & TVSetPAL) && (!(SiS_Pr->SiS_TVMode & TVSetPALM))) {
	   if(ResIndex <= 3) SiS_Pr->SiS_LCDHDES = 256;
	}

     } else if((PanelDesPtr = SiS_GetLVDSDesPtr(SiS_Pr))) {

	SiS_Pr->SiS_LCDHDES = (PanelDesPtr+ResIndex)->LCDHDES;
	SiS_Pr->SiS_LCDVDES = (PanelDesPtr+ResIndex)->LCDVDES;

     } else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {

	if(SiS_Pr->SiS_VGAHDE != SiS_Pr->PanelXRes) {
	   SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_HT - ((SiS_Pr->PanelXRes - SiS_Pr->SiS_VGAHDE) / 2);
	}
	if(SiS_Pr->SiS_VGAVDE != SiS_Pr->PanelYRes) {
	   SiS_Pr->SiS_LCDVDES = SiS_Pr->SiS_VT - ((SiS_Pr->PanelYRes - SiS_Pr->SiS_VGAVDE) / 2);
	} else {
	   if(SiS_Pr->ChipType < SIS_315H) {
	      SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	   } else {
	      switch(SiS_Pr->SiS_LCDResInfo) {
	      case Panel_800x600:
	      case Panel_1024x768:
	      case Panel_1280x1024:
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT;
		 break;
	      case Panel_1400x1050:
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
		 break;
	      }
	   }
	}

     } else {

        if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
	   switch(SiS_Pr->SiS_LCDResInfo) {
	   case Panel_800x600:
	      if(SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) {
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	      } else {
		 SiS_Pr->SiS_LCDHDES = SiS_Pr->PanelHT + 3;
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT;
		 if(SiS_Pr->SiS_VGAVDE == 400) SiS_Pr->SiS_LCDVDES -= 2;
		 else                          SiS_Pr->SiS_LCDVDES -= 4;
	      }
	      break;
	   case Panel_1024x768:
	      if(SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) {
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	      } else {
		 SiS_Pr->SiS_LCDHDES = SiS_Pr->PanelHT - 1;
		 if(SiS_Pr->SiS_VGAVDE <= 400) SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 8;
		 if(SiS_Pr->SiS_VGAVDE <= 350) SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 12;
	      }
	      break;
	   case Panel_1024x600:
	   default:
	      if( (SiS_Pr->SiS_VGAHDE == SiS_Pr->PanelXRes) &&
		  (SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) ) {
		 SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	      } else {
		 SiS_Pr->SiS_LCDHDES = SiS_Pr->PanelHT - 1;
	      }
	      break;
	   }

	   switch(SiS_Pr->SiS_LCDTypeInfo) {
	   case 1:
	      SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_LCDVDES = 0;
	      break;
	   case 3: /* 640x480 only? */
	      SiS_Pr->SiS_LCDHDES = 8;
	      if     (SiS_Pr->SiS_VGAVDE >= 480) SiS_Pr->SiS_LCDVDES = 512;
	      else if(SiS_Pr->SiS_VGAVDE >= 400) SiS_Pr->SiS_LCDVDES = 436;
	      else if(SiS_Pr->SiS_VGAVDE >= 350) SiS_Pr->SiS_LCDVDES = 440;
	      break;
	   }
#endif
        } else {
#ifdef CONFIG_FB_SIS_315
	   switch(SiS_Pr->SiS_LCDResInfo) {
	   case Panel_1024x768:
	   case Panel_1280x1024:
	      if(SiS_Pr->SiS_VGAVDE == SiS_Pr->PanelYRes) {
	         SiS_Pr->SiS_LCDVDES = SiS_Pr->PanelVT - 1;
	      }
	      break;
	   case Panel_320x240_1:
	   case Panel_320x240_2:
	   case Panel_320x240_3:
	      SiS_Pr->SiS_LCDVDES = 524;
	      break;
	   }
#endif
	}
     }

     if((ModeNo <= 0x13) && (SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {
	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	if((SiS_Pr->SiS_VBType & VB_SIS30xBLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	   if(!(modeflag & HalfDCLK)) SiS_Pr->SiS_LCDHDES = 632;
	} else if(!(SiS_Pr->SiS_SetFlag & SetDOSMode)) {
	   if(SiS_Pr->SiS_LCDResInfo != Panel_1280x1024) {
	      if(SiS_Pr->SiS_LCDResInfo >= Panel_1024x768) {
	         if(SiS_Pr->ChipType < SIS_315H) {
	            if(!(modeflag & HalfDCLK)) SiS_Pr->SiS_LCDHDES = 320;
	         } else {
#ifdef CONFIG_FB_SIS_315
		    if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768)  SiS_Pr->SiS_LCDHDES = 480;
		    if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) SiS_Pr->SiS_LCDHDES = 804;
		    if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) SiS_Pr->SiS_LCDHDES = 704;
		    if(!(modeflag & HalfDCLK)) {
		       SiS_Pr->SiS_LCDHDES = 320;
		       if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) SiS_Pr->SiS_LCDHDES = 632;
		       if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) SiS_Pr->SiS_LCDHDES = 542;
        	    }
#endif
		 }
	      }
	   }
	}
     }
  }
}

/*********************************************/
/*           DISABLE VIDEO BRIDGE            */
/*********************************************/

#ifdef CONFIG_FB_SIS_315
static int
SiS_HandlePWD(struct SiS_Private *SiS_Pr)
{
   int ret = 0;
#ifdef SET_PWD
   unsigned char *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short romptr = GetLCDStructPtr661_2(SiS_Pr);
   unsigned char  drivermode = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40;
   unsigned short temp;

   if( (SiS_Pr->SiS_VBType & VB_SISPWD) &&
       (romptr)				&&
       (SiS_Pr->SiS_PWDOffset) ) {
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2b,ROMAddr[romptr + SiS_Pr->SiS_PWDOffset + 0]);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2c,ROMAddr[romptr + SiS_Pr->SiS_PWDOffset + 1]);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2d,ROMAddr[romptr + SiS_Pr->SiS_PWDOffset + 2]);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2e,ROMAddr[romptr + SiS_Pr->SiS_PWDOffset + 3]);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2f,ROMAddr[romptr + SiS_Pr->SiS_PWDOffset + 4]);
      temp = 0x00;
      if((ROMAddr[romptr + 2] & (0x06 << 1)) && !drivermode) {
         temp = 0x80;
	 ret = 1;
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x27,0x7f,temp);
   }
#endif
   return ret;
}
#endif

/* NEVER use any variables (VBInfo), this will be called
 * from outside the context of modeswitch!
 * MUST call getVBType before calling this
 */
void
SiS_DisableBridge(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
  unsigned short tempah, pushax=0, modenum;
#endif
  unsigned short temp=0;

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {		/* ===== For 30xB/C/LV ===== */

	if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300	   /* 300 series */

	   if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
	      if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
		 SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFE);
	      } else {
		 SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x08);
	      }
	      SiS_PanelDelay(SiS_Pr, 3);
	   }
	   if(SiS_Is301B(SiS_Pr)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0x3f);
	      SiS_ShortDelay(SiS_Pr,1);
	   }
	   SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);
	   SiS_DisplayOff(SiS_Pr);
	   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	   SiS_UnLockCRT2(SiS_Pr);
	   if(!(SiS_Pr->SiS_VBType & VB_SISLVDS)) {
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);
	   }
	   if( (!(SiS_CRT2IsLCD(SiS_Pr))) ||
	       (!(SiS_CR36BIOSWord23d(SiS_Pr))) ) {
	      SiS_PanelDelay(SiS_Pr, 2);
	      if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
	      } else {
		 SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x04);
	      }
	   }

#endif  /* CONFIG_FB_SIS_300 */

        } else {

#ifdef CONFIG_FB_SIS_315	   /* 315 series */

	   int didpwd = 0;
	   bool custom1 = (SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) ||
	                  (SiS_Pr->SiS_CustomT == CUT_CLEVO1400);

	   modenum = SiS_GetReg(SiS_Pr->SiS_P3d4,0x34) & 0x7f;

	   if(SiS_Pr->SiS_VBType & VB_SISLVDS) {

#ifdef SET_EMI
	      if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		 if(SiS_Pr->SiS_CustomT != CUT_CLEVO1400) {
		    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
		 }
	      }
#endif

	      didpwd = SiS_HandlePWD(SiS_Pr);

	      if( (modenum <= 0x13)           ||
		  (SiS_IsVAMode(SiS_Pr))      ||
		  (!(SiS_IsDualEdge(SiS_Pr))) ) {
		 if(!didpwd) {
		    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xfe);
		    if(custom1) SiS_PanelDelay(SiS_Pr, 3);
		 } else {
		    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xfc);
		 }
	      }

	      if(!custom1) {
		 SiS_DDC2Delay(SiS_Pr,0xff00);
		 SiS_DDC2Delay(SiS_Pr,0xe000);
		 SiS_SetRegByte(SiS_Pr->SiS_P3c6,0x00);
		 pushax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x06);
		 if(IS_SIS740) {
		    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
		 }
	         SiS_PanelDelay(SiS_Pr, 3);
	      }

	   }

	   if(!(SiS_IsNotM650orLater(SiS_Pr))) {
	      /* if(SiS_Pr->ChipType < SIS_340) {*/
		 tempah = 0xef;
		 if(SiS_IsVAMode(SiS_Pr)) tempah = 0xf7;
		 SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	      /*}*/
	   }

	   if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,~0x10);
	   }

	   tempah = 0x3f;
	   if(SiS_IsDualEdge(SiS_Pr)) {
	      tempah = 0x7f;
	      if(!(SiS_IsVAMode(SiS_Pr))) tempah = 0xbf;
	   }
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	   if((SiS_IsVAMode(SiS_Pr)) ||
	      ((SiS_Pr->SiS_VBType & VB_SISLVDS) && (modenum <= 0x13))) {

	      SiS_DisplayOff(SiS_Pr);
	      if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
		 SiS_PanelDelay(SiS_Pr, 2);
	      }
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1E,0xDF);

	   }

	   if((!(SiS_IsVAMode(SiS_Pr))) ||
	      ((SiS_Pr->SiS_VBType & VB_SISLVDS) && (modenum <= 0x13))) {

	      if(!(SiS_IsDualEdge(SiS_Pr))) {
		 SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xdf);
		 SiS_DisplayOff(SiS_Pr);
	      }
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);

	      if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
		 SiS_PanelDelay(SiS_Pr, 2);
	      }

	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);
	      temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
	      SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);

	   }

	   if(SiS_IsNotM650orLater(SiS_Pr)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
	   }

	   if(SiS_Pr->SiS_VBType & VB_SISLVDS) {

	      if( (!(SiS_IsVAMode(SiS_Pr)))  &&
		  (!(SiS_CRT2IsLCD(SiS_Pr))) &&
		  (!(SiS_IsDualEdge(SiS_Pr))) ) {

		 if(custom1) SiS_PanelDelay(SiS_Pr, 2);
		 if(!didpwd) {
		    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFD);
		 }
		 if(custom1) SiS_PanelDelay(SiS_Pr, 4);
	      }

	      if(!custom1) {
		 SiS_SetReg(SiS_Pr->SiS_P3c4,0x06,pushax);
		 if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		    if(SiS_IsVAorLCD(SiS_Pr)) {
		       SiS_PanelDelayLoop(SiS_Pr, 3, 20);
		    }
		 }
	      }

	   }

#endif /* CONFIG_FB_SIS_315 */

	}

     } else {     /* ============ For 301 ================ */

        if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
	   if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
	      SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x08);
	      SiS_PanelDelay(SiS_Pr, 3);
	   }
#endif
	}

	SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xDF);           /* disable VB */
	SiS_DisplayOff(SiS_Pr);

	if(SiS_Pr->ChipType >= SIS_315H) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);                /* disable lock mode */

	if(SiS_Pr->ChipType >= SIS_315H) {
	    temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
	    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x10);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,temp);
	} else {
#ifdef CONFIG_FB_SIS_300
	    SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);            /* disable CRT2 */
	    if( (!(SiS_CRT2IsLCD(SiS_Pr))) ||
		(!(SiS_CR36BIOSWord23d(SiS_Pr))) ) {
		SiS_PanelDelay(SiS_Pr, 2);
		SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x04);
	    }
#endif
	}

      }

  } else {     /* ============ For LVDS =============*/

    if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300	/* 300 series */

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	   SiS_SetCH700x(SiS_Pr,0x0E,0x09);
	}

	if(SiS_Pr->ChipType == SIS_730) {
	   if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {
	      SiS_WaitVBRetrace(SiS_Pr);
	   }
	   if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
	      SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x08);
	      SiS_PanelDelay(SiS_Pr, 3);
	   }
	} else {
	   if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x08)) {
	      if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
		 if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
		    SiS_WaitVBRetrace(SiS_Pr);
		    if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x06) & 0x1c)) {
		       SiS_DisplayOff(SiS_Pr);
		    }
		    SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x08);
		    SiS_PanelDelay(SiS_Pr, 3);
		 }
	      }
	   }
	}

	SiS_DisplayOff(SiS_Pr);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	SiS_UnLockCRT2(SiS_Pr);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80);
	SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);

	if( (!(SiS_CRT2IsLCD(SiS_Pr))) ||
	    (!(SiS_CR36BIOSWord23d(SiS_Pr))) ) {
	   SiS_PanelDelay(SiS_Pr, 2);
	   SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x04);
	}

#endif  /* CONFIG_FB_SIS_300 */

    } else {

#ifdef CONFIG_FB_SIS_315	/* 315 series */

	if(!(SiS_IsNotM650orLater(SiS_Pr))) {
	   /*if(SiS_Pr->ChipType < SIS_340) { */ /* XGI needs this */
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x4c,~0x18);
	   /* } */
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {

	   if(SiS_Pr->ChipType == SIS_740) {
	      temp = SiS_GetCH701x(SiS_Pr,0x61);
	      if(temp < 1) {
		 SiS_SetCH701x(SiS_Pr,0x76,0xac);
		 SiS_SetCH701x(SiS_Pr,0x66,0x00);
	      }

	      if( (!(SiS_IsDualEdge(SiS_Pr))) ||
		  (SiS_IsTVOrYPbPrOrScart(SiS_Pr)) ) {
		 SiS_SetCH701x(SiS_Pr,0x49,0x3e);
	      }
	   }

	   if( (!(SiS_IsDualEdge(SiS_Pr))) ||
	       (SiS_IsVAMode(SiS_Pr)) ) {
	      SiS_Chrontel701xBLOff(SiS_Pr);
	      SiS_Chrontel701xOff(SiS_Pr);
	   }

	   if(SiS_Pr->ChipType != SIS_740) {
	      if( (!(SiS_IsDualEdge(SiS_Pr))) ||
		  (SiS_IsTVOrYPbPrOrScart(SiS_Pr)) ) {
		 SiS_SetCH701x(SiS_Pr,0x49,0x01);
	      }
	   }

	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x08);
	   SiS_PanelDelay(SiS_Pr, 3);
	}

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr))) ||
	    (!(SiS_IsTVOrYPbPrOrScart(SiS_Pr))) ) {
	   SiS_DisplayOff(SiS_Pr);
	}

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr))) ||
	    (!(SiS_IsVAMode(SiS_Pr))) ) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x00,0x80);
	}

	if(SiS_Pr->ChipType == SIS_740) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
	}

	SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x32,0xDF);

	if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
	    (!(SiS_IsDualEdge(SiS_Pr))) ||
	    (!(SiS_IsVAMode(SiS_Pr))) ) {
	   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1E,0xDF);
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   if(SiS_CRT2IsLCD(SiS_Pr)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	      if(SiS_Pr->ChipType == SIS_550) {
		 SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xbf);
		 SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xef);
	      }
	   }
	} else {
	   if(SiS_Pr->ChipType == SIS_740) {
	      if(SiS_IsLCDOrLCDA(SiS_Pr)) {
		 SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	      }
	   } else if(SiS_IsVAMode(SiS_Pr)) {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x1e,0xdf);
	   }
	}

	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	   if(SiS_IsDualEdge(SiS_Pr)) {
	      /* SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xff); */
	   } else {
	      SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	   }
	}

	SiS_UnLockCRT2(SiS_Pr);

	if(SiS_Pr->ChipType == SIS_550) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x80); /* DirectDVD PAL?*/
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40); /* VB clock / 4 ? */
	} else if( (SiS_Pr->SiS_IF_DEF_CH70xx == 0)   ||
		   (!(SiS_IsDualEdge(SiS_Pr))) ||
		   (!(SiS_IsVAMode(SiS_Pr))) ) {
	   SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);
	}

        if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	   if(SiS_CRT2IsLCD(SiS_Pr)) {
	      if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
		 SiS_PanelDelay(SiS_Pr, 2);
		 SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x04);
	      }
	   }
        }

#endif  /* CONFIG_FB_SIS_315 */

    }  /* 315 series */

  }  /* LVDS */

}

/*********************************************/
/*            ENABLE VIDEO BRIDGE            */
/*********************************************/

/* NEVER use any variables (VBInfo), this will be called
 * from outside the context of a mode switch!
 * MUST call getVBType before calling this
 */
static
void
SiS_EnableBridge(struct SiS_Private *SiS_Pr)
{
  unsigned short temp=0, tempah;
#ifdef CONFIG_FB_SIS_315
  unsigned short temp1, pushax=0;
  bool delaylong = false;
#endif

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

    if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {		/* ====== For 301B et al  ====== */

      if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300     /* 300 series */

	 if(SiS_CRT2IsLCD(SiS_Pr)) {
	    if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
	    } else if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	       SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x00);
	    }
	    if(SiS_Pr->SiS_VBType & (VB_SISLVDS | VB_NoLCD)) {
	       if(!(SiS_CR36BIOSWord23d(SiS_Pr))) {
		  SiS_PanelDelay(SiS_Pr, 0);
	       }
	    }
	 }

	 if((SiS_Pr->SiS_VBType & VB_NoLCD) &&
	    (SiS_CRT2IsLCD(SiS_Pr))) {

	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);   		/* Enable CRT2 */
	    SiS_DisplayOn(SiS_Pr);
	    SiS_UnLockCRT2(SiS_Pr);
	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
	    if(SiS_BridgeInSlavemode(SiS_Pr)) {
	       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
	    } else {
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
	    }
	    if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
	       if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		  if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
		     SiS_PanelDelay(SiS_Pr, 1);
		  }
		  SiS_WaitVBRetrace(SiS_Pr);
		  SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x00);
	       }
	    }

	 } else {

	    temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;             /* lock mode */
	    if(SiS_BridgeInSlavemode(SiS_Pr)) {
	       tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
	       if(!(tempah & SetCRT2ToRAMDAC)) temp |= 0x20;
	    }
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);        /* enable VB processor */
	    SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,0xC0);
	    SiS_DisplayOn(SiS_Pr);
	    if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	       if(SiS_CRT2IsLCD(SiS_Pr)) {
		  if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		     if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
		        SiS_PanelDelay(SiS_Pr, 1);
		     }
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
		  }
	       }
	    }

	 }


#endif /* CONFIG_FB_SIS_300 */

      } else {

#ifdef CONFIG_FB_SIS_315    /* 315 series */

#ifdef SET_EMI
	 unsigned char   r30=0, r31=0, r32=0, r33=0, cr36=0;
	 int didpwd = 0;
	 /* unsigned short  emidelay=0; */
#endif

	 if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x1f,0xef);
#ifdef SET_EMI
	    if(SiS_Pr->SiS_VBType & VB_SISEMI) {
	       SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
	    }
#endif
	 }

	 if(!(SiS_IsNotM650orLater(SiS_Pr))) {
	    /*if(SiS_Pr->ChipType < SIS_340) { */
	       tempah = 0x10;
	       if(SiS_LCDAEnabled(SiS_Pr)) {
		  if(SiS_TVEnabled(SiS_Pr)) tempah = 0x18;
		  else			    tempah = 0x08;
	       }
	       SiS_SetReg(SiS_Pr->SiS_Part1Port,0x4c,tempah);
	    /*}*/
	 }

	 if(SiS_Pr->SiS_VBType & VB_SISLVDS) {

	    SiS_SetRegByte(SiS_Pr->SiS_P3c6,0x00);
	    SiS_DisplayOff(SiS_Pr);
	    pushax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x06);
	    if(IS_SIS740) {
	       SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x06,0xE3);
	    }

	    didpwd = SiS_HandlePWD(SiS_Pr);

	    if(SiS_IsVAorLCD(SiS_Pr)) {
	       if(!didpwd) {
		  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
		     SiS_PanelDelayLoop(SiS_Pr, 3, 2);
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
		     SiS_PanelDelayLoop(SiS_Pr, 3, 2);
		     if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		        SiS_GenericDelay(SiS_Pr, 17664);
		     }
		  }
	       } else {
		  SiS_PanelDelayLoop(SiS_Pr, 3, 2);
		  if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		     SiS_GenericDelay(SiS_Pr, 17664);
		  }
	       }
	    }

	    if(!(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40)) {
	       SiS_PanelDelayLoop(SiS_Pr, 3, 10);
	       delaylong = true;
	    }

	 }

	 if(!(SiS_IsVAMode(SiS_Pr))) {

	    temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;
	    if(SiS_BridgeInSlavemode(SiS_Pr)) {
	       tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
	       if(!(tempah & SetCRT2ToRAMDAC)) {
		  if(!(SiS_LCDAEnabled(SiS_Pr))) temp |= 0x20;
	       }
	    }
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);

	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                   /* enable CRT2 */

	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
	    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);

	    if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	       SiS_PanelDelay(SiS_Pr, 2);
	    }

	 } else {

	    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x20);

	 }

	 SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1f,0x20);
	 SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);

	 if(SiS_Pr->SiS_VBType & VB_SISPOWER) {
	    if( (SiS_LCDAEnabled(SiS_Pr)) ||
	        (SiS_CRT2IsLCD(SiS_Pr)) ) {
	       /* Enable "LVDS PLL power on" (even on 301C) */
	       SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x2a,0x7f);
	       /* Enable "LVDS Driver Power on" (even on 301C) */
	       SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x7f);
	    }
	 }

	 tempah = 0xc0;
	 if(SiS_IsDualEdge(SiS_Pr)) {
	    tempah = 0x80;
	    if(!(SiS_IsVAMode(SiS_Pr))) tempah = 0x40;
	 }
	 SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1F,tempah);

	 if(SiS_Pr->SiS_VBType & VB_SISLVDS) {

	    SiS_PanelDelay(SiS_Pr, 2);

	    SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x1f,0x10);
	    SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2e,0x80);

	    if(SiS_Pr->SiS_CustomT != CUT_CLEVO1400) {
#ifdef SET_EMI
	       if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		  SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
		  SiS_GenericDelay(SiS_Pr, 2048);
	       }
#endif
	       SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x0c);

	       if(SiS_Pr->SiS_VBType & VB_SISEMI) {
#ifdef SET_EMI
		  cr36 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36);

		  if(SiS_Pr->SiS_ROMNew) {
		     unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
		     unsigned short romptr = GetLCDStructPtr661_2(SiS_Pr);
		     if(romptr) {
			SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x20); /* Reset */
			SiS_Pr->EMI_30 = 0;
			SiS_Pr->EMI_31 = ROMAddr[romptr + SiS_Pr->SiS_EMIOffset + 0];
			SiS_Pr->EMI_32 = ROMAddr[romptr + SiS_Pr->SiS_EMIOffset + 1];
			SiS_Pr->EMI_33 = ROMAddr[romptr + SiS_Pr->SiS_EMIOffset + 2];
			if(ROMAddr[romptr + 1] & 0x10) SiS_Pr->EMI_30 = 0x40;
			/* emidelay = SISGETROMW((romptr + 0x22)); */
			SiS_Pr->HaveEMI = SiS_Pr->HaveEMILCD = SiS_Pr->OverruleEMI = true;
		     }
		  }

		  /*                                              (P4_30|0x40)  */
		  /* Compal 1400x1050: 0x05, 0x60, 0x00                YES  (1.10.7w;  CR36=69)      */
		  /* Compal 1400x1050: 0x0d, 0x70, 0x40                YES  (1.10.7x;  CR36=69)      */
		  /* Acer   1280x1024: 0x12, 0xd0, 0x6b                NO   (1.10.9k;  CR36=73)      */
		  /* Compaq 1280x1024: 0x0d, 0x70, 0x6b                YES  (1.12.04b; CR36=03)      */
		  /* Clevo   1024x768: 0x05, 0x60, 0x33                NO   (1.10.8e;  CR36=12, DL!) */
		  /* Clevo   1024x768: 0x0d, 0x70, 0x40 (if type == 3) YES  (1.10.8y;  CR36=?2)      */
		  /* Clevo   1024x768: 0x05, 0x60, 0x33 (if type != 3) YES  (1.10.8y;  CR36=?2)      */
		  /* Asus    1024x768: ?                                ?   (1.10.8o;  CR36=?2)      */
		  /* Asus    1024x768: 0x08, 0x10, 0x3c (problematic)  YES  (1.10.8q;  CR36=22)      */

		  if(SiS_Pr->HaveEMI) {
		     r30 = SiS_Pr->EMI_30; r31 = SiS_Pr->EMI_31;
		     r32 = SiS_Pr->EMI_32; r33 = SiS_Pr->EMI_33;
		  } else {
		     r30 = 0;
		  }

		  /* EMI_30 is read at driver start; however, the BIOS sets this
		   * (if it is used) only if the LCD is in use. In case we caught
		   * the machine while on TV output, this bit is not set and we
		   * don't know if it should be set - hence our detection is wrong.
		   * Work-around this here:
		   */

		  if((!SiS_Pr->HaveEMI) || (!SiS_Pr->HaveEMILCD)) {
		     switch((cr36 & 0x0f)) {
		     case 2:
			r30 |= 0x40;
			if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) r30 &= ~0x40;
			if(!SiS_Pr->HaveEMI) {
			   r31 = 0x05; r32 = 0x60; r33 = 0x33;
			   if((cr36 & 0xf0) == 0x30) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x40;
			   }
			}
			break;
		     case 3:  /* 1280x1024 */
			if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) r30 |= 0x40;
			if(!SiS_Pr->HaveEMI) {
			   r31 = 0x12; r32 = 0xd0; r33 = 0x6b;
			   if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x6b;
			   }
			}
			break;
		     case 9:  /* 1400x1050 */
			r30 |= 0x40;
			if(!SiS_Pr->HaveEMI) {
			   r31 = 0x05; r32 = 0x60; r33 = 0x00;
			   if(SiS_Pr->SiS_CustomT == CUT_COMPAL1400_2) {
			      r31 = 0x0d; r32 = 0x70; r33 = 0x40;  /* BIOS values */
			   }
			}
			break;
		     case 11: /* 1600x1200 - unknown */
			r30 |= 0x40;
			if(!SiS_Pr->HaveEMI) {
			   r31 = 0x05; r32 = 0x60; r33 = 0x00;
			}
		     }
                  }

		  /* BIOS values don't work so well sometimes */
		  if(!SiS_Pr->OverruleEMI) {
#ifdef COMPAL_HACK
		     if(SiS_Pr->SiS_CustomT == CUT_COMPAL1400_2) {
			if((cr36 & 0x0f) == 0x09) {
			   r30 = 0x60; r31 = 0x05; r32 = 0x60; r33 = 0x00;
			}
 		     }
#endif
#ifdef COMPAQ_HACK
		     if(SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) {
			if((cr36 & 0x0f) == 0x03) {
			   r30 = 0x20; r31 = 0x12; r32 = 0xd0; r33 = 0x6b;
			}
		     }
#endif
#ifdef ASUS_HACK
		     if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
			if((cr36 & 0x0f) == 0x02) {
			   /* r30 = 0x60; r31 = 0x05; r32 = 0x60; r33 = 0x33;  */   /* rev 2 */
			   /* r30 = 0x20; r31 = 0x05; r32 = 0x60; r33 = 0x33;  */   /* rev 3 */
			   /* r30 = 0x60; r31 = 0x0d; r32 = 0x70; r33 = 0x40;  */   /* rev 4 */
			   /* r30 = 0x20; r31 = 0x0d; r32 = 0x70; r33 = 0x40;  */   /* rev 5 */
			}
		     }
#endif
		  }

		  if(!(SiS_Pr->OverruleEMI && (!r30) && (!r31) && (!r32) && (!r33))) {
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x20); /* Reset */
		     SiS_GenericDelay(SiS_Pr, 2048);
		  }
		  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x31,r31);
		  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x32,r32);
		  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x33,r33);
#endif	/* SET_EMI */

		  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);

#ifdef SET_EMI
		  if( (SiS_LCDAEnabled(SiS_Pr)) ||
		      (SiS_CRT2IsLCD(SiS_Pr)) ) {
		     if(r30 & 0x40) {
			/*SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x2a,0x80);*/
			SiS_PanelDelayLoop(SiS_Pr, 3, 5);
			if(delaylong) {
			   SiS_PanelDelayLoop(SiS_Pr, 3, 5);
			   delaylong = false;
			}
			SiS_WaitVBRetrace(SiS_Pr);
			SiS_WaitVBRetrace(SiS_Pr);
			if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
			   SiS_GenericDelay(SiS_Pr, 1280);
			}
			SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x30,0x40);   /* Enable */
			/*SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x2a,0x7f);*/
		     }
		  }
#endif
	       }
	    }

	    if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
	       if(SiS_IsVAorLCD(SiS_Pr)) {
		  SiS_PanelDelayLoop(SiS_Pr, 3, 10);
		  if(delaylong) {
		     SiS_PanelDelayLoop(SiS_Pr, 3, 10);
		  }
		  SiS_WaitVBRetrace(SiS_Pr);
		  if(SiS_Pr->SiS_VBType & VB_SISEMI) {
		     SiS_GenericDelay(SiS_Pr, 2048);
		     SiS_WaitVBRetrace(SiS_Pr);
		  }
		  if(!didpwd) {
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
		  } else {
		     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x03);
		  }
	       }
	    }

	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x06,pushax);
	    SiS_DisplayOn(SiS_Pr);
	    SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xff);

	 }

	 if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
	 }

#endif /* CONFIG_FB_SIS_315 */

      }

    } else {	/* ============  For 301 ================ */

       if(SiS_Pr->ChipType < SIS_315H) {
	  if(SiS_CRT2IsLCD(SiS_Pr)) {
	     SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x00);
	     SiS_PanelDelay(SiS_Pr, 0);
	  }
       }

       temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x32) & 0xDF;          /* lock mode */
       if(SiS_BridgeInSlavemode(SiS_Pr)) {
	  tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
	  if(!(tempah & SetCRT2ToRAMDAC)) temp |= 0x20;
       }
       SiS_SetReg(SiS_Pr->SiS_P3c4,0x32,temp);

       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);                  /* enable CRT2 */

       if(SiS_Pr->ChipType >= SIS_315H) {
	  temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
	  if(!(temp & 0x80)) {
	     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);         /* BVBDOENABLE=1 */
	  }
       }

       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x00,0x1F,0x20);     /* enable VB processor */

       SiS_VBLongWait(SiS_Pr);
       SiS_DisplayOn(SiS_Pr);
       if(SiS_Pr->ChipType >= SIS_315H) {
	  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }
       SiS_VBLongWait(SiS_Pr);

       if(SiS_Pr->ChipType < SIS_315H) {
	  if(SiS_CRT2IsLCD(SiS_Pr)) {
	     SiS_PanelDelay(SiS_Pr, 1);
	     SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x00);
	  }
       }

    }

  } else {   /* =================== For LVDS ================== */

    if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300    /* 300 series */

       if(SiS_CRT2IsLCD(SiS_Pr)) {
	  if(SiS_Pr->ChipType == SIS_730) {
	     SiS_PanelDelay(SiS_Pr, 1);
	     SiS_PanelDelay(SiS_Pr, 1);
	     SiS_PanelDelay(SiS_Pr, 1);
	  }
	  SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x00);
	  if(!(SiS_CR36BIOSWord23d(SiS_Pr))) {
	     SiS_PanelDelay(SiS_Pr, 0);
	  }
       }

       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
       SiS_DisplayOn(SiS_Pr);
       SiS_UnLockCRT2(SiS_Pr);
       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xBF);
       if(SiS_BridgeInSlavemode(SiS_Pr)) {
	  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x01,0x1F);
       } else {
	  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0x1F,0x40);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
	  if(!(SiS_CRT2IsLCD(SiS_Pr))) {
	     SiS_WaitVBRetrace(SiS_Pr);
	     SiS_SetCH700x(SiS_Pr,0x0E,0x0B);
	  }
       }

       if(SiS_CRT2IsLCD(SiS_Pr)) {
	  if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x40)) {
	     if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x10)) {
		if(!(SiS_CR36BIOSWord23b(SiS_Pr))) {
		   SiS_PanelDelay(SiS_Pr, 1);
		   SiS_PanelDelay(SiS_Pr, 1);
		}
		SiS_WaitVBRetrace(SiS_Pr);
		SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x00);
	     }
	  }
       }

#endif  /* CONFIG_FB_SIS_300 */

    } else {

#ifdef CONFIG_FB_SIS_315    /* 315 series */

       if(!(SiS_IsNotM650orLater(SiS_Pr))) {
	  /*if(SiS_Pr->ChipType < SIS_340) {*/  /* XGI needs this */
	     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x4c,0x18);
	  /*}*/
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	  if(SiS_CRT2IsLCD(SiS_Pr)) {
	     SiS_SetRegSR11ANDOR(SiS_Pr,0xFB,0x00);
	     SiS_PanelDelay(SiS_Pr, 0);
	  }
       }

       SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
       SiS_UnLockCRT2(SiS_Pr);

       SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0xf7);

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	  temp = SiS_GetCH701x(SiS_Pr,0x66);
	  temp &= 0x20;
	  SiS_Chrontel701xBLOff(SiS_Pr);
       }

       if(SiS_Pr->ChipType != SIS_550) {
	  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2e,0x7f);
       }

       if(SiS_Pr->ChipType == SIS_740) {
	  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	     if(SiS_IsLCDOrLCDA(SiS_Pr)) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	     }
	  }
       }

       temp1 = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x2E);
       if(!(temp1 & 0x80)) {
	  SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2E,0x80);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	  if(temp) {
	     SiS_Chrontel701xBLOn(SiS_Pr);
	  }
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	  if(SiS_CRT2IsLCD(SiS_Pr)) {
	     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	     if(SiS_Pr->ChipType == SIS_550) {
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x40);
		SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x10);
	     }
	  }
       } else if(SiS_IsVAMode(SiS_Pr)) {
	  if(SiS_Pr->ChipType != SIS_740) {
	     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1E,0x20);
	  }
       }

       if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
	  SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x00,0x7f);
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	  if(SiS_IsTVOrYPbPrOrScart(SiS_Pr)) {
	     SiS_Chrontel701xOn(SiS_Pr);
	  }
	  if( (SiS_IsVAMode(SiS_Pr)) ||
	      (SiS_IsLCDOrLCDA(SiS_Pr)) ) {
	     SiS_ChrontelDoSomething1(SiS_Pr);
	  }
       }

       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
	  if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
	     if( (SiS_IsVAMode(SiS_Pr)) ||
		 (SiS_IsLCDOrLCDA(SiS_Pr)) ) {
		SiS_Chrontel701xBLOn(SiS_Pr);
		SiS_ChrontelInitTVVSync(SiS_Pr);
	     }
	  }
       } else if(SiS_Pr->SiS_IF_DEF_CH70xx == 0) {
	  if(!(SiS_WeHaveBacklightCtrl(SiS_Pr))) {
	     if(SiS_CRT2IsLCD(SiS_Pr)) {
		SiS_PanelDelay(SiS_Pr, 1);
		SiS_SetRegSR11ANDOR(SiS_Pr,0xF7,0x00);
	     }
	  }
       }

#endif  /* CONFIG_FB_SIS_315 */

    } /* 310 series */

  }  /* LVDS */

}

/*********************************************/
/*         SET PART 1 REGISTER GROUP         */
/*********************************************/

/* Set CRT2 OFFSET / PITCH */
static void
SiS_SetCRT2Offset(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RRTI)
{
   unsigned short offset;
   unsigned char  temp;

   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) return;

   offset = SiS_GetOffset(SiS_Pr,ModeNo,ModeIdIndex,RRTI);

   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x07,(offset & 0xFF));
   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x09,(offset >> 8));

   temp = (unsigned char)(((offset >> 3) & 0xFF) + 1);
   if(offset & 0x07) temp++;
   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,temp);
}

/* Set CRT2 sync and PanelLink mode */
static void
SiS_SetCRT2Sync(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short RefreshRateTableIndex)
{
   unsigned short tempah=0, tempbl, infoflag;

   tempbl = 0xC0;

   if(SiS_Pr->UseCustomMode) {
      infoflag = SiS_Pr->CInfoFlag;
   } else {
      infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
   }

   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {					/* LVDS */

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	 tempah = 0;
      } else if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (SiS_Pr->SiS_LCDInfo & LCDSync)) {
	 tempah = SiS_Pr->SiS_LCDInfo;
      } else tempah = infoflag >> 8;
      tempah &= 0xC0;
      tempah |= 0x20;
      if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	 if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
	    (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
	    tempah |= 0xf0;
	 }
	 if( (SiS_Pr->SiS_IF_DEF_FSTN) ||
	     (SiS_Pr->SiS_IF_DEF_DSTN) ||
	     (SiS_Pr->SiS_IF_DEF_TRUMPION) ||
	     (SiS_Pr->SiS_CustomT == CUT_PANEL848) ||
	     (SiS_Pr->SiS_CustomT == CUT_PANEL856) ) {
	    tempah |= 0x30;
	 }
	 if( (SiS_Pr->SiS_IF_DEF_FSTN) ||
	     (SiS_Pr->SiS_IF_DEF_DSTN) ) {
	    tempah &= ~0xc0;
	 }
      }
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	 if(SiS_Pr->ChipType >= SIS_315H) {
	    tempah >>= 3;
	    tempah &= 0x18;
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xE7,tempah);
	    /* Don't care about 12/18/24 bit mode - TV is via VGA, not PL */
	 } else {
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,0xe0);
	 }
      } else {
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
      }

   } else if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300  /* ---- 300 series --- */

	 if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {			/* 630 - 301B(-DH) */

	    tempah = infoflag >> 8;
	    tempbl = 0;
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	       if(SiS_Pr->SiS_LCDInfo & LCDSync) {
		  tempah = SiS_Pr->SiS_LCDInfo;
		  tempbl = (tempah >> 6) & 0x03;
	       }
	    }
	    tempah &= 0xC0;
	    tempah |= 0x20;
	    if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
	    tempah |= 0xc0;
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
	    if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (!(SiS_Pr->SiS_VBType & VB_NoLCD))) {
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xf0,tempbl);
	    }

	 } else {							/* 630 - 301 */

	    tempah = ((infoflag >> 8) & 0xc0) | 0x20;
	    if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);

	 }

#endif /* CONFIG_FB_SIS_300 */

      } else {

#ifdef CONFIG_FB_SIS_315  /* ------- 315 series ------ */

	 if(SiS_Pr->SiS_VBType & VB_SISLVDS) {	  		/* 315 - LVDS */

	    tempbl = 0;
	    if((SiS_Pr->SiS_CustomT == CUT_COMPAQ1280) &&
	       (SiS_Pr->SiS_LCDResInfo == Panel_1280x1024)) {
	       tempah = infoflag >> 8;
	       if(SiS_Pr->SiS_LCDInfo & LCDSync) {
		 tempbl = ((SiS_Pr->SiS_LCDInfo & 0xc0) >> 6);
	       }
	    } else if((SiS_Pr->SiS_CustomT == CUT_CLEVO1400)  &&
		      (SiS_Pr->SiS_LCDResInfo == Panel_1400x1050)) {
	       tempah = infoflag >> 8;
	       tempbl = 0x03;
	    } else {
	       tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
	       tempbl = (tempah >> 6) & 0x03;
	       tempbl |= 0x08;
	       if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempbl |= 0x04;
	    }
	    tempah &= 0xC0;
	    tempah |= 0x20;
	    if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)   tempah |= 0xc0;
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
	    if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	       if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xf0,tempbl);
	       }
	    }

	 } else {							/* 315 - TMDS */

	    tempah = tempbl = infoflag >> 8;
	    if(!SiS_Pr->UseCustomMode) {
	       tempbl = 0;
	       if((SiS_Pr->SiS_VBType & VB_SIS30xC) && (SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
		  if(ModeNo <= 0x13) {
		     tempah = SiS_GetRegByte((SiS_Pr->SiS_P3ca+0x02));
		  }
	       }
	       if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
		  if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
		    if(SiS_Pr->SiS_LCDInfo & LCDSync) {
		       tempah = SiS_Pr->SiS_LCDInfo;
		       tempbl = (tempah >> 6) & 0x03;
		    }
		  }
	       }
	    }
	    tempah &= 0xC0;
	    tempah |= 0x20;
	    if(!(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit)) tempah |= 0x10;
	    if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	       /* Imitate BIOS bug */
	       if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)  tempah |= 0xc0;
	    }
	    if((SiS_Pr->SiS_VBType & VB_SIS30xC) && (SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC)) {
	       tempah >>= 3;
	       tempah &= 0x18;
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xe7,tempah);
	    } else {
	       SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0F,tempah);
	       if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
		  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
		     SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xf0,tempbl);
		  }
	       }
	    }

         }
#endif  /* CONFIG_FB_SIS_315 */
      }
   }
}

/* Set CRT2 FIFO on 300/540/630/730 */
#ifdef CONFIG_FB_SIS_300
static void
SiS_SetCRT2FIFO_300(struct SiS_Private *SiS_Pr,unsigned short ModeNo)
{
  unsigned char  *ROMAddr  = SiS_Pr->VirtualRomBase;
  unsigned short temp, index, modeidindex, refreshratetableindex;
  unsigned short VCLK = 0, MCLK, colorth = 0, data2 = 0;
  unsigned short tempbx, tempcl, CRT1ModeNo, CRT2ModeNo, SelectRate_backup;
  unsigned int   data, pci50, pciA0;
  static const unsigned char colortharray[] = {
  	1, 1, 2, 2, 3, 4
  };

  SelectRate_backup = SiS_Pr->SiS_SelectCRT2Rate;

  if(!SiS_Pr->CRT1UsesCustomMode) {

     CRT1ModeNo = SiS_Pr->SiS_CRT1Mode;                                 /* get CRT1 ModeNo */
     SiS_SearchModeID(SiS_Pr, &CRT1ModeNo, &modeidindex);
     SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
     SiS_Pr->SiS_SelectCRT2Rate = 0;
     refreshratetableindex = SiS_GetRatePtr(SiS_Pr, CRT1ModeNo, modeidindex);

     if(CRT1ModeNo >= 0x13) {
        /* Get VCLK */
	index = SiS_GetRefCRTVCLK(SiS_Pr, refreshratetableindex, SiS_Pr->SiS_UseWide);
	VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;

	/* Get colordepth */
	colorth = SiS_GetColorDepth(SiS_Pr,CRT1ModeNo,modeidindex) >> 1;
	if(!colorth) colorth++;
     }

  } else {

     CRT1ModeNo = 0xfe;

     /* Get VCLK */
     VCLK = SiS_Pr->CSRClock_CRT1;

     /* Get color depth */
     colorth = colortharray[((SiS_Pr->CModeFlag_CRT1 & ModeTypeMask) - 2)];

  }

  if(CRT1ModeNo >= 0x13) {
     /* Get MCLK */
     if(SiS_Pr->ChipType == SIS_300) {
        index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3A);
     } else {
        index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1A);
     }
     index &= 0x07;
     MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;

     temp = ((SiS_GetReg(SiS_Pr->SiS_P3c4,0x14) >> 6) & 0x03) << 1;
     if(!temp) temp++;
     temp <<= 2;

     data2 = temp - ((colorth * VCLK) / MCLK);

     temp = (28 * 16) % data2;
     data2 = (28 * 16) / data2;
     if(temp) data2++;

     if(SiS_Pr->ChipType == SIS_300) {

	SiS_GetFIFOThresholdIndex300(SiS_Pr, &tempbx, &tempcl);
	data = SiS_GetFIFOThresholdB300(tempbx, tempcl);

     } else {

	pci50 = sisfb_read_nbridge_pci_dword(SiS_Pr, 0x50);
	pciA0 = sisfb_read_nbridge_pci_dword(SiS_Pr, 0xa0);

        if(SiS_Pr->ChipType == SIS_730) {

	   index = (unsigned short)(((pciA0 >> 28) & 0x0f) * 3);
	   index += (unsigned short)(((pci50 >> 9)) & 0x03);

	   /* BIOS BUG (2.04.5d, 2.04.6a use ah here, which is unset!) */
	   index = 0;  /* -- do it like the BIOS anyway... */

	} else {

	   pci50 >>= 24;
	   pciA0 >>= 24;

	   index = (pci50 >> 1) & 0x07;

	   if(pci50 & 0x01)    index += 6;
	   if(!(pciA0 & 0x01)) index += 24;

	   if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x14) & 0x80) index += 12;

	}

	data = SiS_GetLatencyFactor630(SiS_Pr, index) + 15;
	if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x14) & 0x80)) data += 5;

     }

     data += data2;						/* CRT1 Request Period */

     SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
     SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;

     if(!SiS_Pr->UseCustomMode) {

	CRT2ModeNo = ModeNo;
	SiS_SearchModeID(SiS_Pr, &CRT2ModeNo, &modeidindex);

	refreshratetableindex = SiS_GetRatePtr(SiS_Pr, CRT2ModeNo, modeidindex);

	/* Get VCLK  */
	index = SiS_GetVCLK2Ptr(SiS_Pr, CRT2ModeNo, modeidindex, refreshratetableindex);
	VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;

	if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) || (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
	   if(SiS_Pr->SiS_UseROM) {
	      if(ROMAddr[0x220] & 0x01) {
		 VCLK = ROMAddr[0x229] | (ROMAddr[0x22a] << 8);
	      }
           }
        }

     } else {

	/* Get VCLK */
	CRT2ModeNo = 0xfe;
	VCLK = SiS_Pr->CSRClock;

     }

     /* Get colordepth */
     colorth = SiS_GetColorDepth(SiS_Pr,CRT2ModeNo,modeidindex) >> 1;
     if(!colorth) colorth++;

     data = data * VCLK * colorth;
     temp = data % (MCLK << 4);
     data = data / (MCLK << 4);
     if(temp) data++;

     if(data < 6) data = 6;
     else if(data > 0x14) data = 0x14;

     if(SiS_Pr->ChipType == SIS_300) {
        temp = 0x16;
	if((data <= 0x0f) || (SiS_Pr->SiS_LCDResInfo == Panel_1280x1024))
	   temp = 0x13;
     } else {
        temp = 0x16;
	if(( (SiS_Pr->ChipType == SIS_630) ||
	     (SiS_Pr->ChipType == SIS_730) )  &&
	   (SiS_Pr->ChipRevision >= 0x30))
	   temp = 0x1b;
     }
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x01,0xe0,temp);

     if((SiS_Pr->ChipType == SIS_630) &&
	(SiS_Pr->ChipRevision >= 0x30)) {
	if(data > 0x13) data = 0x13;
     }
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,0xe0,data);

  } else {  /* If mode <= 0x13, we just restore everything */

     SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
     SiS_Pr->SiS_SelectCRT2Rate = SelectRate_backup;

  }
}
#endif

/* Set CRT2 FIFO on 315/330 series */
#ifdef CONFIG_FB_SIS_315
static void
SiS_SetCRT2FIFO_310(struct SiS_Private *SiS_Pr)
{
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,0x3B);
  if( (SiS_Pr->ChipType == SIS_760)      &&
      (SiS_Pr->SiS_SysFlags & SF_760LFB)  &&
      (SiS_Pr->SiS_ModeType == Mode32Bpp) &&
      (SiS_Pr->SiS_VGAHDE >= 1280)	  &&
      (SiS_Pr->SiS_VGAVDE >= 1024) ) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2f,0x03);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x01,0x3b);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x4d,0xc0);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2f,0x01);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x4d,0xc0);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,0x6e);
  } else {
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x02,~0x3f,0x04);
  }

}
#endif

static unsigned short
SiS_GetVGAHT2(struct SiS_Private *SiS_Pr)
{
  unsigned int tempax,tempbx;

  tempbx = (SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) * SiS_Pr->SiS_RVBHCMAX;
  tempax = (SiS_Pr->SiS_VT - SiS_Pr->SiS_VDE) * SiS_Pr->SiS_RVBHCFACT;
  tempax = (tempax * SiS_Pr->SiS_HT) / tempbx;
  return (unsigned short)tempax;
}

/* Set Part 1 / SiS bridge slave mode */
static void
SiS_SetGroup1_301(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex,
                  unsigned short RefreshRateTableIndex)
{
  unsigned short temp, modeflag, i, j, xres=0, VGAVDE;
  static const unsigned short CRTranslation[] = {
       /* CR0   CR1   CR2   CR3   CR4   CR5   CR6   CR7   */
	  0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
       /* CR8   CR9   SR0A  SR0B  SR0C  SR0D  SR0E  CR0F  */
	  0x00, 0x0b, 0x17, 0x18, 0x19, 0x00, 0x1a, 0x00,
       /* CR10  CR11  CR12  CR13  CR14  CR15  CR16  CR17  */
	  0x0c, 0x0d, 0x0e, 0x00, 0x0f, 0x10, 0x11, 0x00
  };

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
     xres = SiS_Pr->CHDisplay;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     xres = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].XRes;
  }

  /* The following is only done if bridge is in slave mode: */

  if(SiS_Pr->ChipType >= SIS_315H) {
     if(xres >= 1600) {  /* BIOS: == 1600 */
        SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x31,0x04);
     }
  }

  SiS_Pr->CHTotal = 8224;  /* Max HT, 0x2020, results in 0x3ff in registers */

  SiS_Pr->CHDisplay = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK) SiS_Pr->CHDisplay >>= 1;

  SiS_Pr->CHBlankStart = SiS_Pr->CHDisplay;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     SiS_Pr->CHBlankStart += 16;
  }

  SiS_Pr->CHBlankEnd = 32;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     if(xres == 1600) SiS_Pr->CHBlankEnd += 80;
  }

  temp = SiS_Pr->SiS_VGAHT - 96;
  if(!(modeflag & HalfDCLK)) temp -= 32;
  if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
     temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x04);
     temp |= ((SiS_GetReg(SiS_Pr->SiS_P3c4,0x0b) & 0xc0) << 2);
     temp -= 3;
     temp <<= 3;
  } else {
     if(SiS_Pr->SiS_RVBHRS2) temp = SiS_Pr->SiS_RVBHRS2;
  }
  SiS_Pr->CHSyncStart = temp;

  SiS_Pr->CHSyncEnd = 0xffe8; 	/* results in 0x2000 in registers */

  SiS_Pr->CVTotal = 2049;  	/* Max VT, 0x0801, results in 0x7ff in registers */

  VGAVDE = SiS_Pr->SiS_VGAVDE;
  if     (VGAVDE ==  357) VGAVDE =  350;
  else if(VGAVDE ==  360) VGAVDE =  350;
  else if(VGAVDE ==  375) VGAVDE =  350;
  else if(VGAVDE ==  405) VGAVDE =  400;
  else if(VGAVDE ==  420) VGAVDE =  400;
  else if(VGAVDE ==  525) VGAVDE =  480;
  else if(VGAVDE == 1056) VGAVDE = 1024;
  SiS_Pr->CVDisplay = VGAVDE;

  SiS_Pr->CVBlankStart = SiS_Pr->CVDisplay;

  SiS_Pr->CVBlankEnd = 1;
  if(ModeNo == 0x3c) SiS_Pr->CVBlankEnd = 226;

  temp = (SiS_Pr->SiS_VGAVT - VGAVDE) >> 1;
  SiS_Pr->CVSyncStart = VGAVDE + temp;

  temp >>= 3;
  SiS_Pr->CVSyncEnd = SiS_Pr->CVSyncStart + temp;

  SiS_CalcCRRegisters(SiS_Pr, 0);
  SiS_Pr->CCRT1CRTC[16] &= ~0xE0;

  for(i = 0; i <= 7; i++) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,CRTranslation[i],SiS_Pr->CCRT1CRTC[i]);
  }
  for(i = 0x10, j = 8; i <= 0x12; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,CRTranslation[i],SiS_Pr->CCRT1CRTC[j]);
  }
  for(i = 0x15, j = 11; i <= 0x16; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,CRTranslation[i],SiS_Pr->CCRT1CRTC[j]);
  }
  for(i = 0x0a, j = 13; i <= 0x0c; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,CRTranslation[i],SiS_Pr->CCRT1CRTC[j]);
  }

  temp = SiS_Pr->CCRT1CRTC[16] & 0xE0;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,CRTranslation[0x0E],0x1F,temp);

  temp = (SiS_Pr->CCRT1CRTC[16] & 0x01) << 5;
  if(modeflag & DoubleScanMode) temp |= 0x80;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,CRTranslation[0x09],0x5F,temp);

  temp = 0;
  temp |= (SiS_GetReg(SiS_Pr->SiS_P3c4,0x01) & 0x01);
  if(modeflag & HalfDCLK) temp |= 0x08;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,temp);              	/* SR01: HalfDCLK[3], 8/9 div dotclock[0] */

  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0F,0x00);              	/* CR14: (text mode: underline location) */
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x12,0x00);              	/* CR17: n/a */

  temp = 0;
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
     temp = (SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) << 7;
  }
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1A,temp);                	/* SR0E, dither[7] */

  temp = SiS_GetRegByte((SiS_Pr->SiS_P3ca+0x02));
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,temp);			/* ? */
}

/* Setup panel link
 * This is used for LVDS, LCDA and Chrontel TV output
 * 300/LVDS+TV, 300/301B-DH, 315/LVDS+TV, 315/LCDA
 */
static void
SiS_SetGroup1_LVDS(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned short modeflag, resinfo = 0;
  unsigned short push2, tempax, tempbx, tempcx, temp;
  unsigned int   tempeax = 0, tempebx, tempecx, tempvcfact = 0;
  bool islvds = false, issis  = false, chkdclkfirst = false;
#ifdef CONFIG_FB_SIS_300
  unsigned short crt2crtc = 0;
#endif
#ifdef CONFIG_FB_SIS_315
  unsigned short pushcx;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
#ifdef CONFIG_FB_SIS_300
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
#endif
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
#ifdef CONFIG_FB_SIS_300
     crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
#endif
  }

  /* is lvds if really LVDS, or 301B-DH with external LVDS transmitter */
  if((SiS_Pr->SiS_IF_DEF_LVDS == 1) || (SiS_Pr->SiS_VBType & VB_NoLCD)) {
     islvds = true;
  }

  /* is really sis if sis bridge, but not 301B-DH */
  if((SiS_Pr->SiS_VBType & VB_SISVB) && (!(SiS_Pr->SiS_VBType & VB_NoLCD))) {
     issis = true;
  }

  if((SiS_Pr->ChipType >= SIS_315H) && (islvds) && (!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA))) {
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) {
        chkdclkfirst = true;
     }
  }

#ifdef CONFIG_FB_SIS_315
  if((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
     if(IS_SIS330) {
        SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);
     } else if(IS_SIS740) {
        if(islvds) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x03);
        } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x10);
        }
     } else {
        if(islvds) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,0xfb,0x04);
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x00);
        } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
           SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x2D,0x0f);
	   if(SiS_Pr->SiS_VBType & VB_SIS30xC) {
	      if((SiS_Pr->SiS_LCDResInfo == Panel_1024x768) ||
	         (SiS_Pr->SiS_LCDResInfo == Panel_1280x1024)) {
	         SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2D,0x20);
	      }
	   }
        }
     }
  }
#endif

  /* Horizontal */

  tempax = SiS_Pr->SiS_LCDHDES;
  if(islvds) {
     if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	if(!SiS_Pr->SiS_IF_DEF_FSTN && !SiS_Pr->SiS_IF_DEF_DSTN) {
	   if((SiS_Pr->SiS_LCDResInfo == Panel_640x480) &&
	      (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))) {
	      tempax -= 8;
	   }
	}
     }
  }

  temp = (tempax & 0x0007);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1A,temp);			/* BPLHDESKEW[2:0]   */
  temp = (tempax >> 3) & 0x00FF;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,temp);			/* BPLHDESKEW[10:3]  */

  tempbx = SiS_Pr->SiS_HDE;
  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        tempbx = SiS_Pr->PanelXRes;
     }
     if((SiS_Pr->SiS_LCDResInfo == Panel_320x240_1) ||
        (SiS_Pr->SiS_LCDResInfo == Panel_320x240_2) ||
        (SiS_Pr->SiS_LCDResInfo == Panel_320x240_3)) {
        tempbx >>= 1;
     }
  }

  tempax += tempbx;
  if(tempax >= SiS_Pr->SiS_HT) tempax -= SiS_Pr->SiS_HT;

  temp = tempax;
  if(temp & 0x07) temp += 8;
  temp >>= 3;
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,temp);			/* BPLHDEE  */

  tempcx = (SiS_Pr->SiS_HT - tempbx) >> 2;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        if(SiS_Pr->PanelHRS != 999) tempcx = SiS_Pr->PanelHRS;
     }
  }

  tempcx += tempax;
  if(tempcx >= SiS_Pr->SiS_HT) tempcx -= SiS_Pr->SiS_HT;

  temp = (tempcx >> 3) & 0x00FF;
  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->SiS_IF_DEF_TRUMPION) {
	if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   switch(ModeNo) {
	   case 0x04:
	   case 0x05:
	   case 0x0d: temp = 0x56; break;
	   case 0x10: temp = 0x60; break;
	   case 0x13: temp = 0x5f; break;
	   case 0x40:
	   case 0x41:
	   case 0x4f:
	   case 0x43:
	   case 0x44:
	   case 0x62:
	   case 0x56:
	   case 0x53:
	   case 0x5d:
	   case 0x5e: temp = 0x54; break;
	   }
	}
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,temp);			/* BPLHRS */

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     temp += 2;
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
	temp += 8;
	if(SiS_Pr->PanelHRE != 999) {
	   temp = tempcx + SiS_Pr->PanelHRE;
	   if(temp >= SiS_Pr->SiS_HT) temp -= SiS_Pr->SiS_HT;
	   temp >>= 3;
	}
     }
  } else {
     temp += 10;
  }

  temp &= 0x1F;
  temp |= ((tempcx & 0x07) << 5);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,temp);			/* BPLHRE */

  /* Vertical */

  tempax = SiS_Pr->SiS_VGAVDE;
  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
	tempax = SiS_Pr->PanelYRes;
     }
  }

  tempbx = SiS_Pr->SiS_LCDVDES + tempax;
  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  push2 = tempbx;

  tempcx = SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE;
  if(SiS_Pr->ChipType < SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
	   tempcx = SiS_Pr->SiS_VGAVT - SiS_Pr->PanelYRes;
	}
     }
  }
  if(islvds) tempcx >>= 1;
  else       tempcx >>= 2;

  if( (SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) &&
      (!(SiS_Pr->SiS_LCDInfo & LCDPass11)) 		    &&
      (SiS_Pr->PanelVRS != 999) ) {
     tempcx = SiS_Pr->PanelVRS;
     tempbx += tempcx;
     if(issis) tempbx++;
  } else {
     tempbx += tempcx;
     if(SiS_Pr->ChipType < SIS_315H) tempbx++;
     else if(issis)                   tempbx++;
  }

  if(tempbx >= SiS_Pr->SiS_VT) tempbx -= SiS_Pr->SiS_VT;

  temp = tempbx & 0x00FF;
  if(SiS_Pr->SiS_IF_DEF_TRUMPION) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	if(ModeNo == 0x10) temp = 0xa9;
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,temp);			/* BPLVRS */

  tempcx >>= 3;
  tempcx++;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
        if(SiS_Pr->PanelVRE != 999) tempcx = SiS_Pr->PanelVRE;
     }
  }

  tempcx += tempbx;
  temp = tempcx & 0x000F;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0xF0,temp);	/* BPLVRE  */

  temp = ((tempbx >> 8) & 0x07) << 3;
  if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
     if(SiS_Pr->SiS_HDE != 640) {
        if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE)  temp |= 0x40;
     }
  } else if(SiS_Pr->SiS_VGAVDE != SiS_Pr->SiS_VDE) temp |= 0x40;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA)          temp |= 0x40;
  tempbx = 0x87;
  if((SiS_Pr->ChipType >= SIS_315H) ||
     (SiS_Pr->ChipRevision >= 0x30)) {
     tempbx = 0x07;
     if((SiS_Pr->SiS_IF_DEF_CH70xx == 1) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
	if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x03)    temp |= 0x80;
     }
     /* Chrontel 701x operates in 24bit mode (8-8-8, 2x12bit multiplexed) via VGA2 */
     if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	   if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x06) & 0x10)      temp |= 0x80;
	} else {
	   if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) temp |= 0x80;
	}
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1A,tempbx,temp);

  tempbx = push2;						/* BPLVDEE */

  tempcx = SiS_Pr->SiS_LCDVDES;					/* BPLVDES */

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     switch(SiS_Pr->SiS_LCDResInfo) {
     case Panel_640x480:
	tempbx = SiS_Pr->SiS_VGAVDE - 1;
	tempcx = SiS_Pr->SiS_VGAVDE;
	break;
     case Panel_800x600:
	if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	   if(resinfo == SIS_RI_800x600) tempcx++;
	}
	break;
     case Panel_1024x600:
	if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	   if(resinfo == SIS_RI_1024x600) tempcx++;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	      if(resinfo == SIS_RI_800x600) tempcx++;
	   }
	}
	break;
     case Panel_1024x768:
	if(SiS_Pr->ChipType < SIS_315H) {
	   if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	      if(resinfo == SIS_RI_1024x768) tempcx++;
	   }
	}
	break;
     }
  }

  temp = ((tempbx >> 8) & 0x07) << 3;
  temp |= ((tempcx >> 8) & 0x07);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1D,temp);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1C,tempbx);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1B,tempcx);

  /* Vertical scaling */

  if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300      /* 300 series */
     tempeax = SiS_Pr->SiS_VGAVDE << 6;
     temp = (tempeax % (unsigned int)SiS_Pr->SiS_VDE);
     tempeax = tempeax / (unsigned int)SiS_Pr->SiS_VDE;
     if(temp) tempeax++;

     if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) tempeax = 0x3F;

     temp = (unsigned short)(tempeax & 0x00FF);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1E,temp);      	/* BPLVCFACT */
     tempvcfact = temp;
#endif /* CONFIG_FB_SIS_300 */

  } else {

#ifdef CONFIG_FB_SIS_315  /* 315 series */
     tempeax = SiS_Pr->SiS_VGAVDE << 18;
     tempebx = SiS_Pr->SiS_VDE;
     temp = (tempeax % tempebx);
     tempeax = tempeax / tempebx;
     if(temp) tempeax++;
     tempvcfact = tempeax;

     temp = (unsigned short)(tempeax & 0x00FF);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x37,temp);
     temp = (unsigned short)((tempeax & 0x00FF00) >> 8);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x36,temp);
     temp = (unsigned short)((tempeax & 0x00030000) >> 16);
     if(SiS_Pr->SiS_VDE == SiS_Pr->SiS_VGAVDE) temp |= 0x04;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x35,temp);

     if(SiS_Pr->SiS_VBType & VB_SISPART4SCALER) {
        temp = (unsigned short)(tempeax & 0x00FF);
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x3c,temp);
        temp = (unsigned short)((tempeax & 0x00FF00) >> 8);
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x3b,temp);
        temp = (unsigned short)(((tempeax & 0x00030000) >> 16) << 6);
        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x3a,0x3f,temp);
        temp = 0;
        if(SiS_Pr->SiS_VDE != SiS_Pr->SiS_VGAVDE) temp |= 0x08;
        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x30,0xf3,temp);
     }
#endif

  }

  /* Horizontal scaling */

  tempeax = SiS_Pr->SiS_VGAHDE;		/* 1f = ( (VGAHDE * 65536) / ( (VGAHDE * 65536) / HDE ) ) - 1*/
  if(chkdclkfirst) {
     if(modeflag & HalfDCLK) tempeax >>= 1;
  }
  tempebx = tempeax << 16;
  if(SiS_Pr->SiS_HDE == tempeax) {
     tempecx = 0xFFFF;
  } else {
     tempecx = tempebx / SiS_Pr->SiS_HDE;
     if(SiS_Pr->ChipType >= SIS_315H) {
        if(tempebx % SiS_Pr->SiS_HDE) tempecx++;
     }
  }

  if(SiS_Pr->ChipType >= SIS_315H) {
     tempeax = (tempebx / tempecx) - 1;
  } else {
     tempeax = ((SiS_Pr->SiS_VGAHT << 16) / tempecx) - 1;
  }
  tempecx = (tempecx << 16) | (tempeax & 0xFFFF);
  temp = (unsigned short)(tempecx & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1F,temp);

  if(SiS_Pr->ChipType >= SIS_315H) {
     tempeax = (SiS_Pr->SiS_VGAVDE << 18) / tempvcfact;
     tempbx = (unsigned short)(tempeax & 0xFFFF);
  } else {
     tempeax = SiS_Pr->SiS_VGAVDE << 6;
     tempbx = tempvcfact & 0x3f;
     if(tempbx == 0) tempbx = 64;
     tempeax /= tempbx;
     tempbx = (unsigned short)(tempeax & 0xFFFF);
  }
  if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) tempbx--;
  if(SiS_Pr->SiS_SetFlag & EnableLVDSDDA) {
     if((!SiS_Pr->SiS_IF_DEF_FSTN) && (!SiS_Pr->SiS_IF_DEF_DSTN)) tempbx = 1;
     else if(SiS_Pr->SiS_LCDResInfo != Panel_640x480)             tempbx = 1;
  }

  temp = ((tempbx >> 8) & 0x07) << 3;
  temp = temp | ((tempecx >> 8) & 0x07);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x20,temp);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x21,tempbx);

  tempecx >>= 16;						/* BPLHCFACT  */
  if(!chkdclkfirst) {
     if(modeflag & HalfDCLK) tempecx >>= 1;
  }
  temp = (unsigned short)((tempecx & 0xFF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x22,temp);
  temp = (unsigned short)(tempecx & 0x00FF);
  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x23,temp);

#ifdef CONFIG_FB_SIS_315
  if(SiS_Pr->ChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
        if((islvds) || (SiS_Pr->SiS_VBInfo & VB_SISLVDS)) {
           SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1e,0x20);
	}
     } else {
        if(islvds) {
           if(SiS_Pr->ChipType == SIS_740) {
              SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x03);
           } else {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1e,0x23);
           }
        }
     }
  }
#endif

#ifdef CONFIG_FB_SIS_300
  if(SiS_Pr->SiS_IF_DEF_TRUMPION) {
     unsigned char *ROMAddr = SiS_Pr->VirtualRomBase;
     unsigned char *trumpdata;
     int   i, j = crt2crtc;
     unsigned char TrumpMode13[4]   = { 0x01, 0x10, 0x2c, 0x00 };
     unsigned char TrumpMode10_1[4] = { 0x01, 0x10, 0x27, 0x00 };
     unsigned char TrumpMode10_2[4] = { 0x01, 0x16, 0x10, 0x00 };

     if(SiS_Pr->SiS_UseROM) {
	trumpdata = &ROMAddr[0x8001 + (j * 80)];
     } else {
	if(SiS_Pr->SiS_LCDTypeInfo == 0x0e) j += 7;
	trumpdata = &SiS300_TrumpionData[j][0];
     }

     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x02,0xbf);
     for(i=0; i<5; i++) {
	SiS_SetTrumpionBlock(SiS_Pr, trumpdata);
     }
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	if(ModeNo == 0x13) {
	   for(i=0; i<4; i++) {
	      SiS_SetTrumpionBlock(SiS_Pr, &TrumpMode13[0]);
	   }
	} else if(ModeNo == 0x10) {
	   for(i=0; i<4; i++) {
	      SiS_SetTrumpionBlock(SiS_Pr, &TrumpMode10_1[0]);
	      SiS_SetTrumpionBlock(SiS_Pr, &TrumpMode10_2[0]);
	   }
	}
     }
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x02,0x40);
  }
#endif

#ifdef CONFIG_FB_SIS_315
  if(SiS_Pr->SiS_IF_DEF_FSTN || SiS_Pr->SiS_IF_DEF_DSTN) {
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x25,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x26,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x27,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x28,0x87);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x29,0x5A);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2A,0x4B);
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x44,~0x07,0x03);
     tempax = SiS_Pr->SiS_HDE;					/* Blps = lcdhdee(lcdhdes+HDE) + 64 */
     if(SiS_Pr->SiS_LCDResInfo == Panel_320x240_1 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_2 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_3) tempax >>= 1;
     tempax += 64;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x38,tempax & 0xff);
     temp = (tempax >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,~0x078,temp);
     tempax += 32;						/* Blpe = lBlps+32 */
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x39,tempax & 0xff);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3A,0x00);		/* Bflml = 0 */
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x3C,~0x007);

     tempax = SiS_Pr->SiS_VDE;
     if(SiS_Pr->SiS_LCDResInfo == Panel_320x240_1 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_2 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_3) tempax >>= 1;
     tempax >>= 1;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3B,tempax & 0xff);
     temp = (tempax >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x3C,~0x038,temp);

     tempeax = SiS_Pr->SiS_HDE;
     if(SiS_Pr->SiS_LCDResInfo == Panel_320x240_1 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_2 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_3) tempeax >>= 1;
     tempeax <<= 2;			 			/* BDxFIFOSTOP = (HDE*4)/128 */
     temp = tempeax & 0x7f;
     tempeax >>= 7;
     if(temp) tempeax++;
     temp = tempeax & 0x3f;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x45,temp);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3F,0x00);		/* BDxWadrst0 */
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3E,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3D,0x10);
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x3C,~0x040);

     tempax = SiS_Pr->SiS_HDE;
     if(SiS_Pr->SiS_LCDResInfo == Panel_320x240_1 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_2 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_3) tempax >>= 1;
     tempax >>= 4;						/* BDxWadroff = HDE*4/8/8 */
     pushcx = tempax;
     temp = tempax & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x43,temp);
     temp = ((tempax & 0xFF00) >> 8) << 3;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port, 0x44, 0x07, temp);

     tempax = SiS_Pr->SiS_VDE;				 	/* BDxWadrst1 = BDxWadrst0 + BDxWadroff * VDE */
     if(SiS_Pr->SiS_LCDResInfo == Panel_320x240_1 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_2 ||
        SiS_Pr->SiS_LCDResInfo == Panel_320x240_3) tempax >>= 1;
     tempeax = tempax * pushcx;
     temp = tempeax & 0xFF;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x42,temp);
     temp = (tempeax & 0xFF00) >> 8;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x41,temp);
     temp = ((tempeax & 0xFF0000) >> 16) | 0x10;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x40,temp);
     temp = ((tempeax & 0x01000000) >> 24) << 7;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port, 0x3C, 0x7F, temp);

     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2F,0x03);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x03,0x50);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x04,0x00);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2F,0x01);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0x38);

     if(SiS_Pr->SiS_IF_DEF_FSTN) {
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2b,0x02);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2c,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x35,0x0c);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x36,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x37,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x38,0x80);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x39,0xA0);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3a,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3b,0xf0);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3c,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3d,0x10);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3e,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x3f,0x00);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x40,0x10);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x41,0x25);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x42,0x80);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x43,0x14);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x44,0x03);
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x45,0x0a);
     }
  }
#endif  /* CONFIG_FB_SIS_315 */
}

/* Set Part 1 */
static void
SiS_SetGroup1(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
  unsigned char   *ROMAddr = SiS_Pr->VirtualRomBase;
#endif
  unsigned short  temp=0, tempax=0, tempbx=0, tempcx=0, bridgeadd=0;
  unsigned short  pushbx=0, CRT1Index=0, modeflag, resinfo=0;
#ifdef CONFIG_FB_SIS_315
  unsigned short  tempbl=0;
#endif

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
     SiS_SetGroup1_LVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
     return;
  }

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     CRT1Index = SiS_GetRefCRT1CRTC(SiS_Pr, RefreshRateTableIndex, SiS_Pr->SiS_UseWideCRT2);
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  SiS_SetCRT2Offset(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);

  if( ! ((SiS_Pr->ChipType >= SIS_315H) &&
         (SiS_Pr->SiS_IF_DEF_LVDS == 1) &&
         (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ) {

     if(SiS_Pr->ChipType < SIS_315H ) {
#ifdef CONFIG_FB_SIS_300
	SiS_SetCRT2FIFO_300(SiS_Pr, ModeNo);
#endif
     } else {
#ifdef CONFIG_FB_SIS_315
	SiS_SetCRT2FIFO_310(SiS_Pr);
#endif
     }

     /* 1. Horizontal setup */

     if(SiS_Pr->ChipType < SIS_315H ) {

#ifdef CONFIG_FB_SIS_300   /* ------------- 300 series --------------*/

	temp = (SiS_Pr->SiS_VGAHT - 1) & 0x0FF;   		  /* BTVGA2HT 0x08,0x09 */
	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x08,temp);              /* CRT2 Horizontal Total */

	temp = (((SiS_Pr->SiS_VGAHT - 1) & 0xFF00) >> 8) << 4;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0f,temp);    /* CRT2 Horizontal Total Overflow [7:4] */

	temp = (SiS_Pr->SiS_VGAHDE + 12) & 0x0FF;                 /* BTVGA2HDEE 0x0A,0x0C */
	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0A,temp);              /* CRT2 Horizontal Display Enable End */

	pushbx = SiS_Pr->SiS_VGAHDE + 12;                         /* bx  BTVGA2HRS 0x0B,0x0C */
	tempcx = (SiS_Pr->SiS_VGAHT - SiS_Pr->SiS_VGAHDE) >> 2;
	tempbx = pushbx + tempcx;
	tempcx <<= 1;
	tempcx += tempbx;

	bridgeadd = 12;

#endif /* CONFIG_FB_SIS_300 */

     } else {

#ifdef CONFIG_FB_SIS_315  /* ------------------- 315/330 series --------------- */

	tempcx = SiS_Pr->SiS_VGAHT;				  /* BTVGA2HT 0x08,0x09 */
	if(modeflag & HalfDCLK) {
	   if(SiS_Pr->SiS_VBType & VB_SISVB) {
	      tempcx >>= 1;
	   } else {
	      tempax = SiS_Pr->SiS_VGAHDE >> 1;
	      tempcx = SiS_Pr->SiS_HT - SiS_Pr->SiS_HDE + tempax;
	      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	         tempcx = SiS_Pr->SiS_HT - tempax;
	      }
	   }
	}
	tempcx--;
	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x08,tempcx);            /* CRT2 Horizontal Total */
	temp = (tempcx >> 4) & 0xF0;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0x0F,temp);    /* CRT2 Horizontal Total Overflow [7:4] */

	tempcx = SiS_Pr->SiS_VGAHT;				  /* BTVGA2HDEE 0x0A,0x0C */
	tempbx = SiS_Pr->SiS_VGAHDE;
	tempcx -= tempbx;
	tempcx >>= 2;
	if(modeflag & HalfDCLK) {
	   tempbx >>= 1;
	   tempcx >>= 1;
	}
	tempbx += 16;

	SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0A,tempbx);            /* CRT2 Horizontal Display Enable End */

	pushbx = tempbx;
	tempcx >>= 1;
	tempbx += tempcx;
	tempcx += tempbx;

	bridgeadd = 16;

	if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   if(SiS_Pr->ChipType >= SIS_661) {
	      if((SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) ||
		 (SiS_Pr->SiS_LCDResInfo == Panel_1280x1024)) {
		 if(resinfo == SIS_RI_1280x1024) {
		    tempcx = (tempcx & 0xff00) | 0x30;
		 } else if(resinfo == SIS_RI_1600x1200) {
		    tempcx = (tempcx & 0xff00) | 0xff;
		 }
	      }
	   }
        }

#endif  /* CONFIG_FB_SIS_315 */

     }  /* 315/330 series */

     if(SiS_Pr->SiS_VBType & VB_SISVB) {

	if(SiS_Pr->UseCustomMode) {
	   tempbx = SiS_Pr->CHSyncStart + bridgeadd;
	   tempcx = SiS_Pr->CHSyncEnd + bridgeadd;
	   tempax = SiS_Pr->SiS_VGAHT;
	   if(modeflag & HalfDCLK) tempax >>= 1;
	   tempax--;
	   if(tempcx > tempax) tempcx = tempax;
	}

	if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
	   unsigned char cr4, cr14, cr5, cr15;
	   if(SiS_Pr->UseCustomMode) {
	      cr4  = SiS_Pr->CCRT1CRTC[4];
	      cr14 = SiS_Pr->CCRT1CRTC[14];
	      cr5  = SiS_Pr->CCRT1CRTC[5];
	      cr15 = SiS_Pr->CCRT1CRTC[15];
	   } else {
	      cr4  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[4];
	      cr14 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14];
	      cr5  = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[5];
	      cr15 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[15];
	   }
	   tempbx = ((cr4 | ((cr14 & 0xC0) << 2)) - 3) << 3; 		    /* (VGAHRS-3)*8 */
	   tempcx = (((cr5 & 0x1f) | ((cr15 & 0x04) << (5-2))) - 3) << 3;   /* (VGAHRE-3)*8 */
	   tempcx &= 0x00FF;
	   tempcx |= (tempbx & 0xFF00);
	   tempbx += bridgeadd;
	   tempcx += bridgeadd;
	   tempax = SiS_Pr->SiS_VGAHT;
	   if(modeflag & HalfDCLK) tempax >>= 1;
	   tempax--;
	   if(tempcx > tempax) tempcx = tempax;
	}

	if(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSet525p1024)) {
	   tempbx = 1040;
	   tempcx = 1044;   /* HWCursor bug! */
	}

     }

     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0B,tempbx);            	  /* CRT2 Horizontal Retrace Start */

     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0D,tempcx);               /* CRT2 Horizontal Retrace End */

     temp = ((tempbx >> 8) & 0x0F) | ((pushbx >> 4) & 0xF0);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0C,temp);		  /* Overflow */

     /* 2. Vertical setup */

     tempcx = SiS_Pr->SiS_VGAVT - 1;
     temp = tempcx & 0x00FF;

     if(SiS_Pr->ChipType < SIS_661) {
        if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	   if(SiS_Pr->ChipType < SIS_315H) {
	      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	         if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)) {
	            temp--;
	         }
	      }
	   } else {
	      temp--;
	   }
	} else if(SiS_Pr->ChipType >= SIS_315H) {
	   temp--;
	}
     }
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0E,temp);                 /* CRT2 Vertical Total */

     tempbx = SiS_Pr->SiS_VGAVDE - 1;
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x0F,tempbx);               /* CRT2 Vertical Display Enable End */

     temp = ((tempbx >> 5) & 0x38) | ((tempcx >> 8) & 0x07);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x12,temp);                 /* Overflow */

     if((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->ChipType < SIS_661)) {
	tempbx++;
	tempax = tempbx;
	tempcx++;
	tempcx -= tempax;
	tempcx >>= 2;
	tempbx += tempcx;
	if(tempcx < 4) tempcx = 4;
	tempcx >>= 2;
	tempcx += tempbx;
	tempcx++;
     } else {
	tempbx = (SiS_Pr->SiS_VGAVT + SiS_Pr->SiS_VGAVDE) >> 1;                 /*  BTVGA2VRS     0x10,0x11   */
	tempcx = ((SiS_Pr->SiS_VGAVT - SiS_Pr->SiS_VGAVDE) >> 4) + tempbx + 1;  /*  BTVGA2VRE     0x11        */
     }

     if(SiS_Pr->SiS_VBType & VB_SISVB) {
	if(SiS_Pr->UseCustomMode) {
	   tempbx = SiS_Pr->CVSyncStart;
	   tempcx = SiS_Pr->CVSyncEnd;
	}
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {
	   unsigned char cr8, cr7, cr13;
	   if(SiS_Pr->UseCustomMode) {
	      cr8    = SiS_Pr->CCRT1CRTC[8];
	      cr7    = SiS_Pr->CCRT1CRTC[7];
	      cr13   = SiS_Pr->CCRT1CRTC[13];
	      tempcx = SiS_Pr->CCRT1CRTC[9];
	   } else {
	      cr8    = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[8];
	      cr7    = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[7];
	      cr13   = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[13];
	      tempcx = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[9];
	   }
	   tempbx = cr8;
	   if(cr7  & 0x04) tempbx |= 0x0100;
	   if(cr7  & 0x80) tempbx |= 0x0200;
	   if(cr13 & 0x08) tempbx |= 0x0400;
	}
     }
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x10,tempbx);               /* CRT2 Vertical Retrace Start */

     temp = ((tempbx >> 4) & 0x70) | (tempcx & 0x0F);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x11,temp);                 /* CRT2 Vert. Retrace End; Overflow */

     /* 3. Panel delay compensation */

     if(SiS_Pr->ChipType < SIS_315H) {

#ifdef CONFIG_FB_SIS_300  /* ---------- 300 series -------------- */

	if(SiS_Pr->SiS_VBType & VB_SISVB) {
	   temp = 0x20;
	   if(SiS_Pr->ChipType == SIS_300) {
	      temp = 0x10;
	      if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768)  temp = 0x2c;
	      if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) temp = 0x20;
	   }
	   if(SiS_Pr->SiS_VBType & VB_SIS301) {
	      if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) temp = 0x20;
	   }
	   if(SiS_Pr->SiS_LCDResInfo == Panel_1280x960)     temp = 0x24;
	   if(SiS_Pr->SiS_LCDResInfo == Panel_Custom)       temp = 0x2c;
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) 	    temp = 0x08;
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) 	    temp = 0x2c;
	      else 					    temp = 0x20;
	   }
	   if(SiS_Pr->SiS_UseROM) {
	      if(ROMAddr[0x220] & 0x80) {
		 if(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoYPbPrHiVision)
		    temp = ROMAddr[0x221];
		 else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision)
		    temp = ROMAddr[0x222];
		 else if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024)
		    temp = ROMAddr[0x223];
		 else
		    temp = ROMAddr[0x224];
	      }
	   }
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	      if(SiS_Pr->PDC != -1)  temp = SiS_Pr->PDC;
	   }

	} else {
	   temp = 0x20;
	   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
	      if(SiS_Pr->SiS_LCDResInfo == Panel_640x480) temp = 0x04;
	   }
	   if(SiS_Pr->SiS_UseROM) {
	      if(ROMAddr[0x220] & 0x80) {
	         temp = ROMAddr[0x220];
	      }
	   }
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	      if(SiS_Pr->PDC != -1) temp = SiS_Pr->PDC;
	   }
	}

	temp &= 0x3c;

	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);   /* Panel Link Delay Compensation; (Software Command Reset; Power Saving) */

#endif  /* CONFIG_FB_SIS_300 */

     } else {

#ifdef CONFIG_FB_SIS_315   /* --------------- 315/330 series ---------------*/

	if(SiS_Pr->ChipType < SIS_661) {

	   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {

	      if(SiS_Pr->ChipType == SIS_740) temp = 0x03;
	      else 		              temp = 0x00;

	      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) temp = 0x0a;
	      tempbl = 0xF0;
	      if(SiS_Pr->ChipType == SIS_650) {
		 if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
		    if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) tempbl = 0x0F;
		 }
	      }

	      if(SiS_Pr->SiS_IF_DEF_DSTN || SiS_Pr->SiS_IF_DEF_FSTN) {
		 temp = 0x08;
		 tempbl = 0;
		 if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {
		    if(ROMAddr[0x13c] & 0x80) tempbl = 0xf0;
		 }
	      }

	      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,tempbl,temp);	    /* Panel Link Delay Compensation */
	   }

	} /* < 661 */

	tempax = 0;
	if(modeflag & DoubleScanMode) tempax |= 0x80;
	if(modeflag & HalfDCLK)       tempax |= 0x40;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2C,0x3f,tempax);

#endif  /* CONFIG_FB_SIS_315 */

     }

  }  /* Slavemode */

  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {
	/* For 301BDH with LCD, we set up the Panel Link */
	SiS_SetGroup1_LVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
     } else if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	SiS_SetGroup1_301(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
     }
  } else {
     if(SiS_Pr->ChipType < SIS_315H) {
	SiS_SetGroup1_LVDS(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
     } else {
	if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	   if((!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) || (SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	      SiS_SetGroup1_LVDS(SiS_Pr, ModeNo,ModeIdIndex,RefreshRateTableIndex);
	   }
	} else {
	   SiS_SetGroup1_LVDS(SiS_Pr, ModeNo,ModeIdIndex,RefreshRateTableIndex);
	}
     }
  }
}

/*********************************************/
/*         SET PART 2 REGISTER GROUP         */
/*********************************************/

#ifdef CONFIG_FB_SIS_315
static unsigned char *
SiS_GetGroup2CLVXPtr(struct SiS_Private *SiS_Pr, int tabletype)
{
   const unsigned char *tableptr = NULL;
   unsigned short      a, b, p = 0;

   a = SiS_Pr->SiS_VGAHDE;
   b = SiS_Pr->SiS_HDE;
   if(tabletype) {
      a = SiS_Pr->SiS_VGAVDE;
      b = SiS_Pr->SiS_VDE;
   }

   if(a < b) {
      tableptr = SiS_Part2CLVX_1;
   } else if(a == b) {
      tableptr = SiS_Part2CLVX_2;
   } else {
      if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	 tableptr = SiS_Part2CLVX_4;
      } else {
	 tableptr = SiS_Part2CLVX_3;
      }
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
	 if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) 	tableptr = SiS_Part2CLVX_3;
	 else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) 	tableptr = SiS_Part2CLVX_3;
	 else 				         	tableptr = SiS_Part2CLVX_5;
      } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	 tableptr = SiS_Part2CLVX_6;
      }
      do {
	 if((tableptr[p] | tableptr[p+1] << 8) == a) break;
	 p += 0x42;
      } while((tableptr[p] | tableptr[p+1] << 8) != 0xffff);
      if((tableptr[p] | tableptr[p+1] << 8) == 0xffff) p -= 0x42;
   }
   p += 2;
   return ((unsigned char *)&tableptr[p]);
}

static void
SiS_SetGroup2_C_ELV(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
	      	    unsigned short RefreshRateTableIndex)
{
   unsigned char *tableptr;
   unsigned char temp;
   int i, j;

   if(!(SiS_Pr->SiS_VBType & VB_SISTAP4SCALER)) return;

   tableptr = SiS_GetGroup2CLVXPtr(SiS_Pr, 0);
   for(i = 0x80, j = 0; i <= 0xbf; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part2Port, i, tableptr[j]);
   }
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
      tableptr = SiS_GetGroup2CLVXPtr(SiS_Pr, 1);
      for(i = 0xc0, j = 0; i <= 0xff; i++, j++) {
         SiS_SetReg(SiS_Pr->SiS_Part2Port, i, tableptr[j]);
      }
   }
   temp = 0x10;
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) temp |= 0x04;
   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x4e,0xeb,temp);
}

static bool
SiS_GetCRT2Part2Ptr(struct SiS_Private *SiS_Pr,unsigned short ModeNo,unsigned short ModeIdIndex,
		    unsigned short RefreshRateTableIndex,unsigned short *CRT2Index,
		    unsigned short *ResIndex)
{

  if(SiS_Pr->ChipType < SIS_315H) return false;

  if(ModeNo <= 0x13)
     (*ResIndex) = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  else
     (*ResIndex) = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

  (*ResIndex) &= 0x3f;
  (*CRT2Index) = 0;

  if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
     if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
        (*CRT2Index) = 200;
     }
  }

  if(SiS_Pr->SiS_CustomT == CUT_ASUSA2H_2) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
        if(SiS_Pr->SiS_SetFlag & LCDVESATiming) (*CRT2Index) = 206;
     }
  }
  return (((*CRT2Index) != 0));
}
#endif

#ifdef CONFIG_FB_SIS_300
static void
SiS_Group2LCDSpecial(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short crt2crtc)
{
   unsigned short tempcx;
   static const unsigned char atable[] = {
       0xc3,0x9e,0xc3,0x9e,0x02,0x02,0x02,
       0xab,0x87,0xab,0x9e,0xe7,0x02,0x02
   };

   if(!SiS_Pr->UseCustomMode) {
      if( ( ( (SiS_Pr->ChipType == SIS_630) ||
	      (SiS_Pr->ChipType == SIS_730) ) &&
	    (SiS_Pr->ChipRevision > 2) )  &&
	  (SiS_Pr->SiS_LCDResInfo == Panel_1024x768) &&
	  (!(SiS_Pr->SiS_SetFlag & LCDVESATiming))  &&
	  (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) ) {
	 if(ModeNo == 0x13) {
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,0xB9);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,0xCC);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xA6);
	 } else if((crt2crtc & 0x3F) == 4) {
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x2B);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x13);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,0xE5);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,0x08);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xE2);
	 }
      }

      if(SiS_Pr->ChipType < SIS_315H) {
	 if(SiS_Pr->SiS_LCDTypeInfo == 0x0c) {
	    crt2crtc &= 0x1f;
	    tempcx = 0;
	    if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		  tempcx += 7;
	       }
	    }
	    tempcx += crt2crtc;
	    if(crt2crtc >= 4) {
	       SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,0xff);
	    }

	    if(!(SiS_Pr->SiS_VBInfo & SetNotSimuMode)) {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		  if(crt2crtc == 4) {
		     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x28);
		  }
	       }
	    }
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x18);
	    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,atable[tempcx]);
	 }
      }
   }
}

/* For ECS A907. Highly preliminary. */
static void
SiS_Set300Part2Regs(struct SiS_Private *SiS_Pr, unsigned short ModeIdIndex, unsigned short RefreshRateTableIndex,
		    unsigned short ModeNo)
{
  const struct SiS_Part2PortTbl *CRT2Part2Ptr = NULL;
  unsigned short crt2crtc, resindex;
  int i, j;

  if(SiS_Pr->ChipType != SIS_300) return;
  if(!(SiS_Pr->SiS_VBType & VB_SIS30xBLV)) return;
  if(SiS_Pr->UseCustomMode) return;

  if(ModeNo <= 0x13) {
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  resindex = crt2crtc & 0x3F;
  if(SiS_Pr->SiS_SetFlag & LCDVESATiming) CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
  else                                    CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_2;

  /* The BIOS code (1.16.51,56) is obviously a fragment! */
  if(ModeNo > 0x13) {
     CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;
     resindex = 4;
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
  for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1c; j <= 0x1d; i++, j++ ) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  for(j = 0x1f; j <= 0x21; i++, j++ ) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);
}
#endif

static void
SiS_SetTVSpecial(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
  if(!(SiS_Pr->SiS_VBType & VB_SIS30xBLV)) return;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision)) return;
  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) return;

  if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
     if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
        static const unsigned char specialtv[] = {
		0xa7,0x07,0xf2,0x6e,0x17,0x8b,0x73,0x53,
		0x13,0x40,0x34,0xf4,0x63,0xbb,0xcc,0x7a,
		0x58,0xe4,0x73,0xda,0x13
	};
	int i, j;
	for(i = 0x1c, j = 0; i <= 0x30; i++, j++) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,specialtv[j]);
	}
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x43,0x72);
	if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750)) {
	   if(SiS_Pr->SiS_TVMode & TVSetPALM) {
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x14);
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x1b);
	   } else {
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x14);  /* 15 */
	      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x1a);  /* 1b */
	   }
	}
     }
  } else {
     if((ModeNo == 0x38) || (ModeNo == 0x4a) || (ModeNo == 0x64) ||
        (ModeNo == 0x52) || (ModeNo == 0x58) || (ModeNo == 0x5c)) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x1b);  /* 21 */
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x54);  /* 5a */
     } else {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x1a);  /* 21 */
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x53);  /* 5a */
     }
  }
}

static void
SiS_SetGroup2_Tail(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
  unsigned short temp;

  if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) {
     if(SiS_Pr->SiS_VGAVDE == 525) {
	temp = 0xc3;
	if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	   temp++;
	   if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) temp += 2;
	}
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,0xb3);
     } else if(SiS_Pr->SiS_VGAVDE == 420) {
	temp = 0x4d;
	if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	   temp++;
	   if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) temp++;
	}
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2f,temp);
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) {
	if(SiS_Pr->SiS_VBType & VB_SIS30xB) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x1a,0x03);
	   /* Not always for LV, see SetGrp2 */
	}
	temp = 1;
	if(ModeNo <= 0x13) temp = 3;
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0b,temp);
     }
#if 0
     /* 651+301C, for 1280x768 - do I really need that? */
     if((SiS_Pr->SiS_PanelXRes == 1280) && (SiS_Pr->SiS_PanelYRes == 768)) {
        if(SiS_Pr->SiS_VBInfo & SetSimuScanMode) {
	   if(((SiS_Pr->SiS_HDE == 640) && (SiS_Pr->SiS_VDE == 480)) ||
	      ((SiS_Pr->SiS_HDE == 320) && (SiS_Pr->SiS_VDE == 240))) {
	      SiS_SetReg(SiS_Part2Port,0x01,0x2b);
	      SiS_SetReg(SiS_Part2Port,0x02,0x13);
	      SiS_SetReg(SiS_Part2Port,0x04,0xe5);
	      SiS_SetReg(SiS_Part2Port,0x05,0x08);
	      SiS_SetReg(SiS_Part2Port,0x06,0xe2);
	      SiS_SetReg(SiS_Part2Port,0x1c,0x21);
	      SiS_SetReg(SiS_Part2Port,0x1d,0x45);
	      SiS_SetReg(SiS_Part2Port,0x1f,0x0b);
	      SiS_SetReg(SiS_Part2Port,0x20,0x00);
	      SiS_SetReg(SiS_Part2Port,0x21,0xa9);
	      SiS_SetReg(SiS_Part2Port,0x23,0x0b);
	      SiS_SetReg(SiS_Part2Port,0x25,0x04);
	   }
	}
     }
#endif
  }
}

static void
SiS_SetGroup2(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned short i, j, tempax, tempbx, tempcx, tempch, tempcl, temp;
  unsigned short push2, modeflag, crt2crtc, bridgeoffset;
  unsigned int   longtemp, PhaseIndex;
  bool           newtvphase;
  const unsigned char *TimingPoint;
#ifdef CONFIG_FB_SIS_315
  unsigned short resindex, CRT2Index;
  const struct SiS_Part2PortTbl *CRT2Part2Ptr = NULL;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) return;
#endif

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
     crt2crtc = 0;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     crt2crtc = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  temp = 0;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToAVIDEO)) temp |= 0x08;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToSVIDEO)) temp |= 0x04;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)     temp |= 0x02;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision)  temp |= 0x01;

  if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) 	      temp |= 0x10;

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x00,temp);

  PhaseIndex  = 0x01; /* SiS_PALPhase */
  TimingPoint = SiS_Pr->SiS_PALTiming;

  newtvphase = false;
  if( (SiS_Pr->SiS_VBType & VB_SIS30xBLV) &&
      ( (!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
	(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) ) ) {
     newtvphase = true;
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {

     TimingPoint = SiS_Pr->SiS_HiTVExtTiming;
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        TimingPoint = SiS_Pr->SiS_HiTVSt2Timing;
        if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	   TimingPoint = SiS_Pr->SiS_HiTVSt1Timing;
        }
     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {

     i = 0;
     if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)      i = 2;
     else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) i = 1;

     TimingPoint = &SiS_YPbPrTable[i][0];

     PhaseIndex = 0x00; /* SiS_NTSCPhase */

  } else if(SiS_Pr->SiS_TVMode & TVSetPAL) {

     if(newtvphase) PhaseIndex = 0x09; /* SiS_PALPhase2 */

  } else {

     TimingPoint = SiS_Pr->SiS_NTSCTiming;
     PhaseIndex  = (SiS_Pr->SiS_TVMode & TVSetNTSCJ) ? 0x01 : 0x00;	/* SiS_PALPhase : SiS_NTSCPhase */
     if(newtvphase) PhaseIndex += 8;					/* SiS_PALPhase2 : SiS_NTSCPhase2 */

  }

  if(SiS_Pr->SiS_TVMode & (TVSetPALM | TVSetPALN)) {
     PhaseIndex = (SiS_Pr->SiS_TVMode & TVSetPALM) ? 0x02 : 0x03;	/* SiS_PALMPhase : SiS_PALNPhase */
     if(newtvphase) PhaseIndex += 8;					/* SiS_PALMPhase2 : SiS_PALNPhase2 */
  }

  if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
     if(SiS_Pr->SiS_TVMode & TVSetPALM) {
        PhaseIndex = 0x05; /* SiS_SpecialPhaseM */
     } else if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) {
        PhaseIndex = 0x11; /* SiS_SpecialPhaseJ */
     } else {
        PhaseIndex = 0x10; /* SiS_SpecialPhase */
     }
  }

  for(i = 0x31, j = 0; i <= 0x34; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS_TVPhase[(PhaseIndex * 4) + j]);
  }

  for(i = 0x01, j = 0; i <= 0x2D; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }
  for(i = 0x39; i <= 0x45; i++, j++) {
     SiS_SetReg(SiS_Pr->SiS_Part2Port,i,TimingPoint[j]);
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_ModeType != ModeText) {
        SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x3A,0x1F);
     }
  }

  SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x0A,SiS_Pr->SiS_NewFlickerMode);

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x35,SiS_Pr->SiS_RY1COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x36,SiS_Pr->SiS_RY2COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x37,SiS_Pr->SiS_RY3COE);
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x38,SiS_Pr->SiS_RY4COE);

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision)	tempax = 950;
  else if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)  tempax = 680;
  else if(SiS_Pr->SiS_TVMode & TVSetPAL)	tempax = 520;
  else						tempax = 440; /* NTSC, YPbPr 525 */

  if( ((SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) && (SiS_Pr->SiS_VDE <= tempax)) ||
      ( (SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoHiVision) &&
        ((SiS_Pr->SiS_VGAHDE == 1024) || (SiS_Pr->SiS_VDE <= tempax)) ) ) {

     tempax -= SiS_Pr->SiS_VDE;
     tempax >>= 1;
     if(!(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p))) {
        tempax >>= 1;
     }
     tempax &= 0x00ff;

     temp = tempax + (unsigned short)TimingPoint[0];
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp);

     temp = tempax + (unsigned short)TimingPoint[1];
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,temp);

     if((SiS_Pr->SiS_VBInfo & SetCRT2ToTVNoYPbPrHiVision) && (SiS_Pr->SiS_VGAHDE >= 1024)) {
        if(SiS_Pr->SiS_TVMode & TVSetPAL) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x1b);
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x54);
        } else {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,0x17);
           SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,0x1d);
        }
     }

  }

  tempcx = SiS_Pr->SiS_HT;
  if(SiS_IsDualLink(SiS_Pr)) tempcx >>= 1;
  tempcx--;
  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) tempcx--;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1B,tempcx);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0xF0,((tempcx >> 8) & 0x0f));

  tempcx = SiS_Pr->SiS_HT >> 1;
  if(SiS_IsDualLink(SiS_Pr)) tempcx >>= 1;
  tempcx += 7;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) tempcx -= 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x22,0x0F,((tempcx << 4) & 0xf0));

  tempbx = TimingPoint[j] | (TimingPoint[j+1] << 8);
  tempbx += tempcx;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x24,tempbx);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0F,((tempbx >> 4) & 0xf0));

  tempbx += 8;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     tempbx -= 4;
     tempcx = tempbx;
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x29,0x0F,((tempbx << 4) & 0xf0));

  j += 2;
  tempcx += (TimingPoint[j] | (TimingPoint[j+1] << 8));
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x27,tempcx);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x28,0x0F,((tempcx >> 4) & 0xf0));

  tempcx += 8;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) tempcx -= 4;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2A,0x0F,((tempcx << 4) & 0xf0));

  tempcx = SiS_Pr->SiS_HT >> 1;
  if(SiS_IsDualLink(SiS_Pr)) tempcx >>= 1;
  j += 2;
  tempcx -= (TimingPoint[j] | ((TimingPoint[j+1]) << 8));
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2D,0x0F,((tempcx << 4) & 0xf0));

  tempcx -= 11;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
     tempcx = SiS_GetVGAHT2(SiS_Pr) - 1;
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2E,tempcx);

  tempbx = SiS_Pr->SiS_VDE;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     if(SiS_Pr->SiS_VGAVDE == 360) tempbx = 746;
     if(SiS_Pr->SiS_VGAVDE == 375) tempbx = 746;
     if(SiS_Pr->SiS_VGAVDE == 405) tempbx = 853;
  } else if( (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) &&
             (!(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p|TVSetYPbPr750p))) ) {
     tempbx >>= 1;
     if(SiS_Pr->ChipType >= SIS_315H) {
        if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
	   if((ModeNo <= 0x13) && (crt2crtc == 1)) tempbx++;
	} else if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	   if(SiS_Pr->SiS_ModeType <= ModeVGA) {
	      if(crt2crtc == 4) tempbx++;
	   }
	}
     }
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	   if((ModeNo == 0x2f) || (ModeNo == 0x5d) || (ModeNo == 0x5e)) tempbx++;
	}
	if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {
	   if(ModeNo == 0x03) tempbx++; /* From 1.10.7w - doesn't make sense */
        }
     }
  }
  tempbx -= 2;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2F,tempbx);

  temp = (tempcx >> 8) & 0x0F;
  temp |= ((tempbx >> 2) & 0xC0);
  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)) {
     temp |= 0x10;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToAVIDEO) temp |= 0x20;
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x30,temp);

  if(SiS_Pr->SiS_VBType & VB_SISPART4OVERFLOW) {
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x10,0xdf,((tempbx & 0x0400) >> 5));
  }

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
     tempbx = SiS_Pr->SiS_VDE;
     if( (SiS_Pr->SiS_VBInfo & SetCRT2ToTV) &&
         (!(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p))) ) {
        tempbx >>= 1;
     }
     tempbx -= 3;
     temp = ((tempbx >> 3) & 0x60) | 0x18;
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x46,temp);
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x47,tempbx);

     if(SiS_Pr->SiS_VBType & VB_SISPART4OVERFLOW) {
	SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x10,0xbf,((tempbx & 0x0400) >> 4));
     }
  }

  tempbx = 0;
  if(!(modeflag & HalfDCLK)) {
     if(SiS_Pr->SiS_VGAHDE >= SiS_Pr->SiS_HDE) {
        tempax = 0;
        tempbx |= 0x20;
     }
  }

  tempch = tempcl = 0x01;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     if(SiS_Pr->SiS_VGAHDE >= 960) {
        if((!(modeflag & HalfDCLK)) || (SiS_Pr->ChipType < SIS_315H)) {
	   tempcl = 0x20;
	   if(SiS_Pr->SiS_VGAHDE >= 1280) {
              tempch = 20;
              tempbx &= ~0x20;
           } else {
	      tempch = 25; /* OK */
	   }
        }
     }
  }

  if(!(tempbx & 0x20)) {
     if(modeflag & HalfDCLK) tempcl <<= 1;
     longtemp = ((SiS_Pr->SiS_VGAHDE * tempch) / tempcl) << 13;
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) longtemp <<= 3;
     tempax = longtemp / SiS_Pr->SiS_HDE;
     if(longtemp % SiS_Pr->SiS_HDE) tempax++;
     tempbx |= ((tempax >> 8) & 0x1F);
     tempcx = tempax >> 13;
  }

  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x44,tempax);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x45,0xC0,tempbx);

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {

     tempcx &= 0x07;
     if(tempbx & 0x20) tempcx = 0;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x46,0xF8,tempcx);

     if(SiS_Pr->SiS_TVMode & TVSetPAL) {
        tempbx = 0x0382;
        tempcx = 0x007e;
     } else {
        tempbx = 0x0369;
        tempcx = 0x0061;
     }
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4B,tempbx);
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4C,tempcx);
     temp = (tempcx & 0x0300) >> 6;
     temp |= ((tempbx >> 8) & 0x03);
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
        temp |= 0x10;
	if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p)      temp |= 0x20;
	else if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) temp |= 0x40;
     }
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x4D,temp);

     temp = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x43);
     SiS_SetReg(SiS_Pr->SiS_Part2Port,0x43,(temp - 3));

     SiS_SetTVSpecial(SiS_Pr, ModeNo);

     if(SiS_Pr->SiS_VBType & VB_SIS30xCLV) {
        temp = 0;
        if(SiS_Pr->SiS_TVMode & TVSetPALM) temp = 8;
        SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x4e,0xf7,temp);
     }

  }

  if(SiS_Pr->SiS_TVMode & TVSetPALM) {
     if(!(SiS_Pr->SiS_TVMode & TVSetNTSC1024)) {
        temp = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x01);
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,(temp - 1));
     }
     SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x00,0xEF);
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0B,0x00);
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) return;

  /* From here: Part2 LCD setup */

  tempbx = SiS_Pr->SiS_HDE;
  if(SiS_IsDualLink(SiS_Pr)) tempbx >>= 1;
  tempbx--;			         	/* RHACTE = HDE - 1 */
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x2C,tempbx);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2B,0x0F,((tempbx >> 4) & 0xf0));

  temp = 0x01;
  if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) {
     if(SiS_Pr->SiS_ModeType == ModeEGA) {
        if(SiS_Pr->SiS_VGAHDE >= 1024) {
           temp = 0x02;
           if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
              temp = 0x01;
	   }
        }
     }
  }
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x0B,temp);

  tempbx = SiS_Pr->SiS_VDE - 1;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x03,tempbx);
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0C,0xF8,((tempbx >> 8) & 0x07));

  tempcx = SiS_Pr->SiS_VT - 1;
  SiS_SetReg(SiS_Pr->SiS_Part2Port,0x19,tempcx);
  temp = (tempcx >> 3) & 0xE0;
  if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
     /* Enable dithering; only do this for 32bpp mode */
     if(SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x01) {
        temp |= 0x10;
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1A,0x0f,temp);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x09,0xF0);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x0A,0xF0);

  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x17,0xFB);
  SiS_SetRegAND(SiS_Pr->SiS_Part2Port,0x18,0xDF);

#ifdef CONFIG_FB_SIS_315
  if(SiS_GetCRT2Part2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                          			&CRT2Index, &resindex)) {
      switch(CRT2Index) {
        case 206: CRT2Part2Ptr = SiS310_CRT2Part2_Asus1024x768_3;    break;
	default:
        case 200: CRT2Part2Ptr = SiS_Pr->SiS_CRT2Part2_1024x768_1;   break;
      }

      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,(CRT2Part2Ptr+resindex)->CR[0]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x02,0x80,(CRT2Part2Ptr+resindex)->CR[1]);
      for(i = 2, j = 0x04; j <= 0x06; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1c; j <= 0x1d; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      for(j = 0x1f; j <= 0x21; i++, j++ ) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,j,(CRT2Part2Ptr+resindex)->CR[i]);
      }
      SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,(CRT2Part2Ptr+resindex)->CR[10]);
      SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0x0f,(CRT2Part2Ptr+resindex)->CR[11]);

      SiS_SetGroup2_Tail(SiS_Pr, ModeNo);

  } else {
#endif

    /* Checked for 1024x768, 1280x1024, 1400x1050, 1600x1200 */
    /*             Clevo dual-link 1024x768 */
    /* 		   Compaq 1280x1024 has HT 1696 sometimes (calculation OK, if given HT is correct)  */
    /*		   Acer: OK, but uses different setting for VESA timing at 640/800/1024 and 640x400 */

    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
       if((SiS_Pr->SiS_LCDInfo & LCDPass11) || (SiS_Pr->PanelYRes == SiS_Pr->SiS_VDE)) {
          tempbx = SiS_Pr->SiS_VDE - 1;
          tempcx = SiS_Pr->SiS_VT - 1;
       } else {
          tempbx = SiS_Pr->SiS_VDE + ((SiS_Pr->PanelYRes - SiS_Pr->SiS_VDE) / 2);
	  tempcx = SiS_Pr->SiS_VT - ((SiS_Pr->PanelYRes - SiS_Pr->SiS_VDE) / 2);
       }
    } else {
       tempbx = SiS_Pr->PanelYRes;
       tempcx = SiS_Pr->SiS_VT;
       tempax = 1;
       if(SiS_Pr->PanelYRes != SiS_Pr->SiS_VDE) {
          tempax = SiS_Pr->PanelYRes;
	  /* if(SiS_Pr->SiS_VGAVDE == 525) tempax += 0x3c;   */  /* 651+301C */
          if(SiS_Pr->PanelYRes < SiS_Pr->SiS_VDE) {
             tempax = tempcx = 0;
          } else {
             tempax -= SiS_Pr->SiS_VDE;
          }
          tempax >>= 1;
       }
       tempcx -= tempax; /* lcdvdes */
       tempbx -= tempax; /* lcdvdee */
    }

    /* Non-expanding: lcdvdes = tempcx = VT-1; lcdvdee = tempbx = VDE-1 */

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x05,tempcx);	/* lcdvdes  */
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x06,tempbx);	/* lcdvdee  */

    temp = (tempbx >> 5) & 0x38;
    temp |= ((tempcx >> 8) & 0x07);
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x02,temp);

    tempax = SiS_Pr->SiS_VDE;
    if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
       tempax = SiS_Pr->PanelYRes;
    }
    tempcx = (SiS_Pr->SiS_VT - tempax) >> 4;
    if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
       if(SiS_Pr->PanelYRes != SiS_Pr->SiS_VDE) {
	  tempcx = (SiS_Pr->SiS_VT - tempax) / 10;
       }
    }

    tempbx = ((SiS_Pr->SiS_VT + SiS_Pr->SiS_VDE) >> 1) - 1;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
       if(SiS_Pr->PanelYRes != SiS_Pr->SiS_VDE) {
          if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) { /* ? */
             tempax = SiS_Pr->SiS_VT - SiS_Pr->PanelYRes;
	     if(tempax % 4) { tempax >>= 2; tempax++; }
	     else           { tempax >>= 2;           }
             tempbx -= (tempax - 1);
	  } else {
	     tempbx -= 10;
	     if(tempbx <= SiS_Pr->SiS_VDE) tempbx = SiS_Pr->SiS_VDE + 1;
	  }
       }
    }
    if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
       tempbx++;
       if((!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) || (crt2crtc == 6)) {
          if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
	     tempbx = 770;
	     tempcx = 3;
	  }
       }
    }

    /* non-expanding: lcdvrs = ((VT + VDE) / 2) - 10 */

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CVSyncStart;
    }

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,tempbx);	    /* lcdvrs */

    temp = (tempbx >> 4) & 0xF0;
    tempbx += (tempcx + 1);
    temp |= (tempbx & 0x0F);

    if(SiS_Pr->UseCustomMode) {
       temp &= 0xf0;
       temp |= (SiS_Pr->CVSyncEnd & 0x0f);
    }

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x01,temp);

#ifdef CONFIG_FB_SIS_300
    SiS_Group2LCDSpecial(SiS_Pr, ModeNo, crt2crtc);
#endif

    bridgeoffset = 7;
    if(SiS_Pr->SiS_VBType & VB_SIS30xBLV)	bridgeoffset += 2;
    if(SiS_Pr->SiS_VBType & VB_SIS30xCLV)	bridgeoffset += 2; /* OK for Averatec 1280x800 (301C) */
    if(SiS_IsDualLink(SiS_Pr))			bridgeoffset++;
    else if(SiS_Pr->SiS_VBType & VB_SIS302LV)	bridgeoffset++;    /* OK for Asus A4L 1280x800 */
    /* Higher bridgeoffset shifts to the LEFT */

    temp = 0;
    if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
       if(SiS_Pr->PanelXRes != SiS_Pr->SiS_HDE) {
	  temp = SiS_Pr->SiS_HT - ((SiS_Pr->PanelXRes - SiS_Pr->SiS_HDE) / 2);
	  if(SiS_IsDualLink(SiS_Pr)) temp >>= 1;
       }
    }
    temp += bridgeoffset;
    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1F,temp);  	     /* lcdhdes */
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x20,0x0F,((temp >> 4) & 0xf0));

    tempcx = SiS_Pr->SiS_HT;
    tempax = tempbx = SiS_Pr->SiS_HDE;
    if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
       if(SiS_Pr->PanelXRes != SiS_Pr->SiS_HDE) {
          tempax = SiS_Pr->PanelXRes;
          tempbx = SiS_Pr->PanelXRes - ((SiS_Pr->PanelXRes - SiS_Pr->SiS_HDE) / 2);
       }
    }
    if(SiS_IsDualLink(SiS_Pr)) {
       tempcx >>= 1;
       tempbx >>= 1;
       tempax >>= 1;
    }

    tempbx += bridgeoffset;

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x23,tempbx);	    /* lcdhdee */
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x25,0xF0,((tempbx >> 8) & 0x0f));

    tempcx = (tempcx - tempax) >> 2;

    tempbx += tempcx;
    push2 = tempbx;

    if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) {
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
          if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
             if(SiS_Pr->SiS_HDE == 1280) tempbx = (tempbx & 0xff00) | 0x47;
	  }
       }
    }

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CHSyncStart;
       if(modeflag & HalfDCLK) tempbx <<= 1;
       if(SiS_IsDualLink(SiS_Pr)) tempbx >>= 1;
       tempbx += bridgeoffset;
    }

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1C,tempbx);	    /* lcdhrs */
    SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1D,0x0F,((tempbx >> 4) & 0xf0));

    tempbx = push2;

    tempcx <<= 1;
    if((SiS_Pr->SiS_LCDInfo & DontExpandLCD) && (!(SiS_Pr->SiS_LCDInfo & LCDPass11))) {
       if(SiS_Pr->PanelXRes != SiS_Pr->SiS_HDE) tempcx >>= 2;
    }
    tempbx += tempcx;

    if(SiS_Pr->UseCustomMode) {
       tempbx = SiS_Pr->CHSyncEnd;
       if(modeflag & HalfDCLK) tempbx <<= 1;
       if(SiS_IsDualLink(SiS_Pr)) tempbx >>= 1;
       tempbx += bridgeoffset;
    }

    SiS_SetReg(SiS_Pr->SiS_Part2Port,0x21,tempbx);	    /* lcdhre */

    SiS_SetGroup2_Tail(SiS_Pr, ModeNo);

#ifdef CONFIG_FB_SIS_300
    SiS_Set300Part2Regs(SiS_Pr, ModeIdIndex, RefreshRateTableIndex, ModeNo);
#endif
#ifdef CONFIG_FB_SIS_315
  } /* CRT2-LCD from table */
#endif
}

/*********************************************/
/*         SET PART 3 REGISTER GROUP         */
/*********************************************/

static void
SiS_SetGroup3(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned short i;
  const unsigned char *tempdi;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) return;

#ifndef SIS_CP
  SiS_SetReg(SiS_Pr->SiS_Part3Port,0x00,0x00);
#else
  SIS_CP_INIT301_CP
#endif

  if(SiS_Pr->SiS_TVMode & TVSetPAL) {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xFA);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xC8);
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xF5);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xB7);
  }

  if(SiS_Pr->SiS_TVMode & TVSetPALM) {
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x13,0xFA);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x14,0xC8);
     SiS_SetReg(SiS_Pr->SiS_Part3Port,0x3D,0xA8);
  }

  tempdi = NULL;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     tempdi = SiS_Pr->SiS_HiTVGroup3Data;
     if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) {
        tempdi = SiS_Pr->SiS_HiTVGroup3Simu;
     }
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) {
     if(!(SiS_Pr->SiS_TVMode & TVSetYPbPr525i)) {
        tempdi = SiS_HiTVGroup3_1;
        if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) tempdi = SiS_HiTVGroup3_2;
     }
  }
  if(tempdi) {
     for(i=0; i<=0x3E; i++) {
        SiS_SetReg(SiS_Pr->SiS_Part3Port,i,tempdi[i]);
     }
     if(SiS_Pr->SiS_VBType & VB_SIS30xCLV) {
	if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) {
	   SiS_SetReg(SiS_Pr->SiS_Part3Port,0x28,0x3f);
	}
     }
  }

#ifdef SIS_CP
  SIS_CP_INIT301_CP2
#endif
}

/*********************************************/
/*         SET PART 4 REGISTER GROUP         */
/*********************************************/

#ifdef CONFIG_FB_SIS_315
#if 0
static void
SiS_ShiftXPos(struct SiS_Private *SiS_Pr, int shift)
{
   unsigned short temp, temp1, temp2;

   temp1 = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x1f);
   temp2 = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x20);
   temp = (unsigned short)((int)((temp1 | ((temp2 & 0xf0) << 4))) + shift);
   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x1f,temp);
   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x20,0x0f,((temp >> 4) & 0xf0));
   temp = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x2b) & 0x0f;
   temp = (unsigned short)((int)(temp) + shift);
   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x2b,0xf0,(temp & 0x0f));
   temp1 = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x43);
   temp2 = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x42);
   temp = (unsigned short)((int)((temp1 | ((temp2 & 0xf0) << 4))) + shift);
   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x43,temp);
   SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x42,0x0f,((temp >> 4) & 0xf0));
}
#endif

static void
SiS_SetGroup4_C_ELV(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short temp, temp1;
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;

   if(!(SiS_Pr->SiS_VBType & VB_SIS30xCLV)) return;
   if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToHiVision | SetCRT2ToYPbPr525750))) return;

   if(SiS_Pr->ChipType >= XGI_20) return;

   if((SiS_Pr->ChipType >= SIS_661) && (SiS_Pr->SiS_ROMNew)) {
      if(!(ROMAddr[0x61] & 0x04)) return;
   }

   SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x3a,0x08);
   temp = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x3a);
   if(!(temp & 0x01)) {
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x3a,0xdf);
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x25,0xfc);
      if((SiS_Pr->ChipType < SIS_661) && (!(SiS_Pr->SiS_ROMNew))) {
         SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x25,0xf8);
      }
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x0f,0xfb);
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p)      temp = 0x0000;
      else if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) temp = 0x0002;
      else if(SiS_Pr->SiS_TVMode & TVSetHiVision)  temp = 0x0400;
      else					   temp = 0x0402;
      if((SiS_Pr->ChipType >= SIS_661) || (SiS_Pr->SiS_ROMNew)) {
         temp1 = 0;
	 if(SiS_Pr->SiS_TVMode & TVAspect43) temp1 = 4;
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0f,0xfb,temp1);
	 if(SiS_Pr->SiS_TVMode & TVAspect43LB) temp |= 0x01;
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0x7c,(temp & 0xff));
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x3a,0xfb,(temp >> 8));
	 if(ModeNo > 0x13) {
            SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x39,0xfd);
         }
      } else {
         temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x3b) & 0x03;
	 if(temp1 == 0x01) temp |= 0x01;
	 if(temp1 == 0x03) temp |= 0x04;  /* ? why not 0x10? */
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x26,0xf8,(temp & 0xff));
	 SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x3a,0xfb,(temp >> 8));
	 if(ModeNo > 0x13) {
            SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x3b,0xfd);
         }
      }

#if 0
      if(SiS_Pr->ChipType >= SIS_661) { 		/* ? */
         if(SiS_Pr->SiS_TVMode & TVAspect43) {
            if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) {
	       if(resinfo == SIS_RI_1024x768) {
	          SiS_ShiftXPos(SiS_Pr, 97);
	       } else {
	          SiS_ShiftXPos(SiS_Pr, 111);
	       }
	    } else if(SiS_Pr->SiS_TVMode & TVSetHiVision) {
	       SiS_ShiftXPos(SiS_Pr, 136);
	    }
         }
      }
#endif

   }

}
#endif

static void
SiS_SetCRT2VCLK(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
                 unsigned short RefreshRateTableIndex)
{
  unsigned short vclkindex, temp, reg1, reg2;

  if(SiS_Pr->UseCustomMode) {
     reg1 = SiS_Pr->CSR2B;
     reg2 = SiS_Pr->CSR2C;
  } else {
     vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
     reg1 = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_A;
     reg2 = SiS_Pr->SiS_VBVCLKData[vclkindex].Part4_B;
  }

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
     if(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSet525p1024)) {
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,0x57);
 	SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,0x46);
	SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1f,0xf6);
     } else {
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,reg1);
        SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,reg2);
     }
  } else {
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,0x01);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0b,reg2);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0a,reg1);
  }
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x12,0x00);
  temp = 0x08;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) temp |= 0x20;
  SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x12,temp);
}

static void
SiS_SetDualLinkEtc(struct SiS_Private *SiS_Pr)
{
  if(SiS_Pr->ChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBType & VB_SISDUALLINK) {
	if((SiS_CRT2IsLCD(SiS_Pr)) ||
	   (SiS_IsVAMode(SiS_Pr))) {
	   if(SiS_Pr->SiS_LCDInfo & LCDDualLink) {
	      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x27,0x2c);
	   } else {
	      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x27,~0x20);
	   }
	}
     }
  }
  if(SiS_Pr->SiS_VBType & VB_SISEMI) {
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2a,0x00);
#ifdef SET_EMI
     SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
#endif
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
  }
}

static void
SiS_SetGroup4(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		unsigned short RefreshRateTableIndex)
{
  unsigned short tempax, tempcx, tempbx, modeflag, temp, resinfo;
  unsigned int   tempebx, tempeax, templong;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
     resinfo = 0;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  if(SiS_Pr->ChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x24,0x0e);
	}
     }
  }

  if(SiS_Pr->SiS_VBType & (VB_SIS30xCLV | VB_SIS302LV)) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x10,0x9f);
     }
  }

  if(SiS_Pr->ChipType >= SIS_315H) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	SiS_SetDualLinkEtc(SiS_Pr);
	return;
     }
  }

  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x13,SiS_Pr->SiS_RVBHCFACT);

  tempbx = SiS_Pr->SiS_RVBHCMAX;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x14,tempbx);

  temp = (tempbx >> 1) & 0x80;

  tempcx = SiS_Pr->SiS_VGAHT - 1;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x16,tempcx);

  temp |= ((tempcx >> 5) & 0x78);

  tempcx = SiS_Pr->SiS_VGAVT - 1;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) tempcx -= 5;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x17,tempcx);

  temp |= ((tempcx >> 8) & 0x07);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x15,temp);

  tempbx = SiS_Pr->SiS_VGAHDE;
  if(modeflag & HalfDCLK)    tempbx >>= 1;
  if(SiS_IsDualLink(SiS_Pr)) tempbx >>= 1;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     temp = 0;
     if(tempbx > 800)        temp = 0x60;
  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     temp = 0;
     if(tempbx > 1024)       temp = 0xC0;
     else if(tempbx >= 960)  temp = 0xA0;
  } else if(SiS_Pr->SiS_TVMode & (TVSetYPbPr525p | TVSetYPbPr750p)) {
     temp = 0;
     if(tempbx >= 1280)      temp = 0x40;
     else if(tempbx >= 1024) temp = 0x20;
  } else {
     temp = 0x80;
     if(tempbx >= 1024)      temp = 0xA0;
  }

  temp |= SiS_Pr->Init_P4_0E;

  if(SiS_Pr->SiS_VBType & VB_SIS301) {
     if(SiS_Pr->SiS_LCDResInfo != Panel_1280x1024) {
        temp &= 0xf0;
        temp |= 0x0A;
     }
  }

  SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0E,0x10,temp);

  tempeax = SiS_Pr->SiS_VGAVDE;
  tempebx = SiS_Pr->SiS_VDE;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
     if(!(temp & 0xE0)) tempebx >>=1;
  }

  tempcx = SiS_Pr->SiS_RVBHRS;
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x18,tempcx);
  tempcx >>= 8;
  tempcx |= 0x40;

  if(tempeax <= tempebx) {
     tempcx ^= 0x40;
  } else {
     tempeax -= tempebx;
  }

  tempeax *= (256 * 1024);
  templong = tempeax % tempebx;
  tempeax /= tempebx;
  if(templong) tempeax++;

  temp = (unsigned short)(tempeax & 0x000000FF);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1B,temp);
  temp = (unsigned short)((tempeax & 0x0000FF00) >> 8);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1A,temp);
  temp = (unsigned short)((tempeax >> 12) & 0x70); /* sic! */
  temp |= (tempcx & 0x4F);
  SiS_SetReg(SiS_Pr->SiS_Part4Port,0x19,temp);

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {

     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1C,0x28);

     /* Calc Linebuffer max address and set/clear decimode */
     tempbx = 0;
     if(SiS_Pr->SiS_TVMode & (TVSetHiVision | TVSetYPbPr750p)) tempbx = 0x08;
     tempax = SiS_Pr->SiS_VGAHDE;
     if(modeflag & HalfDCLK)    tempax >>= 1;
     if(SiS_IsDualLink(SiS_Pr)) tempax >>= 1;
     if(tempax > 800) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	   tempax -= 800;
	} else {
	   tempbx = 0x08;
	   if(tempax == 960)	   tempax *= 25; /* Correct */
           else if(tempax == 1024) tempax *= 25;
           else			   tempax *= 20;
	   temp = tempax % 32;
	   tempax /= 32;
	   if(temp) tempax++;
	   tempax++;
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	      if(resinfo == SIS_RI_1024x768 ||
	         resinfo == SIS_RI_1024x576 ||
		 resinfo == SIS_RI_1280x1024 ||
		 resinfo == SIS_RI_1280x720) {
	         /* Otherwise white line or garbage at right edge */
	         tempax = (tempax & 0xff00) | 0x20;
	      }
	   }
	}
     }
     tempax--;
     temp = ((tempax >> 4) & 0x30) | tempbx;
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1D,tempax);
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x1E,temp);

     temp = 0x0036; tempbx = 0xD0;
     if((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SISLVDS)) {
	temp = 0x0026; tempbx = 0xC0; /* See En/DisableBridge() */
     }
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        if(!(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSetHiVision | TVSetYPbPr750p | TVSetYPbPr525p))) {
	   temp |= 0x01;
	   if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	      if(!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
  	         temp &= ~0x01;
	      }
	   }
	}
     }
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x1F,tempbx,temp);

     tempbx = SiS_Pr->SiS_HT >> 1;
     if(SiS_IsDualLink(SiS_Pr)) tempbx >>= 1;
     tempbx -= 2;
     SiS_SetReg(SiS_Pr->SiS_Part4Port,0x22,tempbx);
     temp = (tempbx >> 5) & 0x38;
     SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x21,0xC0,temp);

     if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
           SiS_SetReg(SiS_Pr->SiS_Part4Port,0x24,0x0e);
	   /* LCD-too-dark-error-source, see FinalizeLCD() */
	}
     }

     SiS_SetDualLinkEtc(SiS_Pr);

  }  /* 301B */

  SiS_SetCRT2VCLK(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
}

/*********************************************/
/*         SET PART 5 REGISTER GROUP         */
/*********************************************/

static void
SiS_SetGroup5(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)  return;

  if(SiS_Pr->SiS_ModeType == ModeVGA) {
     if(!(SiS_Pr->SiS_VBInfo & (SetInSlaveMode | LoadDACFlag))) {
        SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x20);
        SiS_LoadDAC(SiS_Pr, ModeNo, ModeIdIndex);
     }
  }
}

/*********************************************/
/*     MODIFY CRT1 GROUP FOR SLAVE MODE      */
/*********************************************/

static bool
SiS_GetLVDSCRT1Ptr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		   unsigned short RefreshRateTableIndex, unsigned short *ResIndex,
		   unsigned short *DisplayType)
 {
  unsigned short modeflag = 0;
  bool checkhd = true;

  /* Pass 1:1 not supported here */

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     (*ResIndex) = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     (*ResIndex) = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;
  }

  (*ResIndex) &= 0x3F;

  if((SiS_Pr->SiS_IF_DEF_CH70xx) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {

     (*DisplayType) = 80;
     if((SiS_Pr->SiS_TVMode & TVSetPAL) && (!(SiS_Pr->SiS_TVMode & TVSetPALM))) {
      	(*DisplayType) = 82;
	if(SiS_Pr->SiS_ModeType > ModeVGA) {
	   if(SiS_Pr->SiS_CHSOverScan) (*DisplayType) = 84;
	}
     }
     if((*DisplayType) != 84) {
        if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) (*DisplayType)++;
     }

  } else {

     (*DisplayType = 0);
     switch(SiS_Pr->SiS_LCDResInfo) {
     case Panel_320x240_1: (*DisplayType) = 50;
			   checkhd = false;
			   break;
     case Panel_320x240_2: (*DisplayType) = 14;
			   break;
     case Panel_320x240_3: (*DisplayType) = 18;
			   break;
     case Panel_640x480:   (*DisplayType) = 10;
			   break;
     case Panel_1024x600:  (*DisplayType) = 26;
			   break;
     default: return true;
     }

     if(checkhd) {
        if(modeflag & HalfDCLK) (*DisplayType)++;
     }

     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x600) {
        if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) (*DisplayType) += 2;
     }

  }

  return true;
}

static void
SiS_ModCRT1CRTC(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
                unsigned short RefreshRateTableIndex)
{
  unsigned short tempah, i, modeflag, j, ResIndex, DisplayType;
  const struct SiS_LVDSCRT1Data *LVDSCRT1Ptr=NULL;
  static const unsigned short CRIdx[] = {
	0x00, 0x02, 0x03, 0x04, 0x05, 0x06,
	0x07, 0x10, 0x11, 0x15, 0x16
  };

  if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
     (SiS_Pr->SiS_CustomT == CUT_BARCO1024) ||
     (SiS_Pr->SiS_CustomT == CUT_PANEL848)  ||
     (SiS_Pr->SiS_CustomT == CUT_PANEL856) )
     return;

  if(SiS_Pr->SiS_IF_DEF_LVDS) {
     if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return;
     }
  } else if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) return;
  } else return;

  if(SiS_Pr->SiS_LCDInfo & LCDPass11) return;

  if(SiS_Pr->ChipType < SIS_315H) {
     if(SiS_Pr->SiS_SetFlag & SetDOSMode) return;
  }

  if(!(SiS_GetLVDSCRT1Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex,
                          &ResIndex, &DisplayType))) {
     return;
  }

  switch(DisplayType) {
    case 50: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x240_1;           break; /* xSTN */
    case 14: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x240_2;           break; /* xSTN */
    case 15: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x240_2_H;         break; /* xSTN */
    case 18: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x240_3;           break; /* xSTN */
    case 19: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1320x240_3_H;         break; /* xSTN */
    case 10: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_1;           break;
    case 11: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT1640x480_1_H;         break;
#if 0 /* Works better with calculated numbers */
    case 26: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1;          break;
    case 27: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_1_H;        break;
    case 28: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2;          break;
    case 29: LVDSCRT1Ptr = SiS_Pr->SiS_LVDSCRT11024x600_2_H;        break;
#endif
    case 80: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UNTSC;               break;
    case 81: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1ONTSC;               break;
    case 82: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1UPAL;                break;
    case 83: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1OPAL;                break;
    case 84: LVDSCRT1Ptr = SiS_Pr->SiS_CHTVCRT1SOPAL;               break;
  }

  if(LVDSCRT1Ptr) {

     SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);

     for(i = 0; i <= 10; i++) {
        tempah = (LVDSCRT1Ptr + ResIndex)->CR[i];
        SiS_SetReg(SiS_Pr->SiS_P3d4,CRIdx[i],tempah);
     }

     for(i = 0x0A, j = 11; i <= 0x0C; i++, j++) {
        tempah = (LVDSCRT1Ptr + ResIndex)->CR[j];
        SiS_SetReg(SiS_Pr->SiS_P3c4,i,tempah);
     }

     tempah = (LVDSCRT1Ptr + ResIndex)->CR[14] & 0xE0;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0x1f,tempah);

     if(ModeNo <= 0x13) modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     else               modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

     tempah = ((LVDSCRT1Ptr + ResIndex)->CR[14] & 0x01) << 5;
     if(modeflag & DoubleScanMode) tempah |= 0x80;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,~0x020,tempah);

  } else {

     SiS_CalcLCDACRT1Timing(SiS_Pr, ModeNo, ModeIdIndex);

  }
}

/*********************************************/
/*              SET CRT2 ECLK                */
/*********************************************/

static void
SiS_SetCRT2ECLK(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
           unsigned short RefreshRateTableIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short clkbase, vclkindex = 0;
  unsigned char  sr2b, sr2c;

  if(SiS_Pr->SiS_LCDInfo & LCDPass11) {
     SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);
     if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK == 2) {
	RefreshRateTableIndex--;
     }
     vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex,
                                    RefreshRateTableIndex);
     SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
  } else {
     vclkindex = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex,
                                    RefreshRateTableIndex);
  }

  sr2b = SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
  sr2c = SiS_Pr->SiS_VCLKData[vclkindex].SR2C;

  if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) || (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
     if(SiS_Pr->SiS_UseROM) {
	if(ROMAddr[0x220] & 0x01) {
	   sr2b = ROMAddr[0x227];
	   sr2c = ROMAddr[0x228];
	}
     }
  }

  clkbase = 0x02B;
  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
     if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) {
	clkbase += 3;
     }
  }

  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x20);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x10);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x00);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase,sr2b);
  SiS_SetReg(SiS_Pr->SiS_P3c4,clkbase+1,sr2c);
}

/*********************************************/
/*           SET UP CHRONTEL CHIPS           */
/*********************************************/

static void
SiS_SetCHTVReg(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
               unsigned short RefreshRateTableIndex)
{
   unsigned short TVType, resindex;
   const struct SiS_CHTVRegData *CHTVRegData = NULL;

   if(ModeNo <= 0x13)
      resindex = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
   else
      resindex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT2CRTC;

   resindex &= 0x3F;

   TVType = 0;
   if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
   if(SiS_Pr->SiS_TVMode & TVSetPAL) {
      TVType += 2;
      if(SiS_Pr->SiS_ModeType > ModeVGA) {
	 if(SiS_Pr->SiS_CHSOverScan) TVType = 8;
      }
      if(SiS_Pr->SiS_TVMode & TVSetPALM) {
	 TVType = 4;
	 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
      } else if(SiS_Pr->SiS_TVMode & TVSetPALN) {
	 TVType = 6;
	 if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) TVType += 1;
      }
   }

   switch(TVType) {
      case  0: CHTVRegData = SiS_Pr->SiS_CHTVReg_UNTSC; break;
      case  1: CHTVRegData = SiS_Pr->SiS_CHTVReg_ONTSC; break;
      case  2: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPAL;  break;
      case  3: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
      case  4: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALM; break;
      case  5: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALM; break;
      case  6: CHTVRegData = SiS_Pr->SiS_CHTVReg_UPALN; break;
      case  7: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPALN; break;
      case  8: CHTVRegData = SiS_Pr->SiS_CHTVReg_SOPAL; break;
      default: CHTVRegData = SiS_Pr->SiS_CHTVReg_OPAL;  break;
   }


   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {

#ifdef CONFIG_FB_SIS_300

      /* Chrontel 7005 - I assume that it does not come with a 315 series chip */

      /* We don't support modes >800x600 */
      if (resindex > 5) return;

      if(SiS_Pr->SiS_TVMode & TVSetPAL) {
	 SiS_SetCH700x(SiS_Pr,0x04,0x43);  /* 0x40=76uA (PAL); 0x03=15bit non-multi RGB*/
	 SiS_SetCH700x(SiS_Pr,0x09,0x69);  /* Black level for PAL (105)*/
      } else {
	 SiS_SetCH700x(SiS_Pr,0x04,0x03);   /* upper nibble=71uA (NTSC), 0x03=15bit non-multi RGB*/
	 SiS_SetCH700x(SiS_Pr,0x09,0x71);   /* Black level for NTSC (113)*/
      }

      SiS_SetCH700x(SiS_Pr,0x00,CHTVRegData[resindex].Reg[0]);	/* Mode register */
      SiS_SetCH700x(SiS_Pr,0x07,CHTVRegData[resindex].Reg[1]);	/* Start active video register */
      SiS_SetCH700x(SiS_Pr,0x08,CHTVRegData[resindex].Reg[2]);	/* Position overflow register */
      SiS_SetCH700x(SiS_Pr,0x0a,CHTVRegData[resindex].Reg[3]);	/* Horiz Position register */
      SiS_SetCH700x(SiS_Pr,0x0b,CHTVRegData[resindex].Reg[4]);	/* Vertical Position register */

      /* Set minimum flicker filter for Luma channel (SR1-0=00),
                minimum text enhancement (S3-2=10),
   	        maximum flicker filter for Chroma channel (S5-4=10)
	        =00101000=0x28 (When reading, S1-0->S3-2, and S3-2->S1-0!)
       */
      SiS_SetCH700x(SiS_Pr,0x01,0x28);

      /* Set video bandwidth
            High bandwidth Luma composite video filter(S0=1)
            low bandwidth Luma S-video filter (S2-1=00)
	    disable peak filter in S-video channel (S3=0)
	    high bandwidth Chroma Filter (S5-4=11)
	    =00110001=0x31
      */
      SiS_SetCH700x(SiS_Pr,0x03,0xb1);       /* old: 3103 */

      /* Register 0x3D does not exist in non-macrovision register map
            (Maybe this is a macrovision register?)
       */
#ifndef SIS_CP
      SiS_SetCH70xx(SiS_Pr,0x3d,0x00);
#endif

      /* Register 0x10 only contains 1 writable bit (S0) for sensing,
             all other bits a read-only. Macrovision?
       */
      SiS_SetCH70xxANDOR(SiS_Pr,0x10,0x00,0x1F);

      /* Register 0x11 only contains 3 writable bits (S0-S2) for
             contrast enhancement (set to 010 -> gain 1 Yout = 17/16*(Yin-30) )
       */
      SiS_SetCH70xxANDOR(SiS_Pr,0x11,0x02,0xF8);

      /* Clear DSEN
       */
      SiS_SetCH70xxANDOR(SiS_Pr,0x1c,0x00,0xEF);

      if(!(SiS_Pr->SiS_TVMode & TVSetPAL)) {		/* ---- NTSC ---- */
         if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) {
            if(resindex == 0x04) {   			/* 640x480 overscan: Mode 16 */
      	       SiS_SetCH70xxANDOR(SiS_Pr,0x20,0x00,0xEF);	/* loop filter off */
               SiS_SetCH70xxANDOR(SiS_Pr,0x21,0x01,0xFE);	/* ACIV on, no need to set FSCI */
            } else if(resindex == 0x05) {    		/* 800x600 overscan: Mode 23 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x18,0x01,0xF0);	/* 0x18-0x1f: FSCI 469,762,048 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x19,0x0C,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1a,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1b,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1c,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1d,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1e,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1f,0x00,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x20,0x01,0xEF);	/* Loop filter on for mode 23 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x21,0x00,0xFE);	/* ACIV off, need to set FSCI */
            }
         } else {
            if(resindex == 0x04) {     			/* ----- 640x480 underscan; Mode 17 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x20,0x00,0xEF);	/* loop filter off */
               SiS_SetCH70xxANDOR(SiS_Pr,0x21,0x01,0xFE);
            } else if(resindex == 0x05) {   		/* ----- 800x600 underscan: Mode 24 */
#if 0
               SiS_SetCH70xxANDOR(SiS_Pr,0x18,0x01,0xF0);	/* (FSCI was 0x1f1c71c7 - this is for mode 22) */
               SiS_SetCH70xxANDOR(SiS_Pr,0x19,0x09,0xF0);	/* FSCI for mode 24 is 428,554,851 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x1a,0x08,0xF0);       /* 198b3a63 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x1b,0x0b,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1c,0x04,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1d,0x01,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1e,0x06,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x1f,0x05,0xF0);
               SiS_SetCH70xxANDOR(SiS_Pr,0x20,0x00,0xEF);	/* loop filter off for mode 24 */
               SiS_SetCH70xxANDOR(SiS_Pr,0x21,0x00,0xFE);	* ACIV off, need to set FSCI */
#endif         /* All alternatives wrong (datasheet wrong?), don't use FSCI */
	       SiS_SetCH70xxANDOR(SiS_Pr,0x20,0x00,0xEF);	 /* loop filter off */
               SiS_SetCH70xxANDOR(SiS_Pr,0x21,0x01,0xFE);
            }
         }
      } else {						/* ---- PAL ---- */
	/* We don't play around with FSCI in PAL mode */
	SiS_SetCH70xxANDOR(SiS_Pr, 0x20, 0x00, 0xEF);	/* loop filter off */
	SiS_SetCH70xxANDOR(SiS_Pr, 0x21, 0x01, 0xFE);	/* ACIV on */
      }

#endif  /* 300 */

   } else {

      /* Chrontel 7019 - assumed that it does not come with a 300 series chip */

#ifdef CONFIG_FB_SIS_315

      unsigned short temp;

      /* We don't support modes >1024x768 */
      if (resindex > 6) return;

      temp = CHTVRegData[resindex].Reg[0];
      if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) temp |= 0x10;
      SiS_SetCH701x(SiS_Pr,0x00,temp);

      SiS_SetCH701x(SiS_Pr,0x01,CHTVRegData[resindex].Reg[1]);
      SiS_SetCH701x(SiS_Pr,0x02,CHTVRegData[resindex].Reg[2]);
      SiS_SetCH701x(SiS_Pr,0x04,CHTVRegData[resindex].Reg[3]);
      SiS_SetCH701x(SiS_Pr,0x03,CHTVRegData[resindex].Reg[4]);
      SiS_SetCH701x(SiS_Pr,0x05,CHTVRegData[resindex].Reg[5]);
      SiS_SetCH701x(SiS_Pr,0x06,CHTVRegData[resindex].Reg[6]);

      temp = CHTVRegData[resindex].Reg[7];
      if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) temp = 0x66;
      SiS_SetCH701x(SiS_Pr,0x07,temp);

      SiS_SetCH701x(SiS_Pr,0x08,CHTVRegData[resindex].Reg[8]);
      SiS_SetCH701x(SiS_Pr,0x15,CHTVRegData[resindex].Reg[9]);
      SiS_SetCH701x(SiS_Pr,0x1f,CHTVRegData[resindex].Reg[10]);
      SiS_SetCH701x(SiS_Pr,0x0c,CHTVRegData[resindex].Reg[11]);
      SiS_SetCH701x(SiS_Pr,0x0d,CHTVRegData[resindex].Reg[12]);
      SiS_SetCH701x(SiS_Pr,0x0e,CHTVRegData[resindex].Reg[13]);
      SiS_SetCH701x(SiS_Pr,0x0f,CHTVRegData[resindex].Reg[14]);
      SiS_SetCH701x(SiS_Pr,0x10,CHTVRegData[resindex].Reg[15]);

      temp = SiS_GetCH701x(SiS_Pr,0x21) & ~0x02;
      /* D1 should be set for PAL, PAL-N and NTSC-J,
         but I won't do that for PAL unless somebody
	 tells me to do so. Since the BIOS uses
	 non-default CIV values and blacklevels,
	 this might be compensated anyway.
       */
      if(SiS_Pr->SiS_TVMode & (TVSetPALN | TVSetNTSCJ)) temp |= 0x02;
      SiS_SetCH701x(SiS_Pr,0x21,temp);

#endif	/* 315 */

   }

#ifdef SIS_CP
   SIS_CP_INIT301_CP3
#endif

}

#ifdef CONFIG_FB_SIS_315  /* ----------- 315 series only ---------- */

void
SiS_Chrontel701xBLOn(struct SiS_Private *SiS_Pr)
{
   unsigned short temp;

   /* Enable Chrontel 7019 LCD panel backlight */
   if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
      if(SiS_Pr->ChipType == SIS_740) {
	 SiS_SetCH701x(SiS_Pr,0x66,0x65);
      } else {
	 temp = SiS_GetCH701x(SiS_Pr,0x66);
	 temp |= 0x20;
	 SiS_SetCH701x(SiS_Pr,0x66,temp);
      }
   }
}

void
SiS_Chrontel701xBLOff(struct SiS_Private *SiS_Pr)
{
   unsigned short temp;

   /* Disable Chrontel 7019 LCD panel backlight */
   if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
      temp = SiS_GetCH701x(SiS_Pr,0x66);
      temp &= 0xDF;
      SiS_SetCH701x(SiS_Pr,0x66,temp);
   }
}

static void
SiS_ChrontelPowerSequencing(struct SiS_Private *SiS_Pr)
{
  static const unsigned char regtable[]      = { 0x67, 0x68, 0x69, 0x6a, 0x6b };
  static const unsigned char table1024_740[] = { 0x01, 0x02, 0x01, 0x01, 0x01 };
  static const unsigned char table1400_740[] = { 0x01, 0x6e, 0x01, 0x01, 0x01 };
  static const unsigned char asus1024_740[]  = { 0x19, 0x6e, 0x01, 0x19, 0x09 };
  static const unsigned char asus1400_740[]  = { 0x19, 0x6e, 0x01, 0x19, 0x09 };
  static const unsigned char table1024_650[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  static const unsigned char table1400_650[] = { 0x01, 0x02, 0x01, 0x01, 0x02 };
  const unsigned char *tableptr = NULL;
  int i;

  /* Set up Power up/down timing */

  if(SiS_Pr->ChipType == SIS_740) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) tableptr = asus1024_740;
	else    			          tableptr = table1024_740;
     } else if((SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) ||
	       (SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) ||
	       (SiS_Pr->SiS_LCDResInfo == Panel_1600x1200)) {
	if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) tableptr = asus1400_740;
        else					  tableptr = table1400_740;
     } else return;
  } else {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	tableptr = table1024_650;
     } else if((SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) ||
	       (SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) ||
	       (SiS_Pr->SiS_LCDResInfo == Panel_1600x1200)) {
	tableptr = table1400_650;
     } else return;
  }

  for(i=0; i<5; i++) {
     SiS_SetCH701x(SiS_Pr, regtable[i], tableptr[i]);
  }
}

static void
SiS_SetCH701xForLCD(struct SiS_Private *SiS_Pr)
{
  const unsigned char *tableptr = NULL;
  unsigned short tempbh;
  int i;
  static const unsigned char regtable[] = {
		0x1c, 0x5f, 0x64, 0x6f, 0x70, 0x71,
		0x72, 0x73, 0x74, 0x76, 0x78, 0x7d, 0x66
  };
  static const unsigned char table1024_740[] = {
		0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
		0xa3, 0xc8, 0xc7, 0xac, 0xe0, 0x02, 0x44
  };
  static const unsigned char table1280_740[] = {
		0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
		0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44
  };
  static const unsigned char table1400_740[] = {
		0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
		0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02, 0x44
  };
  static const unsigned char table1600_740[] = {
		0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
		0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a, 0x44
  };
  static const unsigned char table1024_650[] = {
		0x60, 0x02, 0x00, 0x07, 0x40, 0xed,
		0xa3, 0xc8, 0xc7, 0xac, 0x60, 0x02
  };
  static const unsigned char table1280_650[] = {
		0x60, 0x03, 0x11, 0x00, 0x40, 0xe3,
		0xad, 0xdb, 0xf6, 0xac, 0xe0, 0x02
  };
  static const unsigned char table1400_650[] = {
		0x60, 0x03, 0x11, 0x00, 0x40, 0xef,
		0xad, 0xdb, 0xf6, 0xac, 0x60, 0x02
  };
  static const unsigned char table1600_650[] = {
		0x60, 0x04, 0x11, 0x00, 0x40, 0xe3,
		0xad, 0xde, 0xf6, 0xac, 0x60, 0x1a
  };

  if(SiS_Pr->ChipType == SIS_740) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768)       tableptr = table1024_740;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) tableptr = table1280_740;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) tableptr = table1400_740;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) tableptr = table1600_740;
     else return;
  } else {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768)       tableptr = table1024_650;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) tableptr = table1280_650;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) tableptr = table1400_650;
     else if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) tableptr = table1600_650;
     else return;
  }

  tempbh = SiS_GetCH701x(SiS_Pr,0x74);
  if((tempbh == 0xf6) || (tempbh == 0xc7)) {
     tempbh = SiS_GetCH701x(SiS_Pr,0x73);
     if(tempbh == 0xc8) {
        if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) return;
     } else if(tempbh == 0xdb) {
        if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) return;
	if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) return;
     } else if(tempbh == 0xde) {
        if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) return;
     }
  }

  if(SiS_Pr->ChipType == SIS_740) tempbh = 0x0d;
  else     			  tempbh = 0x0c;

  for(i = 0; i < tempbh; i++) {
     SiS_SetCH701x(SiS_Pr, regtable[i], tableptr[i]);
  }
  SiS_ChrontelPowerSequencing(SiS_Pr);
  tempbh = SiS_GetCH701x(SiS_Pr,0x1e);
  tempbh |= 0xc0;
  SiS_SetCH701x(SiS_Pr,0x1e,tempbh);

  if(SiS_Pr->ChipType == SIS_740) {
     tempbh = SiS_GetCH701x(SiS_Pr,0x1c);
     tempbh &= 0xfb;
     SiS_SetCH701x(SiS_Pr,0x1c,tempbh);
     SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x03);
     tempbh = SiS_GetCH701x(SiS_Pr,0x64);
     tempbh |= 0x40;
     SiS_SetCH701x(SiS_Pr,0x64,tempbh);
     tempbh = SiS_GetCH701x(SiS_Pr,0x03);
     tempbh &= 0x3f;
     SiS_SetCH701x(SiS_Pr,0x03,tempbh);
  }
}

static void
SiS_ChrontelResetVSync(struct SiS_Private *SiS_Pr)
{
  unsigned char temp, temp1;

  temp1 = SiS_GetCH701x(SiS_Pr,0x49);
  SiS_SetCH701x(SiS_Pr,0x49,0x3e);
  temp = SiS_GetCH701x(SiS_Pr,0x47);
  temp &= 0x7f;	/* Use external VSYNC */
  SiS_SetCH701x(SiS_Pr,0x47,temp);
  SiS_LongDelay(SiS_Pr, 3);
  temp = SiS_GetCH701x(SiS_Pr,0x47);
  temp |= 0x80;	/* Use internal VSYNC */
  SiS_SetCH701x(SiS_Pr,0x47,temp);
  SiS_SetCH701x(SiS_Pr,0x49,temp1);
}

static void
SiS_Chrontel701xOn(struct SiS_Private *SiS_Pr)
{
  unsigned short temp;

  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     if(SiS_Pr->ChipType == SIS_740) {
        temp = SiS_GetCH701x(SiS_Pr,0x1c);
        temp |= 0x04;	/* Invert XCLK phase */
        SiS_SetCH701x(SiS_Pr,0x1c,temp);
     }
     if(SiS_IsYPbPr(SiS_Pr)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0x80;	/* Enable YPrPb (HDTV) */
	SiS_SetCH701x(SiS_Pr,0x01,temp);
     }
     if(SiS_IsChScart(SiS_Pr)) {
        temp = SiS_GetCH701x(SiS_Pr,0x01);
	temp &= 0x3f;
	temp |= 0xc0;	/* Enable SCART + CVBS */
	SiS_SetCH701x(SiS_Pr,0x01,temp);
     }
     if(SiS_Pr->ChipType == SIS_740) {
        SiS_ChrontelResetVSync(SiS_Pr);
        SiS_SetCH701x(SiS_Pr,0x49,0x20);   /* Enable TV path */
     } else {
        SiS_SetCH701x(SiS_Pr,0x49,0x20);   /* Enable TV path */
        temp = SiS_GetCH701x(SiS_Pr,0x49);
        if(SiS_IsYPbPr(SiS_Pr)) {
           temp = SiS_GetCH701x(SiS_Pr,0x73);
	   temp |= 0x60;
	   SiS_SetCH701x(SiS_Pr,0x73,temp);
        }
        temp = SiS_GetCH701x(SiS_Pr,0x47);
        temp &= 0x7f;
        SiS_SetCH701x(SiS_Pr,0x47,temp);
        SiS_LongDelay(SiS_Pr, 2);
        temp = SiS_GetCH701x(SiS_Pr,0x47);
        temp |= 0x80;
        SiS_SetCH701x(SiS_Pr,0x47,temp);
     }
  }
}

static void
SiS_Chrontel701xOff(struct SiS_Private *SiS_Pr)
{
  unsigned short temp;

  /* Complete power down of LVDS */
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
     if(SiS_Pr->ChipType == SIS_740) {
        SiS_LongDelay(SiS_Pr, 1);
	SiS_GenericDelay(SiS_Pr, 5887);
	SiS_SetCH701x(SiS_Pr,0x76,0xac);
	SiS_SetCH701x(SiS_Pr,0x66,0x00);
     } else {
        SiS_LongDelay(SiS_Pr, 2);
	temp = SiS_GetCH701x(SiS_Pr,0x76);
	temp &= 0xfc;
	SiS_SetCH701x(SiS_Pr,0x76,temp);
	SiS_SetCH701x(SiS_Pr,0x66,0x00);
     }
  }
}

static void
SiS_ChrontelResetDB(struct SiS_Private *SiS_Pr)
{
     unsigned short temp;

     if(SiS_Pr->ChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x4a);  /* Version ID */
        temp &= 0x01;
        if(!temp) {

           if(SiS_WeHaveBacklightCtrl(SiS_Pr)) {
	      temp = SiS_GetCH701x(SiS_Pr,0x49);
	      SiS_SetCH701x(SiS_Pr,0x49,0x3e);
	   }

	   /* Reset Chrontel 7019 datapath */
           SiS_SetCH701x(SiS_Pr,0x48,0x10);
           SiS_LongDelay(SiS_Pr, 1);
           SiS_SetCH701x(SiS_Pr,0x48,0x18);

	   if(SiS_WeHaveBacklightCtrl(SiS_Pr)) {
	      SiS_ChrontelResetVSync(SiS_Pr);
	      SiS_SetCH701x(SiS_Pr,0x49,temp);
	   }

        } else {

	   /* Clear/set/clear GPIO */
           temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp &= 0xef;
	   SiS_SetCH701x(SiS_Pr,0x5c,temp);
	   temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp |= 0x10;
	   SiS_SetCH701x(SiS_Pr,0x5c,temp);
	   temp = SiS_GetCH701x(SiS_Pr,0x5c);
	   temp &= 0xef;
	   SiS_SetCH701x(SiS_Pr,0x5c,temp);
	   temp = SiS_GetCH701x(SiS_Pr,0x61);
	   if(!temp) {
	      SiS_SetCH701xForLCD(SiS_Pr);
	   }
        }

     } else { /* 650 */
        /* Reset Chrontel 7019 datapath */
        SiS_SetCH701x(SiS_Pr,0x48,0x10);
        SiS_LongDelay(SiS_Pr, 1);
        SiS_SetCH701x(SiS_Pr,0x48,0x18);
     }
}

static void
SiS_ChrontelInitTVVSync(struct SiS_Private *SiS_Pr)
{
     unsigned short temp;

     if(SiS_Pr->ChipType == SIS_740) {

        if(SiS_WeHaveBacklightCtrl(SiS_Pr)) {
           SiS_ChrontelResetVSync(SiS_Pr);
        }

     } else {

        SiS_SetCH701x(SiS_Pr,0x76,0xaf);  /* Power up LVDS block */
        temp = SiS_GetCH701x(SiS_Pr,0x49);
        temp &= 1;
        if(temp != 1) {  /* TV block powered? (0 = yes, 1 = no) */
	   temp = SiS_GetCH701x(SiS_Pr,0x47);
	   temp &= 0x70;
	   SiS_SetCH701x(SiS_Pr,0x47,temp);  /* enable VSYNC */
	   SiS_LongDelay(SiS_Pr, 3);
	   temp = SiS_GetCH701x(SiS_Pr,0x47);
	   temp |= 0x80;
	   SiS_SetCH701x(SiS_Pr,0x47,temp);  /* disable VSYNC */
        }

     }
}

static void
SiS_ChrontelDoSomething3(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
     unsigned short temp,temp1;

     if(SiS_Pr->ChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x61);
        if(temp < 1) {
           temp++;
	   SiS_SetCH701x(SiS_Pr,0x61,temp);
        }
        SiS_SetCH701x(SiS_Pr,0x66,0x45);  /* Panel power on */
        SiS_SetCH701x(SiS_Pr,0x76,0xaf);  /* All power on */
        SiS_LongDelay(SiS_Pr, 1);
        SiS_GenericDelay(SiS_Pr, 5887);

     } else {  /* 650 */

        temp1 = 0;
        temp = SiS_GetCH701x(SiS_Pr,0x61);
        if(temp < 2) {
           temp++;
	   SiS_SetCH701x(SiS_Pr,0x61,temp);
	   temp1 = 1;
        }
        SiS_SetCH701x(SiS_Pr,0x76,0xac);
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp |= 0x5f;
        SiS_SetCH701x(SiS_Pr,0x66,temp);
        if(ModeNo > 0x13) {
           if(SiS_WeHaveBacklightCtrl(SiS_Pr)) {
	      SiS_GenericDelay(SiS_Pr, 1023);
	   } else {
	      SiS_GenericDelay(SiS_Pr, 767);
	   }
        } else {
           if(!temp1)
	      SiS_GenericDelay(SiS_Pr, 767);
        }
        temp = SiS_GetCH701x(SiS_Pr,0x76);
        temp |= 0x03;
        SiS_SetCH701x(SiS_Pr,0x76,temp);
        temp = SiS_GetCH701x(SiS_Pr,0x66);
        temp &= 0x7f;
        SiS_SetCH701x(SiS_Pr,0x66,temp);
        SiS_LongDelay(SiS_Pr, 1);

     }
}

static void
SiS_ChrontelDoSomething2(struct SiS_Private *SiS_Pr)
{
     unsigned short temp;

     SiS_LongDelay(SiS_Pr, 1);

     do {
       temp = SiS_GetCH701x(SiS_Pr,0x66);
       temp &= 0x04;  /* PLL stable? -> bail out */
       if(temp == 0x04) break;

       if(SiS_Pr->ChipType == SIS_740) {
          /* Power down LVDS output, PLL normal operation */
          SiS_SetCH701x(SiS_Pr,0x76,0xac);
       }

       SiS_SetCH701xForLCD(SiS_Pr);

       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp &= 0xfb;  /* Reset PLL */
       SiS_SetCH701x(SiS_Pr,0x76,temp);
       SiS_LongDelay(SiS_Pr, 2);
       temp = SiS_GetCH701x(SiS_Pr,0x76);
       temp |= 0x04;  /* PLL normal operation */
       SiS_SetCH701x(SiS_Pr,0x76,temp);
       if(SiS_Pr->ChipType == SIS_740) {
          SiS_SetCH701x(SiS_Pr,0x78,0xe0);	/* PLL loop filter */
       } else {
          SiS_SetCH701x(SiS_Pr,0x78,0x60);
       }
       SiS_LongDelay(SiS_Pr, 2);
    } while(0);

    SiS_SetCH701x(SiS_Pr,0x77,0x00);  /* MV? */
}

static void
SiS_ChrontelDoSomething1(struct SiS_Private *SiS_Pr)
{
     unsigned short temp;

     temp = SiS_GetCH701x(SiS_Pr,0x03);
     temp |= 0x80;	/* Set datapath 1 to TV   */
     temp &= 0xbf;	/* Set datapath 2 to LVDS */
     SiS_SetCH701x(SiS_Pr,0x03,temp);

     if(SiS_Pr->ChipType == SIS_740) {

        temp = SiS_GetCH701x(SiS_Pr,0x1c);
        temp &= 0xfb;	/* Normal XCLK phase */
        SiS_SetCH701x(SiS_Pr,0x1c,temp);

        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2d,0x03);

        temp = SiS_GetCH701x(SiS_Pr,0x64);
        temp |= 0x40;	/* ? Bit not defined */
        SiS_SetCH701x(SiS_Pr,0x64,temp);

        temp = SiS_GetCH701x(SiS_Pr,0x03);
        temp &= 0x3f;	/* D1 input to both LVDS and TV */
        SiS_SetCH701x(SiS_Pr,0x03,temp);

	if(SiS_Pr->SiS_CustomT == CUT_ASUSL3000D) {
	   SiS_SetCH701x(SiS_Pr,0x63,0x40); /* LVDS off */
	   SiS_LongDelay(SiS_Pr, 1);
	   SiS_SetCH701x(SiS_Pr,0x63,0x00); /* LVDS on */
	   SiS_ChrontelResetDB(SiS_Pr);
	   SiS_ChrontelDoSomething2(SiS_Pr);
	   SiS_ChrontelDoSomething3(SiS_Pr, 0);
	} else {
           temp = SiS_GetCH701x(SiS_Pr,0x66);
           if(temp != 0x45) {
              SiS_ChrontelResetDB(SiS_Pr);
              SiS_ChrontelDoSomething2(SiS_Pr);
              SiS_ChrontelDoSomething3(SiS_Pr, 0);
           }
	}

     } else { /* 650 */

        SiS_ChrontelResetDB(SiS_Pr);
        SiS_ChrontelDoSomething2(SiS_Pr);
        temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x34);
        SiS_ChrontelDoSomething3(SiS_Pr,temp);
        SiS_SetCH701x(SiS_Pr,0x76,0xaf);  /* All power on, LVDS normal operation */

     }

}
#endif  /* 315 series  */

/*********************************************/
/*      MAIN: SET CRT2 REGISTER GROUP        */
/*********************************************/

bool
SiS_SetCRT2Group(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
#ifdef CONFIG_FB_SIS_300
   unsigned char  *ROMAddr  = SiS_Pr->VirtualRomBase;
#endif
   unsigned short ModeIdIndex, RefreshRateTableIndex;

   SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;

   if(!SiS_Pr->UseCustomMode) {
      SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex);
   } else {
      ModeIdIndex = 0;
   }

   /* Used for shifting CR33 */
   SiS_Pr->SiS_SelectCRT2Rate = 4;

   SiS_UnLockCRT2(SiS_Pr);

   RefreshRateTableIndex = SiS_GetRatePtr(SiS_Pr, ModeNo, ModeIdIndex);

   SiS_SaveCRT2Info(SiS_Pr,ModeNo);

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_DisableBridge(SiS_Pr);
      if((SiS_Pr->SiS_IF_DEF_LVDS == 1) && (SiS_Pr->ChipType == SIS_730)) {
         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x00,0x80);
      }
      SiS_SetCRT2ModeRegs(SiS_Pr, ModeNo, ModeIdIndex);
   }

   if(SiS_Pr->SiS_VBInfo & DisableCRT2Display) {
      SiS_LockCRT2(SiS_Pr);
      SiS_DisplayOn(SiS_Pr);
      return true;
   }

   SiS_GetCRT2Data(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);

   /* Set up Panel Link for LVDS and LCDA */
   SiS_Pr->SiS_LCDHDES = SiS_Pr->SiS_LCDVDES = 0;
   if( (SiS_Pr->SiS_IF_DEF_LVDS == 1) ||
       ((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) ||
       ((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->SiS_VBType & VB_SIS30xBLV)) ) {
      SiS_GetLVDSDesData(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
   }

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_SetGroup1(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
   }

   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      if(SiS_Pr->SiS_SetFlag & LowModeTests) {

	 SiS_SetGroup2(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
#ifdef CONFIG_FB_SIS_315
	 SiS_SetGroup2_C_ELV(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
#endif
	 SiS_SetGroup3(SiS_Pr, ModeNo, ModeIdIndex);
	 SiS_SetGroup4(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
#ifdef CONFIG_FB_SIS_315
	 SiS_SetGroup4_C_ELV(SiS_Pr, ModeNo, ModeIdIndex);
#endif
	 SiS_SetGroup5(SiS_Pr, ModeNo, ModeIdIndex);

	 SiS_SetCRT2Sync(SiS_Pr, ModeNo, RefreshRateTableIndex);

	 /* For 301BDH (Panel link initialization): */
	 if((SiS_Pr->SiS_VBType & VB_NoLCD) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD)) {

	    if(!((SiS_Pr->SiS_SetFlag & SetDOSMode) && ((ModeNo == 0x03) || (ModeNo == 0x10)))) {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
		  SiS_ModCRT1CRTC(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	       }
            }
	    SiS_SetCRT2ECLK(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 }
      }

   } else {

      SiS_SetCRT2Sync(SiS_Pr, ModeNo, RefreshRateTableIndex);

      SiS_ModCRT1CRTC(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex);

      SiS_SetCRT2ECLK(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex);

      if(SiS_Pr->SiS_SetFlag & LowModeTests) {
	 if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	    if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
	       if(SiS_Pr->SiS_IF_DEF_CH70xx == 2) {
#ifdef CONFIG_FB_SIS_315
		  SiS_SetCH701xForLCD(SiS_Pr);
#endif
	       }
	    }
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	       SiS_SetCHTVReg(SiS_Pr,ModeNo,ModeIdIndex,RefreshRateTableIndex);
	    }
	 }
      }

   }

#ifdef CONFIG_FB_SIS_300
   if(SiS_Pr->ChipType < SIS_315H) {
      if(SiS_Pr->SiS_SetFlag & LowModeTests) {
	 if(SiS_Pr->SiS_UseOEM) {
	    if((SiS_Pr->SiS_UseROM) && (SiS_Pr->SiS_UseOEM == -1)) {
	       if((ROMAddr[0x233] == 0x12) && (ROMAddr[0x234] == 0x34)) {
		  SiS_OEM300Setting(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	       }
	    } else {
	       SiS_OEM300Setting(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	    }
	 }
	 if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	    if((SiS_Pr->SiS_CustomT == CUT_BARCO1366) ||
	       (SiS_Pr->SiS_CustomT == CUT_BARCO1024)) {
	       SetOEMLCDData2(SiS_Pr, ModeNo, ModeIdIndex,RefreshRateTableIndex);
	    }
	    SiS_DisplayOn(SiS_Pr);
         }
      }
   }
#endif

#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType >= SIS_315H) {
      if(SiS_Pr->SiS_SetFlag & LowModeTests) {
	 if(SiS_Pr->ChipType < SIS_661) {
	    SiS_FinalizeLCD(SiS_Pr, ModeNo, ModeIdIndex);
	    SiS_OEM310Setting(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 } else {
	    SiS_OEM661Setting(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
	 }
	 SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x01,0x40);
      }
   }
#endif

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_EnableBridge(SiS_Pr);
   }

   SiS_DisplayOn(SiS_Pr);

   if(SiS_Pr->SiS_IF_DEF_CH70xx == 1) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
	 /* Disable LCD panel when using TV */
	 SiS_SetRegSR11ANDOR(SiS_Pr,0xFF,0x0C);
      } else {
	 /* Disable TV when using LCD */
	 SiS_SetCH70xxANDOR(SiS_Pr,0x0e,0x01,0xf8);
      }
   }

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      SiS_LockCRT2(SiS_Pr);
   }

   return true;
}


/*********************************************/
/*     ENABLE/DISABLE LCD BACKLIGHT (SIS)    */
/*********************************************/

void
SiS_SiS30xBLOn(struct SiS_Private *SiS_Pr)
{
  /* Switch on LCD backlight on SiS30xLV */
  SiS_DDC2Delay(SiS_Pr,0xff00);
  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x02)) {
     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x02);
     SiS_WaitVBRetrace(SiS_Pr);
  }
  if(!(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x01)) {
     SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x26,0x01);
  }
}

void
SiS_SiS30xBLOff(struct SiS_Private *SiS_Pr)
{
  /* Switch off LCD backlight on SiS30xLV */
  SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x26,0xFE);
  SiS_DDC2Delay(SiS_Pr,0xff00);
}

/*********************************************/
/*          DDC RELATED FUNCTIONS            */
/*********************************************/

static void
SiS_SetupDDCN(struct SiS_Private *SiS_Pr)
{
  SiS_Pr->SiS_DDC_NData = ~SiS_Pr->SiS_DDC_Data;
  SiS_Pr->SiS_DDC_NClk  = ~SiS_Pr->SiS_DDC_Clk;
  if((SiS_Pr->SiS_DDC_Index == 0x11) && (SiS_Pr->SiS_SensibleSR11)) {
     SiS_Pr->SiS_DDC_NData &= 0x0f;
     SiS_Pr->SiS_DDC_NClk  &= 0x0f;
  }
}

#ifdef CONFIG_FB_SIS_300
static unsigned char *
SiS_SetTrumpBlockLoop(struct SiS_Private *SiS_Pr, unsigned char *dataptr)
{
  int i, j, num;
  unsigned short tempah,temp;
  unsigned char *mydataptr;

  for(i=0; i<20; i++) {				/* Do 20 attempts to write */
     mydataptr = dataptr;
     num = *mydataptr++;
     if(!num) return mydataptr;
     if(i) {
        SiS_SetStop(SiS_Pr);
	SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT * 2);
     }
     if(SiS_SetStart(SiS_Pr)) continue;		/* Set start condition */
     tempah = SiS_Pr->SiS_DDC_DeviceAddr;
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* Write DAB (S0=0=write) */
     if(temp) continue;				/*    (ERROR: no ack) */
     tempah = *mydataptr++;
     temp = SiS_WriteDDC2Data(SiS_Pr,tempah);	/* Write register number */
     if(temp) continue;				/*    (ERROR: no ack) */
     for(j=0; j<num; j++) {
        tempah = *mydataptr++;
        temp = SiS_WriteDDC2Data(SiS_Pr,tempah);/* Write DAB (S0=0=write) */
	if(temp) break;
     }
     if(temp) continue;
     if(SiS_SetStop(SiS_Pr)) continue;
     return mydataptr;
  }
  return NULL;
}

static bool
SiS_SetTrumpionBlock(struct SiS_Private *SiS_Pr, unsigned char *dataptr)
{
  SiS_Pr->SiS_DDC_DeviceAddr = 0xF0;  		/* DAB (Device Address Byte) */
  SiS_Pr->SiS_DDC_Index = 0x11;			/* Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x02;			/* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x01;			/* Bitmask in IndexReg for Clk */
  SiS_SetupDDCN(SiS_Pr);

  SiS_SetSwitchDDC2(SiS_Pr);

  while(*dataptr) {
     dataptr = SiS_SetTrumpBlockLoop(SiS_Pr, dataptr);
     if(!dataptr) return false;
  }
  return true;
}
#endif

/* The Chrontel 700x is connected to the 630/730 via
 * the 630/730's DDC/I2C port.
 *
 * On 630(S)T chipset, the index changed from 0x11 to
 * 0x0a, possibly for working around the DDC problems
 */

static bool
SiS_SetChReg(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val, unsigned short myor)
{
  unsigned short temp, i;

  for(i=0; i<20; i++) {				/* Do 20 attempts to write */
     if(i) {
	SiS_SetStop(SiS_Pr);
	SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT * 4);
     }
     if(SiS_SetStart(SiS_Pr)) continue;					/* Set start condition */
     temp = SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_DeviceAddr);	/* Write DAB (S0=0=write) */
     if(temp) continue;							/*    (ERROR: no ack) */
     temp = SiS_WriteDDC2Data(SiS_Pr, (reg | myor));			/* Write RAB (700x: set bit 7, see datasheet) */
     if(temp) continue;							/*    (ERROR: no ack) */
     temp = SiS_WriteDDC2Data(SiS_Pr, val);				/* Write data */
     if(temp) continue;							/*    (ERROR: no ack) */
     if(SiS_SetStop(SiS_Pr)) continue;					/* Set stop condition */
     SiS_Pr->SiS_ChrontelInit = 1;
     return true;
  }
  return false;
}

/* Write to Chrontel 700x */
void
SiS_SetCH700x(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val)
{
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;  		/* DAB (Device Address Byte) */

  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		/* Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;		/* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;		/* Bitmask in IndexReg for Clk */
     SiS_SetupDDCN(SiS_Pr);
  }

  if( (!(SiS_SetChReg(SiS_Pr, reg, val, 0x80))) &&
      (!(SiS_Pr->SiS_ChrontelInit)) ) {
     SiS_Pr->SiS_DDC_Index = 0x0a;
     SiS_Pr->SiS_DDC_Data  = 0x80;
     SiS_Pr->SiS_DDC_Clk   = 0x40;
     SiS_SetupDDCN(SiS_Pr);

     SiS_SetChReg(SiS_Pr, reg, val, 0x80);
  }
}

/* Write to Chrontel 701x */
/* Parameter is [Data (S15-S8) | Register no (S7-S0)] */
void
SiS_SetCH701x(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val)
{
  SiS_Pr->SiS_DDC_Index = 0x11;			/* Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;			/* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;			/* Bitmask in IndexReg for Clk */
  SiS_SetupDDCN(SiS_Pr);
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* DAB (Device Address Byte) */
  SiS_SetChReg(SiS_Pr, reg, val, 0);
}

static
void
SiS_SetCH70xx(struct SiS_Private *SiS_Pr, unsigned short reg, unsigned char val)
{
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 1)
     SiS_SetCH700x(SiS_Pr, reg, val);
  else
     SiS_SetCH701x(SiS_Pr, reg, val);
}

static unsigned short
SiS_GetChReg(struct SiS_Private *SiS_Pr, unsigned short myor)
{
  unsigned short tempah, temp, i;

  for(i=0; i<20; i++) {				/* Do 20 attempts to read */
     if(i) {
	SiS_SetStop(SiS_Pr);
	SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT * 4);
     }
     if(SiS_SetStart(SiS_Pr)) continue;					/* Set start condition */
     temp = SiS_WriteDDC2Data(SiS_Pr,SiS_Pr->SiS_DDC_DeviceAddr);	/* Write DAB (S0=0=write) */
     if(temp) continue;							/*        (ERROR: no ack) */
     temp = SiS_WriteDDC2Data(SiS_Pr,SiS_Pr->SiS_DDC_ReadAddr | myor);	/* Write RAB (700x: | 0x80) */
     if(temp) continue;							/*        (ERROR: no ack) */
     if (SiS_SetStart(SiS_Pr)) continue;				/* Re-start */
     temp = SiS_WriteDDC2Data(SiS_Pr,SiS_Pr->SiS_DDC_DeviceAddr | 0x01);/* DAB (S0=1=read) */
     if(temp) continue;							/*        (ERROR: no ack) */
     tempah = SiS_ReadDDC2Data(SiS_Pr);					/* Read byte */
     if(SiS_SetStop(SiS_Pr)) continue;					/* Stop condition */
     SiS_Pr->SiS_ChrontelInit = 1;
     return tempah;
  }
  return 0xFFFF;
}

/* Read from Chrontel 700x */
/* Parameter is [Register no (S7-S0)] */
unsigned short
SiS_GetCH700x(struct SiS_Private *SiS_Pr, unsigned short tempbx)
{
  unsigned short result;

  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* DAB */

  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);

  if(!(SiS_Pr->SiS_ChrontelInit)) {
     SiS_Pr->SiS_DDC_Index = 0x11;		/* Bit 0 = SC;  Bit 1 = SD */
     SiS_Pr->SiS_DDC_Data  = 0x02;		/* Bitmask in IndexReg for Data */
     SiS_Pr->SiS_DDC_Clk   = 0x01;		/* Bitmask in IndexReg for Clk */
     SiS_SetupDDCN(SiS_Pr);
  }

  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

  if( ((result = SiS_GetChReg(SiS_Pr,0x80)) == 0xFFFF) &&
      (!SiS_Pr->SiS_ChrontelInit) ) {

     SiS_Pr->SiS_DDC_Index = 0x0a;
     SiS_Pr->SiS_DDC_Data  = 0x80;
     SiS_Pr->SiS_DDC_Clk   = 0x40;
     SiS_SetupDDCN(SiS_Pr);

     result = SiS_GetChReg(SiS_Pr,0x80);
  }
  return result;
}

/* Read from Chrontel 701x */
/* Parameter is [Register no (S7-S0)] */
unsigned short
SiS_GetCH701x(struct SiS_Private *SiS_Pr, unsigned short tempbx)
{
  SiS_Pr->SiS_DDC_Index = 0x11;			/* Bit 0 = SC;  Bit 1 = SD */
  SiS_Pr->SiS_DDC_Data  = 0x08;			/* Bitmask in IndexReg for Data */
  SiS_Pr->SiS_DDC_Clk   = 0x04;			/* Bitmask in IndexReg for Clk */
  SiS_SetupDDCN(SiS_Pr);
  SiS_Pr->SiS_DDC_DeviceAddr = 0xEA;		/* DAB */

  SiS_Pr->SiS_DDC_ReadAddr = tempbx;

  return SiS_GetChReg(SiS_Pr,0);
}

/* Read from Chrontel 70xx */
/* Parameter is [Register no (S7-S0)] */
static
unsigned short
SiS_GetCH70xx(struct SiS_Private *SiS_Pr, unsigned short tempbx)
{
  if(SiS_Pr->SiS_IF_DEF_CH70xx == 1)
     return SiS_GetCH700x(SiS_Pr, tempbx);
  else
     return SiS_GetCH701x(SiS_Pr, tempbx);
}

void
SiS_SetCH70xxANDOR(struct SiS_Private *SiS_Pr, unsigned short reg,
		unsigned char myor, unsigned short myand)
{
  unsigned short tempbl;

  tempbl = (SiS_GetCH70xx(SiS_Pr, (reg & 0xFF)) & myand) | myor;
  SiS_SetCH70xx(SiS_Pr, reg, tempbl);
}

/* Our own DDC functions */
static
unsigned short
SiS_InitDDCRegs(struct SiS_Private *SiS_Pr, unsigned int VBFlags, int VGAEngine,
                unsigned short adaptnum, unsigned short DDCdatatype, bool checkcr32,
		unsigned int VBFlags2)
{
     unsigned char ddcdtype[] = { 0xa0, 0xa0, 0xa0, 0xa2, 0xa6 };
     unsigned char flag, cr32;
     unsigned short        temp = 0, myadaptnum = adaptnum;

     if(adaptnum != 0) {
	if(!(VBFlags2 & VB2_SISTMDSBRIDGE)) return 0xFFFF;
	if((VBFlags2 & VB2_30xBDH) && (adaptnum == 1)) return 0xFFFF;
     }

     /* adapternum for SiS bridges: 0 = CRT1, 1 = LCD, 2 = VGA2 */

     SiS_Pr->SiS_ChrontelInit = 0;   /* force re-detection! */

     SiS_Pr->SiS_DDC_SecAddr = 0;
     SiS_Pr->SiS_DDC_DeviceAddr = ddcdtype[DDCdatatype];
     SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_P3c4;
     SiS_Pr->SiS_DDC_Index = 0x11;
     flag = 0xff;

     cr32 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x32);

#if 0
     if(VBFlags2 & VB2_SISBRIDGE) {
	if(myadaptnum == 0) {
	   if(!(cr32 & 0x20)) {
	      myadaptnum = 2;
	      if(!(cr32 & 0x10)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x08)) {
		    myadaptnum = 0;
		 }
	      }
	   }
        }
     }
#endif

     if(VGAEngine == SIS_300_VGA) {		/* 300 series */

        if(myadaptnum != 0) {
	   flag = 0;
	   if(VBFlags2 & VB2_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

	if(!(VBFlags2 & VB2_301)) {
	   if((cr32 & 0x80) && (checkcr32)) {
              if(myadaptnum >= 1) {
	         if(!(cr32 & 0x08)) {
		     myadaptnum = 1;
		     if(!(cr32 & 0x10)) return 0xFFFF;
                 }
	      }
	   }
	}

	temp = 4 - (myadaptnum * 2);
	if(flag) temp = 0;

     } else {						/* 315/330 series */

	/* here we simplify: 0 = CRT1, 1 = CRT2 (VGA, LCD) */

	if(VBFlags2 & VB2_SISBRIDGE) {
	   if(myadaptnum == 2) {
	      myadaptnum = 1;
	   }
	}

        if(myadaptnum == 1) {
	   flag = 0;
	   if(VBFlags2 & VB2_SISBRIDGE) {
	      SiS_Pr->SiS_DDC_Port = SiS_Pr->SiS_Part4Port;
              SiS_Pr->SiS_DDC_Index = 0x0f;
	   }
        }

        if((cr32 & 0x80) && (checkcr32)) {
           if(myadaptnum >= 1) {
	      if(!(cr32 & 0x08)) {
	         myadaptnum = 1;
		 if(!(cr32 & 0x10)) return 0xFFFF;
	      }
	   }
        }

        temp = myadaptnum;
        if(myadaptnum == 1) {
           temp = 0;
	   if(VBFlags2 & VB2_LVDS) flag = 0xff;
        }

	if(flag) temp = 0;
    }

    SiS_Pr->SiS_DDC_Data = 0x02 << temp;
    SiS_Pr->SiS_DDC_Clk  = 0x01 << temp;

    SiS_SetupDDCN(SiS_Pr);

    return 0;
}

static unsigned short
SiS_WriteDABDDC(struct SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_DeviceAddr)) {
      return 0xFFFF;
   }
   if(SiS_WriteDDC2Data(SiS_Pr, SiS_Pr->SiS_DDC_SecAddr)) {
      return 0xFFFF;
   }
   return 0;
}

static unsigned short
SiS_PrepareReadDDC(struct SiS_Private *SiS_Pr)
{
   if(SiS_SetStart(SiS_Pr)) return 0xFFFF;
   if(SiS_WriteDDC2Data(SiS_Pr, (SiS_Pr->SiS_DDC_DeviceAddr | 0x01))) {
      return 0xFFFF;
   }
   return 0;
}

static unsigned short
SiS_PrepareDDC(struct SiS_Private *SiS_Pr)
{
   if(SiS_WriteDABDDC(SiS_Pr)) SiS_WriteDABDDC(SiS_Pr);
   if(SiS_PrepareReadDDC(SiS_Pr)) return (SiS_PrepareReadDDC(SiS_Pr));
   return 0;
}

static void
SiS_SendACK(struct SiS_Private *SiS_Pr, unsigned short yesno)
{
   SiS_SetSCLKLow(SiS_Pr);
   if(yesno) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		      SiS_Pr->SiS_DDC_Index,
		      SiS_Pr->SiS_DDC_NData,
		      SiS_Pr->SiS_DDC_Data);
   } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		      SiS_Pr->SiS_DDC_Index,
		      SiS_Pr->SiS_DDC_NData,
		      0);
   }
   SiS_SetSCLKHigh(SiS_Pr);
}

static unsigned short
SiS_DoProbeDDC(struct SiS_Private *SiS_Pr)
{
    unsigned char mask, value;
    unsigned short  temp, ret=0;
    bool failed = false;

    SiS_SetSwitchDDC2(SiS_Pr);
    if(SiS_PrepareDDC(SiS_Pr)) {
         SiS_SetStop(SiS_Pr);
         return 0xFFFF;
    }
    mask = 0xf0;
    value = 0x20;
    if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr);
       SiS_SendACK(SiS_Pr, 0);
       if(temp == 0) {
           mask = 0xff;
	   value = 0xff;
       } else {
           failed = true;
	   ret = 0xFFFF;
       }
    }
    if(!failed) {
       temp = (unsigned char)SiS_ReadDDC2Data(SiS_Pr);
       SiS_SendACK(SiS_Pr, 1);
       temp &= mask;
       if(temp == value) ret = 0;
       else {
          ret = 0xFFFF;
          if(SiS_Pr->SiS_DDC_DeviceAddr == 0xa0) {
             if(temp == 0x30) ret = 0;
          }
       }
    }
    SiS_SetStop(SiS_Pr);
    return ret;
}

static
unsigned short
SiS_ProbeDDC(struct SiS_Private *SiS_Pr)
{
   unsigned short flag;

   flag = 0x180;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa0;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x02;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa2;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x08;
   SiS_Pr->SiS_DDC_DeviceAddr = 0xa6;
   if(!(SiS_DoProbeDDC(SiS_Pr))) flag |= 0x10;
   if(!(flag & 0x1a)) flag = 0;
   return flag;
}

static
unsigned short
SiS_ReadDDC(struct SiS_Private *SiS_Pr, unsigned short DDCdatatype, unsigned char *buffer)
{
   unsigned short flag, length, i;
   unsigned char chksum,gotcha;

   if(DDCdatatype > 4) return 0xFFFF;

   flag = 0;
   SiS_SetSwitchDDC2(SiS_Pr);
   if(!(SiS_PrepareDDC(SiS_Pr))) {
      length = 127;
      if(DDCdatatype != 1) length = 255;
      chksum = 0;
      gotcha = 0;
      for(i=0; i<length; i++) {
	 buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr);
	 chksum += buffer[i];
	 gotcha |= buffer[i];
	 SiS_SendACK(SiS_Pr, 0);
      }
      buffer[i] = (unsigned char)SiS_ReadDDC2Data(SiS_Pr);
      chksum += buffer[i];
      SiS_SendACK(SiS_Pr, 1);
      if(gotcha) flag = (unsigned short)chksum;
      else flag = 0xFFFF;
   } else {
      flag = 0xFFFF;
   }
   SiS_SetStop(SiS_Pr);
   return flag;
}

/* Our private DDC functions

   It complies somewhat with the corresponding VESA function
   in arguments and return values.

   Since this is probably called before the mode is changed,
   we use our pre-detected pSiS-values instead of SiS_Pr as
   regards chipset and video bridge type.

   Arguments:
       adaptnum: 0=CRT1(analog), 1=CRT2/LCD(digital), 2=CRT2/VGA2(analog)
                 CRT2 DDC is only supported on SiS301, 301B, 301C, 302B.
		 LCDA is CRT1, but DDC is read from CRT2 port.
       DDCdatatype: 0=Probe, 1=EDID, 2=EDID+VDIF, 3=EDID V2 (P&D), 4=EDID V2 (FPDI-2)
       buffer: ptr to 256 data bytes which will be filled with read data.

   Returns 0xFFFF if error, otherwise
       if DDCdatatype > 0:  Returns 0 if reading OK (included a correct checksum)
       if DDCdatatype = 0:  Returns supported DDC modes

 */
unsigned short
SiS_HandleDDC(struct SiS_Private *SiS_Pr, unsigned int VBFlags, int VGAEngine,
              unsigned short adaptnum, unsigned short DDCdatatype, unsigned char *buffer,
	      unsigned int VBFlags2)
{
   unsigned char  sr1f, cr17=1;
   unsigned short result;

   if(adaptnum > 2)
      return 0xFFFF;

   if(DDCdatatype > 4)
      return 0xFFFF;

   if((!(VBFlags2 & VB2_VIDEOBRIDGE)) && (adaptnum > 0))
      return 0xFFFF;

   if(SiS_InitDDCRegs(SiS_Pr, VBFlags, VGAEngine, adaptnum, DDCdatatype, false, VBFlags2) == 0xFFFF)
      return 0xFFFF;

   sr1f = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1f);
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x1f,0x3f,0x04);
   if(VGAEngine == SIS_300_VGA) {
      cr17 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x17) & 0x80;
      if(!cr17) {
         SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x17,0x80);
         SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x01);
         SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x03);
      }
   }
   if((sr1f) || (!cr17)) {
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
      SiS_WaitRetrace1(SiS_Pr);
   }

   if(DDCdatatype == 0) {
      result = SiS_ProbeDDC(SiS_Pr);
   } else {
      result = SiS_ReadDDC(SiS_Pr, DDCdatatype, buffer);
      if((!result) && (DDCdatatype == 1)) {
         if((buffer[0] == 0x00) && (buffer[1] == 0xff) &&
	    (buffer[2] == 0xff) && (buffer[3] == 0xff) &&
	    (buffer[4] == 0xff) && (buffer[5] == 0xff) &&
	    (buffer[6] == 0xff) && (buffer[7] == 0x00) &&
	    (buffer[0x12] == 1)) {
	    if(!SiS_Pr->DDCPortMixup) {
	       if(adaptnum == 1) {
	          if(!(buffer[0x14] & 0x80)) result = 0xFFFE;
	       } else {
	          if(buffer[0x14] & 0x80)    result = 0xFFFE;
	       }
	    }
	 }
      }
   }
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x1f,sr1f);
   if(VGAEngine == SIS_300_VGA) {
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x17,0x7f,cr17);
   }
   return result;
}

/* Generic I2C functions for Chrontel & DDC --------- */

static void
SiS_SetSwitchDDC2(struct SiS_Private *SiS_Pr)
{
  SiS_SetSCLKHigh(SiS_Pr);
  SiS_WaitRetrace1(SiS_Pr);

  SiS_SetSCLKLow(SiS_Pr);
  SiS_WaitRetrace1(SiS_Pr);
}

unsigned short
SiS_ReadDDC1Bit(struct SiS_Private *SiS_Pr)
{
   SiS_WaitRetrace1(SiS_Pr);
   return ((SiS_GetReg(SiS_Pr->SiS_P3c4,0x11) & 0x02) >> 1);
}

/* Set I2C start condition */
/* This is done by a SD high-to-low transition while SC is high */
static unsigned short
SiS_SetStart(struct SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			/* (SC->low)  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);        		/* SD->high */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			/* SC->high */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NData,
		  0x00);					/* SD->low = start condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			/* (SC->low) */
  return 0;
}

/* Set I2C stop condition */
/* This is done by a SD low-to-high transition while SC is high */
static unsigned short
SiS_SetStop(struct SiS_Private *SiS_Pr)
{
  if(SiS_SetSCLKLow(SiS_Pr)) return 0xFFFF;			/* (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NData,
		  0x00);					/* SD->low   */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			/* SC->high  */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);			/* SD->high = stop condition */
  if(SiS_SetSCLKHigh(SiS_Pr)) return 0xFFFF;			/* (SC->high) */
  return 0;
}

/* Write 8 bits of data */
static unsigned short
SiS_WriteDDC2Data(struct SiS_Private *SiS_Pr, unsigned short tempax)
{
  unsigned short i,flag,temp;

  flag = 0x80;
  for(i = 0; i < 8; i++) {
    SiS_SetSCLKLow(SiS_Pr);					/* SC->low */
    if(tempax & flag) {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		      SiS_Pr->SiS_DDC_Index,
		      SiS_Pr->SiS_DDC_NData,
		      SiS_Pr->SiS_DDC_Data);			/* Write bit (1) to SD */
    } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		      SiS_Pr->SiS_DDC_Index,
		      SiS_Pr->SiS_DDC_NData,
		      0x00);					/* Write bit (0) to SD */
    }
    SiS_SetSCLKHigh(SiS_Pr);					/* SC->high */
    flag >>= 1;
  }
  temp = SiS_CheckACK(SiS_Pr);					/* Check acknowledge */
  return temp;
}

static unsigned short
SiS_ReadDDC2Data(struct SiS_Private *SiS_Pr)
{
  unsigned short i, temp, getdata;

  getdata = 0;
  for(i = 0; i < 8; i++) {
    getdata <<= 1;
    SiS_SetSCLKLow(SiS_Pr);
    SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		    SiS_Pr->SiS_DDC_Index,
		    SiS_Pr->SiS_DDC_NData,
		    SiS_Pr->SiS_DDC_Data);
    SiS_SetSCLKHigh(SiS_Pr);
    temp = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
    if(temp & SiS_Pr->SiS_DDC_Data) getdata |= 0x01;
  }
  return getdata;
}

static unsigned short
SiS_SetSCLKLow(struct SiS_Private *SiS_Pr)
{
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NClk,
		  0x00);					/* SetSCLKLow()  */
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

static unsigned short
SiS_SetSCLKHigh(struct SiS_Private *SiS_Pr)
{
  unsigned short temp, watchdog=1000;

  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NClk,
		  SiS_Pr->SiS_DDC_Clk);  			/* SetSCLKHigh()  */
  do {
    temp = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index);
  } while((!(temp & SiS_Pr->SiS_DDC_Clk)) && --watchdog);
  if (!watchdog) {
  	return 0xFFFF;
  }
  SiS_DDC2Delay(SiS_Pr,SiS_I2CDELAYSHORT);
  return 0;
}

/* Check I2C acknowledge */
/* Returns 0 if ack ok, non-0 if ack not ok */
static unsigned short
SiS_CheckACK(struct SiS_Private *SiS_Pr)
{
  unsigned short tempah;

  SiS_SetSCLKLow(SiS_Pr);				           /* (SC->low) */
  SiS_SetRegANDOR(SiS_Pr->SiS_DDC_Port,
		  SiS_Pr->SiS_DDC_Index,
		  SiS_Pr->SiS_DDC_NData,
		  SiS_Pr->SiS_DDC_Data);			   /* (SD->high) */
  SiS_SetSCLKHigh(SiS_Pr);				           /* SC->high = clock impulse for ack */
  tempah = SiS_GetReg(SiS_Pr->SiS_DDC_Port,SiS_Pr->SiS_DDC_Index); /* Read SD */
  SiS_SetSCLKLow(SiS_Pr);				           /* SC->low = end of clock impulse */
  if(tempah & SiS_Pr->SiS_DDC_Data) return 1;			   /* Ack OK if bit = 0 */
  return 0;
}

/* End of I2C functions ----------------------- */


/* =============== SiS 315/330 O.E.M. ================= */

#ifdef CONFIG_FB_SIS_315

static unsigned short
GetRAMDACromptr(struct SiS_Private *SiS_Pr)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short romptr;

  if(SiS_Pr->ChipType < SIS_330) {
     romptr = SISGETROMW(0x128);
     if(SiS_Pr->SiS_VBType & VB_SIS30xB)
        romptr = SISGETROMW(0x12a);
  } else {
     romptr = SISGETROMW(0x1a8);
     if(SiS_Pr->SiS_VBType & VB_SIS30xB)
        romptr = SISGETROMW(0x1aa);
  }
  return romptr;
}

static unsigned short
GetLCDromptr(struct SiS_Private *SiS_Pr)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short romptr;

  if(SiS_Pr->ChipType < SIS_330) {
     romptr = SISGETROMW(0x120);
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV)
        romptr = SISGETROMW(0x122);
  } else {
     romptr = SISGETROMW(0x1a0);
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV)
        romptr = SISGETROMW(0x1a2);
  }
  return romptr;
}

static unsigned short
GetTVromptr(struct SiS_Private *SiS_Pr)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short romptr;

  if(SiS_Pr->ChipType < SIS_330) {
     romptr = SISGETROMW(0x114);
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV)
        romptr = SISGETROMW(0x11a);
  } else {
     romptr = SISGETROMW(0x194);
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV)
        romptr = SISGETROMW(0x19a);
  }
  return romptr;
}

static unsigned short
GetLCDPtrIndexBIOS(struct SiS_Private *SiS_Pr)
{
  unsigned short index;

  if((IS_SIS650) && (SiS_Pr->SiS_VBType & VB_SISLVDS)) {
     if(!(SiS_IsNotM650orLater(SiS_Pr))) {
        if((index = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xf0)) {
	   index >>= 4;
	   index *= 3;
	   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
           else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;
           return index;
	}
     }
  }

  index = SiS_GetBIOSLCDResInfo(SiS_Pr) & 0x0F;
  if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050)      index -= 5;
  if(SiS_Pr->SiS_VBType & VB_SIS301C) {  /* 1.15.20 and later (not VB specific) */
     if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) index -= 5;
     if(SiS_Pr->SiS_LCDResInfo == Panel_1280x768) index -= 5;
  } else {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) index -= 6;
  }
  index--;
  index *= 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;
  return index;
}

static unsigned short
GetLCDPtrIndex(struct SiS_Private *SiS_Pr)
{
  unsigned short index;

  index = ((SiS_GetBIOSLCDResInfo(SiS_Pr) & 0x0F) - 1) * 3;
  if(SiS_Pr->SiS_LCDInfo & DontExpandLCD)         index += 2;
  else if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) index++;
  return index;
}

static unsigned short
GetTVPtrIndex(struct SiS_Private *SiS_Pr)
{
  unsigned short index;

  index = 0;
  if(SiS_Pr->SiS_TVMode & TVSetPAL) index = 1;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) index = 2;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToYPbPr525750) index = 0;

  index <<= 1;

  if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) &&
     (SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
     index++;
  }

  return index;
}

static unsigned int
GetOEMTVPtr661_2_GEN(struct SiS_Private *SiS_Pr, int addme)
{
   unsigned short index = 0, temp = 0;

   if(SiS_Pr->SiS_TVMode & TVSetPAL)   index = 1;
   if(SiS_Pr->SiS_TVMode & TVSetPALM)  index = 2;
   if(SiS_Pr->SiS_TVMode & TVSetPALN)  index = 3;
   if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) index = 6;
   if(SiS_Pr->SiS_TVMode & TVSetNTSC1024) {
      index = 4;
      if(SiS_Pr->SiS_TVMode & TVSetPALM)  index++;
      if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) index = 7;
   }

   if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
      if((!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) ||
         (SiS_Pr->SiS_TVMode & TVSetTVSimuMode)) {
	 index += addme;
	 temp++;
      }
      temp += 0x0100;
   }
   return (unsigned int)(index | (temp << 16));
}

static unsigned int
GetOEMTVPtr661_2_OLD(struct SiS_Private *SiS_Pr)
{
   return (GetOEMTVPtr661_2_GEN(SiS_Pr, 8));
}

#if 0
static unsigned int
GetOEMTVPtr661_2_NEW(struct SiS_Private *SiS_Pr)
{
   return (GetOEMTVPtr661_2_GEN(SiS_Pr, 6));
}
#endif

static int
GetOEMTVPtr661(struct SiS_Private *SiS_Pr)
{
   int index = 0;

   if(SiS_Pr->SiS_TVMode & TVSetPAL)          index = 2;
   if(SiS_Pr->SiS_ROMNew) {
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) index = 4;
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) index = 6;
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) index = 8;
      if(SiS_Pr->SiS_TVMode & TVSetHiVision)  index = 10;
   } else {
      if(SiS_Pr->SiS_TVMode & TVSetHiVision)  index = 4;
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr525i) index = 6;
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr525p) index = 8;
      if(SiS_Pr->SiS_TVMode & TVSetYPbPr750p) index = 10;
   }

   if(SiS_Pr->SiS_TVMode & TVSetTVSimuMode) index++;

   return index;
}

static void
SetDelayComp(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short delay=0,index,myindex,temp,romptr=0;
  bool dochiptest = true;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x20,0xbf);
  } else {
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x35,0x7f);
  }

  /* Find delay (from ROM, internal tables, PCI subsystem) */

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) {			/* ------------ VGA */

     if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {
        romptr = GetRAMDACromptr(SiS_Pr);
     }
     if(romptr) delay = ROMAddr[romptr];
     else {
        delay = 0x04;
        if(SiS_Pr->SiS_VBType & VB_SIS30xB) {
	   if(IS_SIS650) {
	      delay = 0x0a;
	   } else if(IS_SIS740) {
	      delay = 0x00;
	   } else {
	      delay = 0x0c;
	   }
	} else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
           delay = 0x00;
	}
     }

  } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD|SetCRT2ToLCDA)) {  /* ----------	LCD/LCDA */

     bool gotitfrompci = false;

     /* Could we detect a PDC for LCD or did we get a user-defined? If yes, use it */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	if(SiS_Pr->PDC != -1) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0xf0,((SiS_Pr->PDC >> 1) & 0x0f));
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,0x7f,((SiS_Pr->PDC & 0x01) << 7));
	   return;
	}
     } else {
	if(SiS_Pr->PDCA != -1) {
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0x0f,((SiS_Pr->PDCA << 3) & 0xf0));
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x20,0xbf,((SiS_Pr->PDCA & 0x01) << 6));
	   return;
	}
     }

     /* Custom Panel? */

     if(SiS_Pr->SiS_LCDResInfo == Panel_Custom) {
        if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	   delay = 0x00;
	   if((SiS_Pr->PanelXRes <= 1280) && (SiS_Pr->PanelYRes <= 1024)) {
	      delay = 0x20;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0x0f,delay);
	} else {
	   delay = 0x0c;
	   if(SiS_Pr->SiS_VBType & VB_SIS301C) {
	      delay = 0x03;
	      if((SiS_Pr->PanelXRes > 1280) && (SiS_Pr->PanelYRes > 1024)) {
	         delay = 0x00;
	      }
	   } else if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	      if(IS_SIS740) delay = 0x01;
	      else          delay = 0x03;
	   }
	   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0xf0,delay);
	}
        return;
     }

     /* This is a piece of typical SiS crap: They code the OEM LCD
      * delay into the code, at no defined place in the BIOS.
      * We now have to start doing a PCI subsystem check here.
      */

     switch(SiS_Pr->SiS_CustomT) {
     case CUT_COMPAQ1280:
     case CUT_COMPAQ12802:
	if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) {
	   gotitfrompci = true;
	   dochiptest = false;
	   delay = 0x03;
	}
	break;
     case CUT_CLEVO1400:
     case CUT_CLEVO14002:
	gotitfrompci = true;
	dochiptest = false;
	delay = 0x02;
	break;
     case CUT_CLEVO1024:
     case CUT_CLEVO10242:
        if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   gotitfrompci = true;
	   dochiptest = false;
	   delay = 0x33;
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2D,delay);
	   delay &= 0x0f;
	}
	break;
     }

     /* Could we find it through the PCI ID? If no, use ROM or table */

     if(!gotitfrompci) {

        index = GetLCDPtrIndexBIOS(SiS_Pr);
        myindex = GetLCDPtrIndex(SiS_Pr);

        if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SISLVDS)) {

           if(SiS_IsNotM650orLater(SiS_Pr)) {

              if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {
	         /* Always use the second pointer on 650; some BIOSes */
                 /* still carry old 301 data at the first location    */
	         /* romptr = SISGETROMW(0x120);                       */
	         /* if(SiS_Pr->SiS_VBType & VB_SIS302LV)              */
	         romptr = SISGETROMW(0x122);
	         if(!romptr) return;
	         delay = ROMAddr[(romptr + index)];
	      } else {
                 delay = SiS310_LCDDelayCompensation_650301LV[myindex];
	      }

          } else {

             delay = SiS310_LCDDelayCompensation_651301LV[myindex];
	     if(SiS_Pr->SiS_VBType & (VB_SIS302LV | VB_SIS302ELV))
	        delay = SiS310_LCDDelayCompensation_651302LV[myindex];

          }

        } else if(SiS_Pr->SiS_UseROM 			      &&
		  (!(SiS_Pr->SiS_ROMNew))		      &&
	          (SiS_Pr->SiS_LCDResInfo != Panel_1280x1024) &&
		  (SiS_Pr->SiS_LCDResInfo != Panel_1280x768)  &&
		  (SiS_Pr->SiS_LCDResInfo != Panel_1280x960)  &&
		  (SiS_Pr->SiS_LCDResInfo != Panel_1600x1200)  &&
		  ((romptr = GetLCDromptr(SiS_Pr)))) {

	   /* Data for 1280x1024 wrong in 301B BIOS */
	   /* Data for 1600x1200 wrong in 301C BIOS */
	   delay = ROMAddr[(romptr + index)];

        } else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {

	   if(IS_SIS740) delay = 0x03;
	   else          delay = 0x00;

	} else {

           delay = SiS310_LCDDelayCompensation_301[myindex];
	   if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
	      if(IS_SIS740) delay = 0x01;
	      else if(SiS_Pr->ChipType <= SIS_315PRO) delay = SiS310_LCDDelayCompensation_3xx301LV[myindex];
	      else          delay = SiS310_LCDDelayCompensation_650301LV[myindex];
	   } else if(SiS_Pr->SiS_VBType & VB_SIS301C) {
	      if(IS_SIS740) delay = 0x01;  /* ? */
	      else          delay = 0x03;
	      if(SiS_Pr->SiS_LCDResInfo == Panel_1600x1200) delay = 0x00; /* experience */
	   } else if(SiS_Pr->SiS_VBType & VB_SIS30xB) {
	      if(IS_SIS740) delay = 0x01;
	      else          delay = SiS310_LCDDelayCompensation_3xx301B[myindex];
	   }

        }

     }  /* got it from PCI */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0x0F,((delay << 4) & 0xf0));
	dochiptest = false;
     }

  } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {			/* ------------ TV */

     index = GetTVPtrIndex(SiS_Pr);

     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SISLVDS)) {

        if(SiS_IsNotM650orLater(SiS_Pr)) {

           if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {
	      /* Always use the second pointer on 650; some BIOSes */
              /* still carry old 301 data at the first location    */
              /* romptr = SISGETROMW(0x114);			   */
	      /* if(SiS_Pr->SiS_VBType & VB_SIS302LV)              */
	      romptr = SISGETROMW(0x11a);
	      if(!romptr) return;
	      delay = ROMAddr[romptr + index];

	   } else {

	      delay = SiS310_TVDelayCompensation_301B[index];

	   }

        } else {

           switch(SiS_Pr->SiS_CustomT) {
	   case CUT_COMPAQ1280:
	   case CUT_COMPAQ12802:
	   case CUT_CLEVO1400:
	   case CUT_CLEVO14002:
	      delay = 0x02;
	      dochiptest = false;
	      break;
	   case CUT_CLEVO1024:
	   case CUT_CLEVO10242:
	      delay = 0x03;
	      dochiptest = false;
   	      break;
	   default:
              delay = SiS310_TVDelayCompensation_651301LV[index];
	      if(SiS_Pr->SiS_VBType & VB_SIS302LV) {
	         delay = SiS310_TVDelayCompensation_651302LV[index];
	      }
	   }
        }

     } else if((SiS_Pr->SiS_UseROM) && (!(SiS_Pr->SiS_ROMNew))) {

        romptr = GetTVromptr(SiS_Pr);
	if(!romptr) return;
	delay = ROMAddr[romptr + index];

     } else if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {

        delay = SiS310_TVDelayCompensation_LVDS[index];

     } else {

	delay = SiS310_TVDelayCompensation_301[index];
        if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	   if(IS_SIS740) {
	      delay = SiS310_TVDelayCompensation_740301B[index];
	      /* LV: use 301 data? BIOS bug? */
	   } else {
              delay = SiS310_TVDelayCompensation_301B[index];
	      if(SiS_Pr->SiS_VBType & VB_SIS301C) delay = 0x02;
	   }
	}

     }

     if(SiS_LCDAEnabled(SiS_Pr)) {
	delay &= 0x0f;
	dochiptest = false;
     }

  } else return;

  /* Write delay */

  if(SiS_Pr->SiS_VBType & VB_SISVB) {

     if(IS_SIS650 && (SiS_Pr->SiS_VBType & VB_SISLVDS) && dochiptest) {

        temp = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0xf0) >> 4;
        if(temp == 8) {		/* 1400x1050 BIOS (COMPAL) */
	   delay &= 0x0f;
	   delay |= 0xb0;
        } else if(temp == 6) {
           delay &= 0x0f;
	   delay |= 0xc0;
        } else if(temp > 7) {	/* 1280x1024 BIOS (which one?) */
	   delay = 0x35;
        }
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x2D,delay);

     } else {

        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);

     }

  } else {  /* LVDS */

     if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
     } else {
        if(IS_SIS650 && (SiS_Pr->SiS_IF_DEF_CH70xx != 0)) {
           delay <<= 4;
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0x0F,delay);
        } else {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2D,0xF0,delay);
        }
     }

  }

}

static void
SetAntiFlicker(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,temp1,romptr=0;

  if(SiS_Pr->SiS_TVMode & (TVSetYPbPr750p|TVSetYPbPr525p)) return;

  if(ModeNo<=0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVFlickerIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVFlickerIndex;

  temp = GetTVPtrIndex(SiS_Pr);
  temp >>= 1;  	  /* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */
  temp1 = temp;

  if(SiS_Pr->SiS_UseROM && (!(SiS_Pr->SiS_ROMNew))) {
     if(SiS_Pr->ChipType >= SIS_661) {
        temp1 = GetOEMTVPtr661(SiS_Pr);
        temp1 >>= 1;
        romptr = SISGETROMW(0x260);
        if(SiS_Pr->ChipType >= SIS_760) {
	   romptr = SISGETROMW(0x360);
	}
     } else if(SiS_Pr->ChipType >= SIS_330) {
        romptr = SISGETROMW(0x192);
     } else {
        romptr = SISGETROMW(0x112);
     }
  }

  if(romptr) {
     temp1 <<= 1;
     temp = ROMAddr[romptr + temp1 + index];
  } else {
     temp = SiS310_TVAntiFlick1[temp][index];
  }
  temp <<= 4;

  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8f,temp);  /* index 0A D[6:4] */
}

static void
SetEdgeEnhance(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,temp1,romptr=0;

  temp = temp1 = GetTVPtrIndex(SiS_Pr) >> 1; 	/* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */

  if(ModeNo <= 0x13)
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVEdgeIndex;
  else
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVEdgeIndex;

  if(SiS_Pr->SiS_UseROM && (!(SiS_Pr->SiS_ROMNew))) {
     if(SiS_Pr->ChipType >= SIS_661) {
        romptr = SISGETROMW(0x26c);
        if(SiS_Pr->ChipType >= SIS_760) {
	   romptr = SISGETROMW(0x36c);
	}
	temp1 = GetOEMTVPtr661(SiS_Pr);
        temp1 >>= 1;
     } else if(SiS_Pr->ChipType >= SIS_330) {
        romptr = SISGETROMW(0x1a4);
     } else {
        romptr = SISGETROMW(0x124);
     }
  }

  if(romptr) {
     temp1 <<= 1;
     temp = ROMAddr[romptr + temp1 + index];
  } else {
     temp = SiS310_TVEdge1[temp][index];
  }
  temp <<= 5;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x3A,0x1F,temp);  /* index 0A D[7:5] */
}

static void
SetYFilter(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex)
{
  unsigned short index, temp, i, j;

  if(ModeNo <= 0x13) {
     index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].VB_StTVYFilterIndex;
  } else {
     index = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].VB_ExtTVYFilterIndex;
  }

  temp = GetTVPtrIndex(SiS_Pr) >> 1;  /* 0: NTSC/YPbPr, 1: PAL, 2: HiTV */

  if(SiS_Pr->SiS_TVMode & TVSetNTSCJ)	     temp = 1;  /* NTSC-J uses PAL */
  else if(SiS_Pr->SiS_TVMode & TVSetPALM)    temp = 3;  /* PAL-M */
  else if(SiS_Pr->SiS_TVMode & TVSetPALN)    temp = 4;  /* PAL-N */
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) temp = 1;  /* HiVision uses PAL */

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
     for(i=0x35, j=0; i<=0x38; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
     }
     for(i=0x48; i<=0x4A; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter2[temp][index][j]);
     }
  } else {
     for(i=0x35, j=0; i<=0x38; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVYFilter1[temp][index][j]);
     }
  }
}

static void
SetPhaseIncr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,i,j,resinfo,romptr=0;
  unsigned int  lindex;

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) return;

  /* NTSC-J data not in BIOS, and already set in SetGroup2 */
  if(SiS_Pr->SiS_TVMode & TVSetNTSCJ) return;

  if((SiS_Pr->ChipType >= SIS_661) || SiS_Pr->SiS_ROMNew) {
     lindex = GetOEMTVPtr661_2_OLD(SiS_Pr) & 0xffff;
     lindex <<= 2;
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS_TVPhase[lindex + j]);
     }
     return;
  }

  /* PAL-M, PAL-N not in BIOS, and already set in SetGroup2 */
  if(SiS_Pr->SiS_TVMode & (TVSetPALM | TVSetPALN)) return;

  if(ModeNo<=0x13) {
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
  } else {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
  }

  temp = GetTVPtrIndex(SiS_Pr);
  /* 0: NTSC Graphics, 1: NTSC Text,    2: PAL Graphics,
   * 3: PAL Text,      4: HiTV Graphics 5: HiTV Text
   */
  if(SiS_Pr->SiS_UseROM) {
     romptr = SISGETROMW(0x116);
     if(SiS_Pr->ChipType >= SIS_330) {
        romptr = SISGETROMW(0x196);
     }
     if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
        romptr = SISGETROMW(0x11c);
	if(SiS_Pr->ChipType >= SIS_330) {
	   romptr = SISGETROMW(0x19c);
	}
	if((SiS_Pr->SiS_VBInfo & SetInSlaveMode) && (!(SiS_Pr->SiS_TVMode & TVSetTVSimuMode))) {
	   romptr = SISGETROMW(0x116);
	   if(SiS_Pr->ChipType >= SIS_330) {
              romptr = SISGETROMW(0x196);
           }
	}
     }
  }
  if(romptr) {
     romptr += (temp << 2);
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
     }
  } else {
     index = temp % 2;
     temp >>= 1;          /* 0:NTSC, 1:PAL, 2:HiTV */
     for(j=0, i=0x31; i<=0x34; i++, j++) {
        if(!(SiS_Pr->SiS_VBType & VB_SIS30xBLV))
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
        else if((!(SiS_Pr->SiS_VBInfo & SetInSlaveMode)) || (SiS_Pr->SiS_TVMode & TVSetTVSimuMode))
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr2[temp][index][j]);
        else
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS310_TVPhaseIncr1[temp][index][j]);
     }
  }

  if((SiS_Pr->SiS_VBType & VB_SIS30xBLV) && (!(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision))) {
     if((!(SiS_Pr->SiS_TVMode & (TVSetPAL | TVSetYPbPr525p | TVSetYPbPr750p))) && (ModeNo > 0x13)) {
        if((resinfo == SIS_RI_640x480) ||
	   (resinfo == SIS_RI_800x600)) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x31,0x21);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x32,0xf0);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x33,0xf5);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x34,0x7f);
	} else if(resinfo == SIS_RI_1024x768) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x31,0x1e);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x32,0x8b);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x33,0xfb);
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,0x34,0x7b);
	}
     }
  }
}

static void
SetDelayComp661(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
                unsigned short ModeIdIndex, unsigned short RTI)
{
   unsigned short delay = 0, romptr = 0, index, lcdpdcindex;
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;

   if(!(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToLCD | SetCRT2ToLCDA | SetCRT2ToRAMDAC)))
      return;

   /* 1. New ROM: VGA2 and LCD/LCDA-Pass1:1 */
   /* (If a custom mode is used, Pass1:1 is always set; hence we do this:) */

   if(SiS_Pr->SiS_ROMNew) {
      if((SiS_Pr->SiS_VBInfo & SetCRT2ToRAMDAC) 			||
         ((SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) &&
	  (SiS_Pr->SiS_LCDInfo & LCDPass11))) {
         index = 25;
         if(SiS_Pr->UseCustomMode) {
	    index = SiS_Pr->CSRClock;
         } else if(ModeNo > 0x13) {
            index = SiS_GetVCLK2Ptr(SiS_Pr,ModeNo,ModeIdIndex,RTI);
            index = SiS_Pr->SiS_VCLKData[index].CLOCK;
         }
	 if(index < 25) index = 25;
         index = ((index / 25) - 1) << 1;
         if((ROMAddr[0x5b] & 0x80) || (SiS_Pr->SiS_VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToLCD))) {
	    index++;
	 }
	 romptr = SISGETROMW(0x104);
         delay = ROMAddr[romptr + index];
         if(SiS_Pr->SiS_VBInfo & (SetCRT2ToRAMDAC | SetCRT2ToLCD)) {
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0xf0,((delay >> 1) & 0x0f));
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,0x7f,((delay & 0x01) << 7));
         } else {
            SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0x0f,((delay << 3) & 0xf0));
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x20,0xbf,((delay & 0x01) << 6));
         }
         return;
      }
   }

   /* 2. Old ROM: VGA2 and LCD/LCDA-Pass 1:1 */

   if(SiS_Pr->UseCustomMode) delay = 0x04;
   else if(ModeNo <= 0x13)   delay = 0x04;
   else                      delay = (SiS_Pr->SiS_RefIndex[RTI].Ext_PDC >> 4);
   delay |= (delay << 8);

   if(SiS_Pr->ChipType >= XGI_20) {

      delay = 0x0606;
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {

	 delay = 0x0404;
         if(SiS_Pr->SiS_XGIROM) {
	     index = GetTVPtrIndex(SiS_Pr);
	     if((romptr = SISGETROMW(0x35e))) {
	        delay = (ROMAddr[romptr + index] & 0x0f) << 1;
		delay |= (delay << 8);
	     }
	 }

	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) {
	    if(SiS_Pr->ChipType == XGI_40 && SiS_Pr->ChipRevision == 0x02) {
	       delay -= 0x0404;
	    }
	 }
      }

   } else if(SiS_Pr->ChipType >= SIS_340) {

      delay = 0x0606;
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
         delay = 0x0404;
      }
      /* TODO (eventually) */

   } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {

      /* 3. TV */

      index = GetOEMTVPtr661(SiS_Pr);
      if(SiS_Pr->SiS_ROMNew) {
         romptr = SISGETROMW(0x106);
	 if(SiS_Pr->SiS_VBType & VB_UMC) romptr += 12;
         delay = ROMAddr[romptr + index];
      } else {
         delay = 0x04;
	 if(index > 3) delay = 0;
      }

   } else if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {

      /* 4. LCD, LCDA (for new ROM only LV and non-Pass 1:1) */

      if( (SiS_Pr->SiS_LCDResInfo != Panel_Custom) &&
          ((romptr = GetLCDStructPtr661_2(SiS_Pr))) ) {

	 lcdpdcindex = (SiS_Pr->SiS_VBType & VB_UMC) ? 14 : 12;

	 /* For LVDS (and sometimes TMDS), the BIOS must know about the correct value */
	 delay = ROMAddr[romptr + lcdpdcindex + 1];	/* LCD  */
	 delay |= (ROMAddr[romptr + lcdpdcindex] << 8);	/* LCDA */

      } else {

         /* TMDS: Set our own, since BIOS has no idea */
	 /* (This is done on >=661 only, since <661 is calling this only for LVDS) */
         if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
	    switch(SiS_Pr->SiS_LCDResInfo) {
	    case Panel_1024x768:  delay = 0x0008; break;
	    case Panel_1280x720:  delay = 0x0004; break;
	    case Panel_1280x768:
	    case Panel_1280x768_2:delay = 0x0004; break;
	    case Panel_1280x800:
	    case Panel_1280x800_2:delay = 0x0004; break; /* Verified for 1280x800 */
	    case Panel_1280x854:  delay = 0x0004; break; /* FIXME */
	    case Panel_1280x1024: delay = 0x1e04; break;
	    case Panel_1400x1050: delay = 0x0004; break;
	    case Panel_1600x1200: delay = 0x0400; break;
	    case Panel_1680x1050: delay = 0x0e04; break;
	    default:
               if((SiS_Pr->PanelXRes <= 1024) && (SiS_Pr->PanelYRes <= 768)) {
	          delay = 0x0008;
	       } else if((SiS_Pr->PanelXRes == 1280) && (SiS_Pr->PanelYRes == 1024)) {
	          delay = 0x1e04;
               } else if((SiS_Pr->PanelXRes <= 1400) && (SiS_Pr->PanelYRes <= 1050)) {
	          delay = 0x0004;
	       } else if((SiS_Pr->PanelXRes <= 1600) && (SiS_Pr->PanelYRes <= 1200)) {
	          delay = 0x0400;
               } else
	          delay = 0x0e04;
	       break;
	    }
         }

	 /* Override by detected or user-set values */
	 /* (but only if, for some reason, we can't read value from BIOS) */
         if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) && (SiS_Pr->PDC != -1)) {
            delay = SiS_Pr->PDC & 0x1f;
         }
         if((SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) && (SiS_Pr->PDCA != -1)) {
            delay = (SiS_Pr->PDCA & 0x1f) << 8;
         }

      }

   }

   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      delay >>= 8;
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0x0f,((delay << 3) & 0xf0));
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x20,0xbf,((delay & 0x01) << 6));
   } else {
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x2d,0xf0,((delay >> 1) & 0x0f));
      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x35,0x7f,((delay & 0x01) << 7));
   }
}

static void
SetCRT2SyncDither661(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short RTI)
{
   unsigned short infoflag;
   unsigned char  temp;

   if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {

      if(ModeNo <= 0x13) {
         infoflag = SiS_GetRegByte(SiS_Pr->SiS_P3ca+2);
      } else if(SiS_Pr->UseCustomMode) {
         infoflag = SiS_Pr->CInfoFlag;
      } else {
         infoflag = SiS_Pr->SiS_RefIndex[RTI].Ext_InfoFlag;
      }

      if(!(SiS_Pr->SiS_LCDInfo & LCDPass11)) {
         infoflag = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37); /* No longer check D5 */
      }

      infoflag &= 0xc0;

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
         temp = (infoflag >> 6) | 0x0c;
         if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
	    temp ^= 0x04;
	    if(SiS_Pr->SiS_ModeType >= Mode24Bpp) temp |= 0x10;
	 }
         SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x1a,0xe0,temp);
      } else {
         temp = 0x30;
         if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) temp = 0x20;
         temp |= infoflag;
         SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x19,0x0f,temp);
         temp = 0;
         if(SiS_Pr->SiS_LCDInfo & LCDRGB18Bit) {
	    if(SiS_Pr->SiS_ModeType >= Mode24Bpp) temp |= 0x80;
	 }
         SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1a,0x7f,temp);
      }

   }
}

static void
SetPanelParms661(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short romptr, temp1, temp2;

   if(SiS_Pr->SiS_VBType & (VB_SISLVDS | VB_SIS30xC)) {
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x24,0x0f);
   }

   if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
      if(SiS_Pr->LVDSHL != -1) {
         SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,SiS_Pr->LVDSHL);
      }
   }

   if(SiS_Pr->SiS_ROMNew) {

      if((romptr = GetLCDStructPtr661_2(SiS_Pr))) {
         if(SiS_Pr->SiS_VBType & VB_SISLVDS) {
            temp1 = (ROMAddr[romptr] & 0x03) | 0x0c;
	    temp2 = 0xfc;
	    if(SiS_Pr->LVDSHL != -1) {
	      temp1 &= 0xfc;
	      temp2 = 0xf3;
	    }
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,temp2,temp1);
         }
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
            temp1 = (ROMAddr[romptr + 1] & 0x80) >> 1;
            SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0d,0xbf,temp1);
	 }
      }

   }
}

static void
SiS_OEM310Setting(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex, unsigned short RRTI)
{
   if((SiS_Pr->SiS_ROMNew) && (SiS_Pr->SiS_VBType & VB_SISLVDS)) {
      SetDelayComp661(SiS_Pr, ModeNo, ModeIdIndex, RRTI);
      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
         SetCRT2SyncDither661(SiS_Pr, ModeNo, RRTI);
         SetPanelParms661(SiS_Pr);
      }
   } else {
      SetDelayComp(SiS_Pr,ModeNo);
   }

   if((SiS_Pr->SiS_VBType & VB_SISVB) && (SiS_Pr->SiS_VBInfo & SetCRT2ToTV)) {
      SetAntiFlicker(SiS_Pr,ModeNo,ModeIdIndex);
      SetPhaseIncr(SiS_Pr,ModeNo,ModeIdIndex);
      SetYFilter(SiS_Pr,ModeNo,ModeIdIndex);
      if(SiS_Pr->SiS_VBType & VB_SIS301) {
         SetEdgeEnhance(SiS_Pr,ModeNo,ModeIdIndex);
      }
   }
}

static void
SiS_OEM661Setting(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
			unsigned short ModeIdIndex, unsigned short RRTI)
{
   if(SiS_Pr->SiS_VBType & VB_SISVB) {

      SetDelayComp661(SiS_Pr, ModeNo, ModeIdIndex, RRTI);

      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
         SetCRT2SyncDither661(SiS_Pr, ModeNo, RRTI);
         SetPanelParms661(SiS_Pr);
      }

      if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
         SetPhaseIncr(SiS_Pr, ModeNo, ModeIdIndex);
         SetYFilter(SiS_Pr, ModeNo, ModeIdIndex);
         SetAntiFlicker(SiS_Pr, ModeNo, ModeIdIndex);
         if(SiS_Pr->SiS_VBType & VB_SIS301) {
            SetEdgeEnhance(SiS_Pr, ModeNo, ModeIdIndex);
         }
      }
   }
}

/* FinalizeLCD
 * This finalizes some CRT2 registers for the very panel used.
 * If we have a backup if these registers, we use it; otherwise
 * we set the register according to most BIOSes. However, this
 * function looks quite different in every BIOS, so you better
 * pray that we have a backup...
 */
static void
SiS_FinalizeLCD(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned short tempcl,tempch,tempbl,tempbh,tempbx,tempax,temp;
  unsigned short resinfo,modeflag;

  if(!(SiS_Pr->SiS_VBType & VB_SISLVDS)) return;
  if(SiS_Pr->SiS_ROMNew) return;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->LVDSHL != -1) {
        SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,SiS_Pr->LVDSHL);
     }
  }

  if(SiS_Pr->SiS_LCDResInfo == Panel_Custom) return;
  if(SiS_Pr->UseCustomMode) return;

  switch(SiS_Pr->SiS_CustomT) {
  case CUT_COMPAQ1280:
  case CUT_COMPAQ12802:
  case CUT_CLEVO1400:
  case CUT_CLEVO14002:
     return;
  }

  if(ModeNo <= 0x13) {
     resinfo = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ResInfo;
     modeflag =  SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
  } else {
     resinfo = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_RESINFO;
     modeflag =  SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  if(IS_SIS650) {
     if(!(SiS_GetReg(SiS_Pr->SiS_P3d4, 0x5f) & 0xf0)) {
        if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
	   SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x02);
	} else {
           SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x1e,0x03);
	}
     }
  }

  if(SiS_Pr->SiS_CustomT == CUT_CLEVO1024) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
        /* Maybe all panels? */
        if(SiS_Pr->LVDSHL == -1) {
           SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	}
	return;
     }
  }

  if(SiS_Pr->SiS_CustomT == CUT_CLEVO10242) {
     if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
        if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   if(SiS_Pr->LVDSHL == -1) {
	      /* Maybe all panels? */
              SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	   }
	   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	      tempch = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
	      if(tempch == 3) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x25);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x00);
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x1b);
	      }
	   }
	   return;
	}
     }
  }

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToLCDA)) {
     if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	if(SiS_Pr->SiS_VBType & VB_SISEMI) {
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x2a,0x00);
#ifdef SET_EMI
	   SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x30,0x0c);
#endif
	   SiS_SetReg(SiS_Pr->SiS_Part4Port,0x34,0x10);
	}
     } else if(SiS_Pr->SiS_LCDResInfo == Panel_1280x1024) {
        if(SiS_Pr->LVDSHL == -1) {
           /* Maybe ACER only? */
           SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x24,0xfc,0x01);
	}
     }
     tempch = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) >> 4;
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	if(SiS_Pr->SiS_LCDResInfo == Panel_1400x1050) {
	   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1f,0x76);
	} else if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   if(tempch == 0x03) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x25);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x00);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x1b);
	   }
	   if(SiS_Pr->Backup && (SiS_Pr->Backup_Mode == ModeNo)) {
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,SiS_Pr->Backup_14);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,SiS_Pr->Backup_15);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,SiS_Pr->Backup_16);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,SiS_Pr->Backup_17);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,SiS_Pr->Backup_18);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,SiS_Pr->Backup_19);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1a,SiS_Pr->Backup_1a);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,SiS_Pr->Backup_1b);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,SiS_Pr->Backup_1c);
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,SiS_Pr->Backup_1d);
	   } else if(!(SiS_Pr->SiS_LCDInfo & DontExpandLCD)) {	/* 1.10.8w */
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,0x90);
	      if(ModeNo <= 0x13) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x11);
		 if((resinfo == 0) || (resinfo == 2)) return;
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x18);
		 if((resinfo == 1) || (resinfo == 3)) return;
	      }
	      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);
	      if((ModeNo > 0x13) && (resinfo == SIS_RI_1024x768)) {
	         SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x02);  /* 1.10.7u */
#if 0
	         tempbx = 806;  /* 0x326 */			 /* other older BIOSes */
		 tempbx--;
		 temp = tempbx & 0xff;
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,temp);
		 temp = (tempbx >> 8) & 0x03;
		 SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x1d,0xf8,temp);
#endif
	      }
	   } else if(ModeNo <= 0x13) {
	      if(ModeNo <= 1) {
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x70);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xff);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x48);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x12);
	      }
	      if(!(modeflag & HalfDCLK)) {
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x14,0x20);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x15,0x1a);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x16,0x28);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x17,0x00);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x4c);
		 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xdc);
		 if(ModeNo == 0x12) {
		    switch(tempch) {
		       case 0:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x19,0xdc);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1a,0x10);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1c,0x48);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1d,0x12);
			  break;
		       case 2:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,0x95);
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x48);
			  break;
		       case 3:
			  SiS_SetReg(SiS_Pr->SiS_Part1Port,0x1b,0x95);
			  break;
		    }
		 }
	      }
	   }
	}
     } else {
        tempcl = tempbh = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x01);
	tempcl &= 0x0f;
	tempbh &= 0x70;
	tempbh >>= 4;
	tempbl = SiS_GetReg(SiS_Pr->SiS_Part2Port,0x04);
	tempbx = (tempbh << 8) | tempbl;
	if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	   if((resinfo == SIS_RI_1024x768) || (!(SiS_Pr->SiS_LCDInfo & DontExpandLCD))) {
	      if(SiS_Pr->SiS_SetFlag & LCDVESATiming) {
	      	 tempbx = 770;
	      } else {
	         if(tempbx > 770) tempbx = 770;
		 if(SiS_Pr->SiS_VGAVDE < 600) {
		    tempax = 768 - SiS_Pr->SiS_VGAVDE;
		    tempax >>= 4;  				 /* 1.10.7w; 1.10.6s: 3;  */
		    if(SiS_Pr->SiS_VGAVDE <= 480)  tempax >>= 4; /* 1.10.7w; 1.10.6s: < 480; >>=1; */
		    tempbx -= tempax;
		 }
	      }
	   } else return;
	}
	temp = tempbx & 0xff;
	SiS_SetReg(SiS_Pr->SiS_Part2Port,0x04,temp);
	temp = ((tempbx & 0xff00) >> 4) | tempcl;
	SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x01,0x80,temp);
     }
  }
}

#endif

/*  =================  SiS 300 O.E.M. ================== */

#ifdef CONFIG_FB_SIS_300

static void
SetOEMLCDData2(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex,
		unsigned short RefTabIndex)
{
  unsigned short crt2crtc=0, modeflag, myindex=0;
  unsigned char  temp;
  int i;

  if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     crt2crtc = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_CRT2CRTC;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     crt2crtc = SiS_Pr->SiS_RefIndex[RefTabIndex].Ext_CRT2CRTC;
  }

  crt2crtc &= 0x3f;

  if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
     SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xdf);
  }

  if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
     if(modeflag & HalfDCLK) myindex = 1;

     if(SiS_Pr->SiS_SetFlag & LowModeTests) {
        for(i=0; i<7; i++) {
           if(barco_p1[myindex][crt2crtc][i][0]) {
	      SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,
	                      barco_p1[myindex][crt2crtc][i][0],
	   	   	      barco_p1[myindex][crt2crtc][i][2],
			      barco_p1[myindex][crt2crtc][i][1]);
	   }
        }
     }
     temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00);
     if(temp & 0x80) {
        temp = SiS_GetReg(SiS_Pr->SiS_Part1Port,0x18);
        temp++;
        SiS_SetReg(SiS_Pr->SiS_Part1Port,0x18,temp);
     }
  }
}

static unsigned short
GetOEMLCDPtr(struct SiS_Private *SiS_Pr, int Flag)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short tempbx=0,romptr=0;
  static const unsigned char customtable300[] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };
  static const unsigned char customtable630[] = {
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
  };

  if(SiS_Pr->ChipType == SIS_300) {

    tempbx = SiS_GetReg(SiS_Pr->SiS_P3d4,0x36) & 0x0f;
    if(SiS_Pr->SiS_VBType & VB_SIS301) tempbx &= 0x07;
    tempbx -= 2;
    if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx += 4;
    if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx += 3;
    }
    if(SiS_Pr->SiS_UseROM) {
       if(ROMAddr[0x235] & 0x80) {
          tempbx = SiS_Pr->SiS_LCDTypeInfo;
          if(Flag) {
	     romptr = SISGETROMW(0x255);
	     if(romptr) tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	     else       tempbx = customtable300[SiS_Pr->SiS_LCDTypeInfo];
             if(tempbx == 0xFF) return 0xFFFF;
          }
	  tempbx <<= 1;
	  if(!(SiS_Pr->SiS_SetFlag & LCDVESATiming)) tempbx++;
       }
    }

  } else {

    if(Flag) {
       if(SiS_Pr->SiS_UseROM) {
          romptr = SISGETROMW(0x255);
	  if(romptr) tempbx = ROMAddr[romptr + SiS_Pr->SiS_LCDTypeInfo];
	  else 	     tempbx = 0xff;
       } else {
          tempbx = customtable630[SiS_Pr->SiS_LCDTypeInfo];
       }
       if(tempbx == 0xFF) return 0xFFFF;
       tempbx <<= 2;
       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
       if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;
       return tempbx;
    }
    tempbx = SiS_Pr->SiS_LCDTypeInfo << 2;
    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) tempbx += 2;
    if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) tempbx++;

  }

  return tempbx;
}

static void
SetOEMLCDDelay(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,romptr=0;

  if(SiS_Pr->SiS_LCDResInfo == Panel_Custom) return;

  if(SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x02)) return;
     romptr = SISGETROMW(0x24b);
  }

  /* The Panel Compensation Delay should be set according to tables
   * here. Unfortunately, various BIOS versions don't care about
   * a uniform way using eg. ROM byte 0x220, but use different
   * hard coded delays (0x04, 0x20, 0x18) in SetGroup1().
   * Thus we don't set this if the user selected a custom pdc or if
   * we otherwise detected a valid pdc.
   */
  if(SiS_Pr->PDC != -1) return;

  temp = GetOEMLCDPtr(SiS_Pr, 0);

  if(SiS_Pr->UseCustomMode)
     index = 0;
  else
     index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_LCDDelayIndex;

  if(SiS_Pr->ChipType != SIS_300) {
     if(romptr) {
	romptr += (temp * 2);
	romptr = SISGETROMW(romptr);
	romptr += index;
	temp = ROMAddr[romptr];
     } else {
	if(SiS_Pr->SiS_VBType & VB_SISVB) {
    	   temp = SiS300_OEMLCDDelay2[temp][index];
	} else {
           temp = SiS300_OEMLCDDelay3[temp][index];
        }
     }
  } else {
     if(SiS_Pr->SiS_UseROM && (ROMAddr[0x235] & 0x80)) {
	if(romptr) {
	   romptr += (temp * 2);
	   romptr = SISGETROMW(romptr);
	   romptr += index;
	   temp = ROMAddr[romptr];
	} else {
	   temp = SiS300_OEMLCDDelay5[temp][index];
	}
     } else {
        if(SiS_Pr->SiS_UseROM) {
	   romptr = ROMAddr[0x249] | (ROMAddr[0x24a] << 8);
	   if(romptr) {
	      romptr += (temp * 2);
	      romptr = SISGETROMW(romptr);
	      romptr += index;
	      temp = ROMAddr[romptr];
	   } else {
	      temp = SiS300_OEMLCDDelay4[temp][index];
	   }
	} else {
	   temp = SiS300_OEMLCDDelay4[temp][index];
	}
     }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);  /* index 0A D[6:4] */
}

static void
SetOEMLCDData(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
#if 0  /* Unfinished; Data table missing */
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp;

  if((SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x237] & 0x01)) return;
     if(!(ROMAddr[0x237] & 0x04)) return;
     /* No rom pointer in BIOS header! */
  }

  temp = GetOEMLCDPtr(SiS_Pr, 1);
  if(temp == 0xFFFF) return;

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDHIndex;
  for(i=0x14, j=0; i<=0x17; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part1Port,i,SiS300_LCDHData[temp][index][j]);
  }
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1a, 0xf8, (SiS300_LCDHData[temp][index][j] & 0x07));

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex]._VB_LCDVIndex;
  SiS_SetReg(SiS_SiS_Part1Port,0x18, SiS300_LCDVData[temp][index][0]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x19, 0xF0, SiS300_LCDVData[temp][index][1]);
  SiS_SetRegANDOR(SiS_SiS_Part1Port,0x1A, 0xC7, (SiS300_LCDVData[temp][index][2] & 0x38));
  for(i=0x1b, j=3; i<=0x1d; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_Part1Port,i,SiS300_LCDVData[temp][index][j]);
  }
#endif
}

static unsigned short
GetOEMTVPtr(struct SiS_Private *SiS_Pr)
{
  unsigned short index;

  index = 0;
  if(!(SiS_Pr->SiS_VBInfo & SetInSlaveMode))  index += 4;
  if(SiS_Pr->SiS_VBType & VB_SISVB) {
     if(SiS_Pr->SiS_VBInfo & SetCRT2ToSCART)  index += 2;
     else if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) index += 3;
     else if(SiS_Pr->SiS_TVMode & TVSetPAL)   index += 1;
  } else {
     if(SiS_Pr->SiS_TVMode & TVSetCHOverScan) index += 2;
     if(SiS_Pr->SiS_TVMode & TVSetPAL)        index += 1;
  }
  return index;
}

static void
SetOEMTVDelay(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,romptr=0;

  if(SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x02)) return;
     romptr = SISGETROMW(0x241);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVDelayIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = SISGETROMW(romptr);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        temp = SiS300_OEMTVDelay301[temp][index];
     } else {
        temp = SiS300_OEMTVDelayLVDS[temp][index];
     }
  }
  temp &= 0x3c;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x13,~0x3C,temp);
}

static void
SetOEMAntiFlicker(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,romptr=0;

  if(SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x04)) return;
     romptr = SISGETROMW(0x243);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVFlickerIndex;

  if(romptr) {
     romptr += (temp * 2);
     romptr = SISGETROMW(romptr);
     romptr += index;
     temp = ROMAddr[romptr];
  } else {
     temp = SiS300_OEMTVFlicker[temp][index];
  }
  temp &= 0x70;
  SiS_SetRegANDOR(SiS_Pr->SiS_Part2Port,0x0A,0x8F,temp);
}

static void
SetOEMPhaseIncr(struct SiS_Private *SiS_Pr, unsigned short ModeNo,unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,i,j,temp,romptr=0;

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToHiVision) return;

  if(SiS_Pr->SiS_TVMode & (TVSetNTSC1024 | TVSetNTSCJ | TVSetPALM | TVSetPALN)) return;

  if(SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x08)) return;
     romptr = SISGETROMW(0x245);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVPhaseIndex;

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
     for(i=0x31, j=0; i<=0x34; i++, j++) {
        SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Phase2[temp][index][j]);
     }
  } else {
     if(romptr) {
        romptr += (temp * 2);
	romptr = SISGETROMW(romptr);
	romptr += (index * 4);
        for(i=0x31, j=0; i<=0x34; i++, j++) {
	   SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
	}
     } else {
        for(i=0x31, j=0; i<=0x34; i++, j++) {
           SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Phase1[temp][index][j]);
	}
     }
  }
}

static void
SetOEMYFilter(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
  unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
  unsigned short index,temp,i,j,romptr=0;

  if(SiS_Pr->SiS_VBInfo & (SetCRT2ToSCART | SetCRT2ToHiVision | SetCRT2ToYPbPr525750)) return;

  if(SiS_Pr->SiS_UseROM) {
     if(!(ROMAddr[0x238] & 0x01)) return;
     if(!(ROMAddr[0x238] & 0x10)) return;
     romptr = SISGETROMW(0x247);
  }

  temp = GetOEMTVPtr(SiS_Pr);

  if(SiS_Pr->SiS_TVMode & TVSetPALM)      temp = 8;
  else if(SiS_Pr->SiS_TVMode & TVSetPALN) temp = 9;
  /* NTSCJ uses NTSC filters */

  index = SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].VB_TVYFilterIndex;

  if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
      for(i=0x35, j=0; i<=0x38; i++, j++) {
       	SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
      for(i=0x48; i<=0x4A; i++, j++) {
     	SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter2[temp][index][j]);
      }
  } else {
      if((romptr) && (!(SiS_Pr->SiS_TVMode & (TVSetPALM|TVSetPALN)))) {
         romptr += (temp * 2);
	 romptr = SISGETROMW(romptr);
	 romptr += (index * 4);
	 for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg(SiS_Pr->SiS_Part2Port,i,ROMAddr[romptr + j]);
         }
      } else {
         for(i=0x35, j=0; i<=0x38; i++, j++) {
       	    SiS_SetReg(SiS_Pr->SiS_Part2Port,i,SiS300_Filter1[temp][index][j]);
         }
      }
  }
}

static unsigned short
SiS_SearchVBModeID(struct SiS_Private *SiS_Pr, unsigned short *ModeNo)
{
   unsigned short ModeIdIndex;
   unsigned char  VGAINFO = SiS_Pr->SiS_VGAINFO;

   if(*ModeNo <= 5) *ModeNo |= 1;

   for(ModeIdIndex=0; ; ModeIdIndex++) {
      if(SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].ModeID == *ModeNo) break;
      if(SiS_Pr->SiS_VBModeIDTable[ModeIdIndex].ModeID == 0xFF)    return 0;
   }

   if(*ModeNo != 0x07) {
      if(*ModeNo > 0x03) return ModeIdIndex;
      if(VGAINFO & 0x80) return ModeIdIndex;
      ModeIdIndex++;
   }

   if(VGAINFO & 0x10) ModeIdIndex++;   /* 400 lines */
	                               /* else 350 lines */
   return ModeIdIndex;
}

static void
SiS_OEM300Setting(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex,
		  unsigned short RefTableIndex)
{
  unsigned short OEMModeIdIndex = 0;

  if(!SiS_Pr->UseCustomMode) {
     OEMModeIdIndex = SiS_SearchVBModeID(SiS_Pr,&ModeNo);
     if(!(OEMModeIdIndex)) return;
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
     SetOEMLCDDelay(SiS_Pr, ModeNo, OEMModeIdIndex);
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        SetOEMLCDData(SiS_Pr, ModeNo, OEMModeIdIndex);
     }
  }
  if(SiS_Pr->UseCustomMode) return;
  if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
     SetOEMTVDelay(SiS_Pr, ModeNo,OEMModeIdIndex);
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        SetOEMAntiFlicker(SiS_Pr, ModeNo, OEMModeIdIndex);
    	SetOEMPhaseIncr(SiS_Pr, ModeNo, OEMModeIdIndex);
       	SetOEMYFilter(SiS_Pr, ModeNo, OEMModeIdIndex);
     }
  }
}
#endif

