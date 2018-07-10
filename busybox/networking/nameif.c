/* vi: set sw=4 ts=4: */
/*
 * nameif.c - Naming Interfaces based on MAC address for busybox.
 *
 * Written 2000 by Andi Kleen.
 * Busybox port 2002 by Nick Fedchik <nick@fedchik.org.ua>
 *			Glenn McGrath
 * Extended matching support 2008 by Nico Erfurth <masta@perlgolf.de>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config NAMEIF
//config:	bool "nameif (6.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	select FEATURE_SYSLOG
//config:	help
//config:	nameif is used to rename network interface by its MAC address.
//config:	Renamed interfaces MUST be in the down state.
//config:	It is possible to use a file (default: /etc/mactab)
//config:	with list of new interface names and MACs.
//config:	Maximum interface name length: IFNAMSIZ = 16
//config:	File fields are separated by space or tab.
//config:	File format:
//config:		# Comment
//config:		new_interface_name  XX:XX:XX:XX:XX:XX
//config:
//config:config FEATURE_NAMEIF_EXTENDED
//config:	bool "Extended nameif"
//config:	default y
//config:	depends on NAMEIF
//config:	help
//config:	This extends the nameif syntax to support the bus_info, driver,
//config:	phyaddr selectors. The syntax is compatible to the normal nameif.
//config:	File format:
//config:		new_interface_name  driver=asix bus=usb-0000:00:08.2-3
//config:		new_interface_name  bus=usb-0000:00:08.2-3 00:80:C8:38:91:B5
//config:		new_interface_name  phy_address=2 00:80:C8:38:91:B5
//config:		new_interface_name  mac=00:80:C8:38:91:B5
//config:		new_interface_name  00:80:C8:38:91:B5

//applet:IF_NAMEIF(APPLET_NOEXEC(nameif, nameif, BB_DIR_SBIN, BB_SUID_DROP, nameif))

//kbuild:lib-$(CONFIG_NAMEIF) += nameif.o

//usage:#define nameif_trivial_usage
//usage:	IF_NOT_FEATURE_NAMEIF_EXTENDED(
//usage:		"[-s] [-c FILE] [IFNAME HWADDR]..."
//usage:	)
//usage:	IF_FEATURE_NAMEIF_EXTENDED(
//usage:		"[-s] [-c FILE] [IFNAME SELECTOR]..."
//usage:	)
//usage:#define nameif_full_usage "\n\n"
//usage:	"Rename network interface while it in the down state."
//usage:	IF_NOT_FEATURE_NAMEIF_EXTENDED(
//usage:     "\nThe device with address HWADDR is renamed to IFACE."
//usage:	)
//usage:	IF_FEATURE_NAMEIF_EXTENDED(
//usage:     "\nThe device matched by SELECTOR is renamed to IFACE."
//usage:     "\nSELECTOR can be a combination of:"
//usage:     "\n	driver=STRING"
//usage:     "\n	bus=STRING"
//usage:     "\n	phy_address=NUM"
//usage:     "\n	[mac=]XX:XX:XX:XX:XX:XX"
//usage:	)
//usage:     "\n"
//usage:     "\n	-c FILE	Configuration file (default: /etc/mactab)"
//usage:     "\n	-s	Log to syslog"
//usage:
//usage:#define nameif_example_usage
//usage:       "$ nameif -s dmz0 00:A0:C9:8C:F6:3F\n"
//usage:       " or\n"
//usage:       "$ nameif -c /etc/my_mactab_file\n"

#include "libbb.h"
#include <syslog.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <linux/sockios.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

/* Taken from linux/sockios.h */
#define SIOCSIFNAME  0x8923  /* set interface name */

/* Octets in one Ethernet addr, from <linux/if_ether.h> */
#define ETH_ALEN     6

#ifndef ifr_newname
#define ifr_newname ifr_ifru.ifru_slave
#endif

typedef struct ethtable_s {
	struct ethtable_s *next;
	struct ethtable_s *prev;
	char *ifname;
	struct ether_addr *mac;
#if ENABLE_FEATURE_NAMEIF_EXTENDED
	char *bus_info;
	char *driver;
	int32_t phy_address;
#endif
} ethtable_t;

#if ENABLE_FEATURE_NAMEIF_EXTENDED
/* Cut'n'paste from ethtool.h */
#define ETHTOOL_BUSINFO_LEN 32
/* these strings are set to whatever the driver author decides... */
struct ethtool_drvinfo {
	uint32_t cmd;
	char  driver[32]; /* driver short name, "tulip", "eepro100" */
	char  version[32];  /* driver version string */
	char  fw_version[32]; /* firmware version string, if applicable */
	char  bus_info[ETHTOOL_BUSINFO_LEN];  /* Bus info for this IF. */
	/* For PCI devices, use pci_dev->slot_name. */
	char  reserved1[32];
	char  reserved2[16];
	uint32_t n_stats;  /* number of u64's from ETHTOOL_GSTATS */
	uint32_t testinfo_len;
	uint32_t eedump_len; /* Size of data from ETHTOOL_GEEPROM (bytes) */
	uint32_t regdump_len;  /* Size of data from ETHTOOL_GREGS (bytes) */
};

struct ethtool_cmd {
	uint32_t   cmd;
	uint32_t   supported;      /* Features this interface supports */
	uint32_t   advertising;    /* Features this interface advertises */
	uint16_t   speed;          /* The forced speed, 10Mb, 100Mb, gigabit */
	uint8_t    duplex;         /* Duplex, half or full */
	uint8_t    port;           /* Which connector port */
	uint8_t    phy_address;
	uint8_t    transceiver;    /* Which transceiver to use */
	uint8_t    autoneg;        /* Enable or disable autonegotiation */
	uint32_t   maxtxpkt;       /* Tx pkts before generating tx int */
	uint32_t   maxrxpkt;       /* Rx pkts before generating rx int */
	uint16_t   speed_hi;
	uint16_t   reserved2;
	uint32_t   reserved[3];
};

#define ETHTOOL_GSET      0x00000001 /* Get settings. */
#define ETHTOOL_GDRVINFO  0x00000003 /* Get driver info. */
#endif


static void nameif_parse_selector(ethtable_t *ch, char *selector)
{
	struct ether_addr *lmac;
#if ENABLE_FEATURE_NAMEIF_EXTENDED
	int found_selector = 0;

	while (*selector) {
		char *next;
#endif
		selector = skip_whitespace(selector);
#if ENABLE_FEATURE_NAMEIF_EXTENDED
		ch->phy_address = -1;
		if (*selector == '\0')
			break;
		/* Search for the end .... */
		next = skip_non_whitespace(selector);
		if (*next)
			*next++ = '\0';
		/* Check for selectors, mac= is assumed */
		if (is_prefixed_with(selector, "bus=")) {
			ch->bus_info = xstrdup(selector + 4);
			found_selector++;
		} else if (is_prefixed_with(selector, "driver=")) {
			ch->driver = xstrdup(selector + 7);
			found_selector++;
		} else if (is_prefixed_with(selector, "phyaddr=")) {
			ch->phy_address = xatoi_positive(selector + 8);
			found_selector++;
		} else {
#endif
			lmac = xmalloc(ETH_ALEN);
			ch->mac = ether_aton_r(selector + (is_prefixed_with(selector, "mac=") ? 4 : 0), lmac);
			if (ch->mac == NULL)
				bb_error_msg_and_die("can't parse %s", selector);
#if  ENABLE_FEATURE_NAMEIF_EXTENDED
			found_selector++;
		};
		selector = next;
	}
	if (found_selector == 0)
		bb_error_msg_and_die("no selectors found for %s", ch->ifname);
#endif
}

static void prepend_new_eth_table(ethtable_t **clist, char *ifname, char *selector)
{
	ethtable_t *ch;
	if (strlen(ifname) >= IFNAMSIZ)
		bb_error_msg_and_die("interface name '%s' too long", ifname);
	ch = xzalloc(sizeof(*ch));
	ch->ifname = xstrdup(ifname);
	nameif_parse_selector(ch, selector);
	ch->next = *clist;
	if (*clist)
		(*clist)->prev = ch;
	*clist = ch;
}

#if ENABLE_FEATURE_CLEAN_UP
static void delete_eth_table(ethtable_t *ch)
{
	free(ch->ifname);
#if ENABLE_FEATURE_NAMEIF_EXTENDED
	free(ch->bus_info);
	free(ch->driver);
#endif
	free(ch->mac);
	free(ch);
};
#else
void delete_eth_table(ethtable_t *ch);
#endif

int nameif_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int nameif_main(int argc UNUSED_PARAM, char **argv)
{
	ethtable_t *clist = NULL;
	const char *fname = "/etc/mactab";
	int ctl_sk;
	ethtable_t *ch;
	parser_t *parser;
	char *token[2];

	if (1 & getopt32(argv, "sc:", &fname)) {
		openlog(applet_name, 0, LOG_LOCAL0);
		/* Why not just "="? I assume logging to stderr
		 * can't hurt. 2>/dev/null if you don't like it: */
		logmode |= LOGMODE_SYSLOG;
	}
	argv += optind;

	if (argv[0]) {
		do {
			if (!argv[1])
				bb_show_usage();
			prepend_new_eth_table(&clist, argv[0], argv[1]);
			argv += 2;
		} while (*argv);
	} else {
		parser = config_open(fname);
		while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL))
			prepend_new_eth_table(&clist, token[0], token[1]);
		config_close(parser);
	}

	ctl_sk = xsocket(PF_INET, SOCK_DGRAM, 0);
	parser = config_open2("/proc/net/dev", xfopen_for_read);

	while (clist && config_read(parser, token, 2, 2, "\0: \t", PARSE_NORMAL)) {
		struct ifreq ifr;
#if  ENABLE_FEATURE_NAMEIF_EXTENDED
		struct ethtool_drvinfo drvinfo;
		struct ethtool_cmd eth_settings;
#endif
		if (parser->lineno <= 2)
			continue; /* Skip the first two lines */

		/* Find the current interface name and copy it to ifr.ifr_name */
		memset(&ifr, 0, sizeof(struct ifreq));
		strncpy_IFNAMSIZ(ifr.ifr_name, token[0]);

#if ENABLE_FEATURE_NAMEIF_EXTENDED
		/* Check for phy address */
		memset(&eth_settings, 0, sizeof(eth_settings));
		eth_settings.cmd = ETHTOOL_GSET;
		ifr.ifr_data = (caddr_t) &eth_settings;
		ioctl(ctl_sk, SIOCETHTOOL, &ifr);

		/* Check for driver etc. */
		memset(&drvinfo, 0, sizeof(drvinfo));
		drvinfo.cmd = ETHTOOL_GDRVINFO;
		ifr.ifr_data = (caddr_t) &drvinfo;
		/* Get driver and businfo first, so we have it in drvinfo */
		ioctl(ctl_sk, SIOCETHTOOL, &ifr);
#endif
		ioctl(ctl_sk, SIOCGIFHWADDR, &ifr);

		/* Search the list for a matching device */
		for (ch = clist; ch; ch = ch->next) {
#if ENABLE_FEATURE_NAMEIF_EXTENDED
			if (ch->bus_info && strcmp(ch->bus_info, drvinfo.bus_info) != 0)
				continue;
			if (ch->driver && strcmp(ch->driver, drvinfo.driver) != 0)
				continue;
			if (ch->phy_address != -1 && ch->phy_address != eth_settings.phy_address)
				continue;
#endif
			if (ch->mac && memcmp(ch->mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN) != 0)
				continue;
			/* if we came here, all selectors have matched */
			goto found;
		}
		/* Nothing found for current interface */
		continue;
 found:
		if (strcmp(ifr.ifr_name, ch->ifname) != 0) {
			strcpy(ifr.ifr_newname, ch->ifname);
			ioctl_or_perror_and_die(ctl_sk, SIOCSIFNAME, &ifr,
					"can't change ifname %s to %s",
					ifr.ifr_name, ch->ifname);
		}
		/* Remove list entry of renamed interface */
		if (ch->prev != NULL)
			ch->prev->next = ch->next;
		else
			clist = ch->next;
		if (ch->next != NULL)
			ch->next->prev = ch->prev;
		if (ENABLE_FEATURE_CLEAN_UP)
			delete_eth_table(ch);
	} /* while */

	if (ENABLE_FEATURE_CLEAN_UP) {
		ethtable_t *next;
		for (ch = clist; ch; ch = next) {
			next = ch->next;
			delete_eth_table(ch);
		}
		config_close(parser);
	};

	return 0;
}
