.. SPDX-License-Identifier: GPL-2.0

PTP_KVM support for arm/arm64
=============================

PTP_KVM is used for high precision time sync between host and guests.
It relies on transferring the wall clock and counter value from the
host to the guest using a KVM-specific hypercall.

* ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID: 0x86000001

This hypercall uses the SMC32/HVC32 calling convention:

ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID
    ==============    ========    =====================================
    Function ID:      (uint32)    0x86000001
    Arguments:        (uint32)    KVM_PTP_VIRT_COUNTER(0)
                                  KVM_PTP_PHYS_COUNTER(1)
    Return Values:    (int32)     NOT_SUPPORTED(-1) on error, or
                      (uint32)    Upper 32 bits of wall clock time (r0)
                      (uint32)    Lower 32 bits of wall clock time (r1)
                      (uint32)    Upper 32 bits of counter (r2)
                      (uint32)    Lower 32 bits of counter (r3)
    Endianness:                   No Restrictions.
    ==============    ========    =====================================
