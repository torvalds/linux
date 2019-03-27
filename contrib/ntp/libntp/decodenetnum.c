/*
 * decodenetnum - return a net number (this is crude, but careful)
 */
#include <config.h>
#include <sys/types.h>
#include <ctype.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include "ntp.h"
#include "ntp_stdlib.h"
#include "ntp_assert.h"

/*
 * decodenetnum		convert text IP address and port to sockaddr_u
 *
 * Returns 0 for failure, 1 for success.
 */
int
decodenetnum(
	const char *num,
	sockaddr_u *netnum
	)
{
	struct addrinfo hints, *ai = NULL;
	int err;
	u_short port;
	const char *cp;
	const char *port_str;
	char *pp;
	char *np;
	char name[80];

	REQUIRE(num != NULL);

	if (strlen(num) >= sizeof(name)) {
		return 0;
	}

	port_str = NULL;
	if ('[' != num[0]) {
		/*
		 * to distinguish IPv6 embedded colons from a port
		 * specification on an IPv4 address, assume all 
		 * legal IPv6 addresses have at least two colons.
		 */
		pp = strchr(num, ':');
		if (NULL == pp)
			cp = num;	/* no colons */
		else if (NULL != strchr(pp + 1, ':'))
			cp = num;	/* two or more colons */
		else {			/* one colon */
			strlcpy(name, num, sizeof(name));
			cp = name;
			pp = strchr(cp, ':');
			*pp = '\0';
			port_str = pp + 1;
		}
	} else {
		cp = num + 1;
		np = name; 
		while (*cp && ']' != *cp)
			*np++ = *cp++;
		*np = 0;
		if (']' == cp[0] && ':' == cp[1] && '\0' != cp[2])
			port_str = &cp[2];
		cp = name; 
	}
	ZERO(hints);
	hints.ai_flags = Z_AI_NUMERICHOST;
	err = getaddrinfo(cp, "ntp", &hints, &ai);
	if (err != 0)
		return 0;
	INSIST(ai->ai_addrlen <= sizeof(*netnum));
	ZERO(*netnum);
	memcpy(netnum, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	if (NULL == port_str || 1 != sscanf(port_str, "%hu", &port))
		port = NTP_PORT;
	SET_PORT(netnum, port);
	return 1;
}
