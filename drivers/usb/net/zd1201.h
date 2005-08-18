/*
 *	Copyright (c) 2004, 2005 Jeroen Vreeken (pe1rxq@amsat.org)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	version 2 as published by the Free Software Foundation.
 *
 *	Parts of this driver have been derived from a wlan-ng version
 *	modified by ZyDAS.
 *	Copyright (C) 1999 AbsoluteValue Systems, Inc.  All Rights Reserved.
 */

#ifndef _INCLUDE_ZD1201_H_
#define _INCLUDE_ZD1201_H_

#define ZD1201_NUMKEYS		4
#define ZD1201_MAXKEYLEN	13
#define ZD1201_MAXMULTI		16
#define ZD1201_FRAGMAX		2500
#define ZD1201_FRAGMIN		256
#define ZD1201_RTSMAX		2500

#define ZD1201_RXSIZE		3000

struct zd1201 {
	struct usb_device	*usb;
	int			removed;
	struct net_device	*dev;
	struct net_device_stats stats;
	struct iw_statistics	iwstats;

	int			endp_in;
	int			endp_out;
	int			endp_out2;
	struct urb		*rx_urb;
	struct urb		*tx_urb;

	unsigned char 		rxdata[ZD1201_RXSIZE];
	int			rxlen;
	wait_queue_head_t	rxdataq;
	int			rxdatas;
	struct hlist_head	fraglist;
	unsigned char		txdata[ZD1201_RXSIZE];

	int			ap;
	char			essid[IW_ESSID_MAX_SIZE+1];
	int			essidlen;
	int			mac_enabled;
	int			was_enabled;
	int			monitor;
	int			encode_enabled;
	int			encode_restricted;
	unsigned char		encode_keys[ZD1201_NUMKEYS][ZD1201_MAXKEYLEN];
	int			encode_keylen[ZD1201_NUMKEYS];
};

struct zd1201_frag {
	struct hlist_node	fnode;
	int			seq;
	struct sk_buff		*skb;
};

#define ZD1201SIWHOSTAUTH SIOCIWFIRSTPRIV
#define ZD1201GIWHOSTAUTH ZD1201SIWHOSTAUTH+1
#define ZD1201SIWAUTHSTA SIOCIWFIRSTPRIV+2
#define ZD1201SIWMAXASSOC SIOCIWFIRSTPRIV+4
#define ZD1201GIWMAXASSOC ZD1201SIWMAXASSOC+1

#define ZD1201_FW_TIMEOUT	(1000)

#define ZD1201_TX_TIMEOUT	(2000)

#define ZD1201_USB_CMDREQ	0
#define ZD1201_USB_RESREQ	1

#define	ZD1201_CMDCODE_INIT	0x00
#define ZD1201_CMDCODE_ENABLE	0x01
#define ZD1201_CMDCODE_DISABLE	0x02
#define ZD1201_CMDCODE_ALLOC	0x0a
#define ZD1201_CMDCODE_INQUIRE	0x11
#define ZD1201_CMDCODE_SETRXRID	0x17
#define ZD1201_CMDCODE_ACCESS	0x21

#define ZD1201_PACKET_EVENTSTAT	0x0
#define ZD1201_PACKET_RXDATA	0x1
#define ZD1201_PACKET_INQUIRE	0x2
#define ZD1201_PACKET_RESOURCE	0x3

#define ZD1201_ACCESSBIT	0x0100

#define ZD1201_RID_CNFPORTTYPE		0xfc00
#define ZD1201_RID_CNFOWNMACADDR	0xfc01
#define ZD1201_RID_CNFDESIREDSSID	0xfc02
#define ZD1201_RID_CNFOWNCHANNEL	0xfc03
#define ZD1201_RID_CNFOWNSSID		0xfc04
#define ZD1201_RID_CNFMAXDATALEN	0xfc07
#define ZD1201_RID_CNFPMENABLED		0xfc09
#define ZD1201_RID_CNFPMEPS		0xfc0a
#define ZD1201_RID_CNFMAXSLEEPDURATION	0xfc0c
#define ZD1201_RID_CNFDEFAULTKEYID	0xfc23
#define ZD1201_RID_CNFDEFAULTKEY0	0xfc24
#define ZD1201_RID_CNFDEFAULTKEY1	0xfc25
#define ZD1201_RID_CNFDEFAULTKEY2	0xfc26
#define ZD1201_RID_CNFDEFAULTKEY3	0xfc27
#define ZD1201_RID_CNFWEBFLAGS		0xfc28
#define ZD1201_RID_CNFAUTHENTICATION	0xfc2a
#define ZD1201_RID_CNFMAXASSOCSTATIONS	0xfc2b
#define ZD1201_RID_CNFHOSTAUTH		0xfc2e
#define ZD1201_RID_CNFGROUPADDRESS	0xfc80
#define ZD1201_RID_CNFFRAGTHRESHOLD	0xfc82
#define ZD1201_RID_CNFRTSTHRESHOLD	0xfc83
#define ZD1201_RID_TXRATECNTL		0xfc84
#define ZD1201_RID_PROMISCUOUSMODE	0xfc85
#define ZD1201_RID_CNFBASICRATES	0xfcb3
#define ZD1201_RID_AUTHENTICATESTA	0xfce3
#define ZD1201_RID_CURRENTBSSID		0xfd42
#define ZD1201_RID_COMMSQUALITY		0xfd43
#define ZD1201_RID_CURRENTTXRATE	0xfd44
#define ZD1201_RID_CNFMAXTXBUFFERNUMBER	0xfda0
#define ZD1201_RID_CURRENTCHANNEL	0xfdc1

#define ZD1201_INQ_SCANRESULTS		0xf101

#define ZD1201_INF_LINKSTATUS		0xf200
#define ZD1201_INF_ASSOCSTATUS		0xf201
#define ZD1201_INF_AUTHREQ		0xf202

#define ZD1201_ASSOCSTATUS_STAASSOC	0x1
#define ZD1201_ASSOCSTATUS_REASSOC	0x2
#define ZD1201_ASSOCSTATUS_DISASSOC	0x3
#define ZD1201_ASSOCSTATUS_ASSOCFAIL	0x4
#define ZD1201_ASSOCSTATUS_AUTHFAIL	0x5

#define ZD1201_PORTTYPE_IBSS		0
#define ZD1201_PORTTYPE_BSS		1
#define ZD1201_PORTTYPE_WDS		2
#define ZD1201_PORTTYPE_PSEUDOIBSS	3
#define ZD1201_PORTTYPE_AP		6

#define ZD1201_RATEB1	1
#define ZD1201_RATEB2	2
#define ZD1201_RATEB5	4	/* 5.5 really, but 5 is shorter :) */
#define ZD1201_RATEB11	8

#define ZD1201_CNFAUTHENTICATION_OPENSYSTEM	0x0001
#define ZD1201_CNFAUTHENTICATION_SHAREDKEY	0x0002

#endif /* _INCLUDE_ZD1201_H_ */
