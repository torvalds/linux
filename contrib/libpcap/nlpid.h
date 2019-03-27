/*
 * Copyright (c) 1996
 *	Juniper Networks, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution.  The name of Juniper Networks may not
 * be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* Types missing from some systems */

/*
 * Network layer prototocol identifiers
 */
#ifndef ISO8473_CLNP
#define ISO8473_CLNP		0x81
#endif
#ifndef	ISO9542_ESIS
#define	ISO9542_ESIS		0x82
#endif
#ifndef ISO9542X25_ESIS
#define ISO9542X25_ESIS		0x8a
#endif
#ifndef	ISO10589_ISIS
#define	ISO10589_ISIS		0x83
#endif
/*
 * this does not really belong in the nlpid.h file
 * however we need it for generating nice
 * IS-IS related BPF filters
 */
#define ISIS_L1_LAN_IIH      15
#define ISIS_L2_LAN_IIH      16
#define ISIS_PTP_IIH         17
#define ISIS_L1_LSP          18
#define ISIS_L2_LSP          20
#define ISIS_L1_CSNP         24
#define ISIS_L2_CSNP         25
#define ISIS_L1_PSNP         26
#define ISIS_L2_PSNP         27

#ifndef ISO8878A_CONS
#define	ISO8878A_CONS		0x84
#endif
#ifndef	ISO10747_IDRP
#define	ISO10747_IDRP		0x85
#endif
