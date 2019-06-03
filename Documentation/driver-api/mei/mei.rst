.. SPDX-License-Identifier: GPL-2.0

Introduction
============

The Intel Management Engine (Intel ME) is an isolated and protected computing
resource (Co-processor) residing inside certain Intel chipsets. The Intel ME
provides support for computer/IT management features. The feature set
depends on the Intel chipset SKU.

The Intel Management Engine Interface (Intel MEI, previously known as HECI)
is the interface between the Host and Intel ME. This interface is exposed
to the host as a PCI device. The Intel MEI Driver is in charge of the
communication channel between a host application and the Intel ME feature.

Each Intel ME feature (Intel ME Client) is addressed by a GUID/UUID and
each client has its own protocol. The protocol is message-based with a
header and payload up to 512 bytes.

Intel MEI Driver
================

The driver exposes a misc device called /dev/mei.

An application maintains communication with an Intel ME feature while
/dev/mei is open. The binding to a specific feature is performed by calling
MEI_CONNECT_CLIENT_IOCTL, which passes the desired UUID.
The number of instances of an Intel ME feature that can be opened
at the same time depends on the Intel ME feature, but most of the
features allow only a single instance.

The Intel AMT Host Interface (Intel AMTHI) feature supports multiple
simultaneous user connected applications. The Intel MEI driver
handles this internally by maintaining request queues for the applications.

The driver is transparent to data that are passed between firmware feature
and host application.

Because some of the Intel ME features can change the system
configuration, the driver by default allows only a privileged
user to access it.

A code snippet for an application communicating with Intel AMTHI client:

.. code-block:: C

	struct mei_connect_client_data data;
	fd = open(MEI_DEVICE);

	data.d.in_client_uuid = AMTHI_UUID;

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


IOCTLs
======

The Intel MEI Driver supports the following IOCTL commands:
	IOCTL_MEI_CONNECT_CLIENT	Connect to firmware Feature (client).

	usage:
		struct mei_connect_client_data clientData;
		ioctl(fd, IOCTL_MEI_CONNECT_CLIENT, &clientData);

	inputs:
		mei_connect_client_data struct contain the following
		input field:

		in_client_uuid -	UUID of the FW Feature that needs
					to connect to.
	outputs:
		out_client_properties - Client Properties: MTU and Protocol Version.

	error returns:
		EINVAL	Wrong IOCTL Number
		ENODEV	Device or Connection is not initialized or ready. (e.g. Wrong UUID)
		ENOMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EBUSY	Connection Already Open

	Notes:
        max_msg_length (MTU) in client properties describes the maximum
        data that can be sent or received. (e.g. if MTU=2K, can send
        requests up to bytes 2k and received responses up to 2k bytes).

	IOCTL_MEI_NOTIFY_SET: enable or disable event notifications

	Usage:
		uint32_t enable;
		ioctl(fd, IOCTL_MEI_NOTIFY_SET, &enable);

	Inputs:
		uint32_t enable = 1;
		or
		uint32_t enable[disable] = 0;

	Error returns:
		EINVAL	Wrong IOCTL Number
		ENODEV	Device  is not initialized or the client not connected
		ENOMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EOPNOTSUPP if the device doesn't support the feature

	Notes:
	The client must be connected in order to enable notification events


	IOCTL_MEI_NOTIFY_GET : retrieve event

	Usage:
		uint32_t event;
		ioctl(fd, IOCTL_MEI_NOTIFY_GET, &event);

	Outputs:
		1 - if an event is pending
		0 - if there is no even pending

	Error returns:
		EINVAL	Wrong IOCTL Number
		ENODEV	Device is not initialized or the client not connected
		ENOMEM	Unable to allocate memory to client internal data.
		EFAULT	Fatal Error (e.g. Unable to access user input data)
		EOPNOTSUPP if the device doesn't support the feature

	Notes:
	The client must be connected and event notification has to be enabled
	in order to receive an event



Supported Chipsets
==================
82X38/X48 Express and newer

linux-mei@linux.intel.com
