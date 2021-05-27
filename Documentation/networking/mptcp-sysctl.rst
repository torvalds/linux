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
