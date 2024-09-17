.. SPDX-License-Identifier: GPL-2.0

======================
GenieZone Introduction
======================

Overview
========
GenieZone hypervisor(gzvm) is a type-1 hypervisor that supports various virtual
machine types and provides security features such as TEE-like scenarios and
secure boot. It can create guest VMs for security use cases and has
virtualization capabilities for both platform and interrupt. Although the
hypervisor can be booted independently, it requires the assistance of GenieZone
hypervisor kernel driver(gzvm-ko) to leverage the ability of Linux kernel for
vCPU scheduling, memory management, inter-VM communication and virtio backend
support.

Supported Architecture
======================
GenieZone now only supports MediaTek ARM64 SoC.

Features
========

- vCPU Management

VM manager aims to provide vCPUs on the basis of time sharing on physical CPUs.
It requires Linux kernel in host VM for vCPU scheduling and VM power management.

- Memory Management

Direct use of physical memory from VMs is forbidden and designed to be dictated
to the privilege models managed by GenieZone hypervisor for security reason.
With the help of gzvm-ko, the hypervisor would be able to manipulate memory as
objects.

- Virtual Platform

We manage to emulate a virtual mobile platform for guest OS running on guest
VM. The platform supports various architecture-defined devices, such as
virtual arch timer, GIC, MMIO, PSCI, and exception watching...etc.

- Inter-VM Communication

Communication among guest VMs was provided mainly on RPC. More communication
mechanisms were to be provided in the future based on VirtIO-vsock.

- Device Virtualization

The solution is provided using the well-known VirtIO. The gzvm-ko would
redirect MMIO traps back to VMM where the virtual devices are mostly emulated.
Ioeventfd is implemented using eventfd for signaling host VM that some IO
events in guest VMs need to be processed.

- Interrupt virtualization

All Interrupts during some guest VMs running would be handled by GenieZone
hypervisor with the help of gzvm-ko, both virtual and physical ones. In case
there's no guest VM running out there, physical interrupts would be handled by
host VM directly for performance reason. Irqfd is also implemented using
eventfd for accepting vIRQ requests in gzvm-ko.

Platform architecture component
===============================

- vm

The vm component is responsible for setting up the capability and memory
management for the protected VMs. The capability is mainly about the lifecycle
control and boot context initialization. And the memory management is highly
integrated with ARM 2-stage translation tables to convert VA to IPA to PA under
proper security measures required by protected VMs.

- vcpu

The vcpu component is the core of virtualizing aarch64 physical CPU runnable,
and it controls the vCPU lifecycle including creating, running and destroying.
With self-defined exit handler, the vm component would be able to act
accordingly before terminated.

- vgic

The vgic component exposes control interfaces to Linux kernel via irqchip, and
we intend to support all SPI, PPI, and SGI. When it comes to virtual
interrupts, the GenieZone hypervisor would write to list registers and trigger
vIRQ injection in guest VMs via GIC.
