/* vi: set sw=4 ts=4: */
/*
 * Mini hostname implementation for busybox
 *
 * Copyright (C) 1999 by Randolph Chung <tausq@debian.org>
 *
 * Adjusted by Erik Andersen <andersen@codepoet.org> to remove
 * use of long options and GNU getopt.  Improved the usage info.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config HOSTNAME
//config:	bool "hostname (5.6 kb)"
//config:	default y
//config:	help
//config:	Show or set the system's host name.
//config:
//config:config DNSDOMAINNAME
//config:	bool "dnsdomainname (3.6 kb)"
//config:	default y
//config:	help
//config:	Alias to "hostname -d".

//                        APPLET_NOEXEC:name           main      location    suid_type     help
//applet:IF_DNSDOMAINNAME(APPLET_NOEXEC(dnsdomainname, hostname, BB_DIR_BIN, BB_SUID_DROP, dnsdomainname))
//applet:IF_HOSTNAME(     APPLET_NOEXEC(hostname,      hostname, BB_DIR_BIN, BB_SUID_DROP, hostname     ))

//kbuild: lib-$(CONFIG_HOSTNAME) += hostname.o
//kbuild: lib-$(CONFIG_DNSDOMAINNAME) += hostname.o

//usage:#define hostname_trivial_usage
//usage:       "[OPTIONS] [HOSTNAME | -F FILE]"
//usage:#define hostname_full_usage "\n\n"
//usage:       "Get or set hostname or DNS domain name\n"
//usage:     "\n	-s	Short"
//usage:     "\n	-i	Addresses for the hostname"
//usage:     "\n	-d	DNS domain name"
//usage:     "\n	-f	Fully qualified domain name"
//usage:     "\n	-F FILE	Use FILE's content as hostname"
//usage:
//usage:#define hostname_example_usage
//usage:       "$ hostname\n"
//usage:       "sage\n"
//usage:
//usage:#define dnsdomainname_trivial_usage NOUSAGE_STR
//usage:#define dnsdomainname_full_usage ""

#include "libbb.h"

static void do_sethostname(char *s, int isfile)
{
//	if (!s)
//		return;
	if (isfile) {
		parser_t *parser = config_open2(s, xfopen_for_read);
		while (config_read(parser, &s, 1, 1, "# \t", PARSE_NORMAL & ~PARSE_GREEDY)) {
			do_sethostname(s, 0);
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			config_close(parser);
	} else if (sethostname(s, strlen(s))) {
//		if (errno == EPERM)
//			bb_error_msg_and_die(bb_msg_perm_denied_are_you_root);
		bb_perror_msg_and_die("sethostname");
	}
}

/* Manpage circa 2009:
 *
 * hostname [-v] [-a] [--alias] [-d] [--domain] [-f] [--fqdn] [--long]
 *      [-i] [--ip-address] [-s] [--short] [-y] [--yp] [--nis]
 *
 * hostname [-v] [-F filename] [--file filename] / [hostname]
 *
 * domainname [-v] [-F filename] [--file filename]  / [name]
 *  { bbox: not supported }
 *
 * nodename [-v] [-F filename] [--file filename] / [name]
 *  { bbox: not supported }
 *
 * dnsdomainname [-v]
 *  { bbox: supported: Linux kernel build needs this }
 * nisdomainname [-v]
 *  { bbox: not supported }
 * ypdomainname [-v]
 *  { bbox: not supported }
 *
 * -a, --alias
 *  Display the alias name of the host (if used).
 *  { bbox: not supported }
 * -d, --domain
 *  Display the name of the DNS domain. Don't use the command
 *  domainname to get the DNS domain name because it will show the
 *  NIS domain name and not the DNS domain name. Use dnsdomainname
 *  instead.
 * -f, --fqdn, --long
 *  Display the FQDN (Fully Qualified Domain Name). A FQDN consists
 *  of a short host name and the DNS domain name. Unless you are
 *  using bind or NIS for host lookups you can change the FQDN and
 *  the DNS domain name (which is part of the FQDN) in the
 *  /etc/hosts file.
 * -i, --ip-address
 *  Display the IP address(es) of the host.
 * -s, --short
 *  Display the short host name. This is the host name cut at the
 *  first dot.
 * -v, --verbose
 *  Be verbose and tell what's going on.
 *  { bbox: supported but ignored }
 * -y, --yp, --nis
 *  Display the NIS domain name. If a parameter is given (or --file
 *  name ) then root can also set a new NIS domain.
 *  { bbox: not supported }
 * -F, --file filename
 *  Read the host name from the specified file. Comments (lines
 *  starting with a '#') are ignored.
 */
int hostname_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hostname_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		OPT_d = 0x1,
		OPT_f = 0x2,
		OPT_i = 0x4,
		OPT_s = 0x8,
		OPT_F = 0x10,
		OPT_dfi = 0x7,
	};

	unsigned opts;
	char *buf;
	char *hostname_str;

	/* dnsdomainname from net-tools 1.60, hostname 1.100 (2001-04-14),
	 * supports hostname's options too (not just -v as manpage says) */
	opts = getopt32(argv, "dfisF:v", &hostname_str,
		"domain\0"     No_argument "d"
		"fqdn\0"       No_argument "f"
	//Enable if seen in active use in some distro:
	//	"long\0"       No_argument "f"
	//	"ip-address\0" No_argument "i"
	//	"short\0"      No_argument "s"
	//	"verbose\0"    No_argument "v"
		"file\0"       No_argument "F"
	);
	argv += optind;
	buf = safe_gethostname();
	if (ENABLE_DNSDOMAINNAME) {
		if (!ENABLE_HOSTNAME || applet_name[0] == 'd') {
			/* dnsdomainname */
			opts = OPT_d;
		}
	}

	if (opts & OPT_dfi) {
		/* Cases when we need full hostname (or its part) */
		struct hostent *hp;
		char *p;

		hp = xgethostbyname(buf);
		p = strchrnul(hp->h_name, '.');
		if (opts & OPT_f) {
			puts(hp->h_name);
		} else if (opts & OPT_s) {
			*p = '\0';
			puts(hp->h_name);
		} else if (opts & OPT_d) {
			if (*p)
				puts(p + 1);
		} else /*if (opts & OPT_i)*/ {
			if (hp->h_length == sizeof(struct in_addr)) {
				struct in_addr **h_addr_list = (struct in_addr **)hp->h_addr_list;
				while (*h_addr_list) {
					printf(h_addr_list[1] ? "%s " : "%s", inet_ntoa(**h_addr_list));
					h_addr_list++;
				}
				bb_putchar('\n');
			}
		}
	} else if (opts & OPT_s) {
		strchrnul(buf, '.')[0] = '\0';
		puts(buf);
	} else if (opts & OPT_F) {
		/* Set the hostname */
		do_sethostname(hostname_str, 1);
	} else if (argv[0]) {
		/* Set the hostname */
		do_sethostname(argv[0], 0);
	} else {
		/* Just print the current hostname */
		puts(buf);
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		free(buf);
	return EXIT_SUCCESS;
}
