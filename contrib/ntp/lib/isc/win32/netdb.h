/*
 * Copyright (C) 2004, 2006, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: netdb.h,v 1.9 2009/01/18 23:48:14 tbox Exp $ */

#ifndef NETDB_H
#define NETDB_H 1

#include <stddef.h>
#include <winsock2.h>

/*
 * Define if <netdb.h> does not declare struct addrinfo.
 */

struct addrinfo {
	int		ai_flags;      /* AI_PASSIVE, AI_CANONNAME */
	int		ai_family;     /* PF_xxx */
	int		ai_socktype;   /* SOCK_xxx */
	int		ai_protocol;   /* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	size_t		ai_addrlen;    /* Length of ai_addr */
	char		*ai_canonname; /* Canonical name for hostname */
	struct sockaddr	*ai_addr;      /* Binary address */
	struct addrinfo	*ai_next;      /* Next structure in linked list */
};


/*
 * Undefine all \#defines we are interested in as <netdb.h> may or may not have
 * defined them.
 */

/*
 * Error return codes from gethostbyname() and gethostbyaddr()
 * (left in extern int h_errno).
 */

#undef	NETDB_INTERNAL
#undef	NETDB_SUCCESS
#undef	HOST_NOT_FOUND
#undef	TRY_AGAIN
#undef	NO_RECOVERY
#undef	NO_DATA
#undef	NO_ADDRESS

#define	NETDB_INTERNAL	-1	/* see errno */
#define	NETDB_SUCCESS	0	/* no problem */
#define	HOST_NOT_FOUND	1 /* Authoritative Answer Host not found */
#define	TRY_AGAIN	2 /* Non-Authoritative Host not found, or SERVERFAIL */
#define	NO_RECOVERY	3 /* Non recoverable errors, FORMERR, REFUSED, NOTIMP */
#define	NO_DATA		4 /* Valid name, no data record of requested type */
#define	NO_ADDRESS	NO_DATA		/* no address, look for MX record */

/*
 * Error return codes from getaddrinfo()
 */

#undef	EAI_ADDRFAMILY
#undef	EAI_AGAIN
#undef	EAI_BADFLAGS
#undef	EAI_FAIL
#undef	EAI_FAMILY
#undef	EAI_MEMORY
#undef	EAI_NODATA
#undef	EAI_NONAME
#undef	EAI_SERVICE
#undef	EAI_SOCKTYPE
#undef	EAI_SYSTEM
#undef	EAI_BADHINTS
#undef	EAI_PROTOCOL
#undef	EAI_MAX

#define	EAI_ADDRFAMILY	 1	/* address family for hostname not supported */
#define	EAI_AGAIN	 2	/* temporary failure in name resolution */
#define	EAI_BADFLAGS	 3	/* invalid value for ai_flags */
#define	EAI_FAIL	 4	/* non-recoverable failure in name resolution */
#define	EAI_FAMILY	 5	/* ai_family not supported */
#define	EAI_MEMORY	 6	/* memory allocation failure */
#define	EAI_NODATA	 7	/* no address associated with hostname */
#define	EAI_NONAME	 8	/* hostname nor servname provided, or not known */
#define	EAI_SERVICE	 9	/* servname not supported for ai_socktype */
#define	EAI_SOCKTYPE	10	/* ai_socktype not supported */
#define	EAI_SYSTEM	11	/* system error returned in errno */
#define EAI_BADHINTS	12
#define EAI_PROTOCOL	13
#define EAI_MAX		14

/*
 * Flag values for getaddrinfo()
 */
#undef	AI_PASSIVE
#undef	AI_CANONNAME
#undef	AI_NUMERICHOST

#define	AI_PASSIVE	0x00000001
#define	AI_CANONNAME	0x00000002
#define AI_NUMERICHOST	0x00000004

/*
 * Flag values for getipnodebyname()
 */
#undef AI_V4MAPPED
#undef AI_ALL
#undef AI_ADDRCONFIG
#undef AI_DEFAULT

#define AI_V4MAPPED	0x00000008
#define AI_ALL		0x00000010
#define AI_ADDRCONFIG	0x00000020
#define AI_DEFAULT	(AI_V4MAPPED|AI_ADDRCONFIG)

/*
 * Constants for getnameinfo()
 */
#undef	NI_MAXHOST
#undef	NI_MAXSERV

#define	NI_MAXHOST	1025
#define	NI_MAXSERV	32

/*
 * Flag values for getnameinfo()
 */
#undef	NI_NOFQDN
#undef	NI_NUMERICHOST
#undef	NI_NAMEREQD
#undef	NI_NUMERICSERV
#undef	NI_DGRAM
#undef	NI_NUMERICSCOPE

#define	NI_NOFQDN	0x00000001
#define	NI_NUMERICHOST	0x00000002
#define	NI_NAMEREQD	0x00000004
#define	NI_NUMERICSERV	0x00000008
#define	NI_DGRAM	0x00000010
#define	NI_NUMERICSCOPE	0x00000020	/*2553bis-00*/

/*
 * Structures for getrrsetbyname()
 */
struct rdatainfo {
	unsigned int		rdi_length;
	unsigned char		*rdi_data;
};

struct rrsetinfo {
	unsigned int		rri_flags;
	int			rri_rdclass;
	int			rri_rdtype;
	unsigned int		rri_ttl;
	unsigned int		rri_nrdatas;
	unsigned int		rri_nsigs;
	char			*rri_name;
	struct rdatainfo	*rri_rdatas;
	struct rdatainfo	*rri_sigs;
};

/*
 * Flags for getrrsetbyname()
 */
#define RRSET_VALIDATED		0x00000001
	/* Set was dnssec validated */

/*
 * Return codes for getrrsetbyname()
 */
#define ERRSET_SUCCESS		0
#define ERRSET_NOMEMORY		1
#define ERRSET_FAIL		2
#define ERRSET_INVAL		3


#endif /* NETDB_H */
