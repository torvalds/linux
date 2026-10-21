.. SPDX-License-Identifier: GPL-2.0
.. _phy_port:

=================
Ethernet ports
=================

This document is a basic description of the phy_port infrastructure,
introduced to represent physical interfaces of Ethernet devices.

Without phy_port, we already have quite a lot of information about what the
media-facing interface of a NIC can do and looks like, through the
:c:type:`struct ethtool_link_ksettings <ethtool_link_ksettings>` attributes,
which includes :

 - What the NIC can do through the :c:member:`supported` field
 - What the Link Partner advertises through :c:member:`lp_advertising`
 - Which features we're advertising through :c:member:`advertising`

We also have info about the number of pairs and the PORT type. These settings
are built by aggregating together information reported by various devices that
are sitting on the link :

  - The NIC itself, through the :c:member:`get_link_ksettings` callback
  - Precise information from the MAC and PCS by using phylink in the MAC driver
  - Information reported by the PHY device
  - Information reported by an SFP module (which can itself include a PHY)

This model however starts showing its limitations when we consider devices that
have more than one media interface. In such a case, only information about the
actively used interface is reported, and it's not possible to know what the
other interfaces can do. In fact, we have very little information about whether
or not there are any other media interfaces.

The goal of the phy_port representation is to provide a way of representing a
physical interface of a NIC, regardless of what is driving the port (NIC through
a firmware, SFP module, Ethernet PHY).

Multi-port interfaces examples
==============================

Several cases of multi-interface NICs have been observed so far :

Internal MII Mux::

  +------------------+
  | SoC              |
  |          +-----+ |           +-----+
  | +-----+  |     |-------------| PHY |
  | | MAC |--| Mux | |   +-----+ +-----+
  | +-----+  |     |-----| SFP |
  |          +-----+ |   +-----+
  +------------------+

Internal Mux with internal PHY::

  +------------------------+
  | SoC                    |
  |          +-----+ +-----+
  | +-----+  |     |-| PHY |
  | | MAC |--| Mux | +-----+   +-----+
  | +-----+  |     |-----------| SFP |
  |          +-----+       |   +-----+
  +------------------------+

External Mux::

  +---------+
  | SoC     |  +-----+  +-----+
  |         |  |     |--| PHY |
  | +-----+ |  |     |  +-----+
  | | MAC |----| Mux |  +-----+
  | +-----+ |  |     |--| PHY |
  |         |  +-----+  +-----+
  |         |     |
  |    GPIO-------+
  +---------+

Double-port PHY::

  +---------+
  | SoC     | +-----+
  |         | |     |--- RJ45
  | +-----+ | |     |
  | | MAC |---| PHY |   +-----+
  | +-----+ | |     |---| SFP |
  +---------+ +-----+   +-----+

phy_port aims at providing a path to support all the above topologies, by
representing the media interfaces in a way that's agnostic to what's driving
the interface. the struct phy_port object has its own set of callback ops, and
will eventually be able to report its own ksettings::

             _____      +------+
            (     )-----| Port |
 +-----+   (       )    +------+
 | MAC |--(   ???   )
 +-----+   (       )    +------+
            (_____)-----| Port |
                        +------+

SFP ports
=========

SFP interfaces involve 2 distinct components, each represented by
a :c:type:`struct phy_port <phy_port>` instance :

 - The SFP cage itself is a :c:type:`struct phy_port <phy_port>`. It's special
   in that it's not an MDI interface, but rather a hot-pluggable MII.
   The :c:type:`struct phy_port <phy_port>` associated to it lists the different
   MII interfaces we can use on the cage.

 - The SFP module, when inserted, will also be associated to a
   :c:type:`struct phy_port <phy_port>`, that represents the various linkmodes
   that it gives access to. The module's :c:type:`struct phy_port <phy_port>`
   doesn't supersede the cage's port, it references it through
   the :c:type:`struct phy_port <phy_port>` :c:member:`upstream_port` field.

Next steps
==========

As of writing this documentation, the port's presence and information can only
be queried, and it's not possible to change any of the port's settings or select
which one should be used.

Another parallel task is the introduction of a MII muxing framework to allow the
control of non-PHY driven multi-port setups.
