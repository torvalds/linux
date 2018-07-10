/* vi: set sw=4 ts=4: */
/*
 * DHCPv6 client.
 *
 * WARNING: THIS CODE IS INCOMPLETE.
 *
 * Copyright (C) 2011-2017 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config UDHCPC6
//config:	bool "udhcpc6"
//config:	default n  # not yet ready
//config:	depends on FEATURE_IPV6
//config:	help
//config:	udhcpc6 is a DHCPv6 client
//config:
//config:config FEATURE_UDHCPC6_RFC3646
//config:	bool "Support RFC 3646 (DNS server and search list)"
//config:	default y
//config:	depends on UDHCPC6
//config:	help
//config:	List of DNS servers and domain search list can be requested with
//config:	"-O dns" and "-O search". If server gives these values,
//config:	they will be set in environment variables "dns" and "search".
//config:
//config:config FEATURE_UDHCPC6_RFC4704
//config:	bool "Support RFC 4704 (Client FQDN)"
//config:	default y
//config:	depends on UDHCPC6
//config:	help
//config:	You can request FQDN to be given by server using "-O fqdn".
//config:
//config:config FEATURE_UDHCPC6_RFC4833
//config:	bool "Support RFC 4833 (Timezones)"
//config:	default y
//config:	depends on UDHCPC6
//config:	help
//config:	You can request POSIX timezone with "-O tz" and timezone name
//config:	with "-O timezone".
//config:
//config:config FEATURE_UDHCPC6_RFC5970
//config:	bool "Support RFC 5970 (Network Boot)"
//config:	default y
//config:	depends on UDHCPC6
//config:	help
//config:	You can request bootfile-url with "-O bootfile_url" and
//config:	bootfile-params with "-O bootfile_params".

//applet:IF_UDHCPC6(APPLET(udhcpc6, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_UDHCPC6) += d6_dhcpc.o d6_packet.o d6_socket.o common.o socket.o signalpipe.o
//kbuild:lib-$(CONFIG_FEATURE_UDHCPC6_RFC3646) += domain_codec.o
//kbuild:lib-$(CONFIG_FEATURE_UDHCPC6_RFC4704) += domain_codec.o

#include <syslog.h>
/* Override ENABLE_FEATURE_PIDFILE - ifupdown needs our pidfile to always exist */
#define WANT_PIDFILE 1
#include "common.h"
#include "dhcpd.h"
#include "dhcpc.h"
#include "d6_common.h"

#include <netinet/if_ether.h>
#include <netpacket/packet.h>
#include <linux/filter.h>

/* "struct client_config_t client_config" is in bb_common_bufsiz1 */

static const struct dhcp_optflag d6_optflags[] = {
#if ENABLE_FEATURE_UDHCPC6_RFC3646
	{ OPTION_6RD | OPTION_LIST        | OPTION_REQ, D6_OPT_DNS_SERVERS },
	{ OPTION_DNS_STRING | OPTION_LIST | OPTION_REQ, D6_OPT_DOMAIN_LIST },
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4704
	{ OPTION_DNS_STRING,                            D6_OPT_CLIENT_FQDN },
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4833
	{ OPTION_STRING,                                D6_OPT_TZ_POSIX },
	{ OPTION_STRING,                                D6_OPT_TZ_NAME },
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC5970
	{ OPTION_STRING,                                D6_OPT_BOOT_URL },
	{ OPTION_STRING,                                D6_OPT_BOOT_PARAM },
#endif
	{ OPTION_STRING,                                0xd1 }, /* DHCP_PXE_CONF_FILE */
	{ OPTION_STRING,                                0xd2 }, /* DHCP_PXE_PATH_PREFIX */
	{ 0, 0 }
};
/* Must match d6_optflags[] order */
static const char d6_option_strings[] ALIGN1 =
#if ENABLE_FEATURE_UDHCPC6_RFC3646
	"dns" "\0"      /* D6_OPT_DNS_SERVERS */
	"search" "\0"   /* D6_OPT_DOMAIN_LIST */
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4704
	"fqdn" "\0"     /* D6_OPT_CLIENT_FQDN */
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4833
	"tz" "\0"       /* D6_OPT_TZ_POSIX */
	"timezone" "\0" /* D6_OPT_TZ_NAME */
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC5970
	"bootfile_url" "\0" /* D6_OPT_BOOT_URL */
	"bootfile_param" "\0" /* D6_OPT_BOOT_PARAM */
#endif
	"pxeconffile" "\0" /* DHCP_PXE_CONF_FILE  */
	"pxepathprefix" "\0" /* DHCP_PXE_PATH_PREFIX  */
	"\0";

#if ENABLE_LONG_OPTS
static const char udhcpc6_longopts[] ALIGN1 =
	"interface\0"      Required_argument "i"
	"now\0"            No_argument       "n"
	"pidfile\0"        Required_argument "p"
	"quit\0"           No_argument       "q"
	"release\0"        No_argument       "R"
	"request\0"        Required_argument "r"
	"requestprefix\0"  No_argument       "d"
	"script\0"         Required_argument "s"
	"timeout\0"        Required_argument "T"
	"retries\0"        Required_argument "t"
	"tryagain\0"       Required_argument "A"
	"syslog\0"         No_argument       "S"
	"request-option\0" Required_argument "O"
	"no-default-options\0" No_argument   "o"
	"foreground\0"     No_argument       "f"
	USE_FOR_MMU(
	"background\0"     No_argument       "b"
	)
///	IF_FEATURE_UDHCPC_ARPING("arping\0"	No_argument       "a")
	IF_FEATURE_UDHCP_PORT("client-port\0"	Required_argument "P")
	;
#endif
/* Must match getopt32 option string order */
enum {
	OPT_i = 1 << 0,
	OPT_n = 1 << 1,
	OPT_p = 1 << 2,
	OPT_q = 1 << 3,
	OPT_R = 1 << 4,
	OPT_r = 1 << 5,
	OPT_s = 1 << 6,
	OPT_T = 1 << 7,
	OPT_t = 1 << 8,
	OPT_S = 1 << 9,
	OPT_A = 1 << 10,
	OPT_O = 1 << 11,
	OPT_o = 1 << 12,
	OPT_x = 1 << 13,
	OPT_f = 1 << 14,
	OPT_d = 1 << 15,
/* The rest has variable bit positions, need to be clever */
	OPTBIT_d = 15,
	USE_FOR_MMU(             OPTBIT_b,)
	///IF_FEATURE_UDHCPC_ARPING(OPTBIT_a,)
	IF_FEATURE_UDHCP_PORT(   OPTBIT_P,)
	USE_FOR_MMU(             OPT_b = 1 << OPTBIT_b,)
	///IF_FEATURE_UDHCPC_ARPING(OPT_a = 1 << OPTBIT_a,)
	IF_FEATURE_UDHCP_PORT(   OPT_P = 1 << OPTBIT_P,)
};

#if ENABLE_FEATURE_UDHCPC6_RFC4704
static const char opt_fqdn_req[] = {
	(D6_OPT_CLIENT_FQDN >> 8), (D6_OPT_CLIENT_FQDN & 0xff),
	0, 2, /* optlen */
	0, /* flags: */
	/* S=0: server SHOULD NOT perform AAAA RR updates */
	/* O=0: client MUST set this bit to 0 */
	/* N=0: server SHOULD perform updates (PTR RR only in our case, since S=0) */
	0 /* empty DNS-encoded name */
};
#endif

/*** Utility functions ***/

static void *d6_find_option(uint8_t *option, uint8_t *option_end, unsigned code)
{
	/* "length minus 4" */
	int len_m4 = option_end - option - 4;
	while (len_m4 >= 0) {
		/* Next option's len is too big? */
		if (option[3] > len_m4)
			return NULL; /* yes. bogus packet! */
		/* So far we treat any opts with code >255
		 * or len >255 as bogus, and stop at once.
		 * This simplifies big-endian handling.
		 */
		if (option[0] != 0 || option[2] != 0)
			return NULL;
		/* Option seems to be valid */
		/* Does its code match? */
		if (option[1] == code)
			return option; /* yes! */
		len_m4 -= option[3] + 4;
		option += option[3] + 4;
	}
	return NULL;
}

static void *d6_copy_option(uint8_t *option, uint8_t *option_end, unsigned code)
{
	uint8_t *opt = d6_find_option(option, option_end, code);
	if (!opt)
		return opt;
	return xmemdup(opt, opt[3] + 4);
}


/*** Script execution code ***/

static char** new_env(void)
{
	client6_data.env_ptr = xrealloc_vector(client6_data.env_ptr, 3, client6_data.env_idx);
	return &client6_data.env_ptr[client6_data.env_idx++];
}

static char *string_option_to_env(const uint8_t *option,
		const uint8_t *option_end)
{
	const char *ptr, *name = NULL;
	unsigned val_len;
	int i;

	ptr = d6_option_strings;
	i = 0;
	while (*ptr) {
		if (d6_optflags[i].code == option[1]) {
			name = ptr;
			goto found;
		}
		ptr += strlen(ptr) + 1;
		i++;
	}
	bb_error_msg("can't find option name for 0x%x, skipping", option[1]);
	return NULL;

 found:
	val_len = (option[2] << 8) | option[3];
	if (val_len + &option[D6_OPT_DATA] > option_end) {
		bb_error_msg("option data exceeds option length");
		return NULL;
	}
	return xasprintf("%s=%.*s", name, val_len, (char*)option + 4);
}

/* put all the parameters into the environment */
static void option_to_env(const uint8_t *option, const uint8_t *option_end)
{
#if ENABLE_FEATURE_UDHCPC6_RFC3646
	int addrs, option_offset;
#endif
	/* "length minus 4" */
	int len_m4 = option_end - option - 4;

	while (len_m4 >= 0) {
		uint32_t v32;
		char ipv6str[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff")];

		if (option[0] != 0 || option[2] != 0)
			break;

		/* Check if option-length exceeds size of option */
		if (option[3] > len_m4)
			break;

		switch (option[1]) {
		//case D6_OPT_CLIENTID:
		//case D6_OPT_SERVERID:
		case D6_OPT_IA_NA:
		case D6_OPT_IA_PD:
			option_to_env(option + 16, option + 4 + option[3]);
			break;
		//case D6_OPT_IA_TA:
		case D6_OPT_IAADDR:
/*  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |          OPTION_IAADDR        |          option-len           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                         IPv6 address                          |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      preferred-lifetime                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        valid-lifetime                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
			/* Make sure payload contains an address */
			if (option[3] < 24)
				break;

			sprint_nip6(ipv6str, option + 4);
			*new_env() = xasprintf("ipv6=%s", ipv6str);

			move_from_unaligned32(v32, option + 4 + 16 + 4);
			*new_env() = xasprintf("lease=%u", (unsigned)v32);
			break;

		//case D6_OPT_ORO:
		//case D6_OPT_PREFERENCE:
		//case D6_OPT_ELAPSED_TIME:
		//case D6_OPT_RELAY_MSG:
		//case D6_OPT_AUTH:
		//case D6_OPT_UNICAST:
		//case D6_OPT_STATUS_CODE:
		//case D6_OPT_RAPID_COMMIT:
		//case D6_OPT_USER_CLASS:
		//case D6_OPT_VENDOR_CLASS:
		//case D6_OPT_VENDOR_OPTS:
		//case D6_OPT_INTERFACE_ID:
		//case D6_OPT_RECONF_MSG:
		//case D6_OPT_RECONF_ACCEPT:

		case D6_OPT_IAPREFIX:
/*  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |        OPTION_IAPREFIX        |         option-length         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      preferred-lifetime                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        valid-lifetime                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | prefix-length |                                               |
 * +-+-+-+-+-+-+-+-+          IPv6 prefix                          |
 * |                           (16 octets)                         |
 * |                                                               |
 * |               +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |               |
 * +-+-+-+-+-+-+-+-+
 */
			move_from_unaligned32(v32, option + 4 + 4);
			*new_env() = xasprintf("ipv6prefix_lease=%u", (unsigned)v32);

			sprint_nip6(ipv6str, option + 4 + 4 + 4 + 1);
			*new_env() = xasprintf("ipv6prefix=%s/%u", ipv6str, (unsigned)(option[4 + 4 + 4]));
			break;
#if ENABLE_FEATURE_UDHCPC6_RFC3646
		case D6_OPT_DNS_SERVERS: {
			char *dlist;

			/* Make sure payload-size is a multiple of 16 */
			if ((option[3] & 0x0f) != 0)
				break;

			/* Get the number of addresses on the option */
			addrs = option[3] >> 4;

			/* Setup environment variable */
			*new_env() = dlist = xmalloc(4 + addrs * 40 - 1);
			dlist = stpcpy(dlist, "dns=");
			option_offset = 0;

			while (addrs--) {
				sprint_nip6(dlist, option + 4 + option_offset);
				dlist += 39;
				option_offset += 16;
				if (addrs)
					*dlist++ = ' ';
			}

			break;
		}
		case D6_OPT_DOMAIN_LIST: {
			char *dlist;

			dlist = dname_dec(option + 4, (option[2] << 8) | option[3], "search=");
			if (!dlist)
				break;
			*new_env() = dlist;
			break;
		}
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4704
		case D6_OPT_CLIENT_FQDN: {
			char *dlist;

			if (option[3] == 0)
				break;
			/* Work around broken ISC DHCPD6.
			 * ISC DHCPD6 does not implement RFC 4704 correctly: It says the first
			 * byte of option-payload should contain flags where the bits 7-3 are
			 * reserved for future use and MUST be zero. Instead ISC DHCPD6 just
			 * writes the entire FQDN as string to option-payload. We assume a
			 * broken server here if any of the reserved bits are set.
			 */
			if (option[4] & 0xf8) {
				*new_env() = xasprintf("fqdn=%.*s", (int)option[3], (char*)option + 4);
				break;
			}
			dlist = dname_dec(option + 5, (/*(option[2] << 8) |*/ option[3]) - 1, "fqdn=");
			if (!dlist)
				break;
			*new_env() = dlist;
			break;
		}
#endif
#if ENABLE_FEATURE_UDHCPC6_RFC4833
		/* RFC 4833 Timezones */
		case D6_OPT_TZ_POSIX:
			*new_env() = xasprintf("tz=%.*s", (int)option[3], (char*)option + 4);
			break;
		case D6_OPT_TZ_NAME:
			*new_env() = xasprintf("tz_name=%.*s", (int)option[3], (char*)option + 4);
			break;
#endif
		case D6_OPT_BOOT_URL:
		case D6_OPT_BOOT_PARAM:
		case 0xd1: /* DHCP_PXE_CONF_FILE */
		case 0xd2: /* DHCP_PXE_PATH_PREFIX */
			{
			char *tmp = string_option_to_env(option, option_end);
			if (tmp)
				*new_env() = tmp;
			break;
			}
		}
		len_m4 -= 4 + option[3];
		option += 4 + option[3];
	}
}

static char **fill_envp(const uint8_t *option, const uint8_t *option_end)
{
	char **envp, **curr;

	client6_data.env_ptr = NULL;
	client6_data.env_idx = 0;

	*new_env() = xasprintf("interface=%s", client_config.interface);

	if (option)
		option_to_env(option, option_end);

	envp = curr = client6_data.env_ptr;
	while (*curr)
		putenv(*curr++);

	return envp;
}

/* Call a script with a par file and env vars */
static void d6_run_script(const uint8_t *option, const uint8_t *option_end,
		const char *name)
{
	char **envp, **curr;
	char *argv[3];

	envp = fill_envp(option, option_end);

	/* call script */
	log1("executing %s %s", client_config.script, name);
	argv[0] = (char*) client_config.script;
	argv[1] = (char*) name;
	argv[2] = NULL;
	spawn_and_wait(argv);

	for (curr = envp; *curr; curr++) {
		log2(" %s", *curr);
		bb_unsetenv_and_free(*curr);
	}
	free(envp);
}

/* Call a script with a par file and no env var */
static void d6_run_script_no_option(const char *name)
{
	d6_run_script(NULL, NULL, name);
}

/*** Sending/receiving packets ***/

static ALWAYS_INLINE uint32_t random_xid(void)
{
	uint32_t t = rand() & htonl(0x00ffffff);
	return t;
}

/* Initialize the packet with the proper defaults */
static uint8_t *init_d6_packet(struct d6_packet *packet, char type, uint32_t xid)
{
	struct d6_option *clientid;

	memset(packet, 0, sizeof(*packet));

	packet->d6_xid32 = xid;
	packet->d6_msg_type = type;

	clientid = (void*)client_config.clientid;
	return mempcpy(packet->d6_options, clientid, clientid->len + 2+2);
}

static uint8_t *add_d6_client_options(uint8_t *ptr)
{
	struct option_set *curr;
	uint8_t *start = ptr;
	unsigned option;
	uint16_t len;

	ptr += 4;
	for (option = 1; option < 256; option++) {
		if (client_config.opt_mask[option >> 3] & (1 << (option & 7))) {
			ptr[0] = (option >> 8);
			ptr[1] = option;
			ptr += 2;
		}
	}

	if ((ptr - start - 4) != 0) {
		start[0] = (D6_OPT_ORO >> 8);
		start[1] = D6_OPT_ORO;
		start[2] = ((ptr - start - 4) >> 8);
		start[3] = (ptr - start - 4);
	} else
		ptr = start;

#if ENABLE_FEATURE_UDHCPC6_RFC4704
	ptr = mempcpy(ptr, &opt_fqdn_req, sizeof(opt_fqdn_req));
#endif
	/* Add -x options if any */
	curr = client_config.options;
	while (curr) {
		len = (curr->data[D6_OPT_LEN] << 8) | curr->data[D6_OPT_LEN + 1];
		ptr = mempcpy(ptr, curr->data, D6_OPT_DATA + len);
		curr = curr->next;
	}

	return ptr;
}

static int d6_mcast_from_client_config_ifindex(struct d6_packet *packet, uint8_t *end)
{
	static const uint8_t FF02__1_2[16] = {
		0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x02,
	};

	return d6_send_raw_packet(
		packet, (end - (uint8_t*) packet),
		/*src*/ &client6_data.ll_ip6, CLIENT_PORT6,
		/*dst*/ (struct in6_addr*)FF02__1_2, SERVER_PORT6, MAC_BCAST_ADDR,
		client_config.ifindex
	);
}

/* Milticast a DHCPv6 Solicit packet to the network, with an optionally requested IP.
 *
 * RFC 3315 17.1.1. Creation of Solicit Messages
 *
 * The client MUST include a Client Identifier option to identify itself
 * to the server.  The client includes IA options for any IAs to which
 * it wants the server to assign addresses.  The client MAY include
 * addresses in the IAs as a hint to the server about addresses for
 * which the client has a preference. ...
 *
 * The client uses IA_NA options to request the assignment of non-
 * temporary addresses and uses IA_TA options to request the assignment
 * of temporary addresses.  Either IA_NA or IA_TA options, or a
 * combination of both, can be included in DHCP messages.
 *
 * The client SHOULD include an Option Request option (see section 22.7)
 * to indicate the options the client is interested in receiving.  The
 * client MAY additionally include instances of those options that are
 * identified in the Option Request option, with data values as hints to
 * the server about parameter values the client would like to have
 * returned.
 *
 * The client includes a Reconfigure Accept option (see section 22.20)
 * if the client is willing to accept Reconfigure messages from the
 * server.
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |        OPTION_CLIENTID        |          option-len           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      .                                                               .
      .                              DUID                             .
      .                        (variable length)                      .
      .                                                               .
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |          OPTION_IA_NA         |          option-len           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                        IAID (4 octets)                        |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              T1                               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              T2                               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      .                         IA_NA-options                         .
      .                                                               .
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |          OPTION_IAADDR        |          option-len           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                                                               |
      |                         IPv6 address                          |
      |                                                               |
      |                                                               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                      preferred-lifetime                       |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                        valid-lifetime                         |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      .                                                               .
      .                        IAaddr-options                         .
      .                                                               .
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |           OPTION_ORO          |           option-len          |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |    requested-option-code-1    |    requested-option-code-2    |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                              ...                              |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |     OPTION_RECONF_ACCEPT      |               0               |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
/* NOINLINE: limit stack usage in caller */
static NOINLINE int send_d6_discover(uint32_t xid, struct in6_addr *requested_ipv6)
{
	struct d6_packet packet;
	uint8_t *opt_ptr;
	unsigned len;

	/* Fill in: msg type, client id */
	opt_ptr = init_d6_packet(&packet, D6_MSG_SOLICIT, xid);

	/* Create new IA_NA, optionally with included IAADDR with requested IP */
	free(client6_data.ia_na);
	client6_data.ia_na = NULL;
	if (option_mask32 & OPT_r) {
		len = requested_ipv6 ? 2+2+4+4+4 + 2+2+16+4+4 : 2+2+4+4+4;
		client6_data.ia_na = xzalloc(len);
		client6_data.ia_na->code = D6_OPT_IA_NA;
		client6_data.ia_na->len = len - 4;
		*(uint32_t*)client6_data.ia_na->data = rand(); /* IAID */
		if (requested_ipv6) {
			struct d6_option *iaaddr = (void*)(client6_data.ia_na->data + 4+4+4);
			iaaddr->code = D6_OPT_IAADDR;
			iaaddr->len = 16+4+4;
			memcpy(iaaddr->data, requested_ipv6, 16);
		}
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_na, len);
	}

	/* IA_PD */
	free(client6_data.ia_pd);
	client6_data.ia_pd = NULL;
	if (option_mask32 & OPT_d) {
		len = 2+2+4+4+4;
		client6_data.ia_pd = xzalloc(len);
		client6_data.ia_pd->code = D6_OPT_IA_PD;
		client6_data.ia_pd->len = len - 4;
		*(uint32_t*)client6_data.ia_pd->data = rand(); /* IAID */
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_pd, len);
	}

	/* Add options:
	 * "param req" option according to -O, options specified with -x
	 */
	opt_ptr = add_d6_client_options(opt_ptr);

	bb_error_msg("sending %s", "discover");
	return d6_mcast_from_client_config_ifindex(&packet, opt_ptr);
}

/* Multicast a DHCPv6 request message
 *
 * RFC 3315 18.1.1. Creation and Transmission of Request Messages
 *
 * The client uses a Request message to populate IAs with addresses and
 * obtain other configuration information.  The client includes one or
 * more IA options in the Request message.  The server then returns
 * addresses and other information about the IAs to the client in IA
 * options in a Reply message.
 *
 * The client generates a transaction ID and inserts this value in the
 * "transaction-id" field.
 *
 * The client places the identifier of the destination server in a
 * Server Identifier option.
 *
 * The client MUST include a Client Identifier option to identify itself
 * to the server.  The client adds any other appropriate options,
 * including one or more IA options (if the client is requesting that
 * the server assign it some network addresses).
 *
 * The client MUST include an Option Request option (see section 22.7)
 * to indicate the options the client is interested in receiving.  The
 * client MAY include options with data values as hints to the server
 * about parameter values the client would like to have returned.
 *
 * The client includes a Reconfigure Accept option (see section 22.20)
 * indicating whether or not the client is willing to accept Reconfigure
 * messages from the server.
 */
/* NOINLINE: limit stack usage in caller */
static NOINLINE int send_d6_select(uint32_t xid)
{
	struct d6_packet packet;
	uint8_t *opt_ptr;

	/* Fill in: msg type, client id */
	opt_ptr = init_d6_packet(&packet, D6_MSG_REQUEST, xid);

	/* server id */
	opt_ptr = mempcpy(opt_ptr, client6_data.server_id, client6_data.server_id->len + 2+2);
	/* IA NA (contains requested IP) */
	if (client6_data.ia_na)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_na, client6_data.ia_na->len + 2+2);
	/* IA PD */
	if (client6_data.ia_pd)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_pd, client6_data.ia_pd->len + 2+2);

	/* Add options:
	 * "param req" option according to -O, options specified with -x
	 */
	opt_ptr = add_d6_client_options(opt_ptr);

	bb_error_msg("sending %s", "select");
	return d6_mcast_from_client_config_ifindex(&packet, opt_ptr);
}

/* Unicast or broadcast a DHCP renew message
 *
 * RFC 3315 18.1.3. Creation and Transmission of Renew Messages
 *
 * To extend the valid and preferred lifetimes for the addresses
 * associated with an IA, the client sends a Renew message to the server
 * from which the client obtained the addresses in the IA containing an
 * IA option for the IA.  The client includes IA Address options in the
 * IA option for the addresses associated with the IA.  The server
 * determines new lifetimes for the addresses in the IA according to the
 * administrative configuration of the server.  The server may also add
 * new addresses to the IA.  The server may remove addresses from the IA
 * by setting the preferred and valid lifetimes of those addresses to
 * zero.
 *
 * The server controls the time at which the client contacts the server
 * to extend the lifetimes on assigned addresses through the T1 and T2
 * parameters assigned to an IA.
 *
 * At time T1 for an IA, the client initiates a Renew/Reply message
 * exchange to extend the lifetimes on any addresses in the IA.  The
 * client includes an IA option with all addresses currently assigned to
 * the IA in its Renew message.
 *
 * If T1 or T2 is set to 0 by the server (for an IA_NA) or there are no
 * T1 or T2 times (for an IA_TA), the client may send a Renew or Rebind
 * message, respectively, at the client's discretion.
 *
 * The client sets the "msg-type" field to RENEW.  The client generates
 * a transaction ID and inserts this value in the "transaction-id"
 * field.
 *
 * The client places the identifier of the destination server in a
 * Server Identifier option.
 *
 * The client MUST include a Client Identifier option to identify itself
 * to the server.  The client adds any appropriate options, including
 * one or more IA options.  The client MUST include the list of
 * addresses the client currently has associated with the IAs in the
 * Renew message.
 *
 * The client MUST include an Option Request option (see section 22.7)
 * to indicate the options the client is interested in receiving.  The
 * client MAY include options with data values as hints to the server
 * about parameter values the client would like to have returned.
 */
/* NOINLINE: limit stack usage in caller */
static NOINLINE int send_d6_renew(uint32_t xid, struct in6_addr *server_ipv6, struct in6_addr *our_cur_ipv6)
{
	struct d6_packet packet;
	uint8_t *opt_ptr;

	/* Fill in: msg type, client id */
	opt_ptr = init_d6_packet(&packet, DHCPREQUEST, xid);

	/* server id */
	opt_ptr = mempcpy(opt_ptr, client6_data.server_id, client6_data.server_id->len + 2+2);
	/* IA NA (contains requested IP) */
	if (client6_data.ia_na)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_na, client6_data.ia_na->len + 2+2);
	/* IA PD */
	if (client6_data.ia_pd)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_pd, client6_data.ia_pd->len + 2+2);

	/* Add options:
	 * "param req" option according to -O, options specified with -x
	 */
	opt_ptr = add_d6_client_options(opt_ptr);

	bb_error_msg("sending %s", "renew");
	if (server_ipv6)
		return d6_send_kernel_packet(
			&packet, (opt_ptr - (uint8_t*) &packet),
			our_cur_ipv6, CLIENT_PORT6,
			server_ipv6, SERVER_PORT6,
			client_config.ifindex
		);
	return d6_mcast_from_client_config_ifindex(&packet, opt_ptr);
}

/* Unicast a DHCP release message */
static int send_d6_release(struct in6_addr *server_ipv6, struct in6_addr *our_cur_ipv6)
{
	struct d6_packet packet;
	uint8_t *opt_ptr;

	/* Fill in: msg type, client id */
	opt_ptr = init_d6_packet(&packet, D6_MSG_RELEASE, random_xid());
	/* server id */
	opt_ptr = mempcpy(opt_ptr, client6_data.server_id, client6_data.server_id->len + 2+2);
	/* IA NA (contains our current IP) */
	if (client6_data.ia_na)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_na, client6_data.ia_na->len + 2+2);
	/* IA PD */
	if (client6_data.ia_pd)
		opt_ptr = mempcpy(opt_ptr, client6_data.ia_pd, client6_data.ia_pd->len + 2+2);

	bb_error_msg("sending %s", "release");
	return d6_send_kernel_packet(
		&packet, (opt_ptr - (uint8_t*) &packet),
		our_cur_ipv6, CLIENT_PORT6,
		server_ipv6, SERVER_PORT6,
		client_config.ifindex
	);
}

/* Returns -1 on errors that are fatal for the socket, -2 for those that aren't */
/* NOINLINE: limit stack usage in caller */
static NOINLINE int d6_recv_raw_packet(struct in6_addr *peer_ipv6, struct d6_packet *d6_pkt, int fd)
{
	int bytes;
	struct ip6_udp_d6_packet packet;

	bytes = safe_read(fd, &packet, sizeof(packet));
	if (bytes < 0) {
		log1("packet read error, ignoring");
		/* NB: possible down interface, etc. Caller should pause. */
		return bytes; /* returns -1 */
	}

	if (bytes < (int) (sizeof(packet.ip6) + sizeof(packet.udp))) {
		log1("packet is too short, ignoring");
		return -2;
	}

	if (bytes < sizeof(packet.ip6) + ntohs(packet.ip6.ip6_plen)) {
		/* packet is bigger than sizeof(packet), we did partial read */
		log1("oversized packet, ignoring");
		return -2;
	}

	/* ignore any extra garbage bytes */
	bytes = sizeof(packet.ip6) + ntohs(packet.ip6.ip6_plen);

	/* make sure its the right packet for us, and that it passes sanity checks */
	if (packet.ip6.ip6_nxt != IPPROTO_UDP
	 || (packet.ip6.ip6_vfc >> 4) != 6
	 || packet.udp.dest != htons(CLIENT_PORT6)
	/* || bytes > (int) sizeof(packet) - can't happen */
	 || packet.udp.len != packet.ip6.ip6_plen
	) {
		log1("unrelated/bogus packet, ignoring");
		return -2;
	}

//How to do this for ipv6?
//	/* verify UDP checksum. IP header has to be modified for this */
//	memset(&packet.ip, 0, offsetof(struct iphdr, protocol));
//	/* ip.xx fields which are not memset: protocol, check, saddr, daddr */
//	packet.ip.tot_len = packet.udp.len; /* yes, this is needed */
//	check = packet.udp.check;
//	packet.udp.check = 0;
//	if (check && check != inet_cksum((uint16_t *)&packet, bytes)) {
//		log1("packet with bad UDP checksum received, ignoring");
//		return -2;
//	}

	if (peer_ipv6)
		*peer_ipv6 = packet.ip6.ip6_src; /* struct copy */

	log1("received %s", "a packet");
	d6_dump_packet(&packet.data);

	bytes -= sizeof(packet.ip6) + sizeof(packet.udp);
	memcpy(d6_pkt, &packet.data, bytes);
	return bytes;
}


/*** Main ***/

static int sockfd = -1;

#define LISTEN_NONE   0
#define LISTEN_KERNEL 1
#define LISTEN_RAW    2
static smallint listen_mode;

/* initial state: (re)start DHCP negotiation */
#define INIT_SELECTING  0
/* discover was sent, DHCPOFFER reply received */
#define REQUESTING      1
/* select/renew was sent, DHCPACK reply received */
#define BOUND           2
/* half of lease passed, want to renew it by sending unicast renew requests */
#define RENEWING        3
/* renew requests were not answered, lease is almost over, send broadcast renew */
#define REBINDING       4
/* manually requested renew (SIGUSR1) */
#define RENEW_REQUESTED 5
/* release, possibly manually requested (SIGUSR2) */
#define RELEASED        6
static smallint state;

static int d6_raw_socket(int ifindex)
{
	int fd;
	struct sockaddr_ll sock;

	/*
	 * Comment:
	 *
	 *	I've selected not to see LL header, so BPF doesn't see it, too.
	 *	The filter may also pass non-IP and non-ARP packets, but we do
	 *	a more complete check when receiving the message in userspace.
	 *
	 * and filter shamelessly stolen from:
	 *
	 *	http://www.flamewarmaster.de/software/dhcpclient/
	 *
	 * There are a few other interesting ideas on that page (look under
	 * "Motivation").  Use of netlink events is most interesting.  Think
	 * of various network servers listening for events and reconfiguring.
	 * That would obsolete sending HUP signals and/or make use of restarts.
	 *
	 * Copyright: 2006, 2007 Stefan Rompf <sux@loplof.de>.
	 * License: GPL v2.
	 *
	 * TODO: make conditional?
	 */
#if 0
	static const struct sock_filter filter_instr[] = {
		/* load 9th byte (protocol) */
		BPF_STMT(BPF_LD|BPF_B|BPF_ABS, 9),
		/* jump to L1 if it is IPPROTO_UDP, else to L4 */
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, IPPROTO_UDP, 0, 6),
		/* L1: load halfword from offset 6 (flags and frag offset) */
		BPF_STMT(BPF_LD|BPF_H|BPF_ABS, 6),
		/* jump to L4 if any bits in frag offset field are set, else to L2 */
		BPF_JUMP(BPF_JMP|BPF_JSET|BPF_K, 0x1fff, 4, 0),
		/* L2: skip IP header (load index reg with header len) */
		BPF_STMT(BPF_LDX|BPF_B|BPF_MSH, 0),
		/* load udp destination port from halfword[header_len + 2] */
		BPF_STMT(BPF_LD|BPF_H|BPF_IND, 2),
		/* jump to L3 if udp dport is CLIENT_PORT6, else to L4 */
		BPF_JUMP(BPF_JMP|BPF_JEQ|BPF_K, 68, 0, 1),
		/* L3: accept packet */
		BPF_STMT(BPF_RET|BPF_K, 0x7fffffff),
		/* L4: discard packet */
		BPF_STMT(BPF_RET|BPF_K, 0),
	};
	static const struct sock_fprog filter_prog = {
		.len = sizeof(filter_instr) / sizeof(filter_instr[0]),
		/* casting const away: */
		.filter = (struct sock_filter *) filter_instr,
	};
#endif

	log2("opening raw socket on ifindex %d", ifindex);

	fd = xsocket(PF_PACKET, SOCK_DGRAM, htons(ETH_P_IPV6));
	log2("got raw socket fd %d", fd);

	memset(&sock, 0, sizeof(sock)); /* let's be deterministic */
	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_IPV6);
	sock.sll_ifindex = ifindex;
	/*sock.sll_hatype = ARPHRD_???;*/
	/*sock.sll_pkttype = PACKET_???;*/
	/*sock.sll_halen = ???;*/
	/*sock.sll_addr[8] = ???;*/
	xbind(fd, (struct sockaddr *) &sock, sizeof(sock));

#if 0
	if (CLIENT_PORT6 == 546) {
		/* Use only if standard port is in use */
		/* Ignoring error (kernel may lack support for this) */
		if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &filter_prog,
				sizeof(filter_prog)) >= 0)
			log1("attached filter to raw socket fd %d", fd); // log?
	}
#endif

	log1("created raw socket");

	return fd;
}

static void change_listen_mode(int new_mode)
{
	log1("entering listen mode: %s",
		new_mode != LISTEN_NONE
			? (new_mode == LISTEN_KERNEL ? "kernel" : "raw")
			: "none"
	);

	listen_mode = new_mode;
	if (sockfd >= 0) {
		close(sockfd);
		sockfd = -1;
	}
	if (new_mode == LISTEN_KERNEL)
		sockfd = udhcp_listen_socket(/*INADDR_ANY,*/ CLIENT_PORT6, client_config.interface);
	else if (new_mode != LISTEN_NONE)
		sockfd = d6_raw_socket(client_config.ifindex);
	/* else LISTEN_NONE: sockfd stays closed */
}

/* Called only on SIGUSR1 */
static void perform_renew(void)
{
	bb_error_msg("performing DHCP renew");
	switch (state) {
	case BOUND:
		change_listen_mode(LISTEN_KERNEL);
	case RENEWING:
	case REBINDING:
		state = RENEW_REQUESTED;
		break;
	case RENEW_REQUESTED: /* impatient are we? fine, square 1 */
		d6_run_script_no_option("deconfig");
	case REQUESTING:
	case RELEASED:
		change_listen_mode(LISTEN_RAW);
		state = INIT_SELECTING;
		break;
	case INIT_SELECTING:
		break;
	}
}

static void perform_d6_release(struct in6_addr *server_ipv6, struct in6_addr *our_cur_ipv6)
{
	/* send release packet */
	if (state == BOUND
	 || state == RENEWING
	 || state == REBINDING
	 || state == RENEW_REQUESTED
	) {
		bb_error_msg("unicasting a release");
		send_d6_release(server_ipv6, our_cur_ipv6); /* unicast */
	}
	bb_error_msg("entering released state");
/*
 * We can be here on: SIGUSR2,
 * or on exit (SIGTERM) and -R "release on quit" is specified.
 * Users requested to be notified in all cases, even if not in one
 * of the states above.
 */
	d6_run_script_no_option("deconfig");
	change_listen_mode(LISTEN_NONE);
	state = RELEASED;
}

///static uint8_t* alloc_dhcp_option(int code, const char *str, int extra)
///{
///	uint8_t *storage;
///	int len = strnlen(str, 255);
///	storage = xzalloc(len + extra + OPT_DATA);
///	storage[OPT_CODE] = code;
///	storage[OPT_LEN] = len + extra;
///	memcpy(storage + extra + OPT_DATA, str, len);
///	return storage;
///}

#if BB_MMU
static void client_background(void)
{
	bb_daemonize(0);
	logmode &= ~LOGMODE_STDIO;
	/* rewrite pidfile, as our pid is different now */
	write_pidfile(client_config.pidfile);
}
#endif

//usage:#if defined CONFIG_UDHCP_DEBUG && CONFIG_UDHCP_DEBUG >= 1
//usage:# define IF_UDHCP_VERBOSE(...) __VA_ARGS__
//usage:#else
//usage:# define IF_UDHCP_VERBOSE(...)
//usage:#endif
//usage:#define udhcpc6_trivial_usage
//usage:       "[-fbnq"IF_UDHCP_VERBOSE("v")"odR] [-i IFACE] [-r IPv6] [-s PROG] [-p PIDFILE]\n"
//usage:       "	[-x OPT:VAL]... [-O OPT]..." IF_FEATURE_UDHCP_PORT(" [-P N]")
//usage:#define udhcpc6_full_usage "\n"
//usage:     "\n	-i IFACE	Interface to use (default eth0)"
//usage:     "\n	-p FILE		Create pidfile"
//usage:     "\n	-s PROG		Run PROG at DHCP events (default "CONFIG_UDHCPC_DEFAULT_SCRIPT")"
//usage:     "\n	-B		Request broadcast replies"
//usage:     "\n	-t N		Send up to N discover packets"
//usage:     "\n	-T N		Pause between packets (default 3 seconds)"
//usage:     "\n	-A N		Wait N seconds (default 20) after failure"
//usage:     "\n	-f		Run in foreground"
//usage:	USE_FOR_MMU(
//usage:     "\n	-b		Background if lease is not obtained"
//usage:	)
//usage:     "\n	-n		Exit if lease is not obtained"
//usage:     "\n	-q		Exit after obtaining lease"
//usage:     "\n	-R		Release IP on exit"
//usage:     "\n	-S		Log to syslog too"
//usage:	IF_FEATURE_UDHCP_PORT(
//usage:     "\n	-P N		Use port N (default 546)"
//usage:	)
////usage:	IF_FEATURE_UDHCPC_ARPING(
////usage:     "\n	-a		Use arping to validate offered address"
////usage:	)
//usage:     "\n	-O OPT		Request option OPT from server (cumulative)"
//usage:     "\n	-o		Don't request any options (unless -O is given)"
//usage:     "\n	-r IPv6		Request this address ('no' to not request any IP)"
//usage:     "\n	-d		Request prefix"
//usage:     "\n	-x OPT:VAL	Include option OPT in sent packets (cumulative)"
//usage:     "\n			Examples of string, numeric, and hex byte opts:"
//usage:     "\n			-x hostname:bbox - option 12"
//usage:     "\n			-x lease:3600 - option 51 (lease time)"
//usage:     "\n			-x 0x3d:0100BEEFC0FFEE - option 61 (client id)"
//usage:     "\n			-x 14:'\"dumpfile\"' - option 14 (shell-quoted)"
//usage:	IF_UDHCP_VERBOSE(
//usage:     "\n	-v		Verbose"
//usage:	)
//usage:     "\nSignals:"
//usage:     "\n	USR1	Renew lease"
//usage:     "\n	USR2	Release lease"


int udhcpc6_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int udhcpc6_main(int argc UNUSED_PARAM, char **argv)
{
	const char *str_r;
	IF_FEATURE_UDHCP_PORT(char *str_P;)
	void *clientid_mac_ptr;
	llist_t *list_O = NULL;
	llist_t *list_x = NULL;
	int tryagain_timeout = 20;
	int discover_timeout = 3;
	int discover_retries = 3;
	struct in6_addr srv6_buf;
	struct in6_addr ipv6_buf;
	struct in6_addr *requested_ipv6;
	uint32_t xid = 0;
	int packet_num;
	int timeout; /* must be signed */
	unsigned already_waited_sec;
	unsigned opt;
	int retval;

	setup_common_bufsiz();
	/* We want random_xid to be random */
	srand(monotonic_us());

	/* Default options */
	IF_FEATURE_UDHCP_PORT(SERVER_PORT6 = 547;)
	IF_FEATURE_UDHCP_PORT(CLIENT_PORT6 = 546;)
	client_config.interface = "eth0";
	client_config.script = CONFIG_UDHCPC_DEFAULT_SCRIPT;

	/* Parse command line */
	opt = getopt32long(argv, "^"
		/* O,x: list; -T,-t,-A take numeric param */
		"i:np:qRr:s:T:+t:+SA:+O:*ox:*fd"
		USE_FOR_MMU("b")
		///IF_FEATURE_UDHCPC_ARPING("a")
		IF_FEATURE_UDHCP_PORT("P:")
		"v"
		"\0" IF_UDHCP_VERBOSE("vv") /* -v is a counter */
		, udhcpc6_longopts
		, &client_config.interface, &client_config.pidfile, &str_r /* i,p */
		, &client_config.script /* s */
		, &discover_timeout, &discover_retries, &tryagain_timeout /* T,t,A */
		, &list_O
		, &list_x
		IF_FEATURE_UDHCP_PORT(, &str_P)
		IF_UDHCP_VERBOSE(, &dhcp_verbose)
	);
	requested_ipv6 = NULL;
	option_mask32 |= OPT_r;
	if (opt & OPT_r) {
		if (strcmp(str_r, "no") == 0) {
			option_mask32 -= OPT_r;
		} else {
			if (inet_pton(AF_INET6, str_r, &ipv6_buf) <= 0)
				bb_error_msg_and_die("bad IPv6 address '%s'", str_r);
			requested_ipv6 = &ipv6_buf;
		}
	}
#if ENABLE_FEATURE_UDHCP_PORT
	if (opt & OPT_P) {
		CLIENT_PORT6 = xatou16(str_P);
		SERVER_PORT6 = CLIENT_PORT6 + 1;
	}
#endif
	while (list_O) {
		char *optstr = llist_pop(&list_O);
		unsigned n = bb_strtou(optstr, NULL, 0);
		if (errno || n > 254) {
			n = udhcp_option_idx(optstr, d6_option_strings);
			n = d6_optflags[n].code;
		}
		client_config.opt_mask[n >> 3] |= 1 << (n & 7);
	}
	if (!(opt & OPT_o)) {
		unsigned i, n;
		for (i = 0; (n = d6_optflags[i].code) != 0; i++) {
			if (d6_optflags[i].flags & OPTION_REQ) {
				client_config.opt_mask[n >> 3] |= 1 << (n & 7);
			}
		}
	}
	while (list_x) {
		char *optstr = xstrdup(llist_pop(&list_x));
		udhcp_str2optset(optstr, &client_config.options,
				d6_optflags, d6_option_strings,
				/*dhcpv6:*/ 1
		);
		free(optstr);
	}

	if (d6_read_interface(client_config.interface,
			&client_config.ifindex,
			&client6_data.ll_ip6,
			client_config.client_mac)
	) {
		return 1;
	}

	/* Create client ID based on mac, set clientid_mac_ptr */
	{
		struct d6_option *clientid;
		clientid = xzalloc(2+2+2+2+6);
		clientid->code = D6_OPT_CLIENTID;
		clientid->len = 2+2+6;
		clientid->data[1] = 3; /* DUID-LL */
		clientid->data[3] = 1; /* ethernet */
		clientid_mac_ptr = clientid->data + 2+2;
		memcpy(clientid_mac_ptr, client_config.client_mac, 6);
		client_config.clientid = (void*)clientid;
	}

#if !BB_MMU
	/* on NOMMU reexec (i.e., background) early */
	if (!(opt & OPT_f)) {
		bb_daemonize_or_rexec(0 /* flags */, argv);
		logmode = LOGMODE_NONE;
	}
#endif
	if (opt & OPT_S) {
		openlog(applet_name, LOG_PID, LOG_DAEMON);
		logmode |= LOGMODE_SYSLOG;
	}

	/* Make sure fd 0,1,2 are open */
	bb_sanitize_stdio();
	/* Create pidfile */
	write_pidfile(client_config.pidfile);
	/* Goes to stdout (unless NOMMU) and possibly syslog */
	bb_error_msg("started, v"BB_VER);
	/* Set up the signal pipe */
	udhcp_sp_setup();

	state = INIT_SELECTING;
	d6_run_script_no_option("deconfig");
	change_listen_mode(LISTEN_RAW);
	packet_num = 0;
	timeout = 0;
	already_waited_sec = 0;

	/* Main event loop. select() waits on signal pipe and possibly
	 * on sockfd.
	 * "continue" statements in code below jump to the top of the loop.
	 */
	for (;;) {
		int tv;
		struct pollfd pfds[2];
		struct d6_packet packet;
		uint8_t *packet_end;
		/* silence "uninitialized!" warning */
		unsigned timestamp_before_wait = timestamp_before_wait;

		//bb_error_msg("sockfd:%d, listen_mode:%d", sockfd, listen_mode);

		/* Was opening raw or udp socket here
		 * if (listen_mode != LISTEN_NONE && sockfd < 0),
		 * but on fast network renew responses return faster
		 * than we open sockets. Thus this code is moved
		 * to change_listen_mode(). Thus we open listen socket
		 * BEFORE we send renew request (see "case BOUND:"). */

		udhcp_sp_fd_set(pfds, sockfd);

		tv = timeout - already_waited_sec;
		retval = 0;
		/* If we already timed out, fall through with retval = 0, else... */
		if (tv > 0) {
			log1("waiting %u seconds", tv);
			timestamp_before_wait = (unsigned)monotonic_sec();
			retval = poll(pfds, 2, tv < INT_MAX/1000 ? tv * 1000 : INT_MAX);
			if (retval < 0) {
				/* EINTR? A signal was caught, don't panic */
				if (errno == EINTR) {
					already_waited_sec += (unsigned)monotonic_sec() - timestamp_before_wait;
					continue;
				}
				/* Else: an error occured, panic! */
				bb_perror_msg_and_die("poll");
			}
		}

		/* If timeout dropped to zero, time to become active:
		 * resend discover/renew/whatever
		 */
		if (retval == 0) {
			/* When running on a bridge, the ifindex may have changed
			 * (e.g. if member interfaces were added/removed
			 * or if the status of the bridge changed).
			 * Refresh ifindex and client_mac:
			 */
			if (d6_read_interface(client_config.interface,
					&client_config.ifindex,
					&client6_data.ll_ip6,
					client_config.client_mac)
			) {
				goto ret0; /* iface is gone? */
			}

			memcpy(clientid_mac_ptr, client_config.client_mac, 6);

			/* We will restart the wait in any case */
			already_waited_sec = 0;

			switch (state) {
			case INIT_SELECTING:
				if (!discover_retries || packet_num < discover_retries) {
					if (packet_num == 0)
						xid = random_xid();
					/* multicast */
					send_d6_discover(xid, requested_ipv6);
					timeout = discover_timeout;
					packet_num++;
					continue;
				}
 leasefail:
				d6_run_script_no_option("leasefail");
#if BB_MMU /* -b is not supported on NOMMU */
				if (opt & OPT_b) { /* background if no lease */
					bb_error_msg("no lease, forking to background");
					client_background();
					/* do not background again! */
					opt = ((opt & ~OPT_b) | OPT_f);
				} else
#endif
				if (opt & OPT_n) { /* abort if no lease */
					bb_error_msg("no lease, failing");
					retval = 1;
					goto ret;
				}
				/* wait before trying again */
				timeout = tryagain_timeout;
				packet_num = 0;
				continue;
			case REQUESTING:
				if (!discover_retries || packet_num < discover_retries) {
					/* send multicast select packet */
					send_d6_select(xid);
					timeout = discover_timeout;
					packet_num++;
					continue;
				}
				/* Timed out, go back to init state.
				 * "discover...select...discover..." loops
				 * were seen in the wild. Treat them similarly
				 * to "no response to discover" case */
				change_listen_mode(LISTEN_RAW);
				state = INIT_SELECTING;
				goto leasefail;
			case BOUND:
				/* 1/2 lease passed, enter renewing state */
				state = RENEWING;
				client_config.first_secs = 0; /* make secs field count from 0 */
				change_listen_mode(LISTEN_KERNEL);
				log1("entering renew state");
				/* fall right through */
			case RENEW_REQUESTED: /* manual (SIGUSR1) renew */
			case_RENEW_REQUESTED:
			case RENEWING:
				if (timeout > 60) {
					/* send an unicast renew request */
			/* Sometimes observed to fail (EADDRNOTAVAIL) to bind
			 * a new UDP socket for sending inside send_renew.
			 * I hazard to guess existing listening socket
			 * is somehow conflicting with it, but why is it
			 * not deterministic then?! Strange.
			 * Anyway, it does recover by eventually failing through
			 * into INIT_SELECTING state.
			 */
					send_d6_renew(xid, &srv6_buf, requested_ipv6);
					timeout >>= 1;
					continue;
				}
				/* Timed out, enter rebinding state */
				log1("entering rebinding state");
				state = REBINDING;
				/* fall right through */
			case REBINDING:
				/* Switch to bcast receive */
				change_listen_mode(LISTEN_RAW);
				/* Lease is *really* about to run out,
				 * try to find DHCP server using broadcast */
				if (timeout > 0) {
					/* send a broadcast renew request */
					send_d6_renew(xid, /*server_ipv6:*/ NULL, requested_ipv6);
					timeout >>= 1;
					continue;
				}
				/* Timed out, enter init state */
				bb_error_msg("lease lost, entering init state");
				d6_run_script_no_option("deconfig");
				state = INIT_SELECTING;
				client_config.first_secs = 0; /* make secs field count from 0 */
				/*timeout = 0; - already is */
				packet_num = 0;
				continue;
			/* case RELEASED: */
			}
			/* yah, I know, *you* say it would never happen */
			timeout = INT_MAX;
			continue; /* back to main loop */
		} /* if poll timed out */

		/* poll() didn't timeout, something happened */

		/* Is it a signal? */
		switch (udhcp_sp_read()) {
		case SIGUSR1:
			client_config.first_secs = 0; /* make secs field count from 0 */
			already_waited_sec = 0;
			perform_renew();
			if (state == RENEW_REQUESTED) {
				/* We might be either on the same network
				 * (in which case renew might work),
				 * or we might be on a completely different one
				 * (in which case renew won't ever succeed).
				 * For the second case, must make sure timeout
				 * is not too big, or else we can send
				 * futile renew requests for hours.
				 * (Ab)use -A TIMEOUT value (usually 20 sec)
				 * as a cap on the timeout.
				 */
				if (timeout > tryagain_timeout)
					timeout = tryagain_timeout;
				goto case_RENEW_REQUESTED;
			}
			/* Start things over */
			packet_num = 0;
			/* Kill any timeouts, user wants this to hurry along */
			timeout = 0;
			continue;
		case SIGUSR2:
			perform_d6_release(&srv6_buf, requested_ipv6);
			timeout = INT_MAX;
			continue;
		case SIGTERM:
			bb_error_msg("received %s", "SIGTERM");
			goto ret0;
		}

		/* Is it a packet? */
		if (!pfds[1].revents)
			continue; /* no */

		{
			int len;

			/* A packet is ready, read it */
			if (listen_mode == LISTEN_KERNEL)
				len = d6_recv_kernel_packet(&srv6_buf, &packet, sockfd);
			else
				len = d6_recv_raw_packet(&srv6_buf, &packet, sockfd);
			if (len == -1) {
				/* Error is severe, reopen socket */
				bb_error_msg("read error: "STRERROR_FMT", reopening socket" STRERROR_ERRNO);
				sleep(discover_timeout); /* 3 seconds by default */
				change_listen_mode(listen_mode); /* just close and reopen */
			}
			/* If this packet will turn out to be unrelated/bogus,
			 * we will go back and wait for next one.
			 * Be sure timeout is properly decreased. */
			already_waited_sec += (unsigned)monotonic_sec() - timestamp_before_wait;
			if (len < 0)
				continue;
			packet_end = (uint8_t*)&packet + len;
		}

		if ((packet.d6_xid32 & htonl(0x00ffffff)) != xid) {
			log1("xid %x (our is %x), ignoring packet",
				(unsigned)(packet.d6_xid32 & htonl(0x00ffffff)), (unsigned)xid);
			continue;
		}

		switch (state) {
		case INIT_SELECTING:
			if (packet.d6_msg_type == D6_MSG_ADVERTISE)
				goto type_is_ok;
			/* DHCPv6 has "Rapid Commit", when instead of Advertise,
			 * server sends Reply right away.
			 * Fall through to check for this case.
			 */
		case REQUESTING:
		case RENEWING:
		case RENEW_REQUESTED:
		case REBINDING:
			if (packet.d6_msg_type == D6_MSG_REPLY) {
				uint32_t lease_seconds;
				struct d6_option *option;
				unsigned address_timeout;
				unsigned prefix_timeout;
 type_is_ok:
				address_timeout = 0;
				prefix_timeout = 0;
				option = d6_find_option(packet.d6_options, packet_end, D6_OPT_STATUS_CODE);
				if (option && (option->data[0] | option->data[1]) != 0) {
					/* return to init state */
					bb_error_msg("received DHCP NAK (%u)", option->data[4]);
					d6_run_script(packet.d6_options,
							packet_end, "nak");
					if (state != REQUESTING)
						d6_run_script_no_option("deconfig");
					change_listen_mode(LISTEN_RAW);
					sleep(3); /* avoid excessive network traffic */
					state = INIT_SELECTING;
					client_config.first_secs = 0; /* make secs field count from 0 */
					requested_ipv6 = NULL;
					timeout = 0;
					packet_num = 0;
					already_waited_sec = 0;
					continue;
				}
				option = d6_copy_option(packet.d6_options, packet_end, D6_OPT_SERVERID);
				if (!option) {
					bb_error_msg("no server ID, ignoring packet");
					continue;
					/* still selecting - this server looks bad */
				}
//Note: we do not bother comparing server IDs in Advertise and Reply msgs.
//server_id variable is used solely for creation of proper server_id option
//in outgoing packets. (why DHCPv6 even introduced it is a mystery).
				free(client6_data.server_id);
				client6_data.server_id = option;
				if (packet.d6_msg_type == D6_MSG_ADVERTISE) {
					/* enter requesting state */
					state = REQUESTING;
					timeout = 0;
					packet_num = 0;
					already_waited_sec = 0;
					continue;
				}
				/* It's a D6_MSG_REPLY */
/*
 * RFC 3315 18.1.8. Receipt of Reply Messages
 *
 * Upon the receipt of a valid Reply message in response to a Solicit
 * (with a Rapid Commit option), Request, Confirm, Renew, Rebind or
 * Information-request message, the client extracts the configuration
 * information contained in the Reply.  The client MAY choose to report
 * any status code or message from the status code option in the Reply
 * message.
 *
 * The client SHOULD perform duplicate address detection [17] on each of
 * the addresses in any IAs it receives in the Reply message before
 * using that address for traffic.  If any of the addresses are found to
 * be in use on the link, the client sends a Decline message to the
 * server as described in section 18.1.7.
 *
 * If the Reply was received in response to a Solicit (with a Rapid
 * Commit option), Request, Renew or Rebind message, the client updates
 * the information it has recorded about IAs from the IA options
 * contained in the Reply message:
 *
 * -  Record T1 and T2 times.
 *
 * -  Add any new addresses in the IA option to the IA as recorded by
 *    the client.
 *
 * -  Update lifetimes for any addresses in the IA option that the
 *    client already has recorded in the IA.
 *
 * -  Discard any addresses from the IA, as recorded by the client, that
 *    have a valid lifetime of 0 in the IA Address option.
 *
 * -  Leave unchanged any information about addresses the client has
 *    recorded in the IA but that were not included in the IA from the
 *    server.
 *
 * Management of the specific configuration information is detailed in
 * the definition of each option in section 22.
 *
 * If the client receives a Reply message with a Status Code containing
 * UnspecFail, the server is indicating that it was unable to process
 * the message due to an unspecified failure condition.  If the client
 * retransmits the original message to the same server to retry the
 * desired operation, the client MUST limit the rate at which it
 * retransmits the message and limit the duration of the time during
 * which it retransmits the message.
 *
 * When the client receives a Reply message with a Status Code option
 * with the value UseMulticast, the client records the receipt of the
 * message and sends subsequent messages to the server through the
 * interface on which the message was received using multicast.  The
 * client resends the original message using multicast.
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |          OPTION_IA_NA         |          option-len           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        IAID (4 octets)                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                              T1                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                              T2                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * .                         IA_NA-options                         .
 * .                                                               .
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |          OPTION_IAADDR        |          option-len           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * |                         IPv6 address                          |
 * |                                                               |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      preferred-lifetime                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        valid-lifetime                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * .                                                               .
 * .                        IAaddr-options                         .
 * .                                                               .
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
				if (option_mask32 & OPT_r) {
					struct d6_option *iaaddr;

					free(client6_data.ia_na);
					client6_data.ia_na = d6_copy_option(packet.d6_options, packet_end, D6_OPT_IA_NA);
					if (!client6_data.ia_na) {
						bb_error_msg("no %s option, ignoring packet", "IA_NA");
						continue;
					}
					if (client6_data.ia_na->len < (4 + 4 + 4) + (2 + 2 + 16 + 4 + 4)) {
						bb_error_msg("%s option is too short:%d bytes",
							"IA_NA", client6_data.ia_na->len);
						continue;
					}
					iaaddr = d6_find_option(client6_data.ia_na->data + 4 + 4 + 4,
							client6_data.ia_na->data + client6_data.ia_na->len,
							D6_OPT_IAADDR
					);
					if (!iaaddr) {
						bb_error_msg("no %s option, ignoring packet", "IAADDR");
						continue;
					}
					if (iaaddr->len < (16 + 4 + 4)) {
						bb_error_msg("%s option is too short:%d bytes",
							"IAADDR", iaaddr->len);
						continue;
					}
					/* Note: the address is sufficiently aligned for cast:
					 * we _copied_ IA-NA, and copy is always well-aligned.
					 */
					requested_ipv6 = (struct in6_addr*) iaaddr->data;
					move_from_unaligned32(lease_seconds, iaaddr->data + 16 + 4);
					lease_seconds = ntohl(lease_seconds);
/// TODO: check for 0 lease time?
					bb_error_msg("%s obtained, lease time %u",
						"IPv6", /*inet_ntoa(temp_addr),*/ (unsigned)lease_seconds);
					address_timeout = lease_seconds;
				}
				if (option_mask32 & OPT_d) {
					struct d6_option *iaprefix;

					free(client6_data.ia_pd);
					client6_data.ia_pd = d6_copy_option(packet.d6_options, packet_end, D6_OPT_IA_PD);
					if (!client6_data.ia_pd) {
						bb_error_msg("no %s option, ignoring packet", "IA_PD");
						continue;
					}
					if (client6_data.ia_pd->len < (4 + 4 + 4) + (2 + 2 + 4 + 4 + 1 + 16)) {
						bb_error_msg("%s option is too short:%d bytes",
							"IA_PD", client6_data.ia_pd->len);
						continue;
					}
					iaprefix = d6_find_option(client6_data.ia_pd->data + 4 + 4 + 4,
							client6_data.ia_pd->data + client6_data.ia_pd->len,
							D6_OPT_IAPREFIX
					);
					if (!iaprefix) {
						bb_error_msg("no %s option, ignoring packet", "IAPREFIX");
						continue;
					}
					if (iaprefix->len < (4 + 4 + 1 + 16)) {
						bb_error_msg("%s option is too short:%d bytes",
							"IAPREFIX", iaprefix->len);
						continue;
					}
					move_from_unaligned32(lease_seconds, iaprefix->data + 4);
					lease_seconds = ntohl(lease_seconds);
					bb_error_msg("%s obtained, lease time %u",
						"prefix", /*inet_ntoa(temp_addr),*/ (unsigned)lease_seconds);
					prefix_timeout = lease_seconds;
				}
				if (!address_timeout)
					address_timeout = prefix_timeout;
				if (!prefix_timeout)
					prefix_timeout = address_timeout;
				/* note: "int timeout" will not overflow even with 0xffffffff inputs here: */
				timeout = (prefix_timeout < address_timeout ? prefix_timeout : address_timeout) / 2;
				/* paranoia: must not be too small */
				if (timeout < 0x10)
					timeout = 0x10;
				/* enter bound state */
				d6_run_script(packet.d6_options, packet_end,
					(state == REQUESTING ? "bound" : "renew"));

				state = BOUND;
				change_listen_mode(LISTEN_NONE);
				if (opt & OPT_q) { /* quit after lease */
					goto ret0;
				}
				/* future renew failures should not exit (JM) */
				opt &= ~OPT_n;
#if BB_MMU /* NOMMU case backgrounded earlier */
				if (!(opt & OPT_f)) {
					client_background();
					/* do not background again! */
					opt = ((opt & ~OPT_b) | OPT_f);
				}
#endif
				already_waited_sec = 0;
				continue; /* back to main loop */
			}
			continue;
		/* case BOUND: - ignore all packets */
		/* case RELEASED: - ignore all packets */
		}
		/* back to main loop */
	} /* for (;;) - main loop ends */

 ret0:
	if (opt & OPT_R) /* release on quit */
		perform_d6_release(&srv6_buf, requested_ipv6);
	retval = 0;
 ret:
	/*if (client_config.pidfile) - remove_pidfile has its own check */
		remove_pidfile(client_config.pidfile);
	return retval;
}
