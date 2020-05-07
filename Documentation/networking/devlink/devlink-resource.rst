.. SPDX-License-Identifier: GPL-2.0

================
Devlink Resource
================

``devlink`` provides the ability for drivers to register resources, which
can allow administrators to see the device restrictions for a given
resource, as well as how much of the given resource is currently
in use. Additionally, these resources can optionally have configurable size.
This could enable the administrator to limit the number of resources that
are used.

For example, the ``netdevsim`` driver enables ``/IPv4/fib`` and
``/IPv4/fib-rules`` as resources to limit the number of IPv4 FIB entries and
rules for a given device.

Resource Ids
============

Each resource is represented by an id, and contains information about its
current size and related sub resources. To access a sub resource, you
specify the path of the resource. For example ``/IPv4/fib`` is the id for
the ``fib`` sub-resource under the ``IPv4`` resource.

example usage
-------------

The resources exposed by the driver can be observed, for example:

.. code:: shell

    $devlink resource show pci/0000:03:00.0
    pci/0000:03:00.0:
      name kvd size 245760 unit entry
        resources:
          name linear size 98304 occ 0 unit entry size_min 0 size_max 147456 size_gran 128
          name hash_double size 60416 unit entry size_min 32768 size_max 180224 size_gran 128
          name hash_single size 87040 unit entry size_min 65536 size_max 212992 size_gran 128

Some resource's size can be changed. Examples:

.. code:: shell

    $devlink resource set pci/0000:03:00.0 path /kvd/hash_single size 73088
    $devlink resource set pci/0000:03:00.0 path /kvd/hash_double size 74368

The changes do not apply immediately, this can be validated by the 'size_new'
attribute, which represents the pending change in size. For example:

.. code:: shell

    $devlink resource show pci/0000:03:00.0
    pci/0000:03:00.0:
      name kvd size 245760 unit entry size_valid false
      resources:
        name linear size 98304 size_new 147456 occ 0 unit entry size_min 0 size_max 147456 size_gran 128
        name hash_double size 60416 unit entry size_min 32768 size_max 180224 size_gran 128
        name hash_single size 87040 unit entry size_min 65536 size_max 212992 size_gran 128

Note that changes in resource size may require a device reload to properly
take effect.
