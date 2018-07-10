/* vi: set sw=4 ts=4: */
/*
 * ifup/ifdown for busybox
 * Copyright (c) 2002 Glenn McGrath
 * Copyright (c) 2003-2004 Erik Andersen <andersen@codepoet.org>
 *
 * Based on ifupdown v 0.6.4 by Anthony Towns
 * Copyright (c) 1999 Anthony Towns <aj@azure.humbug.org.au>
 *
 * Changes to upstream version
 * Remove checks for kernel version, assume kernel version 2.2.0 or better.
 * Lines in the interfaces file cannot wrap.
 * To adhere to the FHS, the default state file is /var/run/ifstate
 * (defined via CONFIG_IFUPDOWN_IFSTATE_PATH) and can be overridden by build
 * configuration.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config IFUP
//config:	bool "ifup (17 kb)"
//config:	default y
//config:	help
//config:	Activate the specified interfaces. This applet makes use
//config:	of either "ifconfig" and "route" or the "ip" command to actually
//config:	configure network interfaces. Therefore, you will probably also want
//config:	to enable either IFCONFIG and ROUTE, or enable
//config:	FEATURE_IFUPDOWN_IP and the various IP options. Of
//config:	course you could use non-busybox versions of these programs, so
//config:	against my better judgement (since this will surely result in plenty
//config:	of support questions on the mailing list), I do not force you to
//config:	enable these additional options. It is up to you to supply either
//config:	"ifconfig", "route" and "run-parts" or the "ip" command, either
//config:	via busybox or via standalone utilities.
//config:
//config:config IFDOWN
//config:	bool "ifdown (15 kb)"
//config:	default y
//config:	help
//config:	Deactivate the specified interfaces.
//config:
//config:config IFUPDOWN_IFSTATE_PATH
//config:	string "Absolute path to ifstate file"
//config:	default "/var/run/ifstate"
//config:	depends on IFUP || IFDOWN
//config:	help
//config:	ifupdown keeps state information in a file called ifstate.
//config:	Typically it is located in /var/run/ifstate, however
//config:	some distributions tend to put it in other places
//config:	(debian, for example, uses /etc/network/run/ifstate).
//config:	This config option defines location of ifstate.
//config:
//config:config FEATURE_IFUPDOWN_IP
//config:	bool "Use ip tool (else ifconfig/route is used)"
//config:	default y
//config:	depends on IFUP || IFDOWN
//config:	help
//config:	Use the iproute "ip" command to implement "ifup" and "ifdown", rather
//config:	than the default of using the older "ifconfig" and "route" utilities.
//config:
//config:	If Y: you must install either the full-blown iproute2 package
//config:	or enable "ip" applet in busybox, or the "ifup" and "ifdown" applets
//config:	will not work.
//config:
//config:	If N: you must install either the full-blown ifconfig and route
//config:	utilities, or enable these applets in busybox.
//config:
//config:config FEATURE_IFUPDOWN_IPV4
//config:	bool "Support IPv4"
//config:	default y
//config:	depends on IFUP || IFDOWN
//config:	help
//config:	If you want ifup/ifdown to talk IPv4, leave this on.
//config:
//config:config FEATURE_IFUPDOWN_IPV6
//config:	bool "Support IPv6"
//config:	default y
//config:	depends on (IFUP || IFDOWN) && FEATURE_IPV6
//config:	help
//config:	If you need support for IPv6, turn this option on.
//config:
//UNUSED:
////////:config FEATURE_IFUPDOWN_IPX
////////:	bool "Support IPX"
////////:	default y
////////:	depends on IFUP || IFDOWN
////////:	help
////////:	  If this option is selected you can use busybox to work with IPX
////////:	  networks.
//config:
//config:config FEATURE_IFUPDOWN_MAPPING
//config:	bool "Enable mapping support"
//config:	default y
//config:	depends on IFUP || IFDOWN
//config:	help
//config:	This enables support for the "mapping" stanza, unless you have
//config:	a weird network setup you don't need it.
//config:
//config:config FEATURE_IFUPDOWN_EXTERNAL_DHCP
//config:	bool "Support external DHCP clients"
//config:	default n
//config:	depends on IFUP || IFDOWN
//config:	help
//config:	This enables support for the external dhcp clients. Clients are
//config:	tried in the following order: dhcpcd, dhclient, pump and udhcpc.
//config:	Otherwise, if udhcpc applet is enabled, it is used.
//config:	Otherwise, ifup/ifdown will have no support for DHCP.

//                 APPLET_ODDNAME:name    main      location     suid_type     help
//applet:IF_IFUP(  APPLET_ODDNAME(ifup,   ifupdown, BB_DIR_SBIN, BB_SUID_DROP, ifup))
//applet:IF_IFDOWN(APPLET_ODDNAME(ifdown, ifupdown, BB_DIR_SBIN, BB_SUID_DROP, ifdown))

//kbuild:lib-$(CONFIG_IFUP) += ifupdown.o
//kbuild:lib-$(CONFIG_IFDOWN) += ifupdown.o

//usage:#define ifup_trivial_usage
//usage:       "[-an"IF_FEATURE_IFUPDOWN_MAPPING("m")"vf] [-i FILE] IFACE..."
//usage:#define ifup_full_usage "\n\n"
//usage:       "	-a	Configure all interfaces"
//usage:     "\n	-i FILE	Use FILE instead of /etc/network/interfaces"
//usage:     "\n	-n	Print out what would happen, but don't do it"
//usage:	IF_FEATURE_IFUPDOWN_MAPPING(
//usage:     "\n		(note: doesn't disable mappings)"
//usage:     "\n	-m	Don't run any mappings"
//usage:	)
//usage:     "\n	-v	Print out what would happen before doing it"
//usage:     "\n	-f	Force configuration"
//usage:
//usage:#define ifdown_trivial_usage
//usage:       "[-an"IF_FEATURE_IFUPDOWN_MAPPING("m")"vf] [-i FILE] IFACE..."
//usage:#define ifdown_full_usage "\n\n"
//usage:       "	-a	Deconfigure all interfaces"
//usage:     "\n	-i FILE	Use FILE for interface definitions"
//usage:     "\n	-n	Print out what would happen, but don't do it"
//usage:	IF_FEATURE_IFUPDOWN_MAPPING(
//usage:     "\n		(note: doesn't disable mappings)"
//usage:     "\n	-m	Don't run any mappings"
//usage:	)
//usage:     "\n	-v	Print out what would happen before doing it"
//usage:     "\n	-f	Force deconfiguration"

#include <net/if.h>
#include "libbb.h"
#include "common_bufsiz.h"
/* After libbb.h, since it needs sys/types.h on some systems */
#include <sys/utsname.h>
#include <fnmatch.h>

#define MAX_OPT_DEPTH 10

#if ENABLE_FEATURE_IFUPDOWN_MAPPING
#define MAX_INTERFACE_LENGTH 10
#endif

#define UDHCPC_CMD_OPTIONS CONFIG_IFUPDOWN_UDHCPC_CMD_OPTIONS
#define IFSTATE_FILE_PATH  CONFIG_IFUPDOWN_IFSTATE_PATH

#define debug_noise(args...) /*fprintf(stderr, args)*/

/* Forward declaration */
struct interface_defn_t;

typedef int execfn(char *command);

struct method_t {
	const char *name;
	int (*up)(struct interface_defn_t *ifd, execfn *e) FAST_FUNC;
	int (*down)(struct interface_defn_t *ifd, execfn *e) FAST_FUNC;
};

struct address_family_t {
	const char *name;
	int n_methods;
	const struct method_t *method;
};

struct mapping_defn_t {
	struct mapping_defn_t *next;

	int max_matches;
	int n_matches;
	char **match;

	char *script;

	int n_mappings;
	char **mapping;
};

struct variable_t {
	char *name;
	char *value;
};

struct interface_defn_t {
	const struct address_family_t *address_family;
	const struct method_t *method;

	char *iface;
	int n_options;
	struct variable_t *option;
};

struct interfaces_file_t {
	llist_t *autointerfaces;
	llist_t *ifaces;
	struct mapping_defn_t *mappings;
};


#define OPTION_STR "anvf" IF_FEATURE_IFUPDOWN_MAPPING("m") "i:"
enum {
	OPT_do_all      = 0x1,
	OPT_no_act      = 0x2,
	OPT_verbose     = 0x4,
	OPT_force       = 0x8,
	OPT_no_mappings = 0x10,
};
#define DO_ALL      (option_mask32 & OPT_do_all)
#define NO_ACT      (option_mask32 & OPT_no_act)
#define VERBOSE     (option_mask32 & OPT_verbose)
#define FORCE       (option_mask32 & OPT_force)
#define NO_MAPPINGS (option_mask32 & OPT_no_mappings)


struct globals {
	char **my_environ;
	const char *startup_PATH;
	char *shell;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)


static const char keywords_up_down[] ALIGN1 =
	"up\0"
	"down\0"
	"pre-up\0"
	"post-down\0"
;


#if ENABLE_FEATURE_IFUPDOWN_IPV4 || ENABLE_FEATURE_IFUPDOWN_IPV6

static void addstr(char **bufp, const char *str, size_t str_length)
{
	/* xasprintf trick will be smaller, but we are often
	 * called with str_length == 1 - don't want to have
	 * THAT much of malloc/freeing! */
	char *buf = *bufp;
	int len = (buf ? strlen(buf) : 0);
	str_length++;
	buf = xrealloc(buf, len + str_length);
	/* copies at most str_length-1 chars! */
	safe_strncpy(buf + len, str, str_length);
	*bufp = buf;
}

static int strncmpz(const char *l, const char *r, size_t llen)
{
	int i = strncmp(l, r, llen);

	if (i == 0)
		return - (unsigned char)r[llen];
	return i;
}

static char *get_var(const char *id, size_t idlen, struct interface_defn_t *ifd)
{
	int i;

	if (strncmpz(id, "iface", idlen) == 0) {
		// ubuntu's ifup doesn't do this:
		//static char *label_buf;
		//char *result;
		//free(label_buf);
		//label_buf = xstrdup(ifd->iface);
		// Remove virtual iface suffix
		//result = strchrnul(label_buf, ':');
		//*result = '\0';
		//return label_buf;

		return ifd->iface;
	}
	if (strncmpz(id, "label", idlen) == 0) {
		return ifd->iface;
	}
	for (i = 0; i < ifd->n_options; i++) {
		if (strncmpz(id, ifd->option[i].name, idlen) == 0) {
			return ifd->option[i].value;
		}
	}
	return NULL;
}

# if ENABLE_FEATURE_IFUPDOWN_IP
static int count_netmask_bits(const char *dotted_quad)
{
//	int result;
//	unsigned a, b, c, d;
//	/* Found a netmask...  Check if it is dotted quad */
//	if (sscanf(dotted_quad, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
//		return -1;
//	if ((a|b|c|d) >> 8)
//		return -1; /* one of numbers is >= 256 */
//	d |= (a << 24) | (b << 16) | (c << 8); /* IP */
//	d = ~d; /* 11110000 -> 00001111 */

	/* Shorter version */
	int result;
	struct in_addr ip;
	unsigned d;

	if (inet_aton(dotted_quad, &ip) == 0)
		return -1; /* malformed dotted IP */
	d = ntohl(ip.s_addr); /* IP in host order */
	d = ~d; /* 11110000 -> 00001111 */
	if (d & (d+1)) /* check that it is in 00001111 form */
		return -1; /* no it is not */
	result = 32;
	while (d) {
		d >>= 1;
		result--;
	}
	return result;
}
# endif

static char *parse(const char *command, struct interface_defn_t *ifd)
{
	size_t old_pos[MAX_OPT_DEPTH] = { 0 };
	smallint okay[MAX_OPT_DEPTH] = { 1 };
	int opt_depth = 1;
	char *result = NULL;

	while (*command) {
		switch (*command) {
		default:
			addstr(&result, command, 1);
			command++;
			break;
		case '\\':
			if (command[1])
				command++;
			addstr(&result, command, 1);
			command++;
			break;
		case '[':
			if (command[1] == '[' && opt_depth < MAX_OPT_DEPTH) {
				old_pos[opt_depth] = result ? strlen(result) : 0;
				okay[opt_depth] = 1;
				opt_depth++;
				command += 2;
			} else {
				addstr(&result, command, 1);
				command++;
			}
			break;
		case ']':
			if (command[1] == ']' && opt_depth > 1) {
				opt_depth--;
				if (!okay[opt_depth]) {
					result[old_pos[opt_depth]] = '\0';
				}
				command += 2;
			} else {
				addstr(&result, command, 1);
				command++;
			}
			break;
		case '%':
			{
				char *nextpercent;
				char *varvalue;

				command++;
				nextpercent = strchr(command, '%');
				if (!nextpercent) {
					/* Unterminated %var% */
					free(result);
					return NULL;
				}

				varvalue = get_var(command, nextpercent - command, ifd);

				if (varvalue) {
# if ENABLE_FEATURE_IFUPDOWN_IP
					/* "hwaddress <class> <address>":
					 * unlike ifconfig, ip doesnt want <class>
					 * (usually "ether" keyword). Skip it. */
					if (is_prefixed_with(command, "hwaddress")) {
						varvalue = skip_whitespace(skip_non_whitespace(varvalue));
					}
# endif
					addstr(&result, varvalue, strlen(varvalue));
				} else {
# if ENABLE_FEATURE_IFUPDOWN_IP
					/* Sigh...  Add a special case for 'ip' to convert from
					 * dotted quad to bit count style netmasks.  */
					if (is_prefixed_with(command, "bnmask")) {
						unsigned res;
						varvalue = get_var("netmask", 7, ifd);
						if (varvalue) {
							res = count_netmask_bits(varvalue);
							if (res > 0) {
								const char *argument = utoa(res);
								addstr(&result, argument, strlen(argument));
								command = nextpercent + 1;
								break;
							}
						}
					}
# endif
					okay[opt_depth - 1] = 0;
				}

				command = nextpercent + 1;
			}
			break;
		}
	}

	if (opt_depth > 1) {
		/* Unbalanced bracket */
		free(result);
		return NULL;
	}

	if (!okay[0]) {
		/* Undefined variable and we aren't in a bracket */
		free(result);
		return NULL;
	}

	return result;
}

/* execute() returns 1 for success and 0 for failure */
static int execute(const char *command, struct interface_defn_t *ifd, execfn *exec)
{
	char *out;
	int ret;

	out = parse(command, ifd);
	if (!out) {
		/* parse error? */
		return 0;
	}
	/* out == "": parsed ok but not all needed variables known, skip */
	ret = out[0] ? (*exec)(out) : 1;

	free(out);
	if (ret != 1) {
		return 0;
	}
	return 1;
}

#endif /* FEATURE_IFUPDOWN_IPV4 || FEATURE_IFUPDOWN_IPV6 */


#if ENABLE_FEATURE_IFUPDOWN_IPV6

static int FAST_FUNC loopback_up6(struct interface_defn_t *ifd, execfn *exec)
{
# if ENABLE_FEATURE_IFUPDOWN_IP
	int result;
	result = execute("ip addr add ::1 dev %iface%", ifd, exec);
	result += execute("ip link set %iface% up", ifd, exec);
	return ((result == 2) ? 2 : 0);
# else
	return execute("ifconfig %iface% add ::1", ifd, exec);
# endif
}

static int FAST_FUNC loopback_down6(struct interface_defn_t *ifd, execfn *exec)
{
# if ENABLE_FEATURE_IFUPDOWN_IP
	return execute("ip link set %iface% down", ifd, exec);
# else
	return execute("ifconfig %iface% del ::1", ifd, exec);
# endif
}

static int FAST_FUNC manual_up_down6(struct interface_defn_t *ifd UNUSED_PARAM, execfn *exec UNUSED_PARAM)
{
	return 1;
}

static int FAST_FUNC static_up6(struct interface_defn_t *ifd, execfn *exec)
{
	int result;
# if ENABLE_FEATURE_IFUPDOWN_IP
	result = execute("ip addr add %address%/%netmask% dev %iface%[[ label %label%]]", ifd, exec);
	result += execute("ip link set[[ mtu %mtu%]][[ addr %hwaddress%]] %iface% up", ifd, exec);
	/* Reportedly, IPv6 needs "dev %iface%", but IPv4 does not: */
	result += execute("[[ip route add ::/0 via %gateway% dev %iface%]][[ metric %metric%]]", ifd, exec);
# else
	result = execute("ifconfig %iface%[[ media %media%]][[ hw %hwaddress%]][[ mtu %mtu%]] up", ifd, exec);
	result += execute("ifconfig %iface% add %address%/%netmask%", ifd, exec);
	result += execute("[[route -A inet6 add ::/0 gw %gateway%[[ metric %metric%]]]]", ifd, exec);
# endif
	return ((result == 3) ? 3 : 0);
}

static int FAST_FUNC static_down6(struct interface_defn_t *ifd, execfn *exec)
{
	if (!if_nametoindex(ifd->iface))
		return 1; /* already gone */
# if ENABLE_FEATURE_IFUPDOWN_IP
	return execute("ip link set %iface% down", ifd, exec);
# else
	return execute("ifconfig %iface% down", ifd, exec);
# endif
}

# if ENABLE_FEATURE_IFUPDOWN_IP
static int FAST_FUNC v4tunnel_up(struct interface_defn_t *ifd, execfn *exec)
{
	int result;
	result = execute("ip tunnel add %iface% mode sit remote "
			"%endpoint%[[ local %local%]][[ ttl %ttl%]]", ifd, exec);
	result += execute("ip link set %iface% up", ifd, exec);
	result += execute("ip addr add %address%/%netmask% dev %iface%", ifd, exec);
	/* Reportedly, IPv6 needs "dev %iface%", but IPv4 does not: */
	result += execute("[[ip route add ::/0 via %gateway% dev %iface%]]", ifd, exec);
	return ((result == 4) ? 4 : 0);
}

static int FAST_FUNC v4tunnel_down(struct interface_defn_t * ifd, execfn * exec)
{
	return execute("ip tunnel del %iface%", ifd, exec);
}
# endif

static const struct method_t methods6[] = {
# if ENABLE_FEATURE_IFUPDOWN_IP
	{ "v4tunnel" , v4tunnel_up     , v4tunnel_down   , },
# endif
	{ "static"   , static_up6      , static_down6    , },
	{ "manual"   , manual_up_down6 , manual_up_down6 , },
	{ "loopback" , loopback_up6    , loopback_down6  , },
};

static const struct address_family_t addr_inet6 = {
	"inet6",
	ARRAY_SIZE(methods6),
	methods6
};

#endif /* FEATURE_IFUPDOWN_IPV6 */


#if ENABLE_FEATURE_IFUPDOWN_IPV4

static int FAST_FUNC loopback_up(struct interface_defn_t *ifd, execfn *exec)
{
# if ENABLE_FEATURE_IFUPDOWN_IP
	int result;
	result = execute("ip addr add 127.0.0.1/8 dev %iface%", ifd, exec);
	result += execute("ip link set %iface% up", ifd, exec);
	return ((result == 2) ? 2 : 0);
# else
	return execute("ifconfig %iface% 127.0.0.1 up", ifd, exec);
# endif
}

static int FAST_FUNC loopback_down(struct interface_defn_t *ifd, execfn *exec)
{
# if ENABLE_FEATURE_IFUPDOWN_IP
	int result;
	result = execute("ip addr flush dev %iface%", ifd, exec);
	result += execute("ip link set %iface% down", ifd, exec);
	return ((result == 2) ? 2 : 0);
# else
	return execute("ifconfig %iface% 127.0.0.1 down", ifd, exec);
# endif
}

static int FAST_FUNC static_up(struct interface_defn_t *ifd, execfn *exec)
{
	int result;
# if ENABLE_FEATURE_IFUPDOWN_IP
	result = execute("ip addr add %address%/%bnmask%[[ broadcast %broadcast%]] "
			"dev %iface%[[ peer %pointopoint%]][[ label %label%]]", ifd, exec);
	result += execute("ip link set[[ mtu %mtu%]][[ addr %hwaddress%]] %iface% up", ifd, exec);
	result += execute("[[ip route add default via %gateway% dev %iface%[[ metric %metric%]]]]", ifd, exec);
	return ((result == 3) ? 3 : 0);
# else
	/* ifconfig said to set iface up before it processes hw %hwaddress%,
	 * which then of course fails. Thus we run two separate ifconfig */
	result = execute("ifconfig %iface%[[ hw %hwaddress%]][[ media %media%]][[ mtu %mtu%]] up",
				ifd, exec);
	result += execute("ifconfig %iface% %address% netmask %netmask%"
				"[[ broadcast %broadcast%]][[ pointopoint %pointopoint%]]",
				ifd, exec);
	result += execute("[[route add default gw %gateway%[[ metric %metric%]] %iface%]]", ifd, exec);
	return ((result == 3) ? 3 : 0);
# endif
}

static int FAST_FUNC static_down(struct interface_defn_t *ifd, execfn *exec)
{
	int result;

	if (!if_nametoindex(ifd->iface))
		return 2; /* already gone */
# if ENABLE_FEATURE_IFUPDOWN_IP
	/* Optional "label LBL" is necessary if interface is an alias (eth0:0),
	 * otherwise "ip addr flush dev eth0:0" flushes all addresses on eth0.
	 */
	result = execute("ip addr flush dev %iface%[[ label %label%]]", ifd, exec);
	result += execute("ip link set %iface% down", ifd, exec);
# else
	/* result = execute("[[route del default gw %gateway% %iface%]]", ifd, exec); */
	/* Bringing the interface down deletes the routes in itself.
	   Otherwise this fails if we reference 'gateway' when using this from dhcp_down */
	result = 1;
	result += execute("ifconfig %iface% down", ifd, exec);
# endif
	return ((result == 2) ? 2 : 0);
}

# if ENABLE_FEATURE_IFUPDOWN_EXTERNAL_DHCP
struct dhcp_client_t {
	const char *name;
	const char *startcmd;
	const char *stopcmd;
};

static const struct dhcp_client_t ext_dhcp_clients[] = {
	{ "dhcpcd",
		"dhcpcd[[ -h %hostname%]][[ -i %vendor%]][[ -I %client%]][[ -l %leasetime%]] %iface%",
		"dhcpcd -k %iface%",
	},
	{ "dhclient",
		"dhclient -pf /var/run/dhclient.%iface%.pid %iface%",
		"kill -9 `cat /var/run/dhclient.%iface%.pid` 2>/dev/null",
	},
	{ "pump",
		"pump -i %iface%[[ -h %hostname%]][[ -l %leasehours%]]",
		"pump -i %iface% -k",
	},
	{ "udhcpc",
		"udhcpc " UDHCPC_CMD_OPTIONS " -p /var/run/udhcpc.%iface%.pid -i %iface%[[ -x hostname:%hostname%]][[ -c %client%]]"
				"[[ -s %script%]][[ %udhcpc_opts%]]",
		"kill `cat /var/run/udhcpc.%iface%.pid` 2>/dev/null",
	},
};
# endif /* FEATURE_IFUPDOWN_EXTERNAL_DHCPC */

# if ENABLE_FEATURE_IFUPDOWN_EXTERNAL_DHCP
static int FAST_FUNC dhcp_up(struct interface_defn_t *ifd, execfn *exec)
{
	unsigned i;
#  if ENABLE_FEATURE_IFUPDOWN_IP
	/* ip doesn't up iface when it configures it (unlike ifconfig) */
	if (!execute("ip link set[[ addr %hwaddress%]] %iface% up", ifd, exec))
		return 0;
#  else
	/* needed if we have hwaddress on dhcp iface */
	if (!execute("ifconfig %iface%[[ hw %hwaddress%]] up", ifd, exec))
		return 0;
#  endif
	for (i = 0; i < ARRAY_SIZE(ext_dhcp_clients); i++) {
		if (executable_exists(ext_dhcp_clients[i].name))
			return execute(ext_dhcp_clients[i].startcmd, ifd, exec);
	}
	bb_error_msg("no dhcp clients found");
	return 0;
}
# elif ENABLE_UDHCPC
static int FAST_FUNC dhcp_up(struct interface_defn_t *ifd, execfn *exec)
{
#  if ENABLE_FEATURE_IFUPDOWN_IP
	/* ip doesn't up iface when it configures it (unlike ifconfig) */
	if (!execute("ip link set[[ addr %hwaddress%]] %iface% up", ifd, exec))
		return 0;
#  else
	/* needed if we have hwaddress on dhcp iface */
	if (!execute("ifconfig %iface%[[ hw %hwaddress%]] up", ifd, exec))
		return 0;
#  endif
	return execute("udhcpc " UDHCPC_CMD_OPTIONS " -p /var/run/udhcpc.%iface%.pid "
			"-i %iface%[[ -x hostname:%hostname%]][[ -c %client%]][[ -s %script%]][[ %udhcpc_opts%]]",
			ifd, exec);
}
# else
static int FAST_FUNC dhcp_up(struct interface_defn_t *ifd UNUSED_PARAM,
		execfn *exec UNUSED_PARAM)
{
	return 0; /* no dhcp support */
}
# endif

# if ENABLE_FEATURE_IFUPDOWN_EXTERNAL_DHCP
static int FAST_FUNC dhcp_down(struct interface_defn_t *ifd, execfn *exec)
{
	int result = 0;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(ext_dhcp_clients); i++) {
		if (executable_exists(ext_dhcp_clients[i].name)) {
			result = execute(ext_dhcp_clients[i].stopcmd, ifd, exec);
			if (result)
				break;
		}
	}

	if (!result)
		bb_error_msg("warning: no dhcp clients found and stopped");

	/* Sleep a bit, otherwise static_down tries to bring down interface too soon,
	   and it may come back up because udhcpc is still shutting down */
	usleep(100000);
	result += static_down(ifd, exec);
	return ((result == 3) ? 3 : 0);
}
# elif ENABLE_UDHCPC
static int FAST_FUNC dhcp_down(struct interface_defn_t *ifd, execfn *exec)
{
	int result;
	result = execute(
		"test -f /var/run/udhcpc.%iface%.pid && "
		"kill `cat /var/run/udhcpc.%iface%.pid` 2>/dev/null",
		ifd, exec);
	/* Also bring the hardware interface down since
	   killing the dhcp client alone doesn't do it.
	   This enables consecutive ifup->ifdown->ifup */
	/* Sleep a bit, otherwise static_down tries to bring down interface too soon,
	   and it may come back up because udhcpc is still shutting down */
	usleep(100000);
	result += static_down(ifd, exec);
	return ((result == 3) ? 3 : 0);
}
# else
static int FAST_FUNC dhcp_down(struct interface_defn_t *ifd UNUSED_PARAM,
		execfn *exec UNUSED_PARAM)
{
	return 0; /* no dhcp support */
}
# endif

static int FAST_FUNC manual_up_down(struct interface_defn_t *ifd UNUSED_PARAM, execfn *exec UNUSED_PARAM)
{
	return 1;
}

static int FAST_FUNC bootp_up(struct interface_defn_t *ifd, execfn *exec)
{
	return execute("bootpc[[ --bootfile %bootfile%]] --dev %iface%"
			"[[ --server %server%]][[ --hwaddr %hwaddr%]]"
			" --returniffail --serverbcast", ifd, exec);
}

static int FAST_FUNC ppp_up(struct interface_defn_t *ifd, execfn *exec)
{
	return execute("pon[[ %provider%]]", ifd, exec);
}

static int FAST_FUNC ppp_down(struct interface_defn_t *ifd, execfn *exec)
{
	return execute("poff[[ %provider%]]", ifd, exec);
}

static int FAST_FUNC wvdial_up(struct interface_defn_t *ifd, execfn *exec)
{
	return execute("start-stop-daemon --start -x wvdial "
		"-p /var/run/wvdial.%iface% -b -m --[[ %provider%]]", ifd, exec);
}

static int FAST_FUNC wvdial_down(struct interface_defn_t *ifd, execfn *exec)
{
	return execute("start-stop-daemon --stop -x wvdial "
			"-p /var/run/wvdial.%iface% -s 2", ifd, exec);
}

static const struct method_t methods[] = {
	{ "manual"  , manual_up_down, manual_up_down, },
	{ "wvdial"  , wvdial_up     , wvdial_down   , },
	{ "ppp"     , ppp_up        , ppp_down      , },
	{ "static"  , static_up     , static_down   , },
	{ "bootp"   , bootp_up      , static_down   , },
	{ "dhcp"    , dhcp_up       , dhcp_down     , },
	{ "loopback", loopback_up   , loopback_down , },
};

static const struct address_family_t addr_inet = {
	"inet",
	ARRAY_SIZE(methods),
	methods
};

#endif  /* FEATURE_IFUPDOWN_IPV4 */

static int FAST_FUNC link_up_down(struct interface_defn_t *ifd UNUSED_PARAM, execfn *exec UNUSED_PARAM)
{
	return 1;
}

static const struct method_t link_methods[] = {
	{ "none", link_up_down, link_up_down }
};

static const struct address_family_t addr_link = {
	"link", ARRAY_SIZE(link_methods), link_methods
};

/* Returns pointer to the next word, or NULL.
 * In 1st case, advances *buf to the word after this one.
 */
static char *next_word(char **buf)
{
	unsigned length;
	char *word;

	/* Skip over leading whitespace */
	word = skip_whitespace(*buf);

	/* Stop on EOL */
	if (*word == '\0')
		return NULL;

	/* Find the length of this word (can't be 0) */
	length = strcspn(word, " \t\n");

	/* Unless we are already at NUL, store NUL and advance */
	if (word[length] != '\0')
		word[length++] = '\0';

	*buf = skip_whitespace(word + length);

	return word;
}

static const struct address_family_t *get_address_family(const struct address_family_t *const af[], char *name)
{
	int i;

	if (!name)
		return NULL;

	for (i = 0; af[i]; i++) {
		if (strcmp(af[i]->name, name) == 0) {
			return af[i];
		}
	}
	return NULL;
}

static const struct method_t *get_method(const struct address_family_t *af, char *name)
{
	int i;

	if (!name)
		return NULL;
	/* TODO: use index_in_str_array() */
	for (i = 0; i < af->n_methods; i++) {
		if (strcmp(af->method[i].name, name) == 0) {
			return &af->method[i];
		}
	}
	return NULL;
}

static struct interfaces_file_t *read_interfaces(const char *filename, struct interfaces_file_t *defn)
{
	/* Let's try to be compatible.
	 *
	 * "man 5 interfaces" says:
	 * Lines starting with "#" are ignored. Note that end-of-line
	 * comments are NOT supported, comments must be on a line of their own.
	 * A line may be extended across multiple lines by making
	 * the last character a backslash.
	 *
	 * Seen elsewhere in example config file:
	 * A first non-blank "#" character makes the rest of the line
	 * be ignored. Blank lines are ignored. Lines may be indented freely.
	 * A "\" character at the very end of the line indicates the next line
	 * should be treated as a continuation of the current one.
	 *
	 * Lines  beginning with "source" are used to include stanzas from
	 * other files, so configuration can be split into many files.
	 * The word "source" is followed by the path of file to be sourced.
	 */
#if ENABLE_FEATURE_IFUPDOWN_MAPPING
	struct mapping_defn_t *currmap = NULL;
#endif
	struct interface_defn_t *currif = NULL;
	FILE *f;
	char *buf;
	char *first_word;
	char *rest_of_line;
	enum { NONE, IFACE, MAPPING } currently_processing = NONE;

	if (!defn)
		defn = xzalloc(sizeof(*defn));

	debug_noise("reading %s file:\n", filename);
	f = xfopen_for_read(filename);

	while ((buf = xmalloc_fgetline(f)) != NULL) {
#if ENABLE_DESKTOP
		/* Trailing "\" concatenates lines */
		char *p;
		while ((p = last_char_is(buf, '\\')) != NULL) {
			*p = '\0';
			rest_of_line = xmalloc_fgetline(f);
			if (!rest_of_line)
				break;
			p = xasprintf("%s%s", buf, rest_of_line);
			free(buf);
			free(rest_of_line);
			buf = p;
		}
#endif
		rest_of_line = buf;
		first_word = next_word(&rest_of_line);
		if (!first_word || *first_word == '#') {
			free(buf);
			continue; /* blank/comment line */
		}

		if (strcmp(first_word, "mapping") == 0) {
#if ENABLE_FEATURE_IFUPDOWN_MAPPING
			currmap = xzalloc(sizeof(*currmap));

			while ((first_word = next_word(&rest_of_line)) != NULL) {
				currmap->match = xrealloc_vector(currmap->match, 4, currmap->n_matches);
				currmap->match[currmap->n_matches++] = xstrdup(first_word);
			}
			/*currmap->n_mappings = 0;*/
			/*currmap->mapping = NULL;*/
			/*currmap->script = NULL;*/
			{
				struct mapping_defn_t **where = &defn->mappings;
				while (*where != NULL) {
					where = &(*where)->next;
				}
				*where = currmap;
				/*currmap->next = NULL;*/
			}
			debug_noise("Added mapping\n");
#endif
			currently_processing = MAPPING;
		} else if (strcmp(first_word, "iface") == 0) {
			static const struct address_family_t *const addr_fams[] = {
#if ENABLE_FEATURE_IFUPDOWN_IPV4
				&addr_inet,
#endif
#if ENABLE_FEATURE_IFUPDOWN_IPV6
				&addr_inet6,
#endif
				&addr_link,
				NULL
			};
			char *iface_name;
			char *address_family_name;
			char *method_name;

			currif = xzalloc(sizeof(*currif));
			iface_name = next_word(&rest_of_line);
			address_family_name = next_word(&rest_of_line);
			method_name = next_word(&rest_of_line);

			if (method_name == NULL)
				bb_error_msg_and_die("too few parameters for line \"%s\"", buf);

			/* ship any trailing whitespace */
			rest_of_line = skip_whitespace(rest_of_line);

			if (rest_of_line[0] != '\0' /* && rest_of_line[0] != '#' */)
				bb_error_msg_and_die("too many parameters \"%s\"", buf);

			currif->iface = xstrdup(iface_name);

			currif->address_family = get_address_family(addr_fams, address_family_name);
			if (!currif->address_family)
				bb_error_msg_and_die("unknown address type \"%s\"", address_family_name);

			currif->method = get_method(currif->address_family, method_name);
			if (!currif->method)
				bb_error_msg_and_die("unknown method \"%s\"", method_name);
#if 0
// Allegedly, Debian allows a duplicate definition:
// iface eth0 inet static
//     address 192.168.0.15
//     netmask 255.255.0.0
//     gateway 192.168.0.1
//
// iface eth0 inet static
//     address 10.0.0.1
//     netmask 255.255.255.0
//
// This adds *two* addresses to eth0 (probably requires use of "ip", not "ifconfig"
//
			llist_t *iface_list;
			for (iface_list = defn->ifaces; iface_list; iface_list = iface_list->link) {
				struct interface_defn_t *tmp = (struct interface_defn_t *) iface_list->data;
				if ((strcmp(tmp->iface, currif->iface) == 0)
				 && (tmp->address_family == currif->address_family)
				) {
					bb_error_msg_and_die("duplicate interface \"%s\"", tmp->iface);
				}
			}
#endif
			llist_add_to_end(&(defn->ifaces), (char*)currif);

			debug_noise("iface %s %s %s\n", currif->iface, address_family_name, method_name);
			currently_processing = IFACE;
		} else if (strcmp(first_word, "auto") == 0) {
			while ((first_word = next_word(&rest_of_line)) != NULL) {

				/* Check the interface isnt already listed */
				if (llist_find_str(defn->autointerfaces, first_word)) {
					bb_perror_msg_and_die("interface declared auto twice \"%s\"", buf);
				}

				/* Add the interface to the list */
				llist_add_to_end(&(defn->autointerfaces), xstrdup(first_word));
				debug_noise("\nauto %s\n", first_word);
			}
			currently_processing = NONE;
		} else if (strcmp(first_word, "source") == 0) {
			read_interfaces(next_word(&rest_of_line), defn);
		} else {
			switch (currently_processing) {
			case IFACE:
				if (rest_of_line[0] == '\0')
					bb_error_msg_and_die("option with empty value \"%s\"", buf);

				if (strcmp(first_word, "post-up") == 0)
					first_word += 5; /* "up" */
				else if (strcmp(first_word, "pre-down") == 0)
					first_word += 4; /* "down" */

				/* If not one of "up", "down",... words... */
				if (index_in_strings(keywords_up_down, first_word) < 0) {
					int i;
					for (i = 0; i < currif->n_options; i++) {
						if (strcmp(currif->option[i].name, first_word) == 0)
							bb_error_msg_and_die("duplicate option \"%s\"", buf);
					}
				}
				debug_noise("\t%s=%s\n", first_word, rest_of_line);
				currif->option = xrealloc_vector(currif->option, 4, currif->n_options);
				currif->option[currif->n_options].name = xstrdup(first_word);
				currif->option[currif->n_options].value = xstrdup(rest_of_line);
				currif->n_options++;
				break;
			case MAPPING:
#if ENABLE_FEATURE_IFUPDOWN_MAPPING
				if (strcmp(first_word, "script") == 0) {
					if (currmap->script != NULL)
						bb_error_msg_and_die("duplicate script in mapping \"%s\"", buf);
					currmap->script = xstrdup(next_word(&rest_of_line));
				} else if (strcmp(first_word, "map") == 0) {
					currmap->mapping = xrealloc_vector(currmap->mapping, 2, currmap->n_mappings);
					currmap->mapping[currmap->n_mappings] = xstrdup(next_word(&rest_of_line));
					currmap->n_mappings++;
				} else {
					bb_error_msg_and_die("misplaced option \"%s\"", buf);
				}
#endif
				break;
			case NONE:
			default:
				bb_error_msg_and_die("misplaced option \"%s\"", buf);
			}
		}
		free(buf);
	} /* while (fgets) */

	if (ferror(f) != 0) {
		/* ferror does NOT set errno! */
		bb_error_msg_and_die("%s: I/O error", filename);
	}
	fclose(f);
	debug_noise("\ndone reading %s\n\n", filename);

	return defn;
}

static char *setlocalenv(const char *format, const char *name, const char *value)
{
	char *result;
	char *dst;
	char *src;
	char c;

	result = xasprintf(format, name, value);

	for (dst = src = result; (c = *src) != '=' && c; src++) {
		if (c == '-')
			c = '_';
		if (c >= 'a' && c <= 'z')
			c -= ('a' - 'A');
		if (isalnum(c) || c == '_')
			*dst++ = c;
	}
	overlapping_strcpy(dst, src);

	return result;
}

static void set_environ(struct interface_defn_t *iface, const char *mode, const char *opt)
{
	int i;
	char **pp;

	if (G.my_environ != NULL) {
		for (pp = G.my_environ; *pp; pp++) {
			free(*pp);
		}
		free(G.my_environ);
	}

	/* note: last element will stay NULL: */
	G.my_environ = xzalloc(sizeof(char *) * (iface->n_options + 7));
	pp = G.my_environ;

	for (i = 0; i < iface->n_options; i++) {
		if (index_in_strings(keywords_up_down, iface->option[i].name) >= 0) {
			continue;
		}
		*pp++ = setlocalenv("IF_%s=%s", iface->option[i].name, iface->option[i].value);
	}

	*pp++ = setlocalenv("%s=%s", "IFACE", iface->iface);
	*pp++ = setlocalenv("%s=%s", "ADDRFAM", iface->address_family->name);
	*pp++ = setlocalenv("%s=%s", "METHOD", iface->method->name);
	*pp++ = setlocalenv("%s=%s", "MODE", mode);
	*pp++ = setlocalenv("%s=%s", "PHASE", opt);
	if (G.startup_PATH)
		*pp++ = setlocalenv("%s=%s", "PATH", G.startup_PATH);
}

static int doit(char *str)
{
	if (option_mask32 & (OPT_no_act|OPT_verbose)) {
		puts(str);
	}
	if (!(option_mask32 & OPT_no_act)) {
		pid_t child;
		int status;

		fflush_all();
		child = vfork();
		if (child < 0) /* failure */
			return 0;
		if (child == 0) { /* child */
			execle(G.shell, G.shell, "-c", str, (char *) NULL, G.my_environ);
			_exit(127);
		}
		safe_waitpid(child, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			return 0;
		}
	}
	return 1;
}

static int execute_all(struct interface_defn_t *ifd, const char *opt)
{
	int i;
	char *buf;
	for (i = 0; i < ifd->n_options; i++) {
		if (strcmp(ifd->option[i].name, opt) == 0) {
			if (!doit(ifd->option[i].value)) {
				return 0;
			}
		}
	}

	/* Tested on Debian Squeeze: "standard" ifup runs this without
	 * checking that directory exists. If it doesn't, run-parts
	 * complains, and this message _is_ annoyingly visible.
	 * Don't "fix" this (unless newer Debian does).
	 */
	buf = xasprintf("run-parts /etc/network/if-%s.d", opt);
	/* heh, we don't bother free'ing it */
	return doit(buf);
}

static int check(char *str)
{
	return str != NULL;
}

static int iface_up(struct interface_defn_t *iface)
{
	if (!iface->method->up(iface, check)) return -1;
	set_environ(iface, "start", "pre-up");
	if (!execute_all(iface, "pre-up")) return 0;
	if (!iface->method->up(iface, doit)) return 0;
	set_environ(iface, "start", "post-up");
	if (!execute_all(iface, "up")) return 0;
	return 1;
}

static int iface_down(struct interface_defn_t *iface)
{
	if (!iface->method->down(iface, check)) return -1;
	set_environ(iface, "stop", "pre-down");
	if (!execute_all(iface, "down")) return 0;
	if (!iface->method->down(iface, doit)) return 0;
	set_environ(iface, "stop", "post-down");
	if (!execute_all(iface, "post-down")) return 0;
	return 1;
}

#if ENABLE_FEATURE_IFUPDOWN_MAPPING
static int popen2(FILE **in, FILE **out, char *command, char *param)
{
	char *argv[3] = { command, param, NULL };
	struct fd_pair infd, outfd;
	pid_t pid;

	xpiped_pair(infd);
	xpiped_pair(outfd);

	fflush_all();
	pid = xvfork();

	if (pid == 0) {
		/* Child */
		/* NB: close _first_, then move fds! */
		close(infd.wr);
		close(outfd.rd);
		xmove_fd(infd.rd, 0);
		xmove_fd(outfd.wr, 1);
		BB_EXECVP_or_die(argv);
	}
	/* parent */
	close(infd.rd);
	close(outfd.wr);
	*in = xfdopen_for_write(infd.wr);
	*out = xfdopen_for_read(outfd.rd);
	return pid;
}

static char *run_mapping(char *physical, struct mapping_defn_t *map)
{
	FILE *in, *out;
	int i, status;
	pid_t pid;

	char *logical = xstrdup(physical);

	/* Run the mapping script. Never fails. */
	pid = popen2(&in, &out, map->script, physical);

	/* Write mappings to stdin of mapping script. */
	for (i = 0; i < map->n_mappings; i++) {
		fprintf(in, "%s\n", map->mapping[i]);
	}
	fclose(in);
	safe_waitpid(pid, &status, 0);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		/* If the mapping script exited successfully, try to
		 * grab a line of output and use that as the name of the
		 * logical interface. */
		char *new_logical = xmalloc_fgetline(out);

		if (new_logical) {
			/* If we are able to read a line of output from the script,
			 * remove any trailing whitespace and use this value
			 * as the name of the logical interface. */
			char *pch = new_logical + strlen(new_logical) - 1;

			while (pch >= new_logical && isspace(*pch))
				*(pch--) = '\0';

			free(logical);
			logical = new_logical;
		}
	}

	fclose(out);

	return logical;
}
#endif /* FEATURE_IFUPDOWN_MAPPING */

static llist_t *find_iface_state(llist_t *state_list, const char *iface)
{
	llist_t *search = state_list;

	while (search) {
		char *after_iface = is_prefixed_with(search->data, iface);
		if (after_iface
		 && *after_iface == '='
		) {
			return search;
		}
		search = search->link;
	}
	return NULL;
}

/* read the previous state from the state file */
static llist_t *read_iface_state(void)
{
	llist_t *state_list = NULL;
	FILE *state_fp = fopen_for_read(IFSTATE_FILE_PATH);

	if (state_fp) {
		char *start, *end_ptr;
		while ((start = xmalloc_fgets(state_fp)) != NULL) {
			/* We should only need to check for a single character */
			end_ptr = start + strcspn(start, " \t\n");
			*end_ptr = '\0';
			llist_add_to(&state_list, start);
		}
		fclose(state_fp);
	}
	return state_list;
}

/* read the previous state from the state file */
static FILE *open_new_state_file(void)
{
	int fd, flags, cnt;

	cnt = 0;
	flags = (O_WRONLY | O_CREAT | O_EXCL);
	for (;;) {
		fd = open(IFSTATE_FILE_PATH".new", flags, 0666);
		if (fd >= 0)
			break;
		if (errno != EEXIST
		 || flags == (O_WRONLY | O_CREAT | O_TRUNC)
		) {
			bb_perror_msg_and_die("can't open '%s'",
					IFSTATE_FILE_PATH".new");
		}
		/* Someone else created the .new file */
		if (cnt > 30 * 1000) {
			/* Waited for 30*30/2 = 450 milliseconds, still EEXIST.
			 * Assuming a stale file, rewriting it.
			 */
			flags = (O_WRONLY | O_CREAT | O_TRUNC);
			continue;
		}
		usleep(cnt);
		cnt += 1000;
	}

	return xfdopen_for_write(fd);
}

int ifupdown_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ifupdown_main(int argc UNUSED_PARAM, char **argv)
{
	int (*cmds)(struct interface_defn_t *);
	struct interfaces_file_t *defn;
	llist_t *target_list = NULL;
	const char *interfaces = "/etc/network/interfaces";
	bool any_failures = 0;

	INIT_G();

	G.startup_PATH = getenv("PATH");
	G.shell = xstrdup(get_shell_name());

	if (ENABLE_IFUP
	 && (!ENABLE_IFDOWN || applet_name[2] == 'u')
	) {
		/* ifup command */
		cmds = iface_up;
	} else {
		cmds = iface_down;
	}

	getopt32(argv, OPTION_STR, &interfaces);
	argv += optind;
	if (argv[0]) {
		if (DO_ALL) bb_show_usage();
	} else {
		if (!DO_ALL) bb_show_usage();
	}

	defn = read_interfaces(interfaces, NULL);

	/* Create a list of interfaces to work on */
	if (DO_ALL) {
		target_list = defn->autointerfaces;
	} else {
		llist_add_to_end(&target_list, argv[0]);
	}

	/* Update the interfaces */
	while (target_list) {
		llist_t *iface_list;
		struct interface_defn_t *currif;
		char *iface;
		char *liface;
		char *pch;
		bool okay = 0;
		int cmds_ret;
		bool curr_failure = 0;

		iface = xstrdup(target_list->data);
		target_list = target_list->link;

		pch = strchr(iface, '=');
		if (pch) {
			*pch = '\0';
			liface = xstrdup(pch + 1);
		} else {
			liface = xstrdup(iface);
		}

		if (!FORCE) {
			llist_t *state_list = read_iface_state();
			const llist_t *iface_state = find_iface_state(state_list, iface);

			if (cmds == iface_up) {
				/* ifup */
				if (iface_state) {
					bb_error_msg("interface %s already configured", iface);
					goto next;
				}
			} else {
				/* ifdown */
				if (!iface_state) {
					bb_error_msg("interface %s not configured", iface);
					goto next;
				}
			}
			llist_free(state_list, free);
		}

#if ENABLE_FEATURE_IFUPDOWN_MAPPING
		if ((cmds == iface_up) && !NO_MAPPINGS) {
			struct mapping_defn_t *currmap;

			for (currmap = defn->mappings; currmap; currmap = currmap->next) {
				int i;
				for (i = 0; i < currmap->n_matches; i++) {
					if (fnmatch(currmap->match[i], liface, 0) != 0)
						continue;
					if (VERBOSE) {
						printf("Running mapping script %s on %s\n", currmap->script, liface);
					}
					liface = run_mapping(iface, currmap);
					break;
				}
			}
		}
#endif

		iface_list = defn->ifaces;
		while (iface_list) {
			currif = (struct interface_defn_t *) iface_list->data;
			if (strcmp(liface, currif->iface) == 0) {
				char *oldiface = currif->iface;

				okay = 1;
				currif->iface = iface;

				debug_noise("\nConfiguring interface %s (%s)\n", liface, currif->address_family->name);

				/* Call the cmds function pointer, does either iface_up() or iface_down() */
				cmds_ret = cmds(currif);
				if (cmds_ret == -1) {
					bb_error_msg("don't have all variables for %s/%s",
							liface, currif->address_family->name);
					any_failures = curr_failure = 1;
				} else if (cmds_ret == 0) {
					any_failures = curr_failure = 1;
				}

				currif->iface = oldiface;
			}
			iface_list = iface_list->link;
		}
		if (VERBOSE) {
			bb_putchar('\n');
		}

		if (!okay && !FORCE) {
			bb_error_msg("ignoring unknown interface %s", liface);
			any_failures = 1;
		} else if (!NO_ACT) {
			/* update the state file */
			FILE *new_state_fp = open_new_state_file();
			llist_t *state;
			llist_t *state_list = read_iface_state();
			llist_t *iface_state = find_iface_state(state_list, iface);

			if (cmds == iface_up && !curr_failure) {
				char *newiface = xasprintf("%s=%s", iface, liface);
				if (!iface_state) {
					llist_add_to_end(&state_list, newiface);
				} else {
					free(iface_state->data);
					iface_state->data = newiface;
				}
			} else {
				/* Remove an interface from state_list */
				llist_unlink(&state_list, iface_state);
				free(llist_pop(&iface_state));
			}

			/* Actually write the new state */
			state = state_list;
			while (state) {
				if (state->data) {
					fprintf(new_state_fp, "%s\n", state->data);
				}
				state = state->link;
			}
			fclose(new_state_fp);
			xrename(IFSTATE_FILE_PATH".new", IFSTATE_FILE_PATH);
			llist_free(state_list, free);
		}
 next:
		free(iface);
		free(liface);
	}

	return any_failures;
}
