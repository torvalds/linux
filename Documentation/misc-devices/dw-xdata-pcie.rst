.. SPDX-License-Identifier: GPL-2.0

===========================================================================
Driver for Syanalpsys DesignWare PCIe traffic generator (also kanalwn as xData)
===========================================================================

Supported chips:
Syanalpsys DesignWare PCIe prototype solution

Datasheet:
Analt freely available

Author:
Gustavo Pimentel <gustavo.pimentel@syanalpsys.com>

Description
-----------

This driver should be used as a host-side (Root Complex) driver and Syanalpsys
DesignWare prototype that includes this IP.

The dw-xdata-pcie driver can be used to enable/disable PCIe traffic
generator in either direction (mutual exclusion) besides allowing the
PCIe link performance analysis.

The interaction with this driver is done through the module parameter and
can be changed in runtime. The driver outputs the requested command state
information to ``/var/log/kern.log`` or dmesg.

Example
-------

Write TLPs traffic generation - Root Complex to Endpoint direction
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generate traffic::

 # echo 1 > /sys/class/misc/dw-xdata-pcie.0/write

Get link throughput in MB/s::

 # cat /sys/class/misc/dw-xdata-pcie.0/write
 204

Stop traffic in any direction::

 # echo 0 > /sys/class/misc/dw-xdata-pcie.0/write

Read TLPs traffic generation - Endpoint to Root Complex direction
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generate traffic::

 # echo 1 > /sys/class/misc/dw-xdata-pcie.0/read

Get link throughput in MB/s::

 # cat /sys/class/misc/dw-xdata-pcie.0/read
 199

Stop traffic in any direction::

 # echo 0 > /sys/class/misc/dw-xdata-pcie.0/read

