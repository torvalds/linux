/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#ifndef UDHCP_DHCPD_H
#define UDHCP_DHCPD_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

/* Defaults you may want to tweak */
/* Default max_lease_sec */
#define DEFAULT_LEASE_TIME      (60*60*24 * 10)
#define LEASES_FILE             CONFIG_DHCPD_LEASES_FILE
/* Where to find the DHCP server configuration file */
#define DHCPD_CONF_FILE         "/etc/udhcpd.conf"


struct static_lease {
	struct static_lease *next;
	uint32_t nip;
	uint8_t mac[6];
};

struct server_config_t {
	char *interface;                /* interface to use */
//TODO: ifindex, server_nip, server_mac
// are obtained from interface name.
// Instead of querying them *once*, create update_server_network_data_cache()
// and call it before any usage of these fields.
// update_server_network_data_cache() must re-query data
// if more than N seconds have passed after last use.
	int ifindex;
	uint32_t server_nip;
#if ENABLE_FEATURE_UDHCP_PORT
	uint16_t port;
#endif
	uint8_t server_mac[6];          /* our MAC address (used only for ARP probing) */
	struct option_set *options;     /* list of DHCP options loaded from the config file */
	/* start,end are in host order: we need to compare start <= ip <= end */
	uint32_t start_ip;              /* start address of leases, in host order */
	uint32_t end_ip;                /* end of leases, in host order */
	uint32_t max_lease_sec;         /* maximum lease time (host order) */
	uint32_t min_lease_sec;         /* minimum lease time a client can request */
	uint32_t max_leases;            /* maximum number of leases (including reserved addresses) */
	uint32_t auto_time;             /* how long should udhcpd wait before writing a config file.
	                                 * if this is zero, it will only write one on SIGUSR1 */
	uint32_t decline_time;          /* how long an address is reserved if a client returns a
	                                 * decline message */
	uint32_t conflict_time;         /* how long an arp conflict offender is leased for */
	uint32_t offer_time;            /* how long an offered address is reserved */
	uint32_t siaddr_nip;            /* "next server" bootp option */
	char *lease_file;
	char *pidfile;
	char *notify_file;              /* what to run whenever leases are written */
	char *sname;                    /* bootp server name */
	char *boot_file;                /* bootp boot file option */
	struct static_lease *static_leases; /* List of ip/mac pairs to assign static leases */
} FIX_ALIASING;

#define server_config (*(struct server_config_t*)bb_common_bufsiz1)
/* client_config sits in 2nd half of bb_common_bufsiz1 */

#if ENABLE_FEATURE_UDHCP_PORT
#define SERVER_PORT  (server_config.port)
#define SERVER_PORT6 (server_config.port)
#else
#define SERVER_PORT  67
#define SERVER_PORT6 547
#endif


typedef uint32_t leasetime_t;
typedef int32_t signed_leasetime_t;

struct dyn_lease {
	/* Unix time when lease expires. Kept in memory in host order.
	 * When written to file, converted to network order
	 * and adjusted (current time subtracted) */
	leasetime_t expires;
	/* "nip": IP in network order */
	uint32_t lease_nip;
	/* We use lease_mac[6], since e.g. ARP probing uses
	 * only 6 first bytes anyway. We check received dhcp packets
	 * that their hlen == 6 and thus chaddr has only 6 significant bytes
	 * (dhcp packet has chaddr[16], not [6])
	 */
	uint8_t lease_mac[6];
	char hostname[20];
	uint8_t pad[2];
	/* total size is a multiply of 4 */
} PACKED;

POP_SAVED_FUNCTION_VISIBILITY

#endif
