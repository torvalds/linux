/*
 * Copyright (c) 2014 The TCPDUMP project
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

/* \summary: ATA over Ethernet (AoE) protocol printer */

/* specification: http://brantleycoilecompany.com/AoEr11.pdf */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"
#include "ether.h"

static const char tstr[] = " [|aoe]";

#define AOE_V1 1
#define ATA_SECTOR_SIZE 512

#define AOEV1_CMD_ISSUE_ATA_COMMAND        0
#define AOEV1_CMD_QUERY_CONFIG_INFORMATION 1
#define AOEV1_CMD_MAC_MASK_LIST            2
#define AOEV1_CMD_RESERVE_RELEASE          3

static const struct tok cmdcode_str[] = {
	{ AOEV1_CMD_ISSUE_ATA_COMMAND,        "Issue ATA Command"        },
	{ AOEV1_CMD_QUERY_CONFIG_INFORMATION, "Query Config Information" },
	{ AOEV1_CMD_MAC_MASK_LIST,            "MAC Mask List"            },
	{ AOEV1_CMD_RESERVE_RELEASE,          "Reserve/Release"          },
	{ 0, NULL }
};

#define AOEV1_COMMON_HDR_LEN    10U /* up to but w/o Arg                */
#define AOEV1_ISSUE_ARG_LEN     12U /* up to but w/o Data               */
#define AOEV1_QUERY_ARG_LEN      8U /* up to but w/o Config String      */
#define AOEV1_MAC_ARG_LEN        4U /* up to but w/o Directive 0        */
#define AOEV1_RESERVE_ARG_LEN    2U /* up to but w/o Ethernet address 0 */
#define AOEV1_MAX_CONFSTR_LEN 1024U

#define AOEV1_FLAG_R 0x08
#define AOEV1_FLAG_E 0x04

static const struct tok aoev1_flag_str[] = {
	{ AOEV1_FLAG_R, "Response" },
	{ AOEV1_FLAG_E, "Error"    },
	{ 0x02,         "MBZ-0x02" },
	{ 0x01,         "MBZ-0x01" },
	{ 0, NULL }
};

static const struct tok aoev1_errcode_str[] = {
	{ 1, "Unrecognized command code" },
	{ 2, "Bad argument parameter"    },
	{ 3, "Device unavailable"        },
	{ 4, "Config string present"     },
	{ 5, "Unsupported version"       },
	{ 6, "Target is reserved"        },
	{ 0, NULL }
};

#define AOEV1_AFLAG_E 0x40
#define AOEV1_AFLAG_D 0x10
#define AOEV1_AFLAG_A 0x02
#define AOEV1_AFLAG_W 0x01

static const struct tok aoev1_aflag_str[] = {
	{ 0x08,          "MBZ-0x08" },
	{ AOEV1_AFLAG_E, "Ext48"    },
	{ 0x06,          "MBZ-0x06" },
	{ AOEV1_AFLAG_D, "Device"   },
	{ 0x04,          "MBZ-0x04" },
	{ 0x03,          "MBZ-0x03" },
	{ AOEV1_AFLAG_A, "Async"    },
	{ AOEV1_AFLAG_W, "Write"    },
	{ 0, NULL }
};

static const struct tok aoev1_ccmd_str[] = {
	{ 0, "read config string"        },
	{ 1, "test config string"        },
	{ 2, "test config string prefix" },
	{ 3, "set config string"         },
	{ 4, "force set config string"   },
	{ 0, NULL }
};

static const struct tok aoev1_mcmd_str[] = {
	{ 0, "Read Mac Mask List" },
	{ 1, "Edit Mac Mask List" },
	{ 0, NULL }
};

static const struct tok aoev1_merror_str[] = {
	{ 1, "Unspecified Error"  },
	{ 2, "Bad DCmd directive" },
	{ 3, "Mask list full"     },
	{ 0, NULL }
};

static const struct tok aoev1_dcmd_str[] = {
	{ 0, "No Directive"                      },
	{ 1, "Add mac address to mask list"      },
	{ 2, "Delete mac address from mask list" },
	{ 0, NULL }
};

static const struct tok aoev1_rcmd_str[] = {
	{ 0, "Read reserve list"      },
	{ 1, "Set reserve list"       },
	{ 2, "Force set reserve list" },
	{ 0, NULL }
};

static void
aoev1_issue_print(netdissect_options *ndo,
                  const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;

	if (len < AOEV1_ISSUE_ARG_LEN)
		goto invalid;
	/* AFlags */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, "\n\tAFlags: [%s]", bittok2str(aoev1_aflag_str, "none", *cp)));
	cp += 1;
	/* Err/Feature */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", Err/Feature: %u", *cp));
	cp += 1;
	/* Sector Count (not correlated with the length) */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", Sector Count: %u", *cp));
	cp += 1;
	/* Cmd/Status */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", Cmd/Status: %u", *cp));
	cp += 1;
	/* lba0 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, "\n\tlba0: %u", *cp));
	cp += 1;
	/* lba1 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", lba1: %u", *cp));
	cp += 1;
	/* lba2 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", lba2: %u", *cp));
	cp += 1;
	/* lba3 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", lba3: %u", *cp));
	cp += 1;
	/* lba4 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", lba4: %u", *cp));
	cp += 1;
	/* lba5 */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", lba5: %u", *cp));
	cp += 1;
	/* Reserved */
	ND_TCHECK2(*cp, 2);
	cp += 2;
	/* Data */
	if (len > AOEV1_ISSUE_ARG_LEN)
		ND_PRINT((ndo, "\n\tData: %u bytes", len - AOEV1_ISSUE_ARG_LEN));
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
aoev1_query_print(netdissect_options *ndo,
                  const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint16_t cslen;

	if (len < AOEV1_QUERY_ARG_LEN)
		goto invalid;
	/* Buffer Count */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, "\n\tBuffer Count: %u", EXTRACT_16BITS(cp)));
	cp += 2;
	/* Firmware Version */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, ", Firmware Version: %u", EXTRACT_16BITS(cp)));
	cp += 2;
	/* Sector Count */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", Sector Count: %u", *cp));
	cp += 1;
	/* AoE/CCmd */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", AoE: %u, CCmd: %s", (*cp & 0xF0) >> 4,
	          tok2str(aoev1_ccmd_str, "Unknown (0x02x)", *cp & 0x0F)));
	cp += 1;
	/* Config String Length */
	ND_TCHECK2(*cp, 2);
	cslen = EXTRACT_16BITS(cp);
	cp += 2;
	if (cslen > AOEV1_MAX_CONFSTR_LEN || AOEV1_QUERY_ARG_LEN + cslen > len)
		goto invalid;
	/* Config String */
	ND_TCHECK2(*cp, cslen);
	if (cslen) {
		ND_PRINT((ndo, "\n\tConfig String (length %u): ", cslen));
		if (fn_printn(ndo, cp, cslen, ndo->ndo_snapend))
			goto trunc;
	}
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
aoev1_mac_print(netdissect_options *ndo,
                const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint8_t dircount, i;

	if (len < AOEV1_MAC_ARG_LEN)
		goto invalid;
	/* Reserved */
	ND_TCHECK2(*cp, 1);
	cp += 1;
	/* MCmd */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, "\n\tMCmd: %s", tok2str(aoev1_mcmd_str, "Unknown (0x%02x)", *cp)));
	cp += 1;
	/* MError */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", MError: %s", tok2str(aoev1_merror_str, "Unknown (0x%02x)", *cp)));
	cp += 1;
	/* Dir Count */
	ND_TCHECK2(*cp, 1);
	dircount = *cp;
	cp += 1;
	ND_PRINT((ndo, ", Dir Count: %u", dircount));
	if (AOEV1_MAC_ARG_LEN + dircount * 8 > len)
		goto invalid;
	/* directives */
	for (i = 0; i < dircount; i++) {
		/* Reserved */
		ND_TCHECK2(*cp, 1);
		cp += 1;
		/* DCmd */
		ND_TCHECK2(*cp, 1);
		ND_PRINT((ndo, "\n\t DCmd: %s", tok2str(aoev1_dcmd_str, "Unknown (0x%02x)", *cp)));
		cp += 1;
		/* Ethernet Address */
		ND_TCHECK2(*cp, ETHER_ADDR_LEN);
		ND_PRINT((ndo, ", Ethernet Address: %s", etheraddr_string(ndo, cp)));
		cp += ETHER_ADDR_LEN;
	}
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
aoev1_reserve_print(netdissect_options *ndo,
                    const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint8_t nmacs, i;

	if (len < AOEV1_RESERVE_ARG_LEN || (len - AOEV1_RESERVE_ARG_LEN) % ETHER_ADDR_LEN)
		goto invalid;
	/* RCmd */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, "\n\tRCmd: %s", tok2str(aoev1_rcmd_str, "Unknown (0x%02x)", *cp)));
	cp += 1;
	/* NMacs (correlated with the length) */
	ND_TCHECK2(*cp, 1);
	nmacs = *cp;
	cp += 1;
	ND_PRINT((ndo, ", NMacs: %u", nmacs));
	if (AOEV1_RESERVE_ARG_LEN + nmacs * ETHER_ADDR_LEN != len)
		goto invalid;
	/* addresses */
	for (i = 0; i < nmacs; i++) {
		ND_PRINT((ndo, "\n\tEthernet Address %u: %s", i, etheraddr_string(ndo, cp)));
		cp += ETHER_ADDR_LEN;
	}
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

/* cp points to the Ver/Flags octet */
static void
aoev1_print(netdissect_options *ndo,
            const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint8_t flags, command;
	void (*cmd_decoder)(netdissect_options *, const u_char *, const u_int);

	if (len < AOEV1_COMMON_HDR_LEN)
		goto invalid;
	/* Flags */
	flags = *cp & 0x0F;
	ND_PRINT((ndo, ", Flags: [%s]", bittok2str(aoev1_flag_str, "none", flags)));
	cp += 1;
	if (! ndo->ndo_vflag)
		return;
	/* Error */
	ND_TCHECK2(*cp, 1);
	if (flags & AOEV1_FLAG_E)
		ND_PRINT((ndo, "\n\tError: %s", tok2str(aoev1_errcode_str, "Invalid (%u)", *cp)));
	cp += 1;
	/* Major */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, "\n\tMajor: 0x%04x", EXTRACT_16BITS(cp)));
	cp += 2;
	/* Minor */
	ND_TCHECK2(*cp, 1);
	ND_PRINT((ndo, ", Minor: 0x%02x", *cp));
	cp += 1;
	/* Command */
	ND_TCHECK2(*cp, 1);
	command = *cp;
	cp += 1;
	ND_PRINT((ndo, ", Command: %s", tok2str(cmdcode_str, "Unknown (0x%02x)", command)));
	/* Tag */
	ND_TCHECK2(*cp, 4);
	ND_PRINT((ndo, ", Tag: 0x%08x", EXTRACT_32BITS(cp)));
	cp += 4;
	/* Arg */
	cmd_decoder =
		command == AOEV1_CMD_ISSUE_ATA_COMMAND        ? aoev1_issue_print :
		command == AOEV1_CMD_QUERY_CONFIG_INFORMATION ? aoev1_query_print :
		command == AOEV1_CMD_MAC_MASK_LIST            ? aoev1_mac_print :
		command == AOEV1_CMD_RESERVE_RELEASE          ? aoev1_reserve_print :
		NULL;
	if (cmd_decoder != NULL)
		cmd_decoder(ndo, cp, len - AOEV1_COMMON_HDR_LEN);
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

void
aoe_print(netdissect_options *ndo,
          const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint8_t ver;

	ND_PRINT((ndo, "AoE length %u", len));

	if (len < 1)
		goto invalid;
	/* Ver/Flags */
	ND_TCHECK2(*cp, 1);
	ver = (*cp & 0xF0) >> 4;
	/* Don't advance cp yet: low order 4 bits are version-specific. */
	ND_PRINT((ndo, ", Ver %u", ver));

	switch (ver) {
		case AOE_V1:
			aoev1_print(ndo, cp, len);
			break;
	}
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

