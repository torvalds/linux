/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */
#include "ipf.h"

#ifndef USE_INET6
# undef		ICMP6_ECHO_REQUEST
# define	ICMP6_ECHO_REQUEST	0
# undef		ICMP6_ECHO_REPLY
# define	ICMP6_ECHO_REPLY	0
# undef		ICMP6_NI_QUERY
# define	ICMP6_NI_QUERY		0
# undef		ICMP6_NI_REPLY
# define	ICMP6_NI_REPLY		0
# undef		ICMP6_PARAM_PROB
# define	ICMP6_PARAM_PROB	0
# undef		ND_ROUTER_ADVERT
# define	ND_ROUTER_ADVERT	0
# undef		ND_ROUTER_SOLICIT
# define	ND_ROUTER_SOLICIT	0
# undef		ICMP6_TIME_EXCEEDED
# define	ICMP6_TIME_EXCEEDED	0
# undef		ICMP6_DST_UNREACH
# define	ICMP6_DST_UNREACH	0
# undef		ICMP6_PACKET_TOO_BIG
# define	ICMP6_PACKET_TOO_BIG	0
# undef		MLD_LISTENER_QUERY
# define	MLD_LISTENER_QUERY	0
# undef		MLD_LISTENER_REPORT
# define	MLD_LISTENER_REPORT	0
# undef		MLD_LISTENER_DONE
# define	MLD_LISTENER_DONE	0
# undef		ICMP6_MEMBERSHIP_QUERY
# define	ICMP6_MEMBERSHIP_QUERY	0
# undef		ICMP6_MEMBERSHIP_REPORT
# define	ICMP6_MEMBERSHIP_REPORT	0
# undef		ICMP6_MEMBERSHIP_REDUCTION
# define	ICMP6_MEMBERSHIP_REDUCTION	0
# undef		ND_NEIGHBOR_ADVERT
# define	ND_NEIGHBOR_ADVERT	0
# undef		ND_NEIGHBOR_SOLICIT
# define	ND_NEIGHBOR_SOLICIT	0
# undef		ICMP6_ROUTER_RENUMBERING
# define	ICMP6_ROUTER_RENUMBERING	0
# undef		ICMP6_WRUREQUEST
# define	ICMP6_WRUREQUEST	0
# undef		ICMP6_WRUREPLY
# define	ICMP6_WRUREPLY		0
# undef		ICMP6_FQDN_QUERY
# define	ICMP6_FQDN_QUERY	0
# undef		ICMP6_FQDN_REPLY
# define	ICMP6_FQDN_REPLY	0
#else
# if !defined(MLD_LISTENER_QUERY)
#  define	MLD_LISTENER_QUERY	130
# endif
# if !defined(MLD_LISTENER_REPORT)
#  define	MLD_LISTENER_REPORT	131
# endif
# if !defined(MLD_LISTENER_DONE)
#  define	MLD_LISTENER_DONE	132
# endif
# if defined(MLD_LISTENER_REDUCTION) && !defined(MLD_LISTENER_DONE)
#  define	MLD_LISTENER_DONE	MLD_LISTENER_REDUCTION
# endif
#endif

icmptype_t icmptypelist[] = {
	{ "echo",	ICMP_ECHO,		ICMP6_ECHO_REQUEST },
	{ "echorep",	ICMP_ECHOREPLY,		ICMP6_ECHO_REPLY },
	{ "fqdnquery",	-1,			ICMP6_FQDN_QUERY },
	{ "fqdnreply",	-1,			ICMP6_FQDN_REPLY },
	{ "infoqry",	-1,			ICMP6_NI_QUERY },
	{ "inforeq",	ICMP_IREQ,		ICMP6_NI_QUERY },
	{ "inforep",	ICMP_IREQREPLY,		ICMP6_NI_REPLY },
	{ "listendone",	-1,			MLD_LISTENER_DONE },
	{ "listenqry",	-1,			MLD_LISTENER_QUERY },
	{ "listenrep",	-1,			MLD_LISTENER_REPORT },
	{ "maskrep",	ICMP_MASKREPLY,		-1 },
	{ "maskreq",	ICMP_MASKREQ,		-1 },
	{ "memberqry",	-1,			ICMP6_MEMBERSHIP_QUERY },
	{ "memberred",	-1,			ICMP6_MEMBERSHIP_REDUCTION },
	{ "memberreply",-1,			ICMP6_MEMBERSHIP_REPORT },
	{ "neighadvert",	-1,		ND_NEIGHBOR_ADVERT },
	{ "neighborsol",	-1,		ND_NEIGHBOR_SOLICIT },
	{ "neighborsolicit",	-1,		ND_NEIGHBOR_SOLICIT },
	{ "paramprob",	ICMP_PARAMPROB,		ICMP6_PARAM_PROB },
	{ "redir",	ICMP_REDIRECT,		ND_REDIRECT },
	{ "renumber",	-1,			ICMP6_ROUTER_RENUMBERING },
	{ "routerad",	ICMP_ROUTERADVERT,	ND_ROUTER_ADVERT },
	{ "routeradvert",ICMP_ROUTERADVERT,	ND_ROUTER_ADVERT },
	{ "routersol",	ICMP_ROUTERSOLICIT,	ND_ROUTER_SOLICIT },
	{ "routersolcit",ICMP_ROUTERSOLICIT,	ND_ROUTER_SOLICIT },
	{ "squench",	ICMP_SOURCEQUENCH,	-1 },
	{ "timest",	ICMP_TSTAMP,		-1 },
	{ "timestrep",	ICMP_TSTAMPREPLY,	-1 },
	{ "timex",	ICMP_TIMXCEED,		ICMP6_TIME_EXCEEDED },
	{ "toobig",	-1,			ICMP6_PACKET_TOO_BIG },
	{ "unreach",	ICMP_UNREACH,		ICMP6_DST_UNREACH },
	{ "whorep",	-1,			ICMP6_WRUREPLY },
	{ "whoreq",	-1,			ICMP6_WRUREQUEST },
	{ NULL,		-1,			-1 }
};
