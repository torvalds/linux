.. SPDX-License-Identifier: GPL-2.0

=================================
TS-TEE (Trusted Services project)
=================================

This driver provides access to secure services implemented by Trusted Services.

Trusted Services [1] is a TrustedFirmware.org project that provides a framework
for developing and deploying device Root of Trust services in FF-A [2] S-EL0
Secure Partitions. The project hosts the reference implementation of the Arm
Platform Security Architecture [3] for Arm A-profile devices.

The FF-A Secure Partitions (SP) are accessible through the FF-A driver [4] which
provides the low level communication for this driver. On top of that the Trusted
Services RPC protocol is used [5]. To use the driver from user space a reference
implementation is provided at [6], which is part of the Trusted Services client
library called libts [7].

All Trusted Services (TS) SPs have the same FF-A UUID; it identifies the TS RPC
protocol. A TS SP can host one or more services (e.g. PSA Crypto, PSA ITS, etc).
A service is identified by its service UUID; the same type of service cannot be
present twice in the same SP. During SP boot each service in the SP is assigned
an "interface ID". This is just a short ID to simplify message addressing.

The generic TEE design is to share memory at once with the Trusted OS, which can
then be reused to communicate with multiple applications running on the Trusted
OS. However, in case of FF-A, memory sharing works on an endpoint level, i.e.
memory is shared with a specific SP. User space has to be able to separately
share memory with each SP based on its endpoint ID; therefore a separate TEE
device is registered for each discovered TS SP. Opening the SP corresponds to
opening the TEE device and creating a TEE context. A TS SP hosts one or more
services. Opening a service corresponds to opening a session in the given
tee_context.

Overview of a system with Trusted Services components::

   User space                  Kernel space                   Secure world
   ~~~~~~~~~~                  ~~~~~~~~~~~~                   ~~~~~~~~~~~~
   +--------+                                               +-------------+
   | Client |                                               | Trusted     |
   +--------+                                               | Services SP |
      /\                                                    +-------------+
      ||                                                          /\
      ||                                                          ||
      ||                                                          ||
      \/                                                          \/
   +-------+                +----------+--------+           +-------------+
   | libts |                |  TEE     | TS-TEE |           |  FF-A SPMC  |
   |       |                |  subsys  | driver |           |   + SPMD    |
   +-------+----------------+----+-----+--------+-----------+-------------+
   |      Generic TEE API        |     |  FF-A  |     TS RPC protocol     |
   |      IOCTL (TEE_IOC_*)      |     | driver |        over FF-A        |
   +-----------------------------+     +--------+-------------------------+

References
==========

[1] https://www.trustedfirmware.org/projects/trusted-services/

[2] https://developer.arm.com/documentation/den0077/

[3] https://www.arm.com/architecture/security-features/platform-security

[4] drivers/firmware/arm_ffa/

[5] https://trusted-services.readthedocs.io/en/v1.0.0/developer/service-access-protocols.html#abi

[6] https://git.trustedfirmware.org/TS/trusted-services.git/tree/components/rpc/ts_rpc/caller/linux/ts_rpc_caller_linux.c?h=v1.0.0

[7] https://git.trustedfirmware.org/TS/trusted-services.git/tree/deployments/libts/arm-linux/CMakeLists.txt?h=v1.0.0
