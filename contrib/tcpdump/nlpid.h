/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

extern const struct tok nlpid_values[];

#define	NLPID_NULLNS	  0x00
#define NLPID_Q933      0x08 /* ANSI T1.617 Annex D or ITU-T Q.933 Annex A */
#define NLPID_LMI       0x09 /* The original, aka Cisco, aka Gang of Four */
#define NLPID_SNAP      0x80
#define	NLPID_CLNP	    0x81 /* iso9577 */
#define	NLPID_ESIS	    0x82 /* iso9577 */
#define	NLPID_ISIS	    0x83 /* iso9577 */
#define NLPID_CONS      0x84
#define NLPID_IDRP      0x85
#define NLPID_MFR       0xb1 /* FRF.15 */
#define NLPID_SPB       0xc1 /* IEEE 802.1aq/D4.5 */
#define NLPID_IP        0xcc
#define NLPID_PPP       0xcf
#define NLPID_X25_ESIS  0x8a
#define NLPID_IP6       0x8e
