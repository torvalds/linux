.. SPDX-License-Identifier: GPL-2.0

===============
NIC SR-IOV APIs
===============

Modern NICs are strongly encouraged to focus on implementing the ``switchdev``
model (see :ref:`switchdev`) to configure forwarding and security of SR-IOV
functionality.

Legacy API
==========

The old SR-IOV API is implemented in ``rtnetlink`` Netlink family as part of
the ``RTM_GETLINK`` and ``RTM_SETLINK`` commands. On the driver side
it consists of a number of ``ndo_set_vf_*`` and ``ndo_get_vf_*`` callbacks.

Since the legacy APIs do not integrate well with the rest of the stack
the API is considered frozen; no new functionality or extensions
will be accepted. New drivers should not implement the uncommon callbacks;
namely the following callbacks are off limits:

 - ``ndo_get_vf_port``
 - ``ndo_set_vf_port``
 - ``ndo_set_vf_rss_query_en``
