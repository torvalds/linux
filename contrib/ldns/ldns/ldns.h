/*
 * dns.h -- defines for the Domain Name System
 *
 * Copyright (c) 2005-2008, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 * This library was created by:
 * Jelte Jansen, Erik Rozendaal and Miek Gieben
 *
 * A bunch of defines that are used in the DNS.
 */


/**
\mainpage LDNS Documentation

\section introduction Introduction

The goal of ldns is to simplify DNS programming, it supports recent RFCs
like the DNSSEC documents, and allow developers to easily create software
conforming to current RFCs, and experimental software for current Internet
drafts. A secondary benefit of using ldns is speed, because ldns is written
in C, and although it is not optimized for performance, it should be a lot
faster than Perl.

The first main tool to use ldns is Drill, from which part of the library was
derived. From version 1.0.0 on, drill is included in the ldns release
and will not be distributed separately anymore. The library also includes some
other examples and tools to show how it can be used. These can be found in the
examples/ directory in the tarball.

ldns depends on OpenSSL for it's cryptographic functions.
Feature list

  - Transparent IPv4 and IPv6 support (overridable if necessary),
  - TSIG support,
  - DNSSEC support; signing and verification,
  - small size,
  - online documentation as well as manual pages.

If you want to send us patches please use the code from git.

\section using_ldns Using ldns

Almost all interaction between an application and ldns goes through the ldns
data structures (\ref ldns_rr, \ref ldns_pkt, etc.). These are input or
output to the functions of ldns. For example, \ref ldns_zone_new_frm_fp
reads a zone from a \c FILE pointer, and returns an \ref ldns_zone
structure.


Let's use Drill as an example. Drill is a tool much like dig, whose most
basic function is to send 1 query to a nameserver and print the response.

To be able to do this, drill uses the resolver module of ldns, which acts as
a stub resolver. The resolver module uses the net module to actually send
the query that drill requested. It then uses the wire2host module to
translate the response and place it in ldns' internal structures. These are
passed back to drill, which then uses the host2str module to print the
response in presentation format.

\section gettingstarted Getting Started

See the \ref design page for a very high level description of the design
choices made for ldns. 

For an overview of the functions and types ldns provides, you can check out
the \ref ldns ldns header file descriptions.

If you want to see some libdns action, you can read our tutorials:
  - \ref tutorial1_mx
  - \ref tutorial2_zone
  - \ref tutorial3_signzone

Or you can just use the menu above to browse through the API docs.

<div style="visibility:hidden;">
\image html LogoInGradientBar2-y100.png
</div>
*/

/**
 * \file ldns.h
 *
 * Including this file will include all ldns files, and define some lookup tables.
 */

#ifndef LDNS_DNS_H
#define LDNS_DNS_H

#include <stdio.h>
#include <stdlib.h>

#include <ldns/util.h>
#include <ldns/buffer.h>
#include <ldns/common.h>
#include <ldns/dane.h>
#include <ldns/dname.h>
#include <ldns/dnssec.h>
#include <ldns/dnssec_verify.h>
#include <ldns/dnssec_sign.h>
#include <ldns/duration.h>
#include <ldns/error.h>
#include <ldns/higher.h>
#include <ldns/host2str.h>
#include <ldns/host2wire.h>
#include <ldns/net.h>
#include <ldns/packet.h>
#include <ldns/rdata.h>
#include <ldns/resolver.h>
#include <ldns/rr.h>
#include <ldns/str2host.h>
#include <ldns/tsig.h>
#include <ldns/update.h>
#include <ldns/wire2host.h>
#include <ldns/rr_functions.h>
#include <ldns/keys.h>
#include <ldns/parse.h>
#include <ldns/zone.h>
#include <ldns/dnssec_zone.h>
#include <ldns/radix.h>
#include <ldns/rbtree.h>
#include <ldns/sha1.h>
#include <ldns/sha2.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LDNS_IP4ADDRLEN      (32/8)
#define LDNS_IP6ADDRLEN      (128/8)
#define LDNS_PORT	53
#define LDNS_ROOT_LABEL_STR     "."
#define LDNS_DEFAULT_TTL	3600

/* lookup tables for standard DNS stuff  */

/** Taken from RFC 2538, section 2.1.  */
extern ldns_lookup_table ldns_certificate_types[];
/** Taken from RFC 2535, section 7.  */
extern ldns_lookup_table ldns_algorithms[];
/** Taken from RFC 2538.  */
extern ldns_lookup_table ldns_cert_algorithms[];
/** rr types  */
extern ldns_lookup_table ldns_rr_classes[];
/** Response codes */
extern ldns_lookup_table ldns_rcodes[];
/** Operation codes */
extern ldns_lookup_table ldns_opcodes[];
/** EDNS flags */
extern ldns_lookup_table ldns_edns_flags[];

#ifdef __cplusplus
}
#endif

#endif /* LDNS_DNS_H */
