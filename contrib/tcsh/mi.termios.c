/* $Header: /p/tcsh/cvsroot/tcsh/mi.termios.c,v 1.5 2006/03/02 18:46:44 christos Exp $ */
/* termios.c - fake termios interface using sgtty interface 
 * 	       by Magnus Doell and Bruce Evans.
 *
 */
#include "sh.h"
RCSID("$tcsh: mi.termios.c,v 1.5 2006/03/02 18:46:44 christos Exp $")

#if defined(_MINIX) && !defined(_MINIX_VMD)


/* Undefine everything that clashes with sgtty.h. */
#undef B0
#undef B50
#undef B75
#undef B110
#undef B134
#undef B150
#undef B200
#undef B300
#undef B600
#undef B1200
#undef B1800
#undef B2400
#undef B4800
#undef B9600
#undef B19200
#undef B28800
#undef B38400
#undef B57600
#undef B115200
/* Do not #undef CRMOD. We want a warning when they differ! */
#undef ECHO
/* Do not #undef XTABS. We want a warning when they differ! */

/* Redefine some of the termios.h names just undefined with 'T_' prefixed
 * to the name.  Don't bother with the low speeds - Minix does not support
 * them.  Add support for higher speeds (speeds are now easy and don't need
 * defines because they are not encoded).
 */
#define T_ECHO		000001

#include <errno.h>
#include <sgtty.h>

static _PROTOTYPE( int tc_to_sg_speed, (speed_t speed) );
static _PROTOTYPE( speed_t sg_to_tc_speed, (int speed) );
#define B19200   192

/* The speed get/set functions could be macros in the Minix implementation
 * because there are speed fields in the structure with no fancy packing
 * and it is not practical to check the values outside the driver.
 * Where tests are necessary because the driver acts different from what
 * POSIX requires, they are done in tcsetattr.
 */

speed_t cfgetispeed(termios_p)
struct termios *termios_p;
{
    return termios_p->c_ispeed;
}

speed_t cfgetospeed(termios_p)
struct termios *termios_p;
{
    return termios_p->c_ospeed;
}

speed_t cfsetispeed(termios_p, speed)
struct termios *termios_p;
speed_t speed;
{
    termios_p->c_ispeed = speed;
    return 0;
}

speed_t cfsetospeed(termios_p, speed)
struct termios *termios_p;
speed_t speed;
{
    termios_p->c_ospeed = speed;
    return 0;
}

static speed_t sg_to_tc_speed(speed)
int speed;
{
    /* The speed encodings in sgtty.h and termios.h are different.  Both are
     * inflexible.  Minix doesn't really support B0 but we map it through
     * anyway.  It doesn't support B50, B75 or B134.
     */
    switch (speed) {
	case B0: return 0;
	case B110: return 110;
	case B200: return 200;
	case B300: return 300;
	case B600: return 600;
	case B1200: return 1200;
	case B1800: return 1800;
	case B2400: return 2400;
	case B4800: return 4800;
	case B9600: return 9600;
	case B19200: return 19200;
#ifdef B28800
	case B28800: return 28800;
#endif
#ifdef B38400
	case B38400: return 38400;
#endif
#ifdef B57600
	case B57600: return 57600;
#endif
#ifdef B115200
	case B115200: return 115200;
#endif
	default: return (speed_t)-1;
    }
}

static int tc_to_sg_speed(speed)
speed_t speed;
{
    /* Don't use a switch here in case the compiler is 16-bit and doesn't
     * properly support longs (speed_t's) in switches.  It turns out the
     * switch is larger and slower for most compilers anyway!
     */
    if (speed == 0) return 0;
    if (speed == 110) return B110;
    if (speed == 200) return B200;
    if (speed == 300) return B300;
    if (speed == 600) return B600;
    if (speed == 1200) return B1200;
    if (speed == 1800) return B1800;
    if (speed == 2400) return B2400;
    if (speed == 4800) return B4800;
    if (speed == 9600) return B9600;
    if (speed == 19200) return B19200;
#ifdef B28800
    if (speed == 28800) return B28800;
#endif
#ifdef B38400
    if (speed == 38400) return B38400;
#endif
#ifdef B57600
    if (speed == 57600) return B57600;
#endif
#ifdef B115200
    if (speed == 115200) return B115200;
#endif
    return -1;
}

int tcgetattr(filedes, termios_p)
int filedes;
struct termios *termios_p;
{
    struct sgttyb sgbuf;
    struct tchars tcbuf;

    if (ioctl(filedes, TIOCGETP, &sgbuf) < 0
	|| ioctl(filedes, TIOCGETC, (struct sgttyb *) &tcbuf) < 0)
    {
	return -1;
    }

    /* Minix input flags:
     *   BRKINT:  forced off (break is not recognized)
     *   IGNBRK:  forced on (break is not recognized)
     *   ICRNL:   set if CRMOD is set and not RAW (CRMOD also controls output)
     *   IGNCR:   forced off (ignoring cr's is not supported)
     *   INLCR:   forced off (mapping nl's to cr's is not supported)
     *   ISTRIP:  forced off (should be off for consoles, on for rs232 no RAW)
     *   IXOFF:   forced off (rs232 uses CTS instead of XON/XOFF)
     *   IXON:    forced on if not RAW
     *   PARMRK:  forced off (no '\377', '\0', X sequence on errors)
     * ? IGNPAR:  forced off (input with parity/framing errors is kept)
     * ? INPCK:   forced off (input parity checking is not supported)
     */
    termios_p->c_iflag = IGNBRK;
    if (!(sgbuf.sg_flags & RAW))
    {
	termios_p->c_iflag |= IXON;
	if (sgbuf.sg_flags & CRMOD)
	{
	    termios_p->c_iflag |= ICRNL;
	}
    }

    /* Minix output flags:
     *   OPOST:   set if CRMOD or XTABS is set
     *   XTABS:   copied from sg_flags
     *   CRMOD:	  copied from sg_flags
     */
    termios_p->c_oflag = sgbuf.sg_flags & (CRMOD | XTABS);
    if (termios_p->c_oflag)
    {
	termios_p->c_oflag |= OPOST;
    }

    /* Minix local flags:
     *   ECHO:    set if ECHO is set
     *   ECHOE:   set if ECHO is set (ERASE echoed as error-corecting backspace)
     *   ECHOK:   set if ECHO is set ('\n' echoed after KILL char)
     *   ECHONL:  forced off ('\n' not echoed when ECHO isn't set)
     *   ICANON:  set if neither CBREAK nor RAW
     *   IEXTEN:  forced off
     *   ISIG:    set if not RAW
     *   NOFLSH:  forced off (input/output queues are always flushed)
     *   TOSTOP:  forced off (no job control)
     */
    termios_p->c_lflag = 0;
    if (sgbuf.sg_flags & ECHO)
    {
	termios_p->c_lflag |= T_ECHO | ECHOE | ECHOK;
    }
    if (!(sgbuf.sg_flags & RAW))
    {
	termios_p->c_lflag |= ISIG;
	if (!(sgbuf.sg_flags & CBREAK))
	{
	    termios_p->c_lflag |= ICANON;
	}
    }

    /* Minix control flags:
     *   CLOCAL:  forced on (ignore modem status lines - not quite right)
     *   CREAD:   forced on (receiver is always enabled)
     *   CSIZE:   CS5-CS8 correspond directly to BITS5-BITS8
     *   CSTOPB:  set for B110 (driver will generate 2 stop-bits than)
     *   HUPCL:   forced off
     *   PARENB:  set if EVENP or ODDP is set
     *   PARODD:  set if ODDP is set
     */
    termios_p->c_cflag = CLOCAL | CREAD;
    switch (sgbuf.sg_flags & BITS8)
    {
	case BITS5: termios_p->c_cflag |= CS5; break;
	case BITS6: termios_p->c_cflag |= CS6; break;
	case BITS7: termios_p->c_cflag |= CS7; break;
	case BITS8: termios_p->c_cflag |= CS8; break;
    }
    if (sgbuf.sg_flags & ODDP)
    {
	termios_p->c_cflag |= PARENB | PARODD;
    }
    if (sgbuf.sg_flags & EVENP)
    {
	termios_p->c_cflag |= PARENB;
    }
    if (sgbuf.sg_ispeed == B110)
    {
	termios_p->c_cflag |= CSTOPB;
    }

    /* Minix may give back different input and output baudrates,
     * but only the input baudrate is valid for both.
     * As our termios emulation will fail, if input baudrate differs
     * from output baudrate, force them to be equal.
     * Otherwise it would be very suprisingly not to be able to set
     * the terminal back to the state returned by tcgetattr :).
     */
    termios_p->c_ospeed =
    termios_p->c_ispeed =
		sg_to_tc_speed((unsigned char) sgbuf.sg_ispeed);

    /* Minix control characters correspond directly except VSUSP and the
     * important VMIN and VTIME are not really supported.
     */
    termios_p->c_cc[VEOF] = tcbuf.t_eofc;
    termios_p->c_cc[VEOL] = tcbuf.t_brkc;
    termios_p->c_cc[VERASE] = sgbuf.sg_erase;
    termios_p->c_cc[VINTR] = tcbuf.t_intrc;
    termios_p->c_cc[VKILL] = sgbuf.sg_kill;
    termios_p->c_cc[VQUIT] = tcbuf.t_quitc;
    termios_p->c_cc[VSTART] = tcbuf.t_startc;
    termios_p->c_cc[VSTOP] = tcbuf.t_stopc;
    termios_p->c_cc[VMIN] = 1;
    termios_p->c_cc[VTIME] = 0;
    termios_p->c_cc[VSUSP] = 0;

    return 0;
}

int tcsetattr(filedes, opt_actions, termios_p)
int filedes;
int opt_actions;
struct termios *termios_p;
{
    struct sgttyb sgbuf;
    struct tchars tcbuf;
    int sgspeed;

    /* Posix 1003.1-1988 page 135 says:
     * Attempts to set unsupported baud rates shall be ignored, and it is
     * implementation-defined whether an error is returned by any or all of
     * cfsetispeed(), cfsetospeed(), or tcsetattr(). This refers both to
     * changes to baud rates not supported by the hardware, and to changes
     * setting the input and output baud rates to different values if the
     * hardware does not support it.
     * Ignoring means not to change the existing settings, doesn't it?
     */
    if ((termios_p->c_ispeed != 0 && termios_p->c_ispeed != termios_p->c_ospeed)
	|| (sgspeed = tc_to_sg_speed(termios_p->c_ospeed)) < 0)
    {
	errno = EINVAL;
	return -1;
    }

    sgbuf.sg_ispeed = sgbuf.sg_ospeed = sgspeed;
    sgbuf.sg_flags = 0;

    /* I don't know what should happen with requests that are not supported by
     * old Minix drivers and therefore cannot be emulated.
     * Returning an error may confuse the application (the values aren't really
     * invalid or unsupported by the hardware, they just couldn't be satisfied
     * by the driver). Not returning an error might be even worse because the
     * driver will act different to what the application requires it to act
     * after sucessfully setting the attributes as specified.
     * Settings that cannot be emulated fully include:
     *   c_ospeed != 110 && c_cflag & CSTOPB
     *   c_ospeed == 110 && ! c_cflag & CSTOPB
     *   (c_cc[VMIN] != 1 || c_cc[VTIME] != 0) && ! c_lflag & ICANON
     *   c_lflag & ICANON && ! c_lflag & ISIG
     * For the moment I just ignore these conflicts.
     */

    if (termios_p->c_oflag & OPOST)
    {
	/* CRMOD isn't Posix and may conflict with ICRNL, which is Posix,
	 * so we just ignore it.
	 */
	if (termios_p->c_oflag & XTABS)
	{
		sgbuf.sg_flags |= XTABS;
	}
    }

    if (termios_p->c_iflag & ICRNL)
    {
	/* We couldn't do it better :-(. */
	sgbuf.sg_flags |= CRMOD;
    }

    if (termios_p->c_lflag & T_ECHO)
    {
	sgbuf.sg_flags |= ECHO;
    }
    if (!(termios_p->c_lflag & ICANON))
    {
	if (termios_p->c_lflag & ISIG)
	{
	     sgbuf.sg_flags |= CBREAK;
	}
	else
	{
	     sgbuf.sg_flags |= RAW;
	}
    }

    switch (termios_p->c_cflag & CSIZE)
    {
	case CS5: sgbuf.sg_flags |= BITS5; break;
	case CS6: sgbuf.sg_flags |= BITS6; break;
	case CS7: sgbuf.sg_flags |= BITS7; break;
	case CS8: sgbuf.sg_flags |= BITS8; break;
    }
    if (termios_p->c_cflag & PARENB)
    {
	if (termios_p->c_cflag & PARODD)
	{
	    sgbuf.sg_flags |= ODDP;
	}
	else
	{
	    sgbuf.sg_flags |= EVENP;
	}
    }

    sgbuf.sg_erase = termios_p->c_cc[VERASE];
    sgbuf.sg_kill = termios_p->c_cc[VKILL];

    tcbuf.t_intrc = termios_p->c_cc[VINTR];
    tcbuf.t_quitc = termios_p->c_cc[VQUIT];
    tcbuf.t_startc = termios_p->c_cc[VSTART];
    tcbuf.t_stopc = termios_p->c_cc[VSTOP];
    tcbuf.t_eofc = termios_p->c_cc[VEOF];
    tcbuf.t_brkc = termios_p->c_cc[VEOL];

    return ioctl(filedes, TIOCSETP, &sgbuf) < 0 &&
	   ioctl(filedes, TIOCSETC, (struct sgttyb *) &tcbuf) < 0 ?
		-1 : 0;
}
#endif /* _MINIX && !_MINIX_VMD */
