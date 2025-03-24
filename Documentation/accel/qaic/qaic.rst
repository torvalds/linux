.. SPDX-License-Identifier: GPL-2.0-only

=============
 QAIC driver
=============

The QAIC driver is the Kernel Mode Driver (KMD) for the AIC100 family of AI
accelerator products.

Interrupts
==========

IRQ Storm Mitigation
--------------------

While the AIC100 DMA Bridge hardware implements an IRQ storm mitigation
mechanism, it is still possible for an IRQ storm to occur. A storm can happen
if the workload is particularly quick, and the host is responsive. If the host
can drain the response FIFO as quickly as the device can insert elements into
it, then the device will frequently transition the response FIFO from empty to
non-empty and generate MSIs at a rate equivalent to the speed of the
workload's ability to process inputs. The lprnet (license plate reader network)
workload is known to trigger this condition, and can generate in excess of 100k
MSIs per second. It has been observed that most systems cannot tolerate this
for long, and will crash due to some form of watchdog due to the overhead of
the interrupt controller interrupting the host CPU.

To mitigate this issue, the QAIC driver implements specific IRQ handling. When
QAIC receives an IRQ, it disables that line. This prevents the interrupt
controller from interrupting the CPU. Then AIC drains the FIFO. Once the FIFO
is drained, QAIC implements a "last chance" polling algorithm where QAIC will
sleep for a time to see if the workload will generate more activity. The IRQ
line remains disabled during this time. If no activity is detected, QAIC exits
polling mode and reenables the IRQ line.

This mitigation in QAIC is very effective. The same lprnet usecase that
generates 100k IRQs per second (per /proc/interrupts) is reduced to roughly 64
IRQs over 5 minutes while keeping the host system stable, and having the same
workload throughput performance (within run to run noise variation).

Single MSI Mode
---------------

MultiMSI is not well supported on all systems; virtualized ones even less so
(circa 2023). Between hypervisors masking the PCIe MSI capability structure to
large memory requirements for vIOMMUs (required for supporting MultiMSI), it is
useful to be able to fall back to a single MSI when needed.

To support this fallback, we allow the case where only one MSI is able to be
allocated, and share that one MSI between MHI and the DBCs. The device detects
when only one MSI has been configured and directs the interrupts for the DBCs
to the interrupt normally used for MHI. Unfortunately this means that the
interrupt handlers for every DBC and MHI wake up for every interrupt that
arrives; however, the DBC threaded irq handlers only are started when work to be
done is detected (MHI will always start its threaded handler).

If the DBC is configured to force MSI interrupts, this can circumvent the
software IRQ storm mitigation mentioned above. Since the MSI is shared it is
never disabled, allowing each new entry to the FIFO to trigger a new interrupt.


Neural Network Control (NNC) Protocol
=====================================

The implementation of NNC is split between the KMD (QAIC) and UMD. In general
QAIC understands how to encode/decode NNC wire protocol, and elements of the
protocol which require kernel space knowledge to process (for example, mapping
host memory to device IOVAs). QAIC understands the structure of a message, and
all of the transactions. QAIC does not understand commands (the payload of a
passthrough transaction).

QAIC handles and enforces the required little endianness and 64-bit alignment,
to the degree that it can. Since QAIC does not know the contents of a
passthrough transaction, it relies on the UMD to satisfy the requirements.

The terminate transaction is of particular use to QAIC. QAIC is not aware of
the resources that are loaded onto a device since the majority of that activity
occurs within NNC commands. As a result, QAIC does not have the means to
roll back userspace activity. To ensure that a userspace client's resources
are fully released in the case of a process crash, or a bug, QAIC uses the
terminate command to let QSM know when a user has gone away, and the resources
can be released.

QSM can report a version number of the NNC protocol it supports. This is in the
form of a Major number and a Minor number.

Major number updates indicate changes to the NNC protocol which impact the
message format, or transactions (impacts QAIC).

Minor number updates indicate changes to the NNC protocol which impact the
commands (does not impact QAIC).

uAPI
====

QAIC creates an accel device per physical PCIe device. This accel device exists
for as long as the PCIe device is known to Linux.

The PCIe device may not be in the state to accept requests from userspace at
all times. QAIC will trigger KOBJ_ONLINE/OFFLINE uevents to advertise when the
device can accept requests (ONLINE) and when the device is no longer accepting
requests (OFFLINE) because of a reset or other state transition.

QAIC defines a number of driver specific IOCTLs as part of the userspace API.

DRM_IOCTL_QAIC_MANAGE
  This IOCTL allows userspace to send a NNC request to the QSM. The call will
  block until a response is received, or the request has timed out.

DRM_IOCTL_QAIC_CREATE_BO
  This IOCTL allows userspace to allocate a buffer object (BO) which can send
  or receive data from a workload. The call will return a GEM handle that
  represents the allocated buffer. The BO is not usable until it has been
  sliced (see DRM_IOCTL_QAIC_ATTACH_SLICE_BO).

DRM_IOCTL_QAIC_MMAP_BO
  This IOCTL allows userspace to prepare an allocated BO to be mmap'd into the
  userspace process.

DRM_IOCTL_QAIC_ATTACH_SLICE_BO
  This IOCTL allows userspace to slice a BO in preparation for sending the BO
  to the device. Slicing is the operation of describing what portions of a BO
  get sent where to a workload. This requires a set of DMA transfers for the
  DMA Bridge, and as such, locks the BO to a specific DBC.

DRM_IOCTL_QAIC_EXECUTE_BO
  This IOCTL allows userspace to submit a set of sliced BOs to the device. The
  call is non-blocking. Success only indicates that the BOs have been queued
  to the device, but does not guarantee they have been executed.

DRM_IOCTL_QAIC_PARTIAL_EXECUTE_BO
  This IOCTL operates like DRM_IOCTL_QAIC_EXECUTE_BO, but it allows userspace
  to shrink the BOs sent to the device for this specific call. If a BO
  typically has N inputs, but only a subset of those is available, this IOCTL
  allows userspace to indicate that only the first M bytes of the BO should be
  sent to the device to minimize data transfer overhead. This IOCTL dynamically
  recomputes the slicing, and therefore has some processing overhead before the
  BOs can be queued to the device.

DRM_IOCTL_QAIC_WAIT_BO
  This IOCTL allows userspace to determine when a particular BO has been
  processed by the device. The call will block until either the BO has been
  processed and can be re-queued to the device, or a timeout occurs.

DRM_IOCTL_QAIC_PERF_STATS_BO
  This IOCTL allows userspace to collect performance statistics on the most
  recent execution of a BO. This allows userspace to construct an end to end
  timeline of the BO processing for a performance analysis.

DRM_IOCTL_QAIC_DETACH_SLICE_BO
  This IOCTL allows userspace to remove the slicing information from a BO that
  was originally provided by a call to DRM_IOCTL_QAIC_ATTACH_SLICE_BO. This
  is the inverse of DRM_IOCTL_QAIC_ATTACH_SLICE_BO. The BO must be idle for
  DRM_IOCTL_QAIC_DETACH_SLICE_BO to be called. After a successful detach slice
  operation the BO may have new slicing information attached with a new call
  to DRM_IOCTL_QAIC_ATTACH_SLICE_BO. After detach slice, the BO cannot be
  executed until after a new attach slice operation. Combining attach slice
  and detach slice calls allows userspace to use a BO with multiple workloads.

Userspace Client Isolation
==========================

AIC100 supports multiple clients. Multiple DBCs can be consumed by a single
client, and multiple clients can each consume one or more DBCs. Workloads
may contain sensitive information therefore only the client that owns the
workload should be allowed to interface with the DBC.

Clients are identified by the instance associated with their open(). A client
may only use memory they allocate, and DBCs that are assigned to their
workloads. Attempts to access resources assigned to other clients will be
rejected.

Module parameters
=================

QAIC supports the following module parameters:

**datapath_polling (bool)**

Configures QAIC to use a polling thread for datapath events instead of relying
on the device interrupts. Useful for platforms with broken multiMSI. Must be
set at QAIC driver initialization. Default is 0 (off).

**mhi_timeout_ms (unsigned int)**

Sets the timeout value for MHI operations in milliseconds (ms). Must be set
at the time the driver detects a device. Default is 2000 (2 seconds).

**control_resp_timeout_s (unsigned int)**

Sets the timeout value for QSM responses to NNC messages in seconds (s). Must
be set at the time the driver is sending a request to QSM. Default is 60 (one
minute).

**wait_exec_default_timeout_ms (unsigned int)**

Sets the default timeout for the wait_exec ioctl in milliseconds (ms). Must be
set prior to the waic_exec ioctl call. A value specified in the ioctl call
overrides this for that call. Default is 5000 (5 seconds).

**datapath_poll_interval_us (unsigned int)**

Sets the polling interval in microseconds (us) when datapath polling is active.
Takes effect at the next polling interval. Default is 100 (100 us).

**timesync_delay_ms (unsigned int)**

Sets the time interval in milliseconds (ms) between two consecutive timesync
operations. Default is 1000 (1000 ms).
