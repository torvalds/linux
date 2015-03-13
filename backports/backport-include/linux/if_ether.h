#ifndef __BACKPORT_IF_ETHER_H
#define __BACKPORT_IF_ETHER_H
#include_next <linux/if_ether.h>

/* See commit b62faf3c in next-20140311 */
#ifndef ETH_P_80221
#define ETH_P_80221	0x8917	/* IEEE 802.21 Media Independent Handover Protocol */
#endif

/*
 * backport of:
 * commit e5c5d22e8dcf7c2d430336cbf8e180bd38e8daf1
 * Author: Simon Horman <horms@verge.net.au>
 * Date:   Thu Mar 28 13:38:25 2013 +0900
 * 
 *     net: add ETH_P_802_3_MIN
 */
#ifndef ETH_P_802_3_MIN
#define ETH_P_802_3_MIN 0x0600
#endif

#ifndef ETH_P_TDLS
#define ETH_P_TDLS	0x890D          /* TDLS */
#endif

#ifndef ETH_P_LINK_CTL
#define ETH_P_LINK_CTL	0x886c
#endif

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E      /* Port Access Entity (IEEE 802.1X) */
#endif

#ifndef ETH_P_TEB
#define ETH_P_TEB	0x6558		/* Trans Ether Bridging		*/
#endif

#ifndef ETH_P_8021AD
#define ETH_P_8021AD	0x88A8          /* 802.1ad Service VLAN		*/
#endif

#endif /* __BACKPORT_IF_ETHER_H */
