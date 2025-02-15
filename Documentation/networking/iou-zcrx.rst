.. SPDX-License-Identifier: GPL-2.0

=====================
io_uring zero copy Rx
=====================

Introduction
============

io_uring zero copy Rx (ZC Rx) is a feature that removes kernel-to-user copy on
the network receive path, allowing packet data to be received directly into
userspace memory. This feature is different to TCP_ZEROCOPY_RECEIVE in that
there are no strict alignment requirements and no need to mmap()/munmap().
Compared to kernel bypass solutions such as e.g. DPDK, the packet headers are
processed by the kernel TCP stack as normal.

NIC HW Requirements
===================

Several NIC HW features are required for io_uring ZC Rx to work. For now the
kernel API does not configure the NIC and it must be done by the user.

Header/data split
-----------------

Required to split packets at the L4 boundary into a header and a payload.
Headers are received into kernel memory as normal and processed by the TCP
stack as normal. Payloads are received into userspace memory directly.

Flow steering
-------------

Specific HW Rx queues are configured for this feature, but modern NICs
typically distribute flows across all HW Rx queues. Flow steering is required
to ensure that only desired flows are directed towards HW queues that are
configured for io_uring ZC Rx.

RSS
---

In addition to flow steering above, RSS is required to steer all other non-zero
copy flows away from queues that are configured for io_uring ZC Rx.

Usage
=====

Setup NIC
---------

Must be done out of band for now.

Ensure there are at least two queues::

  ethtool -L eth0 combined 2

Enable header/data split::

  ethtool -G eth0 tcp-data-split on

Carve out half of the HW Rx queues for zero copy using RSS::

  ethtool -X eth0 equal 1

Set up flow steering, bearing in mind that queues are 0-indexed::

  ethtool -N eth0 flow-type tcp6 ... action 1

Setup io_uring
--------------

This section describes the low level io_uring kernel API. Please refer to
liburing documentation for how to use the higher level API.

Create an io_uring instance with the following required setup flags::

  IORING_SETUP_SINGLE_ISSUER
  IORING_SETUP_DEFER_TASKRUN
  IORING_SETUP_CQE32

Create memory area
------------------

Allocate userspace memory area for receiving zero copy data::

  void *area_ptr = mmap(NULL, area_size,
                        PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE,
                        0, 0);

Create refill ring
------------------

Allocate memory for a shared ringbuf used for returning consumed buffers::

  void *ring_ptr = mmap(NULL, ring_size,
                        PROT_READ | PROT_WRITE,
                        MAP_ANONYMOUS | MAP_PRIVATE,
                        0, 0);

This refill ring consists of some space for the header, followed by an array of
``struct io_uring_zcrx_rqe``::

  size_t rq_entries = 4096;
  size_t ring_size = rq_entries * sizeof(struct io_uring_zcrx_rqe) + PAGE_SIZE;
  /* align to page size */
  ring_size = (ring_size + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);

Register ZC Rx
--------------

Fill in registration structs::

  struct io_uring_zcrx_area_reg area_reg = {
    .addr = (__u64)(unsigned long)area_ptr,
    .len = area_size,
    .flags = 0,
  };

  struct io_uring_region_desc region_reg = {
    .user_addr = (__u64)(unsigned long)ring_ptr,
    .size = ring_size,
    .flags = IORING_MEM_REGION_TYPE_USER,
  };

  struct io_uring_zcrx_ifq_reg reg = {
    .if_idx = if_nametoindex("eth0"),
    /* this is the HW queue with desired flow steered into it */
    .if_rxq = 1,
    .rq_entries = rq_entries,
    .area_ptr = (__u64)(unsigned long)&area_reg,
    .region_ptr = (__u64)(unsigned long)&region_reg,
  };

Register with kernel::

  io_uring_register_ifq(ring, &reg);

Map refill ring
---------------

The kernel fills in fields for the refill ring in the registration ``struct
io_uring_zcrx_ifq_reg``. Map it into userspace::

  struct io_uring_zcrx_rq refill_ring;

  refill_ring.khead = (unsigned *)((char *)ring_ptr + reg.offsets.head);
  refill_ring.khead = (unsigned *)((char *)ring_ptr + reg.offsets.tail);
  refill_ring.rqes =
    (struct io_uring_zcrx_rqe *)((char *)ring_ptr + reg.offsets.rqes);
  refill_ring.rq_tail = 0;
  refill_ring.ring_ptr = ring_ptr;

Receiving data
--------------

Prepare a zero copy recv request::

  struct io_uring_sqe *sqe;

  sqe = io_uring_get_sqe(ring);
  io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, fd, NULL, 0, 0);
  sqe->ioprio |= IORING_RECV_MULTISHOT;

Now, submit and wait::

  io_uring_submit_and_wait(ring, 1);

Finally, process completions::

  struct io_uring_cqe *cqe;
  unsigned int count = 0;
  unsigned int head;

  io_uring_for_each_cqe(ring, head, cqe) {
    struct io_uring_zcrx_cqe *rcqe = (struct io_uring_zcrx_cqe *)(cqe + 1);

    unsigned long mask = (1ULL << IORING_ZCRX_AREA_SHIFT) - 1;
    unsigned char *data = area_ptr + (rcqe->off & mask);
    /* do something with the data */

    count++;
  }
  io_uring_cq_advance(ring, count);

Recycling buffers
-----------------

Return buffers back to the kernel to be used again::

  struct io_uring_zcrx_rqe *rqe;
  unsigned mask = refill_ring.ring_entries - 1;
  rqe = &refill_ring.rqes[refill_ring.rq_tail & mask];

  unsigned long area_offset = rcqe->off & ~IORING_ZCRX_AREA_MASK;
  rqe->off = area_offset | area_reg.rq_area_token;
  rqe->len = cqe->res;
  IO_URING_WRITE_ONCE(*refill_ring.ktail, ++refill_ring.rq_tail);

Testing
=======

See ``tools/testing/selftests/drivers/net/hw/iou-zcrx.c``
