/*
 * Copyright 2009 Bert Vermeulen <bert@biot.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by Paolo Abeni.''
 * The name of author may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Support for USB packets
 *
 */

/* \summary: USB printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"


#if defined(HAVE_PCAP_USB_H) && defined(DLT_USB_LINUX)
#include <pcap/usb.h>

static const char tstr[] = "[|usb]";

/* returns direction: 1=inbound 2=outbound -1=invalid */
static int
get_direction(int transfer_type, int event_type)
{
	int direction;

	direction = -1;
	switch(transfer_type){
	case URB_BULK:
	case URB_CONTROL:
	case URB_ISOCHRONOUS:
		switch(event_type)
		{
		case URB_SUBMIT:
			direction = 2;
			break;
		case URB_COMPLETE:
		case URB_ERROR:
			direction = 1;
			break;
		default:
			direction = -1;
		}
		break;
	case URB_INTERRUPT:
		switch(event_type)
		{
		case URB_SUBMIT:
			direction = 1;
			break;
		case URB_COMPLETE:
		case URB_ERROR:
			direction = 2;
			break;
		default:
			direction = -1;
		}
		break;
	 default:
		direction = -1;
	}

	return direction;
}

static void
usb_header_print(netdissect_options *ndo, const pcap_usb_header *uh)
{
	int direction;

	switch(uh->transfer_type)
	{
		case URB_ISOCHRONOUS:
			ND_PRINT((ndo, "ISOCHRONOUS"));
			break;
		case URB_INTERRUPT:
			ND_PRINT((ndo, "INTERRUPT"));
			break;
		case URB_CONTROL:
			ND_PRINT((ndo, "CONTROL"));
			break;
		case URB_BULK:
			ND_PRINT((ndo, "BULK"));
			break;
		default:
			ND_PRINT((ndo, " ?"));
	}

	switch(uh->event_type)
	{
		case URB_SUBMIT:
			ND_PRINT((ndo, " SUBMIT"));
			break;
		case URB_COMPLETE:
			ND_PRINT((ndo, " COMPLETE"));
			break;
		case URB_ERROR:
			ND_PRINT((ndo, " ERROR"));
			break;
		default:
			ND_PRINT((ndo, " ?"));
	}

	direction = get_direction(uh->transfer_type, uh->event_type);
	if(direction == 1)
		ND_PRINT((ndo, " from"));
	else if(direction == 2)
		ND_PRINT((ndo, " to"));
	ND_PRINT((ndo, " %d:%d:%d", uh->bus_id, uh->device_address, uh->endpoint_number & 0x7f));
}

/*
 * This is the top level routine of the printer for captures with a
 * 48-byte header.
 *
 * 'p' points to the header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
usb_linux_48_byte_print(netdissect_options *ndo, const struct pcap_pkthdr *h,
                        register const u_char *p)
{
	if (h->caplen < sizeof(pcap_usb_header)) {
		ND_PRINT((ndo, "%s", tstr));
		return(sizeof(pcap_usb_header));
	}

	usb_header_print(ndo, (const pcap_usb_header *) p);

	return(sizeof(pcap_usb_header));
}

#ifdef DLT_USB_LINUX_MMAPPED
/*
 * This is the top level routine of the printer for captures with a
 * 64-byte header.
 *
 * 'p' points to the header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
usb_linux_64_byte_print(netdissect_options *ndo, const struct pcap_pkthdr *h,
                        register const u_char *p)
{
	if (h->caplen < sizeof(pcap_usb_header_mmapped)) {
		ND_PRINT((ndo, "%s", tstr));
		return(sizeof(pcap_usb_header_mmapped));
	}

	usb_header_print(ndo, (const pcap_usb_header *) p);

	return(sizeof(pcap_usb_header_mmapped));
}
#endif /* DLT_USB_LINUX_MMAPPED */

#endif /* defined(HAVE_PCAP_USB_H) && defined(DLT_USB_LINUX) */

