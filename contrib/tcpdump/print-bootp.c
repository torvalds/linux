/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: BOOTP and IPv4 DHCP printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

static const char tstr[] = " [|bootp]";

/*
 * Bootstrap Protocol (BOOTP).  RFC951 and RFC1048.
 *
 * This file specifies the "implementation-independent" BOOTP protocol
 * information which is common to both client and server.
 *
 * Copyright 1988 by Carnegie Mellon.
 *
 * Permission to use, copy, modify, and distribute this program for any
 * purpose and without fee is hereby granted, provided that this copyright
 * and permission notice appear on all copies and supporting documentation,
 * the name of Carnegie Mellon not be used in advertising or publicity
 * pertaining to distribution of the program without specific prior
 * permission, and notice be given in supporting documentation that copying
 * and distribution is by permission of Carnegie Mellon and Stanford
 * University.  Carnegie Mellon makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

struct bootp {
	uint8_t		bp_op;		/* packet opcode type */
	uint8_t		bp_htype;	/* hardware addr type */
	uint8_t		bp_hlen;	/* hardware addr length */
	uint8_t		bp_hops;	/* gateway hops */
	uint32_t	bp_xid;		/* transaction ID */
	uint16_t	bp_secs;	/* seconds since boot began */
	uint16_t	bp_flags;	/* flags - see bootp_flag_values[]
					   in print-bootp.c */
	struct in_addr	bp_ciaddr;	/* client IP address */
	struct in_addr	bp_yiaddr;	/* 'your' IP address */
	struct in_addr	bp_siaddr;	/* server IP address */
	struct in_addr	bp_giaddr;	/* gateway IP address */
	uint8_t		bp_chaddr[16];	/* client hardware address */
	uint8_t		bp_sname[64];	/* server host name */
	uint8_t		bp_file[128];	/* boot file name */
	uint8_t		bp_vend[64];	/* vendor-specific area */
} UNALIGNED;

#define BOOTPREPLY	2
#define BOOTPREQUEST	1

/*
 * Vendor magic cookie (v_magic) for CMU
 */
#define VM_CMU		"CMU"

/*
 * Vendor magic cookie (v_magic) for RFC1048
 */
#define VM_RFC1048	{ 99, 130, 83, 99 }

/*
 * RFC1048 tag values used to specify what information is being supplied in
 * the vendor field of the packet.
 */

#define TAG_PAD			((uint8_t)   0)
#define TAG_SUBNET_MASK		((uint8_t)   1)
#define TAG_TIME_OFFSET		((uint8_t)   2)
#define TAG_GATEWAY		((uint8_t)   3)
#define TAG_TIME_SERVER		((uint8_t)   4)
#define TAG_NAME_SERVER		((uint8_t)   5)
#define TAG_DOMAIN_SERVER	((uint8_t)   6)
#define TAG_LOG_SERVER		((uint8_t)   7)
#define TAG_COOKIE_SERVER	((uint8_t)   8)
#define TAG_LPR_SERVER		((uint8_t)   9)
#define TAG_IMPRESS_SERVER	((uint8_t)  10)
#define TAG_RLP_SERVER		((uint8_t)  11)
#define TAG_HOSTNAME		((uint8_t)  12)
#define TAG_BOOTSIZE		((uint8_t)  13)
#define TAG_END			((uint8_t) 255)
/* RFC1497 tags */
#define	TAG_DUMPPATH		((uint8_t)  14)
#define	TAG_DOMAINNAME		((uint8_t)  15)
#define	TAG_SWAP_SERVER		((uint8_t)  16)
#define	TAG_ROOTPATH		((uint8_t)  17)
#define	TAG_EXTPATH		((uint8_t)  18)
/* RFC2132 */
#define	TAG_IP_FORWARD		((uint8_t)  19)
#define	TAG_NL_SRCRT		((uint8_t)  20)
#define	TAG_PFILTERS		((uint8_t)  21)
#define	TAG_REASS_SIZE		((uint8_t)  22)
#define	TAG_DEF_TTL		((uint8_t)  23)
#define	TAG_MTU_TIMEOUT		((uint8_t)  24)
#define	TAG_MTU_TABLE		((uint8_t)  25)
#define	TAG_INT_MTU		((uint8_t)  26)
#define	TAG_LOCAL_SUBNETS	((uint8_t)  27)
#define	TAG_BROAD_ADDR		((uint8_t)  28)
#define	TAG_DO_MASK_DISC	((uint8_t)  29)
#define	TAG_SUPPLY_MASK		((uint8_t)  30)
#define	TAG_DO_RDISC		((uint8_t)  31)
#define	TAG_RTR_SOL_ADDR	((uint8_t)  32)
#define	TAG_STATIC_ROUTE	((uint8_t)  33)
#define	TAG_USE_TRAILERS	((uint8_t)  34)
#define	TAG_ARP_TIMEOUT		((uint8_t)  35)
#define	TAG_ETH_ENCAP		((uint8_t)  36)
#define	TAG_TCP_TTL		((uint8_t)  37)
#define	TAG_TCP_KEEPALIVE	((uint8_t)  38)
#define	TAG_KEEPALIVE_GO	((uint8_t)  39)
#define	TAG_NIS_DOMAIN		((uint8_t)  40)
#define	TAG_NIS_SERVERS		((uint8_t)  41)
#define	TAG_NTP_SERVERS		((uint8_t)  42)
#define	TAG_VENDOR_OPTS		((uint8_t)  43)
#define	TAG_NETBIOS_NS		((uint8_t)  44)
#define	TAG_NETBIOS_DDS		((uint8_t)  45)
#define	TAG_NETBIOS_NODE	((uint8_t)  46)
#define	TAG_NETBIOS_SCOPE	((uint8_t)  47)
#define	TAG_XWIN_FS		((uint8_t)  48)
#define	TAG_XWIN_DM		((uint8_t)  49)
#define	TAG_NIS_P_DOMAIN	((uint8_t)  64)
#define	TAG_NIS_P_SERVERS	((uint8_t)  65)
#define	TAG_MOBILE_HOME		((uint8_t)  68)
#define	TAG_SMPT_SERVER		((uint8_t)  69)
#define	TAG_POP3_SERVER		((uint8_t)  70)
#define	TAG_NNTP_SERVER		((uint8_t)  71)
#define	TAG_WWW_SERVER		((uint8_t)  72)
#define	TAG_FINGER_SERVER	((uint8_t)  73)
#define	TAG_IRC_SERVER		((uint8_t)  74)
#define	TAG_STREETTALK_SRVR	((uint8_t)  75)
#define	TAG_STREETTALK_STDA	((uint8_t)  76)
/* DHCP options */
#define	TAG_REQUESTED_IP	((uint8_t)  50)
#define	TAG_IP_LEASE		((uint8_t)  51)
#define	TAG_OPT_OVERLOAD	((uint8_t)  52)
#define	TAG_TFTP_SERVER		((uint8_t)  66)
#define	TAG_BOOTFILENAME	((uint8_t)  67)
#define	TAG_DHCP_MESSAGE	((uint8_t)  53)
#define	TAG_SERVER_ID		((uint8_t)  54)
#define	TAG_PARM_REQUEST	((uint8_t)  55)
#define	TAG_MESSAGE		((uint8_t)  56)
#define	TAG_MAX_MSG_SIZE	((uint8_t)  57)
#define	TAG_RENEWAL_TIME	((uint8_t)  58)
#define	TAG_REBIND_TIME		((uint8_t)  59)
#define	TAG_VENDOR_CLASS	((uint8_t)  60)
#define	TAG_CLIENT_ID		((uint8_t)  61)
/* RFC 2241 */
#define	TAG_NDS_SERVERS		((uint8_t)  85)
#define	TAG_NDS_TREE_NAME	((uint8_t)  86)
#define	TAG_NDS_CONTEXT		((uint8_t)  87)
/* RFC 2242 */
#define	TAG_NDS_IPDOMAIN	((uint8_t)  62)
#define	TAG_NDS_IPINFO		((uint8_t)  63)
/* RFC 2485 */
#define	TAG_OPEN_GROUP_UAP	((uint8_t)  98)
/* RFC 2563 */
#define	TAG_DISABLE_AUTOCONF	((uint8_t) 116)
/* RFC 2610 */
#define	TAG_SLP_DA		((uint8_t)  78)
#define	TAG_SLP_SCOPE		((uint8_t)  79)
/* RFC 2937 */
#define	TAG_NS_SEARCH		((uint8_t) 117)
/* RFC 3004 - The User Class Option for DHCP */
#define	TAG_USER_CLASS		((uint8_t)  77)
/* RFC 3011 */
#define	TAG_IP4_SUBNET_SELECT	((uint8_t) 118)
/* RFC 3442 */
#define TAG_CLASSLESS_STATIC_RT	((uint8_t) 121)
#define TAG_CLASSLESS_STA_RT_MS	((uint8_t) 249)
/* RFC 5859 - TFTP Server Address Option for DHCPv4 */
#define	TAG_TFTP_SERVER_ADDRESS	((uint8_t) 150)
/* ftp://ftp.isi.edu/.../assignments/bootp-dhcp-extensions */
#define	TAG_SLP_NAMING_AUTH	((uint8_t)  80)
#define	TAG_CLIENT_FQDN		((uint8_t)  81)
#define	TAG_AGENT_CIRCUIT	((uint8_t)  82)
#define	TAG_AGENT_REMOTE	((uint8_t)  83)
#define	TAG_AGENT_MASK		((uint8_t)  84)
#define	TAG_TZ_STRING		((uint8_t)  88)
#define	TAG_FQDN_OPTION		((uint8_t)  89)
#define	TAG_AUTH		((uint8_t)  90)
#define	TAG_VINES_SERVERS	((uint8_t)  91)
#define	TAG_SERVER_RANK		((uint8_t)  92)
#define	TAG_CLIENT_ARCH		((uint8_t)  93)
#define	TAG_CLIENT_NDI		((uint8_t)  94)
#define	TAG_CLIENT_GUID		((uint8_t)  97)
#define	TAG_LDAP_URL		((uint8_t)  95)
#define	TAG_6OVER4		((uint8_t)  96)
/* RFC 4833, TZ codes */
#define	TAG_TZ_PCODE    	((uint8_t) 100)
#define	TAG_TZ_TCODE    	((uint8_t) 101)
#define	TAG_IPX_COMPAT		((uint8_t) 110)
#define	TAG_NETINFO_PARENT	((uint8_t) 112)
#define	TAG_NETINFO_PARENT_TAG	((uint8_t) 113)
#define	TAG_URL			((uint8_t) 114)
#define	TAG_FAILOVER		((uint8_t) 115)
#define	TAG_EXTENDED_REQUEST	((uint8_t) 126)
#define	TAG_EXTENDED_OPTION	((uint8_t) 127)
#define TAG_MUDURL              ((uint8_t) 161)

/* DHCP Message types (values for TAG_DHCP_MESSAGE option) */
#define DHCPDISCOVER	1
#define DHCPOFFER	2
#define DHCPREQUEST	3
#define DHCPDECLINE	4
#define DHCPACK		5
#define DHCPNAK		6
#define DHCPRELEASE	7
#define DHCPINFORM	8

/*
 * "vendor" data permitted for CMU bootp clients.
 */

struct cmu_vend {
	uint8_t		v_magic[4];	/* magic number */
	uint32_t	v_flags;	/* flags/opcodes, etc. */
	struct in_addr	v_smask;	/* Subnet mask */
	struct in_addr	v_dgate;	/* Default gateway */
	struct in_addr	v_dns1, v_dns2; /* Domain name servers */
	struct in_addr	v_ins1, v_ins2; /* IEN-116 name servers */
	struct in_addr	v_ts1, v_ts2;	/* Time servers */
	uint8_t		v_unused[24];	/* currently unused */
} UNALIGNED;


/* v_flags values */
#define VF_SMASK	1	/* Subnet mask field contains valid data */

/* RFC 4702 DHCP Client FQDN Option */

#define CLIENT_FQDN_FLAGS_S	0x01
#define CLIENT_FQDN_FLAGS_O	0x02
#define CLIENT_FQDN_FLAGS_E	0x04
#define CLIENT_FQDN_FLAGS_N	0x08
/* end of original bootp.h */

static void rfc1048_print(netdissect_options *, const u_char *);
static void cmu_print(netdissect_options *, const u_char *);
static char *client_fqdn_flags(u_int flags);

static const struct tok bootp_flag_values[] = {
	{ 0x8000,	"Broadcast" },
	{ 0, NULL}
};

static const struct tok bootp_op_values[] = {
	{ BOOTPREQUEST,	"Request" },
	{ BOOTPREPLY,	"Reply" },
	{ 0, NULL}
};

/*
 * Print bootp requests
 */
void
bootp_print(netdissect_options *ndo,
	    register const u_char *cp, u_int length)
{
	register const struct bootp *bp;
	static const u_char vm_cmu[4] = VM_CMU;
	static const u_char vm_rfc1048[4] = VM_RFC1048;

	bp = (const struct bootp *)cp;
	ND_TCHECK(bp->bp_op);

	ND_PRINT((ndo, "BOOTP/DHCP, %s",
		  tok2str(bootp_op_values, "unknown (0x%02x)", bp->bp_op)));

	ND_TCHECK(bp->bp_hlen);
	if (bp->bp_htype == 1 && bp->bp_hlen == 6 && bp->bp_op == BOOTPREQUEST) {
		ND_TCHECK2(bp->bp_chaddr[0], 6);
		ND_PRINT((ndo, " from %s", etheraddr_string(ndo, bp->bp_chaddr)));
	}

	ND_PRINT((ndo, ", length %u", length));

	if (!ndo->ndo_vflag)
		return;

	ND_TCHECK(bp->bp_secs);

	/* The usual hardware address type is 1 (10Mb Ethernet) */
	if (bp->bp_htype != 1)
		ND_PRINT((ndo, ", htype %d", bp->bp_htype));

	/* The usual length for 10Mb Ethernet address is 6 bytes */
	if (bp->bp_htype != 1 || bp->bp_hlen != 6)
		ND_PRINT((ndo, ", hlen %d", bp->bp_hlen));

	/* Only print interesting fields */
	if (bp->bp_hops)
		ND_PRINT((ndo, ", hops %d", bp->bp_hops));
	if (EXTRACT_32BITS(&bp->bp_xid))
		ND_PRINT((ndo, ", xid 0x%x", EXTRACT_32BITS(&bp->bp_xid)));
	if (EXTRACT_16BITS(&bp->bp_secs))
		ND_PRINT((ndo, ", secs %d", EXTRACT_16BITS(&bp->bp_secs)));

	ND_TCHECK(bp->bp_flags);
	ND_PRINT((ndo, ", Flags [%s]",
		  bittok2str(bootp_flag_values, "none", EXTRACT_16BITS(&bp->bp_flags))));
	if (ndo->ndo_vflag > 1)
		ND_PRINT((ndo, " (0x%04x)", EXTRACT_16BITS(&bp->bp_flags)));

	/* Client's ip address */
	ND_TCHECK(bp->bp_ciaddr);
	if (EXTRACT_32BITS(&bp->bp_ciaddr.s_addr))
		ND_PRINT((ndo, "\n\t  Client-IP %s", ipaddr_string(ndo, &bp->bp_ciaddr)));

	/* 'your' ip address (bootp client) */
	ND_TCHECK(bp->bp_yiaddr);
	if (EXTRACT_32BITS(&bp->bp_yiaddr.s_addr))
		ND_PRINT((ndo, "\n\t  Your-IP %s", ipaddr_string(ndo, &bp->bp_yiaddr)));

	/* Server's ip address */
	ND_TCHECK(bp->bp_siaddr);
	if (EXTRACT_32BITS(&bp->bp_siaddr.s_addr))
		ND_PRINT((ndo, "\n\t  Server-IP %s", ipaddr_string(ndo, &bp->bp_siaddr)));

	/* Gateway's ip address */
	ND_TCHECK(bp->bp_giaddr);
	if (EXTRACT_32BITS(&bp->bp_giaddr.s_addr))
		ND_PRINT((ndo, "\n\t  Gateway-IP %s", ipaddr_string(ndo, &bp->bp_giaddr)));

	/* Client's Ethernet address */
	if (bp->bp_htype == 1 && bp->bp_hlen == 6) {
		ND_TCHECK2(bp->bp_chaddr[0], 6);
		ND_PRINT((ndo, "\n\t  Client-Ethernet-Address %s", etheraddr_string(ndo, bp->bp_chaddr)));
	}

	ND_TCHECK2(bp->bp_sname[0], 1);		/* check first char only */
	if (*bp->bp_sname) {
		ND_PRINT((ndo, "\n\t  sname \""));
		if (fn_printztn(ndo, bp->bp_sname, (u_int)sizeof bp->bp_sname,
		    ndo->ndo_snapend)) {
			ND_PRINT((ndo, "\""));
			ND_PRINT((ndo, "%s", tstr + 1));
			return;
		}
		ND_PRINT((ndo, "\""));
	}
	ND_TCHECK2(bp->bp_file[0], 1);		/* check first char only */
	if (*bp->bp_file) {
		ND_PRINT((ndo, "\n\t  file \""));
		if (fn_printztn(ndo, bp->bp_file, (u_int)sizeof bp->bp_file,
		    ndo->ndo_snapend)) {
			ND_PRINT((ndo, "\""));
			ND_PRINT((ndo, "%s", tstr + 1));
			return;
		}
		ND_PRINT((ndo, "\""));
	}

	/* Decode the vendor buffer */
	ND_TCHECK(bp->bp_vend[0]);
	if (memcmp((const char *)bp->bp_vend, vm_rfc1048,
		    sizeof(uint32_t)) == 0)
		rfc1048_print(ndo, bp->bp_vend);
	else if (memcmp((const char *)bp->bp_vend, vm_cmu,
			sizeof(uint32_t)) == 0)
		cmu_print(ndo, bp->bp_vend);
	else {
		uint32_t ul;

		ul = EXTRACT_32BITS(&bp->bp_vend);
		if (ul != 0)
			ND_PRINT((ndo, "\n\t  Vendor-#0x%x", ul));
	}

	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

/*
 * The first character specifies the format to print:
 *     i - ip address (32 bits)
 *     p - ip address pairs (32 bits + 32 bits)
 *     l - long (32 bits)
 *     L - unsigned long (32 bits)
 *     s - short (16 bits)
 *     b - period-seperated decimal bytes (variable length)
 *     x - colon-seperated hex bytes (variable length)
 *     a - ascii string (variable length)
 *     B - on/off (8 bits)
 *     $ - special (explicit code to handle)
 */
static const struct tok tag2str[] = {
/* RFC1048 tags */
	{ TAG_PAD,		" PAD" },
	{ TAG_SUBNET_MASK,	"iSubnet-Mask" },	/* subnet mask (RFC950) */
	{ TAG_TIME_OFFSET,	"LTime-Zone" },	/* seconds from UTC */
	{ TAG_GATEWAY,		"iDefault-Gateway" },	/* default gateway */
	{ TAG_TIME_SERVER,	"iTime-Server" },	/* time servers (RFC868) */
	{ TAG_NAME_SERVER,	"iIEN-Name-Server" },	/* IEN name servers (IEN116) */
	{ TAG_DOMAIN_SERVER,	"iDomain-Name-Server" },	/* domain name (RFC1035) */
	{ TAG_LOG_SERVER,	"iLOG" },	/* MIT log servers */
	{ TAG_COOKIE_SERVER,	"iCS" },	/* cookie servers (RFC865) */
	{ TAG_LPR_SERVER,	"iLPR-Server" },	/* lpr server (RFC1179) */
	{ TAG_IMPRESS_SERVER,	"iIM" },	/* impress servers (Imagen) */
	{ TAG_RLP_SERVER,	"iRL" },	/* resource location (RFC887) */
	{ TAG_HOSTNAME,		"aHostname" },	/* ascii hostname */
	{ TAG_BOOTSIZE,		"sBS" },	/* 512 byte blocks */
	{ TAG_END,		" END" },
/* RFC1497 tags */
	{ TAG_DUMPPATH,		"aDP" },
	{ TAG_DOMAINNAME,	"aDomain-Name" },
	{ TAG_SWAP_SERVER,	"iSS" },
	{ TAG_ROOTPATH,		"aRP" },
	{ TAG_EXTPATH,		"aEP" },
/* RFC2132 tags */
	{ TAG_IP_FORWARD,	"BIPF" },
	{ TAG_NL_SRCRT,		"BSRT" },
	{ TAG_PFILTERS,		"pPF" },
	{ TAG_REASS_SIZE,	"sRSZ" },
	{ TAG_DEF_TTL,		"bTTL" },
	{ TAG_MTU_TIMEOUT,	"lMTU-Timeout" },
	{ TAG_MTU_TABLE,	"sMTU-Table" },
	{ TAG_INT_MTU,		"sMTU" },
	{ TAG_LOCAL_SUBNETS,	"BLSN" },
	{ TAG_BROAD_ADDR,	"iBR" },
	{ TAG_DO_MASK_DISC,	"BMD" },
	{ TAG_SUPPLY_MASK,	"BMS" },
	{ TAG_DO_RDISC,		"BRouter-Discovery" },
	{ TAG_RTR_SOL_ADDR,	"iRSA" },
	{ TAG_STATIC_ROUTE,	"pStatic-Route" },
	{ TAG_USE_TRAILERS,	"BUT" },
	{ TAG_ARP_TIMEOUT,	"lAT" },
	{ TAG_ETH_ENCAP,	"BIE" },
	{ TAG_TCP_TTL,		"bTT" },
	{ TAG_TCP_KEEPALIVE,	"lKI" },
	{ TAG_KEEPALIVE_GO,	"BKG" },
	{ TAG_NIS_DOMAIN,	"aYD" },
	{ TAG_NIS_SERVERS,	"iYS" },
	{ TAG_NTP_SERVERS,	"iNTP" },
	{ TAG_VENDOR_OPTS,	"bVendor-Option" },
	{ TAG_NETBIOS_NS,	"iNetbios-Name-Server" },
	{ TAG_NETBIOS_DDS,	"iWDD" },
	{ TAG_NETBIOS_NODE,	"$Netbios-Node" },
	{ TAG_NETBIOS_SCOPE,	"aNetbios-Scope" },
	{ TAG_XWIN_FS,		"iXFS" },
	{ TAG_XWIN_DM,		"iXDM" },
	{ TAG_NIS_P_DOMAIN,	"sN+D" },
	{ TAG_NIS_P_SERVERS,	"iN+S" },
	{ TAG_MOBILE_HOME,	"iMH" },
	{ TAG_SMPT_SERVER,	"iSMTP" },
	{ TAG_POP3_SERVER,	"iPOP3" },
	{ TAG_NNTP_SERVER,	"iNNTP" },
	{ TAG_WWW_SERVER,	"iWWW" },
	{ TAG_FINGER_SERVER,	"iFG" },
	{ TAG_IRC_SERVER,	"iIRC" },
	{ TAG_STREETTALK_SRVR,	"iSTS" },
	{ TAG_STREETTALK_STDA,	"iSTDA" },
	{ TAG_REQUESTED_IP,	"iRequested-IP" },
	{ TAG_IP_LEASE,		"lLease-Time" },
	{ TAG_OPT_OVERLOAD,	"$OO" },
	{ TAG_TFTP_SERVER,	"aTFTP" },
	{ TAG_BOOTFILENAME,	"aBF" },
	{ TAG_DHCP_MESSAGE,	" DHCP-Message" },
	{ TAG_SERVER_ID,	"iServer-ID" },
	{ TAG_PARM_REQUEST,	"bParameter-Request" },
	{ TAG_MESSAGE,		"aMSG" },
	{ TAG_MAX_MSG_SIZE,	"sMSZ" },
	{ TAG_RENEWAL_TIME,	"lRN" },
	{ TAG_REBIND_TIME,	"lRB" },
	{ TAG_VENDOR_CLASS,	"aVendor-Class" },
	{ TAG_CLIENT_ID,	"$Client-ID" },
/* RFC 2485 */
	{ TAG_OPEN_GROUP_UAP,	"aUAP" },
/* RFC 2563 */
	{ TAG_DISABLE_AUTOCONF,	"BNOAUTO" },
/* RFC 2610 */
	{ TAG_SLP_DA,		"bSLP-DA" },	/*"b" is a little wrong */
	{ TAG_SLP_SCOPE,	"bSLP-SCOPE" },	/*"b" is a little wrong */
/* RFC 2937 */
	{ TAG_NS_SEARCH,	"sNSSEARCH" },	/* XXX 's' */
/* RFC 3004 - The User Class Option for DHCP */
	{ TAG_USER_CLASS,	"$User-Class" },
/* RFC 3011 */
	{ TAG_IP4_SUBNET_SELECT, "iSUBNET" },
/* RFC 3442 */
	{ TAG_CLASSLESS_STATIC_RT, "$Classless-Static-Route" },
	{ TAG_CLASSLESS_STA_RT_MS, "$Classless-Static-Route-Microsoft" },
/* RFC 5859 - TFTP Server Address Option for DHCPv4 */
	{ TAG_TFTP_SERVER_ADDRESS, "iTFTP-Server-Address" },
/* http://www.iana.org/assignments/bootp-dhcp-extensions/index.htm */
	{ TAG_SLP_NAMING_AUTH,	"aSLP-NA" },
	{ TAG_CLIENT_FQDN,	"$FQDN" },
	{ TAG_AGENT_CIRCUIT,	"$Agent-Information" },
	{ TAG_AGENT_REMOTE,	"bARMT" },
	{ TAG_AGENT_MASK,	"bAMSK" },
	{ TAG_TZ_STRING,	"aTZSTR" },
	{ TAG_FQDN_OPTION,	"bFQDNS" },	/* XXX 'b' */
	{ TAG_AUTH,		"bAUTH" },	/* XXX 'b' */
	{ TAG_VINES_SERVERS,	"iVINES" },
	{ TAG_SERVER_RANK,	"sRANK" },
	{ TAG_CLIENT_ARCH,	"sARCH" },
	{ TAG_CLIENT_NDI,	"bNDI" },	/* XXX 'b' */
	{ TAG_CLIENT_GUID,	"bGUID" },	/* XXX 'b' */
	{ TAG_LDAP_URL,		"aLDAP" },
	{ TAG_6OVER4,		"i6o4" },
	{ TAG_TZ_PCODE, 	"aPOSIX-TZ" },
	{ TAG_TZ_TCODE, 	"aTZ-Name" },
	{ TAG_IPX_COMPAT,	"bIPX" },	/* XXX 'b' */
	{ TAG_NETINFO_PARENT,	"iNI" },
	{ TAG_NETINFO_PARENT_TAG, "aNITAG" },
	{ TAG_URL,		"aURL" },
	{ TAG_FAILOVER,		"bFAIL" },	/* XXX 'b' */
	{ TAG_MUDURL,           "aMUD-URL" },
	{ 0, NULL }
};
/* 2-byte extended tags */
static const struct tok xtag2str[] = {
	{ 0, NULL }
};

/* DHCP "options overload" types */
static const struct tok oo2str[] = {
	{ 1,	"file" },
	{ 2,	"sname" },
	{ 3,	"file+sname" },
	{ 0, NULL }
};

/* NETBIOS over TCP/IP node type options */
static const struct tok nbo2str[] = {
	{ 0x1,	"b-node" },
	{ 0x2,	"p-node" },
	{ 0x4,	"m-node" },
	{ 0x8,	"h-node" },
	{ 0, NULL }
};

/* ARP Hardware types, for Client-ID option */
static const struct tok arp2str[] = {
	{ 0x1,	"ether" },
	{ 0x6,	"ieee802" },
	{ 0x7,	"arcnet" },
	{ 0xf,	"frelay" },
	{ 0x17,	"strip" },
	{ 0x18,	"ieee1394" },
	{ 0, NULL }
};

static const struct tok dhcp_msg_values[] = {
	{ DHCPDISCOVER,	"Discover" },
	{ DHCPOFFER,	"Offer" },
	{ DHCPREQUEST,	"Request" },
	{ DHCPDECLINE,	"Decline" },
	{ DHCPACK,	"ACK" },
	{ DHCPNAK,	"NACK" },
	{ DHCPRELEASE,	"Release" },
	{ DHCPINFORM,	"Inform" },
	{ 0, NULL }
};

#define AGENT_SUBOPTION_CIRCUIT_ID	1	/* RFC 3046 */
#define AGENT_SUBOPTION_REMOTE_ID	2	/* RFC 3046 */
#define AGENT_SUBOPTION_SUBSCRIBER_ID	6	/* RFC 3993 */
static const struct tok agent_suboption_values[] = {
	{ AGENT_SUBOPTION_CIRCUIT_ID,    "Circuit-ID" },
	{ AGENT_SUBOPTION_REMOTE_ID,     "Remote-ID" },
	{ AGENT_SUBOPTION_SUBSCRIBER_ID, "Subscriber-ID" },
	{ 0, NULL }
};


static void
rfc1048_print(netdissect_options *ndo,
	      register const u_char *bp)
{
	register uint16_t tag;
	register u_int len;
	register const char *cp;
	register char c;
	int first, idx;
	uint32_t ul;
	uint16_t us;
	uint8_t uc, subopt, suboptlen;

	ND_PRINT((ndo, "\n\t  Vendor-rfc1048 Extensions"));

	/* Step over magic cookie */
	ND_PRINT((ndo, "\n\t    Magic Cookie 0x%08x", EXTRACT_32BITS(bp)));
	bp += sizeof(int32_t);

	/* Loop while we there is a tag left in the buffer */
	while (ND_TTEST2(*bp, 1)) {
		tag = *bp++;
		if (tag == TAG_PAD && ndo->ndo_vflag < 3)
			continue;
		if (tag == TAG_END && ndo->ndo_vflag < 3)
			return;
		if (tag == TAG_EXTENDED_OPTION) {
			ND_TCHECK2(*(bp + 1), 2);
			tag = EXTRACT_16BITS(bp + 1);
			/* XXX we don't know yet if the IANA will
			 * preclude overlap of 1-byte and 2-byte spaces.
			 * If not, we need to offset tag after this step.
			 */
			cp = tok2str(xtag2str, "?xT%u", tag);
		} else
			cp = tok2str(tag2str, "?T%u", tag);
		c = *cp++;

		if (tag == TAG_PAD || tag == TAG_END)
			len = 0;
		else {
			/* Get the length; check for truncation */
			ND_TCHECK2(*bp, 1);
			len = *bp++;
		}

		ND_PRINT((ndo, "\n\t    %s Option %u, length %u%s", cp, tag, len,
			  len > 0 ? ": " : ""));

		if (tag == TAG_PAD && ndo->ndo_vflag > 2) {
			u_int ntag = 1;
			while (ND_TTEST2(*bp, 1) && *bp == TAG_PAD) {
				bp++;
				ntag++;
			}
			if (ntag > 1)
				ND_PRINT((ndo, ", occurs %u", ntag));
		}

		if (!ND_TTEST2(*bp, len)) {
			ND_PRINT((ndo, "[|rfc1048 %u]", len));
			return;
		}

		if (tag == TAG_DHCP_MESSAGE && len == 1) {
			uc = *bp++;
			ND_PRINT((ndo, "%s", tok2str(dhcp_msg_values, "Unknown (%u)", uc)));
			continue;
		}

		if (tag == TAG_PARM_REQUEST) {
			idx = 0;
			while (len-- > 0) {
				uc = *bp++;
				cp = tok2str(tag2str, "?Option %u", uc);
				if (idx % 4 == 0)
					ND_PRINT((ndo, "\n\t      "));
				else
					ND_PRINT((ndo, ", "));
				ND_PRINT((ndo, "%s", cp + 1));
				idx++;
			}
			continue;
		}

		if (tag == TAG_EXTENDED_REQUEST) {
			first = 1;
			while (len > 1) {
				len -= 2;
				us = EXTRACT_16BITS(bp);
				bp += 2;
				cp = tok2str(xtag2str, "?xT%u", us);
				if (!first)
					ND_PRINT((ndo, "+"));
				ND_PRINT((ndo, "%s", cp + 1));
				first = 0;
			}
			continue;
		}

		/* Print data */
		if (c == '?') {
			/* Base default formats for unknown tags on data size */
			if (len & 1)
				c = 'b';
			else if (len & 2)
				c = 's';
			else
				c = 'l';
		}
		first = 1;
		switch (c) {

		case 'a':
			/* ascii strings */
			ND_PRINT((ndo, "\""));
			if (fn_printn(ndo, bp, len, ndo->ndo_snapend)) {
				ND_PRINT((ndo, "\""));
				goto trunc;
			}
			ND_PRINT((ndo, "\""));
			bp += len;
			len = 0;
			break;

		case 'i':
		case 'l':
		case 'L':
			/* ip addresses/32-bit words */
			while (len >= sizeof(ul)) {
				if (!first)
					ND_PRINT((ndo, ","));
				ul = EXTRACT_32BITS(bp);
				if (c == 'i') {
					ul = htonl(ul);
					ND_PRINT((ndo, "%s", ipaddr_string(ndo, &ul)));
				} else if (c == 'L')
					ND_PRINT((ndo, "%d", ul));
				else
					ND_PRINT((ndo, "%u", ul));
				bp += sizeof(ul);
				len -= sizeof(ul);
				first = 0;
			}
			break;

		case 'p':
			/* IP address pairs */
			while (len >= 2*sizeof(ul)) {
				if (!first)
					ND_PRINT((ndo, ","));
				memcpy((char *)&ul, (const char *)bp, sizeof(ul));
				ND_PRINT((ndo, "(%s:", ipaddr_string(ndo, &ul)));
				bp += sizeof(ul);
				memcpy((char *)&ul, (const char *)bp, sizeof(ul));
				ND_PRINT((ndo, "%s)", ipaddr_string(ndo, &ul)));
				bp += sizeof(ul);
				len -= 2*sizeof(ul);
				first = 0;
			}
			break;

		case 's':
			/* shorts */
			while (len >= sizeof(us)) {
				if (!first)
					ND_PRINT((ndo, ","));
				us = EXTRACT_16BITS(bp);
				ND_PRINT((ndo, "%u", us));
				bp += sizeof(us);
				len -= sizeof(us);
				first = 0;
			}
			break;

		case 'B':
			/* boolean */
			while (len > 0) {
				if (!first)
					ND_PRINT((ndo, ","));
				switch (*bp) {
				case 0:
					ND_PRINT((ndo, "N"));
					break;
				case 1:
					ND_PRINT((ndo, "Y"));
					break;
				default:
					ND_PRINT((ndo, "%u?", *bp));
					break;
				}
				++bp;
				--len;
				first = 0;
			}
			break;

		case 'b':
		case 'x':
		default:
			/* Bytes */
			while (len > 0) {
				if (!first)
					ND_PRINT((ndo, c == 'x' ? ":" : "."));
				if (c == 'x')
					ND_PRINT((ndo, "%02x", *bp));
				else
					ND_PRINT((ndo, "%u", *bp));
				++bp;
				--len;
				first = 0;
			}
			break;

		case '$':
			/* Guys we can't handle with one of the usual cases */
			switch (tag) {

			case TAG_NETBIOS_NODE:
				/* this option should be at least 1 byte long */
				if (len < 1) {
					ND_PRINT((ndo, "ERROR: length < 1 bytes"));
					break;
				}
				tag = *bp++;
				--len;
				ND_PRINT((ndo, "%s", tok2str(nbo2str, NULL, tag)));
				break;

			case TAG_OPT_OVERLOAD:
				/* this option should be at least 1 byte long */
				if (len < 1) {
					ND_PRINT((ndo, "ERROR: length < 1 bytes"));
					break;
				}
				tag = *bp++;
				--len;
				ND_PRINT((ndo, "%s", tok2str(oo2str, NULL, tag)));
				break;

			case TAG_CLIENT_FQDN:
				/* this option should be at least 3 bytes long */
				if (len < 3) {
					ND_PRINT((ndo, "ERROR: length < 3 bytes"));
					bp += len;
					len = 0;
					break;
				}
				if (*bp)
					ND_PRINT((ndo, "[%s] ", client_fqdn_flags(*bp)));
				bp++;
				if (*bp || *(bp+1))
					ND_PRINT((ndo, "%u/%u ", *bp, *(bp+1)));
				bp += 2;
				ND_PRINT((ndo, "\""));
				if (fn_printn(ndo, bp, len - 3, ndo->ndo_snapend)) {
					ND_PRINT((ndo, "\""));
					goto trunc;
				}
				ND_PRINT((ndo, "\""));
				bp += len - 3;
				len = 0;
				break;

			case TAG_CLIENT_ID:
			    {
				int type;

				/* this option should be at least 1 byte long */
				if (len < 1) {
					ND_PRINT((ndo, "ERROR: length < 1 bytes"));
					break;
				}
				type = *bp++;
				len--;
				if (type == 0) {
					ND_PRINT((ndo, "\""));
					if (fn_printn(ndo, bp, len, ndo->ndo_snapend)) {
						ND_PRINT((ndo, "\""));
						goto trunc;
					}
					ND_PRINT((ndo, "\""));
					bp += len;
					len = 0;
					break;
				} else {
					ND_PRINT((ndo, "%s ", tok2str(arp2str, "hardware-type %u,", type)));
					while (len > 0) {
						if (!first)
							ND_PRINT((ndo, ":"));
						ND_PRINT((ndo, "%02x", *bp));
						++bp;
						--len;
						first = 0;
					}
				}
				break;
			    }

			case TAG_AGENT_CIRCUIT:
				while (len >= 2) {
					subopt = *bp++;
					suboptlen = *bp++;
					len -= 2;
					if (suboptlen > len) {
						ND_PRINT((ndo, "\n\t      %s SubOption %u, length %u: length goes past end of option",
							  tok2str(agent_suboption_values, "Unknown", subopt),
							  subopt,
							  suboptlen));
						bp += len;
						len = 0;
						break;
					}
					ND_PRINT((ndo, "\n\t      %s SubOption %u, length %u: ",
						  tok2str(agent_suboption_values, "Unknown", subopt),
						  subopt,
						  suboptlen));
					switch (subopt) {

					case AGENT_SUBOPTION_CIRCUIT_ID: /* fall through */
					case AGENT_SUBOPTION_REMOTE_ID:
					case AGENT_SUBOPTION_SUBSCRIBER_ID:
						if (fn_printn(ndo, bp, suboptlen, ndo->ndo_snapend))
							goto trunc;
						break;

					default:
						print_unknown_data(ndo, bp, "\n\t\t", suboptlen);
					}

					len -= suboptlen;
					bp += suboptlen;
				}
				break;

			case TAG_CLASSLESS_STATIC_RT:
			case TAG_CLASSLESS_STA_RT_MS:
			    {
				u_int mask_width, significant_octets, i;

				/* this option should be at least 5 bytes long */
				if (len < 5) {
					ND_PRINT((ndo, "ERROR: length < 5 bytes"));
					bp += len;
					len = 0;
					break;
				}
				while (len > 0) {
					if (!first)
						ND_PRINT((ndo, ","));
					mask_width = *bp++;
					len--;
					/* mask_width <= 32 */
					if (mask_width > 32) {
						ND_PRINT((ndo, "[ERROR: Mask width (%d) > 32]", mask_width));
						bp += len;
						len = 0;
						break;
					}
					significant_octets = (mask_width + 7) / 8;
					/* significant octets + router(4) */
					if (len < significant_octets + 4) {
						ND_PRINT((ndo, "[ERROR: Remaining length (%u) < %u bytes]", len, significant_octets + 4));
						bp += len;
						len = 0;
						break;
					}
					ND_PRINT((ndo, "("));
					if (mask_width == 0)
						ND_PRINT((ndo, "default"));
					else {
						for (i = 0; i < significant_octets ; i++) {
							if (i > 0)
								ND_PRINT((ndo, "."));
							ND_PRINT((ndo, "%d", *bp++));
						}
						for (i = significant_octets ; i < 4 ; i++)
							ND_PRINT((ndo, ".0"));
						ND_PRINT((ndo, "/%d", mask_width));
					}
					memcpy((char *)&ul, (const char *)bp, sizeof(ul));
					ND_PRINT((ndo, ":%s)", ipaddr_string(ndo, &ul)));
					bp += sizeof(ul);
					len -= (significant_octets + 4);
					first = 0;
				}
				break;
			    }

			case TAG_USER_CLASS:
			    {
				u_int suboptnumber = 1;

				first = 1;
				if (len < 2) {
					ND_PRINT((ndo, "ERROR: length < 2 bytes"));
					bp += len;
					len = 0;
					break;
				}
				while (len > 0) {
					suboptlen = *bp++;
					len--;
					ND_PRINT((ndo, "\n\t      "));
					ND_PRINT((ndo, "instance#%u: ", suboptnumber));
					if (suboptlen == 0) {
						ND_PRINT((ndo, "ERROR: suboption length must be non-zero"));
						bp += len;
						len = 0;
						break;
					}
					if (len < suboptlen) {
						ND_PRINT((ndo, "ERROR: invalid option"));
						bp += len;
						len = 0;
						break;
					}
					ND_PRINT((ndo, "\""));
					if (fn_printn(ndo, bp, suboptlen, ndo->ndo_snapend)) {
						ND_PRINT((ndo, "\""));
						goto trunc;
					}
					ND_PRINT((ndo, "\""));
					ND_PRINT((ndo, ", length %d", suboptlen));
					suboptnumber++;
					len -= suboptlen;
					bp += suboptlen;
				}
				break;
			    }

			default:
				ND_PRINT((ndo, "[unknown special tag %u, size %u]",
					  tag, len));
				bp += len;
				len = 0;
				break;
			}
			break;
		}
		/* Data left over? */
		if (len) {
			ND_PRINT((ndo, "\n\t  trailing data length %u", len));
			bp += len;
		}
	}
	return;
trunc:
	ND_PRINT((ndo, "|[rfc1048]"));
}

static void
cmu_print(netdissect_options *ndo,
	  register const u_char *bp)
{
	register const struct cmu_vend *cmu;

#define PRINTCMUADDR(m, s) { ND_TCHECK(cmu->m); \
    if (cmu->m.s_addr != 0) \
	ND_PRINT((ndo, " %s:%s", s, ipaddr_string(ndo, &cmu->m.s_addr))); }

	ND_PRINT((ndo, " vend-cmu"));
	cmu = (const struct cmu_vend *)bp;

	/* Only print if there are unknown bits */
	ND_TCHECK(cmu->v_flags);
	if ((cmu->v_flags & ~(VF_SMASK)) != 0)
		ND_PRINT((ndo, " F:0x%x", cmu->v_flags));
	PRINTCMUADDR(v_dgate, "DG");
	PRINTCMUADDR(v_smask, cmu->v_flags & VF_SMASK ? "SM" : "SM*");
	PRINTCMUADDR(v_dns1, "NS1");
	PRINTCMUADDR(v_dns2, "NS2");
	PRINTCMUADDR(v_ins1, "IEN1");
	PRINTCMUADDR(v_ins2, "IEN2");
	PRINTCMUADDR(v_ts1, "TS1");
	PRINTCMUADDR(v_ts2, "TS2");
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
#undef PRINTCMUADDR
}

static char *
client_fqdn_flags(u_int flags)
{
	static char buf[8+1];
	int i = 0;

	if (flags & CLIENT_FQDN_FLAGS_S)
		buf[i++] = 'S';
	if (flags & CLIENT_FQDN_FLAGS_O)
		buf[i++] = 'O';
	if (flags & CLIENT_FQDN_FLAGS_E)
		buf[i++] = 'E';
	if (flags & CLIENT_FQDN_FLAGS_N)
		buf[i++] = 'N';
	buf[i] = '\0';

	return buf;
}
