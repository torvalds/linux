.. SPDX-License-Identifier: GPL-2.0

=================
Devlink Line card
=================

Background
==========

The ``devlink-linecard`` mechanism is targeted for manipulation of
line cards that serve as a detachable PHY modules for modular switch
system. Following operations are provided:

  * Get a list of supported line card types.
  * Provision of a slot with specific line card type.
  * Get and monitor of line card state and its change.

Line card according to the type may contain one or more gearboxes
to mux the lanes with certain speed to multiple ports with lanes
of different speed. Line card ensures N:M mapping between
the switch ASIC modules and physical front panel ports.

Overview
========

Each line card devlink object is created by device driver,
according to the physical line card slots available on the device.

Similar to splitter cable, where the device might have no way
of detection of the splitter cable geometry, the device
might not have a way to detect line card type. For that devices,
concept of provisioning is introduced. It allows the user to:

  * Provision a line card slot with certain line card type

    - Device driver would instruct the ASIC to prepare all
      resources accordingly. The device driver would
      create all instances, namely devlink port and netdevices
      that reside on the line card, according to the line card type
  * Manipulate of line card entities even without line card
    being physically connected or powered-up
  * Setup splitter cable on line card ports

    - As on the ordinary ports, user may provision a splitter
      cable of a certain type, without the need to
      be physically connected to the port
  * Configure devlink ports and netdevices

Netdevice carrier is decided as follows:

  * Line card is not inserted or powered-down

    - The carrier is always down
  * Line card is inserted and powered up

    - The carrier is decided as for ordinary port netdevice

Line card state
===============

The ``devlink-linecard`` mechanism supports the following line card states:

  * ``unprovisioned``: Line card is not provisioned on the slot.
  * ``unprovisioning``: Line card slot is currently being unprovisioned.
  * ``provisioning``: Line card slot is currently in a process of being provisioned
    with a line card type.
  * ``provisioning_failed``: Provisioning was not successful.
  * ``provisioned``: Line card slot is provisioned with a type.
  * ``active``: Line card is powered-up and active.

The following diagram provides a general overview of ``devlink-linecard``
state transitions::

                                          +-------------------------+
                                          |                         |
       +---------------------------------->      unprovisioned      |
       |                                  |                         |
       |                                  +--------|-------^--------+
       |                                           |       |
       |                                           |       |
       |                                  +--------v-------|--------+
       |                                  |                         |
       |                                  |       provisioning      |
       |                                  |                         |
       |                                  +------------|------------+
       |                                               |
       |                 +-----------------------------+
       |                 |                             |
       |    +------------v------------+   +------------v------------+   +-------------------------+
       |    |                         |   |                         ---->                         |
       +-----   provisioning_failed   |   |       provisioned       |   |         active          |
       |    |                         |   |                         <----                         |
       |    +------------^------------+   +------------|------------+   +-------------------------+
       |                 |                             |
       |                 |                             |
       |                 |                +------------v------------+
       |                 |                |                         |
       |                 |                |     unprovisioning      |
       |                 |                |                         |
       |                 |                +------------|------------+
       |                 |                             |
       |                 +-----------------------------+
       |                                               |
       +-----------------------------------------------+


Example usage
=============

.. code:: shell

    $ devlink lc show [ DEV [ lc LC_INDEX ] ]
    $ devlink lc set DEV lc LC_INDEX [ { type LC_TYPE | notype } ]

    # Show current line card configuration and status for all slots:
    $ devlink lc

    # Set slot 8 to be provisioned with type "16x100G":
    $ devlink lc set pci/0000:01:00.0 lc 8 type 16x100G

    # Set slot 8 to be unprovisioned:
    $ devlink lc set pci/0000:01:00.0 lc 8 notype
