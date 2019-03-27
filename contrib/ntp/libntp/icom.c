/*
 * Program to control ICOM radios
 *
 * This is a ripoff of the utility routines in the ICOM software
 * distribution. The only function provided is to load the radio
 * frequency. All other parameters must be manually set before use.
 */
#include <config.h>
#include <ntp_stdlib.h>
#include <ntp_tty.h>
#include <l_stdlib.h>
#include <icom.h>

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>


#ifdef SYS_WINNT
#undef write	/* ports/winnt/include/config.h: #define write _write */
extern int async_write(int, const void *, unsigned int);
#define write(fd, data, octets)	async_write(fd, data, octets)
#endif

/*
 * Packet routines
 *
 * These routines send a packet and receive the response. If an error
 * (collision) occurs on transmit, the packet is resent. If an error
 * occurs on receive (timeout), all input to the terminating FI is
 * discarded and the packet is resent. If the maximum number of retries
 * is not exceeded, the program returns the number of octets in the user
 * buffer; otherwise, it returns zero.
 *
 * ICOM frame format
 *
 * Frames begin with a two-octet preamble PR-PR followyd by the
 * transceiver address RE, controller address TX, control code CN, zero
 * or more data octets DA (depending on command), and terminator FI.
 * Since the bus is bidirectional, every octet output is echoed on
 * input. Every valid frame sent is answered with a frame in the same
 * format, but with the RE and TX fields interchanged. The CN field is
 * set to NAK if an error has occurred. Otherwise, the data are returned
 * in this and following DA octets. If no data are returned, the CN
 * octet is set to ACK.
 *
 *	+------+------+------+------+------+--//--+------+
 *	|  PR  |  PR  |  RE  |  TX  |  CN  |  DA  |  FI  |
 *	+------+------+------+------+------+--//--+------+
 */
/*
 * Scraps
 */
#define DICOM /dev/icom/	/* ICOM port link */

/*
 * Local function prototypes
 */
static void doublefreq		(double, u_char *, int);


/*
 * icom_freq(fd, ident, freq) - load radio frequency
 *
 * returns:
 *  0 (ok)
 * -1 (error)
 *  1 (short write to device)
 */
int
icom_freq(
	int fd,			/* file descriptor */
	int ident,		/* ICOM radio identifier */
	double freq		/* frequency (MHz) */
	)
{
	u_char cmd[] = {PAD, PR, PR, 0, TX, V_SFREQ, 0, 0, 0, 0, FI,
	    FI};
	int temp;
	int rc;

	cmd[3] = (char)ident;
	if (ident == IC735)
		temp = 4;
	else
		temp = 5;
	doublefreq(freq * 1e6, &cmd[6], temp);
	rc = write(fd, cmd, temp + 7);
	if (rc == -1) {
		msyslog(LOG_ERR, "icom_freq: write() failed: %m");
		return -1;
	} else if (rc != temp + 7) {
		msyslog(LOG_ERR, "icom_freq: only wrote %d of %d bytes.",
			rc, temp+7);
		return 1;
	}

	return 0;
}


/*
 * doublefreq(freq, y, len) - double to ICOM frequency with padding
 */
static void
doublefreq(			/* returns void */
	double freq,		/* frequency */
	u_char *x,		/* radio frequency */
	int len			/* length (octets) */
	)
{
	int i;
	char s1[16];
	char *y;

	snprintf(s1, sizeof(s1), " %10.0f", freq);
	y = s1 + 10;
	i = 0;
	while (*y != ' ') {
		x[i] = *y-- & 0x0f;
		x[i] = x[i] | ((*y-- & 0x0f) << 4);
		i++;
	}
	for ( ; i < len; i++)
		x[i] = 0;
	x[i] = FI;
}

/*
 * icom_init() - open and initialize serial interface
 *
 * This routine opens the serial interface for raw transmission; that
 * is, character-at-a-time, no stripping, checking or monkeying with the
 * bits. For Unix, an input operation ends either with the receipt of a
 * character or a 0.5-s timeout.
 */
int
icom_init(
	const char *device,	/* device name/link */
	int speed,		/* line speed */
	int trace		/* trace flags */	)
{
	TTY ttyb;
	int fd;
	int rc;
	int saved_errno;

	fd = tty_open(device, O_RDWR, 0777);
	if (fd < 0)
		return -1;

	rc = tcgetattr(fd, &ttyb);
	if (rc < 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}
	ttyb.c_iflag = 0;	/* input modes */
	ttyb.c_oflag = 0;	/* output modes */
	ttyb.c_cflag = IBAUD|CS8|CLOCAL; /* control modes  (no read) */
	ttyb.c_lflag = 0;	/* local modes */
	ttyb.c_cc[VMIN] = 0;	/* min chars */
	ttyb.c_cc[VTIME] = 5;	/* receive timeout */
	cfsetispeed(&ttyb, (u_int)speed);
	cfsetospeed(&ttyb, (u_int)speed);
	rc = tcsetattr(fd, TCSANOW, &ttyb);
	if (rc < 0) {
		saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}
	return (fd);
}

/* end program */
