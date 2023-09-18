.. SPDX-License-Identifier: BSD-3-Clause

======================================================
Netlink specification support for raw Netlink families
======================================================

This document describes the additional properties required by raw Netlink
families such as ``NETLINK_ROUTE`` which use the ``netlink-raw`` protocol
specification.

Specification
=============

The netlink-raw schema extends the :doc:`genetlink-legacy <genetlink-legacy>`
schema with properties that are needed to specify the protocol numbers and
multicast IDs used by raw netlink families. See :ref:`classic_netlink` for more
information.

Globals
-------

protonum
~~~~~~~~

The ``protonum`` property is used to specify the protocol number to use when
opening a netlink socket.

.. code-block:: yaml

  # SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)

  name: rt-addr
  protocol: netlink-raw
  protonum: 0             # part of the NETLINK_ROUTE protocol


Multicast group properties
--------------------------

value
~~~~~

The ``value`` property is used to specify the group ID to use for multicast
group registration.

.. code-block:: yaml

  mcast-groups:
    list:
      -
        name: rtnlgrp-ipv4-ifaddr
        value: 5
      -
        name: rtnlgrp-ipv6-ifaddr
        value: 9
      -
        name: rtnlgrp-mctp-ifaddr
        value: 34
