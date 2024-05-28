.. SPDX-License-Identifier: GPL-2.0

===============================================
TEE (Trusted Execution Environment) driver API
===============================================

Kernel provides a TEE bus infrastructure where a Trusted Application is
represented as a device identified via Universally Unique Identifier (UUID) and
client drivers register a table of supported device UUIDs.

TEE bus infrastructure registers following APIs:

match():
  iterates over the client driver UUID table to find a corresponding
  match for device UUID. If a match is found, then this particular device is
  probed via corresponding probe API registered by the client driver. This
  process happens whenever a device or a client driver is registered with TEE
  bus.

uevent():
  notifies user-space (udev) whenever a new device is registered on
  TEE bus for auto-loading of modularized client drivers.

TEE bus device enumeration is specific to underlying TEE implementation, so it
is left open for TEE drivers to provide corresponding implementation.

Then TEE client driver can talk to a matched Trusted Application using APIs
listed in include/linux/tee_drv.h.

TEE client driver example
-------------------------

Suppose a TEE client driver needs to communicate with a Trusted Application
having UUID: ``ac6a4085-0e82-4c33-bf98-8eb8e118b6c2``, so driver registration
snippet would look like::

	static const struct tee_client_device_id client_id_table[] = {
		{UUID_INIT(0xac6a4085, 0x0e82, 0x4c33,
			   0xbf, 0x98, 0x8e, 0xb8, 0xe1, 0x18, 0xb6, 0xc2)},
		{}
	};

	MODULE_DEVICE_TABLE(tee, client_id_table);

	static struct tee_client_driver client_driver = {
		.id_table	= client_id_table,
		.driver		= {
			.name		= DRIVER_NAME,
			.bus		= &tee_bus_type,
			.probe		= client_probe,
			.remove		= client_remove,
		},
	};

	static int __init client_init(void)
	{
		return driver_register(&client_driver.driver);
	}

	static void __exit client_exit(void)
	{
		driver_unregister(&client_driver.driver);
	}

	module_init(client_init);
	module_exit(client_exit);
