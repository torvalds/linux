.. SPDX-License-Identifier: GPL-2.0

===============
fwctl subsystem
===============

:Author: Jason Gunthorpe

Overview
========

Modern devices contain extensive amounts of FW, and in many cases, are largely
software-defined pieces of hardware. The evolution of this approach is largely a
reaction to Moore's Law where a chip tape out is now highly expensive, and the
chip design is extremely large. Replacing fixed HW logic with a flexible and
tightly coupled FW/HW combination is an effective risk mitigation against chip
respin. Problems in the HW design can be counteracted in device FW. This is
especially true for devices which present a stable and backwards compatible
interface to the operating system driver (such as NVMe).

The FW layer in devices has grown to incredible size and devices frequently
integrate clusters of fast processors to run it. For example, mlx5 devices have
over 30MB of FW code, and big configurations operate with over 1GB of FW managed
runtime state.

The availability of such a flexible layer has created quite a variety in the
industry where single pieces of silicon are now configurable software-defined
devices and can operate in substantially different ways depending on the need.
Further, we often see cases where specific sites wish to operate devices in ways
that are highly specialized and require applications that have been tailored to
their unique configuration.

Further, devices have become multi-functional and integrated to the point they
no longer fit neatly into the kernel's division of subsystems. Modern
multi-functional devices have drivers, such as bnxt/ice/mlx5/pds, that span many
subsystems while sharing the underlying hardware using the auxiliary device
system.

All together this creates a challenge for the operating system, where devices
have an expansive FW environment that needs robust device-specific debugging
support, and FW-driven functionality that is not well suited to “generic”
interfaces. fwctl seeks to allow access to the full device functionality from
user space in the areas of debuggability, management, and first-boot/nth-boot
provisioning.

fwctl is aimed at the common device design pattern where the OS and FW
communicate via an RPC message layer constructed with a queue or mailbox scheme.
In this case the driver will typically have some layer to deliver RPC messages
and collect RPC responses from device FW. The in-kernel subsystem drivers that
operate the device for its primary purposes will use these RPCs to build their
drivers, but devices also usually have a set of ancillary RPCs that don't really
fit into any specific subsystem. For example, a HW RAID controller is primarily
operated by the block layer but also comes with a set of RPCs to administer the
construction of drives within the HW RAID.

In the past when devices were more single function, individual subsystems would
grow different approaches to solving some of these common problems. For instance,
monitoring device health, manipulating its FLASH, debugging the FW,
provisioning, all have various unique interfaces across the kernel.

fwctl's purpose is to define a common set of limited rules, described below,
that allow user space to securely construct and execute RPCs inside device FW.
The rules serve as an agreement between the operating system and FW on how to
correctly design the RPC interface. As a uAPI the subsystem provides a thin
layer of discovery and a generic uAPI to deliver the RPCs and collect the
response. It supports a system of user space libraries and tools which will
use this interface to control the device using the device native protocols.

Scope of Action
---------------

fwctl drivers are strictly restricted to being a way to operate the device FW.
It is not an avenue to access random kernel internals, or other operating system
SW states.

fwctl instances must operate on a well-defined device function, and the device
should have a well-defined security model for what scope within the physical
device the function is permitted to access. For instance, the most complex PCIe
device today may broadly have several function-level scopes:

 1. A privileged function with full access to the on-device global state and
    configuration

 2. Multiple hypervisor functions with control over itself and child functions
    used with VMs

 3. Multiple VM functions tightly scoped within the VM

The device may create a logical parent/child relationship between these scopes.
For instance, a child VM's FW may be within the scope of the hypervisor FW. It is
quite common in the VFIO world that the hypervisor environment has a complex
provisioning/profiling/configuration responsibility for the function VFIO
assigns to the VM.

Further, within the function, devices often have RPC commands that fall within
some general scopes of action (see enum fwctl_rpc_scope):

 1. Access to function & child configuration, FLASH, etc. that becomes live at a
    function reset. Access to function & child runtime configuration that is
    transparent or non-disruptive to any driver or VM.

 2. Read-only access to function debug information that may report on FW objects
    in the function & child, including FW objects owned by other kernel
    subsystems.

 3. Write access to function & child debug information strictly compatible with
    the principles of kernel lockdown and kernel integrity protection. Triggers
    a kernel taint.

 4. Full debug device access. Triggers a kernel taint, requires CAP_SYS_RAWIO.

User space will provide a scope label on each RPC and the kernel must enforce the
above CAPs and taints based on that scope. A combination of kernel and FW can
enforce that RPCs are placed in the correct scope by user space.

Disallowed behavior
-------------------

There are many things this interface must not allow user space to do (without a
taint or CAP), broadly derived from the principles of kernel lockdown. Some
examples:

 1. DMA to/from arbitrary memory, hang the system, compromise FW integrity with
    untrusted code, or otherwise compromise device or system security and
    integrity.

 2. Provide an abnormal “back door” to kernel drivers. No manipulation of kernel
    objects owned by kernel drivers.

 3. Directly configure or otherwise control kernel drivers. A subsystem kernel
    driver can react to the device configuration at function reset/driver load
    time, but otherwise must not be coupled to fwctl.

 4. Operate the HW in a way that overlaps with the core purpose of another
    primary kernel subsystem, such as read/write to LBAs, send/receive of
    network packets, or operate an accelerator's data plane.

fwctl is not a replacement for device direct access subsystems like uacce or
VFIO.

Operations exposed through fwctl's non-tainting interfaces should be fully
sharable with other users of the device. For instance, exposing a RPC through
fwctl should never prevent a kernel subsystem from also concurrently using that
same RPC or hardware unit down the road. In such cases fwctl will be less
important than proper kernel subsystems that eventually emerge. Mistakes in this
area resulting in clashes will be resolved in favour of a kernel implementation.

fwctl User API
==============

.. kernel-doc:: include/uapi/fwctl/fwctl.h
.. kernel-doc:: include/uapi/fwctl/mlx5.h
.. kernel-doc:: include/uapi/fwctl/pds.h

sysfs Class
-----------

fwctl has a sysfs class (/sys/class/fwctl/fwctlNN/) and character devices
(/dev/fwctl/fwctlNN) with a simple numbered scheme. The character device
operates the iotcl uAPI described above.

fwctl devices can be related to driver components in other subsystems through
sysfs::

    $ ls /sys/class/fwctl/fwctl0/device/infiniband/
    ibp0s10f0

    $ ls /sys/class/infiniband/ibp0s10f0/device/fwctl/
    fwctl0/

    $ ls /sys/devices/pci0000:00/0000:00:0a.0/fwctl/fwctl0
    dev  device  power  subsystem  uevent

User space Community
--------------------

Drawing inspiration from nvme-cli, participating in the kernel side must come
with a user space in a common TBD git tree, at a minimum to usefully operate the
kernel driver. Providing such an implementation is a pre-condition to merging a
kernel driver.

The goal is to build user space community around some of the shared problems
we all have, and ideally develop some common user space programs with some
starting themes of:

 - Device in-field debugging

 - HW provisioning

 - VFIO child device profiling before VM boot

 - Confidential Compute topics (attestation, secure provisioning)

that stretch across all subsystems in the kernel. fwupd is a great example of
how an excellent user space experience can emerge out of kernel-side diversity.

fwctl Kernel API
================

.. kernel-doc:: drivers/fwctl/main.c
   :export:
.. kernel-doc:: include/linux/fwctl.h

fwctl Driver design
-------------------

In many cases a fwctl driver is going to be part of a larger cross-subsystem
device possibly using the auxiliary_device mechanism. In that case several
subsystems are going to be sharing the same device and FW interface layer so the
device design must already provide for isolation and cooperation between kernel
subsystems. fwctl should fit into that same model.

Part of the driver should include a description of how its scope restrictions
and security model work. The driver and FW together must ensure that RPCs
provided by user space are mapped to the appropriate scope. If the validation is
done in the driver then the validation can read a 'command effects' report from
the device, or hardwire the enforcement. If the validation is done in the FW,
then the driver should pass the fwctl_rpc_scope to the FW along with the command.

The driver and FW must cooperate to ensure that either fwctl cannot allocate
any FW resources, or any resources it does allocate are freed on FD closure.  A
driver primarily constructed around FW RPCs may find that its core PCI function
and RPC layer belongs under fwctl with auxiliary devices connecting to other
subsystems.

Each device type must be mindful of Linux's philosophy for stable ABI. The FW
RPC interface does not have to meet a strictly stable ABI, but it does need to
meet an expectation that user space tools that are deployed and in significant
use don't needlessly break. FW upgrade and kernel upgrade should keep widely
deployed tooling working.

Development and debugging focused RPCs under more permissive scopes can have
less stability if the tools using them are only run under exceptional
circumstances and not for every day use of the device. Debugging tools may even
require exact version matching as they may require something similar to DWARF
debug information from the FW binary.

Security Response
=================

The kernel remains the gatekeeper for this interface. If violations of the
scopes, security or isolation principles are found, we have options to let
devices fix them with a FW update, push a kernel patch to parse and block RPC
commands or push a kernel patch to block entire firmware versions/devices.

While the kernel can always directly parse and restrict RPCs, it is expected
that the existing kernel pattern of allowing drivers to delegate validation to
FW to be a useful design.

Existing Similar Examples
=========================

The approach described in this document is not a new idea. Direct, or near
direct device access has been offered by the kernel in different areas for
decades. With more devices wanting to follow this design pattern it is becoming
clear that it is not entirely well understood and, more importantly, the
security considerations are not well defined or agreed upon.

Some examples:

 - HW RAID controllers. This includes RPCs to do things like compose drives into
   a RAID volume, configure RAID parameters, monitor the HW and more.

 - Baseboard managers. RPCs for configuring settings in the device and more.

 - NVMe vendor command capsules. nvme-cli provides access to some monitoring
   functions that different products have defined, but more exist.

 - CXL also has a NVMe-like vendor command system.

 - DRM allows user space drivers to send commands to the device via kernel
   mediation.

 - RDMA allows user space drivers to directly push commands to the device
   without kernel involvement.

 - Various “raw” APIs, raw HID (SDL2), raw USB, NVMe Generic Interface, etc.

The first 4 are examples of areas that fwctl intends to cover. The latter three
are examples of disallowed behavior as they fully overlap with the primary purpose
of a kernel subsystem.

Some key lessons learned from these past efforts are the importance of having a
common user space project to use as a pre-condition for obtaining a kernel
driver. Developing good community around useful software in user space is key to
getting companies to fund participation to enable their products.
