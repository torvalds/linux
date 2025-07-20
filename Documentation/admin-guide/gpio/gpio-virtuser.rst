.. SPDX-License-Identifier: GPL-2.0-only

Virtual GPIO Consumer
=====================

The virtual GPIO Consumer module allows users to instantiate virtual devices
that request GPIOs and then control their behavior over debugfs. Virtual
consumer devices can be instantiated from device-tree or over configfs.

A virtual consumer uses the driver-facing GPIO APIs and allows to cover it with
automated tests driven by user-space. The GPIOs are requested using
``gpiod_get_array()`` and so we support multiple GPIOs per connector ID.

Creating GPIO consumers
-----------------------

The gpio-consumer module registers a configfs subsystem called
``'gpio-virtuser'``. For details of the configfs filesystem, please refer to
the configfs documentation.

The user can create a hierarchy of configfs groups and items as well as modify
values of exposed attributes. Once the consumer is instantiated, this hierarchy
will be translated to appropriate device properties. The general structure is:

**Group:** ``/config/gpio-virtuser``

This is the top directory of the gpio-consumer configfs tree.

**Group:** ``/config/gpio-consumer/example-name``

**Attribute:** ``/config/gpio-consumer/example-name/live``

**Attribute:** ``/config/gpio-consumer/example-name/dev_name``

This is a directory representing a GPIO consumer device.

The read-only ``dev_name`` attribute exposes the name of the device as it will
appear in the system on the platform bus. This is useful for locating the
associated debugfs directory under
``/sys/kernel/debug/gpio-virtuser/$dev_name``.

The ``'live'`` attribute allows to trigger the actual creation of the device
once it's fully configured. The accepted values are: ``'1'`` to enable the
virtual device and ``'0'`` to disable and tear it down.

Creating GPIO lookup tables
---------------------------

Users can create a number of configfs groups under the device group:

**Group:** ``/config/gpio-consumer/example-name/con_id``

The ``'con_id'`` directory represents a single GPIO lookup and its value maps
to the ``'con_id'`` argument of the ``gpiod_get()`` function. For example:
``con_id`` == ``'reset'`` maps to the ``reset-gpios`` device property.

Users can assign a number of GPIOs to each lookup. Each GPIO is a sub-directory
with a user-defined name under the ``'con_id'`` group.

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/key``

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/offset``

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/drive``

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/pull``

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/active_low``

**Attribute:** ``/config/gpio-consumer/example-name/con_id/0/transitory``

This is a group describing a single GPIO in the ``con_id-gpios`` property.

For virtual consumers created using configfs we use machine lookup tables so
this group can be considered as a mapping between the filesystem and the fields
of a single entry in ``'struct gpiod_lookup'``.

The ``'key'`` attribute represents either the name of the chip this GPIO
belongs to or the GPIO line name. This depends on the value of the ``'offset'``
attribute: if its value is >= 0, then ``'key'`` represents the label of the
chip to lookup while ``'offset'`` represents the offset of the line in that
chip. If ``'offset'`` is < 0, then ``'key'`` represents the name of the line.

The remaining attributes map to the ``'flags'`` field of the GPIO lookup
struct. The first two take string values as arguments:

**``'drive'``:** ``'push-pull'``, ``'open-drain'``, ``'open-source'``
**``'pull'``:** ``'pull-up'``, ``'pull-down'``, ``'pull-disabled'``, ``'as-is'``

``'active_low'`` and ``'transitory'`` are boolean attributes.

Activating GPIO consumers
-------------------------

Once the configuration is complete, the ``'live'`` attribute must be set to 1 in
order to instantiate the consumer. It can be set back to 0 to destroy the
virtual device. The module will synchronously wait for the new simulated device
to be successfully probed and if this doesn't happen, writing to ``'live'`` will
result in an error.

Device-tree
-----------

Virtual GPIO consumers can also be defined in device-tree. The compatible string
must be: ``"gpio-virtuser"`` with at least one property following the
standardized GPIO pattern.

An example device-tree code defining a virtual GPIO consumer:

.. code-block :: none

    gpio-virt-consumer {
        compatible = "gpio-virtuser";

        foo-gpios = <&gpio0 5 GPIO_ACTIVE_LOW>, <&gpio1 2 0>;
        bar-gpios = <&gpio0 6 0>;
    };

Controlling virtual GPIO consumers
----------------------------------

Once active, the device will export debugfs attributes for controlling GPIO
arrays as well as each requested GPIO line separately. Let's consider the
following device property: ``foo-gpios = <&gpio0 0 0>, <&gpio0 4 0>;``.

The following debugfs attribute groups will be created:

**Group:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo/``

This is the group that will contain the attributes for the entire GPIO array.

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo/values``

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo/values_atomic``

Both attributes allow to read and set arrays of GPIO values. User must pass
exactly the number of values that the array contains in the form of a string
containing zeroes and ones representing inactive and active GPIO states
respectively. In this example: ``echo 11 > values``.

The ``values_atomic`` attribute works the same as ``values`` but the kernel
will execute the GPIO driver callbacks in interrupt context.

**Group:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/``

This is a group that represents a single GPIO with ``$index`` being its offset
in the array.

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/consumer``

Allows to set and read the consumer label of the GPIO line.

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/debounce``

Allows to set and read the debounce period of the GPIO line.

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/direction``

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/direction_atomic``

These two attributes allow to set the direction of the GPIO line. They accept
"input" and "output" as values. The atomic variant executes the driver callback
in interrupt context.

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/interrupts``

If the line is requested in input mode, writing ``1`` to this attribute will
make the module listen for edge interrupts on the GPIO. Writing ``0`` disables
the monitoring. Reading this attribute returns the current number of registered
interrupts (both edges).

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/value``

**Attribute:** ``/sys/kernel/debug/gpio-virtuser/$dev_name/gpiod:foo:$index/value_atomic``

Both attributes allow to read and set values of individual requested GPIO lines.
They accept the following values: ``1`` and ``0``.
