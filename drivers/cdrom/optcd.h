/*	linux/include/linux/optcd.h - Optics Storage 8000 AT CDROM driver
	$Id: optcd.h,v 1.2 1996/01/15 18:43:44 root Exp root $

	Copyright (C) 1995 Leo Spiekman (spiekman@dutette.et.tudelft.nl)


	Configuration file for linux/drivers/cdrom/optcd.c
*/

#ifndef _LINUX_OPTCD_H
#define _LINUX_OPTCD_H


/* I/O base of drive. Drive uses base to base+2.
   This setting can be overridden with the kernel or insmod command
   line option 'optcd=<portbase>'. Use address of 0 to disable driver. */
#define OPTCD_PORTBASE	0x340


/* enable / disable parts of driver by define / undef */
#define	MULTISESSION		/* multisession support (ALPHA) */


/* Change 0 to 1 to debug various parts of the driver */
#define	DEBUG_DRIVE_IF	0	/* Low level drive interface */
#define	DEBUG_CONV	0	/* Address conversions */
#define	DEBUG_BUFFERS	0	/* Buffering and block size conversion */
#define	DEBUG_REQUEST	0	/* Request mechanism */
#define	DEBUG_STATE	0	/* State machine */
#define	DEBUG_TOC	0	/* Q-channel and Table of Contents */
#define	DEBUG_MULTIS	0	/* Multisession code */
#define	DEBUG_VFS	0	/* VFS interface */


/* Don't touch these unless you know what you're doing. */

/* Various timeout loop repetition counts. */
#define BUSY_TIMEOUT		10000000	/* for busy wait */
#define FAST_TIMEOUT		100000		/* ibid. for probing */
#define SLEEP_TIMEOUT		6000		/* for timer wait */
#define MULTI_SEEK_TIMEOUT	1000		/* for timer wait */
#define READ_TIMEOUT		6000		/* for poll wait */
#define STOP_TIMEOUT		2000		/* for poll wait */
#define RESET_WAIT		5000		/* busy wait at drive reset */

/* # of buffers for block size conversion. 6 is optimal for my setup (P75),
   giving 280 kb/s, with 0.4% CPU usage. Experiment to find your optimal
   setting */
#define N_BUFS		6


#endif /* _LINUX_OPTCD_H */
