.. SPDX-License-Identifier: GPL-2.0

=====================
Multipath TCP (MPTCP)
=====================

Introduction
============

Multipath TCP or MPTCP is an extension to the standard TCP and is described in
`RFC 8684 (MPTCPv1) <https://www.rfc-editor.org/rfc/rfc8684.html>`_. It allows a
device to make use of multiple interfaces at once to send and receive TCP
packets over a single MPTCP connection. MPTCP can aggregate the bandwidth of
multiple interfaces or prefer the one with the lowest latency. It also allows a
fail-over if one path is down, and the traffic is seamlessly reinjected on other
paths.

For more details about Multipath TCP in the Linux kernel, please see the
official website: `mptcp.dev <https://www.mptcp.dev>`_.


Use cases
=========

Thanks to MPTCP, being able to use multiple paths in parallel or simultaneously
brings new use-cases, compared to TCP:

- Seamless handovers: switching from one path to another while preserving
  established connections, e.g. to be used in mobility use-cases, like on
  smartphones.
- Best network selection: using the "best" available path depending on some
  conditions, e.g. latency, losses, cost, bandwidth, etc.
- Network aggregation: using multiple paths at the same time to have a higher
  throughput, e.g. to combine fixed and mobile networks to send files faster.


Concepts
========

Technically, when a new socket is created with the ``IPPROTO_MPTCP`` protocol
(Linux-specific), a *subflow* (or *path*) is created. This *subflow* consists of
a regular TCP connection that is used to transmit data through one interface.
Additional *subflows* can be negotiated later between the hosts. For the remote
host to be able to detect the use of MPTCP, a new field is added to the TCP
*option* field of the underlying TCP *subflow*. This field contains, amongst
other things, a ``MP_CAPABLE`` option that tells the other host to use MPTCP if
it is supported. If the remote host or any middlebox in between does not support
it, the returned ``SYN+ACK`` packet will not contain MPTCP options in the TCP
*option* field. In that case, the connection will be "downgraded" to plain TCP,
and it will continue with a single path.

This behavior is made possible by two internal components: the path manager, and
the packet scheduler.

Path Manager
------------

The Path Manager is in charge of *subflows*, from creation to deletion, and also
address announcements. Typically, it is the client side that initiates subflows,
and the server side that announces additional addresses via the ``ADD_ADDR`` and
``REMOVE_ADDR`` options.

Path managers are controlled by the ``net.mptcp.path_manager`` sysctl knob --
see mptcp-sysctl.rst. There are two types: the in-kernel one (``kernel``) where
the same rules are applied for all the connections (see: ``ip mptcp``) ; and the
userspace one (``userspace``), controlled by a userspace daemon (i.e. `mptcpd
<https://mptcpd.mptcp.dev/>`_) where different rules can be applied for each
connection. The path managers can be controlled via a Netlink API; see
netlink_spec/mptcp_pm.rst.

To be able to use multiple IP addresses on a host to create multiple *subflows*
(paths), the default in-kernel MPTCP path-manager needs to know which IP
addresses can be used. This can be configured with ``ip mptcp endpoint`` for
example.

Packet Scheduler
----------------

The Packet Scheduler is in charge of selecting which available *subflow(s)* to
use to send the next data packet. It can decide to maximize the use of the
available bandwidth, only to pick the path with the lower latency, or any other
policy depending on the configuration.

Packet schedulers are controlled by the ``net.mptcp.scheduler`` sysctl knob --
see mptcp-sysctl.rst.


Sockets API
===========

Creating MPTCP sockets
----------------------

On Linux, MPTCP can be used by selecting MPTCP instead of TCP when creating the
``socket``:

.. code-block:: C

    int sd = socket(AF_INET(6), SOCK_STREAM, IPPROTO_MPTCP);

Note that ``IPPROTO_MPTCP`` is defined as ``262``.

If MPTCP is not supported, ``errno`` will be set to:

- ``EINVAL``: (*Invalid argument*): MPTCP is not available, on kernels < 5.6.
- ``EPROTONOSUPPORT`` (*Protocol not supported*): MPTCP has not been compiled,
  on kernels >= v5.6.
- ``ENOPROTOOPT`` (*Protocol not available*): MPTCP has been disabled using
  ``net.mptcp.enabled`` sysctl knob; see mptcp-sysctl.rst.

MPTCP is then opt-in: applications need to explicitly request it. Note that
applications can be forced to use MPTCP with different techniques, e.g.
``LD_PRELOAD`` (see ``mptcpize``), eBPF (see ``mptcpify``), SystemTAP,
``GODEBUG`` (``GODEBUG=multipathtcp=1``), etc.

Switching to ``IPPROTO_MPTCP`` instead of ``IPPROTO_TCP`` should be as
transparent as possible for the userspace applications.

Socket options
--------------

MPTCP supports most socket options handled by TCP. It is possible some less
common options are not supported, but contributions are welcome.

Generally, the same value is propagated to all subflows, including the ones
created after the calls to ``setsockopt()``. eBPF can be used to set different
values per subflow.

There are some MPTCP specific socket options at the ``SOL_MPTCP`` (284) level to
retrieve info. They fill the ``optval`` buffer of the ``getsockopt()`` system
call:

- ``MPTCP_INFO``: Uses ``struct mptcp_info``.
- ``MPTCP_TCPINFO``: Uses ``struct mptcp_subflow_data``, followed by an array of
  ``struct tcp_info``.
- ``MPTCP_SUBFLOW_ADDRS``: Uses ``struct mptcp_subflow_data``, followed by an
  array of ``mptcp_subflow_addrs``.
- ``MPTCP_FULL_INFO``: Uses ``struct mptcp_full_info``, with one pointer to an
  array of ``struct mptcp_subflow_info`` (including the
  ``struct mptcp_subflow_addrs``), and one pointer to an array of
  ``struct tcp_info``, followed by the content of ``struct mptcp_info``.

Note that at the TCP level, ``TCP_IS_MPTCP`` socket option can be used to know
if MPTCP is currently being used: the value will be set to 1 if it is.


Design choices
==============

A new socket type has been added for MPTCP for the userspace-facing socket. The
kernel is in charge of creating subflow sockets: they are TCP sockets where the
behavior is modified using TCP-ULP.

MPTCP listen sockets will create "plain" *accepted* TCP sockets if the
connection request from the client didn't ask for MPTCP, making the performance
impact minimal when MPTCP is enabled by default.
