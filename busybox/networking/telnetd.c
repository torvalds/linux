/* vi: set sw=4 ts=4: */
/*
 * Simple telnet server
 * Bjorn Wesen, Axis Communications AB (bjornw@axis.com)
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * ---------------------------------------------------------------------------
 * (C) Copyright 2000, Axis Communications AB, LUND, SWEDEN
 ****************************************************************************
 *
 * The telnetd manpage says it all:
 *
 * Telnetd operates by allocating a pseudo-terminal device (see pty(4)) for
 * a client, then creating a login process which has the slave side of the
 * pseudo-terminal as stdin, stdout, and stderr. Telnetd manipulates the
 * master side of the pseudo-terminal, implementing the telnet protocol and
 * passing characters between the remote client and the login process.
 *
 * Vladimir Oleynik <dzo@simtreas.ru> 2001
 * Set process group corrections, initial busybox port
 */
//config:config TELNETD
//config:	bool "telnetd (12 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	A daemon for the TELNET protocol, allowing you to log onto the host
//config:	running the daemon. Please keep in mind that the TELNET protocol
//config:	sends passwords in plain text. If you can't afford the space for an
//config:	SSH daemon and you trust your network, you may say 'y' here. As a
//config:	more secure alternative, you should seriously consider installing the
//config:	very small Dropbear SSH daemon instead:
//config:		http://matt.ucc.asn.au/dropbear/dropbear.html
//config:
//config:	Note that for busybox telnetd to work you need several things:
//config:	First of all, your kernel needs:
//config:		  CONFIG_UNIX98_PTYS=y
//config:
//config:	Next, you need a /dev/pts directory on your root filesystem:
//config:
//config:		  $ ls -ld /dev/pts
//config:		  drwxr-xr-x  2 root root 0 Sep 23 13:21 /dev/pts/
//config:
//config:	Next you need the pseudo terminal master multiplexer /dev/ptmx:
//config:
//config:		  $ ls -la /dev/ptmx
//config:		  crw-rw-rw-  1 root tty 5, 2 Sep 23 13:55 /dev/ptmx
//config:
//config:	Any /dev/ttyp[0-9]* files you may have can be removed.
//config:	Next, you need to mount the devpts filesystem on /dev/pts using:
//config:
//config:		  mount -t devpts devpts /dev/pts
//config:
//config:	You need to be sure that busybox has LOGIN and
//config:	FEATURE_SUID enabled. And finally, you should make
//config:	certain that busybox has been installed setuid root:
//config:
//config:		chown root.root /bin/busybox
//config:		chmod 4755 /bin/busybox
//config:
//config:	with all that done, telnetd _should_ work....
//config:
//config:config FEATURE_TELNETD_STANDALONE
//config:	bool "Support standalone telnetd (not inetd only)"
//config:	default y
//config:	depends on TELNETD
//config:	help
//config:	Selecting this will make telnetd able to run standalone.
//config:
//config:config FEATURE_TELNETD_INETD_WAIT
//config:	bool "Support -w SEC option (inetd wait mode)"
//config:	default y
//config:	depends on FEATURE_TELNETD_STANDALONE
//config:	help
//config:	This option allows you to run telnetd in "inet wait" mode.
//config:	Example inetd.conf line (note "wait", not usual "nowait"):
//config:
//config:	telnet stream tcp wait root /bin/telnetd telnetd -w10
//config:
//config:	In this example, inetd passes _listening_ socket_ as fd 0
//config:	to telnetd when connection appears.
//config:	telnetd will wait for connections until all existing
//config:	connections are closed, and no new connections
//config:	appear during 10 seconds. Then it exits, and inetd continues
//config:	to listen for new connections.
//config:
//config:	This option is rarely used. "tcp nowait" is much more usual
//config:	way of running tcp services, including telnetd.
//config:	You most probably want to say N here.

//applet:IF_TELNETD(APPLET(telnetd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TELNETD) += telnetd.o

//usage:#define telnetd_trivial_usage
//usage:       "[OPTIONS]"
//usage:#define telnetd_full_usage "\n\n"
//usage:       "Handle incoming telnet connections"
//usage:	IF_NOT_FEATURE_TELNETD_STANDALONE(" via inetd") "\n"
//usage:     "\n	-l LOGIN	Exec LOGIN on connect"
//usage:     "\n	-f ISSUE_FILE	Display ISSUE_FILE instead of /etc/issue"
//usage:     "\n	-K		Close connection as soon as login exits"
//usage:     "\n			(normally wait until all programs close slave pty)"
//usage:	IF_FEATURE_TELNETD_STANDALONE(
//usage:     "\n	-p PORT		Port to listen on"
//usage:     "\n	-b ADDR[:PORT]	Address to bind to"
//usage:     "\n	-F		Run in foreground"
//usage:     "\n	-i		Inetd mode"
//usage:	IF_FEATURE_TELNETD_INETD_WAIT(
//usage:     "\n	-w SEC		Inetd 'wait' mode, linger time SEC"
//usage:     "\n	-S		Log to syslog (implied by -i or without -F and -w)"
//usage:	)
//usage:	)

#define DEBUG 0

#include "libbb.h"
#include "common_bufsiz.h"
#include <syslog.h>

#if DEBUG
# define TELCMDS
# define TELOPTS
#endif
#include <arpa/telnet.h>


struct tsession {
	struct tsession *next;
	pid_t shell_pid;
	int sockfd_read;
	int sockfd_write;
	int ptyfd;
	smallint buffered_IAC_for_pty;

	/* two circular buffers */
	/*char *buf1, *buf2;*/
/*#define TS_BUF1(ts) ts->buf1*/
/*#define TS_BUF2(ts) TS_BUF2(ts)*/
#define TS_BUF1(ts) ((unsigned char*)(ts + 1))
#define TS_BUF2(ts) (((unsigned char*)(ts + 1)) + BUFSIZE)
	int rdidx1, wridx1, size1;
	int rdidx2, wridx2, size2;
};

/* Two buffers are directly after tsession in malloced memory.
 * Make whole thing fit in 4k */
enum { BUFSIZE = (4 * 1024 - sizeof(struct tsession)) / 2 };


/* Globals */
struct globals {
	struct tsession *sessions;
	const char *loginpath;
	const char *issuefile;
	int maxfd;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	G.loginpath = "/bin/login"; \
	G.issuefile = "/etc/issue.net"; \
} while (0)


/* Write some buf1 data to pty, processing IACs.
 * Update wridx1 and size1. Return < 0 on error.
 * Buggy if IAC is present but incomplete: skips them.
 */
static ssize_t
safe_write_to_pty_decode_iac(struct tsession *ts)
{
	unsigned wr;
	ssize_t rc;
	unsigned char *buf;
	unsigned char *found;

	buf = TS_BUF1(ts) + ts->wridx1;
	wr = MIN(BUFSIZE - ts->wridx1, ts->size1);
	/* wr is at least 1 here */

	if (ts->buffered_IAC_for_pty) {
		/* Last time we stopped on a "dangling" IAC byte.
		 * We removed it from the buffer back then.
		 * Now pretend it's still there, and jump to IAC processing.
		 */
		ts->buffered_IAC_for_pty = 0;
		wr++;
		ts->size1++;
		buf--; /* Yes, this can point before the buffer. It's ok */
		ts->wridx1--;
		goto handle_iac;
	}

	found = memchr(buf, IAC, wr);
	if (found != buf) {
		/* There is a "prefix" of non-IAC chars.
		 * Write only them, and return.
		 */
		if (found)
			wr = found - buf;

		/* We map \r\n ==> \r for pragmatic reasons:
		 * many client implementations send \r\n when
		 * the user hits the CarriageReturn key.
		 * See RFC 1123 3.3.1 Telnet End-of-Line Convention.
		 */
		rc = wr;
		found = memchr(buf, '\r', wr);
		if (found)
			rc = found - buf + 1;
		rc = safe_write(ts->ptyfd, buf, rc);
		if (rc <= 0)
			return rc;
		if (rc < wr /* don't look past available data */
		 && buf[rc-1] == '\r' /* need this: imagine that write was _short_ */
		 && (buf[rc] == '\n' || buf[rc] == '\0')
		) {
			rc++;
		}
		goto update_and_return;
	}

	/* buf starts with IAC char. Process that sequence.
	 * Example: we get this from our own (bbox) telnet client:
	 * read(5, "\377\374\1""\377\373\37""\377\372\37\0\262\0@\377\360""\377\375\1""\377\375\3"):
	 * IAC WONT ECHO, IAC WILL NAWS, IAC SB NAWS <cols> <rows> IAC SE, IAC DO SGA
	 * Another example (telnet-0.17 from old-netkit):
	 * read(4, "\377\375\3""\377\373\30""\377\373\37""\377\373 ""\377\373!""\377\373\"""\377\373'"
	 * "\377\375\5""\377\373#""\377\374\1""\377\372\37\0\257\0I\377\360""\377\375\1"):
	 * IAC DO SGA, IAC WILL TTYPE, IAC WILL NAWS, IAC WILL TSPEED, IAC WILL LFLOW, IAC WILL LINEMODE, IAC WILL NEW_ENVIRON,
	 * IAC DO STATUS, IAC WILL XDISPLOC, IAC WONT ECHO, IAC SB NAWS <cols> <rows> IAC SE, IAC DO ECHO
	 */
	if (wr <= 1) {
		/* Only the single IAC byte is in the buffer, eat it
		 * and set a flag "process the rest of the sequence
		 * next time we are here".
		 */
		//bb_error_msg("dangling IAC!");
		ts->buffered_IAC_for_pty = 1;
		rc = 1;
		goto update_and_return;
	}

 handle_iac:
	/* 2-byte commands (240..250 and 255):
	 * IAC IAC (255) Literal 255. Supported.
	 * IAC SE  (240) End of subnegotiation. Treated as NOP.
	 * IAC NOP (241) NOP. Supported.
	 * IAC BRK (243) Break. Like serial line break. TODO via tcsendbreak()?
	 * IAC AYT (246) Are you there. Send back evidence that AYT was seen. TODO (send NOP back)?
	 *  These don't look useful:
	 * IAC DM  (242) Data mark. What is this?
	 * IAC IP  (244) Suspend, interrupt or abort the process. (Ancient cousin of ^C).
	 * IAC AO  (245) Abort output. "You can continue running, but do not send me the output".
	 * IAC EC  (247) Erase character. The receiver should delete the last received char.
	 * IAC EL  (248) Erase line. The receiver should delete everything up tp last newline.
	 * IAC GA  (249) Go ahead. For half-duplex lines: "now you talk".
	 *  Implemented only as part of NAWS:
	 * IAC SB  (250) Subnegotiation of an option follows.
	 */
	if (buf[1] == IAC) {
		/* Literal 255 (emacs M-DEL) */
		//bb_error_msg("255!");
		rc = safe_write(ts->ptyfd, &buf[1], 1);
		/*
		 * If we went through buffered_IAC_for_pty==1 path,
		 * bailing out on error like below messes up the buffer.
		 * EAGAIN is highly unlikely here, other errors will be
		 * repeated on next write, let's just skip error check.
		 */
#if 0
		if (rc <= 0)
			return rc;
#endif
		rc = 2;
		goto update_and_return;
	}
	if (buf[1] >= 240 && buf[1] <= 249) {
		/* NOP (241). Ignore (putty keepalive, etc) */
		/* All other 2-byte commands also treated as NOPs here */
		rc = 2;
		goto update_and_return;
	}

	if (wr <= 2) {
/* BUG: only 2 bytes of the IAC is in the buffer, we just eat them.
 * This is not a practical problem since >2 byte IACs are seen only
 * in initial negotiation, when buffer is empty
 */
		rc = 2;
		goto update_and_return;
	}

	if (buf[1] == SB) {
		if (buf[2] == TELOPT_NAWS) {
			/* IAC SB, TELOPT_NAWS, 4-byte, IAC SE */
			struct winsize ws;
			if (wr <= 6) {
/* BUG: incomplete, can't process */
				rc = wr;
				goto update_and_return;
			}
			memset(&ws, 0, sizeof(ws)); /* pixel sizes are set to 0 */
			ws.ws_col = (buf[3] << 8) | buf[4];
			ws.ws_row = (buf[5] << 8) | buf[6];
			ioctl(ts->ptyfd, TIOCSWINSZ, (char *)&ws);
			rc = 7;
			/* trailing IAC SE will be eaten separately, as 2-byte NOP */
			goto update_and_return;
		}
		/* else: other subnegs not supported yet */
	}

	/* Assume it is a 3-byte WILL/WONT/DO/DONT 251..254 command and skip it */
#if DEBUG
	fprintf(stderr, "Ignoring IAC %s,%s\n",
			TELCMD(buf[1]), TELOPT(buf[2]));
#endif
	rc = 3;

 update_and_return:
	ts->wridx1 += rc;
	if (ts->wridx1 >= BUFSIZE) /* actually == BUFSIZE */
		ts->wridx1 = 0;
	ts->size1 -= rc;
	/*
	 * Hack. We cannot process IACs which wrap around buffer's end.
	 * Since properly fixing it requires writing bigger code,
	 * we rely instead on this code making it virtually impossible
	 * to have wrapped IAC (people don't type at 2k/second).
	 * It also allows for bigger reads in common case.
	 */
	if (ts->size1 == 0) { /* very typical */
		//bb_error_msg("zero size1");
		ts->rdidx1 = 0;
		ts->wridx1 = 0;
		return rc;
	}
	wr = ts->wridx1;
	if (wr != 0 && wr < ts->rdidx1) {
		/* Buffer is not wrapped yet.
		 * We can easily move it to the beginning.
		 */
		//bb_error_msg("moved %d", wr);
		memmove(TS_BUF1(ts), TS_BUF1(ts) + wr, ts->size1);
		ts->rdidx1 -= wr;
		ts->wridx1 = 0;
	}
	return rc;
}

/*
 * Converting single IAC into double on output
 */
static size_t safe_write_double_iac(int fd, const char *buf, size_t count)
{
	const char *IACptr;
	size_t wr, rc, total;

	total = 0;
	while (1) {
		if (count == 0)
			return total;
		if (*buf == (char)IAC) {
			static const char IACIAC[] ALIGN1 = { IAC, IAC };
			rc = safe_write(fd, IACIAC, 2);
/* BUG: if partial write was only 1 byte long, we end up emitting just one IAC */
			if (rc != 2)
				break;
			buf++;
			total++;
			count--;
			continue;
		}
		/* count != 0, *buf != IAC */
		IACptr = memchr(buf, IAC, count);
		wr = count;
		if (IACptr)
			wr = IACptr - buf;
		rc = safe_write(fd, buf, wr);
		if (rc != wr)
			break;
		buf += rc;
		total += rc;
		count -= rc;
	}
	/* here: rc - result of last short write */
	if ((ssize_t)rc < 0) { /* error? */
		if (total == 0)
			return rc;
		rc = 0;
	}
	return total + rc;
}

/* Must match getopt32 string */
enum {
	OPT_WATCHCHILD = (1 << 2), /* -K */
	OPT_INETD      = (1 << 3) * ENABLE_FEATURE_TELNETD_STANDALONE, /* -i */
	OPT_PORT       = (1 << 4) * ENABLE_FEATURE_TELNETD_STANDALONE, /* -p PORT */
	OPT_FOREGROUND = (1 << 6) * ENABLE_FEATURE_TELNETD_STANDALONE, /* -F */
	OPT_SYSLOG     = (1 << 7) * ENABLE_FEATURE_TELNETD_INETD_WAIT, /* -S */
	OPT_WAIT       = (1 << 8) * ENABLE_FEATURE_TELNETD_INETD_WAIT, /* -w SEC */
};

static struct tsession *
make_new_session(
		IF_FEATURE_TELNETD_STANDALONE(int sock)
		IF_NOT_FEATURE_TELNETD_STANDALONE(void)
) {
#if !ENABLE_FEATURE_TELNETD_STANDALONE
	enum { sock = 0 };
#endif
	const char *login_argv[2];
	struct termios termbuf;
	int fd, pid;
	char tty_name[GETPTY_BUFSIZE];
	struct tsession *ts = xzalloc(sizeof(struct tsession) + BUFSIZE * 2);

	/*ts->buf1 = (char *)(ts + 1);*/
	/*ts->buf2 = ts->buf1 + BUFSIZE;*/

	/* Got a new connection, set up a tty */
	fd = xgetpty(tty_name);
	if (fd > G.maxfd)
		G.maxfd = fd;
	ts->ptyfd = fd;
	ndelay_on(fd);
	close_on_exec_on(fd);

	/* SO_KEEPALIVE by popular demand */
	setsockopt_keepalive(sock);
#if ENABLE_FEATURE_TELNETD_STANDALONE
	ts->sockfd_read = sock;
	ndelay_on(sock);
	if (sock == 0) { /* We are called with fd 0 - we are in inetd mode */
		sock++; /* so use fd 1 for output */
		ndelay_on(sock);
	}
	ts->sockfd_write = sock;
	if (sock > G.maxfd)
		G.maxfd = sock;
#else
	/* ts->sockfd_read = 0; - done by xzalloc */
	ts->sockfd_write = 1;
	ndelay_on(0);
	ndelay_on(1);
#endif

	/* Make the telnet client understand we will echo characters so it
	 * should not do it locally. We don't tell the client to run linemode,
	 * because we want to handle line editing and tab completion and other
	 * stuff that requires char-by-char support. */
	{
		static const char iacs_to_send[] ALIGN1 = {
			IAC, DO, TELOPT_ECHO,
			IAC, DO, TELOPT_NAWS,
			/* This requires telnetd.ctrlSQ.patch (incomplete) */
			/*IAC, DO, TELOPT_LFLOW,*/
			IAC, WILL, TELOPT_ECHO,
			IAC, WILL, TELOPT_SGA
		};
		/* This confuses safe_write_double_iac(), it will try to duplicate
		 * each IAC... */
		//memcpy(TS_BUF2(ts), iacs_to_send, sizeof(iacs_to_send));
		//ts->rdidx2 = sizeof(iacs_to_send);
		//ts->size2 = sizeof(iacs_to_send);
		/* So just stuff it into TCP stream! (no error check...) */
#if ENABLE_FEATURE_TELNETD_STANDALONE
		safe_write(sock, iacs_to_send, sizeof(iacs_to_send));
#else
		safe_write(1, iacs_to_send, sizeof(iacs_to_send));
#endif
		/*ts->rdidx2 = 0; - xzalloc did it */
		/*ts->size2 = 0;*/
	}

	fflush_all();
	pid = vfork(); /* NOMMU-friendly */
	if (pid < 0) {
		free(ts);
		close(fd);
		/* sock will be closed by caller */
		bb_perror_msg("vfork");
		return NULL;
	}
	if (pid > 0) {
		/* Parent */
		ts->shell_pid = pid;
		return ts;
	}

	/* Child */
	/* Careful - we are after vfork! */

	/* Restore default signal handling ASAP */
	bb_signals((1 << SIGCHLD) + (1 << SIGPIPE), SIG_DFL);

	pid = getpid();

	if (ENABLE_FEATURE_UTMP) {
		len_and_sockaddr *lsa = get_peer_lsa(sock);
		char *hostname = NULL;
		if (lsa) {
			hostname = xmalloc_sockaddr2dotted(&lsa->u.sa);
			free(lsa);
		}
		write_new_utmp(pid, LOGIN_PROCESS, tty_name, /*username:*/ "LOGIN", hostname);
		free(hostname);
	}

	/* Make new session and process group */
	setsid();

	/* Open the child's side of the tty */
	/* NB: setsid() disconnects from any previous ctty's. Therefore
	 * we must open child's side of the tty AFTER setsid! */
	close(0);
	xopen(tty_name, O_RDWR); /* becomes our ctty */
	xdup2(0, 1);
	xdup2(0, 2);
	tcsetpgrp(0, pid); /* switch this tty's process group to us */

	/* The pseudo-terminal allocated to the client is configured to operate
	 * in cooked mode, and with XTABS CRMOD enabled (see tty(4)) */
	tcgetattr(0, &termbuf);
	termbuf.c_lflag |= ECHO; /* if we use readline we dont want this */
	termbuf.c_oflag |= ONLCR | XTABS;
	termbuf.c_iflag |= ICRNL;
	termbuf.c_iflag &= ~IXOFF;
	/*termbuf.c_lflag &= ~ICANON;*/
	tcsetattr_stdin_TCSANOW(&termbuf);

	/* Uses FILE-based I/O to stdout, but does fflush_all(),
	 * so should be safe with vfork.
	 * I fear, though, that some users will have ridiculously big
	 * issue files, and they may block writing to fd 1,
	 * (parent is supposed to read it, but parent waits
	 * for vforked child to exec!) */
	print_login_issue(G.issuefile, tty_name);

	/* Exec shell / login / whatever */
	login_argv[0] = G.loginpath;
	login_argv[1] = NULL;
	/* exec busybox applet (if PREFER_APPLETS=y), if that fails,
	 * exec external program.
	 * NB: sock is either 0 or has CLOEXEC set on it.
	 * fd has CLOEXEC set on it too. These two fds will be closed here.
	 */
	BB_EXECVP(G.loginpath, (char **)login_argv);
	/* _exit is safer with vfork, and we shouldn't send message
	 * to remote clients anyway */
	_exit(EXIT_FAILURE); /*bb_perror_msg_and_die("execv %s", G.loginpath);*/
}

#if ENABLE_FEATURE_TELNETD_STANDALONE

static void
free_session(struct tsession *ts)
{
	struct tsession *t;

	if (option_mask32 & OPT_INETD)
		exit(EXIT_SUCCESS);

	/* Unlink this telnet session from the session list */
	t = G.sessions;
	if (t == ts)
		G.sessions = ts->next;
	else {
		while (t->next != ts)
			t = t->next;
		t->next = ts->next;
	}

#if 0
	/* It was said that "normal" telnetd just closes ptyfd,
	 * doesn't send SIGKILL. When we close ptyfd,
	 * kernel sends SIGHUP to processes having slave side opened. */
	kill(ts->shell_pid, SIGKILL);
	waitpid(ts->shell_pid, NULL, 0);
#endif
	close(ts->ptyfd);
	close(ts->sockfd_read);
	/* We do not need to close(ts->sockfd_write), it's the same
	 * as sockfd_read unless we are in inetd mode. But in inetd mode
	 * we do not reach this */
	free(ts);

	/* Scan all sessions and find new maxfd */
	G.maxfd = 0;
	ts = G.sessions;
	while (ts) {
		if (G.maxfd < ts->ptyfd)
			G.maxfd = ts->ptyfd;
		if (G.maxfd < ts->sockfd_read)
			G.maxfd = ts->sockfd_read;
#if 0
		/* Again, sockfd_write == sockfd_read here */
		if (G.maxfd < ts->sockfd_write)
			G.maxfd = ts->sockfd_write;
#endif
		ts = ts->next;
	}
}

#else /* !FEATURE_TELNETD_STANDALONE */

/* Used in main() only, thus "return 0" actually is exit(EXIT_SUCCESS). */
#define free_session(ts) return 0

#endif

static void handle_sigchld(int sig UNUSED_PARAM)
{
	pid_t pid;
	struct tsession *ts;
	int save_errno = errno;

	/* Looping: more than one child may have exited */
	while (1) {
		pid = wait_any_nohang(NULL);
		if (pid <= 0)
			break;
		ts = G.sessions;
		while (ts) {
			if (ts->shell_pid == pid) {
				ts->shell_pid = -1;
				update_utmp_DEAD_PROCESS(pid);
				break;
			}
			ts = ts->next;
		}
	}

	errno = save_errno;
}

int telnetd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int telnetd_main(int argc UNUSED_PARAM, char **argv)
{
	fd_set rdfdset, wrfdset;
	unsigned opt;
	int count;
	struct tsession *ts;
#if ENABLE_FEATURE_TELNETD_STANDALONE
#define IS_INETD (opt & OPT_INETD)
	int master_fd = master_fd; /* for compiler */
	int sec_linger = sec_linger;
	char *opt_bindaddr = NULL;
	char *opt_portnbr;
#else
	enum {
		IS_INETD = 1,
		master_fd = -1,
	};
#endif
	INIT_G();

	/* Even if !STANDALONE, we accept (and ignore) -i, thus people
	 * don't need to guess whether it's ok to pass -i to us */
	opt = getopt32(argv, "^"
			"f:l:Ki"
			IF_FEATURE_TELNETD_STANDALONE("p:b:F")
			IF_FEATURE_TELNETD_INETD_WAIT("Sw:+") /* -w NUM */
			"\0"
			/* -w implies -F. -w and -i don't mix */
			IF_FEATURE_TELNETD_INETD_WAIT("wF:i--w:w--i"),
			&G.issuefile, &G.loginpath
			IF_FEATURE_TELNETD_STANDALONE(, &opt_portnbr, &opt_bindaddr)
			IF_FEATURE_TELNETD_INETD_WAIT(, &sec_linger)
	);
	if (!IS_INETD /*&& !re_execed*/) {
		/* inform that we start in standalone mode?
		 * May be useful when people forget to give -i */
		/*bb_error_msg("listening for connections");*/
		if (!(opt & OPT_FOREGROUND)) {
			/* DAEMON_CHDIR_ROOT was giving inconsistent
			 * behavior with/without -F, -i */
			bb_daemonize_or_rexec(0 /*was DAEMON_CHDIR_ROOT*/, argv);
		}
	}
	/* Redirect log to syslog early, if needed */
	if (IS_INETD || (opt & OPT_SYSLOG) || !(opt & OPT_FOREGROUND)) {
		openlog(applet_name, LOG_PID, LOG_DAEMON);
		logmode = LOGMODE_SYSLOG;
	}
#if ENABLE_FEATURE_TELNETD_STANDALONE
	if (IS_INETD) {
		G.sessions = make_new_session(0);
		if (!G.sessions) /* pty opening or vfork problem, exit */
			return 1; /* make_new_session printed error message */
	} else {
		master_fd = 0;
		if (!(opt & OPT_WAIT)) {
			unsigned portnbr = 23;
			if (opt & OPT_PORT)
				portnbr = xatou16(opt_portnbr);
			master_fd = create_and_bind_stream_or_die(opt_bindaddr, portnbr);
			xlisten(master_fd, 1);
		}
		close_on_exec_on(master_fd);
	}
#else
	G.sessions = make_new_session();
	if (!G.sessions) /* pty opening or vfork problem, exit */
		return 1; /* make_new_session printed error message */
#endif

	/* We don't want to die if just one session is broken */
	signal(SIGPIPE, SIG_IGN);

	if (opt & OPT_WATCHCHILD)
		signal(SIGCHLD, handle_sigchld);
	else /* prevent dead children from becoming zombies */
		signal(SIGCHLD, SIG_IGN);

/*
   This is how the buffers are used. The arrows indicate data flow.

   +-------+     wridx1++     +------+     rdidx1++     +----------+
   |       | <--------------  | buf1 | <--------------  |          |
   |       |     size1--      +------+     size1++      |          |
   |  pty  |                                            |  socket  |
   |       |     rdidx2++     +------+     wridx2++     |          |
   |       |  --------------> | buf2 |  --------------> |          |
   +-------+     size2++      +------+     size2--      +----------+

   size1: "how many bytes are buffered for pty between rdidx1 and wridx1?"
   size2: "how many bytes are buffered for socket between rdidx2 and wridx2?"

   Each session has got two buffers. Buffers are circular. If sizeN == 0,
   buffer is empty. If sizeN == BUFSIZE, buffer is full. In both these cases
   rdidxN == wridxN.
*/
 again:
	FD_ZERO(&rdfdset);
	FD_ZERO(&wrfdset);

	/* Select on the master socket, all telnet sockets and their
	 * ptys if there is room in their session buffers.
	 * NB: scalability problem: we recalculate entire bitmap
	 * before each select. Can be a problem with 500+ connections. */
	ts = G.sessions;
	while (ts) {
		struct tsession *next = ts->next; /* in case we free ts */
		if (ts->shell_pid == -1) {
			/* Child died and we detected that */
			free_session(ts);
		} else {
			if (ts->size1 > 0)       /* can write to pty */
				FD_SET(ts->ptyfd, &wrfdset);
			if (ts->size1 < BUFSIZE) /* can read from socket */
				FD_SET(ts->sockfd_read, &rdfdset);
			if (ts->size2 > 0)       /* can write to socket */
				FD_SET(ts->sockfd_write, &wrfdset);
			if (ts->size2 < BUFSIZE) /* can read from pty */
				FD_SET(ts->ptyfd, &rdfdset);
		}
		ts = next;
	}
	if (!IS_INETD) {
		FD_SET(master_fd, &rdfdset);
		/* This is needed because free_session() does not
		 * take master_fd into account when it finds new
		 * maxfd among remaining fd's */
		if (master_fd > G.maxfd)
			G.maxfd = master_fd;
	}

	{
		struct timeval *tv_ptr = NULL;
#if ENABLE_FEATURE_TELNETD_INETD_WAIT
		struct timeval tv;
		if ((opt & OPT_WAIT) && !G.sessions) {
			tv.tv_sec = sec_linger;
			tv.tv_usec = 0;
			tv_ptr = &tv;
		}
#endif
		count = select(G.maxfd + 1, &rdfdset, &wrfdset, NULL, tv_ptr);
	}
	if (count == 0) /* "telnetd -w SEC" timed out */
		return 0;
	if (count < 0)
		goto again; /* EINTR or ENOMEM */

#if ENABLE_FEATURE_TELNETD_STANDALONE
	/* Check for and accept new sessions */
	if (!IS_INETD && FD_ISSET(master_fd, &rdfdset)) {
		int fd;
		struct tsession *new_ts;

		fd = accept(master_fd, NULL, NULL);
		if (fd < 0)
			goto again;
		close_on_exec_on(fd);

		/* Create a new session and link it into active list */
		new_ts = make_new_session(fd);
		if (new_ts) {
			new_ts->next = G.sessions;
			G.sessions = new_ts;
		} else {
			close(fd);
		}
	}
#endif

	/* Then check for data tunneling */
	ts = G.sessions;
	while (ts) { /* For all sessions... */
		struct tsession *next = ts->next; /* in case we free ts */

		if (/*ts->size1 &&*/ FD_ISSET(ts->ptyfd, &wrfdset)) {
			/* Write to pty from buffer 1 */
			count = safe_write_to_pty_decode_iac(ts);
			if (count < 0) {
				if (errno == EAGAIN)
					goto skip1;
				goto kill_session;
			}
		}
 skip1:
		if (/*ts->size2 &&*/ FD_ISSET(ts->sockfd_write, &wrfdset)) {
			/* Write to socket from buffer 2 */
			count = MIN(BUFSIZE - ts->wridx2, ts->size2);
			count = safe_write_double_iac(ts->sockfd_write, (void*)(TS_BUF2(ts) + ts->wridx2), count);
			if (count < 0) {
				if (errno == EAGAIN)
					goto skip2;
				goto kill_session;
			}
			ts->wridx2 += count;
			if (ts->wridx2 >= BUFSIZE) /* actually == BUFSIZE */
				ts->wridx2 = 0;
			ts->size2 -= count;
			if (ts->size2 == 0) {
				ts->rdidx2 = 0;
				ts->wridx2 = 0;
			}
		}
 skip2:

		if (/*ts->size1 < BUFSIZE &&*/ FD_ISSET(ts->sockfd_read, &rdfdset)) {
			/* Read from socket to buffer 1 */
			count = MIN(BUFSIZE - ts->rdidx1, BUFSIZE - ts->size1);
			count = safe_read(ts->sockfd_read, TS_BUF1(ts) + ts->rdidx1, count);
			if (count <= 0) {
				if (count < 0 && errno == EAGAIN)
					goto skip3;
				goto kill_session;
			}
			/* Ignore trailing NUL if it is there */
			if (!TS_BUF1(ts)[ts->rdidx1 + count - 1]) {
				--count;
			}
			ts->size1 += count;
			ts->rdidx1 += count;
			if (ts->rdidx1 >= BUFSIZE) /* actually == BUFSIZE */
				ts->rdidx1 = 0;
		}
 skip3:
		if (/*ts->size2 < BUFSIZE &&*/ FD_ISSET(ts->ptyfd, &rdfdset)) {
			/* Read from pty to buffer 2 */
			count = MIN(BUFSIZE - ts->rdidx2, BUFSIZE - ts->size2);
			count = safe_read(ts->ptyfd, TS_BUF2(ts) + ts->rdidx2, count);
			if (count <= 0) {
				if (count < 0 && errno == EAGAIN)
					goto skip4;
				goto kill_session;
			}
			ts->size2 += count;
			ts->rdidx2 += count;
			if (ts->rdidx2 >= BUFSIZE) /* actually == BUFSIZE */
				ts->rdidx2 = 0;
		}
 skip4:
		ts = next;
		continue;
 kill_session:
		if (ts->shell_pid > 0)
			update_utmp_DEAD_PROCESS(ts->shell_pid);
		free_session(ts);
		ts = next;
	}

	goto again;
}
