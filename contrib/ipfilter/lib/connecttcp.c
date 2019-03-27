/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: connecttcp.c,v 1.3.2.2 2012/07/22 08:04:24 darren_r Exp $
 */

#include "ipf.h"
#include <ctype.h>

/*
 * Format expected is one addres per line, at the start of each line.
 */
int
connecttcp(char *server, int port)
{
	struct sockaddr_in sin;
	struct hostent *host;
	int fd;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port & 65535);

	if (ISDIGIT(*server)) {
		if (inet_aton(server, &sin.sin_addr) == -1) {
			return -1;
		}
	} else {
		host = gethostbyname(server);
		if (host == NULL)
			return -1;
		memcpy(&sin.sin_addr, host->h_addr_list[0],
		       sizeof(sin.sin_addr));
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd == -1)
		return -1;

	if (connect(fd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		close(fd);
		return -1;
	}

	return fd;
}
