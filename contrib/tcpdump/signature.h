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
 * Functions for signature and digest verification.
 *
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

/* for netdissect_options */
#include "netdissect.h"

/* signature checking result codes */
#define SIGNATURE_VALID		0
#define SIGNATURE_INVALID	1
#define CANT_ALLOCATE_COPY	2
#define CANT_CHECK_SIGNATURE	3

extern const struct tok signature_check_values[];
extern int signature_verify(netdissect_options *, const u_char *, u_int,
                            const u_char *, void (*)(void *), const void *);
