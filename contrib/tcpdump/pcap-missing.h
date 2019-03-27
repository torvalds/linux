/*
 * Copyright (c) 1988-2002
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

#ifndef netdissect_pcap_missing_h
#define netdissect_pcap_missing_h

/*
 * Declarations of functions that might be missing from libpcap.
 */

#ifndef HAVE_PCAP_LIST_DATALINKS
extern int pcap_list_datalinks(pcap_t *, int **);
#endif

#ifndef HAVE_PCAP_DATALINK_NAME_TO_VAL
/*
 * We assume no platform has one but not the other.
 */
extern int pcap_datalink_name_to_val(const char *);
extern const char *pcap_datalink_val_to_name(int);
#endif

#ifndef HAVE_PCAP_DATALINK_VAL_TO_DESCRIPTION
extern const char *pcap_datalink_val_to_description(int);
#endif

#ifndef HAVE_PCAP_DUMP_FTELL
extern long pcap_dump_ftell(pcap_dumper_t *);
#endif

#endif /* netdissect_pcap_missing_h */
