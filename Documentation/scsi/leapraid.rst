.. SPDX-License-Identifier: GPL-2.0

=========================
LeapRAID Driver for Linux
=========================

Introduction
============

LeapRAID is a storage RAID controller driver developed by LeapIO Tech Inc. The
controller targets enterprise storage, cloud infrastructure, high performance
computing (HPC), and AI workloads.

It provides high-performance storage virtualization over PCI Express Gen4
and supports both SAS and SATA HDDs and SSDs. It offers both Host Bus Adapter
and RAID modes to meet diverse deployment requirements.

Supported devices
=================

- LeapHBA-8200C

Features
========

- PCIe Gen4 x8 host interface
- Support for SAS and SATA devices
- RAID levels: 0, 1, 10, 5, 50, 6, 60
- Advanced error handling and end-to-end data integrity

LeapRAID specific host attributes
=================================

::

   /sys/class/scsi_host/host*/fw_queue_depth
   /sys/class/scsi_host/host*/host_sas_address
   /sys/class/scsi_host/host*/board_name

The host "fw_queue_depth" read-only attribute shows the firmware queue
depth of the host.

The host "host_sas_address" read-only attribute shows the SAS address
of the host.

The host "board_name" read-only attribute shows the board name reported
by manufacturing page 0.

LeapRAID specific disk attributes
=================================

::

   /sys/block/<disk>/device/sas_device_handle
   /sys/block/<disk>/device/sas_ncq_prio_supported
   /sys/block/<disk>/device/sas_ncq_prio_enable

The read-only attribute "sas_device_handle" represents the disk's device
handle, which is a unique identifier maintained by the firmware.

The read-only attribute "sas_ncq_prio_supported" reports whether a SATA
device supports NCQ command priority.

The attribute "sas_ncq_prio_enable" controls NCQ command priority. A value
of 0 disables NCQ priority handling for RT-priority I/O. Writing 1 enables
NCQ priority handling when the device reports support for the feature through
VPD page 0x89. Writes to unsupported devices fail with an error.

LeapRAID module parameters
==========================

The following module parameters can be configured at driver load time to
control driver behavior and tuning options.

1. open_pcie_trace
------------------

This parameter controls whether PCIe transaction tracing is enabled in the
driver. When set to 1, PCIe trace collection is enabled by default, allowing
detailed tracing of PCIe operations for debugging and performance analysis.
Setting it to 0 disables the trace functionality to reduce overhead in
production environments.

2. enable_mp
------------

This parameter enables or disables multipath support for target devices.
When set to 1, multipath functionality is enabled (default), allowing
multiple paths to be established. Setting it to 0 disables multipath
handling.

3. max_msix_vectors
-------------------

This parameter sets the upper limit on the number of MSI-X interrupt
vectors that the driver will request during initialization. The default
value of -1 allows the driver to use all available vectors as provided
by the device. Setting a positive integer restricts the number of vectors.

4. poll_queues
--------------

This parameter specifies the number of I/O queues to be used when operating
in io_uring poll mode. The default value is 0.

File Location
=============

The driver source is located at:

``drivers/scsi/leapraid/``

.. note::

   This document is intended for kernel developers and system
   integrators who need to build, test, and deploy the LeapRAID driver.
