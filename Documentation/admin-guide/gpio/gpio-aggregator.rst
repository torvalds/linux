.. SPDX-License-Identifier: GPL-2.0-only

GPIO Aggregator
===============

The GPIO Aggregator provides a mechanism to aggregate GPIOs, and expose them as
a new gpio_chip.  This supports the following use cases.


Aggregating GPIOs using Sysfs
-----------------------------

GPIO controllers are exported to userspace using /dev/gpiochip* character
devices.  Access control to these devices is provided by standard UNIX file
system permissions, on an all-or-nothing basis: either a GPIO controller is
accessible for a user, or it is not.

The GPIO Aggregator provides access control for a set of one or more GPIOs, by
aggregating them into a new gpio_chip, which can be assigned to a group or user
using standard UNIX file ownership and permissions.  Furthermore, this
simplifies and hardens exporting GPIOs to a virtual machine, as the VM can just
grab the full GPIO controller, and no longer needs to care about which GPIOs to
grab and which not, reducing the attack surface.

Aggregated GPIO controllers are instantiated and destroyed by writing to
write-only attribute files in sysfs.

    /sys/bus/platform/drivers/gpio-aggregator/

	"new_device" ...
		Userspace may ask the kernel to instantiate an aggregated GPIO
		controller by writing a string describing the GPIOs to
		aggregate to the "new_device" file, using the format

		.. code-block:: none

		    [<gpioA>] [<gpiochipB> <offsets>] ...

		Where:

		    "<gpioA>" ...
			    is a GPIO line name,

		    "<gpiochipB>" ...
			    is a GPIO chip label, and

		    "<offsets>" ...
			    is a comma-separated list of GPIO offsets and/or
			    GPIO offset ranges denoted by dashes.

		Example: Instantiate a new GPIO aggregator by aggregating GPIO
		line 19 of "e6052000.gpio" and GPIO lines 20-21 of
		"e6050000.gpio" into a new gpio_chip:

		.. code-block:: sh

		    $ echo 'e6052000.gpio 19 e6050000.gpio 20-21' > new_device

	"delete_device" ...
		Userspace may ask the kernel to destroy an aggregated GPIO
		controller after use by writing its device name to the
		"delete_device" file.

		Example: Destroy the previously-created aggregated GPIO
		controller, assumed to be "gpio-aggregator.0":

		.. code-block:: sh

		    $ echo gpio-aggregator.0 > delete_device


Aggregating GPIOs using Configfs
--------------------------------

**Group:** ``/config/gpio-aggregator``

    This is the root directory of the gpio-aggregator configfs tree.

**Group:** ``/config/gpio-aggregator/<example-name>``

    This directory represents a GPIO aggregator device. You can assign any
    name to ``<example-name>`` (e.g. ``agg0``), except names starting with
    ``_sysfs`` prefix, which are reserved for auto-generated configfs
    entries corresponding to devices created via Sysfs.

**Attribute:** ``/config/gpio-aggregator/<example-name>/live``

    The ``live`` attribute allows to trigger the actual creation of the device
    once it's fully configured. Accepted values are:

    * ``1``, ``yes``, ``true`` : enable the virtual device
    * ``0``, ``no``, ``false`` : disable the virtual device

**Attribute:** ``/config/gpio-aggregator/<example-name>/dev_name``

    The read-only ``dev_name`` attribute exposes the name of the device as it
    will appear in the system on the platform bus (e.g. ``gpio-aggregator.0``).
    This is useful for identifying a character device for the newly created
    aggregator. If it's ``gpio-aggregator.0``,
    ``/sys/devices/platform/gpio-aggregator.0/gpiochipX`` path tells you that the
    GPIO device id is ``X``.

You must create subdirectories for each virtual line you want to
instantiate, named exactly as ``line0``, ``line1``, ..., ``lineY``, when
you want to instantiate ``Y+1`` (Y >= 0) lines.  Configure all lines before
activating the device by setting ``live`` to 1.

**Group:** ``/config/gpio-aggregator/<example-name>/<lineY>/``

    This directory represents a GPIO line to include in the aggregator.

**Attribute:** ``/config/gpio-aggregator/<example-name>/<lineY>/key``

**Attribute:** ``/config/gpio-aggregator/<example-name>/<lineY>/offset``

    The default values after creating the ``<lineY>`` directory are:

    * ``key`` : <empty>
    * ``offset`` : -1

    ``key`` must always be explicitly configured, while ``offset`` depends.
    Two configuration patterns exist for each ``<lineY>``:

    (a). For lookup by GPIO line name:

         * Set ``key`` to the line name.
         * Ensure ``offset`` remains -1 (the default).

    (b). For lookup by GPIO chip name and the line offset within the chip:

         * Set ``key`` to the chip name.
         * Set ``offset`` to the line offset (0 <= ``offset`` < 65535).

**Attribute:** ``/config/gpio-aggregator/<example-name>/<lineY>/name``

    The ``name`` attribute sets a custom name for lineY. If left unset, the
    line will remain unnamed.

Once the configuration is done, the ``'live'`` attribute must be set to 1
in order to instantiate the aggregator device. It can be set back to 0 to
destroy the virtual device. The module will synchronously wait for the new
aggregator device to be successfully probed and if this doesn't happen, writing
to ``'live'`` will result in an error. This is a different behaviour from the
case when you create it using sysfs ``new_device`` interface.

.. note::

   For aggregators created via Sysfs, the configfs entries are
   auto-generated and appear as ``/config/gpio-aggregator/_sysfs.<N>/``. You
   cannot add or remove line directories with mkdir(2)/rmdir(2). To modify
   lines, you must use the "delete_device" interface to tear down the
   existing device and reconfigure it from scratch. However, you can still
   toggle the aggregator with the ``live`` attribute and adjust the
   ``key``, ``offset``, and ``name`` attributes for each line when ``live``
   is set to 0 by hand (i.e. it's not waiting for deferred probe).

Sample configuration commands
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: sh

    # Create a directory for an aggregator device
    $ mkdir /sys/kernel/config/gpio-aggregator/agg0

    # Configure each line
    $ mkdir /sys/kernel/config/gpio-aggregator/agg0/line0
    $ echo gpiochip0 > /sys/kernel/config/gpio-aggregator/agg0/line0/key
    $ echo 6         > /sys/kernel/config/gpio-aggregator/agg0/line0/offset
    $ echo test0     > /sys/kernel/config/gpio-aggregator/agg0/line0/name
    $ mkdir /sys/kernel/config/gpio-aggregator/agg0/line1
    $ echo gpiochip0 > /sys/kernel/config/gpio-aggregator/agg0/line1/key
    $ echo 7         > /sys/kernel/config/gpio-aggregator/agg0/line1/offset
    $ echo test1     > /sys/kernel/config/gpio-aggregator/agg0/line1/name

    # Activate the aggregator device
    $ echo 1         > /sys/kernel/config/gpio-aggregator/agg0/live


Generic GPIO Driver
-------------------

The GPIO Aggregator can also be used as a generic driver for a simple
GPIO-operated device described in DT, without a dedicated in-kernel driver.
This is useful in industrial control, and is not unlike e.g. spidev, which
allows the user to communicate with an SPI device from userspace.

Binding a device to the GPIO Aggregator is performed either by modifying the
gpio-aggregator driver, or by writing to the "driver_override" file in Sysfs.

Example: If "door" is a GPIO-operated device described in DT, using its own
compatible value::

	door {
		compatible = "myvendor,mydoor";

		gpios = <&gpio2 19 GPIO_ACTIVE_HIGH>,
			<&gpio2 20 GPIO_ACTIVE_LOW>;
		gpio-line-names = "open", "lock";
	};

it can be bound to the GPIO Aggregator by either:

1. Adding its compatible value to ``gpio_aggregator_dt_ids[]``,
2. Binding manually using "driver_override":

.. code-block:: sh

    $ echo gpio-aggregator > /sys/bus/platform/devices/door/driver_override
    $ echo door > /sys/bus/platform/drivers/gpio-aggregator/bind

After that, a new gpiochip "door" has been created:

.. code-block:: sh

    $ gpioinfo door
    gpiochip12 - 2 lines:
	    line   0:       "open"       unused   input  active-high
	    line   1:       "lock"       unused   input  active-high
