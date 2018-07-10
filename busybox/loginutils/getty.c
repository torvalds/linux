/* vi: set sw=4 ts=4: */
/*
 * Based on agetty - another getty program for Linux. By W. Z. Venema 1989
 * Ported to Linux by Peter Orbaek <poe@daimi.aau.dk>
 * This program is freely distributable.
 *
 * option added by Eric Rasmussen <ear@usfirst.org> - 12/28/95
 *
 * 1999-02-22 Arkadiusz Mickiewicz <misiek@misiek.eu.org>
 * - Added Native Language Support
 *
 * 1999-05-05 Thorsten Kranzkowski <dl8bcu@gmx.net>
 * - Enabled hardware flow control before displaying /etc/issue
 *
 * 2011-01 Venys Vlasenko
 * - Removed parity detection code. It can't work reliably:
 * if all chars received have bit 7 cleared and odd (or even) parity,
 * it is impossible to determine whether other side is 8-bit,no-parity
 * or 7-bit,odd(even)-parity. It also interferes with non-ASCII usernames.
 * - From now on, we assume that parity is correctly set.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config GETTY
//config:	bool "getty (10 kb)"
//config:	default y
//config:	select FEATURE_SYSLOG
//config:	help
//config:	getty lets you log in on a tty. It is normally invoked by init.
//config:
//config:	Note that you can save a few bytes by disabling it and
//config:	using login applet directly.
//config:	If you need to reset tty attributes before calling login,
//config:	this script approximates getty:
//config:
//config:	exec </dev/$1 >/dev/$1 2>&1 || exit 1
//config:	reset
//config:	stty sane; stty ispeed 38400; stty ospeed 38400
//config:	printf "%s login: " "`hostname`"
//config:	read -r login
//config:	exec /bin/login "$login"

//applet:IF_GETTY(APPLET(getty, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_GETTY) += getty.o

#include "libbb.h"
#include <syslog.h>
#ifndef IUCLC
# define IUCLC 0
#endif

#ifndef LOGIN_PROCESS
# undef ENABLE_FEATURE_UTMP
# undef ENABLE_FEATURE_WTMP
# define ENABLE_FEATURE_UTMP 0
# define ENABLE_FEATURE_WTMP 0
#endif


/* The following is used for understandable diagnostics */
#ifdef DEBUGGING
static FILE *dbf;
# define DEBUGTERM "/dev/ttyp0"
# define debug(...) do { fprintf(dbf, __VA_ARGS__); fflush(dbf); } while (0)
#else
# define debug(...) ((void)0)
#endif


/*
 * Things you may want to modify.
 *
 * You may disagree with the default line-editing etc. characters defined
 * below. Note, however, that DEL cannot be used for interrupt generation
 * and for line editing at the same time.
 */
#undef  _PATH_LOGIN
#define _PATH_LOGIN "/bin/login"

/* Displayed before the login prompt.
 * If ISSUE is not defined, getty will never display the contents of the
 * /etc/issue file. You will not want to spit out large "issue" files at the
 * wrong baud rate.
 */
#define ISSUE "/etc/issue"

/* Macro to build Ctrl-LETTER. Assumes ASCII dialect */
#define CTL(x)          ((x) ^ 0100)

/*
 * When multiple baud rates are specified on the command line,
 * the first one we will try is the first one specified.
 */
#define MAX_SPEED       10              /* max. nr. of baud rates */

struct globals {
	unsigned timeout;
	const char *login;              /* login program */
	const char *fakehost;
	const char *tty_name;
	char *initstring;               /* modem init string */
	const char *issue;              /* alternative issue file */
	int numspeed;                   /* number of baud rates to try */
	int speeds[MAX_SPEED];          /* baud rates to be tried */
	unsigned char eol;              /* end-of-line char seen (CR or NL) */
	struct termios tty_attrs;
	char line_buf[128];
};

#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

//usage:#define getty_trivial_usage
//usage:       "[OPTIONS] BAUD_RATE[,BAUD_RATE]... TTY [TERMTYPE]"
//usage:#define getty_full_usage "\n\n"
//usage:       "Open TTY, prompt for login name, then invoke /bin/login\n"
//usage:     "\n	-h		Enable hardware RTS/CTS flow control"
//usage:     "\n	-L		Set CLOCAL (ignore Carrier Detect state)"
//usage:     "\n	-m		Get baud rate from modem's CONNECT status message"
//usage:     "\n	-n		Don't prompt for login name"
//usage:     "\n	-w		Wait for CR or LF before sending /etc/issue"
//usage:     "\n	-i		Don't display /etc/issue"
//usage:     "\n	-f ISSUE_FILE	Display ISSUE_FILE instead of /etc/issue"
//usage:     "\n	-l LOGIN	Invoke LOGIN instead of /bin/login"
//usage:     "\n	-t SEC		Terminate after SEC if no login name is read"
//usage:     "\n	-I INITSTR	Send INITSTR before anything else"
//usage:     "\n	-H HOST		Log HOST into the utmp file as the hostname"
//usage:     "\n"
//usage:     "\nBAUD_RATE of 0 leaves it unchanged"

#define OPT_STR "I:LH:f:hil:mt:+wn"
#define F_INITSTRING    (1 << 0)   /* -I */
#define F_LOCAL         (1 << 1)   /* -L */
#define F_FAKEHOST      (1 << 2)   /* -H */
#define F_CUSTISSUE     (1 << 3)   /* -f */
#define F_RTSCTS        (1 << 4)   /* -h */
#define F_NOISSUE       (1 << 5)   /* -i */
#define F_LOGIN         (1 << 6)   /* -l */
#define F_PARSE         (1 << 7)   /* -m */
#define F_TIMEOUT       (1 << 8)   /* -t */
#define F_WAITCRLF      (1 << 9)   /* -w */
#define F_NOPROMPT      (1 << 10)  /* -n */


/* convert speed string to speed code; return <= 0 on failure */
static int bcode(const char *s)
{
	int value = bb_strtou(s, NULL, 10); /* yes, int is intended! */
	if (value < 0) /* bad terminating char, overflow, etc */
		return value;
	return tty_value_to_baud(value);
}

/* parse alternate baud rates */
static void parse_speeds(char *arg)
{
	char *cp;

	/* NB: at least one iteration is always done */
	debug("entered parse_speeds\n");
	while ((cp = strsep(&arg, ",")) != NULL) {
		G.speeds[G.numspeed] = bcode(cp);
		if (G.speeds[G.numspeed] < 0)
			bb_error_msg_and_die("bad speed: %s", cp);
		/* note: arg "0" turns into speed B0 */
		G.numspeed++;
		if (G.numspeed > MAX_SPEED)
			bb_error_msg_and_die("too many alternate speeds");
	}
	debug("exiting parse_speeds\n");
}

/* parse command-line arguments */
static void parse_args(char **argv)
{
	char *ts;
	int flags;

	flags = getopt32(argv, "^" OPT_STR "\0" "-2"/* at least 2 args*/,
		&G.initstring, &G.fakehost, &G.issue,
		&G.login, &G.timeout
	);
	if (flags & F_INITSTRING) {
		G.initstring = xstrdup(G.initstring);
		/* decode \ddd octal codes into chars */
		strcpy_and_process_escape_sequences(G.initstring, G.initstring);
	}
	argv += optind;
	debug("after getopt\n");

	/* We loosen up a bit and accept both "baudrate tty" and "tty baudrate" */
	G.tty_name = argv[0];
	ts = argv[1];            /* baud rate(s) */
	if (isdigit(argv[0][0])) {
		/* A number first, assume it's a speed (BSD style) */
		G.tty_name = ts; /* tty name is in argv[1] */
		ts = argv[0];    /* baud rate(s) */
	}
	parse_speeds(ts);

	if (argv[2])
		xsetenv("TERM", argv[2]);

	debug("exiting parse_args\n");
}

/* set up tty as standard input, output, error */
static void open_tty(void)
{
	/* Set up new standard input, unless we are given an already opened port */
	if (NOT_LONE_DASH(G.tty_name)) {
		if (G.tty_name[0] != '/')
			G.tty_name = xasprintf("/dev/%s", G.tty_name); /* will leak it */

		/* Open the tty as standard input */
		debug("open(2)\n");
		close(0);
		xopen(G.tty_name, O_RDWR | O_NONBLOCK); /* uses fd 0 */

		/* Set proper protections and ownership */
		fchown(0, 0, 0);        /* 0:0 */
		fchmod(0, 0620);        /* crw--w---- */
	} else {
		char *n;
		/*
		 * Standard input should already be connected to an open port.
		 * Make sure it is open for read/write.
		 */
		if ((fcntl(0, F_GETFL) & (O_RDWR|O_RDONLY|O_WRONLY)) != O_RDWR)
			bb_error_msg_and_die("stdin is not open for read/write");

		/* Try to get real tty name instead of "-" */
		n = xmalloc_ttyname(0);
		if (n)
			G.tty_name = n;
	}
	applet_name = xasprintf("getty: %s", skip_dev_pfx(G.tty_name));
}

static void set_tty_attrs(void)
{
	if (tcsetattr_stdin_TCSANOW(&G.tty_attrs) < 0)
		bb_perror_msg_and_die("tcsetattr");
}

/* We manipulate tty_attrs this way:
 * - first, we read existing tty_attrs
 * - init_tty_attrs modifies some parts and sets it
 * - auto_baud and/or BREAK processing can set different speed and set tty attrs
 * - finalize_tty_attrs again modifies some parts and sets tty attrs before
 *   execing login
 */
static void init_tty_attrs(int speed)
{
	/* Try to drain output buffer, with 5 sec timeout.
	 * Added on request from users of ~600 baud serial interface
	 * with biggish buffer on a 90MHz CPU.
	 * They were losing hundreds of bytes of buffered output
	 * on tcflush.
	 */
	signal_no_SA_RESTART_empty_mask(SIGALRM, record_signo);
	alarm(5);
	tcdrain(STDIN_FILENO);
	alarm(0);

	/* Flush input and output queues, important for modems! */
	tcflush(STDIN_FILENO, TCIOFLUSH);

	/* Set speed if it wasn't specified as "0" on command line */
	if (speed != B0)
		cfsetspeed(&G.tty_attrs, speed);

	/* Initial settings: 8-bit characters, raw mode, blocking i/o.
	 * Special characters are set after we have read the login name; all
	 * reads will be done in raw mode anyway.
	 */
	/* Clear all bits except: */
	G.tty_attrs.c_cflag &= (0
		/* 2 stop bits (1 otherwise)
		 * Enable parity bit (both on input and output)
		 * Odd parity (else even)
		 */
		| CSTOPB | PARENB | PARODD
#ifdef CMSPAR
		| CMSPAR  /* mark or space parity */
#endif
#ifdef CBAUD
		| CBAUD   /* (output) baud rate */
#endif
#ifdef CBAUDEX
		| CBAUDEX /* (output) baud rate */
#endif
#ifdef CIBAUD
		| CIBAUD   /* input baud rate */
#endif
	);
	/* Set: 8 bits; hang up (drop DTR) on last close; enable receive */
	G.tty_attrs.c_cflag |= CS8 | HUPCL | CREAD;
	if (option_mask32 & F_LOCAL) {
		/* ignore Carrier Detect pin:
		 * opens don't block when CD is low,
		 * losing CD doesn't hang up processes whose ctty is this tty
		 */
		G.tty_attrs.c_cflag |= CLOCAL;
	}
#ifdef CRTSCTS
	if (option_mask32 & F_RTSCTS)
		G.tty_attrs.c_cflag |= CRTSCTS; /* flow control using RTS/CTS pins */
#endif
	G.tty_attrs.c_iflag = 0;
	G.tty_attrs.c_lflag = 0;
	/* non-raw output; add CR to each NL */
	G.tty_attrs.c_oflag = OPOST | ONLCR;

	/* reads will block only if < 1 char is available */
	G.tty_attrs.c_cc[VMIN] = 1;
	/* no timeout (reads block forever) */
	G.tty_attrs.c_cc[VTIME] = 0;
#ifdef __linux__
	G.tty_attrs.c_line = 0;
#endif

	set_tty_attrs();

	debug("term_io 2\n");
}

static void finalize_tty_attrs(void)
{
	/* software flow control on output (stop sending if XOFF is recvd);
	 * and on input (send XOFF when buffer is full)
	 */
	G.tty_attrs.c_iflag |= IXON | IXOFF;
	if (G.eol == '\r') {
		G.tty_attrs.c_iflag |= ICRNL; /* map CR on input to NL */
	}
	/* Other bits in c_iflag:
	 * IXANY   Any recvd char enables output (any char is also a XON)
	 * INPCK   Enable parity check
	 * IGNPAR  Ignore parity errors (drop bad bytes)
	 * PARMRK  Mark parity errors with 0xff, 0x00 prefix
	 *         (else bad byte is received as 0x00)
	 * ISTRIP  Strip parity bit
	 * IGNBRK  Ignore break condition
	 * BRKINT  Send SIGINT on break - maybe set this?
	 * INLCR   Map NL to CR
	 * IGNCR   Ignore CR
	 * ICRNL   Map CR to NL
	 * IUCLC   Map uppercase to lowercase
	 * IMAXBEL Echo BEL on input line too long
	 * IUTF8   Appears to affect tty's idea of char widths,
	 *         observed to improve backspacing through Unicode chars
	 */

	/* ICANON  line buffered input (NL or EOL or EOF chars end a line);
	 * ISIG    recognize INT/QUIT/SUSP chars;
	 * ECHO    echo input chars;
	 * ECHOE   echo BS-SP-BS on erase character;
	 * ECHOK   echo kill char specially, not as ^c (ECHOKE controls how exactly);
	 * ECHOKE  erase all input via BS-SP-BS on kill char (else go to next line)
	 * ECHOCTL Echo ctrl chars as ^c (else echo verbatim:
	 *         e.g. up arrow emits "ESC-something" and thus moves cursor up!)
	 */
	G.tty_attrs.c_lflag |= ICANON | ISIG | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
	/* Other bits in c_lflag:
	 * XCASE   Map uppercase to \lowercase [tried, doesn't work]
	 * ECHONL  Echo NL even if ECHO is not set
	 * ECHOPRT On erase, echo erased chars
	 *         [qwe<BS><BS><BS> input looks like "qwe\ewq/" on screen]
	 * NOFLSH  Don't flush input buffer after interrupt or quit chars
	 * IEXTEN  Enable extended functions (??)
	 *         [glibc says it enables c_cc[LNEXT] "enter literal char"
	 *         and c_cc[VDISCARD] "toggle discard buffered output" chars]
	 * FLUSHO  Output being flushed (c_cc[VDISCARD] is in effect)
	 * PENDIN  Retype pending input at next read or input char
	 *         (c_cc[VREPRINT] is being processed)
	 * TOSTOP  Send SIGTTOU for background output
	 *         (why "stty sane" unsets this bit?)
	 */

	G.tty_attrs.c_cc[VINTR] = CTL('C');
	G.tty_attrs.c_cc[VQUIT] = CTL('\\');
	G.tty_attrs.c_cc[VEOF] = CTL('D');
	G.tty_attrs.c_cc[VEOL] = '\n';
#ifdef VSWTC
	G.tty_attrs.c_cc[VSWTC] = 0;
#endif
#ifdef VSWTCH
	G.tty_attrs.c_cc[VSWTCH] = 0;
#endif
	G.tty_attrs.c_cc[VKILL] = CTL('U');
	/* Other control chars:
	 * VEOL2
	 * VERASE, VWERASE - (word) erase. we may set VERASE in get_logname
	 * VREPRINT - reprint current input buffer
	 * VLNEXT, VDISCARD, VSTATUS
	 * VSUSP, VDSUSP - send (delayed) SIGTSTP
	 * VSTART, VSTOP - chars used for IXON/IXOFF
	 */

	set_tty_attrs();

	/* Now the newline character should be properly written */
	full_write(STDOUT_FILENO, "\n", 1);
}

/* extract baud rate from modem status message */
static void auto_baud(void)
{
	int nread;

	/*
	 * This works only if the modem produces its status code AFTER raising
	 * the DCD line, and if the computer is fast enough to set the proper
	 * baud rate before the message has gone by. We expect a message of the
	 * following format:
	 *
	 * <junk><number><junk>
	 *
	 * The number is interpreted as the baud rate of the incoming call. If the
	 * modem does not tell us the baud rate within one second, we will keep
	 * using the current baud rate. It is advisable to enable BREAK
	 * processing (comma-separated list of baud rates) if the processing of
	 * modem status messages is enabled.
	 */

	G.tty_attrs.c_cc[VMIN] = 0; /* don't block reads (min read is 0 chars) */
	set_tty_attrs();

	/*
	 * Wait for a while, then read everything the modem has said so far and
	 * try to extract the speed of the dial-in call.
	 */
	sleep(1);
	nread = safe_read(STDIN_FILENO, G.line_buf, sizeof(G.line_buf) - 1);
	if (nread > 0) {
		int speed;
		char *bp;
		G.line_buf[nread] = '\0';
		for (bp = G.line_buf; bp < G.line_buf + nread; bp++) {
			if (isdigit(*bp)) {
				speed = bcode(bp);
				if (speed > 0)
					cfsetspeed(&G.tty_attrs, speed);
				break;
			}
		}
	}

	/* Restore terminal settings */
	G.tty_attrs.c_cc[VMIN] = 1; /* restore to value set by init_tty_attrs */
	set_tty_attrs();
}

/* get user name, establish parity, speed, erase, kill, eol;
 * return NULL on BREAK, logname on success
 */
static char *get_logname(void)
{
	char *bp;
	char c;

	/* Flush pending input (esp. after parsing or switching the baud rate) */
	usleep(100*1000); /* 0.1 sec */
	tcflush(STDIN_FILENO, TCIFLUSH);

	/* Prompt for and read a login name */
	do {
		/* Write issue file and prompt */
#ifdef ISSUE
		if (!(option_mask32 & F_NOISSUE))
			print_login_issue(G.issue, G.tty_name);
#endif
		print_login_prompt();

		/* Read name, watch for break, erase, kill, end-of-line */
		bp = G.line_buf;
		while (1) {
			/* Do not report trivial EINTR/EIO errors */
			errno = EINTR; /* make read of 0 bytes be silent too */
			if (read(STDIN_FILENO, &c, 1) < 1) {
				finalize_tty_attrs();
				if (errno == EINTR || errno == EIO)
					exit(EXIT_SUCCESS);
				bb_perror_msg_and_die(bb_msg_read_error);
			}

			switch (c) {
			case '\r':
			case '\n':
				*bp = '\0';
				G.eol = c;
				goto got_logname;
			case CTL('H'):
			case 0x7f:
				G.tty_attrs.c_cc[VERASE] = c;
				if (bp > G.line_buf) {
					full_write(STDOUT_FILENO, "\010 \010", 3);
					bp--;
				}
				break;
			case CTL('U'):
				while (bp > G.line_buf) {
					full_write(STDOUT_FILENO, "\010 \010", 3);
					bp--;
				}
				break;
			case CTL('C'):
			case CTL('D'):
				finalize_tty_attrs();
				exit(EXIT_SUCCESS);
			case '\0':
				/* BREAK. If we have speeds to try,
				 * return NULL (will switch speeds and return here) */
				if (G.numspeed > 1)
					return NULL;
				/* fall through and ignore it */
			default:
				if ((unsigned char)c < ' ') {
					/* ignore garbage characters */
				} else if ((int)(bp - G.line_buf) < sizeof(G.line_buf) - 1) {
					/* echo and store the character */
					full_write(STDOUT_FILENO, &c, 1);
					*bp++ = c;
				}
				break;
			}
		} /* end of get char loop */
 got_logname: ;
	} while (G.line_buf[0] == '\0');  /* while logname is empty */

	return G.line_buf;
}

static void alarm_handler(int sig UNUSED_PARAM)
{
	finalize_tty_attrs();
	_exit(EXIT_SUCCESS);
}

static void sleep10(void)
{
	sleep(10);
}

int getty_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int getty_main(int argc UNUSED_PARAM, char **argv)
{
	int n;
	pid_t pid, tsid;
	char *logname;

	INIT_G();
	G.login = _PATH_LOGIN;    /* default login program */
#ifdef ISSUE
	G.issue = ISSUE;          /* default issue file */
#endif
	G.eol = '\r';

	/* Parse command-line arguments */
	parse_args(argv);

	/* Create new session and pgrp, lose controlling tty */
	pid = setsid();  /* this also gives us our pid :) */
	if (pid < 0) {
		int fd;
		/* :(
		 * docs/ctty.htm says:
		 * "This is allowed only when the current process
		 *  is not a process group leader".
		 * Thus, setsid() will fail if we _already_ are
		 * a session leader - which is quite possible for getty!
		 */
		pid = getpid();
		if (getsid(0) != pid) {
			//for debugging:
			//bb_perror_msg_and_die("setsid failed:"
			//	" pid %d ppid %d"
			//	" sid %d pgid %d",
			//	pid, getppid(),
			//	getsid(0), getpgid(0));
			bb_perror_msg_and_die("setsid");
			/*
			 * When we can end up here?
			 * Example: setsid() fails when run alone in interactive shell:
			 *  # getty 115200 /dev/tty2
			 * because shell's child (getty) is put in a new process group.
			 * But doesn't fail if shell is not interactive
			 * (and therefore doesn't create process groups for pipes),
			 * or if getty is not the first process in the process group:
			 *  # true | getty 115200 /dev/tty2
			 */
		}
		/* Looks like we are already a session leader.
		 * In this case (setsid failed) we may still have ctty,
		 * and it may be different from tty we need to control!
		 * If we still have ctty, on Linux ioctl(TIOCSCTTY)
		 * (which we are going to use a bit later) always fails -
		 * even if we try to take ctty which is already ours!
		 * Try to drop old ctty now to prevent that.
		 * Use O_NONBLOCK: old ctty may be a serial line.
		 */
		fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
		if (fd >= 0) {
			/* TIOCNOTTY sends SIGHUP to the foreground
			 * process group - which may include us!
			 * Make sure to not die on it:
			 */
			sighandler_t old = signal(SIGHUP, SIG_IGN);
			ioctl(fd, TIOCNOTTY);
			close(fd);
			signal(SIGHUP, old);
		}
	}

	/* Close stdio, and stray descriptors, just in case */
	n = xopen(bb_dev_null, O_RDWR);
	/* dup2(n, 0); - no, we need to handle "getty - 9600" too */
	xdup2(n, 1);
	xdup2(n, 2);
	while (n > 2)
		close(n--);

	/* Logging. We want special flavor of error_msg_and_die */
	die_func = sleep10;
	msg_eol = "\r\n";
	/* most likely will internally use fd #3 in CLOEXEC mode: */
	openlog(applet_name, LOG_PID, LOG_AUTH);
	logmode = LOGMODE_BOTH;

#ifdef DEBUGGING
	dbf = xfopen_for_write(DEBUGTERM);
	for (n = 1; argv[n]; n++) {
		debug(argv[n]);
		debug("\n");
	}
#endif

	/* Open the tty as standard input, if it is not "-" */
	debug("calling open_tty\n");
	open_tty();
	ndelay_off(STDIN_FILENO);
	debug("duping\n");
	xdup2(STDIN_FILENO, 1);
	xdup2(STDIN_FILENO, 2);

	/* Steal ctty if we don't have it yet */
	tsid = tcgetsid(STDIN_FILENO);
	if (tsid < 0 || pid != tsid) {
		if (ioctl(STDIN_FILENO, TIOCSCTTY, /*force:*/ (long)1) < 0)
			bb_perror_msg_and_die("TIOCSCTTY");
	}

#ifdef __linux__
	/* Make ourself a foreground process group within our session */
	if (tcsetpgrp(STDIN_FILENO, pid) < 0)
		bb_perror_msg_and_die("tcsetpgrp");
#endif

	/*
	 * The following ioctl will fail if stdin is not a tty, but also when
	 * there is noise on the modem control lines. In the latter case, the
	 * common course of action is (1) fix your cables (2) give the modem more
	 * time to properly reset after hanging up. SunOS users can achieve (2)
	 * by patching the SunOS kernel variable "zsadtrlow" to a larger value;
	 * 5 seconds seems to be a good value.
	 */
	if (tcgetattr(STDIN_FILENO, &G.tty_attrs) < 0)
		bb_perror_msg_and_die("tcgetattr");

	/* Update the utmp file. This tty is ours now! */
	update_utmp(pid, LOGIN_PROCESS, G.tty_name, "LOGIN", G.fakehost);

	/* Initialize tty attrs (raw mode, eight-bit, blocking i/o) */
	debug("calling init_tty_attrs\n");
	init_tty_attrs(G.speeds[0]);

	/* Write the modem init string and DON'T flush the buffers */
	if (option_mask32 & F_INITSTRING) {
		debug("writing init string\n");
		full_write1_str(G.initstring);
	}

	/* Optionally detect the baud rate from the modem status message */
	debug("before autobaud\n");
	if (option_mask32 & F_PARSE)
		auto_baud();

	/* Set the optional timer */
	signal(SIGALRM, alarm_handler);
	alarm(G.timeout); /* if 0, alarm is not set */

	/* Optionally wait for CR or LF before writing /etc/issue */
	if (option_mask32 & F_WAITCRLF) {
		char ch;
		debug("waiting for cr-lf\n");
		while (safe_read(STDIN_FILENO, &ch, 1) == 1) {
			debug("read %x\n", (unsigned char)ch);
			if (ch == '\n' || ch == '\r')
				break;
		}
	}

	logname = NULL;
	if (!(option_mask32 & F_NOPROMPT)) {
		/* NB: init_tty_attrs already set line speed
		 * to G.speeds[0] */
		int baud_index = 0;

		while (1) {
			/* Read the login name */
			debug("reading login name\n");
			logname = get_logname();
			if (logname)
				break;
			/* We are here only if G.numspeed > 1 */
			baud_index = (baud_index + 1) % G.numspeed;
			cfsetspeed(&G.tty_attrs, G.speeds[baud_index]);
			set_tty_attrs();
		}
	}

	/* Disable timer */
	alarm(0);

	finalize_tty_attrs();

	/* Let the login program take care of password validation */
	/* We use PATH because we trust that root doesn't set "bad" PATH,
	 * and getty is not suid-root applet */
	/* With -n, logname == NULL, and login will ask for username instead */
	BB_EXECLP(G.login, G.login, "--", logname, (char *)0);
	bb_error_msg_and_die("can't execute '%s'", G.login);
}
