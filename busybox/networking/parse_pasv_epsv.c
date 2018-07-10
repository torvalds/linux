/*
 * Utility routines.
 *
 * Copyright (C) 2018 Denys Vlasenko
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//kbuild:lib-$(CONFIG_FTPGET) += parse_pasv_epsv.o
//kbuild:lib-$(CONFIG_FTPPUT) += parse_pasv_epsv.o
//kbuild:lib-$(CONFIG_WGET) += parse_pasv_epsv.o

#include "libbb.h"

int FAST_FUNC parse_pasv_epsv(char *buf)
{
/*
 * PASV command will not work for IPv6. RFC2428 describes
 * IPv6-capable "extended PASV" - EPSV.
 *
 * "EPSV [protocol]" asks server to bind to and listen on a data port
 * in specified protocol. Protocol is 1 for IPv4, 2 for IPv6.
 * If not specified, defaults to "same as used for control connection".
 * If server understood you, it should answer "229 <some text>(|||port|)"
 * where "|" are literal pipe chars and "port" is ASCII decimal port#.
 *
 * There is also an IPv6-capable replacement for PORT (EPRT),
 * but we don't need that.
 *
 * NB: PASV may still work for some servers even over IPv6.
 * For example, vsftp happily answers
 * "227 Entering Passive Mode (0,0,0,0,n,n)" and proceeds as usual.
 */
	char *ptr;
	int port;

	if (!ENABLE_FEATURE_IPV6 || buf[2] == '7' /* "227" */) {
		/* Response is "227 garbageN1,N2,N3,N4,P1,P2[)garbage]"
		 * Server's IP is N1.N2.N3.N4 (we ignore it)
		 * Server's port for data connection is P1*256+P2 */
		ptr = strrchr(buf, ')');
		if (ptr) *ptr = '\0';

		ptr = strrchr(buf, ',');
		if (!ptr) return -1;
		*ptr = '\0';
		port = xatou_range(ptr + 1, 0, 255);

		ptr = strrchr(buf, ',');
		if (!ptr) return -1;
		*ptr = '\0';
		port += xatou_range(ptr + 1, 0, 255) * 256;
	} else {
		/* Response is "229 garbage(|||P1|)"
		 * Server's port for data connection is P1 */
		ptr = strrchr(buf, '|');
		if (!ptr) return -1;
		*ptr = '\0';

		ptr = strrchr(buf, '|');
		if (!ptr) return -1;
		*ptr = '\0';
		port = xatou_range(ptr + 1, 0, 65535);
	}

	return port;
}
