.. SPDX-License-Identifier: GPL-2.0-only

.. _auxiliary_bus:

=============
Auxiliary Bus
=============

In some subsystems, the functionality of the core device (PCI/ACPI/other) is
too complex for a single device to be managed by a monolithic driver
(e.g. Sound Open Firmware), multiple devices might implement a common
intersection of functionality (e.g. NICs + RDMA), or a driver may want to
export an interface for another subsystem to drive (e.g. SIOV Physical Function
export Virtual Function management).  A split of the functionality into child-
devices representing sub-domains of functionality makes it possible to
compartmentalize, layer, and distribute domain-specific concerns via a Linux
device-driver model.

An example for this kind of requirement is the audio subsystem where a single
IP is handling multiple entities such as HDMI, Soundwire, local devices such as
mics/speakers etc. The split for the core's functionality can be arbitrary or
be defined by the DSP firmware topology and include hooks for test/debug. This
allows for the audio core device to be minimal and focused on hardware-specific
control and communication.

Each auxiliary_device represents a part of its parent functionality. The
generic behavior can be extended and specialized as needed by encapsulating an
auxiliary_device within other domain-specific structures and the use of .ops
callbacks. Devices on the auxiliary bus do not share any structures and the use
of a communication channel with the parent is domain-specific.

Note that ops are intended as a way to augment instance behavior within a class
of auxiliary devices, it is not the mechanism for exporting common
infrastructure from the parent. Consider EXPORT_SYMBOL_NS() to convey
infrastructure from the parent module to the auxiliary module(s).


When Should the Auxiliary Bus Be Used
=====================================

The auxiliary bus is to be used when a driver and one or more kernel modules,
who share a common header file with the driver, need a mechanism to connect and
provide access to a shared object allocated by the auxiliary_device's
registering driver.  The registering driver for the auxiliary_device(s) and the
kernel module(s) registering auxiliary_drivers can be from the same subsystem,
or from multiple subsystems.

The emphasis here is on a common generic interface that keeps subsystem
customization out of the bus infrastructure.

One example is a PCI network device that is RDMA-capable and exports a child
device to be driven by an auxiliary_driver in the RDMA subsystem.  The PCI
driver allocates and registers an auxiliary_device for each physical
function on the NIC.  The RDMA driver registers an auxiliary_driver that claims
each of these auxiliary_devices.  This conveys data/ops published by the parent
PCI device/driver to the RDMA auxiliary_driver.

Another use case is for the PCI device to be split out into multiple sub
functions.  For each sub function an auxiliary_device is created.  A PCI sub
function driver binds to such devices that creates its own one or more class
devices.  A PCI sub function auxiliary device is likely to be contained in a
struct with additional attributes such as user defined sub function number and
optional attributes such as resources and a link to the parent device.  These
attributes could be used by systemd/udev; and hence should be initialized
before a driver binds to an auxiliary_device.

A key requirement for utilizing the auxiliary bus is that there is no
dependency on a physical bus, device, register accesses or regmap support.
These individual devices split from the core cannot live on the platform bus as
they are not physical devices that are controlled by DT/ACPI.  The same
argument applies for not using MFD in this scenario as MFD relies on individual
function devices being physical devices.

Auxiliary Device Creation
=========================

An auxiliary_device represents a part of its parent device's functionality. It
is given a name that, combined with the registering drivers KBUILD_MODNAME,
creates a match_name that is used for driver binding, and an id that combined
with the match_name provide a unique name to register with the bus subsystem.
For example, a driver registering an auxiliary device is named 'foo_mod.ko' and
the subdevice is named 'foo_dev'.  The match name is therefore
'foo_mod.foo_dev'.

.. code-block:: c

	struct auxiliary_device {
		struct device dev;
                const char *name;
		u32 id;
	};

Registering an auxiliary_device is a three-step process.

First, a 'struct auxiliary_device' needs to be defined or allocated for each
sub-device desired.  The name, id, dev.release, and dev.parent fields of this
structure must be filled in as follows.

The 'name' field is to be given a name that is recognized by the auxiliary
driver.  If two auxiliary_devices with the same match_name, eg
"foo_mod.foo_dev", are registered onto the bus, they must have unique id
values (e.g. "x" and "y") so that the registered devices names are "foo_mod.foo_dev.x"
and "foo_mod.foo_dev.y".  If match_name + id are not unique, then the device_add fails
and generates an error message.

The auxiliary_device.dev.type.release or auxiliary_device.dev.release must be
populated with a non-NULL pointer to successfully register the
auxiliary_device.  This release call is where resources associated with the
auxiliary device must be free'ed.  Because once the device is placed on the bus
the parent driver can not tell what other code may have a reference to this
data.

The auxiliary_device.dev.parent should be set.  Typically to the registering
drivers device.

Second, call auxiliary_device_init(), which checks several aspects of the
auxiliary_device struct and performs a device_initialize().  After this step
completes, any error state must have a call to auxiliary_device_uninit() in its
resolution path.

The third and final step in registering an auxiliary_device is to perform a
call to auxiliary_device_add(), which sets the name of the device and adds the
device to the bus.

.. code-block:: c

        #define MY_DEVICE_NAME "foo_dev"

        ...

	struct auxiliary_device *my_aux_dev = my_aux_dev_alloc(xxx);

        /* Step 1: */
	my_aux_dev->name = MY_DEVICE_NAME;
	my_aux_dev->id = my_unique_id_alloc(xxx);
	my_aux_dev->dev.release = my_aux_dev_release;
	my_aux_dev->dev.parent = my_dev;

        /* Step 2: */
        if (auxiliary_device_init(my_aux_dev))
                goto fail;

        /* Step 3: */
        if (auxiliary_device_add(my_aux_dev)) {
                auxiliary_device_uninit(my_aux_dev);
                goto fail;
        }

        ...


Unregistering an auxiliary_device is a two-step process to mirror the register
process.  First call auxiliary_device_delete(), then call
auxiliary_device_uninit().


.. code-block:: c

        auxiliary_device_delete(my_dev->my_aux_dev);
        auxiliary_device_uninit(my_dev->my_aux_dev);


Auxiliary Device Memory Model and Lifespan
------------------------------------------

The registering driver is the entity that allocates memory for the
auxiliary_device and registers it on the auxiliary bus.  It is important to note
that, as opposed to the platform bus, the registering driver is wholly
responsible for the management of the memory used for the device object.

To be clear the memory for the auxiliary_device is freed in the release()
callback defined by the registering driver.  The registering driver should only
call auxiliary_device_delete() and then auxiliary_device_uninit() when it is
done with the device.  The release() function is then automatically called if
and when other code releases their reference to the devices.

A parent object, defined in the shared header file, contains the
auxiliary_device.  It also contains a pointer to the shared object(s), which
also is defined in the shared header.  Both the parent object and the shared
object(s) are allocated by the registering driver.  This layout allows the
auxiliary_driver's registering module to perform a container_of() call to go
from the pointer to the auxiliary_device, that is passed during the call to the
auxiliary_driver's probe function, up to the parent object, and then have
access to the shared object(s).

The memory for the shared object(s) must have a lifespan equal to, or greater
than, the lifespan of the memory for the auxiliary_device.  The
auxiliary_driver should only consider that the shared object is valid as long
as the auxiliary_device is still registered on the auxiliary bus.  It is up to
the registering driver to manage (e.g. free or keep available) the memory for
the shared object beyond the life of the auxiliary_device.

The registering driver must unregister all auxiliary devices before its own
driver.remove() is completed.  An easy way to ensure this is to use the
devm_add_action_or_reset() call to register a function against the parent device
which unregisters the auxiliary device object(s).

Finally, any operations which operate on the auxiliary devices must continue to
function (if only to return an error) after the registering driver unregisters
the auxiliary device.


Auxiliary Drivers
=================

Auxiliary drivers follow the standard driver model convention, where
discovery/enumeration is handled by the core, and drivers
provide probe() and remove() methods. They support power management
and shutdown notifications using the standard conventions.

.. code-block:: c

	struct auxiliary_driver {
		int (*probe)(struct auxiliary_device *,
                             const struct auxiliary_device_id *id);
		void (*remove)(struct auxiliary_device *);
		void (*shutdown)(struct auxiliary_device *);
		int (*suspend)(struct auxiliary_device *, pm_message_t);
		int (*resume)(struct auxiliary_device *);
		struct device_driver driver;
		const struct auxiliary_device_id *id_table;
	};

Auxiliary drivers register themselves with the bus by calling
auxiliary_driver_register(). The id_table contains the match_names of auxiliary
devices that a driver can bind with.

.. code-block:: c

        static const struct auxiliary_device_id my_auxiliary_id_table[] = {
		{ .name = "foo_mod.foo_dev" },
                {},
        };

        MODULE_DEVICE_TABLE(auxiliary, my_auxiliary_id_table);

        struct auxiliary_driver my_drv = {
                .name = "myauxiliarydrv",
                .id_table = my_auxiliary_id_table,
                .probe = my_drv_probe,
                .remove = my_drv_remove
        };


Example Usage
=============

Auxiliary devices are created and registered by a subsystem-level core device
that needs to break up its functionality into smaller fragments. One way to
extend the scope of an auxiliary_device is to encapsulate it within a domain-
pecific structure defined by the parent device. This structure contains the
auxiliary_device and any associated shared data/callbacks needed to establish
the connection with the parent.

An example is:

.. code-block:: c

        struct foo {
		struct auxiliary_device auxdev;
		void (*connect)(struct auxiliary_device *auxdev);
		void (*disconnect)(struct auxiliary_device *auxdev);
		void *data;
        };

The parent device then registers the auxiliary_device by calling
auxiliary_device_init(), and then auxiliary_device_add(), with the pointer to
the auxdev member of the above structure. The parent provides a name for the
auxiliary_device that, combined with the parent's KBUILD_MODNAME, creates a
match_name that is be used for matching and binding with a driver.

Whenever an auxiliary_driver is registered, based on the match_name, the
auxiliary_driver's probe() is invoked for the matching devices.  The
auxiliary_driver can also be encapsulated inside custom drivers that make the
core device's functionality extensible by adding additional domain-specific ops
as follows:

.. code-block:: c

	struct my_ops {
		void (*send)(struct auxiliary_device *auxdev);
		void (*receive)(struct auxiliary_device *auxdev);
	};


	struct my_driver {
		struct auxiliary_driver auxiliary_drv;
		const struct my_ops ops;
	};

An example of this type of usage is:

.. code-block:: c

	const struct auxiliary_device_id my_auxiliary_id_table[] = {
		{ .name = "foo_mod.foo_dev" },
		{ },
	};

	const struct my_ops my_custom_ops = {
		.send = my_tx,
		.receive = my_rx,
	};

	const struct my_driver my_drv = {
		.auxiliary_drv = {
			.name = "myauxiliarydrv",
			.id_table = my_auxiliary_id_table,
			.probe = my_probe,
			.remove = my_remove,
			.shutdown = my_shutdown,
		},
		.ops = my_custom_ops,
	};
