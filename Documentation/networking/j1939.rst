.. SPDX-License-Identifier: (GPL-2.0 OR MIT)

===================
J1939 Documentation
===================

Overview / What Is J1939
========================

SAE J1939 defines a higher layer protocol on CAN. It implements a more
sophisticated addressing scheme and extends the maximum packet size above 8
bytes. Several derived specifications exist, which differ from the original
J1939 on the application level, like MilCAN A, NMEA2000, and especially
ISO-11783 (ISOBUS). This last one specifies the so-called ETP (Extended
Transport Protocol), which has been included in this implementation. This
results in a maximum packet size of ((2 ^ 24) - 1) * 7 bytes == 111 MiB.

Specifications used
-------------------

* SAE J1939-21 : data link layer
* SAE J1939-81 : network management
* ISO 11783-6  : Virtual Terminal (Extended Transport Protocol)

.. _j1939-motivation:

Motivation
==========

Given the fact there's something like SocketCAN with an API similar to BSD
sockets, we found some reasons to justify a kernel implementation for the
addressing and transport methods used by J1939.

* **Addressing:** when a process on an ECU communicates via J1939, it should
  not necessarily know its source address. Although, at least one process per
  ECU should know the source address. Other processes should be able to reuse
  that address. This way, address parameters for different processes
  cooperating for the same ECU, are not duplicated. This way of working is
  closely related to the UNIX concept, where programs do just one thing and do
  it well.

* **Dynamic addressing:** Address Claiming in J1939 is time critical.
  Furthermore, data transport should be handled properly during the address
  negotiation. Putting this functionality in the kernel eliminates it as a
  requirement for _every_ user space process that communicates via J1939. This
  results in a consistent J1939 bus with proper addressing.

* **Transport:** both TP & ETP reuse some PGNs to relay big packets over them.
  Different processes may thus use the same TP & ETP PGNs without actually
  knowing it. The individual TP & ETP sessions _must_ be serialized
  (synchronized) between different processes. The kernel solves this problem
  properly and eliminates the serialization (synchronization) as a requirement
  for _every_ user space process that communicates via J1939.

J1939 defines some other features (relaying, gateway, fast packet transport,
...). In-kernel code for these would not contribute to protocol stability.
Therefore, these parts are left to user space.

The J1939 sockets operate on CAN network devices (see SocketCAN). Any J1939
user space library operating on CAN raw sockets will still operate properly.
Since such a library does not communicate with the in-kernel implementation, care
must be taken that these two do not interfere. In practice, this means they
cannot share ECU addresses. A single ECU (or virtual ECU) address is used by
the library exclusively, or by the in-kernel system exclusively.

J1939 concepts
==============

Data Sent to the J1939 Stack
----------------------------

The data buffers sent to the J1939 stack from user space are not CAN frames
themselves. Instead, they are payloads that the J1939 stack converts into
proper CAN frames based on the size of the buffer and the type of transfer. The
size of the buffer influences how the stack processes the data and determines
the internal code path used for the transfer.

**Handling of Different Buffer Sizes:**

- **Buffers with a size of 8 bytes or less:**

  - These are handled as simple sessions internally within the stack.

  - The stack converts the buffer directly into a single CAN frame without
    fragmentation.

  - This type of transfer does not require an actual client (receiver) on the
    receiving side.

- **Buffers up to 1785 bytes:**

  - These are automatically handled as J1939 Transport Protocol (TP) transfers.

  - Internally, the stack splits the buffer into multiple 8-byte CAN frames.

  - TP transfers can be unicast or broadcast.

  - **Broadcast TP:** Does not require a receiver on the other side and can be
    used in broadcast scenarios.

  - **Unicast TP:** Requires an active receiver (client) on the other side to
    acknowledge the transfer.

- **Buffers from 1786 bytes up to 111 MiB:**

  - These are handled as ISO 11783 Extended Transport Protocol (ETP) transfers.

  - ETP transfers are used for larger payloads and are split into multiple CAN
    frames internally.

  - **ETP transfers (unicast):** Require a receiver on the other side to
    process the incoming data and acknowledge each step of the transfer.

  - ETP transfers cannot be broadcast like TP transfers, and always require a
    receiver for operation.

**Non-Blocking Operation with `MSG_DONTWAIT`:**

The J1939 stack supports non-blocking operation when used in combination with
the `MSG_DONTWAIT` flag. In this mode, the stack attempts to take as much data
as the available memory for the socket allows. It returns the amount of data
that was successfully taken, and it is the responsibility of user space to
monitor this value and handle partial transfers.

- If the stack cannot take the entire buffer, it returns the number of bytes
  successfully taken, and user space should handle the remainder.

- **Error handling:** When using `MSG_DONTWAIT`, the user must rely on the
  error queue to detect transfer errors. See the **SO_J1939_ERRQUEUE** section
  for details on how to subscribe to error notifications. Without the error
  queue, there is no other way for user space to be notified of transfer errors
  during non-blocking operations.

**Behavior and Requirements:**

- **Simple transfers (<= 8 bytes):** Do not require a receiver on the other
  side, making them easy to send without needing address claiming or
  coordination with a destination.

- **Unicast TP/ETP:** Requires a receiver on the other side to complete the
  transfer. The receiver must acknowledge the transfer for the session to
  proceed successfully.

- **Broadcast TP:** Allows sending data without a receiver, but only works for
  TP transfers. ETP cannot be broadcast and always needs a receiving client.

These different behaviors depend heavily on the size of the buffer provided to
the stack, and the appropriate transport mechanism (TP or ETP) is selected
based on the payload size. The stack automatically manages the fragmentation
and reassembly of large payloads and ensures that the correct CAN frames are
generated and transmitted for each session.

PGN
---

The J1939 protocol uses the 29-bit CAN identifier with the following structure:

  ============  ==============  ====================
  29 bit CAN-ID
  --------------------------------------------------
  Bit positions within the CAN-ID
  --------------------------------------------------
  28 ... 26     25 ... 8        7 ... 0
  ============  ==============  ====================
  Priority      PGN             SA (Source Address)
  ============  ==============  ====================

The PGN (Parameter Group Number) is a number to identify a packet. The PGN
is composed as follows:

  ============  ==============  =================  =================
  PGN
  ------------------------------------------------------------------
  Bit positions within the CAN-ID
  ------------------------------------------------------------------
  25            24              23 ... 16          15 ... 8
  ============  ==============  =================  =================
  R (Reserved)  DP (Data Page)  PF (PDU Format)    PS (PDU Specific)
  ============  ==============  =================  =================

In J1939-21 distinction is made between PDU1 format (where PF < 240) and PDU2
format (where PF >= 240). Furthermore, when using the PDU2 format, the PS-field
contains a so-called Group Extension, which is part of the PGN. When using PDU2
format, the Group Extension is set in the PS-field.

  ==============  ========================
  PDU1 Format (specific) (peer to peer)
  ----------------------------------------
  Bit positions within the CAN-ID
  ----------------------------------------
  23 ... 16       15 ... 8
  ==============  ========================
  00h ... EFh     DA (Destination address)
  ==============  ========================

  ==============  ========================
  PDU2 Format (global) (broadcast)
  ----------------------------------------
  Bit positions within the CAN-ID
  ----------------------------------------
  23 ... 16       15 ... 8
  ==============  ========================
  F0h ... FFh     GE (Group Extension)
  ==============  ========================

On the other hand, when using PDU1 format, the PS-field contains a so-called
Destination Address, which is _not_ part of the PGN. When communicating a PGN
from user space to kernel (or vice versa) and PDU1 format is used, the PS-field
of the PGN shall be set to zero. The Destination Address shall be set
elsewhere.

Regarding PGN mapping to 29-bit CAN identifier, the Destination Address shall
be get/set from/to the appropriate bits of the identifier by the kernel.


Addressing
----------

Both static and dynamic addressing methods can be used.

For static addresses, no extra checks are made by the kernel and provided
addresses are considered right. This responsibility is for the OEM or system
integrator.

For dynamic addressing, so-called Address Claiming, extra support is foreseen
in the kernel. In J1939 any ECU is known by its 64-bit NAME. At the moment of
a successful address claim, the kernel keeps track of both NAME and source
address being claimed. This serves as a base for filter schemes. By default,
packets with a destination that is not locally will be rejected.

Mixed mode packets (from a static to a dynamic address or vice versa) are
allowed. The BSD sockets define separate API calls for getting/setting the
local & remote address and are applicable for J1939 sockets.

Filtering
---------

J1939 defines white list filters per socket that a user can set in order to
receive a subset of the J1939 traffic. Filtering can be based on:

* SA
* SOURCE_NAME
* PGN

When multiple filters are in place for a single socket, and a packet comes in
that matches several of those filters, the packet is only received once for
that socket.

How to Use J1939
================

API Calls
---------

On CAN, you first need to open a socket for communicating over a CAN network.
To use J1939, ``#include <linux/can/j1939.h>``. From there, ``<linux/can.h>`` will be
included too. To open a socket, use:

.. code-block:: C

    s = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);

J1939 does use ``SOCK_DGRAM`` sockets. In the J1939 specification, connections are
mentioned in the context of transport protocol sessions. These still deliver
packets to the other end (using several CAN packets). ``SOCK_STREAM`` is not
supported.

After the successful creation of the socket, you would normally use the ``bind(2)``
and/or ``connect(2)`` system call to bind the socket to a CAN interface. After
binding and/or connecting the socket, you can ``read(2)`` and ``write(2)`` from/to the
socket or use ``send(2)``, ``sendto(2)``, ``sendmsg(2)`` and the ``recv*()`` counterpart
operations on the socket as usual. There are also J1939 specific socket options
described below.

In order to send data, a ``bind(2)`` must have been successful. ``bind(2)`` assigns a
local address to a socket.

Different from CAN is that the payload data is just the data that get sends,
without its header info. The header info is derived from the sockaddr supplied
to ``bind(2)``, ``connect(2)``, ``sendto(2)`` and ``recvfrom(2)``. A ``write(2)`` with size 4 will
result in a packet with 4 bytes.

The sockaddr structure has extensions for use with J1939 as specified below:

.. code-block:: C

      struct sockaddr_can {
         sa_family_t can_family;
         int         can_ifindex;
         union {
            struct {
               __u64 name;
                        /* pgn:
                         * 8 bit: PS in PDU2 case, else 0
                         * 8 bit: PF
                         * 1 bit: DP
                         * 1 bit: reserved
                         */
               __u32 pgn;
               __u8  addr;
            } j1939;
         } can_addr;
      }

``can_family`` & ``can_ifindex`` serve the same purpose as for other SocketCAN sockets.

``can_addr.j1939.pgn`` specifies the PGN (max 0x3ffff). Individual bits are
specified above.

``can_addr.j1939.name`` contains the 64-bit J1939 NAME.

``can_addr.j1939.addr`` contains the address.

The ``bind(2)`` system call assigns the local address, i.e. the source address when
sending packages. If a PGN during ``bind(2)`` is set, it's used as a RX filter.
I.e. only packets with a matching PGN are received. If an ADDR or NAME is set
it is used as a receive filter, too. It will match the destination NAME or ADDR
of the incoming packet. The NAME filter will work only if appropriate Address
Claiming for this name was done on the CAN bus and registered/cached by the
kernel.

On the other hand ``connect(2)`` assigns the remote address, i.e. the destination
address. The PGN from ``connect(2)`` is used as the default PGN when sending
packets. If ADDR or NAME is set it will be used as the default destination ADDR
or NAME. Further a set ADDR or NAME during ``connect(2)`` is used as a receive
filter. It will match the source NAME or ADDR of the incoming packet.

Both ``write(2)`` and ``send(2)`` will send a packet with local address from ``bind(2)`` and the
remote address from ``connect(2)``. Use ``sendto(2)`` to overwrite the destination
address.

If ``can_addr.j1939.name`` is set (!= 0) the NAME is looked up by the kernel and
the corresponding ADDR is used. If ``can_addr.j1939.name`` is not set (== 0),
``can_addr.j1939.addr`` is used.

When creating a socket, reasonable defaults are set. Some options can be
modified with ``setsockopt(2)`` & ``getsockopt(2)``.

RX path related options:

- ``SO_J1939_FILTER`` - configure array of filters
- ``SO_J1939_PROMISC`` - disable filters set by ``bind(2)`` and ``connect(2)``

By default no broadcast packets can be send or received. To enable sending or
receiving broadcast packets use the socket option ``SO_BROADCAST``:

.. code-block:: C

     int value = 1;
     setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

The following diagram illustrates the RX path:

.. code::

                    +--------------------+
                    |  incoming packet   |
                    +--------------------+
                              |
                              V
                    +--------------------+
                    | SO_J1939_PROMISC?  |
                    +--------------------+
                             |  |
                         no  |  | yes
                             |  |
                   .---------'  `---------.
                   |                      |
     +---------------------------+        |
     | bind() + connect() +      |        |
     | SOCK_BROADCAST filter     |        |
     +---------------------------+        |
                   |                      |
                   |<---------------------'
                   V
     +---------------------------+
     |      SO_J1939_FILTER      |
     +---------------------------+
                   |
                   V
     +---------------------------+
     |        socket recv()      |
     +---------------------------+

TX path related options:
``SO_J1939_SEND_PRIO`` - change default send priority for the socket

Message Flags during send() and Related System Calls
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``send(2)``, ``sendto(2)`` and ``sendmsg(2)`` take a 'flags' argument. Currently
supported flags are:

* ``MSG_DONTWAIT``, i.e. non-blocking operation.

recvmsg(2)
^^^^^^^^^^

In most cases ``recvmsg(2)`` is needed if you want to extract more information than
``recvfrom(2)`` can provide. For example package priority and timestamp. The
Destination Address, name and packet priority (if applicable) are attached to
the msghdr in the ``recvmsg(2)`` call. They can be extracted using ``cmsg(3)`` macros,
with ``cmsg_level == SOL_J1939 && cmsg_type == SCM_J1939_DEST_ADDR``,
``SCM_J1939_DEST_NAME`` or ``SCM_J1939_PRIO``. The returned data is a ``uint8_t`` for
``priority`` and ``dst_addr``, and ``uint64_t`` for ``dst_name``.

.. code-block:: C

	uint8_t priority, dst_addr;
	uint64_t dst_name;

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		switch (cmsg->cmsg_level) {
		case SOL_CAN_J1939:
			if (cmsg->cmsg_type == SCM_J1939_DEST_ADDR)
				dst_addr = *CMSG_DATA(cmsg);
			else if (cmsg->cmsg_type == SCM_J1939_DEST_NAME)
				memcpy(&dst_name, CMSG_DATA(cmsg), cmsg->cmsg_len - CMSG_LEN(0));
			else if (cmsg->cmsg_type == SCM_J1939_PRIO)
				priority = *CMSG_DATA(cmsg);
			break;
		}
	}

setsockopt(2)
^^^^^^^^^^^^^

The ``setsockopt(2)`` function is used to configure various socket-level
options for J1939 communication. The following options are supported:

``SO_J1939_FILTER``
~~~~~~~~~~~~~~~~~~~

The ``SO_J1939_FILTER`` option is essential when the default behavior of
``bind(2)`` and ``connect(2)`` is insufficient for specific use cases. By
default, ``bind(2)`` and ``connect(2)`` allow a socket to be associated with a
single unicast or broadcast address. However, there are scenarios where finer
control over the incoming messages is required, such as filtering by Parameter
Group Number (PGN) rather than by addresses.

For example, in a system where multiple types of J1939 messages are being
transmitted, a process might only be interested in a subset of those messages,
such as specific PGNs, and not want to receive all messages destined for its
address or broadcast to the bus.

By applying the ``SO_J1939_FILTER`` option, you can filter messages based on:

- **Source Address (SA)**: Filter messages coming from specific source
  addresses.

- **Source Name**: Filter messages coming from ECUs with specific NAME
  identifiers.

- **Parameter Group Number (PGN)**: Focus on receiving messages with specific
  PGNs, filtering out irrelevant ones.

This filtering mechanism is particularly useful when:

- You want to receive a subset of messages based on their PGNs, even if the
  address is the same.

- You need to handle both broadcast and unicast messages but only care about
  certain message types or parameters.

- The ``bind(2)`` and ``connect(2)`` functions only allow binding to a single
  address, which might not be sufficient if the process needs to handle multiple
  PGNs but does not want to open multiple sockets.

To remove existing filters, you can pass ``optval == NULL`` or ``optlen == 0``
to ``setsockopt(2)``. This will clear all currently set filters. If you want to
**update** the set of filters, you must pass the updated filter set to
``setsockopt(2)``, as the new filter set will **replace** the old one entirely.
This behavior ensures that any previous filter configuration is discarded and
only the new set is applied.

Example of removing all filters:

.. code-block:: c

    setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, NULL, 0);

**Maximum number of filters:** The maximum amount of filters that can be
applied using ``SO_J1939_FILTER`` is defined by ``J1939_FILTER_MAX``, which is
set to 512. This means you can configure up to 512 individual filters to match
your specific filtering needs.

Practical use case: **Monitoring Address Claiming**

One practical use case is monitoring the J1939 address claiming process by
filtering for specific PGNs related to address claiming. This allows a process
to monitor and handle address claims without processing unrelated messages.

Example:

.. code-block:: c

    struct j1939_filter filt[] = {
        {
            .pgn = J1939_PGN_ADDRESS_CLAIMED,
            .pgn_mask = J1939_PGN_PDU1_MAX,
        }, {
            .pgn = J1939_PGN_REQUEST,
            .pgn_mask = J1939_PGN_PDU1_MAX,
        }, {
            .pgn = J1939_PGN_ADDRESS_COMMANDED,
            .pgn_mask = J1939_PGN_MAX,
        },
    };
    setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &filt, sizeof(filt));

In this example, the socket will only receive messages with the PGNs related to
address claiming: ``J1939_PGN_ADDRESS_CLAIMED``, ``J1939_PGN_REQUEST``, and
``J1939_PGN_ADDRESS_COMMANDED``. This is particularly useful in scenarios where
you want to monitor and process address claims without being overwhelmed by
other traffic on the J1939 network.

``SO_J1939_PROMISC``
~~~~~~~~~~~~~~~~~~~~

The ``SO_J1939_PROMISC`` option enables socket-level promiscuous mode. When
this option is enabled, the socket will receive all J1939 traffic, regardless
of any filters set by ``bind()`` or ``connect()``. This is analogous to
enabling promiscuous mode for an Ethernet interface, where all traffic on the
network segment is captured.

However, **`SO_J1939_FILTER` has a higher priority** compared to
``SO_J1939_PROMISC``. This means that even in promiscuous mode, you can reduce
the number of packets received by applying specific filters with
`SO_J1939_FILTER`. The filters will limit which packets are passed to the
socket, allowing for more refined traffic selection while promiscuous mode is
active.

The acceptable value size for this option is ``sizeof(int)``, and the value is
only differentiated between `0` and non-zero. A value of `0` disables
promiscuous mode, while any non-zero value enables it.

This combination can be useful for debugging or monitoring specific types of
traffic while still capturing a broad set of messages.

Example:

.. code-block:: c

    int value = 1;
    setsockopt(sock, SOL_CAN_J1939, SO_J1939_PROMISC, &value, sizeof(value));

In this example, setting ``value`` to any non-zero value (e.g., `1`) enables
promiscuous mode, allowing the socket to receive all J1939 traffic on the
network.

``SO_BROADCAST``
~~~~~~~~~~~~~~~~

The ``SO_BROADCAST`` option enables the sending and receiving of broadcast
messages. By default, broadcast messages are disabled for J1939 sockets. When
this option is enabled, the socket will be allowed to send and receive
broadcast packets on the J1939 network.

Due to the nature of the CAN bus as a shared medium, all messages transmitted
on the bus are visible to all participants. In the context of J1939,
broadcasting refers to using a specific destination address field, where the
destination address is set to a value that indicates the message is intended
for all participants (usually a global address such as 0xFF). Enabling the
broadcast option allows the socket to send and receive such broadcast messages.

The acceptable value size for this option is ``sizeof(int)``, and the value is
only differentiated between `0` and non-zero. A value of `0` disables the
ability to send and receive broadcast messages, while any non-zero value
enables it.

Example:

.. code-block:: c

    int value = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

In this example, setting ``value`` to any non-zero value (e.g., `1`) enables
the socket to send and receive broadcast messages.

``SO_J1939_SEND_PRIO``
~~~~~~~~~~~~~~~~~~~~~~

The ``SO_J1939_SEND_PRIO`` option sets the priority of outgoing J1939 messages
for the socket. In J1939, messages can have different priorities, and lower
numerical values indicate higher priority. This option allows the user to
control the priority of messages sent from the socket by adjusting the priority
bits in the CAN identifier.

The acceptable value **size** for this option is ``sizeof(int)``, and the value
is expected to be in the range of 0 to 7, where `0` is the highest priority,
and `7` is the lowest. By default, the priority is set to `6` if this option is
not explicitly configured.

Note that the priority values `0` and `1` can only be set if the process has
the `CAP_NET_ADMIN` capability. These are reserved for high-priority traffic
and require administrative privileges.

Example:

.. code-block:: c

    int prio = 3;  // Priority value between 0 (highest) and 7 (lowest)
    setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO, &prio, sizeof(prio));

In this example, the priority is set to `3`, meaning the outgoing messages will
be sent with a moderate priority level.

``SO_J1939_ERRQUEUE``
~~~~~~~~~~~~~~~~~~~~~

The ``SO_J1939_ERRQUEUE`` option enables the socket to receive error messages
from the error queue, providing diagnostic information about transmission
failures, protocol violations, or other issues that occur during J1939
communication. Once this option is set, user space is required to handle
``MSG_ERRQUEUE`` messages.

Setting ``SO_J1939_ERRQUEUE`` to ``0`` will purge any currently present error
messages in the error queue. When enabled, error messages can be retrieved
using the ``recvmsg(2)`` system call.

When subscribing to the error queue, the following error events can be
accessed:

- **``J1939_EE_INFO_TX_ABORT``**: Transmission abort errors.
- **``J1939_EE_INFO_RX_RTS``**: Reception of RTS (Request to Send) control
  frames.
- **``J1939_EE_INFO_RX_DPO``**: Reception of data packets with Data Page Offset
  (DPO).
- **``J1939_EE_INFO_RX_ABORT``**: Reception abort errors.

The error queue can be used to correlate errors with specific message transfer
sessions using the session ID (``tskey``). The session ID is assigned via the
``SOF_TIMESTAMPING_OPT_ID`` flag, which is set by enabling the
``SO_TIMESTAMPING`` option.

If ``SO_J1939_ERRQUEUE`` is activated, the user is required to pull messages
from the error queue, meaning that using plain ``recv(2)`` is not sufficient
anymore. The user must use ``recvmsg(2)`` with appropriate flags to handle
error messages. Failure to do so can result in the socket becoming blocked with
unprocessed error messages in the queue.

It is **recommended** that ``SO_J1939_ERRQUEUE`` be used in combination with
``SO_TIMESTAMPING`` in most cases. This enables proper error handling along
with session tracking and timestamping, providing a more detailed analysis of
message transfers and errors.

The acceptable value **size** for this option is ``sizeof(int)``, and the value
is only differentiated between ``0`` and non-zero. A value of ``0`` disables
error queue reception and purges any existing error messages, while any
non-zero value enables it.

Example:

.. code-block:: c

    int enable = 1;  // Enable error queue reception
    setsockopt(sock, SOL_CAN_J1939, SO_J1939_ERRQUEUE, &enable, sizeof(enable));

    // Enable timestamping with session tracking via tskey
    int timestamping = SOF_TIMESTAMPING_OPT_ID | SOF_TIMESTAMPING_TX_ACK |
                       SOF_TIMESTAMPING_TX_SCHED |
                       SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_OPT_CMSG;
    setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &timestamping,
               sizeof(timestamping));

When enabled, error messages can be retrieved using ``recvmsg(2)``. By
combining ``SO_J1939_ERRQUEUE`` with ``SO_TIMESTAMPING`` (with
``SOF_TIMESTAMPING_OPT_ID`` and ``SOF_TIMESTAMPING_OPT_CMSG`` enabled), the
user can track message transfers, retrieve precise timestamps, and correlate
errors with specific sessions.

For more information on enabling timestamps and session tracking, refer to the
`SO_TIMESTAMPING` section.

``SO_TIMESTAMPING``
~~~~~~~~~~~~~~~~~~~

The ``SO_TIMESTAMPING`` option allows the socket to receive timestamps for
various events related to message transmissions and receptions in J1939. This
option is often used in combination with ``SO_J1939_ERRQUEUE`` to provide
detailed diagnostic information, session tracking, and precise timing data for
message transfers.

In J1939, all payloads provided by user space, regardless of size, are
processed by the kernel as **sessions**. This includes both single-frame
messages (up to 8 bytes) and multi-frame protocols such as the Transport
Protocol (TP) and Extended Transport Protocol (ETP). Even for small,
single-frame messages, the kernel creates a session to manage the transmission
and reception. The concept of sessions allows the kernel to manage various
aspects of the protocol, such as reassembling multi-frame messages and tracking
the status of transmissions.

When receiving extended error messages from the error queue, the error
information is delivered through a `struct sock_extended_err`, accessible via
the control message (``cmsg``) retrieved using the ``recvmsg(2)`` system call.

There are two typical origins for the extended error messages in J1939:

1. ``serr->ee_origin == SO_EE_ORIGIN_TIMESTAMPING``:

   In this case, the `serr->ee_info` field will contain one of the following
   timestamp types:

   - ``SCM_TSTAMP_SCHED``: This timestamp is valid for Extended Transport
     Protocol (ETP) transfers and simple transfers (8 bytes or less). It
     indicates when a message or set of frames has been scheduled for
     transmission.

     - For simple transfers (8 bytes or less), it marks the point when the
       message is queued and ready to be sent onto the CAN bus.

     - For ETP transfers, it is sent after receiving a CTS (Clear to Send)
       frame on the sender side, indicating that a new set of frames has been
       scheduled for transmission.

     - The Transport Protocol (TP) case is currently not implemented for this
       timestamp.

     - On the receiver side, the counterpart to this event for ETP is
       represented by the ``J1939_EE_INFO_RX_DPO`` message, which indicates the
       reception of a Data Page Offset (DPO) control frame.

   - ``SCM_TSTAMP_ACK``: This timestamp indicates the acknowledgment of the
     message or session.

     - For simple transfers (8 bytes or less), it marks when the message has
       been sent and an echo confirmation has been received from the CAN
       controller, indicating that the frame was transmitted onto the bus.

     - For multi-frame transfers (TP or ETP), it signifies that the entire
       session has been acknowledged, typically after receiving the End of
       Message Acknowledgment (EOMA) packet.

2. ``serr->ee_origin == SO_EE_ORIGIN_LOCAL``:

   In this case, the `serr->ee_info` field will contain one of the following
   J1939 stack-specific message types:

   - ``J1939_EE_INFO_TX_ABORT``: This message indicates that the transmission
     of a message or session was aborted. The cause of the abort can come from
     various sources:

     - **CAN stack failure**: The J1939 stack was unable to pass the frame to
       the CAN framework for transmission.

     - **Echo failure**: The J1939 stack did not receive an echo confirmation
       from the CAN controller, meaning the frame may not have been successfully
       transmitted to the CAN bus.

     - **Protocol-level issues**: For multi-frame transfers (TP/ETP), this
       could include protocol-related errors, such as an abort signaled by the
       receiver or a timeout at the protocol level, which causes the session to
       terminate prematurely.

     - The corresponding error code is stored in ``serr->ee_data``
       (``session->err`` on kernel side), providing additional details about
       the specific reason for the abort.

   - ``J1939_EE_INFO_RX_RTS``: This message indicates that the J1939 stack has
     received a Request to Send (RTS) control frame, signaling the start of a
     multi-frame transfer using the Transport Protocol (TP) or Extended
     Transport Protocol (ETP).

     - It informs the receiver that the sender is ready to transmit a
       multi-frame message and includes details about the total message size
       and the number of frames to be sent.

     - Statistics such as ``J1939_NLA_TOTAL_SIZE``, ``J1939_NLA_PGN``,
       ``J1939_NLA_SRC_NAME``, and ``J1939_NLA_DEST_NAME`` are provided along
       with the ``J1939_EE_INFO_RX_RTS`` message, giving detailed information
       about the incoming transfer.

   - ``J1939_EE_INFO_RX_DPO``: This message indicates that the J1939 stack has
     received a Data Page Offset (DPO) control frame, which is part of the
     Extended Transport Protocol (ETP).

     - The DPO frame signals the continuation of an ETP multi-frame message by
       indicating the offset position in the data being transferred. It helps
       the receiver manage large data sets by identifying which portion of the
       message is being received.

     - It is typically paired with a corresponding ``SCM_TSTAMP_SCHED`` event
       on the sender side, which indicates when the next set of frames is
       scheduled for transmission.

     - This event includes statistics such as ``J1939_NLA_BYTES_ACKED``, which
       tracks the number of bytes acknowledged up to that point in the session.

   - ``J1939_EE_INFO_RX_ABORT``: This message indicates that the reception of a
     multi-frame message (Transport Protocol or Extended Transport Protocol) has
     been aborted.

     - The abort can be triggered by protocol-level errors such as timeouts, an
       unexpected frame, or a specific abort request from the sender.

     - This message signals that the receiver cannot continue processing the
       transfer, and the session is terminated.

     - The corresponding error code is stored in ``serr->ee_data``
       (``session->err`` on kernel side ), providing further details about the
       reason for the abort, such as protocol violations or timeouts.

     - After receiving this message, the receiver discards the partially received
       frames, and the multi-frame session is considered incomplete.

In both cases, if ``SOF_TIMESTAMPING_OPT_ID`` is enabled, ``serr->ee_data``
will be set to the session’s unique identifier (``session->tskey``). This
allows user space to track message transfers by their session identifier across
multiple frames or stages.

In all other cases, ``serr->ee_errno`` will be set to ``ENOMSG``, except for
the ``J1939_EE_INFO_TX_ABORT`` and ``J1939_EE_INFO_RX_ABORT`` cases, where the
kernel sets ``serr->ee_data`` to the error stored in ``session->err``.  All
protocol-specific errors are converted to standard kernel error values and
stored in ``session->err``. These error values are unified across system calls
and ``serr->ee_errno``.  Some of the known error values are described in the
`Error Codes in the J1939 Stack` section.

When the `J1939_EE_INFO_RX_RTS` message is provided, it will include the
following statistics for multi-frame messages (TP and ETP):

  - ``J1939_NLA_TOTAL_SIZE``: Total size of the message in the session.
  - ``J1939_NLA_PGN``: Parameter Group Number (PGN) identifying the message type.
  - ``J1939_NLA_SRC_NAME``: 64-bit name of the source ECU.
  - ``J1939_NLA_DEST_NAME``: 64-bit name of the destination ECU.
  - ``J1939_NLA_SRC_ADDR``: 8-bit source address of the sending ECU.
  - ``J1939_NLA_DEST_ADDR``: 8-bit destination address of the receiving ECU.

- For other messages (including single-frame messages), only the following
  statistic is included:

  - ``J1939_NLA_BYTES_ACKED``: Number of bytes successfully acknowledged in the
    session.

The key flags for ``SO_TIMESTAMPING`` include:

- ``SOF_TIMESTAMPING_OPT_ID``: Enables the use of a unique session identifier
  (``tskey``) for each transfer. This identifier helps track message transfers
  and errors as distinct sessions in user space. When this option is enabled,
  ``serr->ee_data`` will be set to ``session->tskey``.

- ``SOF_TIMESTAMPING_OPT_CMSG``: Sends timestamp information through control
  messages (``struct scm_timestamping``), allowing the application to retrieve
  timestamps alongside the data.

- ``SOF_TIMESTAMPING_TX_SCHED``: Provides the timestamp for when a message is
  scheduled for transmission (``SCM_TSTAMP_SCHED``).

- ``SOF_TIMESTAMPING_TX_ACK``: Provides the timestamp for when a message
  transmission is fully acknowledged (``SCM_TSTAMP_ACK``).

- ``SOF_TIMESTAMPING_RX_SOFTWARE``: Provides timestamps for reception-related
  events (e.g., ``J1939_EE_INFO_RX_RTS``, ``J1939_EE_INFO_RX_DPO``,
  ``J1939_EE_INFO_RX_ABORT``).

These flags enable detailed monitoring of message lifecycles, including
transmission scheduling, acknowledgments, reception timestamps, and gathering
detailed statistics about the communication session, especially for multi-frame
payloads like TP and ETP.

Example:

.. code-block:: c

    // Enable timestamping with various options, including session tracking and
    // statistics
    int sock_opt = SOF_TIMESTAMPING_OPT_CMSG |
                   SOF_TIMESTAMPING_TX_ACK |
                   SOF_TIMESTAMPING_TX_SCHED |
                   SOF_TIMESTAMPING_OPT_ID |
                   SOF_TIMESTAMPING_RX_SOFTWARE;

    setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &sock_opt, sizeof(sock_opt));



Dynamic Addressing
------------------

Distinction has to be made between using the claimed address and doing an
address claim. To use an already claimed address, one has to fill in the
``j1939.name`` member and provide it to ``bind(2)``. If the name had claimed an address
earlier, all further messages being sent will use that address. And the
``j1939.addr`` member will be ignored.

An exception on this is PGN 0x0ee00. This is the "Address Claim/Cannot Claim
Address" message and the kernel will use the ``j1939.addr`` member for that PGN if
necessary.

To claim an address following code example can be used:

.. code-block:: C

	struct sockaddr_can baddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.name = name,
			.addr = J1939_IDLE_ADDR,
			.pgn = J1939_NO_PGN,	/* to disable bind() rx filter for PGN */
		},
		.can_ifindex = if_nametoindex("can0"),
	};

	bind(sock, (struct sockaddr *)&baddr, sizeof(baddr));

	/* for Address Claiming broadcast must be allowed */
	int value = 1;
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value));

	/* configured advanced RX filter with PGN needed for Address Claiming */
	const struct j1939_filter filt[] = {
		{
			.pgn = J1939_PGN_ADDRESS_CLAIMED,
			.pgn_mask = J1939_PGN_PDU1_MAX,
		}, {
			.pgn = J1939_PGN_REQUEST,
			.pgn_mask = J1939_PGN_PDU1_MAX,
		}, {
			.pgn = J1939_PGN_ADDRESS_COMMANDED,
			.pgn_mask = J1939_PGN_MAX,
		},
	};

	setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &filt, sizeof(filt));

	uint64_t dat = htole64(name);
	const struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.pgn = J1939_PGN_ADDRESS_CLAIMED,
			.addr = J1939_NO_ADDR,
		},
	};

	/* Afterwards do a sendto(2) with data set to the NAME (Little Endian). If the
	 * NAME provided, does not match the j1939.name provided to bind(2), EPROTO
	 * will be returned.
	 */
	sendto(sock, dat, sizeof(dat), 0, (const struct sockaddr *)&saddr, sizeof(saddr));

If no-one else contests the address claim within 250ms after transmission, the
kernel marks the NAME-SA assignment as valid. The valid assignment will be kept
among other valid NAME-SA assignments. From that point, any socket bound to the
NAME can send packets.

If another ECU claims the address, the kernel will mark the NAME-SA expired.
No socket bound to the NAME can send packets (other than address claims). To
claim another address, some socket bound to NAME, must ``bind(2)`` again, but with
only ``j1939.addr`` changed to the new SA, and must then send a valid address claim
packet. This restarts the state machine in the kernel (and any other
participant on the bus) for this NAME.

``can-utils`` also include the ``j1939acd`` tool, so it can be used as code example or as
default Address Claiming daemon.

Send Examples
-------------

Static Addressing
^^^^^^^^^^^^^^^^^

This example will send a PGN (0x12300) from SA 0x20 to DA 0x30.

Bind:

.. code-block:: C

	struct sockaddr_can baddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.name = J1939_NO_NAME,
			.addr = 0x20,
			.pgn = J1939_NO_PGN,
		},
		.can_ifindex = if_nametoindex("can0"),
	};

	bind(sock, (struct sockaddr *)&baddr, sizeof(baddr));

Now, the socket 'sock' is bound to the SA 0x20. Since no ``connect(2)`` was called,
at this point we can use only ``sendto(2)`` or ``sendmsg(2)``.

Send:

.. code-block:: C

	const struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.name = J1939_NO_NAME;
			.addr = 0x30,
			.pgn = 0x12300,
		},
	};

	sendto(sock, dat, sizeof(dat), 0, (const struct sockaddr *)&saddr, sizeof(saddr));


Error Codes in the J1939 Stack
------------------------------

This section lists all potential kernel error codes that can be exposed to user
space when interacting with the J1939 stack. It includes both standard error
codes and those derived from protocol-specific abort codes.

- ``EAGAIN``: Operation would block; retry may succeed. One common reason is
  that an active TP or ETP session exists, and an attempt was made to start a
  new overlapping TP or ETP session between the same peers.

- ``ENETDOWN``: Network is down. This occurs when the CAN interface is switched
  to the "down" state.

- ``ENOBUFS``: No buffer space available. This error occurs when the CAN
  interface's transmit (TX) queue is full, and no more messages can be queued.

- ``EOVERFLOW``: Value too large for defined data type. In J1939, this can
  happen if the requested data lies outside of the queued buffer. For example,
  if a CTS (Clear to Send) requests an offset not available in the kernel buffer
  because user space did not provide enough data.

- ``EBUSY``: Device or resource is busy. For example, this occurs if an
  identical session is already active and the stack is unable to recover from
  the condition.

- ``EACCES``: Permission denied. This error can occur, for example, when
  attempting to send broadcast messages, but the socket is not configured with
  ``SO_BROADCAST``.

- ``EADDRNOTAVAIL``: Address not available. This error occurs in cases such as:

  - When attempting to use ``getsockname(2)`` to retrieve the peer's address,
    but the socket is not connected.

  - When trying to send data to or from a NAME, but address claiming for the
    NAME was not performed or detected by the stack.

- ``EBADFD``: File descriptor in bad state. This error can occur if:

  - Attempting to send data to an unbound socket.

  - The socket is bound but has no source name, and the source address is
    ``J1939_NO_ADDR``.

  - The ``can_ifindex`` is incorrect.

- ``EFAULT``: Bad address. Occurs mostly when the stack can't copy from or to a
  sockptr, when there is insufficient data from user space, or when the buffer
  provided by user space is not large enough for the requested data.

- ``EINTR``: A signal occurred before any data was transmitted; see ``signal(7)``.

- ``EINVAL``: Invalid argument passed. For example:

  - ``msg->msg_namelen`` is less than ``J1939_MIN_NAMELEN``.

  - ``addr->can_family`` is not equal to ``AF_CAN``.

  - An incorrect PGN was provided.

- ``ENODEV``: No such device. This happens when the CAN network device cannot
  be found for the provided ``can_ifindex`` or if ``can_ifindex`` is 0.

- ``ENOMEM``: Out of memory. Typically related to issues with memory allocation
  in the stack.

- ``ENOPROTOOPT``: Protocol not available. This can occur when using
  ``getsockopt(2)`` or ``setsockopt(2)`` if the requested socket option is not
  available.

- ``EDESTADDRREQ``: Destination address required. This error occurs:

  - In the case of ``connect(2)``, if the ``struct sockaddr *uaddr`` is ``NULL``.

  - In the case of ``send*(2)``, if there is an attempt to send an ETP message
    to a broadcast address.

- ``EDOM``: Argument out of domain. This error may happen if attempting to send
  a TP or ETP message to a PGN that is reserved for control PGNs for TP or ETP
  operations.

- ``EIO``: I/O error. This can occur if the amount of data provided to the
  socket for a TP or ETP session does not match the announced amount of data for
  the session.

- ``ENOENT``: No such file or directory. This can happen when the stack
  attempts to transfer CTS or EOMA but cannot find a matching receiving socket
  anymore.

- ``ENOIOCTLCMD``: No ioctls are available for the socket layer.

- ``EPERM``: Operation not permitted. For example, this can occur if a
  requested action requires ``CAP_NET_ADMIN`` privileges.

- ``ENETUNREACH``: Network unreachable. Most likely, this occurs when frames
  cannot be transmitted to the CAN bus.

- ``ETIME``: Timer expired. This can happen if a timeout occurs while
  attempting to send a simple message, for example, when an echo message from
  the controller is not received.

- ``EPROTO``: Protocol error.

  - Used for various protocol-level errors in J1939, including:

    - Duplicate sequence number.

    - Unexpected EDPO or ECTS packet.

    - Invalid PGN or offset in EDPO/ECTS.

    - Number of EDPO packets exceeded CTS allowance.

    - Any other protocol-level error.

- ``EMSGSIZE``: Message too long.

- ``ENOMSG``: No message available.

- ``EALREADY``: The ECU is already engaged in one or more connection-managed
  sessions and cannot support another.

- ``EHOSTUNREACH``: A timeout occurred, and the session was aborted.

- ``EBADMSG``: CTS (Clear to Send) messages were received during an active data
  transfer, causing an abort.

- ``ENOTRECOVERABLE``: The maximum retransmission request limit was reached,
  and the session cannot recover.

- ``ENOTCONN``: An unexpected data transfer packet was received.

- ``EILSEQ``: A bad sequence number was received, and the software could not
  recover.

