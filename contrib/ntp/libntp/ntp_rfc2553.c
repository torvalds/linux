/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * Compatability shims with the rfc2553 API to simplify ntp.
 */

#include <config.h>

#include <sys/types.h>
#include <ctype.h>
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <isc/net.h>
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include "ntp_rfc2553.h"

#include "ntpd.h"
#include "ntp_malloc.h"
#include "ntp_string.h"
#include "ntp_debug.h"


/*
 * copy_addrinfo()	- copy a single addrinfo to malloc()'d block.
 * copy_addrinfo_list() - copy an addrinfo list to malloc()'d block.
 *
 * Copies an addrinfo list and its associated data to a contiguous block
 * of storage from emalloc().  Callback routines invoked via
 * getaddrinfo_sometime() have access to the resulting addrinfo list
 * only until they return.  This routine provides an easy way to make a
 * persistent copy.  Although the list provided to gai_sometime_callback
 * routines is similarly contiguous, to keep this code usable in any
 * context where we might want to duplicate an addrinfo list, it does
 * not require the input list be contiguous.
 *
 * The returned list head pointer is passed to free() to release the
 * entire list.
 *
 * In keeping with the rest of the NTP distribution, sockaddr_u is used
 * in preference to struct sockaddr_storage, which is a member of the
 * former union and so compatible.
 *
 * The rest of ntp_rfc2553.c is conditioned on ISC_PLATFORM_HAVEIPV6
 * not being defined, copy_addrinfo_*() are exceptions.
 */
struct addrinfo * copy_addrinfo_common(const struct addrinfo *, int
#ifdef EREALLOC_CALLSITE
								   ,
				       const char *, int
#endif
				       );


struct addrinfo *
copy_addrinfo_impl(
	const struct addrinfo *	src
#ifdef EREALLOC_CALLSITE
				   ,
	const char *		caller_file,
	int			caller_line
#endif
	)
{
	return copy_addrinfo_common(src, TRUE
#ifdef EREALLOC_CALLSITE
					      ,
				    caller_file, caller_line
#endif
				    );
}


struct addrinfo *
copy_addrinfo_list_impl(
	const struct addrinfo *	src
#ifdef EREALLOC_CALLSITE
				   ,
	const char *		caller_file,
	int			caller_line
#endif
	)
{
	return copy_addrinfo_common(src, FALSE
#ifdef EREALLOC_CALLSITE
					      ,
				    caller_file, caller_line
#endif
				    );
}


struct addrinfo *
copy_addrinfo_common(
	const struct addrinfo *	src,
	int			just_one
#ifdef EREALLOC_CALLSITE
					,
	const char *		caller_file,
	int			caller_line
#endif
	)
{
	const struct addrinfo *	ai_src;
	const struct addrinfo *	ai_nxt;
	struct addrinfo *	ai_cpy;
	struct addrinfo *	dst;
	sockaddr_u *		psau;
	char *			pcanon;
	u_int			elements;
	size_t			octets;
	size_t			canons_octets;
	size_t			str_octets;

	elements = 0;
	canons_octets = 0;

	for (ai_src = src; NULL != ai_src; ai_src = ai_nxt) {
		if (just_one)
			ai_nxt = NULL;
		else
			ai_nxt = ai_src->ai_next;
		++elements;
		if (NULL != ai_src->ai_canonname)
			canons_octets += 1 + strlen(ai_src->ai_canonname);
	}

	octets = elements * (sizeof(*ai_cpy) + sizeof(*psau));
	octets += canons_octets;

	dst = erealloczsite(NULL, octets, 0, TRUE, caller_file,
			    caller_line);
	ai_cpy = dst;
	psau = (void *)(ai_cpy + elements);
	pcanon = (void *)(psau + elements);

	for (ai_src = src; NULL != ai_src; ai_src = ai_nxt) {
		if (just_one)
			ai_nxt = NULL;
		else
			ai_nxt = ai_src->ai_next;
		*ai_cpy = *ai_src;
		DEBUG_INSIST(ai_cpy->ai_canonname == ai_src->ai_canonname);
		INSIST(ai_src->ai_addrlen <= sizeof(sockaddr_u));
		memcpy(psau, ai_src->ai_addr, ai_src->ai_addrlen);
		ai_cpy->ai_addr = &psau->sa;
		++psau;
		if (NULL != ai_src->ai_canonname) {
			ai_cpy->ai_canonname = pcanon;
			str_octets = 1 + strlen(ai_src->ai_canonname);
			memcpy(pcanon, ai_src->ai_canonname, str_octets);
			pcanon += str_octets;
		}
		if (NULL != ai_cpy->ai_next) {
			if (just_one)
				ai_cpy->ai_next = NULL;
			else
				ai_cpy->ai_next = ai_cpy + 1;
		}
		++ai_cpy;
	}
	ENSURE(pcanon == ((char *)dst + octets));

	return dst;
}


#ifndef ISC_PLATFORM_HAVEIPV6

static char *ai_errlist[] = {
	"Success",
	"Address family for hostname not supported",	/* EAI_ADDRFAMILY */
	"Temporary failure in name resolution",		/* EAI_AGAIN      */
	"Invalid value for ai_flags",		       	/* EAI_BADFLAGS   */
	"Non-recoverable failure in name resolution", 	/* EAI_FAIL       */
	"ai_family not supported",			/* EAI_FAMILY     */
	"Memory allocation failure", 			/* EAI_MEMORY     */
	"No address associated with hostname", 		/* EAI_NODATA     */
	"hostname nor servname provided, or not known",	/* EAI_NONAME     */
	"servname not supported for ai_socktype",	/* EAI_SERVICE    */
	"ai_socktype not supported", 			/* EAI_SOCKTYPE   */
	"System error returned in errno", 		/* EAI_SYSTEM     */
	"Invalid value for hints",			/* EAI_BADHINTS	  */
	"Resolved protocol is unknown",			/* EAI_PROTOCOL   */
	"Unknown error", 				/* EAI_MAX        */
};

/*
 * Local declaration
 */
int
DNSlookup_name(
	const char *name,
	int ai_family,
	struct hostent **Addresses
);

#ifndef SYS_WINNT
/*
 * Encapsulate gethostbyname to control the error code
 */
int
DNSlookup_name(
	const char *name,
	int ai_family,
	struct hostent **Addresses
)
{
	*Addresses = gethostbyname(name);
	return (h_errno);
}
#endif

static	int do_nodename (const char *nodename, struct addrinfo *ai,
    const struct addrinfo *hints);

int
getaddrinfo (const char *nodename, const char *servname,
	const struct addrinfo *hints, struct addrinfo **res)
{
	int rval;
	struct servent *sp;
	struct addrinfo *ai = NULL;
	int port;
	const char *proto = NULL;
	int family, socktype, flags, protocol;


	/*
	 * If no name is provide just return an error
	 */
	if (nodename == NULL && servname == NULL)
		return (EAI_NONAME);
	
	ai = calloc(sizeof(struct addrinfo), 1);
	if (ai == NULL)
		return (EAI_MEMORY);

	/*
	 * Copy default values from hints, if available
	 */
	if (hints != NULL) {
		ai->ai_flags = hints->ai_flags;
		ai->ai_family = hints->ai_family;
		ai->ai_socktype = hints->ai_socktype;
		ai->ai_protocol = hints->ai_protocol;

		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		flags = hints->ai_flags;

		switch (family) {
		case AF_UNSPEC:
			switch (hints->ai_socktype) {
			case SOCK_STREAM:
				proto = "tcp";
				break;
			case SOCK_DGRAM:
				proto = "udp";
				break;
			}
			break;
		case AF_INET:
		case AF_INET6:
			switch (hints->ai_socktype) {
			case 0:
				break;
			case SOCK_STREAM:
				proto = "tcp";
				break;
			case SOCK_DGRAM:
				proto = "udp";
				break;
			case SOCK_RAW:
				break;
			default:
				return (EAI_SOCKTYPE);
			}
			break;
#ifdef	AF_LOCAL
		case AF_LOCAL:
			switch (hints->ai_socktype) {
			case 0:
				break;
			case SOCK_STREAM:
				break;
			case SOCK_DGRAM:
				break;
			default:
				return (EAI_SOCKTYPE);
			}
			break;
#endif
		default:
			return (EAI_FAMILY);
		}
	} else {
		protocol = 0;
		family = 0;
		socktype = 0;
		flags = 0;
	}

	rval = do_nodename(nodename, ai, hints);
	if (rval != 0) {
		freeaddrinfo(ai);
		return (rval);
	}

	/*
	 * First, look up the service name (port) if it was
	 * requested.  If the socket type wasn't specified, then
	 * try and figure it out.
	 */
	if (servname != NULL) {
		char *e;

		port = strtol(servname, &e, 10);
		if (*e == '\0') {
			if (socktype == 0)
				return (EAI_SOCKTYPE);
			if (port < 0 || port > 65535)
				return (EAI_SERVICE);
			port = htons((unsigned short) port);
		} else {
			sp = getservbyname(servname, proto);
			if (sp == NULL)
				return (EAI_SERVICE);
			port = sp->s_port;
			if (socktype == 0) {
				if (strcmp(sp->s_proto, "tcp") == 0)
					socktype = SOCK_STREAM;
				else if (strcmp(sp->s_proto, "udp") == 0)
					socktype = SOCK_DGRAM;
			}
		}
	} else
		port = 0;

	/*
	 *
	 * Set up the port number
	 */
	if (ai->ai_family == AF_INET)
		((struct sockaddr_in *)ai->ai_addr)->sin_port = (unsigned short) port;
	else if (ai->ai_family == AF_INET6)
		((struct sockaddr_in6 *)ai->ai_addr)->sin6_port = (unsigned short) port;
	*res = ai;
	return (0);
}

void
freeaddrinfo(struct addrinfo *ai)
{
	if (ai->ai_canonname != NULL)
	{
		free(ai->ai_canonname);
		ai->ai_canonname = NULL;
	}
	if (ai->ai_addr != NULL)
	{
		free(ai->ai_addr);
		ai->ai_addr = NULL;
	}
	free(ai);
	ai = NULL;
}

int
getnameinfo (const struct sockaddr *sa, u_int salen, char *host,
	size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct hostent *hp;

	if (sa->sa_family != AF_INET)
		return (EAI_FAMILY);
	hp = gethostbyaddr(
	    (const char *)&((const struct sockaddr_in *)sa)->sin_addr,
	    4, AF_INET);
	if (hp == NULL) {
		if (h_errno == TRY_AGAIN)
			return (EAI_AGAIN);
		else
			return (EAI_FAIL);
	}
	if (host != NULL && hostlen > 0)
		strlcpy(host, hp->h_name, hostlen);
	return (0);
}

char *
gai_strerror(int ecode)
{
	if (ecode < 0 || ecode > EAI_MAX)
		ecode = EAI_MAX;
	return ai_errlist[ecode];
}

static int
do_nodename(
	const char *nodename,
	struct addrinfo *ai,
	const struct addrinfo *hints)
{
	struct hostent *hp = NULL;
	struct sockaddr_in *sockin;
	struct sockaddr_in6 *sockin6;
	int errval;

	ai->ai_addr = calloc(sizeof(struct sockaddr_storage), 1);
	if (ai->ai_addr == NULL)
		return (EAI_MEMORY);

	/*
	 * For an empty node name just use the wildcard.
	 * NOTE: We need to assume that the address family is
	 * set elsewhere so that we can set the appropriate wildcard
	 */
	if (nodename == NULL) {
		if (ai->ai_family == AF_INET)
		{
			ai->ai_addrlen = sizeof(struct sockaddr_in);
			sockin = (struct sockaddr_in *)ai->ai_addr;
			sockin->sin_family = (short) ai->ai_family;
			sockin->sin_addr.s_addr = htonl(INADDR_ANY);
		}
		else
		{
			ai->ai_addrlen = sizeof(struct sockaddr_in6);
			sockin6 = (struct sockaddr_in6 *)ai->ai_addr;
			sockin6->sin6_family = (short) ai->ai_family;
			/*
			 * we have already zeroed out the address
			 * so we don't actually need to do this
			 * This assignment is causing problems so
			 * we don't do what this would do.
			 sockin6->sin6_addr = in6addr_any;
			 */
		}
#ifdef ISC_PLATFORM_HAVESALEN
		ai->ai_addr->sa_len = SOCKLEN(ai->ai_addr);
#endif

		return (0);
	}

	/*
	 * See if we have an IPv6 address
	 */
	if(strchr(nodename, ':') != NULL) {
		if (inet_pton(AF_INET6, nodename,
		    &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr) == 1) {
			((struct sockaddr_in6 *)ai->ai_addr)->sin6_family = AF_INET6;
			ai->ai_family = AF_INET6;
			ai->ai_addrlen = sizeof(struct sockaddr_in6);
			return (0);
		}
	}

	/*
	 * See if we have an IPv4 address
	 */
	if (inet_pton(AF_INET, nodename,
	    &((struct sockaddr_in *)ai->ai_addr)->sin_addr) == 1) {
		((struct sockaddr *)ai->ai_addr)->sa_family = AF_INET;
		ai->ai_family = AF_INET;
		ai->ai_addrlen = sizeof(struct sockaddr_in);
		return (0);
	}

	/*
	 * If the numeric host flag is set, don't attempt resolution
	 */
	if (hints != NULL && (hints->ai_flags & AI_NUMERICHOST))
		return (EAI_NONAME);

	/*
	 * Look for a name
	 */

	errval = DNSlookup_name(nodename, AF_INET, &hp);

	if (hp == NULL) {
		if (errval == TRY_AGAIN || errval == EAI_AGAIN)
			return (EAI_AGAIN);
		else if (errval == EAI_NONAME) {
			if (inet_pton(AF_INET, nodename,
			    &((struct sockaddr_in *)ai->ai_addr)->sin_addr) == 1) {
				((struct sockaddr *)ai->ai_addr)->sa_family = AF_INET;
				ai->ai_family = AF_INET;
				ai->ai_addrlen = sizeof(struct sockaddr_in);
				return (0);
			}
			return (errval);
		}
		else
		{
			return (errval);
		}
	}
	ai->ai_family = hp->h_addrtype;
	ai->ai_addrlen = sizeof(struct sockaddr);
	sockin = (struct sockaddr_in *)ai->ai_addr;
	memcpy(&sockin->sin_addr, hp->h_addr, hp->h_length);
	ai->ai_addr->sa_family = hp->h_addrtype;
#ifdef ISC_PLATFORM_HAVESALEN
	ai->ai_addr->sa_len = sizeof(struct sockaddr);
#endif
	if (hints != NULL && (hints->ai_flags & AI_CANONNAME))
		ai->ai_canonname = estrdup(hp->h_name);
	return (0);
}

#endif /* !ISC_PLATFORM_HAVEIPV6 */
