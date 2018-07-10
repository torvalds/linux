/* vi: set sw=4 ts=4: */
/*
 * udhcp server
 * Copyright (C) 1999 Matthew Ramsay <matthewr@moreton.com.au>
 *			Chris Trew <ctrew@moreton.com.au>
 *
 * Rewrite by Russ Dill <Russ.Dill@asu.edu> July 2001
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
//applet:IF_UDHCPD(APPLET(udhcpd, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_UDHCPD) += common.o packet.o signalpipe.o socket.o
//kbuild:lib-$(CONFIG_UDHCPD) += dhcpd.o arpping.o
//kbuild:lib-$(CONFIG_FEATURE_UDHCP_RFC3397) += domain_codec.o

//usage:#define udhcpd_trivial_usage
//usage:       "[-fS] [-I ADDR]" IF_FEATURE_UDHCP_PORT(" [-P N]") " [CONFFILE]"
//usage:#define udhcpd_full_usage "\n\n"
//usage:       "DHCP server\n"
//usage:     "\n	-f	Run in foreground"
//usage:     "\n	-S	Log to syslog too"
//usage:     "\n	-I ADDR	Local address"
//usage:     "\n	-a MSEC	Timeout for ARP ping (default 2000)"
//usage:	IF_FEATURE_UDHCP_PORT(
//usage:     "\n	-P N	Use port N (default 67)"
//usage:	)

#include <netinet/ether.h>
#include <syslog.h>
#include "common.h"
#include "dhcpc.h"
#include "dhcpd.h"

/* globals */
#define g_leases ((struct dyn_lease*)ptr_to_globals)
/* struct server_config_t server_config is in bb_common_bufsiz1 */

/* Takes the address of the pointer to the static_leases linked list,
 * address to a 6 byte mac address,
 * 4 byte IP address */
static void add_static_lease(struct static_lease **st_lease_pp,
		uint8_t *mac,
		uint32_t nip)
{
	struct static_lease *st_lease;

	/* Find the tail of the list */
	while ((st_lease = *st_lease_pp) != NULL) {
		st_lease_pp = &st_lease->next;
	}

	/* Add new node */
	*st_lease_pp = st_lease = xzalloc(sizeof(*st_lease));
	memcpy(st_lease->mac, mac, 6);
	st_lease->nip = nip;
	/*st_lease->next = NULL;*/
}

/* Find static lease IP by mac */
static uint32_t get_static_nip_by_mac(struct static_lease *st_lease, void *mac)
{
	while (st_lease) {
		if (memcmp(st_lease->mac, mac, 6) == 0)
			return st_lease->nip;
		st_lease = st_lease->next;
	}

	return 0;
}

static int is_nip_reserved(struct static_lease *st_lease, uint32_t nip)
{
	while (st_lease) {
		if (st_lease->nip == nip)
			return 1;
		st_lease = st_lease->next;
	}

	return 0;
}

#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 2
/* Print out static leases just to check what's going on */
/* Takes the address of the pointer to the static_leases linked list */
static void log_static_leases(struct static_lease **st_lease_pp)
{
	struct static_lease *cur;

	if (dhcp_verbose < 2)
		return;

	cur = *st_lease_pp;
	while (cur) {
		bb_error_msg("static lease: mac:%02x:%02x:%02x:%02x:%02x:%02x nip:%x",
			cur->mac[0], cur->mac[1], cur->mac[2],
			cur->mac[3], cur->mac[4], cur->mac[5],
			cur->nip
		);
		cur = cur->next;
	}
}
#else
# define log_static_leases(st_lease_pp) ((void)0)
#endif

/* Find the oldest expired lease, NULL if there are no expired leases */
static struct dyn_lease *oldest_expired_lease(void)
{
	struct dyn_lease *oldest_lease = NULL;
	leasetime_t oldest_time = time(NULL);
	unsigned i;

	/* Unexpired leases have g_leases[i].expires >= current time
	 * and therefore can't ever match */
	for (i = 0; i < server_config.max_leases; i++) {
		if (g_leases[i].expires == 0 /* empty entry */
		 || g_leases[i].expires < oldest_time
		) {
			oldest_time = g_leases[i].expires;
			oldest_lease = &g_leases[i];
		}
	}
	return oldest_lease;
}

/* Clear out all leases with matching nonzero chaddr OR yiaddr.
 * If chaddr == NULL, this is a conflict lease.
 */
static void clear_leases(const uint8_t *chaddr, uint32_t yiaddr)
{
	unsigned i;

	for (i = 0; i < server_config.max_leases; i++) {
		if ((chaddr && memcmp(g_leases[i].lease_mac, chaddr, 6) == 0)
		 || (yiaddr && g_leases[i].lease_nip == yiaddr)
		) {
			memset(&g_leases[i], 0, sizeof(g_leases[i]));
		}
	}
}

/* Add a lease into the table, clearing out any old ones.
 * If chaddr == NULL, this is a conflict lease.
 */
static struct dyn_lease *add_lease(
		const uint8_t *chaddr, uint32_t yiaddr,
		leasetime_t leasetime,
		const char *hostname, int hostname_len)
{
	struct dyn_lease *oldest;

	/* clean out any old ones */
	clear_leases(chaddr, yiaddr);

	oldest = oldest_expired_lease();

	if (oldest) {
		memset(oldest, 0, sizeof(*oldest));
		if (hostname) {
			char *p;

			hostname_len++; /* include NUL */
			if (hostname_len > sizeof(oldest->hostname))
				hostname_len = sizeof(oldest->hostname);
			p = safe_strncpy(oldest->hostname, hostname, hostname_len);
			/*
			 * Sanitization (s/bad_char/./g).
			 * The intent is not to allow only "DNS-valid" hostnames,
			 * but merely make dumpleases output safe for shells to use.
			 * We accept "0-9A-Za-z._-", all other chars turn to dots.
			 */
			while (*p) {
				if (!isalnum(*p) && *p != '-' && *p != '_')
					*p = '.';
				p++;
			}
		}
		if (chaddr)
			memcpy(oldest->lease_mac, chaddr, 6);
		oldest->lease_nip = yiaddr;
		oldest->expires = time(NULL) + leasetime;
	}

	return oldest;
}

/* True if a lease has expired */
static int is_expired_lease(struct dyn_lease *lease)
{
	return (lease->expires < (leasetime_t) time(NULL));
}

/* Find the first lease that matches MAC, NULL if no match */
static struct dyn_lease *find_lease_by_mac(const uint8_t *mac)
{
	unsigned i;

	for (i = 0; i < server_config.max_leases; i++)
		if (memcmp(g_leases[i].lease_mac, mac, 6) == 0)
			return &g_leases[i];

	return NULL;
}

/* Find the first lease that matches IP, NULL is no match */
static struct dyn_lease *find_lease_by_nip(uint32_t nip)
{
	unsigned i;

	for (i = 0; i < server_config.max_leases; i++)
		if (g_leases[i].lease_nip == nip)
			return &g_leases[i];

	return NULL;
}

/* Check if the IP is taken; if it is, add it to the lease table */
static int nobody_responds_to_arp(uint32_t nip, const uint8_t *safe_mac, unsigned arpping_ms)
{
	struct in_addr temp;
	int r;

	r = arpping(nip, safe_mac,
			server_config.server_nip,
			server_config.server_mac,
			server_config.interface,
			arpping_ms);
	if (r)
		return r;

	temp.s_addr = nip;
	bb_error_msg("%s belongs to someone, reserving it for %u seconds",
		inet_ntoa(temp), (unsigned)server_config.conflict_time);
	add_lease(NULL, nip, server_config.conflict_time, NULL, 0);
	return 0;
}

/* Find a new usable (we think) address */
static uint32_t find_free_or_expired_nip(const uint8_t *safe_mac, unsigned arpping_ms)
{
	uint32_t addr;
	struct dyn_lease *oldest_lease = NULL;

#if ENABLE_FEATURE_UDHCPD_BASE_IP_ON_MAC
	uint32_t stop;
	unsigned i, hash;

	/* hash hwaddr: use the SDBM hashing algorithm.  Seems to give good
	 * dispersal even with similarly-valued "strings".
	 */
	hash = 0;
	for (i = 0; i < 6; i++)
		hash += safe_mac[i] + (hash << 6) + (hash << 16) - hash;

	/* pick a seed based on hwaddr then iterate until we find a free address. */
	addr = server_config.start_ip
		+ (hash % (1 + server_config.end_ip - server_config.start_ip));
	stop = addr;
#else
	addr = server_config.start_ip;
#define stop (server_config.end_ip + 1)
#endif
	do {
		uint32_t nip;
		struct dyn_lease *lease;

		/* ie, 192.168.55.0 */
		if ((addr & 0xff) == 0)
			goto next_addr;
		/* ie, 192.168.55.255 */
		if ((addr & 0xff) == 0xff)
			goto next_addr;
		nip = htonl(addr);
		/* skip our own address */
		if (nip == server_config.server_nip)
			goto next_addr;
		/* is this a static lease addr? */
		if (is_nip_reserved(server_config.static_leases, nip))
			goto next_addr;

		lease = find_lease_by_nip(nip);
		if (!lease) {
//TODO: DHCP servers do not always sit on the same subnet as clients: should *ping*, not arp-ping!
			if (nobody_responds_to_arp(nip, safe_mac, arpping_ms))
				return nip;
		} else {
			if (!oldest_lease || lease->expires < oldest_lease->expires)
				oldest_lease = lease;
		}

 next_addr:
		addr++;
#if ENABLE_FEATURE_UDHCPD_BASE_IP_ON_MAC
		if (addr > server_config.end_ip)
			addr = server_config.start_ip;
#endif
	} while (addr != stop);

	if (oldest_lease
	 && is_expired_lease(oldest_lease)
	 && nobody_responds_to_arp(oldest_lease->lease_nip, safe_mac, arpping_ms)
	) {
		return oldest_lease->lease_nip;
	}

	return 0;
}

/* On these functions, make sure your datatype matches */
static int FAST_FUNC read_str(const char *line, void *arg)
{
	char **dest = arg;

	free(*dest);
	*dest = xstrdup(line);
	return 1;
}

static int FAST_FUNC read_u32(const char *line, void *arg)
{
	*(uint32_t*)arg = bb_strtou32(line, NULL, 10);
	return errno == 0;
}

static int FAST_FUNC read_staticlease(const char *const_line, void *arg)
{
	char *line;
	char *mac_string;
	char *ip_string;
	struct ether_addr mac_bytes; /* it's "struct { uint8_t mac[6]; }" */
	uint32_t nip;

	/* Read mac */
	line = (char *) const_line;
	mac_string = strtok_r(line, " \t", &line);
	if (!mac_string || !ether_aton_r(mac_string, &mac_bytes))
		return 0;

	/* Read ip */
	ip_string = strtok_r(NULL, " \t", &line);
	if (!ip_string || !udhcp_str2nip(ip_string, &nip))
		return 0;

	add_static_lease(arg, (uint8_t*) &mac_bytes, nip);

	log_static_leases(arg);

	return 1;
}

static int FAST_FUNC read_optset(const char *line, void *arg) {
	return udhcp_str2optset(line, arg,
			dhcp_optflags, dhcp_option_strings,
			/*dhcpv6:*/ 0
	);
}

struct config_keyword {
	const char *keyword;
	int (*handler)(const char *line, void *var) FAST_FUNC;
	unsigned ofs;
	const char *def;
};

#define OFS(field) offsetof(struct server_config_t, field)

static const struct config_keyword keywords[] = {
	/* keyword        handler           variable address               default */
	{"start"        , udhcp_str2nip   , OFS(start_ip     ), "192.168.0.20"},
	{"end"          , udhcp_str2nip   , OFS(end_ip       ), "192.168.0.254"},
	{"interface"    , read_str        , OFS(interface    ), "eth0"},
	/* Avoid "max_leases value not sane" warning by setting default
	 * to default_end_ip - default_start_ip + 1: */
	{"max_leases"   , read_u32        , OFS(max_leases   ), "235"},
	{"auto_time"    , read_u32        , OFS(auto_time    ), "7200"},
	{"decline_time" , read_u32        , OFS(decline_time ), "3600"},
	{"conflict_time", read_u32        , OFS(conflict_time), "3600"},
	{"offer_time"   , read_u32        , OFS(offer_time   ), "60"},
	{"min_lease"    , read_u32        , OFS(min_lease_sec), "60"},
	{"lease_file"   , read_str        , OFS(lease_file   ), LEASES_FILE},
	{"pidfile"      , read_str        , OFS(pidfile      ), "/var/run/udhcpd.pid"},
	{"siaddr"       , udhcp_str2nip   , OFS(siaddr_nip   ), "0.0.0.0"},
	/* keywords with no defaults must be last! */
	{"option"       , read_optset     , OFS(options      ), ""},
	{"opt"          , read_optset     , OFS(options      ), ""},
	{"notify_file"  , read_str        , OFS(notify_file  ), NULL},
	{"sname"        , read_str        , OFS(sname        ), NULL},
	{"boot_file"    , read_str        , OFS(boot_file    ), NULL},
	{"static_lease" , read_staticlease, OFS(static_leases), ""},
};
enum { KWS_WITH_DEFAULTS = ARRAY_SIZE(keywords) - 6 };

static NOINLINE void read_config(const char *file)
{
	parser_t *parser;
	const struct config_keyword *k;
	unsigned i;
	char *token[2];

	for (i = 0; i < KWS_WITH_DEFAULTS; i++)
		keywords[i].handler(keywords[i].def, (char*)&server_config + keywords[i].ofs);

	parser = config_open(file);
	while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL)) {
		for (k = keywords, i = 0; i < ARRAY_SIZE(keywords); k++, i++) {
			if (strcasecmp(token[0], k->keyword) == 0) {
				if (!k->handler(token[1], (char*)&server_config + k->ofs)) {
					bb_error_msg("can't parse line %u in %s",
							parser->lineno, file);
					/* reset back to the default value */
					k->handler(k->def, (char*)&server_config + k->ofs);
				}
				break;
			}
		}
	}
	config_close(parser);

	server_config.start_ip = ntohl(server_config.start_ip);
	server_config.end_ip = ntohl(server_config.end_ip);
}

static void write_leases(void)
{
	int fd;
	unsigned i;
	leasetime_t curr;
	int64_t written_at;

	fd = open_or_warn(server_config.lease_file, O_WRONLY|O_CREAT|O_TRUNC);
	if (fd < 0)
		return;

	curr = written_at = time(NULL);

	written_at = SWAP_BE64(written_at);
	full_write(fd, &written_at, sizeof(written_at));

	for (i = 0; i < server_config.max_leases; i++) {
		leasetime_t tmp_time;

		if (g_leases[i].lease_nip == 0)
			continue;

		/* Screw with the time in the struct, for easier writing */
		tmp_time = g_leases[i].expires;

		g_leases[i].expires -= curr;
		if ((signed_leasetime_t) g_leases[i].expires < 0)
			g_leases[i].expires = 0;
		g_leases[i].expires = htonl(g_leases[i].expires);

		/* No error check. If the file gets truncated,
		 * we lose some leases on restart. Oh well. */
		full_write(fd, &g_leases[i], sizeof(g_leases[i]));

		/* Then restore it when done */
		g_leases[i].expires = tmp_time;
	}
	close(fd);

	if (server_config.notify_file) {
		char *argv[3];
		argv[0] = server_config.notify_file;
		argv[1] = server_config.lease_file;
		argv[2] = NULL;
		spawn_and_wait(argv);
	}
}

static NOINLINE void read_leases(const char *file)
{
	struct dyn_lease lease;
	int64_t written_at, time_passed;
	int fd;
#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 1
	unsigned i = 0;
#endif

	fd = open_or_warn(file, O_RDONLY);
	if (fd < 0)
		return;

	if (full_read(fd, &written_at, sizeof(written_at)) != sizeof(written_at))
		goto ret;
	written_at = SWAP_BE64(written_at);

	time_passed = time(NULL) - written_at;
	/* Strange written_at, or lease file from old version of udhcpd
	 * which had no "written_at" field? */
	if ((uint64_t)time_passed > 12 * 60 * 60)
		goto ret;

	while (full_read(fd, &lease, sizeof(lease)) == sizeof(lease)) {
		uint32_t y = ntohl(lease.lease_nip);
		if (y >= server_config.start_ip && y <= server_config.end_ip) {
			signed_leasetime_t expires = ntohl(lease.expires) - (signed_leasetime_t)time_passed;
			uint32_t static_nip;

			if (expires <= 0)
				/* We keep expired leases: add_lease() will add
				 * a lease with 0 seconds remaining.
				 * Fewer IP address changes this way for mass reboot scenario.
				 */
				expires = 0;

			/* Check if there is a different static lease for this IP or MAC */
			static_nip = get_static_nip_by_mac(server_config.static_leases, lease.lease_mac);
			if (static_nip) {
				/* NB: we do not add lease even if static_nip == lease.lease_nip.
				 */
				continue;
			}
			if (is_nip_reserved(server_config.static_leases, lease.lease_nip))
				continue;

			/* NB: add_lease takes "relative time", IOW,
			 * lease duration, not lease deadline. */
			if (add_lease(lease.lease_mac, lease.lease_nip,
					expires,
					lease.hostname, sizeof(lease.hostname)
				) == 0
			) {
				bb_error_msg("too many leases while loading %s", file);
				break;
			}
#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 1
			i++;
#endif
		}
	}
	log1("read %d leases", i);
 ret:
	close(fd);
}

/* Send a packet to a specific mac address and ip address by creating our own ip packet */
static void send_packet_to_client(struct dhcp_packet *dhcp_pkt, int force_broadcast)
{
	const uint8_t *chaddr;
	uint32_t ciaddr;

	// Was:
	//if (force_broadcast) { /* broadcast */ }
	//else if (dhcp_pkt->ciaddr) { /* unicast to dhcp_pkt->ciaddr */ }
	//else if (dhcp_pkt->flags & htons(BROADCAST_FLAG)) { /* broadcast */ }
	//else { /* unicast to dhcp_pkt->yiaddr */ }
	// But this is wrong: yiaddr is _our_ idea what client's IP is
	// (for example, from lease file). Client may not know that,
	// and may not have UDP socket listening on that IP!
	// We should never unicast to dhcp_pkt->yiaddr!
	// dhcp_pkt->ciaddr, OTOH, comes from client's request packet,
	// and can be used.

	if (force_broadcast
	 || (dhcp_pkt->flags & htons(BROADCAST_FLAG))
	 || dhcp_pkt->ciaddr == 0
	) {
		log1("broadcasting packet to client");
		ciaddr = INADDR_BROADCAST;
		chaddr = MAC_BCAST_ADDR;
	} else {
		log1("unicasting packet to client ciaddr");
		ciaddr = dhcp_pkt->ciaddr;
		chaddr = dhcp_pkt->chaddr;
	}

	udhcp_send_raw_packet(dhcp_pkt,
		/*src*/ server_config.server_nip, SERVER_PORT,
		/*dst*/ ciaddr, CLIENT_PORT, chaddr,
		server_config.ifindex);
}

/* Send a packet to gateway_nip using the kernel ip stack */
static void send_packet_to_relay(struct dhcp_packet *dhcp_pkt)
{
	log1("forwarding packet to relay");

	udhcp_send_kernel_packet(dhcp_pkt,
			server_config.server_nip, SERVER_PORT,
			dhcp_pkt->gateway_nip, SERVER_PORT);
}

static void send_packet(struct dhcp_packet *dhcp_pkt, int force_broadcast)
{
	if (dhcp_pkt->gateway_nip)
		send_packet_to_relay(dhcp_pkt);
	else
		send_packet_to_client(dhcp_pkt, force_broadcast);
}

static void init_packet(struct dhcp_packet *packet, struct dhcp_packet *oldpacket, char type)
{
	/* Sets op, htype, hlen, cookie fields
	 * and adds DHCP_MESSAGE_TYPE option */
	udhcp_init_header(packet, type);

	packet->xid = oldpacket->xid;
	memcpy(packet->chaddr, oldpacket->chaddr, sizeof(oldpacket->chaddr));
	packet->flags = oldpacket->flags;
	packet->gateway_nip = oldpacket->gateway_nip;
	packet->ciaddr = oldpacket->ciaddr;
	udhcp_add_simple_option(packet, DHCP_SERVER_ID, server_config.server_nip);
}

/* Fill options field, siaddr_nip, and sname and boot_file fields.
 * TODO: teach this code to use overload option.
 */
static void add_server_options(struct dhcp_packet *packet)
{
	struct option_set *curr = server_config.options;

	while (curr) {
		if (curr->data[OPT_CODE] != DHCP_LEASE_TIME)
			udhcp_add_binary_option(packet, curr->data);
		curr = curr->next;
	}

	packet->siaddr_nip = server_config.siaddr_nip;

	if (server_config.sname)
		strncpy((char*)packet->sname, server_config.sname, sizeof(packet->sname) - 1);
	if (server_config.boot_file)
		strncpy((char*)packet->file, server_config.boot_file, sizeof(packet->file) - 1);
}

static uint32_t select_lease_time(struct dhcp_packet *packet)
{
	uint32_t lease_time_sec = server_config.max_lease_sec;
	uint8_t *lease_time_opt = udhcp_get_option(packet, DHCP_LEASE_TIME);
	if (lease_time_opt) {
		move_from_unaligned32(lease_time_sec, lease_time_opt);
		lease_time_sec = ntohl(lease_time_sec);
		if (lease_time_sec > server_config.max_lease_sec)
			lease_time_sec = server_config.max_lease_sec;
		if (lease_time_sec < server_config.min_lease_sec)
			lease_time_sec = server_config.min_lease_sec;
	}
	return lease_time_sec;
}

/* We got a DHCP DISCOVER. Send an OFFER. */
/* NOINLINE: limit stack usage in caller */
static NOINLINE void send_offer(struct dhcp_packet *oldpacket,
		uint32_t static_lease_nip,
		struct dyn_lease *lease,
		uint8_t *requested_ip_opt,
		unsigned arpping_ms)
{
	struct dhcp_packet packet;
	uint32_t lease_time_sec;
	struct in_addr addr;

	init_packet(&packet, oldpacket, DHCPOFFER);

	/* If it is a static lease, use its IP */
	packet.yiaddr = static_lease_nip;
	/* Else: */
	if (!static_lease_nip) {
		/* We have no static lease for client's chaddr */
		uint32_t req_nip;
		const char *p_host_name;

		if (lease) {
			/* We have a dynamic lease for client's chaddr.
			 * Reuse its IP (even if lease is expired).
			 * Note that we ignore requested IP in this case.
			 */
			packet.yiaddr = lease->lease_nip;
		}
		/* Or: if client has requested an IP */
		else if (requested_ip_opt != NULL
		 /* (read IP) */
		 && (move_from_unaligned32(req_nip, requested_ip_opt), 1)
		 /* and the IP is in the lease range */
		 && ntohl(req_nip) >= server_config.start_ip
		 && ntohl(req_nip) <= server_config.end_ip
		 /* and */
		 && (  !(lease = find_lease_by_nip(req_nip)) /* is not already taken */
		    || is_expired_lease(lease) /* or is taken, but expired */
		    )
		) {
			packet.yiaddr = req_nip;
		}
		else {
			/* Otherwise, find a free IP */
			packet.yiaddr = find_free_or_expired_nip(oldpacket->chaddr, arpping_ms);
		}

		if (!packet.yiaddr) {
			bb_error_msg("no free IP addresses. OFFER abandoned");
			return;
		}
		/* Reserve the IP for a short time hoping to get DHCPREQUEST soon */
		p_host_name = (const char*) udhcp_get_option(oldpacket, DHCP_HOST_NAME);
		lease = add_lease(packet.chaddr, packet.yiaddr,
				server_config.offer_time,
				p_host_name,
				p_host_name ? (unsigned char)p_host_name[OPT_LEN - OPT_DATA] : 0
		);
		if (!lease) {
			bb_error_msg("no free IP addresses. OFFER abandoned");
			return;
		}
	}

	lease_time_sec = select_lease_time(oldpacket);
	udhcp_add_simple_option(&packet, DHCP_LEASE_TIME, htonl(lease_time_sec));
	add_server_options(&packet);

	addr.s_addr = packet.yiaddr;
	bb_error_msg("sending OFFER of %s", inet_ntoa(addr));
	/* send_packet emits error message itself if it detects failure */
	send_packet(&packet, /*force_bcast:*/ 0);
}

/* NOINLINE: limit stack usage in caller */
static NOINLINE void send_NAK(struct dhcp_packet *oldpacket)
{
	struct dhcp_packet packet;

	init_packet(&packet, oldpacket, DHCPNAK);

	log1("sending %s", "NAK");
	send_packet(&packet, /*force_bcast:*/ 1);
}

/* NOINLINE: limit stack usage in caller */
static NOINLINE void send_ACK(struct dhcp_packet *oldpacket, uint32_t yiaddr)
{
	struct dhcp_packet packet;
	uint32_t lease_time_sec;
	struct in_addr addr;
	const char *p_host_name;

	init_packet(&packet, oldpacket, DHCPACK);
	packet.yiaddr = yiaddr;

	lease_time_sec = select_lease_time(oldpacket);
	udhcp_add_simple_option(&packet, DHCP_LEASE_TIME, htonl(lease_time_sec));

	add_server_options(&packet);

	addr.s_addr = yiaddr;
	bb_error_msg("sending ACK to %s", inet_ntoa(addr));
	send_packet(&packet, /*force_bcast:*/ 0);

	p_host_name = (const char*) udhcp_get_option(oldpacket, DHCP_HOST_NAME);
	add_lease(packet.chaddr, packet.yiaddr,
		lease_time_sec,
		p_host_name,
		p_host_name ? (unsigned char)p_host_name[OPT_LEN - OPT_DATA] : 0
	);
	if (ENABLE_FEATURE_UDHCPD_WRITE_LEASES_EARLY) {
		/* rewrite the file with leases at every new acceptance */
		write_leases();
	}
}

/* NOINLINE: limit stack usage in caller */
static NOINLINE void send_inform(struct dhcp_packet *oldpacket)
{
	struct dhcp_packet packet;

	/* "If a client has obtained a network address through some other means
	 * (e.g., manual configuration), it may use a DHCPINFORM request message
	 * to obtain other local configuration parameters.  Servers receiving a
	 * DHCPINFORM message construct a DHCPACK message with any local
	 * configuration parameters appropriate for the client without:
	 * allocating a new address, checking for an existing binding, filling
	 * in 'yiaddr' or including lease time parameters.  The servers SHOULD
	 * unicast the DHCPACK reply to the address given in the 'ciaddr' field
	 * of the DHCPINFORM message.
	 * ...
	 * The server responds to a DHCPINFORM message by sending a DHCPACK
	 * message directly to the address given in the 'ciaddr' field
	 * of the DHCPINFORM message.  The server MUST NOT send a lease
	 * expiration time to the client and SHOULD NOT fill in 'yiaddr'."
	 */
//TODO: do a few sanity checks: is ciaddr set?
//Better yet: is ciaddr == IP source addr?
	init_packet(&packet, oldpacket, DHCPACK);
	add_server_options(&packet);

	send_packet(&packet, /*force_bcast:*/ 0);
}

int udhcpd_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int udhcpd_main(int argc UNUSED_PARAM, char **argv)
{
	int server_socket = -1, retval;
	uint8_t *state;
	unsigned timeout_end;
	unsigned num_ips;
	unsigned opt;
	struct option_set *option;
	char *str_I = str_I;
	const char *str_a = "2000";
	unsigned arpping_ms;
	IF_FEATURE_UDHCP_PORT(char *str_P;)

	setup_common_bufsiz();

	IF_FEATURE_UDHCP_PORT(SERVER_PORT = 67;)
	IF_FEATURE_UDHCP_PORT(CLIENT_PORT = 68;)

	opt = getopt32(argv, "^"
		"fSI:va:"IF_FEATURE_UDHCP_PORT("P:")
		"\0"
#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 1
		"vv"
#endif
		, &str_I
		, &str_a
		IF_FEATURE_UDHCP_PORT(, &str_P)
		IF_UDHCP_VERBOSE(, &dhcp_verbose)
		);
	if (!(opt & 1)) { /* no -f */
		bb_daemonize_or_rexec(0, argv);
		logmode = LOGMODE_NONE;
	}
	/* update argv after the possible vfork+exec in daemonize */
	argv += optind;
	if (opt & 2) { /* -S */
		openlog(applet_name, LOG_PID, LOG_DAEMON);
		logmode |= LOGMODE_SYSLOG;
	}
	if (opt & 4) { /* -I */
		len_and_sockaddr *lsa = xhost_and_af2sockaddr(str_I, 0, AF_INET);
		server_config.server_nip = lsa->u.sin.sin_addr.s_addr;
		free(lsa);
	}
#if ENABLE_FEATURE_UDHCP_PORT
	if (opt & 32) { /* -P */
		SERVER_PORT = xatou16(str_P);
		CLIENT_PORT = SERVER_PORT + 1;
	}
#endif
	arpping_ms = xatou(str_a);

	/* Would rather not do read_config before daemonization -
	 * otherwise NOMMU machines will parse config twice */
	read_config(argv[0] ? argv[0] : DHCPD_CONF_FILE);
	/* prevent poll timeout overflow */
	if (server_config.auto_time > INT_MAX / 1000)
		server_config.auto_time = INT_MAX / 1000;

	/* Make sure fd 0,1,2 are open */
	bb_sanitize_stdio();

	/* Create pidfile */
	write_pidfile(server_config.pidfile);
	/* if (!..) bb_perror_msg("can't create pidfile %s", pidfile); */

	bb_error_msg("started, v"BB_VER);

	option = udhcp_find_option(server_config.options, DHCP_LEASE_TIME);
	server_config.max_lease_sec = DEFAULT_LEASE_TIME;
	if (option) {
		move_from_unaligned32(server_config.max_lease_sec, option->data + OPT_DATA);
		server_config.max_lease_sec = ntohl(server_config.max_lease_sec);
	}

	/* Sanity check */
	num_ips = server_config.end_ip - server_config.start_ip + 1;
	if (server_config.max_leases > num_ips) {
		bb_error_msg("max_leases=%u is too big, setting to %u",
			(unsigned)server_config.max_leases, num_ips);
		server_config.max_leases = num_ips;
	}

	/* this sets g_leases */
	SET_PTR_TO_GLOBALS(xzalloc(server_config.max_leases * sizeof(g_leases[0])));

	read_leases(server_config.lease_file);

	if (udhcp_read_interface(server_config.interface,
			&server_config.ifindex,
			(server_config.server_nip == 0 ? &server_config.server_nip : NULL),
			server_config.server_mac)
	) {
		retval = 1;
		goto ret;
	}

	/* Setup the signal pipe */
	udhcp_sp_setup();

 continue_with_autotime:
	timeout_end = monotonic_sec() + server_config.auto_time;
	while (1) { /* loop until universe collapses */
		struct pollfd pfds[2];
		struct dhcp_packet packet;
		int bytes;
		int tv;
		uint8_t *server_id_opt;
		uint8_t *requested_ip_opt;
		uint32_t requested_nip = requested_nip; /* for compiler */
		uint32_t static_lease_nip;
		struct dyn_lease *lease, fake_lease;

		if (server_socket < 0) {
			server_socket = udhcp_listen_socket(/*INADDR_ANY,*/ SERVER_PORT,
					server_config.interface);
		}

		udhcp_sp_fd_set(pfds, server_socket);

 new_tv:
		tv = -1;
		if (server_config.auto_time) {
			tv = timeout_end - monotonic_sec();
			if (tv <= 0) {
 write_leases:
				write_leases();
				goto continue_with_autotime;
			}
			tv *= 1000;
		}

		/* Block here waiting for either signal or packet */
		retval = poll(pfds, 2, tv);
		if (retval <= 0) {
			if (retval == 0)
				goto write_leases;
			if (errno == EINTR)
				goto new_tv;
			/* < 0 and not EINTR: should not happen */
			bb_perror_msg_and_die("poll");
		}

		if (pfds[0].revents) switch (udhcp_sp_read()) {
		case SIGUSR1:
			bb_error_msg("received %s", "SIGUSR1");
			write_leases();
			/* why not just reset the timeout, eh */
			goto continue_with_autotime;
		case SIGTERM:
			bb_error_msg("received %s", "SIGTERM");
			write_leases();
			goto ret0;
		}

		/* Is it a packet? */
		if (!pfds[1].revents)
			continue; /* no */

		/* Note: we do not block here, we block on poll() instead.
		 * Blocking here would prevent SIGTERM from working:
		 * socket read inside this call is restarted on caught signals.
		 */
		bytes = udhcp_recv_kernel_packet(&packet, server_socket);
		if (bytes < 0) {
			/* bytes can also be -2 ("bad packet data") */
			if (bytes == -1 && errno != EINTR) {
				log1("read error: "STRERROR_FMT", reopening socket" STRERROR_ERRNO);
				close(server_socket);
				server_socket = -1;
			}
			continue;
		}
		if (packet.hlen != 6) {
			bb_error_msg("MAC length != 6, ignoring packet");
			continue;
		}
		if (packet.op != BOOTREQUEST) {
			bb_error_msg("not a REQUEST, ignoring packet");
			continue;
		}
		state = udhcp_get_option(&packet, DHCP_MESSAGE_TYPE);
		if (state == NULL || state[0] < DHCP_MINTYPE || state[0] > DHCP_MAXTYPE) {
			bb_error_msg("no or bad message type option, ignoring packet");
			continue;
		}

		/* Get SERVER_ID if present */
		server_id_opt = udhcp_get_option(&packet, DHCP_SERVER_ID);
		if (server_id_opt) {
			uint32_t server_id_network_order;
			move_from_unaligned32(server_id_network_order, server_id_opt);
			if (server_id_network_order != server_config.server_nip) {
				/* client talks to somebody else */
				log1("server ID doesn't match, ignoring");
				continue;
			}
		}

		/* Look for a static/dynamic lease */
		static_lease_nip = get_static_nip_by_mac(server_config.static_leases, &packet.chaddr);
		if (static_lease_nip) {
			bb_error_msg("found static lease: %x", static_lease_nip);
			memcpy(&fake_lease.lease_mac, &packet.chaddr, 6);
			fake_lease.lease_nip = static_lease_nip;
			fake_lease.expires = 0;
			lease = &fake_lease;
		} else {
			lease = find_lease_by_mac(packet.chaddr);
		}

		/* Get REQUESTED_IP if present */
		requested_ip_opt = udhcp_get_option(&packet, DHCP_REQUESTED_IP);
		if (requested_ip_opt) {
			move_from_unaligned32(requested_nip, requested_ip_opt);
		}

		switch (state[0]) {

		case DHCPDISCOVER:
			log1("received %s", "DISCOVER");

			send_offer(&packet, static_lease_nip, lease, requested_ip_opt, arpping_ms);
			break;

		case DHCPREQUEST:
			log1("received %s", "REQUEST");
/* RFC 2131:

o DHCPREQUEST generated during SELECTING state:

   Client inserts the address of the selected server in 'server
   identifier', 'ciaddr' MUST be zero, 'requested IP address' MUST be
   filled in with the yiaddr value from the chosen DHCPOFFER.

   Note that the client may choose to collect several DHCPOFFER
   messages and select the "best" offer.  The client indicates its
   selection by identifying the offering server in the DHCPREQUEST
   message.  If the client receives no acceptable offers, the client
   may choose to try another DHCPDISCOVER message.  Therefore, the
   servers may not receive a specific DHCPREQUEST from which they can
   decide whether or not the client has accepted the offer.

o DHCPREQUEST generated during INIT-REBOOT state:

   'server identifier' MUST NOT be filled in, 'requested IP address'
   option MUST be filled in with client's notion of its previously
   assigned address. 'ciaddr' MUST be zero. The client is seeking to
   verify a previously allocated, cached configuration. Server SHOULD
   send a DHCPNAK message to the client if the 'requested IP address'
   is incorrect, or is on the wrong network.

   Determining whether a client in the INIT-REBOOT state is on the
   correct network is done by examining the contents of 'giaddr', the
   'requested IP address' option, and a database lookup. If the DHCP
   server detects that the client is on the wrong net (i.e., the
   result of applying the local subnet mask or remote subnet mask (if
   'giaddr' is not zero) to 'requested IP address' option value
   doesn't match reality), then the server SHOULD send a DHCPNAK
   message to the client.

   If the network is correct, then the DHCP server should check if
   the client's notion of its IP address is correct. If not, then the
   server SHOULD send a DHCPNAK message to the client. If the DHCP
   server has no record of this client, then it MUST remain silent,
   and MAY output a warning to the network administrator. This
   behavior is necessary for peaceful coexistence of non-
   communicating DHCP servers on the same wire.

   If 'giaddr' is 0x0 in the DHCPREQUEST message, the client is on
   the same subnet as the server.  The server MUST broadcast the
   DHCPNAK message to the 0xffffffff broadcast address because the
   client may not have a correct network address or subnet mask, and
   the client may not be answering ARP requests.

   If 'giaddr' is set in the DHCPREQUEST message, the client is on a
   different subnet.  The server MUST set the broadcast bit in the
   DHCPNAK, so that the relay agent will broadcast the DHCPNAK to the
   client, because the client may not have a correct network address
   or subnet mask, and the client may not be answering ARP requests.

o DHCPREQUEST generated during RENEWING state:

   'server identifier' MUST NOT be filled in, 'requested IP address'
   option MUST NOT be filled in, 'ciaddr' MUST be filled in with
   client's IP address. In this situation, the client is completely
   configured, and is trying to extend its lease. This message will
   be unicast, so no relay agents will be involved in its
   transmission.  Because 'giaddr' is therefore not filled in, the
   DHCP server will trust the value in 'ciaddr', and use it when
   replying to the client.

   A client MAY choose to renew or extend its lease prior to T1.  The
   server may choose not to extend the lease (as a policy decision by
   the network administrator), but should return a DHCPACK message
   regardless.

o DHCPREQUEST generated during REBINDING state:

   'server identifier' MUST NOT be filled in, 'requested IP address'
   option MUST NOT be filled in, 'ciaddr' MUST be filled in with
   client's IP address. In this situation, the client is completely
   configured, and is trying to extend its lease. This message MUST
   be broadcast to the 0xffffffff IP broadcast address.  The DHCP
   server SHOULD check 'ciaddr' for correctness before replying to
   the DHCPREQUEST.

   The DHCPREQUEST from a REBINDING client is intended to accommodate
   sites that have multiple DHCP servers and a mechanism for
   maintaining consistency among leases managed by multiple servers.
   A DHCP server MAY extend a client's lease only if it has local
   administrative authority to do so.
*/
			if (!requested_ip_opt) {
				requested_nip = packet.ciaddr;
				if (requested_nip == 0) {
					log1("no requested IP and no ciaddr, ignoring");
					break;
				}
			}
			if (lease && requested_nip == lease->lease_nip) {
				/* client requested or configured IP matches the lease.
				 * ACK it, and bump lease expiration time. */
				send_ACK(&packet, lease->lease_nip);
				break;
			}
			/* No lease for this MAC, or lease IP != requested IP */

			if (server_id_opt    /* client is in SELECTING state */
			 || requested_ip_opt /* client is in INIT-REBOOT state */
			) {
				/* "No, we don't have this IP for you" */
				send_NAK(&packet);
			} /* else: client is in RENEWING or REBINDING, do not answer */

			break;

		case DHCPDECLINE:
			/* RFC 2131:
			 * "If the server receives a DHCPDECLINE message,
			 * the client has discovered through some other means
			 * that the suggested network address is already
			 * in use. The server MUST mark the network address
			 * as not available and SHOULD notify the local
			 * sysadmin of a possible configuration problem."
			 *
			 * SERVER_ID must be present,
			 * REQUESTED_IP must be present,
			 * chaddr must be filled in,
			 * ciaddr must be 0 (we do not check this)
			 */
			log1("received %s", "DECLINE");
			if (server_id_opt
			 && requested_ip_opt
			 && lease  /* chaddr matches this lease */
			 && requested_nip == lease->lease_nip
			) {
				memset(lease->lease_mac, 0, sizeof(lease->lease_mac));
				lease->expires = time(NULL) + server_config.decline_time;
			}
			break;

		case DHCPRELEASE:
			/* "Upon receipt of a DHCPRELEASE message, the server
			 * marks the network address as not allocated."
			 *
			 * SERVER_ID must be present,
			 * REQUESTED_IP must not be present (we do not check this),
			 * chaddr must be filled in,
			 * ciaddr must be filled in
			 */
			log1("received %s", "RELEASE");
			if (server_id_opt
			 && lease  /* chaddr matches this lease */
			 && packet.ciaddr == lease->lease_nip
			) {
				lease->expires = time(NULL);
			}
			break;

		case DHCPINFORM:
			log1("received %s", "INFORM");
			send_inform(&packet);
			break;
		}
	}
 ret0:
	retval = 0;
 ret:
	/*if (server_config.pidfile) - server_config.pidfile is never NULL */
		remove_pidfile(server_config.pidfile);
	return retval;
}
