.. SPDX-License-Identifier: GPL-2.0

=================
Linux Kernel TIPC
=================

TIPC (Transparent Inter Process Communication) is a protocol that is
specially designed for intra-cluster communication.

For more information about TIPC, see http://tipc.sourceforge.net.

TIPC Base Types
---------------

.. kernel-doc:: net/tipc/subscr.h
   :internal:

.. kernel-doc:: net/tipc/bearer.h
   :internal:

.. kernel-doc:: net/tipc/name_table.h
   :internal:

.. kernel-doc:: net/tipc/name_distr.h
   :internal:

.. kernel-doc:: net/tipc/bcast.c
   :internal:

TIPC Bearer Interfaces
----------------------

.. kernel-doc:: net/tipc/bearer.c
   :internal:

.. kernel-doc:: net/tipc/udp_media.c
   :internal:

TIPC Crypto Interfaces
----------------------

.. kernel-doc:: net/tipc/crypto.c
   :internal:

TIPC Discoverer Interfaces
--------------------------

.. kernel-doc:: net/tipc/discover.c
   :internal:

TIPC Link Interfaces
--------------------

.. kernel-doc:: net/tipc/link.c
   :internal:

TIPC msg Interfaces
-------------------

.. kernel-doc:: net/tipc/msg.c
   :internal:

TIPC Name Interfaces
--------------------

.. kernel-doc:: net/tipc/name_table.c
   :internal:

.. kernel-doc:: net/tipc/name_distr.c
   :internal:

TIPC Node Management Interfaces
-------------------------------

.. kernel-doc:: net/tipc/node.c
   :internal:

TIPC Socket Interfaces
----------------------

.. kernel-doc:: net/tipc/socket.c
   :internal:

TIPC Network Topology Interfaces
--------------------------------

.. kernel-doc:: net/tipc/subscr.c
   :internal:

TIPC Server Interfaces
----------------------

.. kernel-doc:: net/tipc/topsrv.c
   :internal:

TIPC Trace Interfaces
---------------------

.. kernel-doc:: net/tipc/trace.c
   :internal:
