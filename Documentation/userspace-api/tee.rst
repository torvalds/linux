.. SPDX-License-Identifier: GPL-2.0
.. tee:

==================================================
TEE (Trusted Execution Environment) Userspace API
==================================================

include/uapi/linux/tee.h defines the generic interface to a TEE.

User space (the client) connects to the driver by opening /dev/tee[0-9]* or
/dev/teepriv[0-9]*.

- TEE_IOC_SHM_ALLOC allocates shared memory and returns a file descriptor
  which user space can mmap. When user space doesn't need the file
  descriptor any more, it should be closed. When shared memory isn't needed
  any longer it should be unmapped with munmap() to allow the reuse of
  memory.

- TEE_IOC_VERSION lets user space know which TEE this driver handles and
  its capabilities.

- TEE_IOC_OPEN_SESSION opens a new session to a Trusted Application.

- TEE_IOC_INVOKE invokes a function in a Trusted Application.

- TEE_IOC_CANCEL may cancel an ongoing TEE_IOC_OPEN_SESSION or TEE_IOC_INVOKE.

- TEE_IOC_CLOSE_SESSION closes a session to a Trusted Application.

There are two classes of clients, normal clients and supplicants. The latter is
a helper process for the TEE to access resources in Linux, for example file
system access. A normal client opens /dev/tee[0-9]* and a supplicant opens
/dev/teepriv[0-9].

Much of the communication between clients and the TEE is opaque to the
driver. The main job for the driver is to receive requests from the
clients, forward them to the TEE and send back the results. In the case of
supplicants the communication goes in the other direction, the TEE sends
requests to the supplicant which then sends back the result.
