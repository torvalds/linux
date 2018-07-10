/* vi: set sw=4 ts=4: */
/*
 * whois - tiny client for the whois directory service
 *
 * Copyright (c) 2011 Pere Orga <gotrunks@gmail.com>
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* TODO
 * Add ipv6 support
 * Add proxy support
 */
//config:config WHOIS
//config:	bool "whois (6.6 kb)"
//config:	default y
//config:	help
//config:	whois is a client for the whois directory service

//applet:IF_WHOIS(APPLET(whois, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_WHOIS) += whois.o

//usage:#define whois_trivial_usage
//usage:       "[-i] [-h SERVER] [-p PORT] NAME..."
//usage:#define whois_full_usage "\n\n"
//usage:       "Query WHOIS info about NAME\n"
//usage:     "\n	-i	Show redirect results too"
//usage:     "\n	-h,-p	Server to query"

#include "libbb.h"

enum {
	OPT_i = (1 << 0),
};

static char *query(const char *host, int port, const char *domain)
{
	int fd;
	FILE *fp;
	bool success;
	char *redir = NULL;
	const char *pfx = "";
	char linebuf[1024];
	char *buf = NULL;
	unsigned bufpos = 0;

 again:
	printf("[Querying %s:%d '%s%s']\n", host, port, pfx, domain);
	fd = create_and_connect_stream_or_die(host, port);
	success = 0;
	fdprintf(fd, "%s%s\r\n", pfx, domain);
	fp = xfdopen_for_read(fd);

	while (fgets(linebuf, sizeof(linebuf), fp)) {
		unsigned len = strcspn(linebuf, "\r\n");
		linebuf[len++] = '\n';

		buf = xrealloc(buf, bufpos + len + 1);
		memcpy(buf + bufpos, linebuf, len);
		bufpos += len;
		buf[bufpos] = '\0';

		if (!redir || !success) {
			trim(linebuf);
			str_tolower(linebuf);
			if (!success) {
				success = is_prefixed_with(linebuf, "domain:")
				       || is_prefixed_with(linebuf, "domain name:");
			}
			else if (!redir) {
				char *p = is_prefixed_with(linebuf, "whois server:");
				if (!p)
					p = is_prefixed_with(linebuf, "whois:");
				if (p)
					redir = xstrdup(skip_whitespace(p));
			}
		}
	}
	fclose(fp); /* closes fd too */
	if (!success && !pfx[0]) {
		/*
		 * Looking at /etc/jwhois.conf, some whois servers use
		 * "domain = DOMAIN", "DOMAIN ID <DOMAIN>"
		 * and "domain=DOMAIN_WITHOUT_LAST_COMPONENT"
		 * formats, but those are rare.
		 * (There are a few even more contrived ones.)
		 * We are trying only "domain DOMAIN", the typical one.
		 */
		pfx = "domain ";
		bufpos = 0;
		goto again;
	}

	/* Success */
	if (redir && strcmp(redir, host) == 0) {
		/* Redirect to self does not count */
		free(redir);
		redir = NULL;
	}
	if (!redir || (option_mask32 & OPT_i)) {
		/* Output saved text */
		printf("[%s]\n%s", host, buf ? buf : "");
	}
	free(buf);
	return redir;
}

static void recursive_query(const char *host, int port, const char *domain)
{
	char *free_me = NULL;
	char *redir;
 again:
	redir = query(host, port, domain);
	free(free_me);
	if (redir) {
		printf("[Redirected to %s]\n", redir);
		host = free_me = redir;
		port = 43;
		goto again;
	}
}

/* One of "big" whois implementations has these options:
 *
 * $ whois --help
 * jwhois version 4.0, Copyright (C) 1999-2007  Free Software Foundation, Inc.
 * -v, --verbose              verbose debug output
 * -c FILE, --config=FILE     use FILE as configuration file
 * -h HOST, --host=HOST       explicitly query HOST
 * -n, --no-redirect          disable content redirection
 * -s, --no-whoisservers      disable whois-servers.net service support
 * -a, --raw                  disable reformatting of the query
 * -i, --display-redirections display all redirects instead of hiding them
 * -p PORT, --port=PORT       use port number PORT (in conjunction with HOST)
 * -r, --rwhois               force an rwhois query to be made
 * --rwhois-display=DISPLAY   sets the display option in rwhois queries
 * --rwhois-limit=LIMIT       sets the maximum number of matches to return
 *
 * Example of its output:
 * $ whois cnn.com
 * [Querying whois.verisign-grs.com]
 * [Redirected to whois.corporatedomains.com]
 * [Querying whois.corporatedomains.com]
 * [whois.corporatedomains.com]
 * ...text of the reply...
 *
 * With -i, reply from each server is printed, after all redirects are done:
 * [Querying whois.verisign-grs.com]
 * [Redirected to whois.corporatedomains.com]
 * [Querying whois.corporatedomains.com]
 * [whois.verisign-grs.com]
 * ...text of the reply...
 * [whois.corporatedomains.com]
 * ...text of the reply...
 *
 * With -a, no "DOMAIN" -> "domain DOMAIN" transformation is attempted.

 * With -n, the first reply is shown, redirects are not followed:
 * [Querying whois.verisign-grs.com]
 * [whois.verisign-grs.com]
 * ...text of the reply...
 */

int whois_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int whois_main(int argc UNUSED_PARAM, char **argv)
{
	int port = 43;
	const char *host = "whois.iana.org";

	getopt32(argv, "^" "ih:p:+" "\0" "-1", &host, &port);
	argv += optind;

	do {
		recursive_query(host, port, *argv);
	}
	while (*++argv);

	return EXIT_SUCCESS;
}
