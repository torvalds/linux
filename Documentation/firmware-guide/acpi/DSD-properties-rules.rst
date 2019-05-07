.. SPDX-License-Identifier: GPL-2.0

==================================
_DSD Device Properties Usage Rules
==================================

Properties, Property Sets and Property Subsets
==============================================

The _DSD (Device Specific Data) configuration object, introduced in ACPI 5.1,
allows any type of device configuration data to be provided via the ACPI
namespace.  In principle, the format of the data may be arbitrary, but it has to
be identified by a UUID which must be recognized by the driver processing the
_DSD output.  However, there are generic UUIDs defined for _DSD recognized by
the ACPI subsystem in the Linux kernel which automatically processes the data
packages associated with them and makes those data available to device drivers
as "device properties".

A device property is a data item consisting of a string key and a value (of a
specific type) associated with it.

In the ACPI _DSD context it is an element of the sub-package following the
generic Device Properties UUID in the _DSD return package as specified in the
Device Properties UUID definition document [1]_.

It also may be regarded as the definition of a key and the associated data type
that can be returned by _DSD in the Device Properties UUID sub-package for a
given device.

A property set is a collection of properties applicable to a hardware entity
like a device.  In the ACPI _DSD context it is the set of all properties that
can be returned in the Device Properties UUID sub-package for the device in
question.

Property subsets are nested collections of properties.  Each of them is
associated with an additional key (name) allowing the subset to be referred
to as a whole (and to be treated as a separate entity).  The canonical
representation of property subsets is via the mechanism specified in the
Hierarchical Properties Extension UUID definition document [2]_.

Property sets may be hierarchical.  That is, a property set may contain
multiple property subsets that each may contain property subsets of its
own and so on.

General Validity Rule for Property Sets
=======================================

Valid property sets must follow the guidance given by the Device Properties UUID
definition document [1].

_DSD properties are intended to be used in addition to, and not instead of, the
existing mechanisms defined by the ACPI specification.  Therefore, as a rule,
they should only be used if the ACPI specification does not make direct
provisions for handling the underlying use case.  It generally is invalid to
return property sets which do not follow that rule from _DSD in data packages
associated with the Device Properties UUID.

Additional Considerations
-------------------------

There are cases in which, even if the general rule given above is followed in
principle, the property set may still not be regarded as a valid one.

For example, that applies to device properties which may cause kernel code
(either a device driver or a library/subsystem) to access hardware in a way
possibly leading to a conflict with AML methods in the ACPI namespace.  In
particular, that may happen if the kernel code uses device properties to
manipulate hardware normally controlled by ACPI methods related to power
management, like _PSx and _DSW (for device objects) or _ON and _OFF (for power
resource objects), or by ACPI device disabling/enabling methods, like _DIS and
_SRS.

In all cases in which kernel code may do something that will confuse AML as a
result of using device properties, the device properties in question are not
suitable for the ACPI environment and consequently they cannot belong to a valid
property set.

Property Sets and Device Tree Bindings
======================================

It often is useful to make _DSD return property sets that follow Device Tree
bindings.

In those cases, however, the above validity considerations must be taken into
account in the first place and returning invalid property sets from _DSD must be
avoided.  For this reason, it may not be possible to make _DSD return a property
set following the given DT binding literally and completely.  Still, for the
sake of code re-use, it may make sense to provide as much of the configuration
data as possible in the form of device properties and complement that with an
ACPI-specific mechanism suitable for the use case at hand.

In any case, property sets following DT bindings literally should not be
expected to automatically work in the ACPI environment regardless of their
contents.

References
==========

.. [1] http://www.uefi.org/sites/default/files/resources/_DSD-device-properties-UUID.pdf
.. [2] http://www.uefi.org/sites/default/files/resources/_DSD-hierarchical-data-extension-UUID-v1.1.pdf
