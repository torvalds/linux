/* Copyright (c) 1996 by Manish Vachharajani */

#ifndef _LINUX_VFC_IOCTLS_H_
#define	_LINUX_VFC_IOCTLS_H_

	/* IOCTLs */
#define VFC_IOCTL(a)          (('j' << 8) | a)
#define VFCGCTRL	(VFC_IOCTL (0))	        /* get vfc attributes */
#define VFCSCTRL	(VFC_IOCTL (1))  	/* set vfc attributes */
#define VFCGVID		(VFC_IOCTL (2)) 	/* get video decoder attributes */
#define VFCSVID		(VFC_IOCTL (3))	        /* set video decoder attributes */
#define VFCHUE		(VFC_IOCTL (4))   	/* set hue */
#define VFCPORTCHG	(VFC_IOCTL (5))  	/* change port */
#define VFCRDINFO	(VFC_IOCTL (6))  	/* read info */

	/* Options for setting the vfc attributes and status */
#define MEMPRST		0x1	/* reset FIFO ptr. */
#define CAPTRCMD	0x2	/* start capture and wait */
#define DIAGMODE	0x3	/* diag mode */
#define NORMMODE	0x4	/* normal mode */
#define CAPTRSTR	0x5	/* start capture */
#define CAPTRWAIT	0x6	/* wait for capture to finish */


	/* Options for the decoder */
#define STD_NTSC	0x1	/* NTSC mode */
#define STD_PAL		0x2	/* PAL mode */
#define COLOR_ON	0x3	/* force color ON */
#define MONO		0x4	/* force color OFF */

	/* Values returned by ioctl 2 */

#define NO_LOCK	        1
#define NTSC_COLOR	2
#define NTSC_NOCOLOR    3
#define PAL_COLOR	4
#define PAL_NOCOLOR	5

/* Not too sure what this does yet */
	/* Options for setting Field number */
#define ODD_FIELD	0x1
#define EVEN_FIELD	0x0
#define ACTIVE_ONLY     0x2
#define NON_ACTIVE	0x0

/* Debug options */
#define VFC_I2C_SEND 0
#define VFC_I2C_RECV 1

struct vfc_debug_inout
{
	unsigned long addr;
	unsigned long ret;
	unsigned long len;
	unsigned char __user *buffer;
};

#endif /* _LINUX_VFC_IOCTLS_H_ */
