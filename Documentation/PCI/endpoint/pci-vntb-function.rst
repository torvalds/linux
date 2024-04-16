.. SPDX-License-Identifier: GPL-2.0

=================
PCI vNTB Function
=================

:Author: Frank Li <Frank.Li@nxp.com>

The difference between PCI NTB function and PCI vNTB function is

PCI NTB function need at two endpoint instances and connect HOST1
and HOST2.

PCI vNTB function only use one host and one endpoint(EP), use NTB
connect EP and PCI host

.. code-block:: text


  +------------+         +---------------------------------------+
  |            |         |                                       |
  +------------+         |                        +--------------+
  | NTB        |         |                        | NTB          |
  | NetDev     |         |                        | NetDev       |
  +------------+         |                        +--------------+
  | NTB        |         |                        | NTB          |
  | Transfer   |         |                        | Transfer     |
  +------------+         |                        +--------------+
  |            |         |                        |              |
  |  PCI NTB   |         |                        |              |
  |    EPF     |         |                        |              |
  |   Driver   |         |                        | PCI Virtual  |
  |            |         +---------------+        | NTB Driver   |
  |            |         | PCI EP NTB    |<------>|              |
  |            |         |  FN Driver    |        |              |
  +------------+         +---------------+        +--------------+
  |            |         |               |        |              |
  |  PCI BUS   | <-----> |  PCI EP BUS   |        |  Virtual PCI |
  |            |  PCI    |               |        |     BUS      |
  +------------+         +---------------+--------+--------------+
      PCI RC                        PCI EP

Constructs used for Implementing vNTB
=====================================

	1) Config Region
	2) Self Scratchpad Registers
	3) Peer Scratchpad Registers
	4) Doorbell (DB) Registers
	5) Memory Window (MW)


Config Region:
--------------

It is same as PCI NTB Function driver

Scratchpad Registers:
---------------------

It is appended after Config region.

.. code-block:: text


  +--------------------------------------------------+ Base
  |                                                  |
  |                                                  |
  |                                                  |
  |          Common Config Register                  |
  |                                                  |
  |                                                  |
  |                                                  |
  +-----------------------+--------------------------+ Base + span_offset
  |                       |                          |
  |    Peer Span Space    |    Span Space            |
  |                       |                          |
  |                       |                          |
  +-----------------------+--------------------------+ Base + span_offset
  |                       |                          |      + span_count * 4
  |                       |                          |
  |     Span Space        |   Peer Span Space        |
  |                       |                          |
  +-----------------------+--------------------------+
        Virtual PCI             Pcie Endpoint
        NTB Driver               NTB Driver


Doorbell Registers:
-------------------

  Doorbell Registers are used by the hosts to interrupt each other.

Memory Window:
--------------

  Actual transfer of data between the two hosts will happen using the
  memory window.

Modeling Constructs:
====================

32-bit BARs.

======  ===============
BAR NO  CONSTRUCTS USED
======  ===============
BAR0    Config Region
BAR1    Doorbell
BAR2    Memory Window 1
BAR3    Memory Window 2
BAR4    Memory Window 3
BAR5    Memory Window 4
======  ===============

64-bit BARs.

======  ===============================
BAR NO  CONSTRUCTS USED
======  ===============================
BAR0    Config Region + Scratchpad
BAR1
BAR2    Doorbell
BAR3
BAR4    Memory Window 1
BAR5
======  ===============================


