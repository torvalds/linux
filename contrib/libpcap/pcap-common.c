/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
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
 *
 * pcap-common.c - common code for pcap and pcapng files
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>

#include "pcap-int.h"
#include "extract.h"
#include "pcap/sll.h"
#include "pcap/usb.h"
#include "pcap/nflog.h"
#include "pcap/can_socketcan.h"

#include "pcap-common.h"

/*
 * We don't write DLT_* values to capture files, because they're not the
 * same on all platforms.
 *
 * Unfortunately, the various flavors of BSD have not always used the same
 * numerical values for the same data types, and various patches to
 * libpcap for non-BSD OSes have added their own DLT_* codes for link
 * layer encapsulation types seen on those OSes, and those codes have had,
 * in some cases, values that were also used, on other platforms, for other
 * link layer encapsulation types.
 *
 * This means that capture files of a type whose numerical DLT_* code
 * means different things on different BSDs, or with different versions
 * of libpcap, can't always be read on systems other than those like
 * the one running on the machine on which the capture was made.
 *
 * Instead, we define here a set of LINKTYPE_* codes, and map DLT_* codes
 * to LINKTYPE_* codes when writing a savefile header, and map LINKTYPE_*
 * codes to DLT_* codes when reading a savefile header.
 *
 * For those DLT_* codes that have, as far as we know, the same values on
 * all platforms (DLT_NULL through DLT_FDDI), we define LINKTYPE_xxx as
 * DLT_xxx; that way, captures of those types can still be read by
 * versions of libpcap that map LINKTYPE_* values to DLT_* values, and
 * captures of those types written by versions of libpcap that map DLT_
 * values to LINKTYPE_ values can still be read by older versions
 * of libpcap.
 *
 * The other LINKTYPE_* codes are given values starting at 100, in the
 * hopes that no DLT_* code will be given one of those values.
 *
 * In order to ensure that a given LINKTYPE_* code's value will refer to
 * the same encapsulation type on all platforms, you should not allocate
 * a new LINKTYPE_* value without consulting
 * "tcpdump-workers@lists.tcpdump.org".  The tcpdump developers will
 * allocate a value for you, and will not subsequently allocate it to
 * anybody else; that value will be added to the "pcap.h" in the
 * tcpdump.org Git repository, so that a future libpcap release will
 * include it.
 *
 * You should, if possible, also contribute patches to libpcap and tcpdump
 * to handle the new encapsulation type, so that they can also be checked
 * into the tcpdump.org Git repository and so that they will appear in
 * future libpcap and tcpdump releases.
 *
 * Do *NOT* assume that any values after the largest value in this file
 * are available; you might not have the most up-to-date version of this
 * file, and new values after that one might have been assigned.  Also,
 * do *NOT* use any values below 100 - those might already have been
 * taken by one (or more!) organizations.
 *
 * Any platform that defines additional DLT_* codes should:
 *
 *	request a LINKTYPE_* code and value from tcpdump.org,
 *	as per the above;
 *
 *	add, in their version of libpcap, an entry to map
 *	those DLT_* codes to the corresponding LINKTYPE_*
 *	code;
 *
 *	redefine, in their "net/bpf.h", any DLT_* values
 *	that collide with the values used by their additional
 *	DLT_* codes, to remove those collisions (but without
 *	making them collide with any of the LINKTYPE_*
 *	values equal to 50 or above; they should also avoid
 *	defining DLT_* values that collide with those
 *	LINKTYPE_* values, either).
 */
#define LINKTYPE_NULL		DLT_NULL
#define LINKTYPE_ETHERNET	DLT_EN10MB	/* also for 100Mb and up */
#define LINKTYPE_EXP_ETHERNET	DLT_EN3MB	/* 3Mb experimental Ethernet */
#define LINKTYPE_AX25		DLT_AX25
#define LINKTYPE_PRONET		DLT_PRONET
#define LINKTYPE_CHAOS		DLT_CHAOS
#define LINKTYPE_IEEE802_5	DLT_IEEE802	/* DLT_IEEE802 is used for 802.5 Token Ring */
#define LINKTYPE_ARCNET_BSD	DLT_ARCNET	/* BSD-style headers */
#define LINKTYPE_SLIP		DLT_SLIP
#define LINKTYPE_PPP		DLT_PPP
#define LINKTYPE_FDDI		DLT_FDDI

/*
 * LINKTYPE_PPP is for use when there might, or might not, be an RFC 1662
 * PPP in HDLC-like framing header (with 0xff 0x03 before the PPP protocol
 * field) at the beginning of the packet.
 *
 * This is for use when there is always such a header; the address field
 * might be 0xff, for regular PPP, or it might be an address field for Cisco
 * point-to-point with HDLC framing as per section 4.3.1 of RFC 1547 ("Cisco
 * HDLC").  This is, for example, what you get with NetBSD's DLT_PPP_SERIAL.
 *
 * We give it the same value as NetBSD's DLT_PPP_SERIAL, in the hopes that
 * nobody else will choose a DLT_ value of 50, and so that DLT_PPP_SERIAL
 * captures will be written out with a link type that NetBSD's tcpdump
 * can read.
 */
#define LINKTYPE_PPP_HDLC	50		/* PPP in HDLC-like framing */

#define LINKTYPE_PPP_ETHER	51		/* NetBSD PPP-over-Ethernet */

#define LINKTYPE_SYMANTEC_FIREWALL 99		/* Symantec Enterprise Firewall */

/*
 * These correspond to DLT_s that have different values on different
 * platforms; we map between these values in capture files and
 * the DLT_ values as returned by pcap_datalink() and passed to
 * pcap_open_dead().
 */
#define LINKTYPE_ATM_RFC1483	100		/* LLC/SNAP-encapsulated ATM */
#define LINKTYPE_RAW		101		/* raw IP */
#define LINKTYPE_SLIP_BSDOS	102		/* BSD/OS SLIP BPF header */
#define LINKTYPE_PPP_BSDOS	103		/* BSD/OS PPP BPF header */

/*
 * Values starting with 104 are used for newly-assigned link-layer
 * header type values; for those link-layer header types, the DLT_
 * value returned by pcap_datalink() and passed to pcap_open_dead(),
 * and the LINKTYPE_ value that appears in capture files, are the
 * same.
 *
 * LINKTYPE_MATCHING_MIN is the lowest such value; LINKTYPE_MATCHING_MAX
 * is the highest such value.
 */
#define LINKTYPE_MATCHING_MIN	104		/* lowest value in the "matching" range */

#define LINKTYPE_C_HDLC		104		/* Cisco HDLC */
#define LINKTYPE_IEEE802_11	105		/* IEEE 802.11 (wireless) */
#define LINKTYPE_ATM_CLIP	106		/* Linux Classical IP over ATM */
#define LINKTYPE_FRELAY		107		/* Frame Relay */
#define LINKTYPE_LOOP		108		/* OpenBSD loopback */
#define LINKTYPE_ENC		109		/* OpenBSD IPSEC enc */

/*
 * These three types are reserved for future use.
 */
#define LINKTYPE_LANE8023	110		/* ATM LANE + 802.3 */
#define LINKTYPE_HIPPI		111		/* NetBSD HIPPI */
#define LINKTYPE_HDLC		112		/* NetBSD HDLC framing */

#define LINKTYPE_LINUX_SLL	113		/* Linux cooked socket capture */
#define LINKTYPE_LTALK		114		/* Apple LocalTalk hardware */
#define LINKTYPE_ECONET		115		/* Acorn Econet */

/*
 * Reserved for use with OpenBSD ipfilter.
 */
#define LINKTYPE_IPFILTER	116

#define LINKTYPE_PFLOG		117		/* OpenBSD DLT_PFLOG */
#define LINKTYPE_CISCO_IOS	118		/* For Cisco-internal use */
#define LINKTYPE_IEEE802_11_PRISM 119		/* 802.11 plus Prism II monitor mode radio metadata header */
#define LINKTYPE_IEEE802_11_AIRONET 120		/* 802.11 plus FreeBSD Aironet driver radio metadata header */

/*
 * Reserved for Siemens HiPath HDLC.
 */
#define LINKTYPE_HHDLC		121

#define LINKTYPE_IP_OVER_FC	122		/* RFC 2625 IP-over-Fibre Channel */
#define LINKTYPE_SUNATM		123		/* Solaris+SunATM */

/*
 * Reserved as per request from Kent Dahlgren <kent@praesum.com>
 * for private use.
 */
#define LINKTYPE_RIO		124		/* RapidIO */
#define LINKTYPE_PCI_EXP	125		/* PCI Express */
#define LINKTYPE_AURORA		126		/* Xilinx Aurora link layer */

#define LINKTYPE_IEEE802_11_RADIOTAP 127	/* 802.11 plus radiotap radio metadata header */

/*
 * Reserved for the TZSP encapsulation, as per request from
 * Chris Waters <chris.waters@networkchemistry.com>
 * TZSP is a generic encapsulation for any other link type,
 * which includes a means to include meta-information
 * with the packet, e.g. signal strength and channel
 * for 802.11 packets.
 */
#define LINKTYPE_TZSP		128		/* Tazmen Sniffer Protocol */

#define LINKTYPE_ARCNET_LINUX	129		/* Linux-style headers */

/*
 * Juniper-private data link types, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The corresponding
 * DLT_s are used for passing on chassis-internal
 * metainformation such as QOS profiles, etc..
 */
#define LINKTYPE_JUNIPER_MLPPP  130
#define LINKTYPE_JUNIPER_MLFR   131
#define LINKTYPE_JUNIPER_ES     132
#define LINKTYPE_JUNIPER_GGSN   133
#define LINKTYPE_JUNIPER_MFR    134
#define LINKTYPE_JUNIPER_ATM2   135
#define LINKTYPE_JUNIPER_SERVICES 136
#define LINKTYPE_JUNIPER_ATM1   137

#define LINKTYPE_APPLE_IP_OVER_IEEE1394 138	/* Apple IP-over-IEEE 1394 cooked header */

#define LINKTYPE_MTP2_WITH_PHDR	139
#define LINKTYPE_MTP2		140
#define LINKTYPE_MTP3		141
#define LINKTYPE_SCCP		142

#define LINKTYPE_DOCSIS		143		/* DOCSIS MAC frames */

#define LINKTYPE_LINUX_IRDA	144		/* Linux-IrDA */

/*
 * Reserved for IBM SP switch and IBM Next Federation switch.
 */
#define LINKTYPE_IBM_SP		145
#define LINKTYPE_IBM_SN		146

/*
 * Reserved for private use.  If you have some link-layer header type
 * that you want to use within your organization, with the capture files
 * using that link-layer header type not ever be sent outside your
 * organization, you can use these values.
 *
 * No libpcap release will use these for any purpose, nor will any
 * tcpdump release use them, either.
 *
 * Do *NOT* use these in capture files that you expect anybody not using
 * your private versions of capture-file-reading tools to read; in
 * particular, do *NOT* use them in products, otherwise you may find that
 * people won't be able to use tcpdump, or snort, or Ethereal, or... to
 * read capture files from your firewall/intrusion detection/traffic
 * monitoring/etc. appliance, or whatever product uses that LINKTYPE_ value,
 * and you may also find that the developers of those applications will
 * not accept patches to let them read those files.
 *
 * Also, do not use them if somebody might send you a capture using them
 * for *their* private type and tools using them for *your* private type
 * would have to read them.
 *
 * Instead, in those cases, ask "tcpdump-workers@lists.tcpdump.org" for a
 * new DLT_ and LINKTYPE_ value, as per the comment in pcap/bpf.h, and use
 * the type you're given.
 */
#define LINKTYPE_USER0		147
#define LINKTYPE_USER1		148
#define LINKTYPE_USER2		149
#define LINKTYPE_USER3		150
#define LINKTYPE_USER4		151
#define LINKTYPE_USER5		152
#define LINKTYPE_USER6		153
#define LINKTYPE_USER7		154
#define LINKTYPE_USER8		155
#define LINKTYPE_USER9		156
#define LINKTYPE_USER10		157
#define LINKTYPE_USER11		158
#define LINKTYPE_USER12		159
#define LINKTYPE_USER13		160
#define LINKTYPE_USER14		161
#define LINKTYPE_USER15		162

/*
 * For future use with 802.11 captures - defined by AbsoluteValue
 * Systems to store a number of bits of link-layer information
 * including radio information:
 *
 *	http://www.shaftnet.org/~pizza/software/capturefrm.txt
 */
#define LINKTYPE_IEEE802_11_AVS	163	/* 802.11 plus AVS radio metadata header */

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The corresponding
 * DLT_s are used for passing on chassis-internal
 * metainformation such as QOS profiles, etc..
 */
#define LINKTYPE_JUNIPER_MONITOR 164

/*
 * BACnet MS/TP frames.
 */
#define LINKTYPE_BACNET_MS_TP	165

/*
 * Another PPP variant as per request from Karsten Keil <kkeil@suse.de>.
 *
 * This is used in some OSes to allow a kernel socket filter to distinguish
 * between incoming and outgoing packets, on a socket intended to
 * supply pppd with outgoing packets so it can do dial-on-demand and
 * hangup-on-lack-of-demand; incoming packets are filtered out so they
 * don't cause pppd to hold the connection up (you don't want random
 * input packets such as port scans, packets from old lost connections,
 * etc. to force the connection to stay up).
 *
 * The first byte of the PPP header (0xff03) is modified to accomodate
 * the direction - 0x00 = IN, 0x01 = OUT.
 */
#define LINKTYPE_PPP_PPPD	166

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_s are used
 * for passing on chassis-internal metainformation such as
 * QOS profiles, cookies, etc..
 */
#define LINKTYPE_JUNIPER_PPPOE     167
#define LINKTYPE_JUNIPER_PPPOE_ATM 168

#define LINKTYPE_GPRS_LLC	169		/* GPRS LLC */
#define LINKTYPE_GPF_T		170		/* GPF-T (ITU-T G.7041/Y.1303) */
#define LINKTYPE_GPF_F		171		/* GPF-F (ITU-T G.7041/Y.1303) */

/*
 * Requested by Oolan Zimmer <oz@gcom.com> for use in Gcom's T1/E1 line
 * monitoring equipment.
 */
#define LINKTYPE_GCOM_T1E1	172
#define LINKTYPE_GCOM_SERIAL	173

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_ is used
 * for internal communication to Physical Interface Cards (PIC)
 */
#define LINKTYPE_JUNIPER_PIC_PEER    174

/*
 * Link types requested by Gregor Maier <gregor@endace.com> of Endace
 * Measurement Systems.  They add an ERF header (see
 * http://www.endace.com/support/EndaceRecordFormat.pdf) in front of
 * the link-layer header.
 */
#define LINKTYPE_ERF_ETH	175	/* Ethernet */
#define LINKTYPE_ERF_POS	176	/* Packet-over-SONET */

/*
 * Requested by Daniele Orlandi <daniele@orlandi.com> for raw LAPD
 * for vISDN (http://www.orlandi.com/visdn/).  Its link-layer header
 * includes additional information before the LAPD header, so it's
 * not necessarily a generic LAPD header.
 */
#define LINKTYPE_LINUX_LAPD	177

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The Link Types are used for prepending meta-information
 * like interface index, interface name
 * before standard Ethernet, PPP, Frelay & C-HDLC Frames
 */
#define LINKTYPE_JUNIPER_ETHER  178
#define LINKTYPE_JUNIPER_PPP    179
#define LINKTYPE_JUNIPER_FRELAY 180
#define LINKTYPE_JUNIPER_CHDLC  181

/*
 * Multi Link Frame Relay (FRF.16)
 */
#define LINKTYPE_MFR            182

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for internal communication with a
 * voice Adapter Card (PIC)
 */
#define LINKTYPE_JUNIPER_VP     183

/*
 * Arinc 429 frames.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Every frame contains a 32bit A429 label.
 * More documentation on Arinc 429 can be found at
 * http://www.condoreng.com/support/downloads/tutorials/ARINCTutorial.pdf
 */
#define LINKTYPE_A429           184

/*
 * Arinc 653 Interpartition Communication messages.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Please refer to the A653-1 standard for more information.
 */
#define LINKTYPE_A653_ICM       185

/*
 * This used to be "USB packets, beginning with a USB setup header;
 * requested by Paolo Abeni <paolo.abeni@email.it>."
 *
 * However, that header didn't work all that well - it left out some
 * useful information - and was abandoned in favor of the DLT_USB_LINUX
 * header.
 *
 * This is now used by FreeBSD for its BPF taps for USB; that has its
 * own headers.  So it is written, so it is done.
 */
#define LINKTYPE_USB_FREEBSD	186

/*
 * Bluetooth HCI UART transport layer (part H:4); requested by
 * Paolo Abeni.
 */
#define LINKTYPE_BLUETOOTH_HCI_H4	187

/*
 * IEEE 802.16 MAC Common Part Sublayer; requested by Maria Cruz
 * <cruz_petagay@bah.com>.
 */
#define LINKTYPE_IEEE802_16_MAC_CPS	188

/*
 * USB packets, beginning with a Linux USB header; requested by
 * Paolo Abeni <paolo.abeni@email.it>.
 */
#define LINKTYPE_USB_LINUX		189

/*
 * Controller Area Network (CAN) v. 2.0B packets.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Used to dump CAN packets coming from a CAN Vector board.
 * More documentation on the CAN v2.0B frames can be found at
 * http://www.can-cia.org/downloads/?269
 */
#define LINKTYPE_CAN20B         190

/*
 * IEEE 802.15.4, with address fields padded, as is done by Linux
 * drivers; requested by Juergen Schimmer.
 */
#define LINKTYPE_IEEE802_15_4_LINUX	191

/*
 * Per Packet Information encapsulated packets.
 * LINKTYPE_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 */
#define LINKTYPE_PPI			192

/*
 * Header for 802.16 MAC Common Part Sublayer plus a radiotap radio header;
 * requested by Charles Clancy.
 */
#define LINKTYPE_IEEE802_16_MAC_CPS_RADIO	193

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for internal communication with a
 * integrated service module (ISM).
 */
#define LINKTYPE_JUNIPER_ISM    194

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), and with the FCS at the end of the frame; requested by
 * Mikko Saarnivala <mikko.saarnivala@sensinode.com>.
 *
 * This should only be used if the FCS is present at the end of the
 * frame; if the frame has no FCS, DLT_IEEE802_15_4_NOFCS should be
 * used.
 */
#define LINKTYPE_IEEE802_15_4_WITHFCS	195

/*
 * Various link-layer types, with a pseudo-header, for SITA
 * (http://www.sita.aero/); requested by Fulko Hew (fulko.hew@gmail.com).
 */
#define LINKTYPE_SITA		196

/*
 * Various link-layer types, with a pseudo-header, for Endace DAG cards;
 * encapsulates Endace ERF records.  Requested by Stephen Donnelly
 * <stephen@endace.com>.
 */
#define LINKTYPE_ERF		197

/*
 * Special header prepended to Ethernet packets when capturing from a
 * u10 Networks board.  Requested by Phil Mulholland
 * <phil@u10networks.com>.
 */
#define LINKTYPE_RAIF1		198

/*
 * IPMB packet for IPMI, beginning with the I2C slave address, followed
 * by the netFn and LUN, etc..  Requested by Chanthy Toeung
 * <chanthy.toeung@ca.kontron.com>.
 */
#define LINKTYPE_IPMB		199

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for capturing data on a secure tunnel interface.
 */
#define LINKTYPE_JUNIPER_ST     200

/*
 * Bluetooth HCI UART transport layer (part H:4), with pseudo-header
 * that includes direction information; requested by Paolo Abeni.
 */
#define LINKTYPE_BLUETOOTH_HCI_H4_WITH_PHDR	201

/*
 * AX.25 packet with a 1-byte KISS header; see
 *
 *	http://www.ax25.net/kiss.htm
 *
 * as per Richard Stearn <richard@rns-stearn.demon.co.uk>.
 */
#define LINKTYPE_AX25_KISS	202

/*
 * LAPD packets from an ISDN channel, starting with the address field,
 * with no pseudo-header.
 * Requested by Varuna De Silva <varunax@gmail.com>.
 */
#define LINKTYPE_LAPD		203

/*
 * Variants of various link-layer headers, with a one-byte direction
 * pseudo-header prepended - zero means "received by this host",
 * non-zero (any non-zero value) means "sent by this host" - as per
 * Will Barker <w.barker@zen.co.uk>.
 */
#define LINKTYPE_PPP_WITH_DIR	204	/* PPP */
#define LINKTYPE_C_HDLC_WITH_DIR 205	/* Cisco HDLC */
#define LINKTYPE_FRELAY_WITH_DIR 206	/* Frame Relay */
#define LINKTYPE_LAPB_WITH_DIR	207	/* LAPB */

/*
 * 208 is reserved for an as-yet-unspecified proprietary link-layer
 * type, as requested by Will Barker.
 */

/*
 * IPMB with a Linux-specific pseudo-header; as requested by Alexey Neyman
 * <avn@pigeonpoint.com>.
 */
#define LINKTYPE_IPMB_LINUX	209

/*
 * FlexRay automotive bus - http://www.flexray.com/ - as requested
 * by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define LINKTYPE_FLEXRAY	210

/*
 * Media Oriented Systems Transport (MOST) bus for multimedia
 * transport - http://www.mostcooperation.com/ - as requested
 * by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define LINKTYPE_MOST		211

/*
 * Local Interconnect Network (LIN) bus for vehicle networks -
 * http://www.lin-subbus.org/ - as requested by Hannes Kaelber
 * <hannes.kaelber@x2e.de>.
 */
#define LINKTYPE_LIN		212

/*
 * X2E-private data link type used for serial line capture,
 * as requested by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define LINKTYPE_X2E_SERIAL	213

/*
 * X2E-private data link type used for the Xoraya data logger
 * family, as requested by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define LINKTYPE_X2E_XORAYA	214

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), but with the PHY-level data for non-ASK PHYs (4 octets
 * of 0 as preamble, one octet of SFD, one octet of frame length+
 * reserved bit, and then the MAC-layer data, starting with the
 * frame control field).
 *
 * Requested by Max Filippov <jcmvbkbc@gmail.com>.
 */
#define LINKTYPE_IEEE802_15_4_NONASK_PHY	215

/*
 * David Gibson <david@gibson.dropbear.id.au> requested this for
 * captures from the Linux kernel /dev/input/eventN devices. This
 * is used to communicate keystrokes and mouse movements from the
 * Linux kernel to display systems, such as Xorg.
 */
#define LINKTYPE_LINUX_EVDEV	216

/*
 * GSM Um and Abis interfaces, preceded by a "gsmtap" header.
 *
 * Requested by Harald Welte <laforge@gnumonks.org>.
 */
#define LINKTYPE_GSMTAP_UM	217
#define LINKTYPE_GSMTAP_ABIS	218

/*
 * MPLS, with an MPLS label as the link-layer header.
 * Requested by Michele Marchetto <michele@openbsd.org> on behalf
 * of OpenBSD.
 */
#define LINKTYPE_MPLS		219

/*
 * USB packets, beginning with a Linux USB header, with the USB header
 * padded to 64 bytes; required for memory-mapped access.
 */
#define LINKTYPE_USB_LINUX_MMAPPED		220

/*
 * DECT packets, with a pseudo-header; requested by
 * Matthias Wenzel <tcpdump@mazzoo.de>.
 */
#define LINKTYPE_DECT		221

/*
 * From: "Lidwa, Eric (GSFC-582.0)[SGT INC]" <eric.lidwa-1@nasa.gov>
 * Date: Mon, 11 May 2009 11:18:30 -0500
 *
 * DLT_AOS. We need it for AOS Space Data Link Protocol.
 *   I have already written dissectors for but need an OK from
 *   legal before I can submit a patch.
 *
 */
#define LINKTYPE_AOS		222

/*
 * Wireless HART (Highway Addressable Remote Transducer)
 * From the HART Communication Foundation
 * IES/PAS 62591
 *
 * Requested by Sam Roberts <vieuxtech@gmail.com>.
 */
#define LINKTYPE_WIHART		223

/*
 * Fibre Channel FC-2 frames, beginning with a Frame_Header.
 * Requested by Kahou Lei <kahou82@gmail.com>.
 */
#define LINKTYPE_FC_2		224

/*
 * Fibre Channel FC-2 frames, beginning with an encoding of the
 * SOF, and ending with an encoding of the EOF.
 *
 * The encodings represent the frame delimiters as 4-byte sequences
 * representing the corresponding ordered sets, with K28.5
 * represented as 0xBC, and the D symbols as the corresponding
 * byte values; for example, SOFi2, which is K28.5 - D21.5 - D1.2 - D21.2,
 * is represented as 0xBC 0xB5 0x55 0x55.
 *
 * Requested by Kahou Lei <kahou82@gmail.com>.
 */
#define LINKTYPE_FC_2_WITH_FRAME_DELIMS		225

/*
 * Solaris ipnet pseudo-header; requested by Darren Reed <Darren.Reed@Sun.COM>.
 *
 * The pseudo-header starts with a one-byte version number; for version 2,
 * the pseudo-header is:
 *
 * struct dl_ipnetinfo {
 *     uint8_t   dli_version;
 *     uint8_t   dli_family;
 *     uint16_t  dli_htype;
 *     uint32_t  dli_pktlen;
 *     uint32_t  dli_ifindex;
 *     uint32_t  dli_grifindex;
 *     uint32_t  dli_zsrc;
 *     uint32_t  dli_zdst;
 * };
 *
 * dli_version is 2 for the current version of the pseudo-header.
 *
 * dli_family is a Solaris address family value, so it's 2 for IPv4
 * and 26 for IPv6.
 *
 * dli_htype is a "hook type" - 0 for incoming packets, 1 for outgoing
 * packets, and 2 for packets arriving from another zone on the same
 * machine.
 *
 * dli_pktlen is the length of the packet data following the pseudo-header
 * (so the captured length minus dli_pktlen is the length of the
 * pseudo-header, assuming the entire pseudo-header was captured).
 *
 * dli_ifindex is the interface index of the interface on which the
 * packet arrived.
 *
 * dli_grifindex is the group interface index number (for IPMP interfaces).
 *
 * dli_zsrc is the zone identifier for the source of the packet.
 *
 * dli_zdst is the zone identifier for the destination of the packet.
 *
 * A zone number of 0 is the global zone; a zone number of 0xffffffff
 * means that the packet arrived from another host on the network, not
 * from another zone on the same machine.
 *
 * An IPv4 or IPv6 datagram follows the pseudo-header; dli_family indicates
 * which of those it is.
 */
#define LINKTYPE_IPNET		226

/*
 * CAN (Controller Area Network) frames, with a pseudo-header as supplied
 * by Linux SocketCAN, and with multi-byte numerical fields in that header
 * in big-endian byte order.
 *
 * See Documentation/networking/can.txt in the Linux source.
 *
 * Requested by Felix Obenhuber <felix@obenhuber.de>.
 */
#define LINKTYPE_CAN_SOCKETCAN	227

/*
 * Raw IPv4/IPv6; different from DLT_RAW in that the DLT_ value specifies
 * whether it's v4 or v6.  Requested by Darren Reed <Darren.Reed@Sun.COM>.
 */
#define LINKTYPE_IPV4		228
#define LINKTYPE_IPV6		229

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), and with no FCS at the end of the frame; requested by
 * Jon Smirl <jonsmirl@gmail.com>.
 */
#define LINKTYPE_IEEE802_15_4_NOFCS		230

/*
 * Raw D-Bus:
 *
 *	http://www.freedesktop.org/wiki/Software/dbus
 *
 * messages:
 *
 *	http://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages
 *
 * starting with the endianness flag, followed by the message type, etc.,
 * but without the authentication handshake before the message sequence:
 *
 *	http://dbus.freedesktop.org/doc/dbus-specification.html#auth-protocol
 *
 * Requested by Martin Vidner <martin@vidner.net>.
 */
#define LINKTYPE_DBUS		231

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define LINKTYPE_JUNIPER_VS			232
#define LINKTYPE_JUNIPER_SRX_E2E		233
#define LINKTYPE_JUNIPER_FIBRECHANNEL		234

/*
 * DVB-CI (DVB Common Interface for communication between a PC Card
 * module and a DVB receiver).  See
 *
 *	http://www.kaiser.cx/pcap-dvbci.html
 *
 * for the specification.
 *
 * Requested by Martin Kaiser <martin@kaiser.cx>.
 */
#define LINKTYPE_DVB_CI		235

/*
 * Variant of 3GPP TS 27.010 multiplexing protocol.  Requested
 * by Hans-Christoph Schemmel <hans-christoph.schemmel@cinterion.com>.
 */
#define LINKTYPE_MUX27010	236

/*
 * STANAG 5066 D_PDUs.  Requested by M. Baris Demiray
 * <barisdemiray@gmail.com>.
 */
#define LINKTYPE_STANAG_5066_D_PDU		237

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define LINKTYPE_JUNIPER_ATM_CEMIC		238

/*
 * NetFilter LOG messages
 * (payload of netlink NFNL_SUBSYS_ULOG/NFULNL_MSG_PACKET packets)
 *
 * Requested by Jakub Zawadzki <darkjames-ws@darkjames.pl>
 */
#define LINKTYPE_NFLOG		239

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and always
 * with the payload including the FCS, as supplied by their
 * netANALYZER hardware and software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define LINKTYPE_NETANALYZER	240

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and FCS and
 * 1 byte of SFD, as supplied by their netANALYZER hardware and
 * software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define LINKTYPE_NETANALYZER_TRANSPARENT	241

/*
 * IP-over-InfiniBand, as specified by RFC 4391.
 *
 * Requested by Petr Sumbera <petr.sumbera@oracle.com>.
 */
#define LINKTYPE_IPOIB		242

/*
 * MPEG-2 transport stream (ISO 13818-1/ITU-T H.222.0).
 *
 * Requested by Guy Martin <gmsoft@tuxicoman.be>.
 */
#define LINKTYPE_MPEG_2_TS	243

/*
 * ng4T GmbH's UMTS Iub/Iur-over-ATM and Iub/Iur-over-IP format as
 * used by their ng40 protocol tester.
 *
 * Requested by Jens Grimmer <jens.grimmer@ng4t.com>.
 */
#define LINKTYPE_NG40		244

/*
 * Pseudo-header giving adapter number and flags, followed by an NFC
 * (Near-Field Communications) Logical Link Control Protocol (LLCP) PDU,
 * as specified by NFC Forum Logical Link Control Protocol Technical
 * Specification LLCP 1.1.
 *
 * Requested by Mike Wakerly <mikey@google.com>.
 */
#define LINKTYPE_NFC_LLCP	245

/*
 * pfsync output; DLT_PFSYNC is 18, which collides with DLT_CIP in
 * SuSE 6.3, on OpenBSD, NetBSD, DragonFly BSD, and macOS, and
 * is 121, which collides with DLT_HHDLC, in FreeBSD.  We pick a
 * shiny new link-layer header type value that doesn't collide with
 * anything, in the hopes that future pfsync savefiles, if any,
 * won't require special hacks to distinguish from other savefiles.
 *
 */
#define LINKTYPE_PFSYNC		246

/*
 * Raw InfiniBand packets, starting with the Local Routing Header.
 *
 * Requested by Oren Kladnitsky <orenk@mellanox.com>.
 */
#define LINKTYPE_INFINIBAND	247

/*
 * SCTP, with no lower-level protocols (i.e., no IPv4 or IPv6).
 *
 * Requested by Michael Tuexen <Michael.Tuexen@lurchi.franken.de>.
 */
#define LINKTYPE_SCTP		248

/*
 * USB packets, beginning with a USBPcap header.
 *
 * Requested by Tomasz Mon <desowin@gmail.com>
 */
#define LINKTYPE_USBPCAP	249

/*
 * Schweitzer Engineering Laboratories "RTAC" product serial-line
 * packets.
 *
 * Requested by Chris Bontje <chris_bontje@selinc.com>.
 */
#define DLT_RTAC_SERIAL		250

/*
 * Bluetooth Low Energy air interface link-layer packets.
 *
 * Requested by Mike Kershaw <dragorn@kismetwireless.net>.
 */
#define LINKTYPE_BLUETOOTH_LE_LL	251

/*
 * Link-layer header type for upper-protocol layer PDU saves from wireshark.
 *
 * the actual contents are determined by two TAGs stored with each
 * packet:
 *   EXP_PDU_TAG_LINKTYPE          the link type (LINKTYPE_ value) of the
 *				   original packet.
 *
 *   EXP_PDU_TAG_PROTO_NAME        the name of the wireshark dissector
 * 				   that can make sense of the data stored.
 */
#define LINKTYPE_WIRESHARK_UPPER_PDU	252

/*
 * Link-layer header type for the netlink protocol (nlmon devices).
 */
#define LINKTYPE_NETLINK		253

/*
 * Bluetooth Linux Monitor headers for the BlueZ stack.
 */
#define LINKTYPE_BLUETOOTH_LINUX_MONITOR	254

/*
 * Bluetooth Basic Rate/Enhanced Data Rate baseband packets, as
 * captured by Ubertooth.
 */
#define LINKTYPE_BLUETOOTH_BREDR_BB	255

/*
 * Bluetooth Low Energy link layer packets, as captured by Ubertooth.
 */
#define LINKTYPE_BLUETOOTH_LE_LL_WITH_PHDR	256

/*
 * PROFIBUS data link layer.
 */
#define LINKTYPE_PROFIBUS_DL		257

/*
 * Apple's DLT_PKTAP headers.
 *
 * Sadly, the folks at Apple either had no clue that the DLT_USERn values
 * are for internal use within an organization and partners only, and
 * didn't know that the right way to get a link-layer header type is to
 * ask tcpdump.org for one, or knew and didn't care, so they just
 * used DLT_USER2, which causes problems for everything except for
 * their version of tcpdump.
 *
 * So I'll just give them one; hopefully this will show up in a
 * libpcap release in time for them to get this into 10.10 Big Sur
 * or whatever Mavericks' successor is called.  LINKTYPE_PKTAP
 * will be 258 *even on macOS*; that is *intentional*, so that
 * PKTAP files look the same on *all* OSes (different OSes can have
 * different numerical values for a given DLT_, but *MUST NOT* have
 * different values for what goes in a file, as files can be moved
 * between OSes!).
 */
#define LINKTYPE_PKTAP		258

/*
 * Ethernet packets preceded by a header giving the last 6 octets
 * of the preamble specified by 802.3-2012 Clause 65, section
 * 65.1.3.2 "Transmit".
 */
#define LINKTYPE_EPON		259

/*
 * IPMI trace packets, as specified by Table 3-20 "Trace Data Block Format"
 * in the PICMG HPM.2 specification.
 */
#define LINKTYPE_IPMI_HPM_2	260

/*
 * per  Joshua Wright <jwright@hasborg.com>, formats for Zwave captures.
 */
#define LINKTYPE_ZWAVE_R1_R2	261
#define LINKTYPE_ZWAVE_R3	262

/*
 * per Steve Karg <skarg@users.sourceforge.net>, formats for Wattstopper
 * Digital Lighting Management room bus serial protocol captures.
 */
#define LINKTYPE_WATTSTOPPER_DLM 263

/*
 * ISO 14443 contactless smart card messages.
 */
#define LINKTYPE_ISO_14443      264

/*
 * Radio data system (RDS) groups.  IEC 62106.
 * Per Jonathan Brucker <jonathan.brucke@gmail.com>.
 */
#define LINKTYPE_RDS		265

/*
 * USB packets, beginning with a Darwin (macOS, etc.) header.
 */
#define LINKTYPE_USB_DARWIN	266

/*
 * OpenBSD DLT_OPENFLOW.
 */
#define LINKTYPE_OPENFLOW	267

/*
 * SDLC frames containing SNA PDUs.
 */
#define LINKTYPE_SDLC		268

/*
 * per "Selvig, Bjorn" <b.selvig@ti.com> used for
 * TI protocol sniffer.
 */
#define LINKTYPE_TI_LLN_SNIFFER	269

/*
 * per: Erik de Jong <erikdejong at gmail.com> for
 *   https://github.com/eriknl/LoRaTap/releases/tag/v0.1
 */
#define LINKTYPE_LORATAP        270

/*
 * per: Stefanha at gmail.com for
 *   http://lists.sandelman.ca/pipermail/tcpdump-workers/2017-May/000772.html
 * and: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/vsockmon.h
 * for: http://qemu-project.org/Features/VirtioVsock
 */
#define LINKTYPE_VSOCK          271

/*
 * Nordic Semiconductor Bluetooth LE sniffer.
 */
#define LINKTYPE_NORDIC_BLE	272

/*
 * Excentis DOCSIS 3.1 RF sniffer (XRA-31)
 *   per: bruno.verstuyft at excentis.com
 *        http://www.xra31.com/xra-header
 */
#define LINKTYPE_DOCSIS31_XRA31	273

/*
 * mPackets, as specified by IEEE 802.3br Figure 99-4, starting
 * with the preamble and always ending with a CRC field.
 */
#define LINKTYPE_ETHERNET_MPACKET	274

/*
 * DisplayPort AUX channel monitoring data as specified by VESA
 * DisplayPort(DP) Standard preceeded by a pseudo-header.
 *    per dirk.eibach at gdsys.cc
 */
#define LINKTYPE_DISPLAYPORT_AUX	275

#define LINKTYPE_MATCHING_MAX	275		/* highest value in the "matching" range */

static struct linktype_map {
	int	dlt;
	int	linktype;
} map[] = {
	/*
	 * These DLT_* codes have LINKTYPE_* codes with values identical
	 * to the values of the corresponding DLT_* code.
	 */
	{ DLT_NULL,		LINKTYPE_NULL },
	{ DLT_EN10MB,		LINKTYPE_ETHERNET },
	{ DLT_EN3MB,		LINKTYPE_EXP_ETHERNET },
	{ DLT_AX25,		LINKTYPE_AX25 },
	{ DLT_PRONET,		LINKTYPE_PRONET },
	{ DLT_CHAOS,		LINKTYPE_CHAOS },
	{ DLT_IEEE802,		LINKTYPE_IEEE802_5 },
	{ DLT_ARCNET,		LINKTYPE_ARCNET_BSD },
	{ DLT_SLIP,		LINKTYPE_SLIP },
	{ DLT_PPP,		LINKTYPE_PPP },
	{ DLT_FDDI,	 	LINKTYPE_FDDI },
	{ DLT_SYMANTEC_FIREWALL, LINKTYPE_SYMANTEC_FIREWALL },

	/*
	 * These DLT_* codes have different values on different
	 * platforms; we map them to LINKTYPE_* codes that
	 * have values that should never be equal to any DLT_*
	 * code.
	 */
#ifdef DLT_FR
	/* BSD/OS Frame Relay */
	{ DLT_FR,		LINKTYPE_FRELAY },
#endif

	{ DLT_ATM_RFC1483, 	LINKTYPE_ATM_RFC1483 },
	{ DLT_RAW,		LINKTYPE_RAW },
	{ DLT_SLIP_BSDOS,	LINKTYPE_SLIP_BSDOS },
	{ DLT_PPP_BSDOS,	LINKTYPE_PPP_BSDOS },

	/* BSD/OS Cisco HDLC */
	{ DLT_C_HDLC,		LINKTYPE_C_HDLC },

	/*
	 * These DLT_* codes are not on all platforms, but, so far,
	 * there don't appear to be any platforms that define
	 * other codes with those values; we map them to
	 * different LINKTYPE_* values anyway, just in case.
	 */

	/* Linux ATM Classical IP */
	{ DLT_ATM_CLIP,		LINKTYPE_ATM_CLIP },

	/* NetBSD sync/async serial PPP (or Cisco HDLC) */
	{ DLT_PPP_SERIAL,	LINKTYPE_PPP_HDLC },

	/* NetBSD PPP over Ethernet */
	{ DLT_PPP_ETHER,	LINKTYPE_PPP_ETHER },

	/*
	 * All LINKTYPE_ values between LINKTYPE_MATCHING_MIN
	 * and LINKTYPE_MATCHING_MAX are mapped to identical
	 * DLT_ values.
	 */

	{ -1,			-1 }
};

int
dlt_to_linktype(int dlt)
{
	int i;

	/*
	 * DLTs that, on some platforms, have values in the matching range
	 * but that *don't* have the same value as the corresponding
	 * LINKTYPE because, for some reason, not all OSes have the
	 * same value for that DLT (note that the DLT's value might be
	 * outside the matching range on some of those OSes).
	 */
	if (dlt == DLT_PFSYNC)
		return (LINKTYPE_PFSYNC);
	if (dlt == DLT_PKTAP)
		return (LINKTYPE_PKTAP);

	/*
	 * For all other values in the matching range, the DLT
	 * value is the same as the LINKTYPE value.
	 */
	if (dlt >= DLT_MATCHING_MIN && dlt <= DLT_MATCHING_MAX)
		return (dlt);

	/*
	 * Map the values outside that range.
	 */
	for (i = 0; map[i].dlt != -1; i++) {
		if (map[i].dlt == dlt)
			return (map[i].linktype);
	}

	/*
	 * If we don't have a mapping for this DLT, return an
	 * error; that means that this is a value with no corresponding
	 * LINKTYPE, and we need to assign one.
	 */
	return (-1);
}

int
linktype_to_dlt(int linktype)
{
	int i;

	/*
	 * LINKTYPEs in the matching range that *don't*
	 * have the same value as the corresponding DLTs
	 * because, for some reason, not all OSes have the
	 * same value for that DLT.
	 */
	if (linktype == LINKTYPE_PFSYNC)
		return (DLT_PFSYNC);
	if (linktype == LINKTYPE_PKTAP)
		return (DLT_PKTAP);

	/*
	 * For all other values in the matching range, the LINKTYPE
	 * value is the same as the DLT value.
	 */
	if (linktype >= LINKTYPE_MATCHING_MIN &&
	    linktype <= LINKTYPE_MATCHING_MAX)
		return (linktype);

	/*
	 * Map the values outside that range.
	 */
	for (i = 0; map[i].linktype != -1; i++) {
		if (map[i].linktype == linktype)
			return (map[i].dlt);
	}

	/*
	 * If we don't have an entry for this LINKTYPE, return
	 * the link type value; it may be a DLT from an older
	 * version of libpcap.
	 */
	return linktype;
}

/*
 * Return the maximum snapshot length for a given DLT_ value.
 *
 * For most link-layer types, we use MAXIMUM_SNAPLEN, but for DLT_DBUS,
 * the maximum is 134217728, as per
 *
 *    https://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages
 */
u_int
max_snaplen_for_dlt(int dlt)
{
	if (dlt == DLT_DBUS)
		return 134217728;
	else
		return MAXIMUM_SNAPLEN;
}

/*
 * DLT_LINUX_SLL packets with a protocol type of LINUX_SLL_P_CAN or
 * LINUX_SLL_P_CANFD have SocketCAN headers in front of the payload,
 * with the CAN ID being in host byte order.
 *
 * When reading a DLT_LINUX_SLL capture file, we need to check for those
 * packets and convert the CAN ID from the byte order of the host that
 * wrote the file to this host's byte order.
 */
static void
swap_linux_sll_header(const struct pcap_pkthdr *hdr, u_char *buf)
{
	u_int caplen = hdr->caplen;
	u_int length = hdr->len;
	struct sll_header *shdr = (struct sll_header *)buf;
	uint16_t protocol;
	pcap_can_socketcan_hdr *chdr;

	if (caplen < (u_int) sizeof(struct sll_header) ||
	    length < (u_int) sizeof(struct sll_header)) {
		/* Not enough data to have the protocol field */
		return;
	}

	protocol = EXTRACT_16BITS(&shdr->sll_protocol);
	if (protocol != LINUX_SLL_P_CAN && protocol != LINUX_SLL_P_CANFD)
		return;

	/*
	 * SocketCAN packet; fix up the packet's header.
	 */
	chdr = (pcap_can_socketcan_hdr *)(buf + sizeof(struct sll_header));
	if (caplen < (u_int) sizeof(struct sll_header) + sizeof(chdr->can_id) ||
	    length < (u_int) sizeof(struct sll_header) + sizeof(chdr->can_id)) {
		/* Not enough data to have the CAN ID */
		return;
	}
	chdr->can_id = SWAPLONG(chdr->can_id);
}

/*
 * The DLT_USB_LINUX and DLT_USB_LINUX_MMAPPED headers are in host
 * byte order when capturing (it's supplied directly from a
 * memory-mapped buffer shared by the kernel).
 *
 * When reading a DLT_USB_LINUX or DLT_USB_LINUX_MMAPPED capture file,
 * we need to convert it from the byte order of the host that wrote
 * the file to this host's byte order.
 */
static void
swap_linux_usb_header(const struct pcap_pkthdr *hdr, u_char *buf,
    int header_len_64_bytes)
{
	pcap_usb_header_mmapped *uhdr = (pcap_usb_header_mmapped *)buf;
	bpf_u_int32 offset = 0;

	/*
	 * "offset" is the offset *past* the field we're swapping;
	 * we skip the field *before* checking to make sure
	 * the captured data length includes the entire field.
	 */

	/*
	 * The URB id is a totally opaque value; do we really need to
	 * convert it to the reading host's byte order???
	 */
	offset += 8;			/* skip past id */
	if (hdr->caplen < offset)
		return;
	uhdr->id = SWAPLL(uhdr->id);

	offset += 4;			/* skip past various 1-byte fields */

	offset += 2;			/* skip past bus_id */
	if (hdr->caplen < offset)
		return;
	uhdr->bus_id = SWAPSHORT(uhdr->bus_id);

	offset += 2;			/* skip past various 1-byte fields */

	offset += 8;			/* skip past ts_sec */
	if (hdr->caplen < offset)
		return;
	uhdr->ts_sec = SWAPLL(uhdr->ts_sec);

	offset += 4;			/* skip past ts_usec */
	if (hdr->caplen < offset)
		return;
	uhdr->ts_usec = SWAPLONG(uhdr->ts_usec);

	offset += 4;			/* skip past status */
	if (hdr->caplen < offset)
		return;
	uhdr->status = SWAPLONG(uhdr->status);

	offset += 4;			/* skip past urb_len */
	if (hdr->caplen < offset)
		return;
	uhdr->urb_len = SWAPLONG(uhdr->urb_len);

	offset += 4;			/* skip past data_len */
	if (hdr->caplen < offset)
		return;
	uhdr->data_len = SWAPLONG(uhdr->data_len);

	if (uhdr->transfer_type == URB_ISOCHRONOUS) {
		offset += 4;			/* skip past s.iso.error_count */
		if (hdr->caplen < offset)
			return;
		uhdr->s.iso.error_count = SWAPLONG(uhdr->s.iso.error_count);

		offset += 4;			/* skip past s.iso.numdesc */
		if (hdr->caplen < offset)
			return;
		uhdr->s.iso.numdesc = SWAPLONG(uhdr->s.iso.numdesc);
	} else
		offset += 8;			/* skip USB setup header */

	/*
	 * With the old header, there are no isochronous descriptors
	 * after the header.
	 *
	 * With the new header, the actual number of descriptors in
	 * the header is not s.iso.numdesc, it's ndesc - only the
	 * first N descriptors, for some value of N, are put into
	 * the header, and ndesc is set to the actual number copied.
	 * In addition, if s.iso.numdesc is negative, no descriptors
	 * are captured, and ndesc is set to 0.
	 */
	if (header_len_64_bytes) {
		/*
		 * This is either the "version 1" header, with
		 * 16 bytes of additional fields at the end, or
		 * a "version 0" header from a memory-mapped
		 * capture, with 16 bytes of zeroed-out padding
		 * at the end.  Byte swap them as if this were
		 * a "version 1" header.
		 */
		offset += 4;			/* skip past interval */
		if (hdr->caplen < offset)
			return;
		uhdr->interval = SWAPLONG(uhdr->interval);

		offset += 4;			/* skip past start_frame */
		if (hdr->caplen < offset)
			return;
		uhdr->start_frame = SWAPLONG(uhdr->start_frame);

		offset += 4;			/* skip past xfer_flags */
		if (hdr->caplen < offset)
			return;
		uhdr->xfer_flags = SWAPLONG(uhdr->xfer_flags);

		offset += 4;			/* skip past ndesc */
		if (hdr->caplen < offset)
			return;
		uhdr->ndesc = SWAPLONG(uhdr->ndesc);

		if (uhdr->transfer_type == URB_ISOCHRONOUS) {
			/* swap the values in struct linux_usb_isodesc */
			usb_isodesc *pisodesc;
			uint32_t i;

			pisodesc = (usb_isodesc *)(void *)(buf+offset);
			for (i = 0; i < uhdr->ndesc; i++) {
				offset += 4;		/* skip past status */
				if (hdr->caplen < offset)
					return;
				pisodesc->status = SWAPLONG(pisodesc->status);

				offset += 4;		/* skip past offset */
				if (hdr->caplen < offset)
					return;
				pisodesc->offset = SWAPLONG(pisodesc->offset);

				offset += 4;		/* skip past len */
				if (hdr->caplen < offset)
					return;
				pisodesc->len = SWAPLONG(pisodesc->len);

				offset += 4;		/* skip past padding */

				pisodesc++;
			}
		}
	}
}

/*
 * The DLT_NFLOG "packets" have a mixture of big-endian and host-byte-order
 * data.  They begin with a fixed-length header with big-endian fields,
 * followed by a set of TLVs, where the type and length are in host
 * byte order but the values are either big-endian or are a raw byte
 * sequence that's the same regardless of the host's byte order.
 *
 * When reading a DLT_NFLOG capture file, we need to convert the type
 * and length values from the byte order of the host that wrote the
 * file to the byte order of this host.
 */
static void
swap_nflog_header(const struct pcap_pkthdr *hdr, u_char *buf)
{
	u_char *p = buf;
	nflog_hdr_t *nfhdr = (nflog_hdr_t *)buf;
	nflog_tlv_t *tlv;
	u_int caplen = hdr->caplen;
	u_int length = hdr->len;
	uint16_t size;

	if (caplen < (u_int) sizeof(nflog_hdr_t) ||
	    length < (u_int) sizeof(nflog_hdr_t)) {
		/* Not enough data to have any TLVs. */
		return;
	}

	if (nfhdr->nflog_version != 0) {
		/* Unknown NFLOG version */
		return;
	}

	length -= sizeof(nflog_hdr_t);
	caplen -= sizeof(nflog_hdr_t);
	p += sizeof(nflog_hdr_t);

	while (caplen >= sizeof(nflog_tlv_t)) {
		tlv = (nflog_tlv_t *) p;

		/* Swap the type and length. */
		tlv->tlv_type = SWAPSHORT(tlv->tlv_type);
		tlv->tlv_length = SWAPSHORT(tlv->tlv_length);

		/* Get the length of the TLV. */
		size = tlv->tlv_length;
		if (size % 4 != 0)
			size += 4 - size % 4;

		/* Is the TLV's length less than the minimum? */
		if (size < sizeof(nflog_tlv_t)) {
			/* Yes. Give up now. */
			return;
		}

		/* Do we have enough data for the full TLV? */
		if (caplen < size || length < size) {
			/* No. */
			return;
		}

		/* Skip over the TLV. */
		length -= size;
		caplen -= size;
		p += size;
	}
}

void
swap_pseudo_headers(int linktype, struct pcap_pkthdr *hdr, u_char *data)
{
	/*
	 * Convert pseudo-headers from the byte order of
	 * the host on which the file was saved to our
	 * byte order, as necessary.
	 */
	switch (linktype) {

	case DLT_LINUX_SLL:
		swap_linux_sll_header(hdr, data);
		break;

	case DLT_USB_LINUX:
		swap_linux_usb_header(hdr, data, 0);
		break;

	case DLT_USB_LINUX_MMAPPED:
		swap_linux_usb_header(hdr, data, 1);
		break;

	case DLT_NFLOG:
		swap_nflog_header(hdr, data);
		break;
	}
}
