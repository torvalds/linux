/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000
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
 * Support for splitting captures into multiple files with a maximum
 * file size:
 *
 * Copyright (c) 2001
 *	Seth Webster <swebster@sst.ll.mit.edu>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "print.h"

struct printer {
	if_printer f;
	int type;
};

static const struct printer printers[] = {
	{ ether_if_print,	DLT_EN10MB },
#ifdef DLT_IPNET
	{ ipnet_if_print,	DLT_IPNET },
#endif
#ifdef DLT_IEEE802_15_4
	{ ieee802_15_4_if_print, DLT_IEEE802_15_4 },
#endif
#ifdef DLT_IEEE802_15_4_NOFCS
	{ ieee802_15_4_if_print, DLT_IEEE802_15_4_NOFCS },
#endif
#ifdef DLT_PPI
	{ ppi_if_print,		DLT_PPI },
#endif
#ifdef DLT_NETANALYZER
	{ netanalyzer_if_print, DLT_NETANALYZER },
#endif
#ifdef DLT_NETANALYZER_TRANSPARENT
	{ netanalyzer_transparent_if_print, DLT_NETANALYZER_TRANSPARENT },
#endif
#if defined(DLT_NFLOG) && defined(HAVE_PCAP_NFLOG_H)
	{ nflog_if_print,	DLT_NFLOG},
#endif
#ifdef DLT_CIP
	{ cip_if_print,         DLT_CIP },
#endif
#ifdef DLT_ATM_CLIP
	{ cip_if_print,		DLT_ATM_CLIP },
#endif
#ifdef DLT_IP_OVER_FC
	{ ipfc_if_print,	DLT_IP_OVER_FC },
#endif
	{ null_if_print,	DLT_NULL },
#ifdef DLT_LOOP
	{ null_if_print,	DLT_LOOP },
#endif
#ifdef DLT_APPLE_IP_OVER_IEEE1394
	{ ap1394_if_print,	DLT_APPLE_IP_OVER_IEEE1394 },
#endif
#if defined(DLT_BLUETOOTH_HCI_H4_WITH_PHDR) && defined(HAVE_PCAP_BLUETOOTH_H)
	{ bt_if_print,		DLT_BLUETOOTH_HCI_H4_WITH_PHDR},
#endif
#ifdef DLT_LANE8023
	{ lane_if_print,        DLT_LANE8023 },
#endif
	{ arcnet_if_print,	DLT_ARCNET },
#ifdef DLT_ARCNET_LINUX
	{ arcnet_linux_if_print, DLT_ARCNET_LINUX },
#endif
	{ raw_if_print,		DLT_RAW },
#ifdef DLT_IPV4
	{ raw_if_print,		DLT_IPV4 },
#endif
#ifdef DLT_IPV6
	{ raw_if_print,		DLT_IPV6 },
#endif
#ifdef HAVE_PCAP_USB_H
#ifdef DLT_USB_LINUX
	{ usb_linux_48_byte_print, DLT_USB_LINUX},
#endif /* DLT_USB_LINUX */
#ifdef DLT_USB_LINUX_MMAPPED
	{ usb_linux_64_byte_print, DLT_USB_LINUX_MMAPPED},
#endif /* DLT_USB_LINUX_MMAPPED */
#endif /* HAVE_PCAP_USB_H */
#ifdef DLT_SYMANTEC_FIREWALL
	{ symantec_if_print,	DLT_SYMANTEC_FIREWALL },
#endif
#ifdef DLT_C_HDLC
	{ chdlc_if_print,	DLT_C_HDLC },
#endif
#ifdef DLT_HDLC
	{ chdlc_if_print,	DLT_HDLC },
#endif
#ifdef DLT_PPP_ETHER
	{ pppoe_if_print,	DLT_PPP_ETHER },
#endif
#if defined(DLT_PFLOG) && defined(HAVE_NET_IF_PFLOG_H)
	{ pflog_if_print,	DLT_PFLOG },
#endif
	{ token_if_print,	DLT_IEEE802 },
	{ fddi_if_print,	DLT_FDDI },
#ifdef DLT_LINUX_SLL
	{ sll_if_print,		DLT_LINUX_SLL },
#endif
#ifdef DLT_FR
	{ fr_if_print,		DLT_FR },
#endif
#ifdef DLT_FRELAY
	{ fr_if_print,		DLT_FRELAY },
#endif
#ifdef DLT_MFR
	{ mfr_if_print,		DLT_MFR },
#endif
	{ atm_if_print,		DLT_ATM_RFC1483 },
#ifdef DLT_SUNATM
	{ sunatm_if_print,	DLT_SUNATM },
#endif
#ifdef DLT_ENC
	{ enc_if_print,		DLT_ENC },
#endif
	{ sl_if_print,		DLT_SLIP },
#ifdef DLT_SLIP_BSDOS
	{ sl_bsdos_if_print,	DLT_SLIP_BSDOS },
#endif
#ifdef DLT_LTALK
	{ ltalk_if_print,	DLT_LTALK },
#endif
#ifdef DLT_JUNIPER_ATM1
	{ juniper_atm1_print,	DLT_JUNIPER_ATM1 },
#endif
#ifdef DLT_JUNIPER_ATM2
	{ juniper_atm2_print,	DLT_JUNIPER_ATM2 },
#endif
#ifdef DLT_JUNIPER_MFR
	{ juniper_mfr_print,	DLT_JUNIPER_MFR },
#endif
#ifdef DLT_JUNIPER_MLFR
	{ juniper_mlfr_print,	DLT_JUNIPER_MLFR },
#endif
#ifdef DLT_JUNIPER_MLPPP
	{ juniper_mlppp_print,	DLT_JUNIPER_MLPPP },
#endif
#ifdef DLT_JUNIPER_PPPOE
	{ juniper_pppoe_print,	DLT_JUNIPER_PPPOE },
#endif
#ifdef DLT_JUNIPER_PPPOE_ATM
	{ juniper_pppoe_atm_print, DLT_JUNIPER_PPPOE_ATM },
#endif
#ifdef DLT_JUNIPER_GGSN
	{ juniper_ggsn_print,	DLT_JUNIPER_GGSN },
#endif
#ifdef DLT_JUNIPER_ES
	{ juniper_es_print,	DLT_JUNIPER_ES },
#endif
#ifdef DLT_JUNIPER_MONITOR
	{ juniper_monitor_print, DLT_JUNIPER_MONITOR },
#endif
#ifdef DLT_JUNIPER_SERVICES
	{ juniper_services_print, DLT_JUNIPER_SERVICES },
#endif
#ifdef DLT_JUNIPER_ETHER
	{ juniper_ether_print,	DLT_JUNIPER_ETHER },
#endif
#ifdef DLT_JUNIPER_PPP
	{ juniper_ppp_print,	DLT_JUNIPER_PPP },
#endif
#ifdef DLT_JUNIPER_FRELAY
	{ juniper_frelay_print,	DLT_JUNIPER_FRELAY },
#endif
#ifdef DLT_JUNIPER_CHDLC
	{ juniper_chdlc_print,	DLT_JUNIPER_CHDLC },
#endif
#ifdef DLT_PKTAP
	{ pktap_if_print,	DLT_PKTAP },
#endif
#ifdef DLT_IEEE802_11_RADIO
	{ ieee802_11_radio_if_print,	DLT_IEEE802_11_RADIO },
#endif
#ifdef DLT_IEEE802_11
	{ ieee802_11_if_print,	DLT_IEEE802_11},
#endif
#ifdef DLT_IEEE802_11_RADIO_AVS
	{ ieee802_11_radio_avs_if_print,	DLT_IEEE802_11_RADIO_AVS },
#endif
#ifdef DLT_PRISM_HEADER
	{ prism_if_print,	DLT_PRISM_HEADER },
#endif
	{ ppp_if_print,		DLT_PPP },
#ifdef DLT_PPP_WITHDIRECTION
	{ ppp_if_print,		DLT_PPP_WITHDIRECTION },
#endif
#ifdef DLT_PPP_BSDOS
	{ ppp_bsdos_if_print,	DLT_PPP_BSDOS },
#endif
#ifdef DLT_PPP_SERIAL
	{ ppp_hdlc_if_print,	DLT_PPP_SERIAL },
#endif
	{ NULL,			0 },
};

static void	ndo_default_print(netdissect_options *ndo, const u_char *bp,
		    u_int length);

static void	ndo_error(netdissect_options *ndo,
		    FORMAT_STRING(const char *fmt), ...)
		    NORETURN PRINTFLIKE(2, 3);
static void	ndo_warning(netdissect_options *ndo,
		    FORMAT_STRING(const char *fmt), ...)
		    PRINTFLIKE(2, 3);

static int	ndo_printf(netdissect_options *ndo,
		     FORMAT_STRING(const char *fmt), ...)
		     PRINTFLIKE(2, 3);

void
init_print(netdissect_options *ndo, uint32_t localnet, uint32_t mask,
    uint32_t timezone_offset)
{

	thiszone = timezone_offset;
	init_addrtoname(ndo, localnet, mask);
	init_checksum();
}

if_printer
lookup_printer(int type)
{
	const struct printer *p;

	for (p = printers; p->f; ++p)
		if (type == p->type)
			return p->f;

#if defined(DLT_USER2) && defined(DLT_PKTAP)
	/*
	 * Apple incorrectly chose to use DLT_USER2 for their PKTAP
	 * header.
	 *
	 * We map DLT_PKTAP, whether it's DLT_USER2 as it is on Darwin-
	 * based OSes or the same value as LINKTYPE_PKTAP as it is on
	 * other OSes, to LINKTYPE_PKTAP, so files written with
	 * this version of libpcap for a DLT_PKTAP capture have a link-
	 * layer header type of LINKTYPE_PKTAP.
	 *
	 * However, files written on OS X Mavericks for a DLT_PKTAP
	 * capture have a link-layer header type of LINKTYPE_USER2.
	 * If we don't have a printer for DLT_USER2, and type is
	 * DLT_USER2, we look up the printer for DLT_PKTAP and use
	 * that.
	 */
	if (type == DLT_USER2) {
		for (p = printers; p->f; ++p)
			if (DLT_PKTAP == p->type)
				return p->f;
	}
#endif

	return NULL;
	/* NOTREACHED */
}

int
has_printer(int type)
{
	return (lookup_printer(type) != NULL);
}

if_printer
get_if_printer(netdissect_options *ndo, int type)
{
	const char *dltname;
	if_printer printer;

	printer = lookup_printer(type);
	if (printer == NULL) {
		dltname = pcap_datalink_val_to_name(type);
		if (dltname != NULL)
			(*ndo->ndo_error)(ndo,
					  "packet printing is not supported for link type %s: use -w",
					  dltname);
		else
			(*ndo->ndo_error)(ndo,
					  "packet printing is not supported for link type %d: use -w", type);
	}
	return printer;
}

void
pretty_print_packet(netdissect_options *ndo, const struct pcap_pkthdr *h,
    const u_char *sp, u_int packets_captured)
{
	u_int hdrlen;

	if(ndo->ndo_packet_number)
		ND_PRINT((ndo, "%5u  ", packets_captured));

	ts_print(ndo, &h->ts);

	/*
	 * Some printers want to check that they're not walking off the
	 * end of the packet.
	 * Rather than pass it all the way down, we set this member
	 * of the netdissect_options structure.
	 */
	ndo->ndo_snapend = sp + h->caplen;

        hdrlen = (ndo->ndo_if_printer)(ndo, h, sp);

	/*
	 * Restore the original snapend, as a printer might have
	 * changed it.
	 */
	ndo->ndo_snapend = sp + h->caplen;
	if (ndo->ndo_Xflag) {
		/*
		 * Print the raw packet data in hex and ASCII.
		 */
		if (ndo->ndo_Xflag > 1) {
			/*
			 * Include the link-layer header.
			 */
			hex_and_ascii_print(ndo, "\n\t", sp, h->caplen);
		} else {
			/*
			 * Don't include the link-layer header - and if
			 * we have nothing past the link-layer header,
			 * print nothing.
			 */
			if (h->caplen > hdrlen)
				hex_and_ascii_print(ndo, "\n\t", sp + hdrlen,
				    h->caplen - hdrlen);
		}
	} else if (ndo->ndo_xflag) {
		/*
		 * Print the raw packet data in hex.
		 */
		if (ndo->ndo_xflag > 1) {
			/*
			 * Include the link-layer header.
			 */
                        hex_print(ndo, "\n\t", sp, h->caplen);
		} else {
			/*
			 * Don't include the link-layer header - and if
			 * we have nothing past the link-layer header,
			 * print nothing.
			 */
			if (h->caplen > hdrlen)
				hex_print(ndo, "\n\t", sp + hdrlen,
                                          h->caplen - hdrlen);
		}
	} else if (ndo->ndo_Aflag) {
		/*
		 * Print the raw packet data in ASCII.
		 */
		if (ndo->ndo_Aflag > 1) {
			/*
			 * Include the link-layer header.
			 */
			ascii_print(ndo, sp, h->caplen);
		} else {
			/*
			 * Don't include the link-layer header - and if
			 * we have nothing past the link-layer header,
			 * print nothing.
			 */
			if (h->caplen > hdrlen)
				ascii_print(ndo, sp + hdrlen, h->caplen - hdrlen);
		}
	}

	ND_PRINT((ndo, "\n"));
}

/*
 * By default, print the specified data out in hex and ASCII.
 */
static void
ndo_default_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
	hex_and_ascii_print(ndo, "\n\t", bp, length); /* pass on lf and indentation string */
}

/* VARARGS */
static void
ndo_error(netdissect_options *ndo, const char *fmt, ...)
{
	va_list ap;

	if(ndo->program_name)
		(void)fprintf(stderr, "%s: ", ndo->program_name);
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (*fmt) {
		fmt += strlen(fmt);
		if (fmt[-1] != '\n')
			(void)fputc('\n', stderr);
	}
	nd_cleanup();
	exit(1);
	/* NOTREACHED */
}

/* VARARGS */
static void
ndo_warning(netdissect_options *ndo, const char *fmt, ...)
{
	va_list ap;

	if(ndo->program_name)
		(void)fprintf(stderr, "%s: ", ndo->program_name);
	(void)fprintf(stderr, "WARNING: ");
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (*fmt) {
		fmt += strlen(fmt);
		if (fmt[-1] != '\n')
			(void)fputc('\n', stderr);
	}
}

static int
ndo_printf(netdissect_options *ndo, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = vfprintf(stdout, fmt, args);
	va_end(args);

	if (ret < 0)
		ndo_error(ndo, "Unable to write output: %s", pcap_strerror(errno));
	return (ret);
}

void
ndo_set_function_pointers(netdissect_options *ndo)
{
	ndo->ndo_default_print=ndo_default_print;
	ndo->ndo_printf=ndo_printf;
	ndo->ndo_error=ndo_error;
	ndo->ndo_warning=ndo_warning;
}
/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
