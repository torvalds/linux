/*
 * By John G. Myers, jgm+@cmu.edu
 * Version 1.2
 *
 * Process a BITNET "internet.listing" file, producing output
 * suitable for input to makemap.
 *
 * The input file can be obtained via anonymous FTP to bitnic.educom.edu.
 * Change directory to "netinfo" and get the file internet.listing
 * The file is updated monthly.
 *
 * Feed the output of this program to "makemap hash /etc/mail/bitdomain.db"
 * to create the table used by the "FEATURE(bitdomain)" config file macro.
 * If your sendmail does not have the db library compiled in, you can instead
 * use "makemap dbm /etc/mail/bitdomain" and
 * "FEATURE(bitdomain,`dbm -o /etc/mail/bitdomain')"
 *
 * The bitdomain table should be rebuilt monthly.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>
#include <ctype.h>
#include <string.h>

/* don't use sizeof because sizeof(long) is different on 64-bit machines */
#define SHORTSIZE	2	/* size of a short (really, must be 2) */
#define LONGSIZE	4	/* size of a long (really, must be 4) */

typedef union
{
	HEADER	qb1;
	char	qb2[PACKETSZ];
} querybuf;

extern int h_errno;
extern char *malloc();
extern char *optarg;
extern int optind;

char *lookup();

main(argc, argv)
int argc;
char **argv;
{
    int opt;

    while ((opt = getopt(argc, argv, "o:")) != -1) {
	switch (opt) {
	case 'o':
	    if (!freopen(optarg, "w", stdout)) {
		perror(optarg);
		exit(1);
	    }
	    break;

	default:
	    fprintf(stderr, "usage: %s [-o outfile] [internet.listing]\n",
		    argv[0]);
	    exit(1);
	}
    }

    if (optind < argc) {
	if (!freopen(argv[optind], "r", stdin)) {
	    perror(argv[optind]);
	    exit(1);
	}
    }
    readfile(stdin);
    finish();
    exit(0);
}

/*
 * Parse and process an input file
 */
readfile(infile)
FILE *infile;
{
    int skippingheader = 1;
    char buf[1024], *node, *hostname, *p;

    while (fgets(buf, sizeof(buf), infile)) {
	for (p = buf; *p && isspace(*p); p++);
	if (!*p) {
	    skippingheader = 0;
	    continue;
	}
	if (skippingheader) continue;

	node = p;
	for (; *p && !isspace(*p); p++) {
	    if (isupper(*p)) *p = tolower(*p);
	}
	if (!*p) {
	    fprintf(stderr, "%-8s: no domain name in input file\n", node);
	    continue;
	}
	*p++ = '\0';

	for (; *p && isspace(*p); p++) ;
	if (!*p) {
	    fprintf(stderr, "%-8s no domain name in input file\n", node);
	    continue;
	}

	hostname = p;
	for (; *p && !isspace(*p); p++) {
	    if (isupper(*p)) *p = tolower(*p);
	}
	*p = '\0';

	/* Chop off any trailing .bitnet */
	if (strlen(hostname) > 7 &&
	    !strcmp(hostname+strlen(hostname)-7, ".bitnet")) {
	    hostname[strlen(hostname)-7] = '\0';
	}
	entry(node, hostname, sizeof(buf)-(hostname - buf));
    }
}

/*
 * Process a single entry in the input file.
 * The entry tells us that "node" expands to "domain".
 * "domain" can either be a domain name or a bitnet node name
 * The buffer pointed to by "domain" may be overwritten--it
 * is of size "domainlen".
 */
entry(node, domain, domainlen)
char *node;
char *domain;
char *domainlen;
{
    char *otherdomain, *p, *err;

    /* See if we have any remembered information about this node */
    otherdomain = lookup(node);

    if (otherdomain && strchr(otherdomain, '.')) {
	/* We already have a domain for this node */
	if (!strchr(domain, '.')) {
	    /*
	     * This entry is an Eric Thomas FOO.BITNET kludge.
	     * He doesn't want LISTSERV to do transitive closures, so we
	     * do them instead.  Give the the domain expansion for "node"
	     * (which is in "otherdomian") to FOO (which is in "domain")
	     * if "domain" doesn't have a domain expansion already.
	     */
	    p = lookup(domain);
	    if (!p || !strchr(p, '.')) remember(domain, otherdomain);
	}
    }
    else {
	if (!strchr(domain, '.') || valhost(domain, domainlen)) {
	    remember(node, domain);
	    if (otherdomain) {
		/*
		 * We previously mapped the node "node" to the node
		 * "otherdomain".  If "otherdomain" doesn't already
		 * have a domain expansion, give it the expansion "domain".
		 */
		p = lookup(otherdomain);
		if (!p || !strchr(p, '.')) remember(otherdomain, domain);
	    }
	}
	else {
	    switch (h_errno) {
	    case HOST_NOT_FOUND:
		err = "not registered in DNS";
		break;

	    case TRY_AGAIN:
		err = "temporary DNS lookup failure";
		break;

	    case NO_RECOVERY:
		err = "non-recoverable nameserver error";
		break;

	    case NO_DATA:
		err = "registered in DNS, but not mailable";
		break;

	    default:
		err = "unknown nameserver error";
		break;
	    }

	    fprintf(stderr, "%-8s %s %s\n", node, domain, err);
	}
    }
}

/*
 * Validate whether the mail domain "host" is registered in the DNS.
 * If "host" is a CNAME, it is expanded in-place if the expansion fits
 * into the buffer of size "hbsize".  Returns nonzero if it is, zero
 * if it is not.  A BIND error code is left in h_errno.
 */
int
valhost(host, hbsize)
	char *host;
	int hbsize;
{
	register u_char *eom, *ap;
	register int n;
	HEADER *hp;
	querybuf answer;
	int ancount, qdcount;
	int ret;
	int type;
	int qtype;
	char nbuf[1024];

	if ((_res.options & RES_INIT) == 0 && res_init() == -1)
		return (0);

	_res.options &= ~(RES_DNSRCH|RES_DEFNAMES);
	_res.retrans = 30;
	_res.retry = 10;

	qtype = T_ANY;

	for (;;) {
		h_errno = NO_DATA;
		ret = res_querydomain(host, "", C_IN, qtype,
				      &answer, sizeof(answer));
		if (ret <= 0)
		{
			if (errno == ECONNREFUSED || h_errno == TRY_AGAIN)
			{
				/* the name server seems to be down */
				h_errno = TRY_AGAIN;
				return 0;
			}

			if (h_errno != HOST_NOT_FOUND)
			{
				/* might have another type of interest */
				if (qtype == T_ANY)
				{
					qtype = T_A;
					continue;
				}
				else if (qtype == T_A)
				{
					qtype = T_MX;
					continue;
				}
			}

			/* otherwise, no record */
			return 0;
		}

		/*
		**  This might be a bogus match.  Search for A, MX, or
		**  CNAME records.
		*/

		hp = (HEADER *) &answer;
		ap = (u_char *) &answer + sizeof(HEADER);
		eom = (u_char *) &answer + ret;

		/* skip question part of response -- we know what we asked */
		for (qdcount = ntohs(hp->qdcount); qdcount--; ap += ret + QFIXEDSZ)
		{
			if ((ret = dn_skipname(ap, eom)) < 0)
			{
				return 0;		/* ???XXX??? */
			}
		}

		for (ancount = ntohs(hp->ancount); --ancount >= 0 && ap < eom; ap += n)
		{
			n = dn_expand((u_char *) &answer, eom, ap,
				      (u_char *) nbuf, sizeof nbuf);
			if (n < 0)
				break;
			ap += n;
			GETSHORT(type, ap);
			ap += SHORTSIZE + LONGSIZE;
			GETSHORT(n, ap);
			switch (type)
			{
			  case T_MX:
			  case T_A:
				return 1;

			  case T_CNAME:
				/* value points at name */
				if ((ret = dn_expand((u_char *)&answer,
				    eom, ap, (u_char *)nbuf, sizeof(nbuf))) < 0)
					break;
				if (strlen(nbuf) < hbsize) {
				    (void)strcpy(host, nbuf);
				}
				return 1;

			  default:
				/* not a record of interest */
				continue;
			}
		}

		/*
		**  If this was a T_ANY query, we may have the info but
		**  need an explicit query.  Try T_A, then T_MX.
		*/

		if (qtype == T_ANY)
			qtype = T_A;
		else if (qtype == T_A)
			qtype = T_MX;
		else
			return 0;
	}
}

struct entry {
    struct entry *next;
    char *node;
    char *domain;
};
struct entry *firstentry;

/*
 * Find any remembered information about "node"
 */
char *lookup(node)
char *node;
{
    struct entry *p;

    for (p = firstentry; p; p = p->next) {
	if (!strcmp(node, p->node)) {
	    return p->domain;
	}
    }
    return 0;
}

/*
 * Mark the node "node" as equivalent to "domain".  "domain" can either
 * be a bitnet node or a domain name--if it is the latter, the mapping
 * will be written to stdout.
 */
remember(node, domain)
char *node;
char *domain;
{
    struct entry *p;

    if (strchr(domain, '.')) {
	fprintf(stdout, "%-8s %s\n", node, domain);
    }

    for (p = firstentry; p; p = p->next) {
	if (!strcmp(node, p->node)) {
	    p->domain = malloc(strlen(domain)+1);
	    if (!p->domain) {
		goto outofmemory;
	    }
	    strcpy(p->domain, domain);
	    return;
	}
    }

    p = (struct entry *)malloc(sizeof(struct entry));
    if (!p) goto outofmemory;

    p->next = firstentry;
    firstentry = p;
    p->node = malloc(strlen(node)+1);
    p->domain = malloc(strlen(domain)+1);
    if (!p->node || !p->domain) goto outofmemory;
    strcpy(p->node, node);
    strcpy(p->domain, domain);
    return;

  outofmemory:
    fprintf(stderr, "Out of memory\n");
    exit(1);
}

/*
 * Walk through the database, looking for any cases where we know
 * node FOO is equivalent to node BAR and node BAR has a domain name.
 * For those cases, give FOO the same domain name as BAR.
 */
finish()
{
    struct entry *p;
    char *domain;

    for (p = firstentry; p; p = p->next) {
	if (!strchr(p->domain, '.') && (domain = lookup(p->domain))) {
	    remember(p->node, domain);
	}
    }
}

