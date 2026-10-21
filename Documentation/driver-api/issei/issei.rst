.. SPDX-License-Identifier: GPL-2.0

Introduction
============

The Intel Silicon Security Engine (Intel SSE) is an isolated and
protected computing resource (Co-processor) residing inside
Intel client chipsets released in 2024 (Lunar Lake) or later.
The Intel SSE provide security support and platform boot orchestration.
The actual feature set depends on the Intel chipset SKU.

The Intel Silicon Security Engine Interface (Intel SSEI)
is the interface between the Host and Intel SSE.
This interface is exposed to the host as one or more PCI devices.
The Intel SSEI Driver is in charge of the communication channel between
a host application and the Intel SSE features.

Each Intel SSE feature, or Intel SSE Client is addressed by a unique UUID and
each client has its own protocol. The protocol is message-based with a
header and payload up to maximal number of bytes advertised by the client,
upon connection.

Intel SSEI Driver
=================

The driver exposes a character device with device nodes /dev/isseiX.

An application maintains communication with an Intel SSE feature while
/dev/isseiX is open. The binding to a specific feature is performed by calling
:c:macro:`IOCTL_ISSEI_CONNECT_CLIENT`, which passes the desired UUID.
The number of instances of an Intel SSE feature that can be opened
at the same time is limited to single instance.

The driver is transparent to data that are passed between firmware feature
and host application.

Because some of the Intel SSE features can change the system
configuration, the driver by default allows only a privileged
user to access it.

The connection termination is performed by calling
:c:macro:`IOCTL_ISSEI_DISCONNECT_CLIENT`.

The session is terminated calling :c:expr:`close(fd)`.

A code snippet for an application communicating with SPDM client:

.. code-block:: C

        struct issei_connect_client_data data = {.in_client_uuid =
                {0xe8, 0x51, 0x49, 0xdf, 0x94, 0x47, 0x4C,
                 0x9A, 0x83, 0x67, 0xC4, 0xE3, 0x34, 0x64, 0xF1, 0xB4}};
        __u8 req_data[] = {0x10, 0x84, 0x00, 0x00}; /* SPDM Get Version */
        size_t req_data_len = sizeof(req_data);
        __u8 res_data[256];
        size_t res_data_len = sizeof(res_data);
        int fd = open("/dev/issei0", O_RDWR);

        ioctl(fd, IOCTL_ISSEI_CONNECT_CLIENT, &data);

        printf("Ver=%d, MaxLen=%u, Flags=0x%08X\n",
               data.out_client_properties.protocol_version,
               data.out_client_properties.max_msg_length,
               data.out_client_properties.flags);

        [...]

        write(fd, req_data, req_data_len);

        [...]

        read(fd, res_data, res_data_len);

        printf("SPDM version count %u, version[0]=%02X%02X\n",
               res_data[5], res_data[6], res_data[7]);

        [...]

        ioctl(fd, IOCTL_ISSEI_DISCONNECT_CLIENT, &data);

        [...]

        close(fd);


User space API ioctl
====================

The Intel SSEI Driver supports the following ioctl commands:

IOCTL_ISSEI_CONNECT_CLIENT
--------------------------
Connect to firmware Feature/Client.

.. code-block:: none

        Usage:

        struct issei_connect_client_data client_data;

        ioctl(fd, IOCTL_ISSEI_CONNECT_CLIENT, &client_data);

        struct issei_connect_client_data - contain the following
        Inputs:
                in_client_uuid        - UUID of the FW Feature that needs to connect to.
        Outputs:
                out_client_properties - Client Properties: MTU, Protocol Version and Flags.

        Error returns:
                ENOTTY  No such client (i.e. wrong UUID) or connection is not allowed.
                EINVAL  Wrong IOCTL Number
                ENODEV  Device or Connection is not initialized or ready.
                ENOMEM  Unable to allocate memory to client internal data.
                EFAULT  Fatal Error (e.g. Unable to access user input data)
                EBUSY   Connection Already Open

:Note:
        max_msg_length (MTU) in client properties describes the maximum
        data that can be sent or received. (e.g. with MTU=2K, can send
        requests up to bytes 2k and received responses up to 2k bytes).

IOCTL_ISSEI_DISCONNECT_CLIENT
-----------------------------
Disconnect from firmware Feature/Client.

.. code-block:: none

        Usage:

        ioctl(fd, IOCTL_ISSEI_DISCONNECT_CLIENT, NULL);

        Error returns:
                EINVAL    Wrong IOCTL Number
                ENODEV    Device or Connection is not initialized or ready.
                ENOTCONN  Feature/Client is not connected.
