.. SPDX-License-Identifier: GPL-2.0

===============================================
PPE Ethernet Driver for Qualcomm IPQ SoC Family
===============================================

Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

Author: Lei Wei <quic_leiwei@quicinc.com>


Contents
========

- `PPE Overview`_
- `PPE Driver Overview`_
- `PPE Driver Supported SoCs`_
- `Enabling the Driver`_
- `Debugging`_


PPE Overview
============

IPQ (Qualcomm Internet Processor) SoC (System-on-Chip) series is Qualcomm's series of
networking SoC for Wi-Fi access points. The PPE (Packet Process Engine) is the Ethernet
packet process engine in the IPQ SoC.

Below is a simplified hardware diagram of IPQ9574 SoC which includes the PPE engine and
other blocks which are in the SoC but outside the PPE engine. These blocks work together
to enable the Ethernet for the IPQ SoC::

               +------+ +------+ +------+ +------+ +------+  +------+ start +-------+
               |netdev| |netdev| |netdev| |netdev| |netdev|  |netdev|<------|PHYLINK|
               +------+ +------+ +------+ +------+ +------+  +------+ stop  +-+-+-+-+
                                             |                                | | ^
 +-------+     +-------------------------+--------+----------------------+    | | |
 | GCC   |     |                         |  EDMA  |                      |    | | |
 +---+---+     |  PPE                    +---+----+                      |    | | |
     | clk     |                             |                           |    | | |
     +-------->| +-----------------------+------+-----+---------------+  |    | | |
               | |   Switch Core         |Port0 |     |Port7(EIP FIFO)|  |    | | |
               | |                       +---+--+     +------+--------+  |    | | |
               | |                           |               |        |  |    | | |
 +-------+     | |                    +------+---------------+----+   |  |    | | |
 |CMN PLL|     | | +---+ +---+ +----+ | +--------+                |   |  |    | | |
 +---+---+     | | |BM | |QM | |SCH | | | L2/L3  |  .......       |   |  |    | | |
 |   |         | | +---+ +---+ +----+ | +--------+                |   |  |    | | |
 |   |         | |                    +------+--------------------+   |  |    | | |
 |   |         | |                           |                        |  |    | | |
 |   v         | | +-----+-+-----+-+-----+-+-+---+--+-----+-+-----+   |  |    | | |
 | +------+    | | |Port1| |Port2| |Port3| |Port4|  |Port5| |Port6|   |  |    | | |
 | |NSSCC |    | | +-----+ +-----+ +-----+ +-----+  +-----+ +-----+   |  | mac| | |
 | +-+-+--+    | | |MAC0 | |MAC1 | |MAC2 | |MAC3 |  |MAC4 | |MAC5 |   |  |<---+ | |
 | ^ | |clk    | | +-----+-+-----+-+-----+-+-----+--+-----+-+-----+   |  | ops  | |
 | | | +------>| +----|------|-------|-------|---------|--------|-----+  |      | |
 | | |         +---------------------------------------------------------+      | |
 | | |                |      |       |       |         |        |               | |
 | | |   MII clk      |      QSGMII               USXGMII   USXGMII             | |
 | | +--------------->|      |       |       |         |        |               | |
 | |                +-------------------------+ +---------+ +---------+         | |
 | |125/312.5MHz clk|       (PCS0)            | | (PCS1)  | | (PCS2)  | pcs ops | |
 | +----------------+       UNIPHY0           | | UNIPHY1 | | UNIPHY2 |<--------+ |
 +----------------->|                         | |         | |         |           |
 | 31.25MHz ref clk +-------------------------+ +---------+ +---------+           |
 |                     |     |      |      |          |          |                |
 |                +-----------------------------------------------------+         |
 |25/50MHz ref clk| +-------------------------+    +------+   +------+  | link    |
 +--------------->| |      QUAD PHY           |    | PHY4 |   | PHY5 |  |---------+
                  | +-------------------------+    +------+   +------+  | change
                  |                                                     |
                  |                       MDIO bus                      |
                  +-----------------------------------------------------+

The CMN (Common) PLL, NSSCC (Networking Sub System Clock Controller) and GCC (Global
Clock Controller) blocks are in the SoC and act as clock providers.

The UNIPHY block is in the SoC and provides the PCS (Physical Coding Sublayer) and
XPCS (10-Gigabit Physical Coding Sublayer) functions to support different interface
modes between the PPE MAC and the external PHY.

This documentation focuses on the descriptions of PPE engine and the PPE driver.

The Ethernet functionality in the PPE (Packet Process Engine) is comprised of three
components: the switch core, port wrapper and Ethernet DMA.

The Switch core in the IPQ9574 PPE has maximum of 6 front panel ports and two FIFO
interfaces. One of the two FIFO interfaces is used for Ethernet port to host CPU
communication using Ethernet DMA. The other one is used to communicate to the EIP
engine which is used for IPsec offload. On the IPQ9574, the PPE includes 6 GMAC/XGMACs
that can be connected with external Ethernet PHY. Switch core also includes BM (Buffer
Management), QM (Queue Management) and SCH (Scheduler) modules for supporting the
packet processing.

The port wrapper provides connections from the 6 GMAC/XGMACS to UNIPHY (PCS) supporting
various modes such as SGMII/QSGMII/PSGMII/USXGMII/10G-BASER. There are 3 UNIPHY (PCS)
instances supported on the IPQ9574.

Ethernet DMA is used to transmit and receive packets between the Ethernet subsystem
and ARM host CPU.

The following lists the main blocks in the PPE engine which will be driven by this
PPE driver:

- BM
    BM is the hardware buffer manager for the PPE switch ports.
- QM
    Queue Manager for managing the egress hardware queues of the PPE switch ports.
- SCH
    The scheduler which manages the hardware traffic scheduling for the PPE switch ports.
- L2
    The L2 block performs the packet bridging in the switch core. The bridge domain is
    represented by the VSI (Virtual Switch Instance) domain in PPE. FDB learning can be
    enabled based on the VSI domain and bridge forwarding occurs within the VSI domain.
- MAC
    The PPE in the IPQ9574 supports up to six MACs (MAC0 to MAC5) which are corresponding
    to six switch ports (port1 to port6). The MAC block is connected with external PHY
    through the UNIPHY PCS block. Each MAC block includes the GMAC and XGMAC blocks and
    the switch port can select to use GMAC or XMAC through a MUX selection according to
    the external PHY's capability.
- EDMA (Ethernet DMA)
    The Ethernet DMA is used to transmit and receive Ethernet packets between the PPE
    ports and the ARM cores.

The received packet on a PPE MAC port can be forwarded to another PPE MAC port. It can
be also forwarded to internal switch port0 so that the packet can be delivered to the
ARM cores using the Ethernet DMA (EDMA) engine. The Ethernet DMA driver will deliver the
packet to the corresponding 'netdevice' interface.

The software instantiations of the PPE MAC (netdevice), PCS and external PHYs interact
with the Linux PHYLINK framework to manage the connectivity between the PPE ports and
the connected PHYs, and the port link states. This is also illustrated in above diagram.


PPE Driver Overview
===================
PPE driver is Ethernet driver for the Qualcomm IPQ SoC. It is a single platform driver
which includes the PPE part and Ethernet DMA part. The PPE part initializes and drives the
various blocks in PPE switch core such as BM/QM/L2 blocks and the PPE MACs. The EDMA part
drives the Ethernet DMA for packet transfer between PPE ports and ARM cores, and enables
the netdevice driver for the PPE ports.

The PPE driver files in drivers/net/ethernet/qualcomm/ppe/ are listed as below:

- Makefile
- ppe.c
- ppe.h
- ppe_config.c
- ppe_config.h
- ppe_debugfs.c
- ppe_debugfs.h
- ppe_regs.h

The ppe.c file contains the main PPE platform driver and undertakes the initialization of
PPE switch core blocks such as QM, BM and L2. The configuration APIs for these hardware
blocks are provided in the ppe_config.c file.

The ppe.h defines the PPE device data structure which will be used by PPE driver functions.

The ppe_debugfs.c enables the PPE statistics counters such as PPE port Rx and Tx counters,
CPU code counters and queue counters.


PPE Driver Supported SoCs
=========================

The PPE driver supports the following IPQ SoC:

- IPQ9574


Enabling the Driver
===================

The driver is located in the menu structure at::

  -> Device Drivers
    -> Network device support (NETDEVICES [=y])
      -> Ethernet driver support
        -> Qualcomm devices
          -> Qualcomm Technologies, Inc. PPE Ethernet support

If the driver is built as a module, the module will be called qcom-ppe.

The PPE driver functionally depends on the CMN PLL and NSSCC clock controller drivers.
Please make sure the dependent modules are installed before installing the PPE driver
module.


Debugging
=========

The PPE hardware counters can be accessed using debugfs interface from the
``/sys/kernel/debug/ppe/`` directory.
