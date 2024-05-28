.. SPDX-License-Identifier: GPL-2.0

==================
AF_XDP TX Metadata
==================

This document describes how to enable offloads when transmitting packets
via :doc:`af_xdp`. Refer to :doc:`xdp-rx-metadata` on how to access similar
metadata on the receive side.

General Design
==============

The headroom for the metadata is reserved via ``tx_metadata_len`` in
``struct xdp_umem_reg``. The metadata length is therefore the same for
every socket that shares the same umem. The metadata layout is a fixed UAPI,
refer to ``union xsk_tx_metadata`` in ``include/uapi/linux/if_xdp.h``.
Thus, generally, the ``tx_metadata_len`` field above should contain
``sizeof(union xsk_tx_metadata)``.

The headroom and the metadata itself should be located right before
``xdp_desc->addr`` in the umem frame. Within a frame, the metadata
layout is as follows::

           tx_metadata_len
     /                         \
    +-----------------+---------+----------------------------+
    | xsk_tx_metadata | padding |          payload           |
    +-----------------+---------+----------------------------+
                                ^
                                |
                          xdp_desc->addr

An AF_XDP application can request headrooms larger than ``sizeof(struct
xsk_tx_metadata)``. The kernel will ignore the padding (and will still
use ``xdp_desc->addr - tx_metadata_len`` to locate
the ``xsk_tx_metadata``). For the frames that shouldn't carry
any metadata (i.e., the ones that don't have ``XDP_TX_METADATA`` option),
the metadata area is ignored by the kernel as well.

The flags field enables the particular offload:

- ``XDP_TXMD_FLAGS_TIMESTAMP``: requests the device to put transmission
  timestamp into ``tx_timestamp`` field of ``union xsk_tx_metadata``.
- ``XDP_TXMD_FLAGS_CHECKSUM``: requests the device to calculate L4
  checksum. ``csum_start`` specifies byte offset of where the checksumming
  should start and ``csum_offset`` specifies byte offset where the
  device should store the computed checksum.

Besides the flags above, in order to trigger the offloads, the first
packet's ``struct xdp_desc`` descriptor should set ``XDP_TX_METADATA``
bit in the ``options`` field. Also note that in a multi-buffer packet
only the first chunk should carry the metadata.

Software TX Checksum
====================

For development and testing purposes its possible to pass
``XDP_UMEM_TX_SW_CSUM`` flag to ``XDP_UMEM_REG`` UMEM registration call.
In this case, when running in ``XDK_COPY`` mode, the TX checksum
is calculated on the CPU. Do not enable this option in production because
it will negatively affect performance.

Querying Device Capabilities
============================

Every devices exports its offloads capabilities via netlink netdev family.
Refer to ``xsk-flags`` features bitmask in
``Documentation/netlink/specs/netdev.yaml``.

- ``tx-timestamp``: device supports ``XDP_TXMD_FLAGS_TIMESTAMP``
- ``tx-checksum``: device supports ``XDP_TXMD_FLAGS_CHECKSUM``

See ``tools/net/ynl/samples/netdev.c`` on how to query this information.

Example
=======

See ``tools/testing/selftests/bpf/xdp_hw_metadata.c`` for an example
program that handles TX metadata. Also see https://github.com/fomichev/xskgen
for a more bare-bones example.
