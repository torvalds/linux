/*
 * ntp_tty.h - header file for serial lines handling
 */
#ifndef NTP_TTY_H
#define NTP_TTY_H

/*
 * use only one tty model - no use in initialising
 * a tty in three ways
 * HAVE_TERMIOS is preferred over HAVE_SYSV_TTYS over HAVE_BSD_TTYS
 */

#if defined(HAVE_TERMIOS_H) || defined(HAVE_SYS_TERMIOS_H)
# define HAVE_TERMIOS
#elif defined(HAVE_TERMIO_H)
# define HAVE_SYSV_TTYS
#elif defined(HAVE_SGTTY_H)
# define HAVE_BSD_TTYS
#endif

#if !defined(VMS) && !defined(SYS_VXWORKS)
# if	!defined(HAVE_SYSV_TTYS) \
	&& !defined(HAVE_BSD_TTYS) \
	&& !defined(HAVE_TERMIOS)
#include "ERROR: no tty type defined!"
# endif
#endif /* !VMS && !SYS_VXWORKS*/

#if defined(HAVE_BSD_TTYS)
#include <sgtty.h>
#define TTY	struct sgttyb
#endif /* HAVE_BSD_TTYS */

#if defined(HAVE_SYSV_TTYS)
#include <termio.h>
#define TTY	struct termio
#ifndef tcsetattr
#define tcsetattr(fd, cmd, arg) ioctl(fd, cmd, arg)
#endif
#ifndef TCSANOW
#define TCSANOW	TCSETA
#endif
#ifndef TCIFLUSH
#define TCIFLUSH 0
#endif
#ifndef TCOFLUSH
#define TCOFLUSH 1
#endif
#ifndef TCIOFLUSH
#define TCIOFLUSH 2
#endif
#ifndef tcflush
#define tcflush(fd, arg) ioctl(fd, TCFLSH, arg)
#endif
#endif /* HAVE_SYSV_TTYS */

#if defined(HAVE_TERMIOS)
# if defined(HAVE_TERMIOS_H)
#  ifdef TERMIOS_NEEDS__SVID3
#   define _SVID3
#  endif
#  include <termios.h>
#  ifdef TERMIOS_NEEDS__SVID3
#   undef _SVID3
#  endif
# elif defined(HAVE_SYS_TERMIOS_H)
#  include <sys/termios.h>
# endif
# define TTY	struct termios
#endif

#if defined(HAVE_SYS_MODEM_H)
#include <sys/modem.h>
#endif

/*
 * Line discipline flags.  The depredated ones required line discipline
 * or streams modules to be installed/loaded in the kernel and are now
 * ignored.  Leave the LDISC_CLK and other deprecated symbols defined
 * until 2013 or 2014 to avoid complicating the use of newer drivers on
 * older ntpd, which is often as easy as dropping in the refclock *.c.
 */
#define LDISC_STD	0x000	/* standard */
#define LDISC_CLK	0x001	/* depredated tty_clk \n */
#define LDISC_CLKPPS	0x002	/* depredated tty_clk \377 */
#define LDISC_ACTS	0x004	/* depredated tty_clk #* */
#define LDISC_CHU	0x008	/* depredated */
#define LDISC_PPS	0x010	/* depredated */
#define LDISC_RAW	0x020	/* raw binary */
#define LDISC_ECHO	0x040	/* enable echo */
#define	LDISC_REMOTE	0x080	/* remote mode */
#define	LDISC_7O1	0x100	/* 7-bit, odd parity for Z3801A */

/* function prototypes for ntp_tty.c */
#if !defined(SYS_VXWORKS) && !defined(SYS_WINNT)
# if defined(HAVE_TERMIOS) || defined(HAVE_SYSV_TTYS) || \
     defined(HAVE_BSD_TTYS)
extern	int	ntp_tty_setup(int, u_int, u_int);
extern	int	ntp_tty_ioctl(int, u_int);
# endif
#endif

#endif /* NTP_TTY_H */
