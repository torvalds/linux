.. SPDX-License-Identifier: GPL-2.0+

========================================================
Linux Driver for the Pensando(R) Ethernet adapter family
========================================================

Pensando Linux Ethernet driver.
Copyright(c) 2019 Pensando Systems, Inc

Contents
========

- Identifying the Adapter
- Enabling the driver
- Configuring the driver
- Statistics
- Support

Identifying the Adapter
=======================

To find if one or more Pensando PCI Ethernet devices are installed on the
host, check for the PCI devices::

  $ lspci -d 1dd8:
  b5:00.0 Ethernet controller: Device 1dd8:1002
  b6:00.0 Ethernet controller: Device 1dd8:1002

If such devices are listed as above, then the ionic.ko driver should find
and configure them for use.  There should be log entries in the kernel
messages such as these::

  $ dmesg | grep ionic
  ionic 0000:b5:00.0: 126.016 Gb/s available PCIe bandwidth (8.0 GT/s PCIe x16 link)
  ionic 0000:b5:00.0 enp181s0: renamed from eth0
  ionic 0000:b5:00.0 enp181s0: Link up - 100 Gbps
  ionic 0000:b6:00.0: 126.016 Gb/s available PCIe bandwidth (8.0 GT/s PCIe x16 link)
  ionic 0000:b6:00.0 enp182s0: renamed from eth0
  ionic 0000:b6:00.0 enp182s0: Link up - 100 Gbps

Driver and firmware version information can be gathered with either of
ethtool or devlink tools::

  $ ethtool -i enp181s0
  driver: ionic
  version: 5.7.0
  firmware-version: 1.8.0-28
  ...

  $ devlink dev info pci/0000:b5:00.0
  pci/0000:b5:00.0:
    driver ionic
    serial_number FLM18420073
    versions:
        fixed:
          asic.id 0x0
          asic.rev 0x0
        running:
          fw 1.8.0-28

See Documentation/networking/devlink/ionic.rst for more information
on the devlink dev info data.

Enabling the driver
===================

The driver is enabled via the standard kernel configuration system,
using the make command::

  make oldconfig/menuconfig/etc.

The driver is located in the menu structure at:

  -> Device Drivers
    -> Network device support (NETDEVICES [=y])
      -> Ethernet driver support
        -> Pensando devices
          -> Pensando Ethernet IONIC Support

Configuring the Driver
======================

MTU
---

Jumbo frame support is available with a maximum size of 9194 bytes.

Interrupt coalescing
--------------------

Interrupt coalescing can be configured by changing the rx-usecs value with
the "ethtool -C" command.  The rx-usecs range is 0-190.  The tx-usecs value
reflects the rx-usecs value as they are tied together on the same interrupt.

SR-IOV
------

Minimal SR-IOV support is currently offered and can be enabled by setting
the sysfs 'sriov_numvfs' value, if supported by your particular firmware
configuration.

Statistics
==========

Basic hardware stats
--------------------

The commands ``netstat -i``, ``ip -s link show``, and ``ifconfig`` show
a limited set of statistics taken directly from firmware.  For example::

  $ ip -s link show enp181s0
  7: enp181s0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP mode DEFAULT group default qlen 1000
      link/ether 00:ae:cd:00:07:68 brd ff:ff:ff:ff:ff:ff
      RX: bytes  packets  errors  dropped overrun mcast
      414        5        0       0       0       0
      TX: bytes  packets  errors  dropped carrier collsns
      1384       18       0       0       0       0

ethtool -S
----------

The statistics shown from the ``ethtool -S`` command includes a combination of
driver counters and firmware counters, including port and queue specific values.
The driver values are counters computed by the driver, and the firmware values
are gathered by the firmware from the port hardware and passed through the
driver with no further interpretation.

Driver port specific::

     tx_packets: 12
     tx_bytes: 964
     rx_packets: 5
     rx_bytes: 414
     tx_tso: 0
     tx_tso_bytes: 0
     tx_csum_none: 12
     tx_csum: 0
     rx_csum_none: 0
     rx_csum_complete: 3
     rx_csum_error: 0

Driver queue specific::

     tx_0_pkts: 3
     tx_0_bytes: 294
     tx_0_clean: 3
     tx_0_dma_map_err: 0
     tx_0_linearize: 0
     tx_0_frags: 0
     tx_0_tso: 0
     tx_0_tso_bytes: 0
     tx_0_csum_none: 3
     tx_0_csum: 0
     tx_0_vlan_inserted: 0
     rx_0_pkts: 2
     rx_0_bytes: 120
     rx_0_dma_map_err: 0
     rx_0_alloc_err: 0
     rx_0_csum_none: 0
     rx_0_csum_complete: 0
     rx_0_csum_error: 0
     rx_0_dropped: 0
     rx_0_vlan_stripped: 0

Firmware port specific::

     hw_tx_dropped: 0
     hw_rx_dropped: 0
     hw_rx_over_errors: 0
     hw_rx_missed_errors: 0
     hw_tx_aborted_errors: 0
     frames_rx_ok: 15
     frames_rx_all: 15
     frames_rx_bad_fcs: 0
     frames_rx_bad_all: 0
     octets_rx_ok: 1290
     octets_rx_all: 1290
     frames_rx_unicast: 10
     frames_rx_multicast: 5
     frames_rx_broadcast: 0
     frames_rx_pause: 0
     frames_rx_bad_length: 0
     frames_rx_undersized: 0
     frames_rx_oversized: 0
     frames_rx_fragments: 0
     frames_rx_jabber: 0
     frames_rx_pripause: 0
     frames_rx_stomped_crc: 0
     frames_rx_too_long: 0
     frames_rx_vlan_good: 3
     frames_rx_dropped: 0
     frames_rx_less_than_64b: 0
     frames_rx_64b: 4
     frames_rx_65b_127b: 11
     frames_rx_128b_255b: 0
     frames_rx_256b_511b: 0
     frames_rx_512b_1023b: 0
     frames_rx_1024b_1518b: 0
     frames_rx_1519b_2047b: 0
     frames_rx_2048b_4095b: 0
     frames_rx_4096b_8191b: 0
     frames_rx_8192b_9215b: 0
     frames_rx_other: 0
     frames_tx_ok: 31
     frames_tx_all: 31
     frames_tx_bad: 0
     octets_tx_ok: 2614
     octets_tx_total: 2614
     frames_tx_unicast: 8
     frames_tx_multicast: 21
     frames_tx_broadcast: 2
     frames_tx_pause: 0
     frames_tx_pripause: 0
     frames_tx_vlan: 0
     frames_tx_less_than_64b: 0
     frames_tx_64b: 4
     frames_tx_65b_127b: 27
     frames_tx_128b_255b: 0
     frames_tx_256b_511b: 0
     frames_tx_512b_1023b: 0
     frames_tx_1024b_1518b: 0
     frames_tx_1519b_2047b: 0
     frames_tx_2048b_4095b: 0
     frames_tx_4096b_8191b: 0
     frames_tx_8192b_9215b: 0
     frames_tx_other: 0
     frames_tx_pri_0: 0
     frames_tx_pri_1: 0
     frames_tx_pri_2: 0
     frames_tx_pri_3: 0
     frames_tx_pri_4: 0
     frames_tx_pri_5: 0
     frames_tx_pri_6: 0
     frames_tx_pri_7: 0
     frames_rx_pri_0: 0
     frames_rx_pri_1: 0
     frames_rx_pri_2: 0
     frames_rx_pri_3: 0
     frames_rx_pri_4: 0
     frames_rx_pri_5: 0
     frames_rx_pri_6: 0
     frames_rx_pri_7: 0
     tx_pripause_0_1us_count: 0
     tx_pripause_1_1us_count: 0
     tx_pripause_2_1us_count: 0
     tx_pripause_3_1us_count: 0
     tx_pripause_4_1us_count: 0
     tx_pripause_5_1us_count: 0
     tx_pripause_6_1us_count: 0
     tx_pripause_7_1us_count: 0
     rx_pripause_0_1us_count: 0
     rx_pripause_1_1us_count: 0
     rx_pripause_2_1us_count: 0
     rx_pripause_3_1us_count: 0
     rx_pripause_4_1us_count: 0
     rx_pripause_5_1us_count: 0
     rx_pripause_6_1us_count: 0
     rx_pripause_7_1us_count: 0
     rx_pause_1us_count: 0
     frames_tx_truncated: 0


Support
=======

For general Linux networking support, please use the netdev mailing
list, which is monitored by Pensando personnel::

  netdev@vger.kernel.org

For more specific support needs, please use the Pensando driver support
email::

  drivers@pensando.io
