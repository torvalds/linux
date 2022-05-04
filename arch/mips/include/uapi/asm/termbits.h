/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 99, 2001, 06 Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 */
#ifndef _ASM_TERMBITS_H
#define _ASM_TERMBITS_H

#include <linux/posix_types.h>

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

/*
 * The ABI says nothing about NCC but seems to use NCCS as
 * replacement for it in struct termio
 */
#define NCCS	23
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
#define VINTR		 0		/* Interrupt character [ISIG].	*/
#define VQUIT		 1		/* Quit character [ISIG].  */
#define VERASE		 2		/* Erase character [ICANON].  */
#define VKILL		 3		/* Kill-line character [ICANON].  */
#define VMIN		 4		/* Minimum number of bytes read at once [!ICANON].  */
#define VTIME		 5		/* Time-out value (tenths of a second) [!ICANON].  */
#define VEOL2		 6		/* Second EOL character [ICANON].  */
#define VSWTC		 7		/* ??? */
#define VSWTCH		VSWTC
#define VSTART		 8		/* Start (X-ON) character [IXON, IXOFF].  */
#define VSTOP		 9		/* Stop (X-OFF) character [IXON, IXOFF].  */
#define VSUSP		10		/* Suspend character [ISIG].  */
#if 0
/*
 * VDSUSP is not supported
 */
#define VDSUSP		11		/* Delayed suspend character [ISIG].  */
#endif
#define VREPRINT	12		/* Reprint-line character [ICANON].  */
#define VDISCARD	13		/* Discard character [IEXTEN].	*/
#define VWERASE		14		/* Word-erase character [ICANON].  */
#define VLNEXT		15		/* Literal-next character [IEXTEN].  */
#define VEOF		16		/* End-of-file character [ICANON].  */
#define VEOL		17		/* End-of-line character [ICANON].  */

/* c_iflag bits */
#define IGNBRK	0x00001		/* Ignore break condition.  */
#define BRKINT	0x00002		/* Signal interrupt on break.  */
#define IGNPAR	0x00004		/* Ignore characters with parity errors.  */
#define PARMRK	0x00008		/* Mark parity and framing errors.  */
#define INPCK	0x00010		/* Enable input parity check.  */
#define ISTRIP	0x00020		/* Strip 8th bit off characters.  */
#define INLCR	0x00040		/* Map NL to CR on input.  */
#define IGNCR	0x00080		/* Ignore CR.  */
#define ICRNL	0x00100		/* Map CR to NL on input.  */
#define IUCLC	0x00200		/* Map upper case to lower case on input.  */
#define IXON	0x00400		/* Enable start/stop output control.  */
#define IXANY	0x00800		/* Any character will restart after stop.  */
#define IXOFF	0x01000		/* Enable start/stop input control.  */
#define IMAXBEL	0x02000		/* Ring bell when input queue is full.	*/
#define IUTF8	0x04000		/* Input is UTF-8 */

/* c_oflag bits */
#define OPOST	0x00001		/* Perform output processing.  */
#define OLCUC	0x00002		/* Map lower case to upper case on output.  */
#define ONLCR	0x00004		/* Map NL to CR-NL on output.  */
#define OCRNL	0x00008
#define ONOCR	0x00010
#define ONLRET	0x00020
#define OFILL	0x00040
#define OFDEL	0x00080
#define NLDLY	0x00100
#define	  NL0	0x00000
#define	  NL1	0x00100
#define CRDLY	0x00600
#define	  CR0	0x00000
#define	  CR1	0x00200
#define	  CR2	0x00400
#define	  CR3	0x00600
#define TABDLY	0x01800
#define	  TAB0	0x00000
#define	  TAB1	0x00800
#define	  TAB2	0x01000
#define	  TAB3	0x01800
#define	  XTABS	0x01800
#define BSDLY	0x02000
#define	  BS0	0x00000
#define	  BS1	0x02000
#define VTDLY	0x04000
#define	  VT0	0x00000
#define	  VT1	0x04000
#define FFDLY	0x08000
#define	  FF0	0x00000
#define	  FF1	0x08000
/*
#define PAGEOUT ???
#define WRAP	???
 */

/* c_cflag bit meaning */
#define CBAUD		0x0000100f
#define	 B0		0x00000000	/* hang up */
#define	 B50		0x00000001
#define	 B75		0x00000002
#define	 B110		0x00000003
#define	 B134		0x00000004
#define	 B150		0x00000005
#define	 B200		0x00000006
#define	 B300		0x00000007
#define	 B600		0x00000008
#define	 B1200		0x00000009
#define	 B1800		0x0000000a
#define	 B2400		0x0000000b
#define	 B4800		0x0000000c
#define	 B9600		0x0000000d
#define	 B19200		0x0000000e
#define	 B38400		0x0000000f
#define EXTA B19200
#define EXTB B38400
#define CSIZE		0x00000030	/* Number of bits per byte (mask) */
#define	  CS5		0x00000000	/* 5 bits per byte */
#define	  CS6		0x00000010	/* 6 bits per byte */
#define	  CS7		0x00000020	/* 7 bits per byte */
#define	  CS8		0x00000030	/* 8 bits per byte */
#define CSTOPB		0x00000040	/* Two stop bits instead of one */
#define CREAD		0x00000080	/* Enable receiver */
#define PARENB		0x00000100	/* Parity enable */
#define PARODD		0x00000200	/* Odd parity instead of even */
#define HUPCL		0x00000400	/* Hang up on last close */
#define CLOCAL		0x00000800	/* Ignore modem status lines */
#define CBAUDEX		0x00001000
#define	   BOTHER	0x00001000
#define	   B57600	0x00001001
#define	  B115200	0x00001002
#define	  B230400	0x00001003
#define	  B460800	0x00001004
#define	  B500000	0x00001005
#define	  B576000	0x00001006
#define	  B921600	0x00001007
#define	 B1000000	0x00001008
#define	 B1152000	0x00001009
#define	 B1500000	0x0000100a
#define	 B2000000	0x0000100b
#define	 B2500000	0x0000100c
#define	 B3000000	0x0000100d
#define	 B3500000	0x0000100e
#define	 B4000000	0x0000100f
#define CIBAUD		0x100f0000	/* input baud rate */
#define CMSPAR		0x40000000	/* mark or space (stick) parity */
#define CRTSCTS		0x80000000	/* flow control */

#define IBSHIFT 16		/* Shift from CBAUD to CIBAUD */

/* c_lflag bits */
#define ISIG	0x00001		/* Enable signals.  */
#define ICANON	0x00002		/* Do erase and kill processing.  */
#define XCASE	0x00004
#define ECHO	0x00008		/* Enable echo.	 */
#define ECHOE	0x00010		/* Visual erase for ERASE.  */
#define ECHOK	0x00020		/* Echo NL after KILL.	*/
#define ECHONL	0x00040		/* Echo NL even if ECHO is off.	 */
#define NOFLSH	0x00080		/* Disable flush after interrupt.  */
#define IEXTEN	0x00100		/* Enable DISCARD and LNEXT.  */
#define ECHOCTL	0x00200		/* Echo control characters as ^X.  */
#define ECHOPRT	0x00400		/* Hardcopy visual erase.  */
#define ECHOKE	0x00800		/* Visual erase for KILL.  */
#define FLUSHO	0x02000
#define PENDIN	0x04000		/* Retype pending input (state).  */
#define TOSTOP	0x08000		/* Send SIGTTOU for background output.	*/
#define ITOSTOP	TOSTOP
#define EXTPROC	0x10000		/* External processing on pty */

/* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
#define TIOCSER_TEMT	0x01	/* Transmitter physically empty */

/* tcflow() and TCXONC use these */
#define TCOOFF		0	/* Suspend output.  */
#define TCOON		1	/* Restart suspended output.  */
#define TCIOFF		2	/* Send a STOP character.  */
#define TCION		3	/* Send a START character.  */

/* tcflush() and TCFLSH use these */
#define TCIFLUSH	0	/* Discard data received but not yet read.  */
#define TCOFLUSH	1	/* Discard data written but not yet sent.  */
#define TCIOFLUSH	2	/* Discard all pending data.  */

/* tcsetattr uses these */
#define TCSANOW		TCSETS	/* Change immediately.	*/
#define TCSADRAIN	TCSETSW /* Change when pending output is written.  */
#define TCSAFLUSH	TCSETSF /* Flush pending input before changing.	 */

#endif /* _ASM_TERMBITS_H */
