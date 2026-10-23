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
running at EL2 from the rest of the host running at EL1/0. pKVM supports
GICv3 only: GICv2 is not supported in protected mode, and neither are
GICv5-based guests.

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

pKVM relies on a number of mechanisms to isolate pVMs from the host:

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

* Memslots cannot be moved or deleted once the pVM has started running
  (``-EPERM``).
* Read-only memslots and dirty logging are not supported (``-EPERM``).
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
  from the ``KVM_RUN`` ioctl().

CPU state isolation
-------------------

Status: CPU register state of protected vCPUs is managed entirely at EL2.

pKVM performs the complete context switch for protected vCPUs within the
hypervisor. The hypervisor sets a protected vCPU's initial state, not the
host, and only what each exit needs is synchronised back.

The user-visible consequences are described under `API behaviour for
protected VMs`_.

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
installed when they are executing in the host. This proxy is distinct
from the PSCI handling provided to protected guests, which is described
under `API behaviour for protected VMs`_.

Protected VM firmware (pvmfw)
-----------------------------

Status: **Unimplemented.**

API behaviour for protected VMs
===============================

Protected vCPU state is owned by EL2 (see `CPU state isolation`_). The VMM
configures a vCPU before its first ``KVM_RUN``; afterwards the state is
private to the guest and the ioctls that access it return ``-EPERM``. The
vCPU ioctls' errors follow one rule: ``-EPERM`` means the host asked for
state that the guest owns, ``-EINVAL`` means the request is not valid for a
protected VM, and a feature pKVM doesn't support for protected VMs is
rejected the way KVM rejects one that isn't implemented (``-ENXIO`` from
``KVM_ARM_VCPU_PVTIME_CTRL``, for example). The ioctls themselves are
described in Documentation/virt/kvm/api.rst.

Boot
----

A protected VM boots from a single primary vCPU. Before the first
``KVM_RUN``, the VMM prepares the boot state:

* Set ``KVM_MP_STATE_RUNNABLE`` on the primary vCPU and
  ``KVM_MP_STATE_STOPPED`` on every other vCPU. EL2 allows only one
  RUNNABLE primary per protected VM. A second RUNNABLE vCPU fails at its
  first ``KVM_RUN``.
* Set the primary vCPU's boot state with ``KVM_SET_ONE_REG``: the kernel
  entry address in ``PC`` and the DTB pointer in ``x0``.

``PC`` and ``x0`` are the only registers EL2 takes from the host. Other
pre-run writes are accepted, but the guest starts from the state the
hypervisor resets it to.

Secondary vCPUs are started by the guest itself through PSCI ``CPU_ON``
(see `Power state`_), which supplies their entry point and context ID.
The VMM cannot choose where they boot.

vCPU state
----------

* ``KVM_GET_ONE_REG`` and ``KVM_SET_ONE_REG`` return ``-EPERM`` once the
  vCPU has run. Before that, they access the host-side copy from which
  EL2 builds the guest's boot state (see `Boot`_).
* ``KVM_ARM_VCPU_INIT`` accepts only the vCPU features that a protected
  guest supports and returns ``-EINVAL`` otherwise.
  ``KVM_ARM_VCPU_PSCI_0_2`` is required, as EL2 implements PSCI 1.1 for
  the guest (see `Power state`_). ``KVM_ARM_VCPU_EL1_32BIT`` is not
  supported: protected guests run in AArch64 only and see no AArch32
  support in ``ID_AA64PFR0_EL1``. Once the vCPU has run,
  ``KVM_ARM_VCPU_INIT`` returns ``-EPERM``, as re-initialising it would
  reset the host-side copy alone.
* ``KVM_SET_VCPU_EVENTS`` returns ``-EPERM`` for external-abort injection
  (``ext_dabt_pending``), and ``KVM_CAP_ARM_INJECT_EXT_DABT`` is not
  advertised to a protected VM. SError injection is unaffected.
* ``KVM_SET_GUEST_DEBUG`` returns ``-EPERM``, and ``KVM_CAP_SET_GUEST_DEBUG``
  is not advertised to a protected VM (see `Debug`_).

Power state
-----------

EL2 implements PSCI 1.1 for a protected guest. The calls that move a
vCPU's power state, ``CPU_ON`` and ``CPU_OFF``, are handled at EL2, and
the host cannot change the outcome: for ``CPU_ON`` the host only
schedules the target, which EL2 has already reset to the entry point
the guest chose, and for ``CPU_OFF`` it only stops scheduling it.
``AFFINITY_INFO`` is handled by the host, which returns ``OFF`` once
it has stopped scheduling every vCPU the query covers, as for a
non-protected VM. A vCPU becomes a valid ``CPU_ON`` target at its first
``KVM_RUN``, and before that EL2 returns ``INVALID_PARAMETERS``. The
platform calls, ``CPU_SUSPEND``, ``SYSTEM_OFF``, ``SYSTEM_RESET`` and
``SYSTEM_RESET2``, are forwarded to the host and behave as for a
non-protected VM, with the ``SYSTEM_*`` calls exiting to the VMM as
``KVM_EXIT_SYSTEM_EVENT``; the ``SYSTEM_RESET2`` reset type and cookie
are in the guest's registers, which ``KVM_GET_ONE_REG`` rejects once
the vCPU has run. Any other function returns ``NOT_SUPPORTED``, and
``PSCI_FEATURES`` reports the same set. Because the host handles those
forwarded calls, ``KVM_SET_ONE_REG`` on ``KVM_REG_ARM_PSCI_VERSION``
returns ``-EINVAL`` for a version below 1.1.

Once a vCPU has run, its power state follows the guest's PSCI calls, not
the VMM's. A vCPU that the guest has powered off with ``CPU_OFF``, or has
not yet brought online with ``CPU_ON``, reads as
``KVM_MP_STATE_UNINITIALIZED``, and only an in-guest ``CPU_ON`` can power
it on: ``KVM_SET_MP_STATE`` returns ``-EPERM`` for ``KVM_MP_STATE_RUNNABLE``
and ``KVM_MP_STATE_SUSPENDED`` on it, accepts ``KVM_MP_STATE_STOPPED`` and
leaves it as it is, and never takes ``KVM_MP_STATE_UNINITIALIZED`` from
the VMM. ``KVM_MP_STATE_STOPPED`` still stops a vCPU EL2 has powered on,
and ``KVM_MP_STATE_RUNNABLE`` resumes it, so the VMM can pause it.

Other interface differences
---------------------------

* ``KVM_CHECK_EXTENSION`` reports only the arm64 capabilities that pKVM
  supports for protected guests, and ``KVM_ENABLE_CAP`` accepts only
  those. Query them on the VM file descriptor: the system file descriptor
  has no VM to filter against. The capabilities the generic KVM code
  answers itself (``KVM_CAP_SYNC_MMU`` or ``KVM_CAP_DEVICE_CTRL``, for
  example) are reported as for any VM, and don't mean the feature works
  for a protected one. The filter does not cover every interface either:
  device-fd configuration (for example the VGIC after
  ``KVM_CREATE_DEVICE``) and vCPU attributes are unfiltered, so a feature
  the filter excludes is rejected by the attribute itself.
  ``KVM_ARM_VCPU_PVTIME_CTRL`` returns ``-ENXIO``, for example, as it does
  where steal time isn't implemented.
* The vGIC of a protected VM remains host-managed: device creation,
  configuration and interrupt injection all work as they do for a
  non-protected VM.
* ``KVM_ARM_SET_COUNTER_OFFSET`` and ``KVM_ARM_GET_REG_WRITABLE_MASKS``
  return ``-EINVAL``: their capabilities are not advertised to a protected
  VM, whose counter offset and ID registers are set by EL2. A protected
  guest sees the physical timebase.
* A protected guest's first access to each page of memory exits to the
  host, since the hypervisor cannot tell memory from a device before the
  page is mapped. For a store, the host sees the value of the register
  the syndrome names, clamped to the access width, and nothing else from
  the register file. For a load, the register the syndrome names takes
  the value the host returns, as for any emulated access.
* A protected guest that uses a feature it was not given, or executes an
  ``SMC``, takes an undefined instruction exception from the hypervisor;
  the host is not involved.
* Apart from PSCI, the hypervisor handles a protected guest's SMCCC
  calls itself and does not involve the host. A function it does not
  implement returns ``NOT_SUPPORTED``, and the host's SMCCC filter does
  not apply: the VM device attribute ioctls (``KVM_ARM_VM_SMCCC_CTRL``)
  return ``-EINVAL`` for a protected VM.
* The hypervisor can decode a trapped guest access only from the CPU's
  instruction syndrome, which is provided only for a load or store of a
  single general-purpose register. An access without one (for example a
  load/store pair or a SIMD/FP access) cannot be decoded. For a
  non-protected VM it can exit to the VMM as ``KVM_EXIT_ARM_NISV``. For a
  protected VM it cannot be emulated by the VMM, so the guest takes a
  synchronous external abort instead.

Debug
-----

Hardware-assisted debugging is not available to protected guests.
``MDSCR_EL1``, ``OSLAR_EL1``, ``OSLSR_EL1``, ``OSDLR_EL1``, and breakpoint
and watchpoint 0 are RAZ/WI; any other debug register access takes an
undefined instruction exception.

Resources
=========

Quentin Perret's KVM Forum 2022 talk entitled "Protected KVM on arm64: A
technical deep dive" remains a good resource for learning more about
pKVM, despite some of the details having changed in the meantime:

https://www.youtube.com/watch?v=9npebeVFbFw
