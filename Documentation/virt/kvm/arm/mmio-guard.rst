.. SPDX-License-Identifier: GPL-2.0

==============
KVM MMIO guard
==============

KVM implements device emulation by handling translation faults to any
IPA range that is not contained in a memory slot. Such a translation
fault is in most cases passed on to userspace (or in rare cases to the
host kernel) with the address, size and possibly data of the access
for emulation.

Should the guest exit with an address that is not one that corresponds
to an emulatable device, userspace may take measures that are not the
most graceful as far as the guest is concerned (such as terminating it
or delivering a fatal exception).

There is also an element of trust: by forwarding the request to
userspace, the kernel assumes that the guest trusts userspace to do
the right thing.

The KVM MMIO guard offers a way to mitigate this last point: a guest
can request that only certain regions of the IPA space are valid as
MMIO. Only these regions will be handled as an MMIO, and any other
will result in an exception being delivered to the guest.

This relies on a set of hypercalls defined in the KVM-specific range,
using the HVC64 calling convention.

* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_INFO

    ==============    ========    ================================
    Function ID:      (uint32)    0xC6000005
    Arguments:        r1-r3       Reserved / Must be zero
    Return Values:    (int64)     NOT_SUPPORTED(-1) on error, or
                      (uint64)    Protection Granule (PG) size in
                                  bytes (r0)
    ==============    ========    ================================

* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_ENROLL

    ==============    ========    ==============================
    Function ID:      (uint32)    0xC6000006
    Arguments:        none
    Return Values:    (int64)     NOT_SUPPORTED(-1) on error, or
                                  RET_SUCCESS(0) (r0)
    ==============    ========    ==============================

* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_MAP

    ==============    ========    ====================================
    Function ID:      (uint32)    0xC6000007
    Arguments:        (uint64)    The base of the PG-sized IPA range
                                  that is allowed to be accessed as
                                  MMIO. Must be aligned to the PG size
                                  (r1)
                      (uint64)    Index in the MAIR_EL1 register
		                  providing the memory attribute that
				  is used by the guest (r2)
    Return Values:    (int64)     NOT_SUPPORTED(-1) on error, or
                                  RET_SUCCESS(0) (r0)
    ==============    ========    ====================================

* ARM_SMCCC_KVM_FUNC_MMIO_GUARD_UNMAP

    ==============    ========    ======================================
    Function ID:      (uint32)    0xC6000008
    Arguments:        (uint64)    PG-sized IPA range aligned to the PG
                                  size which has been previously mapped.
                                  Must be aligned to the PG size and
                                  have been previously mapped (r1)
    Return Values:    (int64)     NOT_SUPPORTED(-1) on error, or
                                  RET_SUCCESS(0) (r0)
    ==============    ========    ======================================
