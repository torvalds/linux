/* vi: set sw=4 ts=4: */
/*
 * RFC3927 ZeroConf IPv4 Link-Local addressing
 * (see <http://www.zeroconf.org/>)
 *
 * Copyright (C) 2003 by Arthur van Hoff (avh@strangeberry.com)
 * Copyright (C) 2004 by David Brownell
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/*
 * ZCIP just manages the 169.254.*.* addresses.  That network is not
 * routed at the IP level, though various proxies or bridges can
 * certainly be used.  Its naming is built over multicast DNS.
 */
//config:config ZCIP
//config:	bool "zcip (7.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	select FEATURE_SYSLOG
//config:	help
//config:	ZCIP provides ZeroConf IPv4 address selection, according to RFC 3927.
//config:	It's a daemon that allocates and defends a dynamically assigned
//config:	address on the 169.254/16 network, requiring no system administrator.
//config:
//config:	See http://www.zeroconf.org for further details, and "zcip.script"
//config:	in the busybox examples.

//applet:IF_ZCIP(APPLET(zcip, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_ZCIP) += zcip.o

//#define DEBUG

// TODO:
// - more real-world usage/testing, especially daemon mode
// - kernel packet filters to reduce scheduling noise
// - avoid silent script failures, especially under load...
// - link status monitoring (restart on link-up; stop on link-down)

//usage:#define zcip_trivial_usage
//usage:       "[OPTIONS] IFACE SCRIPT"
//usage:#define zcip_full_usage "\n\n"
//usage:       "Manage a ZeroConf IPv4 link-local address\n"
//usage:     "\n	-f		Run in foreground"
//usage:     "\n	-q		Quit after obtaining address"
//usage:     "\n	-r 169.254.x.x	Request this address first"
//usage:     "\n	-l x.x.0.0	Use this range instead of 169.254"
//usage:     "\n	-v		Verbose"
//usage:     "\n"
//usage:     "\n$LOGGING=none		Suppress logging"
//usage:     "\n$LOGGING=syslog 	Log to syslog"
//usage:     "\n"
//usage:     "\nWith no -q, runs continuously monitoring for ARP conflicts,"
//usage:     "\nexits only on I/O errors (link down etc)"

#include "libbb.h"
#include "common_bufsiz.h"
#include <netinet/ether.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/sockios.h>

#include <syslog.h>

/* We don't need more than 32 bits of the counter */
#define MONOTONIC_US() ((unsigned)monotonic_us())

struct arp_packet {
	struct ether_header eth;
	struct ether_arp arp;
} PACKED;

enum {
	/* 0-1 seconds before sending 1st probe */
	PROBE_WAIT = 1,
	/* 1-2 seconds between probes */
	PROBE_MIN = 1,
	PROBE_MAX = 2,
	PROBE_NUM = 3,		/* total probes to send */
	ANNOUNCE_INTERVAL = 2,  /* 2 seconds between announces */
	ANNOUNCE_NUM = 3,	/* announces to send */
	/* if probe/announce sees a conflict, multiply RANDOM(NUM_CONFLICT) by... */
	CONFLICT_MULTIPLIER = 2,
	/* if we monitor and see a conflict, how long is defend state? */
	DEFEND_INTERVAL = 10,
};

/* States during the configuration process. */
enum {
	PROBE = 0,
	ANNOUNCE,
	MONITOR,
	DEFEND
};

#define VDBG(...) do { } while (0)


enum {
	sock_fd = 3
};

struct globals {
	struct sockaddr iface_sockaddr;
	struct ether_addr our_ethaddr;
	uint32_t localnet_ip;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)


/**
 * Pick a random link local IP address on 169.254/16, except that
 * the first and last 256 addresses are reserved.
 */
static uint32_t pick_nip(void)
{
	unsigned tmp;

	do {
		tmp = rand() & IN_CLASSB_HOST;
	} while (tmp > (IN_CLASSB_HOST - 0x0200));
	return htonl((G.localnet_ip + 0x0100) + tmp);
}

static const char *nip_to_a(uint32_t nip)
{
	struct in_addr in;
	in.s_addr = nip;
	return inet_ntoa(in);
}

/**
 * Broadcast an ARP packet.
 */
static void send_arp_request(
	/* int op, - always ARPOP_REQUEST */
	/* const struct ether_addr *source_eth, - always &G.our_ethaddr */
					uint32_t source_nip,
	const struct ether_addr *target_eth, uint32_t target_nip)
{
	enum { op = ARPOP_REQUEST };
#define source_eth (&G.our_ethaddr)

	struct arp_packet p;
	memset(&p, 0, sizeof(p));

	// ether header
	p.eth.ether_type = htons(ETHERTYPE_ARP);
	memcpy(p.eth.ether_shost, source_eth, ETH_ALEN);
	memset(p.eth.ether_dhost, 0xff, ETH_ALEN);

	// arp request
	p.arp.arp_hrd = htons(ARPHRD_ETHER);
	p.arp.arp_pro = htons(ETHERTYPE_IP);
	p.arp.arp_hln = ETH_ALEN;
	p.arp.arp_pln = 4;
	p.arp.arp_op = htons(op);
	memcpy(&p.arp.arp_sha, source_eth, ETH_ALEN);
	memcpy(&p.arp.arp_spa, &source_nip, 4);
	memcpy(&p.arp.arp_tha, target_eth, ETH_ALEN);
	memcpy(&p.arp.arp_tpa, &target_nip, 4);

	// send it
	// Even though sock_fd is already bound to G.iface_sockaddr, just send()
	// won't work, because "socket is not connected"
	// (and connect() won't fix that, "operation not supported").
	// Thus we sendto() to G.iface_sockaddr. I wonder which sockaddr
	// (from bind() or from sendto()?) kernel actually uses
	// to determine iface to emit the packet from...
	xsendto(sock_fd, &p, sizeof(p), &G.iface_sockaddr, sizeof(G.iface_sockaddr));
#undef source_eth
}

/**
 * Run a script.
 * argv[0]:intf argv[1]:script_name argv[2]:junk argv[3]:NULL
 */
static int run(char *argv[3], const char *param, uint32_t nip)
{
	int status;
	const char *addr = addr; /* for gcc */
	const char *fmt = "%s %s %s" + 3;
	char *env_ip = env_ip;

	argv[2] = (char*)param;

	VDBG("%s run %s %s\n", argv[0], argv[1], argv[2]);

	if (nip != 0) {
		addr = nip_to_a(nip);
		/* Must not use setenv() repeatedly, it leaks memory. Use putenv() */
		env_ip = xasprintf("ip=%s", addr);
		putenv(env_ip);
		fmt -= 3;
	}
	bb_error_msg(fmt, argv[2], argv[0], addr);
	status = spawn_and_wait(argv + 1);
	if (nip != 0)
		bb_unsetenv_and_free(env_ip);

	if (status < 0) {
		bb_perror_msg("%s %s %s" + 3, argv[2], argv[0]);
		return -errno;
	}
	if (status != 0)
		bb_error_msg("script %s %s failed, exitcode=%d", argv[1], argv[2], status & 0xff);
	return status;
}

/**
 * Return milliseconds of random delay, up to "secs" seconds.
 */
static ALWAYS_INLINE unsigned random_delay_ms(unsigned secs)
{
	return (unsigned)rand() % (secs * 1000);
}

/**
 * main program
 */
int zcip_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int zcip_main(int argc UNUSED_PARAM, char **argv)
{
	char *r_opt;
	const char *l_opt = "169.254.0.0";
	int state;
	int nsent;
	unsigned opts;

	// Ugly trick, but I want these zeroed in one go
	struct {
		const struct ether_addr null_ethaddr;
		struct ifreq ifr;
		uint32_t chosen_nip;
		int conflicts;
		int timeout_ms; // must be signed
		int verbose;
	} L;
#define null_ethaddr (L.null_ethaddr)
#define ifr          (L.ifr         )
#define chosen_nip   (L.chosen_nip  )
#define conflicts    (L.conflicts   )
#define timeout_ms   (L.timeout_ms  )
#define verbose      (L.verbose     )

	memset(&L, 0, sizeof(L));
	INIT_G();

#define FOREGROUND (opts & 1)
#define QUIT       (opts & 2)
	// Parse commandline: prog [options] ifname script
	// exactly 2 args; -v accumulates and implies -f
	opts = getopt32(argv, "^" "fqr:l:v" "\0" "=2:vv:vf",
				&r_opt, &l_opt, &verbose
	);
#if !BB_MMU
	// on NOMMU reexec early (or else we will rerun things twice)
	if (!FOREGROUND)
		bb_daemonize_or_rexec(0 /*was: DAEMON_CHDIR_ROOT*/, argv);
#endif
	// Open an ARP socket
	// (need to do it before openlog to prevent openlog from taking
	// fd 3 (sock_fd==3))
	xmove_fd(xsocket(AF_PACKET, SOCK_PACKET, htons(ETH_P_ARP)), sock_fd);
	if (!FOREGROUND) {
		// do it before all bb_xx_msg calls
		openlog(applet_name, 0, LOG_DAEMON);
		logmode |= LOGMODE_SYSLOG;
	}
	bb_logenv_override();

	{ // -l n.n.n.n
		struct in_addr net;
		if (inet_aton(l_opt, &net) == 0
		 || (net.s_addr & htonl(IN_CLASSB_NET)) != net.s_addr
		) {
			bb_error_msg_and_die("invalid network address");
		}
		G.localnet_ip = ntohl(net.s_addr);
	}
	if (opts & 4) { // -r n.n.n.n
		struct in_addr ip;
		if (inet_aton(r_opt, &ip) == 0
		 || (ntohl(ip.s_addr) & IN_CLASSB_NET) != G.localnet_ip
		) {
			bb_error_msg_and_die("invalid link address");
		}
		chosen_nip = ip.s_addr;
	}
	argv += optind - 1;

	/* Now: argv[0]:junk argv[1]:intf argv[2]:script argv[3]:NULL */
	/* We need to make space for script argument: */
	argv[0] = argv[1];
	argv[1] = argv[2];
	/* Now: argv[0]:intf argv[1]:script argv[2]:junk argv[3]:NULL */
#define argv_intf (argv[0])

	xsetenv("interface", argv_intf);

	// Initialize the interface (modprobe, ifup, etc)
	if (run(argv, "init", 0))
		return EXIT_FAILURE;

	// Initialize G.iface_sockaddr
	// G.iface_sockaddr is: { u16 sa_family; u8 sa_data[14]; }
	//memset(&G.iface_sockaddr, 0, sizeof(G.iface_sockaddr));
	//TODO: are we leaving sa_family == 0 (AF_UNSPEC)?!
	safe_strncpy(G.iface_sockaddr.sa_data, argv_intf, sizeof(G.iface_sockaddr.sa_data));

	// Bind to the interface's ARP socket
	xbind(sock_fd, &G.iface_sockaddr, sizeof(G.iface_sockaddr));

	// Get the interface's ethernet address
	//memset(&ifr, 0, sizeof(ifr));
	strncpy_IFNAMSIZ(ifr.ifr_name, argv_intf);
	xioctl(sock_fd, SIOCGIFHWADDR, &ifr);
	memcpy(&G.our_ethaddr, &ifr.ifr_hwaddr.sa_data, ETH_ALEN);

	// Start with some stable ip address, either a function of
	// the hardware address or else the last address we used.
	// we are taking low-order four bytes, as top-order ones
	// aren't random enough.
	// NOTE: the sequence of addresses we try changes only
	// depending on when we detect conflicts.
	{
		uint32_t t;
		move_from_unaligned32(t, ((char *)&G.our_ethaddr + 2));
		srand(t);
	}
	// FIXME cases to handle:
	//  - zcip already running!
	//  - link already has local address... just defend/update

	// Daemonize now; don't delay system startup
	if (!FOREGROUND) {
#if BB_MMU
		bb_daemonize(0 /*was: DAEMON_CHDIR_ROOT*/);
#endif
		bb_error_msg("start, interface %s", argv_intf);
	}

	// Run the dynamic address negotiation protocol,
	// restarting after address conflicts:
	//  - start with some address we want to try
	//  - short random delay
	//  - arp probes to see if another host uses it
	//    00:04:e2:64:23:c2 > ff:ff:ff:ff:ff:ff arp who-has 169.254.194.171 tell 0.0.0.0
	//  - arp announcements that we're claiming it
	//    00:04:e2:64:23:c2 > ff:ff:ff:ff:ff:ff arp who-has 169.254.194.171 (00:04:e2:64:23:c2) tell 169.254.194.171
	//  - use it
	//  - defend it, within limits
	// exit if:
	// - address is successfully obtained and -q was given:
	//   run "<script> config", then exit with exitcode 0
	// - poll error (when does this happen?)
	// - read error (when does this happen?)
	// - sendto error (in send_arp_request()) (when does this happen?)
	// - revents & POLLERR (link down). run "<script> deconfig" first
	if (chosen_nip == 0) {
 new_nip_and_PROBE:
		chosen_nip = pick_nip();
	}
	nsent = 0;
	state = PROBE;
	while (1) {
		struct pollfd fds[1];
		unsigned deadline_us = deadline_us;
		struct arp_packet p;
		int ip_conflict;
		int n;

		fds[0].fd = sock_fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;

		// Poll, being ready to adjust current timeout
		if (!timeout_ms) {
			timeout_ms = random_delay_ms(PROBE_WAIT);
			// FIXME setsockopt(sock_fd, SO_ATTACH_FILTER, ...) to
			// make the kernel filter out all packets except
			// ones we'd care about.
		}
		if (timeout_ms >= 0) {
			// Set deadline_us to the point in time when we timeout
			deadline_us = MONOTONIC_US() + timeout_ms * 1000;
		}

		VDBG("...wait %d %s nsent=%u\n",
				timeout_ms, argv_intf, nsent);

		n = safe_poll(fds, 1, timeout_ms);
		if (n < 0) {
			//bb_perror_msg("poll"); - done in safe_poll
			return EXIT_FAILURE;
		}
		if (n == 0) { // timed out?
			VDBG("state:%d\n", state);
			switch (state) {
			case PROBE:
				// No conflicting ARP packets were seen:
				// we can progress through the states
				if (nsent < PROBE_NUM) {
					nsent++;
					VDBG("probe/%u %s@%s\n",
							nsent, argv_intf, nip_to_a(chosen_nip));
					timeout_ms = PROBE_MIN * 1000;
					timeout_ms += random_delay_ms(PROBE_MAX - PROBE_MIN);
					send_arp_request(0, &null_ethaddr, chosen_nip);
					continue;
				}
				// Switch to announce state
				nsent = 0;
				state = ANNOUNCE;
				goto send_announce;
			case ANNOUNCE:
				// No conflicting ARP packets were seen:
				// we can progress through the states
				if (nsent < ANNOUNCE_NUM) {
 send_announce:
					nsent++;
					VDBG("announce/%u %s@%s\n",
							nsent, argv_intf, nip_to_a(chosen_nip));
					timeout_ms = ANNOUNCE_INTERVAL * 1000;
					send_arp_request(chosen_nip, &G.our_ethaddr, chosen_nip);
					continue;
				}
				// Switch to monitor state
				// FIXME update filters
				run(argv, "config", chosen_nip);
				// NOTE: all other exit paths should deconfig...
				if (QUIT)
					return EXIT_SUCCESS;
				// fall through: switch to MONITOR
			default:
			// case DEFEND:
			// case MONITOR: (shouldn't happen, MONITOR timeout is infinite)
				// Defend period ended with no ARP replies - we won
				timeout_ms = -1; // never timeout in monitor state
				state = MONITOR;
				continue;
			}
		}

		// Packet arrived, or link went down.
		// We need to adjust the timeout in case we didn't receive
		// a conflicting packet.
		if (timeout_ms > 0) {
			unsigned diff = deadline_us - MONOTONIC_US();
			if ((int)(diff) < 0) {
				// Current time is greater than the expected timeout time.
				diff = 0;
			}
			VDBG("adjusting timeout\n");
			timeout_ms = (diff / 1000) | 1; // never 0
		}

		if ((fds[0].revents & POLLIN) == 0) {
			if (fds[0].revents & POLLERR) {
				// FIXME: links routinely go down;
				// this shouldn't necessarily exit.
				bb_error_msg("iface %s is down", argv_intf);
				if (state >= MONITOR) {
					// Only if we are in MONITOR or DEFEND
					run(argv, "deconfig", chosen_nip);
				}
				return EXIT_FAILURE;
			}
			continue;
		}

		// Read ARP packet
		if (safe_read(sock_fd, &p, sizeof(p)) < 0) {
			bb_perror_msg_and_die(bb_msg_read_error);
		}

		if (p.eth.ether_type != htons(ETHERTYPE_ARP))
			continue;
		if (p.arp.arp_op != htons(ARPOP_REQUEST)
		 && p.arp.arp_op != htons(ARPOP_REPLY)
		) {
			continue;
		}
#ifdef DEBUG
		{
			struct ether_addr *sha = (struct ether_addr *) p.arp.arp_sha;
			struct ether_addr *tha = (struct ether_addr *) p.arp.arp_tha;
			struct in_addr *spa = (struct in_addr *) p.arp.arp_spa;
			struct in_addr *tpa = (struct in_addr *) p.arp.arp_tpa;
			VDBG("source=%s %s\n", ether_ntoa(sha),	inet_ntoa(*spa));
			VDBG("target=%s %s\n", ether_ntoa(tha),	inet_ntoa(*tpa));
		}
#endif
		ip_conflict = 0;
		if (memcmp(&p.arp.arp_sha, &G.our_ethaddr, ETH_ALEN) != 0) {
			if (memcmp(p.arp.arp_spa, &chosen_nip, 4) == 0) {
				// A probe or reply with source_ip == chosen ip
				ip_conflict = 1;
			}
			if (p.arp.arp_op == htons(ARPOP_REQUEST)
			 && memcmp(p.arp.arp_spa, &const_int_0, 4) == 0
			 && memcmp(p.arp.arp_tpa, &chosen_nip, 4) == 0
			) {
				// A probe with source_ip == 0.0.0.0, target_ip == chosen ip:
				// another host trying to claim this ip!
				ip_conflict |= 2;
			}
		}
		VDBG("state:%d ip_conflict:%d\n", state, ip_conflict);
		if (!ip_conflict)
			continue;

		// Either src or target IP conflict exists
		if (state <= ANNOUNCE) {
			// PROBE or ANNOUNCE
			conflicts++;
			timeout_ms = PROBE_MIN * 1000
				+ CONFLICT_MULTIPLIER * random_delay_ms(conflicts);
			goto new_nip_and_PROBE;
		}

		// MONITOR or DEFEND: only src IP conflict is a problem
		if (ip_conflict & 1) {
			if (state == MONITOR) {
				// Src IP conflict, defend with a single ARP probe
				VDBG("monitor conflict - defending\n");
				timeout_ms = DEFEND_INTERVAL * 1000;
				state = DEFEND;
				send_arp_request(chosen_nip, &G.our_ethaddr, chosen_nip);
				continue;
			}
			// state == DEFEND
			// Another src IP conflict, start over
			VDBG("defend conflict - starting over\n");
			run(argv, "deconfig", chosen_nip);
			conflicts = 0;
			timeout_ms = 0;
			goto new_nip_and_PROBE;
		}
		// Note: if we only have a target IP conflict here (ip_conflict & 2),
		// IOW: if we just saw this sort of ARP packet:
		//  aa:bb:cc:dd:ee:ff > xx:xx:xx:xx:xx:xx arp who-has <chosen_nip> tell 0.0.0.0
		// we expect _kernel_ to respond to that, because <chosen_nip>
		// is (expected to be) configured on this iface.
	} // while (1)
#undef argv_intf
}
