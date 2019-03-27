/*
 * Copyright (c) 2013 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: inet6_ntop.c,v 1.2 2013-11-22 20:51:43 ca Exp $")

#if NETINET6
# include <sm/conf.h>
# include <sm/types.h>
# include <sm/io.h>
# include <sm/string.h>
# include <netinet/in.h>

/*
**  SM_INET6_NTOP -- convert IPv6 address to ASCII string (uncompressed)
**
**	Parameters:
**		ipv6 -- IPv6 address
**		dst -- ASCII representation of address (output)
**		len -- length of dst
**
**	Returns:
**		error: NULL
*/

char *
sm_inet6_ntop(ipv6, dst, len)
	const void *ipv6;
	char *dst;
	size_t len;
{
	SM_UINT16 *u16;
	int r;

	u16 = (SM_UINT16 *)ipv6;
	r = sm_snprintf(dst, len,
		"%x:%x:%x:%x:%x:%x:%x:%x"
			, htons(u16[0])
			, htons(u16[1])
			, htons(u16[2])
			, htons(u16[3])
			, htons(u16[4])
			, htons(u16[5])
			, htons(u16[6])
			, htons(u16[7])
		);
	if (r > 0)
		return dst;
	return NULL;
}
#endif /* NETINET6 */
