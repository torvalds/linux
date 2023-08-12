.. SPDX-License-Identifier: GPL-2.0

=======================
Virtual Machine Manager
=======================

The Gunyah Virtual Machine Manager is a Linux driver to support launching
virtual machines using Gunyah.

Configuration of a Gunyah virtual machine is done via a devicetree. When the VM
is launched, memory is provided by the host VM which contains the devictree.
Gunyah reads the devicetree to configure the memory map and create resources
such as vCPUs for the VM. Memory can be shared with the VM with
`GH_VM_SET_USER_MEM_REGION`_. Userspace can interact with the resources in Linux
by adding "functions" to the VM.

Gunyah Functions
================

Components of a Gunyah VM's configuration that need kernel configuration are
called "functions" and are built on top of a framework. Functions are identified
by a string and have some argument(s) to configure them. They are typically
created by the `GH_VM_ADD_FUNCTION`_ ioctl.

Functions typically will always do at least one of these operations:

1. Create resource ticket(s). Resource tickets allow a function to register
   itself as the client for a Gunyah resource (e.g. doorbell or vCPU) and
   the function is given the pointer to the &struct gh_resource when the
   VM is starting.

2. Register IO handler(s). IO handlers allow a function to handle stage-2 faults
   from the virtual machine.

Sample Userspace VMM
====================

A sample userspace VMM is included in samples/gunyah/ along with a minimal
devicetree that can be used to launch a VM. To build this sample, enable
CONFIG_SAMPLE_GUNYAH.

IOCTLs and userspace VMM flows
==============================

The kernel exposes a char device interface at /dev/gunyah.

To create a VM, use the `GH_CREATE_VM`_ ioctl. A successful call will return a
"Gunyah VM" file descriptor.

/dev/gunyah API Descriptions
----------------------------

GH_CREATE_VM
~~~~~~~~~~~~

Creates a Gunyah VM. The argument is reserved for future use and must be 0.
A successful call will return a Gunyah VM file descriptor. See
`Gunyah VM API Descriptions`_ for list of IOCTLs that can be made on this file
file descriptor.

Gunyah VM API Descriptions
--------------------------

GH_VM_SET_USER_MEM_REGION
~~~~~~~~~~~~~~~~~~~~~~~~~

This ioctl allows the user to create or delete a memory parcel for a guest
virtual machine. Each memory region is uniquely identified by a label;
attempting to create two regions with the same label is not allowed. Labels are
unique per virtual machine.

While VMM is guest-agnostic and allows runtime addition of memory regions,
Linux guest virtual machines do not support accepting memory regions at runtime.
Thus, for Linux guests, memory regions should be provided before starting the VM
and the VM must be configured via the devicetree to accept these at boot-up.

The guest physical address is used by Linux kernel to check that the requested
user regions do not overlap and to help find the corresponding memory region
for calls like `GH_VM_SET_DTB_CONFIG`_. It must be page aligned.

To add a memory region, call `GH_VM_SET_USER_MEM_REGION`_ with fields set as
described above.

.. kernel-doc:: include/uapi/linux/gunyah.h
   :identifiers: gh_userspace_memory_region gh_mem_flags

GH_VM_SET_DTB_CONFIG
~~~~~~~~~~~~~~~~~~~~

This ioctl sets the location of the VM's devicetree blob and is used by Gunyah
Resource Manager to allocate resources. The guest physical memory must be part
of the primary memory parcel provided to the VM prior to GH_VM_START.

.. kernel-doc:: include/uapi/linux/gunyah.h
   :identifiers: gh_vm_dtb_config

GH_VM_START
~~~~~~~~~~~

This ioctl starts the VM.

GH_VM_ADD_FUNCTION
~~~~~~~~~~~~~~~~~~

This ioctl registers a Gunyah VM function with the VM manager. The VM function
is described with a &struct gh_fn_desc.type and some arguments for that type.
Typically, the function is added before the VM starts, but the function doesn't
"operate" until the VM starts with `GH_VM_START`_. For example, vCPU ioctls will
all return an error until the VM starts because the vCPUs don't exist until the
VM is started. This allows the VMM to set up all the kernel functions needed for
the VM *before* the VM starts.

.. kernel-doc:: include/uapi/linux/gunyah.h
   :identifiers: gh_fn_desc gh_fn_type

The argument types are documented below:

.. kernel-doc:: include/uapi/linux/gunyah.h
   :identifiers: gh_fn_vcpu_arg gh_fn_irqfd_arg gh_irqfd_flags gh_fn_ioeventfd_arg gh_ioeventfd_flags

Gunyah VCPU API Descriptions
----------------------------

A vCPU file descriptor is created after calling `GH_VM_ADD_FUNCTION` with the type `GH_FN_VCPU`.

GH_VCPU_RUN
~~~~~~~~~~~

This ioctl is used to run a guest virtual cpu.  While there are no
explicit parameters, there is an implicit parameter block that can be
obtained by mmap()ing the vcpu fd at offset 0, with the size given by
`GH_VCPU_MMAP_SIZE`_. The parameter block is formatted as a 'struct
gh_vcpu_run' (see below).

GH_VCPU_MMAP_SIZE
~~~~~~~~~~~~~~~~~

The `GH_VCPU_RUN`_ ioctl communicates with userspace via a shared
memory region. This ioctl returns the size of that region. See the
`GH_VCPU_RUN`_ documentation for details.

.. kernel-doc:: include/uapi/linux/gunyah.h
   :identifiers: gh_vcpu_exit gh_vcpu_run gh_vm_status gh_vm_exit_info
