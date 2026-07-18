.. SPDX-License-Identifier: GPL-2.0

=================
Checksum Offloads
=================


Introduction
============

This document describes a set of techniques in the Linux networking stack to
take advantage of checksum offload capabilities of various NICs.

The following technologies are described:

* TX Checksum Offload
* LCO: Local Checksum Offload
* RCO: Remote Checksum Offload

Things that should be documented here but aren't yet:

* CHECKSUM_UNNECESSARY conversion


TX Checksum Offload
===================

In brief, Tx checksum offload allows to request the device fill in a single
ones-complement
checksum defined by the sk_buff fields skb->csum_start and skb->csum_offset.
The device should compute the 16-bit ones-complement checksum (i.e. the
'IP-style' checksum) from csum_start to the end of the packet, and fill in the
result at (csum_start + csum_offset).

Because csum_offset cannot be negative, this ensures that the previous value of
the checksum field is included in the checksum computation, thus it can be used
to supply any needed corrections to the checksum (such as the sum of the
pseudo-header for UDP or TCP).

This interface only allows a single checksum to be offloaded.  Where
encapsulation is used, the packet may have multiple checksum fields in
different header layers, and the rest will have to be handled by another
mechanism such as LCO or RCO.

SCTP CRC32c can also be offloaded using this interface, by means of filling
skb->csum_start and skb->csum_offset as described above, setting
skb->csum_not_inet, and advertising NETIF_F_SCTP_CRC. Drivers must not treat
ordinary IP checksum offload as SCTP CRC32c support.

No offloading of the IP header checksum is performed; it is always done in
software.  This is OK because when we build the IP header, we obviously have it
in cache, so summing it isn't expensive.  It's also rather short.

The requirements for GSO are more complicated, because when segmenting an
encapsulated packet both the inner and outer checksums may need to be edited or
recomputed for each resulting segment.

A driver declares its offload capabilities in netdev->hw_features; see
Documentation/networking/netdev-features.rst for more. NETIF_F_IP_CSUM and
NETIF_F_IPV6_CSUM are restricted legacy features and are being deprecated in
favor of NETIF_F_HW_CSUM. New devices should use NETIF_F_HW_CSUM to advertise
generic checksum offload. The skb_csum_hwoffload_help() helper can resolve
CHECKSUM_PARTIAL according to the device's advertised checksum capabilities,
falling back to software when needed.

The stack should, for the most part, assume that checksum offload is supported
by the underlying device.  The only place that should check is
validate_xmit_skb(), and the functions it calls directly or indirectly.  That
function compares the offload features requested by the SKB (which may include
other offloads besides TX Checksum Offload) and, if they are not supported or
enabled on the device (determined by netdev->features), performs the
corresponding offload in software.  In the case of TX Checksum Offload, that
means calling skb_csum_hwoffload_help(skb, features).


LCO: Local Checksum Offload
===========================

LCO is a technique for efficiently computing the outer checksum of an
encapsulated datagram when the inner checksum is due to be offloaded.

The ones-complement sum of a correctly checksummed TCP or UDP packet is equal
to the complement of the sum of the pseudo header, because everything else gets
'cancelled out' by the checksum field.  This is because the sum was
complemented before being written to the checksum field.

More generally, this holds in any case where the 'IP-style' ones complement
checksum is used, and thus any checksum that TX Checksum Offload supports.

That is, if we have set up TX Checksum Offload with a start/offset pair, we
know that after the device has filled in that checksum, the ones complement sum
from csum_start to the end of the packet will be equal to the complement of
whatever value we put in the checksum field beforehand.  This allows us to
compute the outer checksum without looking at the payload: we simply stop
summing when we get to csum_start, then add the complement of the 16-bit word
at (csum_start + csum_offset).

Then, when the true inner checksum is filled in (either by hardware or by
skb_checksum_help()), the outer checksum will become correct by virtue of the
arithmetic.

LCO is performed by the stack when constructing an outer UDP header for an
encapsulation such as VXLAN or GENEVE, in udp_set_csum().  Similarly for the
IPv6 equivalents, in udp6_set_csum().

It is also performed when constructing GRE headers with the shared
gre_build_header() helper in include/net/gre.h, which is used by both IPv4 and
IPv6 GRE.

All of the LCO implementations use a helper function lco_csum(), in
include/linux/skbuff.h.

LCO can safely be used for nested encapsulations; in this case, the outer
encapsulation layer will sum over both its own header and the 'middle' header.
This does mean that the 'middle' header will get summed multiple times, but
there doesn't seem to be a way to avoid that without incurring bigger costs
(e.g. in SKB bloat).


RCO: Remote Checksum Offload
============================

RCO is a technique for eliding the inner checksum of an encapsulated datagram,
allowing the outer checksum to be offloaded.  It does, however, involve a
change to the encapsulation protocols, which the receiver must also support.
For this reason, it is disabled by default.

RCO is detailed in the following Internet-Drafts:

* https://tools.ietf.org/html/draft-herbert-remotecsumoffload-00
* https://tools.ietf.org/html/draft-herbert-vxlan-rco-00

In Linux, RCO is implemented individually in each encapsulation protocol, and
most tunnel types have flags controlling its use. For instance, VXLAN has the
configuration flag VXLAN_F_REMCSUM_TX to indicate that RCO should be used when
transmitting.


RX Checksum Offload
===================

RX checksum offload is controlled via NETIF_F_RXCSUM. When disabled the driver
must not set skb->ip_summed on ingress packets. As mentioned, IPv4 checksum
is not offloaded, the RXCSUM feature controls the offload of verification of
transport layer checksums.

Note that packets with bad TCP/UDP checksums must still be passed
to the stack. skb->ip_summed of such packets can be set to ``CHECKSUM_COMPLETE``
or left at ``CHECKSUM_NONE``. Drivers **must not discard** packets with
bad TCP/UDP checksum and must not configure the device to drop them.
Checksum validation is relatively inexpensive and having bad packets reflected
in SNMP counters is crucial for network monitoring.

skb checksum documentation
==========================

.. kernel-doc:: include/linux/skbuff.h
   :doc: skb checksums
