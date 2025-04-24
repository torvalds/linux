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

``ARM_SMCCC_KVM_FUNC_HYP_MEMINFO``
----------------------------------

Query the memory protection parameters for a pKVM protected virtual machine.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional; pKVM protected guests only.                       |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000002                                       |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``INVALID_PARAMETER (-3)`` on error, else   |
|                     |          |    | memory protection granule in bytes          |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_MEM_SHARE``
--------------------------------

Share a region of memory with the KVM host, granting it read, write and execute
permissions. The size of the region is equal to the memory protection granule
advertised by ``ARM_SMCCC_KVM_FUNC_HYP_MEMINFO``.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional; pKVM protected guests only.                       |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000003                                       |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Base IPA of memory region to share          |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_MEM_UNSHARE``
----------------------------------

Revoke access permission from the KVM host to a memory region previously shared
with ``ARM_SMCCC_KVM_FUNC_MEM_SHARE``. The size of the region is equal to the
memory protection granule advertised by ``ARM_SMCCC_KVM_FUNC_HYP_MEMINFO``.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional; pKVM protected guests only.                       |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000004                                       |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Base IPA of memory region to unshare        |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_KVM_FUNC_MMIO_GUARD``
----------------------------------

Request that a given memory region is handled as MMIO by the hypervisor,
allowing accesses to this region to be emulated by the KVM host. The size of the
region is equal to the memory protection granule advertised by
``ARM_SMCCC_KVM_FUNC_HYP_MEMINFO``.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional; pKVM protected guests only.                       |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000007                                       |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | Base IPA of MMIO memory region              |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_VENDOR_HYP_KVM_DISCOVER_IMPL_VER_FUNC_ID``
-------------------------------------------------------
Request the target CPU implementation version information and the number of target
implementations for the Guest VM.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional;  KVM/ARM64 Guests only                            |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000040                                       |
+---------------------+----------+--------------------------------------------------+
| Arguments:          | None                                                        |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``NOT_SUPPORTED (-1)``                      |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R1 | Bits [63:32] Reserved/Must be zero          |
|                     |          |    +---------------------------------------------+
|                     |          |    | Bits [31:16] Major version                  |
|                     |          |    +---------------------------------------------+
|                     |          |    | Bits [15:0] Minor version                   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Number of target implementations            |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+

``ARM_SMCCC_VENDOR_HYP_KVM_DISCOVER_IMPL_CPUS_FUNC_ID``
-------------------------------------------------------

Request the target CPU implementation information for the Guest VM. The Guest kernel
will use this information to enable the associated errata.

+---------------------+-------------------------------------------------------------+
| Presence:           | Optional;  KVM/ARM64 Guests only                            |
+---------------------+-------------------------------------------------------------+
| Calling convention: | HVC64                                                       |
+---------------------+----------+--------------------------------------------------+
| Function ID:        | (uint32) | 0xC6000041                                       |
+---------------------+----------+----+---------------------------------------------+
| Arguments:          | (uint64) | R1 | selected implementation index               |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | Reserved / Must be zero                     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | Reserved / Must be zero                     |
+---------------------+----------+----+---------------------------------------------+
| Return Values:      | (int64)  | R0 | ``SUCCESS (0)``                             |
|                     |          |    +---------------------------------------------+
|                     |          |    | ``INVALID_PARAMETER (-3)``                  |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R1 | MIDR_EL1 of the selected implementation     |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R2 | REVIDR_EL1 of the selected implementation   |
|                     +----------+----+---------------------------------------------+
|                     | (uint64) | R3 | AIDR_EL1  of the selected implementation    |
+---------------------+----------+----+---------------------------------------------+
