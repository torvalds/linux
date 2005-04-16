/*
 *  drivers/s390/char/sclp_tty.h
 *    interface to the SCLP-read/write driver
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Peschke <mpeschke@de.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __SCLP_TTY_H__
#define __SCLP_TTY_H__

#include <linux/ioctl.h>
#include <linux/termios.h>
#include <linux/tty_driver.h>

/* This is the type of data structures storing sclp ioctl setting. */
struct sclp_ioctls {
	unsigned short htab;
	unsigned char echo;
	unsigned short columns;
	unsigned char final_nl;
	unsigned short max_sccb;
	unsigned short kmem_sccb;	/* can't be modified at run time */
	unsigned char tolower;
	unsigned char delim;
};

/* must be unique, FIXME: must be added in Documentation/ioctl_number.txt */
#define SCLP_IOCTL_LETTER 'B'

/* set width of horizontal tabulator */
#define TIOCSCLPSHTAB	_IOW(SCLP_IOCTL_LETTER, 0, unsigned short)
/* enable/disable echo of input (independent from line discipline) */
#define TIOCSCLPSECHO	_IOW(SCLP_IOCTL_LETTER, 1, unsigned char)
/* set number of colums for output */
#define TIOCSCLPSCOLS	_IOW(SCLP_IOCTL_LETTER, 2, unsigned short)
/* enable/disable writing without final new line character */
#define TIOCSCLPSNL	_IOW(SCLP_IOCTL_LETTER, 4, signed char)
/* set the maximum buffers size for output, rounded up to next 4kB boundary */
#define TIOCSCLPSOBUF	_IOW(SCLP_IOCTL_LETTER, 5, unsigned short)
/* set initial (default) sclp ioctls */
#define TIOCSCLPSINIT	_IO(SCLP_IOCTL_LETTER, 6)
/* enable/disable conversion from upper to lower case of input */
#define TIOCSCLPSCASE	_IOW(SCLP_IOCTL_LETTER, 7, unsigned char)
/* set special character used for separating upper and lower case, */
/* 0x00 disables this feature */
#define TIOCSCLPSDELIM	_IOW(SCLP_IOCTL_LETTER, 9, unsigned char)

/* get width of horizontal tabulator */
#define TIOCSCLPGHTAB	_IOR(SCLP_IOCTL_LETTER, 10, unsigned short)
/* Is echo of input enabled ? (independent from line discipline) */
#define TIOCSCLPGECHO	_IOR(SCLP_IOCTL_LETTER, 11, unsigned char)
/* get number of colums for output */
#define TIOCSCLPGCOLS	_IOR(SCLP_IOCTL_LETTER, 12, unsigned short)
/* Is writing without final new line character enabled ? */
#define TIOCSCLPGNL	_IOR(SCLP_IOCTL_LETTER, 14, signed char)
/* get the maximum buffers size for output */
#define TIOCSCLPGOBUF	_IOR(SCLP_IOCTL_LETTER, 15, unsigned short)
/* Is conversion from upper to lower case of input enabled ? */
#define TIOCSCLPGCASE	_IOR(SCLP_IOCTL_LETTER, 17, unsigned char)
/* get special character used for separating upper and lower case, */
/* 0x00 disables this feature */
#define TIOCSCLPGDELIM	_IOR(SCLP_IOCTL_LETTER, 19, unsigned char)
/* get the number of buffers/pages got from kernel at startup */
#define TIOCSCLPGKBUF	_IOR(SCLP_IOCTL_LETTER, 20, unsigned short)

extern struct tty_driver *sclp_tty_driver;

#endif	/* __SCLP_TTY_H__ */
