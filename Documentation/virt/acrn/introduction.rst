.. SPDX-License-Identifier: GPL-2.0

ACRN Hypervisor Introduction
============================

The ACRN Hypervisor is a Type 1 hypervisor, running directly on bare-metal
hardware. It has a privileged management VM, called Service VM, to manage User
VMs and do I/O emulation.

ACRN userspace is an application running in the Service VM that emulates
devices for a User VM based on command line configurations. ACRN Hypervisor
Service Module (HSM) is a kernel module in the Service VM which provides
hypervisor services to the ACRN userspace.

Below figure shows the architecture.

::

                Service VM                    User VM
      +----------------------------+  |  +------------------+
      |        +--------------+    |  |  |                  |
      |        |ACRN userspace|    |  |  |                  |
      |        +--------------+    |  |  |                  |
      |-----------------ioctl------|  |  |                  |   ...
      |kernel space   +----------+ |  |  |                  |
      |               |   HSM    | |  |  | Drivers          |
      |               +----------+ |  |  |                  |
      +--------------------|-------+  |  +------------------+
  +---------------------hypercall----------------------------------------+
  |                         ACRN Hypervisor                              |
  +----------------------------------------------------------------------+
  |                          Hardware                                    |
  +----------------------------------------------------------------------+

ACRN userspace allocates memory for the User VM, configures and initializes the
devices used by the User VM, loads the virtual bootloader, initializes the
virtual CPU state and handles I/O request accesses from the User VM. It uses
ioctls to communicate with the HSM. HSM implements hypervisor services by
interacting with the ACRN Hypervisor via hypercalls. HSM exports a char device
interface (/dev/acrn_hsm) to userspace.

The ACRN hypervisor is open for contribution from anyone. The source repo is
available at https://github.com/projectacrn/acrn-hypervisor.
