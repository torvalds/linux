.. SPDX-License-Identifier: GPL-2.0

.. _ethernet-flow-control:

=====================
Ethernet Flow Control
=====================

This document is a practical guide to Ethernet Flow Control in Linux, covering
what it is, how it works, and how to configure it.

What is Flow Control?
=====================

Flow control is a mechanism to prevent a fast sender from overwhelming a
slow receiver with data, which would cause buffer overruns and dropped packets.
The receiver can signal the sender to temporarily stop transmitting, giving it
time to process its backlog.

Standards references
====================

Ethernet flow control mechanisms are specified across consolidated IEEE base
standards; some originated as amendments:

- Collision-based flow control is part of CSMA/CD in **IEEE 802.3**
  (half-duplex).
- Link-wide PAUSE is defined in **IEEE 802.3 Annex 31B**
  (originally **802.3x**).
- Priority-based Flow Control (PFC) is defined in **IEEE 802.1Q Clause 36**
  (originally **802.1Qbb**).

In the remainder of this document, the consolidated clause numbers are used.

How It Works: The Mechanisms
============================

The method used for flow control depends on the link's duplex mode.

.. note::
   The user-visible ``ethtool`` pause API described in this document controls
   **link-wide PAUSE** (IEEE 802.3 Annex 31B) only. It does not control the
   collision-based behavior that exists on half-duplex links.

1. Half-Duplex: Collision-Based Flow Control
--------------------------------------------
On half-duplex links, a device cannot send and receive simultaneously, so PAUSE
frames are not used. Flow control is achieved by leveraging the CSMA/CD
(Carrier Sense Multiple Access with Collision Detection) protocol itself.

* **How it works**: To inhibit incoming data, a receiving device can force a
  collision on the line. When the sending station detects this collision, it
  terminates its transmission, sends a "jam" signal, and then executes the
  "Collision backoff and retransmission" procedure as defined in IEEE 802.3,
  Section 4.2.3.2.5. This algorithm makes the sender wait for a random
  period before attempting to retransmit. By repeatedly forcing collisions,
  the receiver can effectively throttle the sender's transmission rate.

.. note::
    While this mechanism is part of the IEEE standard, there is currently no
    generic kernel API to configure or control it. Drivers should not enable
    this feature until a standardized interface is available.

.. warning::
   On shared-medium networks (e.g. 10BASE2, or twisted-pair networks using a
   hub rather than a switch) forcing collisions inhibits traffic **across the
   entire shared segment**, not just a single point-to-point link. Enabling
   such behavior is generally undesirable.

2. Full-Duplex: Link-wide PAUSE (IEEE 802.3 Annex 31B)
------------------------------------------------------
On full-duplex links, devices can send and receive at the same time. Flow
control is achieved by sending a special **PAUSE frame**, defined by IEEE
802.3 Annex 31B. This mechanism pauses all traffic on the link and is therefore
called *link-wide PAUSE*.

* **What it is**: A standard Ethernet frame with a globally reserved
  destination MAC address (``01-80-C2-00-00-01``). This address is in a range
  that standard IEEE 802.1D-compliant bridges do not forward. However, some
  unmanaged or misconfigured bridges have been reported to forward these
  frames, which can disrupt flow control across a network.

* **How it works**: The frame contains a MAC Control opcode for PAUSE
  (``0x0001``) and a ``pause_time`` value, telling the sender how long to
  wait before sending more data frames. This time is specified in units of
  "pause quantum", where one quantum is the time it takes to transmit 512 bits.
  For example, one pause quantum is 51.2 microseconds on a 10 Mbit/s link,
  and 512 nanoseconds on a 1 Gbit/s link. A ``pause_time`` of zero indicates
  that the transmitter can resume transmission, even if a previous non-zero
  pause time has not yet elapsed.

* **Who uses it**: Any full-duplex link, from 10 Mbit/s to multi-gigabit speeds.

3. Full-Duplex: Priority-based Flow Control (PFC) (IEEE 802.1Q Clause 36)
-------------------------------------------------------------------------
Priority-based Flow Control is an enhancement to the standard PAUSE mechanism
that allows flow control to be applied independently to different classes of
traffic, identified by their priority level.

* **What it is**: PFC allows a receiver to pause traffic for one or more of the
  8 standard priority levels without stopping traffic for other priorities.
  This is critical in data center environments for protocols that cannot
  tolerate packet loss due to congestion (e.g., Fibre Channel over Ethernet
  or RoCE).

* **How it works**: PFC uses a specific PAUSE frame format. It shares the same
  globally reserved destination MAC address (``01-80-C2-00-00-01``) as legacy
  PAUSE frames but uses a unique opcode (``0x0101``). The frame payload
  contains two key fields:

  - **``priority_enable_vector``**: An 8-bit mask where each bit corresponds to
    one of the 8 priorities. If a bit is set to 1, it means the pause time
    for that priority is active.
  - **``time_vector``**: A list of eight 2-octet fields, one for each priority.
    Each field specifies the ``pause_time`` for its corresponding priority,
    measured in units of ``pause_quanta`` (the time to transmit 512 bits).

.. note::
    When PFC is enabled for at least one priority on a port, the standard
    **link-wide PAUSE** (IEEE 802.3 Annex 31B) must be disabled for that port.
    The two mechanisms are mutually exclusive (IEEE 802.1Q Clause 36).

Configuring Flow Control
========================

Link-wide PAUSE and Priority-based Flow Control are configured with different
tools.

Configuring Link-wide PAUSE with ``ethtool`` (IEEE 802.3 Annex 31B)
-------------------------------------------------------------------
Use ``ethtool -a <interface>`` to view and ``ethtool -A <interface>`` to change
the link-wide PAUSE settings.

.. code-block:: bash

  # View current link-wide PAUSE settings
  ethtool -a eth0

  # Enable RX and TX pause, with autonegotiation
  ethtool -A eth0 autoneg on rx on tx on

**Key Configuration Concepts**:

* **Pause Autoneg vs Generic Autoneg**: ``ethtool -A ... autoneg {on,off}``
  controls **Pause Autoneg** (Annex 31B) only. It is independent from the
  **Generic link autonegotiation** configured with ``ethtool -s``. A device can
  have Generic autoneg **on** while Pause Autoneg is **off**, and vice versa.

* **If Pause Autoneg is off** (``-A ... autoneg off``): the device will **not**
  advertise pause in the PHY. The MAC PAUSE state is **forced** according to
  ``rx``/``tx`` and does not depend on partner capabilities or resolution.
  Ensure the peer is configured complementarily for PAUSE to be effective.

* **If generic autoneg is off** but **Pause Autoneg is on**, the pause policy
  is **remembered** by the kernel and applied later when Generic autoneg is
  enabled again.

* **Autonegotiation Mode**: The PHY will *advertise* the ``rx`` and ``tx``
  capabilities. The final active state is determined by what both sides of the
  link agree on. See the "PHY (Physical Layer Transceiver)" section below,
  especially the *Resolution* subsection, for details of the negotiation rules.

* **Forced Mode**: This mode is necessary when autonegotiation is not used or
  not possible. This includes links where one or both partners have
  autonegotiation disabled, or in setups without a PHY (e.g., direct
  MAC-to-MAC connections). The driver bypasses PHY advertisement and
  directly forces the MAC into the specified ``rx``/``tx`` state. The
  configuration on both sides of the link must be complementary. For
  example, if one side is set to ``tx on`` ``rx off``, the link partner must be
  set to ``tx off`` ``rx on`` for flow control to function correctly.

Configuring PFC with ``dcb`` (IEEE 802.1Q Clause 36)
----------------------------------------------------
PFC is part of the Data Center Bridging (DCB) subsystem and is managed with the
``dcb`` tool (iproute2). Some deployments use ``dcbtool`` (lldpad) instead; this
document shows ``dcb(8)`` examples.

**Viewing PFC Settings**:

.. code-block:: text

  $ dcb pfc show dev eth0
  pfc-cap 8 macsec-bypass off delay 4096
  prio-pfc 0:off 1:off 2:off 3:off 4:off 5:off 6:on 7:on

This shows the PFC state (on/off) for each priority (0-7).

**Changing PFC Settings**:

.. code-block:: bash

  # Enable PFC on priorities 6 and 7, leaving others as they are
  $ dcb pfc set dev eth0 prio-pfc 6:on 7:on

  # Disable PFC for all priorities except 6 and 7
  $ dcb pfc set dev eth0 prio-pfc all:off 6:on 7:on

Monitoring Flow Control
=======================

The standard way to check if flow control is actively being used is to view the
pause-related statistics.

**Monitoring Link-wide PAUSE**:
Use ``ethtool --include-statistics -a <interface>``.

.. code-block:: text

  $ ethtool --include-statistics -a eth0
  Pause parameters for eth0:
  ...
  Statistics:
    tx_pause_frames: 0
    rx_pause_frames: 0

**Monitoring PFC**:
PFC statistics (sent and received frames per priority) are available
through the ``dcb`` tool.

.. code-block:: text

  $ dcb pfc show dev eth0 requests indications
  requests 0:0 1:0 2:0 3:1024 4:2048 5:0 6:0 7:0
  indications 0:0 1:0 2:0 3:512 4:4096 5:0 6:0 7:0

The ``requests`` counters track transmitted PFC frames (TX), and the
``indications`` counters track received PFC frames (RX).

Link-wide PAUSE Autonegotiation Details
=======================================

The autonegotiation process for link-wide PAUSE is managed by the PHY and
involves advertising capabilities and resolving the outcome.

* Terminology (link-wide PAUSE):

  - **Symmetric pause**: both directions are paused when requested (TX+RX
    enabled).
  - **Asymmetric pause**: only one direction is paused (e.g., RX-only or
    TX-only).

  In IEEE 802.3 advertisement/resolution, symmetric/asymmetric are encoded
  using two bits (Pause/Asym) and resolved per the standard truth tables
  below.

* **Advertisement**: The PHY advertises the MAC's flow control capabilities.
  This is done using two bits in the advertisement register: "Symmetric
  Pause" (Pause) and "Asymmetric Pause" (Asym). These bits should be
  interpreted as a combined value, not as independent flags. The kernel
  converts the user's ``rx`` and ``tx`` settings into this two-bit value as
  follows:

  .. code-block:: text

    tx  rx | Pause  Asym
    -------+-------------
     0   0 |   0      0
     0   1 |   1      1
     1   0 |   0      1
     1   1 |   1      0

* **Resolution**: After negotiation, the PHY reports the link partner's
  advertised Pause and Asym bits. The final flow control mode is determined
  by the combination of the local and partner advertisements, according to
  the IEEE 802.3 standard:

  .. code-block:: text

    Local Device       | Link Partner       | Result
    Pause  Asym        | Pause   Asym       |
    -------------------+--------------------+---------
      0      X         |  0       X         | Disabled
      0      1         |  1       0         | Disabled
      0      1         |  1       1         | TX only
      1      0         |  0       X         | Disabled
      1      X         |  1       X         | TX + RX
      1      1         |  0       1         | RX only

  It is important to note that the advertised bits reflect the *current
  configuration* of the MAC, which may not represent its full hardware
  capabilities.

Kernel Policy: "Set and Trust"
==============================

The ethtool pause API is defined as a **wish policy** for
IEEE 802.3 link-wide PAUSE only. A user request is always accepted
as the preferred configuration, but it may not be possible to apply
it in all link states.

Key constraints:

- Link-wide PAUSE is not valid on half-duplex links.
- Link-wide PAUSE cannot be used together with Priority-based Flow Control
  (PFC, IEEE 802.1Q Clause 36).
- If autonegotiation is active and the link is currently down, the future
  mode is not yet known.

Because of these constraints, the kernel stores the requested setting
and applies it only when the link is in a compatible state.

Implications for userspace:

1. Set once (the "wish"): the requested Rx/Tx PAUSE policy is
   remembered even if it cannot be applied immediately.
2. Applied conditionally: when the link comes up, the kernel enables
   PAUSE only if the active mode allows it.

Component Roles in Flow Control
===============================

The configuration of flow control involves several components, each with a
distinct role.

The MAC (Media Access Controller)
---------------------------------
The MAC is the hardware component that actually sends and receives PAUSE
frames. Its capabilities define the upper limit of what the driver can support.
For link-wide PAUSE, MACs can vary in their support for symmetric (both
directions) or asymmetric (independent TX/RX) flow control.

For PFC, the MAC must be capable of generating and interpreting the
priority-based PAUSE frames and managing separate pause states for each
traffic class.

Many MACs also implement automatic PAUSE frame transmission based on the fill
level of their internal RX FIFO. This is typically configured with two
thresholds:

* **FLOW_ON (High Water Mark)**: When the RX FIFO usage reaches this
  threshold, the MAC automatically transmits a PAUSE frame to stop the sender.

* **FLOW_OFF (Low Water Mark)**: When the RX FIFO usage drops below this
  threshold, the MAC transmits a PAUSE frame with a quantum of zero to tell
  the sender it can resume transmission.

The PHY (Physical Layer Transceiver)
------------------------------------
The PHY's role is distinct for each flow control mechanism:

* **Link-wide PAUSE**: During the autonegotiation process, the PHY is
  responsible for advertising the device's flow control capabilities. See the
  "Link-wide PAUSE Autonegotiation Details" section for more information.

* **Half-Duplex Collision-Based Flow Control**: The PHY is fundamental to the
  CSMA/CD process. It performs carrier sensing (checking if the line is idle)
  and collision detection, which is the mechanism leveraged to throttle the
  sender.

* **Priority-based Flow Control (PFC)**: The PHY is not directly involved in
  negotiating PFC capabilities. Its role is to establish the physical link.
  PFC negotiation happens at a higher layer via the Data Center Bridging
  Capability Exchange Protocol (DCBX).

User Space Interface
====================
The primary user space tools are ``ethtool`` for link-wide PAUSE and ``dcb`` for
PFC. They communicate with the kernel to configure the network device driver
and underlying hardware.

**Link-wide PAUSE Netlink Interface (``ethtool``)**

See the ethtool Netlink spec (``Documentation/netlink/specs/ethtool.yaml``)
for the authoritative definition of the Pause control and Pause statistics
attributes. The generated UAPI is in
``include/uapi/linux/ethtool_netlink_generated.h``.

**PFC Netlink Interface (``dcb``)**

The authoritative definitions for DCB/PFC netlink attributes and commands are in
``include/uapi/linux/dcbnl.h``. See also the ``dcb(8)`` manual page and the DCB
subsystem documentation for userspace configuration details.

