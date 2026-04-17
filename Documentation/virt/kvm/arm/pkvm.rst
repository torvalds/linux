.. SPDX-License-Identifier: GPL-2.0

====================
Protected KVM (pKVM)
====================

**NOTE**: pKVM is currently an experimental, development feature and
subject to breaking changes as new isolation features are implemented.
Please reach out to the developers at kvmarm@lists.linux.dev if you have
any questions.

Overview
========

Booting a host kernel with '``kvm-arm.mode=protected``' enables
"Protected KVM" (pKVM). During boot, pKVM installs a stage-2 identity
map page-table for the host and uses it to isolate the hypervisor
running at EL2 from the rest of the host running at EL1/0.

pKVM permits creation of protected virtual machines (pVMs) by passing
the ``KVM_VM_TYPE_ARM_PROTECTED`` machine type identifier to the
``KVM_CREATE_VM`` ioctl(). The hypervisor isolates pVMs from the host by
unmapping pages from the stage-2 identity map as they are accessed by a
pVM. Hypercalls are provided for a pVM to share specific regions of its
IPA space back with the host, allowing for communication with the VMM.
A Linux guest must be configured with ``CONFIG_ARM_PKVM_GUEST=y`` in
order to issue these hypercalls.

See hypercalls.rst for more details.

Isolation mechanisms
====================

pKVM relies on a number of mechanisms to isolate PVMs from the host:

CPU memory isolation
--------------------

Status: Isolation of anonymous memory and metadata pages.

Metadata pages (e.g. page-table pages and '``struct kvm_vcpu``' pages)
are donated from the host to the hypervisor during pVM creation and
are consequently unmapped from the stage-2 identity map until the pVM is
destroyed.

Similarly to regular KVM, pages are lazily mapped into the guest in
response to stage-2 page faults handled by the host. However, when
running a pVM, these pages are first pinned and then unmapped from the
stage-2 identity map as part of the donation procedure. This gives rise
to some user-visible differences when compared to non-protected VMs,
largely due to the lack of MMU notifiers:

* Memslots cannot be moved or deleted once the pVM has started running.
* Read-only memslots and dirty logging are not supported.
* With the exception of swap, file-backed pages cannot be mapped into a
  pVM.
* Donated pages are accounted against ``RLIMIT_MLOCK`` and so the VMM
  must have a sufficient resource limit or be granted ``CAP_IPC_LOCK``.
  The lack of a runtime reclaim mechanism means that memory locked for
  a pVM will remain locked until the pVM is destroyed.
* Changes to the VMM address space (e.g. a ``MAP_FIXED`` mmap() over a
  mapping associated with a memslot) are not reflected in the guest and
  may lead to loss of coherency.
* Accessing pVM memory that has not been shared back will result in the
  delivery of a SIGSEGV.
* If a system call accesses pVM memory that has not been shared back
  then it will either return ``-EFAULT`` or forcefully reclaim the
  memory pages. Reclaimed memory is zeroed by the hypervisor and a
  subsequent attempt to access it in the pVM will return ``-EFAULT``
  from the ``VCPU_RUN`` ioctl().

CPU state isolation
-------------------

Status: **Unimplemented.**

DMA isolation using an IOMMU
----------------------------

Status: **Unimplemented.**

Proxying of Trustzone services
------------------------------

Status: FF-A and PSCI calls from the host are proxied by the pKVM
hypervisor.

The FF-A proxy ensures that the host cannot share pVM or hypervisor
memory with Trustzone as part of a "confused deputy" attack.

The PSCI proxy ensures that CPUs always have the stage-2 identity map
installed when they are executing in the host.

Protected VM firmware (pvmfw)
-----------------------------

Status: **Unimplemented.**

Resources
=========

Quentin Perret's KVM Forum 2022 talk entitled "Protected KVM on arm64: A
technical deep dive" remains a good resource for learning more about
pKVM, despite some of the details having changed in the meantime:

https://www.youtube.com/watch?v=9npebeVFbFw
