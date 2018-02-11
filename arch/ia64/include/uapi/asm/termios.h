/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Modified 1999
 *	David Mosberger-Tang <davidm@hpl.hp.com>, Hewlett-Packard Co
 *
 * 99/01/28	Added N_IRDA and N_SMSBLOCK
 */
#ifndef _UAPI_ASM_IA64_TERMIOS_H
#define _UAPI_ASM_IA64_TERMIOS_H


#include <asm/termbits.h>
#include <asm/ioctls.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCC 8
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};

/* modem lines */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_OUT1	0x2000
#define TIOCM_OUT2	0x4000
#define TIOCM_LOOP	0x8000

/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */


#endif /* _UAPI_ASM_IA64_TERMIOS_H */
