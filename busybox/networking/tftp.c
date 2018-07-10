/* vi: set sw=4 ts=4: */
/*
 * A simple tftp client/server for busybox.
 * Tries to follow RFC1350.
 * Only "octet" mode supported.
 * Optional blocksize negotiation (RFC2347 + RFC2348)
 *
 * Copyright (C) 2001 Magnus Damm <damm@opensource.se>
 *
 * Parts of the code based on:
 *
 * atftp:  Copyright (C) 2000 Jean-Pierre Lefebvre <helix@step.polymtl.ca>
 *                        and Remi Lefebvre <remi@debian.org>
 *
 * utftp:  Copyright (C) 1999 Uwe Ohse <uwe@ohse.de>
 *
 * tftpd added by Denys Vlasenko & Vladimir Dronnikov
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config TFTP
//config:	bool "tftp (12 kb)"
//config:	default y
//config:	help
//config:	Trivial File Transfer Protocol client. TFTP is usually used
//config:	for simple, small transfers such as a root image
//config:	for a network-enabled bootloader.
//config:
//config:config FEATURE_TFTP_PROGRESS_BAR
//config:	bool "Enable progress bar"
//config:	default y
//config:	depends on TFTP
//config:
//config:config TFTPD
//config:	bool "tftpd (10 kb)"
//config:	default y
//config:	help
//config:	Trivial File Transfer Protocol server.
//config:	It expects that stdin is a datagram socket and a packet
//config:	is already pending on it. It will exit after one transfer.
//config:	In other words: it should be run from inetd in nowait mode,
//config:	or from udpsvd. Example: "udpsvd -E 0 69 tftpd DIR"
//config:
//config:comment "Common options for tftp/tftpd"
//config:	depends on TFTP || TFTPD
//config:
//config:config FEATURE_TFTP_GET
//config:	bool "Enable 'tftp get' and/or tftpd upload code"
//config:	default y
//config:	depends on TFTP || TFTPD
//config:	help
//config:	Add support for the GET command within the TFTP client. This allows
//config:	a client to retrieve a file from a TFTP server.
//config:	Also enable upload support in tftpd, if tftpd is selected.
//config:
//config:	Note: this option does _not_ make tftpd capable of download
//config:	(the usual operation people need from it)!
//config:
//config:config FEATURE_TFTP_PUT
//config:	bool "Enable 'tftp put' and/or tftpd download code"
//config:	default y
//config:	depends on TFTP || TFTPD
//config:	help
//config:	Add support for the PUT command within the TFTP client. This allows
//config:	a client to transfer a file to a TFTP server.
//config:	Also enable download support in tftpd, if tftpd is selected.
//config:
//config:config FEATURE_TFTP_BLOCKSIZE
//config:	bool "Enable 'blksize' and 'tsize' protocol options"
//config:	default y
//config:	depends on TFTP || TFTPD
//config:	help
//config:	Allow tftp to specify block size, and tftpd to understand
//config:	"blksize" and "tsize" options.
//config:
//config:config TFTP_DEBUG
//config:	bool "Enable debug"
//config:	default n
//config:	depends on TFTP || TFTPD
//config:	help
//config:	Make tftp[d] print debugging messages on stderr.
//config:	This is useful if you are diagnosing a bug in tftp[d].

//applet:#if ENABLE_FEATURE_TFTP_GET || ENABLE_FEATURE_TFTP_PUT
//applet:IF_TFTP(APPLET(tftp, BB_DIR_USR_BIN, BB_SUID_DROP))
//applet:IF_TFTPD(APPLET(tftpd, BB_DIR_USR_SBIN, BB_SUID_DROP))
//applet:#endif

//kbuild:lib-$(CONFIG_TFTP) += tftp.o
//kbuild:lib-$(CONFIG_TFTPD) += tftp.o

//usage:#define tftp_trivial_usage
//usage:       "[OPTIONS] HOST [PORT]"
//usage:#define tftp_full_usage "\n\n"
//usage:       "Transfer a file from/to tftp server\n"
//usage:     "\n	-l FILE	Local FILE"
//usage:     "\n	-r FILE	Remote FILE"
//usage:	IF_FEATURE_TFTP_GET(
//usage:     "\n	-g	Get file"
//usage:	)
//usage:	IF_FEATURE_TFTP_PUT(
//usage:     "\n	-p	Put file"
//usage:	)
//usage:	IF_FEATURE_TFTP_BLOCKSIZE(
//usage:     "\n	-b SIZE	Transfer blocks of SIZE octets"
//usage:	)
//usage:
//usage:#define tftpd_trivial_usage
//usage:       "[-cr] [-u USER] [DIR]"
//usage:#define tftpd_full_usage "\n\n"
//usage:       "Transfer a file on tftp client's request\n"
//usage:       "\n"
//usage:       "tftpd should be used as an inetd service.\n"
//usage:       "tftpd's line for inetd.conf:\n"
//usage:       "	69 dgram udp nowait root tftpd tftpd -l /files/to/serve\n"
//usage:       "It also can be ran from udpsvd:\n"
//usage:       "	udpsvd -vE 0.0.0.0 69 tftpd /files/to/serve\n"
//usage:     "\n	-r	Prohibit upload"
//usage:     "\n	-c	Allow file creation via upload"
//usage:     "\n	-u	Access files as USER"
//usage:     "\n	-l	Log to syslog (inetd mode requires this)"

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>

#if ENABLE_FEATURE_TFTP_GET || ENABLE_FEATURE_TFTP_PUT

#define TFTP_BLKSIZE_DEFAULT       512  /* according to RFC 1350, don't change */
#define TFTP_BLKSIZE_DEFAULT_STR "512"
/* Was 50 ms but users asked to bump it up a bit */
#define TFTP_TIMEOUT_MS            100
#define TFTP_MAXTIMEOUT_MS        2000
#define TFTP_NUM_RETRIES            12  /* number of backed-off retries */

/* opcodes we support */
#define TFTP_RRQ   1
#define TFTP_WRQ   2
#define TFTP_DATA  3
#define TFTP_ACK   4
#define TFTP_ERROR 5
#define TFTP_OACK  6

/* error codes sent over network (we use only 0, 1, 3 and 8) */
/* generic (error message is included in the packet) */
#define ERR_UNSPEC   0
#define ERR_NOFILE   1
#define ERR_ACCESS   2
/* disk full or allocation exceeded */
#define ERR_WRITE    3
#define ERR_OP       4
#define ERR_BAD_ID   5
#define ERR_EXIST    6
#define ERR_BAD_USER 7
#define ERR_BAD_OPT  8

/* masks coming from getopt32 */
enum {
	TFTP_OPT_GET = (1 << 0),
	TFTP_OPT_PUT = (1 << 1),
	/* pseudo option: if set, it's tftpd */
	TFTPD_OPT = (1 << 7) * ENABLE_TFTPD,
	TFTPD_OPT_r = (1 << 8) * ENABLE_TFTPD,
	TFTPD_OPT_c = (1 << 9) * ENABLE_TFTPD,
	TFTPD_OPT_u = (1 << 10) * ENABLE_TFTPD,
	TFTPD_OPT_l = (1 << 11) * ENABLE_TFTPD,
};

#if ENABLE_FEATURE_TFTP_GET && !ENABLE_FEATURE_TFTP_PUT
#define IF_GETPUT(...)
#define CMD_GET(cmd) 1
#define CMD_PUT(cmd) 0
#elif !ENABLE_FEATURE_TFTP_GET && ENABLE_FEATURE_TFTP_PUT
#define IF_GETPUT(...)
#define CMD_GET(cmd) 0
#define CMD_PUT(cmd) 1
#else
#define IF_GETPUT(...) __VA_ARGS__
#define CMD_GET(cmd) ((cmd) & TFTP_OPT_GET)
#define CMD_PUT(cmd) ((cmd) & TFTP_OPT_PUT)
#endif
/* NB: in the code below
 * CMD_GET(cmd) and CMD_PUT(cmd) are mutually exclusive
 */


struct globals {
	/* u16 TFTP_ERROR; u16 reason; both network-endian, then error text: */
	uint8_t error_pkt[4 + 32];
	struct passwd *pw;
	/* Used in tftpd_main() for initial packet */
	/* Some HP PA-RISC firmware always sends fixed 516-byte requests */
	char block_buf[516];
	char block_buf_tail[1];
#if ENABLE_FEATURE_TFTP_PROGRESS_BAR
	off_t pos;
	off_t size;
	const char *file;
	bb_progress_t pmt;
#endif
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)

#define G_error_pkt_reason (G.error_pkt[3])
#define G_error_pkt_str    ((char*)(G.error_pkt + 4))

#if ENABLE_FEATURE_TFTP_PROGRESS_BAR && ENABLE_FEATURE_TFTP_BLOCKSIZE
static void tftp_progress_update(void)
{
	bb_progress_update(&G.pmt, 0, G.pos, G.size);
}
static void tftp_progress_init(void)
{
	bb_progress_init(&G.pmt, G.file);
	tftp_progress_update();
}
static void tftp_progress_done(void)
{
	if (is_bb_progress_inited(&G.pmt)) {
		tftp_progress_update();
		bb_putchar_stderr('\n');
		bb_progress_free(&G.pmt);
	}
}
#else
# define tftp_progress_update() ((void)0)
# define tftp_progress_init() ((void)0)
# define tftp_progress_done() ((void)0)
#endif

#if ENABLE_FEATURE_TFTP_BLOCKSIZE

static int tftp_blksize_check(const char *blksize_str, int maxsize)
{
	/* Check if the blksize is valid:
	 * RFC2348 says between 8 and 65464,
	 * but our implementation makes it impossible
	 * to use blksizes smaller than 22 octets. */
	unsigned blksize = bb_strtou(blksize_str, NULL, 10);
	if (errno
	 || (blksize < 24) || (blksize > maxsize)
	) {
		bb_error_msg("bad blocksize '%s'", blksize_str);
		return -1;
	}
# if ENABLE_TFTP_DEBUG
	bb_error_msg("using blksize %u", blksize);
# endif
	return blksize;
}

static char *tftp_get_option(const char *option, char *buf, int len)
{
	int opt_val = 0;
	int opt_found = 0;
	int k;

	/* buf points to:
	 * "opt_name<NUL>opt_val<NUL>opt_name2<NUL>opt_val2<NUL>..." */

	while (len > 0) {
		/* Make sure options are terminated correctly */
		for (k = 0; k < len; k++) {
			if (buf[k] == '\0') {
				goto nul_found;
			}
		}
		return NULL;
 nul_found:
		if (opt_val == 0) { /* it's "name" part */
			if (strcasecmp(buf, option) == 0) {
				opt_found = 1;
			}
		} else if (opt_found) {
			return buf;
		}

		k++;
		buf += k;
		len -= k;
		opt_val ^= 1;
	}

	return NULL;
}

#endif

static int tftp_protocol(
		/* NULL if tftp, !NULL if tftpd: */
		len_and_sockaddr *our_lsa,
		len_and_sockaddr *peer_lsa,
		const char *local_file
		IF_TFTP(, const char *remote_file)
#if !ENABLE_TFTP
# define remote_file NULL
#endif
		/* 1 for tftp; 1/0 for tftpd depending whether client asked about it: */
		IF_FEATURE_TFTP_BLOCKSIZE(, int want_transfer_size)
		IF_FEATURE_TFTP_BLOCKSIZE(, int blksize))
{
#if !ENABLE_FEATURE_TFTP_BLOCKSIZE
	enum { blksize = TFTP_BLKSIZE_DEFAULT };
#endif

	struct pollfd pfd[1];
#define socket_fd (pfd[0].fd)
	int len;
	int send_len;
	IF_FEATURE_TFTP_BLOCKSIZE(smallint expect_OACK = 0;)
	smallint finished = 0;
	uint16_t opcode;
	uint16_t block_nr;
	uint16_t recv_blk;
	int open_mode, local_fd;
	int retries, waittime_ms;
	int io_bufsize = blksize + 4;
	char *cp;
	/* Can't use RESERVE_CONFIG_BUFFER here since the allocation
	 * size varies meaning BUFFERS_GO_ON_STACK would fail.
	 *
	 * We must keep the transmit and receive buffers separate
	 * in case we rcv a garbage pkt - we need to rexmit the last pkt.
	 */
	char *xbuf = xmalloc(io_bufsize);
	char *rbuf = xmalloc(io_bufsize);

	socket_fd = xsocket(peer_lsa->u.sa.sa_family, SOCK_DGRAM, 0);
	setsockopt_reuseaddr(socket_fd);

	if (!ENABLE_TFTP || our_lsa) { /* tftpd */
		/* Create a socket which is:
		 * 1. bound to IP:port peer sent 1st datagram to,
		 * 2. connected to peer's IP:port
		 * This way we will answer from the IP:port peer
		 * expects, will not get any other packets on
		 * the socket, and also plain read/write will work. */
		xbind(socket_fd, &our_lsa->u.sa, our_lsa->len);
		xconnect(socket_fd, &peer_lsa->u.sa, peer_lsa->len);

		/* Is there an error already? Send pkt and bail out */
		if (G_error_pkt_reason || G_error_pkt_str[0])
			goto send_err_pkt;

		if (G.pw) {
			change_identity(G.pw); /* initgroups, setgid, setuid */
		}
	}

	/* Prepare open mode */
	if (CMD_PUT(option_mask32)) {
		open_mode = O_RDONLY;
	} else {
		open_mode = O_WRONLY | O_TRUNC | O_CREAT;
#if ENABLE_TFTPD
		if ((option_mask32 & (TFTPD_OPT+TFTPD_OPT_c)) == TFTPD_OPT) {
			/* tftpd without -c */
			open_mode = O_WRONLY | O_TRUNC;
		}
#endif
	}

	/* Examples of network traffic.
	 * Note two cases when ACKs with block# of 0 are sent.
	 *
	 * Download without options:
	 * tftp -> "\0\1FILENAME\0octet\0"
	 *         "\0\3\0\1FILEDATA..." <- tftpd
	 * tftp -> "\0\4\0\1"
	 * ...
	 * Download with option of blksize 16384:
	 * tftp -> "\0\1FILENAME\0octet\0blksize\00016384\0"
	 *         "\0\6blksize\00016384\0" <- tftpd
	 * tftp -> "\0\4\0\0"
	 *         "\0\3\0\1FILEDATA..." <- tftpd
	 * tftp -> "\0\4\0\1"
	 * ...
	 * Upload without options:
	 * tftp -> "\0\2FILENAME\0octet\0"
	 *         "\0\4\0\0" <- tftpd
	 * tftp -> "\0\3\0\1FILEDATA..."
	 *         "\0\4\0\1" <- tftpd
	 * ...
	 * Upload with option of blksize 16384:
	 * tftp -> "\0\2FILENAME\0octet\0blksize\00016384\0"
	 *         "\0\6blksize\00016384\0" <- tftpd
	 * tftp -> "\0\3\0\1FILEDATA..."
	 *         "\0\4\0\1" <- tftpd
	 * ...
	 */
	block_nr = 1;
	cp = xbuf + 2;

	if (!ENABLE_TFTP || our_lsa) { /* tftpd */
		/* Open file (must be after changing user) */
		local_fd = open(local_file, open_mode, 0666);
		if (local_fd < 0) {
			G_error_pkt_reason = ERR_NOFILE;
			strcpy(G_error_pkt_str, "can't open file");
			goto send_err_pkt;
		}
/* gcc 4.3.1 would NOT optimize it out as it should! */
#if ENABLE_FEATURE_TFTP_BLOCKSIZE
		if (blksize != TFTP_BLKSIZE_DEFAULT || want_transfer_size) {
			/* Create and send OACK packet. */
			/* For the download case, block_nr is still 1 -
			 * we expect 1st ACK from peer to be for (block_nr-1),
			 * that is, for "block 0" which is our OACK pkt */
			opcode = TFTP_OACK;
			goto add_blksize_opt;
		}
#endif
		if (CMD_GET(option_mask32)) {
			/* It's upload and we don't send OACK.
			 * We must ACK 1st packet (with filename)
			 * as if it is "block 0" */
			block_nr = 0;
		}
	} else { /* tftp */
		/* Open file (must be after changing user) */
		local_fd = CMD_GET(option_mask32) ? STDOUT_FILENO : STDIN_FILENO;
		if (NOT_LONE_DASH(local_file))
			local_fd = xopen(local_file, open_mode);
/* Removing #if, or using if() statement instead of #if may lead to
 * "warning: null argument where non-null required": */
#if ENABLE_TFTP
		/* tftp */

		/* We can't (and don't really need to) bind the socket:
		 * we don't know from which local IP datagrams will be sent,
		 * but kernel will pick the same IP every time (unless routing
		 * table is changed), thus peer will see dgrams consistently
		 * coming from the same IP.
		 * We would like to connect the socket, but since peer's
		 * UDP code can be less perfect than ours, _peer's_ IP:port
		 * in replies may differ from IP:port we used to send
		 * our first packet. We can connect() only when we get
		 * first reply. */

		/* build opcode */
		opcode = TFTP_WRQ;
		if (CMD_GET(option_mask32)) {
			opcode = TFTP_RRQ;
		}
		/* add filename and mode */
		/* fill in packet if the filename fits into xbuf */
		len = strlen(remote_file) + 1;
		if (2 + len + sizeof("octet") >= io_bufsize) {
			bb_error_msg("remote filename is too long");
			goto ret;
		}
		strcpy(cp, remote_file);
		cp += len;
		/* add "mode" part of the packet */
		strcpy(cp, "octet");
		cp += sizeof("octet");

# if ENABLE_FEATURE_TFTP_BLOCKSIZE
		if (blksize == TFTP_BLKSIZE_DEFAULT && !want_transfer_size)
			goto send_pkt;

		/* Need to add option to pkt */
		if ((&xbuf[io_bufsize - 1] - cp) < sizeof("blksize NNNNN tsize ") + sizeof(off_t)*3) {
			bb_error_msg("remote filename is too long");
			goto ret;
		}
		expect_OACK = 1;
# endif
#endif /* ENABLE_TFTP */

#if ENABLE_FEATURE_TFTP_BLOCKSIZE
 add_blksize_opt:
		if (blksize != TFTP_BLKSIZE_DEFAULT) {
			/* add "blksize", <nul>, blksize, <nul> */
			strcpy(cp, "blksize");
			cp += sizeof("blksize");
			cp += snprintf(cp, 6, "%d", blksize) + 1;
		}
		if (want_transfer_size) {
			/* add "tsize", <nul>, size, <nul> (see RFC2349) */
			/* if tftp and downloading, we send "0" (since we opened local_fd with O_TRUNC)
			 * and this makes server to send "tsize" option with the size */
			/* if tftp and uploading, we send file size (maybe dont, to not confuse old servers???) */
			/* if tftpd and downloading, we are answering to client's request */
			/* if tftpd and uploading: !want_transfer_size, this code is not executed */
			struct stat st;
			strcpy(cp, "tsize");
			cp += sizeof("tsize");
			st.st_size = 0;
			fstat(local_fd, &st);
			cp += sprintf(cp, "%"OFF_FMT"u", (off_t)st.st_size) + 1;
# if ENABLE_FEATURE_TFTP_PROGRESS_BAR
			/* Save for progress bar. If 0 (tftp downloading),
			 * we look at server's reply later */
			G.size = st.st_size;
			if (remote_file && st.st_size)
				tftp_progress_init();
# endif
		}
#endif
		/* First packet is built, so skip packet generation */
		goto send_pkt;
	}

	/* Using mostly goto's - continue/break will be less clear
	 * in where we actually jump to */
	while (1) {
		/* Build ACK or DATA */
		cp = xbuf + 2;
		*((uint16_t*)cp) = htons(block_nr);
		cp += 2;
		block_nr++;
		opcode = TFTP_ACK;
		if (CMD_PUT(option_mask32)) {
			opcode = TFTP_DATA;
			len = full_read(local_fd, cp, blksize);
			if (len < 0) {
				goto send_read_err_pkt;
			}
			if (len != blksize) {
				finished = 1;
			}
			cp += len;
			IF_FEATURE_TFTP_PROGRESS_BAR(G.pos += len;)
		}
 send_pkt:
		/* Send packet */
		*((uint16_t*)xbuf) = htons(opcode); /* fill in opcode part */
		send_len = cp - xbuf;
		/* NB: send_len value is preserved in code below
		 * for potential resend */

		retries = TFTP_NUM_RETRIES;  /* re-initialize */
		waittime_ms = TFTP_TIMEOUT_MS;

 send_again:
#if ENABLE_TFTP_DEBUG
		fprintf(stderr, "sending %u bytes\n", send_len);
		for (cp = xbuf; cp < &xbuf[send_len]; cp++)
			fprintf(stderr, "%02x ", (unsigned char) *cp);
		fprintf(stderr, "\n");
#endif
		xsendto(socket_fd, xbuf, send_len, &peer_lsa->u.sa, peer_lsa->len);

#if ENABLE_FEATURE_TFTP_PROGRESS_BAR
		if (is_bb_progress_inited(&G.pmt))
			tftp_progress_update();
#endif
		/* Was it final ACK? then exit */
		if (finished && (opcode == TFTP_ACK))
			goto ret;

 recv_again:
		/* Receive packet */
		/*pfd[0].fd = socket_fd;*/
		pfd[0].events = POLLIN;
		switch (safe_poll(pfd, 1, waittime_ms)) {
		default:
			/*bb_perror_msg("poll"); - done in safe_poll */
			goto ret;
		case 0:
			retries--;
			if (retries == 0) {
				tftp_progress_done();
				bb_error_msg("timeout");
				goto ret; /* no err packet sent */
			}

			/* exponential backoff with limit */
			waittime_ms += waittime_ms/2;
			if (waittime_ms > TFTP_MAXTIMEOUT_MS) {
				waittime_ms = TFTP_MAXTIMEOUT_MS;
			}

			goto send_again; /* resend last sent pkt */
		case 1:
			if (!our_lsa) {
				/* tftp (not tftpd!) receiving 1st packet */
				our_lsa = ((void*)(ptrdiff_t)-1); /* not NULL */
				len = recvfrom(socket_fd, rbuf, io_bufsize, 0,
						&peer_lsa->u.sa, &peer_lsa->len);
				/* Our first dgram went to port 69
				 * but reply may come from different one.
				 * Remember and use this new port (and IP) */
				if (len >= 0)
					xconnect(socket_fd, &peer_lsa->u.sa, peer_lsa->len);
			} else {
				/* tftpd, or not the very first packet:
				 * socket is connect()ed, can just read from it. */
				/* Don't full_read()!
				 * This is not TCP, one read == one pkt! */
				len = safe_read(socket_fd, rbuf, io_bufsize);
			}
			if (len < 0) {
				goto send_read_err_pkt;
			}
			if (len < 4) { /* too small? */
				goto recv_again;
			}
		}

		/* Process recv'ed packet */
		opcode = ntohs( ((uint16_t*)rbuf)[0] );
		recv_blk = ntohs( ((uint16_t*)rbuf)[1] );
#if ENABLE_TFTP_DEBUG
		fprintf(stderr, "received %d bytes: %04x %04x\n", len, opcode, recv_blk);
#endif
		if (opcode == TFTP_ERROR) {
			static const char errcode_str[] ALIGN1 =
				"\0"
				"file not found\0"
				"access violation\0"
				"disk full\0"
				"bad operation\0"
				"unknown transfer id\0"
				"file already exists\0"
				"no such user\0"
				"bad option";

			const char *msg = "";

			if (len > 4 && rbuf[4] != '\0') {
				msg = &rbuf[4];
				rbuf[io_bufsize - 1] = '\0'; /* paranoia */
			} else if (recv_blk <= 8) {
				msg = nth_string(errcode_str, recv_blk);
			}
			bb_error_msg("server error: (%u) %s", recv_blk, msg);
			goto ret;
		}

#if ENABLE_FEATURE_TFTP_BLOCKSIZE
		if (expect_OACK) {
			expect_OACK = 0;
			if (opcode == TFTP_OACK) {
				/* server seems to support options */
				char *res;

				res = tftp_get_option("blksize", &rbuf[2], len - 2);
				if (res) {
					blksize = tftp_blksize_check(res, blksize);
					if (blksize < 0) {
						G_error_pkt_reason = ERR_BAD_OPT;
						goto send_err_pkt;
					}
					io_bufsize = blksize + 4;
				}
# if ENABLE_FEATURE_TFTP_PROGRESS_BAR
				if (remote_file && G.size == 0) { /* if we don't know it yet */
					res = tftp_get_option("tsize", &rbuf[2], len - 2);
					if (res) {
						G.size = bb_strtoull(res, NULL, 10);
						if (G.size)
							tftp_progress_init();
					}
				}
# endif
				if (CMD_GET(option_mask32)) {
					/* We'll send ACK for OACK,
					 * such ACK has "block no" of 0 */
					block_nr = 0;
				}
				continue;
			}
			/* rfc2347:
			 * "An option not acknowledged by the server
			 * must be ignored by the client and server
			 * as if it were never requested." */
			if (blksize != TFTP_BLKSIZE_DEFAULT)
				bb_error_msg("falling back to blocksize "TFTP_BLKSIZE_DEFAULT_STR);
			blksize = TFTP_BLKSIZE_DEFAULT;
			io_bufsize = TFTP_BLKSIZE_DEFAULT + 4;
		}
#endif
		/* block_nr is already advanced to next block# we expect
		 * to get / block# we are about to send next time */

		if (CMD_GET(option_mask32) && (opcode == TFTP_DATA)) {
			if (recv_blk == block_nr) {
				int sz = full_write(local_fd, &rbuf[4], len - 4);
				if (sz != len - 4) {
					strcpy(G_error_pkt_str, bb_msg_write_error);
					G_error_pkt_reason = ERR_WRITE;
					goto send_err_pkt;
				}
				if (sz != blksize) {
					finished = 1;
				}
				IF_FEATURE_TFTP_PROGRESS_BAR(G.pos += sz;)
				continue; /* send ACK */
			}
/* Disabled to cope with servers with Sorcerer's Apprentice Syndrome */
#if 0
			if (recv_blk == (block_nr - 1)) {
				/* Server lost our TFTP_ACK.  Resend it */
				block_nr = recv_blk;
				continue;
			}
#endif
		}

		if (CMD_PUT(option_mask32) && (opcode == TFTP_ACK)) {
			/* did peer ACK our last DATA pkt? */
			if (recv_blk == (uint16_t) (block_nr - 1)) {
				if (finished)
					goto ret;
				continue; /* send next block */
			}
		}
		/* Awww... recv'd packet is not recognized! */
		goto recv_again;
		/* why recv_again? - rfc1123 says:
		 * "The sender (i.e., the side originating the DATA packets)
		 *  must never resend the current DATA packet on receipt
		 *  of a duplicate ACK".
		 * DATA pkts are resent ONLY on timeout.
		 * Thus "goto send_again" will ba a bad mistake above.
		 * See:
		 * http://en.wikipedia.org/wiki/Sorcerer's_Apprentice_Syndrome
		 */
	} /* end of "while (1)" */
 ret:
	if (ENABLE_FEATURE_CLEAN_UP) {
		close(local_fd);
		close(socket_fd);
		free(xbuf);
		free(rbuf);
	}
	return finished == 0; /* returns 1 on failure */

 send_read_err_pkt:
	strcpy(G_error_pkt_str, bb_msg_read_error);
 send_err_pkt:
	if (G_error_pkt_str[0])
		bb_error_msg("%s", G_error_pkt_str);
	G.error_pkt[1] = TFTP_ERROR;
	xsendto(socket_fd, G.error_pkt, 4 + 1 + strlen(G_error_pkt_str),
			&peer_lsa->u.sa, peer_lsa->len);
	return EXIT_FAILURE;
#undef remote_file
}

#if ENABLE_TFTP

int tftp_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tftp_main(int argc UNUSED_PARAM, char **argv)
{
	len_and_sockaddr *peer_lsa;
	const char *local_file = NULL;
	const char *remote_file = NULL;
# if ENABLE_FEATURE_TFTP_BLOCKSIZE
	const char *blksize_str = TFTP_BLKSIZE_DEFAULT_STR;
	int blksize;
# endif
	int result;
	int port;
	IF_GETPUT(int opt;)

	INIT_G();

	IF_GETPUT(opt =) getopt32(argv, "^"
			IF_FEATURE_TFTP_GET("g") IF_FEATURE_TFTP_PUT("p")
			"l:r:" IF_FEATURE_TFTP_BLOCKSIZE("b:")
			"\0"
			/* -p or -g is mandatory, and they are mutually exclusive */
			IF_FEATURE_TFTP_GET("g:") IF_FEATURE_TFTP_PUT("p:")
			IF_GETPUT("g--p:p--g:"),
			&local_file, &remote_file
			IF_FEATURE_TFTP_BLOCKSIZE(, &blksize_str)
	);
	argv += optind;

# if ENABLE_FEATURE_TFTP_BLOCKSIZE
	/* Check if the blksize is valid:
	 * RFC2348 says between 8 and 65464 */
	blksize = tftp_blksize_check(blksize_str, 65564);
	if (blksize < 0) {
		//bb_error_msg("bad block size");
		return EXIT_FAILURE;
	}
# endif

	if (remote_file) {
		if (!local_file) {
			const char *slash = strrchr(remote_file, '/');
			local_file = slash ? slash + 1 : remote_file;
		}
	} else {
		remote_file = local_file;
	}

	/* Error if filename or host is not known */
	if (!remote_file || !argv[0])
		bb_show_usage();

	port = bb_lookup_port(argv[1], "udp", 69);
	peer_lsa = xhost2sockaddr(argv[0], port);

# if ENABLE_TFTP_DEBUG
	fprintf(stderr, "using server '%s', remote_file '%s', local_file '%s'\n",
			xmalloc_sockaddr2dotted(&peer_lsa->u.sa),
			remote_file, local_file);
# endif

# if ENABLE_FEATURE_TFTP_PROGRESS_BAR
	G.file = remote_file;
# endif
	result = tftp_protocol(
		NULL /*our_lsa*/, peer_lsa,
		local_file, remote_file
		IF_FEATURE_TFTP_BLOCKSIZE(, 1 /* want_transfer_size */)
		IF_FEATURE_TFTP_BLOCKSIZE(, blksize)
	);
	tftp_progress_done();

	if (result != EXIT_SUCCESS && NOT_LONE_DASH(local_file) && CMD_GET(opt)) {
		unlink(local_file);
	}
	return result;
}

#endif /* ENABLE_TFTP */

#if ENABLE_TFTPD
int tftpd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int tftpd_main(int argc UNUSED_PARAM, char **argv)
{
	len_and_sockaddr *our_lsa;
	len_and_sockaddr *peer_lsa;
	char *mode, *user_opt;
	char *local_file = local_file;
	const char *error_msg;
	int opt, result, opcode;
	IF_FEATURE_TFTP_BLOCKSIZE(int blksize = TFTP_BLKSIZE_DEFAULT;)
	IF_FEATURE_TFTP_BLOCKSIZE(int want_transfer_size = 0;)

	INIT_G();

	our_lsa = get_sock_lsa(STDIN_FILENO);
	if (!our_lsa) {
		/* This is confusing:
		 *bb_error_msg_and_die("stdin is not a socket");
		 * Better: */
		bb_show_usage();
		/* Help text says that tftpd must be used as inetd service,
		 * which is by far the most usual cause of get_sock_lsa
		 * failure */
	}
	peer_lsa = xzalloc(LSA_LEN_SIZE + our_lsa->len);
	peer_lsa->len = our_lsa->len;

	/* Shifting to not collide with TFTP_OPTs */
	opt = option_mask32 = TFTPD_OPT | (getopt32(argv, "rcu:l", &user_opt) << 8);
	argv += optind;
	if (opt & TFTPD_OPT_l) {
		openlog(applet_name, LOG_PID, LOG_DAEMON);
		logmode = LOGMODE_SYSLOG;
	}
	if (opt & TFTPD_OPT_u) {
		/* Must be before xchroot */
		G.pw = xgetpwnam(user_opt);
	}
	if (argv[0]) {
		xchroot(argv[0]);
	}

	result = recv_from_to(STDIN_FILENO,
			G.block_buf, sizeof(G.block_buf) + 1,
			/* ^^^ sizeof+1 to reliably detect oversized input */
			0 /* flags */,
			&peer_lsa->u.sa, &our_lsa->u.sa, our_lsa->len);

	error_msg = "malformed packet";
	opcode = ntohs(*(uint16_t*)G.block_buf);
	if (result < 4 || result > sizeof(G.block_buf)
	/*|| G.block_buf[result-1] != '\0' - bug compatibility, see below */
	 || (IF_FEATURE_TFTP_PUT(opcode != TFTP_RRQ) /* not download */
	     IF_GETPUT(&&)
	     IF_FEATURE_TFTP_GET(opcode != TFTP_WRQ) /* not upload */
	    )
	) {
		goto err;
	}
	/* Some HP PA-RISC firmware always sends fixed 516-byte requests,
	 * with trailing garbage.
	 * Support that by not requiring NUL to be the last byte (see above).
	 * To make strXYZ() ops safe, force NUL termination:
	 */
	G.block_buf_tail[0] = '\0';

	local_file = G.block_buf + 2;
	if (local_file[0] == '.' || strstr(local_file, "/.")) {
		error_msg = "dot in file name";
		goto err;
	}
	mode = local_file + strlen(local_file) + 1;
	/* RFC 1350 says mode string is case independent */
	if (mode >= G.block_buf + result || strcasecmp(mode, "octet") != 0) {
		goto err;
	}
# if ENABLE_FEATURE_TFTP_BLOCKSIZE
	{
		char *res;
		char *opt_str = mode + sizeof("octet");
		int opt_len = G.block_buf + result - opt_str;
		if (opt_len > 0) {
			res = tftp_get_option("blksize", opt_str, opt_len);
			if (res) {
				blksize = tftp_blksize_check(res, 65564);
				if (blksize < 0) {
					G_error_pkt_reason = ERR_BAD_OPT;
					/* will just send error pkt */
					goto do_proto;
				}
			}
			if (opcode != TFTP_WRQ /* download? */
			/* did client ask us about file size? */
			 && tftp_get_option("tsize", opt_str, opt_len)
			) {
				want_transfer_size = 1;
			}
		}
	}
# endif

	if (!ENABLE_FEATURE_TFTP_PUT || opcode == TFTP_WRQ) {
		if (opt & TFTPD_OPT_r) {
			/* This would mean "disk full" - not true */
			/*G_error_pkt_reason = ERR_WRITE;*/
			error_msg = bb_msg_write_error;
			goto err;
		}
		IF_GETPUT(option_mask32 |= TFTP_OPT_GET;) /* will receive file's data */
	} else {
		IF_GETPUT(option_mask32 |= TFTP_OPT_PUT;) /* will send file's data */
	}

	/* NB: if G_error_pkt_str or G_error_pkt_reason is set up,
	 * tftp_protocol() just sends one error pkt and returns */

 do_proto:
	close(STDIN_FILENO); /* close old, possibly wildcard socket */
	/* tftp_protocol() will create new one, bound to particular local IP */
	result = tftp_protocol(
		our_lsa, peer_lsa,
		local_file IF_TFTP(, NULL /*remote_file*/)
		IF_FEATURE_TFTP_BLOCKSIZE(, want_transfer_size)
		IF_FEATURE_TFTP_BLOCKSIZE(, blksize)
	);

	return result;
 err:
	strcpy(G_error_pkt_str, error_msg);
	goto do_proto;
}

#endif /* ENABLE_TFTPD */

#endif /* ENABLE_FEATURE_TFTP_GET || ENABLE_FEATURE_TFTP_PUT */
