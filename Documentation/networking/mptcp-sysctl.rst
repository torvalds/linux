.. SPDX-License-Identifier: GPL-2.0

=====================
MPTCP Sysfs variables
=====================

/proc/sys/net/mptcp/* Variables
===============================

enabled - BOOLEAN
	Control whether MPTCP sockets can be created.

	MPTCP sockets can be created if the value is 1. This is a
	per-namespace sysctl.

	Default: 1 (enabled)

add_addr_timeout - INTEGER (seconds)
	Set the timeout after which an ADD_ADDR control message will be
	resent to an MPTCP peer that has not acknowledged a previous
	ADD_ADDR message.

	The default value matches TCP_RTO_MAX. This is a per-namespace
	sysctl.

	Default: 120

checksum_enabled - BOOLEAN
	Control whether DSS checksum can be enabled.

	DSS checksum can be enabled if the value is nonzero. This is a
	per-namespace sysctl.

	Default: 0

allow_join_initial_addr_port - BOOLEAN
	Allow peers to send join requests to the IP address and port number used
	by the initial subflow if the value is 1. This controls a flag that is
	sent to the peer at connection time, and whether such join requests are
	accepted or denied.

	Joins to addresses advertised with ADD_ADDR are not affected by this
	value.

	This is a per-namespace sysctl.

	Default: 1
