.. SPDX-License-Identifier: GPL-2.0

======
AF_XDP
======

Overview
========

AF_XDP is an address family that is optimized for high performance
packet processing.

This document assumes that the reader is familiar with BPF and XDP. If
not, the Cilium project has an excellent reference guide at
http://cilium.readthedocs.io/en/latest/bpf/.

Using the XDP_REDIRECT action from an XDP program, the program can
redirect ingress frames to other XDP enabled netdevs, using the
bpf_redirect_map() function. AF_XDP sockets enable the possibility for
XDP programs to redirect frames to a memory buffer in a user-space
application.

An AF_XDP socket (XSK) is created with the normal socket()
syscall. Associated with each XSK are two rings: the RX ring and the
TX ring. A socket can receive packets on the RX ring and it can send
packets on the TX ring. These rings are registered and sized with the
setsockopts XDP_RX_RING and XDP_TX_RING, respectively. It is mandatory
to have at least one of these rings for each socket. An RX or TX
descriptor ring points to a data buffer in a memory area called a
UMEM. RX and TX can share the same UMEM so that a packet does not have
to be copied between RX and TX. Moreover, if a packet needs to be kept
for a while due to a possible retransmit, the descriptor that points
to that packet can be changed to point to another and reused right
away. This again avoids copying data.

The UMEM consists of a number of equally sized chunks. A descriptor in
one of the rings references a frame by referencing its addr. The addr
is simply an offset within the entire UMEM region. The user space
allocates memory for this UMEM using whatever means it feels is most
appropriate (malloc, mmap, huge pages, etc). This memory area is then
registered with the kernel using the new setsockopt XDP_UMEM_REG. The
UMEM also has two rings: the FILL ring and the COMPLETION ring. The
fill ring is used by the application to send down addr for the kernel
to fill in with RX packet data. References to these frames will then
appear in the RX ring once each packet has been received. The
completion ring, on the other hand, contains frame addr that the
kernel has transmitted completely and can now be used again by user
space, for either TX or RX. Thus, the frame addrs appearing in the
completion ring are addrs that were previously transmitted using the
TX ring. In summary, the RX and FILL rings are used for the RX path
and the TX and COMPLETION rings are used for the TX path.

The socket is then finally bound with a bind() call to a device and a
specific queue id on that device, and it is not until bind is
completed that traffic starts to flow.

The UMEM can be shared between processes, if desired. If a process
wants to do this, it simply skips the registration of the UMEM and its
corresponding two rings, sets the XDP_SHARED_UMEM flag in the bind
call and submits the XSK of the process it would like to share UMEM
with as well as its own newly created XSK socket. The new process will
then receive frame addr references in its own RX ring that point to
this shared UMEM. Note that since the ring structures are
single-consumer / single-producer (for performance reasons), the new
process has to create its own socket with associated RX and TX rings,
since it cannot share this with the other process. This is also the
reason that there is only one set of FILL and COMPLETION rings per
UMEM. It is the responsibility of a single process to handle the UMEM.

How is then packets distributed from an XDP program to the XSKs? There
is a BPF map called XSKMAP (or BPF_MAP_TYPE_XSKMAP in full). The
user-space application can place an XSK at an arbitrary place in this
map. The XDP program can then redirect a packet to a specific index in
this map and at this point XDP validates that the XSK in that map was
indeed bound to that device and ring number. If not, the packet is
dropped. If the map is empty at that index, the packet is also
dropped. This also means that it is currently mandatory to have an XDP
program loaded (and one XSK in the XSKMAP) to be able to get any
traffic to user space through the XSK.

AF_XDP can operate in two different modes: XDP_SKB and XDP_DRV. If the
driver does not have support for XDP, or XDP_SKB is explicitly chosen
when loading the XDP program, XDP_SKB mode is employed that uses SKBs
together with the generic XDP support and copies out the data to user
space. A fallback mode that works for any network device. On the other
hand, if the driver has support for XDP, it will be used by the AF_XDP
code to provide better performance, but there is still a copy of the
data into user space.

Concepts
========

In order to use an AF_XDP socket, a number of associated objects need
to be setup.

Jonathan Corbet has also written an excellent article on LWN,
"Accelerating networking with AF_XDP". It can be found at
https://lwn.net/Articles/750845/.

UMEM
----

UMEM is a region of virtual contiguous memory, divided into
equal-sized frames. An UMEM is associated to a netdev and a specific
queue id of that netdev. It is created and configured (chunk size,
headroom, start address and size) by using the XDP_UMEM_REG setsockopt
system call. A UMEM is bound to a netdev and queue id, via the bind()
system call.

An AF_XDP is socket linked to a single UMEM, but one UMEM can have
multiple AF_XDP sockets. To share an UMEM created via one socket A,
the next socket B can do this by setting the XDP_SHARED_UMEM flag in
struct sockaddr_xdp member sxdp_flags, and passing the file descriptor
of A to struct sockaddr_xdp member sxdp_shared_umem_fd.

The UMEM has two single-producer/single-consumer rings, that are used
to transfer ownership of UMEM frames between the kernel and the
user-space application.

Rings
-----

There are a four different kind of rings: Fill, Completion, RX and
TX. All rings are single-producer/single-consumer, so the user-space
application need explicit synchronization of multiple
processes/threads are reading/writing to them.

The UMEM uses two rings: Fill and Completion. Each socket associated
with the UMEM must have an RX queue, TX queue or both. Say, that there
is a setup with four sockets (all doing TX and RX). Then there will be
one Fill ring, one Completion ring, four TX rings and four RX rings.

The rings are head(producer)/tail(consumer) based rings. A producer
writes the data ring at the index pointed out by struct xdp_ring
producer member, and increasing the producer index. A consumer reads
the data ring at the index pointed out by struct xdp_ring consumer
member, and increasing the consumer index.

The rings are configured and created via the _RING setsockopt system
calls and mmapped to user-space using the appropriate offset to mmap()
(XDP_PGOFF_RX_RING, XDP_PGOFF_TX_RING, XDP_UMEM_PGOFF_FILL_RING and
XDP_UMEM_PGOFF_COMPLETION_RING).

The size of the rings need to be of size power of two.

UMEM Fill Ring
~~~~~~~~~~~~~~

The Fill ring is used to transfer ownership of UMEM frames from
user-space to kernel-space. The UMEM addrs are passed in the ring. As
an example, if the UMEM is 64k and each chunk is 4k, then the UMEM has
16 chunks and can pass addrs between 0 and 64k.

Frames passed to the kernel are used for the ingress path (RX rings).

The user application produces UMEM addrs to this ring. Note that, if
running the application with aligned chunk mode, the kernel will mask
the incoming addr.  E.g. for a chunk size of 2k, the log2(2048) LSB of
the addr will be masked off, meaning that 2048, 2050 and 3000 refers
to the same chunk. If the user application is run in the unaligned
chunks mode, then the incoming addr will be left untouched.


UMEM Completion Ring
~~~~~~~~~~~~~~~~~~~~

The Completion Ring is used transfer ownership of UMEM frames from
kernel-space to user-space. Just like the Fill ring, UMEM indicies are
used.

Frames passed from the kernel to user-space are frames that has been
sent (TX ring) and can be used by user-space again.

The user application consumes UMEM addrs from this ring.


RX Ring
~~~~~~~

The RX ring is the receiving side of a socket. Each entry in the ring
is a struct xdp_desc descriptor. The descriptor contains UMEM offset
(addr) and the length of the data (len).

If no frames have been passed to kernel via the Fill ring, no
descriptors will (or can) appear on the RX ring.

The user application consumes struct xdp_desc descriptors from this
ring.

TX Ring
~~~~~~~

The TX ring is used to send frames. The struct xdp_desc descriptor is
filled (index, length and offset) and passed into the ring.

To start the transfer a sendmsg() system call is required. This might
be relaxed in the future.

The user application produces struct xdp_desc descriptors to this
ring.

XSKMAP / BPF_MAP_TYPE_XSKMAP
----------------------------

On XDP side there is a BPF map type BPF_MAP_TYPE_XSKMAP (XSKMAP) that
is used in conjunction with bpf_redirect_map() to pass the ingress
frame to a socket.

The user application inserts the socket into the map, via the bpf()
system call.

Note that if an XDP program tries to redirect to a socket that does
not match the queue configuration and netdev, the frame will be
dropped. E.g. an AF_XDP socket is bound to netdev eth0 and
queue 17. Only the XDP program executing for eth0 and queue 17 will
successfully pass data to the socket. Please refer to the sample
application (samples/bpf/) in for an example.

Usage
=====

In order to use AF_XDP sockets there are two parts needed. The
user-space application and the XDP program. For a complete setup and
usage example, please refer to the sample application. The user-space
side is xdpsock_user.c and the XDP side is part of libbpf.

The XDP code sample included in tools/lib/bpf/xsk.c is the following::

   SEC("xdp_sock") int xdp_sock_prog(struct xdp_md *ctx)
   {
       int index = ctx->rx_queue_index;

       // A set entry here means that the correspnding queue_id
       // has an active AF_XDP socket bound to it.
       if (bpf_map_lookup_elem(&xsks_map, &index))
           return bpf_redirect_map(&xsks_map, index, 0);

       return XDP_PASS;
   }

Naive ring dequeue and enqueue could look like this::

    // struct xdp_rxtx_ring {
    // 	__u32 *producer;
    // 	__u32 *consumer;
    // 	struct xdp_desc *desc;
    // };

    // struct xdp_umem_ring {
    // 	__u32 *producer;
    // 	__u32 *consumer;
    // 	__u64 *desc;
    // };

    // typedef struct xdp_rxtx_ring RING;
    // typedef struct xdp_umem_ring RING;

    // typedef struct xdp_desc RING_TYPE;
    // typedef __u64 RING_TYPE;

    int dequeue_one(RING *ring, RING_TYPE *item)
    {
        __u32 entries = *ring->producer - *ring->consumer;

        if (entries == 0)
            return -1;

        // read-barrier!

        *item = ring->desc[*ring->consumer & (RING_SIZE - 1)];
        (*ring->consumer)++;
        return 0;
    }

    int enqueue_one(RING *ring, const RING_TYPE *item)
    {
        u32 free_entries = RING_SIZE - (*ring->producer - *ring->consumer);

        if (free_entries == 0)
            return -1;

        ring->desc[*ring->producer & (RING_SIZE - 1)] = *item;

        // write-barrier!

        (*ring->producer)++;
        return 0;
    }


For a more optimized version, please refer to the sample application.

Sample application
==================

There is a xdpsock benchmarking/test application included that
demonstrates how to use AF_XDP sockets with both private and shared
UMEMs. Say that you would like your UDP traffic from port 4242 to end
up in queue 16, that we will enable AF_XDP on. Here, we use ethtool
for this::

      ethtool -N p3p2 rx-flow-hash udp4 fn
      ethtool -N p3p2 flow-type udp4 src-port 4242 dst-port 4242 \
          action 16

Running the rxdrop benchmark in XDP_DRV mode can then be done
using::

      samples/bpf/xdpsock -i p3p2 -q 16 -r -N

For XDP_SKB mode, use the switch "-S" instead of "-N" and all options
can be displayed with "-h", as usual.

FAQ
=======

Q: I am not seeing any traffic on the socket. What am I doing wrong?

A: When a netdev of a physical NIC is initialized, Linux usually
   allocates one Rx and Tx queue pair per core. So on a 8 core system,
   queue ids 0 to 7 will be allocated, one per core. In the AF_XDP
   bind call or the xsk_socket__create libbpf function call, you
   specify a specific queue id to bind to and it is only the traffic
   towards that queue you are going to get on you socket. So in the
   example above, if you bind to queue 0, you are NOT going to get any
   traffic that is distributed to queues 1 through 7. If you are
   lucky, you will see the traffic, but usually it will end up on one
   of the queues you have not bound to.

   There are a number of ways to solve the problem of getting the
   traffic you want to the queue id you bound to. If you want to see
   all the traffic, you can force the netdev to only have 1 queue, queue
   id 0, and then bind to queue 0. You can use ethtool to do this::

     sudo ethtool -L <interface> combined 1

   If you want to only see part of the traffic, you can program the
   NIC through ethtool to filter out your traffic to a single queue id
   that you can bind your XDP socket to. Here is one example in which
   UDP traffic to and from port 4242 are sent to queue 2::

     sudo ethtool -N <interface> rx-flow-hash udp4 fn
     sudo ethtool -N <interface> flow-type udp4 src-port 4242 dst-port \
     4242 action 2

   A number of other ways are possible all up to the capabilitites of
   the NIC you have.

Credits
=======

- Björn Töpel (AF_XDP core)
- Magnus Karlsson (AF_XDP core)
- Alexander Duyck
- Alexei Starovoitov
- Daniel Borkmann
- Jesper Dangaard Brouer
- John Fastabend
- Jonathan Corbet (LWN coverage)
- Michael S. Tsirkin
- Qi Z Zhang
- Willem de Bruijn
