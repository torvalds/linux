.. SPDX-License-Identifier: GPL-2.0

==================
AF_XDP TX Metadata
==================

This document describes how to enable offloads when transmitting packets
via :doc:`af_xdp`. Refer to :doc:`xdp-rx-metadata` on how to access similar
metadata on the receive side.

General Design
==============

The headroom for the metadata is reserved via ``tx_metadata_len`` and
``XDP_UMEM_TX_METADATA_LEN`` flag in ``struct xdp_umem_reg``. The metadata
length is therefore the same for every socket that shares the same umem.
The metadata layout is a fixed UAPI, refer to ``union xsk_tx_metadata`` in
``include/uapi/linux/if_xdp.h``. Thus, generally, the ``tx_metadata_len``
field above should contain ``sizeof(union xsk_tx_metadata)``.

Note that in the original implementation the ``XDP_UMEM_TX_METADATA_LEN``
flag was not required. Applications might attempt to create a umem
with a flag first and if it fails, do another attempt without a flag.

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
- ``XDP_TXMD_FLAGS_LAUNCH_TIME``: requests the device to schedule the
  packet for transmission at a pre-determined time called launch time. The
  value of launch time is indicated by ``launch_time`` field of
  ``union xsk_tx_metadata``.

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

Launch Time
===========

The value of the requested launch time should be based on the device's PTP
Hardware Clock (PHC) to ensure accuracy. AF_XDP takes a different data path
compared to the ETF queuing discipline, which organizes packets and delays
their transmission. Instead, AF_XDP immediately hands off the packets to
the device driver without rearranging their order or holding them prior to
transmission. Since the driver maintains FIFO behavior and does not perform
packet reordering, a packet with a launch time request will block other
packets in the same Tx Queue until it is sent. Therefore, it is recommended
to allocate separate queue for scheduling traffic that is intended for
future transmission.

In scenarios where the launch time offload feature is disabled, the device
driver is expected to disregard the launch time request. For correct
interpretation and meaningful operation, the launch time should never be
set to a value larger than the farthest programmable time in the future
(the horizon). Different devices have different hardware limitations on the
launch time offload feature.

stmmac driver
-------------

For stmmac, TSO and launch time (TBS) features are mutually exclusive for
each individual Tx Queue. By default, the driver configures Tx Queue 0 to
support TSO and the rest of the Tx Queues to support TBS. The launch time
hardware offload feature can be enabled or disabled by using the tc-etf
command to call the driver's ndo_setup_tc() callback.

The value of the launch time that is programmed in the Enhanced Normal
Transmit Descriptors is a 32-bit value, where the most significant 8 bits
represent the time in seconds and the remaining 24 bits represent the time
in 256 ns increments. The programmed launch time is compared against the
PTP time (bits[39:8]) and rolls over after 256 seconds. Therefore, the
horizon of the launch time for dwmac4 and dwxlgmac2 is 128 seconds in the
future.

igc driver
----------

For igc, all four Tx Queues support the launch time feature. The launch
time hardware offload feature can be enabled or disabled by using the
tc-etf command to call the driver's ndo_setup_tc() callback. When entering
TSN mode, the igc driver will reset the device and create a default Qbv
schedule with a 1-second cycle time, with all Tx Queues open at all times.

The value of the launch time that is programmed in the Advanced Transmit
Context Descriptor is a relative offset to the starting time of the Qbv
transmission window of the queue. The Frst flag of the descriptor can be
set to schedule the packet for the next Qbv cycle. Therefore, the horizon
of the launch time for i225 and i226 is the ending time of the next cycle
of the Qbv transmission window of the queue. For example, when the Qbv
cycle time is set to 1 second, the horizon of the launch time ranges
from 1 second to 2 seconds, depending on where the Qbv cycle is currently
running.

Querying Device Capabilities
============================

Every devices exports its offloads capabilities via netlink netdev family.
Refer to ``xsk-flags`` features bitmask in
``Documentation/netlink/specs/netdev.yaml``.

- ``tx-timestamp``: device supports ``XDP_TXMD_FLAGS_TIMESTAMP``
- ``tx-checksum``: device supports ``XDP_TXMD_FLAGS_CHECKSUM``
- ``tx-launch-time-fifo``: device supports ``XDP_TXMD_FLAGS_LAUNCH_TIME``

See ``tools/net/ynl/samples/netdev.c`` on how to query this information.

Example
=======

See ``tools/testing/selftests/bpf/xdp_hw_metadata.c`` for an example
program that handles TX metadata. Also see https://github.com/fomichev/xskgen
for a more bare-bones example.
