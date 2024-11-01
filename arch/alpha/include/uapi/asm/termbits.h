/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ALPHA_TERMBITS_H
#define _ALPHA_TERMBITS_H

#include <asm-generic/termbits-common.h>

typedef unsigned int	tcflag_t;

/*
 * termios type and macro definitions.  Be careful about adding stuff
 * to this file since it's used in GNU libc and there are strict rules
 * concerning namespace pollution.
 */

#define NCCS 19
struct termios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_cc[NCCS];		/* control characters */
	cc_t c_line;			/* line discipline (== c_cc[19]) */
	speed_t c_ispeed;		/* input speed */
	speed_t c_ospeed;		/* output speed */
};

/* Alpha has identical termios and termios2 */

struct termios2 {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_cc[NCCS];		/* control characters */
	cc_t c_line;			/* line discipline (== c_cc[19]) */
	speed_t c_ispeed;		/* input speed */
	speed_t c_ospeed;		/* output speed */
};

/* Alpha has matching termios and ktermios */

struct ktermios {
	tcflag_t c_iflag;		/* input mode flags */
	tcflag_t c_oflag;		/* output mode flags */
	tcflag_t c_cflag;		/* control mode flags */
	tcflag_t c_lflag;		/* local mode flags */
	cc_t c_cc[NCCS];		/* control characters */
	cc_t c_line;			/* line discipline (== c_cc[19]) */
	speed_t c_ispeed;		/* input speed */
	speed_t c_ospeed;		/* output speed */
};

/* c_cc characters */
#define VEOF		 0
#define VEOL		 1
#define VEOL2		 2
#define VERASE		 3
#define VWERASE		 4
#define VKILL		 5
#define VREPRINT	 6
#define VSWTC		 7
#define VINTR		 8
#define VQUIT		 9
#define VSUSP		10
#define VSTART		12
#define VSTOP		13
#define VLNEXT		14
#define VDISCARD	15
#define VMIN		16
#define VTIME		17

/* c_iflag bits */
#define IXON	0x0200
#define IXOFF	0x0400
#define IUCLC	0x1000
#define IMAXBEL	0x2000
#define IUTF8	0x4000

/* c_oflag bits */
#define ONLCR	0x00002
#define OLCUC	0x00004
#define NLDLY	0x00300
#define   NL0	0x00000
#define   NL1	0x00100
#define   NL2	0x00200
#define   NL3	0x00300
#define TABDLY	0x00c00
#define   TAB0	0x00000
#define   TAB1	0x00400
#define   TAB2	0x00800
#define   TAB3	0x00c00
#define CRDLY	0x03000
#define   CR0	0x00000
#define   CR1	0x01000
#define   CR2	0x02000
#define   CR3	0x03000
#define FFDLY	0x04000
#define   FF0	0x00000
#define   FF1	0x04000
#define BSDLY	0x08000
#define   BS0	0x00000
#define   BS1	0x08000
#define VTDLY	0x10000
#define   VT0	0x00000
#define   VT1	0x10000
/*
 * Should be equivalent to TAB3, see description of TAB3 in
 * POSIX.1-2008, Ch. 11.2.3 "Output Modes"
 */
#define XTABS	TAB3

/* c_cflag bit meaning */
#define CBAUD		0x0000001f
#define CBAUDEX		0x00000000
#define BOTHER		0x0000001f
#define     B57600	0x00000010
#define    B115200	0x00000011
#define    B230400	0x00000012
#define    B460800	0x00000013
#define    B500000	0x00000014
#define    B576000	0x00000015
#define    B921600	0x00000016
#define   B1000000	0x00000017
#define   B1152000	0x00000018
#define   B1500000	0x00000019
#define   B2000000	0x0000001a
#define   B2500000	0x0000001b
#define   B3000000	0x0000001c
#define   B3500000	0x0000001d
#define   B4000000	0x0000001e
#define CSIZE		0x00000300
#define   CS5		0x00000000
#define   CS6		0x00000100
#define   CS7		0x00000200
#define   CS8		0x00000300
#define CSTOPB		0x00000400
#define CREAD		0x00000800
#define PARENB		0x00001000
#define PARODD		0x00002000
#define HUPCL		0x00004000
#define CLOCAL		0x00008000
#define CIBAUD		0x001f0000

/* c_lflag bits */
#define ISIG	0x00000080
#define ICANON	0x00000100
#define XCASE	0x00004000
#define ECHO	0x00000008
#define ECHOE	0x00000002
#define ECHOK	0x00000004
#define ECHONL	0x00000010
#define NOFLSH	0x80000000
#define TOSTOP	0x00400000
#define ECHOCTL	0x00000040
#define ECHOPRT	0x00000020
#define ECHOKE	0x00000001
#define FLUSHO	0x00800000
#define PENDIN	0x20000000
#define IEXTEN	0x00000400
#define EXTPROC	0x10000000

/* Values for the OPTIONAL_ACTIONS argument to `tcsetattr'.  */
#define	TCSANOW		0
#define	TCSADRAIN	1
#define	TCSAFLUSH	2

#endif /* _ALPHA_TERMBITS_H */
