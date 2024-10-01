/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ARCH_PARISC_TERMBITS_H__
#define __ARCH_PARISC_TERMBITS_H__

#include <asm-generic/termbits-common.h>

typedef unsigned int	tcflag_t;

#define NCCS 19
struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
};

struct termios2 {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
	speed_t c_ispeed;		/* input speed */
	speed_t c_ospeed;		/* output speed */
};

struct ktermios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_line;			/* line discipline */
	cc_t c_cc[NCCS];		/* control characters */
	speed_t c_ispeed;		/* input speed */
	speed_t c_ospeed;		/* output speed */
};

/* c_cc characters */
#define VINTR		 0
#define VQUIT		 1
#define VERASE		 2
#define VKILL		 3
#define VEOF		 4
#define VTIME		 5
#define VMIN		 6
#define VSWTC		 7
#define VSTART		 8
#define VSTOP		 9
#define VSUSP		10
#define VEOL		11
#define VREPRINT	12
#define VDISCARD	13
#define VWERASE		14
#define VLNEXT		15
#define VEOL2		16

/* c_iflag bits */
#define IUCLC	0x0200
#define IXON	0x0400
#define IXOFF	0x1000
#define IMAXBEL	0x4000
#define IUTF8	0x8000

/* c_oflag bits */
#define OLCUC	0x00002
#define ONLCR	0x00004
#define NLDLY	0x00100
#define   NL0	0x00000
#define   NL1	0x00100
#define CRDLY	0x00600
#define   CR0	0x00000
#define   CR1	0x00200
#define   CR2	0x00400
#define   CR3	0x00600
#define TABDLY	0x01800
#define   TAB0	0x00000
#define   TAB1	0x00800
#define   TAB2	0x01000
#define   TAB3	0x01800
#define   XTABS	0x01800
#define BSDLY	0x02000
#define   BS0	0x00000
#define   BS1	0x02000
#define VTDLY	0x04000
#define   VT0	0x00000
#define   VT1	0x04000
#define FFDLY	0x08000
#define   FF0	0x00000
#define   FF1	0x08000

/* c_cflag bit meaning */
#define CBAUD		0x0000100f
#define CSIZE		0x00000030
#define   CS5		0x00000000
#define   CS6		0x00000010
#define   CS7		0x00000020
#define   CS8		0x00000030
#define CSTOPB		0x00000040
#define CREAD		0x00000080
#define PARENB		0x00000100
#define PARODD		0x00000200
#define HUPCL		0x00000400
#define CLOCAL		0x00000800
#define CBAUDEX		0x00001000
#define BOTHER		0x00001000
#define     B57600	0x00001001
#define    B115200	0x00001002
#define    B230400	0x00001003
#define    B460800	0x00001004
#define    B500000	0x00001005
#define    B576000	0x00001006
#define    B921600	0x00001007
#define   B1000000	0x00001008
#define   B1152000	0x00001009
#define   B1500000	0x0000100a
#define   B2000000	0x0000100b
#define   B2500000	0x0000100c
#define   B3000000	0x0000100d
#define   B3500000	0x0000100e
#define   B4000000	0x0000100f
#define CIBAUD		0x100f0000		/* input baud rate */

/* c_lflag bits */
#define ISIG	0x00001
#define ICANON	0x00002
#define XCASE	0x00004
#define ECHO	0x00008
#define ECHOE	0x00010
#define ECHOK	0x00020
#define ECHONL	0x00040
#define NOFLSH	0x00080
#define TOSTOP	0x00100
#define ECHOCTL	0x00200
#define ECHOPRT	0x00400
#define ECHOKE	0x00800
#define FLUSHO	0x01000
#define PENDIN	0x04000
#define IEXTEN	0x08000
#define EXTPROC	0x10000

/* tcsetattr uses these */
#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

#endif
