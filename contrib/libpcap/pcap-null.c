/*
 * Copyright (c) 1994, 1995, 1996
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "pcap-int.h"

static char nosup[] = "live packet capture not supported on this system";

pcap_t *
pcap_create_interface(const char *device _U_, char *ebuf)
{
	(void)strlcpy(ebuf, nosup, PCAP_ERRBUF_SIZE);
	return (NULL);
}

int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
	/*
	 * There are no interfaces on which we can capture.
	 */
	return (0);
}

#ifdef _WIN32
int
pcap_lookupnet(const char *device _U_, bpf_u_int32 *netp _U_,
    bpf_u_int32 *maskp _U_, char *errbuf)
{
	(void)strlcpy(errbuf, nosup, PCAP_ERRBUF_SIZE);
	return (-1);
}
#endif

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING);
}
