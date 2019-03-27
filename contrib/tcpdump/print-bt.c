/*
 * Copyright (c) 2007
 *	paolo.abeni@email.it  All rights reserved.
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
 */

/* \summary: Bluetooth printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#if defined(DLT_BLUETOOTH_HCI_H4_WITH_PHDR) && defined(HAVE_PCAP_BLUETOOTH_H)
#include <pcap/bluetooth.h>

#define	BT_HDRLEN sizeof(pcap_bluetooth_h4_header)
/*
 * This is the top level routine of the printer.  'p' points
 * to the bluetooth header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
bt_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;
	const pcap_bluetooth_h4_header* hdr = (const pcap_bluetooth_h4_header*)p;

	if (caplen < BT_HDRLEN) {
		ND_PRINT((ndo, "[|bt]"));
		return (BT_HDRLEN);
	}
	caplen -= BT_HDRLEN;
	length -= BT_HDRLEN;
	p += BT_HDRLEN;
	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "hci length %d, direction %s, ", length, (EXTRACT_32BITS(&hdr->direction)&0x1)?"in":"out"));

	if (!ndo->ndo_suppress_default_print)
		ND_DEFAULTPRINT(p, caplen);

	return (BT_HDRLEN);
}
#endif


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
