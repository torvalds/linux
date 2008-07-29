#ifndef __ARCH_PARISC_IOCTLS_H__
#define __ARCH_PARISC_IOCTLS_H__

#include <asm/ioctl.h>

/* 0x54 is just a magic number to make these relatively unique ('T') */

#define TCGETS		_IOR('T', 16, struct termios) /* TCGETATTR */
#define TCSETS		_IOW('T', 17, struct termios) /* TCSETATTR */
#define TCSETSW		_IOW('T', 18, struct termios) /* TCSETATTRD */
#define TCSETSF		_IOW('T', 19, struct termios) /* TCSETATTRF */
#define TCGETA		_IOR('T', 1, struct termio)
#define TCSETA		_IOW('T', 2, struct termio)
#define TCSETAW		_IOW('T', 3, struct termio)
#define TCSETAF		_IOW('T', 4, struct termio)
#define TCSBRK		_IO('T', 5)
#define TCXONC		_IO('T', 6)
#define TCFLSH		_IO('T', 7)
#define TIOCEXCL	0x540C
#define TIOCNXCL	0x540D
#define TIOCSCTTY	0x540E
#define TIOCGPGRP	_IOR('T', 30, int)
#define TIOCSPGRP	_IOW('T', 29, int)
#define TIOCOUTQ	0x5411
#define TIOCSTI		0x5412
#define TIOCGWINSZ	0x5413
#define TIOCSWINSZ	0x5414
#define TIOCMGET	0x5415
#define TIOCMBIS	0x5416
#define TIOCMBIC	0x5417
#define TIOCMSET	0x5418
#define TIOCGSOFTCAR	0x5419
#define TIOCSSOFTCAR	0x541A
#define FIONREAD	0x541B
#define TIOCINQ		FIONREAD
#define TIOCLINUX	0x541C
#define TIOCCONS	0x541D
#define TIOCGSERIAL	0x541E
#define TIOCSSERIAL	0x541F
#define TIOCPKT		0x5420
#define FIONBIO		0x5421
#define TIOCNOTTY	0x5422
#define TIOCSETD	0x5423
#define TIOCGETD	0x5424
#define TCSBRKP		0x5425	/* Needed for POSIX tcsendbreak() */
#define TIOCSBRK	0x5427  /* BSD compatibility */
#define TIOCCBRK	0x5428  /* BSD compatibility */
#define TIOCGSID	_IOR('T', 20, int) /* Return the session ID of FD */
#define TCGETS2		_IOR('T',0x2A, struct termios2)
#define TCSETS2		_IOW('T',0x2B, struct termios2)
#define TCSETSW2	_IOW('T',0x2C, struct termios2)
#define TCSETSF2	_IOW('T',0x2D, struct termios2)
#define TIOCGPTN	_IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TIOCSPTLCK	_IOW('T',0x31, int)  /* Lock/unlock Pty */

#define FIONCLEX	0x5450  /* these numbers need to be adjusted. */
#define FIOCLEX		0x5451
#define FIOASYNC	0x5452
#define TIOCSERCONFIG	0x5453
#define TIOCSERGWILD	0x5454
#define TIOCSERSWILD	0x5455
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x5458 /* For debugging only */
#define TIOCSERGETLSR   0x5459 /* Get line status register */
#define TIOCSERGETMULTI 0x545A /* Get multiport config  */
#define TIOCSERSETMULTI 0x545B /* Set multiport config */

#define TIOCMIWAIT	0x545C	/* wait for a change on serial input line(s) */
#define TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */
#define TIOCGHAYESESP   0x545E  /* Get Hayes ESP configuration */
#define TIOCSHAYESESP   0x545F  /* Set Hayes ESP configuration */
#define FIOQSIZE	0x5460	/* Get exact space used by quota */

#define TIOCSTART	0x5461
#define TIOCSTOP	0x5462
#define TIOCSLTC	0x5462

/* Used for packet mode */
#define TIOCPKT_DATA		 0
#define TIOCPKT_FLUSHREAD	 1
#define TIOCPKT_FLUSHWRITE	 2
#define TIOCPKT_STOP		 4
#define TIOCPKT_START		 8
#define TIOCPKT_NOSTOP		16
#define TIOCPKT_DOSTOP		32

#define TIOCSER_TEMT    0x01	/* Transmitter physically empty */

#endif /* _ASM_PARISC_IOCTLS_H */
