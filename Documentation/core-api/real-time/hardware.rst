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

Firmware
--------

The firmware often plays a significant role in system operation because it can
perform tasks that the kernel cannot directly access, and in some cases it can
even preempt or intercept the kernel.

A common example of firmware assisting the kernel is when it provides a generic
interface to a resource. Instead of accessing an RTC chip through an I2C host
controller, the kernel may query the firmware for the current time, and the
firmware then accesses the RTC behind the scenes.

Firmware can also intercept kernel execution by providing services that
temporarily take control of the system. One example is memory scrubbing, where
the firmware periodically pauses the kernel, reads back portions of system
memory, and then returns control. During this time, the kernel is effectively
interrupted.
In contrast, some systems provide hardware-based memory scrubbing, which
operates independently of firmware or software. See
Documentation/edac/scrub.rst for details.

If the kernel is intercepted for longer periods then these periods can be made
visible with the hardware latency detector. See
Documentation/trace/hwlat_detector.rst.

The kernel can also be intercepted in response to specific events, such as
overheating. In this case, the firmware may throttle the CPU or shut it down
immediately to prevent hardware damage.

Unless the firmware is well documented, it should be thoroughly tested to
uncover any unexpected behaviour.

EFI
~~~~

EFI provides runtime services that act as a communication interface between the
firmware and the operating system. One such service is reading and writing EFI
variables, which are used, for example, to determine the boot source.

Invoking a runtime service may require the architecture to disable kernel
preemption or interrupts during the call. This means the duration of a service
invocation directly affects the system’s observable latency. There is also
nothing that prevents a service call from disabling interrupts internally while
it runs.

For these reasons, EFI runtime services are disabled by default on a PREEMPT_RT
kernel. They can still be enabled at boot time or via a Kconfig option if
required.
The native EFI runtime service implementation (where both the EFI service and
the kernel are either 32-bit or 64-bit executables) uses a wrapper mechanism
that invokes the service through a dedicated workqueue. This workqueue is named
efi_runtime, and it can be restricted to a housekeeping CPU using the
``/sys/devices/virtual/workqueue/efi_runtime/cpumask`` sysfs file. Assigning it
to a housekeeping CPU ensures that potentially long service invocations do not
impact the real-time workload which is restricted to other CPUs.

It must also be verified that the runtime services behave as expected. Some
implementations on the x86 architecture pause all other CPUs while one CPU
performs the service call. In such cases, the interruption affects all CPUs,
and restricting the workqueue to a single CPU provides no benefit.

OP-TEE (ARM)
~~~~~~~~~~~~

Execution flows from the normal world (Linux) into the secure world (OP-TEE)
through the secure monitor at EL3. The transition is initiated by the `smc`
(Secure Monitor Call) opcode or the `hvc` (Hypervisor Call) opcode together
with a function identifier. The calling convention defines two types of calls:
**yielding calls** and **fast calls**:

- A **yielding call** unmasks interrupts before handling the requested service,
  allowing normal world interrupts to occur.
- A **fast call** handles the requested service atomically, without allowing
  interrupts from either the normal world or the secure world.

In addition, the secure world (EL3 and OP-TEE) can receive interrupts routed to
the secure world. While a secure world interrupt is being serviced,
normal world interrupts are masked and cannot preempt the operation.

The transition from normal world to secure monitor to OP-TEE and back introduces
additional latency due to world switching and context save/restore. This
overhead is typically a few microseconds and usually remains within the noise
floor.

It is worth noting that the normal world cannot mask secure interrupts, while
the secure world can mask normal-world interrupts during execution. How OP-TEE
affects real-time workloads depends on whether secure interrupts are enabled
and which OP-TEE services are invoked.

A practical concern is any fast call that runs longer than expected, for
example a function that occasionally performs a long-running cryptographic
computation. Another example that may block in an unexpected way are OP-TEE
drivers that issue RPC requests. An OP-TEE service in the secure world (RPMB
for instance) may need to issue a request back to the normal world (the Linux
driver) in order to complete the operation. While Linux remains preemptible,
the thread that issued the request stays blocked until the RPC completes and
the secure function call returns.

The TF-A project provides documentation on interrupt management:
https://trustedfirmware-a.readthedocs.io/en/latest/design/interrupt-framework-design.html#interrupt-management-framework

The OP-TEE project provides documentation on how interrupts are handled:
https://optee.readthedocs.io/en/latest/architecture/core.html#interrupt-handling
