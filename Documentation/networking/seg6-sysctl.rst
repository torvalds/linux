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
