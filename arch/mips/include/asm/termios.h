/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 2000, 2001 by Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_TERMIOS_H
#define _ASM_TERMIOS_H

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
        char    t_suspc;        /* stop process signal */
        char    t_dsuspc;       /* delayed stop process signal */
        char    t_rprntc;       /* reprint line */
        char    t_flushc;       /* flush output (toggles) */
        char    t_werasc;       /* word erase */
        char    t_lnextc;       /* literal next character */
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

#ifdef __KERNEL__
#include <asm/uaccess.h>

/*
 *	intr=^C		quit=^\		erase=del	kill=^U
 *	vmin=\1		vtime=\0	eol2=\0		swtc=\0
 *	start=^Q	stop=^S		susp=^Z		vdsusp=
 *	reprint=^R	discard=^U	werase=^W	lnext=^V
 *	eof=^D		eol=\0
 */
#define INIT_C_CC "\003\034\177\025\1\0\0\0\021\023\032\0\022\017\027\026\004\0"
#endif

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

#ifdef __KERNEL__

#include <linux/string.h>

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
static inline int user_termio_to_kernel_termios(struct ktermios *termios,
	struct termio __user *termio)
{
	unsigned short iflag, oflag, cflag, lflag;
	unsigned int err;

	if (!access_ok(VERIFY_READ, termio, sizeof(struct termio)))
		return -EFAULT;

	err = __get_user(iflag, &termio->c_iflag);
	termios->c_iflag = (termios->c_iflag & 0xffff0000) | iflag;
	err |=__get_user(oflag, &termio->c_oflag);
	termios->c_oflag = (termios->c_oflag & 0xffff0000) | oflag;
	err |=__get_user(cflag, &termio->c_cflag);
	termios->c_cflag = (termios->c_cflag & 0xffff0000) | cflag;
	err |=__get_user(lflag, &termio->c_lflag);
	termios->c_lflag = (termios->c_lflag & 0xffff0000) | lflag;
	err |=__get_user(termios->c_line, &termio->c_line);
	if (err)
		return -EFAULT;

	if (__copy_from_user(termios->c_cc, termio->c_cc, NCC))
		return -EFAULT;

	return 0;
}

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
static inline int kernel_termios_to_user_termio(struct termio __user *termio,
	struct ktermios *termios)
{
	int err;

	if (!access_ok(VERIFY_WRITE, termio, sizeof(struct termio)))
		return -EFAULT;

	err = __put_user(termios->c_iflag, &termio->c_iflag);
	err |= __put_user(termios->c_oflag, &termio->c_oflag);
	err |= __put_user(termios->c_cflag, &termio->c_cflag);
	err |= __put_user(termios->c_lflag, &termio->c_lflag);
	err |= __put_user(termios->c_line, &termio->c_line);
	if (err)
		return -EFAULT;

	if (__copy_to_user(termio->c_cc, termios->c_cc, NCC))
		return -EFAULT;

	return 0;
}

static inline int user_termios_to_kernel_termios(struct ktermios __user *k,
	struct termios2 *u)
{
	return copy_from_user(k, u, sizeof(struct termios2)) ? -EFAULT : 0;
}

static inline int kernel_termios_to_user_termios(struct termios2 __user *u,
	struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios2)) ? -EFAULT : 0;
}

static inline int user_termios_to_kernel_termios_1(struct ktermios *k,
	struct termios __user *u)
{
	return copy_from_user(k, u, sizeof(struct termios)) ? -EFAULT : 0;
}

static inline int kernel_termios_to_user_termios_1(struct termios __user *u,
	struct ktermios *k)
{
	return copy_to_user(u, k, sizeof(struct termios)) ? -EFAULT : 0;
}

#endif /* defined(__KERNEL__) */

#endif /* _ASM_TERMIOS_H */
