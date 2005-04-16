/*
    Definitions for the Philips SAA7196 digital video decoder,
    scaler, and clock generator circuit (DESCpro), as used in
    the PlanB video input of the Powermac 7x00/8x00 series.
  
    Copyright (C) 1998 Michel Lanners (mlan@cpu.lu)

    The register defines are shamelessly copied from the meteor
    driver out of NetBSD (with permission),
    and are copyrighted (c) 1995 Mark Tinguely and Jim Lowe
    (Thanks !)
  
    Additional debugging and coding by Takashi Oe (toe@unlinfo.unl.edu)

    The default values used for PlanB are my mistakes.
*/

/* $Id: saa7196.h,v 1.5 1999/03/26 23:28:47 mlan Exp $ */

#ifndef _SAA7196_H_
#define _SAA7196_H_

#define SAA7196_NUMREGS	0x31	/* Number of registers (used)*/
#define NUM_SUPPORTED_NORM 3	/* Number of supported norms by PlanB */

/* Decoder part: */
#define SAA7196_IDEL    0x00    /* Increment delay */
#define SAA7196_HSB5    0x01    /* H-sync begin; 50 hz */
#define SAA7196_HSS5    0x02    /* H-sync stop; 50 hz */
#define SAA7196_HCB5    0x03    /* H-clamp begin; 50 hz */
#define SAA7196_HCS5    0x04    /* H-clamp stop; 50 hz */
#define SAA7196_HSP5    0x05    /* H-sync after PHI1; 50 hz */
#define SAA7196_LUMC    0x06    /* Luminance control */
#define SAA7196_HUEC    0x07    /* Hue control */
#define SAA7196_CKTQ    0x08    /* Colour Killer Threshold QAM (PAL, NTSC) */
#define SAA7196_CKTS    0x09    /* Colour Killer Threshold SECAM */
#define SAA7196_PALS    0x0a    /* PAL switch sensitivity */
#define SAA7196_SECAMS  0x0b    /* SECAM switch sensitivity */
#define SAA7196_CGAINC  0x0c    /* Chroma gain control */
#define SAA7196_STDC    0x0d    /* Standard/Mode control */
#define SAA7196_IOCC    0x0e    /* I/O and Clock Control */
#define SAA7196_CTRL1   0x0f    /* Control #1 */
#define SAA7196_CTRL2   0x10    /* Control #2 */
#define SAA7196_CGAINR  0x11    /* Chroma Gain Reference */
#define SAA7196_CSAT    0x12    /* Chroma Saturation */
#define SAA7196_CONT    0x13    /* Luminance Contrast */
#define SAA7196_HSB6    0x14    /* H-sync begin; 60 hz */
#define SAA7196_HSS6    0x15    /* H-sync stop; 60 hz */
#define SAA7196_HCB6    0x16    /* H-clamp begin; 60 hz */
#define SAA7196_HCS6    0x17    /* H-clamp stop; 60 hz */
#define SAA7196_HSP6    0x18    /* H-sync after PHI1; 60 hz */
#define SAA7196_BRIG    0x19    /* Luminance Brightness */

/* Scaler part: */
#define SAA7196_FMTS    0x20    /* Formats and sequence */
#define SAA7196_OUTPIX  0x21    /* Output data pixel/line */
#define SAA7196_INPIX   0x22    /* Input data pixel/line */
#define SAA7196_HWS     0x23    /* Horiz. window start */
#define SAA7196_HFILT   0x24    /* Horiz. filter */
#define SAA7196_OUTLINE 0x25    /* Output data lines/field */
#define SAA7196_INLINE  0x26    /* Input data lines/field */
#define SAA7196_VWS     0x27    /* Vertical window start */
#define SAA7196_VYP     0x28    /* AFS/vertical Y processing */
#define SAA7196_VBS     0x29    /* Vertical Bypass start */
#define SAA7196_VBCNT   0x2a    /* Vertical Bypass count */
#define SAA7196_VBP     0x2b    /* veritcal Bypass Polarity */
#define SAA7196_VLOW    0x2c    /* Colour-keying lower V limit */
#define SAA7196_VHIGH   0x2d    /* Colour-keying upper V limit */
#define SAA7196_ULOW    0x2e    /* Colour-keying lower U limit */
#define SAA7196_UHIGH   0x2f    /* Colour-keying upper U limit */
#define SAA7196_DPATH   0x30    /* Data path setting  */

/* Initialization default values: */

unsigned char saa_regs[NUM_SUPPORTED_NORM][SAA7196_NUMREGS] = {

/* PAL, 768x576 (no scaling), composite video-in */
/* Decoder: */
      { 0x50, 0x30, 0x00, 0xe8, 0xb6, 0xe5, 0x63, 0xff,
	0xfe, 0xf0, 0xfe, 0xe0, 0x20, 0x06, 0x3b, 0x98,
	0x00, 0x59, 0x41, 0x45, 0x34, 0x0a, 0xf4, 0xd2,
	0xe9, 0xa2,
/* Padding */
		    0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/* Scaler: */
	0x72, 0x80, 0x00, 0x03, 0x8d, 0x20, 0x20, 0x12,
	0xa5, 0x12, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00,
	0x87 },

/* NTSC, 640x480? (no scaling), composite video-in */
/* Decoder: */
      { 0x50, 0x30, 0x00, 0xe8, 0xb6, 0xe5, 0x50, 0x00,
	0xf8, 0xf0, 0xfe, 0xe0, 0x00, 0x06, 0x3b, 0x98,
	0x00, 0x2c, 0x3d, 0x40, 0x34, 0x0a, 0xf4, 0xd2,
	0xe9, 0x98,
/* Padding */
		    0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/* Scaler: */
	0x72, 0x80, 0x80, 0x03, 0x89, 0xf0, 0xf0, 0x0d,
	0xa0, 0x0d, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00,
	0x87 },

/* SECAM, 768x576 (no scaling), composite video-in */
/* Decoder: */
      { 0x50, 0x30, 0x00, 0xe8, 0xb6, 0xe5, 0x63, 0xff,
	0xfe, 0xf0, 0xfe, 0xe0, 0x20, 0x07, 0x3b, 0x98,
	0x00, 0x59, 0x41, 0x45, 0x34, 0x0a, 0xf4, 0xd2,
	0xe9, 0xa2,
/* Padding */
		    0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
/* Scaler: */
	0x72, 0x80, 0x00, 0x03, 0x8d, 0x20, 0x20, 0x12,
	0xa5, 0x12, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00,
	0x87 }
	};

#endif /* _SAA7196_H_ */
