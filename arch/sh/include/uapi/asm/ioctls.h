/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __ASM_SH_IOCTLS_H
#define __ASM_SH_IOCTLS_H

#include <asm/ioctl.h>

#define FIOCLEX		_IO('f', 1)
#define FIONCLEX	_IO('f', 2)
#define FIOASYNC	_IOW('f', 125, int)
#define FIONBIO		_IOW('f', 126, int)
#define FIONREAD	_IOR('f', 127, int)
#define TIOCINQ		FIONREAD
#define FIOQSIZE	_IOR('f', 128, loff_t)

#define TCGETS		0x5401
#define TCSETS		0x5402
#define TCSETSW		0x5403
#define TCSETSF		0x5404

#define TCGETA		0x80127417	/* _IOR('t', 23, struct termio) */
#define TCSETA		0x40127418	/* _IOW('t', 24, struct termio) */
#define TCSETAW		0x40127419	/* _IOW('t', 25, struct termio) */
#define TCSETAF		0x4012741C	/* _IOW('t', 28, struct termio) */

#define TCSBRK		_IO('t', 29)
#define TCXONC		_IO('t', 30)
#define TCFLSH		_IO('t', 31)

#define TIOCSWINSZ	0x40087467	/* _IOW('t', 103, struct winsize) */
#define TIOCGWINSZ	0x80087468	/* _IOR('t', 104, struct winsize) */
#define	TIOCSTART	_IO('t', 110)		/* start output, like ^Q */
#define	TIOCSTOP	_IO('t', 111)		/* stop output, like ^S */
#define TIOCOUTQ        _IOR('t', 115, int)     /* output queue size */

#define TIOCSPGRP	_IOW('t', 118, int)
#define TIOCGPGRP	_IOR('t', 119, int)

#define TIOCEXCL	_IO('T', 12) /* 0x540C */
#define TIOCNXCL	_IO('T', 13) /* 0x540D */
#define TIOCSCTTY	_IO('T', 14) /* 0x540E */

#define TIOCSTI		_IOW('T', 18, char) /* 0x5412 */
#define TIOCMGET	_IOR('T', 21, unsigned int) /* 0x5415 */
#define TIOCMBIS	_IOW('T', 22, unsigned int) /* 0x5416 */
#define TIOCMBIC	_IOW('T', 23, unsigned int) /* 0x5417 */
#define TIOCMSET	_IOW('T', 24, unsigned int) /* 0x5418 */
# define TIOCM_LE	0x001
# define TIOCM_DTR	0x002
# define TIOCM_RTS	0x004
# define TIOCM_ST	0x008
# define TIOCM_SR	0x010
# define TIOCM_CTS	0x020
# define TIOCM_CAR	0x040
# define TIOCM_RNG	0x080
# define TIOCM_DSR	0x100
# define TIOCM_CD	TIOCM_CAR
# define TIOCM_RI	TIOCM_RNG

#define TIOCGSOFTCAR	_IOR('T', 25, unsigned int) /* 0x5419 */
#define TIOCSSOFTCAR	_IOW('T', 26, unsigned int) /* 0x541A */
#define TIOCLINUX	_IOW('T', 28, char) /* 0x541C */
#define TIOCCONS	_IO('T', 29) /* 0x541D */
#define TIOCGSERIAL	0x803C541E	/* _IOR('T', 30, struct serial_struct) 0x541E */
#define TIOCSSERIAL	0x403C541F	/* _IOW('T', 31, struct serial_struct) 0x541F */
#define TIOCPKT		_IOW('T', 32, int) /* 0x5420 */
# define TIOCPKT_DATA		 0
# define TIOCPKT_FLUSHREAD	 1
# define TIOCPKT_FLUSHWRITE	 2
# define TIOCPKT_STOP		 4
# define TIOCPKT_START		 8
# define TIOCPKT_NOSTOP		16
# define TIOCPKT_DOSTOP		32
# define TIOCPKT_IOCTL		64


#define TIOCNOTTY	_IO('T', 34) /* 0x5422 */
#define TIOCSETD	_IOW('T', 35, int) /* 0x5423 */
#define TIOCGETD	_IOR('T', 36, int) /* 0x5424 */
#define TCSBRKP		_IOW('T', 37, int) /* 0x5425 */	/* Needed for POSIX tcsendbreak() */
#define TIOCSBRK	_IO('T', 39) /* 0x5427 */ /* BSD compatibility */
#define TIOCCBRK	_IO('T', 40) /* 0x5428 */ /* BSD compatibility */
#define TIOCGSID	_IOR('T', 41, pid_t) /* 0x5429 */ /* Return the session ID of FD */
#define TCGETS2		_IOR('T', 42, struct termios2)
#define TCSETS2		_IOW('T', 43, struct termios2)
#define TCSETSW2	_IOW('T', 44, struct termios2)
#define TCSETSF2	_IOW('T', 45, struct termios2)
#define TIOCGRS485	_IOR('T', 46, struct serial_rs485)
#define TIOCSRS485	_IOWR('T', 47, struct serial_rs485)
#define TIOCGPTN	_IOR('T',0x30, unsigned int) /* Get Pty Number (of pty-mux device) */
#define TIOCSPTLCK	_IOW('T',0x31, int)  /* Lock/unlock Pty */
#define TIOCGDEV	_IOR('T',0x32, unsigned int) /* Get primary device node of /dev/console */
#define TIOCSIG		_IOW('T',0x36, int)  /* Generate signal on Pty slave */
#define TIOCVHANGUP	_IO('T', 0x37)
#define TIOCGPKT	_IOR('T', 0x38, int) /* Get packet mode state */
#define TIOCGPTLCK	_IOR('T', 0x39, int) /* Get Pty lock state */
#define TIOCGEXCL	_IOR('T', 0x40, int) /* Get exclusive mode state */
#define TIOCGPTPEER	_IO('T', 0x41) /* Safely open the slave */
#define TIOCGISO7816	_IOR('T', 0x42, struct serial_iso7816)
#define TIOCSISO7816	_IOWR('T', 0x43, struct serial_iso7816)

#define TIOCSERCONFIG	_IO('T', 83) /* 0x5453 */
#define TIOCSERGWILD	_IOR('T', 84,  int) /* 0x5454 */
#define TIOCSERSWILD	_IOW('T', 85,  int) /* 0x5455 */
#define TIOCGLCKTRMIOS	0x5456
#define TIOCSLCKTRMIOS	0x5457
#define TIOCSERGSTRUCT	0x80d85458	/* _IOR('T', 88, struct async_struct) 0x5458 */ /* For debugging only */
#define TIOCSERGETLSR   _IOR('T', 89, unsigned int) /* 0x5459 */ /* Get line status register */
  /* ioctl (fd, TIOCSERGETLSR, &result) where result may be as below */
# define TIOCSER_TEMT    0x01	/* Transmitter physically empty */
#define TIOCSERGETMULTI 0x80A8545A	/* _IOR('T', 90, struct serial_multiport_struct) 0x545A */ /* Get multiport config */
#define TIOCSERSETMULTI 0x40A8545B	/* _IOW('T', 91, struct serial_multiport_struct) 0x545B */ /* Set multiport config */

#define TIOCMIWAIT	_IO('T', 92) /* 0x545C */	/* wait for a change on serial input line(s) */
#define TIOCGICOUNT	0x545D	/* read serial port inline interrupt counts */

#endif /* __ASM_SH_IOCTLS_H */
