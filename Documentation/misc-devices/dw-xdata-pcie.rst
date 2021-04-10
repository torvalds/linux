.. SPDX-License-Identifier: GPL-2.0

===========================================================================
Driver for Synopsys DesignWare PCIe traffic generator (also known as xData)
===========================================================================

This driver should be used as a host-side (Root Complex) driver and Synopsys
DesignWare prototype that includes this IP.

The "dw-xdata-pcie" driver can be used to enable/disable PCIe traffic
generator in either direction (mutual exclusion) besides allowing the
PCIe link performance analysis.

The interaction with this driver is done through the module parameter and
can be changed in runtime. The driver outputs the requested command state
information to /var/log/kern.log or dmesg.

Request write TLPs traffic generation - Root Complex to Endpoint direction
- Command:
echo 1 > /sys/class/misc/dw-xdata-pcie/write

Get write TLPs traffic link throughput in MB/s
- Command:
cat /sys/class/misc/dw-xdata-pcie/write
- Output example:
204

Request read TLPs traffic generation - Endpoint to Root Complex direction:
- Command:
echo 1 > /sys/class/misc/dw-xdata-pcie/read

Get read TLPs traffic link throughput in MB/s
- Command:
cat /sys/class/misc/dw-xdata-pcie/read
- Output example:
199

Request to stop any current TLP transfer:
- Command:
echo 1 > /sys/class/misc/dw-xdata-pcie/stop
