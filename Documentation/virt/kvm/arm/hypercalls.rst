.. SPDX-License-Identifier: GPL-2.0

===============================================
KVM/arm64-specific hypercalls exposed to guests
===============================================

This file documents the KVM/arm64-specific hypercalls which may be
exposed by KVM/arm64 to guest operating systems. These hypercalls are
issued using the HVC instruction according to version 1.1 of the Arm SMC
Calling Convention (DEN0028/C):

https://developer.arm.com/docs/den0028/c

All KVM/arm64-specific hypercalls are allocated within the "Vendor
Specific Hypervisor Service Call" range with a UID of
``28b46fb6-2ec5-11e9-a9ca-4b564d003a74``. This UID should be queried by the
guest using the standard "Call UID" function for the service range in
order to determine that the KVM/arm64-specific hypercalls are available.

``ARM_SMCCC_VENDOR_HYP_KVM_FEATURES_FUNC_ID``
---------------------------------------------

Provides a discovery mechanism for other KVM/arm64 hypercalls.

+---------------------+-------------------------------------------------------------+
| Presence:           | Mandatory for the KVM/arm64 UID                             |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC32                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0x86000000                                       |
+---------------------+----------+--------------------------------------------------+
| Arguments:          | None                                                        |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (uint32) | R0 | Bitmap of available function numbers 0-31   |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R1 | Bitmap of available function numbers 32-63  |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R2 | Bitmap of available function numbers 64-95  |
|                     +----------+----+---------------------------------------------+
|                     | (uint32) | R3 | Bitmap of available function numbers 96-127 |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_VENDOR_HYP_KVM_PTP_FUNC_ID``
----------------------------------------

See ptp_kvm.rst
