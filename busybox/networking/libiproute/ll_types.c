/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include <sys/socket.h> /* linux/if_arp.h needs it on some systems */
#include <arpa/inet.h>
#include <linux/if_arp.h>

#include "libbb.h"
#include "rt_names.h"

const char* FAST_FUNC ll_type_n2a(int type, char *buf)
{
	static const char arphrd_name[] ALIGN1 =
	/* 0,                  */ "generic" "\0"
	/* ARPHRD_LOOPBACK,    */ "loopback" "\0"
	/* ARPHRD_ETHER,       */ "ether" "\0"
#ifdef ARPHRD_INFINIBAND
	/* ARPHRD_INFINIBAND,  */ "infiniband" "\0"
#endif
#ifdef ARPHRD_IEEE802_TR
	/* ARPHRD_IEEE802,     */ "ieee802" "\0"
	/* ARPHRD_IEEE802_TR,  */ "tr" "\0"
#else
	/* ARPHRD_IEEE802,     */ "tr" "\0"
#endif
#ifdef ARPHRD_IEEE80211
	/* ARPHRD_IEEE80211,   */ "ieee802.11" "\0"
#endif
#ifdef ARPHRD_IEEE1394
	/* ARPHRD_IEEE1394,    */ "ieee1394" "\0"
#endif
	/* ARPHRD_IRDA,        */ "irda" "\0"
	/* ARPHRD_SLIP,        */ "slip" "\0"
	/* ARPHRD_CSLIP,       */ "cslip" "\0"
	/* ARPHRD_SLIP6,       */ "slip6" "\0"
	/* ARPHRD_CSLIP6,      */ "cslip6" "\0"
	/* ARPHRD_PPP,         */ "ppp" "\0"
	/* ARPHRD_TUNNEL,      */ "ipip" "\0"
	/* ARPHRD_TUNNEL6,     */ "tunnel6" "\0"
	/* ARPHRD_SIT,         */ "sit" "\0"
	/* ARPHRD_IPGRE,       */ "gre" "\0"
#ifdef ARPHRD_VOID
	/* ARPHRD_VOID,        */ "void" "\0"
#endif

#if ENABLE_FEATURE_IP_RARE_PROTOCOLS
	/* ARPHRD_EETHER,      */ "eether" "\0"
	/* ARPHRD_AX25,        */ "ax25" "\0"
	/* ARPHRD_PRONET,      */ "pronet" "\0"
	/* ARPHRD_CHAOS,       */ "chaos" "\0"
	/* ARPHRD_ARCNET,      */ "arcnet" "\0"
	/* ARPHRD_APPLETLK,    */ "atalk" "\0"
	/* ARPHRD_DLCI,        */ "dlci" "\0"
#ifdef ARPHRD_ATM
	/* ARPHRD_ATM,         */ "atm" "\0"
#endif
	/* ARPHRD_METRICOM,    */ "metricom" "\0"
	/* ARPHRD_RSRVD,       */ "rsrvd" "\0"
	/* ARPHRD_ADAPT,       */ "adapt" "\0"
	/* ARPHRD_ROSE,        */ "rose" "\0"
	/* ARPHRD_X25,         */ "x25" "\0"
#ifdef ARPHRD_HWX25
	/* ARPHRD_HWX25,       */ "hwx25" "\0"
#endif
	/* ARPHRD_HDLC,        */ "hdlc" "\0"
	/* ARPHRD_LAPB,        */ "lapb" "\0"
#ifdef ARPHRD_DDCMP
	/* ARPHRD_DDCMP,       */ "ddcmp" "\0"
	/* ARPHRD_RAWHDLC,     */ "rawhdlc" "\0"
#endif
	/* ARPHRD_FRAD,        */ "frad" "\0"
	/* ARPHRD_SKIP,        */ "skip" "\0"
	/* ARPHRD_LOCALTLK,    */ "ltalk" "\0"
	/* ARPHRD_FDDI,        */ "fddi" "\0"
	/* ARPHRD_BIF,         */ "bif" "\0"
	/* ARPHRD_IPDDP,       */ "ip/ddp" "\0"
	/* ARPHRD_PIMREG,      */ "pimreg" "\0"
	/* ARPHRD_HIPPI,       */ "hippi" "\0"
	/* ARPHRD_ASH,         */ "ash" "\0"
	/* ARPHRD_ECONET,      */ "econet" "\0"
	/* ARPHRD_FCPP,        */ "fcpp" "\0"
	/* ARPHRD_FCAL,        */ "fcal" "\0"
	/* ARPHRD_FCPL,        */ "fcpl" "\0"
	/* ARPHRD_FCFABRIC,    */ "fcfb0" "\0"
	/* ARPHRD_FCFABRIC+1,  */ "fcfb1" "\0"
	/* ARPHRD_FCFABRIC+2,  */ "fcfb2" "\0"
	/* ARPHRD_FCFABRIC+3,  */ "fcfb3" "\0"
	/* ARPHRD_FCFABRIC+4,  */ "fcfb4" "\0"
	/* ARPHRD_FCFABRIC+5,  */ "fcfb5" "\0"
	/* ARPHRD_FCFABRIC+6,  */ "fcfb6" "\0"
	/* ARPHRD_FCFABRIC+7,  */ "fcfb7" "\0"
	/* ARPHRD_FCFABRIC+8,  */ "fcfb8" "\0"
	/* ARPHRD_FCFABRIC+9,  */ "fcfb9" "\0"
	/* ARPHRD_FCFABRIC+10, */ "fcfb10" "\0"
	/* ARPHRD_FCFABRIC+11, */ "fcfb11" "\0"
	/* ARPHRD_FCFABRIC+12, */ "fcfb12" "\0"
#endif /* FEATURE_IP_RARE_PROTOCOLS */
	;

	/* Keep these arrays in sync! */

	static const uint16_t arphrd_type[] ALIGN2 = {
	0,                  /* "generic" "\0" */
	ARPHRD_LOOPBACK,    /* "loopback" "\0" */
	ARPHRD_ETHER,       /* "ether" "\0" */
#ifdef ARPHRD_INFINIBAND
	ARPHRD_INFINIBAND,  /* "infiniband" "\0" */
#endif
#ifdef ARPHRD_IEEE802_TR
	ARPHRD_IEEE802,     /* "ieee802" "\0" */
	ARPHRD_IEEE802_TR,  /* "tr" "\0" */
#else
	ARPHRD_IEEE802,     /* "tr" "\0" */
#endif
#ifdef ARPHRD_IEEE80211
	ARPHRD_IEEE80211,   /* "ieee802.11" "\0" */
#endif
#ifdef ARPHRD_IEEE1394
	ARPHRD_IEEE1394,    /* "ieee1394" "\0" */
#endif
	ARPHRD_IRDA,        /* "irda" "\0" */
	ARPHRD_SLIP,        /* "slip" "\0" */
	ARPHRD_CSLIP,       /* "cslip" "\0" */
	ARPHRD_SLIP6,       /* "slip6" "\0" */
	ARPHRD_CSLIP6,      /* "cslip6" "\0" */
	ARPHRD_PPP,         /* "ppp" "\0" */
	ARPHRD_TUNNEL,      /* "ipip" "\0" */
	ARPHRD_TUNNEL6,     /* "tunnel6" "\0" */
	ARPHRD_SIT,         /* "sit" "\0" */
	ARPHRD_IPGRE,       /* "gre" "\0" */
#ifdef ARPHRD_VOID
	ARPHRD_VOID,        /* "void" "\0" */
#endif

#if ENABLE_FEATURE_IP_RARE_PROTOCOLS
	ARPHRD_EETHER,      /* "eether" "\0" */
	ARPHRD_AX25,        /* "ax25" "\0" */
	ARPHRD_PRONET,      /* "pronet" "\0" */
	ARPHRD_CHAOS,       /* "chaos" "\0" */
	ARPHRD_ARCNET,      /* "arcnet" "\0" */
	ARPHRD_APPLETLK,    /* "atalk" "\0" */
	ARPHRD_DLCI,        /* "dlci" "\0" */
#ifdef ARPHRD_ATM
	ARPHRD_ATM,         /* "atm" "\0" */
#endif
	ARPHRD_METRICOM,    /* "metricom" "\0" */
	ARPHRD_RSRVD,       /* "rsrvd" "\0" */
	ARPHRD_ADAPT,       /* "adapt" "\0" */
	ARPHRD_ROSE,        /* "rose" "\0" */
	ARPHRD_X25,         /* "x25" "\0" */
#ifdef ARPHRD_HWX25
	ARPHRD_HWX25,       /* "hwx25" "\0" */
#endif
	ARPHRD_HDLC,        /* "hdlc" "\0" */
	ARPHRD_LAPB,        /* "lapb" "\0" */
#ifdef ARPHRD_DDCMP
	ARPHRD_DDCMP,       /* "ddcmp" "\0" */
	ARPHRD_RAWHDLC,     /* "rawhdlc" "\0" */
#endif
	ARPHRD_FRAD,        /* "frad" "\0" */
	ARPHRD_SKIP,        /* "skip" "\0" */
	ARPHRD_LOCALTLK,    /* "ltalk" "\0" */
	ARPHRD_FDDI,        /* "fddi" "\0" */
	ARPHRD_BIF,         /* "bif" "\0" */
	ARPHRD_IPDDP,       /* "ip/ddp" "\0" */
	ARPHRD_PIMREG,      /* "pimreg" "\0" */
	ARPHRD_HIPPI,       /* "hippi" "\0" */
	ARPHRD_ASH,         /* "ash" "\0" */
	ARPHRD_ECONET,      /* "econet" "\0" */
	ARPHRD_FCPP,        /* "fcpp" "\0" */
	ARPHRD_FCAL,        /* "fcal" "\0" */
	ARPHRD_FCPL,        /* "fcpl" "\0" */
	ARPHRD_FCFABRIC,    /* "fcfb0" "\0" */
	ARPHRD_FCFABRIC+1,  /* "fcfb1" "\0" */
	ARPHRD_FCFABRIC+2,  /* "fcfb2" "\0" */
	ARPHRD_FCFABRIC+3,  /* "fcfb3" "\0" */
	ARPHRD_FCFABRIC+4,  /* "fcfb4" "\0" */
	ARPHRD_FCFABRIC+5,  /* "fcfb5" "\0" */
	ARPHRD_FCFABRIC+6,  /* "fcfb6" "\0" */
	ARPHRD_FCFABRIC+7,  /* "fcfb7" "\0" */
	ARPHRD_FCFABRIC+8,  /* "fcfb8" "\0" */
	ARPHRD_FCFABRIC+9,  /* "fcfb9" "\0" */
	ARPHRD_FCFABRIC+10, /* "fcfb10" "\0" */
	ARPHRD_FCFABRIC+11, /* "fcfb11" "\0" */
	ARPHRD_FCFABRIC+12, /* "fcfb12" "\0" */
#endif /* FEATURE_IP_RARE_PROTOCOLS */
	};

	unsigned i;
	const char *aname = arphrd_name;
	for (i = 0; i < ARRAY_SIZE(arphrd_type); i++) {
		if (arphrd_type[i] == type)
			return aname;
		aname += strlen(aname) + 1;
	}
	sprintf(buf, "[%d]", type);
	return buf;
}
