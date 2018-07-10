/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2006 Rob Landley
 * Copyright (C) 2006 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
/* We need to have separate xfuncs.c and xfuncs_printf.c because
 * with current linkers, even with section garbage collection,
 * if *.o module references any of XXXprintf functions, you pull in
 * entire printf machinery. Even if you do not use the function
 * which uses XXXprintf.
 *
 * xfuncs.c contains functions (not necessarily xfuncs)
 * which do not pull in printf, directly or indirectly.
 * xfunc_printf.c contains those which do.
 *
 * TODO: move xmalloc() and xatonum() here.
 */
#include "libbb.h"

/* Turn on nonblocking I/O on a fd */
int FAST_FUNC ndelay_on(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (flags & O_NONBLOCK)
		return flags;
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	return flags;
}

int FAST_FUNC ndelay_off(int fd)
{
	int flags = fcntl(fd, F_GETFL);
	if (!(flags & O_NONBLOCK))
		return flags;
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
	return flags;
}

void FAST_FUNC close_on_exec_on(int fd)
{
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

char* FAST_FUNC strncpy_IFNAMSIZ(char *dst, const char *src)
{
#ifndef IFNAMSIZ
	enum { IFNAMSIZ = 16 };
#endif
	return strncpy(dst, src, IFNAMSIZ);
}


/* Convert unsigned integer to ascii, writing into supplied buffer.
 * A truncated result contains the first few digits of the result ala strncpy.
 * Returns a pointer past last generated digit, does _not_ store NUL.
 */
char* FAST_FUNC utoa_to_buf(unsigned n, char *buf, unsigned buflen)
{
	unsigned i, out, res;

	if (buflen) {
		out = 0;

		BUILD_BUG_ON(sizeof(n) != 4 && sizeof(n) != 8);
		if (sizeof(n) == 4)
		// 2^32-1 = 4294967295
			i = 1000000000;
#if UINT_MAX > 0xffffffff /* prevents warning about "const too large" */
		else
		if (sizeof(n) == 8)
		// 2^64-1 = 18446744073709551615
			i = 10000000000000000000;
#endif
		for (; i; i /= 10) {
			res = n / i;
			n = n % i;
			if (res || out || i == 1) {
				if (--buflen == 0)
					break;
				out++;
				*buf++ = '0' + res;
			}
		}
	}
	return buf;
}

/* Convert signed integer to ascii, like utoa_to_buf() */
char* FAST_FUNC itoa_to_buf(int n, char *buf, unsigned buflen)
{
	if (!buflen)
		return buf;
	if (n < 0) {
		n = -n;
		*buf++ = '-';
		buflen--;
	}
	return utoa_to_buf((unsigned)n, buf, buflen);
}

// The following two functions use a static buffer, so calling either one a
// second time will overwrite previous results.
//
// The largest 32 bit integer is -2 billion plus NUL, or 1+10+1=12 bytes.
// It so happens that sizeof(int) * 3 is enough for 32+ bit ints.
// (sizeof(int) * 3 + 2 is correct for any width, even 8-bit)

static char local_buf[sizeof(int) * 3];

/* Convert unsigned integer to ascii using a static buffer (returned). */
char* FAST_FUNC utoa(unsigned n)
{
	*(utoa_to_buf(n, local_buf, sizeof(local_buf) - 1)) = '\0';

	return local_buf;
}

/* Convert signed integer to ascii using a static buffer (returned). */
char* FAST_FUNC itoa(int n)
{
	*(itoa_to_buf(n, local_buf, sizeof(local_buf) - 1)) = '\0';

	return local_buf;
}

/* Emit a string of hex representation of bytes */
char* FAST_FUNC bin2hex(char *p, const char *cp, int count)
{
	while (count) {
		unsigned char c = *cp++;
		/* put lowercase hex digits */
		*p++ = 0x20 | bb_hexdigits_upcase[c >> 4];
		*p++ = 0x20 | bb_hexdigits_upcase[c & 0xf];
		count--;
	}
	return p;
}

/* Convert "[x]x[:][x]x[:][x]x[:][x]x" hex string to binary, no more than COUNT bytes */
char* FAST_FUNC hex2bin(char *dst, const char *str, int count)
{
	errno = EINVAL;
	while (*str && count) {
		uint8_t val;
		uint8_t c = *str++;
		if (isdigit(c))
			val = c - '0';
		else if ((c|0x20) >= 'a' && (c|0x20) <= 'f')
			val = (c|0x20) - ('a' - 10);
		else
			return NULL;
		val <<= 4;
		c = *str;
		if (isdigit(c))
			val |= c - '0';
		else if ((c|0x20) >= 'a' && (c|0x20) <= 'f')
			val |= (c|0x20) - ('a' - 10);
		else if (c == ':' || c == '\0')
			val >>= 4;
		else
			return NULL;

		*dst++ = val;
		if (c != '\0')
			str++;
		if (*str == ':')
			str++;
		count--;
	}
	errno = (*str ? ERANGE : 0);
	return dst;
}

/* Return how long the file at fd is, if there's any way to determine it. */
#ifdef UNUSED
off_t FAST_FUNC fdlength(int fd)
{
	off_t bottom = 0, top = 0, pos;
	long size;

	// If the ioctl works for this, return it.

	if (ioctl(fd, BLKGETSIZE, &size) >= 0) return size*512;

	// FIXME: explain why lseek(SEEK_END) is not used here!

	// If not, do a binary search for the last location we can read.  (Some
	// block devices don't do BLKGETSIZE right.)

	do {
		char temp;

		pos = bottom + (top - bottom) / 2;

		// If we can read from the current location, it's bigger.

		if (lseek(fd, pos, SEEK_SET)>=0 && safe_read(fd, &temp, 1)==1) {
			if (bottom == top) bottom = top = (top+1) * 2;
			else bottom = pos;

		// If we can't, it's smaller.
		} else {
			if (bottom == top) {
				if (!top) return 0;
				bottom = top/2;
			}
			else top = pos;
		}
	} while (bottom + 1 != top);

	return pos + 1;
}
#endif

int FAST_FUNC bb_putchar_stderr(char ch)
{
	return write(STDERR_FILENO, &ch, 1);
}

ssize_t FAST_FUNC full_write1_str(const char *str)
{
	return full_write(STDOUT_FILENO, str, strlen(str));
}

ssize_t FAST_FUNC full_write2_str(const char *str)
{
	return full_write(STDERR_FILENO, str, strlen(str));
}

static int wh_helper(int value, int def_val, const char *env_name, int *err)
{
	/* Envvars override even if "value" from ioctl is valid (>0).
	 * Rationale: it's impossible to guess what user wants.
	 * For example: "man CMD | ...": should "man" format output
	 * to stdout's width? stdin's width? /dev/tty's width? 80 chars?
	 * We _cant_ know it. If "..." saves text for e.g. email,
	 * then it's probably 80 chars.
	 * If "..." is, say, "grep -v DISCARD | $PAGER", then user
	 * would prefer his tty's width to be used!
	 *
	 * Since we don't know, at least allow user to do this:
	 * "COLUMNS=80 man CMD | ..."
	 */
	char *s = getenv(env_name);
	if (s) {
		value = atoi(s);
		/* If LINES/COLUMNS are set, pretend that there is
		 * no error getting w/h, this prevents some ugly
		 * cursor tricks by our callers */
		*err = 0;
	}

	if (value <= 1 || value >= 30000)
		value = def_val;
	return value;
}

/* It is perfectly ok to pass in a NULL for either width or for
 * height, in which case that value will not be set.  */
int FAST_FUNC get_terminal_width_height(int fd, unsigned *width, unsigned *height)
{
	struct winsize win;
	int err;
	int close_me = -1;

	if (fd == -1) {
		if (isatty(STDOUT_FILENO))
			fd = STDOUT_FILENO;
		else
		if (isatty(STDERR_FILENO))
			fd = STDERR_FILENO;
		else
		if (isatty(STDIN_FILENO))
			fd = STDIN_FILENO;
		else
			close_me = fd = open("/dev/tty", O_RDONLY);
	}

	win.ws_row = 0;
	win.ws_col = 0;
	/* I've seen ioctl returning 0, but row/col is (still?) 0.
	 * We treat that as an error too.  */
	err = ioctl(fd, TIOCGWINSZ, &win) != 0 || win.ws_row == 0;
	if (height)
		*height = wh_helper(win.ws_row, 24, "LINES", &err);
	if (width)
		*width = wh_helper(win.ws_col, 80, "COLUMNS", &err);

	if (close_me >= 0)
		close(close_me);

	return err;
}
int FAST_FUNC get_terminal_width(int fd)
{
	unsigned width;
	get_terminal_width_height(fd, &width, NULL);
	return width;
}

int FAST_FUNC tcsetattr_stdin_TCSANOW(const struct termios *tp)
{
	return tcsetattr(STDIN_FILENO, TCSANOW, tp);
}

int FAST_FUNC get_termios_and_make_raw(int fd, struct termios *newterm, struct termios *oldterm, int flags)
{
//TODO: slattach, shell read might be adapted to use this too: grep for "tcsetattr", "[VTIME] = 0"
	int r;

	memset(oldterm, 0, sizeof(*oldterm)); /* paranoia */
	r = tcgetattr(fd, oldterm);
	*newterm = *oldterm;

	/* Turn off buffered input (ICANON)
	 * Turn off echoing (ECHO)
	 * and separate echoing of newline (ECHONL, normally off anyway)
	 */
	newterm->c_lflag &= ~(ICANON | ECHO | ECHONL);
	if (flags & TERMIOS_CLEAR_ISIG) {
		/* dont recognize INT/QUIT/SUSP chars */
		newterm->c_lflag &= ~ISIG;
	}
	/* reads will block only if < 1 char is available */
	newterm->c_cc[VMIN] = 1;
	/* no timeout (reads block forever) */
	newterm->c_cc[VTIME] = 0;
/* IXON, IXOFF, and IXANY:
 * IXOFF=1: sw flow control is enabled on input queue:
 * tty transmits a STOP char when input queue is close to full
 * and transmits a START char when input queue is nearly empty.
 * IXON=1: sw flow control is enabled on output queue:
 * tty will stop sending if STOP char is received,
 * and resume sending if START is received, or if any char
 * is received and IXANY=1.
 */
	if (flags & TERMIOS_RAW_CRNL_INPUT) {
		/* IXON=0: XON/XOFF chars are treated as normal chars (why we do this?) */
		/* dont convert CR to NL on input */
		newterm->c_iflag &= ~(IXON | ICRNL);
	}
	if (flags & TERMIOS_RAW_CRNL_OUTPUT) {
		/* dont convert NL to CR+NL on output */
		newterm->c_oflag &= ~(ONLCR);
		/* Maybe clear more c_oflag bits? Usually, only OPOST and ONLCR are set.
		 * OPOST  Enable output processing (reqd for OLCUC and *NL* bits to work)
		 * OLCUC  Map lowercase characters to uppercase on output.
		 * OCRNL  Map CR to NL on output.
		 * ONOCR  Don't output CR at column 0.
		 * ONLRET Don't output CR.
		 */
	}
	if (flags & TERMIOS_RAW_INPUT) {
#ifndef IMAXBEL
# define IMAXBEL 0
#endif
#ifndef IUCLC
# define IUCLC 0
#endif
#ifndef IXANY
# define IXANY 0
#endif
		/* IXOFF=0: disable sending XON/XOFF if input buf is full
		 * IXON=0: input XON/XOFF chars are not special
		 * BRKINT=0: dont send SIGINT on break
		 * IMAXBEL=0: dont echo BEL on input line too long
		 * INLCR,ICRNL,IUCLC: dont convert anything on input
		 */
		newterm->c_iflag &= ~(IXOFF|IXON|IXANY|BRKINT|INLCR|ICRNL|IUCLC|IMAXBEL);
	}
	return r;
}

int FAST_FUNC set_termios_to_raw(int fd, struct termios *oldterm, int flags)
{
	struct termios newterm;

	get_termios_and_make_raw(fd, &newterm, oldterm, flags);
	return tcsetattr(fd, TCSANOW, &newterm);
}

pid_t FAST_FUNC safe_waitpid(pid_t pid, int *wstat, int options)
{
	pid_t r;

	do
		r = waitpid(pid, wstat, options);
	while ((r == -1) && (errno == EINTR));
	return r;
}

pid_t FAST_FUNC wait_any_nohang(int *wstat)
{
	return safe_waitpid(-1, wstat, WNOHANG);
}

// Wait for the specified child PID to exit, returning child's error return.
int FAST_FUNC wait4pid(pid_t pid)
{
	int status;

	if (pid <= 0) {
		/*errno = ECHILD; -- wrong. */
		/* we expect errno to be already set from failed [v]fork/exec */
		return -1;
	}
	if (safe_waitpid(pid, &status, 0) == -1)
		return -1;
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return WTERMSIG(status) + 0x180;
	return 0;
}

// Useful when we do know that pid is valid, and we just want to wait
// for it to exit. Not existing pid is fatal. waitpid() status is not returned.
int FAST_FUNC wait_for_exitstatus(pid_t pid)
{
	int exit_status, n;

	n = safe_waitpid(pid, &exit_status, 0);
	if (n < 0)
		bb_perror_msg_and_die("waitpid");
	return exit_status;
}
