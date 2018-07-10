/* vi: set sw=4 ts=4: */
/*
 * telnet implementation for busybox
 *
 * Author: Tomi Ollila <too@iki.fi>
 * Copyright (C) 1994-2000 by Tomi Ollila
 *
 * Created: Thu Apr  7 13:29:41 1994 too
 * Last modified: Fri Jun  9 14:34:24 2000 too
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * HISTORY
 * Revision 3.1  1994/04/17  11:31:54  too
 * initial revision
 * Modified 2000/06/13 for inclusion into BusyBox by Erik Andersen <andersen@codepoet.org>
 * Modified 2001/05/07 to add ability to pass TTYPE to remote host by Jim McQuillan
 * <jam@ltsp.org>
 * Modified 2004/02/11 to add ability to pass the USER variable to remote host
 * by Fernando Silveira <swrh@gmx.net>
 */
//config:config TELNET
//config:	bool "telnet (8.7 kb)"
//config:	default y
//config:	help
//config:	Telnet is an interface to the TELNET protocol, but is also commonly
//config:	used to test other simple protocols.
//config:
//config:config FEATURE_TELNET_TTYPE
//config:	bool "Pass TERM type to remote host"
//config:	default y
//config:	depends on TELNET
//config:	help
//config:	Setting this option will forward the TERM environment variable to the
//config:	remote host you are connecting to. This is useful to make sure that
//config:	things like ANSI colors and other control sequences behave.
//config:
//config:config FEATURE_TELNET_AUTOLOGIN
//config:	bool "Pass USER type to remote host"
//config:	default y
//config:	depends on TELNET
//config:	help
//config:	Setting this option will forward the USER environment variable to the
//config:	remote host you are connecting to. This is useful when you need to
//config:	log into a machine without telling the username (autologin). This
//config:	option enables '-a' and '-l USER' options.
//config:
//config:config FEATURE_TELNET_WIDTH
//config:	bool "Enable window size autodetection"
//config:	default y
//config:	depends on TELNET

//applet:IF_TELNET(APPLET(telnet, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_TELNET) += telnet.o

//usage:#if ENABLE_FEATURE_TELNET_AUTOLOGIN
//usage:#define telnet_trivial_usage
//usage:       "[-a] [-l USER] HOST [PORT]"
//usage:#define telnet_full_usage "\n\n"
//usage:       "Connect to telnet server\n"
//usage:     "\n	-a	Automatic login with $USER variable"
//usage:     "\n	-l USER	Automatic login as USER"
//usage:
//usage:#else
//usage:#define telnet_trivial_usage
//usage:       "HOST [PORT]"
//usage:#define telnet_full_usage "\n\n"
//usage:       "Connect to telnet server"
//usage:#endif

#include <arpa/telnet.h>
#include <netinet/in.h>
#include "libbb.h"
#include "common_bufsiz.h"

#ifdef __BIONIC__
/* should be in arpa/telnet.h */
# define IAC         255  /* interpret as command: */
# define DONT        254  /* you are not to use option */
# define DO          253  /* please, you use option */
# define WONT        252  /* I won't use option */
# define WILL        251  /* I will use option */
# define SB          250  /* interpret as subnegotiation */
# define SE          240  /* end sub negotiation */
# define TELOPT_ECHO   1  /* echo */
# define TELOPT_SGA    3  /* suppress go ahead */
# define TELOPT_TTYPE 24  /* terminal type */
# define TELOPT_NAWS  31  /* window size */
#endif

enum {
	DATABUFSIZE = 128,
	IACBUFSIZE  = 128,

	CHM_TRY = 0,
	CHM_ON = 1,
	CHM_OFF = 2,

	UF_ECHO = 0x01,
	UF_SGA = 0x02,

	TS_NORMAL = 0,
	TS_COPY = 1,
	TS_IAC = 2,
	TS_OPT = 3,
	TS_SUB1 = 4,
	TS_SUB2 = 5,
	TS_CR = 6,
};

typedef unsigned char byte;

enum { netfd = 3 };

struct globals {
	int	iaclen; /* could even use byte, but it's a loss on x86 */
	byte	telstate; /* telnet negotiation state from network input */
	byte	telwish;  /* DO, DONT, WILL, WONT */
	byte    charmode;
	byte    telflags;
	byte	do_termios;
#if ENABLE_FEATURE_TELNET_TTYPE
	char	*ttype;
#endif
#if ENABLE_FEATURE_TELNET_AUTOLOGIN
	const char *autologin;
#endif
#if ENABLE_FEATURE_TELNET_WIDTH
	unsigned win_width, win_height;
#endif
	/* same buffer used both for network and console read/write */
	char    buf[DATABUFSIZE];
	/* buffer to handle telnet negotiations */
	char    iacbuf[IACBUFSIZE];
	struct termios termios_def;
	struct termios termios_raw;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { \
	setup_common_bufsiz(); \
	BUILD_BUG_ON(sizeof(G) > COMMON_BUFSIZE); \
} while (0)


static void rawmode(void);
static void cookmode(void);
static void do_linemode(void);
static void will_charmode(void);
static void telopt(byte c);
static void subneg(byte c);

static void iac_flush(void)
{
	full_write(netfd, G.iacbuf, G.iaclen);
	G.iaclen = 0;
}

static void doexit(int ev) NORETURN;
static void doexit(int ev)
{
	cookmode();
	exit(ev);
}

static void con_escape(void)
{
	char b;

	if (bb_got_signal) /* came from line mode... go raw */
		rawmode();

	full_write1_str("\r\nConsole escape. Commands are:\r\n\n"
			" l	go to line mode\r\n"
			" c	go to character mode\r\n"
			" z	suspend telnet\r\n"
			" e	exit telnet\r\n");

	if (read(STDIN_FILENO, &b, 1) <= 0)
		doexit(EXIT_FAILURE);

	switch (b) {
	case 'l':
		if (!bb_got_signal) {
			do_linemode();
			goto ret;
		}
		break;
	case 'c':
		if (bb_got_signal) {
			will_charmode();
			goto ret;
		}
		break;
	case 'z':
		cookmode();
		kill(0, SIGTSTP);
		rawmode();
		break;
	case 'e':
		doexit(EXIT_SUCCESS);
	}

	full_write1_str("continuing...\r\n");

	if (bb_got_signal)
		cookmode();
 ret:
	bb_got_signal = 0;
}

static void handle_net_output(int len)
{
	byte outbuf[2 * DATABUFSIZE];
	byte *dst = outbuf;
	byte *src = (byte*)G.buf;
	byte *end = src + len;

	while (src < end) {
		byte c = *src++;
		if (c == 0x1d) {
			con_escape();
			return;
		}
		*dst = c;
		if (c == IAC)
			*++dst = c; /* IAC -> IAC IAC */
		else
		if (c == '\r' || c == '\n') {
			/* Enter key sends '\r' in raw mode and '\n' in cooked one.
			 *
			 * See RFC 1123 3.3.1 Telnet End-of-Line Convention.
			 * Using CR LF instead of other allowed possibilities
			 * like CR NUL - easier to talk to HTTP/SMTP servers.
			 */
			*dst = '\r'; /* Enter -> CR LF */
			*++dst = '\n';
		}
		dst++;
	}
	if (dst - outbuf != 0)
		full_write(netfd, outbuf, dst - outbuf);
}

static void handle_net_input(int len)
{
	int i;
	int cstart = 0;

	for (i = 0; i < len; i++) {
		byte c = G.buf[i];

		if (G.telstate == TS_NORMAL) { /* most typical state */
			if (c == IAC) {
				cstart = i;
				G.telstate = TS_IAC;
			}
			else if (c == '\r') {
				cstart = i + 1;
				G.telstate = TS_CR;
			}
			/* No IACs were seen so far, no need to copy
			 * bytes within G.buf: */
			continue;
		}

		switch (G.telstate) {
		case TS_CR:
			/* Prev char was CR. If cur one is NUL, ignore it.
			 * See RFC 1123 section 3.3.1 for discussion of telnet EOL handling.
			 */
			G.telstate = TS_COPY;
			if (c == '\0')
				break;
			/* else: fall through - need to handle CR IAC ... properly */

		case TS_COPY: /* Prev char was ordinary */
			/* Similar to NORMAL, but in TS_COPY we need to copy bytes */
			if (c == IAC)
				G.telstate = TS_IAC;
			else
				G.buf[cstart++] = c;
			if (c == '\r')
				G.telstate = TS_CR;
			break;

		case TS_IAC: /* Prev char was IAC */
			if (c == IAC) { /* IAC IAC -> one IAC */
				G.buf[cstart++] = c;
				G.telstate = TS_COPY;
				break;
			}
			/* else */
			switch (c) {
			case SB:
				G.telstate = TS_SUB1;
				break;
			case DO:
			case DONT:
			case WILL:
			case WONT:
				G.telwish = c;
				G.telstate = TS_OPT;
				break;
			/* DATA MARK must be added later */
			default:
				G.telstate = TS_COPY;
			}
			break;

		case TS_OPT: /* Prev chars were IAC WILL/WONT/DO/DONT */
			telopt(c);
			G.telstate = TS_COPY;
			break;

		case TS_SUB1: /* Subnegotiation */
		case TS_SUB2: /* Subnegotiation */
			subneg(c); /* can change G.telstate */
			break;
		}
	}

	if (G.telstate != TS_NORMAL) {
		/* We had some IACs, or CR */
		if (G.iaclen)
			iac_flush();
		if (G.telstate == TS_COPY) /* we aren't in the middle of IAC */
			G.telstate = TS_NORMAL;
		len = cstart;
	}

	if (len)
		full_write(STDOUT_FILENO, G.buf, len);
}

static void put_iac(int c)
{
	G.iacbuf[G.iaclen++] = c;
}

static void put_iac2_merged(unsigned wwdd_and_c)
{
	if (G.iaclen + 3 > IACBUFSIZE)
		iac_flush();

	put_iac(IAC);
	put_iac(wwdd_and_c >> 8);
	put_iac(wwdd_and_c & 0xff);
}
#define put_iac2(wwdd,c) put_iac2_merged(((wwdd)<<8) + (c))

#if ENABLE_FEATURE_TELNET_TTYPE
static void put_iac_subopt(byte c, char *str)
{
	int len = strlen(str) + 6;   // ( 2 + 1 + 1 + strlen + 2 )

	if (G.iaclen + len > IACBUFSIZE)
		iac_flush();

	put_iac(IAC);
	put_iac(SB);
	put_iac(c);
	put_iac(0);

	while (*str)
		put_iac(*str++);

	put_iac(IAC);
	put_iac(SE);
}
#endif

#if ENABLE_FEATURE_TELNET_AUTOLOGIN
static void put_iac_subopt_autologin(void)
{
	int len = strlen(G.autologin) + 6;	// (2 + 1 + 1 + strlen + 2)
	const char *p = "USER";

	if (G.iaclen + len > IACBUFSIZE)
		iac_flush();

	put_iac(IAC);
	put_iac(SB);
	put_iac(TELOPT_NEW_ENVIRON);
	put_iac(TELQUAL_IS);
	put_iac(NEW_ENV_VAR);

	while (*p)
		put_iac(*p++);

	put_iac(NEW_ENV_VALUE);

	p = G.autologin;
	while (*p)
		put_iac(*p++);

	put_iac(IAC);
	put_iac(SE);
}
#endif

#if ENABLE_FEATURE_TELNET_WIDTH
static void put_iac_naws(byte c, int x, int y)
{
	if (G.iaclen + 9 > IACBUFSIZE)
		iac_flush();

	put_iac(IAC);
	put_iac(SB);
	put_iac(c);

	/* "... & 0xff" implicitly done below */
	put_iac(x >> 8);
	put_iac(x);
	put_iac(y >> 8);
	put_iac(y);

	put_iac(IAC);
	put_iac(SE);
}
#endif

static void setConMode(void)
{
	if (G.telflags & UF_ECHO) {
		if (G.charmode == CHM_TRY) {
			G.charmode = CHM_ON;
			printf("\r\nEntering %s mode"
				"\r\nEscape character is '^%c'.\r\n", "character", ']');
			rawmode();
		}
	} else {
		if (G.charmode != CHM_OFF) {
			G.charmode = CHM_OFF;
			printf("\r\nEntering %s mode"
				"\r\nEscape character is '^%c'.\r\n", "line", 'C');
			cookmode();
		}
	}
}

static void will_charmode(void)
{
	G.charmode = CHM_TRY;
	G.telflags |= (UF_ECHO | UF_SGA);
	setConMode();

	put_iac2(DO, TELOPT_ECHO);
	put_iac2(DO, TELOPT_SGA);
	iac_flush();
}

static void do_linemode(void)
{
	G.charmode = CHM_TRY;
	G.telflags &= ~(UF_ECHO | UF_SGA);
	setConMode();

	put_iac2(DONT, TELOPT_ECHO);
	put_iac2(DONT, TELOPT_SGA);
	iac_flush();
}

static void to_notsup(char c)
{
	if (G.telwish == WILL)
		put_iac2(DONT, c);
	else if (G.telwish == DO)
		put_iac2(WONT, c);
}

static void to_echo(void)
{
	/* if server requests ECHO, don't agree */
	if (G.telwish == DO) {
		put_iac2(WONT, TELOPT_ECHO);
		return;
	}
	if (G.telwish == DONT)
		return;

	if (G.telflags & UF_ECHO) {
		if (G.telwish == WILL)
			return;
	} else if (G.telwish == WONT)
		return;

	if (G.charmode != CHM_OFF)
		G.telflags ^= UF_ECHO;

	if (G.telflags & UF_ECHO)
		put_iac2(DO, TELOPT_ECHO);
	else
		put_iac2(DONT, TELOPT_ECHO);

	setConMode();
	full_write1_str("\r\n");  /* sudden modec */
}

static void to_sga(void)
{
	/* daemon always sends will/wont, client do/dont */

	if (G.telflags & UF_SGA) {
		if (G.telwish == WILL)
			return;
	} else if (G.telwish == WONT)
		return;

	G.telflags ^= UF_SGA; /* toggle */
	if (G.telflags & UF_SGA)
		put_iac2(DO, TELOPT_SGA);
	else
		put_iac2(DONT, TELOPT_SGA);
}

#if ENABLE_FEATURE_TELNET_TTYPE
static void to_ttype(void)
{
	/* Tell server we will (or won't) do TTYPE */
	if (G.ttype)
		put_iac2(WILL, TELOPT_TTYPE);
	else
		put_iac2(WONT, TELOPT_TTYPE);
}
#endif

#if ENABLE_FEATURE_TELNET_AUTOLOGIN
static void to_new_environ(void)
{
	/* Tell server we will (or will not) do AUTOLOGIN */
	if (G.autologin)
		put_iac2(WILL, TELOPT_NEW_ENVIRON);
	else
		put_iac2(WONT, TELOPT_NEW_ENVIRON);
}
#endif

#if ENABLE_FEATURE_TELNET_WIDTH
static void to_naws(void)
{
	/* Tell server we will do NAWS */
	put_iac2(WILL, TELOPT_NAWS);
}
#endif

static void telopt(byte c)
{
	switch (c) {
	case TELOPT_ECHO:
		to_echo(); break;
	case TELOPT_SGA:
		to_sga(); break;
#if ENABLE_FEATURE_TELNET_TTYPE
	case TELOPT_TTYPE:
		to_ttype(); break;
#endif
#if ENABLE_FEATURE_TELNET_AUTOLOGIN
	case TELOPT_NEW_ENVIRON:
		to_new_environ(); break;
#endif
#if ENABLE_FEATURE_TELNET_WIDTH
	case TELOPT_NAWS:
		to_naws();
		put_iac_naws(c, G.win_width, G.win_height);
		break;
#endif
	default:
		to_notsup(c);
		break;
	}
}

/* subnegotiation -- ignore all (except TTYPE,NAWS) */
static void subneg(byte c)
{
	switch (G.telstate) {
	case TS_SUB1:
		if (c == IAC)
			G.telstate = TS_SUB2;
#if ENABLE_FEATURE_TELNET_TTYPE
		else
		if (c == TELOPT_TTYPE && G.ttype)
			put_iac_subopt(TELOPT_TTYPE, G.ttype);
#endif
#if ENABLE_FEATURE_TELNET_AUTOLOGIN
		else
		if (c == TELOPT_NEW_ENVIRON && G.autologin)
			put_iac_subopt_autologin();
#endif
		break;
	case TS_SUB2:
		if (c == SE) {
			G.telstate = TS_COPY;
			return;
		}
		G.telstate = TS_SUB1;
		break;
	}
}

static void rawmode(void)
{
	if (G.do_termios)
		tcsetattr(0, TCSADRAIN, &G.termios_raw);
}

static void cookmode(void)
{
	if (G.do_termios)
		tcsetattr(0, TCSADRAIN, &G.termios_def);
}

int telnet_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int telnet_main(int argc UNUSED_PARAM, char **argv)
{
	char *host;
	int port;
	int len;
	struct pollfd ufds[2];

	INIT_G();

#if ENABLE_FEATURE_TELNET_TTYPE
	G.ttype = getenv("TERM");
#endif

	if (tcgetattr(0, &G.termios_def) >= 0) {
		G.do_termios = 1;
		G.termios_raw = G.termios_def;
		cfmakeraw(&G.termios_raw);
	}

#if ENABLE_FEATURE_TELNET_AUTOLOGIN
	if (1 == getopt32(argv, "al:", &G.autologin)) {
		/* Only -a without -l USER picks $USER from envvar */
		G.autologin = getenv("USER");
	}
	argv += optind;
#else
	argv++;
#endif
	if (!*argv)
		bb_show_usage();
	host = *argv++;
	port = *argv ? bb_lookup_port(*argv++, "tcp", 23)
		: bb_lookup_std_port("telnet", "tcp", 23);
	if (*argv) /* extra params?? */
		bb_show_usage();

	xmove_fd(create_and_connect_stream_or_die(host, port), netfd);

	setsockopt_keepalive(netfd);

#if ENABLE_FEATURE_TELNET_WIDTH
	get_terminal_width_height(0, &G.win_width, &G.win_height);
//TODO: support dynamic resize?
#endif

	signal(SIGINT, record_signo);

	ufds[0].fd = STDIN_FILENO;
	ufds[0].events = POLLIN;
	ufds[1].fd = netfd;
	ufds[1].events = POLLIN;

	while (1) {
		if (poll(ufds, 2, -1) < 0) {
			/* error, ignore and/or log something, bay go to loop */
			if (bb_got_signal)
				con_escape();
			else
				sleep(1);
			continue;
		}

// FIXME: reads can block. Need full bidirectional buffering.

		if (ufds[0].revents) {
			len = safe_read(STDIN_FILENO, G.buf, DATABUFSIZE);
			if (len <= 0)
				doexit(EXIT_SUCCESS);
			handle_net_output(len);
		}

		if (ufds[1].revents) {
			len = safe_read(netfd, G.buf, DATABUFSIZE);
			if (len <= 0) {
				full_write1_str("Connection closed by foreign host\r\n");
				doexit(EXIT_FAILURE);
			}
			handle_net_input(len);
		}
	} /* while (1) */
}
