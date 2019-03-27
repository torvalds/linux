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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "gmpls.h"

/* rfc3471 */
const struct tok gmpls_link_prot_values[] = {
    { 0x01, "Extra Traffic"},
    { 0x02, "Unprotected"},
    { 0x04, "Shared"},
    { 0x08, "Dedicated 1:1"},
    { 0x10, "Dedicated 1+1"},
    { 0x20, "Enhanced"},
    { 0x40, "Reserved"},
    { 0x80, "Reserved"},
    { 0, NULL }
};

/* rfc3471 */
const struct tok gmpls_switch_cap_values[] = {
    { GMPLS_PSC1, "Packet-Switch Capable-1"},
    { GMPLS_PSC2, "Packet-Switch Capable-2"},
    { GMPLS_PSC3, "Packet-Switch Capable-3"},
    { GMPLS_PSC4, "Packet-Switch Capable-4"},
    { GMPLS_L2SC, "Layer-2 Switch Capable"},
    { GMPLS_TSC, "Time-Division-Multiplex"},
    { GMPLS_LSC, "Lambda-Switch Capable"},
    { GMPLS_FSC, "Fiber-Switch Capable"},
    { 0, NULL }
};

/* rfc4205 */
const struct tok gmpls_switch_cap_tsc_indication_values[] = {
    { 0, "Standard SONET/SDH" },
    { 1, "Arbitrary SONET/SDH" },
    { 0, NULL }
};

/* rfc3471 */
const struct tok gmpls_encoding_values[] = {
    { 1,    "Packet"},
    { 2,    "Ethernet V2/DIX"},
    { 3,    "ANSI/ETSI PDH"},
    { 4,    "Reserved"},
    { 5,    "SDH ITU-T G.707/SONET ANSI T1.105"},
    { 6,    "Reserved"},
    { 7,    "Digital Wrapper"},
    { 8,    "Lambda (photonic)"},
    { 9,    "Fiber"},
    { 10,   "Reserved"},
    { 11,   "FiberChannel"},
    { 0, NULL }
};

/* rfc3471 */
const struct tok gmpls_payload_values[] = {
    {  0,   "Unknown"},
    {  1,   "Reserved"},
    {  2,   "Reserved"},
    {  3,   "Reserved"},
    {  4,   "Reserved"},
    {  5,   "Asynchronous mapping of E4"},
    {  6,   "Asynchronous mapping of DS3/T3"},
    {  7,   "Asynchronous mapping of E3"},
    {  8,   "Bit synchronous mapping of E3"},
    {  9,   "Byte synchronous mapping of E3"},
    { 10,   "Asynchronous mapping of DS2/T2"},
    { 11,   "Bit synchronous mapping of DS2/T2"},
    { 12,   "Reserved"},
    { 13,   "Asynchronous mapping of E1"},
    { 14,   "Byte synchronous mapping of E1"},
    { 15,   "Byte synchronous mapping of 31 * DS0"},
    { 16,   "Asynchronous mapping of DS1/T1"},
    { 17,   "Bit synchronous mapping of DS1/T1"},
    { 18,   "Byte synchronous mapping of DS1/T1"},
    { 19,   "VC-11 in VC-12"},
    { 20,   "Reserved"},
    { 21,   "Reserved"},
    { 22,   "DS1 SF Asynchronous"},
    { 23,   "DS1 ESF Asynchronous"},
    { 24,   "DS3 M23 Asynchronous"},
    { 25,   "DS3 C-Bit Parity Asynchronous"},
    { 26,   "VT/LOVC"},
    { 27,   "STS SPE/HOVC"},
    { 28,   "POS - No Scrambling, 16 bit CRC"},
    { 29,   "POS - No Scrambling, 32 bit CRC"},
    { 30,   "POS - Scrambling, 16 bit CRC"},
    { 31,   "POS - Scrambling, 32 bit CRC"},
    { 32,   "ATM mapping"},
    { 33,   "Ethernet PHY"},
    { 34,   "SONET/SDH"},
    { 35,   "Reserved (SONET deprecated)"},
    { 36,   "Digital Wrapper"},
    { 37,   "Lambda"},
    { 38,   "ANSI/ETSI PDH"},
    { 39,   "Reserved"},
    { 40,   "Link Access Protocol SDH (X.85 and X.86)"},
    { 41,   "FDDI"},
    { 42,   "DQDB (ETSI ETS 300 216)"},
    { 43,   "FiberChannel-3 (Services)"},
    { 44,   "HDLC"},
    { 45,   "Ethernet V2/DIX (only)"},
    { 46,   "Ethernet 802.3 (only)"},
/* draft-ietf-ccamp-gmpls-g709-04.txt */
    { 47,   "G.709 ODUj"},
    { 48,   "G.709 OTUk(v)"},
    { 49,   "CBR/CBRa"},
    { 50,   "CBRb"},
    { 51,   "BSOT"},
    { 52,   "BSNT"},
    { 53,   "IP/PPP (GFP)"},
    { 54,   "Ethernet MAC (framed GFP)"},
    { 55,   "Ethernet PHY (transparent GFP)"},
    { 56,   "ESCON"},
    { 57,   "FICON"},
    { 58,   "Fiber Channel"},
    { 0, NULL }
};

/*
 * Link Type values used by LMP Service Discovery (specifically, the Client
 * Port Service Attributes Object). See UNI 1.0 section 9.4.2 for details.
 */
const struct tok lmp_sd_service_config_cpsa_link_type_values[] = {
    { 5, "SDH ITU-T G.707"},
    { 6, "SONET ANSI T1.105"},
    { 0, NULL}
};

/*
 * Signal Type values for SDH links used by LMP Service Discovery (specifically,
 * the Client Port Service Attributes Object). See UNI 1.0 section 9.4.2 for
 * details.
 */
const struct tok lmp_sd_service_config_cpsa_signal_type_sdh_values[] = {
    { 5,  "VC-3"},
    { 6,  "VC-4"},
    { 7,  "STM-0"},
    { 8,  "STM-1"},
    { 9,  "STM-4"},
    { 10, "STM-16"},
    { 11, "STM-64"},
    { 12, "STM-256"},
    { 0, NULL}
};

/*
 * Signal Type values for SONET links used by LMP Service Discovery (specifically,
 * the Client Port Service Attributes Object). See UNI 1.0 section 9.4.2 for
 * details.
 */
const struct tok lmp_sd_service_config_cpsa_signal_type_sonet_values[] = {
    { 5,  "STS-1 SPE"},
    { 6,  "STS-3c SPE"},
    { 7,  "STS-1"},
    { 8,  "STM-3"},
    { 9,  "STM-12"},
    { 10, "STM-48"},
    { 11, "STM-192"},
    { 12, "STM-768"},
    { 0, NULL}
};

#define DIFFSERV_BC_MODEL_RDM           0   /* draft-ietf-tewg-diff-te-proto-07 */
#define DIFFSERV_BC_MODEL_MAM           1   /* draft-ietf-tewg-diff-te-proto-07 */
#define DIFFSERV_BC_MODEL_EXTD_MAM      254 /* experimental */

const struct tok diffserv_te_bc_values[] = {
    {  DIFFSERV_BC_MODEL_RDM, "Russian dolls"},
    {  DIFFSERV_BC_MODEL_MAM, "Maximum allocation"},
    {  DIFFSERV_BC_MODEL_EXTD_MAM, "Maximum allocation with E-LSP support"},
    { 0, NULL }
};
