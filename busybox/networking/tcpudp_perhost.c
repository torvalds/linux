/* Based on ipsvd utilities written by Gerrit Pape <pape@smarden.org>
 * which are released into public domain by the author.
 * Homepage: http://smarden.sunsite.dk/ipsvd/
 *
 * Copyright (C) 2007 Denys Vlasenko.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

#include "libbb.h"
#include "tcpudp_perhost.h"

struct hcc* FAST_FUNC ipsvd_perhost_init(unsigned c)
{
//	free(cc);
	struct hcc *cc = xzalloc((c + 1) * sizeof(*cc));
	cc[c].pid = -1; /* "end" marker */
	return cc;
}

unsigned FAST_FUNC ipsvd_perhost_add(struct hcc *cc, char *ip, unsigned maxconn, struct hcc **hccpp)
{
	unsigned i;
	unsigned conn = 1;
	int freepos = -1;

	for (i = 0; cc[i].pid >= 0; ++i) {
		if (!cc[i].ip) {
			freepos = i;
			continue;
		}
		if (strcmp(cc[i].ip, ip) == 0) {
			conn++;
			continue;
		}
	}
	if (freepos == -1) return 0;
	if (conn <= maxconn) {
		cc[freepos].ip = ip;
		*hccpp = &cc[freepos];
	}
	return conn;
}

void FAST_FUNC ipsvd_perhost_remove(struct hcc *cc, int pid)
{
	unsigned i;
	for (i = 0; cc[i].pid >= 0; ++i) {
		if (cc[i].pid == pid) {
			free(cc[i].ip);
			cc[i].ip = NULL;
			cc[i].pid = 0;
			return;
		}
	}
}

//void ipsvd_perhost_free(struct hcc *cc)
//{
//	free(cc);
//}
