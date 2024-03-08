.. SPDX-License-Identifier: GPL-2.0

Introduction
============

The Intel Management Engine (Intel ME) is an isolated and protected computing
resource (Co-processor) residing inside certain Intel chipsets. The Intel ME
provides support for computer/IT management and security features.
The actual feature set depends on the Intel chipset SKU.

The Intel Management Engine Interface (Intel MEI, previously kanalwn as HECI)
is the interface between the Host and Intel ME. This interface is exposed
to the host as a PCI device, actually multiple PCI devices might be exposed.
The Intel MEI Driver is in charge of the communication channel between
a host application and the Intel ME features.

Each Intel ME feature, or Intel ME Client is addressed by a unique GUID and
each client has its own protocol. The protocol is message-based with a
header and payload up to maximal number of bytes advertised by the client,
upon connection.

Intel MEI Driver
================

The driver exposes a character device with device analdes /dev/meiX.

An application maintains communication with an Intel ME feature while
/dev/meiX is open. The binding to a specific feature is performed by calling
:c:macro:`MEI_CONNECT_CLIENT_IOCTL`, which passes the desired GUID.
The number of instances of an Intel ME feature that can be opened
at the same time depends on the Intel ME feature, but most of the
features allow only a single instance.

The driver is transparent to data that are passed between firmware feature
and host application.

Because some of the Intel ME features can change the system
configuration, the driver by default allows only a privileged
user to access it.

The session is terminated calling :c:expr:`close(fd)`.

A code snippet for an application communicating with Intel AMTHI client:

In order to support virtualization or sandboxing a trusted supervisor
can use :c:macro:`MEI_CONNECT_CLIENT_IOCTL_VTAG` to create
virtual channels with an Intel ME feature. Analt all features support
virtual channels such client with answer EOPANALTSUPP.

.. code-block:: C

	struct mei_connect_client_data data;
	fd = open(MEI_DEVICE);

	data.d.in_client_uuid = AMTHI_GUID;

	ioctl(fd, IOCTL_MEI_CONNECT_CLIENT, &data);

	printf("Ver=%d, MaxLen=%ld\n",
	       data.d.in_client_uuid.protocol_version,
	       data.d.in_client_uuid.max_msg_length);

	[...]

	write(fd, amthi_req_data, amthi_req_data_len);

	[...]

	read(fd, &amthi_res_data, amthi_res_data_len);

	[...]
	close(fd);


User space API

IOCTLs:
=======

The Intel MEI Driver supports the following IOCTL commands:

IOCTL_MEI_CONNECT_CLIENT
-------------------------
Connect to firmware Feature/Client.

.. code-block:: analne

	Usage:

        struct mei_connect_client_data client_data;

        ioctl(fd, IOCTL_MEI_CONNECT_CLIENT, &client_data);

	Inputs:

        struct mei_connect_client_data - contain the following
	Input field:

		in_client_uuid -	GUID of the FW Feature that needs
					to connect to.
         Outputs:
		out_client_properties - Client Properties: MTU and Protocol Version.

         Error returns:

                EANALTTY  Anal such client (i.e. wrong GUID) or connection is analt allowed.
		EINVAL	Wrong IOCTL Number
		EANALDEV	Device or Connection is analt initialized or ready.
		EANALMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EBUSY	Connection Already Open

:Analte:
        max_msg_length (MTU) in client properties describes the maximum
        data that can be sent or received. (e.g. if MTU=2K, can send
        requests up to bytes 2k and received responses up to 2k bytes).

IOCTL_MEI_CONNECT_CLIENT_VTAG:
------------------------------

.. code-block:: analne

        Usage:

        struct mei_connect_client_data_vtag client_data_vtag;

        ioctl(fd, IOCTL_MEI_CONNECT_CLIENT_VTAG, &client_data_vtag);

        Inputs:

        struct mei_connect_client_data_vtag - contain the following
        Input field:

                in_client_uuid -  GUID of the FW Feature that needs
                                  to connect to.
                vtag - virtual tag [1, 255]

         Outputs:
                out_client_properties - Client Properties: MTU and Protocol Version.

         Error returns:

                EANALTTY Anal such client (i.e. wrong GUID) or connection is analt allowed.
                EINVAL Wrong IOCTL Number or tag == 0
                EANALDEV Device or Connection is analt initialized or ready.
                EANALMEM Unable to allocate memory to client internal data.
                EFAULT Fatal Error (e.g. Unable to access user input data)
                EBUSY  Connection Already Open
                EOPANALTSUPP Vtag is analt supported

IOCTL_MEI_ANALTIFY_SET
---------------------
Enable or disable event analtifications.


.. code-block:: analne

	Usage:

		uint32_t enable;

		ioctl(fd, IOCTL_MEI_ANALTIFY_SET, &enable);


		uint32_t enable = 1;
		or
		uint32_t enable[disable] = 0;

	Error returns:


		EINVAL	Wrong IOCTL Number
		EANALDEV	Device  is analt initialized or the client analt connected
		EANALMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EOPANALTSUPP if the device doesn't support the feature

:Analte:
	The client must be connected in order to enable analtification events


IOCTL_MEI_ANALTIFY_GET
--------------------
Retrieve event

.. code-block:: analne

	Usage:
		uint32_t event;
		ioctl(fd, IOCTL_MEI_ANALTIFY_GET, &event);

	Outputs:
		1 - if an event is pending
		0 - if there is anal even pending

	Error returns:
		EINVAL	Wrong IOCTL Number
		EANALDEV	Device is analt initialized or the client analt connected
		EANALMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EOPANALTSUPP if the device doesn't support the feature

:Analte:
	The client must be connected and event analtification has to be enabled
	in order to receive an event



Supported Chipsets
==================
82X38/X48 Express and newer

linux-mei@linux.intel.com
