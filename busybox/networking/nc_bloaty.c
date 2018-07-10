/* Based on netcat 1.10 RELEASE 960320 written by hobbit@avian.org.
 * Released into public domain by the author.
 *
 * Copyright (C) 2007 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* Author's comments from nc 1.10:
 * =====================
 * Netcat is entirely my own creation, although plenty of other code was used as
 * examples.  It is freely given away to the Internet community in the hope that
 * it will be useful, with no restrictions except giving credit where it is due.
 * No GPLs, Berkeley copyrights or any of that nonsense.  The author assumes NO
 * responsibility for how anyone uses it.  If netcat makes you rich somehow and
 * you're feeling generous, mail me a check.  If you are affiliated in any way
 * with Microsoft Network, get a life.  Always ski in control.  Comments,
 * questions, and patches to hobbit@avian.org.
 * ...
 * Netcat and the associated package is a product of Avian Research, and is freely
 * available in full source form with no restrictions save an obligation to give
 * credit where due.
 * ...
 * A damn useful little "backend" utility begun 950915 or thereabouts,
 * as *Hobbit*'s first real stab at some sockets programming.  Something that
 * should have and indeed may have existed ten years ago, but never became a
 * standard Unix utility.  IMHO, "nc" could take its place right next to cat,
 * cp, rm, mv, dd, ls, and all those other cryptic and Unix-like things.
 * =====================
 *
 * Much of author's comments are still retained in the code.
 *
 * Functionality removed (rationale):
 * - miltiple-port ranges, randomized port scanning (use nmap)
 * - telnet support (use telnet)
 * - source routing
 * - multiple DNS checks
 * Functionalty which is different from nc 1.10:
 * - PROG in '-e PROG' can have ARGS (and options).
 *   Because of this -e option must be last.
//TODO: remove -e incompatibility?
 * - we don't redirect stderr to the network socket for the -e PROG.
 *   (PROG can do it itself if needed, but sometimes it is NOT wanted!)
 * - numeric addresses are printed in (), not [] (IPv6 looks better),
 *   port numbers are inside (): (1.2.3.4:5678)
 * - network read errors are reported on verbose levels > 1
 *   (nc 1.10 treats them as EOF)
 * - TCP connects from wrong ip/ports (if peer ip:port is specified
 *   on the command line, but accept() says that it came from different addr)
 *   are closed, but we don't exit - we continue to listen/accept.
 * Since bbox 1.22:
 * - nc exits when _both_ stdin and network are closed.
 *   This makes these two commands:
 *    echo "Yes" | nc 127.0.0.1 1234
 *    echo "no" | nc -lp 1234
 *   exchange their data _and exit_ instead of being stuck.
 */

/* done in nc.c: #include "libbb.h" */

//usage:#if ENABLE_NC_110_COMPAT
//usage:
//usage:#define nc_trivial_usage
//usage:       "[OPTIONS] HOST PORT  - connect"
//usage:	IF_NC_SERVER("\n"
//usage:       "nc [OPTIONS] -l -p PORT [HOST] [PORT]  - listen"
//usage:	)
//usage:#define nc_full_usage "\n\n"
//usage:       "	-e PROG	Run PROG after connect (must be last)"
//usage:	IF_NC_SERVER(
//usage:     "\n	-l	Listen mode, for inbound connects"
//usage:     "\n	-lk	With -e, provides persistent server"
/* -ll does the same as -lk, but its our extension, while -k is BSD'd,
 * presumably more widely known. Therefore we advertise it, not -ll.
 * I would like to drop -ll support, but our "small" nc supports it,
 * and Rob uses it.
 */
//usage:	)
//usage:     "\n	-p PORT	Local port"
//usage:     "\n	-s ADDR	Local address"
//usage:     "\n	-w SEC	Timeout for connects and final net reads"
//usage:	IF_NC_EXTRA(
//usage:     "\n	-i SEC	Delay interval for lines sent" /* ", ports scanned" */
//usage:	)
//usage:     "\n	-n	Don't do DNS resolution"
//usage:     "\n	-u	UDP mode"
//usage:     "\n	-v	Verbose"
//usage:	IF_NC_EXTRA(
//usage:     "\n	-o FILE	Hex dump traffic"
//usage:     "\n	-z	Zero-I/O mode (scanning)"
//usage:	)
//usage:#endif

/*   "\n	-r		Randomize local and remote ports" */
/*   "\n	-g gateway	Source-routing hop point[s], up to 8" */
/*   "\n	-G num		Source-routing pointer: 4, 8, 12, ..." */
/*   "\nport numbers can be individual or ranges: lo-hi [inclusive]" */

/* -e PROG can take ARGS too: "nc ... -e ls -l", but we don't document it
 * in help text: nc 1.10 does not allow that. We don't want to entice
 * users to use this incompatibility */

enum {
	SLEAZE_PORT = 31337,               /* for UDP-scan RTT trick, change if ya want */
	BIGSIZ = 8192,                     /* big buffers */

	netfd = 3,
	ofd = 4,
};

struct globals {
	/* global cmd flags: */
	unsigned o_verbose;
	unsigned o_wait;
#if ENABLE_NC_EXTRA
	unsigned o_interval;
#endif

	/*int netfd;*/
	/*int ofd;*/                     /* hexdump output fd */
#if ENABLE_LFS
#define SENT_N_RECV_M "sent %llu, rcvd %llu\n"
	unsigned long long wrote_out;          /* total stdout bytes */
	unsigned long long wrote_net;          /* total net bytes */
#else
#define SENT_N_RECV_M "sent %u, rcvd %u\n"
	unsigned wrote_out;          /* total stdout bytes */
	unsigned wrote_net;          /* total net bytes */
#endif
	char *proggie0saved;
	/* ouraddr is never NULL and goes through three states as we progress:
	 1 - local address before bind (IP/port possibly zero)
	 2 - local address after bind (port is nonzero)
	 3 - local address after connect??/recv/accept (IP and port are nonzero) */
	struct len_and_sockaddr *ouraddr;
	/* themaddr is NULL if no peer hostname[:port] specified on command line */
	struct len_and_sockaddr *themaddr;
	/* remend is set after connect/recv/accept to the actual ip:port of peer */
	struct len_and_sockaddr remend;

	jmp_buf jbuf;                /* timer crud */

	char bigbuf_in[BIGSIZ];      /* data buffers */
	char bigbuf_net[BIGSIZ];
};

#define G (*ptr_to_globals)
#define wrote_out  (G.wrote_out )
#define wrote_net  (G.wrote_net )
#define ouraddr    (G.ouraddr   )
#define themaddr   (G.themaddr  )
#define remend     (G.remend    )
#define jbuf       (G.jbuf      )
#define bigbuf_in  (G.bigbuf_in )
#define bigbuf_net (G.bigbuf_net)
#define o_verbose  (G.o_verbose )
#define o_wait     (G.o_wait    )
#if ENABLE_NC_EXTRA
#define o_interval (G.o_interval)
#else
#define o_interval 0
#endif
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


/* Must match getopt32 call! */
enum {
	OPT_n = (1 << 0),
	OPT_p = (1 << 1),
	OPT_s = (1 << 2),
	OPT_u = (1 << 3),
	OPT_v = (1 << 4),
	OPT_w = (1 << 5),
	OPT_l = (1 << 6) * ENABLE_NC_SERVER,
	OPT_k = (1 << 7) * ENABLE_NC_SERVER,
	OPT_i = (1 << (6+2*ENABLE_NC_SERVER)) * ENABLE_NC_EXTRA,
	OPT_o = (1 << (7+2*ENABLE_NC_SERVER)) * ENABLE_NC_EXTRA,
	OPT_z = (1 << (8+2*ENABLE_NC_SERVER)) * ENABLE_NC_EXTRA,
};

#define o_nflag   (option_mask32 & OPT_n)
#define o_udpmode (option_mask32 & OPT_u)
#if ENABLE_NC_EXTRA
#define o_ofile   (option_mask32 & OPT_o)
#define o_zero    (option_mask32 & OPT_z)
#else
#define o_ofile   0
#define o_zero    0
#endif

/* Debug: squirt whatever message and sleep a bit so we can see it go by. */
/* Beware: writes to stdOUT... */
#if 0
#define Debug(...) do { printf(__VA_ARGS__); printf("\n"); fflush_all(); sleep(1); } while (0)
#else
#define Debug(...) do { } while (0)
#endif

#define holler_error(...)  do { if (o_verbose) bb_error_msg(__VA_ARGS__); } while (0)
#define holler_perror(...) do { if (o_verbose) bb_perror_msg(__VA_ARGS__); } while (0)

/* catch: no-brainer interrupt handler */
static void catch(int sig)
{
	if (o_verbose > 1)                /* normally we don't care */
		fprintf(stderr, SENT_N_RECV_M, wrote_net, wrote_out);
	fprintf(stderr, "punt!\n");
	kill_myself_with_sig(sig);
}

/* unarm  */
static void unarm(void)
{
	signal(SIGALRM, SIG_IGN);
	alarm(0);
}

/* timeout and other signal handling cruft */
static void tmtravel(int sig UNUSED_PARAM)
{
	unarm();
	longjmp(jbuf, 1);
}

/* arm: set the timer.  */
static void arm(unsigned secs)
{
	signal(SIGALRM, tmtravel);
	alarm(secs);
}

/* findline:
 find the next newline in a buffer; return inclusive size of that "line",
 or the entire buffer size, so the caller knows how much to then write().
 Not distinguishing \n vs \r\n for the nonce; it just works as is... */
static unsigned findline(char *buf, unsigned siz)
{
	char * p;
	int x;
	if (!buf)                        /* various sanity checks... */
		return 0;
	if (siz > BIGSIZ)
		return 0;
	x = siz;
	for (p = buf; x > 0; x--) {
		if (*p == '\n') {
			x = (int) (p - buf);
			x++;                        /* 'sokay if it points just past the end! */
Debug("findline returning %d", x);
			return x;
		}
		p++;
	} /* for */
Debug("findline returning whole thing: %d", siz);
	return siz;
} /* findline */

/* doexec:
 fiddle all the file descriptors around, and hand off to another prog.  Sort
 of like a one-off "poor man's inetd".  This is the only section of code
 that would be security-critical, which is why it's ifdefed out by default.
 Use at your own hairy risk; if you leave shells lying around behind open
 listening ports you deserve to lose!! */
static int doexec(char **proggie) NORETURN;
static int doexec(char **proggie)
{
	if (G.proggie0saved)
		proggie[0] = G.proggie0saved;
	xmove_fd(netfd, 0);
	dup2(0, 1);
	/* dup2(0, 2); - do we *really* want this? NO!
	 * exec'ed prog can do it yourself, if needed */
	BB_EXECVP_or_die(proggie);
}

/* connect_w_timeout:
 return an fd for one of
 an open outbound TCP connection, a UDP stub-socket thingie, or
 an unconnected TCP or UDP socket to listen on.
 Examines various global o_blah flags to figure out what to do.
 lad can be NULL, then socket is not bound to any local ip[:port] */
static int connect_w_timeout(int fd)
{
	int rr;

	/* wrap connect inside a timer, and hit it */
	arm(o_wait);
	if (setjmp(jbuf) == 0) {
		rr = connect(fd, &themaddr->u.sa, themaddr->len);
		unarm();
	} else { /* setjmp: connect failed... */
		rr = -1;
		errno = ETIMEDOUT; /* fake it */
	}
	return rr;
}

/* dolisten:
 listens for
 incoming and returns an open connection *from* someplace.  If we were
 given host/port args, any connections from elsewhere are rejected.  This
 in conjunction with local-address binding should limit things nicely... */
static void dolisten(int is_persistent, char **proggie)
{
	int rr;

	if (!o_udpmode)
		xlisten(netfd, 1); /* TCP: gotta listen() before we can get */

	/* Various things that follow temporarily trash bigbuf_net, which might contain
	 a copy of any recvfrom()ed packet, but we'll read() another copy later. */

	/* I can't believe I have to do all this to get my own goddamn bound address
	 and port number.  It should just get filled in during bind() or something.
	 All this is only useful if we didn't say -p for listening, since if we
	 said -p we *know* what port we're listening on.  At any rate we won't bother
	 with it all unless we wanted to see it, although listening quietly on a
	 random unknown port is probably not very useful without "netstat". */
	if (o_verbose) {
		char *addr;
		getsockname(netfd, &ouraddr->u.sa, &ouraddr->len);
		//if (rr < 0)
		//	bb_perror_msg_and_die("getsockname after bind");
		addr = xmalloc_sockaddr2dotted(&ouraddr->u.sa);
		fprintf(stderr, "listening on %s ...\n", addr);
		free(addr);
	}

	if (o_udpmode) {
		/* UDP is a speeeeecial case -- we have to do I/O *and* get the calling
		 party's particulars all at once, listen() and accept() don't apply.
		 At least in the BSD universe, however, recvfrom/PEEK is enough to tell
		 us something came in, and we can set things up so straight read/write
		 actually does work after all.  Yow.  YMMV on strange platforms!  */

		/* I'm not completely clear on how this works -- BSD seems to make UDP
		 just magically work in a connect()ed context, but we'll undoubtedly run
		 into systems this deal doesn't work on.  For now, we apparently have to
		 issue a connect() on our just-tickled socket so we can write() back.
		 Again, why the fuck doesn't it just get filled in and taken care of?!
		 This hack is anything but optimal.  Basically, if you want your listener
		 to also be able to send data back, you need this connect() line, which
		 also has the side effect that now anything from a different source or even a
		 different port on the other end won't show up and will cause ICMP errors.
		 I guess that's what they meant by "connect".
		 Let's try to remember what the "U" is *really* for, eh? */

		/* If peer address is specified, connect to it */
		remend.len = LSA_SIZEOF_SA;
		if (themaddr) {
			remend = *themaddr;
			xconnect(netfd, &themaddr->u.sa, themaddr->len);
		}
		/* peek first packet and remember peer addr */
		arm(o_wait);                /* might as well timeout this, too */
		if (setjmp(jbuf) == 0) {       /* do timeout for initial connect */
			/* (*ouraddr) is prefilled with "default" address */
			/* and here we block... */
			rr = recv_from_to(netfd, NULL, 0, MSG_PEEK, /*was bigbuf_net, BIGSIZ*/
				&remend.u.sa, &ouraddr->u.sa, ouraddr->len);
			if (rr < 0)
				bb_perror_msg_and_die("recvfrom");
			unarm();
		} else
			bb_error_msg_and_die("timeout");
/* Now we learned *to which IP* peer has connected, and we want to anchor
our socket on it, so that our outbound packets will have correct local IP.
Unfortunately, bind() on already bound socket will fail now (EINVAL):
	xbind(netfd, &ouraddr->u.sa, ouraddr->len);
Need to read the packet, save data, close this socket and
create new one, and bind() it. TODO */
		if (!themaddr)
			xconnect(netfd, &remend.u.sa, ouraddr->len);
	} else {
		/* TCP */
 another:
		arm(o_wait); /* wrap this in a timer, too; 0 = forever */
		if (setjmp(jbuf) == 0) {
 again:
			remend.len = LSA_SIZEOF_SA;
			rr = accept(netfd, &remend.u.sa, &remend.len);
			if (rr < 0)
				bb_perror_msg_and_die("accept");
			if (themaddr) {
				int sv_port, port, r;

				sv_port = get_nport(&remend.u.sa); /* save */
				port = get_nport(&themaddr->u.sa);
				if (port == 0) {
					/* "nc -nl -p LPORT RHOST" (w/o RPORT!):
					 * we should accept any remote port */
					set_nport(&remend.u.sa, 0); /* blot out remote port# */
				}
				r = memcmp(&remend.u.sa, &themaddr->u.sa, remend.len);
				set_nport(&remend.u.sa, sv_port); /* restore */
				if (r != 0) {
					/* nc 1.10 bails out instead, and its error message
					 * is not suppressed by o_verbose */
					if (o_verbose) {
						char *remaddr = xmalloc_sockaddr2dotted(&remend.u.sa);
						bb_error_msg("connect from wrong ip/port %s ignored", remaddr);
						free(remaddr);
					}
					close(rr);
					goto again;
				}
			}
			unarm();
		} else
			bb_error_msg_and_die("timeout");

		if (is_persistent && proggie) {
			/* -l -k -e PROG */
			signal(SIGCHLD, SIG_IGN); /* no zombies please */
			if (xvfork() != 0) {
				/* parent: go back and accept more connections */
				close(rr);
				goto another;
			}
			/* child */
			signal(SIGCHLD, SIG_DFL);
		}

		xmove_fd(rr, netfd); /* dump the old socket, here's our new one */
		/* find out what address the connection was *to* on our end, in case we're
		 doing a listen-on-any on a multihomed machine.  This allows one to
		 offer different services via different alias addresses, such as the
		 "virtual web site" hack. */
		getsockname(netfd, &ouraddr->u.sa, &ouraddr->len);
		//if (rr < 0)
		//	bb_perror_msg_and_die("getsockname after accept");
	}

	if (o_verbose) {
		char *lcladdr, *remaddr, *remhostname;

#if ENABLE_NC_EXTRA && defined(IP_OPTIONS)
	/* If we can, look for any IP options.  Useful for testing the receiving end of
	 such things, and is a good exercise in dealing with it.  We do this before
	 the connect message, to ensure that the connect msg is uniformly the LAST
	 thing to emerge after all the intervening crud.  Doesn't work for UDP on
	 any machines I've tested, but feel free to surprise me. */
		char optbuf[40];
		socklen_t x = sizeof(optbuf);

		rr = getsockopt(netfd, IPPROTO_IP, IP_OPTIONS, optbuf, &x);
		if (rr >= 0 && x) {    /* we've got options, lessee em... */
			*bin2hex(bigbuf_net, optbuf, x) = '\0';
			fprintf(stderr, "IP options: %s\n", bigbuf_net);
		}
#endif

	/* now check out who it is.  We don't care about mismatched DNS names here,
	 but any ADDR and PORT we specified had better fucking well match the caller.
	 Converting from addr to inet_ntoa and back again is a bit of a kludge, but
	 gethostpoop wants a string and there's much gnarlier code out there already,
	 so I don't feel bad.
	 The *real* question is why BFD sockets wasn't designed to allow listens for
	 connections *from* specific hosts/ports, instead of requiring the caller to
	 accept the connection and then reject undesirable ones by closing.
	 In other words, we need a TCP MSG_PEEK. */
	/* bbox: removed most of it */
		lcladdr = xmalloc_sockaddr2dotted(&ouraddr->u.sa);
		remaddr = xmalloc_sockaddr2dotted(&remend.u.sa);
		remhostname = o_nflag ? remaddr : xmalloc_sockaddr2host(&remend.u.sa);
		fprintf(stderr, "connect to %s from %s (%s)\n",
				lcladdr, remhostname, remaddr);
		free(lcladdr);
		free(remaddr);
		if (!o_nflag)
			free(remhostname);
	}

	if (proggie)
		doexec(proggie);
}

/* udptest:
 fire a couple of packets at a UDP target port, just to see if it's really
 there.  On BSD kernels, ICMP host/port-unreachable errors get delivered to
 our socket as ECONNREFUSED write errors.  On SV kernels, we lose; we'll have
 to collect and analyze raw ICMP ourselves a la satan's probe_udp_ports
 backend.  Guess where one could swipe the appropriate code from...

 Use the time delay between writes if given, otherwise use the "tcp ping"
 trick for getting the RTT.  [I got that idea from pluvius, and warped it.]
 Return either the original fd, or clean up and return -1. */
#if ENABLE_NC_EXTRA
static int udptest(void)
{
	int rr;

	rr = write(netfd, bigbuf_in, 1);
	if (rr != 1)
		bb_perror_msg("udptest first write");

	if (o_wait)
		sleep(o_wait); // can be interrupted! while (t) nanosleep(&t)?
	else {
	/* use the tcp-ping trick: try connecting to a normally refused port, which
	 causes us to block for the time that SYN gets there and RST gets back.
	 Not completely reliable, but it *does* mostly work. */
	/* Set a temporary connect timeout, so packet filtration doesn't cause
	 us to hang forever, and hit it */
		o_wait = 5;                     /* enough that we'll notice?? */
		rr = xsocket(ouraddr->u.sa.sa_family, SOCK_STREAM, 0);
		set_nport(&themaddr->u.sa, htons(SLEAZE_PORT));
		connect_w_timeout(rr);
		/* don't need to restore themaddr's port, it's not used anymore */
		close(rr);
		o_wait = 0; /* restore */
	}

	rr = write(netfd, bigbuf_in, 1);
	return (rr != 1); /* if rr == 1, return 0 (success) */
}
#else
int udptest(void);
#endif

/* oprint:
 Hexdump bytes shoveled either way to a running logfile, in the format:
 D offset       -  - - - --- 16 bytes --- - - -  -     # .... ascii .....
 where "which" sets the direction indicator, D:
 0 -- sent to network, or ">"
 1 -- rcvd and printed to stdout, or "<"
 and "buf" and "n" are data-block and length.  If the current block generates
 a partial line, so be it; we *want* that lockstep indication of who sent
 what when.  Adapted from dgaudet's original example -- but must be ripping
 *fast*, since we don't want to be too disk-bound... */
#if ENABLE_NC_EXTRA
static void oprint(int direction, unsigned char *p, unsigned bc)
{
	unsigned obc;           /* current "global" offset */
	unsigned x;
	unsigned char *op;      /* out hexdump ptr */
	unsigned char *ap;      /* out asc-dump ptr */
	unsigned char stage[100];

	if (bc == 0)
		return;

	obc = wrote_net; /* use the globals! */
	if (direction == '<')
		obc = wrote_out;
	stage[0] = direction;
	stage[59] = '#'; /* preload separator */
	stage[60] = ' ';

	do {    /* for chunk-o-data ... */
		x = 16;
		if (bc < 16) {
			/* memset(&stage[bc*3 + 11], ' ', 16*3 - bc*3); */
			memset(&stage[11], ' ', 16*3);
			x = bc;
		}
		sprintf((char *)&stage[1], " %8.8x ", obc);  /* xxx: still slow? */
		bc -= x;          /* fix current count */
		obc += x;         /* fix current offset */
		op = &stage[11];  /* where hex starts */
		ap = &stage[61];  /* where ascii starts */

		do {  /* for line of dump, however long ... */
			*op++ = 0x20 | bb_hexdigits_upcase[*p >> 4];
			*op++ = 0x20 | bb_hexdigits_upcase[*p & 0x0f];
			*op++ = ' ';
			if ((*p > 31) && (*p < 127))
				*ap = *p;   /* printing */
			else
				*ap = '.';  /* nonprinting, loose def */
			ap++;
			p++;
		} while (--x);
		*ap++ = '\n';  /* finish the line */
		xwrite(ofd, stage, ap - stage);
	} while (bc);
}
#else
void oprint(int direction, unsigned char *p, unsigned bc);
#endif

/* readwrite:
 handle stdin/stdout/network I/O.  Bwahaha!! -- the i/o loop from hell.
 In this instance, return what might become our exit status. */
static int readwrite(void)
{
	char *zp = zp; /* gcc */  /* stdin buf ptr */
	char *np = np;            /* net-in buf ptr */
	unsigned rzleft;
	unsigned rnleft;
	unsigned netretry;              /* net-read retry counter */
	unsigned fds_open;

	struct pollfd pfds[2];
	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = netfd;
	pfds[1].events = POLLIN;

	fds_open = 2;
	netretry = 2;
	rzleft = rnleft = 0;
	if (o_interval)
		sleep(o_interval);                /* pause *before* sending stuff, too */

	/* and now the big ol' shoveling loop ... */
	/* nc 1.10 has "while (FD_ISSET(netfd)" here */
	while (fds_open) {
		int rr;
		int poll_tmout_ms;
		unsigned wretry = 8200;               /* net-write sanity counter */

		poll_tmout_ms = -1;
		if (o_wait) {
			poll_tmout_ms = INT_MAX;
			if (o_wait < INT_MAX / 1000)
				poll_tmout_ms = o_wait * 1000;
		}
		rr = poll(pfds, 2, poll_tmout_ms);
		if (rr < 0 && errno != EINTR) {                /* might have gotten ^Zed, etc */
			holler_perror("poll");
			close(netfd);
			return 1;
		}
	/* if we have a timeout AND stdin is closed AND we haven't heard anything
	 from the net during that time, assume it's dead and close it too. */
		if (rr == 0) {
			if (!pfds[0].revents) {
				netretry--;                        /* we actually try a coupla times. */
				if (!netretry) {
					if (o_verbose > 1)         /* normally we don't care */
						fprintf(stderr, "net timeout\n");
					/*close(netfd); - redundant, exit will do it */
					return 0;                  /* not an error! */
				}
			}
		} /* timeout */

	/* Ding!!  Something arrived, go check all the incoming hoppers, net first */
		if (pfds[1].revents) {                /* net: ding! */
			rr = read(netfd, bigbuf_net, BIGSIZ);
			if (rr <= 0) {
				if (rr < 0 && o_verbose > 1) {
					/* nc 1.10 doesn't do this */
					bb_perror_msg("net read");
				}
				pfds[1].fd = -1;                   /* don't poll for netfd anymore */
				fds_open--;
				rzleft = 0;                        /* can't write anymore: broken pipe */
			} else {
				rnleft = rr;
				np = bigbuf_net;
			}
Debug("got %d from the net, errno %d", rr, errno);
		} /* net:ding */

	/* if we're in "slowly" mode there's probably still stuff in the stdin
	 buffer, so don't read unless we really need MORE INPUT!  MORE INPUT! */
		if (rzleft)
			goto shovel;

	/* okay, suck more stdin */
		if (pfds[0].revents) {                /* stdin: ding! */
			rr = read(STDIN_FILENO, bigbuf_in, BIGSIZ);
	/* Considered making reads here smaller for UDP mode, but 8192-byte
	 mobygrams are kinda fun and exercise the reassembler. */
			if (rr <= 0) {                        /* at end, or fukt, or ... */
				pfds[0].fd = -1;              /* disable stdin */
				/*close(STDIN_FILENO); - not really necessary */
				/* Let peer know we have no more data */
				/* nc 1.10 doesn't do this: */
				shutdown(netfd, SHUT_WR);
				fds_open--;
			} else {
				rzleft = rr;
				zp = bigbuf_in;
			}
		} /* stdin:ding */
 shovel:
	/* now that we've dingdonged all our thingdings, send off the results.
	 Geez, why does this look an awful lot like the big loop in "rsh"? ...
	 not sure if the order of this matters, but write net -> stdout first. */

		if (rnleft) {
			rr = write(STDOUT_FILENO, np, rnleft);
			if (rr > 0) {
				if (o_ofile) /* log the stdout */
					oprint('<', (unsigned char *)np, rr);
				np += rr;
				rnleft -= rr;
				wrote_out += rr; /* global count */
			}
Debug("wrote %d to stdout, errno %d", rr, errno);
		} /* rnleft */
		if (rzleft) {
			if (o_interval)                        /* in "slowly" mode ?? */
				rr = findline(zp, rzleft);
			else
				rr = rzleft;
			rr = write(netfd, zp, rr);        /* one line, or the whole buffer */
			if (rr > 0) {
				if (o_ofile) /* log what got sent */
					oprint('>', (unsigned char *)zp, rr);
				zp += rr;
				rzleft -= rr;
				wrote_net += rr; /* global count */
			}
Debug("wrote %d to net, errno %d", rr, errno);
		} /* rzleft */
		if (o_interval) {                        /* cycle between slow lines, or ... */
			sleep(o_interval);
			continue;                        /* ...with hairy loop... */
		}
		if (rzleft || rnleft) {                  /* shovel that shit till they ain't */
			wretry--;                        /* none left, and get another load */
	/* net write retries sometimes happen on UDP connections */
			if (!wretry) {                   /* is something hung? */
				holler_error("too many output retries");
				return 1;
			}
			goto shovel;
		}
	} /* while (fds_open) */

	/* XXX: maybe want a more graceful shutdown() here, or screw around with
	 linger times??  I suspect that I don't need to since I'm always doing
	 blocking reads and writes and my own manual "last ditch" efforts to read
	 the net again after a timeout.  I haven't seen any screwups yet, but it's
	 not like my test network is particularly busy... */
	close(netfd);
	return 0;
} /* readwrite */

/* main: now we pull it all together... */
int nc_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nc_main(int argc UNUSED_PARAM, char **argv)
{
	char *str_p, *str_s;
	IF_NC_EXTRA(char *str_i, *str_o;)
	char *themdotted = themdotted; /* for compiler */
	char **proggie;
	int x;
	unsigned cnt_l = 0;
	unsigned o_lport = 0;

	INIT_G();

	/* catch a signal or two for cleanup */
	bb_signals(0
		+ (1 << SIGINT)
		+ (1 << SIGQUIT)
		+ (1 << SIGTERM)
		, catch);
	/* and suppress others... */
	bb_signals(0
#ifdef SIGURG
		+ (1 << SIGURG)
#endif
		+ (1 << SIGPIPE) /* important! */
		, SIG_IGN);

	proggie = argv;
	while (*++proggie) {
		if (strcmp(*proggie, "-e") == 0) {
			*proggie = NULL;
			proggie++;
			goto e_found;
		}
		/* -<other_opts>e PROG [ARGS] ? */
		/* (aboriginal linux uses this form) */
		if (proggie[0][0] == '-') {
			char *optpos = *proggie + 1;
			/* Skip all valid opts w/o params */
			optpos = optpos + strspn(optpos, "nuv"IF_NC_SERVER("lk")IF_NC_EXTRA("z"));
			if (*optpos == 'e' && !optpos[1]) {
				*optpos = '\0';
				proggie++;
				G.proggie0saved = *proggie;
				*proggie = NULL; /* terminate argv for getopt32 */
				goto e_found;
			}
		}
	}
	proggie = NULL;
 e_found:

	// -g -G -t -r deleted, unimplemented -a deleted too
	getopt32(argv, "^"
		"np:s:uvw:+"/* -w N */ IF_NC_SERVER("lk")
		IF_NC_EXTRA("i:o:z")
			"\0"
			"?2:vv"IF_NC_SERVER(":ll"), /* max 2 params; -v and -l are counters */
		&str_p, &str_s, &o_wait
		IF_NC_EXTRA(, &str_i, &str_o)
			, &o_verbose IF_NC_SERVER(, &cnt_l)
	);
	argv += optind;
#if ENABLE_NC_EXTRA
	if (option_mask32 & OPT_i) /* line-interval time */
		o_interval = xatou_range(str_i, 1, 0xffff);
#endif
#if ENABLE_NC_SERVER
	//if (option_mask32 & OPT_l) /* listen mode */
	if (option_mask32 & OPT_k) /* persistent server mode */
		cnt_l = 2;
#endif
	//if (option_mask32 & OPT_n) /* numeric-only, no DNS lookups */
	//if (option_mask32 & OPT_o) /* hexdump log */
	if (option_mask32 & OPT_p) { /* local source port */
		o_lport = bb_lookup_port(str_p, o_udpmode ? "udp" : "tcp", 0);
		if (!o_lport)
			bb_error_msg_and_die("bad local port '%s'", str_p);
	}
	//if (option_mask32 & OPT_r) /* randomize various things */
	//if (option_mask32 & OPT_u) /* use UDP */
	//if (option_mask32 & OPT_v) /* verbose */
	//if (option_mask32 & OPT_w) /* wait time */
	//if (option_mask32 & OPT_z) /* little or no data xfer */

	/* We manage our fd's so that they are never 0,1,2 */
	/*bb_sanitize_stdio(); - not needed */

	if (argv[0]) {
		themaddr = xhost2sockaddr(argv[0],
			argv[1]
			? bb_lookup_port(argv[1], o_udpmode ? "udp" : "tcp", 0)
			: 0);
	}

	/* create & bind network socket */
	x = (o_udpmode ? SOCK_DGRAM : SOCK_STREAM);
	if (option_mask32 & OPT_s) { /* local address */
		/* if o_lport is still 0, then we will use random port */
		ouraddr = xhost2sockaddr(str_s, o_lport);
#ifdef BLOAT
		/* prevent spurious "UDP listen needs !0 port" */
		o_lport = get_nport(ouraddr);
		o_lport = ntohs(o_lport);
#endif
		x = xsocket(ouraddr->u.sa.sa_family, x, 0);
	} else {
		/* We try IPv6, then IPv4, unless addr family is
		 * implicitly set by way of remote addr/port spec */
		x = xsocket_type(&ouraddr,
				(themaddr ? themaddr->u.sa.sa_family : AF_UNSPEC),
				x);
		if (o_lport)
			set_nport(&ouraddr->u.sa, htons(o_lport));
	}
	xmove_fd(x, netfd);
	setsockopt_reuseaddr(netfd);
	if (o_udpmode)
		socket_want_pktinfo(netfd);
	if (!ENABLE_FEATURE_UNIX_LOCAL
	 || cnt_l != 0 /* listen */
	 || ouraddr->u.sa.sa_family != AF_UNIX
	) {
		xbind(netfd, &ouraddr->u.sa, ouraddr->len);
	}
#if 0
	setsockopt_SOL_SOCKET_int(netfd, SO_RCVBUF, o_rcvbuf);
	setsockopt_SOL_SOCKET_int(netfd, SO_SNDBUF, o_sndbuf);
#endif

#ifdef BLOAT
	if (OPT_l && (option_mask32 & (OPT_u|OPT_l)) == (OPT_u|OPT_l)) {
		/* apparently UDP can listen ON "port 0",
		 but that's not useful */
		if (!o_lport)
			bb_error_msg_and_die("UDP listen needs nonzero -p port");
	}
#endif

	if (proggie) {
		close(STDIN_FILENO); /* won't need stdin */
		option_mask32 &= ~OPT_o; /* -o with -e is meaningless! */
	}
#if ENABLE_NC_EXTRA
	if (o_ofile)
		xmove_fd(xopen(str_o, O_WRONLY|O_CREAT|O_TRUNC), ofd);
#endif

	if (cnt_l != 0) {
		dolisten((cnt_l - 1), proggie);
		/* dolisten does its own connect reporting */
		x = readwrite(); /* it even works with UDP! */
	} else {
		/* Outbound connects.  Now we're more picky about args... */
		if (!themaddr)
			bb_show_usage();

		remend = *themaddr;
		if (o_verbose)
			themdotted = xmalloc_sockaddr2dotted(&themaddr->u.sa);

		x = connect_w_timeout(netfd);
		if (o_zero && x == 0 && o_udpmode)        /* if UDP scanning... */
			x = udptest();
		if (x == 0) {                        /* Yow, are we OPEN YET?! */
			if (o_verbose)
				fprintf(stderr, "%s (%s) open\n", argv[0], themdotted);
			if (proggie)                        /* exec is valid for outbound, too */
				doexec(proggie);
			if (!o_zero)
				x = readwrite();
		} else { /* connect or udptest wasn't successful */
			x = 1;                                /* exit status */
			/* if we're scanning at a "one -v" verbosity level, don't print refusals.
			 Give it another -v if you want to see everything. */
			if (o_verbose > 1 || (o_verbose && errno != ECONNREFUSED))
				bb_perror_msg("%s (%s)", argv[0], themdotted);
		}
	}
	if (o_verbose > 1)                /* normally we don't care */
		fprintf(stderr, SENT_N_RECV_M, wrote_net, wrote_out);
	return x;
}
