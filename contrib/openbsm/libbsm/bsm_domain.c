/*-
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. 
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <config/config.h>

#include <bsm/audit_domain.h>
#include <bsm/libbsm.h>

struct bsm_domain {
	u_short	bd_bsm_domain;
	int	bd_local_domain;
};

#define	PF_NO_LOCAL_MAPPING	-600

static const struct bsm_domain bsm_domains[] = {
	{ BSM_PF_UNSPEC, PF_UNSPEC },
	{ BSM_PF_LOCAL, PF_LOCAL },
	{ BSM_PF_INET, PF_INET },
	{ BSM_PF_IMPLINK,
#ifdef PF_IMPLINK
	PF_IMPLINK
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_PUP,
#ifdef PF_PUP
	PF_PUP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_CHAOS,
#ifdef PF_CHAOS
	PF_CHAOS
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NS,
#ifdef PF_NS
	PF_NS
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NBS,
#ifdef PF_NBS
	PF_NBS
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ECMA,
#ifdef PF_ECMA
	PF_ECMA
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_DATAKIT,
#ifdef PF_DATAKIT
	PF_DATAKIT
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_CCITT,
#ifdef PF_CCITT
	PF_CCITT
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_SNA, PF_SNA },
	{ BSM_PF_DECnet, PF_DECnet },
	{ BSM_PF_DLI,
#ifdef PF_DLI
	PF_DLI
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_LAT,
#ifdef PF_LAT
	PF_LAT
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_HYLINK,
#ifdef PF_HYLINK
	PF_HYLINK
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_APPLETALK, PF_APPLETALK },
	{ BSM_PF_NIT,
#ifdef PF_NIT
	PF_NIT
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_802,
#ifdef PF_802
	PF_802
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_OSI,
#ifdef PF_OSI
	PF_OSI
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_X25,
#ifdef PF_X25
	PF_X25
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_OSINET,
#ifdef PF_OSINET
	PF_OSINET
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_GOSIP,
#ifdef PF_GOSIP
	PF_GOSIP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_IPX, PF_IPX },
	{ BSM_PF_ROUTE, PF_ROUTE },
	{ BSM_PF_LINK,
#ifdef PF_LINK
	PF_LINK
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_INET6, PF_INET6 },
	{ BSM_PF_KEY, PF_KEY },
	{ BSM_PF_NCA,
#ifdef PF_NCA
	PF_NCA
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_POLICY,
#ifdef PF_POLICY
	PF_POLICY
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_INET_OFFLOAD,
#ifdef PF_INET_OFFLOAD
	PF_INET_OFFLOAD
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NETBIOS,
#ifdef PF_NETBIOS
	PF_NETBIOS
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ISO,
#ifdef PF_ISO
	PF_ISO
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_XTP,
#ifdef PF_XTP
	PF_XTP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_COIP,
#ifdef PF_COIP
	PF_COIP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_CNT,
#ifdef PF_CNT
	PF_CNT
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_RTIP,
#ifdef PF_RTIP
	PF_RTIP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_SIP,
#ifdef PF_SIP
	PF_SIP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_PIP,
#ifdef PF_PIP
	PF_PIP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ISDN,
#ifdef PF_ISDN
	PF_ISDN
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_E164,
#ifdef PF_E164
	PF_E164
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NATM,
#ifdef PF_NATM
	PF_NATM
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ATM,
#ifdef PF_ATM
	PF_ATM
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NETGRAPH,
#ifdef PF_NETGRAPH
	PF_NETGRAPH
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_SLOW,
#ifdef PF_SLOW
	PF_SLOW
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_SCLUSTER,
#ifdef PF_SCLUSTER
	PF_SCLUSTER
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ARP,
#ifdef PF_ARP
	PF_ARP
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_BLUETOOTH,
#ifdef PF_BLUETOOTH
	PF_BLUETOOTH
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_AX25,
#ifdef PF_AX25
	PF_AX25
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ROSE,
#ifdef PF_ROSE
	PF_ROSE
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_NETBEUI,
#ifdef PF_NETBEUI
	PF_NETBEUI
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_SECURITY,
#ifdef PF_SECURITY
	PF_SECURITY
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_PACKET,
#ifdef PF_PACKET
	PF_PACKET
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ASH,
#ifdef PF_ASH
	PF_ASH
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ECONET,
#ifdef PF_ECONET
	PF_ECONET
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_ATMSVC,
#ifdef PF_ATMSVC
	PF_ATMSVC
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_IRDA,
#ifdef PF_IRDA
	PF_IRDA
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_PPPOX,
#ifdef PF_PPPOX
	PF_PPPOX
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_WANPIPE,
#ifdef PF_WANPIPE
	PF_WANPIPE
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_LLC,
#ifdef PF_LLC
	PF_LLC
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_CAN,
#ifdef PF_CAN
	PF_CAN
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_TIPC,
#ifdef PF_TIPC
	PF_TIPC
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_IUCV,
#ifdef PF_IUCV
	PF_IUCV
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_RXRPC,
#ifdef PF_RXRPC
	PF_RXRPC
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
	{ BSM_PF_PHONET,
#ifdef PF_PHONET
	PF_PHONET
#else
	PF_NO_LOCAL_MAPPING
#endif
	},
};
static const int bsm_domains_count = sizeof(bsm_domains) /
	    sizeof(bsm_domains[0]);

static const struct bsm_domain *
bsm_lookup_local_domain(int local_domain)
{
	int i;

	for (i = 0; i < bsm_domains_count; i++) {
		if (bsm_domains[i].bd_local_domain == local_domain)
			return (&bsm_domains[i]);
	}
	return (NULL);
}

u_short
au_domain_to_bsm(int local_domain)
{
	const struct bsm_domain *bstp;

	bstp = bsm_lookup_local_domain(local_domain);
	if (bstp == NULL)
		return (BSM_PF_UNKNOWN);
	return (bstp->bd_bsm_domain);
}

static const struct bsm_domain *
bsm_lookup_bsm_domain(u_short bsm_domain)
{
	int i;

	for (i = 0; i < bsm_domains_count; i++) {
		if (bsm_domains[i].bd_bsm_domain == bsm_domain)
			return (&bsm_domains[i]);
	}
	return (NULL);
}

int
au_bsm_to_domain(u_short bsm_domain, int *local_domainp)
{
	const struct bsm_domain *bstp;

	bstp = bsm_lookup_bsm_domain(bsm_domain);
	if (bstp == NULL || bstp->bd_local_domain)
		return (-1);
	*local_domainp = bstp->bd_local_domain;
	return (0);
}
