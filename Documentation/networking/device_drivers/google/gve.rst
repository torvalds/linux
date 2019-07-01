.. SPDX-License-Identifier: GPL-2.0+

==============================================================
Linux kernel driver for Compute Engine Virtual Ethernet (gve):
==============================================================

Supported Hardware
===================
The GVE driver binds to a single PCI device id used by the virtual
Ethernet device found in some Compute Engine VMs.

+--------------+----------+---------+
|Field         | Value    | Comments|
+==============+==========+=========+
|Vendor ID     | `0x1AE0` | Google  |
+--------------+----------+---------+
|Device ID     | `0x0042` |         |
+--------------+----------+---------+
|Sub-vendor ID | `0x1AE0` | Google  |
+--------------+----------+---------+
|Sub-device ID | `0x0058` |         |
+--------------+----------+---------+
|Revision ID   | `0x0`    |         |
+--------------+----------+---------+
|Device Class  | `0x200`  | Ethernet|
+--------------+----------+---------+

PCI Bars
========
The gVNIC PCI device exposes three 32-bit memory BARS:
- Bar0 - Device configuration and status registers.
- Bar1 - MSI-X vector table
- Bar2 - IRQ, RX and TX doorbells

Device Interactions
===================
The driver interacts with the device in the following ways:
 - Registers
    - A block of MMIO registers
    - See gve_register.h for more detail
 - Admin Queue
    - See description below
 - Interrupts
    - See supported interrupts below

Registers
---------
All registers are MMIO and big endian.

The registers are used for initializing and configuring the device as well as
querying device status in response to management interrupts.

Admin Queue (AQ)
----------------
The Admin Queue is a PAGE_SIZE memory block, treated as an array of AQ
commands, used by the driver to issue commands to the device and set up
resources.The driver and the device maintain a count of how many commands
have been submitted and executed. To issue AQ commands, the driver must do
the following (with proper locking):

1)  Copy new commands into next available slots in the AQ array
2)  Increment its counter by he number of new commands
3)  Write the counter into the GVE_ADMIN_QUEUE_DOORBELL register
4)  Poll the ADMIN_QUEUE_EVENT_COUNTER register until it equals
    the value written to the doorbell, or until a timeout.

The device will update the status field in each AQ command reported as
executed through the ADMIN_QUEUE_EVENT_COUNTER register.

Interrupts
----------
The following interrupts are supported by the driver:

Management Interrupt
~~~~~~~~~~~~~~~~~~~~
The management interrupt is used by the device to tell the driver to
look at the GVE_DEVICE_STATUS register.

Notification Block Interrupts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
The notification block interrupts are used to tell the driver to poll
the queues associated with that interrupt.
