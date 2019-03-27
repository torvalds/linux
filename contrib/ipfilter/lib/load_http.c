/* $FreeBSD$ */

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: load_http.c,v 1.5.2.5 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"
#include <ctype.h>

/*
 * Because the URL can be included twice into the buffer, once as the
 * full path for the "GET" and once as the "Host:", the buffer it is
 * put in needs to be larger than 512*2 to make room for the supporting
 * text. Why not just use snprintf and truncate? The warning about the
 * URL being too long tells you something is wrong and does not fetch
 * any data - just truncating the URL (with snprintf, etc) and sending
 * that to the server is allowing an unknown and unintentioned action
 * to happen.
 */
#define	MAX_URL_LEN	512
#define	LOAD_BUFSIZE	(MAX_URL_LEN * 2 + 128)

/*
 * Format expected is one addres per line, at the start of each line.
 */
alist_t *
load_http(char *url)
{
	int fd, len, left, port, endhdr, removed, linenum = 0;
	char *s, *t, *u, buffer[LOAD_BUFSIZE], *myurl;
	alist_t *a, *rtop, *rbot;
	size_t avail;
	int error;

	/*
	 * More than this would just be absurd.
	 */
	if (strlen(url) > MAX_URL_LEN) {
		fprintf(stderr, "load_http has a URL > %d bytes?!\n",
			MAX_URL_LEN);
		return NULL;
	}

	fd = -1;
	rtop = NULL;
	rbot = NULL;

	avail = sizeof(buffer);
	error = snprintf(buffer, avail, "GET %s HTTP/1.0\r\n", url);

	/*
	 * error is always less then avail due to the constraint on
	 * the url length above.
	 */
	avail -= error;

	myurl = strdup(url);
	if (myurl == NULL)
		goto done;

	s = myurl + 7;			/* http:// */
	t = strchr(s, '/');
	if (t == NULL) {
		fprintf(stderr, "load_http has a malformed URL '%s'\n", url);
		free(myurl);
		return NULL;
	}
	*t++ = '\0';

	/*
	 * 10 is the length of 'Host: \r\n\r\n' below.
	 */
	if (strlen(s) + strlen(buffer) + 10 > sizeof(buffer)) {
		fprintf(stderr, "load_http has a malformed URL '%s'\n", url);
		free(myurl);
		return NULL;
	}

	u = strchr(s, '@');
	if (u != NULL)
		s = u + 1;		/* AUTH */

	error = snprintf(buffer + strlen(buffer), avail, "Host: %s\r\n\r\n", s);
	if (error >= avail) {
		fprintf(stderr, "URL is too large: %s\n", url);
		goto done;
	}

	u = strchr(s, ':');
	if (u != NULL) {
		*u++ = '\0';
		port = atoi(u);
		if (port < 0 || port > 65535)
			goto done;
	} else {
		port = 80;
	}


	fd = connecttcp(s, port);
	if (fd == -1)
		goto done;


	len = strlen(buffer);
	if (write(fd, buffer, len) != len)
		goto done;

	s = buffer;
	endhdr = 0;
	left = sizeof(buffer) - 1;

	while ((len = read(fd, s, left)) > 0) {
		s[len] = '\0';
		left -= len;
		s += len;

		if (endhdr >= 0) {
			if (endhdr == 0) {
				t = strchr(buffer, ' ');
				if (t == NULL)
					continue;
				t++;
				if (*t != '2')
					break;
			}

			u = buffer;
			while ((t = strchr(u, '\r')) != NULL) {
				if (t == u) {
					if (*(t + 1) == '\n') {
						u = t + 2;
						endhdr = -1;
						break;
					} else
						t++;
				} else if (*(t + 1) == '\n') {
					endhdr++;
					u = t + 2;
				} else
					u = t + 1;
			}
			if (endhdr >= 0)
				continue;
			removed = (u - buffer) + 1;
			memmove(buffer, u, (sizeof(buffer) - left) - removed);
			s -= removed;
			left += removed;
		}

		do {
			t = strchr(buffer, '\n');
			if (t == NULL)
				break;

			linenum++;
			*t = '\0';

			/*
			 * Remove comment and continue to the next line if
			 * the comment is at the start of the line.
			 */
			u = strchr(buffer, '#');
			if (u != NULL) {
				*u = '\0';
				if (u == buffer)
					continue;
			}

			/*
			 * Trim off tailing white spaces, will include \r
			 */
			for (u = t - 1; (u >= buffer) && ISSPACE(*u); u--)
				*u = '\0';

			a = alist_new(AF_UNSPEC, buffer);
			if (a != NULL) {
				if (rbot != NULL)
					rbot->al_next = a;
				else
					rtop = a;
				rbot = a;
			} else {
				fprintf(stderr,
					"%s:%d unrecognised content:%s\n",
					url, linenum, buffer);
			}

			t++;
			removed = t - buffer;
			memmove(buffer, t, sizeof(buffer) - left - removed);
			s -= removed;
			left += removed;

		} while (1);
	}

done:
	if (myurl != NULL)
		free(myurl);
	if (fd != -1)
		close(fd);
	return rtop;
}
