/* vi: set sw=4 ts=4: */
/*
 * Mini ipcalc implementation for busybox
 *
 * By Jordan Crouse <jordan@cosmicpenguin.net>
 *    Stephan Linz  <linz@li-pro.net>
 *
 * This is a complete reimplementation of the ipcalc program
 * from Red Hat.  I didn't look at their source code, but there
 * is no denying that this is a loving reimplementation
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config IPCALC
//config:	bool "ipcalc (4.3 kb)"
//config:	default y
//config:	help
//config:	ipcalc takes an IP address and netmask and calculates the
//config:	resulting broadcast, network, and host range.
//config:
//config:config FEATURE_IPCALC_LONG_OPTIONS
//config:	bool "Enable long options"
//config:	default y
//config:	depends on IPCALC && LONG_OPTS
//config:
//config:config FEATURE_IPCALC_FANCY
//config:	bool "Fancy IPCALC, more options, adds 1 kbyte"
//config:	default y
//config:	depends on IPCALC
//config:	help
//config:	Adds the options hostname, prefix and silent to the output of
//config:	"ipcalc".

//applet:IF_IPCALC(APPLET_NOEXEC(ipcalc, ipcalc, BB_DIR_BIN, BB_SUID_DROP, ipcalc))

//kbuild:lib-$(CONFIG_IPCALC) += ipcalc.o

//usage:#define ipcalc_trivial_usage
//usage:       "[OPTIONS] ADDRESS"
//usage:       IF_FEATURE_IPCALC_FANCY("[/PREFIX]") " [NETMASK]"
//usage:#define ipcalc_full_usage "\n\n"
//usage:       "Calculate and display network settings from IP address\n"
//usage:     "\n	-b	Broadcast address"
//usage:     "\n	-n	Network address"
//usage:     "\n	-m	Default netmask for IP"
//usage:	IF_FEATURE_IPCALC_FANCY(
//usage:     "\n	-p	Prefix for IP/NETMASK"
//usage:     "\n	-h	Resolved host name"
//usage:     "\n	-s	No error messages"
//usage:	)

#include "libbb.h"
/* After libbb.h, because on some systems it needs other includes */
#include <arpa/inet.h>

#define CLASS_A_NETMASK ntohl(0xFF000000)
#define CLASS_B_NETMASK ntohl(0xFFFF0000)
#define CLASS_C_NETMASK ntohl(0xFFFFFF00)

static unsigned long get_netmask(unsigned long ipaddr)
{
	ipaddr = htonl(ipaddr);

	if ((ipaddr & 0xC0000000) == 0xC0000000)
		return CLASS_C_NETMASK;
	else if ((ipaddr & 0x80000000) == 0x80000000)
		return CLASS_B_NETMASK;
	else if ((ipaddr & 0x80000000) == 0)
		return CLASS_A_NETMASK;
	else
		return 0;
}

#if ENABLE_FEATURE_IPCALC_FANCY
static int get_prefix(unsigned long netmask)
{
	unsigned long msk = 0x80000000;
	int ret = 0;

	netmask = htonl(netmask);
	while (msk) {
		if (netmask & msk)
			ret++;
		msk >>= 1;
	}
	return ret;
}
#else
int get_prefix(unsigned long netmask);
#endif


#define NETMASK   0x01
#define BROADCAST 0x02
#define NETWORK   0x04
#define NETPREFIX 0x08
#define HOSTNAME  0x10
#define SILENT    0x20

#if ENABLE_FEATURE_IPCALC_LONG_OPTIONS
	static const char ipcalc_longopts[] ALIGN1 =
		"netmask\0"   No_argument "m" // netmask from IP (assuming complete class A, B, or C network)
		"broadcast\0" No_argument "b" // broadcast from IP [netmask]
		"network\0"   No_argument "n" // network from IP [netmask]
# if ENABLE_FEATURE_IPCALC_FANCY
		"prefix\0"    No_argument "p" // prefix from IP[/prefix] [netmask]
		"hostname\0"  No_argument "h" // hostname from IP
		"silent\0"    No_argument "s" // don’t ever display error messages
# endif
		;
# define GETOPT32 getopt32long
# define LONGOPTS ,ipcalc_longopts
#else
# define GETOPT32 getopt32
# define LONGOPTS
#endif

int ipcalc_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ipcalc_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	bool have_netmask = 0;
	struct in_addr s_netmask, s_broadcast, s_network, s_ipaddr;
	/* struct in_addr { in_addr_t s_addr; }  and  in_addr_t
	 * (which in turn is just a typedef to uint32_t)
	 * are essentially the same type. A few macros for less verbosity: */
#define netmask   (s_netmask.s_addr)
#define broadcast (s_broadcast.s_addr)
#define network   (s_network.s_addr)
#define ipaddr    (s_ipaddr.s_addr)
	char *ipstr;

	opt = GETOPT32(argv, "^"
			"mbn" IF_FEATURE_IPCALC_FANCY("phs")
			"\0" "-1:?2"/*min 1, max 2 args*/
			LONGOPTS
	);
	argv += optind;
	if (opt & SILENT)
		logmode = LOGMODE_NONE; /* suppress error_msg() output */
	opt &= ~SILENT;
	if (!(opt & (BROADCAST | NETWORK | NETPREFIX))) {
		/* if no options at all or
		 * (no broadcast,network,prefix) and (two args)... */
		if (!opt || argv[1])
			bb_show_usage();
	}

	ipstr = argv[0];
	if (ENABLE_FEATURE_IPCALC_FANCY) {
		unsigned long netprefix = 0;
		char *prefixstr;

		prefixstr = ipstr;

		while (*prefixstr) {
			if (*prefixstr == '/') {
				*prefixstr++ = '\0';
				if (*prefixstr) {
					unsigned msk;
					netprefix = xatoul_range(prefixstr, 0, 32);
					netmask = 0;
					msk = 0x80000000;
					while (netprefix > 0) {
						netmask |= msk;
						msk >>= 1;
						netprefix--;
					}
					netmask = htonl(netmask);
					/* Even if it was 0, we will signify that we have a netmask. This allows */
					/* for specification of default routes, etc which have a 0 netmask/prefix */
					have_netmask = 1;
				}
				break;
			}
			prefixstr++;
		}
	}

	if (inet_aton(ipstr, &s_ipaddr) == 0) {
		bb_error_msg_and_die("bad IP address: %s", argv[0]);
	}

	if (argv[1]) {
		if (ENABLE_FEATURE_IPCALC_FANCY && have_netmask) {
			bb_error_msg_and_die("use prefix or netmask, not both");
		}
		if (inet_aton(argv[1], &s_netmask) == 0) {
			bb_error_msg_and_die("bad netmask: %s", argv[1]);
		}
	} else {
		/* JHC - If the netmask wasn't provided then calculate it */
		if (!ENABLE_FEATURE_IPCALC_FANCY || !have_netmask)
			netmask = get_netmask(ipaddr);
	}

	if (opt & NETMASK) {
		printf("NETMASK=%s\n", inet_ntoa(s_netmask));
	}

	if (opt & BROADCAST) {
		broadcast = (ipaddr & netmask) | ~netmask;
		printf("BROADCAST=%s\n", inet_ntoa(s_broadcast));
	}

	if (opt & NETWORK) {
		network = ipaddr & netmask;
		printf("NETWORK=%s\n", inet_ntoa(s_network));
	}

	if (ENABLE_FEATURE_IPCALC_FANCY) {
		if (opt & NETPREFIX) {
			printf("PREFIX=%i\n", get_prefix(netmask));
		}

		if (opt & HOSTNAME) {
			struct hostent *hostinfo;

			hostinfo = gethostbyaddr((char *) &ipaddr, sizeof(ipaddr), AF_INET);
			if (!hostinfo) {
				bb_herror_msg_and_die("can't find hostname for %s", argv[0]);
			}
			str_tolower(hostinfo->h_name);

			printf("HOSTNAME=%s\n", hostinfo->h_name);
		}
	}

	return EXIT_SUCCESS;
}
