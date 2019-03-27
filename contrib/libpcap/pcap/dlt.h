/*-
 * Copyright (c) 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
 *      @(#)bpf.h       7.1 (Berkeley) 5/7/91
 */

#ifndef lib_pcap_dlt_h
#define lib_pcap_dlt_h

/*
 * Link-layer header type codes.
 *
 * Do *NOT* add new values to this list without asking
 * "tcpdump-workers@lists.tcpdump.org" for a value.  Otherwise, you run
 * the risk of using a value that's already being used for some other
 * purpose, and of having tools that read libpcap-format captures not
 * being able to handle captures with your new DLT_ value, with no hope
 * that they will ever be changed to do so (as that would destroy their
 * ability to read captures using that value for that other purpose).
 *
 * See
 *
 *	http://www.tcpdump.org/linktypes.html
 *
 * for detailed descriptions of some of these link-layer header types.
 */

/*
 * These are the types that are the same on all platforms, and that
 * have been defined by <net/bpf.h> for ages.
 */
#define DLT_NULL	0	/* BSD loopback encapsulation */
#define DLT_EN10MB	1	/* Ethernet (10Mb) */
#define DLT_EN3MB	2	/* Experimental Ethernet (3Mb) */
#define DLT_AX25	3	/* Amateur Radio AX.25 */
#define DLT_PRONET	4	/* Proteon ProNET Token Ring */
#define DLT_CHAOS	5	/* Chaos */
#define DLT_IEEE802	6	/* 802.5 Token Ring */
#define DLT_ARCNET	7	/* ARCNET, with BSD-style header */
#define DLT_SLIP	8	/* Serial Line IP */
#define DLT_PPP		9	/* Point-to-point Protocol */
#define DLT_FDDI	10	/* FDDI */

/*
 * These are types that are different on some platforms, and that
 * have been defined by <net/bpf.h> for ages.  We use #ifdefs to
 * detect the BSDs that define them differently from the traditional
 * libpcap <net/bpf.h>
 *
 * XXX - DLT_ATM_RFC1483 is 13 in BSD/OS, and DLT_RAW is 14 in BSD/OS,
 * but I don't know what the right #define is for BSD/OS.
 */
#define DLT_ATM_RFC1483	11	/* LLC-encapsulated ATM */

#ifdef __OpenBSD__
#define DLT_RAW		14	/* raw IP */
#else
#define DLT_RAW		12	/* raw IP */
#endif

/*
 * Given that the only OS that currently generates BSD/OS SLIP or PPP
 * is, well, BSD/OS, arguably everybody should have chosen its values
 * for DLT_SLIP_BSDOS and DLT_PPP_BSDOS, which are 15 and 16, but they
 * didn't.  So it goes.
 */
#if defined(__NetBSD__) || defined(__FreeBSD__)
#ifndef DLT_SLIP_BSDOS
#define DLT_SLIP_BSDOS	13	/* BSD/OS Serial Line IP */
#define DLT_PPP_BSDOS	14	/* BSD/OS Point-to-point Protocol */
#endif
#else
#define DLT_SLIP_BSDOS	15	/* BSD/OS Serial Line IP */
#define DLT_PPP_BSDOS	16	/* BSD/OS Point-to-point Protocol */
#endif

/*
 * 17 was used for DLT_PFLOG in OpenBSD; it no longer is.
 *
 * It was DLT_LANE8023 in SuSE 6.3, so we defined LINKTYPE_PFLOG
 * as 117 so that pflog captures would use a link-layer header type
 * value that didn't collide with any other values.  On all
 * platforms other than OpenBSD, we defined DLT_PFLOG as 117,
 * and we mapped between LINKTYPE_PFLOG and DLT_PFLOG.
 *
 * OpenBSD eventually switched to using 117 for DLT_PFLOG as well.
 *
 * Don't use 17 for anything else.
 */

/*
 * 18 is used for DLT_PFSYNC in OpenBSD, NetBSD, DragonFly BSD and
 * macOS; don't use it for anything else.  (FreeBSD uses 121, which
 * collides with DLT_HHDLC, even though it doesn't use 18 for
 * anything and doesn't appear to have ever used it for anything.)
 *
 * We define it as 18 on those platforms; it is, unfortunately, used
 * for DLT_CIP in Suse 6.3, so we don't define it as DLT_PFSYNC
 * in general.  As the packet format for it, like that for
 * DLT_PFLOG, is not only OS-dependent but OS-version-dependent,
 * we don't support printing it in tcpdump except on OSes that
 * have the relevant header files, so it's not that useful on
 * other platforms.
 */
#if defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
#define DLT_PFSYNC	18
#endif

#define DLT_ATM_CLIP	19	/* Linux Classical-IP over ATM */

/*
 * Apparently Redback uses this for its SmartEdge 400/800.  I hope
 * nobody else decided to use it, too.
 */
#define DLT_REDBACK_SMARTEDGE	32

/*
 * These values are defined by NetBSD; other platforms should refrain from
 * using them for other purposes, so that NetBSD savefiles with link
 * types of 50 or 51 can be read as this type on all platforms.
 */
#define DLT_PPP_SERIAL	50	/* PPP over serial with HDLC encapsulation */
#define DLT_PPP_ETHER	51	/* PPP over Ethernet */

/*
 * The Axent Raptor firewall - now the Symantec Enterprise Firewall - uses
 * a link-layer type of 99 for the tcpdump it supplies.  The link-layer
 * header has 6 bytes of unknown data, something that appears to be an
 * Ethernet type, and 36 bytes that appear to be 0 in at least one capture
 * I've seen.
 */
#define DLT_SYMANTEC_FIREWALL	99

/*
 * Values between 100 and 103 are used in capture file headers as
 * link-layer header type LINKTYPE_ values corresponding to DLT_ types
 * that differ between platforms; don't use those values for new DLT_
 * new types.
 */

/*
 * Values starting with 104 are used for newly-assigned link-layer
 * header type values; for those link-layer header types, the DLT_
 * value returned by pcap_datalink() and passed to pcap_open_dead(),
 * and the LINKTYPE_ value that appears in capture files, are the
 * same.
 *
 * DLT_MATCHING_MIN is the lowest such value; DLT_MATCHING_MAX is
 * the highest such value.
 */
#define DLT_MATCHING_MIN	104

/*
 * This value was defined by libpcap 0.5; platforms that have defined
 * it with a different value should define it here with that value -
 * a link type of 104 in a save file will be mapped to DLT_C_HDLC,
 * whatever value that happens to be, so programs will correctly
 * handle files with that link type regardless of the value of
 * DLT_C_HDLC.
 *
 * The name DLT_C_HDLC was used by BSD/OS; we use that name for source
 * compatibility with programs written for BSD/OS.
 *
 * libpcap 0.5 defined it as DLT_CHDLC; we define DLT_CHDLC as well,
 * for source compatibility with programs written for libpcap 0.5.
 */
#define DLT_C_HDLC	104	/* Cisco HDLC */
#define DLT_CHDLC	DLT_C_HDLC

#define DLT_IEEE802_11	105	/* IEEE 802.11 wireless */

/*
 * 106 is reserved for Linux Classical IP over ATM; it's like DLT_RAW,
 * except when it isn't.  (I.e., sometimes it's just raw IP, and
 * sometimes it isn't.)  We currently handle it as DLT_LINUX_SLL,
 * so that we don't have to worry about the link-layer header.)
 */

/*
 * Frame Relay; BSD/OS has a DLT_FR with a value of 11, but that collides
 * with other values.
 * DLT_FR and DLT_FRELAY packets start with the Q.922 Frame Relay header
 * (DLCI, etc.).
 */
#define DLT_FRELAY	107

/*
 * OpenBSD DLT_LOOP, for loopback devices; it's like DLT_NULL, except
 * that the AF_ type in the link-layer header is in network byte order.
 *
 * DLT_LOOP is 12 in OpenBSD, but that's DLT_RAW in other OSes, so
 * we don't use 12 for it in OSes other than OpenBSD.
 */
#ifdef __OpenBSD__
#define DLT_LOOP	12
#else
#define DLT_LOOP	108
#endif

/*
 * Encapsulated packets for IPsec; DLT_ENC is 13 in OpenBSD, but that's
 * DLT_SLIP_BSDOS in NetBSD, so we don't use 13 for it in OSes other
 * than OpenBSD.
 */
#ifdef __OpenBSD__
#define DLT_ENC		13
#else
#define DLT_ENC		109
#endif

/*
 * Values between 110 and 112 are reserved for use in capture file headers
 * as link-layer types corresponding to DLT_ types that might differ
 * between platforms; don't use those values for new DLT_ types
 * other than the corresponding DLT_ types.
 */

/*
 * This is for Linux cooked sockets.
 */
#define DLT_LINUX_SLL	113

/*
 * Apple LocalTalk hardware.
 */
#define DLT_LTALK	114

/*
 * Acorn Econet.
 */
#define DLT_ECONET	115

/*
 * Reserved for use with OpenBSD ipfilter.
 */
#define DLT_IPFILTER	116

/*
 * OpenBSD DLT_PFLOG.
 */
#define DLT_PFLOG	117

/*
 * Registered for Cisco-internal use.
 */
#define DLT_CISCO_IOS	118

/*
 * For 802.11 cards using the Prism II chips, with a link-layer
 * header including Prism monitor mode information plus an 802.11
 * header.
 */
#define DLT_PRISM_HEADER	119

/*
 * Reserved for Aironet 802.11 cards, with an Aironet link-layer header
 * (see Doug Ambrisko's FreeBSD patches).
 */
#define DLT_AIRONET_HEADER	120

/*
 * Sigh.
 *
 * 121 was reserved for Siemens HiPath HDLC on 2002-01-25, as
 * requested by Tomas Kukosa.
 *
 * On 2004-02-25, a FreeBSD checkin to sys/net/bpf.h was made that
 * assigned 121 as DLT_PFSYNC.  In current versions, its libpcap
 * does DLT_ <-> LINKTYPE_ mapping, mapping DLT_PFSYNC to a
 * LINKTYPE_PFSYNC value of 246, so it should write out DLT_PFSYNC
 * dump files with 246 as the link-layer header type.  (Earlier
 * versions might not have done mapping, in which case they would
 * have written them out with a link-layer header type of 121.)
 *
 * OpenBSD, from which pf came, however, uses 18 for DLT_PFSYNC;
 * its libpcap does no DLT_ <-> LINKTYPE_ mapping, so it would
 * write out DLT_PFSYNC dump files with use 18 as the link-layer
 * header type.
 *
 * NetBSD, DragonFly BSD, and Darwin also use 18 for DLT_PFSYNC; in
 * current versions, their libpcaps do DLT_ <-> LINKTYPE_ mapping,
 * mapping DLT_PFSYNC to a LINKTYPE_PFSYNC value of 246, so they
 * should write out DLT_PFSYNC dump files with 246 as the link-layer
 * header type.  (Earlier versions might not have done mapping,
 * in which case they'd work the same way OpenBSD does, writing
 * them out with a link-layer header type of 18.)
 *
 * We'll define DLT_PFSYNC as:
 *
 *    18 on NetBSD, OpenBSD, DragonFly BSD, and Darwin;
 *
 *    121 on FreeBSD;
 *
 *    246 everywhere else.
 *
 * We'll define DLT_HHDLC as 121 on everything except for FreeBSD;
 * anybody who wants to compile, on FreeBSD, code that uses DLT_HHDLC
 * is out of luck.
 *
 * We'll define LINKTYPE_PFSYNC as 246 on *all* platforms, so that
 * savefiles written using *this* code won't use 18 or 121 for PFSYNC,
 * they'll all use 246.
 *
 * Code that uses pcap_datalink() to determine the link-layer header
 * type of a savefile won't, when built and run on FreeBSD, be able
 * to distinguish between LINKTYPE_PFSYNC and LINKTYPE_HHDLC capture
 * files, as pcap_datalink() will give 121 for both of them.  Code
 * that doesn't, such as the code in Wireshark, will be able to
 * distinguish between them.
 *
 * FreeBSD's libpcap won't map a link-layer header type of 18 - i.e.,
 * DLT_PFSYNC files from OpenBSD and possibly older versions of NetBSD,
 * DragonFly BSD, and macOS - to DLT_PFSYNC, so code built with FreeBSD's
 * libpcap won't treat those files as DLT_PFSYNC files.
 *
 * Other libpcaps won't map a link-layer header type of 121 to DLT_PFSYNC;
 * this means they can read DLT_HHDLC files, if any exist, but won't
 * treat pcap files written by any older versions of FreeBSD libpcap that
 * didn't map to 246 as DLT_PFSYNC files.
 */
#ifdef __FreeBSD__
#define DLT_PFSYNC		121
#else
#define DLT_HHDLC		121
#endif

/*
 * This is for RFC 2625 IP-over-Fibre Channel.
 *
 * This is not for use with raw Fibre Channel, where the link-layer
 * header starts with a Fibre Channel frame header; it's for IP-over-FC,
 * where the link-layer header starts with an RFC 2625 Network_Header
 * field.
 */
#define DLT_IP_OVER_FC		122

/*
 * This is for Full Frontal ATM on Solaris with SunATM, with a
 * pseudo-header followed by an AALn PDU.
 *
 * There may be other forms of Full Frontal ATM on other OSes,
 * with different pseudo-headers.
 *
 * If ATM software returns a pseudo-header with VPI/VCI information
 * (and, ideally, packet type information, e.g. signalling, ILMI,
 * LANE, LLC-multiplexed traffic, etc.), it should not use
 * DLT_ATM_RFC1483, but should get a new DLT_ value, so tcpdump
 * and the like don't have to infer the presence or absence of a
 * pseudo-header and the form of the pseudo-header.
 */
#define DLT_SUNATM		123	/* Solaris+SunATM */

/*
 * Reserved as per request from Kent Dahlgren <kent@praesum.com>
 * for private use.
 */
#define DLT_RIO                 124     /* RapidIO */
#define DLT_PCI_EXP             125     /* PCI Express */
#define DLT_AURORA              126     /* Xilinx Aurora link layer */

/*
 * Header for 802.11 plus a number of bits of link-layer information
 * including radio information, used by some recent BSD drivers as
 * well as the madwifi Atheros driver for Linux.
 */
#define DLT_IEEE802_11_RADIO	127	/* 802.11 plus radiotap radio header */

/*
 * Reserved for the TZSP encapsulation, as per request from
 * Chris Waters <chris.waters@networkchemistry.com>
 * TZSP is a generic encapsulation for any other link type,
 * which includes a means to include meta-information
 * with the packet, e.g. signal strength and channel
 * for 802.11 packets.
 */
#define DLT_TZSP                128     /* Tazmen Sniffer Protocol */

/*
 * BSD's ARCNET headers have the source host, destination host,
 * and type at the beginning of the packet; that's what's handed
 * up to userland via BPF.
 *
 * Linux's ARCNET headers, however, have a 2-byte offset field
 * between the host IDs and the type; that's what's handed up
 * to userland via PF_PACKET sockets.
 *
 * We therefore have to have separate DLT_ values for them.
 */
#define DLT_ARCNET_LINUX	129	/* ARCNET */

/*
 * Juniper-private data link types, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_s are used
 * for passing on chassis-internal metainformation such as
 * QOS profiles, etc..
 */
#define DLT_JUNIPER_MLPPP       130
#define DLT_JUNIPER_MLFR        131
#define DLT_JUNIPER_ES          132
#define DLT_JUNIPER_GGSN        133
#define DLT_JUNIPER_MFR         134
#define DLT_JUNIPER_ATM2        135
#define DLT_JUNIPER_SERVICES    136
#define DLT_JUNIPER_ATM1        137

/*
 * Apple IP-over-IEEE 1394, as per a request from Dieter Siegmund
 * <dieter@apple.com>.  The header that's presented is an Ethernet-like
 * header:
 *
 *	#define FIREWIRE_EUI64_LEN	8
 *	struct firewire_header {
 *		u_char  firewire_dhost[FIREWIRE_EUI64_LEN];
 *		u_char  firewire_shost[FIREWIRE_EUI64_LEN];
 *		u_short firewire_type;
 *	};
 *
 * with "firewire_type" being an Ethernet type value, rather than,
 * for example, raw GASP frames being handed up.
 */
#define DLT_APPLE_IP_OVER_IEEE1394	138

/*
 * Various SS7 encapsulations, as per a request from Jeff Morriss
 * <jeff.morriss[AT]ulticom.com> and subsequent discussions.
 */
#define DLT_MTP2_WITH_PHDR	139	/* pseudo-header with various info, followed by MTP2 */
#define DLT_MTP2		140	/* MTP2, without pseudo-header */
#define DLT_MTP3		141	/* MTP3, without pseudo-header or MTP2 */
#define DLT_SCCP		142	/* SCCP, without pseudo-header or MTP2 or MTP3 */

/*
 * DOCSIS MAC frames.
 */
#define DLT_DOCSIS		143

/*
 * Linux-IrDA packets. Protocol defined at http://www.irda.org.
 * Those packets include IrLAP headers and above (IrLMP...), but
 * don't include Phy framing (SOF/EOF/CRC & byte stuffing), because Phy
 * framing can be handled by the hardware and depend on the bitrate.
 * This is exactly the format you would get capturing on a Linux-IrDA
 * interface (irdaX), but not on a raw serial port.
 * Note the capture is done in "Linux-cooked" mode, so each packet include
 * a fake packet header (struct sll_header). This is because IrDA packet
 * decoding is dependant on the direction of the packet (incomming or
 * outgoing).
 * When/if other platform implement IrDA capture, we may revisit the
 * issue and define a real DLT_IRDA...
 * Jean II
 */
#define DLT_LINUX_IRDA		144

/*
 * Reserved for IBM SP switch and IBM Next Federation switch.
 */
#define DLT_IBM_SP		145
#define DLT_IBM_SN		146

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
 * monitoring/etc. appliance, or whatever product uses that DLT_ value,
 * and you may also find that the developers of those applications will
 * not accept patches to let them read those files.
 *
 * Also, do not use them if somebody might send you a capture using them
 * for *their* private type and tools using them for *your* private type
 * would have to read them.
 *
 * Instead, ask "tcpdump-workers@lists.tcpdump.org" for a new DLT_ value,
 * as per the comment above, and use the type you're given.
 */
#define DLT_USER0		147
#define DLT_USER1		148
#define DLT_USER2		149
#define DLT_USER3		150
#define DLT_USER4		151
#define DLT_USER5		152
#define DLT_USER6		153
#define DLT_USER7		154
#define DLT_USER8		155
#define DLT_USER9		156
#define DLT_USER10		157
#define DLT_USER11		158
#define DLT_USER12		159
#define DLT_USER13		160
#define DLT_USER14		161
#define DLT_USER15		162

/*
 * For future use with 802.11 captures - defined by AbsoluteValue
 * Systems to store a number of bits of link-layer information
 * including radio information:
 *
 *	http://www.shaftnet.org/~pizza/software/capturefrm.txt
 *
 * but it might be used by some non-AVS drivers now or in the
 * future.
 */
#define DLT_IEEE802_11_RADIO_AVS 163	/* 802.11 plus AVS radio header */

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_s are used
 * for passing on chassis-internal metainformation such as
 * QOS profiles, etc..
 */
#define DLT_JUNIPER_MONITOR     164

/*
 * BACnet MS/TP frames.
 */
#define DLT_BACNET_MS_TP	165

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
#define DLT_PPP_PPPD		166

/*
 * Names for backwards compatibility with older versions of some PPP
 * software; new software should use DLT_PPP_PPPD.
 */
#define DLT_PPP_WITH_DIRECTION	DLT_PPP_PPPD
#define DLT_LINUX_PPP_WITHDIRECTION	DLT_PPP_PPPD

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_s are used
 * for passing on chassis-internal metainformation such as
 * QOS profiles, cookies, etc..
 */
#define DLT_JUNIPER_PPPOE       167
#define DLT_JUNIPER_PPPOE_ATM   168

#define DLT_GPRS_LLC		169	/* GPRS LLC */
#define DLT_GPF_T		170	/* GPF-T (ITU-T G.7041/Y.1303) */
#define DLT_GPF_F		171	/* GPF-F (ITU-T G.7041/Y.1303) */

/*
 * Requested by Oolan Zimmer <oz@gcom.com> for use in Gcom's T1/E1 line
 * monitoring equipment.
 */
#define DLT_GCOM_T1E1		172
#define DLT_GCOM_SERIAL		173

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.  The DLT_ is used
 * for internal communication to Physical Interface Cards (PIC)
 */
#define DLT_JUNIPER_PIC_PEER    174

/*
 * Link types requested by Gregor Maier <gregor@endace.com> of Endace
 * Measurement Systems.  They add an ERF header (see
 * http://www.endace.com/support/EndaceRecordFormat.pdf) in front of
 * the link-layer header.
 */
#define DLT_ERF_ETH		175	/* Ethernet */
#define DLT_ERF_POS		176	/* Packet-over-SONET */

/*
 * Requested by Daniele Orlandi <daniele@orlandi.com> for raw LAPD
 * for vISDN (http://www.orlandi.com/visdn/).  Its link-layer header
 * includes additional information before the LAPD header, so it's
 * not necessarily a generic LAPD header.
 */
#define DLT_LINUX_LAPD		177

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ are used for prepending meta-information
 * like interface index, interface name
 * before standard Ethernet, PPP, Frelay & C-HDLC Frames
 */
#define DLT_JUNIPER_ETHER       178
#define DLT_JUNIPER_PPP         179
#define DLT_JUNIPER_FRELAY      180
#define DLT_JUNIPER_CHDLC       181

/*
 * Multi Link Frame Relay (FRF.16)
 */
#define DLT_MFR                 182

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for internal communication with a
 * voice Adapter Card (PIC)
 */
#define DLT_JUNIPER_VP          183

/*
 * Arinc 429 frames.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Every frame contains a 32bit A429 label.
 * More documentation on Arinc 429 can be found at
 * http://www.condoreng.com/support/downloads/tutorials/ARINCTutorial.pdf
 */
#define DLT_A429                184

/*
 * Arinc 653 Interpartition Communication messages.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Please refer to the A653-1 standard for more information.
 */
#define DLT_A653_ICM            185

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
 *
 * For source-code compatibility, we also define DLT_USB to have this
 * value.  We do it numerically so that, if code that includes this
 * file (directly or indirectly) also includes an OS header that also
 * defines DLT_USB as 186, we don't get a redefinition warning.
 * (NetBSD 7 does that.)
 */
#define DLT_USB_FREEBSD		186
#define DLT_USB			186

/*
 * Bluetooth HCI UART transport layer (part H:4); requested by
 * Paolo Abeni.
 */
#define DLT_BLUETOOTH_HCI_H4	187

/*
 * IEEE 802.16 MAC Common Part Sublayer; requested by Maria Cruz
 * <cruz_petagay@bah.com>.
 */
#define DLT_IEEE802_16_MAC_CPS	188

/*
 * USB packets, beginning with a Linux USB header; requested by
 * Paolo Abeni <paolo.abeni@email.it>.
 */
#define DLT_USB_LINUX		189

/*
 * Controller Area Network (CAN) v. 2.0B packets.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 * Used to dump CAN packets coming from a CAN Vector board.
 * More documentation on the CAN v2.0B frames can be found at
 * http://www.can-cia.org/downloads/?269
 */
#define DLT_CAN20B              190

/*
 * IEEE 802.15.4, with address fields padded, as is done by Linux
 * drivers; requested by Juergen Schimmer.
 */
#define DLT_IEEE802_15_4_LINUX	191

/*
 * Per Packet Information encapsulated packets.
 * DLT_ requested by Gianluca Varenni <gianluca.varenni@cacetech.com>.
 */
#define DLT_PPI			192

/*
 * Header for 802.16 MAC Common Part Sublayer plus a radiotap radio header;
 * requested by Charles Clancy.
 */
#define DLT_IEEE802_16_MAC_CPS_RADIO	193

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for internal communication with a
 * integrated service module (ISM).
 */
#define DLT_JUNIPER_ISM         194

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing); requested by Mikko Saarnivala <mikko.saarnivala@sensinode.com>.
 * For this one, we expect the FCS to be present at the end of the frame;
 * if the frame has no FCS, DLT_IEEE802_15_4_NOFCS should be used.
 *
 * We keep the name DLT_IEEE802_15_4 as an alias for backwards
 * compatibility, but, again, this should *only* be used for 802.15.4
 * frames that include the FCS.
 */
#define DLT_IEEE802_15_4_WITHFCS	195
#define DLT_IEEE802_15_4		DLT_IEEE802_15_4_WITHFCS

/*
 * Various link-layer types, with a pseudo-header, for SITA
 * (http://www.sita.aero/); requested by Fulko Hew (fulko.hew@gmail.com).
 */
#define DLT_SITA		196

/*
 * Various link-layer types, with a pseudo-header, for Endace DAG cards;
 * encapsulates Endace ERF records.  Requested by Stephen Donnelly
 * <stephen@endace.com>.
 */
#define DLT_ERF			197

/*
 * Special header prepended to Ethernet packets when capturing from a
 * u10 Networks board.  Requested by Phil Mulholland
 * <phil@u10networks.com>.
 */
#define DLT_RAIF1		198

/*
 * IPMB packet for IPMI, beginning with the I2C slave address, followed
 * by the netFn and LUN, etc..  Requested by Chanthy Toeung
 * <chanthy.toeung@ca.kontron.com>.
 */
#define DLT_IPMB		199

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 * The DLT_ is used for capturing data on a secure tunnel interface.
 */
#define DLT_JUNIPER_ST          200

/*
 * Bluetooth HCI UART transport layer (part H:4), with pseudo-header
 * that includes direction information; requested by Paolo Abeni.
 */
#define DLT_BLUETOOTH_HCI_H4_WITH_PHDR	201

/*
 * AX.25 packet with a 1-byte KISS header; see
 *
 *	http://www.ax25.net/kiss.htm
 *
 * as per Richard Stearn <richard@rns-stearn.demon.co.uk>.
 */
#define DLT_AX25_KISS		202

/*
 * LAPD packets from an ISDN channel, starting with the address field,
 * with no pseudo-header.
 * Requested by Varuna De Silva <varunax@gmail.com>.
 */
#define DLT_LAPD		203

/*
 * Variants of various link-layer headers, with a one-byte direction
 * pseudo-header prepended - zero means "received by this host",
 * non-zero (any non-zero value) means "sent by this host" - as per
 * Will Barker <w.barker@zen.co.uk>.
 */
#define DLT_PPP_WITH_DIR	204	/* PPP - don't confuse with DLT_PPP_WITH_DIRECTION */
#define DLT_C_HDLC_WITH_DIR	205	/* Cisco HDLC */
#define DLT_FRELAY_WITH_DIR	206	/* Frame Relay */
#define DLT_LAPB_WITH_DIR	207	/* LAPB */

/*
 * 208 is reserved for an as-yet-unspecified proprietary link-layer
 * type, as requested by Will Barker.
 */

/*
 * IPMB with a Linux-specific pseudo-header; as requested by Alexey Neyman
 * <avn@pigeonpoint.com>.
 */
#define DLT_IPMB_LINUX		209

/*
 * FlexRay automotive bus - http://www.flexray.com/ - as requested
 * by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define DLT_FLEXRAY		210

/*
 * Media Oriented Systems Transport (MOST) bus for multimedia
 * transport - http://www.mostcooperation.com/ - as requested
 * by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define DLT_MOST		211

/*
 * Local Interconnect Network (LIN) bus for vehicle networks -
 * http://www.lin-subbus.org/ - as requested by Hannes Kaelber
 * <hannes.kaelber@x2e.de>.
 */
#define DLT_LIN			212

/*
 * X2E-private data link type used for serial line capture,
 * as requested by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define DLT_X2E_SERIAL		213

/*
 * X2E-private data link type used for the Xoraya data logger
 * family, as requested by Hannes Kaelber <hannes.kaelber@x2e.de>.
 */
#define DLT_X2E_XORAYA		214

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), but with the PHY-level data for non-ASK PHYs (4 octets
 * of 0 as preamble, one octet of SFD, one octet of frame length+
 * reserved bit, and then the MAC-layer data, starting with the
 * frame control field).
 *
 * Requested by Max Filippov <jcmvbkbc@gmail.com>.
 */
#define DLT_IEEE802_15_4_NONASK_PHY	215

/*
 * David Gibson <david@gibson.dropbear.id.au> requested this for
 * captures from the Linux kernel /dev/input/eventN devices. This
 * is used to communicate keystrokes and mouse movements from the
 * Linux kernel to display systems, such as Xorg.
 */
#define DLT_LINUX_EVDEV		216

/*
 * GSM Um and Abis interfaces, preceded by a "gsmtap" header.
 *
 * Requested by Harald Welte <laforge@gnumonks.org>.
 */
#define DLT_GSMTAP_UM		217
#define DLT_GSMTAP_ABIS		218

/*
 * MPLS, with an MPLS label as the link-layer header.
 * Requested by Michele Marchetto <michele@openbsd.org> on behalf
 * of OpenBSD.
 */
#define DLT_MPLS		219

/*
 * USB packets, beginning with a Linux USB header, with the USB header
 * padded to 64 bytes; required for memory-mapped access.
 */
#define DLT_USB_LINUX_MMAPPED	220

/*
 * DECT packets, with a pseudo-header; requested by
 * Matthias Wenzel <tcpdump@mazzoo.de>.
 */
#define DLT_DECT		221

/*
 * From: "Lidwa, Eric (GSFC-582.0)[SGT INC]" <eric.lidwa-1@nasa.gov>
 * Date: Mon, 11 May 2009 11:18:30 -0500
 *
 * DLT_AOS. We need it for AOS Space Data Link Protocol.
 *   I have already written dissectors for but need an OK from
 *   legal before I can submit a patch.
 *
 */
#define DLT_AOS                 222

/*
 * Wireless HART (Highway Addressable Remote Transducer)
 * From the HART Communication Foundation
 * IES/PAS 62591
 *
 * Requested by Sam Roberts <vieuxtech@gmail.com>.
 */
#define DLT_WIHART		223

/*
 * Fibre Channel FC-2 frames, beginning with a Frame_Header.
 * Requested by Kahou Lei <kahou82@gmail.com>.
 */
#define DLT_FC_2		224

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
#define DLT_FC_2_WITH_FRAME_DELIMS	225

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
#define DLT_IPNET		226

/*
 * CAN (Controller Area Network) frames, with a pseudo-header as supplied
 * by Linux SocketCAN, and with multi-byte numerical fields in that header
 * in big-endian byte order.
 *
 * See Documentation/networking/can.txt in the Linux source.
 *
 * Requested by Felix Obenhuber <felix@obenhuber.de>.
 */
#define DLT_CAN_SOCKETCAN	227

/*
 * Raw IPv4/IPv6; different from DLT_RAW in that the DLT_ value specifies
 * whether it's v4 or v6.  Requested by Darren Reed <Darren.Reed@Sun.COM>.
 */
#define DLT_IPV4		228
#define DLT_IPV6		229

/*
 * IEEE 802.15.4, exactly as it appears in the spec (no padding, no
 * nothing), and with no FCS at the end of the frame; requested by
 * Jon Smirl <jonsmirl@gmail.com>.
 */
#define DLT_IEEE802_15_4_NOFCS	230

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
#define DLT_DBUS		231

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define DLT_JUNIPER_VS			232
#define DLT_JUNIPER_SRX_E2E		233
#define DLT_JUNIPER_FIBRECHANNEL	234

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
#define DLT_DVB_CI		235

/*
 * Variant of 3GPP TS 27.010 multiplexing protocol (similar to, but
 * *not* the same as, 27.010).  Requested by Hans-Christoph Schemmel
 * <hans-christoph.schemmel@cinterion.com>.
 */
#define DLT_MUX27010		236

/*
 * STANAG 5066 D_PDUs.  Requested by M. Baris Demiray
 * <barisdemiray@gmail.com>.
 */
#define DLT_STANAG_5066_D_PDU	237

/*
 * Juniper-private data link type, as per request from
 * Hannes Gredler <hannes@juniper.net>.
 */
#define DLT_JUNIPER_ATM_CEMIC	238

/*
 * NetFilter LOG messages
 * (payload of netlink NFNL_SUBSYS_ULOG/NFULNL_MSG_PACKET packets)
 *
 * Requested by Jakub Zawadzki <darkjames-ws@darkjames.pl>
 */
#define DLT_NFLOG		239

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and always
 * with the payload including the FCS, as supplied by their
 * netANALYZER hardware and software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define DLT_NETANALYZER		240

/*
 * Hilscher Gesellschaft fuer Systemautomation mbH link-layer type
 * for Ethernet packets with a 4-byte pseudo-header and FCS and
 * with the Ethernet header preceded by 7 bytes of preamble and
 * 1 byte of SFD, as supplied by their netANALYZER hardware and
 * software.
 *
 * Requested by Holger P. Frommer <HPfrommer@hilscher.com>
 */
#define DLT_NETANALYZER_TRANSPARENT	241

/*
 * IP-over-InfiniBand, as specified by RFC 4391.
 *
 * Requested by Petr Sumbera <petr.sumbera@oracle.com>.
 */
#define DLT_IPOIB		242

/*
 * MPEG-2 transport stream (ISO 13818-1/ITU-T H.222.0).
 *
 * Requested by Guy Martin <gmsoft@tuxicoman.be>.
 */
#define DLT_MPEG_2_TS		243

/*
 * ng4T GmbH's UMTS Iub/Iur-over-ATM and Iub/Iur-over-IP format as
 * used by their ng40 protocol tester.
 *
 * Requested by Jens Grimmer <jens.grimmer@ng4t.com>.
 */
#define DLT_NG40		244

/*
 * Pseudo-header giving adapter number and flags, followed by an NFC
 * (Near-Field Communications) Logical Link Control Protocol (LLCP) PDU,
 * as specified by NFC Forum Logical Link Control Protocol Technical
 * Specification LLCP 1.1.
 *
 * Requested by Mike Wakerly <mikey@google.com>.
 */
#define DLT_NFC_LLCP		245

/*
 * 246 is used as LINKTYPE_PFSYNC; do not use it for any other purpose.
 *
 * DLT_PFSYNC has different values on different platforms, and all of
 * them collide with something used elsewhere.  On platforms that
 * don't already define it, define it as 246.
 */
#if !defined(__FreeBSD__) && !defined(__OpenBSD__) && !defined(__NetBSD__) && !defined(__DragonFly__) && !defined(__APPLE__)
#define DLT_PFSYNC		246
#endif

/*
 * Raw InfiniBand packets, starting with the Local Routing Header.
 *
 * Requested by Oren Kladnitsky <orenk@mellanox.com>.
 */
#define DLT_INFINIBAND		247

/*
 * SCTP, with no lower-level protocols (i.e., no IPv4 or IPv6).
 *
 * Requested by Michael Tuexen <Michael.Tuexen@lurchi.franken.de>.
 */
#define DLT_SCTP		248

/*
 * USB packets, beginning with a USBPcap header.
 *
 * Requested by Tomasz Mon <desowin@gmail.com>
 */
#define DLT_USBPCAP		249

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
#define DLT_BLUETOOTH_LE_LL	251

/*
 * DLT type for upper-protocol layer PDU saves from wireshark.
 *
 * the actual contents are determined by two TAGs stored with each
 * packet:
 *   EXP_PDU_TAG_LINKTYPE          the link type (LINKTYPE_ value) of the
 *				   original packet.
 *
 *   EXP_PDU_TAG_PROTO_NAME        the name of the wireshark dissector
 * 				   that can make sense of the data stored.
 */
#define DLT_WIRESHARK_UPPER_PDU	252

/*
 * DLT type for the netlink protocol (nlmon devices).
 */
#define DLT_NETLINK		253

/*
 * Bluetooth Linux Monitor headers for the BlueZ stack.
 */
#define DLT_BLUETOOTH_LINUX_MONITOR	254

/*
 * Bluetooth Basic Rate/Enhanced Data Rate baseband packets, as
 * captured by Ubertooth.
 */
#define DLT_BLUETOOTH_BREDR_BB	255

/*
 * Bluetooth Low Energy link layer packets, as captured by Ubertooth.
 */
#define DLT_BLUETOOTH_LE_LL_WITH_PHDR	256

/*
 * PROFIBUS data link layer.
 */
#define DLT_PROFIBUS_DL		257

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
 *
 * When capturing, on a system with a Darwin-based OS, on a device
 * that returns 149 (DLT_USER2 and Apple's DLT_PKTAP) with this
 * version of libpcap, the DLT_ value for the pcap_t  will be DLT_PKTAP,
 * and that will continue to be DLT_USER2 on Darwin-based OSes. That way,
 * binary compatibility with Mavericks is preserved for programs using
 * this version of libpcap.  This does mean that if you were using
 * DLT_USER2 for some capture device on macOS, you can't do so with
 * this version of libpcap, just as you can't with Apple's libpcap -
 * on macOS, they define DLT_PKTAP to be DLT_USER2, so programs won't
 * be able to distinguish between PKTAP and whatever you were using
 * DLT_USER2 for.
 *
 * If the program saves the capture to a file using this version of
 * libpcap's pcap_dump code, the LINKTYPE_ value in the file will be
 * LINKTYPE_PKTAP, which will be 258, even on Darwin-based OSes.
 * That way, the file will *not* be a DLT_USER2 file.  That means
 * that the latest version of tcpdump, when built with this version
 * of libpcap, and sufficiently recent versions of Wireshark will
 * be able to read those files and interpret them correctly; however,
 * Apple's version of tcpdump in OS X 10.9 won't be able to handle
 * them.  (Hopefully, Apple will pick up this version of libpcap,
 * and the corresponding version of tcpdump, so that tcpdump will
 * be able to handle the old LINKTYPE_USER2 captures *and* the new
 * LINKTYPE_PKTAP captures.)
 */
#ifdef __APPLE__
#define DLT_PKTAP	DLT_USER2
#else
#define DLT_PKTAP	258
#endif

/*
 * Ethernet packets preceded by a header giving the last 6 octets
 * of the preamble specified by 802.3-2012 Clause 65, section
 * 65.1.3.2 "Transmit".
 */
#define DLT_EPON	259

/*
 * IPMI trace packets, as specified by Table 3-20 "Trace Data Block Format"
 * in the PICMG HPM.2 specification.
 */
#define DLT_IPMI_HPM_2	260

/*
 * per  Joshua Wright <jwright@hasborg.com>, formats for Zwave captures.
 */
#define DLT_ZWAVE_R1_R2  261
#define DLT_ZWAVE_R3     262

/*
 * per Steve Karg <skarg@users.sourceforge.net>, formats for Wattstopper
 * Digital Lighting Management room bus serial protocol captures.
 */
#define DLT_WATTSTOPPER_DLM     263

/*
 * ISO 14443 contactless smart card messages.
 */
#define DLT_ISO_14443	264

/*
 * Radio data system (RDS) groups.  IEC 62106.
 * Per Jonathan Brucker <jonathan.brucke@gmail.com>.
 */
#define DLT_RDS		265

/*
 * USB packets, beginning with a Darwin (macOS, etc.) header.
 */
#define DLT_USB_DARWIN	266

/*
 * OpenBSD DLT_OPENFLOW.
 */
#define DLT_OPENFLOW	267

/*
 * SDLC frames containing SNA PDUs.
 */
#define DLT_SDLC	268

/*
 * per "Selvig, Bjorn" <b.selvig@ti.com> used for
 * TI protocol sniffer.
 */
#define DLT_TI_LLN_SNIFFER	269

/*
 * per: Erik de Jong <erikdejong at gmail.com> for
 *   https://github.com/eriknl/LoRaTap/releases/tag/v0.1
 */
#define DLT_LORATAP             270

/*
 * per: Stefanha at gmail.com for
 *   http://lists.sandelman.ca/pipermail/tcpdump-workers/2017-May/000772.html
 * and: https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/vsockmon.h
 * for: http://qemu-project.org/Features/VirtioVsock
 */
#define DLT_VSOCK               271

/*
 * Nordic Semiconductor Bluetooth LE sniffer.
 */
#define DLT_NORDIC_BLE		272

/*
 * Excentis DOCSIS 3.1 RF sniffer (XRA-31)
 *   per: bruno.verstuyft at excentis.com
 *        http://www.xra31.com/xra-header
 */
#define DLT_DOCSIS31_XRA31	273

/*
 * mPackets, as specified by IEEE 802.3br Figure 99-4, starting
 * with the preamble and always ending with a CRC field.
 */
#define DLT_ETHERNET_MPACKET	274

/*
 * DisplayPort AUX channel monitoring data as specified by VESA
 * DisplayPort(DP) Standard preceeded by a pseudo-header.
 *    per dirk.eibach at gdsys.cc
 */
#define DLT_DISPLAYPORT_AUX	275

/*
 * In case the code that includes this file (directly or indirectly)
 * has also included OS files that happen to define DLT_MATCHING_MAX,
 * with a different value (perhaps because that OS hasn't picked up
 * the latest version of our DLT definitions), we undefine the
 * previous value of DLT_MATCHING_MAX.
 */
#ifdef DLT_MATCHING_MAX
#undef DLT_MATCHING_MAX
#endif
#define DLT_MATCHING_MAX	275	/* highest value in the "matching" range */

/*
 * DLT and savefile link type values are split into a class and
 * a member of that class.  A class value of 0 indicates a regular
 * DLT_/LINKTYPE_ value.
 */
#define DLT_CLASS(x)		((x) & 0x03ff0000)

/*
 * NetBSD-specific generic "raw" link type.  The class value indicates
 * that this is the generic raw type, and the lower 16 bits are the
 * address family we're dealing with.  Those values are NetBSD-specific;
 * do not assume that they correspond to AF_ values for your operating
 * system.
 */
#define	DLT_CLASS_NETBSD_RAWAF	0x02240000
#define	DLT_NETBSD_RAWAF(af)	(DLT_CLASS_NETBSD_RAWAF | (af))
#define	DLT_NETBSD_RAWAF_AF(x)	((x) & 0x0000ffff)
#define	DLT_IS_NETBSD_RAWAF(x)	(DLT_CLASS(x) == DLT_CLASS_NETBSD_RAWAF)

#endif /* !defined(lib_pcap_dlt_h) */
