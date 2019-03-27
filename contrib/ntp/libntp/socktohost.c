/*
 * socktoa - return a numeric host name from a sockaddr_storage structure
 */
#include <config.h>
#include <sys/types.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#include <arpa/inet.h>

#include <stdio.h>

#include "ntp_fp.h"
#include "lib_strbuf.h"
#include "ntp_stdlib.h"
#include "ntp.h"
#include "ntp_debug.h"


const char *
socktohost(
	const sockaddr_u *sock
	)
{
	const char		svc[] = "ntp";
	char *			pbuf;
	char *			pliar;
	int			gni_flags;
	struct addrinfo		hints;
	struct addrinfo *	alist;
	struct addrinfo *	ai;
	sockaddr_u		addr;
	size_t			octets;
	int			a_info;
	int			saved_errno;

	saved_errno = socket_errno();

	/* reverse the address to purported DNS name */
	LIB_GETBUF(pbuf);
	gni_flags = NI_DGRAM | NI_NAMEREQD;
	if (getnameinfo(&sock->sa, SOCKLEN(sock), pbuf, LIB_BUFLENGTH,
			NULL, 0, gni_flags)) {
		errno = saved_errno;
		return stoa(sock);	/* use address */
	}

	TRACE(1, ("%s reversed to %s\n", stoa(sock), pbuf));

	/*
	 * Resolve the reversed name and make sure the reversed address
	 * is among the results.
	 */
	ZERO(hints);
	hints.ai_family = AF(sock);
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;
	alist = NULL;

	a_info = getaddrinfo(pbuf, svc, &hints, &alist);
	if (a_info == EAI_NONAME
#ifdef EAI_NODATA
	    || a_info == EAI_NODATA
#endif
	   ) {
		hints.ai_flags = AI_CANONNAME;
#ifdef AI_ADDRCONFIG
		hints.ai_flags |= AI_ADDRCONFIG;
#endif
		a_info = getaddrinfo(pbuf, svc, &hints, &alist);	
	}
#ifdef AI_ADDRCONFIG
	/* Some older implementations don't like AI_ADDRCONFIG. */
	if (a_info == EAI_BADFLAGS) {
		hints.ai_flags &= ~AI_ADDRCONFIG;
		a_info = getaddrinfo(pbuf, svc, &hints, &alist);	
	}
#endif
	if (a_info)
		goto forward_fail;

	INSIST(alist != NULL);

	for (ai = alist; ai != NULL; ai = ai->ai_next) {
		/*
		 * Make a convenience sockaddr_u copy from ai->ai_addr
		 * because casting from sockaddr * to sockaddr_u * is
		 * risking alignment problems on platforms where
		 * sockaddr_u has stricter alignment than sockaddr,
		 * such as sparc.
		 */
		ZERO_SOCK(&addr);
		octets = min(sizeof(addr), ai->ai_addrlen);
		memcpy(&addr, ai->ai_addr, octets);
		if (SOCK_EQ(sock, &addr))
			break;
	}
	freeaddrinfo(alist);

	if (ai != NULL) {
		errno = saved_errno;
		return pbuf;	/* forward check passed */
	}

    forward_fail:
	TRACE(1, ("%s forward check lookup fail: %s\n", pbuf,
		  gai_strerror(a_info)));
	LIB_GETBUF(pliar);
	snprintf(pliar, LIB_BUFLENGTH, "%s (%s)", stoa(sock), pbuf);

	errno = saved_errno;
	return pliar;
}
