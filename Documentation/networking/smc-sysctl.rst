.. SPDX-License-Identifier: GPL-2.0

==========
SMC Sysctl
==========

/proc/sys/net/smc/* Variables
=============================

autocorking_size - INTEGER
	Setting SMC auto corking size:
	SMC auto corking is like TCP auto corking from the application's
	perspective of view. When applications do consecutive small
	write()/sendmsg() system calls, we try to coalesce these small writes
	as much as possible, to lower total amount of CDC and RDMA Write been
	sent.
	autocorking_size limits the maximum corked bytes that can be sent to
	the under device in 1 single sending. If set to 0, the SMC auto corking
	is disabled.
	Applications can still use TCP_CORK for optimal behavior when they
	know how/when to uncork their sockets.

	Default: 64K

smcr_buf_type - INTEGER
        Controls which type of sndbufs and RMBs to use in later newly created
        SMC-R link group. Only for SMC-R.

        Default: 0 (physically contiguous sndbufs and RMBs)

        Possible values:

        - 0 - Use physically contiguous buffers
        - 1 - Use virtually contiguous buffers
        - 2 - Mixed use of the two types. Try physically contiguous buffers first.
          If not available, use virtually contiguous buffers then.

smcr_testlink_time - INTEGER
	How frequently SMC-R link sends out TEST_LINK LLC messages to confirm
	viability, after the last activity of connections on it. Value 0 means
	disabling TEST_LINK.

	Default: 30 seconds.

wmem - INTEGER
	Initial size of send buffer used by SMC sockets.
	The default value inherits from net.ipv4.tcp_wmem[1].

	The minimum value is 16KiB and there is no hard limit for max value, but
	only allowed 512KiB for SMC-R and 1MiB for SMC-D.

	Default: 16K

rmem - INTEGER
	Initial size of receive buffer (RMB) used by SMC sockets.
	The default value inherits from net.ipv4.tcp_rmem[1].

	The minimum value is 16KiB and there is no hard limit for max value, but
	only allowed 512KiB for SMC-R and 1MiB for SMC-D.

	Default: 128K
