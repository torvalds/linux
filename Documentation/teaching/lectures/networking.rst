==================
Network Management
==================

`View slides <networking-slides.html>`_

.. slideconf::
   :autoslides: False
   :theme: single-level

Lecture objectives:
===================

.. slide:: Network Management
   :inline-contents: True
   :level: 2

   * Socket implementation

   * Routing implementation

   * Network Device Interface

   * Hardware and Software Acceleration Techniques


Network Management Overview
===========================

.. slide:: Network Management Overview
   :inline-contents: True
   :level: 2

   .. ditaa::
      :height: 100%

      +---------------------------+
      | Berkeley Socket Interface |
      +---------------------------+

      +---------------------------+
      |      Transport layer      |
      +-------------+-------------+
      |      TCP    |     UDP     |
      +-------------+-------------+

      +---------------------------+
      |      Network layer        |
      +-----+---------+-----------+
      | IP  | Routing | NetFilter |
      +-----+---------+-----------+

      +---------------------------+
      |     Data link layer       |
      +-------+-------+-----------+
      |  ETH  |  ARP  | BRIDGING  |
      +-------+-------+-----------+

      +---------------------------+
      |    Queuing discipline     |
      +---------------------------+

      +---------------------------+
      | Network device drivers    |
      +---------------------------+


Sockets Implementation Overview
===============================

.. slide:: Sockets Implementation Overview
   :inline-contents: True
   :level: 2

   .. ditaa::
      :height: 100%

                                        Socket
                                        File
      +------+                          Operations
      | FILE | ----------------------> +-----------+
      +------+                         | read      |
       | |       struct socket_alloc   +-----------+
       | |        +---------------+    | write     |
       | +------->| struct socket |    +-----------+
       | f_private| +-----------+ |    | select    |
       |          | | ...       | |    +-----------+
       |          | +-----------+ |    | ...       |
       |          +---------------+    +-----------+
       +--------->| struct inode  |
        f_inode   | +-----------+ |
                  | | ...       | |
                  | +-----------+ |
                  +---------------+


Sockets Families and Protocols
===============================

.. slide:: Sockets Families and Protocols
   :inline-contents: True
   :level: 2

   .. ditaa::
      :height: 100%



                        struct socket                +---------> struct proto_ops
                       +--------------------+        |          +-----------------+
                       | struct socket      |        |          | release         |
                       |                    |        |          +-----------------+
                       +--------------------+        |          | bind            |
                       | struct proto_ops * |--------+          +-----------------+
                       +--------------------+                   | connect         |
                       | ...           |                        +-----------------+
                       +---------------+                        | accept          |
             +---------| struct sock * |-------+                +-----------------+
             |         +---------------+       |                | sendmsg         |
             |                                 |                +-----------------+
             |                                 |                | recvmsg         |
             |                                 |                +-----------------+
             |                                 |                | poll            |
             |                                 |                +-----------------+
             |                                 |                | ...             |
             |                                 |                +-----------------+
             |                                 |
             v                                 v            +--> struct sk_prot
        struct tcp_sock                struct tcp_sock      |   +--------------------+
      +-------------------+          +-------------------+  |   | inet_dgram_connect |
      | struct inet_sock  |          | struct inet_sock  |  |   +--------------------+
      | +---------------+ |          | +---------------+ |  |   | inet_sendmsg       |
      | | struct sock   | |          | | struct sock   | |  |   +--------------------+
      | | +-----------+ | |          | | +-----------+ | |  |   | udp_poll           |
      | | | ...       | | |          | | | ...       | | |  |   +--------------------+
      | | +-----------+ | |          | | +-----------+ | |  |   | inet_release       |
      | +---------------+ |          | +---------------+ |  |   +--------------------+
      | | sk_prot *     | |          | | sk_prot *     | |--+   | inet_bind          |
      | +---------------+ |          | +---------------+ |      +--------------------+
      +-------------------+          +-------------------+      | ...                |
      |  ...              |          |  ...              |      +--------------------+
      +-------------------+          +-------------------+


Example: UDP send
-----------------

.. slide:: Example: UDP send
   :inline-contents: True
   :level: 2


   .. code-block:: c

      char c;
      struct sockaddr_in addr;
      int s;

      s = socket(AF_INET, SOCK_DGRAM, 0);
      connect(s, (struct sockaddr*)&addr, sizeof(addr));
      write(s, &c, 1);
      close(s);


.. slide:: Example: UDP send
   :inline-contents: True
   :level: 2

   .. ditaa::

      -:------------------------------------------------------------------------------------

      VFS layer                 sys_write → vfs_write → do_sync_write → filp->f_op->aio_write

      -:------------------------------------------------------------------------------------

      Generic socket layer      sock_aio_write → sock->ops->sendmsg

      -:------------------------------------------------------------------------------------

      IP socket layer           sk->sk_prot->sendmsg

      -:------------------------------------------------------------------------------------

      UDP socket layer          ip_append_data                   udp_flush_pending_frames
                                      |                              |
      -:------------------------------+------------------------------+-----------------------
                                      V                              V
      IP socket layer           skb = sock_alloc_send_skb();     ip_local_out
                                skb_queue_tail(sk, skb)

      -:------------------------------------------------------------------------------------

                                         routing


Network processing phases
=========================

.. slide:: Network processing phases
   :inline-contents: True
   :level: 2

   * Interrupt handler - device driver fetches data from the RX ring,
     creates a network packet and queues it to the network stack for
     processing

   * NET_SOFTIRQ - packet goes through the stack layer and it is
     processed: decapsulate Ethernet frame, check IP packet and route
     it, if local packet decapsulate protocol packet (e.g. TCP) and
     queues it to a socket

   * Process context - application fetches data from the socket queue
     or pushes data to the socket queue


Packet Routing
==============

.. slide:: Packet Routing
   :inline-contents: True
   :level: 2

   .. ditaa::

      +----------------------+           +----------------------+
      |     Application      |           |     Application      |
      +----------------------+           +----------------------+
         |            ^                     |            ^
         | send()     | recv()              | send()     | recv()
         V            |                     V            |
      +----------------------+           +----------------------+
      |       Socket         |           |       Socket         |
      +----------------------+           +----------------------+
         |            ^                     |            ^
         |            |                     |            |
         v            |                     v            |
      +---------------------------------------------------------+
      |                    Transport layer                      |
      +---------------------------------------------------------+
         |            ^                    |             ^
         |            |                    |             |
         v            |                    v             |
      +---------------------------------------------------------+
      |                    Network layer                        |
      +---------------------------------------------------------+
          |                                         ^
          |                                         |
          v                                         |
      /---------------------------------------------------------\
      |                     Routing                             |  ----> Drop packet
      \---------------------------------------------------------/
          ^             |             ^             |
          | RX          | TX          | RX          | TX
          |             v             |             v
      +-----------------------+   +-----------------------+
      | Network Device Driver |   | Network Device Driver |
      +-----------------------+   +-----------------------+


Routing Table(s)
----------------

.. slide:: Routing Table
   :inline-contents: True
   :level: 2


   .. code-block:: shell

      tavi@desktop-tavi:~/src/linux$ ip route list table main
      default via 172.30.240.1 dev eth0
      172.30.240.0/20 dev eth0 proto kernel scope link src 172.30.249.241

      tavi@desktop-tavi:~/src/linux$ ip route list table local
      broadcast 127.0.0.0 dev lo proto kernel scope link src 127.0.0.1
      local 127.0.0.0/8 dev lo proto kernel scope host src 127.0.0.1
      local 127.0.0.1 dev lo proto kernel scope host src 127.0.0.1
      broadcast 127.255.255.255 dev lo proto kernel scope link src 127.0.0.1
      broadcast 172.30.240.0 dev eth0 proto kernel scope link src 172.30.249.241
      local 172.30.249.241 dev eth0 proto kernel scope host src 172.30.249.241
      broadcast 172.30.255.255 dev eth0 proto kernel scope link src 172.30.249.241

      tavi@desktop-tavi:~/src/linux$ ip rule list
      0:      from all lookup local
      32766:  from all lookup main
      32767:  from all lookup default


Routing Policy Database
-----------------------

.. slide:: Routing Policy Database
   :inline-contents: True
   :level: 2

   * "Regular" routing only uses the destination address

   * To increase flexibility a "Routing Policy Database" is used that
     allows different routing based on other fields such as the source
     address, protocol type, transport ports, etc.

   * This is encoded as a list of rules that are evaluated based on
     their priority (priority 0 is the highest)

   * Each rule has a selector (how to match the packet) and an
     action (what action to take if the packet matches)

   * Selectors: source address, destination address, type of service (TOS),
     input interface, output interface, etc.

   * Action: lookup / unicast - use given routing table, blackhole -
     drop packet, unreachable - send ICMP unreachable message and drop
     packet, etc.



Routing table processing
------------------------

.. slide:: Routing table processing
   :inline-contents: True
   :level: 2

   * Special table for local addreses -> route packets to sockets
     based on family, type, ports

   * Check every routing entry for starting with the most specific
     routes (e.g. 192.168.0.0/24 is checked before 192.168.0.0/16)

   * A route matches if the packet destination addreess logical ORed
     with the subnet mask equals the subnet address

   * Once a route matches the following information is retrieved:
     interface, link layer next-hop address, network next host address


Forwarding Information Database
-------------------------------

.. slide:: Forward Information Database (removed in 3.6)
   :inline-contents: True
   :level: 2

   |_|

   .. image::  ../res/fidb-overview.png


.. slide:: Forward Information Database (removed in 3.6)
   :inline-contents: True
   :level: 2

   .. image::  ../res/fidb-details.png

.. slide:: Routing Cache (removed in 3.6)
   :inline-contents: True
   :level: 2

   |_|

   .. image::  ../res/routing-cache.png

.. slide:: FIB TRIE
   :inline-contents: True
   :level: 2

   |_|

   .. image::  ../res/fib-trie.png

.. slide:: Compressed Trie
   :inline-contents: True
   :level: 2

   |_|

   .. image::  ../res/fib-trie-compressed.png


Netfilter
=========

.. slide:: Netfilter
   :inline-contents: True
   :level: 2


   * Framework that implements packet filtering and NAT

   * It uses hooks inserted in key places in the packet flow:

     * NF_IP_PRE_ROUTING

     * NF_IP_LOCAL_IN

     * NF_IP_FORWARD

     * NF_IP_LOCAL_OUT

     * NF_IP_POST_ROUTING

     * NF_IP_NUMHOOKS



Network packets / skbs (struct sk_buff)
=======================================

.. slide:: Network packets (skbs)
   :inline-contents: True
   :level: 2

   .. image:: ../res/skb.png


.. slide:: struct sk_buff
   :inline-contents: True
   :level: 2

   .. code-block:: c

      struct sk_buff {
          struct sk_buff *next;
          struct sk_buff *prev;

          struct sock *sk;
          ktime_t tstamp;
          struct net_device *dev;
          char cb[48];

          unsigned int len,
          data_len;
          __u16 mac_len,
          hdr_len;

          void (*destructor)(struct sk_buff *skb);

          sk_buff_data_t transport_header;
          sk_buff_data_t network_header;
          sk_buff_data_t mac_header;
          sk_buff_data_t tail;
          sk_buff_data_t end;

          unsigned char *head,
          *data;
          unsigned int truesize;
          atomic_t users;


.. slide:: skb APIs
   :inline-contents: True
   :level: 2

   .. code-block:: c

      /* reserve head room */
      void skb_reserve(struct sk_buff *skb, int len);

      /* add data to the end */
      unsigned char *skb_put(struct sk_buff *skb, unsigned int len);

      /* add data to the top */
      unsigned char *skb_push(struct sk_buff *skb, unsigned int len);

      /* discard data at the top */
      unsigned char *skb_pull(struct sk_buff *skb, unsigned int len);

      /* discard data at the end */
      unsigned char *skb_trim(struct sk_buff *skb, unsigned int len);

      unsigned char *skb_transport_header(const struct sk_buff *skb);

      void skb_reset_transport_header(struct sk_buff *skb);

      void skb_set_transport_header(struct sk_buff *skb, const int offset);

      unsigned char *skb_network_header(const struct sk_buff *skb);

      void skb_reset_network_header(struct sk_buff *skb);

      void skb_set_network_header(struct sk_buff *skb, const int offset);

      unsigned char *skb_mac_header(const struct sk_buff *skb);

      int skb_mac_header_was_set(const struct sk_buff *skb);

      void skb_reset_mac_header(struct sk_buff *skb);

      void skb_set_mac_header(struct sk_buff *skb, const int offset);


.. slide:: skb data management
   :inline-contents: True
   :level: 2

   |_|

   .. ditaa::
      :height: 50%

                    Head
                ^ +---------------+
      skb_push  | |               | | skb_reserve
                  +---------------+ v
                  | Data          | | skb_pull
                ^ |               | v
      skb_trim  | |          Tail |
                  +---------------+
                  |               | | skb_put
                  +---------------+ v
                              End


Network Device
==============

.. slide:: Network Device Interface
   :inline-contents: True
   :level: 2

   .. image::  ../res/net-dev-hw.png


.. slide:: Advanced features
   :inline-contents: True
   :level: 2

   * Scatter-Gather

   * Checksum offloading: Ethernet, IP, UDP, TCP

   * Adaptive interrupt handling (coalescence, adaptive)



Hardware and Software Acceleration Techniques
=============================================

.. slide:: TCP offload
   :inline-contents: True
   :level: 2

   * Full offload - Implement TCP/IP stack in hardware

   * Issues:

     * Scaling number of connections

     * Security

     * Conformance

.. slide:: Performance observation
   :inline-contents: True
   :level: 2

   * Performance is proportional with the number of packets to be
     processed

   * Example: if an end-point can process 60K pps

     * 1538 MSS -> 738Mbps
     * 2038 MSS -> 978Mbps
     * 9038 MSS -> 4.3Gbps
     * 20738 MSS -> 9.9Gbps

.. slide:: Stateless offload
   :inline-contents: True
   :level: 2

   * The networking stack processes large packets

   * TX path: the hardware splits large packets in smaller packets
     (TCP Segmentation Offload)

   * RX path: the hardware aggregates small packets into larger
     packets (Large Receive Offload - LRO)


.. slide:: TCP Segmentation Offload)
   :inline-contents: True
   :level: 2

   .. image::  ../res/tso.png

.. slide:: Large Receive Offload
   :inline-contents: True
   :level: 2

   .. image::  ../res/lro.png



