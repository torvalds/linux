.. SPDX-License-Identifier: GPL-2.0

Overview
========
The Linux kernel contains a variety of code for running as a fully
enlightened guest on Microsoft's Hyper-V hypervisor.  Hyper-V
consists primarily of a bare-metal hypervisor plus a virtual machine
management service running in the parent partition (roughly
equivalent to KVM and QEMU, for example).  Guest VMs run in child
partitions.  In this documentation, references to Hyper-V usually
encompass both the hypervisor and the VMM service without making a
distinction about which functionality is provided by which
component.

Hyper-V runs on x86/x64 and arm64 architectures, and Linux guests
are supported on both.  The functionality and behavior of Hyper-V is
generally the same on both architectures unless noted otherwise.

Linux Guest Communication with Hyper-V
--------------------------------------
Linux guests communicate with Hyper-V in four different ways:

* Implicit traps: As defined by the x86/x64 or arm64 architecture,
  some guest actions trap to Hyper-V.  Hyper-V emulates the action and
  returns control to the guest.  This behavior is generally invisible
  to the Linux kernel.

* Explicit hypercalls: Linux makes an explicit function call to
  Hyper-V, passing parameters.  Hyper-V performs the requested action
  and returns control to the caller.  Parameters are passed in
  processor registers or in memory shared between the Linux guest and
  Hyper-V.   On x86/x64, hypercalls use a Hyper-V specific calling
  sequence.  On arm64, hypercalls use the ARM standard SMCCC calling
  sequence.

* Synthetic register access: Hyper-V implements a variety of
  synthetic registers.  On x86/x64 these registers appear as MSRs in
  the guest, and the Linux kernel can read or write these MSRs using
  the normal mechanisms defined by the x86/x64 architecture.  On
  arm64, these synthetic registers must be accessed using explicit
  hypercalls.

* VMbus: VMbus is a higher-level software construct that is built on
  the other 3 mechanisms.  It is a message passing interface between
  the Hyper-V host and the Linux guest.  It uses memory that is shared
  between Hyper-V and the guest, along with various signaling
  mechanisms.

The first three communication mechanisms are documented in the
`Hyper-V Top Level Functional Spec (TLFS)`_.  The TLFS describes
general Hyper-V functionality and provides details on the hypercalls
and synthetic registers.  The TLFS is currently written for the
x86/x64 architecture only.

.. _Hyper-V Top Level Functional Spec (TLFS): https://docs.microsoft.com/en-us/virtualization/hyper-v-on-windows/tlfs/tlfs

VMbus is not documented.  This documentation provides a high-level
overview of VMbus and how it works, but the details can be discerned
only from the code.

Sharing Memory
--------------
Many aspects are communication between Hyper-V and Linux are based
on sharing memory.  Such sharing is generally accomplished as
follows:

* Linux allocates memory from its physical address space using
  standard Linux mechanisms.

* Linux tells Hyper-V the guest physical address (GPA) of the
  allocated memory.  Many shared areas are kept to 1 page so that a
  single GPA is sufficient.   Larger shared areas require a list of
  GPAs, which usually do not need to be contiguous in the guest
  physical address space.  How Hyper-V is told about the GPA or list
  of GPAs varies.  In some cases, a single GPA is written to a
  synthetic register.  In other cases, a GPA or list of GPAs is sent
  in a VMbus message.

* Hyper-V translates the GPAs into "real" physical memory addresses,
  and creates a virtual mapping that it can use to access the memory.

* Linux can later revoke sharing it has previously established by
  telling Hyper-V to set the shared GPA to zero.

Hyper-V operates with a page size of 4 Kbytes. GPAs communicated to
Hyper-V may be in the form of page numbers, and always describe a
range of 4 Kbytes.  Since the Linux guest page size on x86/x64 is
also 4 Kbytes, the mapping from guest page to Hyper-V page is 1-to-1.
On arm64, Hyper-V supports guests with 4/16/64 Kbyte pages as
defined by the arm64 architecture.   If Linux is using 16 or 64
Kbyte pages, Linux code must be careful to communicate with Hyper-V
only in terms of 4 Kbyte pages.  HV_HYP_PAGE_SIZE and related macros
are used in code that communicates with Hyper-V so that it works
correctly in all configurations.

As described in the TLFS, a few memory pages shared between Hyper-V
and the Linux guest are "overlay" pages.  With overlay pages, Linux
uses the usual approach of allocating guest memory and telling
Hyper-V the GPA of the allocated memory.  But Hyper-V then replaces
that physical memory page with a page it has allocated, and the
original physical memory page is no longer accessible in the guest
VM.  Linux may access the memory normally as if it were the memory
that it originally allocated.  The "overlay" behavior is visible
only because the contents of the page (as seen by Linux) change at
the time that Linux originally establishes the sharing and the
overlay page is inserted.  Similarly, the contents change if Linux
revokes the sharing, in which case Hyper-V removes the overlay page,
and the guest page originally allocated by Linux becomes visible
again.

Before Linux does a kexec to a kdump kernel or any other kernel,
memory shared with Hyper-V should be revoked.  Hyper-V could modify
a shared page or remove an overlay page after the new kernel is
using the page for a different purpose, corrupting the new kernel.
Hyper-V does not provide a single "set everything" operation to
guest VMs, so Linux code must individually revoke all sharing before
doing kexec.   See hv_kexec_handler() and hv_crash_handler().  But
the crash/panic path still has holes in cleanup because some shared
pages are set using per-CPU synthetic registers and there's no
mechanism to revoke the shared pages for CPUs other than the CPU
running the panic path.

CPU Management
--------------
Hyper-V does not have a ability to hot-add or hot-remove a CPU
from a running VM.  However, Windows Server 2019 Hyper-V and
earlier versions may provide guests with ACPI tables that indicate
more CPUs than are actually present in the VM.  As is normal, Linux
treats these additional CPUs as potential hot-add CPUs, and reports
them as such even though Hyper-V will never actually hot-add them.
Starting in Windows Server 2022 Hyper-V, the ACPI tables reflect
only the CPUs actually present in the VM, so Linux does not report
any hot-add CPUs.

A Linux guest CPU may be taken offline using the normal Linux
mechanisms, provided no VMbus channel interrupts are assigned to
the CPU.  See the section on VMbus Interrupts for more details
on how VMbus channel interrupts can be re-assigned to permit
taking a CPU offline.

32-bit and 64-bit
-----------------
On x86/x64, Hyper-V supports 32-bit and 64-bit guests, and Linux
will build and run in either version. While the 32-bit version is
expected to work, it is used rarely and may suffer from undetected
regressions.

On arm64, Hyper-V supports only 64-bit guests.

Endian-ness
-----------
All communication between Hyper-V and guest VMs uses Little-Endian
format on both x86/x64 and arm64.  Big-endian format on arm64 is not
supported by Hyper-V, and Linux code does not use endian-ness macros
when accessing data shared with Hyper-V.

Versioning
----------
Current Linux kernels operate correctly with older versions of
Hyper-V back to Windows Server 2012 Hyper-V. Support for running
on the original Hyper-V release in Windows Server 2008/2008 R2
has been removed.

A Linux guest on Hyper-V outputs in dmesg the version of Hyper-V
it is running on.  This version is in the form of a Windows build
number and is for display purposes only. Linux code does not
test this version number at runtime to determine available features
and functionality. Hyper-V indicates feature/function availability
via flags in synthetic MSRs that Hyper-V provides to the guest,
and the guest code tests these flags.

VMbus has its own protocol version that is negotiated during the
initial VMbus connection from the guest to Hyper-V. This version
number is also output to dmesg during boot.  This version number
is checked in a few places in the code to determine if specific
functionality is present.

Furthermore, each synthetic device on VMbus also has a protocol
version that is separate from the VMbus protocol version. Device
drivers for these synthetic devices typically negotiate the device
protocol version, and may test that protocol version to determine
if specific device functionality is present.

Code Packaging
--------------
Hyper-V related code appears in the Linux kernel code tree in three
main areas:

1. drivers/hv

2. arch/x86/hyperv and arch/arm64/hyperv

3. individual device driver areas such as drivers/scsi, drivers/net,
   drivers/clocksource, etc.

A few miscellaneous files appear elsewhere. See the full list under
"Hyper-V/Azure CORE AND DRIVERS" and "DRM DRIVER FOR HYPERV
SYNTHETIC VIDEO DEVICE" in the MAINTAINERS file.

The code in #1 and #2 is built only when CONFIG_HYPERV is set.
Similarly, the code for most Hyper-V related drivers is built only
when CONFIG_HYPERV is set.

Most Hyper-V related code in #1 and #3 can be built as a module.
The architecture specific code in #2 must be built-in.  Also,
drivers/hv/hv_common.c is low-level code that is common across
architectures and must be built-in.
