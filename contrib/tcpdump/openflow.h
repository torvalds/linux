/*
 * Copyright (c) 2013 The TCPDUMP project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* OpenFlow: protocol between controller and datapath. */

/* for netdissect_options */
#include "netdissect.h"

#define OF_HEADER_LEN 8

#define ONF_EXP_ONF               0x4f4e4600
#define ONF_EXP_BUTE              0xff000001
#define ONF_EXP_NOVIFLOW          0xff000002
#define ONF_EXP_L3                0xff000003
#define ONF_EXP_L4L7              0xff000004
#define ONF_EXP_WMOB              0xff000005
#define ONF_EXP_FABS              0xff000006
#define ONF_EXP_OTRANS            0xff000007
extern const struct tok onf_exp_str[];

/*
 * Routines to print packets for various versions of OpenFlow.
 */
extern const u_char *of10_header_body_print(netdissect_options *ndo,
	const u_char *, const u_char *,
	const uint8_t, const uint16_t, const uint32_t);
extern const char * of_vendor_name(const uint32_t);
