.. SPDX-License-Identifier: GPL-2.0

====================
Seg6 Sysfs variables
====================


/proc/sys/net/conf/<iface>/seg6_* variables:
============================================

seg6_enabled - BOOL
	Accept or drop SR-enabled IPv6 packets on this interface.

	Relevant packets are those with SRH present and DA = local.

	* 0 - disabled (default)
	* not 0 - enabled

seg6_require_hmac - INTEGER
	Define HMAC policy for ingress SR-enabled packets on this interface.

	* -1 - Ignore HMAC field
	* 0 - Accept SR packets without HMAC, validate SR packets with HMAC
	* 1 - Drop SR packets without HMAC, validate SR packets with HMAC

	Default is 0.

/proc/sys/net/ipv6/seg6_* variables:
====================================

seg6_flowlabel - INTEGER
	Controls the behaviour of computing the flowlabel of outer
	IPv6 header in case of SR T.encaps

	 == =======================================================
	 -1  set flowlabel to zero.
	  0  copy flowlabel from Inner packet in case of Inner IPv6
	     (Set flowlabel to 0 in case IPv4/L2)
	  1  Compute the flowlabel using seg6_make_flowlabel()
	 == =======================================================

	Default is 0.
