.. SPDX-License-Identifier: GPL-2.0

Protected virtual machines (pKVM)
=================================

Introduction
------------

Protected KVM (pKVM) is a KVM/arm64 extension which uses the two-stage
translation capability of the Armv8 MMU to isolate guest memory from the host
system. This allows for the creation of a confidential computing environment
without relying on whizz-bang features in hardware, but still allowing room for
complementary technologies such as memory encryption and hardware-backed
attestation.

The major implementation change brought about by pKVM is that the hypervisor
code running at EL2 is now largely independent of (and isolated from) the rest
of the host kernel running at EL1 and therefore additional hypercalls are
introduced to manage manipulation of guest stage-2 page tables, creation of VM
data structures and reclamation of memory on teardown. An immediate consequence
of this change is that the host itself runs with an identity mapping enabled
at stage-2, providing the hypervisor code with a mechanism to restrict host
access to an arbitrary physical page.

Enabling pKVM
-------------

The pKVM hypervisor is enabled by booting the host kernel at EL2 with
"``kvm-arm.mode=protected``" on the command-line. Once enabled, VMs can be spawned
in either protected or non-protected state, although the hypervisor is still
responsible for managing most of the VM metadata in either case.

Limitations
-----------

Enabling pKVM places some significant limitations on KVM guests, regardless of
whether they are spawned in protected state. It is therefore recommended only
to enable pKVM if protected VMs are required, with non-protected state acting
primarily as a debug and development aid.

If you're still keen, then here is an incomplete list of caveats that apply
to all VMs running under pKVM:

- Guest memory cannot be file-backed (with the exception of shmem/memfd) and is
  pinned as it is mapped into the guest. This prevents the host from
  swapping-out, migrating, merging or generally doing anything useful with the
  guest pages. It also requires that the VMM has either ``CAP_IPC_LOCK`` or
  sufficient ``RLIMIT_MEMLOCK`` to account for this pinned memory.

- GICv2 is not supported and therefore GICv3 hardware is required in order
  to expose a virtual GICv3 to the guest.

- Read-only memslots are unsupported and therefore dirty logging cannot be
  enabled.

- Memslot configuration is fixed once a VM has started running, with subsequent
  move or deletion requests being rejected with ``-EPERM``.

- There are probably many others.

Since the host is unable to tear down the hypervisor when pKVM is enabled,
hibernation (``CONFIG_HIBERNATION``) and kexec (``CONFIG_KEXEC``) will fail
with ``-EBUSY``.

If you are not happy with these limitations, then please don't enable pKVM :)

VM creation
-----------

When pKVM is enabled, protected VMs can be created by specifying the
``KVM_VM_TYPE_ARM_PROTECTED`` flag in the machine type identifier parameter
passed to ``KVM_CREATE_VM``.

Protected VMs are instantiated according to a fixed vCPU configuration
described by the ID register definitions in
``arch/arm64/include/asm/kvm_pkvm.h``. Only a subset of the architectural
features that may be available to the host are exposed to the guest and the
capabilities advertised by ``KVM_CHECK_EXTENSION`` are limited accordingly,
with the vCPU registers being initialised to their architecturally-defined
values.

Where not defined by the architecture, the registers of a protected vCPU
are reset to zero with the exception of the PC and X0 which can be set
either by the ``KVM_SET_ONE_REG`` interface or by a call to PSCI ``CPU_ON``.

VM runtime
----------

By default, memory pages mapped into a protected guest are inaccessible to the
host and any attempt by the host to access such a page will result in the
injection of an abort at EL1 by the hypervisor. For accesses originating from
EL0, the host will then terminate the current task with a ``SIGSEGV``.

pKVM exposes additional hypercalls to protected guests, primarily for the
purpose of establishing shared-memory regions with the host for communication
and I/O. These hypercalls are documented in hypercalls.rst.
