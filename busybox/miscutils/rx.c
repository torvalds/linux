/* vi: set sw=4 ts=4: */
/*
 * Copyright:     Copyright (C) 2001, Hewlett-Packard Company
 * Author:        Christopher Hoover <ch@hpl.hp.com>
 * Description:   xmodem functionality for uploading of kernels
 *                and the like
 * Created at:    Thu Dec 20 01:58:08 PST 2001
 *
 * xmodem functionality for uploading of kernels and the like
 *
 * Copyright (C) 2001 Hewlett-Packard Laboratories
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * This was originally written for blob and then adapted for busybox.
 */
//config:config RX
//config:	bool "rx (2.9 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Receive files using the Xmodem protocol.

//applet:IF_RX(APPLET(rx, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RX) += rx.o

//usage:#define rx_trivial_usage
//usage:       "FILE"
//usage:#define rx_full_usage "\n\n"
//usage:       "Receive a file using the xmodem protocol"
//usage:
//usage:#define rx_example_usage
//usage:       "$ rx /tmp/foo\n"

#include "libbb.h"

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define BS  0x08
#define PAD 0x1A

/*
Cf:
  http://www.textfiles.com/apple/xmodem
  http://www.phys.washington.edu/~belonis/xmodem/docxmodem.txt
  http://www.phys.washington.edu/~belonis/xmodem/docymodem.txt
  http://www.phys.washington.edu/~belonis/xmodem/modmprot.col
*/

#define TIMEOUT 1
#define TIMEOUT_LONG 10
#define MAXERRORS 10

#define read_fd  STDIN_FILENO
#define write_fd STDOUT_FILENO

static int read_byte(unsigned timeout)
{
	unsigned char buf;
	int n;

	alarm(timeout);
	/* NOT safe_read! We want ALRM to interrupt us */
	n = read(read_fd, &buf, 1);
	alarm(0);
	if (n == 1)
		return buf;
	return -1;
}

static int receive(/*int read_fd, */int file_fd)
{
	unsigned char blockBuf[1024];
	unsigned blockLength = 0;
	unsigned errors = 0;
	unsigned wantBlockNo = 1;
	unsigned length = 0;
	int do_crc = 1;
	char reply_char;
	unsigned timeout = TIMEOUT_LONG;

	/* Flush pending input */
	tcflush(read_fd, TCIFLUSH);

	/* Ask for CRC; if we get errors, we will go with checksum */
	reply_char = 'C';
	full_write(write_fd, &reply_char, 1);

	for (;;) {
		int blockBegin;
		int blockNo, blockNoOnesCompl;
		int cksum_or_crc;
		unsigned expected;
		int i, j;

		blockBegin = read_byte(timeout);
		if (blockBegin < 0)
			goto timeout;

		/* If last block, remove padding */
		if (blockBegin == EOT) {
			/* Data blocks can be padded with ^Z characters */
			/* This code tries to detect and remove them */
			if (blockLength >= 3
			 && blockBuf[blockLength - 1] == PAD
			 && blockBuf[blockLength - 2] == PAD
			 && blockBuf[blockLength - 3] == PAD
			) {
				while (blockLength
				    && blockBuf[blockLength - 1] == PAD
				) {
					blockLength--;
				}
			}
		}
		/* Write previously received block */
		errno = 0;
		if (full_write(file_fd, blockBuf, blockLength) != blockLength) {
			bb_perror_msg(bb_msg_write_error);
			goto fatal;
		}

		timeout = TIMEOUT;
		reply_char = NAK;

		switch (blockBegin) {
		case SOH:
		case STX:
			break;
		case EOT:
			reply_char = ACK;
			full_write(write_fd, &reply_char, 1);
			return length;
		default:
			goto error;
		}

		/* Block no */
		blockNo = read_byte(TIMEOUT);
		if (blockNo < 0)
			goto timeout;

		/* Block no, in one's complement form */
		blockNoOnesCompl = read_byte(TIMEOUT);
		if (blockNoOnesCompl < 0)
			goto timeout;

		if (blockNo != (255 - blockNoOnesCompl)) {
			bb_error_msg("bad block ones compl");
			goto error;
		}

		blockLength = (blockBegin == SOH) ? 128 : 1024;

		for (i = 0; i < blockLength; i++) {
			int cc = read_byte(TIMEOUT);
			if (cc < 0)
				goto timeout;
			blockBuf[i] = cc;
		}

		cksum_or_crc = read_byte(TIMEOUT);
		if (cksum_or_crc < 0)
			goto timeout;
		if (do_crc) {
			cksum_or_crc = (cksum_or_crc << 8) | read_byte(TIMEOUT);
			if (cksum_or_crc < 0)
				goto timeout;
		}

		if (blockNo == ((wantBlockNo - 1) & 0xff)) {
			/* a repeat of the last block is ok, just ignore it. */
			/* this also ignores the initial block 0 which is */
			/* meta data. */
			blockLength = 0;
			goto next;
		}
		if (blockNo != (wantBlockNo & 0xff)) {
			bb_error_msg("unexpected block no, 0x%08x, expecting 0x%08x", blockNo, wantBlockNo);
			goto error;
		}

		expected = 0;
		if (do_crc) {
			for (i = 0; i < blockLength; i++) {
				expected = expected ^ blockBuf[i] << 8;
				for (j = 0; j < 8; j++) {
					if (expected & 0x8000)
						expected = (expected << 1) ^ 0x1021;
					else
						expected = (expected << 1);
				}
			}
			expected &= 0xffff;
		} else {
			for (i = 0; i < blockLength; i++)
				expected += blockBuf[i];
			expected &= 0xff;
		}
		if (cksum_or_crc != expected) {
			bb_error_msg(do_crc ? "crc error, expected 0x%04x, got 0x%04x"
					: "checksum error, expected 0x%02x, got 0x%02x",
				expected, cksum_or_crc);
			goto error;
		}

		wantBlockNo++;
		length += blockLength;
 next:
		errors = 0;
		reply_char = ACK;
		full_write(write_fd, &reply_char, 1);
		continue;
 error:
 timeout:
		blockLength = 0;
		errors++;
		if (errors == MAXERRORS) {
			/* Abort */

			/* If were asking for crc, try again w/o crc */
			if (reply_char == 'C') {
				reply_char = NAK;
				errors = 0;
				do_crc = 0;
				goto timeout;
			}
			bb_error_msg("too many errors; giving up");
 fatal:
			/* 5 CAN followed by 5 BS. Don't try too hard... */
			safe_write(write_fd, "\030\030\030\030\030\010\010\010\010\010", 10);
			return -1;
		}

		/* Flush pending input */
		tcflush(read_fd, TCIFLUSH);

		full_write(write_fd, &reply_char, 1);
	} /* for (;;) */
}

static void sigalrm_handler(int UNUSED_PARAM signum)
{
}

int rx_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rx_main(int argc UNUSED_PARAM, char **argv)
{
	struct termios tty, orig_tty;
	int termios_err;
	int file_fd;
	int n;

	/* Disabled by vda:
	 * why we can't receive from stdin? Why we *require*
	 * controlling tty?? */
	/*read_fd = xopen(CURRENT_TTY, O_RDWR);*/
	file_fd = xopen(single_argv(argv), O_RDWR|O_CREAT|O_TRUNC);

	termios_err = tcgetattr(read_fd, &tty);
	if (termios_err == 0) {
//TODO: use set_termios_to_raw()
		orig_tty = tty;
		cfmakeraw(&tty);
		tcsetattr(read_fd, TCSAFLUSH, &tty);
	}

	/* No SA_RESTART: we want ALRM to interrupt read() */
	signal_no_SA_RESTART_empty_mask(SIGALRM, sigalrm_handler);

	n = receive(file_fd);

	if (termios_err == 0)
		tcsetattr(read_fd, TCSAFLUSH, &orig_tty);
	if (ENABLE_FEATURE_CLEAN_UP)
		close(file_fd);
	fflush_stdout_and_exit(n >= 0);
}
