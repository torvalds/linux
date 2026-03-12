.. SPDX-License-Identifier: GPL-2.0

====================
Considering hardware
====================

:Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>

The way a workload is handled can be influenced by the hardware it runs on.
Key components include the CPU, memory, and the buses that connect them.
These resources are shared among all applications on the system.
As a result, heavy utilization of one resource by a single application
can affect the deterministic handling of workloads in other applications.

Below is a brief overview.

System memory and cache
-----------------------

Main memory and the associated caches are the most common shared resources among
tasks in a system. One task can dominate the available caches, forcing another
task to wait until a cache line is written back to main memory before it can
proceed. The impact of this contention varies based on write patterns and the
size of the caches available. Larger caches may reduce stalls because more lines
can be buffered before being written back. Conversely, certain write patterns
may trigger the cache controller to flush many lines at once, causing
applications to stall until the operation completes.

This issue can be partly mitigated if applications do not share the same CPU
cache. The kernel is aware of the cache topology and exports this information to
user space. Tools such as **lstopo** from the Portable Hardware Locality (hwloc)
project (https://www.open-mpi.org/projects/hwloc/) can visualize the hierarchy.

Avoiding shared L2 or L3 caches is not always possible. Even when cache sharing
is minimized, bottlenecks can still occur when accessing system memory. Memory
is used not only by the CPU but also by peripheral devices via DMA, such as
graphics cards or network adapters.

In some cases, cache and memory bottlenecks can be controlled if the hardware
provides the necessary support. On x86 systems, Intel offers Cache Allocation
Technology (CAT), which enables cache partitioning among applications and
provides control over the interconnect. AMD provides similar functionality under
Platform Quality of Service (PQoS). On Arm64, the equivalent is Memory
System Resource Partitioning and Monitoring (MPAM).

These features can be configured through the Linux Resource Control interface.
For details, see Documentation/filesystems/resctrl.rst.

The perf tool can be used to monitor cache behavior. It can analyze
cache misses of an application and compare how they change under
different workloads on a neighboring CPU. Even more powerful, the perf
c2c tool can help identify cache-to-cache issues, where multiple CPU
cores repeatedly access and modify data on the same cache line.

Hardware buses
--------------

Real-time systems often need to access hardware directly to perform their work.
Any latency in this process is undesirable, as it can affect the outcome of the
task. For example, on an I/O bus, a changed output may not become immediately
visible but instead appear with variable delay depending on the latency of the
bus used for communication.

A bus such as PCI is relatively simple because register accesses are routed
directly to the connected device. In the worst case, a read operation stalls the
CPU until the device responds.

A bus such as USB is more complex, involving multiple layers. A register read
or write is wrapped in a USB Request Block (URB), which is then sent by the
USB host controller to the device. Timing and latency are influenced by the
underlying USB bus. Requests cannot be sent immediately; they must align with
the next frame boundary according to the endpoint type and the host controller's
scheduling rules. This can introduce delays and additional latency. For example,
a network device connected via USB may still deliver sufficient throughput, but
the added latency when sending or receiving packets may fail to meet the
requirements of certain real-time use cases.

Additional restrictions on bus latency can arise from power management. For
instance, PCIe with Active State Power Management (ASPM) enabled can suspend
the link between the device and the host. While this behavior is beneficial for
power savings, it delays device access and adds latency to responses. This issue
is not limited to PCIe; internal buses within a System-on-Chip (SoC) can also be
affected by power management mechanisms.

Virtualization
--------------

In a virtualized environment such as KVM, each guest CPU is represented as a
thread on the host. If such a thread runs with real-time priority, the system
should be tested to confirm it can sustain this behavior over extended periods.
Because of its priority, the thread will not be preempted by lower-priority
threads (such as SCHED_OTHER), which may then receive no CPU time. This can
cause problems if a lower-priority thread is pinned to a CPU already occupied by
a real-time task and unable to make progress. Even if a CPU has been isolated,
the system may still (accidentally) start a per‑CPU thread on that CPU.
Ensuring that a guest CPU goes idle is difficult, as it requires avoiding both
task scheduling and interrupt handling. Furthermore, if the guest CPU does go
idle but the guest system is booted with the option **idle=poll**, the guest
CPU will never enter an idle state and will instead spin until an event
arrives.

Device handling introduces additional considerations. Emulated PCI devices or
VirtIO devices require a counterpart on the host to complete requests. This
adds latency because the host must intercept and either process the request
directly or schedule a thread for its completion. These delays can be avoided if
the required PCI device is passed directly through to the guest. Some devices,
such as networking or storage controllers, support the PCIe SR-IOV feature.
SR-IOV allows a single PCIe device to be divided into multiple virtual functions,
which can then be assigned to different guests.

Networking
----------

For low-latency networking, the full networking stack may be undesirable, as it
can introduce additional sources of delay. In this context, XDP can be used
as a shortcut to bypass much of the stack while still relying on the kernel's
network driver.

The requirements are that the network driver must support XDP- preferably using
an "skb pool" and that the application must use an XDP socket. Additional
configuration may involve BPF filters, tuning networking queues, or configuring
qdiscs for time-based transmission. These techniques are often
applied in Time-Sensitive Networking (TSN) environments.

Documenting all required steps exceeds the scope of this text. For detailed
guidance, see the TSN documentation at https://tsn.readthedocs.io.

Another useful resource is the Linux Real-Time Communication Testbench
https://github.com/Linutronix/RTC-Testbench.
The goal of this project is to validate real-time network communication. It can
be thought of as a "cyclictest" for networking and also serves as a starting
point for application development.
