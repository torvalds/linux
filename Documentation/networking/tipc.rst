.. SPDX-License-Identifier: GPL-2.0

=================
Linux Kernel TIPC
=================

Introduction
============

TIPC (Transparent Inter Process Communication) is a protocol that is specially
designed for intra-cluster communication. It can be configured to transmit
messages either on UDP or directly across Ethernet. Message delivery is
sequence guaranteed, loss free and flow controlled. Latency times are shorter
than with any other known protocol, while maximal throughput is comparable to
that of TCP.

TIPC Features
-------------

- Cluster wide IPC service

  Have you ever wished you had the convenience of Unix Domain Sockets even when
  transmitting data between cluster nodes? Where you yourself determine the
  addresses you want to bind to and use? Where you don't have to perform DNS
  lookups and worry about IP addresses? Where you don't have to start timers
  to monitor the continuous existence of peer sockets? And yet without the
  downsides of that socket type, such as the risk of lingering inodes?

  Welcome to the Transparent Inter Process Communication service, TIPC in short,
  which gives you all of this, and a lot more.

- Service Addressing

  A fundamental concept in TIPC is that of Service Addressing which makes it
  possible for a programmer to chose his own address, bind it to a server
  socket and let client programs use only that address for sending messages.

- Service Tracking

  A client wanting to wait for the availability of a server, uses the Service
  Tracking mechanism to subscribe for binding and unbinding/close events for
  sockets with the associated service address.

  The service tracking mechanism can also be used for Cluster Topology Tracking,
  i.e., subscribing for availability/non-availability of cluster nodes.

  Likewise, the service tracking mechanism can be used for Cluster Connectivity
  Tracking, i.e., subscribing for up/down events for individual links between
  cluster nodes.

- Transmission Modes

  Using a service address, a client can send datagram messages to a server socket.

  Using the same address type, it can establish a connection towards an accepting
  server socket.

  It can also use a service address to create and join a Communication Group,
  which is the TIPC manifestation of a brokerless message bus.

  Multicast with very good performance and scalability is available both in
  datagram mode and in communication group mode.

- Inter Node Links

  Communication between any two nodes in a cluster is maintained by one or two
  Inter Node Links, which both guarantee data traffic integrity and monitor
  the peer node's availability.

- Cluster Scalability

  By applying the Overlapping Ring Monitoring algorithm on the inter node links
  it is possible to scale TIPC clusters up to 1000 nodes with a maintained
  neighbor failure discovery time of 1-2 seconds. For smaller clusters this
  time can be made much shorter.

- Neighbor Discovery

  Neighbor Node Discovery in the cluster is done by Ethernet broadcast or UDP
  multicast, when any of those services are available. If not, configured peer
  IP addresses can be used.

- Configuration

  When running TIPC in single node mode no configuration whatsoever is needed.
  When running in cluster mode TIPC must as a minimum be given a node address
  (before Linux 4.17) and told which interface to attach to. The "tipc"
  configuration tool makes is possible to add and maintain many more
  configuration parameters.

- Performance

  TIPC message transfer latency times are better than in any other known protocol.
  Maximal byte throughput for inter-node connections is still somewhat lower than
  for TCP, while they are superior for intra-node and inter-container throughput
  on the same host.

- Language Support

  The TIPC user API has support for C, Python, Perl, Ruby, D and Go.

More Information
----------------

- How to set up TIPC:

  http://tipc.io/getting_started.html

- How to program with TIPC:

  http://tipc.io/programming.html

- How to contribute to TIPC:

- http://tipc.io/contacts.html

- More details about TIPC specification:

  http://tipc.io/protocol.html


Implementation
==============

TIPC is implemented as a kernel module in net/tipc/ directory.

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
