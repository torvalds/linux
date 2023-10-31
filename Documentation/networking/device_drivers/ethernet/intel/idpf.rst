.. SPDX-License-Identifier: GPL-2.0+

==========================================================================
idpf Linux* Base Driver for the Intel(R) Infrastructure Data Path Function
==========================================================================

Intel idpf Linux driver.
Copyright(C) 2023 Intel Corporation.

.. contents::

The idpf driver serves as both the Physical Function (PF) and Virtual Function
(VF) driver for the Intel(R) Infrastructure Data Path Function.

Driver information can be obtained using ethtool, lspci, and ip.

For questions related to hardware requirements, refer to the documentation
supplied with your Intel adapter. All hardware requirements listed apply to use
with Linux.


Identifying Your Adapter
========================
For information on how to identify your adapter, and for the latest Intel
network drivers, refer to the Intel Support website:
http://www.intel.com/support


Additional Features and Configurations
======================================

ethtool
-------
The driver utilizes the ethtool interface for driver configuration and
diagnostics, as well as displaying statistical information. The latest ethtool
version is required for this functionality. If you don't have one yet, you can
obtain it at:
https://kernel.org/pub/software/network/ethtool/


Viewing Link Messages
---------------------
Link messages will not be displayed to the console if the distribution is
restricting system messages. In order to see network driver link messages on
your console, set dmesg to eight by entering the following::

  # dmesg -n 8

.. note::
   This setting is not saved across reboots.


Jumbo Frames
------------
Jumbo Frames support is enabled by changing the Maximum Transmission Unit (MTU)
to a value larger than the default value of 1500.

Use the ip command to increase the MTU size. For example, enter the following
where <ethX> is the interface number::

  # ip link set mtu 9000 dev <ethX>
  # ip link set up dev <ethX>

.. note::
   The maximum MTU setting for jumbo frames is 9706. This corresponds to the
   maximum jumbo frame size of 9728 bytes.

.. note::
   This driver will attempt to use multiple page sized buffers to receive
   each jumbo packet. This should help to avoid buffer starvation issues when
   allocating receive packets.

.. note::
   Packet loss may have a greater impact on throughput when you use jumbo
   frames. If you observe a drop in performance after enabling jumbo frames,
   enabling flow control may mitigate the issue.


Performance Optimization
========================
Driver defaults are meant to fit a wide variety of workloads, but if further
optimization is required, we recommend experimenting with the following
settings.


Interrupt Rate Limiting
-----------------------
This driver supports an adaptive interrupt throttle rate (ITR) mechanism that
is tuned for general workloads. The user can customize the interrupt rate
control for specific workloads, via ethtool, adjusting the number of
microseconds between interrupts.

To set the interrupt rate manually, you must disable adaptive mode::

  # ethtool -C <ethX> adaptive-rx off adaptive-tx off

For lower CPU utilization:
 - Disable adaptive ITR and lower Rx and Tx interrupts. The examples below
   affect every queue of the specified interface.

 - Setting rx-usecs and tx-usecs to 80 will limit interrupts to about
   12,500 interrupts per second per queue::

     # ethtool -C <ethX> adaptive-rx off adaptive-tx off rx-usecs 80
     tx-usecs 80

For reduced latency:
 - Disable adaptive ITR and ITR by setting rx-usecs and tx-usecs to 0
   using ethtool::

     # ethtool -C <ethX> adaptive-rx off adaptive-tx off rx-usecs 0
     tx-usecs 0

Per-queue interrupt rate settings:
 - The following examples are for queues 1 and 3, but you can adjust other
   queues.

 - To disable Rx adaptive ITR and set static Rx ITR to 10 microseconds or
   about 100,000 interrupts/second, for queues 1 and 3::

     # ethtool --per-queue <ethX> queue_mask 0xa --coalesce adaptive-rx off
     rx-usecs 10

 - To show the current coalesce settings for queues 1 and 3::

     # ethtool --per-queue <ethX> queue_mask 0xa --show-coalesce



Virtualized Environments
------------------------
In addition to the other suggestions in this section, the following may be
helpful to optimize performance in VMs.

 - Using the appropriate mechanism (vcpupin) in the VM, pin the CPUs to
   individual LCPUs, making sure to use a set of CPUs included in the
   device's local_cpulist: /sys/class/net/<ethX>/device/local_cpulist.

 - Configure as many Rx/Tx queues in the VM as available. (See the idpf driver
   documentation for the number of queues supported.) For example::

     # ethtool -L <virt_interface> rx <max> tx <max>


Support
=======
For general information, go to the Intel support website at:
http://www.intel.com/support/

If an issue is identified with the released source code on a supported kernel
with a supported adapter, email the specific information related to the issue
to intel-wired-lan@lists.osuosl.org.


Trademarks
==========
Intel is a trademark or registered trademark of Intel Corporation or its
subsidiaries in the United States and/or other countries.

* Other names and brands may be claimed as the property of others.
