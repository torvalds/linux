.. SPDX-License-Identifier: GPL-2.0

=================
Gunyah Hypervisor
=================

.. toctree::
   :maxdepth: 1

   vm-manager
   message-queue

Gunyah is a Type-1 hypervisor which is independent of any OS kernel, and runs in
a higher CPU privilege level. It does not depend on any lower-privileged operating system
for its core functionality. This increases its security and can support a much smaller
trusted computing base than a Type-2 hypervisor.

Gunyah is an open source hypervisor. The source repo is available at
https://github.com/quic/gunyah-hypervisor.

Gunyah provides these following features.

- Scheduling:

  A scheduler for virtual CPUs (vCPUs) on physical CPUs enables time-sharing
  of the CPUs. Gunyah supports two models of scheduling:

    1. "Behind the back" scheduling in which Gunyah hypervisor schedules vCPUS on its own.
    2. "Proxy" scheduling in which a delegated VM can donate part of one of its vCPU slice
       to another VM's vCPU via a hypercall.

- Memory Management:

  APIs handling memory, abstracted as objects, limiting direct use of physical
  addresses. Memory ownership and usage tracking of all memory under its control.
  Memory partitioning between VMs is a fundamental security feature.

- Interrupt Virtualization:

  Uses CPU hardware interrupt virtualization capabilities. Interrupts are handled
  in the hypervisor and routed to the assigned VM.

- Inter-VM Communication:

  There are several different mechanisms provided for communicating between VMs.

- Virtual platform:

  Architectural devices such as interrupt controllers and CPU timers are directly provided
  by the hypervisor as well as core virtual platform devices and system APIs such as ARM PSCI.

- Device Virtualization:

  Para-virtualization of devices is supported using inter-VM communication.

Architectures supported
=======================
AArch64 with a GIC

Resources and Capabilities
==========================

Some services or resources provided by the Gunyah hypervisor are described to a virtual machine by
capability IDs. For instance, inter-VM communication is performed with doorbells and message queues.
Gunyah allows access to manipulate that doorbell via the capability ID. These resources are
described in Linux as a struct gh_resource.

High level management of these resources is performed by the resource manager VM. RM informs a
guest VM about resources it can access through either the device tree or via guest-initiated RPC.

For each virtual machine, Gunyah maintains a table of resources which can be accessed by that VM.
An entry in this table is called a "capability" and VMs can only access resources via this
capability table. Hence, virtual Gunyah resources are referenced by a "capability IDs" and not
"resource IDs". If 2 VMs have access to the same resource, they might not be using the same
capability ID to access that resource since the capability tables are independent per VM.

Resource Manager
================

The resource manager (RM) is a privileged application VM supporting the Gunyah Hypervisor.
It provides policy enforcement aspects of the virtualization system. The resource manager can
be treated as an extension of the Hypervisor but is separated to its own partition to ensure
that the hypervisor layer itself remains small and secure and to maintain a separation of policy
and mechanism in the platform. RM runs at arm64 NS-EL1 similar to other virtual machines.

Communication with the resource manager from each guest VM happens with message-queue.rst. Details
about the specific messages can be found in drivers/virt/gunyah/rsc_mgr.c

::

  +-------+   +--------+   +--------+
  |  RM   |   |  VM_A  |   |  VM_B  |
  +-.-.-.-+   +---.----+   +---.----+
    | |           |            |
  +-.-.-----------.------------.----+
  | | \==========/             |    |
  |  \========================/     |
  |            Gunyah               |
  +---------------------------------+

The source for the resource manager is available at https://github.com/quic/gunyah-resource-manager.

The resource manager provides the following features:

- VM lifecycle management: allocating a VM, starting VMs, destruction of VMs
- VM access control policy, including memory sharing and lending
- Interrupt routing configuration
- Forwarding of system-level events (e.g. VM shutdown) to owner VM

When booting a virtual machine which uses a devicetree such as Linux, resource manager overlays a
/hypervisor node. This node can let Linux know it is running as a Gunyah guest VM,
how to communicate with resource manager, and basic description and capabilities of
this VM. See Documentation/devicetree/bindings/firmware/gunyah-hypervisor.yaml for a description
of this node.
