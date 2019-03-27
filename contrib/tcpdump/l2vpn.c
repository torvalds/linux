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
#include "l2vpn.h"

/*
 * BGP Layer 2 Encapsulation Types
 *
 * RFC 6624
 *
 * http://www.iana.org/assignments/bgp-parameters/bgp-parameters.xhtml#bgp-l2-encapsulation-types-registry
 */
const struct tok l2vpn_encaps_values[] = {
    { 0, "Reserved"},
    { 1, "Frame Relay"},
    { 2, "ATM AAL5 SDU VCC transport"},
    { 3, "ATM transparent cell transport"},
    { 4, "Ethernet (VLAN) Tagged Mode"},
    { 5, "Ethernet Raw Mode"},
    { 6, "Cisco HDLC"},
    { 7, "PPP"},
    { 8, "SONET/SDH Circuit Emulation Service over MPLS"},
    { 9, "ATM n-to-one VCC cell transport"},
    { 10, "ATM n-to-one VPC cell transport"},
    { 11, "IP layer 2 transport"},
    { 15, "Frame Relay Port mode"},
    { 17, "Structure-agnostic E1 over packet"},
    { 18, "Structure-agnostic T1 (DS1) over packet"},
    { 19, "VPLS"},
    { 20, "Structure-agnostic T3 (DS3) over packet"},
    { 21, "Nx64kbit/s Basic Service using Structure-aware"},
    { 25, "Frame Relay DLCI"},
    { 40, "Structure-agnostic E3 over packet"},
    { 41, "Octet-aligned playload for Structure-agnostic DS1 circuits"},
    { 42, "E1 Nx64kbit/s with CAS using Structure-aware"},
    { 43, "DS1 (ESF) Nx64kbit/s with CAS using Structure-aware"},
    { 44, "DS1 (SF) Nx64kbit/s with CAS using Structure-aware"},
    { 0, NULL}
};

/*
 * MPLS Pseudowire Types
 *
 * RFC 4446
 *
 * http://www.iana.org/assignments/pwe3-parameters/pwe3-parameters.xhtml#pwe3-parameters-2
 */
const struct tok mpls_pw_types_values[] = {
    { 0x0000, "Reserved"},
    { 0x0001, "Frame Relay DLCI (Martini Mode)"},
    { 0x0002, "ATM AAL5 SDU VCC transport"},
    { 0x0003, "ATM transparent cell transport"},
    { 0x0004, "Ethernet VLAN"},
    { 0x0005, "Ethernet"},
    { 0x0006, "Cisco-HDLC"},
    { 0x0007, "PPP"},
    { 0x0008, "SONET/SDH Circuit Emulation Service over MPLS"},
    { 0x0009, "ATM n-to-one VCC cell transport"},
    { 0x000a, "ATM n-to-one VPC cell transport"},
    { 0x000b, "IP Layer2 Transport"},
    { 0x000c, "ATM one-to-one VCC Cell Mode"},
    { 0x000d, "ATM one-to-one VPC Cell Mode"},
    { 0x000e, "ATM AAL5 PDU VCC transport"},
    { 0x000f, "Frame-Relay Port mode"},
    { 0x0010, "SONET/SDH Circuit Emulation over Packet"},
    { 0x0011, "Structure-agnostic E1 over Packet"},
    { 0x0012, "Structure-agnostic T1 (DS1) over Packet"},
    { 0x0013, "Structure-agnostic E3 over Packet"},
    { 0x0014, "Structure-agnostic T3 (DS3) over Packet"},
    { 0x0015, "CESoPSN basic mode"},
    { 0x0016, "TDMoIP basic mode"},
    { 0x0017, "CESoPSN TDM with CAS"},
    { 0x0018, "TDMoIP TDM with CAS"},
    { 0x0019, "Frame Relay DLCI"},
    { 0x0040, "IP-interworking"},
    { 0, NULL}
};
