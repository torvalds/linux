.. SPDX-License-Identifier: GPL-2.0+

=============================================================
Linux Base Driver for Intel(R) Ethernet Multi-host Controller
=============================================================

August 20, 2018
Copyright(c) 2015-2018 Intel Corporation.

Contents
========
- Identifying Your Adapter
- Additional Configurations
- Performance Tuning
- Known Issues
- Support

Identifying Your Adapter
========================
The driver in this release is compatible with devices based on the Intel(R)
Ethernet Multi-host Controller.

For information on how to identify your adapter, and for the latest Intel
network drivers, refer to the Intel Support website:
https://www.intel.com/support


Flow Control
------------
The Intel(R) Ethernet Switch Host Interface Driver does not support Flow
Control. It will not send pause frames. This may result in dropped frames.


Virtual Functions (VFs)
-----------------------
Use sysfs to enable VFs.
Valid Range: 0-64

For example::

    echo $num_vf_enabled > /sys/class/net/$dev/device/sriov_numvfs //enable VFs
    echo 0 > /sys/class/net/$dev/device/sriov_numvfs //disable VFs

NOTE: Neither the device nor the driver control how VFs are mapped into config
space. Bus layout will vary by operating system. On operating systems that
support it, you can check sysfs to find the mapping.

NOTE: When SR-IOV mode is enabled, hardware VLAN filtering and VLAN tag
stripping/insertion will remain enabled. Please remove the old VLAN filter
before the new VLAN filter is added. For example::

    ip link set eth0 vf 0 vlan 100	// set vlan 100 for VF 0
    ip link set eth0 vf 0 vlan 0	// Delete vlan 100
    ip link set eth0 vf 0 vlan 200	// set a new vlan 200 for VF 0


Additional Features and Configurations
======================================

Jumbo Frames
------------
Jumbo Frames support is enabled by changing the Maximum Transmission Unit (MTU)
to a value larger than the default value of 1500.

Use the ifconfig command to increase the MTU size. For example, enter the
following where <x> is the interface number::

    ifconfig eth<x> mtu 9000 up

Alternatively, you can use the ip command as follows::

    ip link set mtu 9000 dev eth<x>
    ip link set up dev eth<x>

This setting is not saved across reboots. The setting change can be made
permanent by adding 'MTU=9000' to the file:

- For RHEL: /etc/sysconfig/network-scripts/ifcfg-eth<x>
- For SLES: /etc/sysconfig/network/<config_file>

NOTE: The maximum MTU setting for Jumbo Frames is 15342. This value coincides
with the maximum Jumbo Frames size of 15364 bytes.

NOTE: This driver will attempt to use multiple page sized buffers to receive
each jumbo packet. This should help to avoid buffer starvation issues when
allocating receive packets.


Generic Receive Offload, aka GRO
--------------------------------
The driver supports the in-kernel software implementation of GRO. GRO has
shown that by coalescing Rx traffic into larger chunks of data, CPU
utilization can be significantly reduced when under large Rx load. GRO is an
evolution of the previously-used LRO interface. GRO is able to coalesce
other protocols besides TCP. It's also safe to use with configurations that
are problematic for LRO, namely bridging and iSCSI.



Supported ethtool Commands and Options for Filtering
----------------------------------------------------
-n --show-nfc
  Retrieves the receive network flow classification configurations.

rx-flow-hash tcp4|udp4|ah4|esp4|sctp4|tcp6|udp6|ah6|esp6|sctp6
  Retrieves the hash options for the specified network traffic type.

-N --config-nfc
  Configures the receive network flow classification.

rx-flow-hash tcp4|udp4|ah4|esp4|sctp4|tcp6|udp6|ah6|esp6|sctp6 m|v|t|s|d|f|n|r
  Configures the hash options for the specified network traffic type.

- udp4: UDP over IPv4
- udp6: UDP over IPv6
- f Hash on bytes 0 and 1 of the Layer 4 header of the rx packet.
- n Hash on bytes 2 and 3 of the Layer 4 header of the rx packet.


Known Issues/Troubleshooting
============================

Enabling SR-IOV in a 64-bit Microsoft Windows Server 2012/R2 guest OS under Linux KVM
-------------------------------------------------------------------------------------
KVM Hypervisor/VMM supports direct assignment of a PCIe device to a VM. This
includes traditional PCIe devices, as well as SR-IOV-capable devices based on
the Intel Ethernet Controller XL710.


Support
=======
For general information, go to the Intel support website at:
https://www.intel.com/support/

If an issue is identified with the released source code on a supported kernel
with a supported adapter, email the specific information related to the issue
to intel-wired-lan@lists.osuosl.org.
