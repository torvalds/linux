/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ipft_hx.c	1.1 3/9/96 (C) 1996 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#include <ctype.h>

#include "ipf.h"
#include "ipt.h"


extern	int	opts;

static	int	hex_open __P((char *));
static	int	hex_close __P((void));
static	int	hex_readip __P((mb_t *, char **, int *));
static	char	*readhex __P((char *, char *));

struct	ipread	iphex = { hex_open, hex_close, hex_readip, 0 };
static	FILE	*tfp = NULL;
static	int	tfd = -1;

static	int	hex_open(fname)
	char	*fname;
{
	if (tfp && tfd != -1) {
		rewind(tfp);
		return tfd;
	}

	if (!strcmp(fname, "-")) {
		tfd = 0;
		tfp = stdin;
	} else {
		tfd = open(fname, O_RDONLY);
		if (tfd != -1)
			tfp = fdopen(tfd, "r");
	}
	return tfd;
}


static	int	hex_close()
{
	int	cfd = tfd;

	tfd = -1;
	return close(cfd);
}


static	int	hex_readip(mb, ifn, dir)
	mb_t	*mb;
	char	**ifn;
	int	*dir;
{
	register char *s, *t, *u;
	char	line[513];
	ip_t	*ip;
	char	*buf;
	int	cnt;

	buf = (char *)mb->mb_buf;
	cnt = sizeof(mb->mb_buf);
	/*
	 * interpret start of line as possibly "[ifname]" or
	 * "[in/out,ifname]".
	 */
	if (ifn)
		*ifn = NULL;
	if (dir)
		*dir = 0;
 	ip = (ip_t *)buf;
	while (fgets(line, sizeof(line)-1, tfp)) {
		if ((s = strchr(line, '\n'))) {
			if (s == line) {
				mb->mb_len = (char *)ip - buf;
				return mb->mb_len;
			}
			*s = '\0';
		}
		if ((s = strchr(line, '#')))
			*s = '\0';
		if (!*line)
			continue;
		if ((opts & OPT_DEBUG) != 0) {
			printf("input: %s", line);
		}

		if ((*line == '[') && (s = strchr(line, ']'))) {
			t = line + 1;
			if (s - t > 0) {
				*s++ = '\0';
				if ((u = strchr(t, ',')) && (u < s)) {
					u++;
					if (ifn)
						*ifn = strdup(u);
					if (dir) {
						if (*t == 'i')
							*dir = 0;
						else if (*t == 'o')
							*dir = 1;
					}
				} else if (ifn)
					*ifn = t;
			}

			while (*s++ == '+') {
				if (!strncasecmp(s, "mcast", 5)) {
					mb->mb_flags |= M_MCAST;
					s += 5;
				}
				if (!strncasecmp(s, "bcast", 5)) {
					mb->mb_flags |= M_BCAST;
					s += 5;
				}
				if (!strncasecmp(s, "mbcast", 6)) {
					mb->mb_flags |= M_MBCAST;
					s += 6;
				}
			}
			while (ISSPACE(*s))
				s++;
		} else
			s = line;
		t = (char *)ip;
		ip = (ip_t *)readhex(s, (char *)ip);
		if ((opts & OPT_DEBUG) != 0) {
			if (opts & OPT_ASCII) {
				int c = *t;
				if (t < (char *)ip)
					putchar('\t');
				while (t < (char *)ip) {
					if (isprint(c) && isascii(c))
						putchar(c);
					else
						putchar('.');
					t++;
				}
			}
			putchar('\n');
			fflush(stdout);
		}
	}
	if (feof(tfp))
		return 0;
	return -1;
}


static	char	*readhex(src, dst)
register char	*src, *dst;
{
	int	state = 0;
	char	c;

	while ((c = *src++)) {
		if (ISSPACE(c)) {
			if (state) {
				dst++;
				state = 0;
			}
			continue;
		} else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
			   (c >= 'A' && c <= 'F')) {
			c = ISDIGIT(c) ? (c - '0') : (TOUPPER(c) - 55);
			if (state == 0) {
				*dst = (c << 4);
				state++;
			} else {
				*dst++ |= c;
				state = 0;
			}
		} else
			break;
	}
	return dst;
}
