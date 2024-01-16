.. SPDX-License-Identifier: GPL-2.0

PTP_KVM support for arm/arm64
=============================

PTP_KVM is used for high precision time sync between host and guests.
It relies on transferring the wall clock and counter value from the
host to the guest using a KVM-specific hypercall.

``ARM_SMCCC_KVM_FUNC_PTP``
----------------------------------------

Retrieve current time information for the specific counter. There are no
endianness restrictions.

+---------------------+-------------------------------------------------------+
| Presence:           | Optional                                              |
+---------------------+-------------------------------------------------------+
| Calling convention: | HVC32                                                 |
+---------------------+----------+--------------------------------------------+
| Function ID:        | (uint32) | 0x86000001                                 |
+---------------------+----------+----+---------------------------------------+
| Arguments:          | (uint32) | R1 | ``KVM_PTP_VIRT_COUNTER (0)``          |
|                     |          |    +---------------------------------------+
|                     |          |    | ``KVM_PTP_PHYS_COUNTER (1)``          |
+---------------------+----------+----+---------------------------------------+
| Return Values:      | (int32)  | R0 | ``NOT_SUPPORTED (-1)`` on error, else |
|                     |          |    | upper 32 bits of wall clock time      |
|                     +----------+----+---------------------------------------+
|                     | (uint32) | R1 | Lower 32 bits of wall clock time      |
|                     +----------+----+---------------------------------------+
|                     | (uint32) | R2 | Upper 32 bits of counter              |
|                     +----------+----+---------------------------------------+
|                     | (uint32) | R3 | Lower 32 bits of counter              |
+---------------------+----------+----+---------------------------------------+
