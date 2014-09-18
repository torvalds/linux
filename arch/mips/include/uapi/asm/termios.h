/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 2000, 2001 by Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _UAPI_ASM_TERMIOS_H
#define _UAPI_ASM_TERMIOS_H

#include <linux/errno.h>
#include <asm/termbits.h>
#include <asm/ioctls.h>

struct sgttyb {
	char	sg_ispeed;
	char	sg_ospeed;
	char	sg_erase;
	char	sg_kill;
	int	sg_flags;	/* SGI special - int, not short */
};

struct tchars {
	char	t_intrc;
	char	t_quitc;
	char	t_startc;
	char	t_stopc;
	char	t_eofc;
	char	t_brkc;
};

struct ltchars {
	char	t_suspc;	/* stop process signal */
	char	t_dsuspc;	/* delayed stop process signal */
	char	t_rprntc;	/* reprint line */
	char	t_flushc;	/* flush output (toggles) */
	char	t_werasc;	/* word erase */
	char	t_lnextc;	/* literal next character */
};

/* TIOCGSIZE, TIOCSSIZE not defined yet.  Only needed for SunOS source
   compatibility anyway ... */

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCC	8
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	char c_line;			/* line discipline */
	unsigned char c_cc[NCCS];	/* control characters */
};


/* modem lines */
#define TIOCM_LE	0x001		/* line enable */
#define TIOCM_DTR	0x002		/* data terminal ready */
#define TIOCM_RTS	0x004		/* request to send */
#define TIOCM_ST	0x010		/* secondary transmit */
#define TIOCM_SR	0x020		/* secondary receive */
#define TIOCM_CTS	0x040		/* clear to send */
#define TIOCM_CAR	0x100		/* carrier detect */
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RNG	0x200		/* ring */
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_DSR	0x400		/* data set ready */
#define TIOCM_OUT1	0x2000
#define TIOCM_OUT2	0x4000
#define TIOCM_LOOP	0x8000


#endif /* _UAPI_ASM_TERMIOS_H */
